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

typedef enum { II_LABEL, II_JMP, II_CBR, II_EVAL, II_RET } IiKind;

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

static IrExpr *build_expr(Node *node);

// Address-producing lowering (mirrors gen_addr for the supported lvalues).
static IrExpr *build_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR: {
    IrExpr *e = ie_new(IE_LEA);
    if (node->var->is_local) {
      e->is_local = true;
      e->off = node->var->offset;
    } else {
      e->is_local = false;
      e->name = node->var->name;
    }
    return e;
  }
  case ND_DEREF:
    return build_expr(node->lhs);
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
      IrExpr *e = ie_new(IE_LOAD);
      e->a = build_expr(node->lhs);
      e->size = node->ty->size;
      e->uns = node->ty->is_unsigned;
      return e;
    }
  case ND_ADDR:
    return build_addr(node->lhs);
  case ND_ASSIGN: {
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
  *ea = lea->is_local ? format("%d(a6)", lea->off) : cg_symref(lea->name);
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
  case IE_LOAD:
    if (ir_indexed_load(e))
      return;
    ir_expr(e->a);
    EMIT("  movea.l d0,a0");
    emit_load_from_a0(e->size, e->uns);
    return;
  case IE_STORE: {
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
