#include <stdio.h>
#include <stdarg.h>

int vsprintf(char *buf, const char *fmt, va_list ap) {
  return vsnprintf(buf, 0x7fffffff, fmt, ap);
}
