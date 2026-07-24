#include <wchar.h>
#include <uchar.h> /* mbrtoc32 / c32rtomb -- the shared UTF-8 codec */
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

/* =====================================================================
 * <wchar.h>: wide strings + restartable UTF-8<->UTF-32 conversion.
 * wchar_t is 32-bit UTF-32 and the multibyte encoding is UTF-8, so the
 * conversions reuse P8's uchar.c codec verbatim (mbrtowc == mbrtoc32,
 * wcrtomb == c32rtomb).  No wide I/O yet (fwprintf/fgetwc/...): deferred.
 * ===================================================================== */

/* ---- wide string operations ---- */
size_t wcslen(const wchar_t *s) {
  const wchar_t *p = s;
  while (*p)
    p++;
  return (size_t)(p - s);
}
wchar_t *wcscpy(wchar_t *d, const wchar_t *s) {
  wchar_t *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}
wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n) {
  wchar_t *r = d;
  while (n && (*d = *s)) {
    d++;
    s++;
    n--;
  }
  while (n--)
    *d++ = 0;
  return r;
}
wchar_t *wcscat(wchar_t *d, const wchar_t *s) {
  wchar_t *r = d;
  while (*d)
    d++;
  while ((*d++ = *s++))
    ;
  return r;
}
wchar_t *wcsncat(wchar_t *d, const wchar_t *s, size_t n) {
  wchar_t *r = d;
  while (*d)
    d++;
  while (n && *s) {
    *d++ = *s++;
    n--;
  }
  *d = 0;
  return r;
}
int wcscmp(const wchar_t *a, const wchar_t *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (!n)
    return 0;
  return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}
int wcscoll(const wchar_t *a, const wchar_t *b) { return wcscmp(a, b); }
size_t wcsxfrm(wchar_t *d, const wchar_t *s, size_t n) {
  size_t l = wcslen(s);
  if (n) {
    size_t i = 0;
    for (; i < n - 1 && s[i]; i++)
      d[i] = s[i];
    d[i] = 0;
  }
  return l;
}
wchar_t *wcschr(const wchar_t *s, wchar_t c) {
  for (;; s++) {
    if (*s == c)
      return (wchar_t *)s;
    if (!*s)
      return 0;
  }
}
wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
  const wchar_t *r = 0;
  for (;; s++) {
    if (*s == c)
      r = s;
    if (!*s)
      break;
  }
  return (wchar_t *)r;
}
static int wset(const wchar_t *set, wchar_t c) {
  for (; *set; set++)
    if (*set == c)
      return 1;
  return 0;
}
size_t wcsspn(const wchar_t *s, const wchar_t *set) {
  const wchar_t *p = s;
  while (*p && wset(set, *p))
    p++;
  return (size_t)(p - s);
}
size_t wcscspn(const wchar_t *s, const wchar_t *set) {
  const wchar_t *p = s;
  while (*p && !wset(set, *p))
    p++;
  return (size_t)(p - s);
}
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *set) {
  for (; *s; s++)
    if (wset(set, *s))
      return (wchar_t *)s;
  return 0;
}
wchar_t *wcsstr(const wchar_t *hay, const wchar_t *needle) {
  if (!*needle)
    return (wchar_t *)hay;
  for (; *hay; hay++) {
    const wchar_t *h = hay, *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n)
      return (wchar_t *)hay;
  }
  return 0;
}
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **save) {
  if (!s)
    s = *save;
  if (!s)
    return 0;
  s += wcsspn(s, delim);
  if (!*s) {
    *save = 0;
    return 0;
  }
  wchar_t *tok = s;
  s += wcscspn(s, delim);
  if (*s) {
    *s = 0;
    *save = s + 1;
  } else {
    *save = 0;
  }
  return tok;
}

/* ---- wide memory operations ---- */
wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n) {
  wchar_t *r = d;
  while (n--)
    *d++ = *s++;
  return r;
}
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n) {
  wchar_t *d = dst;
  const wchar_t *s = src;
  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }
  return dst;
}
wchar_t *wmemset(wchar_t *d, wchar_t c, size_t n) {
  wchar_t *r = d;
  while (n--)
    *d++ = c;
  return r;
}
int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n) {
  while (n--) {
    if (*a != *b)
      return (*a < *b) ? -1 : 1;
    a++;
    b++;
  }
  return 0;
}
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
  while (n--) {
    if (*s == c)
      return (wchar_t *)s;
    s++;
  }
  return 0;
}

