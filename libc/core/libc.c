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
#include <sys/stat.h>

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

unsigned long long strtoull(const char *s, char **end, int base) {
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
  unsigned long long val = 0;
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
    val = val * (unsigned long long)base + (unsigned long long)d;
    s++;
  }
  if (end)
    *end = (char *)s;
  return neg ? -val : val;
}

long long strtoll(const char *s, char **end, int base) {
  return (long long)strtoull(s, end, base);
}

char *strerror(int errnum) {
  switch (errnum) {
  case 0:
    return "Success";
  case ENOENT:
    return "No such file or directory";
  case EINVAL:
    return "Invalid argument";
  case EMFILE:
    return "Too many open files";
  default:
    return "Error";
  }
}

char *strtok(char *s, const char *delim) {
  static char *save;
  if (!s)
    s = save;
  if (!s)
    return NULL;
  while (*s && strchr(delim, (unsigned char)*s))
    s++;
  if (!*s) {
    save = NULL;
    return NULL;
  }
  char *tok = s;
  while (*s && !strchr(delim, (unsigned char)*s))
    s++;
  if (*s) {
    *s = 0;
    save = s + 1;
  } else {
    save = NULL;
  }
  return tok;
}

long double strtold(const char *s, char **end) { return strtod(s, end); }

/* ---------------------------------------------------------------------------
 * 64-bit integer <-> IEEE float/double conversions -- the soft-float runtime
 * helpers the code generator emits by name (_fplltod etc.).  Built by
 * decomposition over the 32-bit conversions the back end already supports plus
 * IEEE double arithmetic, so the result is correctly rounded without a
 * dedicated 64-bit float routine.
 * ------------------------------------------------------------------------- */
#define _P32 4294967296.0                /* 2^32 */
#define _P31 2147483648.0                /* 2^31 */

/* uint32 -> double (the signed-only 32-bit primitive can't see bit 31). */
static double fp_u32d(unsigned long u) {
  if (u & 0x80000000UL)
    return (double)(long)(u & 0x7FFFFFFFUL) + _P31;
  return (double)(long)u;
}

/* double in [0, 2^32) -> uint32. */
static unsigned long fp_d32u(double d) {
  if (d >= _P31)
    return (unsigned long)(long)(d - _P31) + 0x80000000UL;
  return (unsigned long)(long)d;
}

/* c68k prefixes C symbols with '_', so `fplltod` here is the `_fplltod` the
 * code generator emits. */
double fpulltod(unsigned long long u) {
  unsigned long hi = (unsigned long)(u >> 32);
  unsigned long lo = (unsigned long)u;
  return fp_u32d(hi) * _P32 + fp_u32d(lo);
}

double fplltod(long long v) {
  if (v < 0)
    return -fpulltod(-(unsigned long long)v);
  return fpulltod((unsigned long long)v);
}

float fpulltof(unsigned long long u) { return (float)fpulltod(u); }
float fplltof(long long v) { return (float)fplltod(v); }

unsigned long long fpdtoull(double d) {
  unsigned long h = fp_d32u(d / _P32);   /* high 32 bits ~ floor(d / 2^32) */
  double lo = d - fp_u32d(h) * _P32;      /* remainder, nominally [0, 2^32) */
  if (lo < 0.0) {                         /* correct a 1-off from rounding   */
    h--;
    lo += _P32;
  } else if (lo >= _P32) {
    h++;
    lo -= _P32;
  }
  return ((unsigned long long)h << 32) | fp_d32u(lo);
}

long long fpdtoll(double d) {
  if (d < 0)
    return -(long long)fpdtoull(-d);
  return (long long)fpdtoull(d);
}

unsigned long long fpftoull(float f) { return fpdtoull((double)f); }
long long fpftoll(float f) { return fpdtoll((double)f); }

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

/* open_memstream() growth: append n bytes to fp->mem, keeping it NUL-
 * terminated, and republish the user's *ptr / *sizeloc.  Returns 0 / -1. */
static int _memstream_append(FILE *fp, const void *data, int n) {
  size_t need = fp->memlen + (size_t)n + 1;
  if (need > fp->memcap) {
    size_t ncap = fp->memcap ? fp->memcap : 64;
    while (ncap < need)
      ncap *= 2;
    unsigned char *nm = realloc(fp->mem, ncap);
    if (!nm)
      return -1;
    fp->mem = nm;
    fp->memcap = ncap;
  }
  if (n)
    memcpy(fp->mem + fp->memlen, data, n);
  fp->memlen += n;
  fp->mem[fp->memlen] = 0;
  if (fp->memuptr)
    *fp->memuptr = (char *)fp->mem;
  if (fp->memusize)
    *fp->memusize = fp->memlen;
  return 0;
}

