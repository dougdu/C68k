# c68k — Programmer's Reference Manual

> **Status:** Draft 1.0 (2026-07) · Applies to `c68k` 0.1.0.
> The normative reference for the language model, ABI, driver, optimizations, **every supported
> standard-library function**, the syscall seam, and the toolchain. For task-oriented guidance see
> the [User's Manual](users-manual.md); for design rationale see [architecture.md](architecture.md)
> and [libc-and-toolchain.md](libc-and-toolchain.md).

## Contents

1. [Scope & conventions](#1-scope--conventions)
2. [Language & type model](#2-language--type-model)
3. [Calling convention & ABI](#3-calling-convention--abi)
4. [Object format & relocations](#4-object-format--relocations)
5. [Compiler driver reference](#5-compiler-driver-reference)
6. [Optimizations](#6-optimizations)
7. [Standard library reference](#7-standard-library-reference)
8. [The syscall seam](#8-the-syscall-seam)
9. [Runtime support library](#9-runtime-support-library)
10. [Toolchain reference](#10-toolchain-reference)
11. [Platform reference: Osiris vs CP/M-68K](#11-platform-reference-osiris-vs-cpm-68k)

---

## 1. Scope & conventions

c68k is a retarget of **chibicc** (a C11 front end) to the MC68000 with a fresh 68000 code generator
and a small hosted C library. The front end accepts C99/C11; the library is a **hosted C99 subset**
([§7.15](#715-conformance-scope)). Function signatures below are given exactly as declared in
`libc/include/`. "Osiris" means Osiris DOS (OS/68K); "CP/M" means CP/M-68K.

## 2. Language & type model

The front end is chibicc's C11 preprocessor + parser, retargeted to **ILP32 big-endian**.

### 2.1 Type sizes

| Type | Size | Notes |
| --- | --- | --- |
| `char` (signed) / `_Bool` | 1 | |
| `short` | 2 | |
| `int` | **4** | 32-bit `int` (ILP32) |
| `long` | 4 | distinct from `long long` |
| `long long` | 8 | software-emulated ([§9](#9-runtime-support-library)) |
| pointer, `size_t`, `ptrdiff_t` | 4 | flat 32-bit address space |
| `float` | 4 | IEEE-754 single, soft-float |
| `double`, `long double` | 8 | IEEE-754 double, soft-float (`long double` == `double`) |

- **Endianness:** big-endian; multi-byte scalars and struct layout are MSB-first.
- **Alignment:** the GNU `m68k-elf` SysV default — **2-byte for every type ≥ 16 bits**, `char` to 1.
  The 68000 traps on odd word/long accesses; the compiler guarantees even offsets for its own output.
- **`int` is 32-bit.** A future `-mshort` (16-bit `int`) and `-malign-int` (4-byte) are reserved,
  non-default knobs.

### 2.2 Predefined macros

| Macro | Value / when |
| --- | --- |
| `__c68k__` | `1` — always (compiler identity) |
| `__osiris__` | `1` under `-target osiris` |
| `__CPM68K__` | `1` under `-target cpm` |
| `__m68k__`, `__mc68000__` | `1` |
| `__BYTE_ORDER__` == `__ORDER_BIG_ENDIAN__` | big-endian |
| `__SIZEOF_INT__`=4, `__SIZEOF_LONG__`=4, `__SIZEOF_POINTER__`=4, `__SIZEOF_LONG_LONG__`=8 | ILP32 |
| `__STDC_HOSTED__` | `1`, or `0` under `-ffreestanding` |
| `__STDC_VERSION__` | `201112L` |

`-target` affects **only** preprocessing; the emitted object is byte-identical regardless of target.

### 2.3 Language exclusions

- **Variable-length arrays** are not supported (use a fixed bound or `malloc`).
- No hardware-FPU codegen (soft-float only); `_Complex`, `<fenv.h>` exceptions, `<tgmath.h>`, and
  threads are out of scope.

## 3. Calling convention & ABI

A standard m68k C ABI, register-compatible with the Osiris ABI, so c68k output and hand-written m68k
assembly intercall without thunks.

| Aspect | Convention |
| --- | --- |
| Argument passing | on the **stack**, pushed **right-to-left**; **caller** cleans up (cdecl) |
| Integer / pointer return | **`D0`** (`D0:D1` for 64-bit `long long`, `D0` = high word) |
| Float / double return | `D0` / `D0:D1` as the IEEE bit pattern (no FPU) |
| Struct / union return | caller passes a **hidden pointer** as the leftmost argument; the callee copies the result there and returns that pointer in `D0` |
| Scratch (caller-saved) | **`D0`, `D1`, `A0`, `A1`** |
| Preserved (callee-saved) | **`D2–D7`, `A2–A6`** |
| Frame pointer | **`A6`** (`LINK A6,#-frame` / `UNLK A6`) |
| Stack pointer | **`A7`** (`SP`); full-descending, even-aligned |
| Varargs | all arguments on the stack; the callee reads successive stack slots |

Narrow scalar arguments occupy a 4-byte slot (the value in the low bytes = the high address of the
slot, big-endian). `main` is called as `main(argc, argv, envp)`; `argv` is tokenised from the
command tail by `crt0`.

## 4. Object format & relocations

- **Objects:** ELF32 **big-endian** (`ELFCLASS32` / `ELFDATA2MSB`), `EM_68K`, relocatable
  (`ET_REL`). Sections: `.text`, `.data`, `.bss`, `.rodata`, `.rela.text`/`.rela.data`, `.symtab`,
  `.strtab` (+ `.debug_*` and their `.rela` under `-g`).
- **Relocations:** `R_68K_32` (absolute 32-bit) and `R_68K_PC16` (PC-relative 16-bit).
- **Osiris `.PRG`:** an ELF32-MSB **static-PIE** — the loader applies only `R_68K_RELATIVE` fixups;
  the ELF *is* the executable. Global/data references are absolute words the linker turns into
  relative fixups (absolute avoids the ±32 KB reach limit of PC-relative).
- **CP/M-68K `.68K`:** a **DRI-format** image produced by linking at TPA base `0x500` and converting
  with `mkdri`, which writes the base-page/segment headers and DRI relocation fixups.

## 5. Compiler driver reference

```
c68k [options] file...
```

| Option | Meaning |
| --- | --- |
| `-c` | compile + assemble to `.o`; do not link |
| `-S` | compile to `.s` (68000 assembly) |
| `-E` | preprocess only |
| `-o <path>` | output path (`-` = stdout) |
| `-I <dir>` | add include search directory |
| `-D <m>[=v]` / `-U <m>` | define / undefine macro |
| `-include <f>` | implicit leading `#include "<f>"` |
| `-target osiris` \| `cpm` (also `os68k`, `cpm68k`, …) | predefine the OS macro |
| `-O0`/`-O1`/`-O2`/`-O3`/`-Os`/`-Ofast`/`-O` | optimization level; `≥ 1` maps to the single tier |
| `-g` / `-g0` | emit / suppress DWARF debug info |
| `-Werror` | warnings become errors; other `-W*` accepted and ignored |
| `-ffreestanding` | `__STDC_HOSTED__ = 0`, freestanding headers only |
| `-fpic` / `-fPIC` | position-independent code |
| `-fintegrated-as` | emit the ELF object directly (no external assembler) |
| `--version` / `--help` | print and exit `0` |

**Diagnostics** are GCC/Clang-style `file:line:col:` with a source line, a caret, and an explicit
`error:` / `warning:` label; a fatal driver error is prefixed `c68k: error:`. **Exit codes:** `0`
success; non-zero on any error (or on a warning under `-Werror`).

**Passes.** `cc1` preprocesses → parses → runs the code generator (Motorola-syntax text) into an
in-memory buffer; the driver then either writes `.s` (`-S`), or assembles it to an ELF `.o` — via the
**integrated emitter** (`-fintegrated-as` / `C68K_INTEGRATED_AS`) or external `asm68K`.

## 6. Optimizations

Enabled at `-O1`+ (a single tier; `-O2`/`-O3`/`-Os`/`-Ofast` are aliases). All are
semantics-preserving and verified by the full lockstep suite on both OSes; `-O0` output is
byte-identical to the pre-optimization compiler (which keeps the self-host guarantee).

| Transform | Description |
| --- | --- |
| **Immediate-operand selection** | a constant right operand is folded into an immediate instruction, skipping the stack round-trip: `ADD`→`addq`/`add.l`, `SUB`, `AND`/`OR`/`XOR`→`andi`/`ori`/`eori`, comparisons→`cmp.l #imm`, shifts→`asl`/`lsr`/`asr #n` |
| **Strength reduction** | `× 2ᵏ` → `asl.l #k`; unsigned `÷ 2ᵏ` → `lsr.l #k`; unsigned `% 2ᵏ` → `andi.l #(2ᵏ−1)`; `× 0/1/−1` → `moveq`/nop/`neg`. Signed div/mod and non-power-of-two multiply stay on the runtime helpers. |
| **Peephole** | removes the address↔data register round-trips (`move.l a0,d0` / `movea.l d0,a0`) the stack machine leaves on every load, iterated to a fixpoint |
| **Addressing-mode fold** | folds a variable's address into the load: `lea D(a6),a0` / `move.l (a0),d0` → `move.l D(a6),d0` (guarded so an 8-byte load's second word, which reuses `a0`, is never broken) |

**Effect:** roughly 20 % smaller code (`CORETEST.PRG` 95,824 → 75,440 bytes). A full register
allocator (callee-saved `D2–D7`/`A2–A5` with `MOVEM` + spill) is reserved future work.

## 7. Standard library reference

The library is a **hosted C99 subset**: an OS-independent core over a thin per-OS seam
([§8](#8-the-syscall-seam)). Every function below is declared in `libc/include/` and implemented in
`libc/core/libc.c` (+ the per-OS seams). Behaviour is identical on both OSes except where a
**Per-OS** note appears.

### 7.1 `<stdio.h>` — streams & formatted I/O

Types & macros: `FILE`; `stdin`, `stdout`, `stderr`; `EOF` (−1); `BUFSIZ` (512); `SEEK_SET`/
`SEEK_CUR`/`SEEK_END`.

| Function | Description |
| --- | --- |
| `FILE *fopen(const char *path, const char *mode)` | Open a file. `mode[0]` is `r`/`w`/`a`; a trailing `b` is accepted and ignored (I/O is byte-exact). Returns `NULL` on failure. |
| `int fclose(FILE *fp)` | Flush and close; returns `0` or `EOF`. |
| `int fflush(FILE *fp)` | Flush a write stream's buffer to the OS. |
| `size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp)` | Read up to `nmemb` items of `size` bytes; returns items read. |
| `size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp)` | Write `nmemb` items; returns items written. |
| `int fgetc(FILE *fp)` / `int getc(FILE *fp)` | Read one byte, or `EOF`. |
| `int getchar(void)` | `fgetc(stdin)`. |
| `char *fgets(char *s, int n, FILE *fp)` | Read a line (≤ `n−1` bytes, stops at `\n`); `NULL` at EOF. |
| `int fputc(int c, FILE *fp)` / `int putc(int c, FILE *fp)` | Write one byte. |
| `int putchar(int c)` | `fputc(c, stdout)`. |
| `int fputs(const char *s, FILE *fp)` | Write a string (no added newline). |
| `int puts(const char *s)` | Write a string **plus a newline** to `stdout`. |
| `int fseek(FILE *fp, long off, int whence)` | Reposition (`SEEK_SET`/`CUR`/`END`). **Per-OS:** on CP/M a read handle has limited seek — see [§11](#11-platform-reference-osiris-vs-cpm-68k). |
| `long ftell(FILE *fp)` | Current offset. |
| `int feof(FILE *fp)` / `int ferror(FILE *fp)` | End-of-file / error indicators. |
| `int printf(const char *fmt, ...)` | Formatted output to `stdout`. |
| `int fprintf(FILE *fp, const char *fmt, ...)` | Formatted output to a stream. |
| `int sprintf(char *buf, const char *fmt, ...)` | Formatted output to a buffer (unbounded). |
| `int snprintf(char *buf, size_t size, const char *fmt, ...)` | Bounded formatted output. |
| `int vfprintf(FILE *, const char *, va_list)` / `int vsnprintf(char *, size_t, const char *, va_list)` | `va_list` variants. |
| `int sscanf(const char *s, const char *fmt, ...)` / `int vsscanf(const char *, const char *, va_list)` | Parse from a string. |
| `FILE *open_memstream(char **ptr, size_t *sizeloc)` | Open a growable in-memory write stream (POSIX). |

**`printf`/`scanf` conversions:** flags `-` `+` space `0`; field width; precision (`.n`); length
modifiers `l`/`ll`/`h`; conversions `d i u x X o c s p %` and the floating `f e g` (soft-float).
Width/precision from an argument (`*`) is **not** supported.

### 7.2 `<stdlib.h>` — general utilities

Types & macros: `div_t`, `ldiv_t`; `RAND_MAX` (32767); `EXIT_SUCCESS`/`EXIT_FAILURE`.

| Function | Description |
| --- | --- |
| `void *malloc(size_t n)` | Allocate `n` bytes (uninitialised). |
| `void *calloc(size_t nmemb, size_t size)` | Allocate and zero. |
| `void *realloc(void *p, size_t n)` | Resize an allocation. |
| `void free(void *p)` | Release an allocation. The allocator is backed by **libheap**, so `free` **really reclaims** and memory can be reused. |
| `int atoi(const char *s)` / `long atol(const char *s)` / `double atof(const char *s)` | Parse an int / long / double. |
| `long strtol(const char *, char **end, int base)` / `unsigned long strtoul(...)` | Parse with base and end pointer. |
| `long long strtoll(...)` / `unsigned long long strtoull(...)` | 64-bit variants. |
| `double strtod(const char *, char **end)` / `long double strtold(...)` | Parse a floating value. |
| `int abs(int)` / `long labs(long)` | Absolute value. |
| `div_t div(int, int)` / `ldiv_t ldiv(long, long)` | Quotient + remainder. |
| `int rand(void)` / `void srand(unsigned)` | Pseudo-random sequence (0…`RAND_MAX`). |
| `void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *))` | Sort in place. |
| `void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, cmp)` | Binary search. |
| `void exit(int code)` | Run `atexit` handlers, flush streams, terminate. |
| `int atexit(void (*fn)(void))` | Register an exit handler. |
| `void abort(void)` | Abnormal termination (`raise(SIGABRT)`). |

### 7.3 `<string.h>` — memory & string operations

| Function | Description |
| --- | --- |
| `void *memcpy(void *dst, const void *src, size_t n)` | Copy `n` bytes (no overlap). |
| `void *memmove(void *dst, const void *src, size_t n)` | Copy allowing overlap. |
| `void *memset(void *dst, int c, size_t n)` | Fill `n` bytes with `c`. |
| `int memcmp(const void *, const void *, size_t n)` | Compare `n` bytes. |
| `void *memchr(const void *s, int c, size_t n)` | Find a byte. |
| `size_t strlen(const char *s)` | String length. |
| `char *strcpy(char *, const char *)` / `char *strncpy(char *, const char *, size_t)` | Copy a string / bounded copy. |
| `char *strcat(char *, const char *)` / `char *strncat(char *, const char *, size_t)` | Append / bounded append. |
| `int strcmp(const char *, const char *)` / `int strncmp(const char *, const char *, size_t)` | Compare / bounded compare. |
| `char *strchr(const char *, int)` / `char *strrchr(const char *, int)` | First / last occurrence of a byte. |
| `char *strstr(const char *hay, const char *needle)` | Substring search. |
| `char *strdup(const char *)` / `char *strndup(const char *, size_t)` | Allocate a copy. |
| `char *strtok(char *s, const char *delim)` | Tokenise (stateful). |
| `char *strerror(int errnum)` | Message string for an `errno` value. |

### 7.4 `<strings.h>` — case-insensitive comparison

| Function | Description |
| --- | --- |
| `int strcasecmp(const char *, const char *)` | Case-insensitive compare. |
| `int strncasecmp(const char *, const char *, size_t)` | Bounded case-insensitive compare. |

### 7.5 `<ctype.h>` — character classification

`isalpha`, `isdigit`, `isalnum`, `isspace`, `isupper`, `islower`, `isxdigit`, `ispunct`, `isprint`,
`iscntrl` — classify an `int` (an `unsigned char` value or `EOF`); return non-zero if in class.
`toupper`, `tolower` — case conversion. (ASCII / "C" locale.)

### 7.6 `<math.h>` — floating-point math (soft-double)

Constants: `M_PI`, `M_E`, `M_LN2`, `M_LN10`. All functions take/return `double` over the soft-float
runtime.

| Function | Description |
| --- | --- |
| `sin`, `cos`, `tan` | Trigonometric. |
| `asin`, `acos`, `atan`, `atan2` | Inverse trig (`atan2(y,x)`). |
| `exp`, `log`, `log10` | Exponential / natural & base-10 log. |
| `sqrt`, `pow` | Square root, power. |
| `floor`, `ceil`, `fabs` | Round down / up, absolute value. |
| `fmod` | Floating remainder. |
| `modf(double x, double *ip)` | Split integer / fractional parts. |

The runtime primitives are also exposed under `d`-suffixed names (`sind`, `cosd`, `expd`, `logd`,
`sqrtd`, `powd`, `floord`, `ceild`, `fabsd`, `fmodd`, `modfd`, `atand`) for code that wants to bypass
the inline wrappers.

### 7.7 `<time.h>` — dates & times

Types & macros: `time_t` (32-bit seconds since the 1970 UTC epoch, good through 2038), `clock_t`,
`struct tm`, `CLOCKS_PER_SEC` (1000000). No timezone/DST — the seam clock is treated as UTC, so
`localtime` == `gmtime`.

| Function | Description |
| --- | --- |
| `time_t time(time_t *)` | Wall-clock seconds. **Per-OS:** resolution is OS-limited (DOS date+time on Osiris; BDOS/BIOS tick on CP/M). |
| `clock_t clock(void)` | Processor time (best effort). |
| `double difftime(time_t, time_t)` | Seconds between two times. |
| `time_t mktime(struct tm *)` | `struct tm` → `time_t`. |
| `struct tm *gmtime(const time_t *)` / `struct tm *localtime(const time_t *)` | Broken-down UTC time. |
| `char *asctime(const struct tm *)` / `char *ctime(const time_t *)` / `char *ctime_r(const time_t *, char *)` | Text form. |
| `size_t strftime(char *s, size_t max, const char *fmt, const struct tm *)` | Formatted time. |

### 7.8 `<assert.h>`

`assert(expr)` — if `expr` is false, print `expr`, file, and line and abort; compiled out when
`NDEBUG` is defined. Backed by `void __assert_fail(const char *expr, const char *file, int line)`.

### 7.9 `<errno.h>`

`extern int errno;` and the codes `ENOENT` (2), `EIO` (5), `EBADF` (9), `ENOMEM` (12), `EACCES`
(13), `EEXIST` (17), `EINVAL` (22), `EMFILE` (24). Each seam maps native OS status to one of these.

### 7.10 `<signal.h>` (minimal)

`signal(int sig, void (*)(int))` and `raise(int sig)` with `SIGINT`, `SIGILL`, `SIGABRT`, `SIGFPE`,
`SIGSEGV`, `SIGTERM` and `SIG_DFL`/`SIG_IGN`/`SIG_ERR`. Minimal: handlers are registered and
`raise`/`abort` invoke them; asynchronous delivery is limited to what the OS surfaces (e.g.
Ctrl-Break).

### 7.11 `<unistd.h>` / `<libgen.h>` (POSIX helpers)

- `int unlink(const char *path)`, `int close(int fd)` — delete a file, close a descriptor.
- `char *dirname(char *path)`, `char *basename(char *path)` — path components (may modify the
  argument, POSIX semantics); used by the preprocessor and driver.

### 7.12 `<sys/stat.h>` (stubs)

`struct stat` (with `st_mode`, `st_size`, `st_mtime`, `st_atime`, `st_ctime`) and
`int stat(const char *, struct stat *)` / `int fstat(int, struct stat *)`. **These are stubs that
fail** — the targets have no general `stat` service; only `st_mtime` is read (for `__TIMESTAMP__`,
which then falls back to its "unknown" form).

### 7.13 Fixed-width & limits headers

Type/macro-only headers, ILP32 values: `<stddef.h>` (`size_t`, `ptrdiff_t`, `NULL`, `offsetof`),
`<stdint.h>` (`int8_t`…`int64_t`, `uint*`, `intptr_t`, `INT*_MAX`…), `<inttypes.h>` (`PRI*`/`SCN*`
format macros — 32-bit is `"d"/"u"/"x"`, 64-bit is `"lld"/"llu"/"llx"`), `<limits.h>` (`INT_MAX`,
`CHAR_BIT`, …), `<float.h>` (IEEE single/double limits), `<stdbool.h>`, `<stdarg.h>` (`va_list`,
`va_start`/`va_arg`/`va_end`), `<iso646.h>`.

### 7.14 Standard streams & handles

`stdin`/`stdout`/`stderr` are file descriptors `0/1/2` on Osiris. **Per-OS:** CP/M has no handle
0/1/2 — the seam routes `_sys_read(0,…)`/`_sys_write(1,…)` to the console primitives with line
buffering.

### 7.15 Conformance scope

- **In scope (hosted subset):** `<assert.h>`, `<ctype.h>`, `<errno.h>`, `<float.h>`, `<inttypes.h>`
  (subset), `<limits.h>`, `<math.h>`, `<signal.h>` (minimal), `<stdarg.h>`, `<stdbool.h>`,
  `<stddef.h>`, `<stdint.h>`, `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<strings.h>`, `<time.h>`
  (formatting; resolution OS-limited), plus POSIX `<unistd.h>`/`<libgen.h>` and stub `<sys/stat.h>`.
- **Out of scope:** `<complex.h>`, `<fenv.h>`, `<tgmath.h>`, `<wchar.h>`/`<wctype.h>` (beyond
  minimal), `<locale.h>` beyond the "C" locale, and threads.
- **Freestanding** (`-ffreestanding`): only `<float.h>`, `<iso646.h>`, `<limits.h>`, `<stdarg.h>`,
  `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` plus the runtime helper library — no OS seam.

## 8. The syscall seam

The seam is the **only** place OS `TRAP`s appear (~15 primitives). The core library is written
against these signatures and is identical for both OSes.

| Seam primitive | Purpose | **Osiris** (DOS `TRAP #1`) | **CP/M-68K** (BDOS `TRAP #2`) |
| --- | --- | --- | --- |
| `_sys_conout(c)` | write a console char | `02h` | `2` Console Output |
| `_sys_conin()` | read a console char | `01h`/`08h` | `1` Console Input |
| `_sys_constat()` | console input ready? | `0Bh` | `11` Console Status |
| `_sys_open(path,mode)` | open existing file | `3Dh` → handle | `15` Open (FCB) |
| `_sys_creat(path,attr)` | create/truncate | `3Ch` → handle | `22` Make (FCB) |
| `_sys_close(fd)` | close | `3Eh` | `16` Close (FCB) |
| `_sys_read(fd,buf,n)` | read bytes | `3Fh` → got | `20`/`33` Read Seq/Random over DMA |
| `_sys_write(fd,buf,n)` | write bytes | `40h` → put | `21`/`34` Write Seq/Random over DMA |
| `_sys_seek(fd,off,whence)` | reposition | `42h` → `D0.L` | random-record calc + `33`/`34` |
| `_sys_unlink(path)` | delete | `41h` | `19` Delete (FCB) |
| `_sys_rename(old,new)` | rename | `56h` | `23` Rename (FCB) |
| `_sys_sbrk(delta)` | grow/shrink heap | `48h`/`4Ah` | advance the break within the TPA |
| `_sys_time(&t)` | wall clock / ticks | `2Ah`+`2Ch` | `T`-function / BIOS tick |
| `_sys_exit(code)` | terminate | `4Ch` / `TRAP #3` | `0` System Reset |
| `_sys_args(argv,env)` | fetch cmd line / env | PSP-equivalent + `64h` | base-page command tail |

**File model.** Osiris is handle-based (an `fd` maps directly). CP/M is FCB + 128-byte record/DMA:
the CP/M seam maps each `fd` to an FCB, packs 8.3 names, sets the DMA address, and translates byte
offsets into record reads/writes with a buffer so `stdio`'s byte-stream contract holds.

## 9. Runtime support library

`lib/runtime/` (`rt68k`) supplies the routines the base 68000 lacks as single instructions;
OS-independent, `libgcc`/EABI-named so the code generator's calls are conventional.

| Group | Symbols |
| --- | --- |
| 32-bit integer | `__mulsi3`, `__divsi3`, `__udivsi3`, `__modsi3`, `__umodsi3` (+ variable shifts) |
| 64-bit `long long` | `__muldi3`, `__divdi3`, `__udivdi3`, `__moddi3`, `__umoddi3`, `__ashldi3`, `__ashrdi3`, `__lshrdi3`, `__cmpdi2`, `__ucmpdi2` |
| Soft `float`/`double` | the IEEE-754 library (`libieee754d`): `_fpadd`/`_fpsub`/`_fpmult`/`_fpdiv`/`_fpcmp` (single) and `…d` (double), conversions `_fpltof`/`_fpftol`/`_fpltod`/`_fpdtol`/`_fpftod`/`_fpdtof` |

The 68000's `MULS/MULU/DIVS/DIVU` are only 16-bit, so 32-bit and all 64-bit / floating arithmetic go
through these helpers. The compiler's `-O1` strength reduction removes many of the integer calls.

## 10. Toolchain reference

| Tool | Where | Role |
| --- | --- | --- |
| **`c68k`** | host | the cross-compiler (this manual) |
| **`CC.PRG` / `CC.68K`** | Osiris / CP/M | the native self-hosted compiler |
| **`m68k-elf-ld`** | host | link objects + linker script → executable ELF |
| **`LINK.PRG` / `LINK.68K`** | Osiris / CP/M | native linkers (Osiris ships `LINK`; CP/M is a port) |
| **`m68k-elf-ar` / `LIB.PRG`** | host / Osiris | build `.a` archives |
| **`asm68K`** | host | assemble the `.a68` crt0/seam/runtime (Motorola syntax) |
| **`mkdri`** | host / CP/M | ELF → DRI `.68K` conversion + base-page/segment headers |
| **`sim68k`** | host | headless 68000 system simulator (Osiris + CP/M images) |
| **`m68k-elf-objdump`** | host | disassembly; `-dl` interleaves source (with `-g`) |
| **`m68k-elf-nm` / `readelf` / `size`** | host | symbol / section / size inspection |
| **`m68k-elf-addr2line`** | host | address → source `file:line` (with `-g`) |
| **`m68k-elf-gdb`** | host | source-level debugging on the linked `.PRG` (with `-g`) |
| **`m68k-elf-objcopy` / `strip`** | host | object surgery, stripping |

**Linker scripts:** `osiris-prg.ld` (Osiris static-PIE `.PRG` layout) and `cpm68k.ld` (CP/M TPA
layout at `0x500`, then `mkdri`).

**Archives & dead-stripping:** `libc`/`libm`/`libheap` are `.a` archives of one object per function,
so a program links only what it references — a `puts`-only program is ~10 KB, versus ~88 KB when the
whole `libc` was one object. Both `m68k-elf-ld` and the native Osiris `LINK.PRG` member-select the
archives (the native `LINK.PRG` takes one archive per link, so the three are merged for that path;
heap programs use the cross `ld` pending a native `.bss`-zeroing fix).

## 11. Platform reference: Osiris vs CP/M-68K

| Aspect | **Osiris DOS (OS/68K)** | **CP/M-68K** |
| --- | --- | --- |
| System-call vector | `TRAP #1` (DOS) | `TRAP #2` (BDOS) |
| Function selector | code in `D0.W` high byte; carry-set = error | function in `D0.W`, arg in `D1.L`, result in `D0.L` |
| File model | **handles** (small-int `fd`) | **FCB** + 128-byte record / DMA |
| File length | exact byte length (FAT12) | **128-byte record granular**; short files read back `0x1A`-padded |
| Executable | `.PRG` — ELF32-MSB **static-PIE** (the ELF is the image) | `.68K` — **DRI** format via `mkdri` |
| Relocation | `R_68K_RELATIVE` applied by the loader | DRI fixups applied at load |
| Startup (`crt0`) | ELF entry; marshal PSP command tail → `argv`; heap via `48h` | base-page entry; TPA *is* the heap; command tail → `argv` |
| Standard handles | `fd` 0/1/2 | routed to console primitives by the seam |
| Memory | ≤ 1 MB (68008 board) | ≤ 1 MB; program in the TPA (~583 KB usable on base CP/M) |
| Debug info | DWARF survives in the `.PRG` (with `-g`) | none in the `.68K`; debug the intermediate `.elf` |

The **code generator is identical** for both — the split is entirely the libc seam + `crt0` + the
link/convert step. One C source, compiled once per target's macros, links into either container.

---

### Changelog

| Date | Version | Change |
| --- | --- | --- |
| 2026-07 | Draft 1.0 | Initial reference: type model, ABI, object format, driver, optimizations, the full standard-library reference (every supported function), the syscall seam, runtime helpers, toolchain, and the Osiris/CP/M-68K platform table. |