/* ---- restartable conversion (delegates to the uchar.c UTF-8 codec) ---- */
int mbsinit(const mbstate_t *ps) {
  return !ps || (ps->__nbytes == 0 && ps->__pending == 0);
}
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
  static mbstate_t st;
  if (!ps)
    ps = &st;
  return mbrtoc32((char32_t *)pwc, s, n, ps);
}
size_t mbrlen(const char *s, size_t n, mbstate_t *ps) {
  static mbstate_t st;
  return mbrtowc(0, s, n, ps ? ps : &st);
}
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
  static mbstate_t st;
  if (!ps)
    ps = &st;
  return c32rtomb(s, (char32_t)wc, ps);
}
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps) {
  static mbstate_t st;
  if (!ps)
    ps = &st;
  const char *s = *src;
  size_t cnt = 0;
  while (!dst || cnt < len) {
    wchar_t wc;
    size_t r = mbrtowc(&wc, s, 4, ps);
    if (r == (size_t)-1 || r == (size_t)-2) {
      *src = s;
      return (size_t)-1;
    }
    if (r == 0) { /* terminating NUL */
      if (dst)
        dst[cnt] = 0;
      *src = 0;
      return cnt;
    }
    if (dst)
      dst[cnt] = wc;
    s += r;
    cnt++;
  }
  *src = s;
  return cnt;
}
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps) {
  static mbstate_t st;
  if (!ps)
    ps = &st;
  const wchar_t *s = *src;
  size_t cnt = 0;
  char buf[4];
  for (;;) {
    wchar_t wc = *s;
    if (wc == 0) {
      if (dst && cnt < len)
        dst[cnt] = 0;
      *src = 0;
      return cnt;
    }
    size_t r = c32rtomb(buf, (char32_t)wc, ps);
    if (r == (size_t)-1) {
      *src = s;
      return (size_t)-1;
    }
    if (dst) {
      if (cnt + r > len)
        break;
      for (size_t i = 0; i < r; i++)
        dst[cnt + i] = buf[i];
    }
    cnt += r;
    s++;
  }
  *src = s;
  return cnt;
}
wint_t btowc(int c) {
  return (c == -1 || (unsigned)c > 0x7F) ? WEOF : (wint_t)c;
}
int wctob(wint_t c) { return (c > 0x7F) ? -1 : (int)c; }

/* ---- wide numeric conversion (copy the ASCII prefix, delegate) ---- */
static size_t w2n(const wchar_t *s, char *buf, size_t bufsz) {
  size_t i = 0;
  while (i + 1 < bufsz && s[i] && (unsigned)s[i] < 0x80) {
    buf[i] = (char)s[i];
    i++;
  }
  buf[i] = 0;
  return i;
}
long wcstol(const wchar_t *s, wchar_t **end, int base) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  long v = strtol(b, &ep, base);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
unsigned long wcstoul(const wchar_t *s, wchar_t **end, int base) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  unsigned long v = strtoul(b, &ep, base);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
long long wcstoll(const wchar_t *s, wchar_t **end, int base) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  long long v = strtoll(b, &ep, base);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
unsigned long long wcstoull(const wchar_t *s, wchar_t **end, int base) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  unsigned long long v = strtoull(b, &ep, base);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
double wcstod(const wchar_t *s, wchar_t **end) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  double v = strtod(b, &ep);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
float wcstof(const wchar_t *s, wchar_t **end) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  float v = strtof(b, &ep);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}
long double wcstold(const wchar_t *s, wchar_t **end) {
  char b[128], *ep;
  w2n(s, b, sizeof b);
  long double v = strtold(b, &ep);
  if (end)
    *end = (wchar_t *)s + (ep - b);
  return v;
}

/* <inttypes.h> wide integer conversion (wrap the wcstoll/ull above). */
intmax_t wcstoimax(const wchar_t *s, wchar_t **end, int base) {
  return wcstoll(s, end, base);
}
uintmax_t wcstoumax(const wchar_t *s, wchar_t **end, int base) {
  return wcstoull(s, end, base);
}

/* ---- wide strftime (format via narrow strftime, then widen) ---- */
size_t wcsftime(wchar_t *s, size_t max, const wchar_t *fmt,
                const struct tm *tm) {
  char fbuf[256], obuf[256];
  size_t i = 0;
  for (; i < sizeof(fbuf) - 1 && fmt[i]; i++)
    fbuf[i] = ((unsigned)fmt[i] < 0x80) ? (char)fmt[i] : '?';
  fbuf[i] = 0;
  size_t r = strftime(obuf, sizeof obuf, fbuf, tm);
  if (max == 0)
    return 0;
  if (r == 0 || r >= max) {
    s[0] = 0;
    return 0;
  }
  for (size_t k = 0; k < r; k++)
    s[k] = (unsigned char)obuf[k];
  s[r] = 0;
  return r;
}
