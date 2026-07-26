# C68K Actionable Items — CP/M‑68K

Every open conformance / platform item that applies when targeting **CP/M‑68K**
(`-target cpm`, DRI `.68K` output), why it exists, and how it could be addressed.
Derived from [c99-conformance.md](c99-conformance.md) and
[posix-and-platform-plan.md](posix-and-platform-plan.md). The companion list for
the other target is [osiris-actionable-items.md](osiris-actionable-items.md).

Items are grouped into **CP/M‑68K‑specific** (unique to this OS) and **Shared**
(compiler/libc‑wide — they manifest on CP/M‑68K but are tracked once and also
appear in the Osiris list).

**Solvability legend:** 🟢 solvable (clear path) · 🟡 partially solvable /
mitigable · 🔴 no in‑band solution (intrinsic to the OS or the target).

> CP/M‑68K is the *leaner* target. Digital Research's 1983 CP/M has **no
> environment, no subdirectories (user areas instead of paths), no child
> processes, no per‑file timestamps, and a 128‑byte record file model**. Those
> are the OS‑specific items below; C68K's contract is that each affected function
> still links and returns a documented failure (`NULL`/`-1`/`errno = ENOSYS`),
> never a silent wrong answer.

---

## 1. CP/M‑68K‑specific items

### 1.1 `getenv` always returns `NULL` (no environment) — 🟡
- **What.** Every `getenv` lookup misses; `setenv`/`putenv`/`unsetenv` are no‑ops;
  `environ` is `{ NULL }`.
- **Why it exists.** CP/M‑68K has **no process environment block** — there is no OS
  facility to store or inherit `NAME=VALUE` pairs. The `sys_getenv` seam
  ([cpm.c](../libc/core/cpm.c)) returns `NULL` unconditionally. (Standards‑wise an
  empty environment is still conforming — C99 §7.20.4.5 lets the environment be
  empty.)
- **Possible solution.** Provide a **process‑local libc env table**: `setenv`
  writes into a libc‑owned array and `getenv` reads it back, giving working
  intra‑program semantics (e.g. a program can `setenv("TZ", …)` and have
  `localtime` honour it). *Limits:* there is no shell to seed it and no child
  process to inherit it, so it is per‑run only. Worth doing for self‑configured
  programs; cannot become a true system environment.

### 1.2 `system()` / child processes unavailable — 🔴
- **What.** `system(cmd)` returns `-1`; `system(NULL)` returns `0` (correctly
  reporting "no command processor").
- **Why it exists.** CP/M‑68K has no command processor reachable from a transient
  and no **returning** child‑process facility. Program load / chain (`P_LOAD`/
  chain) *replaces* the running transient in the TPA and does not return control
  or an exit status to the caller.
- **Possible solution.** None for standard `system()` semantics (run a command,
  come back with its status) — the OS cannot return to the parent after running a
  child. A **non‑returning** `_chain("PROG")` extension (load‑and‑go, never
  returns) is the only thing CP/M‑68K can offer, and it is not what `system()`
  promises. Documented ⛔.

### 1.3 No subdirectories — `mkdir`/`chdir` fail — 🔴
- **What.** `mkdir`/`chdir`/`getcwd`‑style hierarchy is unavailable; the filesystem
  is flat.
- **Why it exists.** CP/M‑68K organises files by **user area** (0–15) on each
  drive, not by hierarchical path. There is no directory tree to create or walk.
- **Possible solution.** None that yields real POSIX paths. A libc extension could
  map "directories" onto the 16 user areas (a flat, fixed namespace), but it would
  not be hierarchical and would confuse path‑joining code, so the honest behaviour
  is `ENOSYS`/`-1`. Documented ⛔.

### 1.4 No locale/country service — `""` falls back to `"C"` — 🟢
- **What.** `setlocale(LC_*, "")` yields the `"C"` locale; `localeconv` reports the
  `"C"` values; `strcoll`/`strxfrm` are byte‑order only.
- **Why it exists.** CP/M‑68K has no NLS/country service (the Osiris path uses DOS
  `38h` for `lconv` and `65h`/`06` for the collating sequence). With no OS source
  for locale data, `""` can only be `"C"`.
- **Possible solution.** Ship **built‑in locale data in libc**: embed one or more
  country `struct lconv` tables and a collating‑weight table compiled into the
  library, so a non‑`"C"` locale is available on CP/M‑68K without any OS support.
  Pure libc data + a selector; fully feasible (it just trades code size for the
  feature).

### 1.5 Record‑granular file size / `fseek(SEEK_END)` — 🔴
- **What.** `fseek(…, SEEK_END)` lands on a 128‑byte **record** boundary; a file's
  exact byte length is not directly known.
- **Why it exists.** CP/M stores a file's length as a **record count** (128‑byte
  records), never an exact byte count. The libc seam recovers a *text* file's
  logical length by reading the last record and trimming a trailing Ctrl‑Z
  (`0x1A`); a *binary* file has no sub‑record length information at all.
