/* Wide formatted input (Tier C): swscanf/fwscanf families over both source
 * kinds.  Numeric conversions read the same tokens as the proven narrow
 * scanner; the wide-specific %c/%lc/%s/%ls/%[ semantics (multibyte store by
 * default, wchar_t store under l), width/suppression/%n, and a fwscanf FILE
 * round-trip (UTF-8 transcode) are checked directly.  Cross-OS.  Prints
 * "WSCANF PASS n/n". */
#include <wchar.h>
#include <stdio.h>
#include <string.h>

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
  int d, d2, np;
  unsigned u;
  long long ll;
  signed char sc;
  double da;
  float fa;
  char nb[32];
  wchar_t wb[32];

  /* --- integer conversions --- */
  CHECK(swscanf(L"42", L"%d", &d) == 1 && d == 42);
  CHECK(swscanf(L"-7", L"%d", &d) == 1 && d == -7);
  CHECK(swscanf(L"  0x2A rest", L"%i", &d) == 1 && d == 42);   /* auto-base */
  CHECK(swscanf(L"12345", L"%3d%n", &d, &np) == 1 && d == 123 && np == 3);
  CHECK(swscanf(L"", L"%d", &d) == EOF);                       /* input failure */
  CHECK(swscanf(L"ff", L"%x", &u) == 1 && u == 255);
  CHECK(swscanf(L"100", L"%o", &u) == 1 && u == 64);
  CHECK(swscanf(L"1000000000000", L"%lld", &ll) == 1 && ll == 1000000000000LL);
  CHECK(swscanf(L"200", L"%hhd", &sc) == 1 && sc == (signed char)200);
  {
    void *p = 0;
    CHECK(swscanf(L"0x1234", L"%p", &p) == 1 && p == (void *)0x1234);
  }

  /* --- float conversions (decimal + hex-float, the inverse of printf %a) --- */
  CHECK(swscanf(L"3.5", L"%lf", &da) == 1 && da == 3.5);
  CHECK(swscanf(L"1.25e2", L"%lf", &da) == 1 && da == 125.0);
  CHECK(swscanf(L"-2.5", L"%lg", &da) == 1 && da == -2.5);
  CHECK(swscanf(L"0x1.8p3", L"%la", &da) == 1 && da == 12.0);
  CHECK(swscanf(L"-0x1p-1", L"%a", &fa) == 1 && fa == -0.5f);

  /* --- %s: default stores multibyte (UTF-8), %ls stores wide --- */
  CHECK(swscanf(L"hello world", L"%s", nb) == 1 && strcmp(nb, "hello") == 0);
  {
    wchar_t in[] = {0xE9, 0x41, 0x20, 0}; /* U+00E9, 'A', space */
    CHECK(swscanf(in, L"%s", nb) == 1 && (unsigned char)nb[0] == 0xC3 &&
          (unsigned char)nb[1] == 0xA9 && nb[2] == 'A' && nb[3] == 0);
  }
  CHECK(swscanf(L"hello world", L"%ls", wb) == 1 && wcscmp(wb, L"hello") == 0);
  {
    wchar_t in[] = {0xE9, 0x1F600, 0x20, 0x41, 0};
    CHECK(swscanf(in, L"%ls", wb) == 1 && wb[0] == 0xE9 && wb[1] == 0x1F600 &&
          wb[2] == 0);
  }

  /* --- %c: default one wide char -> multibyte, %lc -> wchar_t --- */
  {
    char cb[4];
    CHECK(swscanf(L"Z", L"%c", cb) == 1 && cb[0] == 'Z');
    CHECK(swscanf(L"abc", L"%2c", cb) == 1 && cb[0] == 'a' && cb[1] == 'b');
    wchar_t in[] = {0xE9, 0}; /* U+00E9 -> C3 A9, no terminator added */
    CHECK(swscanf(in, L"%c", cb) == 1 && (unsigned char)cb[0] == 0xC3 &&
          (unsigned char)cb[1] == 0xA9);
    wchar_t in2[] = {0x1F600, 0};
    wchar_t wc = 0;
    CHECK(swscanf(in2, L"%lc", &wc) == 1 && wc == 0x1F600);
  }

  /* --- %[ scanset (no ws skip; C99: every char incl. '-' is literal) --- */
  {
    char sset[32];
    wchar_t wset[32];
    CHECK(swscanf(L"abcXYZ", L"%[abc]", sset) == 1 && strcmp(sset, "abc") == 0);
    CHECK(swscanf(L"hello, world", L"%[^,]", sset) == 1 &&
          strcmp(sset, "hello") == 0);
    CHECK(swscanf(L"aaaaaa", L"%3[a]", sset) == 1 && strcmp(sset, "aaa") == 0);
    CHECK(swscanf(L"   x", L"%[abc]", sset) == 0); /* no leading-ws skip */
    CHECK(swscanf(L"abcXYZ", L"%l[abc]", wset) == 1 &&
          wcscmp(wset, L"abc") == 0);
  }

  /* --- multiple assignments, suppression, literal match, %% --- */
  CHECK(swscanf(L"42 abc 7", L"%d %s %d", &d, nb, &d2) == 3 && d == 42 &&
        strcmp(nb, "abc") == 0 && d2 == 7);
  CHECK(swscanf(L"10 20", L"%*d %d", &d) == 1 && d == 20);
  CHECK(swscanf(L"x=5", L"x=%d", &d) == 1 && d == 5);
  CHECK(swscanf(L"50%", L"%d%%", &d) == 1 && d == 50);

  /* --- FILE source: fwscanf over a wide-oriented stream (UTF-8 transcode) --- */
  {
    const char *fn = "WSC.TMP";
    FILE *fp = fopen(fn, "wb");
    CHECK(fp != NULL);
    fputs("42 3.5 hello\n", fp);
    {
      char e[] = {(char)0xC3, (char)0xA9, '\n', 0}; /* U+00E9 + newline */
      fputs(e, fp);
    }
    CHECK(fclose(fp) == 0);

    fp = fopen(fn, "rb");
    CHECK(fp != NULL);
    {
      int fd = 0;
      double ff = 0;
      char fs[16];
      CHECK(fwscanf(fp, L"%d %lf %s", &fd, &ff, fs) == 3 && fd == 42 &&
            ff == 3.5 && strcmp(fs, "hello") == 0);
      wchar_t fw = 0;
      CHECK(fwscanf(fp, L" %lc", &fw) == 1 && fw == 0xE9);
    }
    CHECK(fclose(fp) == 0);
    remove(fn);
  }

  printf("WSCANF %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
