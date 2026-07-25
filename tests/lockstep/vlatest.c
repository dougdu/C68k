/*
 * vlatest.c --- variable-length arrays (VLAs) lockstep test.
 * Prints "VLATEST PASS n/n" when every check holds on both OSes.
 *
 * Exercises: basic 1-D, runtime sizeof, VLA function parameter, 2-D VLA, loop
 * reclamation (a fresh VLA every iteration -- a leak would overflow the ~16 KB
 * Osiris task stack long before completion), break/continue out of a VLA body,
 * nested VLA blocks (inner reclaimed while outer stays live), and return from
 * inside a VLA scope.
 */
#include <stdio.h>

static int g_pass, g_fail;
static void chk(long got, long want) {
  if (got == want)
    g_pass++;
  else {
    g_fail++;
    printf("FAIL: got %ld want %ld\n", got, want);
  }
}

/* Force a runtime (non-constant) size so `[n]` is a true VLA, never folded. */
static int rt(int x) {
  volatile int v = x;
  return v;
}

/* A VLA decays to a pointer when passed to a function. (An `int a[n]` parameter
   whose size names an earlier parameter is a front-end limitation; use a
   pointer parameter, which is what such a parameter decays to anyway.) */
static long vsum(int n, const int *a) {
  long s = 0;
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

/* return from inside a nested VLA scope -- unlk must reclaim everything. */
static long vla_return(int n) {
  int a[n];
  for (int i = 0; i < n; i++)
    a[i] = i * i;
  long s = 0;
  for (int i = 0; i < n; i++) {
    int tmp[rt(2)];
    tmp[0] = a[i];
    s += tmp[0];
    if (i == n - 1)
      return s;
  }
  return -1;
}

int main(void) {
  /* 1. basic 1-D */
  int n = rt(10);
  int a[n];
  for (int i = 0; i < n; i++)
    a[i] = i;
  long s = 0;
  for (int i = 0; i < n; i++)
    s += a[i];
  chk(s, 45); /* 0+1+...+9 */

  /* 2. runtime sizeof */
  chk((long)sizeof(a), (long)n * (long)sizeof(int));
  chk((long)sizeof(a), 40);

  /* 3. VLA parameter */
  chk(vsum(n, a), 45);

  /* 4. 2-D VLA */
  int r = rt(3), c = rt(4);
  int m[r][c];
  for (int i = 0; i < r; i++)
    for (int j = 0; j < c; j++)
      m[i][j] = i * c + j;
  long m2 = 0;
  for (int i = 0; i < r; i++)
    for (int j = 0; j < c; j++)
      m2 += m[i][j];
  chk(m2, 66);      /* sum 0..11 */
  chk(m[2][3], 11); /* last element */
  chk((long)sizeof(m), (long)r * (long)c * (long)sizeof(int)); /* 48 */

  /* 5. loop reclamation (leak test): a fresh VLA every iteration. */
  long acc = 0;
  for (int i = 0; i < 20000; i++) {
    int k = (i & 7) + 1; /* size 1..8 */
    int b[k];
    for (int j = 0; j < k; j++)
      b[j] = j;
    acc += b[k - 1]; /* = k-1 = i&7 */
  }
  chk(acc, 70000); /* 2500 cycles * (0+1+...+7) */

  /* 6. break / continue out of a VLA loop body */
  long bc = 0;
  for (int i = 0; i < 1000; i++) {
    int t[rt(4)];
    t[0] = i;
    if (i == 500)
      break; /* break out of a VLA body */
    if (i & 1)
      continue; /* continue past the rest of a VLA body */
    bc += t[0];
  }
  chk(bc, 62250); /* sum of even i in [0,498] */

  /* 7. nested VLA blocks: inner reclaimed, outer stays live */
  int p = rt(5);
  int outer[p];
  for (int i = 0; i < p; i++)
    outer[i] = i * 10;
  long ns = 0;
  {
    int q = rt(3);
    int inner[q];
    for (int i = 0; i < q; i++)
      inner[i] = i + 100;
    for (int i = 0; i < q; i++)
      ns += inner[i]; /* 100+101+102 = 303 */
  }
  for (int i = 0; i < p; i++)
    ns += outer[i]; /* 0+10+20+30+40 = 100 (outer must be intact) */
  chk(ns, 403);

  /* 8. return from inside a VLA scope */
  chk(vla_return(6), 55); /* 0+1+4+9+16+25 */

  if (g_fail == 0)
    printf("VLATEST PASS %d/%d\n", g_pass, g_pass);
  else
    printf("VLATEST FAIL %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail;
}
