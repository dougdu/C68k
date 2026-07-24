/* <wchar.h> / <wctype.h> + <stdlib.h> multibyte: wide strings and UTF-8 <->
 * UTF-32 conversion.  Cross-OS lockstep test.  Prints "WCHART PASS n/n". */
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
  const wchar_t *h = L"hello";

  /* wide string ops */
  CHECK(wcslen(L"hello") == 5);
  CHECK(wcscmp(L"abc", L"abc") == 0);
  CHECK(wcscmp(L"abc", L"abd") < 0);
  CHECK(wcsncmp(L"abcX", L"abcY", 3) == 0);
  CHECK(wcschr(h, L'l') - h == 2);
  CHECK(wcsrchr(h, L'l') - h == 3);
  const wchar_t *hw = L"hello world";
  CHECK(wcsstr(hw, L"world") != 0 && wcsstr(hw, L"world") - hw == 6);

  wchar_t buf[16];
  wcscpy(buf, L"ab");
  wcscat(buf, L"cd");
  CHECK(wcscmp(buf, L"abcd") == 0);
  CHECK(wcsspn(L"aabbc", L"ab") == 4);
  CHECK(wcscspn(L"abc123", L"123") == 3);

  wcsncpy(buf, L"ab", 5);
  CHECK(buf[1] == L'b' && buf[2] == 0 && buf[4] == 0);

  wchar_t mb[4];
  wmemset(mb, L'x', 3);
  CHECK(mb[0] == L'x' && mb[2] == L'x');
  CHECK(wmemcmp(L"abc", L"abc", 3) == 0 && wmemcmp(L"abc", L"abd", 3) < 0);

  /* multibyte <-> wide: 'h' + U+00E9 (e-acute, UTF-8 C3 A9) */
  wchar_t w[8];
  size_t n = mbstowcs(w, "h\xC3\xA9", 8);
  CHECK(n == 2 && w[0] == L'h' && w[1] == 0xE9 && w[2] == 0);
  char nb[8];
  size_t m = wcstombs(nb, w, 8);
  CHECK(m == 3 && (unsigned char)nb[0] == 'h' && (unsigned char)nb[1] == 0xC3 &&
        (unsigned char)nb[2] == 0xA9);

  /* restartable: U+1F600 emoji (UTF-8 F0 9F 98 80) */
  mbstate_t st;
  memset(&st, 0, sizeof st);
  wchar_t wc;
  CHECK(mbrtowc(&wc, "\xF0\x9F\x98\x80", 4, &st) == 4 && wc == 0x1F600);
  memset(&st, 0, sizeof st);
  char e[4];
  size_t er = wcrtomb(e, 0x1F600, &st);
  CHECK(er == 4 && (unsigned char)e[0] == 0xF0 && (unsigned char)e[3] == 0x80);

  /* btowc / wctob */
  CHECK(btowc('A') == 0x41);
  CHECK(btowc(0x80) == WEOF);
  CHECK(wctob(0x41) == 'A');
  CHECK(wctob(0x1F600) == EOF);

  /* stateless multibyte */
  CHECK(mblen("A", 1) == 1 && mblen("\xC3\xA9", 2) == 2);
  wchar_t wc2;
  CHECK(mbtowc(&wc2, "\xC3\xA9", 2) == 2 && wc2 == 0xE9);
  char c1[4];
  CHECK(wctomb(c1, 0xE9) == 2 && (unsigned char)c1[0] == 0xC3 &&
        (unsigned char)c1[1] == 0xA9);

  /* wide numeric */
  wchar_t *end;
  CHECK(wcstol(L"  -42xyz", &end, 10) == -42 && *end == L'x');
  CHECK(wcstoul(L"2A", 0, 16) == 42);

  /* wide classification ("C" locale) */
  CHECK(iswalpha(L'A') && iswdigit(L'7') && iswspace(L' ') && !iswalpha(L'7'));
  CHECK(towupper(L'a') == L'A' && towlower(L'A') == L'a');
  CHECK(iswalpha(0xE9) == 0); /* non-ASCII unclassified in C locale */
  wctype_t wt = wctype("digit");
  CHECK(wt != 0 && iswctype(L'5', wt) && !iswctype(L'x', wt));
  CHECK(towctrans(L'a', wctrans("toupper")) == L'A');

  /* wide strftime */
  struct tm tmv;
  memset(&tmv, 0, sizeof tmv);
  tmv.tm_year = 121;
  tmv.tm_mon = 2;
  tmv.tm_mday = 14;
  wchar_t ws[16];
  CHECK(wcsftime(ws, 16, L"%Y-%m-%d", &tmv) == 10 && wcscmp(ws, L"2021-03-14") == 0);

  printf("WCHART %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
