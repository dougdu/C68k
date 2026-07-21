#include <stdlib.h>
#include <ctype.h>

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
