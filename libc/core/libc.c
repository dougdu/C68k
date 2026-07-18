/*
 * libc.c --- c68k minimal C library core (OS-independent).
 *
 * string / ctype / stdlib(malloc over sbrk) / errno and a small buffered
 * <stdio.h>. All OS access goes through the syscall seam (sys_*), which each
 * target provides (Osiris: libc/osiris/osiris_sys.a68). printf (variadic) is
 * intentionally not here yet -- it needs m68k va_arg codegen (follow-up).
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

/* ---- syscall seam (asm _sys_*; C name has no leading underscore) ---- */
extern int sys_write(int fd, const void *buf, int n);
extern int sys_read(int fd, void *buf, int n);
extern int sys_open(const char *path, int mode);
extern int sys_creat(const char *path, int attr);
extern int sys_close(int fd);
extern long sys_seek(int fd, long off, int whence);
extern int sys_unlink(const char *path);
extern void sys_exit(int code);
extern void *sys_sbrk(int delta);

/* soft-float runtime helpers used by %f/%e/%g formatting (libieee754d). */
extern long fpdtol(double);
extern double floord(double);

int errno;

/* =====================================================================
 * <string.h>   (memcpy/memset/memmove come from the runtime, rt68k)
 * ===================================================================== */
size_t strlen(const char *s) {
  const char *p = s;
  while (*p)
    p++;
  return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (n == 0)
    return 0;
  return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++))
    ;
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n && src[i]; i++)
    dst[i] = src[i];
  for (; i < n; i++)
    dst[i] = 0;
  return dst;
}

char *strcat(char *dst, const char *src) {
  strcpy(dst + strlen(dst), src);
  return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
  char *d = dst + strlen(dst);
  size_t i = 0;
  for (; i < n && src[i]; i++)
    d[i] = src[i];
  d[i] = 0;
  return dst;
}

char *strchr(const char *s, int c) {
  for (; *s; s++)
    if (*s == (char)c)
      return (char *)s;
  return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  for (; *s; s++)
    if (*s == (char)c)
      last = s;
  if (c == 0)
    return (char *)s;
  return (char *)last;
}

char *strstr(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  if (nl == 0)
    return (char *)hay;
  for (; *hay; hay++)
    if (strncmp(hay, needle, nl) == 0)
      return (char *)hay;
  return NULL;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = a, *pb = b;
  for (; n; n--, pa++, pb++)
    if (*pa != *pb)
      return (int)*pa - (int)*pb;
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  for (; n; n--, p++)
    if (*p == (unsigned char)c)
      return (void *)p;
  return NULL;
}

/* =====================================================================
 * <ctype.h>
 * ===================================================================== */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isxdigit(int c) {
  return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

/* =====================================================================
 * <stdlib.h> -- bump allocator over sys_sbrk (free is a no-op).
 * ===================================================================== */
void *malloc(size_t n) {
  size_t total = (n + 4 + 3) & ~(size_t)3; /* 4-byte size header, rounded */
  char *p = sys_sbrk((int)total);
  if (p == (char *)-1) {
    errno = ENOMEM;
    return NULL;
  }
  *(size_t *)p = n;
  return p + 4;
}

void free(void *p) { (void)p; }

void *calloc(size_t nmemb, size_t size) {
  size_t n = nmemb * size;
  void *p = malloc(n);
  if (p)
    memset(p, 0, n);
  return p;
}

void *realloc(void *p, size_t n) {
  if (!p)
    return malloc(n);
  size_t old = *(size_t *)((char *)p - 4);
  void *q = malloc(n);
  if (q)
    memcpy(q, p, old < n ? old : n);
  return q;
}

int abs(int n) { return n < 0 ? -n : n; }

long strtol(const char *s, char **end, int base) {
  while (isspace((unsigned char)*s))
    s++;
  int neg = 0;
  if (*s == '+' || *s == '-')
    neg = (*s++ == '-');
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    base = 16;
  } else if (base == 0 && s[0] == '0') {
    base = 8;
  } else if (base == 0) {
    base = 10;
  }
  long val = 0;
  for (;;) {
    int c = (unsigned char)*s;
    int d;
    if (isdigit(c))
      d = c - '0';
    else if (c >= 'a' && c <= 'z')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z')
      d = c - 'A' + 10;
    else
      break;
    if (d >= base)
      break;
    val = val * base + d;
    s++;
  }
  if (end)
    *end = (char *)s;
  return neg ? -val : val;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

unsigned long strtoul(const char *s, char **end, int base) {
  while (isspace((unsigned char)*s))
    s++;
  if (*s == '+')
    s++;
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    base = 16;
  } else if (base == 0 && s[0] == '0') {
    base = 8;
  } else if (base == 0) {
    base = 10;
  }
  unsigned long val = 0;
  for (;;) {
    int c = (unsigned char)*s;
    int d;
    if (isdigit(c))
      d = c - '0';
    else if (c >= 'a' && c <= 'z')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z')
      d = c - 'A' + 10;
    else
      break;
    if (d >= base)
      break;
    val = val * (unsigned long)base + (unsigned long)d;
    s++;
  }
  if (end)
    *end = (char *)s;
  return val;
}

