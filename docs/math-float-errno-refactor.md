# `<math.h>` float-variant `errno` refactor — precise specification

**Goal:** make the C99 `float` math functions (`sqrtf`, `logf`, `expf`, `powf`,
`fmodf`, `asinf`, `acosf`, …) set `errno` (`EDOM`/`ERANGE`) exactly like the
`double` base functions already do, so the **only** thing keeping `<math.h>`
yellow (⚠️) in [c99-conformance.md](c99-conformance.md) is the missing
`_Complex` support (`<complex.h>` / `<tgmath.h>`).

This document is the authoritative work-order for that change. `lib/libm` is
**vendored from worm68k** via [tools/vendor-sync.ps1](../tools/vendor-sync.ps1),
so the library half lands upstream first and is then re-synced.

---

## 1. Root cause — the naming law the last refactor broke

A libc `errno`-setting wrapper is interposed by giving the **public C name** to a
`static` inline wrapper that calls the **raw kernel under a *different* C name**.
That only works if the kernel does **not** occupy the C-standard public name.

* **Double side (correct):** kernel is `sqrtd` → asm `_sqrtd`; the public `sqrt`
  is a `static` inline wrapper that sets `errno` and calls `sqrtd`. No `_sqrt`
  symbol exists. The wrapper name (`sqrt`) ≠ the kernel name (`sqrtd`), so both
  can be declared in the same header. ✅

* **Single side (broken by commit `87d581d` / worm68k `8ae6d5a`):** that refactor
  renamed the single kernels **`_sqrt` → `_sqrtf`**, i.e. **onto** the C99 public
  names. Now the kernel *is* `sqrtf`. You cannot write

  ```c
  extern float sqrtf(float);            /* the kernel            */
  static float sqrtf(float x){ ...; }   /* the wrapper — SAME name → illegal */
  ```

  and a libc-side real `sqrtf` wrapper would emit `_sqrtf`, colliding with the
  libm kernel symbol `_sqrtf` at link time. So **no errno wrapper can be
  interposed.** The previous refactor moved the kernels in exactly the wrong
  direction for this goal (it did unblock `fmaf`, which needs *no* errno, but by
  consuming the public names it blocked the 7 functions that *do*).

**The law:** *every libm kernel that libc must wrap for `errno` must have a name
distinct from its C-standard public name* (as the `d` kernels already do).

