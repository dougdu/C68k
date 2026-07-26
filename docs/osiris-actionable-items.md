# C68K Actionable Items — Osiris

Every open conformance / platform item that applies when targeting **Osiris**
(`-target osiris`, `.PRG` output), why it exists, and how it could be addressed.
Derived from [c99-conformance.md](c99-conformance.md) and
[posix-and-platform-plan.md](posix-and-platform-plan.md). The companion list for
the other target is [cpm68k-actionable-items.md](cpm68k-actionable-items.md).

Items are grouped into **Osiris‑specific** (unique to this OS) and **Shared**
(compiler/libc‑wide — they manifest on Osiris but are tracked once and also
appear in the CP/M‑68K list).

**Solvability legend:** 🟢 solvable (clear path) · 🟡 partially solvable /
mitigable · 🔴 no in‑band solution (intrinsic to the OS or the target).

> Osiris is the *fuller* of the two targets: it has an environment, child
> processes, subdirectories, per‑file timestamps, and an NLS/country service.
> Consequently it has very few OS‑specific gaps — almost everything below is a
> shared compiler/libc item.

---

## 1. Osiris‑specific items

### 1.1 `system()` command tail capped at 127 bytes — 🔴
- **What.** A string passed to `system()` is truncated to 127 bytes.
- **Why it exists.** Osiris follows the MS‑DOS PSP ABI: the spawned command
  processor receives its command tail at `PSP:0x80` as a one‑byte length + up to
  127 characters + a `CR`. `libc/core/system.c` clamps to that. The 127‑byte cap
  is structural to the DOS `4Bh` EXEC contract, not a C68K choice.
- **Possible solution.** None that keeps the DOS EXEC contract — the child reads
  its tail from the PSP, which is 127 bytes wide. *Workaround:* write the long
  command into a temporary batch/response file and `system()` that file. This is
  the same wall real MS‑DOS programs hit (it is why `COMMAND.COM` refused
  >127‑char lines), so it is documented rather than fixed.

### 1.2 `system()` needs free memory for the EXEC child — 🟡
- **What.** `system()` fails if too little conventional memory is free for the
  child; crt0 permanently reserves `EXEC_RESERVE` (`$40000`, up to 256 KiB) —
  [osiris_sys.a68](../libc/osiris/osiris_sys.a68).
- **Why it exists.** DOS EXEC (`4Bh`) loads the child into the largest free
  block. A program that owns all memory leaves none for a child, so Osiris crt0
  keeps a fixed pool free up front. A program whose heap needs more than
  `TOTAL − 256 KiB`, or a child larger than 256 KiB, then fails.
- **Possible solution.** Replace the fixed reserve with **dynamic
  shrink‑before‑EXEC**: free the heap's unused tail via the OS resize call
  immediately before the EXEC and reclaim it after (the classic DOS C‑runtime
  approach), so the reserve tracks actual need instead of a fixed 256 KiB.
  Feasible crt0 + `system.c` change.

---

## 2. Shared items (compiler/libc‑wide — also apply to Osiris)

These are not Osiris‑specific; they are tracked once and repeated in the
CP/M‑68K list. Where Osiris has a *better* path than CP/M‑68K, it is noted.

### 2.1 No `_Complex` → `<complex.h>` / `<tgmath.h>` absent — 🟡
- **What.** The complex types and both headers are unimplemented (❌).
- **Why it exists.** The chibicc‑derived front end has no `_Complex` type; without
  it neither `<complex.h>` (its functions) nor `<tgmath.h>` (type‑generic macros
  over real *and* complex) can be provided.
- **Possible solution.** Add `_Complex` to the front end (type system, arithmetic
  lowered to double pairs, codegen), then a libc `<complex.h>` over the soft‑float
  and the `<tgmath.h>` generic macros. Large but well‑scoped compiler work;
  deliberately deferred (no hardware complex support to leverage).

### 2.2 `long double` == `double` → `strtold` / `*l` math are 64‑bit — 🔴
- **What.** `long double` is an alias for `double`; `strtold`, `sinl`, … are thin
  wrappers over the 64‑bit versions.
- **Why it exists.** The plain 68000 has **no FPU**; the soft‑float runtime is
  IEEE‑754 **binary64**. There is no wider format to map `long double` onto, so it
  aliases `double` (a choice C99 explicitly permits).
- **Possible solution.** None worthwhile for the FPU‑less target. A true wider
  `long double` would need either a full IEEE‑754 binary80/binary128 soft‑float
  (large runtime + ABI change, no hardware benefit) or a **68881/68882** FPU
  target (native 80‑bit). Until a hardware‑FP variant is added, this stays as is —
  and remains **conforming**.

### 2.3 `qsort` is a shell sort — 🟢 (intentional; not a conformance gap)
- **What.** `qsort` uses shell sort, not the named quicksort.
- **Why it exists.** C99 mandates only the *result*, not the algorithm. Shell sort
  is in‑place, non‑recursive (no stack growth, no O(n²) stack blow‑up), and small —
  ideal for memory‑constrained targets.