/* String->double lives in the soft-float archive (returns D0:D1). */
extern double atod(const char *s);

double atof(const char *s) { return atod(s); }

double strtod(const char *s, char **end) {
  const char *p = s;
  while (isspace((unsigned char)*p))
    p++;
  const char *start = p;
  if (*p == '+' || *p == '-')
    p++;
  while (isdigit((unsigned char)*p))
    p++;
  if (*p == '.') {
    p++;
    while (isdigit((unsigned char)*p))
      p++;
  }
  if (*p == 'e' || *p == 'E') {
    const char *e = p + 1;
    if (*e == '+' || *e == '-')
      e++;
    if (isdigit((unsigned char)*e)) {
      p = e;
      while (isdigit((unsigned char)*p))
        p++;
    }
  }
  if (end)
    *end = (char *)p;
  return atod(start);
}

long labs(long n) { return n < 0 ? -n : n; }

div_t div(int num, int den) {
  div_t r;
  r.quot = num / den;
  r.rem = num % den;
  return r;
}

ldiv_t ldiv(long num, long den) {
  ldiv_t r;
  r.quot = num / den;
  r.rem = num % den;
  return r;
}

static unsigned long _rand_state = 1;

void srand(unsigned seed) { _rand_state = seed; }

int rand(void) {
  _rand_state = _rand_state * 1103515245UL + 12345UL;
  return (int)((_rand_state >> 16) & 0x7FFF);
}

static void _swap(char *a, char *b, size_t size) {
  while (size--) {
    char t = *a;
    *a++ = *b;
    *b++ = t;
  }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *)) {
  char *a = (char *)base;
  for (size_t gap = nmemb / 2; gap > 0; gap /= 2) {
    for (size_t i = gap; i < nmemb; i++) {
      for (size_t j = i; j >= gap; j -= gap) {
        char *x = a + (j - gap) * size;
        char *y = a + j * size;
        if (cmp(x, y) <= 0)
          break;
        _swap(x, y, size);
      }
    }
  }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
  const char *a = (const char *)base;
  size_t lo = 0, hi = nmemb;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = cmp(key, a + mid * size);
    if (c < 0)
      hi = mid;
    else if (c > 0)
      lo = mid + 1;
    else
      return (void *)(a + mid * size);
  }
  return NULL;
}

void __assert_fail(const char *expr, const char *file, int line) {
  fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
  abort();
}

/* =====================================================================
 * <signal.h> -- minimal, synchronous. These OSes deliver no async
 * signals, so signal() just records a disposition and raise() dispatches
 * it directly (SIGABRT default terminates via abort()).
 * ===================================================================== */
#define _NSIG 32
static void (*_sigtab[_NSIG])(int);

void (*signal(int sig, void (*func)(int)))(int) {
  if (sig < 1 || sig >= _NSIG)
    return (void (*)(int)) - 1; /* SIG_ERR */
  void (*old)(int) = _sigtab[sig];
  _sigtab[sig] = func;
  return old;
}