The toolchain maps C `foo` → asm `_foo` (single underscore, unconditional — see
[codegen68k.c](../src/codegen68k.c#L104)), so a C name `__sqrtf` would need the
asm label `___sqrtf` (triple underscore). The clean, symmetric choice is the
**`s` (single) suffix**, mirroring the existing `d` (double) suffix:
`sqrts`/`sqrtd`, `exps`/`expd`, ….

---

## 2. Functions that need a wrapper

**All 15 single math kernels are renamed `_Xf` → `_Xs`** (fully uniform — §3).
The **7** with a domain/range error also get `errno` logic in a `static` inline
wrapper (matching the existing `double` wrappers exactly); the other 8 get a
trivial pass-through. The 7 with error semantics:

| public `f` name | kernel today | kernel after | error semantics (C99, mirrors the `double` wrapper) |
|---|---|---|---|
| `sqrtf` | `_sqrtf` | `_sqrts` | `x < 0` → `EDOM`, return NaN |
| `expf`  | `_expf`  | `_exps`  | overflow (finite `x`, `inf` result) → `ERANGE` |
| `logf`  | `_logf`  | `_logs`  | `x < 0` → `EDOM`; `x == 0` → `ERANGE`, return −inf |
| `powf`  | `_powf`  | `_pows`  | NaN from non-NaN args → `EDOM`; overflow → `ERANGE` |
| `fmodf` | `_fmodf` | `_fmods` | `y == 0` → `EDOM`, return NaN |
| `asinf` | `_asinf` | `_asins` | `\|x\| > 1` → `EDOM`, return NaN |
| `acosf` | `_acosf` | `_acoss` | `\|x\| > 1` → `EDOM`, return NaN |

The remaining single kernels have **no** domain/range error, but under the
uniform naming they are **also renamed to `_Xs`** and reached through a trivial
one-line `static` pass-through: `sinf`, `cosf`, `atanf`, `floorf`, `ceilf`,
`fabsf`, `modff`, `fmaf`. The composed `tanf`, `log10f`, `atan2f` stay as-is —
and `log10f` **inherits `logf`'s `errno` for free** once `logf` is a wrapper.

The `long double` variants (`sqrtl`, …) already call the `double` wrappers, so
they inherit `errno` today — no change.

---

## 3. libm change (worm68k) — rename all single math kernels `_Xf` → `_Xs`

Fully uniform: every single-precision **math** kernel is renamed so the library
rule is trivially *single = `s`, double = `d`*, structurally identical to the
double side. Rename each definition **and every reference** — the entry label,
in-file `bsr`/`jsr`/`lea`, and cross-file `extern`s.

**Kernel definitions (15):**

| file | rename |
|---|---|
| [`math/sqrt.a68`](../lib/libm/math/sqrt.a68) | `_sqrtf` → `_sqrts` |
| [`math/exp.a68`](../lib/libm/math/exp.a68) | `_expf` → `_exps` |
| [`math/log.a68`](../lib/libm/math/log.a68) | `_logf` → `_logs` |
| [`math/sincos.a68`](../lib/libm/math/sincos.a68) | `_sinf` → `_sins`, `_cosf` → `_coss` |
| [`math/atan.a68`](../lib/libm/math/atan.a68) | `_atanf` → `_atans` |
| [`math/asincos.a68`](../lib/libm/math/asincos.a68) | `_asinf` → `_asins`, `_acosf` → `_acoss` |
| [`math/floor.a68`](../lib/libm/math/floor.a68) | `_floorf` → `_floors`, `_ceilf` → `_ceils`, `_modff` → `_modfs` |
| [`core/fppwr.a68`](../lib/libm/core/fppwr.a68) | `_powf` → `_pows` |
| [`core/fmod.a68`](../lib/libm/core/fmod.a68) | `_fmodf` → `_fmods` |
| [`core/fabs.a68`](../lib/libm/core/fabs.a68) | `_fabsf` → `_fabss` |
| [`core/fma.a68`](../lib/libm/core/fma.a68) | `_fmaf` → `_fmas` |

**Cross-file `extern` references (in addition to each file's own label + calls):**

| file | externs to rename |
|---|---|
| [`math/dpmath.a68`](../lib/libm/math/dpmath.a68) | `_sqrtf` → `_sqrts` |
| [`math/asincos.a68`](../lib/libm/math/asincos.a68) | `_sqrtf` → `_sqrts`, `_atanf` → `_atans` |
| [`math/sincos.a68`](../lib/libm/math/sincos.a68) | `_fmodf` → `_fmods` |
| [`core/fppwr.a68`](../lib/libm/core/fppwr.a68) | `_logf` → `_logs`, `_expf` → `_exps` |

Authoritative sweep: replace the whole-word tokens `_sqrtf _expf _logf _sinf
_cosf _atanf _powf _fmodf _floorf _ceilf _modff _fabsf _fmaf _asinf _acosf` with
their `_…s` forms across `lib/libm/**` (comment banners included), then assemble
every source and confirm no `_Xf` token remains.

> **FP core helpers keep their names.** `_fpadd`/`_fpmult`/`_fpcmp`… stay
> "bare = single, `…d` = double" because they collide with **no** C library name;
> conversions (`_fpftod`…), `_fe_*`, and `_FormatFloat`/`_FormatDouble` are
> likewise unchanged. The `s`/`d` suffix is applied only to the *math* kernels,
> which collide with `<math.h>` names — that principled split (suffix only where
> C names collide) is the whole point.

Bump [`lib/libm/VENDOR.txt`](../lib/libm/VENDOR.txt) on re-sync.

---

## 4. libc uptake — [`libc/include/math.h`](../libc/include/math.h)

Replace the current "C99 `float` variants" block (the `extern float sqrtf(float);`
… list plus the composed trio) with kernel-externs + `static` inline wrappers
that mirror the `double` section. **Copy-paste ready:**

```c
/* ------------------------------------------------------------------------
 * C99 `float` variants.  The single-precision kernels are the `s`-suffixed
 * libm exports (sqrts/exps/...).  The C99 `f` names with a domain/range error
 * are `static` inline wrappers that set EDOM/ERANGE (mirroring the double
 * wrappers) and then dispatch to the single kernel, so `float` math runs
 * 32-bit soft-float with conforming errno instead of promoting to double.
 * The no-error names are trivial `static` pass-throughs over their kernels;
 * tan/log10/atan2 are composed over the wrappers (log10f inherits logf's errno).
 * ------------------------------------------------------------------------ */
extern float sqrts(float);          /* raw single kernels (…s = single) */
extern float exps(float);
extern float logs(float);
extern float pows(float, float);
extern float fmods(float, float);
extern float asins(float);
extern float acoss(float);
extern float sins(float);           /* no domain error: pass-through below */
extern float coss(float);
extern float atans(float);
extern float floors(float);
extern float ceils(float);
extern float fabss(float);
extern float modfs(float, float *);
extern float fmas(float, float, float);

static float sqrtf(float x) {
  if (x < 0.0f) { errno = EDOM; return (float)__nan_val(); }
  return sqrts(x);
}
static float expf(float x) {
  float r = exps(x);
  if (__isinf((double)r) && __isfinite((double)x))
    errno = ERANGE;                 /* overflow */
  return r;
}
static float logf(float x) {
  if (x < 0.0f) { errno = EDOM; return (float)__nan_val(); }
  if (x == 0.0f) { errno = ERANGE; return -(float)__huge_val(); }
  return logs(x);
}
static float powf(float b, float e) {
  float r = pows(b, e);
  if (__isnan((double)r) && !__isnan((double)b) && !__isnan((double)e))
    errno = EDOM;
  else if (__isinf((double)r) && __isfinite((double)b) && __isfinite((double)e))
    errno = ERANGE;
  return r;
}
static float fmodf(float a, float b) {
  if (b == 0.0f) { errno = EDOM; return (float)__nan_val(); }
  return fmods(a, b);
}
static float asinf(float x) {
  if (x < -1.0f || x > 1.0f) { errno = EDOM; return (float)__nan_val(); }
  return asins(x);
}
static float acosf(float x) {
  if (x < -1.0f || x > 1.0f) { errno = EDOM; return (float)__nan_val(); }
  return acoss(x);
}

/* no domain/range error — trivial pass-throughs over the single kernels */
static float sinf(float x) { return sins(x); }
static float cosf(float x) { return coss(x); }
static float atanf(float x) { return atans(x); }
static float floorf(float x) { return floors(x); }
static float ceilf(float x) { return ceils(x); }
static float fabsf(float x) { return fabss(x); }
static float modff(float x, float *ip) { return modfs(x, ip); }
static float fmaf(float x, float y, float z) { return fmas(x, y, z); }

/* composed — unchanged; log10f inherits logf's EDOM/ERANGE */
static float tanf(float x) { return sinf(x) / cosf(x); }
static float log10f(float x) { return logf(x) / (float)M_LN10; }
static float atan2f(float y, float x) {
  if (x > 0.0f)
    return atanf(y / x);
  if (x < 0.0f)
    return atanf(y / x) + (y >= 0.0f ? (float)M_PI : -(float)M_PI);
  return y > 0.0f ? (float)M_PI_2 : (y < 0.0f ? -(float)M_PI_2 : 0.0f);
}
```

Notes:
* The classification helpers (`__isinf`/`__isnan`/`__isfinite`) and
  `__nan_val`/`__huge_val` take/return `double`; a `float` promotes exactly
  (NaN→NaN, ±inf→±inf, finite→finite), so the promoted checks are correct.
  `(float)__nan_val()` / `-(float)__huge_val()` yield a single NaN / −inf.
* *Optional micro-opt:* if the per-call `float`→`double` promotion in `expf`/`powf`
  is undesirable, add single-precision `__isinff`/`__isnanf`/`__isfinitef`
  helpers; not required for conformance.
* The no-error `f` names are `static` pass-throughs over their `…s` kernels
  (zero overhead once inlined), keeping the whole single API uniform: `…s`
  kernels + C-standard `f`/`l` wrappers.

---

## 5. Docs — [`docs/c99-conformance.md`](c99-conformance.md)

1. **Table 1 row `<math.h>` (~L90).** Rewrite the Deviations cell so `_Complex` is
   the sole conformance gap:
   > Full C99 function set: base transcendentals + `f`/`l` type variants +
   > classification/constants (Tier 2 2a/2b/2c) + native `asin`/`acos`. **Both**
   > the `double` base functions and the `float` variants set `EDOM`/`ERANGE` on
   > domain/range errors (the `f` names are errno-setting header wrappers over
   > the `s`-suffixed single kernels; `l` == `double`). **Sole deviation: no
   > `_Complex`** (so `<complex.h>`/`<tgmath.h>` absent). Soft-float
   > transcendentals are not correctly-rounded (~1–2 ULP for
   > `sqrt`/`atan`/`asin`/`acos`, more for others), but that is an *accuracy*
   > note, not a conformance gap — C68K does not define `__STDC_IEC_559__`, so
   > Annex F does not apply.

2. **§`<math.h>` detail (~L221).** Delete the now-false line *"Remaining gap:
   `errno` is not set by the inline base functions."* — the `double` wrappers
   set `errno` today and the `float` wrappers do after this change. State the
   sole gap is `_Complex`.

3. **"`float` / `long double` variants" paragraph (~L253).** Replace *"bind to
   libm's real single-precision kernels …"* with: the `f` base functions are
   `static` inline wrappers that add `EDOM`/`ERANGE` over the `s`-suffixed single
   kernels (32-bit soft-float, conforming `errno`); the no-domain-error names
   (`sinf`/`cosf`/`atanf`/`floorf`/`ceilf`/`fabsf`/`modff`/`fmaf`) are trivial
   `static` pass-throughs over their `s`-kernels; `tanf`/`log10f`/`atan2f` are
   composed over the wrappers.

4. Keep the ULP accuracy table as a **quality** note; add one sentence that it is
   not a conformance requirement absent `__STDC_IEC_559__`.

---

## 6. Tests — [`tests/lockstep/tier2f.c`](../tests/lockstep/tier2f.c)

Add `errno` checks mirroring the `double` ones (include `<errno.h>`, clear
`errno` before each, assert after):

* `sqrtf(-1.0f)` → NaN, `errno == EDOM`
* `logf(-1.0f)` → NaN, `EDOM`; `logf(0.0f)` → −inf, `ERANGE`
* `log10f(0.0f)` → −inf, `ERANGE` (inherited from `logf`)
* `expf(200.0f)` → +inf, `ERANGE`
* `powf(-1.0f, 0.5f)` → NaN, `EDOM`; `powf(1e30f, 3.0f)` → +inf, `ERANGE`
* `fmodf(1.0f, 0.0f)` → NaN, `EDOM`
* `asinf(2.0f)` / `acosf(2.0f)` → NaN, `EDOM`
* one no-error call (e.g. `sinf`) with `errno` left unchanged, to confirm the
  pass-through path sets nothing.

---

## 7. Cross-repo & validation

* **worm68k:** apply §3, commit, push; then in C68k run
  `pwsh tools/vendor-sync.ps1` and `tools/build-libm.ps1`.
* **Sibling repos:** any code that calls the single **math** kernels *directly*
  updates `_Xf` → `_Xs`. prolog68k uses only the `…d` double API and is
  unaffected; audit pascal68k/fortran68k for single-`REAL` calls to
  `_sqrtf`/`_expf`/`_sinf`/… and rename them.
* **Validate:** rebuild libm → MATHTEST, TIER2F (+ new `errno` checks), LIBTEST
  on **both** Osiris and CP/M-68K; then the C68k lockstep suite. No codegen
  change is involved, so no host-compiler rebuild is required.

---

## 8. Why the previous refactor didn't achieve this

It renamed the single kernels **bare → `_Xf`**, moving them *onto* the C99 public
names. That is precisely the position that blocks an `errno` wrapper (§1). It was
a lateral move for unification (and unblocked the no-errno `fmaf`), but it gained
nothing toward conforming `float` `errno` and actively blocked it. The fix is the
opposite direction — kernels **off** the public names (`_Xs`), leaving `sqrtf`…
free for the wrappers.
