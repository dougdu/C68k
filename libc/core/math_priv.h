#ifndef MATH_PRIV_H
#define MATH_PRIV_H
/*
 * math_priv.h -- private support for the C math layer (libc/core).
 *
 * Implementation files include THIS instead of <math.h> so they do not drag in
 * the public header's `static` inline base wrappers (sin/cos/...), which c68k
 * would emit as dead weight into every math object.
 *
 * IEEE-754 double bit access: m68k is big-endian, so a double and an unsigned
 * long long overlay the same eight bytes with the sign in bit 63, the 11-bit
 * biased exponent in bits 62..52, and the 52-bit mantissa in bits 51..0
 * (bias 1023; biased 0 = zero/subnormal, 0x7FF = infinity/NaN).
 */
typedef union {
  double d;
  unsigned long long u;
} __dbl;

#define __DEXP(u) ((int)(((u) >> 52) & 0x7FFULL))
#define __DMANT(u) ((u) & 0x000FFFFFFFFFFFFFULL)
#define __DSIGN(u) ((int)((u) >> 63))

/* Classification results -- keep in sync with <math.h>. */
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN 2147483647

/* libm double kernels used by the C math layer. */
extern double floord(double);
extern double ceild(double);
extern double fabsd(double);
extern double sqrtd(double);
extern double expd(double);
extern double logd(double);
extern double powd(double, double);
extern double fmodd(double, double);

/* Internal helpers / cross-references within the math layer. */
extern double __huge_val(void);
extern double __nan_val(void);
extern int __isnan(double);
extern int __isinf(double);
extern int __signbit(double);
extern double scalbn(double, int);
extern double round(double);
extern double rint(double);
extern double nextafter(double, double);
extern double expm1(double);
extern double log1p(double);

#endif /* MATH_PRIV_H */
