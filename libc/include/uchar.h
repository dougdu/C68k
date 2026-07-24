#ifndef _UCHAR_H
#define _UCHAR_H

#include <stddef.h>
#include <stdint.h>

typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;

#ifndef __MBSTATE_T_DEFINED
#define __MBSTATE_T_DEFINED
typedef struct {
  unsigned char __nbytes;   /* UTF-8 bytes accumulated so far */
  unsigned char __explen;   /* expected total bytes of the sequence */
  unsigned char __buf[4];   /* the accumulated bytes */
  unsigned short __pending; /* a held UTF-16 surrogate, or 0 */
} mbstate_t;
#endif

/* Conversions between the multibyte encoding (UTF-8 in the C locale here) and
 * UTF-16/UTF-32.  mbrtoc16 emits a surrogate pair over two calls (the second
 * returns (size_t)-3); c16rtomb holds a high surrogate until the low one. */
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#endif /* _UCHAR_H */
