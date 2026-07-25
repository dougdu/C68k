#ifndef _MATH_H
#define _MATH_H

#include <errno.h>

/*
 * <math.h> over the soft-float runtime (IEEE-754 libm). The runtime exposes
 * the double primitives as the `d`-suffixed names (sind, cosd, ...) and the
 * single primitives as the `f`-suffixed names (sinf, cosf, ...).  The C double
 * names below are thin `static` inline wrappers over the `d` kernels; the
 * `f` names (further down) bind straight to the single kernels.
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
extern double asind(double);
extern double acosd(double);

/* Value/classification helpers used by the errno-setting wrappers below (also
 * declared with the other IEEE utilities further down; a redundant but legal
 * re-declaration). */
extern double __huge_val(void);
extern double __nan_val(void);
extern int __isinf(double);
extern int __isnan(double);
extern int __isfinite(double);

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
static double exp(double x) {
  double r = expd(x);
  if (__isinf(r) && __isfinite(x))
    errno = ERANGE; /* overflow */
  return r;
}
static double log(double x) {
  if (x < 0.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (x == 0.0) {
    errno = ERANGE;
    return -__huge_val();
  }
  return logd(x);
}
static double log10(double x) {
  if (x < 0.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (x == 0.0) {
    errno = ERANGE;
    return -__huge_val();
  }
  return logd(x) / M_LN10;
}
static double sqrt(double x) {
  if (x < 0.0) {
    errno = EDOM;
    return __nan_val();
  }
  return sqrtd(x);
}
static double pow(double b, double e) {
  double r = powd(b, e);
  if (__isnan(r) && !__isnan(b) && !__isnan(e))
    errno = EDOM;
  else if (__isinf(r) && __isfinite(b) && __isfinite(e))
    errno = ERANGE;
  return r;
}
static double floor(double x) { return floord(x); }
static double ceil(double x) { return ceild(x); }
static double fabs(double x) { return fabsd(x); }
static double fmod(double a, double b) {
  if (b == 0.0) {
    errno = EDOM;
    return __nan_val();
  }
  return fmodd(a, b);
}
static double modf(double x, double *ip) { return modfd(x, ip); }

static double atan2(double y, double x) {
  if (x > 0.0)
    return atand(y / x);
  if (x < 0.0)
    return atand(y / x) + (y >= 0.0 ? M_PI : -M_PI);
  return y > 0.0 ? M_PI / 2.0 : (y < 0.0 ? -M_PI / 2.0 : 0.0);
}

/* asin/acos bind to libm's native double kernels (cancellation-safe
   s = sqrt((1-|x|)(1+|x|)); ~1-2 ULP).  They stay `static` because libm's plain
   `_asin` is the SINGLE-precision arcsine, so a C double `asin` must route to
   the d-suffixed `_asind`/`_acosd` instead of colliding with it. */
static double asin(double x) {
  if (x < -1.0 || x > 1.0) {
    errno = EDOM;
    return __nan_val();
  }
  return asind(x);
}
static double acos(double x) {
  if (x < -1.0 || x > 1.0) {
    errno = EDOM;
    return __nan_val();
  }
  return acosd(x);
}

/* ------------------------------------------------------------------------
 * C99 `float` variants.  The single-precision kernels are the `s`-suffixed
 * libm exports (`sqrts`/`exps`/...).  The C99 `f` names with a domain/range
 * error are `static` inline wrappers that set EDOM/ERANGE (mirroring the double
 * wrappers above) and then dispatch to the single kernel, so `float` math runs
 * 32-bit soft-float with conforming errno instead of promoting to double.  The
 * no-error names are trivial pass-throughs over their kernels; tan/log10/atan2
 * are composed over the wrappers (log10f inherits logf's errno).
 * ------------------------------------------------------------------------ */
extern float sqrts(float);
extern float exps(float);
extern float logs(float);
extern float pows(float, float);
extern float fmods(float, float);
extern float asins(float);
extern float acoss(float);
extern float sins(float);
extern float coss(float);
extern float atans(float);
extern float floors(float);
extern float ceils(float);
extern float fabss(float);
extern float modfs(float, float *);
extern float fmas(float, float, float);

static float sqrtf(float x) {
  if (x < 0.0f) {
    errno = EDOM;
    return (float)__nan_val();
  }
  return sqrts(x);
}
static float expf(float x) {
  float r = exps(x);
  if (__isinf((double)r) && __isfinite((double)x))
    errno = ERANGE; /* overflow */
  return r;
}
static float logf(float x) {
  if (x < 0.0f) {
    errno = EDOM;
    return (float)__nan_val();
  }
  if (x == 0.0f) {
    errno = ERANGE;
    return -(float)__huge_val();
  }
  return logs(x);
}
static float powf(float b, float e) {
  float r = pows(b, e);
  if (__isnan((double)r) && !__isnan((double)b) && !__isnan((double)e))
    errno = EDOM;
  else if (__isinf((double)r) && __isfinite((double)b) && __isfinite((double)e))
    errno = ERANGE;
  return r;
}
static float fmodf(float a, float b) {
  if (b == 0.0f) {
    errno = EDOM;
    return (float)__nan_val();
  }
  return fmods(a, b);
}
static float asinf(float x) {
  if (x < -1.0f || x > 1.0f) {
    errno = EDOM;
    return (float)__nan_val();
  }
  return asins(x);
}
static float acosf(float x) {
  if (x < -1.0f || x > 1.0f) {
    errno = EDOM;
    return (float)__nan_val();
  }
  return acoss(x);
}

/* no domain/range error -- trivial pass-throughs over the single kernels */
static float sinf(float x) { return sins(x); }
static float cosf(float x) { return coss(x); }
static float atanf(float x) { return atans(x); }
static float floorf(float x) { return floors(x); }
static float ceilf(float x) { return ceils(x); }
static float fabsf(float x) { return fabss(x); }
static float modff(float x, float *ip) { return modfs(x, ip); }
static float fmaf(float x, float y, float z) { return fmas(x, y, z); }

/* composed over the wrappers; log10f inherits logf's EDOM/ERANGE */
static float tanf(float x) { return sinf(x) / cosf(x); }
static float log10f(float x) { return logf(x) / (float)M_LN10; }
static float atan2f(float y, float x) {
  if (x > 0.0f)
    return atanf(y / x);
  if (x < 0.0f)
    return atanf(y / x) + (y >= 0.0f ? (float)M_PI : -(float)M_PI);
  return y > 0.0f ? (float)M_PI_2 : (y < 0.0f ? -(float)M_PI_2 : 0.0f);
}

/* ------------------------------------------------------------------------
 * C99 `long double` variants.  `long double` == `double` on this target, so
 * each is a thin wrapper over the double version above.
 * ------------------------------------------------------------------------ */
static long double sinl(long double x) { return sin((double)x); }
static long double cosl(long double x) { return cos((double)x); }
static long double tanl(long double x) { return tan((double)x); }
static long double asinl(long double x) { return asin((double)x); }
static long double acosl(long double x) { return acos((double)x); }
static long double atanl(long double x) { return atan((double)x); }
static long double atan2l(long double y, long double x) {
  return atan2((double)y, (double)x);
}
static long double expl(long double x) { return exp((double)x); }
static long double logl(long double x) { return log((double)x); }
static long double log10l(long double x) { return log10((double)x); }
static long double sqrtl(long double x) { return sqrt((double)x); }
static long double powl(long double b, long double e) {
  return pow((double)b, (double)e);
}
static long double floorl(long double x) { return floor((double)x); }
static long double ceill(long double x) { return ceil((double)x); }
static long double fabsl(long double x) { return fabs((double)x); }
static long double fmodl(long double a, long double b) {
  return fmod((double)a, (double)b);
}
static long double modfl(long double x, long double *ip) {
  double i, r = modf((double)x, &i);
  *ip = i;
  return r;
}

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