int raise(int sig) {
  if (sig < 1 || sig >= _NSIG)
    return -1;
  void (*h)(int) = _sigtab[sig];
  if (h == (void (*)(int))1) /* SIG_IGN */
    return 0;
  if (h != (void (*)(int))0) { /* installed handler */
    h(sig);
    return 0;
  }
  /* SIG_DFL */
  if (sig == 6) /* SIGABRT */
    abort();
  return 0;
}

/* =====================================================================
 * <stdio.h> -- small buffered streams over the seam.
 * ===================================================================== */
#define NSTREAM 11
static FILE _streams[NSTREAM] = {
    {0, _SF_READ | _SF_USED, 0, 0, {0}},   /* stdin  */
    {1, _SF_WRITE | _SF_USED, 0, 0, {0}},  /* stdout */
    {2, _SF_WRITE | _SF_USED, 0, 0, {0}},  /* stderr */
};
FILE *stdin = &_streams[0];
FILE *stdout = &_streams[1];
FILE *stderr = &_streams[2];

int fflush(FILE *fp) {
  if (!fp) {
    for (int i = 0; i < NSTREAM; i++)
      if ((_streams[i].flags & (_SF_USED | _SF_WRITE)) == (_SF_USED | _SF_WRITE))
        fflush(&_streams[i]);
    return 0;
  }
  if ((fp->flags & _SF_WRITE) && fp->cnt > 0) {
    if (sys_write(fp->fd, fp->buf, fp->cnt) != fp->cnt) {
      fp->flags |= _SF_ERR;
      return EOF;
    }
    fp->cnt = 0;
  }
  return 0;
}

