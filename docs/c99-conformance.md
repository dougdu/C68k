# C68K and the C99 Standard Library — Conformance Survey

This document enumerates the complete set of standard headers and standard
library functions specified by **ISO/IEC 9899:1999 (C99)**, describes what each
is for, and maps every item onto the current C68K implementation: whether it is
present, which library/file provides it, and how (if at all) its behaviour
deviates from the standard.

It is a working reference for driving the library toward conformance. Rows
marked ⚠️ or ❌ are the actionable gaps.

**Status (2026‑07‑22):** the Tier 1 gaps in the roadmap below are now
implemented and verified — `tests/lockstep/tier1.c` passes 42/42 on both Osiris
and CP/M‑68K. This includes a real `rename` (Osiris DOS 56h / CP/M BDOS 23), a
true character‑streaming `scanf`/`fscanf`/`sscanf` engine, and text/binary
`fopen` modes with Ctrl‑Z text‑EOF on CP/M. The tables reflect that work.

## Target model

C68K targets the Motorola 68000 family under two operating systems (Osiris and
CP/M‑68K). The data model is **ILP32, big‑endian**:

| Type | Width | Notes |
|------|-------|-------|
| `char` | 8 | signed |
| `short` | 16 | |
| `int`, `long`, pointer | 32 | |
| `long long` | 64 | |
| `float` | 32 | IEEE‑754 single |
| `double`, `long double` | 64 | IEEE‑754 double (`long double` == `double`) |

Floating point is provided by a soft‑float runtime (`lib/libm`), with fixed
round‑to‑nearest and no floating‑point exception machinery.

## How the library is organised

| Tag | Artifact | What it holds |
|-----|----------|---------------|
| **libc** | `libc/core/*.c` → `libc.a` | The bulk of the hosted C library (compiled by c68k itself). |
| **rt** | `lib/runtime/rt68k.a68` → linked object | Compiler runtime + `memcpy`/`memset`/`memmove` and 64‑bit/soft‑float helpers. |
| **libm** | `lib/libm/libm.a` | Soft‑float primitives + transcendentals, reached through `<math.h>` inline wrappers. |
| **libheap** | `lib/heap/libheap.a` | Vendored SOA heap backing `malloc`/`free`/`realloc`/`calloc`. |
| **hdr** | header only | Macros/types with no runtime component. |
| **osiris / cpm** | `libc/osiris/osiris_sys.a68`, `libc/cpm/{cpm.c,cpm_sys.a68}` | Per‑OS syscall seam (crt0, file I/O, RTC). |

Headers live in two roots: `include/` (freestanding: available with
`-ffreestanding`) and `libc/include/` (hosted).

## Legend

- ✅ Present and substantially conforming.
- ⚠️ Present but deviates from the standard (see Notes).
- ❌ Not implemented.

---

## Table 1 — Standard headers (C99 §7)

C99 defines 24 standard headers. Seven are *freestanding* (available without an
OS): `<float.h>`, `<iso646.h>`, `<limits.h>`, `<stdarg.h>`, `<stdbool.h>`,
`<stddef.h>`, `<stdint.h>`.

