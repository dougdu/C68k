// c68k freestanding <limits.h> --- big-endian ILP32 (m68k).
// int/long = 32-bit, long long = 64-bit, char is signed.
#ifndef __LIMITS_H
#define __LIMITS_H

#define CHAR_BIT   8

#define SCHAR_MIN  (-128)
#define SCHAR_MAX  127
#define UCHAR_MAX  255

// c68k: `char` is signed (m68k default).
#define CHAR_MIN   SCHAR_MIN
#define CHAR_MAX   SCHAR_MAX

#define MB_LEN_MAX 1

#define SHRT_MIN   (-SHRT_MAX - 1)
#define SHRT_MAX   32767
#define USHRT_MAX  65535

#define INT_MIN    (-INT_MAX - 1)
#define INT_MAX    2147483647
#define UINT_MAX   4294967295U

// ILP32: `long` is 32-bit (same range as int).
#define LONG_MIN   (-LONG_MAX - 1L)
#define LONG_MAX   2147483647L
#define ULONG_MAX  4294967295UL

#define LLONG_MIN  (-LLONG_MAX - 1LL)
#define LLONG_MAX  9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

#endif