- **Possible solution.** No exact fix — the filesystem does not store byte‑precise
  length. The `^Z`‑trim heuristic (already implemented) handles text files; binary
  files remain record‑padded by definition. This is an OS/filesystem limit, so it
  is documented, not solved. (Later CP/M variants with a byte‑length BDOS
  extension could improve it where present.)

### 1.6 In‑place random *modify* is best‑effort — 🟡
- **What.** Writing after a read without an intervening `fseek` is best‑effort; the
  verified pattern is write → `rewind` → read.
- **Why it exists.** The 128‑byte record model plus record‑level buffering means a
  byte write does not cleanly map to an in‑place update mid‑record.
- **Possible solution.** Implement **record read‑modify‑write** in the seam: buffer
  the target 128‑byte record, patch the bytes, and write the whole record back.
  That gives byte‑precise in‑place updates within the record model. Feasible libc
  seam improvement.

### 1.7 Per‑file timestamps limited to BDOSEXT — 🔴 (🟡 where BDOSEXT present)
- **What.** `utime` is effectively a no‑op; `stat` timestamps are unreliable.
- **Why it exists.** Base CP/M‑68K stores **no per‑file date/time**. Only the
  optional BDOSEXT extension (when the running system provides it) exposes a
  timestamp field.
- **Possible solution.** None on base CP/M — there is no field to write. Where
  BDOSEXT is present, wire `utime`/`stat` to it (partial, 🟡). Otherwise documented
  ⛔.

### 1.8 ~583 KiB TPA memory wall — 🟡
- **What.** A transient runs inside the **TPA** (Transient Program Area); large
  programs — e.g. the self‑hosted compiler — are constrained by it (self‑host is
  content‑identical but does not fully fit the tested ~583 KiB TPA — a memory wall,
  not a correctness gap).
- **Why it exists.** CP/M‑68K loads a single transient into the TPA; its size is
  fixed by the machine's RAM below the OS. There is no paging or overlay loader in
  the base model.
- **Possible solution.** Mitigate by **reducing footprint** (tighter compiler data
  structures, overlays, streaming passes) or run on a machine with a larger TPA.
  The hard ceiling is the physical TPA, so it can be pushed but not removed.

---

## 2. Shared items (compiler/libc‑wide — also apply to CP/M‑68K)

These are not CP/M‑specific; they are tracked once and repeated in the Osiris
list. Where CP/M‑68K is *more* limited than Osiris, it is noted.

### 2.1 No `_Complex` → `<complex.h>` / `<tgmath.h>` absent — 🟡
- **What.** Complex types and both headers are unimplemented (❌).
- **Why it exists.** The front end has no `_Complex` type, so neither `<complex.h>`
  nor the `<tgmath.h>` generic macros can exist.
- **Possible solution.** Add `_Complex` to the compiler (types, arithmetic over
  double pairs, codegen), then the libc `<complex.h>` and `<tgmath.h>`. Large but
  scoped compiler work; deferred. OS‑independent.

### 2.2 `long double` == `double` → `strtold` / `*l` math are 64‑bit — 🔴
- **What.** `long double` aliases `double`.
- **Why it exists.** The plain 68000 has no FPU; the soft‑float runtime is
  IEEE‑754 binary64, with no wider format to map onto (C99 permits the alias).
- **Possible solution.** None worthwhile for an FPU‑less target — a real wider
  `long double` needs a binary80/binary128 soft‑float (large runtime + ABI change,
  no hardware benefit) or a 68881/68882 FPU target. Stays as is; remains
  **conforming**. OS‑independent.

### 2.3 `qsort` is a shell sort — 🟢 (intentional; not a conformance gap)
- **What.** `qsort` is shell sort, not quicksort.
- **Why it exists.** C99 mandates the result, not the algorithm; shell sort is
  in‑place, non‑recursive, and small — good for constrained targets (especially the
  CP/M TPA).
- **Possible solution.** Swap in an introsort if large‑array speed matters. Already
  conforming, so this is a performance option.

### 2.4 `abort()` does not raise `SIGABRT` — 🟢
- **What.** `abort()` == `exit(1)`: runs `atexit`, flushes, never raises `SIGABRT`.
- **Why it exists.** Wired to `exit` before the synchronous signal layer existed.
- **Possible solution.** Route `abort()` through `raise(SIGABRT)`, then terminate
  abnormally via `_Exit` without `atexit`/flush if uncaught. Small libc fix;
  OS‑independent.

### 2.5 `signal`/`raise` are synchronous‑only — 🔴 on CP/M‑68K
- **What.** Only `raise` fires; no async delivery.
- **Why it exists.** No OS async‑signal mechanism.
- **Possible solution.** Unlike Osiris (which can source `SIGINT` from a console
  break and `SIGFPE`/`SIGILL`/`SIGSEGV` from CPU traps), base CP/M‑68K offers no
  console‑break callback or trap routing to user code, so async signals are not
  achievable here. `raise` remains conforming for program‑generated signals.

