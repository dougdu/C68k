# c68k — Implementation Plan & Progress

> **Status:** Draft 0.1 (2026-07) · Companion to [architecture.md](architecture.md) and
> [libc-and-toolchain.md](libc-and-toolchain.md).
> **This is the document that tracks progress.** Update the [dashboard](#progress-dashboard) and the
> per-phase checkboxes as work lands.

The plan is **14 phases, P0–P13**. Each phase has an objective, a task checklist, explicit **exit
criteria**, and its dependencies. Phases are ordered so that every phase ends at a **runnable,
testable** milestone — bring-up reaches "hello world on both OSes" early (P4–P5), then breadth
(P6–P7), then self-hosting (P8–P10), then hardening and speed (P11–P13).

Legend: ☐ not started · ◐ in progress · ☑ done.

---

## Progress dashboard

| Phase | Title | Status | Tasks | Milestone |
| --- | --- | :---: | :---: | --- |
| **P0** | [Scaffolding & host baseline](#p0--scaffolding--host-baseline) | ☑ | 6 / 6 | chibicc forks in, builds & self-hosts on host |
| **P1** | [ILP32 type-model retarget](#p1--ilp32-type-model-retarget) | ☑ | 6 / 6 | front end is big-endian ILP32 |
| **P2** | [68000 code generation](#p2--68000-code-generation) | ☑ | 8 / 8 | C runs on bare 68000 under sim68k |
| **P3** | [Runtime support library](#p3--runtime-support-library) | ☑ | 6 / 6 | float / `long long` math correct |
| **P4** | [libc core + Osiris backend](#p4--libc-core--osiris-backend) | ☑ | 7 / 7 | **`HELLO.PRG` runs on Osiris** |
| **P5** | [CP/M-68K backend](#p5--cpm-68k-backend) | ☑ | 7 / 7 | **`HELLO.68K` runs on CP/M-68K; lockstep** |
| **P6** | [C99 language completeness](#p6--c99-language-completeness) | ☑ | 6 / 6 | language suite green on both OSes |
| **P7** | [C99 standard library](#p7--c99-standard-library) | ☑ | 7 / 7 | library + `libm` suite green |
| **P8** | [Integrated object emitter](#p8--integrated-object-emitter) | ☑ | 5 / 5 | compiler emits ELF `.o` with no assembler |
| **P9** | [Native LINK / LIB / mkdri](#p9--native-link--lib--mkdri) | ☑ | 6 / 6 | native link chain on both OSes |
| **P10** | [Self-hosting bootstrap](#p10--self-hosting-bootstrap) | ☐ | 0 / 5 | **stage2 == stage3 on both OSes** |
| **P11** | [Cross-compiler hardening](#p11--cross-compiler-hardening) | ☐ | 0 / 6 | cross is a CI'd, maintained product |
| **P12** | [Optimization](#p12--optimization) | ☐ | 0 / 6 | register allocation + peephole |
| **P13** | [Tooling & debug polish](#p13--tooling--debug-polish) | ☐ | 0 / 6 | DWARF, diagnostics, samples, SDK docs |
| | **Total** | **2 / 14** | **12 / 87** | |

**Milestones (headline):**

1. **M1 — Bare-metal C** (end P2): compiled C executes correctly on the 68000 under `sim68k`. **✅ reached** — 17-case golden suite green (`tools/m68k/run-tests.ps1`).
2. **M2 — Hello, both OSes** (end P5): the same C source builds and runs as a `.PRG` on Osiris and a
   `.68K` on CP/M-68K, verified in lockstep. **✅ reached** — hello / filerw / printftest 3/3 lockstep (`tools/run-lockstep.ps1`).
3. **M3 — Conforming C99** (end P7): the language + hosted library suites pass on both OSes. **✅ reached** — language + `libm` + library + `<time.h>` suites 8/8 lockstep on both OSes (`coretest` 41, `c99test` 18, `mathtest` 14, `libtest` 26, `timetest` 15); freestanding mode 40/40 bare-metal (`tools/m68k/run-tests.ps1`).
4. **M4 — Self-hosting** (end P10): the native `CC` recompiles itself to a byte-identical binary on
   both OSes.
5. **M5 — Product** (end P11): the cross-compiler is hardened, CI-gated, and building real tools.

---

## P0 — Scaffolding & host baseline

**Objective:** fork chibicc into the repo, establish the build, and confirm the compiler builds on
the maintainers' hosts (**Windows/MSVC** and **macOS/Clang**) with an x86-64 **Linux CI** job kept
as a full-suite + self-host safety net.

- [x] Import chibicc into `src/`, **preserving its MIT copyright/notices** ([LICENSE](../LICENSE)).
      Imported verbatim at upstream commit `90d1f7f`; provenance in [`src/README.md`](../src/README.md),
      license in [`src/CHIBICC-LICENSE`](../src/CHIBICC-LICENSE).
- [x] Create the [repository layout](architecture.md#11-repository-layout) (`src`, `libc`, `lib`,
      `tools`, `tests`, `include`, `samples`).
- [x] `makefile` + `CMakeLists.txt` build `c68k` — **Windows (MSVC)**, **macOS (Clang)**, and
      **Linux (GCC/Clang)**. A thin [`src/compat.{h,c}`](../src/compat.c) platform layer supplies the
      POSIX shims MSVC lacks (`fork`/`spawn`, `open_memstream`, `strndup`, `dirname`/`basename`,
      `mkstemp`, `ctime_r`, case-compare); this is the one deliberate change to the imported baseline.
- [x] Bring chibicc's own test suite over (imported into `tests/`); it passes on x86-64 **Linux**
      unchanged — **green in CI** (gcc + clang). *(Execution tests are Linux-only: the interim x86-64
      back end can't assemble/link on Windows or macOS, where P0 instead runs `make smoke` / a
      front-end check — `-E`/`-S`/`--help`.)*
- [x] Confirm the compiler **self-hosts** (stage2 == stage3) as the baseline — `make selfhost`
      byte-compares the two stages; **green in CI** (gcc + clang).
- [x] CI skeleton: build + test on every commit ([`.github/workflows/ci.yml`](../.github/workflows/ci.yml))
      — Linux (full suite + self-host), macOS (build + front-end), Windows/MSVC (build + front-end).

**Exit:** `c68k` builds on Windows/MSVC + macOS/Clang; the front end runs there; the full conformance
suite and stage2==stage3 self-host pass on the Linux CI safety net.
**Depends on:** —

> **Host strategy (P0 decision).** The cross-compiler is built and maintained on **Windows (MSVC)**
> and **macOS (Clang)** — the tools actually in use. chibicc's *interim* x86-64 back end emits
> Linux/ELF assembly and shells out to GNU `as`/`ld`, so full execution + self-host only run on
> x86-64 **Linux**, which is retained as a **CI-only** correctness net. Once the **68000** back end
> lands (P2+), real execution testing happens under `sim68k` with the `m68k-elf` toolchain on every
> host, and the x86-64 vestige is retired. **Status: P0 CI is green on all three hosts** — Linux
> (full conformance suite + stage2==stage3 self-host, gcc + clang), macOS (build + front-end), and
> Windows/MSVC (build + front-end).

## P1 — ILP32 type-model retarget

**Objective:** convert the front end from chibicc's LP64 to **big-endian ILP32**
([architecture.md §7.1](architecture.md#71-type-model-ilp32-big-endian)).

- [x] `type.c`: sizes/alignments → `int`/`long`/pointer = 4, `short` = 2, `long long` = 8,
      `double` = 8, natural even alignment. **Alignment = 2 bytes** for every type ≥ 16 bits (the GNU
      m68k-elf SysV default; verify vs Osiris `abi-68k.md`). `long` and `long long` are now **distinct**
      (chibicc conflated them at 8 bytes on LP64).
- [x] Big-endian struct/bitfield layout — bitfields allocate MSB-first (`parse.c`). *(Runtime bit
      placement + big-endian data encoding are verified in P2 under `sim68k`, with the 68000 back end.)*
- [x] Integer-constant (`L`/`LL`/`U` + value-based promotion), `sizeof`, `_Alignof`, and
      usual-arithmetic-conversion rules on ILP32.
- [x] Predefined macros: dropped the x86-64/Linux set; added `__m68k__`, `__BYTE_ORDER__=BIG`,
      ILP32 `__SIZEOF_*__`, and the `__INT_MAX__`/`__LONG_MAX__` family.
- [x] `<limits.h>`/`<stdint.h>` added for ILP32; `<stddef.h>` verified (`size_t`/`ptrdiff_t` = 4).
- [x] Type/size unit test [`tests/typemodel.c`](../tests/typemodel.c) — a **compile-time** battery
      (`#if`/`#error` + GNU case-range asserts) run via `make type-check` on **every host** (no
      assembler/linker/execution). Passes locally on the MSVC build.

**Exit:** front end reports correct ILP32-BE sizes/offsets/limits; type tests pass on host.
**Depends on:** P0

> **P1 note.** Flipping to ILP32 makes the interim x86-64 back end non-runnable (pointers are 4 vs 8
> bytes), so the x86-64 conformance suite + self-host are retired here — replaced in CI by the
> compile-time `type-check` (build + smoke + type-model on Linux/macOS/Windows). Real execution
> testing returns in **P2** under `sim68k` with the 68000 back end.

## P2 — 68000 code generation

**Objective:** replace `codegen.c` with a **68000** generator emitting assembly text; run compiled C
on the bare CPU under `sim68k`.

- [x] `codegen68k.c`: stack-machine lowering (accumulator = `D0`, spill via `-(SP)`).
- [x] The **m68k C ABI** ([architecture.md §7.2](architecture.md#72-calling-convention--abi)):
      stack args, `D0(:D1)` return, `D2–D7/A2–A6` callee-saved, `A6` frame via `LINK`/`UNLK`.
      _(Integer scope: only caller-saved `D0/D1/A0/A1` are used, so callee-saved regs are honored
      trivially; 64-bit `D0:D1` return lands with `long long` in P3.)_
- [x] Integer arithmetic, comparisons, logical/bitwise, shifts (helper calls where needed).
- [x] Control flow: `if`/`for`/`while`/`switch`/`goto`, `&&`/`||`, `?:`.
- [x] Pointers, arrays, structs/unions, member access, aggregate copy. _(Struct/union **by-value**
      args & return use a hidden result-buffer pointer at `8(a6)`; aggregate copy is byte-wise.)_
- [x] PC-relative addressing for code/data; even-alignment enforcement.
- [x] Emit Motorola-syntax `.s`; assemble with **`asm68K`** (`/elf`); link a freestanding test (GNU
      `m68k-elf-ld`) with a minimal stub.
- [x] `sim68k` bare-metal harness captures a result register / memory and diffs to golden.
      _(11-case golden suite in [`tests/m68k/`](../tests/m68k) via
      [`tools/m68k/run-tests.ps1`](../tools/m68k/run-tests.ps1); all green.)_

**Exit (M1):** arithmetic, control-flow, function-call, and struct tests run correctly on the 68000
under `sim68k`.
**Depends on:** P1

## P3 — Runtime support library

**Objective:** the helper library the generator calls
([libc-and-toolchain.md §5](libc-and-toolchain.md#5-the-runtime-support-library)).

- [x] 32-bit integer helpers: `__mulsi3`, `__divsi3`/`__udivsi3`, `__modsi3`/`__umodsi3`, shifts.
      _(mul/div/mod in [`rt68k.a68`](../lib/runtime/rt68k.a68); 32-bit shifts emit the 68000's own
      register-count `asl/lsr/asr` inline — no helper needed.)_
- [x] 64-bit `long long`: `__muldi3`, `__divdi3`/`__udivdi3`, `__moddi3`, shifts, compares.
      _(`rt68k.a68`: `__muldi3` via a 16×16 `umul64`, 64-iteration `udivmod64`, `__ashldi3`/`__ashrdi3`/
      `__lshrdi3`, `__cmpdi2`/`__ucmpdi2`; add/sub/logical inline via `addx`/`subx`.)_
- [x] Soft **single** float: add/sub/mul/div/compare/convert.
- [x] Soft **double** float: add/sub/mul/div/compare/convert/extend/truncate (big-endian word order).
      _(Both provided by the worm68k **IEEE754** library `libieee754d.a` — C-stack ABI, pure 68000,
      PIC; the codegen lowers float/double ops and conversions to its `_fpadd`/`_fpaddd`/`_fpltof`/…
      entries. **TODO:** vendor/build the IEEE754 source into c68k's own `librt` for a self-owned,
      Osiris/CP/M-linkable runtime. `long long`↔`float` conversions are deferred, the lib has no
      64-bit int convert.)_
- [x] `memcpy`/`memset` fast paths + struct-copy thunks. _(`_memcpy`/`_memset`/`_memmove` in
      `rt68k.a68`; aggregate copy is emitted inline by the code generator.)_
- [x] Numeric tests vs. host `double`/`long long` golden values (both OSes once P4/P5 land).
      _(20 golden cases in [`tests/m68k/`](../tests/m68k): `ll_*`, `f_*`, `d_*`, `fd_conv` — all green
      under `sim68k`.)_

**Exit:** float and `long long` programs compute results matching host golden.
**Depends on:** P2

## P4 — libc core + Osiris backend

**Objective:** the OS-independent core + the **Osiris** seam and `crt0`; a real `.PRG` runs on
Osiris under `sim68k`.

- [x] Osiris seam over DOS `TRAP #1` ([libc-and-toolchain.md §3](libc-and-toolchain.md#3-the-syscall-seam)).
      _(`libc/osiris/osiris_sys.a68`: write/read/open/creat/close/seek/unlink/exit/sbrk over TRAP #1.)_
- [x] `crt0.osiris` (relocs/argv/heap/`_exit` via `4Ch`). _(`_start` in `osiris_sys.a68`: DOS 48h arena
      claim, stack+heap, `main`, exit via 4Ch; loader applies the R_68K_RELATIVE relocs & zero-fills
      bss. argv is minimal (argc=1) — full command-tail parsing is a refinement.)_
- [x] Core `<string.h>`, `<ctype.h>`, `<stdlib.h>` (`malloc` over `_sbrk`), `<errno.h>`.
      _([`libc/core/libc.c`](../libc/core/libc.c) + [`libc/include/`](../libc/include); malloc is a
      bump allocator over `sys_sbrk`.)_
- [x] Core `<stdio.h>`: buffered `FILE`, `printf`/`fwrite`/`fopen`/`fread`/`fseek`. _(Buffered `FILE`,
      `fopen`/`fclose`/`fread`/`fwrite`/`fgets`/`fputs`/`puts`/`fseek`, and the `printf` family
      (`printf`/`fprintf`/`snprintf`, integer/string/char formats incl. `%lld`) — all running on
      Osiris. m68k `va_arg` is supported (the prologue stores the first stack vararg in `__va_area__`;
      float `%f` conversion is a later add.)_
- [x] `-target osiris` driver: assemble + link with `osiris-prg.ld` → `.PRG`.
      _(via [`tools/osiris/build-prg.ps1`](../tools/osiris/build-prg.ps1): asm68K + c68k + the osiris
      binutils `ld -pie -T osiris-prg.ld`. A c68k-internal `-target osiris` flag is a follow-up.)_
- [x] `HELLO.PRG` and a file-read/write program run correctly on Osiris under `sim68k`.
      _([`samples/hello.c`](../samples/hello.c), [`samples/filerw.c`](../samples/filerw.c) — both PASS.)_
- [x] Osiris lockstep harness (compile → run under `sim68k` → diff golden).
      _([`tools/osiris/run-osiris.ps1`](../tools/osiris/run-osiris.ps1): build → FAT12 deploy → boot
      `c68k-sim68k` → capture ACIA → assert.)_

**Exit:** `HELLO.PRG` + file-I/O programs pass on Osiris.
**Depends on:** P3

## P5 — CP/M-68K backend

**Objective:** the **CP/M-68K** seam, FCB shim, and `crt0`; the same programs run as `.68K`, and the
suite goes **lockstep** across both OSes.

- [x] CP/M seam over BDOS `TRAP #2`, incl. the FCB/record/DMA→byte-stream shim.
      _([`libc/cpm/cpm.c`](../libc/cpm/cpm.c): fd→FCB table, 128-byte record buffering with `F_DMAOFF`,
      sequential `F_READ`/`F_WRITE`, `^Z`-padded close; console via BDOS 6. BDOS primitive +
      crt0 in [`libc/cpm/cpm_sys.a68`](../libc/cpm/cpm_sys.a68).)_
- [x] `crt0.cpm` (base-page cmd tail → `argv`, TPA heap/stack, BDOS-0 exit). _(`_start`: discard
      return-to-CCP, capture base page, stack at `HIGHTPA`, zero bss, heap above bss, `main`, BDOS 0.
      argv minimal (argc=1).)_
- [x] `errno` mapping for CP/M status codes; console-routed `stdin/stdout/stderr`. _(fds 0/1/2 route to
      the BDOS console; `errno` is set at the libc layer on failures. A per-code BDOS→errno table is a
      refinement.)_
- [x] `-target cpm` driver: link with `cpm68k.ld`, then `mkdri` → `.68K`. _(via
      [`tools/cpm/build-68k.ps1`](../tools/cpm/build-68k.ps1): `ld -T cpm68k.ld -Ttext 0x500` +
      `mkdri -b500`. A c68k-internal `-target cpm` flag is a follow-up.)_
- [x] `HELLO.68K` + file-I/O run correctly on CP/M-68K under `sim68k`. _([`tools/cpm/run-cpm.ps1`](../tools/cpm/run-cpm.ps1):
      `cpmcp` deploy to D:, boot `c68k-sim68k`, run — hello + filerw both PASS.)_
- [x] Lockstep runner: every test compiled for **both** OSes, one golden file, both must match.
      _([`tools/run-lockstep.ps1`](../tools/run-lockstep.ps1): hello / filerw / printftest — **3/3
      identical on Osiris and CP/M-68K**.)_
- [x] Port the P0–P4 tests into the lockstep suite. _([`tests/lockstep/coretest.c`](../tests/lockstep/coretest.c):
      a 41-check self-verifying battery (arithmetic, control flow, structs/unions, long long,
      float/double, string, snprintf) — `SUITE PASS 41/41` on both OSes.)_

**Exit (M2): ✅ reached** — the same C source runs as `.PRG` (Osiris) and `.68K` (CP/M-68K) with matching output.
**Depends on:** P4

## P6 — C99 language completeness

**Objective:** close remaining C99 language gaps and prove them on both OSes.

- [x] Full initializer support (designated initializers, compound literals, nested aggregates).
      _(All verified in [`tests/lockstep/c99test.c`](../tests/lockstep/c99test.c): `.field`/`[idx]`
      designators, `(T){...}` literals incl. array compound literals, nested struct/array init.)_
- [x] Flexible array members, `_Bool`, `restrict`/`inline` semantics, `long long` everywhere.
      _(Flexible array member, `_Bool` (1 byte, normalize-to-0/1), and `long long` all tested;
      `restrict`/`inline` are parsed and honored by the front end.)_
- [x] Variadic functions end-to-end on the m68k ABI (`<stdarg.h>` `va_*`). _(P4: prologue stores the
      first stack vararg in `__va_area__`; drives the `printf` family.)_
- [x] VLAs / variably-modified types (or a documented, tested exclusion). _(**Documented exclusion**:
      c68k rejects a VLA with a clear diagnostic — "variable-length arrays are not supported" — rather
      than miscompiling; use a fixed bound or `malloc`.)_
- [x] Bitfield edge cases on big-endian ILP32. _(Codegen does shift/mask extract + load-modify-store;
      signed/unsigned fields and width truncation verified.)_
- [x] A C99 language conformance battery, green on both OSes. _(`c99test.c`: **C99 PASS 18/18** on both
      Osiris and CP/M-68K via [`tools/run-lockstep.ps1`](../tools/run-lockstep.ps1) — lockstep now 5/5.)_

**Exit (M3a): ✅ reached** — language battery passes lockstep on both OSes.
**Depends on:** P5

## P7 — C99 standard library

**Objective:** complete the hosted-subset library + `libm`
([libc-and-toolchain.md §9](libc-and-toolchain.md#9-c99-library-conformance-scope)).

- [x] `printf`/`scanf` full conversion coverage (incl. `%lld`, `%f`/`%g`, `%p`, width/precision/flags). `printf` family + `sscanf`/`vsscanf`.
- [x] `<stdlib.h>` breadth: `strtol`/`strtoul`/`strtod`, `qsort`/`bsearch`, `rand`, `div`/`ldiv`, `labs`, `atof`.
- [x] `<string.h>` full set; `<time.h>` formatting over the seam clock. *(string set done; `<time.h>` wall clock over Osiris DOS `2Ah`/`2Ch` and CP/M-68K BDOS `105` — `time`/`gmtime`/`localtime`/`mktime`/`difftime`/`asctime`/`ctime`/`strftime`.)*
- [x] `<math.h>` via the `libieee754d` soft-float donor (double transcendentals) on soft-float.
- [x] `<inttypes.h>`, `<stdint.h>`, `<float.h>` completeness; `<assert.h>`, `<signal.h>` (minimal, synchronous).
- [x] Freestanding mode (`-ffreestanding`) validated (headers-only + runtime lib). *(`-ffreestanding` sets `__STDC_HOSTED__=0`; full C99 freestanding header set — `<stddef.h>`, `<stdint.h>`, `<limits.h>`, `<stdbool.h>`, `<stdarg.h>`, `<float.h>`, `<iso646.h>`; `tests/m68k/freestd.c` links no libc, 40/40 bare-metal.)*
- [x] Library conformance suite, green lockstep on both OSes (`mathtest` 14/14, `libtest` 26/26, `timetest` 15/15).

**Exit (M3):** hosted library + `libm` suites pass lockstep on both OSes.
**Depends on:** P6

## P8 — Integrated object emitter

**Objective:** emit **ELF32-BE relocatable objects directly**, removing the external-assembler
dependency ([architecture.md §8](architecture.md#8-object-emission-text-asm-now-integrated-elf-later)).

- [x] `emit_elf.c`: ELF32-BE object writer (headers, `.text`/`.data`/`.bss`/`.rodata`, symtab, strtab). *(Integrated assembler: parses the compiler's own Motorola text and encodes it — codegen stays untouched.)*
- [x] 68000 instruction **binary encoder** shared with the text path's instruction selection. *(Full vocabulary c68k emits; verified vs `asm68K` via `objdump`.)*
- [x] Relocation records: `R_68K_32`, `R_68K_PC16`/`PC32`, `R_68K_RELATIVE` as needed. *(Objects emit `R_68K_32` for absolute symbol refs; branches stay intra-section (assembled displacements, no reloc); `ld -pie` derives `R_68K_RELATIVE` for the PIE.)*
- [x] `-c` integrated-emit mode in the driver. *(`-fintegrated-as`; hosted builds via `C68K_INTEGRATED_AS=1`.)*
- [x] **Byte-diff** integrated objects vs. `m68k-elf-as` across the whole corpus; links must match. *(Byte-identical isn't a goal — `asm68K` relaxes/peepholes `bsr`/`addq`/short branches; equivalence is proven by `objdump`-diff showing only those substitutions **and** by link+run: bare-metal 40/40 and hosted lockstep 8/8 on both OSes through the integrated emitter.)*

**Exit:** the compiler produces linkable objects with **no assembler**; validated against `as`. **✅ reached** — `src/emit_elf.c`; 40/40 + 8/8 integrated.
**Depends on:** P7

## P9 — Native LINK / LIB / mkdri

**Objective:** a **native** link/archive chain on both OSes
([libc-and-toolchain.md §7](libc-and-toolchain.md#7-the-native-toolchain)).

- [x] Verify Osiris `LINK.PRG` / `LIB.PRG` consume c68k objects/archives; wire the native recipe. *(Proven on Osiris under sim68k — [`tools/osiris/run-native-link.ps1`](../tools/osiris/run-native-link.ps1): c68k integrated objects link with the native `LINK.PRG` into a running `.PRG` (bare, multi-TU cross-object call, and the full `libc.o` all parse cleanly); `LIB.PRG` (`LIB rcs`) archives a c68k object and `LINK` pulls the member. Samples: `bare.c`, `multi.c`+`multi_helper.c`.)*
- [x] Port `LINK` to CP/M-68K (`LINK.68K`) — file I/O moved to BDOS FCBs. *(Done — links objects **byte-identically to `m68k-elf-ld`** on CP/M-68K under `sim68k`. **One source tree**: the OS-neutral Osiris linker modules assemble with `/DCPM` (asm68K `ifdef` conditional assembly) over a CP/M **cmdlib backend** (`worm68k\cpm68k\cmdlib`: crt0 + machine-heap `cl_alloc` + FCB `cl_open/cl_fsize/cl_read/cl_write/cl_close` + BDOS console + base-page command-tail args + formatters). New `cl_alloc`/`cl_fsize` cmdlib primitives abstract the DOS `$48`/`42h` traps so `lk_main`/`lk_out`/`ol_elf` are OS-neutral (Osiris re-validated: `bare`/`nofloat`/full `hello` all link+run). Built ELF→`mkdri`→`.68K`; test `worm68k\cpm68k\link\test-link.ps1`.)*
- [x] Port `LIB` to CP/M-68K (`LIB.68K`). *(Done — archives objects **byte-identically to `ar Drcs`** on CP/M-68K (of the 128-byte-record-padded members CP/M stores). Same one-source-tree `/DCPM` + CP/M cmdlib backend; `lib_main`/`lib_out` OS-neutralized via `cl_alloc`/`cl_fsize`, operation-key decode case-folded under `/DCPM` (the CP/M CCP upper-cases the command tail). Test `worm68k\cpm68k\lib\test-lib.ps1`.)*
- [x] Build `mkdri` as a native `.68K` (or confirm the host path) for the CP/M final step. *(Superseded — **`LINK` now emits the DRI command file directly** (below), so no separate `mkdri`/`reloc` pass is needed on CP/M. The host `mkdri` remains available for ELF/COFF conversion and symbol maps.)*
- [x] Native recipes: Osiris `CC→LINK→.PRG`; CP/M `CC→LINK→.68K` (LINK emits DRI directly). *(Osiris `CC→LINK→.PRG` and `CC→LIB→LINK→.PRG` done. **CP/M `CC→LINK→.68K` is now one native step**: under `/DCPM`, `LINK` lays the program out as a base-`0` relocatable module (`lk_layout`), records every `R_68K_32` fixup (`lk_reloc`), and `lk_dri` writes a **DRI command file** directly — the default absolute `.68K` (`rlbflg=$FFFF`, entry = load base) or, with `/R`, a relocatable `.REL` (`rlbflg=0`, `LUPPER`+`T/D/BRELOC` reloc-bit streams the CP/M loader slides). `/B:hhhh` sets the load base (default `0x500`). Validated on CP/M under `sim68k` ([`worm68k\cpm68k\link\test-cpmlink.ps1`]): the absolute `.68K` **and** the relocatable `.REL` both run; the `.68K` is byte-identical to `mkdri -b500` of the `ld` reference; and the **original CP/M-68K `reloc` tool** relocates LINK's `.REL` to a byte-identical `.68K` — proving LINK's `.REL` is a bona-fide DRI relocatable. Plain Osiris (`no /DCPM`) build re-verified **byte-identical** — every LINK/objlib `.o` unchanged.)*
- [x] A multi-object + archive program links natively on both OSes and runs under `sim68k`. *(Osiris — multi-TU + `LIB` archive both run (`MULTI TU OK`). CP/M — `LINK.68K` links a multi-object program and consumes a `LIB.68K` archive, output byte-identical to `ld`/`ar`; `LINK` emits a **runnable** DRI `.68K`/`.REL` directly (see the recipe above).)*

> **Finding (Osiris `LINK.PRG` bug — root-caused and FIXED).** c68k's objects — including the full single-TU `libc.o` — link cleanly, but `LINK.PRG` originally address-exceptioned on the 28-member `libieee754d.a`. Traced under **sid68k** (`!ex catch addr` on `sim68k --gdb`, unstripped LINK re-link for symbols): `move.l (a5),d1` in `lk_sym_add_object` with `A5` **odd** (`0x0EFABB`) — an archive member's ELF `.symtab` pointer. Member `dpmath.o` has its `.symtab` at an **odd file offset (`0x18d3`)**, so the in-place pointer (even member base + odd `sh_offset`) is odd and the 68000 can't `move.l` from it. **Fixed** in the Osiris repo (`commands/objlib/ol_elf.a68` `oe_sym`: copy an odd-offset symtab to an even block so all readers stay aligned). Validated: the native `LINK.PRG` now links the full **`hello`** (c68k libc + the 28-member float archive) into a running `.PRG` on the 16 MB Osiris model (`--cpu 68000`). *(On the 1 MB model it's memory-tight — LINK's file + arena allocations for the 125 KB archive exceed 1 MB, a capacity note, not the bug.)* Repro/analysis tool: [`tools/osiris/debug-link-fault.ps1`](../tools/osiris/debug-link-fault.ps1).

**Exit:** native linking/archiving builds real (multi-TU) programs on both OSes.
**Depends on:** P8

## P10 — Self-hosting bootstrap

**Objective:** the native compiler compiles **its own source** to a byte-identical binary.

- [ ] Cross-compile the compiler for m68k → `CC.PRG` / `CC.68K` (stage2).
- [ ] Run `CC` under `sim68k` to compile its own source → stage3.
- [ ] **stage2 == stage3** (byte-identical) on **both** OSes.
- [ ] Fit/perf pass: the native compiler runs within a realistic Osiris/CP/M memory budget.
- [ ] Make the three-stage check a permanent CI gate.

> **Progress (self-host groundwork).** The self-host build **configuration** and the **libc gaps**
> are in: a `-DC68K_SELFHOST` mode in [`src/compat.h`](../src/compat.h) bypasses the host driver
> shims and pulls only the POSIX headers c68k's own libc provides, and [`libc/core/libc.c`](../libc/core/libc.c)
> gained the functions the compiler's source needs — `open_memstream` (a growing memory-backed
> `FILE`), `strdup`/`strndup`, `strncasecmp`/`strcasecmp`, `strtoull`/`strtoll`/`strtold`, `strtok`,
> `strerror`, `dirname`/`basename`, `ctime_r`, and a stub `stat` (so `__TIMESTAMP__` takes its
> "unknown" fallback) — plus new `<strings.h>`/`<libgen.h>`/`<sys/stat.h>`/`<sys/types.h>` headers.
> With these, **all 9 core compiler translation units self-compile** with the cross-compiler's
> integrated ELF emitter (`strings`, `hashmap`, `unicode`, `type`, `tokenize`, `preprocess`, `parse`,
> `codegen68k`, `emit_elf`), and so does the extended `libc.c`; existing programs are unregressed
> (`printftest` runs, incl. 64-bit `printf`). The last front-end blocker — `parse.c`'s `long long ↔
> float/double` conversion (a **P3-completeness** gap) — is now **closed**: [`codegen68k.c`](../src/codegen68k.c)
> `cast()` emits `_fplltod`/`_fpulltod`/`_fplltof`/`_fpulltof`/`_fpdtoll`/`_fpdtoull`/`_fpftoll`/`_fpftoull`
> (signed/unsigned aware), implemented in `libc.c` by decomposition over the existing 32-bit
> conversions + IEEE double arithmetic (correctly rounded, no hand-rolled asm). Verified on Osiris
> ([`samples/fp64conv.c`](../samples/fp64conv.c)): all int64⇄double round-trips, the `2^63` uint64
> edge, truncation-toward-zero, and float rounding are correct. The **native `main.c` driver** is
> now in too: under `C68K_SELFHOST`, `main()` forces the integrated emitter and compiles each `.c`
> to an ELF `.o` **in-process** (no subprocess, no external assembler, no linker — linking is the
> separate `LINK.PRG`/`LINK.68K` step); the host spawn/`run_cc1`/`run_linker`/`find_*` paths are
> `#ifdef`'d out, and libc gained `atexit`/`unlink`/`close` + a native `file_exists`/`create_tmpfile`
> (new `<unistd.h>`). So **all 10 compiler translation units + `libc.c` self-compile**, and
> [`tools/osiris/build-cc.ps1`](../tools/osiris/build-cc.ps1) **links the stage2 `CC.PRG`** (a
> ~440 KB static-PIE Osiris binary) from them + crt0 + runtime + the soft-float archive. **Next:**
> smoke-run `CC.PRG` under `sim68k` (compile a `.c` → `.o` on-target; the compiler's arena vs. the
> memory model is the risk), then the stage3 self-recompile and the byte-identity check, then CP/M.

**Exit (M4):** `CC` self-hosts to a byte-identical binary on both OSes.
**Depends on:** P9

## P11 — Cross-compiler hardening

**Objective:** treat the cross-compiler as a **maintained product** for building any Osiris/CP/M-68K
tool.

- [ ] Driver/option parity (`-c`/`-S`/`-o`/`-I`/`-D`/`-L`/`-l`/`-O`/`-g`/`-target`/`-ffreestanding`).
- [ ] Robust diagnostics (carets, notes, sane messages) and exit codes.
- [ ] Packaging/install for host OSes; documented invocation.
- [ ] CI **matrix**: build cross + run the **full lockstep suite** on both OSes per commit.
- [ ] Build a **real external tool** (e.g. an Osiris/CP/M utility) with c68k as a proof.
- [ ] SDK usage docs for third-party programs.

**Exit (M5):** cross-compiler is CI-gated, packaged, and building real programs for both OSes.
**Depends on:** P10 (usable earlier; hardened here)

## P12 — Optimization

**Objective:** move beyond the stack machine to reasonable code quality — **without** regressing
correctness.

- [ ] Temporary/register allocator: keep hot values in `D2–D7`/`A2–A5`, spill on pressure.
- [ ] Peephole pass (kill push/pop pairs, redundant moves, `tst` after arithmetic).
- [ ] Constant folding/propagation and strength reduction in the back end.
- [ ] 68000 addressing-mode selection (indexed/PC-relative/`Dn` predecrement) for common patterns.
- [ ] `-O` levels; size vs. speed knobs.
- [ ] Full suite still green lockstep; record size/speed deltas.

**Exit:** measurable size/speed improvement; all tests still pass on both OSes.
**Depends on:** P10

## P13 — Tooling & debug polish

**Objective:** debuggability and developer experience.

- [ ] DWARF (or a `sim68k`-friendly) line/symbol info; `-g`.
- [ ] Assembly listings and link map output.
- [ ] Diagnostic quality pass (warnings set, `-W` flags).
- [ ] `samples/` gallery building for both OSes.
- [ ] Finalize the SDK docs; per-phase changelogs reconciled.
- [ ] Source-level debugging demonstrated under `sim68k` / `m68k-elf-gdb`.

**Exit:** compiled programs are source-debuggable; docs and samples complete.
**Depends on:** P11, P12

---

## Dependency graph

```mermaid
flowchart LR
    P0 --> P1 --> P2 --> P3 --> P4 --> P5 --> P6 --> P7 --> P8 --> P9 --> P10
    P10 --> P11
    P10 --> P12 --> P13
    P11 --> P13
```

## How to update this document

1. Flip the task checkbox (`[ ]`→`[x]`) as each task lands.
2. Update the phase **Status** (☐/◐/☑) and its **Tasks n / N** count in the
   [dashboard](#progress-dashboard).
3. Update the **Total** row (`x / 14` phases, `n / 87` tasks).
4. When a milestone's phase closes, note it in the phase changelog and here.

---

### Changelog

| Date | Version | Change |
| --- | --- | --- |
| 2026-07 | Draft 0.1 | Initial 14-phase plan (P0–P13), progress dashboard, milestones, dependency graph. |
| 2026-07 | Draft 0.1 | P0 scaffolding landed: chibicc imported (unmodified, commit `90d1f7f`) into `src/`; repo layout created; `Makefile` + `CMakeLists.txt` host build; conformance suite in `tests/`; `selfhost` stage2==stage3 check; GitHub Actions CI. Test-green + self-host verification run on Linux CI (dev host is Windows/MSVC-only). |
| 2026-07 | Draft 0.1 | P0 host strategy revised (decision D11): build the cross-compiler on **Windows/MSVC** + **macOS/Clang** via a new `src/compat.{h,c}` POSIX/Win shim; Linux becomes a CI-only full-suite + self-host safety net; Windows/macOS CI run a front-end smoke check. MSVC `cl` build + front end verified locally on Windows. |
| 2026-07 | Draft 0.1 | **P0 complete (6/6).** CI green on all three hosts: Linux full conformance suite + `stage2==stage3` self-host (gcc + clang), macOS + Windows/MSVC build + front-end smoke. Landing fixes: `tests/` path-rename stragglers, `driver.sh` exec bit + `.gitattributes` LF, `actions/checkout@v5` (Node 24), and `-Iinclude` for the relocated stage2/stage3 self-host. |
| 2026-07 | Draft 0.1 | **P1 complete (6/6).** Front end retargeted to big-endian ILP32: `type.c` sizes/alignments (2-byte, the m68k-elf default), a `long` vs `long long` split, big-endian bitfields, ILP32 integer-literal typing, m68k/big-endian predefined macros, and new `<limits.h>`/`<stdint.h>`. Verified by a compile-time `tests/typemodel.c` (`make type-check`) on every host. The non-runnable x86-64 back end's execution + self-host CI is retired until P2 (`sim68k`). |
