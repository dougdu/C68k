# `long long` ↔ `float`/`double` soft-float conversions — ieee754 ABI spec

> **Status (2026‑07‑26): IMPLEMENTED — this is the live design.** The eight
> routines are provided by worm68k ieee754 [`conv/llconv.a68`](../lib/libm/conv/llconv.a68),
> vendored into `lib/libm` and linked by c68k; the earlier C fallback
> `libc/core/fp64.c` was removed as the duplicate. One correction to the premise
> below: they were never truly "missing" — the `A2006` was a missing
> *compiler‑emitted `EXTERN`* for the codegen call (fixed by the `##`
> inline‑external refactor), not an absent routine. The ABI spec stands.

**Purpose.** Eight conversion routines that the C68k compiler already emits calls
to, but the vendored IEEE-754 float library does not yet provide. Implement them
in **worm68k `ieee754/conv/`** — the home of the existing 32-bit converters
(`ftol.a68`, `ltof.a68`, `dpconv.a68`) — then re-sync into C68k `lib/libm/`.

**Why they're needed.** [codegen68k.c](../src/codegen68k.c) `cast()` emits a `jsr`
to one of these for every C cast between a 64-bit integer (`long long` /
`unsigned long long`) and `float`/`double`. They are the exact 64-bit siblings of
the existing 32-bit `_fpltod`/`_fpdtol`/`_fpltof`/`_fpftol`. Because they are
missing, any such cast currently **fails to link** (integrated-as path) or throws
**`A2006 undefined symbol`** (asm68K path — first seen assembling `llrint.c`).

---

## 1. The eight functions — exact `public` symbol names

Declare each as `public _fp…` in the `conv/` sources. **Do not vary the spelling**
— the compiler emits these literal strings; a mismatch reintroduces the link/A2006
failure.

| `public` symbol | converts | arg (stack) | returns |
|---|---|---|---|
| `_fplltod`  | signed `long long` → `double` | 8 bytes | `double` in `D0:D1` |
| `_fpulltod` | unsigned `long long` → `double` | 8 bytes | `double` in `D0:D1` |
| `_fplltof`  | signed `long long` → `float` | 8 bytes | `float` in `D0` |
| `_fpulltof` | unsigned `long long` → `float` | 8 bytes | `float` in `D0` |
| `_fpdtoll`  | `double` → signed `long long` | 8 bytes | `long long` in `D0:D1` |
| `_fpdtoull` | `double` → unsigned `long long` | 8 bytes | `unsigned long long` in `D0:D1` |
| `_fpftoll`  | `float` → signed `long long` | 4 bytes | `long long` in `D0:D1` |
| `_fpftoull` | `float` → unsigned `long long` | 4 bytes | `unsigned long long` in `D0:D1` |

