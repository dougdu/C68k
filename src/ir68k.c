// ir68k.c --- c68k OP4 (Tier D): the IR + CFG back-end.
//
// This is the "pivot" the optimizer roadmap (docs/optimization-plan.md OP4,
// docs/codegen.md Tier D) is built around: instead of walking the AST straight
// to text, an eligible function is lowered to a small intermediate
// representation, partitioned into basic blocks with a control-flow graph, and
// then tile-selected to 68000 instructions. It is the substrate the later
// phases need --- local register allocation (OP5) consumes the IR's virtual
// operands and the per-block structure; the global optimizations (OP6) consume
// the CFG edges.
//
// It is an OPT-IN FOUNDATION. It engages only when `opt_use_ir` (the -fir flag
// / C68K_IR env) is set and opt_level >= 2; -O0/-O1 and the default -O2/-O3
// paths never reach it, so the byte-identical baselines are untouched. Any
// function that uses a construct outside the supported integer/pointer/control-
// flow subset makes ir_emit_body() return false --- having emitted nothing ---
// so codegen68k.c's proven single-pass generator handles it. That whole-
// function fallback keeps correctness guaranteed while the IR grows.
//
// Register model: none yet. Like the single-pass generator, the selector uses
// D0 as the accumulator and spills temporaries to the SP stack (via the shared
// cg_push/cg_pop). Real register allocation is OP5; this phase deliberately
// reproduces the -O2 instruction *selection* (the OP2 addressing-mode / memory-
// operand / strength-reduction tiles and the OP3 condition-context branching)
// over the IR, emitting the same primitive sequences as the single-pass path so
// the shared peephole normalizes them identically.

#include "chibicc.h"

// Emit one formatted line through the shared codegen sink (buffered; the
// peephole pass and both encoders apply to it exactly as to single-pass output).
#define EMIT(...) cg_emit(format(__VA_ARGS__))

// ---------------------------------------------------------------------------
// IR expression tree (AST -> IR).  A thin, explicitly-lowered form: loads and
// addresses are first-class (IE_LOAD / IE_LEA) so the tiler can match memory
// operands and indexed addressing directly, and every node carries its result
// size + operand signedness so no Type* is needed during selection.
// ---------------------------------------------------------------------------

typedef enum {
  IE_NOP,     // no value / no code (ND_NULL_EXPR)
  IE_NUM,     // integer constant  -> D0
  IE_LEA,     // address of a frame slot (is_local) or a global (name) -> D0
  IE_LOAD,    // load scalar from address `a`             -> D0
  IE_STORE,   // store value `b` to address `a`; result (value) -> D0
  IE_BINOP,   // a <op> b (op = NodeKind ND_ADD..ND_LE)   -> D0
  IE_UNARY,   // <op> a   (op = ND_NEG/ND_BITNOT/ND_NOT)  -> D0
  IE_LOGAND,  // a && b  (materialized 0/1)               -> D0
  IE_LOGOR,   // a || b  (materialized 0/1)               -> D0
  IE_COND,    // a ? b : c                                -> D0
  IE_COMMA,   // a , b                                    -> D0 (= b)
  IE_CAST,    // integer cast of a (op = target TypeKind) -> D0
  IE_CALL,    // call (name direct, else a = fn-pointer)  -> D0
  IE_MEMZERO, // zero-clear the frame local at `off`, `size` bytes
} IeKind;

typedef struct IrExpr IrExpr;
struct IrExpr {
  IeKind kind;
  int op;             // NodeKind (BINOP/UNARY) or target TypeKind (CAST)
  int64_t val;        // NUM
  int off;            // LEA local offset / MEMZERO offset
  int size;           // result size in bytes / MEMZERO clear size
  bool uns;           // operand/result unsignedness
  bool is_local;      // LEA: frame slot vs global
  bool ptradd;        // BINOP ND_ADD produced by pointer arithmetic
  char *name;         // LEA global name / CALL direct callee
  Obj *var;           // LEA / MEMZERO source variable (for register promotion)
  int fsize;          // CAST from-size
  bool funs;          // CAST from-unsigned
  IrExpr *a, *b, *c;  // kids
  IrExpr **args;      // CALL argument exprs (source order)
  int nargs;
};

static IrExpr *ie_new(IeKind k) {
  IrExpr *e = calloc(1, sizeof(IrExpr));
  e->kind = k;
  return e;
}

// ---------------------------------------------------------------------------
// Linear IR items + basic blocks + CFG.  The builder emits a flat item list;
// ir_build_cfg() partitions it into basic blocks and wires successor /
// predecessor edges.  Emission then walks the blocks in layout order (the CFG's
// first consumer); OP5/OP6 will consume the same block/edge structure.
// ---------------------------------------------------------------------------

typedef enum { II_LABEL, II_JMP, II_CBR, II_EVAL, II_RET, II_NOP } IiKind;

typedef struct {
  IiKind kind;
  IrExpr *e;    // EVAL / CBR condition / RET value (NULL for void return)
  char *label;  // LABEL definition, JMP target, or CBR target
  bool when;    // CBR: branch when the condition's truth == when
} IrItem;

typedef struct {
  int start, end;   // half-open item range [start,end)
  int succ[2];      // successor block ids
  int nsucc;
  int *pred;        // predecessor block ids (slice of a per-function pool)
  int npred;
} IrBlock;

static Obj *ir_fn;

static IrItem *items;
static int nitems, capitems;

static IrBlock *blocks;
static int nblocks, capblocks;

static void add_item(IiKind k, IrExpr *e, char *label, bool when) {
  if (nitems == capitems) {
    capitems = capitems ? capitems * 2 : 128;
    items = realloc(items, capitems * sizeof(IrItem));
  }
  items[nitems].kind = k;
  items[nitems].e = e;
  items[nitems].label = label;
  items[nitems].when = when;
  nitems++;
}

// ---------------------------------------------------------------------------
// Small numeric helpers (mirroring codegen68k.c so the emitted text matches).
// ---------------------------------------------------------------------------

static bool is_pow2_32(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

static int ctz32(uint32_t v) {
  int n = 0;
  while (!(v & 1u)) { v >>= 1; n++; }
  return n;
}

static void add_imm(int32_t c) {
  if (c == 0)
    return;
  if (c >= 1 && c <= 8)
    EMIT("  addq.l #%ld,d0", (long)c);
  else if (c >= -8 && c <= -1)
    EMIT("  subq.l #%ld,d0", (long)(-c));
  else
    EMIT("  add.l #%ld,d0", (long)c);
}

static void shift_by(char *op, int count, char *reg, char *scratch) {
  if (count <= 0)
    return;
  if (count <= 8) {
    EMIT("  %s #%d,%s", op, count, reg);
  } else {
    EMIT("  moveq #%d,%s", count, scratch);
    EMIT("  %s %s,%s", op, scratch, reg);
  }
}

// Condition relation (mirrors codegen68k.c's Rel for condition-context lowering).
typedef enum { R_LT, R_LE, R_EQ, R_NE, R_GT, R_GE } Rel;

static Rel rel_of(int op) {
  return op == ND_LT ? R_LT : op == ND_LE ? R_LE : op == ND_EQ ? R_EQ : R_NE;
}
static Rel rel_negate(Rel r) {
  switch (r) {
  case R_LT: return R_GE;
  case R_GE: return R_LT;
  case R_LE: return R_GT;
  case R_GT: return R_LE;
  case R_EQ: return R_NE;
  default:   return R_EQ;
  }
}
static Rel rel_swap(Rel r) {
  switch (r) {
  case R_LT: return R_GT;
  case R_GT: return R_LT;
  case R_LE: return R_GE;
  case R_GE: return R_LE;
  default:   return r;
  }
}
static char *bcc_mnem(Rel r, bool u) {
  switch (r) {
  case R_EQ: return "beq";
  case R_NE: return "bne";
  case R_LT: return u ? "bcs" : "blt";
  case R_LE: return u ? "bls" : "ble";
  case R_GT: return u ? "bhi" : "bgt";
  default:   return u ? "bcc" : "bge";
  }
}

static bool is_commutative(int op) {
  return op == ND_ADD || op == ND_MUL || op == ND_BITAND || op == ND_BITOR ||
         op == ND_BITXOR || op == ND_EQ || op == ND_NE;
}

// True for the integer TypeKinds (matches type.c is_integer): the CAST-unwrap
// tiles fold only integer casts, exactly like the single-pass const_int32 /
// simple_lval_ea, so a pointer cast (e.g. `(void*)0`) is not mis-folded.
static bool tk_is_integer(int k) {
  return k == TY_BOOL || (k >= TY_CHAR && k <= TY_LONG) || k == TY_ENUM;
}

// ---------------------------------------------------------------------------
// Eligibility: does this function stay entirely within the IR's subset?  A
// single "no" makes the whole function fall back to the single-pass generator,
// so correctness never depends on the IR covering a construct.
// ---------------------------------------------------------------------------

static bool ty_ok(Type *ty) {
  if (!ty)
    return true;
  switch (ty->kind) {
  case TY_FLOAT: case TY_DOUBLE: case TY_LDOUBLE:  // soft-float path (v1: none)
  case TY_STRUCT: case TY_UNION:                   // aggregate values (v1: none)
  case TY_VLA:                                      // VLA SP discipline (v1)
    return false;
  }
  if (is_integer(ty) && ty->size == 8)             // long long helper calls (v1)
    return false;
  return true;
}

static bool expr_ok(Node *node) {
  if (!node)
    return true;
  if (!ty_ok(node->ty))
    return false;

  switch (node->kind) {
  case ND_NULL_EXPR:
  case ND_NUM:
  case ND_VAR:
  case ND_MEMZERO:
    return true;
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
  case ND_COMMA: case ND_LOGAND: case ND_LOGOR:
    return expr_ok(node->lhs) && expr_ok(node->rhs);
  case ND_NEG: case ND_BITNOT: case ND_NOT:
  case ND_ADDR: case ND_DEREF: case ND_CAST:
    return expr_ok(node->lhs);
  case ND_ASSIGN:
    if (node->lhs->kind == ND_VLA_PTR)
      return false;
    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield)
      return false;
    return expr_ok(node->lhs) && expr_ok(node->rhs);
  case ND_COND:
    return expr_ok(node->cond) && expr_ok(node->then) && expr_ok(node->els);
  case ND_FUNCALL:
    if (node->ret_buffer)                          // struct/union return
      return false;
    if (node->lhs->kind == ND_VAR && node->lhs->var &&
        (!strcmp(node->lhs->var->name, "alloca") ||
         !strcmp(node->lhs->var->name, "setjmp")))
      return false;
    for (Node *a = node->args; a; a = a->next) {
      // Only <=4-byte scalars and decayed array/function pointers (one 4-byte
      // slot each); ty_ok already rejects 8-byte scalars, structs and floats.
      if (!ty_ok(a->ty))
        return false;
      if (a->ty->kind == TY_STRUCT || a->ty->kind == TY_UNION)
        return false;
      if (!expr_ok(a))
        return false;
    }
    if (!(node->lhs->kind == ND_VAR && node->lhs->ty->kind == TY_FUNC) &&
        !expr_ok(node->lhs))
      return false;
    return true;
  }
  // ND_MEMBER, ND_STMT_EXPR, ND_CAS, ND_EXCH, ND_ASM, ND_VLA_PTR,
  // ND_GOTO_EXPR, ND_LABEL_VAL, ... -> not yet handled: fall back.
  return false;
}

