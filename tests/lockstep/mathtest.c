/*
 * mathtest.c --- <math.h> + printf float formatting battery (lockstep).
 * Prints "MATH PASS n/n" when every check holds on both OSes.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass, g_fail;
static void chk(const char *got, const char *want) {
  if (strcmp(got, want) == 0)
    g_pass++;
  else {
    g_fail++;
    printf("FAIL: got '%s' want '%s'\n", got, want);
  }
}
static void chkb(int got, int want) {
  if (got == want)
    g_pass++;
  else {
    g_fail++;
    printf("FAIL: cmp got %d want %d\n", got, want);
  }
}

int main(void) {
  char b[64];
  snprintf(b, sizeof b, "%.4f", sqrt(2.0));       chk(b, "1.4142");
  snprintf(b, sizeof b, "%.1f", pow(2.0, 10.0));  chk(b, "1024.0");
  snprintf(b, sizeof b, "%.6f", exp(1.0));        chk(b, "2.718282");
  snprintf(b, sizeof b, "%.4f", sin(M_PI / 6.0)); chk(b, "0.5000");
  snprintf(b, sizeof b, "%.1f", cos(0.0));        chk(b, "1.0");
  snprintf(b, sizeof b, "%.4f", log(M_E));        chk(b, "1.0000");
  snprintf(b, sizeof b, "%.1f", floor(3.7));      chk(b, "3.0");
  snprintf(b, sizeof b, "%.1f", ceil(3.2));       chk(b, "4.0");
  snprintf(b, sizeof b, "%.2f", fabs(-2.5));      chk(b, "2.50");
  snprintf(b, sizeof b, "%.2f", fmod(10.0, 3.0)); chk(b, "1.00");
  snprintf(b, sizeof b, "%.4f", atan2(1.0, 1.0)); chk(b, "0.7854");
  snprintf(b, sizeof b, "%f", 3.14159265);        chk(b, "3.141593");
  snprintf(b, sizeof b, "%e", 12345.678);         chk(b, "1.234568e+04");
  snprintf(b, sizeof b, "%.2f", -1.5);            chk(b, "-1.50");

  /* Double comparisons via _fpcmpd. Regression: the compare routine used to
     mis-flag a zero-high-word operand, so `0.0 < x` (and hence `x > 0.0`) was
     wrong whenever 0.0 was the first operand. `volatile` forces the compare to
     run through the library rather than being constant-folded. */
  volatile double z = 0.0, p = 1.5, n = -1.5;
  chkb(p > z, 1);   /* 1.5 > 0.0   (chibicc lowers to 0.0 < 1.5: 0.0 is dest) */
  chkb(n > z, 0);   /* -1.5 > 0.0                                             */
  chkb(z < p, 1);   /* 0.0 < 1.5   (0.0 is dest directly)                     */
  chkb(z < n, 0);   /* 0.0 < -1.5                                             */
  chkb(n < z, 1);   /* -1.5 < 0.0                                             */
  chkb(p < z, 0);   /* 1.5 < 0.0                                              */
  chkb(z > n, 1);   /* 0.0 > -1.5                                             */
  chkb(p >= z, 1);  /* 1.5 >= 0.0  (lowered to 0.0 <= 1.5)                    */
  chkb(z >= z, 1);  /* 0.0 >= 0.0  (equal)                                    */
  chkb(z <= z, 1);  /* 0.0 <= 0.0                                             */
  chkb(z == z, 1);  /* 0.0 == 0.0                                             */
  chkb(p != z, 1);  /* 1.5 != 0.0                                             */
  chkb(z != p, 1);  /* 0.0 != 1.5                                             */
  chkb(p == z, 0);  /* 1.5 == 0.0                                             */

  if (g_fail == 0)
    printf("MATH PASS %d/%d\n", g_pass, g_pass);
  else
    printf("MATH FAIL %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail;
}
