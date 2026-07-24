#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *_progname = "";

const char *getprogname(void) { return _progname; }
void setprogname(const char *name) { _progname = name ? name : ""; }

static void prefix(void) {
  if (_progname[0])
    fprintf(stderr, "%s: ", _progname);
}

void vwarnx(const char *fmt, va_list ap) {
  prefix();
  if (fmt)
    vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void vwarn(const char *fmt, va_list ap) {
  int e = errno;
  prefix();
  if (fmt) {
    vfprintf(stderr, fmt, ap);
    fputs(": ", stderr);
  }
  fputs(strerror(e), stderr);
  fputc('\n', stderr);
}

void verrx(int eval, const char *fmt, va_list ap) {
  vwarnx(fmt, ap);
  exit(eval);
}

void verr(int eval, const char *fmt, va_list ap) {
  vwarn(fmt, ap);
  exit(eval);
}

void warnx(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vwarnx(fmt, ap);
  va_end(ap);
}

void warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vwarn(fmt, ap);
  va_end(ap);
}

void errx(int eval, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verrx(eval, fmt, ap);
  va_end(ap);
}

void err(int eval, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verr(eval, fmt, ap);
  va_end(ap);
}
