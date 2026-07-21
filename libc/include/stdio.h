#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define BUFSIZ 512
#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Stream flags. */
#define _SF_READ 0x01
#define _SF_WRITE 0x02
#define _SF_EOF 0x04
#define _SF_ERR 0x08
#define _SF_USED 0x10
#define _SF_MEM 0x20

typedef struct _FILE {
  int fd;      /* Osiris file handle */
  int flags;   /* _SF_* */
  int cnt;     /* input: bytes left in buf; output: bytes buffered */
  unsigned char *p;              /* input: next byte to serve */
  unsigned char buf[BUFSIZ];
  /* open_memstream() backing store (mem == NULL for ordinary streams). */
  unsigned char *mem;
  size_t memcap;
  size_t memlen;
  char **memuptr;
  size_t *memusize;
  /* memstream drain hook: append n bytes to the stream's malloc'd buffer.
   * Non-NULL only for open_memstream() streams, so fflush/fclose (stdio core)
   * never name _memstream_append/realloc unless the program uses memstreams. */
  int (*drain)(struct _FILE *fp, const void *data, int n);
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
int fflush(FILE *fp);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);

int fgetc(FILE *fp);
int getc(FILE *fp);
int getchar(void);
char *fgets(char *s, int n, FILE *fp);

int fputc(int c, FILE *fp);
int putc(int c, FILE *fp);
int putchar(int c);
int fputs(const char *s, FILE *fp);
int puts(const char *s);

int fseek(FILE *fp, long off, int whence);
long ftell(FILE *fp);
int feof(FILE *fp);
int ferror(FILE *fp);

int printf(const char *fmt, ...);
int fprintf(FILE *fp, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int sscanf(const char *s, const char *fmt, ...);
int vsscanf(const char *s, const char *fmt, va_list ap);

FILE *open_memstream(char **ptr, size_t *sizeloc);

#endif /* _STDIO_H */
