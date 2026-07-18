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

static double sin(double x) { return sind(x); }
static double cos(double x) { return cosd(x); }
static double tan(double x) { return sind(x) / cosd(x); }
static double atan(double x) { return atand(x); }
static double exp(double x) { return expd(x); }
static double log(double x) { return logd(x); }
static double log10(double x) { return logd(x) / M_LN10; }
static double sqrt(double x) { return sqrtd(x); }
static double pow(double b, double e) { return powd(b, e); }
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

#endif /* _MATH_H */
