/* Tier 2 math -- float (single-precision) and long double variants.
 * Verifies the C99 *f API actually runs libm's single-precision kernels
 * (_sqrtf/_expf/...), the derived *f (tanf/asinf/acosf/log10f/atan2f), and
 * the *l API (long double == double).  Prints "TIER2F PASS n/n" on success. */
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

/* absolute-tolerance compare (single precision ~1e-6..1e-5) */
static int neart(double a, double b, double tol) {
  double d = a - b;
  if (d < 0.0)
    d = -d;
  return d <= tol;
}

int main(void) {
  float fip;
  long double lip;

  /* ---- Bucket 1: float functions bound to libm single kernels ---- */
  CHECK(neart(sqrtf(2.0f), 1.41421356, 1e-5));
  CHECK(neart(sqrtf(16.0f), 4.0, 1e-5));
  CHECK(neart(expf(1.0f), 2.71828183, 1e-4));
  CHECK(neart(expf(-1.0f), 0.36787944, 1e-5));
  CHECK(neart(expf(-7.5f), 0.000553084, 1e-6));
  CHECK(expf(0.0f) == 1.0f);
  CHECK(neart(logf(2.71828183f), 1.0, 1e-5));
  CHECK(neart(logf(1.0f), 0.0, 1e-6));
  CHECK(neart(sinf(0.5235988f), 0.5, 1e-5)); /* sin(pi/6) */
  CHECK(neart(sinf(0.0f), 0.0, 1e-6));
  CHECK(neart(cosf(1.0471976f), 0.5, 1e-5)); /* cos(pi/3) */
  CHECK(neart(cosf(0.0f), 1.0, 1e-6));
  CHECK(neart(atanf(1.0f), 0.78539816, 1e-5)); /* pi/4 */
  CHECK(neart(powf(2.0f, 10.0f), 1024.0, 2e-2));
  CHECK(neart(powf(2.0f, 0.5f), 1.41421356, 1e-4));
  CHECK(neart(powf(0.5f, 2.0f), 0.25, 1e-5));
  CHECK(neart(powf(2.0f, -3.0f), 0.125, 1e-5));
  CHECK(neart(fmodf(7.0f, 3.0f), 1.0, 1e-5));
  CHECK(neart(fmodf(5.5f, 2.0f), 1.5, 1e-5));
  CHECK(floorf(2.7f) == 2.0f);
  CHECK(floorf(-2.3f) == -3.0f);
  CHECK(ceilf(2.1f) == 3.0f);
  CHECK(ceilf(-2.7f) == -2.0f);
  CHECK(fabsf(-3.5f) == 3.5f);
  CHECK(fabsf(3.5f) == 3.5f);
  {
    float fr = modff(3.75f, &fip);
    CHECK(neart(fr, 0.75, 1e-6) && neart(fip, 3.0, 1e-6));
  }

  /* ---- Bucket 2: derived float (composed from the single kernels) ---- */
  CHECK(neart(tanf(0.7853982f), 1.0, 1e-4)); /* tan(pi/4) */
  CHECK(neart(tanf(0.0f), 0.0, 1e-6));
  CHECK(neart(asinf(0.5f), 0.5235988, 1e-5));   /* pi/6 */
  CHECK(neart(asinf(-0.5f), -0.5235988, 1e-5));
  CHECK(neart(acosf(0.5f), 1.0471976, 1e-5));   /* pi/3 */
  CHECK(neart(log10f(1000.0f), 3.0, 1e-4));
  CHECK(neart(log10f(1.0f), 0.0, 1e-6));
  CHECK(neart(atan2f(1.0f, 1.0f), 0.78539816, 1e-5));  /* pi/4 */
  CHECK(neart(atan2f(1.0f, -1.0f), 2.3561945, 1e-5));  /* 3pi/4 */
  CHECK(neart(atan2f(-1.0f, -1.0f), -2.3561945, 1e-5));
  CHECK(neart(atan2f(1.0f, 0.0f), 1.5707963, 1e-5));   /* pi/2 */

  /* ---- long double variants (long double == double on this target) ---- */
  CHECK(neart(sqrtl(2.0), 1.4142135623730951, 1e-6));
  CHECK(neart(expl(1.0), 2.718281828459045, 1e-6));
  CHECK(neart(expl(-1.0), 0.36787944117144233, 1e-7));
  CHECK(neart(logl(2.718281828459045), 1.0, 1e-6));
  CHECK(neart(sinl(0.0), 0.0, 1e-9));
  CHECK(neart(cosl(0.0), 1.0, 1e-9));
  CHECK(neart(powl(2.0, 10.0), 1024.0, 1e-3));
  CHECK(neart(powl(0.5, 2.0), 0.25, 1e-7));
  CHECK(neart(log10l(1000.0), 3.0, 1e-6));
  CHECK(neart(fabsl(-2.5), 2.5, 1e-9));
  CHECK(neart(floorl(2.7), 2.0, 1e-9));
  CHECK(neart(ceill(2.1), 3.0, 1e-9));
  CHECK(neart(fmodl(7.0, 3.0), 1.0, 1e-7));
  CHECK(neart(atan2l(1.0, 1.0), 0.7853981633974483, 1e-6));
  CHECK(neart(asinl(0.5), 0.5235987755982989, 1e-6));
  CHECK(neart(acosl(0.5), 1.0471975511965976, 1e-6));
  CHECK(neart(tanl(0.7853981633974483), 1.0, 1e-6));
  CHECK(neart(atanl(1.0), 0.7853981633974483, 1e-6));
  {
    long double lr = modfl(3.75, &lip);
    CHECK(neart(lr, 0.75, 1e-9) && neart(lip, 3.0, 1e-9));
  }

  printf("TIER2F %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
