#include <stdlib.h>

/* Convert v to a string in the given base (2..36), most-significant digit
 * first, with an optional leading '-'.  Shared by itoa/utoa/ltoa/ultoa. */
static char *conv(unsigned long v, char *buf, int base, int neg) {
  char tmp[33];
  int i = 0;
  if (base < 2 || base > 36)
    base = 10;
  if (v == 0)
    tmp[i++] = '0';
  while (v) {
    unsigned d = (unsigned)(v % (unsigned)base);
    tmp[i++] = (d < 10) ? (char)('0' + d) : (char)('a' + d - 10);
    v /= (unsigned)base;
  }
  char *p = buf;
  if (neg)
    *p++ = '-';
  while (i)
    *p++ = tmp[--i];
  *p = '\0';
  return buf;
}

char *itoa(int value, char *buf, int base) {
  if (base == 10 && value < 0)
    return conv((unsigned long)(-(long long)value), buf, base, 1);
  return conv((unsigned long)(unsigned int)value, buf, base, 0);
}

char *utoa(unsigned value, char *buf, int base) {
  return conv((unsigned long)value, buf, base, 0);
}

char *ltoa(long value, char *buf, int base) {
  if (base == 10 && value < 0)
    return conv((unsigned long)(-(long long)value), buf, base, 1);
  return conv((unsigned long)value, buf, base, 0);
}

char *ultoa(unsigned long value, char *buf, int base) {
  return conv(value, buf, base, 0);
}
