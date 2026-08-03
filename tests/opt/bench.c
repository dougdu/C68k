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

// OP6 (Tier F, -O3 only): constant folding, x+x -> x<<1 same-operand strength
// reduction, and dead-branch elimination. Byte-identical to -O2 below -O3.
int constfold(void)   { return 2 * 3 + 4; }          // -> moveq #10,d0
int dbl(int x)        { return x + x; }              // -> asl.l #1,d0
int deadbranch(int x) { if (0) return 99; return x; } // then-arm eliminated

// OP7 (Tier G, -O3 only): global register allocation. Ten sequential loops with
// disjoint index variables plus the accumulator = 11 candidates, one more than
// the D2-D7/A2-A5 pool. The OP5 local allocator (-O2) gives each its own register
// and overflows the last index var to memory (movem d2-d7/a2-a5). OP7 (-O3) sees
// the index ranges are disjoint and shares one register across all ten, so every
// index is promoted (movem d2-d3).
int manyloops(int n) {
  int s = 0;
  for (int a = 0; a < n; a++) s += a;
  for (int b = 0; b < n; b++) s += b * 2;
  for (int c = 0; c < n; c++) s += c * 3;
  for (int d = 0; d < n; d++) s += d * 5;
  for (int e = 0; e < n; e++) s += e * 7;
  for (int f = 0; f < n; f++) s += f * 11;
  for (int h = 0; h < n; h++) s += h * 13;
  for (int i = 0; i < n; i++) s += i * 17;
  for (int j = 0; j < n; j++) s += j * 19;
  for (int k = 0; k < n; k++) s += k * 23;
  return s;
}