| Header | Purpose | Status | Location | Deviations / Gaps |
|--------|---------|:------:|----------|-------------------|
| `<assert.h>` | `assert` diagnostic macro | ✅ | `libc/include/assert.h`, `libc/core/assert.c` | Honours `NDEBUG`. Conforming. |
| `<complex.h>` | Complex arithmetic (`_Complex`) | ❌ | — | Not provided; compiler has no `_Complex` support. |
| `<ctype.h>` | Character classification | ✅ | `libc/include/ctype.h`, `libc/core/is*.c` | Complete (C/ASCII locale). |
| `<errno.h>` | Error numbers | ✅ | `libc/include/errno.h`, `libc/core/errno.c` | `EDOM`/`ERANGE`/`EILSEQ` now defined (plus a POSIX subset). Math routines do not yet *set* them (Tier 2). |
| `<fenv.h>` | Floating‑point environment | ❌ | — | No FP exception/rounding control (soft‑float is fixed round‑to‑nearest). |
| `<float.h>` | Floating‑type characteristics | ✅ | `include/float.h`, `libc/include/float.h` | Complete. |
| `<inttypes.h>` | Integer format conversions | ✅ | `libc/include/inttypes.h`, `libc/core/imax*.c`, `strto[iu]max.c` | Full `PRI*`/`SCN*` set; `imaxabs`/`imaxdiv`/`strtoimax`/`strtoumax` present. |
| `<iso646.h>` | Alternative operator spellings | ✅ | `libc/include/iso646.h` | Complete. |
| `<limits.h>` | Integer‑type limits | ✅ | `include/limits.h`, `libc/include/limits.h` | Complete for ILP32. |
| `<locale.h>` | Localization | ❌ | — | Only the "C" locale is implied; `setlocale`/`localeconv` absent. |
| `<math.h>` | Mathematics | ⚠️ | `libc/include/math.h` → **libm** | `double`‑only subset via `static` inline wrappers; no `float`/`long double` variants, macros, classification, or `errno`. |
| `<setjmp.h>` | Non‑local jumps | ❌ | — | `setjmp`/`longjmp` not implemented. |
| `<signal.h>` | Signal handling | ⚠️ | `libc/include/signal.h`, `libc/core/signal.c` | Synchronous only — no async delivery on these OSes; `raise` calls handlers inline. |
| `<stdarg.h>` | Variable arguments | ✅ | `include/stdarg.h` | m68k `va_list`. Conforming. |
| `<stdbool.h>` | Boolean type/values | ✅ | `include/stdbool.h`, `libc/include/stdbool.h` | Complete. |
| `<stddef.h>` | Common definitions | ✅ | `include/stddef.h` | `size_t`, `ptrdiff_t`, `wchar_t`, `NULL`, `offsetof`. |
| `<stdint.h>` | Fixed‑width integers | ✅ | `include/stdint.h` | Complete (exact/least/fast/ptr/max + limits + `*_C` macros). |
| `<stdio.h>` | Input/output | ⚠️ | `libc/include/stdio.h`, `libc/core/*.c` | Added `scanf`/`fscanf`/`vscanf`/`vfscanf`/`vprintf`/`vsprintf`/`ungetc`/`rewind`/`clearerr`/`perror`/`remove`/`rename`. Still no `freopen`/`setvbuf`/`tmp*`/`fgetpos`/wide. See §stdio. |
| `<stdlib.h>` | General utilities | ⚠️ | `libc/include/stdlib.h`, `libc/core/*.c` | Added `atoll`/`llabs`/`lldiv`/`strtof`/`_Exit`/`getenv`/`system`. Only the multibyte functions (`mblen`/`mbtowc`/…) remain absent (no wide‑char support). |
| `<string.h>` | String handling | ⚠️ | `libc/include/string.h`, `libc/core/str*.c`, **rt** | Added `strspn`/`strcspn`/`strpbrk`. Only `strcoll`/`strxfrm` remain absent (no locale). |
| `<tgmath.h>` | Type‑generic math | ❌ | — | Requires `<complex.h>` + `<math.h>` generic macros. |
| `<time.h>` | Date and time | ⚠️ | `libc/include/time.h`, `libc/core/time.c` | `clock` stubbed; `localtime`==`gmtime` (no TZ/DST); `time_t` 32‑bit. |
| `<wchar.h>` | Extended/wide characters | ❌ | — | Not provided. |
| `<wctype.h>` | Wide‑character classification | ❌ | — | Not provided. |

**Beyond C99:** C68K also ships the C11 freestanding headers `include/stdalign.h`,
`include/stdatomic.h`, `include/stdnoreturn.h`. Non‑standard (POSIX‑ish)
extension headers present: `libc/include/{strings.h, unistd.h, libgen.h,
sys/stat.h, sys/types.h}`.

---

## Table 2 — Standard library functions (C99 §7)

Grouped by header. The **Library / File** column names the providing artifact.

### `<assert.h>` — diagnostics

| Function/Macro | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `assert` | Runtime assertion, disabled by `NDEBUG` | ✅ | libc / `assert.c` (`__assert_fail`) | Prints expr/file/line, then `abort()`. |

