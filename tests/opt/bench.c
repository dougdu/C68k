// tests/opt/bench.c --- optimizer benchmark corpus (freestanding; no includes,
// so `c68k -S` measures pure codegen quality with no library noise).
//
// These are the functions analyzed in docs/codegen.md #10 plus a few extra
// integer/loop/array patterns. tools/opt-measure.ps1 records .text size +
// instruction count at -O0..-O3; the per-function transforms each optimizer
// phase (docs/optimization-plan.md OP1..OP7) targets are asserted by
// tools/opt-check.ps1. Keep this file freestanding and stable so the numbers
// are comparable across phases.

int g;
int arr[16];

int add3(int a, int b, int c) { return a + b + c; }

// sum_loop / idx / reuse: register-allocation + CSE + LICM targets (OP5/OP6).
int sum_loop(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += arr[i];
  return s;
}

int idx(int *p, int i) { return p[i] + p[i + 1]; }

int reuse(int a, int b) {
  int t = a * b;
  return t + t;
}

// cond / cmp_chain: condition-context branch fusion targets (OP3).
int cond(int x) { return x > 10 ? x * 2 : x + 1; }

int cmp_chain(int a, int b) {
  if (a < b && b < 100)
    return 1;
  return 0;
}

// muldiv: strength-reduction targets (OP2 #8) -- a*7 = (a<<3)-a, signed a/4.
long muldiv(int a) { return a * 7 + a / 4; }

// store_const / gset: direct-store-to-EA + constant-index folding (OP2 #5/#7).
void store_const(void) { g = 42; arr[0] = 0; }
int  gset(int v) { g = v; return g; }

// Already reduced at -O1 (const-right immediate select + pow2 strength
// reduction) -- the opt-check self-test cases.
int mul8(int x)   { return x * 8; }      // -> asl.l #3,d0
int addk(int x)   { return x + 5; }      // -> addq.l #5,d0
int ltk(int x)    { return x < 10; }     // -> cmp.l #10,d0
unsigned udiv4(unsigned x) { return x / 4; } // -> lsr.l #2,d0