int fflush(FILE *fp) {
  if (!fp) {
    for (int i = 0; i < NSTREAM; i++)
      if ((_streams[i].flags & (_SF_USED | _SF_WRITE)) == (_SF_USED | _SF_WRITE))
        fflush(&_streams[i]);
    return 0;
  }
  if ((fp->flags & _SF_WRITE) && fp->cnt > 0) {
    if (fp->flags & _SF_MEM) {
      if (_memstream_append(fp, fp->buf, fp->cnt) != 0) {
        fp->flags |= _SF_ERR;
        return EOF;
      }
    } else if (sys_write(fp->fd, fp->buf, fp->cnt) != fp->cnt) {
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
  int r = 0;
  if (fp->flags & _SF_MEM) {
    if (!fp->mem)                 /* empty stream still yields a valid buffer */
      _memstream_append(fp, "", 0);
    if (fp->memuptr)
      *fp->memuptr = (char *)fp->mem;
    if (fp->memusize)
      *fp->memusize = fp->memlen;
  } else {
    r = sys_close(fp->fd);
  }
  fp->flags = 0;
  return r;
}

/* POSIX open_memstream(): a write stream backed by a malloc'd buffer that
 * grows on flush; on fclose the caller owns *ptr (size in *sizeloc). */
FILE *open_memstream(char **ptr, size_t *sizeloc) {
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
  fp->fd = -1;
  fp->flags = _SF_WRITE | _SF_MEM | _SF_USED;
  fp->cnt = 0;
  fp->p = fp->buf;
  fp->mem = NULL;
  fp->memcap = 0;
  fp->memlen = 0;
  fp->memuptr = ptr;
  fp->memusize = sizeloc;
  if (ptr)
    *ptr = NULL;
  if (sizeloc)
    *sizeloc = 0;
  return fp;
}

/* =====================================================================
 * self-host support: <string.h>/<strings.h>/<libgen.h>/<sys/stat.h>
 * helpers the compiler's own source needs.
 * ===================================================================== */
char *strdup(const char *s) {
  size_t n = strlen(s);
  char *p = malloc(n + 1);
  if (p)
    memcpy(p, s, n + 1);
  return p;
}

char *strndup(const char *s, size_t n) {
  size_t len = 0;
  while (len < n && s[len])
    len++;
  char *p = malloc(len + 1);
  if (p) {
    memcpy(p, s, len);
    p[len] = 0;
  }
  return p;
}

static int _lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strncasecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int ca = _lc((unsigned char)a[i]), cb = _lc((unsigned char)b[i]);
    if (ca != cb)
      return ca - cb;
    if (!ca)
      return 0;
  }
  return 0;
}

int strcasecmp(const char *a, const char *b) {
  for (;; a++, b++) {
    int ca = _lc((unsigned char)*a), cb = _lc((unsigned char)*b);
    if (ca != cb)
      return ca - cb;
    if (!ca)
      return 0;
  }
}

/* dirname()/basename(): may modify `path` (POSIX).  Handle both separators. */
char *dirname(char *path) {
  if (!path || !*path)
    return ".";
  char *end = path + strlen(path) - 1;
  while (end > path && (*end == '/' || *end == '\\'))
    *end-- = 0;
  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;
  if (!slash)
    return ".";
  if (slash == path) {
    path[1] = 0;
    return path;
  }
  *slash = 0;
  return path;
}

char *basename(char *path) {
  if (!path || !*path)
    return ".";
  char *end = path + strlen(path) - 1;
  while (end > path && (*end == '/' || *end == '\\'))
    *end-- = 0;
  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;
  return slash ? slash + 1 : path;
}

/* No general stat() on these targets: fail so __TIMESTAMP__ takes its
 * "unknown" fallback (the compiler reads nothing else from stat). */
int stat(const char *path, struct stat *st) {
  (void)path;
  (void)st;
  errno = EINVAL;
  return -1;
}

int fstat(int fd, struct stat *st) {
  (void)fd;
  (void)st;
  errno = EINVAL;
  return -1;
}

