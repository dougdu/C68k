# BUG: `-O2` self-host miscompiles `.data` — a libheap SOA slab aliases `assemble_to_elf`'s `data.data` buffer

- **Status:** **RESOLVED (2026-08-02)** — root cause found + fixed; see [Resolution](#resolution-2026-08-02).
- **Severity:** High — blocked `-O2` (and `-O1`) self-host; latent cross-heap corruption
- **Component:** `lib/heap` (Small Object Allocator) and/or the block allocator, exercised by `src/emit_elf.c`
- **First seen:** P12/OP1 self-host bring-up. **Confirmed:** 2026-07-31.
- **Reproduces on:** Osiris (`c68k-sim68k`). **Not yet observed on:** CP/M-68K — but this is a
  *layout-sensitive* corruption (§8), so "not observed" is **not** "not present". Do not assume CP/M is safe.

---

## Resolution (2026-08-02)

**Root cause — a stale cross-heap `SLAB` magic left behind by `HeapDestroy`.** Caught live with a
non-perturbing external observer: a single `ILLEGAL` baked at `_start` as a resumable breakpoint
under `sim68k`'s `!ex catch illegal break`, plus symbol-biased `sid68k` write-watchpoints. That
broke *inside* the running stripped `CC.PRG` without an in-image probe (which had always moved the
Heisenbug, §8).

The sequence (machine heap = ID 0; the cc1 scratch sub-heap = ID 1):

1. `cc1` builds class-24 SOA slabs in the **scratch** sub-heap (interning symbol/string names).
2. `__heap_release` → `_HeapDestroy` zeroes **only the heap header**, then `_HeapFree`s the ~15 MB
   scratch block **unzeroed** — leaving every scratch slab tile's `SLAB` magic (plus its free-list
   `F2EE` cell markers and `AREN` descriptors) intact in the freed bytes.
3. `assemble_to_elf` re-carves `data.data`'s block from that reused region — landing it *inside* a
   stale 4 KB tile (observed: `data.data = 0x17a080`, block header `0x17a060`, both inside stale
   tile `0x17a000`, whose slab header still reads `SLAB`, class 2, `cellSz` 24, **heap id 1**).
4. `__HeapSlabFromPtr(data.data)` masks to the tile base, finds the intact `SLAB` magic + a valid
   arena + an in-range, cell-aligned index, and **mis-recognizes the plain block pointer as an SOA
   cell** of the destroyed heap.

