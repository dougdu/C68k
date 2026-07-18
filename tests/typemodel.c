// c68k P1 type-model test --- big-endian ILP32 (m68k).
//
// This is a COMPILE-TIME test: `c68k -Iinclude -S -o- tests/typemodel.c` exits 0
// only if the front end reports the correct ILP32-BE model. It needs no
// assembler, linker, or execution, so it runs identically on every host
// (Linux, macOS, Windows/MSVC) --- see the `type-check` make target.
//
//  * `#if`/`#error` validates the predefined macros, <limits.h>, and <stdint.h>.
//  * TM_ASSERT() validates the type SYSTEM's sizeof/_Alignof/offsetof using a
//    GNU case-range whose bounds invert (LO > HI -> "empty case range") when the
//    condition is false, which is a hard front-end error.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Predefined macros
// ---------------------------------------------------------------------------
#if __SIZEOF_SHORT__ != 2
#error "short != 2"
#endif
#if __SIZEOF_INT__ != 4
#error "int != 4"
#endif
#if __SIZEOF_LONG__ != 4
#error "long != 4 (ILP32)"
#endif
#if __SIZEOF_LONG_LONG__ != 8
#error "long long != 8"
#endif
#if __SIZEOF_POINTER__ != 4
#error "pointer != 4"
#endif
#if __SIZEOF_SIZE_T__ != 4
#error "size_t != 4"
#endif
#if __SIZEOF_FLOAT__ != 4 || __SIZEOF_DOUBLE__ != 8 || __SIZEOF_LONG_DOUBLE__ != 8
#error "float/double sizes"
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "not big-endian"
#endif
#ifndef __m68k__
#error "__m68k__ not defined"
#endif
#if defined(__x86_64__) || defined(__LP64__) || defined(__linux__)
#error "stale host macros still defined"
#endif
#if __INT_MAX__ != 2147483647 || __LONG_MAX__ != 2147483647L
#error "predefined *_MAX"
#endif

// ---------------------------------------------------------------------------
// <limits.h> (ILP32)
// ---------------------------------------------------------------------------
#if CHAR_BIT != 8
#error "CHAR_BIT"
#endif
#if SCHAR_MAX != 127 || SHRT_MAX != 32767 || INT_MAX != 2147483647
#error "signed maxima"
#endif
#if LONG_MAX != 2147483647L
#error "LONG_MAX (ILP32)"
#endif
#if LLONG_MAX != 9223372036854775807LL
#error "LLONG_MAX"
#endif
#if UINT_MAX != 4294967295U || ULONG_MAX != 4294967295UL
#error "unsigned maxima"
#endif

// ---------------------------------------------------------------------------
// <stdint.h> (ILP32)
// ---------------------------------------------------------------------------
#if INT32_MAX != 2147483647 || UINT32_MAX != 4294967295U
#error "int32 limits"
#endif
#if INT64_MAX != 9223372036854775807LL
#error "int64 limits"
#endif
#if SIZE_MAX != 4294967295UL || INTPTR_MAX != 2147483647L || PTRDIFF_MAX != 2147483647L
#error "pointer-width limits"
#endif

// ---------------------------------------------------------------------------
// Type system: sizeof / _Alignof / offsetof
// ---------------------------------------------------------------------------
typedef struct { char a; int b; }       S_ci;
typedef struct { char a; short b; }      S_cs;
typedef struct { char a; long long b; }  S_cll;
typedef struct { char a; double b; }     S_cd;

// case N ... (N + (cond ? 0 : -1)): compiles iff cond is true.
#define TM_ASSERT(cond) case __LINE__ ... (__LINE__ + ((cond) ? 0 : -1)):

static int type_model_checks(int x) {
  switch (x) {
    // sizes
    TM_ASSERT(sizeof(char) == 1)
    TM_ASSERT(sizeof(short) == 2)
    TM_ASSERT(sizeof(int) == 4)
    TM_ASSERT(sizeof(long) == 4)
    TM_ASSERT(sizeof(long long) == 8)
    TM_ASSERT(sizeof(float) == 4)
    TM_ASSERT(sizeof(double) == 8)
    TM_ASSERT(sizeof(long double) == 8)
    TM_ASSERT(sizeof(void *) == 4)
    TM_ASSERT(sizeof(int *) == 4)
    TM_ASSERT(sizeof(_Bool) == 1)
    TM_ASSERT(sizeof(size_t) == 4)
    TM_ASSERT(sizeof(ptrdiff_t) == 4)
    TM_ASSERT(sizeof(intptr_t) == 4)
    TM_ASSERT(sizeof(int32_t) == 4)
    TM_ASSERT(sizeof(int64_t) == 8)

    // alignments (2-byte max on m68k)
    TM_ASSERT(_Alignof(char) == 1)
    TM_ASSERT(_Alignof(short) == 2)
    TM_ASSERT(_Alignof(int) == 2)
    TM_ASSERT(_Alignof(long) == 2)
    TM_ASSERT(_Alignof(long long) == 2)
    TM_ASSERT(_Alignof(float) == 2)
    TM_ASSERT(_Alignof(double) == 2)
    TM_ASSERT(_Alignof(void *) == 2)

    // struct layout / offsets (big-endian byte offsets are endian-neutral)
    TM_ASSERT(sizeof(S_ci) == 6)
    TM_ASSERT(offsetof(S_ci, b) == 2)
    TM_ASSERT(sizeof(S_cs) == 4)
    TM_ASSERT(offsetof(S_cs, b) == 2)
    TM_ASSERT(sizeof(S_cll) == 10)
    TM_ASSERT(offsetof(S_cll, b) == 2)
    TM_ASSERT(sizeof(S_cd) == 10)
    TM_ASSERT(offsetof(S_cd, b) == 2)

    // integer-literal typing
    TM_ASSERT(sizeof(1) == 4)
    TM_ASSERT(sizeof(1L) == 4)
    TM_ASSERT(sizeof(1LL) == 8)
    TM_ASSERT(sizeof(1U) == 4)
    TM_ASSERT(sizeof(3000000000) == 8)
    TM_ASSERT(sizeof(0xFFFFFFFF) == 4)
    ;
  }
  return x;
}