### `<complex.h>` — complex arithmetic

Header absent; **no** complex support. All of the following (and their `f`/`l`
variants) are ❌: `cabs`, `cacos`, `cacosh`, `carg`, `casin`, `casinh`, `catan`,
`catanh`, `ccos`, `ccosh`, `cexp`, `cimag`, `clog`, `conj`, `cpow`, `cproj`,
`creal`, `csin`, `csinh`, `csqrt`, `ctan`, `ctanh`.

### `<ctype.h>` — character handling

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `isalnum` | alphanumeric | ✅ | libc / `isalnum.c` | ASCII/C locale. |
| `isalpha` | alphabetic | ✅ | libc / `isalpha.c` | |
| `isblank` | space or tab | ✅ | libc / `isblank.c` | |
| `iscntrl` | control char | ✅ | libc / `iscntrl.c` | |
| `isdigit` | decimal digit | ✅ | libc / `isdigit.c` | |
| `isgraph` | printable, non‑space | ✅ | libc / `isgraph.c` | |
| `islower` | lowercase | ✅ | libc / `islower.c` | |
| `isprint` | printable incl. space | ✅ | libc / `isprint.c` | |
| `ispunct` | punctuation | ✅ | libc / `ispunct.c` | |
| `isspace` | whitespace | ✅ | libc / `isspace.c` | |
| `isupper` | uppercase | ✅ | libc / `isupper.c` | |
| `isxdigit` | hex digit | ✅ | libc / `isxdigit.c` | |
| `tolower` | to lowercase | ✅ | libc / `tolower.c` | |
| `toupper` | to uppercase | ✅ | libc / `toupper.c` | |

### `<errno.h>` — errors

| Item | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `errno` | Last‑error lvalue | ⚠️ | libc / `errno.c` | Plain `extern int` (single‑threaded). Conforming for a hosted single‑thread. |
| `EDOM` | Domain error | ✅ | libc / `errno.h` | Value 33. Not yet *set* by math routines (Tier 2). |
| `ERANGE` | Range error | ✅ | libc / `errno.h` | Value 34. Not yet *set* by math/`strto*` (Tier 2). |
| `EILSEQ` | Illegal byte sequence | ✅ | libc / `errno.h` | Value 84. |

Also provided (POSIX numbers): `ENOENT`, `EIO`, `EBADF`, `ENOMEM`, `EACCES`,
`EEXIST`, `EINVAL`, `EMFILE`.

### `<fenv.h>` — floating‑point environment

Header absent. All ❌: `feclearexcept`, `fegetexceptflag`, `feraiseexcept`,
`fesetexceptflag`, `fetestexcept`, `fegetround`, `fesetround`, `fegetenv`,
`feholdexcept`, `fesetenv`, `feupdateenv`.

### `<inttypes.h>` — integer format conversion

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `imaxabs` | `intmax_t` abs | ✅ | libc / `imaxabs.c` | |
| `imaxdiv` | `intmax_t` div/rem | ✅ | libc / `imaxdiv.c` | `imaxdiv_t` in `<inttypes.h>`. |
| `strtoimax` | string → `intmax_t` | ✅ | libc / `strtoimax.c` | wraps `strtoll`. |
| `strtoumax` | string → `uintmax_t` | ✅ | libc / `strtoumax.c` | wraps `strtoull`. |
| `wcstoimax` | wide string → `intmax_t` | ❌ | — | Needs `<wchar.h>`. |
| `wcstoumax` | wide string → `uintmax_t` | ❌ | — | Needs `<wchar.h>`. |
| `PRI*` / `SCN*` macros | `printf`/`scanf` format macros | ✅ | hdr | Full set: `d/i/o/u/x/X` (PRI) and `d/i/o/u/x` (SCN) for 8/16/32/64/`LEAST`/`FAST`/`MAX`/`PTR`. |

### `<locale.h>` — localization

Header absent. `setlocale` ❌, `localeconv` ❌, `struct lconv` ❌.

### `<math.h>` — mathematics