int fputc(int c, FILE *fp) {
  if (!(fp->flags & _SF_WRITE)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  fp->buf[fp->cnt++] = (unsigned char)c;
  if (fp->cnt == BUFSIZ || c == '\n')
    if (fflush(fp) == EOF)
      return EOF;
  return (unsigned char)c;
}

int putc(int c, FILE *fp) { return fputc(c, fp); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *fp) {
  while (*s)
    if (fputc((unsigned char)*s++, fp) == EOF)
      return EOF;
  return 0;
}

int puts(const char *s) {
  if (fputs(s, stdout) == EOF)
    return EOF;
  return fputc('\n', stdout);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
  const unsigned char *p = ptr;
  size_t total = size * nmemb;
  for (size_t i = 0; i < total; i++)
    if (fputc(p[i], fp) == EOF)
      return size ? i / size : 0;
  return nmemb;
}

static int _fill(FILE *fp) {
  int n = sys_read(fp->fd, fp->buf, BUFSIZ);
  if (n <= 0) {
    fp->flags |= (n == 0) ? _SF_EOF : _SF_ERR;
    fp->cnt = 0;
    return EOF;
  }
  fp->cnt = n;
  fp->p = fp->buf;
  return 0;
}

int fgetc(FILE *fp) {
  if (!(fp->flags & _SF_READ)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  if (fp->cnt == 0 && _fill(fp) == EOF)
    return EOF;
  fp->cnt--;
  return *fp->p++;
}

int getc(FILE *fp) { return fgetc(fp); }
int getchar(void) { return fgetc(stdin); }

char *fgets(char *s, int n, FILE *fp) {
  int i = 0;
  while (i < n - 1) {
    int c = fgetc(fp);
    if (c == EOF)
      break;
    s[i++] = (char)c;
    if (c == '\n')
      break;
  }
  if (i == 0)
    return NULL;
  s[i] = 0;
  return s;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
  unsigned char *p = ptr;
  size_t total = size * nmemb;
  for (size_t i = 0; i < total; i++) {
    int c = fgetc(fp);
    if (c == EOF)
      return size ? i / size : 0;
    p[i] = (unsigned char)c;
  }
  return nmemb;
}

FILE *fopen(const char *path, const char *mode) {
  FILE *fp = NULL;
  for (int i = 3; i < NSTREAM; i++)
    if (!(_streams[i].flags & _SF_USED)) {
      fp = &_streams[i];
      break;
    }
  if (!fp) {
    errno = EMFILE;
    return NULL;
  }

  int fd, flags;
  if (mode[0] == 'r') {
    fd = sys_open(path, 0);
    flags = _SF_READ;
  } else if (mode[0] == 'w') {
    fd = sys_creat(path, 0);
    flags = _SF_WRITE;
  } else if (mode[0] == 'a') {
    fd = sys_open(path, 1);
    if (fd < 0)
      fd = sys_creat(path, 0);
    if (fd >= 0)
      sys_seek(fd, 0, SEEK_END);
    flags = _SF_WRITE;
  } else {
    errno = EINVAL;
    return NULL;
  }
  if (fd < 0) {
    errno = ENOENT;
    return NULL;
  }

  fp->fd = fd;
  fp->flags = flags | _SF_USED;
  fp->cnt = 0;
  fp->p = fp->buf;
  return fp;
}

int fclose(FILE *fp) {
  if (!fp || !(fp->flags & _SF_USED))
    return EOF;
  fflush(fp);
  int r = sys_close(fp->fd);
  fp->flags = 0;
  return r;
}

int fseek(FILE *fp, long off, int whence) {
  fflush(fp);
  fp->cnt = 0;
  fp->p = fp->buf;
  fp->flags &= ~_SF_EOF;
  return sys_seek(fp->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *fp) { return sys_seek(fp->fd, 0, SEEK_CUR); }
int feof(FILE *fp) { return (fp->flags & _SF_EOF) != 0; }
int ferror(FILE *fp) { return (fp->flags & _SF_ERR) != 0; }

/* =====================================================================
 * printf family -- integer / string / char formatting over a sink that is
 * either a FILE (printf/fprintf) or a bounded buffer (snprintf).
 * ===================================================================== */
typedef struct {
  FILE *fp;
  char *buf;
  int cap;
  int len;
} _psink;

static void _emit(_psink *s, int c) {
  if (s->fp)
    fputc(c, s->fp);
  else if (s->buf && s->len + 1 < s->cap)
    s->buf[s->len] = (char)c;
  s->len++;
}

static int _u64toa(unsigned long long v, int base, int upper, char *out) {
  const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[24];
  int n = 0;
  do {
    tmp[n++] = digs[v % base];
    v /= base;
  } while (v);
  for (int i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  out[n] = 0;
  return n;
}

/* Fixed-point (%f): format v >= 0 with `prec` fractional digits. The integer
   part is assumed to fit a 32-bit long (fpdtol truncates). */
static int fmt_fixed(double v, int prec, char *buf) {
  double r = 0.5;
  for (int i = 0; i < prec; i++)
    r = r / 10.0;
  v = v + r; /* round */
  double ip = floord(v);
  double frac = v - ip;
  int n = 0;
  long li = fpdtol(ip);
  char tmp[16];
  int t = 0;
  if (li == 0)
    tmp[t++] = '0';
  while (li > 0) {
    tmp[t++] = (char)('0' + (int)(li % 10));
    li = li / 10;
  }
  while (t > 0)
    buf[n++] = tmp[--t];
  if (prec > 0) {
    buf[n++] = '.';
    for (int i = 0; i < prec; i++) {
      frac = frac * 10.0;
      double d = floord(frac);
      buf[n++] = (char)('0' + (int)fpdtol(d));
      frac = frac - d;
    }
  }
  buf[n] = 0;
  return n;
}

/* Scientific (%e): d.ddde+XX with `prec` fractional digits. */
static int fmt_sci(double v, int prec, char *buf) {
  int exp = 0;
  if (v != 0.0) {
    while (v >= 10.0) {
      v = v / 10.0;
      exp++;
    }
    while (v < 1.0) {
      v = v * 10.0;
      exp--;
    }
  }
  int n = fmt_fixed(v, prec, buf);
  buf[n++] = 'e';
  buf[n++] = (char)(exp < 0 ? '-' : '+');
  int ae = exp < 0 ? -exp : exp;
  buf[n++] = (char)('0' + (ae / 10) % 10);
  buf[n++] = (char)('0' + ae % 10);
  buf[n] = 0;
  return n;
}

/* General (%g): pick %e for very large/small magnitudes, else %f. */
static int fmt_gen(double v, int prec, char *buf) {
  if (prec <= 0)
    prec = 1;
  int exp = 0;
  double t = v;
  if (t != 0.0) {
    while (t >= 10.0) {
      t = t / 10.0;
      exp++;
    }
    while (t < 1.0) {
      t = t * 10.0;
      exp--;
    }
  }
  if (exp < -4 || exp >= prec)
    return fmt_sci(v, prec - 1, buf);
  int fp = prec - 1 - exp;
  return fmt_fixed(v, fp > 0 ? fp : 0, buf);
}

static int _vformat(_psink *s, const char *fmt, va_list ap) {
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      _emit(s, (unsigned char)*fmt);
      continue;
    }
    fmt++;

    int left = 0, zero = 0;
    for (;; fmt++) {
      if (*fmt == '-')
        left = 1;
      else if (*fmt == '0')
        zero = 1;
      else
        break;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9')
      width = width * 10 + (*fmt++ - '0');
    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      while (*fmt >= '0' && *fmt <= '9')
        prec = prec * 10 + (*fmt++ - '0');
    }
    int lng = 0;
    while (*fmt == 'l') {
      lng++;
      fmt++;
    }
    while (*fmt == 'h')
      fmt++;

    char numbuf[64];
    const char *str = numbuf;
    int slen = 0;
    char sign = 0;

    switch (*fmt) {
    case 'd':
    case 'i': {
      long long v = (lng >= 2) ? va_arg(ap, long long) : va_arg(ap, long);
      unsigned long long uv;
      if (v < 0) {
        sign = '-';
        uv = (unsigned long long)(-v);
      } else {
        uv = (unsigned long long)v;
      }
      slen = _u64toa(uv, 10, 0, numbuf);
      break;
    }
    case 'u':
    case 'x':
    case 'X':
    case 'o': {
      unsigned long long uv =
          (lng >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
      int base = (*fmt == 'x' || *fmt == 'X') ? 16 : (*fmt == 'o') ? 8 : 10;
      slen = _u64toa(uv, base, *fmt == 'X', numbuf);
      break;
    }
    case 'c':
      numbuf[0] = (char)va_arg(ap, int);
      numbuf[1] = 0;
      slen = 1;
      break;
    case 's':
      str = va_arg(ap, const char *);
      if (!str)
        str = "(null)";
      while (str[slen] && (prec < 0 || slen < prec))
        slen++;
      break;
    case 'p': {
      unsigned long uv = (unsigned long)va_arg(ap, void *);
      numbuf[0] = '0';
      numbuf[1] = 'x';
      slen = _u64toa(uv, 16, 0, numbuf + 2) + 2;
      break;
    }
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G': {
      double dv = va_arg(ap, double);
      int p = (prec < 0) ? 6 : prec;
      if (dv < 0.0) {
        sign = '-';
        dv = -dv;
      }
      if (*fmt == 'e' || *fmt == 'E')
        slen = fmt_sci(dv, p, numbuf);
      else if (*fmt == 'g' || *fmt == 'G')
        slen = fmt_gen(dv, p, numbuf);
      else
        slen = fmt_fixed(dv, p, numbuf);
      break;
    }
    case '%':
      _emit(s, '%');
      continue;
    default:
      _emit(s, '%');
      _emit(s, (unsigned char)*fmt);
      continue;
    }

    int total = slen + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;
    if (!left && !zero)
      while (pad-- > 0)
        _emit(s, ' ');
    if (sign)
      _emit(s, sign);
    if (!left && zero)
      while (pad-- > 0)
        _emit(s, '0');
    for (int i = 0; i < slen; i++)
      _emit(s, (unsigned char)str[i]);
    if (left)
      while (pad-- > 0)
        _emit(s, ' ');
  }
  return s->len;
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
  _psink s;
  s.fp = fp;
  s.buf = NULL;
  s.cap = 0;
  s.len = 0;
  return _vformat(&s, fmt, ap);
}

int fprintf(FILE *fp, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfprintf(fp, fmt, ap);
  va_end(ap);
  return n;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  _psink s;
  s.fp = NULL;
  s.buf = buf;
  s.cap = (int)size;
  s.len = 0;
  _vformat(&s, fmt, ap);
  if (size > 0)
    buf[s.len < (int)size ? s.len : (int)size - 1] = 0;
  return s.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int sprintf(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, 0x7fffffff, fmt, ap);
  va_end(ap);
  return n;
}

/* =====================================================================
 * sscanf -- formatted input from a NUL-terminated string.
 * Supports %d %i %u %o %x %c %s %f/%e/%g %%, field width, '*' suppress,
 * and length modifiers h / l / ll.
 * ===================================================================== */
int vsscanf(const char *s, const char *fmt, va_list ap) {
  int count = 0;
  for (; *fmt; fmt++) {
    if (isspace((unsigned char)*fmt)) {
      while (isspace((unsigned char)*s))
        s++;
      continue;
    }
    if (*fmt != '%') {
      if (*s != *fmt)
        break;
      s++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (*s != '%')
        break;
      s++;
      continue;
    }
    int suppress = 0;
    if (*fmt == '*') {
      suppress = 1;
      fmt++;
    }
    int width = 0, haswidth = 0;
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt++ - '0');
      haswidth = 1;
    }
    int lng = 0, shrt = 0;
    while (*fmt == 'l' || *fmt == 'h') {
      if (*fmt == 'l')
        lng++;
      else
        shrt++;
      fmt++;
    }
    char conv = *fmt;
    if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
        conv == 'x' || conv == 'X' || conv == 'p') {
      while (isspace((unsigned char)*s))
        s++;
      int base = (conv == 'x' || conv == 'X' || conv == 'p')
                     ? 16
                     : (conv == 'o' ? 8 : 10);
      char *end;
      int is_signed = (conv == 'd' || conv == 'i');
      unsigned long uv;
      long sv;
      if (is_signed) {
        sv = strtol(s, &end, base);
        uv = (unsigned long)sv;
      } else {
        uv = strtoul(s, &end, base);
        sv = (long)uv;
      }
      if (end == s)
        break;
      s = end;
      if (!suppress) {
        if (lng >= 2)
          *va_arg(ap, long long *) = (long long)sv;
        else if (lng == 1)
          *va_arg(ap, long *) = sv;
        else if (shrt)
          *va_arg(ap, short *) = (short)sv;
        else
          *va_arg(ap, int *) = (int)sv;
        count++;
      }
    } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' ||
               conv == 'G') {
      while (isspace((unsigned char)*s))
        s++;
      char *end;
      double dv = strtod(s, &end);
      if (end == s)
        break;
      s = end;
      if (!suppress) {
        if (lng)
          *va_arg(ap, double *) = dv;
        else
          *va_arg(ap, float *) = (float)dv;
        count++;
      }
    } else if (conv == 's') {
      while (isspace((unsigned char)*s))
        s++;
      if (!*s)
        break;
      int w = haswidth ? width : 0x7fffffff;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (*s && !isspace((unsigned char)*s) && k < w) {
        if (out)
          out[k] = *s;
        k++;
        s++;
      }
      if (out)
        out[k] = '\0';
      if (!suppress)
        count++;
    } else if (conv == 'c') {
      int w = haswidth ? width : 1;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (*s && k < w) {
        if (out)
          out[k] = *s;
        k++;
        s++;
      }
      if (k < w)
        break;
      if (!suppress)
        count++;
    } else {
      break;
    }
  }
  return count;
}

int sscanf(const char *s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsscanf(s, fmt, ap);
  va_end(ap);
  return n;
}

/* =====================================================================
 * process exit -- flush all output streams, then hand off to the OS.
 * ===================================================================== */
void exit(int code) {
  fflush(NULL);
  sys_exit(code);
  for (;;)
    ;
}

void abort(void) { exit(1); }
