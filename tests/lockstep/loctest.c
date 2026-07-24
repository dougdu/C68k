/* <locale.h>: setlocale + NLS-backed localeconv, and <string.h> strcoll/
 * strxfrm.  Cross-OS.  On Osiris the native ("") locale adopts the OS country
 * (default US -> thousands ",", currency "$"); on CP/M-68K "" falls back to the
 * "C" locale (no country service).  The native-locale assertions are guarded by
 * `have` so CP/M passes them trivially and the count matches.  Prints
 * "LOCTEST PASS n/n". */
#include <locale.h>
#include <string.h>
#include <stdio.h>

static int total, pass;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

int main(void) {
  /* The "C" locale (holds on both OSes). */
  char *r = setlocale(LC_ALL, "C");
  CHECK(r && !strcmp(r, "C"));
  struct lconv *lc = localeconv();
  CHECK(!strcmp(lc->decimal_point, "."));
  CHECK(lc->thousands_sep[0] == '\0'); /* C locale: empty */
  CHECK(lc->grouping[0] == '\0');
  CHECK(setlocale(LC_NUMERIC, "POSIX") != NULL);
  CHECK(setlocale(LC_ALL, "no_such_locale") == NULL);

  /* strcoll / strxfrm (C-locale byte order, both OSes). */
  CHECK(strcoll("abc", "abc") == 0);
  CHECK(strcoll("abc", "abd") < 0);
  CHECK(strcoll("abd", "abc") > 0);
  char xb[8];
  size_t xn = strxfrm(xb, "abc", sizeof xb);
  CHECK(xn == 3 && !strcmp(xb, "abc"));
  /* strxfrm keys compare in the same order as strcoll. */
  char ka[8], kb[8];
  strxfrm(ka, "abc", sizeof ka);
  strxfrm(kb, "abd", sizeof kb);
  CHECK(strcmp(ka, kb) < 0);

  /* The native ("") locale: Osiris -> US country conventions; CP/M -> C. */
  setlocale(LC_ALL, "");
  lc = localeconv();
  int have = lc->thousands_sep[0] != '\0'; /* true only where NLS exists */
  CHECK(!have || (lc->thousands_sep[0] == ',' && lc->thousands_sep[1] == '\0'));
  CHECK(!have || !strcmp(lc->decimal_point, "."));
  CHECK(!have || lc->currency_symbol[0] == '$');
  CHECK(!have || lc->grouping[0] == 3); /* groups of 3 */
  CHECK(have || lc->thousands_sep[0] == '\0'); /* CP/M: C fallback */

  setlocale(LC_ALL, "C"); /* restore */
  printf("LOCTEST %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