Present functions are `double`‑only and implemented as **`static` inline
wrappers** in the header over libm's `d`‑suffixed primitives.

Global deviations for this header: no `float`/`long double` (`…f`/`…l`)
variants; no `errno` (`EDOM`/`ERANGE`) reporting; no domain/range checks; no
`HUGE_VAL`/`HUGE_VALF`/`HUGE_VALL`, `INFINITY`, `NAN`; no classification macros
(`fpclassify`, `isnan`, `isinf`, `isfinite`, `isnormal`, `signbit`) or comparison
macros (`isgreater`, `isless`, …). Because the wrappers are `static`, each
translation unit gets its own copy — their addresses are not unique across TUs
and they cannot satisfy an external reference.

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `sin` | sine | ✅ | libm / `math/sincos.a68` | via `sind`. |
| `cos` | cosine | ✅ | libm / `math/sincos.a68` | via `cosd`. |
| `tan` | tangent | ⚠️ | libm | Computed as `sin/cos` (no dedicated routine). |
| `asin` | arcsine | ⚠️ | libm | Derived via `atan`; reduced accuracy. |
| `acos` | arccosine | ⚠️ | libm | Derived via `asin`. |
| `atan` | arctangent | ✅ | libm / `math/atan.a68` | |
| `atan2` | arctangent of y/x | ✅ | libc header + libm | Quadrant logic in header. |
| `exp` | e^x | ✅ | libm / `math/exp.a68` | |
| `log` | natural log | ✅ | libm / `math/log.a68` | |
| `log10` | base‑10 log | ⚠️ | libm | `log(x)/M_LN10`. |
| `pow` | x^y | ✅ | libm / `core/fppwr.a68`,`math/…` | |
| `sqrt` | square root | ✅ | libm / `math/sqrt.a68` | |
| `ceil` | ceiling | ✅ | libm / `math/floor.a68` | |
| `floor` | floor | ✅ | libm / `math/floor.a68` | |
| `fabs` | absolute value | ✅ | libm / `core/fabs.a68` | |
| `fmod` | floating remainder | ✅ | libm / `core/fmod.a68` | |
| `modf` | split int/frac | ✅ | libm / `math/dpmath.a68` | |
| `sinh` `cosh` `tanh` | hyperbolic | ❌ | — | |
| `asinh` `acosh` `atanh` | inverse hyperbolic | ❌ | — | |
| `exp2` `expm1` | 2^x, e^x−1 | ❌ | — | |
| `log2` `log1p` `logb` `ilogb` | logarithms/exponent | ❌ | — | |
| `frexp` `ldexp` `scalbn` `scalbln` | exponent manipulation | ❌ | — | |
| `cbrt` `hypot` | cube root, hypotenuse | ❌ | — | |
| `erf` `erfc` `lgamma` `tgamma` | error/gamma | ❌ | — | |
| `trunc` `round` `nearbyint` `rint` | rounding | ❌ | — | |
| `lround` `llround` `lrint` `llrint` | round‑to‑integer | ❌ | — | |
| `remainder` `remquo` | IEEE remainder | ❌ | — | |
| `copysign` `nan` `nextafter` `nexttoward` | sign/representation | ❌ | — | |
| `fdim` `fmax` `fmin` `fma` | difference/max/min/FMA | ❌ | — | |

### `<setjmp.h>` — non‑local jumps

Header absent. `setjmp` ❌, `longjmp` ❌ (both need a small asm shim to
save/restore `d2‑d7/a2‑a7/pc`).

### `<signal.h>` — signal handling

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `signal` | Install handler | ⚠️ | libc / `signal.c` | Records disposition in a 32‑entry table. |
| `raise` | Raise a signal | ⚠️ | libc / `signal.c` | Dispatches synchronously (inline); `SIGABRT` default → `abort()`. |

Deviation: these OSes deliver no asynchronous signals, so only program‑generated
(`raise`) signals fire. The six C99 signals `SIGABRT`, `SIGFPE`, `SIGILL`,
`SIGINT`, `SIGSEGV`, `SIGTERM` and `sig_atomic_t` are defined.

### `<stdarg.h>` — variable arguments

