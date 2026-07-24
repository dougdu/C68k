#ifndef _ERR_H
#define _ERR_H

#include <stdarg.h>

/* BSD-style error reporting.  All write to stderr in the form
 *   "[progname: ]message[: strerror(errno)]\n"
 * where progname comes from setprogname()/getprogname() (empty by default, in
 * which case the prefix is omitted).  err/errx also exit(eval); warn/warnx
 * return.  The x-suffixed variants omit the strerror(errno) suffix. */
void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);
void verr(int eval, const char *fmt, va_list ap);
void verrx(int eval, const char *fmt, va_list ap);
void vwarn(const char *fmt, va_list ap);
void vwarnx(const char *fmt, va_list ap);

#endif /* _ERR_H */
