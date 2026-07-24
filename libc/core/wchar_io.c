#include <stdio.h>
#include <wchar.h>
#include <string.h>

/* =====================================================================
 * Wide stream I/O (Tier A): stream orientation + wide character I/O.
 * The byte encoding is UTF-8, so each wide operation transcodes through the
 * <wchar.h> codec (mbrtowc/wcrtomb) over the existing byte stdio layer.
 * Formatted wide I/O (fwprintf/fwscanf families) is a later tier.
 * ===================================================================== */

int fwide(FILE *fp, int mode) {
  int cur = (fp->flags & _SF_WORIENT) ? 1 : (fp->flags & _SF_BORIENT) ? -1 : 0;
  if (cur == 0 && mode != 0) {
    fp->flags |= (mode > 0) ? _SF_WORIENT : _SF_BORIENT;
    cur = (mode > 0) ? 1 : -1;
  }
  return cur;
}

/* Make the stream wide-oriented on first wide use (leaves an existing
 * orientation untouched). */
static void _worient(FILE *fp) {
  if (!(fp->flags & (_SF_WORIENT | _SF_BORIENT)))
    fp->flags |= _SF_WORIENT;
}

wint_t fputwc(wchar_t wc, FILE *fp) {
  _worient(fp);
  char buf[4];
  mbstate_t st;
  memset(&st, 0, sizeof st);
  size_t n = wcrtomb(buf, wc, &st);
  if (n == (size_t)-1) {
    fp->flags |= _SF_ERR;
    return WEOF;
  }
  for (size_t i = 0; i < n; i++)
    if (fputc((unsigned char)buf[i], fp) == EOF)
      return WEOF;
  return (wint_t)wc;
}

wint_t putwc(wchar_t wc, FILE *fp) { return fputwc(wc, fp); }
wint_t putwchar(wchar_t wc) { return fputwc(wc, stdout); }

wint_t fgetwc(FILE *fp) {
  _worient(fp);
  if (fp->flags & _SF_WUNGET) { /* a pushed-back wide char */
    fp->flags &= ~_SF_WUNGET;
    return (wint_t)fp->wunget;
  }
  mbstate_t st;
  memset(&st, 0, sizeof st);
  for (;;) {
    int c = fgetc(fp);
    if (c == EOF)
      return WEOF; /* clean EOF, or an incomplete tail at EOF */
    char b = (char)c;
    wchar_t wc;
    size_t r = mbrtowc(&wc, &b, 1, &st); /* accumulates in st across bytes */
    if (r == (size_t)-2)
      continue; /* need more bytes for this character */
    if (r == (size_t)-1) {
      fp->flags |= _SF_ERR;
      return WEOF;
    }
    return (wint_t)wc; /* r == 0 (a NUL) or the completing byte count */
  }
}

wint_t getwc(FILE *fp) { return fgetwc(fp); }
wint_t getwchar(void) { return fgetwc(stdin); }

wint_t ungetwc(wint_t wc, FILE *fp) {
  if (wc == WEOF || (fp->flags & _SF_WUNGET))
    return WEOF; /* only one pushback slot */
  _worient(fp);
  fp->wunget = (long)wc;
  fp->flags |= _SF_WUNGET;
  fp->flags &= ~_SF_EOF;
  return wc;
}

wchar_t *fgetws(wchar_t *s, int n, FILE *fp) {
  if (n <= 0)
    return 0;
  wchar_t *p = s;
  int i = 0;
  while (i < n - 1) {
    wint_t w = fgetwc(fp);
    if (w == WEOF) {
      if (i == 0)
        return 0; /* nothing read -> NULL */
      break;
    }
    *p++ = (wchar_t)w;
    i++;
    if (w == L'\n') /* keep the newline, then stop */
      break;
  }
  *p = 0;
  return s;
}

int fputws(const wchar_t *s, FILE *fp) {
  for (; *s; s++)
    if (fputwc(*s, fp) == WEOF)
      return -1;
  return 0;
}