static bool stmt_ok(Node *node) {
  if (!node)
    return true;
  switch (node->kind) {
  case ND_BLOCK:
    if (node->vla_mark)                            // VLA reclamation (v1)
      return false;
    for (Node *n = node->body; n; n = n->next)
      if (!stmt_ok(n))
        return false;
    return true;
  case ND_EXPR_STMT:
    return expr_ok(node->lhs);
  case ND_NULL_EXPR:
    return true;
  case ND_RETURN:
    if (node->lhs) {
      if (node->lhs->ty->kind == TY_STRUCT || node->lhs->ty->kind == TY_UNION)
        return false;
      return expr_ok(node->lhs);
    }
    return true;
  case ND_IF:
    return expr_ok(node->cond) && stmt_ok(node->then) &&
           (!node->els || stmt_ok(node->els));
  case ND_FOR:
    if (node->then->kind == ND_BLOCK && node->then->vla_mark)
      return false;
    return (!node->init || stmt_ok(node->init)) &&
           (!node->cond || expr_ok(node->cond)) &&
           (!node->inc || expr_ok(node->inc)) && stmt_ok(node->then);
  case ND_DO:
    if (node->then->kind == ND_BLOCK && node->then->vla_mark)
      return false;
    return expr_ok(node->cond) && stmt_ok(node->then);
  case ND_GOTO:
    return true;
  case ND_LABEL:
    return stmt_ok(node->lhs);
  }
  // ND_SWITCH / ND_CASE / ND_ASM / ND_GOTO_EXPR / ... -> fall back.
  return false;
}

// ---------------------------------------------------------------------------
// Builder: AST -> IR.
// ---------------------------------------------------------------------------

// OP5 compound-assign fold: chibicc lowers `x op= B` / `x++` to
// `tmp = &x, *tmp = *tmp op B` with `tmp` a fresh empty-name pointer. When x is
// a simple promotable local, folding the idiom (substitute `*tmp` -> x, drop the
// `tmp = &x` init) frees x from the forced `&x` so it can live in a register.
// ir_plan_regs (below) populates this map; the builder consumes it.
static Obj *fa_tmp[256]; // the synthesized `&x` pointer temp
static Obj *fa_v[256];   // the target local x it aliases
static Node *fa_init[256]; // the ADDR(&x) node (ignored by the address-taken scan)
static int fa_n;

// If `n` is a foldable compound-assign temp (`VAR tmp`), the local x it aliases.
static Obj *foldable_alias(Node *n) {
  if (!n || n->kind != ND_VAR)
    return NULL;
  for (int i = 0; i < fa_n; i++)
    if (fa_tmp[i] == n->var)
      return fa_v[i];
  return NULL;
}

static bool fa_is_init(Node *addr) {
  for (int i = 0; i < fa_n; i++)
    if (fa_init[i] == addr)
      return true;
  return false;
}

static IrExpr *build_expr(Node *node);

// Address of a variable -> IE_LEA (frame slot or global), tagged with the var
// so register promotion can substitute its register.
static IrExpr *build_lea(Obj *v) {
  IrExpr *e = ie_new(IE_LEA);
  e->var = v;
  if (v->is_local) {
    e->is_local = true;
    e->off = v->offset;
  } else {
    e->is_local = false;
    e->name = v->name;
  }
  return e;
}

// Address-producing lowering (mirrors gen_addr for the supported lvalues).
static IrExpr *build_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    return build_lea(node->var);
  case ND_DEREF: {
    Obj *v = foldable_alias(node->lhs); // *tmp (tmp = &x) -> &x
    return v ? build_lea(v) : build_expr(node->lhs);
  }
  case ND_COMMA: {
    IrExpr *e = ie_new(IE_COMMA);
    e->a = build_expr(node->lhs);
    e->b = build_addr(node->rhs);
    e->size = 4;
    return e;
  }
  }
  return ie_new(IE_NOP); // unreachable: eligibility restricts lvalue shapes
}

