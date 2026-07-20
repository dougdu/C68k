# Building programs with c68k — SDK guide

> **Status:** Draft 0.1 (2026-07) · Companion to [architecture.md](architecture.md),
> [libc-and-toolchain.md](libc-and-toolchain.md), and [implementation-plan.md](implementation-plan.md).

`c68k` is a C99 cross-compiler for the Motorola 68000 that targets **Osiris DOS (OS/68K)** and
**CP/M-68K** from **one source tree**. The compiler and the object it emits are OS-neutral; the
platform choice happens entirely at **link** time (the container format and the libc backend). This
guide is the quickstart for building your own programs.

For the design rationale see [architecture.md](architecture.md); for the C library and the full
toolchain reference see [libc-and-toolchain.md](libc-and-toolchain.md).

## 1. What you need

| Component | Purpose |
| --- | --- |
| `c68k` (this repo) | the C compiler — C source → OS-neutral ELF32-BE object |
| `m68k-elf-ld` | GNU linker (links objects + the ld script into the executable ELF) |
| `mkdri` | ELF → DRI `.68K` converter (CP/M target only) |
| `asm68K` | assembles the per-OS `crt0`/seam and the integer runtime (`.a68`) |
| `libc/`, `lib/` (this repo) | the C library, soft-float, and integer runtime sources |
| `c68k-sim68k` | the 68000 system simulator, to run and test the result |

The one-shot build scripts [`tools/osiris/build-prg.ps1`](../tools/osiris/build-prg.ps1) and
[`tools/cpm/build-68k.ps1`](../tools/cpm/build-68k.ps1) wire all of this together; start from them.

## 2. Quickstart

`hello.c` — the same source builds for both systems:

```c
#include <stdio.h>

int main(void) {
#ifdef __osiris__
  puts("hello from Osiris");
#elif defined(__CPM68K__)
  puts("hello from CP/M-68K");
#else
  puts("hello");
#endif
  return 0;
}
```

Build a `.PRG` (Osiris) and a `.68K` (CP/M):

```powershell
# Osiris → HELLO.PRG
$env:C68K_INTEGRATED_AS = '1'          # use c68k's integrated ELF emitter (no external asm for C)
pwsh tools/osiris/build-prg.ps1 -Src hello.c

# CP/M-68K → HELLO.68K
pwsh tools/cpm/build-68k.ps1 -Src hello.c
```

Run under the simulator with the per-OS harnesses in `tools/osiris/` and `tools/cpm/`
(e.g. [`tools/osiris/run-osiris.ps1`](../tools/osiris/run-osiris.ps1)).

## 3. Compiler options

`c68k` follows GCC/Clang driver conventions:

| Option | Meaning |
| --- | --- |
| `-c` | compile **and** assemble to an object (`.o`); do not link |
| `-S` | compile to 68000 assembly (`.s`) |
| `-E` | preprocess only |
| `-o <path>` | output path (`-` = stdout) |
| `-I <dir>` | add an include search directory |
| `-D <m>[=v]` / `-U <m>` | define / undefine a macro |
| `-include <f>` | process `<f>` as an implicit leading `#include` |
| `-target osiris` \| `-target cpm` | predefine the target OS macro (see §4) |
| `-ffreestanding` | freestanding environment (`__STDC_HOSTED__=0`) |
| `-fpic` / `-fPIC` | position-independent code (Osiris `.PRG` is a static PIE) |
| `--version`, `--help` | print version / usage |

`c68k --help` prints the full list. Unknown options are rejected with a diagnostic and a non-zero
exit code; diagnostics are `file:line:col:` with a caret and an `error:`/`warning:` label.

## 4. Predefined macros

| Macro | When | Use |
| --- | --- | --- |
| `__c68k__` | always | detect this compiler |
| `__osiris__` | `-target osiris` | Osiris-specific code |
| `__CPM68K__` | `-target cpm` | CP/M-68K-specific code |

Plus the standard 68000 set (`__m68k__`, `__mc68000__`, `__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__`,
the ILP32 `__SIZEOF_*__`/`__*_MAX__` families, `__STDC_VERSION__ == 201112L`, …).

The **object is identical** whichever target you pick — `-target` only changes preprocessing, so it
matters only when your source uses `#ifdef __osiris__` / `#ifdef __CPM68K__`. The executable
container is decided at link time, not by the compiler.

## 5. Manual link recipes

If you are not using the build scripts, link the compiled program object with one `crt0`/seam, the
libc core, the integer runtime, and the soft-float archive.

**Osiris `.PRG`** (ELF32-MSB static-PIE — the ELF *is* the `.PRG`):

```
m68k-elf-ld -pie --no-dynamic-linker -z max-page-size=0x20 -s \
    -T osiris-prg.ld  osiris_sys.o  prog.o  libc.o  rt68k.o  libieee754d.a  -o HELLO.PRG
```

**CP/M-68K `.68K`** (link at the TPA base `0x500`, then convert ELF → DRI contiguous):

```
m68k-elf-ld -T cpm68k.ld -Ttext 0x500  cpm_sys.o  prog.o  cpm.o  libc.o  rt68k.o  libieee754d.a \
    -o prog.elf
mkdri -b500 -y -o HELLO.68K prog.elf
```

Link order puts the seam/`crt0` first (`osiris_sys.o` / `cpm_sys.o`) so `_start` lands correctly;
on Osiris `ENTRY(_start)` fixes the entry regardless, on CP/M there is no `ENTRY` so it must be
first. See [libc-and-toolchain.md §8](libc-and-toolchain.md#8-build-recipes) for the full detail.

## 6. Library support

The C library is a shared OS-independent core over a thin per-OS syscall seam; a program links one
seam + one `crt0` plus the shared core, integer runtime, and math library. Conformance scope and the
per-header status are documented in [libc-and-toolchain.md](libc-and-toolchain.md). Freestanding
programs (`-ffreestanding`) can skip the hosted library entirely and use only the builtin headers in
[`include/`](../include).

## 7. Notes & limitations

- Both targets are big-endian, flat-address, no-MMU, no-FPU 68000 systems; floating point is
  soft-float.
- `long double` is `double` (64-bit IEEE).
- The 68008 Osiris board and CP/M-68K are ≤ 1 MB machines; large programs should mind the heap.
- The compiler does not link; linking is a separate `m68k-elf-ld` (+ `mkdri` for CP/M) step, or the
  on-target `LINK.PRG` / `LINK.68K`.
