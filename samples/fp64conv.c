#include <stdio.h>

/* Exercise the 64-bit int <-> IEEE float/double conversions the code
 * generator emits (_fplltod/_fpulltod/_fpdtoll/_fpdtoull/... in libc). */
int main(void) {
  long long v[] = {0LL,        1LL,          -1LL,        42LL,      -42LL,
                   1000000000000LL,           -1000000000000LL,
                   4294967295LL,              4294967296LL,
                   4294967297LL,              9007199254740992LL /* 2^53 */};
  int n = sizeof(v) / sizeof(v[0]);
  int bad = 0;
  for (int i = 0; i < n; i++) {
    double d = (double)v[i];       /* int64 -> double        */
    long long r = (long long)d;    /* double -> int64         */
    int ok = (r == v[i]);
    if (!ok) bad++;
    printf("%lld => %lld %s\n", v[i], r, ok ? "ok" : "DIFF");
  }

  unsigned long long u = 0x8000000000000000ULL;   /* 2^63 (bit 63 set) */
  double du = (double)u;                           /* uint64 -> double  */
  unsigned long long ur = (unsigned long long)du;  /* double -> uint64  */
  printf("2^63: %llu => %llu %s\n", u, ur, (ur == u) ? "ok" : "DIFF");
  if (ur != u) bad++;

  printf("trunc: %lld %lld (want 3 -3)\n", (long long)3.9, (long long)-3.9);

  long long fv = 16777217LL;      /* 2^24 + 1 (inexact in float) */
  float f = (float)fv;
  printf("float 2^24+1 -> %lld (want 16777216)\n", (long long)f);

  printf(bad ? "FP64 FAIL\n" : "FP64 OK\n");
  return 0;
}