int unlink(const char *path) { return sys_unlink(path); }
int close(int fd) { return sys_close(fd); }

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

    int left = 0, zero = 0, plus = 0, space = 0;
    for (;; fmt++) {
      if (*fmt == '-')
        left = 1;
      else if (*fmt == '0')
        zero = 1;
      else if (*fmt == '+')
        plus = 1;
      else if (*fmt == ' ')
        space = 1;
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
        sign = plus ? '+' : (space ? ' ' : 0);
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
      } else {
        sign = plus ? '+' : (space ? ' ' : 0);
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
 * <time.h> -- wall clock over the seam. The per-OS backend fills a
 * broken-down calendar (year/mon/mday/hour/min/sec) via sys_time(); the
 * epoch/calendar arithmetic is done here, once, in portable C.
 *
 * Calendar math is Howard Hinnant's public-domain civil<->days algorithm
 * (days relative to 1970-01-01). __days_from_civil / __civil_from_days are
 * exported so the CP/M seam (cpm.c), which only gets days-since-1978 from
 * BDOS 105, can reuse the same conversion.
 * ===================================================================== */
#include <time.h>

struct __sysdt {
  long year; /* full year, e.g. 2026 */
  long mon;  /* 1..12 */
  long mday; /* 1..31 */
  long hour; /* 0..23 */
  long min;  /* 0..59 */
  long sec;  /* 0..59 */
};
extern int sys_time(struct __sysdt *dt); /* 0 = ok, -1 = no clock */

/* days since 1970-01-01 for a Gregorian y-m-d (m,d may be out of range;
   the result stays linear in d, which mktime() relies on to normalize). */
long __days_from_civil(int y, int m, int d) {
  y -= (m <= 2);
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;                                  /* [0,399] */
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0,365] */
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* [0,146096] */
  return (long)era * 146097 + doe - 719468;
}

/* inverse: days since 1970-01-01 -> y/m/d. */
void __civil_from_days(long z, int *py, int *pm, int *pd) {
  z += 719468;
  int era = (int)((z >= 0 ? z : z - 146096) / 146097);
  int doe = (int)(z - (long)era * 146097);                   /* [0,146096] */
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0,399] */
  int y = yoe + era * 400;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100); /* [0,365] */
  int mp = (5 * doy + 2) / 153;                      /* [0,11] */
  int d = doy - (153 * mp + 2) / 5 + 1;              /* [1,31] */
  int m = mp + (mp < 10 ? 3 : -9);                   /* [1,12] */
  *py = y + (m <= 2);
  *pm = m;
  *pd = d;
}

static struct tm _tm_buf;
static const char _wday_abbr[7][4] = {"Sun", "Mon", "Tue", "Wed",
                                      "Thu", "Fri", "Sat"};