Source of truth: `cast()` in [codegen68k.c](../src/codegen68k.c#L420) —
`fn = to->is_unsigned ? "_fpdtoull" : "_fpdtoll"` etc.

---

## 2. Calling convention — **MANDATORY** (must match the compiler byte-for-byte)

The compiler emits, via `cast_call(argsz, fn)`:

```asm
; argsz == 8  (push64):          ; argsz == 4 (push):
    move.l  d1,-(sp)             ;     move.l  d0,-(sp)
    move.l  d0,-(sp)             ;
    jsr     _fpXXX               ;     jsr     _fpXXX
    adda.w  #8,sp                ;     adda.w  #4,sp        ; <-- CALLER cleans
```

* **Arguments are on the stack, above the return address. The callee must NOT
  pop them — end with a plain `rts`.** The caller removes them (`adda.w #argsz,sp`).
* **Stack layout at entry** (after the `jsr`, so the return address is at `0(sp)`):
  * **8-byte arg** (`double` or `long long`): `4(sp)` = **HIGH** longword,
    `8(sp)` = **LOW** longword. Big-endian: the high half is at the lower address.
    For a signed `long long` the sign is bit 31 of the high longword (= value bit 63).
  * **4-byte arg** (`float`): `4(sp)` = the 32-bit value.
* **Registers also hold the argument on entry.** `push`/`push64` copy `D0`(`:D1`)
  to the stack *without clobbering them*, so on entry the arg is **also** live in
  `D0` (4-byte) or `D0`=high / `D1`=low (8-byte). Read from the stack or the
  registers — both are valid. (Existing routines mix styles: `_fpftod` reads
  `4(sp)`, `_fpdtof` reads `D0:D1`.)
* **Result registers:**
  * 4-byte result (`float`): **`D0`**.
  * 8-byte result (`double` or `long long`): **`D0` = high longword, `D1` = low
    longword** (same big-endian high/low order as the argument).
* **Register preservation (this codebase's ABI):** `D0`/`D1`/`A0`/`A1` are
  **scratch** (caller-saved) — clobber freely. **`D2`–`D7` and `A2`–`A6` MUST be
  preserved** — `movem.l` them at entry and restore before `rts`, exactly as every
  existing `conv/` routine does. No `A6` frame is required.

That register/stack contract is the hard requirement: get it right and the
compiler links unchanged.

---

## 3. Numeric semantics — match the existing family + `<fenv.h>`

Integrate with the library's IEEE state the same way `ftol.a68` / `ltof.a68` /
`dpconv.a68` do (`_fe_status` sticky flags; `_fe_round` / `_fe_dirup` directed
rounding). This is what keeps the lockstep math suite and `errno` tests green.

### `long long` → `double`/`float` (`_fplltod`, `_fpulltod`, `_fplltof`, `_fpulltof`)

* A 64-bit integer has more significant bits than `double` (53) or `float` (24),
  so **rounding is required**. Round-to-nearest-even by default; honor the directed
  mode in `_fe_round` (through `_fe_dirup`), like `_fpltof`.
* **Signed** variants: take the sign from bit 63, convert the magnitude.
  **Unsigned** variants: use the full 64-bit magnitude — a value ≥ 2⁶³ must **not**
  be read as negative.
* Set **`FE_INEXACT`** in `_fe_status` whenever rounding drops any bit.
* `0` → `+0.0`.

### `double`/`float` → `long long` (`_fpdtoll`, `_fpdtoull`, `_fpftoll`, `_fpftoull`)

* **Truncate toward zero** (discard the fraction), matching `_fpdtol`/`_fpftol`.
* **Saturate** out-of-range inputs, extending `_fpftol`'s 32-bit rule to 64-bit:
  * **signed:** `x ≥ 2⁶³` or `+inf` → `LLONG_MAX` = `$7FFFFFFFFFFFFFFF`;
    `x < −2⁶³` or `−inf` → `LLONG_MIN` = `$8000000000000000`;
    `NaN` → by its sign bit (`+`→`LLONG_MAX`, `−`→`LLONG_MIN`);
    `x == −2⁶³` exactly is **in range** → `LLONG_MIN`.
  * **unsigned:** `x ≤ 0` (any negative, `−inf`) → `0`;
    `x ≥ 2⁶⁴` or `+inf` → `ULLONG_MAX` = `$FFFFFFFFFFFFFFFF`; `NaN` → `0`.
* Set **`FE_INVALID`** for any saturated (out-of-range / `inf` / `NaN`) result;
  set **`FE_INEXACT`** when a nonzero fraction is discarded.

> If you want a first cut, the §2 register/stack ABI is the only hard requirement;
> the flag bookkeeping can follow. But the **truncate-toward-zero + the saturation
> constants above should match on the first pass** so the double/`long double`
> lockstep tests don't shift.

---

## 4. C68k-side change that lands with the re-sync (one line)

So the **asm68K** build path declares the new externs (the integrated ELF emitter
tracks undefined symbols itself and needs no change), extend codegen's `EXTERN`
block at [codegen68k.c](../src/codegen68k.c#L1457):

```c
  println("  EXTERN _fpltof,_fpftol,_fpltod,_fpdtol,_fpftod,_fpdtof");
  println("  EXTERN _fplltod,_fpulltod,_fplltof,_fpulltof");   /* + */
  println("  EXTERN _fpdtoll,_fpdtoull,_fpftoll,_fpftoull");   /* + */
```

---

## 5. Validation

1. **worm68k:** assemble the new `conv/` sources; run the library's own conversion
   tests (round-trip each direction, incl. the ±2⁶³ / 2⁶⁴ boundaries and `inf`/`NaN`).
2. **C68k:** `pwsh tools/vendor-sync.ps1` → `pwsh tools/build-libm.ps1`; then
   `m68k-elf-nm lib/libm/libm.a` must show all eight as `T _fp…`.
3. **End-to-end:** a `long long`↔`double`/`float` round-trip test plus
   `llrint`/`llround` must build (both `C68K_INTEGRATED_AS=1` and the asm68K path)
   and run identically on Osiris and CP/M-68K.

---

## 6. Out of scope (separate, pre-existing)

The **32-bit** `unsigned int` conversions currently reuse the **signed**
`_fpltod`/`_fpltof` (int→fp) and `_fpdtol`/`_fpftol` (fp→int) — wrong for 32-bit
values ≥ 2³¹. That is a different defect, not one of these eight; fix separately
if/when needed.
