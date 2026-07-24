/* Wide stream I/O (Tier A): fwide + wide character I/O over a UTF-8 byte file.
 * Cross-OS.  Writes wide chars (transcoded to UTF-8), reads them back, and
 * exercises orientation + ungetwc.  Prints "WCIO PASS n/n". */
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

int main(void) {
  const char *fn = "WCIO.TMP";

  /* Write: 'A', U+00E9 (2 UTF-8 bytes), U+1F600 emoji (4 bytes), then "XY\n". */
  FILE *fp = fopen(fn, "wb");
  CHECK(fp != NULL);
  CHECK(fwide(fp, 0) == 0);  /* unoriented before any I/O */
  CHECK(fwide(fp, 1) > 0);   /* select wide orientation */
  CHECK(fwide(fp, -1) > 0);  /* orientation cannot be changed once set */
  CHECK(fputwc(L'A', fp) == L'A');
  CHECK(fputwc(0xE9, fp) == 0xE9);
  CHECK(fputwc(0x1F600, fp) == 0x1F600);
  CHECK(fputws(L"XY\n", fp) == 0);
  CHECK(fclose(fp) == 0);

  /* Read the wide characters back. */
  fp = fopen(fn, "rb");
  CHECK(fp != NULL);
  CHECK(fgetwc(fp) == L'A');
  CHECK(fgetwc(fp) == 0xE9);
  wint_t w = fgetwc(fp);
  CHECK(w == 0x1F600);
  CHECK(ungetwc(w, fp) == 0x1F600); /* push it back */
  CHECK(fgetwc(fp) == 0x1F600);     /* and read it again */
  wchar_t line[8];
  CHECK(fgetws(line, 8, fp) == line);
  CHECK(wcscmp(line, L"XY\n") == 0); /* newline kept, NUL-terminated */
  /* fgetwc eventually returns WEOF (bounded: CP/M pads the last record, so a
     binary read yields padding before EOF; FAT hits EOF at the exact length). */
  int guard = 200, sawEOF = 0;
  while (guard-- > 0)
    if (fgetwc(fp) == WEOF) {
      sawEOF = 1;
      break;
    }
  CHECK(sawEOF);
  CHECK(fclose(fp) == 0);
  remove(fn);

  printf("WCIO %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
