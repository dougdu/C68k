# Building programs with c68k — SDK guide

> **Status:** Draft 0.1 (2026-07) · Companion to [architecture.md](architecture.md),
> [libc-and-toolchain.md](libc-and-toolchain.md), and [implementation-plan.md](implementation-plan.md).

`c68k` is a C99 cross-compiler for the Motorola 68000 that targets **Osiris DOS (OS/68K)** and
**CP/M-68K** from **one source tree**. The compiler and the object it emits are OS-neutral; the
platform choice happens entirely at **link** time (the container format and the libc backend). This
guide is the quickstart for building your own programs.

> **Looking for more?** This is the condensed quickstart. The [User's Manual](users-manual.md) is the
> full task guide (install, driver, optimization, `-g` debugging, per-OS build/run, troubleshooting),
> and the [Programmer's Reference Manual](reference-manual.md) is the normative reference (ABI,
> object format, and **every supported library function**).

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

### 2.1 A worked example — `hexdump`

[`samples/hexdump.c`](../samples/hexdump.c) is a complete, real utility built with the SDK: it
takes a filename on the command line (`HEXDUMP <file>`) and prints the classic offset / hex / ASCII
dump; with no argument it writes a known payload and dumps it back. One source builds for both OSes
and — because the code generator is identical — the self-test produces **byte-identical** output on
Osiris and CP/M-68K. It exercises the pieces a genuine program depends on: `argv` from the command
tail, binary file I/O (`fopen`/`fread`/`fwrite`), and `printf` width/zero-pad/hex conversions.

```
A>HEXDUMP
hexdump of HEXTEST.BIN
00000000  63 36 38 6b 20 68 65 78  64 75 6d 70 20 4f 4b 0a  |c68k hexdump OK.|
00000010  20 21 22 23 24 25 26 27  28 29 2a 2b 2c 2d 2e 2f  | !"#$%&'()*+,-./|
...
128 bytes
```

It runs on both OSes on every commit through the dual-target lockstep gate
([`tools/run-lockstep.ps1`](../tools/run-lockstep.ps1)).

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
| `-O0` / `-O1` (`-O`, `-O2`, `-O3`, `-Os`) | optimization level (see §3.1); `-O0` is the default |
| `-g` | emit DWARF line/symbol debug info (see §7; integrated assembler) |
| `-Werror` | turn warnings into errors (other `-W*` flags are accepted) |
| `-ffreestanding` | freestanding environment (`__STDC_HOSTED__=0`) |
| `-fpic` / `-fPIC` | position-independent code (Osiris `.PRG` is a static PIE) |
| `--version`, `--help` | print version / usage |

`c68k --help` prints the full list. Unknown options are rejected with a diagnostic and a non-zero
exit code; diagnostics are `file:line:col:` with a caret and an `error:`/`warning:` label.

### 3.1 Optimization levels

`-O0` (the default) emits straightforward stack-machine code and is the most predictable to read and
debug. `-O1` and above enable the back-end optimizations: constant right-operands are folded into
immediate instructions (`x + 5` → `addq.l #5,d0`), multiply/divide/modulo by a power of two are
strength-reduced to shifts/masks (`x * 8` → `asl.l #3,d0`, `u / 4` → `lsr.l #2,d0`), and a peephole
pass removes the address↔data register round-trips left by the naive load sequences and folds a
variable's address into the load itself (`lea D(a6),a0` / `move.l (a0),d0` → `move.l D(a6),d0`).
There is currently a single optimization tier, so `-O2`, `-O3`, `-Os` and `-Ofast` behave as `-O1`.
The optimizations are semantics-preserving — the full lockstep suite passes on both OSes at `-O1` —
and typically cut a program's code size by roughly 20 % (`CORETEST.PRG`: 95,824 → 75,440 bytes).

The build scripts honour a `C68K_OPT` environment variable: `C68K_OPT=1` compiles both the libc and
your program at `-O1` (mirrors the `C68K_INTEGRATED_AS` knob).

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

## 7. Debugging with `-g`

`c68k -g` emits **DWARF** debug info through the integrated assembler: `STT_FUNC` symbols with
sizes, plus `.debug_info` / `.debug_line` / `.debug_abbrev` with relocations, so the info survives
linking. The standard `m68k-elf-*` tools then work on the linked Osiris `.PRG` (which stays an ELF):

```powershell
# Build with debug info kept in the .PRG (C68K_G drops the link-time -s strip).
$env:C68K_INTEGRATED_AS = '1'; $env:C68K_G = '1'
pwsh tools/osiris/build-prg.ps1 -Src samples/hexdump.c

m68k-elf-objdump  -dl HEXDUMP.PRG          # source-interleaved disassembly
m68k-elf-addr2line -e HEXDUMP.PRG 0x2e5e   # address -> file:line
m68k-elf-gdb --batch -ex "info line hexdump.c:88" -ex "list" HEXDUMP.PRG
```

[`tools/debug-demo.ps1`](../tools/debug-demo.ps1) runs this end to end (build + `readelf` + `objdump
-dl` + `gdb`) and asserts that gdb resolves a source line to an address. Notes:

- `-g` needs the **integrated assembler** (`C68K_INTEGRATED_AS=1`); the external `asm68K` path
  treats the debug markers as comments and produces no DWARF.
- The unstripped `.PRG` still runs — the loader ignores the non-alloc debug/symbol sections.
- The CP/M-68K `.68K` (DRI format) carries no DWARF; debug the intermediate linked `.elf` instead.

The whole standalone `samples/` gallery can be built for both OSes (a build-coverage gate) with
[`tools/build-samples.ps1`](../tools/build-samples.ps1); each link also writes a `.map` file.

## 8. Notes & limitations

- Both targets are big-endian, flat-address, no-MMU, no-FPU 68000 systems; floating point is
  soft-float.
- `long double` is `double` (64-bit IEEE).
- The 68008 Osiris board and CP/M-68K are ≤ 1 MB machines; large programs should mind the heap.
- **CP/M record granularity.** CP/M-68K files are stored in 128-byte records with no exact byte
  length; a short file reads back **padded to the next 128-byte boundary with `0x1A`** (Ctrl-Z, the
  CP/M soft-EOF). Osiris FAT12 stores the exact length instead. A tool that must behave identically
  on both OSes should either round its own data to 128-byte records (as `samples/hexdump.c` does for
  its self-test) or treat `0x1A` as end-of-text for CP/M text files.
- The compiler does not link; linking is a separate `m68k-elf-ld` (+ `mkdri` for CP/M) step, or the
  on-target `LINK.PRG` / `LINK.68K`. The standard library ships as **dead-strippable archives**
  (`libc.a`/`libm.a`/`libheap.a`), so a program links only the objects it references — a `puts`-only
  program is ~10 KB, not the ~88 KB of the old whole-`libc` link. The native Osiris `LINK.PRG`
  member-selects these archives too (non-heap programs; see
  [libc-and-toolchain.md](libc-and-toolchain.md) §7.2).
