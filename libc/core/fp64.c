/*
 * fp64.c --- 64-bit integer <-> IEEE float/double conversions (the soft-float
 * runtime helpers the code generator emits by name: _fplltod, _fpdtoull, ...).
 * Carved out of libc.c (Phase 4b) into its own object so a program that does
 * 64-bit float conversion pulls only this, not all of <stdlib.h>. Built by
 * decomposition over the 32-bit conversions the back end supports plus IEEE
 * double arithmetic, so the result is correctly rounded without a dedicated
 * 64-bit float routine. Uses no library functions (pure arithmetic).
 */
#define _P32 4294967296.0                /* 2^32 */
#define _P31 2147483648.0                /* 2^31 */

/* uint32 -> double (the signed-only 32-bit primitive can't see bit 31). */
static double fp_u32d(unsigned long u) {
  if (u & 0x80000000UL)
    return (double)(long)(u & 0x7FFFFFFFUL) + _P31;
  return (double)(long)u;
}

/* double in [0, 2^32) -> uint32. */
static unsigned long fp_d32u(double d) {
  if (d >= _P31)
    return (unsigned long)(long)(d - _P31) + 0x80000000UL;
  return (unsigned long)(long)d;
}

/* c68k prefixes C symbols with '_', so `fplltod` here is the `_fplltod` the
 * code generator emits. */
double fpulltod(unsigned long long u) {
  unsigned long hi = (unsigned long)(u >> 32);
  unsigned long lo = (unsigned long)u;
  return fp_u32d(hi) * _P32 + fp_u32d(lo);
}

double fplltod(long long v) {
  if (v < 0)
    return -fpulltod(-(unsigned long long)v);
  return fpulltod((unsigned long long)v);
}

float fpulltof(unsigned long long u) { return (float)fpulltod(u); }
float fplltof(long long v) { return (float)fplltod(v); }

unsigned long long fpdtoull(double d) {
  unsigned long h = fp_d32u(d / _P32);   /* high 32 bits ~ floor(d / 2^32) */
  double lo = d - fp_u32d(h) * _P32;      /* remainder, nominally [0, 2^32) */
  if (lo < 0.0) {                         /* correct a 1-off from rounding   */
    h--;
    lo += _P32;
  } else if (lo >= _P32) {
    h++;
    lo -= _P32;
  }
  return ((unsigned long long)h << 32) | fp_d32u(lo);
}

long long fpdtoll(double d) {
  if (d < 0)
    return -(long long)fpdtoull(-d);
  return (long long)fpdtoull(d);
}

unsigned long long fpftoull(float f) { return fpdtoull((double)f); }
long long fpftoll(float f) { return fpdtoll((double)f); }
