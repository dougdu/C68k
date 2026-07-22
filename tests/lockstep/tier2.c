/* Tier 2 math -- Phase 2a: constants, classification, and IEEE-754 utilities.
 * Prints "TIER2 PASS n/n" when every check holds. */
#include <stdio.h>
#include <math.h>
#include <errno.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

/* relative tolerance compare for transcendental results */
static int nearv(double a, double b) {
  double d = a - b;
  if (d < 0.0)
    d = -d;
  double m = b < 0.0 ? -b : b;
  return d <= 1e-7 * (1.0 + m);
}
#define NEAR(a, b) CHECK(nearv((a), (b)))

int main(void) {
  /* classification */
  CHECK(isnan(NAN));
  CHECK(!isnan(1.0));
  CHECK(isinf(INFINITY));
  CHECK(isinf(-INFINITY));
  CHECK(!isinf(1.0));
  CHECK(isfinite(1.0));
  CHECK(!isfinite(INFINITY));
  CHECK(!isfinite(NAN));
  CHECK(isnormal(1.0));
  CHECK(!isnormal(0.0));
  CHECK(signbit(-1.0));
  CHECK(!signbit(1.0));
  CHECK(fpclassify(0.0) == FP_ZERO);
  CHECK(fpclassify(1.0) == FP_NORMAL);
  CHECK(fpclassify(INFINITY) == FP_INFINITE);
  CHECK(fpclassify(NAN) == FP_NAN);

  /* copysign / fmax / fmin / fdim */
  CHECK(copysign(3.0, -1.0) == -3.0);
  CHECK(copysign(-3.0, 1.0) == 3.0);
  CHECK(fmax(2.0, 5.0) == 5.0);
  CHECK(fmin(2.0, 5.0) == 2.0);
  CHECK(fmax(NAN, 5.0) == 5.0);
  CHECK(fmin(2.0, NAN) == 2.0);
  CHECK(fdim(5.0, 2.0) == 3.0);
  CHECK(fdim(2.0, 5.0) == 0.0);

  /* frexp / ldexp / scalbn */
  int e;
  double m = frexp(48.0, &e);
  CHECK(m == 0.75 && e == 6); /* 48 = 0.75 * 2^6 */
  CHECK(ldexp(0.75, 6) == 48.0);
  CHECK(ldexp(1.0, 10) == 1024.0);
  CHECK(scalbn(1.0, 3) == 8.0);
  CHECK(scalbn(3.0, -1) == 1.5);

  /* logb / ilogb */
  CHECK(logb(8.0) == 3.0);
  CHECK(ilogb(8.0) == 3);
  CHECK(ilogb(1.0) == 0);
  CHECK(ilogb(0.25) == -2);

  /* trunc / round / rint */
  CHECK(trunc(3.7) == 3.0);
  CHECK(trunc(-3.7) == -3.0);
  CHECK(round(2.5) == 3.0);
  CHECK(round(-2.5) == -3.0);
  CHECK(round(2.4) == 2.0);
  CHECK(rint(2.4) == 2.0);
  CHECK(rint(2.6) == 3.0);
  CHECK(rint(2.5) == 2.0); /* ties to even */
  CHECK(rint(3.5) == 4.0); /* ties to even */
  CHECK(nearbyint(-0.5) == 0.0);

  /* long/long long rounding */
  CHECK(lround(2.5) == 3);
  CHECK(llround(-2.5) == -3LL);
  CHECK(lrint(2.5) == 2);
  CHECK(llrint(3.5) == 4LL);

  /* nextafter */
  CHECK(nextafter(1.0, 2.0) > 1.0);
  CHECK(nextafter(1.0, 2.0) < 1.0000001);
  CHECK(nextafter(1.0, 0.0) < 1.0);
  CHECK(nextafter(0.0, 1.0) > 0.0);
  CHECK(nextafter(5.0, 5.0) == 5.0);

  /* fma */
  CHECK(fma(2.0, 3.0, 4.0) == 10.0);

  /* ---- Phase 2b: exp / log family ---- */
  NEAR(exp2(10.0), 1024.0);
  NEAR(exp2(0.5), 1.4142135623730951);
  NEAR(exp2(log2(7.0)), 7.0);
  NEAR(expm1(0.0), 0.0);
  NEAR(expm1(1.0), 1.718281828459045);
  NEAR(expm1(1e-10), 1e-10);
  NEAR(log2(1024.0), 10.0);
  NEAR(log2(8.0), 3.0);
  NEAR(log1p(0.0), 0.0);
  NEAR(log1p(1e-10), 1e-10);

  /* ---- hyperbolic ---- */
  NEAR(sinh(0.0), 0.0);
  NEAR(sinh(1.0), 1.1752011936438014);
  NEAR(sinh(-1.0), -1.1752011936438014);
  NEAR(cosh(0.0), 1.0);
  NEAR(cosh(1.0), 1.5430806348152437);
  NEAR(tanh(0.0), 0.0);
  NEAR(tanh(1.0), 0.7615941559557649);
  NEAR(tanh(100.0), 1.0);
  NEAR(asinh(0.0), 0.0);
  NEAR(sinh(asinh(2.0)), 2.0);
  NEAR(acosh(1.0), 0.0);
  NEAR(cosh(acosh(3.0)), 3.0);
  NEAR(atanh(0.5), 0.5493061443340548);
  NEAR(tanh(atanh(0.3)), 0.3);

  /* ---- power / remainder ---- */
  NEAR(cbrt(27.0), 3.0);
  NEAR(cbrt(-8.0), -2.0);
  NEAR(cbrt(0.0), 0.0);
  NEAR(hypot(3.0, 4.0), 5.0);
  NEAR(hypot(5.0, 12.0), 13.0);
  NEAR(remainder(5.0, 3.0), -1.0);
  NEAR(remainder(7.0, 3.0), 1.0);
  int q = 0;
  NEAR(remquo(7.0, 3.0, &q), 1.0);
  CHECK(q == 2);

  /* ---- errno domain / range ---- */
  errno = 0;
  CHECK(isnan(acosh(0.5)) && errno == EDOM);
  errno = 0;
  CHECK(isnan(atanh(2.0)) && errno == EDOM);
  errno = 0;
  CHECK(isinf(atanh(1.0)) && errno == ERANGE);
  errno = 0;
  CHECK(isnan(log2(-1.0)) && errno == EDOM);

  printf("TIER2 %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
