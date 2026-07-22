#ifndef _MATH_H
#define _MATH_H

/*
 * <math.h> over the soft-float runtime (libieee754d). The runtime provides
 * double primitives under the `d`-suffixed names (sind, cosd, ...); the C
 * names here are `static` so a program's `sqrt`/`exp`/`log`/`fmod` never clash
 * with the runtime's own single-precision `_sqrt`/`_exp`/`_log`/`_fmod`, which
 * its double routines call internally.
 */

extern double sind(double);
extern double cosd(double);
extern double atand(double);
extern double expd(double);
extern double logd(double);
extern double sqrtd(double);
extern double powd(double, double);
extern double floord(double);
extern double ceild(double);
extern double fabsd(double);
extern double fmodd(double, double);
extern double modfd(double, double *);

#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.43429448190325182765
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

static double sin(double x) { return sind(x); }
static double cos(double x) { return cosd(x); }
static double tan(double x) { return sind(x) / cosd(x); }
static double atan(double x) { return atand(x); }
static double exp(double x) { return x < 0.0 ? 1.0 / expd(-x) : expd(x); }
static double log(double x) { return logd(x); }
static double log10(double x) { return logd(x) / M_LN10; }
static double sqrt(double x) { return sqrtd(x); }
static double pow(double b, double e) {
  /* Work around libm's exp bug: powd is wrong when the result is < 1, so
     compute b^e = 1 / b^(-e) in that case (the reciprocal has result > 1). */
  if (b > 0.0 && ((b > 1.0 && e < 0.0) || (b < 1.0 && e > 0.0)))
    return 1.0 / powd(b, -e);
  return powd(b, e);
}
static double floor(double x) { return floord(x); }
static double ceil(double x) { return ceild(x); }
static double fabs(double x) { return fabsd(x); }
static double fmod(double a, double b) { return fmodd(a, b); }
static double modf(double x, double *ip) { return modfd(x, ip); }

static double atan2(double y, double x) {
  if (x > 0.0)
    return atand(y / x);
  if (x < 0.0)
    return atand(y / x) + (y >= 0.0 ? M_PI : -M_PI);
  return y > 0.0 ? M_PI / 2.0 : (y < 0.0 ? -M_PI / 2.0 : 0.0);
}

static double asin(double x) { return atand(x / sqrtd(1.0 - x * x)); }
static double acos(double x) { return M_PI / 2.0 - asin(x); }

/* ------------------------------------------------------------------------
 * C99 additions.  The base transcendentals above stay `static` inline over
 * the soft-float kernels (their C names would collide with libm's internal
 * single-precision `_sqrt`/`_exp`/... symbols); everything below is a real
 * extern function or a macro, implemented in C in libc/core over the same
 * kernels -- see docs/c99-conformance.md.
 * ---------------------------------------------------------------------- */

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN 2147483647

#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERRNO /* no FP exception flags on this target */

extern double __huge_val(void);
extern double __nan_val(void);
#define HUGE_VAL __huge_val()
#define HUGE_VALF ((float)__huge_val())
#define HUGE_VALL __huge_val()
#define INFINITY __huge_val()
#define NAN __nan_val()

extern int __fpclassify(double);
extern int __signbit(double);
extern int __isinf(double);
extern int __isnan(double);
extern int __isfinite(double);
extern int __isnormal(double);
#define fpclassify(x) __fpclassify(x)
#define signbit(x) __signbit(x)
#define isinf(x) __isinf(x)
#define isnan(x) __isnan(x)
#define isfinite(x) __isfinite(x)
#define isnormal(x) __isnormal(x)
#define isgreater(x, y) ((x) > (y))
#define isgreaterequal(x, y) ((x) >= (y))
#define isless(x, y) ((x) < (y))
#define islessequal(x, y) ((x) <= (y))
#define islessgreater(x, y) ((x) < (y) || (x) > (y))
#define isunordered(x, y) (__isnan(x) || __isnan(y))

/* IEEE-754 utilities and integral rounding (libc/core/*.c). */
extern double copysign(double, double);
extern double fmax(double, double);
extern double fmin(double, double);
extern double fdim(double, double);
extern double nan(const char *);
extern double nextafter(double, double);
extern double nexttoward(double, long double);
extern double frexp(double, int *);
extern double ldexp(double, int);
extern double scalbn(double, int);
extern double scalbln(double, long);
extern double logb(double);
extern int ilogb(double);
extern double trunc(double);
extern double round(double);
extern double rint(double);
extern double nearbyint(double);
extern long lround(double);
extern long long llround(double);
extern long lrint(double);
extern long long llrint(double);
extern double fma(double, double, double);

/* Exponential, logarithmic, power, and hyperbolic (Phase 2b, libc/core). */
extern double exp2(double);
extern double expm1(double);
extern double log2(double);
extern double log1p(double);
extern double cbrt(double);
extern double hypot(double, double);
extern double sinh(double);
extern double cosh(double);
extern double tanh(double);
extern double asinh(double);
extern double acosh(double);
extern double atanh(double);
extern double remainder(double, double);
extern double remquo(double, double, int *);

/* Error and gamma functions (Phase 2c, libc/core). */
extern double erf(double);
extern double erfc(double);
extern double tgamma(double);
extern double lgamma(double);

#endif /* _MATH_H */
