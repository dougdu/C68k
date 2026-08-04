/*
 * regalloc.c -- c68k register-allocation (OP5/OP7) correctness battery.
 *
 * Exercises the code paths the whole-function allocators change: sequential
 * loops with disjoint index ranges (OP7's -O3 interference sharing packs their
 * indices into one register), simultaneously-live accumulators (all interfere,
 * so they take distinct registers and overflow to memory), nested loops,
 * pointer loops, and break/continue (chibicc lowers these to goto; V3's CFG
 * dataflow liveness now allocates through those structured edges).  Every result
 * is
 * checked against a closed form, so a mis-shared register would corrupt a sum
 * and fail here on real hardware at whatever -O level the harness built.
 * Self-checking: prints "REGALLOC PASS n/n".  Name kept <= 8 chars for CP/M 8.3.
 */
#include <stdio.h>

static int passes = 0;
static int total = 0;

static void chk(int cond, int id) {
  total++;
  if (cond)
    passes++;
  else
    printf("FAIL: check %d\n", id);
}

/* Ten sequential loops with disjoint index variables plus one accumulator = 11
 * candidates, one more than the D2-D7/A2-A5 pool. At -O3 OP7 shares a single
 * register across the ten disjoint index ranges; at -O2 OP5 gives each its own
 * and overflows the last to memory. Same result either way.
 * Sum of weights 1+2+3+5+7+11+13+17+19+23 = 101, so = 101 * n(n-1)/2. */
static int manyloops(int n) {
  int s = 0;
  for (int a = 0; a < n; a++) s += a;
  for (int b = 0; b < n; b++) s += b * 2;
  for (int c = 0; c < n; c++) s += c * 3;
  for (int d = 0; d < n; d++) s += d * 5;
  for (int e = 0; e < n; e++) s += e * 7;
  for (int f = 0; f < n; f++) s += f * 11;
  for (int g = 0; g < n; g++) s += g * 13;
  for (int h = 0; h < n; h++) s += h * 17;
  for (int i = 0; i < n; i++) s += i * 19;
  for (int j = 0; j < n; j++) s += j * 23;
  return s;
}

/* Two disjoint loops whose accumulators (p, q) are both live at the return, so
 * p and q interfere and take distinct registers while the loop indices a and b
 * -- disjoint -- share one. */
static int twouse(int n) {
  int p = 0, q = 0;
  for (int a = 0; a < n; a++) p += a;
  for (int b = 0; b < n; b++) q += b * 2;
  return p * 100 + q;
}

/* Twelve simultaneously-live accumulators: all interfere, so OP7 reproduces the
 * OP5 assignment (distinct registers, the surplus overflowing to memory).
 * = 12 * n(n-1)/2. */
static int manyacc(int n) {
  int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
  int g = 0, h = 0, i = 0, j = 0, k = 0, l = 0;
  for (int t = 0; t < n; t++) {
    a += t; b += t; c += t; d += t; e += t; f += t;
    g += t; h += t; i += t; j += t; k += t; l += t;
  }
  return a + b + c + d + e + f + g + h + i + j + k + l;
}

/* Nested loops: i and j are live together, so they interfere. = (n(n-1)/2)^2. */
static int nested(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      s += i * j;
  return s;
}

/* break/continue -> goto: V3 allocates through these structured edges (before
 * V3 they forced the OP5 whole-function fallback). s is live across both the
 * continue and break edges, so it interferes with i and keeps its own reg. */
static int withbreak(int n) {
  int s = 0;
  for (int i = 0; i < n; i++) {
    if (i == 5)
      continue;
    if (i == 12)
      break;
    s += i;
  }
  return s;
}

/* Two sequential loops that each contain a continue and a break.  Their index
 * ranges (a, then b) are disjoint, so V3's interference allocator shares ONE
 * register across them through the break/continue edges, while the accumulator s
 * -- live across both loops -- keeps a distinct register.  A register mis-shared
 * across a continue/break edge would corrupt the sum and fail the closed-form
 * check on real hardware. */
static int breakshare(int n) {
  int s = 0;
  for (int a = 0; a < n; a++) {
    if (a == 4)
      continue; /* skip 4 */
    if (a == 9)
      break; /* stop before 9 */
    s += a;
  }
  for (int b = 0; b < n; b++) {
    if (b == 2)
      continue; /* skip 2 */
    if (b == 7)
      break; /* stop before 7 */
    s += b * 10;
  }
  return s;
}

/* Pointer loop: a promoted pointer parameter lives in an address register. */
static int psum(int *p, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

int main(void) {
  chk(manyloops(0) == 0, 1);
  chk(manyloops(1) == 0, 2);
  chk(manyloops(10) == 101 * 45, 3);      /* 45 = 10*9/2 */
  chk(manyloops(20) == 101 * 190, 4);     /* 190 = 20*19/2 */

  chk(twouse(10) == 45 * 100 + 90, 5);    /* p=45, q=90 */
  chk(twouse(0) == 0, 6);

  chk(manyacc(10) == 12 * 45, 7);         /* 540 */
  chk(manyacc(100) == 12 * 4950, 8);

  chk(nested(5) == 100, 9);               /* (10)^2 */
  chk(nested(1) == 0, 10);

  chk(withbreak(20) == 61, 11);           /* 0..11 minus 5 */
  chk(withbreak(3) == 3, 12);             /* 0+1+2 */

  chk(breakshare(20) == 222, 15);         /* (0+1+2+3+5+6+7+8) + 10*(0+1+3+4+5+6) */
  chk(breakshare(3) == 13, 16);           /* (0+1+2) + 10*(0+1) */

  {
    int arr[8];
    for (int i = 0; i < 8; i++)
      arr[i] = i * i;
    chk(psum(arr, 8) == 0 + 1 + 4 + 9 + 16 + 25 + 36 + 49, 13);
    chk(psum(arr, 0) == 0, 14);
  }

  printf("REGALLOC PASS %d/%d\n", passes, total);
  return 0;
}