This one defect produces *every* observed symptom: the `F2EE`-in-`.data` scribble (residual scratch
free-list markers surviving under `data.data`'s bytes); a `512 → 1024` `realloc` returning **NULL**
(`_HeapReAlloc` recognizes the phantom "cell", then rejects it because its `slId` 1 ≠ the machine
heap id 0 → `INVALID_HEAP`, so `buf_need` then `malloc`s a fresh 2048 buffer and drops `[0,512)`);
"passes iff the scratch arena is kept alive" (no destroy → no reuse → no stale tile); the size/layout
sensitivity (only a `.data` large enough to grow into an overlapping stale tile — `unicode`,
`preprocess`); and host-ASAN blindness (ASAN instruments MSVC `malloc`, never libheap's SOA).

**Answers to the §12 open questions:** (1) `data.data`'s block is genuinely **allocated** (id 0) —
not a use-after-free of *that* block; the collision is with a **stale, no-longer-owned** SOA tile
from the destroyed id-1 heap. (2) There is no *live* arena overlap — the overlapping `SLAB`/`AREN`
structures are **dead metadata** left in freed memory (`__HeapSlabFromPtr` has no cross-heap /
liveness check beyond the magic). (3) The defect is platform-independent (any sub-heap destroy with
live SOA slabs); CP/M merely never hit an overlapping layout.

**The fix — `lib/heap/HeapDestroy.a68` (`_HeapDestroy`).** Before the header-zero + `_HeapFree`, walk
the sub-heap's SOA arena list and `clr.l` each slab tile's `slMagic` (and each arena's `arMagic`),
mirroring what `__HeapSlabRelease` already does per tile on release. It must run **before** the
header zero, which wipes `hhSmallPool` (the SCB pointer). After the fix the freed bytes carry no
recognizable SOA magic, so `__HeapSlabFromPtr` cannot mis-recognize a reused block as a stale cell.

```asm
        ; ... after validating the heap to destroy, BEFORE the header-zero loop ...
        IF SOA_ROUTE
        movea.l d1,a0                   ; heap header
        move.l  hhSmallPool(a0),d0       ; SCB (0 if the heap never used the SOA)
        beq.s   _hdNoSoa
        movea.l d0,a0                    ; A0 --> SCB
        movea.l scbArenaHead(a0),a2      ; A2 --> first arena
_hdArenaLoop:
        cmpa.l  #0,a2
        beq.s   _hdNoSoa
        clr.l   arMagic(a2)              ; invalidate the arena descriptor
        moveq   #0,d3
        move.w  arSlabs(a2),d3           ; tile count
        andi.l  #$0000FFFF,d3
        movea.l arBase(a2),a3            ; A3 --> tile 0
        bra.s   _hdTileTest
_hdTileLoop:
        clr.l   slMagic(a3)              ; invalidate the slab tile header
        adda.l  #SLAB_SIZE,a3
        subq.l  #1,d3
_hdTileTest:
        tst.l   d3
        bne.s   _hdTileLoop
        movea.l arNext(a2),a2
        bra.s   _hdArenaLoop
_hdNoSoa:
        ENDIF
```

**Verification.**
- Upstream worm68k heap test suite (`run-heap-tests.ps1`): **9/9 pass**, including `test_soa`.
- `-O2` self-host, full `stage3-cc.ps1`: **13/13 byte-identical** (`unicode` and `preprocess`, the
  two that failed at `byte@3504`, now `PASS`, along with all other TUs).

**Provenance.** `lib/heap` is vendored from worm68k (`lib/heap/VENDOR.txt`). Fixed upstream first
(worm68k commit `8e75cf50`), then re-vendored via `tools/vendor-sync.ps1`.

The pre-resolution analysis (§1–§12) is retained below as the investigation record; §9's "needs a
runtime store-catch to localize" was carried out and is what produced this resolution.

---

## 1. Summary

When the self-hosted compiler `CC.PRG` (built at `-O1` or `-O2`) recompiles its own `unicode.c`
(or `preprocess.c`) on-target, the emitted object's `.data` section is corrupted: a run of
**libheap SOA class-24 free-list markers (`$F2EE`)** is written *into* the integrated assembler's
`data.data` output buffer. The object is the right size and its `.text` is byte-perfect; only
`.data` content is wrong. A live SOA slab tile and `emit_elf`'s `data.data` heap block occupy the
**same bytes** at the same time (a double-live / double-hand-out window).

The compiler does **not** crash — it produces a well-formed but wrong `UNICODE.O`.

## 2. Impact

- `-O1`/`-O2` self-host is not byte-identical (the stage2 == stage3 oracle fails for `unicode`,
  `preprocess`).
- Because the trigger is a heap-layout coincidence, the *same latent defect* could corrupt any
  allocation on any platform; it simply has not manifested detectably elsewhere yet.

## 3. Affected configuration

| Factor | Triggers bug | Notes |
|---|---|---|
| `CC.PRG` optimization | `-O1`, `-O2` | `-O0` `CC.PRG` passes |
| Translation unit compiled on-target | `unicode.c`, `preprocess.c` | Largest `.data` tables + most small-alloc churn |
| Platform | Osiris | CP/M-68K not yet observed (but suspect — see §8) |
| `emit_elf.o` opt level (per-TU) | `-O1`/`-O2` | `-O0` `emit_elf.o` passes → trigger is an OP0/OP1 alloc-pattern change, **not** a codegen miscompile |

## 4. Build the failing compiler (host, Windows/pwsh)

```powershell
# Host cross-compiler is assumed already built at %TEMP%\c68k-p2\c68k.exe
# Build the on-target compiler at -O2 (THE failing configuration):
$env:C68K_OPT = '2'
& 'C:\git\C68k\tools\osiris\build-cc.ps1'
Remove-Item Env:\C68K_OPT
# Output: %TEMP%\c68k-cc\CC.PRG  (stripped, ~399,600 bytes) -- this is the image that fails.
```

For symbols during a live debug session, also produce an **unstripped** twin with identical VMAs
(same objects, drop `-s`) — see §10.

## 5. Reproduce — automated (host harness)

```powershell
& 'C:\git\C68k\tools\osiris\stage3-cc.ps1' -Tu unicode -KeepArtifacts
```

Expected failing output:

```
unicode     UNICODE.C  ref=  9104 got=  9104  FAIL(byte@3504)
```

Artifacts are left in `%TEMP%\c68k-stage3\`:
- `UNICODE.O` — the corrupt on-target object (9104 bytes)
- `REF_UNICODE.O` — the host reference object (9104 bytes, correct)
- `con.log` — the on-target console session

## 6. Reproduce — manual, on a live Osiris console (for the watchpoint session)

**6a. Host prep — produce the self-contained input and the reference object:**

```powershell
$cc  = "$env:TEMP\c68k-p2\c68k.exe"
$repo = 'C:\git\C68k'
# Preprocess unicode.c to a single self-contained file (no headers/-I needed on target):
& $cc -E -DC68K_SELFHOST "-I$repo\include" "-I$repo\libc\include" "-I$repo\src" `
      "$repo\src\unicode.c" | Set-Content "$env:TEMP\UNICODE.C" -Encoding ASCII
# Host reference object (what the on-target output MUST equal, byte for byte):
& $cc -fintegrated-as -c "$env:TEMP\UNICODE.C" -o "$env:TEMP\REF_UNICODE.O"
```

**6b. Stage on an Osiris-visible medium** (mirror the harness layout):
- Boot volume `A:` — `CC.PRG` (the `-O2` build from §4).
- Data volume `B:` — `UNICODE.C` (from 6a). `B:` must have room for CC's large scratch `.s`
  (~a few hundred KB) plus the 9 KB output; the harness uses a blank 1.44 MB floppy for this.

**6c. On-target, from the `B:` prompt:**

```
B>A:CC -c UNICODE.C -o UNICODE.O
```

**6d. Verify the corruption.** Read `UNICODE.O` back to the host and compare to `REF_UNICODE.O`:
- The failure is **silent**: the compile succeeds and `UNICODE.O` is always **9104 bytes** whether
  correct or corrupt — only `.data` *content* differs, so exit-code/size checks give a false pass.
- First differing byte is at **object offset 3504** (`0xDB0`) — the start of the `.data` section.
- `UNICODE.O` contains **48 occurrences of the two-byte pattern `F2 EE`** inside `.data`
  (first at object offset **3506** = `0xDB2`, through ~object `0x11AA`); `REF_UNICODE.O` contains **zero**.

## 7. Corruption signature (byte level)

- Both objects are **9104 bytes**; section table identical; `.text` (obj `52`..`3501`) byte-identical.
- `.data`: object offset `3504`, size `2035` (== runtime `data.data.len` `0x7F3`). `cap` is `2048`,
  so `data.data` is a **block-allocator** allocation (`> SOA_MAX` 256), *not* an SOA cell, at the
  time of corruption.
- The corrupt bytes are an SOA **free-cell control word** repeated: `[nextIdx:BE16][F2EE:BE16]`,
  **predominant stride 24** = class-24 (`slCellSz` 24). The first control word is at object
  `3512` = `data.data + 8`.
- `nextIdx` values are **scrambled** (`0x06, 0x1C, 0x1D, 0x21, 0x22, 0x23, …`), i.e. the *accumulated*
  free list of a **used** class-24 slab (not a fresh `1,2,3,…` init), interleaved with surviving
  real `.data` bytes → a **live** class-24 slab was actively alloc/free'd while its cells physically
  coincided with `data.data`.

`$F2EE` is `SOA_FREE_TAG` (`lib/heap/heap.inc`); the pattern is unambiguously libheap's, not codegen's.

## 8. Determinism & layout sensitivity (Heisenbug)

- **Deterministic:** repeated stage3 runs produce a **byte-identical** 9104-byte corrupt `UNICODE.O`,
  same offset — so the runtime heap addresses are stable for a given `CC.PRG`. A watchpoint on the
  victim address is therefore viable.
- **Layout sensitive:** *any* code change to the running image (an added `fprintf`, a `DebugBreak`,
  an in-libheap assertion, a `malloc.c` probe, or a one-instruction bisect edit) **moves or hides**
  the corruption. Every in-image instrument attempted so far perturbs it away. **Only a
  non-perturbing, external observer will catch it.** This is why we need the live watchpoint.

## 9. Root-cause analysis to date

- **Not codegen.** `CC.PRG`'s intermediate assembly for `unicode.c` is byte-identical to the host
  `-S` reference; host ASAN (x64 **and** ILP32) on `emit_elf.c` is clean. The bug is in the
  m68k **runtime**, not the generated code.
- **It is the SOA.** Setting `SOA_ROUTE = 0` (`heap.inc`, bypass the small-object allocator) → PASS.
- **It needs the machine heap after the scratch arena is freed.** `src/main.c` brackets cc1 with
  `__heap_mark`/`__heap_release`; `__heap_release` `HeapDestroy`s the ~15 MB scratch sub-heap,
  coalescing it into the machine heap, and `assemble_to_elf` then carves `data.data` + class-24 SOA
  arenas from it. **Keeping the arena alive** (skip `__heap_release`) → PASS.
- **All static audits are clean.** Full reads of the SOA (alloc / free / slab create+release /
  arena create+release / class-list / realloc / compact) and the block allocator
  (alloc / split / free / coalesce / unlink / create / destroy) plus the libc arena routing
  (`heap_arena.c`, `owner_heap`) found **no defect**; every invariant holds. In particular, with the
  arena never freed, a reused tile is always inside a live arena block and cannot alias a foreign
  block — so the observed overlap requires a **block-level double-hand-out** that no audited routine
  produces. The earlier `_sofEmptyChk`/`_slrCheck` "bisect culprit" was a layout artifact.
- **Conclusion:** the corruption is an emergent, layout-triggered double-live between a class-24 SOA
  slab tile and the `data.data` block, not a statically visible defect in any single routine. It
  needs a runtime store-catch to localize.

## 10. Live watchpoint plan (what to arm, and when)

Goal: catch the **store** that writes `F2EE` into `data.data`, with its PC and the SOA state at that
moment. Run the **stripped** `CC.PRG` (the failing image); use an unstripped twin only as a symbol
sidecar (its VMAs match — `-s` omits only `.symtab`, not section placement).

**Symbols for your build** (do not trust hard-coded addresses across rebuilds):

```powershell
# Relink the SAME objects without -s to get CC_SYM.PRG (identical VMAs), then:
& 'C:\git\C68k\tools\prgsym\prgsym.exe' extract '<CC_SYM.PRG>' -o '<CC.SYM>'
# or: m68k-elf-nm CC_SYM.PRG
```

**Runtime base:** break at `_start`; `base = PC - <link addr of _start>`. Any symbol's runtime
address = `base + <link addr>`.

**Locate the victim (`data.data` buffer):** `emit_elf`'s `static Buf data` is the symbol `_data`
(layout `struct { char *data; int len; int cap; }`). The buffer pointer is the first word:

```
data.data = *(uint32*)(base + <link addr of _data>)
```

`data.data` is realloc'd 256 → 512 → 1024 → 2048; the **final 2048-cap buffer** is the victim.
Two-pass arm:

1. Break at `_assemble_to_elf`. Set a **write watchpoint on the 4-byte pointer** at
   `base + _data` (the `Buf.data` field). Continue; each hit is a realloc reassignment. Note the
   address written on the **last** hit (when `cap` reaches 2048). *(Alternatively: break just after
   the `resolve_fixups()` call — by then `data.data` is at its final 2048 buffer and stable.)*
2. Set a **data write watchpoint over `[data.data, data.data + data.len)`** (2035 bytes), or just the
   first control word at `data.data + 8`, and continue. The next hit that writes an `F2EE` low half
   is the culprit store.

**Expected culprit PCs** (whichever fires — capture the PC and decode):
- `__HeapSoaFree` push (`[oldHead|F2EE]` into one cell per free) — matches the *scrambled* nextIdx
  signature; most likely.
- `__HeapSlabCreate` free-list writer (`$_slcFL`, `[i+1|F2EE]` per cell of a fresh slab).

**What to capture at the hit** (this is what pins the root cause):
- PC (→ routine via symbols).
- The **slab** being written (`pMem & ~0xFFF` = tile base): its `slMagic`, `slClass`, `slCellSz`,
  `slArena`, and the owning **arena**'s `arBase`, `arSlabs`, `arSlabUse`, `arTileMap`, `arBlock`.
- The **machine-heap block** that contains `data.data` (walk `hhHeapHead` or use `!heap`): confirm
  whether the class-24 tile's `[tile, tile+0x1000)` overlaps `data.data`'s block `[hdr, hdr+bhSize)`,
  and whether **both are simultaneously "live"** (arena block allocated AND `data.data` block
  allocated) → the double-hand-out, or whether one is on the free list → a use-after-free.

**Confirmation levers** (each PASSES, so use only to sanity-check you're on the right bug — each
shifts layout, so don't debug *in* these states):
- `heap.inc`: `SOA_ROUTE equ 0` → PASS.
- `src/main.c`: comment out the `__heap_release(...)` call → PASS.
- Build only `emit_elf.o` at `-O0` (rest `-O2`), relink → PASS.

## 11. Reference link addresses (build-specific — verify against YOUR `nm`)

From a prior `-O2` `CC_SYM.PRG` (PIE, pre-relocation). **Illustrative only**; recompute for your build:

| Symbol | Link addr | Meaning |
|---|---|---|
| `_start` | `0x14c18` | base = runtime PC − this |
| `_assemble_to_elf` | `0x4373c` | break here (once per compile) |
| `_data` | `0x61998` | `&data` (Buf struct); `data.data = *(long*)(base+0x61998)` |
| `_rodata` / `_text` | `0x6198c` / `0x619a4` | sibling Bufs |
| `__HeapSlabCreate` | `0x5850c` | free-list writer at `+0xEE` (`$_slcFL` `0x585fa`) |

In one prior (perturbed) run the load base was ~`0x108000` and the machine heap ~`0x16A000`, giving
`&data.data` ~`0x169998`; treat these as ballpark only and derive exactly from `_start` at runtime.

## 12. Open questions for the live session

1. At the corrupting store, is `data.data`'s block still **allocated** (double-hand-out) or has it
   been **freed** (use-after-free)? This is the fork the static audit could not resolve.
2. Which allocation created the class-24 arena that overlaps `data.data`, and from which free block
   was it carved? (Capture the arena's `arBlock` and walk back the block chain.)
3. Does the same store fire on CP/M-68K with a class-24-heavy input (to settle whether CP/M is
   merely masking the same defect)?
