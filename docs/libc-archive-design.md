# c68k Standard Library Archives — Architecture & Design Spec

**Status:** Draft v1.0  ·  **Target:** `libc.a`, `libm.a`, `libheap.a` (m68k, c68k toolchain)  ·  **Owner:** _unassigned_
**Related docs:** [libc-and-toolchain.md](libc-and-toolchain.md) · [sdk.md](sdk.md) · [architecture.md](architecture.md) · [implementation-plan.md](implementation-plan.md) · [reference-manual.md](reference-manual.md)

---

## Table of Contents

1. [Purpose & Scope](#1-purpose--scope)
2. [Problem Statement](#2-problem-statement)
3. [Goals & Non-Goals](#3-goals--non-goals)
4. [Constraints & Invariants](#4-constraints--invariants)
5. [Current State (inventory)](#5-current-state-inventory)
6. [Chosen Architecture](#6-chosen-architecture)
7. [Directory Layout](#7-directory-layout)
8. [Build & Link Model](#8-build--link-model)
9. [Vendoring: libheap & libieee754d](#9-vendoring-libheap--libieee754d)
10. [Testing & Measurement](#10-testing--measurement)
11. [Backward Compatibility & Migration](#11-backward-compatibility--migration)
12. [Risks & Mitigations](#12-risks--mitigations)
13. [Phased Implementation Plan & Progress Tracker](#13-phased-implementation-plan--progress-tracker)
14. [Open Questions](#14-open-questions)
15. [Changelog](#15-changelog)

---

## 1. Purpose & Scope

The c68k target C library is today a single translation unit, [libc/core/libc.c](../libc/core/libc.c),
compiled to one `libc.o` and **linked as a bare object**. A bare object is pulled into the
executable *in its entirety*, so every `.PRG` / `.68k` carries **all** of libc — `stdio`,
`stdlib`, `string`, `ctype`, `signal`, `time` — even a program that only calls `puts`. The
soft-float/math library (`libieee754d.a`) is consumed as a **prebuilt binary from a sibling
repo** (`C:\git\worm68k\68kTools\libraries\float\ieee754\`), referenced by five build scripts,
and there is no first-class `libm`.

This document specifies the architecture and a **phased, progress-tracked plan** to restructure
the standard library into proper, dead-strippable **archives** built entirely inside the c68k
tree: `libc.a`, `libm.a`, and the vendored `libheap.a`. The plan is incremental — each phase is
independently buildable, testable against the lockstep suite, and reversible.

Scope **includes**: splitting `libc.c` into per-function objects; standing up `libc.a`/`libm.a`;
vendoring the heap and IEEE-754 float sources; updating the three link contexts (Osiris cross,
CP/M, native self-host); wiring `malloc` onto the heap; size/behaviour measurement.

Scope **excludes**: the Small Object Allocator internals (tracked in the heap spec); new libc
features beyond what `libc.c` already implements; async signals (still synchronous); adding
C standards coverage.

---

## 2. Problem Statement

1. **No dead-code elimination.** `libc.o` is a bare object, so `ld` includes the whole thing.
   Every executable pays for all of stdio + stdlib + string + time regardless of what it uses.
   Archive member selection (an archive member is linked only to satisfy an undefined symbol)
   would strip unused code — but only if the library is an **archive of small objects**.
2. **External binary dependency.** `libieee754d.a` is a prebuilt artifact from `worm68k`,
   hard-coded in five scripts ([build-prg.ps1](../tools/osiris/build-prg.ps1),
   [build-cc.ps1](../tools/osiris/build-cc.ps1), [build-68k.ps1](../tools/cpm/build-68k.ps1),
   [build-cc-68k.ps1](../tools/cpm/build-cc-68k.ps1), [run-tests.ps1](../tools/m68k/run-tests.ps1)).
   The tree is not self-contained or reproducible from source.
3. **No `libm`.** `lib/libm/` is an empty placeholder. Math has no first-class home, and the
   float ABI + `<math.h>` are entangled in the single float archive.
4. **Pending heap integration.** The SOA heap (`libheap`) is to be vendored (option **b**); it
   needs an archive home and a `malloc`/`free` seam.

---

## 3. Goals & Non-Goals

### Goals
- **G1** Ship `libc.a` and `libm.a` built entirely in-tree; remove the external `libieee754d.a`
  path from all build scripts.
- **G2** Achieve object-granularity dead-stripping: a program links only the library objects it
  references. Measure a size reduction on the sample programs vs. the current bare-object link.
- **G3** One-function-per-file where practical (matching the heap and IEEE-754 libraries), with
  genuinely-coupled cores (stdio) kept cohesive.
- **G4** Preserve behaviour: the lockstep suite (`coretest`/`libtest`/`mathtest`/`timetest`) and
  the Osiris + CP/M smoke tests pass byte-for-behaviour at every phase.
- **G5** Work across **all three link contexts**: Osiris cross (`m68k-elf-ld`), CP/M
  (`SysGCC ld` / `mkdri`), and native self-host (Osiris `LIB.PRG` + `LINK.PRG`).
- **G6** Vendor the heap and IEEE-754 float **sources** (not prebuilt `.a`) so each toolchain
  builds its own archive from a single source of truth.
- **G7** Provide a `malloc`/`free`/`realloc`/`calloc` seam over `libheap` (real reclamation),
  gated so it can A/B against the current bump allocator.

### Non-Goals
- **N1** SOA internals / small-object density (heap spec; lands transparently once `libheap` is in).
- **N2** New libc surface area or standards conformance work.
- **N3** Async signal delivery (remains synchronous).
- **N4** Changing the c68k symbol mangling (one leading `_`) or the syscall seam ABI.

---

## 4. Constraints & Invariants

| # | Constraint | Source |
|---|-----------|--------|
| C1 | Assembly is **asm68K, `/Cx`** (case-sensitive), ELF output. The same assembler builds the heap and float `.a68`. | [libc-and-toolchain.md](libc-and-toolchain.md) |
| C2 | c68k mangles C `name` → `_name` (one leading underscore); asm exports match. | seam convention |
| C3 | `ld` selects archive members at **object granularity**, not function. One function per file maximises stripping. | GNU ld |
| C4 | **Three link contexts** must all consume the archives: Osiris `m68k-elf-ld` (GNU 2.44), CP/M `SysGCC ld`/`mkdri`, and native Osiris `LINK.PRG` (archived by `LIB.PRG`). Archive format compatibility is not guaranteed across GNU `ar` and Osiris `LIB` — build each with its own archiver from shared sources. | [run-native-link.ps1](../tools/osiris/run-native-link.ps1) |
| C5 | Self-host requires stage2 and stage3 objects to stay **byte-identical**; the target-libc restructure must not perturb the host compiler build. | [Makefile](../Makefile) `selfhost` |
| C6 | `rt68k` is the **compiler runtime** (integer helpers `__mulsi3`/`__divsi3`…, `memcpy`/`memset`/`memmove`) and is **always** linked — it is not part of `libc.a`. | [lib/runtime/rt68k.a68](../lib/runtime/rt68k.a68), [libc.c](../libc/core/libc.c) L37 |
| C7 | The syscall seam (`_sys_*`) is provided per-platform (`osiris_sys.a68`, `cpm_sys.a68`) and stays outside the portable archives. | [osiris_sys.a68](../libc/osiris/osiris_sys.a68) |
| C8 | Single-threaded; signals synchronous. No reentrancy guarantees required. | [libc.c](../libc/core/libc.c) L553 |

**Invariant I1 — always-available float ABI.** The soft-float arithmetic the compiler emits
(double/float `+ − × ÷`, compares, conversions) must resolve **without** the user passing `-lm`.
Therefore `libm.a` is **always placed in the default link line**; archive member selection makes
it cost **zero** for programs that touch no float. (`-lm` remains the documented SDK convention
for standalone links.)

---

## 5. Current State (inventory)

**`libc/core/libc.c`** (~1.4k LoC) — sectioned by header, giving natural split seams:

| Section | Line | Notes |
|--------|-----:|------|
| syscall seam externs + `errno` | 19 | `_sys_*` decls; `int errno;` global |
| soft-float format helpers | 30 | `fpdtol`, `floord` (from float lib) for `%f/%e/%g` |
| `<string.h>` | 37 | `mem*` come from `rt68k` |
| `<ctype.h>` | 139 | |
| `<stdlib.h>` | 157 | **bump `malloc` / no-op `free` / `realloc` / `calloc`**, `atoi`/`strtol`/`qsort`/`exit`… |
| `<signal.h>` | 553 | minimal, synchronous |
| `<stdio.h>` | 585 | buffered streams: `_streams[]` FILE table, `printf` family, `fopen`/`fread`/`fwrite`/`fflush`/`fclose`, exit-time flush |
| `<time.h>` | 1381 | wall clock over the seam |

**`lib/runtime/rt68k.a68`** — integer runtime + `mem*` (always linked). **`lib/libm/`** — empty
placeholder. **`libieee754d.a`** — external; already a per-function archive of: soft-float **core**
(`fpadd/fpmul/fpdiv/dpadd/dpmul/dpdiv/…cmp/neg`), **math** (`sqrt/sincos/atan/exp/log/floor/dpmath`),
**conv** (`ltof/ftol/atof/dpconv/atod`), **fmt** (`fcvt/FormatFloat/FormatDouble`).

**Link (Osiris, [build-prg.ps1](../tools/osiris/build-prg.ps1)):**
`ld … osiris_sys.o prog.o libc.o rt68k.o libieee754d.a` — one bare `libc.o`, external float `.a`.

---

## 6. Chosen Architecture

Five link inputs, cleanly layered:

```
crt0 + seam   (per platform: osiris_sys.o / cpm_sys.o)   [not archived]
librt68k      (integer helpers + mem*, soft-float ABI)   [always linked]
libc.a        (string, ctype, stdlib, stdio, signal, time, malloc-shims)
libm.a        (<math.h>: sqrt/sin/cos/atan/exp/log/pow/floor + float fmt)   [always in link; member-selected]
libheap.a     (SOA heap engine; malloc/free backend)     [vendored]
```

- **Object granularity = one function per file** (`str_len.c`, `printf.c`, `qsort.c`, …), so `ld`
  strips everything unreferenced. Genuinely-coupled code stays together (below).
- **stdio core cohesion.** The `_streams[]` FILE table + `__fillbuf`/`__flushbuf` buffer engine
  live in **one** `stdio_core` object that thin per-function files (`printf.o`, `fopen.o`,
  `fwrite.o`…) reference. **Exit-time flush** is a hook pulled in **only if** a stdio object is
  linked (a registered-flush slot), so `exit` does not drag stdio into non-stdio programs.
- **`malloc` shims** (`malloc`/`free`/`realloc`/`calloc`) are small `libc.a` objects that call the
  `libheap` engine; behind a build switch they can fall back to today's bump allocator.
- **Soft-float ABI placement (I1).** The compiler-emitted float core is always available; the
  pragmatic default is to keep the vendored IEEE-754 as one `libm.a` that is always in the link
  (member-selected). Splitting the ABI core into `librt68k` is a later refinement (§14 Q1).
- **`librt68k` unchanged in spirit** — the libgcc analogue, always linked.

---

## 7. Directory Layout

Proposed (mirrors the one-function-per-file convention already used by the heap/float libs):

```
libc/
  core/            string/*.c ctype/*.c stdlib/*.c stdio/*.c signal/*.c time/*.c errno.c
                   malloc/*.c        (heap shims: malloc.c free.c realloc.c calloc.c)
  include/         (unchanged public headers)
  osiris/ cpm/     (unchanged per-platform seams: *_sys.a68)
lib/
  runtime/         rt68k.a68                       -> librt68k (or rt68k.o)
  libm/            core/*.a68 math/*.a68 conv/*.a68 fmt/*.a68   (VENDORED from ieee754)
                   Makefile  (-> libm.a)
  heap/            *.a68  heap.inc  Makefile        (VENDORED from worm68k libheap) -> libheap.a
```

Each library keeps its own `Makefile`/build fragment producing an archive via `ar -rcs` + `ranlib`
(the heap library's makefile is the template).

---

## 8. Build & Link Model

- **Archiver.** GNU cross path: `m68k-elf-ar -rcs` + `m68k-elf-ranlib` (writes the symbol index so
  intra-archive cross-refs resolve regardless of member order). Native path: Osiris `LIB.PRG`.
- **Link order.** `crt0/seam  prog  -lc  -lm  -lheap  librt68k`. `ranlib` indices allow multi-pass
  resolution within an archive; for any cross-archive cycle use `--start-group/--end-group`
  (GNU) or list in dependency order.
- **Three contexts (all repointed off the external float `.a`):**
  1. **Osiris cross** — [build-prg.ps1](../tools/osiris/build-prg.ps1),
     [build-cc.ps1](../tools/osiris/build-cc.ps1): `m68k-elf-ld -pie …` + in-tree `libm.a`/`libheap.a`.
  2. **CP/M** — [build-68k.ps1](../tools/cpm/build-68k.ps1),
     [build-cc-68k.ps1](../tools/cpm/build-cc-68k.ps1), [run-tests.ps1](../tools/m68k/run-tests.ps1).
  3. **Native self-host** — [run-native-link.ps1](../tools/osiris/run-native-link.ps1): archives
     built on-target with `LIB.PRG`, consumed by `LINK.PRG`.
- **Switches preserved:** integrated-AS (`C68K_INTEGRATED_AS`), `-O` (`C68K_OPT`), `-g` (`C68K_G`).

---

## 9. Vendoring: libheap & libieee754d

**Decision — vendor the sources of both (option b), not the prebuilt archives.** Rationale:

1. **Self-containment / reproducibility (same reason as the heap).** `libieee754d.a` is external
   and referenced in five scripts; vendoring its sources removes the cross-repo binary dependency,
   exactly as for `libheap`.
2. **One source of truth for three archivers.** Only sources let the GNU cross path *and* the
   native Osiris `LIB.PRG` each build their own archive format (C4). A prebuilt GNU `.a` may not
   be consumable by `LINK.PRG`.
3. **The float library already *is* the model.** It is one-function-per-file archived with `ar`,
   and its `math/*` members (`sqrt/sincos/atan/exp/log/floor/dpmath`) are precisely `libm`’s
   content — so "vendor libieee754d" and "stand up libm.a" are the **same task**.

**What to vendor:** copy the `ieee754` `.a68` tree (`core/ conv/ fmt/ math/` + `ieee754*.inc` +
Makefile) into `lib/libm/`, and the heap `.a68` + `heap.inc` + makefile into `lib/heap/`. Keep the
upstream spec docs as provenance. Record the source commit/version for future sync.

---

## 10. Testing & Measurement

- **Baseline (Phase A).** Record `.PRG`/`.68k` sizes for the sample set
  ([samples/](../samples/): `hello`, `printftest`, `fp64conv`, `hexdump`, `filerw`) under the
  current bare-object link. This is the size-win yardstick for G2.
- **Behaviour parity.** [tools/run-lockstep.ps1](../tools/run-lockstep.ps1)
  (`coretest`/`libtest`/`mathtest`/`timetest`) must stay green at **every** phase.
- **Smoke.** Osiris ([run-osiris.ps1](../tools/osiris/run-osiris.ps1)) and CP/M
  ([run-cpm.ps1](../tools/cpm/run-cpm.ps1)) run `hello` + a float + a file-I/O program.
- **Self-host.** After the malloc/archive changes, rebuild the self-hosted CC and re-run lockstep
  on-target (validates the target-only paths the host never exercises).
- **Size delta.** Re-measure the sample set after Phase 4; report per-program bytes saved.

---

## 11. Backward Compatibility & Migration

- **Public headers unchanged** — `libc/include/*` keep their contents; only the implementation is
  refactored and re-archived.
- **Link lines change**, not source: scripts swap `libc.o … libieee754d.a` for `-lc -lm -lheap`.
- **`malloc` semantics** improve (real `free`) behind a switch; default can stay bump-allocator
  until the heap path is validated (G7), then flip.
- **Incremental & reversible** — Phase 1 archives the *current* single object with no behaviour
  change, proving the plumbing before any code moves.

---

## 12. Risks & Mitigations

| # | Risk | Impact | Mitigation |
|---|------|--------|-----------|
| R1 | Archive format differs between GNU `ar` and Osiris `LIB.PRG` | Self-host link fails | Vendor **sources**; build each archive with its own archiver (C4); smoke both linkers early (Phase 1). |
| R2 | stdio split forces `exit`→stdio dependency or poor stripping | Bloat / coupling | `stdio_core` cohesive object + registered exit-flush hook (§6). |
| R3 | Cross-archive symbol cycles (libc↔libm↔heap) | Link errors | `ranlib` indices + `--start-group`/dependency ordering (§8). |
| R4 | Removing the external float path breaks a script not in the inventory | Build break | Grep-audit all `*.ps1`/`makefile*` for `libieee754d`/float path before flipping (Phase 2). |
| R5 | Soft-float ABI not linked without `-lm` | Float programs fail to link | I1: `libm.a` always in the default link line; member-selected so free when unused. |
| R6 | Self-host stage2≠stage3 after refactor | Selfhost gate breaks | Target-libc change is orthogonal to host compiler objects (C5); re-run `selfhost`. |
| R7 | Real `free()` surfaces latent UAF in target-only code | Regression | Host runs already exercise real `free` cleanly; gate malloc-on-heap behind a switch; run on-target lockstep + smoke (Phase 5). |
| R8 | Per-function explosion complicates the build | Maintenance | Mirror the heap makefile pattern; generate object lists; keep coupled cores together. |

---

## 13. Phased Implementation Plan & Progress Tracker

**Status legend:** ⬜ Not started · 🟨 In progress · ✅ Done · ⛔ Blocked

| Phase | Title | Status | Exit criteria |
|------:|-------|:------:|---------------|
| A | Design & baseline | ✅ | This spec accepted; sample `.PRG` sizes + lockstep baseline recorded |
| 1 | Archive plumbing MVP | ✅ | `libc.a` (single member) links via `-lc` on Osiris + CP/M; behaviour identical |
| 2 | Vendor float → `libm.a` | ✅ | ieee754 sources in `lib/libm`; `libm.a` built in-tree; all 5 scripts off the external path; float samples pass |
| 3 | Vendor heap → `libheap.a` | ✅ | heap sources in `lib/heap`; `libheap.a` builds in all contexts; heap smoke links |
| 4a | libc split — infrastructure + clean carves | ✅ | multi-file `libc.a` build; carve errno/signal/time; lockstep green; measured drop |
| 4b | libc split — decouple hotspots | ✅ | fp64 → own object; stdio core no longer references `realloc` (drain hook) |
| 4c | libc split — one object per function (**goal**) | ⬜ | every public libc function separately linkable; shared engines/state in cohesive cores |
| 5 | `malloc` on `libheap` | ⬜ | real `free` behind a switch; on-target lockstep + smoke pass |
| 6 | Native self-host archives + docs + CP/M parity | ⬜ | `LIB.PRG`/`LINK.PRG` build+consume the archives; `sdk.md`/`reference-manual.md` updated; final size win recorded |

### Phase A — Design & baseline  ✅
- [x] Author this spec; answer the `libieee754d` vendoring question (§9).
- [x] Record current sample `.PRG`/`.68K` sizes (bare-object link) as the G2 yardstick — see below.
- [x] Capture a clean lockstep + Osiris/CP-M smoke baseline — **9/9 pass on both OSes**.

**Baseline captured 2026-07-20** (compiler `%TEMP%\c68k-p2\c68k.exe`, `-O0`, integrated ELF
emitter `C68K_INTEGRATED_AS=1`). These whole-`libc.o`-linked sizes are the yardstick the Phase 4
dead-stripping is measured against:

| Sample | PRG (bytes) | .68K (bytes) |
|--------|------------:|-------------:|
| bare       | 88,268 | 83,220 |
| hello      | 88,240 | 83,216 |
| printftest | 88,532 | 83,436 |
| filerw     | 88,980 | 83,728 |
| fp64conv   | 90,736 | 85,228 |
| hexdump    | 91,044 | 85,456 |

> Note the **~88 KB floor**: `bare` (a do-nothing program) is essentially the same size as the
> fuller samples because the bare `libc.o` is force-linked whole. The gap between `bare` and what
> it actually uses is the dead-strip headroom Phase 4 targets.

**Lockstep:** 9/9 cases pass on both Osiris and CP/M (`HELLO FILERW PRINTF CORETEST C99TEST
MATHTEST LIBTEST TIMETEST HEXDUMP`), exit 0.

**Baseline caveat:** the suite is green under the **integrated ELF emitter**
(`C68K_INTEGRATED_AS=1`, the primary path). The non-integrated asm68K C path currently fails at
`cc libc` (pre-existing, orthogonal to this work). The vendored `.a68` (heap/float) still
assemble via asm68K regardless; only the C→object step uses the integrated emitter.

### Phase 1 — Archive plumbing MVP  ✅
- [x] Add an `ar rcs` step (writes the symbol index) that archives the current `libc.o` into `libc.a`.
- [x] Repoint [build-prg.ps1](../tools/osiris/build-prg.ps1) **and** [build-68k.ps1](../tools/cpm/build-68k.ps1) to link `libc` via `-L<outdir> -lc`.
- [x] Prove on Osiris **and** CP/M (both use the Osiris binutils `ld`/`ar`) with zero behaviour change; lockstep green.

**Result (2026-07-21).** Lockstep **9/9 on both OSes**; every libc-using sample links
**byte-identical** to the Phase A baseline (a single referenced member ⇒ full inclusion). Member
selection is already visible: `bare.PRG` (references no libc) dropped **88,268 → 2,820 bytes** as
`-lc` pulled zero members. On CP/M the C seam (`cpm.o`) itself references libc, so `bare.68K` stays
full — extra motivation for the Phase 4 split.

### Phase 2 — Vendor float → `libm.a`  ✅
- [x] Vendored the **28** `.a68` sources (`core/ conv/ fmt/ math/`) + `ieee754.inc`/`ieee754d.inc` into `lib/libm/`; provenance recorded in the [build-libm.ps1](../tools/build-libm.ps1) header.
- [x] Build `libm.a` in-tree via [tools/build-libm.ps1](../tools/build-libm.ps1) (`asm68K` + `ar rcs` + `ranlib`) — **28 objects, 93,440 bytes** (leaner than the upstream 125,564: no `/Zi` DWARF, which the linker strips anyway).
- [x] Grep-audited and repointed **all 7** scripts off `worm68k\…\libieee754d.a` to in-tree `lib/libm/libm.a` (param defaults via `$PSScriptRoot` + doc-comment examples).
- [x] Lockstep **9/9 on both OSes** — `MATHTEST` (transcendentals), `PRINTF` (`%f`), and float cases pass against the vendored `libm.a`.

> Chose a C68k-native `build-libm.ps1` over vendoring the upstream `Makefile` (worm68k-specific tool paths); it globs the vendored sources so it stays correct as they sync.

### Phase 3 — Vendor heap → `libheap.a`  ✅
- [x] Vendored the **31** library `.a68` (`Heap*`/`GetMachine*`, minus `*Test.a68` and the harness `stub.a68`) + `heap.inc` into `lib/heap/`; provenance in `lib/heap/VENDOR.txt`.
- [x] Build `libheap.a` in-tree via [tools/build-libheap.ps1](../tools/build-libheap.ps1) (`asm68K` + `ar rcs` + `ranlib`) — **31 objects, 260,136 bytes**.
- [x] Link smoke: `ld -r --whole-archive libheap.a` + `nm -u` shows **zero undefined symbols** — the archive is fully self-contained (all core + SOA cross-refs resolve).
- [x] **Vendor-sync loop** ([tools/vendor-sync.ps1](../tools/vendor-sync.ps1)): re-pulls libm + libheap sources, prunes artifacts, writes per-file SHA-256 `VENDOR.txt` manifests. Full loop validated end-to-end: sync → rebuild → lockstep **9/9**.

> The sync pulls current upstream, so ongoing upstream fixes (e.g. the SOA hardening) land by re-running `vendor-sync.ps1` + the build scripts; `git diff lib/**/VENDOR.txt` shows exactly what changed.

### Phase 4 — Split `libc.c` → per-function `libc.a`  (goal: one linkable object per stdlib function)

End state: **every standard-library function is its own linkable object**, with shared engines
and state (the `_vformat` machinery, the `_streams[]` table, `g_heap_top`, …) kept in cohesive
"core" objects the entry points reference. Reached **incrementally**: the build globs
`libc/core/*.c`, so each carve adds files with no build change; `libc.c` is the shrinking
remainder, drained to empty and deleted at the end.

#### Phase 4a — Infrastructure + clean carves  ✅
- [x] Private header [libc/core/libc_internal.h](../libc/core/libc_internal.h) (syscall seam + soft-float externs).
- [x] [tools/build-libc.ps1](../tools/build-libc.ps1): compiles `libc/core/*.c` (c68k) → `libc.a` (`ar rcs` + `ranlib`); objects prefixed `libc__` to avoid colliding with a program object of the same basename.
- [x] Carved the cleanly-separable, contiguous sections **errno / signal / time** into their own TUs (`libc.c` 1634 → 1371 lines).
- [x] Repointed all consumers off the single `cc libc.c`: program builds ([build-prg.ps1](../tools/osiris/build-prg.ps1), [build-68k.ps1](../tools/cpm/build-68k.ps1)) and self-host CC builds ([build-cc.ps1](../tools/osiris/build-cc.ps1), [build-cc-68k.ps1](../tools/cpm/build-cc-68k.ps1)) build `libc.a` and link `-lc`; both stage3 harnesses extended to cover the new TUs.
- [x] **Lockstep 9/9** (incl. `TIMETEST`); self-host `CC.PRG` links (473 KB). **Measured drop:** Osiris `.PRG` **88,240 → 74,216** (−14 KB, ~16% — `time`+`signal` strip out); CP/M `.68K` −584 B (its seam legitimately uses `time.c`'s calendar math via `__days_from_civil`). Baseline: Phase A.
- [ ] _(follow-up)_ Manual `stage3-cc.ps1` run to confirm errno/signal/time determinism on-target.

#### Phase 4b — Decouple the hotspots  ✅
- [x] **fp64 → its own object** [fp64.c](../libc/core/fp64.c): the compiler-emitted 64-bit float-conv helpers (`fplltod`/`fpdtoull`/…) leave the stdlib mega-object. A program doing no 64-bit float conversion drops them — Osiris `.PRG` **−~2.2 KB** (hello 74,216 → 72,068); `fp64conv` +108 B (it uses them, now a separate member).
- [x] **memstream drain-hook** (`FILE.drain`, [stdio.h](../libc/include/stdio.h)): `fflush`/`fclose`/`printf` drain memstreams through a per-`FILE` function pointer instead of naming `_memstream_append`, so **stdio core no longer references `realloc`** — the mechanism that lets 4c split stdio without pulling `stdlib`. `open_memstream` installs the hook; `fopen`/`fclose` clear it (**fixes a stale-hook-on-slot-reuse bug** that would corrupt file output). The **physical** `open_memstream`/`_memstream_append` → own object lands in 4c with the stdio split (needs shared `_streams`).
- [x] Validated: **lockstep 9/9** (its `filerw` drives the file drain path) and the **self-host smoke PASSes byte-identical** (`CC.PRG` compiles on-target through its `open_memstream` assembly buffer). Total from Phase A: hello **88,240 → 72,068 (−18.3%)**.

#### Phase 4c — One object per function (goal)  ⬜
- [ ] Carve `string`/`ctype`/`stdlib`/`stdio` into per-function files; de-`static` shared helpers into `libc_internal.h` with a private prefix; keep `_vformat`/`_streams`/… as cohesive core objects. Drain and delete `libc.c`.
- [ ] Re-measure: a `printf`-only program should link stdio-core + fmt only (not `malloc`/`qsort`/`strto*`).

### Phase 5 — `malloc` on `libheap`  ⬜
- [ ] `malloc`/`free`/`realloc`/`calloc` shims over the heap; `_MachineHeapInitialize` in the crt0
      seam over `c_heapbase`/`c_heaplen`; gate behind a build switch (A/B vs bump allocator).
- [ ] `realloc(NULL,·)`/`realloc(·,0)` and `calloc` overflow handled; on-target lockstep + smoke pass.

### Phase 6 — Native self-host archives + docs + CP/M parity  ⬜
- [ ] Build `libc.a`/`libm.a`/`libheap.a` on Osiris via `LIB.PRG`; link with `LINK.PRG`.
- [ ] CP/M path parity; all three contexts green.
- [ ] Update [sdk.md](sdk.md), [reference-manual.md](reference-manual.md),
      [libc-and-toolchain.md](libc-and-toolchain.md); record final size win.

---

## 14. Open Questions

- **Q1 Soft-float ABI home.** Keep the whole IEEE-754 in `libm.a` (always-linked, member-selected)
  vs. move the arithmetic **core** into `librt68k` so float arithmetic never implies `-lm`
  (matches gcc/libgcc). *Start with the always-linked `libm.a`; refine in Phase 6 if wanted.*
- **Q2 Heap in `libc.a` vs separate `libheap.a`.** Separate keeps the engine reusable and
  unit-testable (chosen); folding into `libc.a` is more convenient for `-lc`-only users.
- **Q3 `librt68k` archive vs bare `rt68k.o`.** Archiving lets the linker drop unreferenced helpers;
  a bare object is simpler and always small. *Decide in Phase 1.*
- **Q4 Function-per-file granularity for stdio.** How finely to split without shredding the
  buffer core — settle the `stdio_core` boundary in Phase 4.
- **Q5 Vendor sync policy.** **Resolved:** [tools/vendor-sync.ps1](../tools/vendor-sync.ps1) re-pulls both upstream trees into `lib/libm` + `lib/heap`, prunes artifacts, and writes per-file SHA-256 `VENDOR.txt` manifests; drift is a `git diff` of the manifest. Rebuild (`build-libm.ps1`/`build-libheap.ps1`) + lockstep after each sync.

---

## 15. Changelog

| Date | Version | Change |
|------|---------|--------|
| 2026-07-20 | Draft v1.0 | Initial architecture & phased plan; decision to vendor both `libheap` and IEEE-754 float **sources** and stand up `libc.a`/`libm.a`. |
| 2026-07-20 | Phase A | Captured baseline: sample `.PRG`/`.68K` sizes (~88 KB floor) + lockstep **9/9 on both OSes**. Noted the integrated-emitter caveat. |
| 2026-07-21 | Phase 1 | Archive plumbing MVP: `ar rcs libc.a libc.o` + link via `-L … -lc` in both build scripts. Lockstep **9/9** both OSes; libc-using binaries byte-identical; `bare.PRG` 88,268→2,820 (member selection proven). |
| 2026-07-21 | Phase 2 | Vendored the 28 IEEE-754 `.a68` sources into `lib/libm`; added `tools/build-libm.ps1` → in-tree `libm.a` (28 objs, 93 KB); repointed all 7 scripts off the external `libieee754d.a`. Lockstep **9/9** both OSes. |
| 2026-07-21 | Phase 3 | Vendored the 31 heap library `.a68` + `heap.inc` into `lib/heap`; `tools/build-libheap.ps1` → `libheap.a` (31 objs, 260 KB); link-smoke self-contained (`nm -u` empty). Added `tools/vendor-sync.ps1` (libm+libheap re-pull + drift manifests). Lockstep **9/9**. |
| 2026-07-21 | Phase 4a | Split libc: infra (`libc_internal.h`, `tools/build-libc.ps1` → `libc.a`) + carved errno/signal/time; all program + self-host builds link `-lc`; stage3 harnesses extended. Lockstep **9/9**; Osiris `.PRG` −14 KB (~16%); `CC.PRG` links. Goal restated: **one object per stdlib function** (4b/4c). |
| 2026-07-21 | Phase 4b | Decoupled hotspots: fp64 conv helpers → `fp64.c` (own object; −~2.2 KB for non-fp64 programs); memstream `FILE.drain` hook so stdio core no longer references `realloc` (+ stale-hook slot-reuse fix). Lockstep **9/9**; self-host smoke byte-identical. hello −18.3% vs baseline. |
