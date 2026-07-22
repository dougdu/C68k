/* Tier 2 math -- Phase 2a: constants, classification, and IEEE-754 utilities.
 * Prints "TIER2 PASS n/n" when every check holds. */
#include <stdio.h>
#include <math.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

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

  printf("TIER2 %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
