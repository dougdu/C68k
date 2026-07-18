// codegen68k.c --- c68k MC68000 code generator (Motorola syntax for asm68K).
//
// Replaces chibicc's x86-64 codegen.c. It mirrors chibicc's stack-machine
// model, retargeted to the 68000 and the c68k m68k C ABI:
//
//   * Accumulator = D0. The second operand of a binary op is popped into D1.
//     Temporaries spill to the stack via -(SP) / (SP)+.
//   * Addresses computed by gen_addr() are produced as a 32-bit VALUE in D0
//     (moved into A0/A1 by load()/store()).
//   * ABI (architecture.md 7.2): arguments pushed right-to-left on the stack,
//     caller cleans up (cdecl); integer/pointer return in D0; A6 frame via
//     LINK/UNLK; A7 = SP. The pure stack machine only touches the caller-saved
//     regs D0/D1/A0/A1, so no callee-saved MOVEM is needed yet (that arrives
//     with the register allocator in P12).
//
// Scope note: integer + pointer + control-flow + struct/call codegen, plus
// 64-bit `long long` and IEEE `float`/`double`. Operations the 68000 lacks are
// lowered to runtime-helper calls: 32/64-bit multiply/divide/shift to rt68k
// (__mulsi3, __muldi3, ...), and soft-float arithmetic/compare/convert to the
// IEEE754 library (libieee754d: _fpadd, _fpaddd, _fpltof, ...).

#include "chibicc.h"

static FILE *output_file;
static int depth;
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

__attribute__((format(printf, 1, 2)))
static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static int count(void) {
  static int i = 1;
  return i++;
}

// C symbols get a leading underscore in the emitted asm: no 68000 mnemonic
// begins with '_', so this avoids collisions (e.g. a C function named `add`
// vs the ADD instruction). Traditional m68k/CP/M-68K C ABI convention.
static char *sym(char *name) {
  return format("_%s", name);
}

// Round up `n` to the nearest multiple of `align`.
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// push D0 onto the stack (4 bytes).
static void push(void) {
  println("  move.l d0,-(sp)");
  depth++;
}

// pop the stack top into `reg` (a data or address register spelling).
static void pop(char *reg) {
  println("  move.l (sp)+,%s", reg);
  depth--;
}

// push a 64-bit value D0:D1 (D0=high) so the high longword lands at the lower
// address (big-endian, matching the m68k C ABI for an 8-byte stack argument).
static void push64(void) {
  println("  move.l d1,-(sp)");
  println("  move.l d0,-(sp)");
  depth += 2;
}

// pop a 64-bit value: high longword (at the lower address) first.
static void pop64(char *hi, char *lo) {
  println("  move.l (sp)+,%s", hi);
  println("  move.l (sp)+,%s", lo);
  depth -= 2;
}

// Materialize an integer constant into D0 (MOVEQ when it fits a signed byte).
static void load_imm(int64_t val) {
  if (val >= -128 && val <= 127)
    println("  moveq #%ld,d0", val);
  else
    println("  move.l #%ld,d0", val);
}

// Materialize a 64-bit integer constant into D0:D1 (D0=high, D1=low).
static void load_imm64(int64_t val) {
  uint64_t u = (uint64_t)val;
  println("  move.l #$%08lx,d0", (unsigned long)(uint32_t)(u >> 32));
  println("  move.l #$%08lx,d1", (unsigned long)(uint32_t)u);
}

// Materialize a floating constant as its IEEE-754 bit pattern: D0 for float,
// D0:D1 (D0=high) for double. long double == double in this ILP32 model.
static void load_fval(Node *node) {
  if (node->ty->kind == TY_FLOAT) {
    union { float f; uint32_t u; } v;
    v.f = (float)node->fval;
    println("  move.l #$%08lx,d0", (unsigned long)v.u);
  } else {
    union { double d; uint64_t u; } v;
    v.d = (double)node->fval;
    println("  move.l #$%08lx,d0", (unsigned long)(uint32_t)(v.u >> 32));
    println("  move.l #$%08lx,d1", (unsigned long)(uint32_t)v.u);
  }
}

// Compute the address of an lvalue node into D0.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local / parameter: A6-relative.
      println("  lea %d(a6),a0", node->var->offset);
      println("  move.l a0,d0");
      return;
    }
    // Global / function: PC-relative address (in-module) for position
    // independence; the assembler/linker relaxes the reference.
    println("  lea %s(pc),a0", sym(node->var->name));
    println("  move.l a0,d0");
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    if (node->member->offset)
      println("  add.l #%d,d0", node->member->offset);
    return;
  case ND_ASSIGN:
  case ND_COND:
  case ND_FUNCALL:
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
      gen_expr(node);
      return;
    }
    break;
  }
  error_tok(node->tok, "not an lvalue");
}

