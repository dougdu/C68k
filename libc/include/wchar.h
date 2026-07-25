#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h> /* size_t, wchar_t, NULL */
#include <stdarg.h> /* va_list, for the v*wprintf family */

#ifndef __WINT_T_DEFINED
#define __WINT_T_DEFINED
typedef unsigned int wint_t;
#endif

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

#ifndef WCHAR_MIN
#define WCHAR_MIN 0
#define WCHAR_MAX 0xFFFFFFFFu
#endif

#ifndef __MBSTATE_T_DEFINED
#define __MBSTATE_T_DEFINED
typedef struct {
  unsigned char __nbytes;   /* UTF-8 bytes accumulated so far */
  unsigned char __explen;   /* expected total bytes of the sequence */
  unsigned char __buf[4];   /* the accumulated bytes */
  unsigned short __pending; /* a held UTF-16 surrogate, or 0 */
} mbstate_t;
#endif

#ifndef __FILE_DEFINED
#define __FILE_DEFINED
typedef struct _FILE FILE; /* completed by <stdio.h>; opaque here */
#endif

struct tm; /* for wcsftime, without pulling in <time.h> */

/* --- wide string operations (wchar_t is 32-bit UTF-32) --- */
size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *d, const wchar_t *s);
wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n);
wchar_t *wcscat(wchar_t *d, const wchar_t *s);
wchar_t *wcsncat(wchar_t *d, const wchar_t *s, size_t n);
int wcscmp(const wchar_t *a, const wchar_t *b);
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
int wcscoll(const wchar_t *a, const wchar_t *b);
size_t wcsxfrm(wchar_t *d, const wchar_t *s, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
size_t wcsspn(const wchar_t *s, const wchar_t *set);
size_t wcscspn(const wchar_t *s, const wchar_t *set);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *set);
wchar_t *wcsstr(const wchar_t *hay, const wchar_t *needle);
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **save);

/* --- wide memory operations --- */
wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n);
wchar_t *wmemmove(wchar_t *d, const wchar_t *s, size_t n);
wchar_t *wmemset(wchar_t *d, wchar_t c, size_t n);
int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);

/* --- restartable multibyte <-> wide conversion (UTF-8 <-> UTF-32) --- */
int mbsinit(const mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
wint_t btowc(int c);
int wctob(wint_t c);

/* --- wide numeric conversion --- */
long wcstol(const wchar_t *s, wchar_t **end, int base);
unsigned long wcstoul(const wchar_t *s, wchar_t **end, int base);
long long wcstoll(const wchar_t *s, wchar_t **end, int base);
unsigned long long wcstoull(const wchar_t *s, wchar_t **end, int base);
double wcstod(const wchar_t *s, wchar_t **end);
float wcstof(const wchar_t *s, wchar_t **end);
long double wcstold(const wchar_t *s, wchar_t **end);

/* --- wide strftime --- */
size_t wcsftime(wchar_t *s, size_t max, const wchar_t *fmt, const struct tm *tm);

/* --- wide stream I/O (UTF-8 byte streams; each op transcodes) --- */
int fwide(FILE *fp, int mode);
wint_t fgetwc(FILE *fp);
wint_t getwc(FILE *fp);
wint_t getwchar(void);
wchar_t *fgetws(wchar_t *s, int n, FILE *fp);
wint_t fputwc(wchar_t wc, FILE *fp);
wint_t putwc(wchar_t wc, FILE *fp);
wint_t putwchar(wchar_t wc);
int fputws(const wchar_t *s, FILE *fp);
wint_t ungetwc(wint_t wc, FILE *fp);

/* --- wide formatted output (Tier B) ---
 * Same conversions as printf, written as wide characters.  %c/%s take a
 * (multibyte) char / int by default and a wchar_t / wint_t under the l
 * modifier (%lc/%ls); every numeric conversion emits the same digits as the
 * narrow engine, widened to the stream.  swprintf returns a negative value on
 * truncation (unlike snprintf). */
int fwprintf(FILE *fp, const wchar_t *fmt, ...);
int wprintf(const wchar_t *fmt, ...);
int swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...);
int vfwprintf(FILE *fp, const wchar_t *fmt, va_list ap);
int vwprintf(const wchar_t *fmt, va_list ap);
int vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap);

#endif /* _WCHAR_H */
