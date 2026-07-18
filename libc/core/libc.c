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
 * process exit -- flush all output streams, then hand off to the OS.
 * ===================================================================== */
void exit(int code) {
  fflush(NULL);
  sys_exit(code);
  for (;;)
    ;
}

void abort(void) { exit(1); }