// Load a value of type `ty` from the address currently in D0, leaving the
// value in D0 (sign/zero-extended to 32 bits for sub-int scalars).
static void load(Type *ty) {
  switch (ty->kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
  case TY_VLA:
    // Aggregates/functions evaluate to their address (already in D0).
    return;
  }

  println("  movea.l d0,a0");

  if (ty->kind == TY_FLOAT) {
    println("  move.l (a0),d0");
    return;
  }
  if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
    println("  move.l (a0),d0");
    println("  move.l 4(a0),d1");
    return;
  }

  if (ty->size == 1) {
    if (ty->is_unsigned) {
      println("  moveq #0,d0");
      println("  move.b (a0),d0");
    } else {
      println("  move.b (a0),d0");
      println("  ext.w d0");
      println("  ext.l d0");
    }
  } else if (ty->size == 2) {
    if (ty->is_unsigned) {
      println("  moveq #0,d0");
      println("  move.w (a0),d0");
    } else {
      println("  move.w (a0),d0");
      println("  ext.l d0");
    }
  } else if (ty->size == 4) {
    println("  move.l (a0),d0");
  } else {
    // 8-byte scalar (long long): D0=high, D1=low (big-endian).
    println("  move.l (a0),d0");
    println("  move.l 4(a0),d1");
  }
}

// Store D0 to the address on top of the stack (which is popped).
static void store(Type *ty) {
  pop("a1");

  switch (ty->kind) {
  case TY_STRUCT:
  case TY_UNION:
    // Byte-wise struct copy: A1 = dst, D0 = src address.
    println("  movea.l d0,a0");
    for (int i = 0; i < ty->size; i++) {
      println("  move.b %d(a0),d1", i);
      println("  move.b d1,%d(a1)", i);
    }
    return;
  case TY_FLOAT:
    println("  move.l d0,(a1)");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:
    println("  move.l d0,(a1)");
    println("  move.l d1,4(a1)");
    return;
  }

  if (ty->size == 1)
    println("  move.b d0,(a1)");
  else if (ty->size == 2)
    println("  move.w d0,(a1)");
  else if (ty->size == 4)
    println("  move.l d0,(a1)");
  else {
    // 8-byte scalar (long long).
    println("  move.l d0,(a1)");
    println("  move.l d1,4(a1)");
  }
}

// Set the condition codes so a following Bcc/Scc tests "value != 0". May clobber
// D0/D1 (callers use this only where the value is not needed afterwards).
static void cmp_zero(Type *ty) {
  if (ty->kind == TY_FLOAT) {
    println("  andi.l #$7FFFFFFF,d0");   // Z set iff +/-0.0
    return;
  }
  if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
    println("  andi.l #$7FFFFFFF,d0");
    println("  or.l d1,d0");             // Z set iff +/-0.0
    return;
  }
  if (ty->size == 8) {
    println("  or.l d1,d0");             // Z set iff the 64-bit value is 0
    return;
  }
  println("  tst.l d0");
}

// Narrow/sign-adjust the 32-bit integer accumulator D0 to `to` (size <= 4).
static void cast_int_narrow(Type *to) {
  switch (to->size) {
  case 1:
    if (to->is_unsigned)
      println("  andi.l #255,d0");
    else {
      println("  ext.w d0");
      println("  ext.l d0");
    }
    return;
  case 2:
    if (to->is_unsigned)
      println("  andi.l #65535,d0");
    else
      println("  ext.l d0");
    return;
  default:
    return; // 4-byte: already a 32-bit accumulator
  }
}

// Push the accumulator (argsz bytes), call a soft-float runtime conversion,
// and clean the stack. Result comes back in D0 / D0:D1.
static void cast_call(int argsz, char *fn) {
  if (argsz == 8)
    push64();
  else
    push();
  println("  jsr %s", fn);
  println("  adda.w #%d,sp", argsz);
  depth -= argsz / 4;
}