| Macro | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `va_start` | Begin varargs | ✅ | hdr (`include/stdarg.h`) | m68k stack walk. |
| `va_arg` | Fetch next arg | ✅ | hdr | 4/8‑byte rounding. |
| `va_end` | Finish | ✅ | hdr | |
| `va_copy` | Copy `va_list` | ✅ | hdr | |

### `<stdbool.h>` / `<stddef.h>` / `<stdint.h>` — types & macros

All macro/type only and complete: `bool`/`true`/`false`; `size_t`,
`ptrdiff_t`, `wchar_t`, `NULL`, `offsetof`, `max_align_t`; the full fixed‑width
integer set with limits and `INT*_C`/`UINT*_C` constructors. ✅

### `<stdio.h>` — input/output

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `fopen` | Open stream | ⚠️ | libc / `fopen.c` | Modes `r`/`w`/`a` (+`b`); no `+`/update. Text streams honor a Ctrl‑Z (`0x1A`) EOF; binary (`b`) reads raw — this is what makes CP/M's record‑padded files read back at their logical length. |
| `fclose` | Close stream | ✅ | libc / `fclose.c` | |
| `fflush` | Flush buffer | ✅ | libc / `fflush.c` | |
| `freopen` | Reassign stream | ❌ | — | |
| `setbuf` / `setvbuf` | Buffering control | ❌ | — | Buffering fixed at `BUFSIZ`. |
| `remove` | Delete file | ✅ | libc / `remove.c` | wraps `sys_unlink`. |
| `rename` | Rename file | ✅ | libc / `rename.c` + seam | `sys_rename`: Osiris DOS 56h (A0=old, A1=new); CP/M BDOS 23 (combined FCB). |
| `tmpfile` / `tmpnam` | Temp files | ❌ | — | |
| `printf` | Formatted stdout | ⚠️ | libc / `printf.c`,`vformat.c` | Int/str/char, `%f/%e/%g`, width/prec/flags; no `%a`, `%n`, wide. |
| `fprintf` | Formatted to stream | ⚠️ | libc / `fprintf.c` | as `printf`. |
| `sprintf` | Formatted to buffer | ⚠️ | libc / `sprintf.c` | as `printf`. |
| `snprintf` | Bounded to buffer | ✅ | libc / `snprintf.c` | |
| `vfprintf` | va_list to stream | ✅ | libc / `vfprintf.c` | |
| `vsnprintf` | va_list, bounded | ✅ | libc / `vsnprintf.c` | |
| `vprintf` `vsprintf` | va_list print variants | ✅ | libc / `vprintf.c`,`vsprintf.c` | |
| `vscanf` `vfscanf` | va_list scan variants | ✅ | libc / `vscanf.c`,`vfscanf.c` | Share the streaming engine. |
| `scanf` `fscanf` | Formatted input | ⚠️ | libc / `scanf.c`,`fscanf.c`,`vsscanf.c` | True char-streaming scanner (`_vscan`): consumes exactly what it matches and leaves the rest in the stream. No `%[` scanset or `%a`. |
| `sscanf` | Parse from string | ⚠️ | libc / `sscanf.c`,`vsscanf.c` | `d/i/u/o/x/X/p`, `f/e/g`, `s/c/n/%`, width, `*`, `hh/h/l/ll`. No `%[`/`%a`. |
| `vsscanf` | va_list parse | ✅ | libc / `vsscanf.c` | |
| `fgetc` `getc` `getchar` | Read char | ✅ | libc / `fgetc.c`,`getc.c`,`getchar.c` | |
| `fgets` | Read line | ✅ | libc / `fgets.c` | |
| `ungetc` | Push back char | ✅ | libc / `ungetc.c` | One‑char pushback guaranteed. |
| `fputc` `putc` `putchar` | Write char | ✅ | libc / `fputc.c`,`putc.c`,`putchar.c` | |
| `fputs` `puts` | Write string | ✅ | libc / `fputs.c`,`puts.c` | |
| `gets` | Read line (unsafe) | ❌ | — | Intentionally omitted (removed in C11). |
| `fread` `fwrite` | Binary I/O | ✅ | libc / `fread.c`,`fwrite.c` | |
| `fseek` | Set position | ⚠️ | libc / `fseek.c` | `SEEK_*`; backend seek support varies. |
| `ftell` | Get position | ✅ | libc / `ftell.c` | |
| `rewind` | Reset position | ✅ | libc / `rewind.c` | `fseek(...,SEEK_SET)` + clears EOF/err. |
| `fgetpos` `fsetpos` | `fpos_t` position | ❌ | — | |
| `feof` `ferror` | Stream status | ✅ | libc / `feof.c`,`ferror.c` | |
| `clearerr` | Clear status | ✅ | libc / `clearerr.c` | |
| `perror` | Print error message | ✅ | libc / `perror.c` | Uses `strerror(errno)`. |

