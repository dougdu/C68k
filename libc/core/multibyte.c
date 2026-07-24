#include <stdlib.h>
#include <uchar.h> /* mbrtoc32 / c32rtomb -- the shared UTF-8 codec */

/* <stdlib.h> multibyte functions (UTF-8 <-> UTF-32 wchar_t).  The state-less
 * variants keep their own internal mbstate; s==NULL queries whether the
 * encoding is state-dependent (UTF-8 is not, so they return 0). */

int mblen(const char *s, size_t n) {
  static mbstate_t st;
  if (!s) {
    st.__nbytes = 0;
    st.__pending = 0;
    return 0;
  }
  char32_t c;
  size_t r = mbrtoc32(&c, s, n, &st);
  if (r == (size_t)-1 || r == (size_t)-2)
    return -1;
  return (int)(c == 0 ? 0 : r);
}

int mbtowc(wchar_t *pwc, const char *s, size_t n) {
  static mbstate_t st;
  if (!s) {
    st.__nbytes = 0;
    st.__pending = 0;
    return 0;
  }
  char32_t c;
  size_t r = mbrtoc32(&c, s, n, &st);
  if (r == (size_t)-1 || r == (size_t)-2)
    return -1;
  if (pwc)
    *pwc = (wchar_t)c;
  return (int)(c == 0 ? 0 : r);
}

int wctomb(char *s, wchar_t wc) {
  static mbstate_t st;
  if (!s)
    return 0; /* UTF-8 is not state-dependent */
  size_t r = c32rtomb(s, (char32_t)wc, &st);
  return (r == (size_t)-1) ? -1 : (int)r;
}

size_t mbstowcs(wchar_t *dst, const char *src, size_t n) {
  mbstate_t st = {0};
  size_t cnt = 0;
  while (!dst || cnt < n) {
    char32_t c;
    size_t r = mbrtoc32(&c, src, 4, &st);
    if (r == (size_t)-1 || r == (size_t)-2)
      return (size_t)-1;
    if (r == 0) { /* terminating NUL */
      if (dst)
        dst[cnt] = 0;
      return cnt;
    }
    if (dst)
      dst[cnt] = (wchar_t)c;
    src += r;
    cnt++;
  }
  return cnt;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t n) {
  mbstate_t st = {0};
  char buf[4];
  size_t cnt = 0;
  for (;;) {
    wchar_t wc = *src;
    if (wc == 0) {
      if (dst && cnt < n)
        dst[cnt] = 0;
      return cnt;
    }
    size_t r = c32rtomb(buf, (char32_t)wc, &st);
    if (r == (size_t)-1)
      return (size_t)-1;
    if (dst) {
      if (cnt + r > n)
        break;
      for (size_t i = 0; i < r; i++)
        dst[cnt + i] = buf[i];
    }
    cnt += r;
    src++;
  }
  return cnt;
}
