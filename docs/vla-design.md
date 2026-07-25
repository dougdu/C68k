# Variable-Length Arrays (VLAs) — design & implementation spec

> Project: **C68k** (chibicc-derived C compiler for the MC68000, targeting Osiris and CP/M-68K)
> Status: **implemented (2026-07-25)** — `tests/lockstep/vlatest.c` 11/11 on Osiris and CP/M-68K
> Companion: [reference-manual.md §2.3](reference-manual.md) (current exclusion),
> [implementation-plan.md — P6](implementation-plan.md) (C99 language completeness),
> [architecture.md §7](architecture.md) (the m68k C ABI / frame model).

---

## 1. Purpose

Close the one *mandatory* C99 language feature C68k currently rejects: **variable-length
arrays** (and the variably-modified types that ride on them). Today the front end fully parses
VLAs and lowers `int x[n]` to `x = alloca(size)`, but the m68k code generator emits a hard
diagnostic (`"variable-length arrays are not supported by c68k"`) at the `ND_VLA_PTR` lvalue
([src/codegen68k.c](../src/codegen68k.c)). This spec finishes the code generator and adds
**standard-conforming, block-scoped** reclamation so VLAs in loops do not leak the stack.

Non-goal: making VLAs *safe against untrusted sizes* — like every C implementation, an
oversized VLA overflows the stack. §8 adds an optional debug guard but the default remains the
standard "it's on you" behaviour.

---

## 2. Background — what already exists

The chibicc front end (retained in C68k) does the hard parsing/typing work:

- **Types:** `TY_VLA` carries `vla_len` (the runtime element-count expression) and `vla_size`
  (a hidden `unsigned long` local holding the total byte size). Nested VLAs (`int a[n][m]`)
  chain their `vla_size` computations. See [src/type.c](../src/type.c) `vla_of()`.
- **Size computation:** `compute_vla_size()` ([src/parse.c](../src/parse.c)) emits code that
  evaluates the byte size into the `vla_size` local at the point of declaration.
- **Lowering:** a VLA declaration `int x[n]` is rewritten to `x = alloca(vla_size)` via
  `new_alloca()` → an `ND_FUNCALL` to the `builtin_alloca` `Obj`. The VLA *variable* `x` holds
  a **pointer** to the allocated data (its slot is a fixed 4-byte local); the data lives in the
  `alloca` region.
- **`sizeof`:** `sizeof(vla)` lowers to a load of the `vla_size` local (a runtime value).
- **Constraints:** the parser already rejects `static`/file-scope VLAs and jumps *into* a VLA
  scope (both C99 constraint violations).

So the remaining work is **entirely in the code generator**, plus a small parser hook to record
per-block reclamation markers and a lockstep test.

---

## 3. Design goals

1. **Correct block-scoped lifetime** (C99 §6.2.4p7): a VLA is reclaimed when execution leaves the
   block that declares it — *not* at function return. In particular, a VLA in a loop body must be
   reclaimed each iteration (no per-iteration leak).
2. **ABI-clean**: no violation of the callee-saved register contract (A2–A7 preserved for the
   caller); no reliance on registers the future register allocator will want.
3. **Zero cost where unused**: functions/blocks without VLAs emit byte-identical code to today.
4. **Cross-OS**: identical behaviour on Osiris and CP/M-68K (VLAs are pure stack/codegen; no OS
   seam involved).
5. **Composes** with the existing pure-SP stack machine, the `setjmp` spill logic, struct
   return, and variadics.

---

## 4. The m68k frame model (recap)

- Prologue `link a6,#-stack_size` establishes the A6 frame; locals are A6-relative; epilogue
  `unlk a6` (which does `SP = A6; A6 = (SP)+`) discards the *entire* frame — fixed locals **and**
  any `alloca`'d VLA space — in one instruction.
- Expressions evaluate on the **SP stack** (`-(SP)`/`(SP)+`), tracked by `depth`. At every
  **statement boundary** `depth == 0`, so `SP` sits at the block's current base (frame base minus
  the VLAs alloca'd so far in the enclosing scopes).
- `SP` must stay **even** (word-aligned); we keep it 4-aligned.

Consequence used throughout: **`unlk a6` reclaims all VLAs on `return` for free** — reclamation
work is only needed on the *in-function* exit edges of a VLA block.

---

## 5. Code generator changes (`src/codegen68k.c`)

### 5.1 Inline `alloca`

Special-case an `ND_FUNCALL` whose callee is the `builtin_alloca` `Obj` (recognise by identity,
not name). Instead of a real call:

```
    ; size expr already evaluated into D0
    addq.l  #3,d0              ; round the size up to a multiple of 4 …
    andi.l  #-4,d0             ; … keeping SP 4-aligned (and even)
    suba.l  d0,sp              ; grow the stack downward
    move.l  sp,d0              ; return the new SP as the VLA data pointer
```