### 2.6 `errno` is a plain `extern int` — 🟢 (N/A single‑threaded)
- **What / why.** Single global; fully conforming for the single‑threaded model.
- **Possible solution.** Only matters if threads are added (→ thread‑local). No
  action today.

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
  `__STDC_IEC_559__`, so C99 Annex F does not apply. OS‑independent.

### 2.8 `atof` uses the libm `atod` parser — 🟢
- **What / why.** `atof` routes through libm `atod`, not `strtod`; conforming but a
  separate parser.
- **Possible solution.** Re‑express `atof(s)` as `strtod(s, NULL)`. One‑line libc
  change; OS‑independent.

### 2.9 `strerror` has a limited message set — 🟢
- **What / why.** Only a subset of `errno` values map to strings (still conforming).
- **Possible solution.** Extend the message table to every defined `errno`. Pure
  libc data change; OS‑independent.

### 2.10 `rand` is a 32767‑range LCG — 🟢 (conforming; quality option)
- **What / why.** LCG with `RAND_MAX == 32767` (C99 minimum); weak bits.
- **Possible solution.** Optionally improve the engine (e.g. xorshift). Conforming
  as is. OS‑independent.

### 2.11 `clock()` returns `-1` — 🔴 on CP/M‑68K (typically)
- **What.** `clock()` reports `(clock_t)-1`.
- **Why it exists.** No CPU‑time counter is read.
- **Possible solution.** On Osiris a system tick can approximate CPU time; base
  CP/M‑68K commonly exposes **no tick/clock source** to a transient, so `clock()`
  usually has no source to draw on here. Where the specific BIOS provides a tick, a
  wall‑time approximation is possible (🟡); otherwise it stays `-1`.

### 2.12 `strftime` supports a subset of specifiers — 🟡 on CP/M‑68K
- **What.** Implements the common specifiers; several C99 ones are missing
  (`%U%W%V%G%g%C%D%F%R%T%u%w%c%x%X%r`).
- **Why it exists.** Incremental implementation.
- **Possible solution.** Add the numeric/week specifiers (OS‑independent). The
  locale‑composed `%c`/`%x`/`%X` would only ever be the `"C"` form on CP/M‑68K
  unless the built‑in locale data of item 1.4 is added (Osiris can use its NLS
  country data directly).

### 2.13 `time_t` is 32‑bit (Year‑2038) — 🟡
- **What / why.** 32‑bit signed `time_t`, overflowing in 2038; compact ABI.
- **Possible solution.** Widen to 64‑bit (`long long`) — an ABI change across
  `time.c` and callers; the RTC seam returns broken‑down fields so epoch math can
  be 64‑bit. OS‑independent.

### 2.14 `INFINITY`/`NAN`/`HUGE_VAL` are function‑backed — 🟢
- **What.** They expand to calls, so they are not constant expressions.
- **Why it exists.** No hardware FP literals; produced at runtime.
- **Possible solution.** Fold `__builtin_inf[f]`/`__builtin_nan[f]` to IEEE‑754 bit
  patterns in the compiler so the macros become constant expressions. Small
  compiler + header change; OS‑independent.

---

## Summary — CP/M‑68K

| # | Item | Solvability |
|---|------|:---:|
| 1.1 | `getenv`/env — none native | 🟡 (process‑local table) |
| 1.2 | `system`/child processes | 🔴 |
| 1.3 | No subdirectories (`mkdir`/`chdir`) | 🔴 |
| 1.4 | No locale/country service | 🟢 (embed libc data) |
| 1.5 | Record‑granular file size / `SEEK_END` | 🔴 |
| 1.6 | In‑place random modify | 🟡 (record RMW) |
| 1.7 | Per‑file timestamps | 🔴 (🟡 with BDOSEXT) |
| 1.8 | ~583 KiB TPA wall | 🟡 (footprint) |
| 2.1 | `_Complex` / `<complex.h>` / `<tgmath.h>` | 🟡 |
| 2.2 | `long double` == `double` | 🔴 |
| 2.3 | `qsort` shell sort | 🟢 (intentional) |
| 2.4 | `abort` no `SIGABRT` | 🟢 |
| 2.5 | `signal`/`raise` sync‑only | 🔴 (no trap routing) |
| 2.6 | `errno` single global | 🟢 (N/A) |
| 2.7 | soft‑float accuracy — campaign done | 🟢 |
| 2.8 | `atof` via `atod` | 🟢 |
| 2.9 | `strerror` sparse table | 🟢 |
| 2.10 | `rand` 32767 LCG | 🟢 (quality) |
| 2.11 | `clock()` == −1 | 🔴 (no tick) |
| 2.12 | `strftime` subset | 🟡 |
| 2.13 | `time_t` 32‑bit | 🟡 |
| 2.14 | `INFINITY`/`NAN` not constant | 🟢 |

The **hard** (🔴) CP/M‑68K items all trace to the 1983 OS design — no returning
child process, no directory tree, record‑granular files, no timestamps, no async
traps — plus the target‑FP `long double` reality. The remaining items are solvable
in libc/compiler or are deliberate, conforming choices; several (env, locale,
record RMW) are recoverable *inside* libc despite the OS lacking the facility.