Macros: `EOF`, `BUFSIZ`, `SEEK_SET/CUR/END`, `stdin/stdout/stderr` present.
Missing: `FOPEN_MAX`, `FILENAME_MAX`, `TMP_MAX`, `L_tmpnam`,
`_IOFBF/_IOLBF/_IONBF`, `fpos_t`.

### `<stdlib.h>` — general utilities

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `malloc` `calloc` `realloc` `free` | Dynamic memory | ✅ | libc / `malloc.c` → **libheap** | Real reclaiming heap; `free`/`realloc` work. |
| `atoi` `atol` | string → int/long | ✅ | libc / `atoi.c`,`atol.c` | |
| `atoll` | string → long long | ✅ | libc / `atoll.c` | |
| `atof` | string → double | ⚠️ | libc / `atof.c` | via libm `atod`. |
| `strtol` `strtoul` | string → (u)long | ✅ | libc / `strtol.c`,`strtoul.c` | |
| `strtoll` `strtoull` | string → (u)long long | ✅ | libc / `strtoll.c`,`strtoull.c` | |
| `strtod` | string → double | ✅ | libc / `strtod.c` | |
| `strtof` | string → float | ✅ | libc / `strtof.c` | narrows `strtod`. |
| `strtold` | string → long double | ⚠️ | libc / `strtold.c` | `long double`==`double`. |
| `rand` `srand` | PRNG | ⚠️ | libc / `rand.c` | LCG, `RAND_MAX` 32767 (minimum compliant). |
| `abs` `labs` | int/long abs | ✅ | libc / `abs.c`,`labs.c` | |
| `llabs` | long long abs | ✅ | libc / `llabs.c` | |
| `div` `ldiv` | int/long div+rem | ✅ | libc / `div.c`,`ldiv.c` | |
| `lldiv` | long long div+rem | ✅ | libc / `lldiv.c` | `lldiv_t` in `<stdlib.h>`. |
| `bsearch` | Binary search | ✅ | libc / `bsearch.c` | |
| `qsort` | Sort | ⚠️ | libc / `qsort.c` | Shell sort (conforming behaviour, not the named algorithm). |
| `abort` | Abnormal exit | ⚠️ | libc / `exit.c` | Implemented as `exit(1)`: runs `atexit` handlers + flushes, and does **not** raise `SIGABRT`. |
| `atexit` | Register exit handler | ✅ | libc / `exit.c` | LIFO table, 32 handlers (meets the C99 minimum). |
| `exit` | Normal exit | ✅ | libc / `exit.c` | Flushes streams. |
| `_Exit` | Exit w/o cleanup | ✅ | libc / `_Exit.c` | No `atexit`/flush. |
| `getenv` | Environment lookup | ⚠️ | libc / `getenv.c` | Always returns `NULL` (no environment on these OSes). |
| `system` | Run command | ⚠️ | libc / `system.c` | `system(NULL)`→0 (no processor); any command→−1. |
| `mblen` `mbtowc` `wctomb` `mbstowcs` `wcstombs` | Multibyte/wide | ❌ | — | No wide‑char support. |