- **Possible solution.** Swap in an introsort/median‑of‑three quicksort with an
  insertion‑sort cutoff if large‑array throughput ever matters. Behaviour is
  already conforming, so this is a performance option, not a fix.

### 2.4 `abort()` does not raise `SIGABRT` — 🟢
- **What.** `abort()` is implemented as `exit(1)`: it runs `atexit` handlers and
  flushes streams, and never raises `SIGABRT`.
- **Why it exists.** It predates the synchronous `signal`/`raise` layer and was
  wired straight to `exit`. C99 §7.20.4.1 says `abort` raises `SIGABRT` (catchable),
  and should *not* run `atexit` handlers.
- **Possible solution.** Route `abort()` through `raise(SIGABRT)` first (the
  synchronous signal layer already exists), and if the handler returns or is
  `SIG_DFL`, terminate abnormally via `_Exit` **without** running `atexit`/flush.
  Small, self‑contained libc fix.

### 2.5 `signal`/`raise` are synchronous‑only — 🟡
- **What.** Only program‑generated signals (`raise`) fire; no asynchronous
  delivery. All six C99 signals and `sig_atomic_t` are defined.
- **Why it exists.** Neither OS delivers async signals to user code by default.
- **Possible solution (Osiris angle).** Osiris *can* offer a partial async story:
  hook the console Ctrl‑C/break path to deliver `SIGINT`, and route the CPU
  exception vectors (divide‑by‑zero, illegal instruction, address/bus error) to
  `SIGFPE`/`SIGILL`/`SIGSEGV`. Full POSIX async signalling is still out of scope,
  but trap‑sourced signals are achievable on Osiris.

### 2.6 `errno` is a plain `extern int` — 🟢 (N/A single‑threaded)
- **What.** `errno` is a single global.
- **Why it exists.** The runtime is single‑threaded; a global `errno` is fully
  conforming for a single‑threaded hosted implementation.
- **Possible solution.** Only relevant if threads are ever added, at which point
  `errno` must become thread‑local. No action needed today.

### 2.7 Soft‑float accuracy — extensively addressed; residual is by design — 🟢
- **What (current state).** The IEEE‑754 **mandated** operations are
  correctly‑rounded (round‑to‑nearest‑even): single & double `+ − × ÷`, compare,
  int↔float / float↔float conversions, and **both** single and double `sqrt`
  (`sqrtd` corrected 2026‑07‑24). The double **elementary functions** were brought
  from the earlier ~10²–10⁴ ULP down to **≤ 1–3 ULP (gated ≤ 4)** — `expd` ≤ 1,
  `logd` ≤ 2, `sind`/`cosd` ≤ 2, `powd` ≤ 3 — and a **Payne–Hanek** reducer
  (`__rem_pio2_big`, 1792‑bit 2/π) covers `sin`/`cos`/`sind`/`cosd` for |x| ≥ 2²⁰,
  so there is **no large‑argument accuracy cliff**. Gradual underflow (subnormals),
  directed rounding, and sticky exception flags are also wired (`<fenv.h>`‑backed).
- **Why the earlier numbers were high.** The kernels were low‑order minimax
  approximations with no fused multiply‑add. The fix was structural: a
  correctly‑rounded software **FMA** (`_fmad`/`_fmas`) was built first, then the
  transcendentals were re‑expressed on it with **double‑double** argument reduction
  (FMA‑Horner roughly halves the per‑step rounding error), reaching ≤ 1 ULP without
  80‑bit hardware.
- **Status / residual.** The accuracy campaign is **complete upstream** (worm68k
  `ieee754` Phases A–D, 2026‑07‑24 — `IEEE_FLOAT_CONFORMANCE_ROADMAP.md`, closing
  `bugs/BUG‑0021`) and is **synced into c68k's `lib/libm`** (the FMA core
  `core/dpfma.a68`, the Payne–Hanek reducer in `math/dpmath.a68`, correctly‑rounded
  `sqrtd` and `atod`). The **residual is deliberate**: the library targets ≤ 1 ULP,
  *not* correctly‑rounded (< 0.5 ULP) transcendentals — IEEE‑754 clause 9 only
  *recommends* that, the hardest‑to‑round cases need hundreds of bits, and ≤ 1 ULP
  matches the good historical libms. (D2 traps and D6 sNaN/payloads stay deferred by
  design.) None of this is a C99 requirement — C68K does not define
  `__STDC_IEC_559__`, so C99 Annex F does not apply.

### 2.8 `atof` uses the libm `atod` parser — 🟢
- **What.** `atof` goes through libm's `atod` rather than the C `strtod`.
- **Why it exists.** Historical wiring to the soft‑float parser. C99 lets `atof`
  behave like `strtod(nptr, NULL)` with unspecified `errno`, so it is conforming,
  but it can round/handle edges differently from the (more careful) `strtod`.