// Cast the value in D0(:D1) from `from` to `to`. Integer widths, long long,
// and IEEE float/double (via the soft-float runtime, libieee754d).
static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  sne d0");
    println("  andi.l #1,d0");
    return;
  }

  bool ff = is_flonum(from), tf = is_flonum(to);
  bool fd = from->kind == TY_DOUBLE || from->kind == TY_LDOUBLE;
  bool td = to->kind == TY_DOUBLE || to->kind == TY_LDOUBLE;

  if (ff && tf) {                       // float <-> double
    if (fd && !td)
      cast_call(8, "_fpdtof");           // double -> float
    else if (!fd && td)
      cast_call(4, "_fpftod");           // float -> double
    return;                             // same kind: nop
  }

  if (!ff && tf) {                      // integer -> float/double
    if (from->size == 8)
      error("long long -> float/double not yet supported (P3)");
    cast_call(4, td ? "_fpltod" : "_fpltof");
    return;
  }

  if (ff && !tf) {                      // float/double -> integer
    if (to->size == 8)
      error("float/double -> long long not yet supported (P3)");
    cast_call(fd ? 8 : 4, fd ? "_fpdtol" : "_fpftol");
    cast_int_narrow(to);
    return;
  }

  // Both integer. Guard the 64-bit paths with is_integer so an aggregate that
  // merely happens to be 8 bytes (e.g. char[8] decaying to a pointer, whose
  // value is a 32-bit address in D0) is not mistaken for a long long.
  bool from64 = is_integer(from) && from->size == 8;
  bool to64 = is_integer(to) && to->size == 8;
  if (to64) {
    if (from64)
      return;                           // 64 -> 64: nop
    // 32 -> 64: extend into D0 (high).
    println("  move.l d0,d1");
    if (from->is_unsigned) {
      println("  moveq #0,d0");
    } else {
      int c = count();
      println("  moveq #0,d0");
      println("  tst.l d1");
      println("  bpl L_sx_%d", c);
      println("  moveq #-1,d0");
      println("L_sx_%d:", c);
    }
    return;
  }
  if (from64)
    println("  move.l d1,d0");          // 64 -> <=32: keep low longword
  cast_int_narrow(to);
}

// Copy a struct/union whose address is in D0 onto the top of the stack,
// occupying align_to(size,4) bytes (struct byte 0 at the lowest address). A
// block copy is byte-order agnostic, so this is correct on big-endian m68k.
static void push_struct(Type *ty) {
  int sz = align_to(ty->size, 4);
  println("  suba.w #%d,sp", sz);
  depth += sz / 4;
  println("  movea.l d0,a1");
  for (int i = 0; i < ty->size; i++) {
    println("  move.b %d(a1),d1", i);
    println("  move.b d1,%d(sp)", i);
  }
}

// Push each call argument right-to-left; returns the total bytes pushed so the
// caller can clean the stack. Scalars occupy a 4-byte slot; a struct/union is
// copied as a block rounded up to a multiple of 4 (long long/double 8 -> P3).
static int push_args(Node *arg) {
  if (!arg)
    return 0;
  int rest = push_args(arg->next);

  gen_expr(arg);
  if (arg->ty->kind == TY_STRUCT || arg->ty->kind == TY_UNION) {
    push_struct(arg->ty);
    return rest + align_to(arg->ty->size, 4);
  }
  if (arg->ty->size == 8) {
    push64();                // long long or double (8-byte stack slot)
    return rest + 8;
  }
  push();
  return rest + 4;
}

// Float/double binary op -> C-ABI call into the soft-float runtime
// (libieee754d): push rhs then lhs (so lhs is arg1), result in D0 / D0:D1.
static void gen_flonum_binop(Node *node) {
  bool dbl = node->lhs->ty->kind != TY_FLOAT;   // double / long double
  int sz = dbl ? 8 : 4;
  char *suf = dbl ? "d" : "";

  gen_expr(node->rhs);
  if (dbl) push64(); else push();
  gen_expr(node->lhs);
  if (dbl) push64(); else push();

  bool cmp = false;
  switch (node->kind) {
  case ND_ADD: println("  jsr _fpadd%s", suf); break;
  case ND_SUB: println("  jsr _fpsub%s", suf); break;
  case ND_MUL: println("  jsr _fpmult%s", suf); break;
  case ND_DIV: println("  jsr _fpdiv%s", suf); break;
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    println("  jsr _fpcmp%s", suf); cmp = true; break;
  default: error_tok(node->tok, "unsupported float operator");
  }
  println("  adda.w #%d,sp", sz * 2);
  depth -= (sz * 2) / 4;

  if (cmp) {
    // _fpcmp sets N/Z (V=0) for (lhs - rhs); reuse the signed conditions.
    char *cc = node->kind == ND_EQ ? "seq" :
               node->kind == ND_NE ? "sne" :
               node->kind == ND_LT ? "slt" : "sle";
    println("  %s d0", cc);
    println("  andi.l #1,d0");
  }
}