static IrExpr *build_expr(Node *node) {
  switch (node->kind) {
  case ND_NULL_EXPR:
    return ie_new(IE_NOP);
  case ND_NUM: {
    IrExpr *e = ie_new(IE_NUM);
    e->val = node->val;
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_VAR:
    if (node->ty->kind == TY_ARRAY || node->ty->kind == TY_FUNC)
      return build_addr(node);
    {
      IrExpr *e = ie_new(IE_LOAD);
      e->a = build_addr(node);
      e->size = node->ty->size;
      e->uns = node->ty->is_unsigned;
      return e;
    }
  case ND_DEREF:
    if (node->ty->kind == TY_ARRAY || node->ty->kind == TY_FUNC ||
        node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
      return build_expr(node->lhs); // aggregate rvalue == its address
    {
      Obj *v = foldable_alias(node->lhs); // *tmp (tmp = &x) -> x
      IrExpr *e = ie_new(IE_LOAD);
      e->a = v ? build_lea(v) : build_expr(node->lhs);
      e->size = node->ty->size;
      e->uns = node->ty->is_unsigned;
      return e;
    }
  case ND_ADDR:
    return build_addr(node->lhs);
  case ND_ASSIGN: {
    // The folded compound-assign init `tmp = &x` is dead (we substitute `*tmp`
    // with x directly), so drop it.
    if (node->lhs->kind == ND_VAR && foldable_alias(node->lhs))
      return ie_new(IE_NOP);
    IrExpr *e = ie_new(IE_STORE);
    e->a = build_addr(node->lhs);
    e->b = build_expr(node->rhs);
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_COMMA: {
    IrExpr *e = ie_new(IE_COMMA);
    e->a = build_expr(node->lhs);
    e->b = build_expr(node->rhs);
    // A comma's value (and type) is its rhs. node->ty may be NULL --- e.g. the
    // compute_vla_size no-op `COMMA(NULL_EXPR, NULL_EXPR)` chibicc emits for any
    // pointer/array local --- so never dereference it here.
    e->size = e->b->size;
    e->uns = e->b->uns;
    return e;
  }
  case ND_CAST: {
    IrExpr *e = ie_new(IE_CAST);
    e->a = build_expr(node->lhs);
    e->op = node->ty->kind;
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    e->fsize = node->lhs->ty->size;
    e->funs = node->lhs->ty->is_unsigned;
    return e;
  }
  case ND_COND: {
    IrExpr *e = ie_new(IE_COND);
    e->a = build_expr(node->cond);
    e->b = build_expr(node->then);
    e->c = build_expr(node->els);
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_NOT: {
    IrExpr *e = ie_new(IE_UNARY);
    e->op = ND_NOT;
    e->a = build_expr(node->lhs);
    e->size = 4;
    return e;
  }
  case ND_BITNOT: {
    IrExpr *e = ie_new(IE_UNARY);
    e->op = ND_BITNOT;
    e->a = build_expr(node->lhs);
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_NEG: {
    IrExpr *e = ie_new(IE_UNARY);
    e->op = ND_NEG;
    e->a = build_expr(node->lhs);
    e->size = node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_LOGAND:
  case ND_LOGOR: {
    IrExpr *e = ie_new(node->kind == ND_LOGAND ? IE_LOGAND : IE_LOGOR);
    e->a = build_expr(node->lhs);
    e->b = build_expr(node->rhs);
    e->size = 4;
    return e;
  }
  case ND_MEMZERO: {
    IrExpr *e = ie_new(IE_MEMZERO);
    e->var = node->var;
    e->off = node->var->offset;
    e->size = node->var->ty->size;
    return e;
  }
  case ND_FUNCALL: {
    IrExpr *e = ie_new(IE_CALL);
    if (node->lhs->kind == ND_VAR && node->lhs->ty->kind == TY_FUNC)
      e->name = node->lhs->var->name;
    else
      e->a = build_expr(node->lhs); // indirect: fn-pointer value
    int n = 0;
    for (Node *arg = node->args; arg; arg = arg->next)
      n++;
    e->args = calloc(n ? n : 1, sizeof(IrExpr *));
    e->nargs = n;
    int i = 0;
    for (Node *arg = node->args; arg; arg = arg->next)
      e->args[i++] = build_expr(arg);
    e->size = node->ty->kind == TY_VOID ? 0 : node->ty->size;
    e->uns = node->ty->is_unsigned;
    return e;
  }
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE: {
    IrExpr *e = ie_new(IE_BINOP);
    e->op = node->kind;
    e->a = build_expr(node->lhs);
    e->b = build_expr(node->rhs);
    e->size = node->ty->size;
    e->uns = node->lhs->ty->is_unsigned; // operand signedness (cmp/shift/div)
    e->ptradd = (node->kind == ND_ADD && node->ty->base != NULL);
    return e;
  }
  }
  return ie_new(IE_NOP); // unreachable: gated by expr_ok
}

// Lower a statement into linear IR items (mirrors gen_stmt's structure so the
// emitted layout + labels match the single-pass generator, which the shared
// peephole then normalizes identically). opt_level>=2 here, so conditions
// always take the condition-context (cmp+Bcc) form (OP3).
static void lower_stmt(Node *node) {
  switch (node->kind) {
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      lower_stmt(n);
    return;
  case ND_NULL_EXPR:
    return;
  case ND_EXPR_STMT:
    add_item(II_EVAL, build_expr(node->lhs), NULL, false);
    return;
  case ND_RETURN:
    add_item(II_RET, node->lhs ? build_expr(node->lhs) : NULL, NULL, false);
    return;
  case ND_IF: {
    int c = cg_uid();
    char *lelse = format("L_else_%d", c);
    char *lend = format("L_end_%d", c);
    add_item(II_CBR, build_expr(node->cond), lelse, false);
    lower_stmt(node->then);
    add_item(II_JMP, NULL, lend, false);
    add_item(II_LABEL, NULL, lelse, false);
    if (node->els)
      lower_stmt(node->els);
    add_item(II_LABEL, NULL, lend, false);
    return;
  }
  case ND_FOR: {
    int c = cg_uid();
    char *lbegin = format("L_begin_%d", c);
    if (node->init)
      lower_stmt(node->init);
    add_item(II_LABEL, NULL, lbegin, false);
    if (node->cond)
      add_item(II_CBR, build_expr(node->cond), node->brk_label, false);
    lower_stmt(node->then);
    add_item(II_LABEL, NULL, node->cont_label, false);
    if (node->inc)
      add_item(II_EVAL, build_expr(node->inc), NULL, false);
    add_item(II_JMP, NULL, lbegin, false);
    add_item(II_LABEL, NULL, node->brk_label, false);
    return;
  }
  case ND_DO: {
    int c = cg_uid();
    char *lbegin = format("L_begin_%d", c);
    add_item(II_LABEL, NULL, lbegin, false);
    lower_stmt(node->then);
    add_item(II_LABEL, NULL, node->cont_label, false);
    add_item(II_CBR, build_expr(node->cond), lbegin, true);
    add_item(II_LABEL, NULL, node->brk_label, false);
    return;
  }
  case ND_GOTO:
    add_item(II_JMP, NULL, node->unique_label, false);
    return;
  case ND_LABEL:
    add_item(II_LABEL, NULL, node->unique_label, false);
    lower_stmt(node->lhs);
    return;
  }
}

// ---------------------------------------------------------------------------
// CFG construction: leaders -> basic blocks -> successor / predecessor edges.
// ---------------------------------------------------------------------------

static int block_of_label(char *name) {
  for (int b = 0; b < nblocks; b++)
    for (int i = blocks[b].start; i < blocks[b].end; i++)
      if (items[i].kind == II_LABEL && !strcmp(items[i].label, name))
        return b;
  return -1;
}

static void ir_build_cfg(void) {
  nblocks = 0;
  if (nitems == 0)
    return;

  // Leaders: the first item, every label, and the item after any transfer.
  bool *lead = calloc(nitems, 1);
  lead[0] = true;
  for (int i = 0; i < nitems; i++) {
    if (items[i].kind == II_LABEL)
      lead[i] = true;
    if ((items[i].kind == II_JMP || items[i].kind == II_CBR ||
         items[i].kind == II_RET) &&
        i + 1 < nitems)
      lead[i + 1] = true;
  }

  if (nitems > capblocks) {
    capblocks = nitems;
    blocks = realloc(blocks, capblocks * sizeof(IrBlock));
  }
  for (int i = 0; i < nitems; i++) {
    if (!lead[i])
      continue;
    int j = i + 1;
    while (j < nitems && !lead[j])
      j++;
    blocks[nblocks].start = i;
    blocks[nblocks].end = j;
    blocks[nblocks].nsucc = 0;
    blocks[nblocks].npred = 0;
    blocks[nblocks].pred = NULL;
    nblocks++;
  }
  free(lead);

  // Successors from each block's terminator (the last item).
  for (int b = 0; b < nblocks; b++) {
    IrItem *last = &items[blocks[b].end - 1];
    switch (last->kind) {
    case II_JMP: {
      int t = block_of_label(last->label);
      if (t >= 0)
        blocks[b].succ[blocks[b].nsucc++] = t;
      break;
    }
    case II_RET:
      break; // returns leave the function: no CFG successor
    case II_CBR: {
      int t = block_of_label(last->label);
      if (t >= 0)
        blocks[b].succ[blocks[b].nsucc++] = t; // branch taken
      if (b + 1 < nblocks)
        blocks[b].succ[blocks[b].nsucc++] = b + 1; // fall-through
      break;
    }
    default: // LABEL / EVAL terminator -> fall through to the next block
      if (b + 1 < nblocks)
        blocks[b].succ[blocks[b].nsucc++] = b + 1;
      break;
    }
  }

  // Predecessors: invert the successor edges into one flat pool.
  int edges = 0;
  for (int b = 0; b < nblocks; b++)
    edges += blocks[b].nsucc;
  int *pool = calloc(edges ? edges : 1, sizeof(int));
  for (int b = 0; b < nblocks; b++)
    for (int s = 0; s < blocks[b].nsucc; s++)
      blocks[blocks[b].succ[s]].npred++;
  int off = 0;
  for (int b = 0; b < nblocks; b++) {
    blocks[b].pred = pool + off;
    off += blocks[b].npred;
    blocks[b].npred = 0;
  }
  for (int b = 0; b < nblocks; b++)
    for (int s = 0; s < blocks[b].nsucc; s++) {
      IrBlock *sb = &blocks[blocks[b].succ[s]];
      sb->pred[sb->npred++] = b;
    }
}

// ---------------------------------------------------------------------------
// Instruction selection (tiling) + emission.  Ports the -O2 selection onto the
// IR: OP2 memory-operand / indexed / direct-store / constant-either-side /
// strength-reduction tiles, and OP3 condition-context branching.
// ---------------------------------------------------------------------------

static void ir_expr(IrExpr *e);
static void ir_cond(IrExpr *e, char *label, bool when);

// OP5 register promotion (opt_regalloc): a promoted local/param lives in a
// callee-saved register instead of its frame slot. Obj.reg encodes the target:
// 2..7 = D2..D7 (data), 10..13 = A2..A5 (address; 8 + An index).
static bool var_promoted(Obj *v) { return v && v->reg; }

// True if reg code `r` names an address register (A2..A5).
static bool reg_is_addr(int r) { return r >= 8; }

// The assembler operand name for reg code `r` ("d2".."d7" / "a2".."a5").
static char *reg_name(int r) {
  return reg_is_addr(r) ? format("a%d", r - 8) : format("d%d", r);
}

// If `e` is the address of a promoted local (IE_LEA of a var held in a
// register), return that register code (2..7 / 10..13); else 0.
static int lea_reg(IrExpr *e) {
  return (e->kind == IE_LEA && e->is_local && var_promoted(e->var)) ? e->var->reg
                                                                    : 0;
}

// An IE_NUM behind width-preserving (>=4-byte) integer casts -> its 32-bit value.
static bool ie_const(IrExpr *e, int32_t *out) {
  while (e->kind == IE_CAST && tk_is_integer(e->op) && e->size >= 4)
    e = e->a;
  if (e->kind != IE_NUM || e->size < 4)
    return false;
  *out = (int32_t)e->val;
  return true;
}

// A load of a simple size-4 scalar var (frame slot or global) -> its EA text.
// Mirrors simple_lval_ea: the fold tiles below use it for a memory operand.
static IrExpr *ie_simple_lval(IrExpr *e, char **ea) {
  while (e->kind == IE_CAST && tk_is_integer(e->op) && e->size == 4)
    e = e->a;
  if (e->kind != IE_LOAD || e->a->kind != IE_LEA)
    return NULL;
  IrExpr *lea = e->a;
  int r = lea_reg(lea);
  if (reg_is_addr(r)) // promoted to an address reg: An is not a valid ALU <ea>
    return NULL;       // (invalid for and/or); read via move.l aR,d0 instead
  *ea = r          ? format("d%d", r) // promoted: register operand (add.l dN,d0)
        : lea->is_local ? format("%d(a6)", lea->off)
                        : cg_symref(lea->name);
  return e;
}

// A "simple var" base for the indexed-addressing tile: an array's address
// (IE_LEA) or a pointer var's value (IE_LOAD of IE_LEA) -- both touch only
// D0/A0, so a scaled index stashed in D1 survives the base evaluation.
static bool ie_simple_var_base(IrExpr *e) {
  while (e->kind == IE_CAST)
    e = e->a;
  return e->kind == IE_LEA || (e->kind == IE_LOAD && e->a->kind == IE_LEA);
}

static void load_imm(int64_t val) {
  if (val >= -128 && val <= 127)
    EMIT("  moveq #%ld,d0", (long)val);
  else
    EMIT("  move.l #%ld,d0", (long)val);
}

// Narrow/sign-adjust the 32-bit accumulator to a <=4-byte integer type.
static void cast_int_narrow(int size, bool uns) {
  switch (size) {
  case 1:
    if (uns)
      EMIT("  andi.l #255,d0");
    else {
      EMIT("  ext.w d0");
      EMIT("  ext.l d0");
    }
    return;
  case 2:
    if (uns)
      EMIT("  andi.l #65535,d0");
    else
      EMIT("  ext.l d0");
    return;
  }
}

// Emit the size/sign-extending load from the address already in A0.
static void emit_load_from_a0(int size, bool uns) {
  if (size == 1) {
    if (uns) {
      EMIT("  moveq #0,d0");
      EMIT("  move.b (a0),d0");
    } else {
      EMIT("  move.b (a0),d0");
      EMIT("  ext.w d0");
      EMIT("  ext.l d0");
    }
  } else if (size == 2) {
    if (uns) {
      EMIT("  moveq #0,d0");
      EMIT("  move.w (a0),d0");
    } else {
      EMIT("  move.w (a0),d0");
      EMIT("  ext.l d0");
    }
  } else {
    EMIT("  move.l (a0),d0");
  }
}

// Integer binop with a compile-time constant operand `c`; `operand` is the
// other (evaluated) side. Ports gen_const_binop: emits nothing and returns
// false when it does not specialize, so the caller falls back.
static bool ir_const_binop(IrExpr *e, IrExpr *operand, int32_t c) {
  bool u = e->uns;
  uint32_t uc = (uint32_t)c;

  switch (e->op) {
  case ND_ADD:
    ir_expr(operand);
    add_imm(c);
    return true;
  case ND_SUB:
    ir_expr(operand);
    add_imm(-c);
    return true;
  case ND_BITAND:
    ir_expr(operand);
    EMIT("  andi.l #%ld,d0", (long)c);
    return true;
  case ND_BITOR:
    ir_expr(operand);
    EMIT("  ori.l #%ld,d0", (long)c);
    return true;
  case ND_BITXOR:
    ir_expr(operand);
    EMIT("  eori.l #%ld,d0", (long)c);
    return true;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE: {
    ir_expr(operand);
    EMIT("  cmp.l #%ld,d0", (long)c);
    char *cc = e->op == ND_EQ ? "seq" :
               e->op == ND_NE ? "sne" :
               e->op == ND_LT ? (u ? "scs" : "slt") : (u ? "sls" : "sle");
    EMIT("  %s d0", cc);
    EMIT("  andi.l #1,d0");
    return true;
  }
  case ND_SHL:
    if (c < 1 || c > 31)
      return false;
    ir_expr(operand);
    shift_by("asl.l", c, "d0", "d1");
    return true;
  case ND_SHR:
    if (c < 1 || c > 31)
      return false;
    ir_expr(operand);
    shift_by(u ? "lsr.l" : "asr.l", c, "d0", "d1");
    return true;
  case ND_MUL:
    if (c == 0 || c == 1 || c == -1 || is_pow2_32(uc)) {
      ir_expr(operand);
      if (c == 0)
        EMIT("  moveq #0,d0");
      else if (c == -1)
        EMIT("  neg.l d0");
      else if (c != 1)
        shift_by("asl.l", ctz32(uc), "d0", "d1");
      return true;
    }
    if (opt_level >= 2 && c >= 3) {
      if (is_pow2_32(uc - 1) && ctz32(uc - 1) <= 8) {
        ir_expr(operand);
        EMIT("  move.l d0,d1");
        EMIT("  asl.l #%d,d0", ctz32(uc - 1));
        EMIT("  add.l d1,d0");
        return true;
      }
      if (is_pow2_32(uc + 1) && ctz32(uc + 1) <= 8) {
        ir_expr(operand);
        EMIT("  move.l d0,d1");
        EMIT("  asl.l #%d,d0", ctz32(uc + 1));
        EMIT("  sub.l d1,d0");
        return true;
      }
    }
    return false;
  case ND_DIV:
    if (u && is_pow2_32(uc)) {
      ir_expr(operand);
      shift_by("lsr.l", ctz32(uc), "d0", "d1");
      return true;
    }
    if (opt_level >= 2 && !u && c >= 2 && is_pow2_32(uc)) {
      int lc = cg_uid();
      ir_expr(operand);
      EMIT("  tst.l d0");
      EMIT("  bpl L_sdiv_%d", lc);
      add_imm((int32_t)uc - 1);
      EMIT("L_sdiv_%d:", lc);
      shift_by("asr.l", ctz32(uc), "d0", "d1");
      return true;
    }
    return false;
  case ND_MOD:
    if (u && is_pow2_32(uc)) {
      ir_expr(operand);
      EMIT("  andi.l #%ld,d0", (long)(uc - 1));
      return true;
    }
    if (opt_level >= 2 && !u && c >= 2 && is_pow2_32(uc)) {
      int lc = cg_uid();
      ir_expr(operand);
      EMIT("  tst.l d0");
      EMIT("  bpl L_smod_%d", lc);
      EMIT("  neg.l d0");
      EMIT("  andi.l #%ld,d0", (long)(uc - 1));
      EMIT("  neg.l d0");
      EMIT("  bra L_smodx_%d", lc);
      EMIT("L_smod_%d:", lc);
      EMIT("  andi.l #%ld,d0", (long)(uc - 1));
      EMIT("L_smodx_%d:", lc);
      return true;
    }
    return false;
  }
  return false;
}

// Constant LEFT operand: commutative ops reuse the fast path; c<x / c<=x emit
// x>c / x>=c with the reversed condition. Ports gen_const_binop_left.
static bool ir_const_binop_left(IrExpr *e, int32_t c) {
  if (is_commutative(e->op))
    return ir_const_binop(e, e->b, c);
  if (e->op == ND_LT || e->op == ND_LE) {
    bool u = e->uns;
    ir_expr(e->b);
    EMIT("  cmp.l #%ld,d0", (long)c);
    char *cc = e->op == ND_LT ? (u ? "shi" : "sgt") : (u ? "shs" : "sge");
    EMIT("  %s d0", cc);
    EMIT("  andi.l #1,d0");
    return true;
  }
  return false;
}

// Memory-source operand (OP2 #4): RHS a simple size-4 lvalue -> fold into the
// ALU op. EOR has no <ea>,Dn form and stays generic. Ports gen_mem_binop.
static bool ir_mem_binop(IrExpr *e) {
  char *ea;
  IrExpr *v = ie_simple_lval(e->b, &ea);
  if (!v || v->size != 4)
    return false;

  switch (e->op) {
  case ND_ADD:
    ir_expr(e->a);
    EMIT("  add.l %s,d0", ea);
    return true;
  case ND_SUB:
    ir_expr(e->a);
    EMIT("  sub.l %s,d0", ea);
    return true;
  case ND_BITAND:
    ir_expr(e->a);
    EMIT("  and.l %s,d0", ea);
    return true;
  case ND_BITOR:
    ir_expr(e->a);
    EMIT("  or.l %s,d0", ea);
    return true;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE: {
    bool u = e->uns;
    ir_expr(e->a);
    EMIT("  cmp.l %s,d0", ea);
    char *cc = e->op == ND_EQ ? "seq" :
               e->op == ND_NE ? "sne" :
               e->op == ND_LT ? (u ? "scs" : "slt") : (u ? "sls" : "sle");
    EMIT("  %s d0", cc);
    EMIT("  andi.l #1,d0");
    return true;
  }
  }
  return false;
}

// Indexed load (OP2 #7): *(base + index) with a simple var base and a longword
// element -> (An,Xn.L). Ports gen_indexed_load.
static bool ir_indexed_load(IrExpr *e) {
  if (e->size != 4)
    return false;
  IrExpr *add = e->a;
  if (add->kind != IE_BINOP || add->op != ND_ADD || !add->ptradd)
    return false;
  if (!ie_simple_var_base(add->a))
    return false;
  ir_expr(add->b); // scaled index -> D0
  EMIT("  move.l d0,d1");
  ir_expr(add->a); // base -> D0 (touches only D0/A0, so D1 survives)
  EMIT("  movea.l d0,a0");
  EMIT("  move.l (a0,d1.l),d0");
  return true;
}

static void ir_binop(IrExpr *e) {
  int32_t c;
  if (ie_const(e->b, &c) && ir_const_binop(e, e->a, c))
    return;
  if (ie_const(e->a, &c) && ir_const_binop_left(e, c))
    return;
  if (ir_mem_binop(e))
    return;

  ir_expr(e->b);
  cg_push();
  ir_expr(e->a);
  cg_pop("d1");

  switch (e->op) {
  case ND_ADD:
    EMIT("  add.l d1,d0");
    return;
  case ND_SUB:
    EMIT("  sub.l d1,d0");
    return;
  case ND_MUL:
    EMIT("  jsr __mulsi3##");
    return;
  case ND_DIV:
  case ND_MOD:
    if (e->uns)
      EMIT("  jsr %s##", e->op == ND_DIV ? "__udivsi3" : "__umodsi3");
    else
      EMIT("  jsr %s##", e->op == ND_DIV ? "__divsi3" : "__modsi3");
    return;
  case ND_BITAND:
    EMIT("  and.l d1,d0");
    return;
  case ND_BITOR:
    EMIT("  or.l d1,d0");
    return;
  case ND_BITXOR:
    EMIT("  eor.l d1,d0");
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE: {
    EMIT("  cmp.l d1,d0");
    bool u = e->uns;
    char *cc = e->op == ND_EQ ? "seq" :
               e->op == ND_NE ? "sne" :
               e->op == ND_LT ? (u ? "scs" : "slt") : (u ? "sls" : "sle");
    EMIT("  %s d0", cc);
    EMIT("  andi.l #1,d0");
    return;
  }
  case ND_SHL:
    EMIT("  asl.l d1,d0");
    return;
  case ND_SHR:
    EMIT(e->uns ? "  lsr.l d1,d0" : "  asr.l d1,d0");
    return;
  }
}

static void ir_call(IrExpr *e) {
  // Push arguments right-to-left (first argument ends up at the lowest
  // address). Every argument is a <=4-byte scalar or a decayed pointer.
  for (int i = e->nargs - 1; i >= 0; i--) {
    ir_expr(e->args[i]);
    cg_push();
  }
  int bytes = e->nargs * 4;

  if (e->name) {
    EMIT("  jsr %s", cg_symref(e->name));
  } else {
    ir_expr(e->a);
    EMIT("  movea.l d0,a0");
    EMIT("  jsr (a0)");
  }

  if (bytes) {
    EMIT("  adda.w #%d,sp", bytes);
    cg_adjust_depth(-(bytes / 4));
  }
}

static void ir_memzero(IrExpr *e) {
  if (var_promoted(e->var)) {
    int r = e->var->reg; // promoted local: clear the register in place
    if (reg_is_addr(r))
      EMIT("  movea.l #0,%s", reg_name(r)); // moveq targets data regs only
    else
      EMIT("  moveq #0,%s", reg_name(r));
    return;
  }
  int sz = e->size, off = e->off;
  if (opt_level >= 1 && sz <= 16 && (off & 1) == 0) {
    int p = 0;
    while (sz - p >= 4) {
      EMIT("  clr.l %d(a6)", off + p);
      p += 4;
    }
    if (sz - p >= 2) {
      EMIT("  clr.w %d(a6)", off + p);
      p += 2;
    }
    if (sz - p >= 1)
      EMIT("  clr.b %d(a6)", off + p);
    return;
  }
  int c = cg_uid();
  EMIT("  lea %d(a6),a0", off);
  EMIT("  move.w #%d,d1", sz - 1);
  EMIT("L_memzero_%d:", c);
  EMIT("  clr.b (a0)+");
  EMIT("  dbra d1,L_memzero_%d", c);
}

// Evaluate `e`, leaving the value in D0 (D0:D1 unused -- no 8-byte scalars).
static void ir_expr(IrExpr *e) {
  switch (e->kind) {
  case IE_NOP:
    return;
  case IE_NUM:
    load_imm(e->val);
    return;
  case IE_LEA:
    if (e->is_local)
      EMIT("  lea %d(a6),a0", e->off);
    else
      EMIT("  lea %s,a0", cg_symref(e->name));
    EMIT("  move.l a0,d0");
    return;
  case IE_LOAD: {
    int r = lea_reg(e->a);
    if (r) {
      EMIT("  move.l %s,d0", reg_name(r)); // promoted local: no memory access
      return;
    }
    if (ir_indexed_load(e))
      return;
    ir_expr(e->a);
    EMIT("  movea.l d0,a0");
    emit_load_from_a0(e->size, e->uns);
    return;
  }
  case IE_STORE: {
    int r = lea_reg(e->a);
    if (r) {
      ir_expr(e->b);
      if (reg_is_addr(r))
        EMIT("  movea.l d0,%s", reg_name(r)); // promoted local: no memory access
      else
        EMIT("  move.l d0,%s", reg_name(r));
      return;
    }
    // Direct store to a simple lvalue EA (OP2 #5): skip the address push/pop.
    if (e->a->kind == IE_LEA && e->size <= 4) {
      ir_expr(e->b);
      char *msz = e->size == 1 ? "b" : e->size == 2 ? "w" : "l";
      char *ea = e->a->is_local ? format("%d(a6)", e->a->off)
                                : cg_symref(e->a->name);
      EMIT("  move.%s d0,%s", msz, ea);
      return;
    }
    ir_expr(e->a); // destination address
    cg_push();
    ir_expr(e->b); // value
    cg_pop("a1");
    if (e->size == 1)
      EMIT("  move.b d0,(a1)");
    else if (e->size == 2)
      EMIT("  move.w d0,(a1)");
    else
      EMIT("  move.l d0,(a1)");
    return;
  }
  case IE_BINOP:
    ir_binop(e);
    return;
  case IE_UNARY:
    ir_expr(e->a);
    if (e->op == ND_NEG)
      EMIT("  neg.l d0");
    else if (e->op == ND_BITNOT)
      EMIT("  not.l d0");
    else { // ND_NOT
      EMIT("  tst.l d0");
      EMIT("  seq d0");
      EMIT("  andi.l #1,d0");
    }
    return;
  case IE_LOGAND: {
    int c = cg_uid();
    ir_expr(e->a);
    EMIT("  tst.l d0");
    EMIT("  beq L_false_%d", c);
    ir_expr(e->b);
    EMIT("  tst.l d0");
    EMIT("  beq L_false_%d", c);
    EMIT("  moveq #1,d0");
    EMIT("  bra L_end_%d", c);
    EMIT("L_false_%d:", c);
    EMIT("  moveq #0,d0");
    EMIT("L_end_%d:", c);
    return;
  }
  case IE_LOGOR: {
    int c = cg_uid();
    ir_expr(e->a);
    EMIT("  tst.l d0");
    EMIT("  bne L_true_%d", c);
    ir_expr(e->b);
    EMIT("  tst.l d0");
    EMIT("  bne L_true_%d", c);
    EMIT("  moveq #0,d0");
    EMIT("  bra L_end_%d", c);
    EMIT("L_true_%d:", c);
    EMIT("  moveq #1,d0");
    EMIT("L_end_%d:", c);
    return;
  }
  case IE_COND: {
    int c = cg_uid();
    ir_cond(e->a, format("L_else_%d", c), false);
    ir_expr(e->b);
    EMIT("  bra L_end_%d", c);
    EMIT("L_else_%d:", c);
    ir_expr(e->c);
    EMIT("L_end_%d:", c);
    return;
  }
  case IE_COMMA:
    ir_expr(e->a);
    ir_expr(e->b);
    return;
  case IE_CAST:
    ir_expr(e->a);
    if (e->op == TY_VOID)
      return;
    if (e->op == TY_BOOL) {
      EMIT("  tst.l d0"); // from is <=4-byte integer/pointer
      EMIT("  sne d0");
      EMIT("  andi.l #1,d0");
      return;
    }
    cast_int_narrow(e->size, e->uns);
    return;
  case IE_CALL:
    ir_call(e);
    return;
  case IE_MEMZERO:
    ir_memzero(e);
    return;
  }
}

// Branch to `label` exactly when the truth of `e` equals `when`. Ports gen_cond
// (OP3 #9): relationals / ! / && / || lower straight to cmp+Bcc.
static void ir_cond(IrExpr *e, char *label, bool when) {
  if (e->kind == IE_UNARY && e->op == ND_NOT) {
    ir_cond(e->a, label, !when);
    return;
  }
  if (e->kind == IE_LOGAND) {
    if (when) {
      char *skip = format("L_condskip_%d", cg_uid());
      ir_cond(e->a, skip, false);
      ir_cond(e->b, label, true);
      EMIT("%s:", skip);
    } else {
      ir_cond(e->a, label, false);
      ir_cond(e->b, label, false);
    }
    return;
  }
  if (e->kind == IE_LOGOR) {
    if (when) {
      ir_cond(e->a, label, true);
      ir_cond(e->b, label, true);
    } else {
      char *skip = format("L_condskip_%d", cg_uid());
      ir_cond(e->a, skip, true);
      ir_cond(e->b, label, false);
      EMIT("%s:", skip);
    }
    return;
  }

  if (e->kind == IE_BINOP &&
      (e->op == ND_EQ || e->op == ND_NE || e->op == ND_LT || e->op == ND_LE)) {
    Rel base;
    bool u = e->uns;
    int32_t c;
    char *ea;
    IrExpr *v;
    if (ie_const(e->b, &c)) {
      ir_expr(e->a);
      EMIT("  cmp.l #%ld,d0", (long)c);
      base = rel_of(e->op);
    } else if (ie_const(e->a, &c)) {
      ir_expr(e->b);
      EMIT("  cmp.l #%ld,d0", (long)c);
      base = rel_swap(rel_of(e->op));
    } else if ((v = ie_simple_lval(e->b, &ea)) && v->size == 4) {
      ir_expr(e->a);
      EMIT("  cmp.l %s,d0", ea);
      base = rel_of(e->op);
    } else {
      ir_expr(e->b);
      cg_push();
      ir_expr(e->a);
      cg_pop("d1");
      EMIT("  cmp.l d1,d0");
      base = rel_of(e->op);
    }
    Rel eff = when ? base : rel_negate(base);
    EMIT("  %s %s", bcc_mnem(eff, u), label);
    return;
  }

  // Fallback: materialize the value and test it against zero (<=4-byte scalar).
  ir_expr(e);
  EMIT("  tst.l d0");
  EMIT("  %s %s", when ? "bne" : "beq", label);
}

static void ir_emit_item(IrItem *it) {
  switch (it->kind) {
  case II_LABEL:
    EMIT("%s:", it->label);
    return;
  case II_NOP:
    return; // OP6 removed this item (folded branch / dead code)
  case II_JMP:
    EMIT("  bra %s", it->label);
    return;
  case II_CBR:
    ir_cond(it->e, it->label, it->when);
    return;
  case II_EVAL:
    ir_expr(it->e);
    return;
  case II_RET:
    if (it->e)
      ir_expr(it->e);
    EMIT("  bra L_return_%s", ir_fn->name);
    return;
  }
}

// ---------------------------------------------------------------------------
// OP5: whole-function register promotion of hot scalar locals/params.
// ---------------------------------------------------------------------------

static Obj *ra_addr[256]; // vars whose address is taken (cannot be a register)
static int ra_naddr;

static void ra_note_addr(Node *n) { // n = the operand of an ND_ADDR
  while (n && n->kind == ND_MEMBER)
    n = n->lhs;
  if (n && n->kind == ND_VAR && n->var && ra_naddr < 256)
    ra_addr[ra_naddr++] = n->var;
}

static bool ra_addr_taken(Obj *v) {
  for (int i = 0; i < ra_naddr; i++)
    if (ra_addr[i] == v)
      return true;
  return false;
}

typedef struct {
  Obj *var;
  int uses;     // total references
  int loopuses; // references at loop depth >= 1 (where promotion pays off)
} RaCand;
static RaCand ra_cand[256];
static int ra_ncand;
static int ra_loopdepth; // current loop nesting during ra_walk

static int ra_cand_index(Obj *v) {
  for (int i = 0; i < ra_ncand; i++)
    if (ra_cand[i].var == v)
      return i;
  return -1;
}

// One AST walk: note address-taken vars and count candidate references, giving
// uses inside loops a separate tally (register residency only pays for its
// movem/param-load overhead when the accesses repeat).
static void ra_walk(Node *n) {
  if (!n)
    return;
  // Folded `*tmp` (tmp = &x) counts as a use of x; recursing into the tmp is
  // pointless (it is folded away).
  if (n->kind == ND_DEREF) {
    Obj *v = foldable_alias(n->lhs);
    if (v) {
      int i = ra_cand_index(v);
      if (i >= 0) {
        ra_cand[i].uses++;
        if (ra_loopdepth)
          ra_cand[i].loopuses++;
      }
      return;
    }
  }
  if (n->kind == ND_ADDR) {
    if (fa_is_init(n)) // folded `&x` init: does not make x address-taken
      return;
    ra_note_addr(n->lhs);
  }
  if (n->kind == ND_VAR) {
    int i = ra_cand_index(n->var);
    if (i >= 0) {
      ra_cand[i].uses++;
      if (ra_loopdepth)
        ra_cand[i].loopuses++;
    }
  }
  // A loop's per-iteration parts recurse one nesting level deeper; the for-init
  // runs once and stays at the current depth.
  if (n->kind == ND_FOR) {
    ra_walk(n->init);
    ra_loopdepth++;
    ra_walk(n->cond);
    ra_walk(n->inc);
    ra_walk(n->then);
    ra_loopdepth--;
    return;
  }
  if (n->kind == ND_DO) {
    ra_loopdepth++;
    ra_walk(n->cond);
    ra_walk(n->then);
    ra_loopdepth--;
    return;
  }
  ra_walk(n->lhs);
  ra_walk(n->rhs);
  ra_walk(n->cond);
  ra_walk(n->then);
  ra_walk(n->els);
  ra_walk(n->init);
  ra_walk(n->inc);
  for (Node *b = n->body; b; b = b->next)
    ra_walk(b);
  for (Node *a = n->args; a; a = a->next)
    ra_walk(a);
}

// Recognize COMMA(ASSIGN(VAR tmp, [cast] ADDR(VAR x)), _) -> record tmp -> x.
// chibicc's to_assign wraps the `&x` in a pointer cast, so peel ND_CAST first.
static void fa_find(Node *n) {
  if (!n)
    return;
  if (n->kind == ND_COMMA && n->lhs && n->lhs->kind == ND_ASSIGN) {
    Node *as = n->lhs, *addr = as->rhs;
    while (addr && addr->kind == ND_CAST)
      addr = addr->lhs;
    if (as->lhs && as->lhs->kind == ND_VAR && addr && addr->kind == ND_ADDR &&
        addr->lhs && addr->lhs->kind == ND_VAR) {
      Obj *tmp = as->lhs->var, *v = addr->lhs->var;
      if (tmp && tmp->is_local && tmp->name && !tmp->name[0] &&
          tmp->ty->kind == TY_PTR && v && v->is_local && v->name && v->name[0] &&
          v->ty->size == 4 && (is_integer(v->ty) || v->ty->kind == TY_PTR) &&
          fa_n < 256) {
        fa_tmp[fa_n] = tmp;
        fa_v[fa_n] = v;
        fa_init[fa_n] = addr;
        fa_n++;
      }
    }
  }
  fa_find(n->lhs);
  fa_find(n->rhs);
  fa_find(n->cond);
  fa_find(n->then);
  fa_find(n->els);
  fa_find(n->init);
  fa_find(n->inc);
  for (Node *b = n->body; b; b = b->next)
    fa_find(b);
  for (Node *a = n->args; a; a = a->next)
    fa_find(a);
}

// Safety: a foldable tmp must appear ONLY as its init lhs or inside `*tmp`, so a
// valid tmp has (total refs) == 1 + (deref refs). Any bare escape disqualifies.
static int fa_ref[256], fa_deref[256];

static int fa_index(Obj *tmp) {
  for (int i = 0; i < fa_n; i++)
    if (fa_tmp[i] == tmp)
      return i;
  return -1;
}

static void fa_count(Node *n) {
  if (!n)
    return;
  if (n->kind == ND_DEREF && n->lhs && n->lhs->kind == ND_VAR) {
    int i = fa_index(n->lhs->var);
    if (i >= 0)
      fa_deref[i]++;
  }
  if (n->kind == ND_VAR) {
    int i = fa_index(n->var);
    if (i >= 0)
      fa_ref[i]++;
  }
  fa_count(n->lhs);
  fa_count(n->rhs);
  fa_count(n->cond);
  fa_count(n->then);
  fa_count(n->els);
  fa_count(n->init);
  fa_count(n->inc);
  for (Node *b = n->body; b; b = b->next)
    fa_count(b);
  for (Node *a = n->args; a; a = a->next)
    fa_count(a);
}

static void ra_add_cand(Obj *v, Obj *fn) {
  if (ra_ncand >= 256 || v == fn->va_area || v == fn->alloca_bottom)
    return;
  // Skip compiler-synthesized temps (empty name) -- notably the `tmp = &x`
  // pointer chibicc emits for every compound assignment (`x += ...`, `x++`).
  // The fold above rewrites `*tmp` back to `x` so the underlying `x` becomes the
  // candidate; the bare pointer temp itself is never worth a register.
  if (!v->name || !v->name[0])
    return;
  Type *t = v->ty;
  // Only 4-byte int/pointer scalars fit a register cleanly (v1); such a value
  // round-trips through a data or address register losslessly (movea.l is full
  // 32-bit), so either class can hold it.
  if (t->size != 4 || !(is_integer(t) || t->kind == TY_PTR))
    return;
  ra_cand[ra_ncand].var = v;
  ra_cand[ra_ncand].uses = 0;
  ra_cand[ra_ncand].loopuses = 0;
  ra_ncand++;
}

bool ir_body_eligible(Obj *fn) { return stmt_ok(fn->body); }

// ---------------------------------------------------------------------------
// OP7 (Tier G): global register allocation.  Lands -O3.
//
// The OP5 pass below gives every promoted variable its OWN register for the
// whole function.  OP7 instead computes a live range per candidate and lets
// candidates whose ranges are DISJOINT share a register, so more variables are
// promoted under the same D2-D7/A2-A5 budget (e.g. the index variables of two
// sequential loops need only one register between them).  It subsumes OP5: when
// every candidate is simultaneously live the coloring reproduces OP5's D2,D3,...
// assignment exactly, so most functions stay identical.
//
// Liveness model = linear-scan intervals.  A pre-order walk numbers program
// points; a candidate's interval is [first ref, last ref], EXTENDED to span any
// loop it is referenced in (so a value live across the loop back edge is
// covered).  Two candidates interfere iff their intervals overlap.  For
// goto-free structured code this OVER-approximates true liveness (a variable is
// treated as live across the whole span, holes included), so the interference
// is conservative and sharing is always sound.  A function containing a goto or
// a label (which includes chibicc-lowered break/continue) is not structured
// this way, so ra_plan falls back to the OP5 whole-function assignment there.
//
// This is a v1: no live-range splitting and no spilling of temporaries (a
// candidate that cannot get a color simply stays in its frame slot, exactly as
// under OP5's overflow).  The value-materialization optimizations OP6 deferred
// here (CSE materialization, LICM, IV strength reduction) build on this reg
// budget and are a follow-up.
// ---------------------------------------------------------------------------

static int ra_time;         // pre-order program-point counter
static int ra_lo[256];      // per-candidate live interval [lo,hi]; lo>hi = unref
static int ra_hi[256];

// True if the subtree contains a goto or label -- unstructured control flow that
// the interval model cannot bound (chibicc lowers break/continue to ND_GOTO too).
static bool ra_has_goto(Node *n) {
  if (!n)
    return false;
  if (n->kind == ND_GOTO || n->kind == ND_LABEL)
    return true;
  if (ra_has_goto(n->lhs) || ra_has_goto(n->rhs) || ra_has_goto(n->cond) ||
      ra_has_goto(n->then) || ra_has_goto(n->els) || ra_has_goto(n->init) ||
      ra_has_goto(n->inc))
    return true;
  for (Node *b = n->body; b; b = b->next)
    if (ra_has_goto(b))
      return true;
  for (Node *a = n->args; a; a = a->next)
    if (ra_has_goto(a))
      return true;
  return false;
}

// Record a reference to candidate `v` at the current program point.
static void ra_live_ref(Obj *v) {
  int i = ra_cand_index(v);
  if (i < 0)
    return;
  if (ra_lo[i] > ra_hi[i]) {
    ra_lo[i] = ra_hi[i] = ra_time;
  } else {
    if (ra_time < ra_lo[i])
      ra_lo[i] = ra_time;
    if (ra_time > ra_hi[i])
      ra_hi[i] = ra_time;
  }
}

// After walking a loop spanning program points [lstart,lend] (inclusive; lend is
// the time of the loop's last node), extend every candidate referenced inside it
// to cover the whole loop (cross-iteration liveness): the value may be live from
// the loop bottom back to the top.
static void ra_extend_loop(int lstart, int lend) {
  for (int i = 0; i < ra_ncand; i++) {
    if (ra_lo[i] > ra_hi[i]) // unreferenced
      continue;
    if (ra_hi[i] >= lstart && ra_lo[i] <= lend) { // referenced inside the loop
      if (lstart < ra_lo[i])
        ra_lo[i] = lstart;
      if (lend > ra_hi[i])
        ra_hi[i] = lend;
    }
  }
}

// Pre-order interval walk, mirroring ra_walk's reference semantics (the folded
// `*tmp` counts as a use of x; the folded `&x` init is not a reference).
static void ra_live_walk(Node *n) {
  if (!n)
    return;
  ra_time++;
  if (n->kind == ND_DEREF) {
    Obj *v = foldable_alias(n->lhs);
    if (v) {
      ra_live_ref(v);
      return; // the pointer temp is folded away; don't recurse into it
    }
  }
  if (n->kind == ND_ADDR && fa_is_init(n))
    return; // folded `&x` init: not a reference
  if (n->kind == ND_VAR)
    ra_live_ref(n->var);
  if (n->kind == ND_FOR) {
    int lstart = ra_time;
    ra_live_walk(n->init);
    ra_live_walk(n->cond);
    ra_live_walk(n->inc);
    ra_live_walk(n->then);
    ra_extend_loop(lstart, ra_time);
    return;
  }
  if (n->kind == ND_DO) {
    int lstart = ra_time;
    ra_live_walk(n->cond);
    ra_live_walk(n->then);
    ra_extend_loop(lstart, ra_time);
    return;
  }
  ra_live_walk(n->lhs);
  ra_live_walk(n->rhs);
  ra_live_walk(n->cond);
  ra_live_walk(n->then);
  ra_live_walk(n->els);
  ra_live_walk(n->init);
  ra_live_walk(n->inc);
  for (Node *b = n->body; b; b = b->next)
    ra_live_walk(b);
  for (Node *a = n->args; a; a = a->next)
    ra_live_walk(a);
}

// True if candidates i and j have overlapping (interfering) live intervals.
static bool ra_interfere(int i, int j) {
  return !(ra_hi[i] < ra_lo[j] || ra_hi[j] < ra_lo[i]);
}

// OP7 coloring: interference-aware assignment. Candidates in OP5 priority order
// (uses desc, declaration order asc) each take the lowest-numbered register not
// used by an already-colored interfering neighbor. Returns the highest data
// register assigned (address regs are recovered by the prologue via ->reg).
static int ra_color_global(Obj *fn) {
  ra_time = 0;
  for (int i = 0; i < ra_ncand; i++) {
    ra_lo[i] = 1; // empty interval (lo > hi)
    ra_hi[i] = 0;
  }
  ra_live_walk(fn->body);

  // Colorable candidates (same gate as OP5: used in a loop, address not taken).
  int order[256], no = 0;
  for (int i = 0; i < ra_ncand; i++)
    if (ra_cand[i].loopuses >= 1 && !ra_addr_taken(ra_cand[i].var))
      order[no++] = i;
  // Stable insertion sort by uses descending (ties keep declaration order, so an
  // all-interfering function reproduces OP5's D2,D3,... assignment exactly).
  for (int a = 1; a < no; a++) {
    int cur = order[a], j = a - 1;
    while (j >= 0 && ra_cand[order[j]].uses < ra_cand[cur].uses) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = cur;
  }

  static const int pool[] = {2, 3, 4, 5, 6, 7, 10, 11, 12, 13};
  int hi = 0;
  for (int k = 0; k < no; k++) {
    int ci = order[k];
    bool used[14] = {false};
    for (int m = 0; m < k; m++) {
      int cj = order[m];
      if (ra_cand[cj].var->reg && ra_interfere(ci, cj))
        used[ra_cand[cj].var->reg] = true;
    }
    int chosen = 0;
    for (int p = 0; p < (int)(sizeof(pool) / sizeof(pool[0])); p++)
      if (!used[pool[p]]) {
        chosen = pool[p];
        break;
      }
    ra_cand[ci].var->reg = chosen; // 0 = all registers conflict -> stay in memory
    if (chosen >= 2 && chosen <= 7 && chosen > hi)
      hi = chosen;
  }
  return hi;
}

// Promote the most-used, address-not-taken scalar locals/params into callee-
// saved registers (they survive calls): the six data registers D2-D7 first,
// then the four address registers A2-A5 as overflow. Returns the highest DATA
// register used (0 = none) so the prologue/epilogue can movem-save exactly
// D2..that (address registers are recovered separately by scanning ->reg).
int ir_plan_regs(Obj *fn) {
  for (Obj *v = fn->params; v; v = v->next)
    v->reg = 0;
  for (Obj *v = fn->locals; v; v = v->next)
    v->reg = 0;
  fa_n = 0; // reset the fold map -- with regalloc off the builder folds nothing
  if (!opt_regalloc || fn->uses_returns_twice)
    return 0;

  // Fold the compound-assign `tmp = &x` idiom (so x can be promoted), then drop
  // any tmp that escapes its idiom.
  fa_find(fn->body);
  for (int i = 0; i < fa_n; i++) {
    fa_ref[i] = 0;
    fa_deref[i] = 0;
  }
  fa_count(fn->body);
  for (int i = 0; i < fa_n; i++)
    if (fa_ref[i] != 1 + fa_deref[i]) {
      fa_tmp[i] = NULL;
      fa_init[i] = NULL;
    }

  ra_naddr = 0;
  ra_ncand = 0;
  ra_loopdepth = 0;
  for (Obj *v = fn->params; v; v = v->next)
    ra_add_cand(v, fn);
  for (Obj *v = fn->locals; v; v = v->next)
    ra_add_cand(v, fn);
  ra_walk(fn->body);

  // OP7 (Tier G, -O3): liveness-based interference coloring lets candidates with
  // disjoint live ranges SHARE a register, promoting more of them under the same
  // D2-D7/A2-A5 budget. Only for goto-free functions (structured control flow, so
  // the interval liveness model is a sound over-approximation); a goto/label (or
  // a lowered break/continue) falls through to the OP5 whole-function assignment
  // below, which gives each candidate its own register and is always safe. -O2
  // never enters here, so its output stays byte-identical.
  if (opt_level >= 3 && !ra_has_goto(fn->body))
    return ra_color_global(fn);

  // Assign D2,D3,...,D7 then A2,...,A5 to the most-used candidates. Require a
  // loop use: register residency only repays its movem/param-load overhead when
  // the accesses repeat, so straight-line leaf code is left in frame slots
  // (promoting it there is code-size-neutral at best). Data registers fill
  // first (they take moveq/direct-operand forms an address reg cannot); the
  // address registers A2-A5 extend the pool for register-hungry loops. Filling
  // D2-D7 first keeps functions with <=6 candidates byte-identical to before.
  static const int seq[] = {2, 3, 4, 5, 6, 7, 10, 11, 12, 13};
  int hi = 0;
  for (int k = 0; k < (int)(sizeof(seq) / sizeof(seq[0])); k++) {
    int best = -1;
    for (int i = 0; i < ra_ncand; i++) {
      if (ra_cand[i].var->reg || ra_cand[i].loopuses < 1 ||
          ra_addr_taken(ra_cand[i].var))
        continue;
      if (best < 0 || ra_cand[i].uses > ra_cand[best].uses)
        best = i;
    }
    if (best < 0)
      break;
    ra_cand[best].var->reg = seq[k];
    if (seq[k] <= 7)
      hi = seq[k]; // highest data register (address regs tracked via ->reg)
  }
  return hi;
}

// ---------------------------------------------------------------------------
// OP6 (Tier F): global optimizations over the IR + CFG.  Lands -O3: everything
// here is gated on opt_level >= 3 (see ir_emit_body), so -O0/-O1/-O2 never run
// any of it and stay byte-identical to the single-pass baseline.
//
// v1 is the catalog-#12 cluster that needs no new value storage and is strictly
// <= the -O2 instruction count: constant folding + propagation, algebraic
// identities (including the x+x -> x<<1 same-operand reduction), constant-
// condition branch folding, and dead-code elimination (unreachable blocks +
// trivially-pure eval statements).  CSE-with-materialization, cross-block copy
// propagation, LICM and induction-variable strength reduction (#12 CSE and all
// of #13) need somewhere to keep a reused value live across blocks -- i.e. the
// OP7 whole-function allocator ("allocate after global opts") -- so they co-land
// there; doing them here would spill to the stack and pessimize without it.
// ---------------------------------------------------------------------------

// A bare integer constant (kids already folded) -> its 32-bit value.
static bool ie_num(IrExpr *e, int32_t *out) {
  if (e && e->kind == IE_NUM) {
    *out = (int32_t)e->val;
    return true;
  }
  return false;
}

static IrExpr *ie_num_new(int32_t v, int size, bool uns) {
  IrExpr *e = ie_new(IE_NUM);
  e->val = v;
  e->size = size ? size : 4;
  e->uns = uns;
  return e;
}

// No observable side effect: no call, store or memzero anywhere.  A duplicate
// evaluation may then be dropped (x + x -> x << 1) since the value is unchanged.
static bool ie_pure(IrExpr *e) {
  if (!e)
    return true;
  if (e->kind == IE_CALL || e->kind == IE_STORE || e->kind == IE_MEMZERO)
    return false;
  if (!ie_pure(e->a) || !ie_pure(e->b) || !ie_pure(e->c))
    return false;
  for (int i = 0; i < e->nargs; i++)
    if (!ie_pure(e->args[i]))
      return false;
  return true;
}

// Pure AND touches no memory at all (also no load): the whole expression may be
// dropped (x * 0 -> 0, dead pure eval) without deleting an observable read.
static bool ie_trivial(IrExpr *e) {
  if (!e)
    return true;
  switch (e->kind) {
  case IE_LOAD:
  case IE_STORE:
  case IE_CALL:
  case IE_MEMZERO:
    return false;
  }
  if (!ie_trivial(e->a) || !ie_trivial(e->b) || !ie_trivial(e->c))
    return false;
  for (int i = 0; i < e->nargs; i++)
    if (!ie_trivial(e->args[i]))
      return false;
  return true;
}

// Structural equality (bounded to the shapes the x + x reduction cares about;
// anything else is conservatively unequal).
static bool ie_equal(IrExpr *x, IrExpr *y) {
  if (x == y)
    return true;
  if (!x || !y || x->kind != y->kind)
    return false;
  switch (x->kind) {
  case IE_NUM:
    return x->val == y->val;
  case IE_LEA:
    return x->var == y->var && x->is_local == y->is_local && x->off == y->off &&
           (x->name == y->name ||
            (x->name && y->name && !strcmp(x->name, y->name)));
  case IE_LOAD:
    return x->size == y->size && x->uns == y->uns && ie_equal(x->a, y->a);
  case IE_CAST:
    return x->op == y->op && x->size == y->size && x->uns == y->uns &&
           x->fsize == y->fsize && x->funs == y->funs && ie_equal(x->a, y->a);
  case IE_UNARY:
    return x->op == y->op && ie_equal(x->a, y->a);
  case IE_BINOP:
    return x->op == y->op && x->size == y->size && x->uns == y->uns &&
           ie_equal(x->a, y->a) && ie_equal(x->b, y->b);
  }
  return false;
}

// Fold a constant binop to its 32-bit result, matching the m68k width and
// signedness the tiler would emit.  Forms the tiler would not fold (division or
// shift UB) are left alone so behaviour is identical.
static bool fold_binop(int op, bool uns, int32_t a, int32_t b, int32_t *out) {
  uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
  switch (op) {
  case ND_ADD: *out = (int32_t)(ua + ub); return true;
  case ND_SUB: *out = (int32_t)(ua - ub); return true;
  case ND_MUL: *out = (int32_t)(ua * ub); return true;
  case ND_BITAND: *out = (int32_t)(ua & ub); return true;
  case ND_BITOR: *out = (int32_t)(ua | ub); return true;
  case ND_BITXOR: *out = (int32_t)(ua ^ ub); return true;
  case ND_DIV:
    if (b == 0)
      return false; // runtime UB: leave the divide for codegen
    if (uns) { *out = (int32_t)(ua / ub); return true; }
    if (a == (-2147483647 - 1) && b == -1)
      return false; // signed overflow: leave it
    *out = a / b;
    return true;
  case ND_MOD:
    if (b == 0)
      return false;
    if (uns) { *out = (int32_t)(ua % ub); return true; }
    if (a == (-2147483647 - 1) && b == -1)
      return false;
    *out = a % b;
    return true;
  case ND_SHL:
    if (b < 0 || b > 31)
      return false;
    *out = (int32_t)(ua << b);
    return true;
  case ND_SHR:
    if (b < 0 || b > 31)
      return false;
    *out = uns ? (int32_t)(ua >> b) : (a >> b);
    return true;
  case ND_EQ: *out = a == b; return true;
  case ND_NE: *out = a != b; return true;
  case ND_LT: *out = uns ? (ua < ub) : (a < b); return true;
  case ND_LE: *out = uns ? (ua <= ub) : (a <= b); return true;
  }
  return false;
}

// x + x -> x << 1 (x already known pure, so evaluating it once is enough).
static IrExpr *ie_shl1(IrExpr *e, IrExpr *x) {
  IrExpr *s = ie_new(IE_BINOP);
  s->op = ND_SHL;
  s->a = x;
  s->b = ie_num_new(1, 4, false);
  s->size = e->size;
  s->uns = e->uns;
  return s;
}

// Algebraic identities for a binop with at most one constant operand.
static IrExpr *ie_binop_identity(IrExpr *e) {
  int32_t ca, cb;
  bool a_const = ie_num(e->a, &ca), b_const = ie_num(e->b, &cb);

  switch (e->op) {
  case ND_ADD:
    if (b_const && cb == 0) return e->a;
    if (a_const && ca == 0) return e->b;
    if (ie_pure(e->a) && ie_equal(e->a, e->b)) return ie_shl1(e, e->a);
    break;
  case ND_SUB:
    if (b_const && cb == 0) return e->a;
    break;
  case ND_MUL:
    if (b_const && cb == 1) return e->a;
    if (a_const && ca == 1) return e->b;
    if (b_const && cb == 0 && ie_trivial(e->a)) return ie_num_new(0, e->size, e->uns);
    if (a_const && ca == 0 && ie_trivial(e->b)) return ie_num_new(0, e->size, e->uns);
    break;
  case ND_BITAND:
    if (b_const && cb == -1) return e->a;
    if (a_const && ca == -1) return e->b;
    if (b_const && cb == 0 && ie_trivial(e->a)) return ie_num_new(0, e->size, e->uns);
    if (a_const && ca == 0 && ie_trivial(e->b)) return ie_num_new(0, e->size, e->uns);
    break;
  case ND_BITOR:
    if (b_const && cb == 0) return e->a;
    if (a_const && ca == 0) return e->b;
    break;
  case ND_BITXOR:
    if (b_const && cb == 0) return e->a;
    if (a_const && ca == 0) return e->b;
    break;
  case ND_SHL:
  case ND_SHR:
    if (b_const && cb == 0) return e->a;
    break;
  }
  return e;
}

// Bottom-up constant folding + identity/propagation over an IR expression tree.
static IrExpr *ie_fold(IrExpr *e) {
  if (!e)
    return e;
  e->a = ie_fold(e->a);
  e->b = ie_fold(e->b);
  e->c = ie_fold(e->c);
  for (int i = 0; i < e->nargs; i++)
    e->args[i] = ie_fold(e->args[i]);

  int32_t ca, cb, r;
  switch (e->kind) {
  case IE_CAST:
    if (ie_num(e->a, &ca)) {
      if (e->op == TY_BOOL)
        return ie_num_new(ca != 0, e->size, e->uns);
      if (tk_is_integer(e->op)) {
        int32_t v = ca;
        if (e->size == 1) v = e->uns ? (uint8_t)ca : (int8_t)ca;
        else if (e->size == 2) v = e->uns ? (uint16_t)ca : (int16_t)ca;
        return ie_num_new(v, e->size, e->uns);
      }
    }
    return e;
  case IE_UNARY:
    if (ie_num(e->a, &ca)) {
      if (e->op == ND_NEG) return ie_num_new((int32_t)(0u - (uint32_t)ca), e->size, e->uns);
      if (e->op == ND_BITNOT) return ie_num_new(~ca, e->size, e->uns);
      if (e->op == ND_NOT) return ie_num_new(ca == 0, 4, false);
    }
    return e;
  case IE_BINOP:
    if (ie_num(e->a, &ca) && ie_num(e->b, &cb)) {
      if (fold_binop(e->op, e->uns, ca, cb, &r))
        return ie_num_new(r, e->size, e->uns);
      return e; // e.g. division by zero: leave the op for codegen
    }
    return ie_binop_identity(e);
  case IE_COND:
    if (ie_num(e->a, &ca))
      return ca != 0 ? e->b : e->c; // constant condition: pick a branch
    return e;
  case IE_LOGAND:
    if (ie_num(e->a, &ca)) {
      if (ca == 0) return ie_num_new(0, 4, false);        // 0 && x -> 0
      if (ie_num(e->b, &cb)) return ie_num_new(cb != 0, 4, false);
    }
    return e;
  case IE_LOGOR:
    if (ie_num(e->a, &ca)) {
      if (ca != 0) return ie_num_new(1, 4, false);        // 1 || x -> 1
      if (ie_num(e->b, &cb)) return ie_num_new(cb != 0, 4, false);
    }
    return e;
  }
  return e;
}

// Fold every expression tree in place.
static void op6_fold_exprs(void) {
  for (int i = 0; i < nitems; i++)
    if (items[i].e)
      items[i].e = ie_fold(items[i].e);
}

// A conditional branch on a now-constant condition becomes unconditional (when
// taken) or vanishes (when not).  The folded condition is a bare constant with
// no side effect, so dropping it is safe.
static void op6_fold_branches(void) {
  for (int i = 0; i < nitems; i++) {
    if (items[i].kind != II_CBR)
      continue;
    int32_t c;
    if (!ie_num(items[i].e, &c))
      continue;
    bool taken = (c != 0) == items[i].when;
    items[i].kind = taken ? II_JMP : II_NOP;
    items[i].e = NULL;
  }
}

// Mark every item in a block unreachable from the entry as removed.
static void op6_dce_unreachable(void) {
  if (nblocks == 0)
    return;
  bool *seen = calloc(nblocks, 1);
  int *stack = calloc(nblocks, sizeof(int));
  int sp = 0;
  seen[0] = true;
  stack[sp++] = 0;
  while (sp) {
    int b = stack[--sp];
    for (int s = 0; s < blocks[b].nsucc; s++)
      if (!seen[blocks[b].succ[s]]) {
        seen[blocks[b].succ[s]] = true;
        stack[sp++] = blocks[b].succ[s];
      }
  }
  for (int b = 0; b < nblocks; b++)
    if (!seen[b])
      for (int i = blocks[b].start; i < blocks[b].end; i++) {
        items[i].kind = II_NOP;
        items[i].e = NULL;
      }
  free(seen);
  free(stack);
}

// Drop expression statements that compute nothing observable.
static void op6_dce_dead_eval(void) {
  for (int i = 0; i < nitems; i++)
    if (items[i].kind == II_EVAL && ie_trivial(items[i].e)) {
      items[i].kind = II_NOP;
      items[i].e = NULL;
    }
}

static void ir_optimize(void) {
  op6_fold_exprs();
  op6_fold_branches();
  ir_build_cfg(); // branch folding changed the CFG edges
  op6_dce_unreachable();
  op6_dce_dead_eval();
}

// ---------------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------------

bool ir_emit_body(Obj *fn) {
  bool stats = getenv("C68K_IR_STATS") != NULL;

  if (!stmt_ok(fn->body)) {
    if (stats)
      fprintf(stderr, "ir: %-24s fallback\n", fn->name);
    return false;
  }

  nitems = 0;
  ir_fn = fn;
  lower_stmt(fn->body);
  ir_build_cfg();

  // OP6 (-O3): global optimizations over the IR + CFG. Gated here so -O0/-O1/-O2
  // keep the exact single-pass-parity output; only -O3 diverges.
  if (opt_level >= 3)
    ir_optimize();

  // Emit block by block in layout order (the CFG's first consumer).
  for (int b = 0; b < nblocks; b++)
    for (int i = blocks[b].start; i < blocks[b].end; i++)
      ir_emit_item(&items[i]);

  if (stats) {
    int edges = 0;
    for (int b = 0; b < nblocks; b++)
      edges += blocks[b].nsucc;
    fprintf(stderr, "ir: %-24s ok  items=%d blocks=%d edges=%d\n", fn->name,
            nitems, nblocks, edges);
  }
  return true;
}