Notes:
- `depth` is **not** adjusted — the `alloca` SP move is a scope-lifetime move, not an eval-stack
  push/pop, and it always executes at `depth == 0` (a declaration statement).
- A size of 0 rounds to 0 (a valid, zero-length allocation returning the current SP).

### 5.2 Address of a VLA variable (`gen_addr`, `ND_VAR` with `ty->kind == TY_VLA`)

A VLA variable's "address" for indexing is the **runtime data pointer stored in its slot**, so
**load** it rather than taking the slot address:

```
    move.l  offset(a6),d0      ; D0 = the stored VLA data pointer
```

(Contrast an ordinary local, whose address is `lea offset(a6)` → D0.)

### 5.3 `ND_VLA_PTR` (`gen_addr`) — replace the error

`ND_VLA_PTR` is the **lvalue of the `x = alloca(...)` assignment** — i.e. the VLA variable's slot
(where the pointer is stored). Emit the slot's effective address into D0 exactly like an ordinary
A6-relative local:

```
    lea     offset(a6),a0
    move.l  a0,d0
```

Remove the `error_tok(...)`.

### 5.4 `sizeof` a VLA

Already lowered by the front end to a load of the `vla_size` local — verify it flows through the
normal `ND_VAR` integer-load path (no new codegen expected).

### 5.5 `load()` for `TY_VLA`

Already returns the address (aggregate convention) — keep.

---

## 6. Block-scoped reclamation (the lifetime fix)

Each block that declares ≥1 VLA gets **one** hidden reclamation marker; a single marker reclaims
**all** VLAs of that block (restoring SP to the block-entry level discards every `alloca` done
after it). Nested VLA blocks get their own markers; the number of marker slots equals the
maximum VLA-block nesting depth (a first cut may allocate one lexical marker per VLA-block;
sibling reuse is a later optimisation).

### 6.1 Parser hook

- Add `Obj *vla_mark;` to `Node` (used on `ND_BLOCK`).
- While parsing a compound statement, if the block lowers ≥1 VLA declaration, allocate a hidden
  `void *` local and store it in `node->vla_mark`. (Track "this block declared a VLA" with a flag
  set in the VLA-lowering path.)

### 6.2 Codegen — marker discipline

Maintain a codegen-side stack of the currently-open VLA-block markers (mirrors lexical nesting).

- **`ND_BLOCK` with `vla_mark`:**
  ```
      move.l  sp, mark(a6)     ; save the block-entry SP  (on entry)
      … body …
      move.l  mark(a6), sp     ; reclaim on normal fall-through exit
  ```
  Push the marker while generating the body; pop it after.
- **`return`:** no SP restore — the epilogue `unlk a6` reclaims all VLAs. (Just fall to the
  epilogue as today.)
- **`break` / `continue`:** these jump out of / around the loop/switch body. Record, per
  loop/switch, the **SP-reclaim target at loop entry** (the marker on top of the stack when the
  loop began, or the fixed frame base if none). Before the `break`/`continue` jump, restore SP to
  that target so every VLA opened inside the loop is dropped:
  ```
      move.l  target(a6), sp   ; (or: lea -stack_size(a6),sp  when the target is the frame base)
      bra     .Lbreak_N / .Lcont_N
  ```
- **`goto L`:** restore SP to label `L`'s scope marker (the innermost VLA marker still live at
  `L`) before the jump. Labels record their enclosing VLA-scope depth. A `goto` that stays within
  the same VLA scope needs no restore.

Frame-base restore (`target` is "no VLA active"): SP at the frame base is statically
`A6 − stack_size`, emitted as `lea -stack_size(a6),sp` for frames < 32 KB; larger frames keep an
explicit base marker saved right after the prologue.

### 6.3 Why a memory-slot marker (not a second `LINK`/`UNLK`)

Using a spare address register with `link a5,#0` / `unlk a5` also reclaims, but A5 is
callee-saved: a `link a5` that is left via `return` (which reclaims SP via `unlk a6` but does
**not** restore the A5 *register*) would violate the ABI unless A5 is additionally saved/restored
in the prologue/epilogue. The A6-relative **memory** marker has no register to preserve, nests
naturally (one slot per block), and makes `return` completely free. Chosen for those reasons.

---

## 7. Interactions to verify

- **`setjmp` spill:** `SETJMP_SPILL_SLOTS` are A6-relative — unaffected by SP moves. A VLA
  alloc'd *after* a `setjmp` is reclaimed correctly on `longjmp` (which restores SP to the
  `setjmp` SP). VLA scope crossing a `setjmp`/`longjmp` pair is UB per the standard.
- **Struct return hidden pointer:** passed as the leftmost arg (caller side) and referenced
  A6-relative — unaffected.
- **Function calls after an alloca:** args push below the VLA, callee runs, caller cleans up;
  the VLA region is untouched (pushes are always *below* current SP).
