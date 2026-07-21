# c68k — User's Manual

> **Status:** Draft 1.0 (2026-07) · Applies to `c68k` 0.1.0.
> Companion to the [Programmer's Reference Manual](reference-manual.md), the
> [SDK quickstart](sdk.md), and [architecture.md](architecture.md).

This manual is the practical, task-oriented guide to **using** c68k: installing the SDK, compiling
programs, choosing options, optimizing, debugging, and building and running executables for **Osiris
DOS (OS/68K)** and **CP/M-68K**. For the normative details of the language, ABI, and every library
function, see the [Programmer's Reference Manual](reference-manual.md).

## Contents

1. [What c68k is](#1-what-c68k-is)
2. [Prerequisites & installation](#2-prerequisites--installation)
3. [Quick start](#3-quick-start)
4. [The compiler driver](#4-the-compiler-driver)
5. [Optimization](#5-optimization)
6. [Debugging](#6-debugging)
7. [Building for Osiris (`.PRG`)](#7-building-for-osiris-prg)
8. [Building for CP/M-68K (`.68K`)](#8-building-for-cpm-68k)
9. [The toolchain tools](#9-the-toolchain-tools)
10. [Testing your programs](#10-testing-your-programs)
11. [Platform notes & limitations](#11-platform-notes--limitations)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. What c68k is

`c68k` is a **C99 cross-compiler for the Motorola 68000** that builds native executables for two
operating systems from **one source tree**:

- **Osiris DOS (OS/68K)** — an MS-DOS-style kernel; DOS services on `TRAP #1`, handle-based files,
  `.PRG` (ELF32-BE static-PIE) images.
- **CP/M-68K** (Digital Research) — BDOS services on `TRAP #2`, FCB files, `.68K` (DRI) images.

The **compiler and the object file it emits are OS-neutral.** The platform choice happens entirely
at **link** time — the container format and the libc backend. `-target` only selects predefined
macros so one source can `#ifdef`.

There are two compilers built from the same source:

| Compiler | Runs on | Produces |
| --- | --- | --- |
| **`c68k`** (cross) | your host PC (Windows/Linux/macOS) | Osiris `.PRG` and CP/M-68K `.68K` |
| **`CC.PRG` / `CC.68K`** (native) | Osiris / CP/M-68K | the same — it recompiles itself |

Most users work with the cross-compiler. This manual assumes the cross-compiler unless a section is
explicitly about the native chain.

## 2. Prerequisites & installation

### 2.1 What the SDK gives you

The repo-owned SDK (staged by [`tools/package.ps1`](../tools/package.ps1) into `dist/c68k-sdk-<ver>/`)
contains everything c68k owns:

| Item | Purpose |
| --- | --- |
| `bin/c68k.exe` | the cross-compiler |
| `include/` | the compiler's freestanding builtin headers |
| `libc/` | the C library sources (core + per-OS seam + headers) |
| `lib/` | the integer runtime and soft-float sources |
| `docs/` | this manual, the reference, the SDK guide, architecture |

### 2.2 The external link-time toolchain

c68k **compiles**; it does not link. Linking and image conversion use tools that are **prerequisites**
(documented, not vendored by the SDK):

| Tool | Needed for | Source |
| --- | --- | --- |
| `m68k-elf-ld` | linking objects into the executable ELF | GNU binutils (Osiris `toolchain/`) |
| `m68k-elf-ar`, `m68k-elf-objdump`, `m68k-elf-nm`, `m68k-elf-readelf`, `m68k-elf-addr2line`, `m68k-elf-gdb` | archiving, inspection, debugging | GNU binutils |
| `asm68K` | assembling the per-OS `crt0`/seam + integer runtime (`.a68`) | worm68k `68kTools/` |
| `mkdri` | ELF → DRI `.68K` conversion (CP/M target only) | worm68k `68kTools/` |
| `sim68k` | running and testing the result under emulation | worm68k `68kTools/` |

The one-shot build scripts wire all of this together; you normally never call the linker by hand.

### 2.3 Building the cross-compiler from source

On any host with a C11 compiler:

```powershell
# Windows / MSVC
cl /nologo /std:c11 /D_CRT_SECURE_NO_WARNINGS /Fe:c68k.exe src\*.c
```

```sh
# Linux / macOS
make c68k          # or: cc -std=c11 -o c68k src/*.c
```

### 2.4 Staging the simulator environment

To **run** programs, [`tools/bootstrap-simenv.ps1`](../tools/bootstrap-simenv.ps1) copies `sim68k`,
the boot ROM, the boot disk images, and the CP/M disk tools into `simenv/`. It expects the sibling
repos `worm68k` and `osiris` checked out next to this one. The per-OS run harnesses call it
automatically on first use.

## 3. Quick start

`hello.c` — one source, both systems:

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

Build and run a `.PRG` (Osiris) and a `.68K` (CP/M-68K):

```powershell
$env:C68K_INTEGRATED_AS = '1'                       # use the integrated ELF emitter

# Osiris -> HELLO.PRG, then run it under the sim
pwsh tools/osiris/run-osiris.ps1 -Src hello.c -Run HELLO -Expect 'hello from Osiris'

# CP/M-68K -> HELLO.68K, then run it under the sim
pwsh tools/cpm/run-cpm.ps1 -Src hello.c -Run HELLO -Expect 'hello from CP/M-68K'
```

To just **build** (no run):

```powershell
pwsh tools/osiris/build-prg.ps1 -Src hello.c        # -> %TEMP%\c68k-osiris\HELLO.PRG
pwsh tools/cpm/build-68k.ps1    -Src hello.c        # -> %TEMP%\c68k-cpm\HELLO.68K
```

## 4. The compiler driver

`c68k` follows GCC/Clang conventions. Full invocation:

```
c68k [options] file...
```

| Option | Meaning |
| --- | --- |
| `-c` | compile **and** assemble to an object (`.o`); do not link |
| `-S` | compile to 68000 assembly (`.s`) |
| `-E` | preprocess only |
| `-o <path>` | output path (`-` = stdout) |
| `-I <dir>` | add an include search directory |
| `-D <m>[=v]` | define macro `<m>` (default value `1`) |
| `-U <m>` | undefine macro `<m>` |
| `-include <f>` | process `<f>` as an implicit leading `#include "<f>"` |
| `-target osiris` \| `-target cpm` | predefine the target-OS macro (see below) |
| `-O0` … `-O3`, `-Os`, `-O` | optimization level ([§5](#5-optimization)); `-O0` is the default |
| `-g`, `-g0` | emit / suppress DWARF debug info ([§6](#6-debugging)) |
| `-Werror` | turn warnings into errors (other `-W*` are accepted and ignored) |
| `-ffreestanding` | freestanding environment (`__STDC_HOSTED__ = 0`) |
| `-fpic`, `-fPIC` | position-independent code (Osiris `.PRG` is a static PIE) |
| `-fintegrated-as` | emit the ELF object directly (no external assembler) |
| `--version`, `--help` | print version / usage and exit |

The emitted object is **OS-neutral**; you choose the container at link time. Unknown options are
rejected with a diagnostic and a non-zero exit code.

### Predefined macros

| Macro | Defined when | Use |
| --- | --- | --- |
| `__c68k__` | always | detect this compiler |
| `__osiris__` | `-target osiris` | Osiris-specific code |
| `__CPM68K__` | `-target cpm` | CP/M-68K-specific code |

Plus the standard 68000 set (`__m68k__`, `__mc68000__`, big-endian `__BYTE_ORDER__`, the ILP32
`__SIZEOF_*__` / `__*_MAX__` families, `__STDC_VERSION__`). Because `-target` only changes
preprocessing, **the object is identical whichever target you pick** — it matters only when your
source `#ifdef`s on the OS.

### Environment variables (build scripts)

The build/run scripts honour these knobs:

| Variable | Effect |
| --- | --- |
| `C68K_INTEGRATED_AS=1` | compile C via the integrated ELF emitter (no external `asm68K` for `.c`) |
| `C68K_OPT=<n>` | compile the libc **and** your program at `-O<n>` |
| `C68K_G=1` | compile the program with `-g` and keep symbols/debug in the `.PRG` (Osiris) |

## 5. Optimization

`-O0` (the default) emits straightforward, easy-to-read stack-machine code. `-O1` and above enable
the back-end optimization tier (a single tier today, so `-O2`, `-O3`, `-Os`, `-Ofast` all behave as
`-O1`):

| Transform | Example |
| --- | --- |
| **Immediate-operand selection** | `x + 5` → `addq.l #5,d0`; `x & 15` → `andi.l #15,d0`; `x < 10` → `cmp.l #10,d0` |
| **Strength reduction** (powers of two) | `x * 8` → `asl.l #3,d0`; unsigned `x / 4` → `lsr.l #2,d0`; `x % 8` → `andi.l #7,d0` |
| **Peephole** | removes the address↔data register round-trips left by the naive load sequences |
| **Addressing-mode fold** | `lea D(a6),a0` / `move.l (a0),d0` → `move.l D(a6),d0` |

The optimizations are **semantics-preserving** — the full lockstep suite passes on both OSes at
`-O1` — and typically cut code size by about **20 %** (`CORETEST.PRG`: 95,824 → 75,440 bytes).

Signed division/modulo and multiply by a non-power-of-two stay on the runtime helpers (correctness
over cleverness). A full register allocator is future work; today values live in `D0`/`D1` around
the stack.

To build the whole libc + program optimized, set `C68K_OPT=1` for the build scripts.

## 6. Debugging

`c68k -g` emits **DWARF** through the integrated assembler: `STT_FUNC` symbols with sizes, plus
`.debug_info` / `.debug_line` / `.debug_abbrev` with relocations, so the debug info **survives
linking**. The stock `m68k-elf-*` tools then work on the linked Osiris `.PRG` (which stays an ELF):

```powershell
$env:C68K_INTEGRATED_AS = '1'; $env:C68K_G = '1'
pwsh tools/osiris/build-prg.ps1 -Src samples/hexdump.c

m68k-elf-objdump  -dl HEXDUMP.PRG          # source-interleaved disassembly
m68k-elf-addr2line -e HEXDUMP.PRG 0x2e5e   # address -> file:line
m68k-elf-gdb --batch -ex "info line hexdump.c:88" -ex "list" HEXDUMP.PRG
```

[`tools/debug-demo.ps1`](../tools/debug-demo.ps1) runs the whole flow (build + `readelf` + `objdump
-dl` + `gdb`) and confirms gdb resolves a source line to an address.

Notes:

- `-g` needs the **integrated assembler** (`C68K_INTEGRATED_AS=1`); with the external `asm68K` path
  the debug markers are treated as comments and no DWARF is produced.
- The unstripped `.PRG` still runs — the loader ignores the non-alloc debug/symbol sections.
- The CP/M-68K `.68K` (DRI format) carries no DWARF; debug the intermediate linked `.elf` instead.

## 7. Building for Osiris (`.PRG`)

An Osiris `.PRG` **is** an ELF32-MSB static-PIE — the ELF the linker produces is directly loadable.

### 7.1 With the build script (recommended)

```powershell
$env:C68K_INTEGRATED_AS = '1'
pwsh tools/osiris/build-prg.ps1 -Src myprog.c -Name MYPROG
```

The script assembles the `crt0`/seam + integer runtime, compiles the libc and your program, and
links with `osiris-prg.ld`. It also writes a `MYPROG.map` linker map alongside the output.

### 7.2 Manual link recipe

```
m68k-elf-ld -pie --no-dynamic-linker -z max-page-size=0x20 -s \
    -T osiris-prg.ld  osiris_sys.o  prog.o  libc.o  rt68k.o  libieee754d.a  -o MYPROG.PRG
```

Link order puts the seam/`crt0` first; `ENTRY(_start)` fixes the entry regardless.

### 7.3 Running under the simulator

```powershell
pwsh tools/osiris/run-osiris.ps1 -Src myprog.c -Run MYPROG -Expect 'expected output'
```

The harness copies the vendored FAT12 boot floppy, adds your freshly built `.PRG`, boots `sim68k`
headless, types the program name at the `A>` shell, captures the ACIA console, and checks the
expected output.

## 8. Building for CP/M-68K (`.68K`)

A CP/M-68K `.68K` is a **DRI-format** image: link at the TPA base `0x500`, then convert the ELF with
`mkdri`.

### 8.1 With the build script (recommended)

```powershell
$env:C68K_INTEGRATED_AS = '1'
pwsh tools/cpm/build-68k.ps1 -Src myprog.c -Name MYPROG
```

### 8.2 Manual link recipe

```
m68k-elf-ld -T cpm68k.ld -Ttext 0x500  cpm_sys.o  prog.o  cpm.o  libc.o  rt68k.o  libieee754d.a \
    -o prog.elf
mkdri -b500 -y -o MYPROG.68K prog.elf
```

### 8.3 Running under the simulator

```powershell
pwsh tools/cpm/run-cpm.ps1 -Src myprog.c -Run MYPROG -Expect 'expected output'
```

## 9. The toolchain tools

| Tool | Host (cross) | Native (on-target) | Role |
| --- | --- | --- | --- |
| **`c68k`** | ✔ | `CC.PRG` / `CC.68K` | compile C → OS-neutral ELF32-BE object |
| **`m68k-elf-ld`** | ✔ | `LINK.PRG` (Osiris) | link objects + script → executable ELF |
| **`m68k-elf-ar`** | ✔ | `LIB.PRG` (Osiris) | build `.a` archives |
| **`mkdri`** | ✔ | native `.68K` | ELF → DRI `.68K` (CP/M only) |
| **`asm68K`** | ✔ | — | assemble the `.a68` crt0/seam/runtime |
| **`sim68k`** | ✔ | — | run/test images under emulation |
| **`m68k-elf-objdump`** | ✔ | — | disassembly, `-dl` source listing |
| **`m68k-elf-nm` / `readelf` / `size`** | ✔ | — | symbol/section/size inspection |
| **`m68k-elf-addr2line`** | ✔ | — | address → source `file:line` |
| **`m68k-elf-gdb`** | ✔ | — | source-level debugging (with `-g`) |

Convenience scripts in `tools/`:

| Script | What it does |
| --- | --- |
| `osiris/build-prg.ps1`, `cpm/build-68k.ps1` | one-shot build for each OS |
| `osiris/run-osiris.ps1`, `cpm/run-cpm.ps1` | build + run + check output under `sim68k` |
| `build-samples.ps1` | build every standalone sample for both OSes (a build-coverage gate) |
| `run-lockstep.ps1` | build + run each test on **both** OSes and require identical golden output |
| `debug-demo.ps1` | demonstrate source-level debugging end to end |
| `package.ps1` | stage/zip the SDK |
| `bootstrap-simenv.ps1` | stage the `sim68k` environment |

## 10. Testing your programs

The **lockstep** model is the project's core guarantee: one C source, one golden expectation, and it
must hold on **both** OSes. [`tools/run-lockstep.ps1`](../tools/run-lockstep.ps1) compiles each case
for Osiris and CP/M-68K, runs both under `sim68k`, and requires the same output. To add your own
program to the gallery build, drop a single-source `.c` in `samples/` and add its stem to the
`$samples` list in [`tools/build-samples.ps1`](../tools/build-samples.ps1).

## 11. Platform notes & limitations

- **Big-endian, flat-address, no-MMU, no-FPU** 68000 systems; floating point is **soft-float**.
- **`int` is 32-bit** (ILP32); `long` is 32-bit, `long long` is 64-bit (software-emulated);
  `long double` is `double` (64-bit IEEE).
- The 68008 Osiris board and CP/M-68K are **≤ 1 MB** machines; large programs should mind the heap.
  The native self-host `CC` fits Osiris; on base CP/M-68K only small translation units fit the
  ~583 KB TPA.
- **CP/M record granularity.** CP/M-68K files are stored in 128-byte records with no exact byte
  length; a short file reads back **padded to the next 128-byte boundary with `0x1A`** (Ctrl-Z, the
  CP/M soft-EOF). Osiris FAT12 stores the exact length. A tool that must behave identically on both
  OSes should round its own data to 128-byte records or treat `0x1A` as end-of-text.
- **No variable-length arrays** (use a fixed bound or `malloc`); `<complex.h>`, `<fenv.h>`,
  `<tgmath.h>`, and threads are out of scope. See the
  [reference manual](reference-manual.md#7-standard-library-reference) for the full library scope.

## 12. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `exec failed: asm68K: No such file or directory` | The C compile tried to invoke `asm68K`. Set `C68K_INTEGRATED_AS=1` to use the integrated ELF emitter, or put `asm68K` on `PATH`. |
| `emit_elf: unhandled instruction '<mnem>'` | The integrated assembler met a mnemonic it doesn't encode. File a bug; the external `asm68K` path (`C68K_INTEGRATED_AS` unset) is the fallback. |
| Build script "Cannot convert … to SwitchParameter" | A PowerShell param-name collision — update to the current scripts. |
| A `.PRG`/`.68K` runs on one OS but not the other | Check `#ifdef __osiris__` / `#ifdef __CPM68K__` branches and file-model assumptions (record granularity, exact lengths). |
| `-g` produced no DWARF | `-g` requires `C68K_INTEGRATED_AS=1`; the external `asm68K` path ignores the debug markers. |
| Program builds but faults immediately | Often a stack/heap-size or odd-address issue on the 1 MB board; reduce static buffers and confirm word data is even-aligned (the compiler guarantees this for its own output). |

---

### Changelog

| Date | Version | Change |
| --- | --- | --- |
| 2026-07 | Draft 1.0 | Initial user's manual: install, quick start, driver, optimization, debugging, per-OS build/run, toolchain, testing, limitations, troubleshooting. |