### `<string.h>` — string handling

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `memcpy` | Copy memory | ✅ | **rt** / `rt68k.a68` | |
| `memmove` | Copy (overlapping) | ✅ | **rt** / `rt68k.a68` | |
| `memset` | Fill memory | ✅ | **rt** / `rt68k.a68` | |
| `memcmp` | Compare memory | ✅ | libc / `memcmp.c` | |
| `memchr` | Find byte | ✅ | libc / `memchr.c` | |
| `strcpy` `strncpy` | Copy string | ✅ | libc / `strcpy.c`,`strncpy.c` | |
| `strcat` `strncat` | Concatenate | ✅ | libc / `strcat.c`,`strncat.c` | |
| `strcmp` `strncmp` | Compare | ✅ | libc / `strcmp.c`,`strncmp.c` | |
| `strcoll` | Locale compare | ❌ | — | No locale. |
| `strxfrm` | Locale transform | ❌ | — | No locale. |
| `strchr` `strrchr` | Find char | ✅ | libc / `strchr.c`,`strrchr.c` | |
| `strspn` `strcspn` `strpbrk` | Span/find‑set | ✅ | libc / `strspn.c`,`strcspn.c`,`strpbrk.c` | |
| `strstr` | Find substring | ✅ | libc / `strstr.c` | |
| `strtok` | Tokenize | ✅ | libc / `strtok.c` | |
| `strlen` | Length | ✅ | libc / `strlen.c` | |
| `strerror` | Error → message | ⚠️ | libc / `strerror.c` | Limited message set. |

### `<tgmath.h>` — type‑generic math

Header absent; requires `<complex.h>` + generic `<math.h>` macros. All
type‑generic macros ❌.

### `<time.h>` — date and time

| Function | Purpose | Status | Library / File | Notes |
|---|---|:--:|---|---|
| `time` | Current time | ✅ | libc / `time.c` + seam | Reads RTC via `sys_time`. |
| `clock` | Processor time | ⚠️ | libc / `time.c` | Returns `(clock_t)-1` (no CPU‑time source). |
| `difftime` | Seconds between | ✅ | libc / `time.c` | via soft float. |
| `mktime` | `struct tm` → `time_t` | ✅ | libc / `time.c` | Normalizes fields. |
| `gmtime` | `time_t` → UTC `tm` | ✅ | libc / `time.c` | |
| `localtime` | `time_t` → local `tm` | ⚠️ | libc / `time.c` | Identical to `gmtime` (no TZ/DST). |
| `asctime` | `tm` → string | ✅ | libc / `time.c` | |
| `ctime` | `time_t` → string | ✅ | libc / `time.c` | |
| `strftime` | Formatted time | ⚠️ | libc / `time.c` | Subset of specifiers (`%Y%y%m%d%e%H%M%S%j%a%b%h%p%%`). |

`time_t`/`clock_t` are 32‑bit signed (valid through 2038).

### `<wchar.h>` and `<wctype.h>` — wide characters

Both headers absent. **All** wide‑stream I/O (`fwprintf`, `wprintf`, `fwscanf`,
`getwc`, `putwc`, `fgetws`, `fputws`, `ungetwc`, …), wide string/number
conversion (`wcstod/f/l`, `wcstol/ul/ll/ull`, `wcscpy`, `wcscmp`, `wcschr`,
`wcsstr`, `wcstok`, `wcslen`, `wmemcpy`, `wmemcmp`, `wmemset`, `wcsftime`, …),
restartable multibyte conversion (`mbrtowc`, `wcrtomb`, `mbsrtowcs`,
`wcsrtombs`, `mbrlen`, `mbsinit`, `btowc`, `wctob`), and wide classification
(`iswalnum`, `iswalpha`, `iswblank`, `iswcntrl`, `iswdigit`, `iswgraph`,
`iswlower`, `iswprint`, `iswpunct`, `iswspace`, `iswupper`, `iswxdigit`,
`towlower`, `towupper`, `wctype`, `iswctype`, `wctrans`, `towctrans`) are ❌.

---

## Non‑standard extensions currently provided

These are present in C68K but are **not** part of C99 (mostly POSIX). They are
listed for completeness so they are not mistaken for standard coverage.

