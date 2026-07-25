/* Wide formatted output (Tier B): fwprintf/swprintf families over both sink
 * kinds.  Numeric conversions are asserted to match the proven narrow printf
 * engine (eqn); the wide-specific %c/%lc/%s/%ls semantics, precision/width,
 * swprintf truncation, %n and a FILE round-trip (UTF-8 transcode) are checked
 * directly.  Cross-OS.  Prints "WPRINTF PASS n/n". */
#include <wchar.h>
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

/* wide buffer equals the (ASCII) narrow string byte-for-byte */
static int eqn(const wchar_t *w, const char *n) {
  int i = 0;
  for (; n[i]; i++)
    if (w[i] != (wchar_t)(unsigned char)n[i])
      return 0;
  return w[i] == 0;
}

/* format the same directive/args both ways and require they agree */
#define PARITY(fmtw, fmtn, ...)                                                 \
  do {                                                                         \
    wchar_t wb[80];                                                            \
    char nb[80];                                                               \
    swprintf(wb, 80, fmtw, __VA_ARGS__);                                       \
    snprintf(nb, 80, fmtn, __VA_ARGS__);                                       \
    CHECK(eqn(wb, nb));                                                        \
  } while (0)

int main(void) {
  wchar_t buf[80];
  int r;

  /* --- numeric parity with the narrow engine --- */
  PARITY(L"%d", "%d", 42);
  PARITY(L"%5d", "%5d", 42);
  PARITY(L"%-5d|", "%-5d|", 42);
  PARITY(L"%05d", "%05d", 42);
  PARITY(L"%+d", "%+d", 42);
  PARITY(L"%d", "%d", -7);
  PARITY(L"%i", "%i", -12345);
  PARITY(L"%u", "%u", 4000000000UL);
  PARITY(L"%x", "%x", 255);
  PARITY(L"%X", "%X", 255);
  PARITY(L"%o", "%o", 64);
  PARITY(L"%lld", "%lld", 1000000000000LL);
  PARITY(L"%f", "%f", 3.5);
  PARITY(L"%.2f", "%.2f", 3.14159);
  PARITY(L"%e", "%e", 12345.678);
  PARITY(L"%g", "%g", 0.0001);
  PARITY(L"%g", "%g", 123456.0);
  PARITY(L"%a", "%a", 1.0);
  PARITY(L"%%", "%%", 0);
  {
    int x = 0;
    PARITY(L"%p", "%p", (void *)&x);
  }

  /* --- wide-specific %c / %lc --- */
  swprintf(buf, 80, L"%c", 'Z');
  CHECK(wcscmp(buf, L"Z") == 0);
  swprintf(buf, 80, L"%lc", (wint_t)L'W');
  CHECK(wcscmp(buf, L"W") == 0);
  swprintf(buf, 80, L"%lc", (wint_t)0x1F600); /* emoji, one wide char */
  CHECK(buf[0] == 0x1F600 && buf[1] == 0);
  swprintf(buf, 80, L"[%3c]", 'Q'); /* width on a char */
  CHECK(wcscmp(buf, L"[  Q]") == 0);

  /* --- %s: narrow multibyte (UTF-8) transcoded to wide --- */
  swprintf(buf, 80, L"%s", "abc");
  CHECK(wcscmp(buf, L"abc") == 0);
  {
    char e[] = {(char)0xC3, (char)0xA9, 0}; /* U+00E9 */
    swprintf(buf, 80, L"%s", e);
    CHECK(buf[0] == 0xE9 && buf[1] == 0);
  }
  {
    char em[] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 0}; /* U+1F600 */
    swprintf(buf, 80, L"%s", em);
    CHECK(buf[0] == 0x1F600 && buf[1] == 0);
  }
  swprintf(buf, 80, L"%.2s", "abcdef"); /* precision = max wide chars */
  CHECK(wcscmp(buf, L"ab") == 0);
  swprintf(buf, 80, L"%5.2s", "abcdef");
  CHECK(wcscmp(buf, L"   ab") == 0);
  swprintf(buf, 80, L"%-5.2s|", "abcdef");
  CHECK(wcscmp(buf, L"ab   |") == 0);

  /* --- %ls: wide string --- */
  swprintf(buf, 80, L"%ls", L"XYZ");
  CHECK(wcscmp(buf, L"XYZ") == 0);
  swprintf(buf, 80, L"%6ls", L"XYZ");
  CHECK(wcscmp(buf, L"   XYZ") == 0);
  {
    wchar_t ws[] = {0xE9, 0x1F600, 0};
    swprintf(buf, 80, L"%ls", ws);
    CHECK(buf[0] == 0xE9 && buf[1] == 0x1F600 && buf[2] == 0);
  }

  /* --- mixed directives --- */
  swprintf(buf, 80, L"[%d:%s:%c]", 1, "ab", 'Z');
  CHECK(wcscmp(buf, L"[1:ab:Z]") == 0);

  /* --- return value, %n, truncation --- */
  r = swprintf(buf, 80, L"hello");
  CHECK(r == 5 && wcscmp(buf, L"hello") == 0);
  {
    int cnt = -1;
    swprintf(buf, 80, L"ab%ncd", &cnt);
    CHECK(cnt == 2 && wcscmp(buf, L"abcd") == 0);
  }
  {
    wchar_t sb[3];
    r = swprintf(sb, 3, L"%d", 12345); /* truncated -> negative */
    CHECK(r < 0);
    CHECK(sb[2] == 0 && wcscmp(sb, L"12") == 0);
  }

  /* --- FILE sink: fwprintf transcodes to UTF-8 bytes, read back wide --- */
  {
    const char *fn = "WBP.TMP";
    FILE *fp = fopen(fn, "wb");
    CHECK(fp != NULL);
    CHECK(fwprintf(fp, L"n=%d x=%X\n", 42, 255) == 10);
    CHECK(fwprintf(fp, L"%lc%lc", (wint_t)0xE9, (wint_t)0x1F600) == 2);
    CHECK(fclose(fp) == 0);

    fp = fopen(fn, "rb");
    CHECK(fp != NULL);
    wchar_t line[16];
    CHECK(fgetws(line, 16, fp) == line);
    CHECK(wcscmp(line, L"n=42 x=FF\n") == 0);
    CHECK(fgetwc(fp) == 0xE9);
    CHECK(fgetwc(fp) == 0x1F600);
    CHECK(fclose(fp) == 0);
    remove(fn);
  }

  printf("WPRINTF %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