- **Possible solution.** Re‑express `atof(s)` as `strtod(s, NULL)` so both share one
  parser. One‑line libc change.

### 2.9 `strerror` has a limited message set — 🟢
- **What.** Only a subset of `errno` values map to descriptive strings.
- **Why it exists.** The table was seeded with the C99/math + a POSIX subset;
  unlisted codes get a generic message. C99 only requires *some* message, so this
  is conforming but sparse.
- **Possible solution.** Extend the table to cover every defined `errno`
  (`ENOENT`/`EIO`/`EBADF`/`ENOMEM`/`EACCES`/`EEXIST`/`EINVAL`/`EMFILE`/`EROFS`/
  `ENOSYS`/…). Pure libc data change.

### 2.10 `rand` is a 32767‑range LCG — 🟢 (conforming; quality option)
- **What.** `rand` is an LCG with `RAND_MAX == 32767` (the C99 minimum).
- **Why it exists.** Smallest compliant generator; weak low bits / short period.
- **Possible solution.** Keep `RAND_MAX` for compatibility but improve the engine
  (e.g. xorshift), or raise `RAND_MAX` and expose more bits. Optional quality work.

### 2.11 `clock()` returns `-1` — 🟡
- **What.** `clock()` reports `(clock_t)-1` (no CPU‑time source).
- **Why it exists.** No process CPU‑time counter is read today.
- **Possible solution (Osiris angle).** On a single‑tasking OS wall‑time ≈ CPU‑time
  for the running program, so Osiris could derive `clock()` from a system tick /
  RTC‑based counter and a real `CLOCKS_PER_SEC`. Approximate but useful; feasible
  where a tick source is exposed.

### 2.12 `strftime` supports a subset of specifiers — 🟢
- **What.** Implements `%Y%y%m%d%e%H%M%S%j%a%b%h%p%z%Z%%`; several C99 specifiers
  are missing (`%U%W%V%G%g%C%D%F%R%T%u%w%c%x%X%r`).
- **Why it exists.** Incremental implementation of the common specifiers.
- **Possible solution.** Add the remaining specifiers — most are compositions of
  existing fields; `%U`/`%W`/`%V` need ISO week‑number logic; `%c`/`%x`/`%X` are
  locale‑composed (Osiris has the NLS country data to do this well). Pure libc work.

### 2.13 `time_t` is 32‑bit (Year‑2038) — 🟡
- **What.** `time_t`/`clock_t` are 32‑bit signed, overflowing in 2038.
- **Why it exists.** Compact era‑appropriate ABI; the RTC/DOS date sits well inside
  the range.
- **Possible solution.** Widen `time_t` to 64‑bit (`long long`) — an ABI change
  touching `time.c` and every caller; the RTC seam already returns broken‑down
  fields, so the epoch math can be 64‑bit. Feasible but a deliberate ABI break.

### 2.14 `INFINITY`/`NAN`/`HUGE_VAL` are function‑backed — 🟢
- **What.** These expand to calls (`__nan_val`/`__huge_val`), so they are not
  constant expressions and cannot appear in static initializers.
- **Why it exists.** No hardware FP literals; the values are produced at runtime.
- **Possible solution.** Have the compiler recognize `__builtin_inf[f]`/
  `__builtin_nan[f]` and fold them to the IEEE‑754 bit pattern at compile time, so
  the macros become constant expressions. Small compiler + header change.

---

## Summary — Osiris

| # | Item | Solvability |
|---|------|:---:|
| 1.1 | `system()` 127‑byte command tail | 🔴 |
| 1.2 | `system()` EXEC memory reserve | 🟡 |
| 2.1 | `_Complex` / `<complex.h>` / `<tgmath.h>` | 🟡 |
| 2.2 | `long double` == `double` | 🔴 |
| 2.3 | `qsort` shell sort | 🟢 (intentional) |
| 2.4 | `abort` no `SIGABRT` | 🟢 |
| 2.5 | `signal`/`raise` sync‑only | 🟡 |
| 2.6 | `errno` single global | 🟢 (N/A) |
| 2.7 | soft‑float accuracy — campaign done | 🟢 |
| 2.8 | `atof` via `atod` | 🟢 |
| 2.9 | `strerror` sparse table | 🟢 |
| 2.10 | `rand` 32767 LCG | 🟢 (quality) |
| 2.11 | `clock()` == −1 | 🟡 |
| 2.12 | `strftime` subset | 🟢 |
| 2.13 | `time_t` 32‑bit | 🟡 |
| 2.14 | `INFINITY`/`NAN` not constant | 🟢 |

The only **hard** (🔴) Osiris items are the two structural DOS‑ABI limits on
`system()` (127‑byte tail; and `long double`, which is a target‑FP reality). Every
other item is solvable or is a deliberate, conforming choice.