// 64-bit long long binary op. add/sub/logical inline (addx/subx); mul/div/mod/
// shift/compare via the runtime helpers (rt68k). a = D0:D1, b = D2:D3.
static void gen_int64_binop(Node *node) {
  bool u = node->lhs->ty->is_unsigned;

  if (node->kind == ND_SHL || node->kind == ND_SHR) {
    gen_expr(node->rhs);                 // shift count
    if (node->rhs->ty->size == 8)
      println("  move.l d1,d0");          // count = low longword
    push();
    gen_expr(node->lhs);                 // value -> D0:D1
    pop("d2");                           // count -> D2
    if (node->kind == ND_SHL)
      println("  jsr __ashldi3");
    else
      println("  jsr %s", u ? "__lshrdi3" : "__ashrdi3");
    return;
  }

  gen_expr(node->rhs);
  push64();
  gen_expr(node->lhs);
  pop64("d2", "d3");

  switch (node->kind) {
  case ND_ADD:
    println("  add.l d3,d1");
    println("  addx.l d2,d0");
    return;
  case ND_SUB:
    println("  sub.l d3,d1");
    println("  subx.l d2,d0");
    return;
  case ND_BITAND:
    println("  and.l d3,d1");
    println("  and.l d2,d0");
    return;
  case ND_BITOR:
    println("  or.l d3,d1");
    println("  or.l d2,d0");
    return;
  case ND_BITXOR:
    println("  eor.l d3,d1");
    println("  eor.l d2,d0");
    return;
  case ND_MUL:
    println("  jsr __muldi3");
    return;
  case ND_DIV:
    println("  jsr %s", u ? "__udivdi3" : "__divdi3");
    return;
  case ND_MOD:
    println("  jsr %s", u ? "__umoddi3" : "__moddi3");
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE: {
    println("  jsr %s", u ? "__ucmpdi2" : "__cmpdi2");
    println("  tst.l d0");
    char *cc = node->kind == ND_EQ ? "seq" :
               node->kind == ND_NE ? "sne" :
               node->kind == ND_LT ? "slt" : "sle";
    println("  %s d0", cc);
    println("  andi.l #1,d0");
    return;
  }
  }
  error_tok(node->tok, "unsupported long long operator");
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    if (is_flonum(node->ty))
      load_fval(node);
    else if (node->ty->size == 8)
      load_imm64(node->val);
    else
      load_imm(node->val);
    return;
  case ND_MEMZERO: {
    // Zero-clear a local of node->var->ty->size bytes at A6+offset.
    int c = count();
    println("  lea %d(a6),a0", node->var->offset);
    println("  move.w #%d,d1", node->var->ty->size - 1);
    println("L_memzero_%d:", c);
    println("  clr.b (a0)+");
    println("  dbra d1,L_memzero_%d", c);
    return;
  }
  case ND_NEG:
    gen_expr(node->lhs);
    if (is_flonum(node->ty))
      println("  eori.l #$80000000,d0");   // toggle the IEEE sign bit
    else if (node->ty->size == 8) {
      println("  neg.l d1");
      println("  negx.l d0");
    } else
      println("  neg.l d0");
    return;
  case ND_VAR:
  case ND_MEMBER:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_COND: {
    int c = count();
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  beq L_else_%d", c);
    gen_expr(node->then);
    println("  bra L_end_%d", c);
    println("L_else_%d:", c);
    gen_expr(node->els);
    println("L_end_%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  seq d0");
    println("  andi.l #1,d0");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    println("  not.l d0");
    return;
  case ND_LOGAND: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  beq L_false_%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  beq L_false_%d", c);
    println("  moveq #1,d0");
    println("  bra L_end_%d", c);
    println("L_false_%d:", c);
    println("  moveq #0,d0");
    println("L_end_%d:", c);
    return;
  }
  case ND_LOGOR: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  bne L_true_%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  bne L_true_%d", c);
    println("  moveq #0,d0");
    println("  bra L_end_%d", c);
    println("L_true_%d:", c);
    println("  moveq #1,d0");
    println("L_end_%d:", c);
    return;
  }
  case ND_FUNCALL: {
    int bytes = push_args(node->args);

    // Struct/union return: pass the address of the caller-allocated result
    // buffer as a hidden leftmost argument (pushed last, so it lands at 8(a6)
    // in the callee). The callee copies the result there and returns the
    // pointer in D0, so the aggregate rvalue is its address as usual.
    if (node->ret_buffer) {
      println("  lea %d(a6),a0", node->ret_buffer->offset);
      println("  move.l a0,-(sp)");
      depth++;
      bytes += 4;
    }

    if (node->lhs->kind == ND_VAR && node->lhs->ty->kind == TY_FUNC) {
      println("  jsr %s", sym(node->lhs->var->name));
    } else {
      gen_expr(node->lhs);
      println("  movea.l d0,a0");
      println("  jsr (a0)");
    }

    if (bytes)
      println("  adda.w #%d,sp", bytes);
    depth -= bytes / 4; // the pushed argument slots are gone now

    // Narrow return values are already delivered sign/zero-extended in D0 by
    // the callee's epilogue convention; nothing to fix up here.
    return;
  }
  }

  if (!node->lhs || !node->lhs->ty)
    error("codegen68k: unhandled expr node kind %d", node->kind);

  if (is_flonum(node->lhs->ty)) {
    gen_flonum_binop(node);
    return;
  }
  if (node->lhs->ty->size == 8) {
    gen_int64_binop(node);
    return;
  }

  // Integer binary operators: evaluate rhs, push; evaluate lhs; pop rhs->D1.
  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("d1");

  switch (node->kind) {
  case ND_ADD:
    println("  add.l d1,d0");
    return;
  case ND_SUB:
    println("  sub.l d1,d0");
    return;
  case ND_MUL:
    // 68000 has no 32x32 multiply; use the runtime helper (d0*d1 -> d0).
    println("  jsr __mulsi3");
    return;
  case ND_DIV:
  case ND_MOD:
    if (node->ty->is_unsigned)
      println("  jsr %s", node->kind == ND_DIV ? "__udivsi3" : "__umodsi3");
    else
      println("  jsr %s", node->kind == ND_DIV ? "__divsi3" : "__modsi3");
    return;
  case ND_BITAND:
    println("  and.l d1,d0");
    return;
  case ND_BITOR:
    println("  or.l d1,d0");
    return;
  case ND_BITXOR:
    println("  eor.l d1,d0");
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE: {
    println("  cmp.l d1,d0"); // flags for (lhs - rhs)
    char *cc;
    bool u = node->lhs->ty->is_unsigned;
    if (node->kind == ND_EQ)
      cc = "seq";
    else if (node->kind == ND_NE)
      cc = "sne";
    else if (node->kind == ND_LT)
      cc = u ? "scs" : "slt";
    else
      cc = u ? "sls" : "sle";
    println("  %s d0", cc);
    println("  andi.l #1,d0");
    return;
  }
  case ND_SHL:
    println("  asl.l d1,d0");
    return;
  case ND_SHR:
    if (node->lhs->ty->is_unsigned)
      println("  lsr.l d1,d0");
    else
      println("  asr.l d1,d0");
    return;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  beq L_else_%d", c);
    gen_stmt(node->then);
    println("  bra L_end_%d", c);
    println("L_else_%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println("L_end_%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println("L_begin_%d:", c);
    if (node->cond) {
      gen_expr(node->cond);
      cmp_zero(node->cond->ty);
      println("  beq %s", node->brk_label);
    }
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    if (node->inc)
      gen_expr(node->inc);
    println("  bra L_begin_%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_DO: {
    int c = count();
    println("L_begin_%d:", c);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  bne L_begin_%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_SWITCH:
    gen_expr(node->cond);
    for (Node *n = node->case_next; n; n = n->case_next) {
      if (n->begin == n->end) {
        println("  cmp.l #%ld,d0", n->begin);
        println("  beq %s", n->label);
        continue;
      }
      // [GNU] case ranges
      println("  move.l d0,d1");
      println("  sub.l #%ld,d1", n->begin);
      println("  cmp.l #%ld,d1", n->end - n->begin);
      println("  bls %s", n->label);
    }
    if (node->default_case)
      println("  bra %s", node->default_case->label);
    println("  bra %s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return;
  case ND_CASE:
    println("%s:", node->label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_GOTO:
    println("  bra %s", node->unique_label);
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      Type *ty = node->lhs->ty;
      if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // Copy the result into the caller's buffer via the hidden pointer at
        // 8(a6), and hand that pointer back in D0.
        println("  movea.l d0,a0");
        println("  movea.l 8(a6),a1");
        for (int i = 0; i < ty->size; i++) {
          println("  move.b %d(a0),d1", i);
          println("  move.b d1,%d(a1)", i);
        }
        println("  move.l 8(a6),d0");
      }
    }
    println("  bra L_return_%s", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign A6-relative offsets: parameters at positive offsets (from A6+8),
// locals at negative offsets.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    // Parameters (all pass-by-stack). First arg sits at A6+8 (above the saved
    // A6 and return address). A narrow scalar occupies the low bytes of its
    // 4-byte slot, which on big-endian is the high address of the slot.
    int top = 8;

    // A struct/union-returning function receives a hidden pointer to the
    // result buffer as its first (leftmost) stack argument, at 8(a6); the
    // declared parameters follow it.
    Type *rty = fn->ty->return_ty;
    if (rty && (rty->kind == TY_STRUCT || rty->kind == TY_UNION))
      top += 4;

    for (Obj *var = fn->params; var; var = var->next) {
      int sz = var->ty->size;
      int slot = (sz <= 4) ? 4 : align_to(sz, 4);
      bool scalar = (var->ty->kind != TY_STRUCT && var->ty->kind != TY_UNION &&
                     var->ty->kind != TY_ARRAY);
      var->offset = (scalar && sz < slot) ? top + (slot - sz) : top;
      top += slot;
    }

    // Locals: negative offsets below A6.
    int bottom = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->offset)
        continue;
      bottom += var->ty->size;
      bottom = align_to(bottom, var->align < 2 ? 2 : var->align);
      var->offset = -bottom;
    }
    fn->stack_size = align_to(bottom, 2);
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    if (!var->is_static)
      println("  PUBLIC %s", sym(var->name));

    int align = var->align < 2 ? 2 : var->align;

    if (var->init_data) {
      println("  .data");
      println("  ALIGN %d", align);

      // asm68K data labels sit on the SAME line as their first DC/DS directive
      // (MASM-style, no colon), unlike code labels.
      bool first = true;
      Relocation *rel = var->rel;
      int pos = 0;
      while (pos < var->ty->size) {
        char *dir;
        if (rel && rel->offset == pos) {
          dir = format("DC.L %s%+ld", sym(*rel->label), rel->addend);
          rel = rel->next;
          pos += 4;
        } else {
          dir = format("DC.B %d", var->init_data[pos++]);
        }
        if (first) {
          println("%s %s", sym(var->name), dir);
          first = false;
        } else {
          println("  %s", dir);
        }
      }
      continue;
    }

    // Zero-initialized -> BSS.
    println("  .data?");
    println("  ALIGN %d", align);
    println("%s DS.B %d", sym(var->name), var->ty->size);
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;
    if (!fn->is_live)
      continue;

    if (!fn->is_static)
      println("  PUBLIC %s", sym(fn->name));

    println("  .code");
    println("%s:", sym(fn->name));
    current_fn = fn;

    // Prologue: set up the A6 frame and reserve locals.
    println("  link a6,#%d", -fn->stack_size);

    gen_stmt(fn->body);
    assert(depth == 0);

    // main() falling off the end returns 0.
    if (!strcmp(fn->name, "main"))
      println("  moveq #0,d0");

    // Epilogue.
    println("L_return_%s:", fn->name);
    println("  unlk a6");
    println("  rts");
  }
}

void codegen(Obj *prog, FILE *out) {
  output_file = out;

  println("  .model flat");
  // Runtime helpers the 68000 lacks as single instructions: 32/64-bit integer
  // mul/div/mod/shift/compare (rt68k) and IEEE soft-float (libieee754d).
  println("  EXTERN __mulsi3,__divsi3,__udivsi3,__modsi3,__umodsi3");
  println("  EXTERN __muldi3,__divdi3,__udivdi3,__moddi3,__umoddi3");
  println("  EXTERN __ashldi3,__ashrdi3,__lshrdi3,__cmpdi2,__ucmpdi2");
  println("  EXTERN _fpadd,_fpsub,_fpmult,_fpdiv,_fpcmp");
  println("  EXTERN _fpaddd,_fpsubd,_fpmultd,_fpdivd,_fpcmpd");
  println("  EXTERN _fpltof,_fpftol,_fpltod,_fpdtol,_fpftod,_fpdtof");

  // Import every global declared but not defined here (external functions like
  // memcpy, extern variables) so asm68K can resolve the references (A2006).
  for (Obj *var = prog; var; var = var->next)
    if (!var->is_local && !var->is_definition)
      println("  EXTERN %s", sym(var->name));

  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);

  println("  END");
}
