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
// Scope note (P2): integer + pointer + control-flow + struct/call codegen.
// 32-bit multiply/divide the 68000 lacks go to runtime helpers (__mulsi3 etc.,
// provided in P3). Full 64-bit `long long` and soft-float go to helper calls in
// P3; here they error if reached.

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

// Materialize an integer constant into D0 (MOVEQ when it fits a signed byte).
static void load_imm(int64_t val) {
  if (val >= -128 && val <= 127)
    println("  moveq #%ld,d0", val);
  else
    println("  move.l #%ld,d0", val);
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
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
    error("float load not yet supported (P3)");
    return;
  }

  println("  movea.l d0,a0");

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
    error("8-byte load not yet supported (P3)");
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
  case TY_DOUBLE:
  case TY_LDOUBLE:
    error("float store not yet supported (P3)");
    return;
  }

  if (ty->size == 1)
    println("  move.b d0,(a1)");
  else if (ty->size == 2)
    println("  move.w d0,(a1)");
  else if (ty->size == 4)
    println("  move.l d0,(a1)");
  else
    error("8-byte store not yet supported (P3)");
}

// Set the condition codes from D0 relative to zero (for a following Bcc/Scc).
static void cmp_zero(Type *ty) {
  if (is_flonum(ty)) {
    error("float compare not yet supported (P3)");
    return;
  }
  println("  tst.l d0");
}

// Cast the value in D0 from `from` to `to` (integer subset).
static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  sne d0");
    println("  andi.l #1,d0");
    return;
  }

  if (is_flonum(from) || is_flonum(to)) {
    error("float cast not yet supported (P3)");
    return;
  }

  // Integer size/sign adjustment of the 32-bit accumulator.
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
  case 4:
    return; // already a 32-bit accumulator
  default:
    error("8-byte cast not yet supported (P3)");
    return;
  }
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
  if (arg->ty->size > 4)
    error_tok(arg->tok, "8-byte argument not yet supported (P3)");
  push();
  return rest + 4;
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
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
    error_tok(node->tok, "float arithmetic not yet supported (P3)");
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
  // Runtime helpers the 68000 lacks as single instructions (defined in the
  // runtime support library, P3).
  println("  EXTERN __mulsi3,__divsi3,__udivsi3,__modsi3,__umodsi3");

  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);

  println("  END");
}