| Item | Header | Library / File |
|---|---|---|
| `strcasecmp`, `strncasecmp` | `<strings.h>` | libc / `strcasecmp.c` |
| `strdup`, `strndup` | `<string.h>` (ext) | libc / `strdup.c`,`strndup.c` |
| `dirname`, `basename` | `<libgen.h>` | libc / `dirname.c`,`basename.c` |
| `open_memstream` | `<stdio.h>` (ext) | libc / `open_memstream.c` |
| `ctime_r` | `<time.h>` (ext) | libc / `time.c` |
| `unlink`, `close` | `<unistd.h>` | libc / `unlink.c`,`close.c` |
| `stat`, `fstat` | `<sys/stat.h>` | libc / `stat.c` (stub: always fails) |

---

## Conformance gap summary & suggested roadmap

Ordered roughly by value‑to‑effort. None of these require compiler changes
except where noted.

### Tier 1 — small, high‑value fixes — ✅ DONE (2026‑07‑22)
Implemented as pure header/libc additions and verified by
`tests/lockstep/tier1.c` (27/27 on Osiris and CP/M‑68K):
1. ✅ **`<errno.h>`**: `EDOM`, `ERANGE`, `EILSEQ` defined.
2. ✅ **`<ctype.h>`**: `isblank`, `isgraph`.
3. ✅ **`<string.h>`**: `strspn`, `strcspn`, `strpbrk`.
4. ✅ **`<stdlib.h>`**: `atoll`, `llabs`, `lldiv`, `strtof`, `_Exit`, `getenv`
   (→`NULL`), `system` (`NULL`→0 else −1).
5. ✅ **`<stdio.h>`**: `rewind`, `clearerr`, `perror`, `ungetc`, `remove`,
   `vprintf`, `vsprintf`, and `scanf`/`fscanf`/`vscanf`/`vfscanf` (layered on
   `vsscanf`); also `hh` length support added to the scanner.
6. ✅ **`<inttypes.h>`**: `imaxabs`, `imaxdiv`, `strtoimax`, `strtoumax`, and the
   complete `PRI*`/`SCN*` macro set.

All Tier 1 items are complete, including a true character‑streaming
`scanf`/`fscanf`/`sscanf` engine (`_vscan` in `libc/core/vsscanf.c`, shared by the
string and stream entry points). Remaining scanf gaps — the `%[` scanset and
`%a` hex‑float — are minor and tracked with the broader conversion‑coverage work.

### Tier 2 — moderate
7. **`<math.h>` correctness**: give the functions **external** linkage (real
   library symbols in libm/libc instead of `static` inline), add `HUGE_VAL`/
   `INFINITY`/`NAN`, `EDOM`/`ERANGE` handling, and the classification macros
   (`isnan`, `isinf`, `isfinite`, `signbit`, `fpclassify`). Then broaden the
   function set (`sinh`…`atanh`, `log2`, `exp2`, `cbrt`, `hypot`, `trunc`,
   `round`, `copysign`, `frexp`, `ldexp`, `fmax`, `fmin`, `fdim`, `fma`, …) and
   add the `f`/`l` variants (may alias the `double` routines given
   `long double`==`double`).
8. **`<setjmp.h>`**: `setjmp`/`longjmp` via a small asm shim (save/restore
   `d2‑d7`, `a2‑a7`, PC).
9. **`<locale.h>`**: minimal "C"‑only `setlocale`/`localeconv`.

### Tier 3 — large or blocked
10. **`<wchar.h>` / `<wctype.h>`**: sizeable; needs a multibyte/wide strategy.
11. **`<fenv.h>`**: limited by the soft‑float runtime (fixed rounding, no flags).
12. **`<complex.h>` / `<tgmath.h>`**: blocked on **compiler** `_Complex` support;
    defer until the front end grows complex types.

### Known behavioural deviations to document (not necessarily "fix")
- `localtime` == `gmtime` (no timezone database).
- `clock()` returns `-1` (no CPU‑time counter on target).
- `signal`/`raise` are synchronous only (no async delivery).
- `abort()` runs `atexit` handlers and flushes streams and does not raise `SIGABRT` (it is `exit(1)`).
- `time_t` is 32‑bit (Year‑2038 limit).
- `long double` == `double`.