- **Variadics:** `__va_area__` is A6-relative — unaffected.
- **Even SP:** guaranteed by rounding every alloca size to a multiple of 4 (§5.1).
- **`-O1` peephole pass:** the new fixed instruction sequences must survive the peephole (add a
  guard/test so it does not fold an `suba.l d0,sp` incorrectly).

---

## 8. Optional safety guard (debug builds)

Default behaviour is standard (no size check). Optionally, under a debug flag, emit a check that
the requested size does not cross a low-stack limit and trap/abort if it would — valuable on the
1 MB Osiris model (≈16 KB task stack, `STKSIZE $4000`), where an input-sized VLA can overflow.
Deferred; noted so the hook point (right after §5.1's size computation) is reserved.

---

## 9. Edge cases

- **Multiple VLAs per block:** one marker reclaims all (§6).
- **Nested / 2-D VLAs (`int a[n][m]`):** front end computes the nested `vla_size`; codegen does
  the pointer load (§5.2) and the existing `__mulsi3` index arithmetic. No special handling.
- **VLA function parameters (`void f(int n, int a[n])`):** the parameter decays to a pointer;
  the `[n]` is evaluated for its side effects/`sizeof` but the object is a pointer — normal
  parameter codegen. Verify `sizeof(a)` inside `f` uses the parameter's `vla_size`.
- **VLA in a loop:** reclaimed each iteration via the loop-body block marker (§6.2) → no leak.
- **Sibling VLA blocks:** independent markers (or shared slots later); lifetimes don't overlap.
- **`goto`/`switch` across VLA scopes:** SP restored to the destination scope (§6.2); jumping
  *into* a VLA scope stays a front-end constraint error.

---

## 10. Test plan — `tests/lockstep/vlatest.c`

A cross-OS lockstep case, `VLATEST`, printing `VLATEST PASS n/n`, wired into
[tools/run-lockstep.ps1](../tools/run-lockstep.ps1). Cases:

1. **Basic 1-D:** `int a[n]` — fill `a[i]=i`, sum, check.
2. **Runtime `sizeof`:** `sizeof(a) == (size_t)n * sizeof(int)`.
3. **VLA parameter:** `int sum(int n, int a[n])` returns the element sum; `sizeof(a)` inside is
   the pointer size (decayed) — assert the decay behaviour.
4. **2-D VLA:** `int m[r][c]`; write `m[i][j]=i*c+j`, read back.
5. **Loop reclamation (the leak test):** a tight loop (e.g. 100 000 iterations) each declaring a
   fresh VLA sized from the iteration; accumulate and check. A leak would overflow the ≈16 KB
   Osiris stack long before completion, so *completing with the right sum* proves reclamation.
6. **`break`/`continue`** out of a VLA loop body (SP restored on both edges).
7. **Nested VLA blocks** — inner reclaimed while outer stays live.
8. **`return` from inside a VLA scope** — result correct, no corruption.

Validate: `VLATEST` on Osiris (1 MB/68008) and CP/M-68K (16 MB), then the full lockstep green on
both OSes with no regression (especially `SETJMP`, `IOTEST`, `MEMTEST`).

---

## 11. Rollout / docs on completion

- Delete the `ND_VLA_PTR` error; land the codegen + parser hook + test.
- Flip the docs: [reference-manual.md §2.3](reference-manual.md) (remove the VLA exclusion, keep
  the stack-cost caveat), [implementation-plan.md P6](implementation-plan.md) (VLA item from
  "documented exclusion" to supported), and the compiler note in
  [c99-conformance.md](c99-conformance.md). Optionally emit `-D` docs for the debug guard (§8).
- With VLAs done, the remaining substantive C99 language gaps are `_Complex`/`<tgmath.h>` and the
  `<fenv.h>` FP-exception semantics (the latter arriving with directed rounding).

---

## 12. Implementation checklist

- [x] `Node.vla_mark` field + parser records a marker lvar for VLA-declaring blocks.
- [x] Codegen: inline `alloca` (§5.1).
- [x] Codegen: `gen_addr` VLA-var pointer load (§5.2) and `ND_VLA_PTR` slot address (§5.3).
- [x] Codegen: block marker save/restore + loop break/continue SP reclaim (§6.2). _(goto/switch
      across VLA scopes self-correct at the enclosing block exit; not separately reclaimed.)_
- [x] Verify `sizeof`, 2-D (§5.4, §9). _A VLA parameter whose size names an earlier parameter is a
      front-end limitation (params are not scoped during parameter parsing) — write `int *a`._
- [x] `tests/lockstep/vlatest.c` + run-lockstep wiring; VLATEST 11/11 on both OSes.
- [x] Regression: libc.a byte-identical (573822 B), SETJMP 6/6 — non-VLA codegen unchanged; docs + commit.