static const char _mon_abbr[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

time_t time(time_t *timer) {
  struct __sysdt dt;
  if (sys_time(&dt) != 0) {
    if (timer)
      *timer = (time_t)-1;
    return (time_t)-1;
  }
  long days = __days_from_civil((int)dt.year, (int)dt.mon, (int)dt.mday);
  time_t t = days * 86400L + dt.hour * 3600L + dt.min * 60L + dt.sec;
  if (timer)
    *timer = t;
  return t;
}

clock_t clock(void) { return (clock_t)-1; } /* no CPU-time source */

double difftime(time_t end, time_t start) { return (double)(end - start); }

struct tm *gmtime(const time_t *timer) {
  time_t t = *timer;
  long days = t / 86400L;
  long rem = t % 86400L;
  if (rem < 0) {
    rem += 86400L;
    days -= 1;
  }
  int y, m, d;
  __civil_from_days(days, &y, &m, &d);
  _tm_buf.tm_year = y - 1900;
  _tm_buf.tm_mon = m - 1;
  _tm_buf.tm_mday = d;
  _tm_buf.tm_hour = (int)(rem / 3600);
  _tm_buf.tm_min = (int)((rem % 3600) / 60);
  _tm_buf.tm_sec = (int)(rem % 60);
  int wd = (int)((days % 7 + 4) % 7); /* 1970-01-01 was a Thursday (4) */
  if (wd < 0)
    wd += 7;
  _tm_buf.tm_wday = wd;
  _tm_buf.tm_yday = (int)(days - __days_from_civil(y, 1, 1));
  _tm_buf.tm_isdst = 0;
  return &_tm_buf;
}

struct tm *localtime(const time_t *timer) { return gmtime(timer); }

time_t mktime(struct tm *tm) {
  int y = tm->tm_year + 1900;
  int mo = tm->tm_mon; /* 0-based; may be out of range */
  int yadj = mo / 12;
  mo -= yadj * 12;
  y += yadj;
  if (mo < 0) {
    mo += 12;
    y -= 1;
  }
  long days = __days_from_civil(y, mo + 1, tm->tm_mday);
  time_t t =
      days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
  *tm = *gmtime(&t); /* normalize the caller's struct */
  return t;
}

char *asctime(const struct tm *tm) {
  static char buf[32];
  int wd = tm->tm_wday % 7;
  int mo = tm->tm_mon % 12;
  if (wd < 0)
    wd += 7;
  if (mo < 0)
    mo += 12;
  sprintf(buf, "%s %s %2d %02d:%02d:%02d %d\n", _wday_abbr[wd], _mon_abbr[mo],
          tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
  return buf;
}

char *ctime(const time_t *timer) { return asctime(localtime(timer)); }

char *ctime_r(const time_t *timer, char *buf) {
  char *s = asctime(localtime(timer));
  size_t i = 0;
  while (s[i]) {
    buf[i] = s[i];
    i++;
  }
  buf[i] = 0;
  return buf;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
  size_t n = 0;
  char tmp[16];
  const char *p;
#define _PUT(ch)                                                               \
  do {                                                                         \
    if (n + 1 < max)                                                           \
      s[n] = (char)(ch);                                                       \
    n++;                                                                       \
  } while (0)
#define _PUTS(str)                                                             \
  do {                                                                         \
    for (p = (str); *p; p++)                                                   \
      _PUT(*p);                                                                \
  } while (0)
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      _PUT(*fmt);
      continue;
    }
    fmt++;
    switch (*fmt) {
    case 'Y':
      sprintf(tmp, "%d", tm->tm_year + 1900);
      _PUTS(tmp);
      break;
    case 'y':
      sprintf(tmp, "%02d", (tm->tm_year + 1900) % 100);
      _PUTS(tmp);
      break;
    case 'm':
      sprintf(tmp, "%02d", tm->tm_mon + 1);
      _PUTS(tmp);
      break;
    case 'd':
      sprintf(tmp, "%02d", tm->tm_mday);
      _PUTS(tmp);
      break;
    case 'e':
      sprintf(tmp, "%2d", tm->tm_mday);
      _PUTS(tmp);
      break;
    case 'H':
      sprintf(tmp, "%02d", tm->tm_hour);
      _PUTS(tmp);
      break;
    case 'M':
      sprintf(tmp, "%02d", tm->tm_min);
      _PUTS(tmp);
      break;
    case 'S':
      sprintf(tmp, "%02d", tm->tm_sec);
      _PUTS(tmp);
      break;
    case 'j':
      sprintf(tmp, "%03d", tm->tm_yday + 1);
      _PUTS(tmp);
      break;
    case 'a':
      _PUTS(_wday_abbr[(tm->tm_wday % 7 + 7) % 7]);
      break;
    case 'b':
    case 'h':
      _PUTS(_mon_abbr[(tm->tm_mon % 12 + 12) % 12]);
      break;
    case 'p':
      _PUTS(tm->tm_hour < 12 ? "AM" : "PM");
      break;
    case '%':
      _PUT('%');
      break;
    case '\0':
      fmt--;
      break;
    default:
      _PUT('%');
      _PUT(*fmt);
      break;
    }
  }
#undef _PUT
#undef _PUTS
  if (max)
    s[n < max ? n : max - 1] = '\0';
  return n < max ? n : 0;
}

/* =====================================================================
 * process exit -- flush all output streams, then hand off to the OS.
 * ===================================================================== */
/* atexit handlers, run LIFO at normal exit. */
#define _ATEXIT_MAX 32
static void (*_atexit_fns[_ATEXIT_MAX])(void);
static int _atexit_n;

int atexit(void (*fn)(void)) {
  if (_atexit_n >= _ATEXIT_MAX)
    return -1;
  _atexit_fns[_atexit_n++] = fn;
  return 0;
}

void exit(int code) {
  while (_atexit_n > 0)
    _atexit_fns[--_atexit_n]();
  fflush(NULL);
  sys_exit(code);
  for (;;)
    ;
}

void abort(void) { exit(1); }
