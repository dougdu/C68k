# C68K — Beyond‑C99 Library & Platform Plan

This spec drives the library work that goes **past** the C99 standard surface
tracked in [`c99-conformance.md`](c99-conformance.md): the POSIX/Unix layer, a
real filesystem/metadata API, the environment story, native OS escape hatches,
and the universally‑shipped conveniences that make C68K feel like a production
toolchain rather than a conformance demo.

Sections are ordered by the agreed **value‑to‑effort priority** (P1 highest).
Each function carries its own status so we can land the work incrementally and
see the burndown at a glance.

---

## Legend

| Symbol | Meaning |
|:------:|---------|
| ✅ | Done and verified on both targets (or documented `ENOSYS` where a target lacks the facility). |
| 🚧 | In progress. |
| ❌ | Not started. |
| ⛔ | Not applicable on this target by design (degrades to `errno = ENOSYS`). |

---

## Progress dashboard

| # | Section | Done / Total | Status |
|---|---------|:------------:|:------:|
| P1 | POSIX file‑descriptor layer + `<fcntl.h>` | 13 / 13 | ✅ |
| P2 | Real `stat` / `fstat` | 4 / 4 | ✅ |
| P3 | `<dirent.h>` + directory & filesystem metadata | 10 / 10 | ✅ |
| P4 | Environment: `setenv` family + `environ` / `envp` | 7 / 7 | ✅ |
| P5 | Native syscall headers + `<conio.h>` | 12 / 12 | ✅ |
| P6 | Conformance polish: errno mapping, math errno, locale | 5 / 5 | ✅ |
| P7 | Ubiquitous conveniences (`getopt`, `strlcpy`, …) | 11 / 11 | ✅ |
| P8 | C11/C17 hosted additions + `<fenv.h>` stubs | 7 / 7 | ✅ |

---

## Design conventions (apply to every item below)

- **Seam naming.** A new syscall seam is a C function `sys_foo` declared in
  [`libc/core/libc_internal.h`](../libc/core/libc_internal.h); its Osiris asm
  definition is `PUBLIC _sys_foo` in
  [`libc/osiris/osiris_sys.a68`](../libc/osiris/osiris_sys.a68) (c68k mangling
  adds exactly one leading underscore). The CP/M side is C in
  [`libc/cpm/cpm.c`](../libc/cpm/cpm.c) (calling `cpm_bdos`), with an asm shim in
  `libc/cpm/cpm_sys.a68` only when a BDOS call needs register work the C shim
  can't express.
- **⚠️ Osiris trap numbers are *proposed* here and MUST be verified against the
  authoritative `dos/osdos.a68` in the osiris tree before wiring** — the loose
  `dos-api.md` table has been wrong before. Every "Osiris" mapping cell is a
  starting hypothesis, not gospel.
- **Dual‑target contract.** CP/M‑68K has **no environment, no subdirectories
  (user areas instead of paths), and no child processes**. Where a target lacks
  the facility, the function still links and returns a documented failure
  (`errno = ENOSYS`, `-1`/`NULL`), never a silent wrong answer. Mark those ⛔.
- **errno discipline.** Seams return the raw OS status; the libc wrapper maps it
  through the central `__oserr_to_errno` (P6) and sets `errno`. Until P6 lands,
  wrappers set a best‑effort `errno` locally.
- **Definition of done per function:** (1) header declaration, (2) impl file,
  (3) correct behaviour on **both** OSes (or a documented ⛔ stub), (4) a
  self‑checking lockstep test or Osiris‑only probe (`X PASS n/n`, test name
  ≤ 8 chars for CP/M 8.3), (5) a row update in `c99-conformance.md`'s
  extensions table.

---

## P1 — POSIX file‑descriptor layer + `<fcntl.h>`

**Why first:** the seams already exist (`sys_open`/`sys_creat`/`sys_read`/
`sys_write`/`sys_seek`/`sys_close`), so most of this is thin wrappers. Today
[`unistd.h`](../libc/include/unistd.h) exposes `close()` but there is no
`open()`, so you can't obtain an fd from the POSIX side — this closes that
asymmetry and gives `fdopen`/`fileno` a foundation.

New header `<fcntl.h>`: `O_RDONLY O_WRONLY O_RDWR O_CREAT O_TRUNC O_APPEND
O_EXCL` (+ `O_BINARY`/`O_TEXT` for the CP/M ^Z text mode already modelled in
stdio). `open()` translates these to the existing `sys_open` mode word and
routes `O_CREAT` to `sys_creat`.

| Function | Header | Impl | Osiris | CP/M | Status |
|---|---|---|---|---|:--:|
| `open` | `<fcntl.h>` | `libc/core/open.c` | reuse `sys_open`/`sys_creat` (3Dh/3Ch) | reuse (F_OPEN 15 / F_MAKE 22) | ✅ |
| `creat` | `<fcntl.h>` | `open.c` | reuse `sys_creat` (3Ch) | F_MAKE 22 | ✅ |
| `read` | `<unistd.h>` | `read.c` | reuse `sys_read` (3Fh) | reuse `sys_read` | ✅ |
| `write` | `<unistd.h>` | `write.c` | reuse `sys_write` (40h) | reuse `sys_write` | ✅ |
| `lseek` | `<unistd.h>` | `lseek.c` | reuse `sys_seek` (42h) | reuse `sys_seek` | ✅ |
| `close` | `<unistd.h>` | `close.c` | reuse `sys_close` (3Eh) | reuse `sys_close` | ✅ |
| `dup` | `<unistd.h>` | `dup.c` | new `sys_dup` (45h) | ⛔ no fd dup (deferred) | ✅ |
| `dup2` | `<unistd.h>` | `dup.c` | new `sys_dup2` (46h) | ⛔ no fd dup (deferred) | ✅ |
| `fileno` | `<stdio.h>` | `fileno.c` | portable C (`FILE.fd`) | portable C | ✅ |
| `fdopen` | `<stdio.h>` | `fdopen.c` | portable C (wrap fd in `FILE`) | portable C | ✅ |
| `isatty` | `<unistd.h>` | `isatty.c` | new `sys_isatty` (44h IOCTL get‑devinfo) | fd 0/1/2 → 1, else 0 | ✅ |
| `access` | `<unistd.h>` | `access.c` | new `sys_access` (43h get‑attr) | F_SFIRST 17 + R/O bit | ✅ |
| `fcntl` | `<fcntl.h>` | `fcntl.c` | minimal `F_GETFL`/`F_SETFL`/`F_DUPFD` | minimal | ✅ |

**Notes.** fds 0/1/2 already map to the OS console handles (DOS predefined
handles 0–2; CP/M console via BDOS) — `open` returns handles ≥ 3.
`fdopen`/`fileno` require `FILE` to carry the OS fd, which stdio already does.

**Status (2026‑07‑23): ✅ DONE.** Verified on Osiris (`FDTEST` 18/18 +
`DUPPROBE` 9/9) and CP/M‑68K (`FDTEST` 18/18); full lockstep **13/13** on both
OSes. New seams verified against `osdos.a68`: 45h/46h dup, 44h/00h IOCTL
device‑info (→ D3, bit 7 = ISDEV), 43h/00h get‑attr (→ D2). `dup`/`dup2` are
Osiris‑only — CP/M‑68K has no descriptor duplication, so they return −1 there;
a shared‑offset fd‑table refactor is a future follow‑up. Tests:
[`tests/lockstep/fdtest.c`](../tests/lockstep/fdtest.c) (cross‑OS) and
[`tests/lockstep/dupprobe.c`](../tests/lockstep/dupprobe.c) (Osiris‑only).

---

## P2 — Real `stat` / `fstat`

**Why:** [`stat.c`](../libc/core/stat.c) is a permanent stub that always fails —
the single biggest "looks done but isn't" gap. Build tools, `find`‑like
utilities, and the self‑host want file size + mtime. The
[`struct stat`](../libc/include/sys/stat.h) already declares the fields.

| Item | Impl | Osiris | CP/M | Status |
|---|---|---|---|:--:|
| `stat` | `stat.c` | `sys_findfirst` (4Eh: attr/date) + `sys_open`+`sys_fdsize` (exact size) | `sys_findfirst` (F_SFIRST) + open + `sys_fdsize` | ✅ |
| `fstat` | `stat.c` | `sys_fdsize` (42h SEEK_END) + `sys_getfiletime` (57h/00) | `sys_fdsize` (extent/RC + `^Z` trim) | ✅ |
| `lstat` | `stat.c` | alias of `stat` (no symlinks) | alias | ✅ |
| `struct stat` + `S_IS*`/`S_IF*` | `sys/stat.h` | `st_mode`/`st_size`/`st_mtime` | `st_mtime` = 0 (no CP/M timestamp) | ✅ |

**Status (2026‑07‑23): ✅ DONE.** Verified cross‑OS (`STATDIR` 20/20 both).
`st_mode` = `S_IFREG`/`S_IFDIR` + a read‑only permission approximation; `st_mtime`
from the FAT date/time on Osiris, 0 on CP/M (no per‑file timestamp).
**Exact size:** Osiris stores byte‑exact lengths; CP/M stores only a 128‑byte
record count, so `sys_fdsize` reads the last record and trims trailing `^Z`
(0x1A) padding — exact for text and files ≤ one extent (binary data ending in
0x1A is over‑trimmed, an inherent CP/M limit). `stat` routes its size through the
same seam so `stat`/`fstat` agree. *(Gotcha: CP/M `F_SIZE`/35h misreports a
freshly‑opened handle as 65536 records, so the record count comes from the open
FCB's extent/RC instead.)*

---

## P3 — `<dirent.h>` + directory & filesystem metadata

**Why:** unlocks a whole class of programs (anything that lists or walks a
directory). Osiris find‑first/next is INT 21h‑shaped; CP/M has the classic
search‑first/next FCB scan.

| Function | Header | Impl | Osiris | CP/M | Status |
|---|---|---|---|---|:--:|
| `opendir` | `<dirent.h>` | `opendir.c` | `sys_findfirst` (1Ah+4Eh, own DTA per DIR) | F_SFIRST 17 (wildcard FCB) | ✅ |
| `readdir` | `<dirent.h>` | `opendir.c` | `sys_findnext` (1Ah+4Fh) | F_SNEXT 18 (extent‑0 dedup) | ✅ |
| `closedir` | `<dirent.h>` | `opendir.c` | portable C (free `DIR`) | portable C | ✅ |
| `rewinddir` | `<dirent.h>` | `opendir.c` | re‑issue find‑first | re‑issue search | ✅ |
| `mkdir` | `<sys/stat.h>` | `mkdir.c` | `sys_mkdir` (39h) | ⛔ ENOSYS | ✅ |
| `rmdir` | `<unistd.h>` | `rmdir.c` | `sys_rmdir` (3Ah) | ⛔ ENOSYS | ✅ |
| `chdir` | `<unistd.h>` | `chdir.c` | `sys_chdir` (3Bh) | ⛔ ENOSYS | ✅ |
| `getcwd` | `<unistd.h>` | `getcwd.c` | `sys_getcwd` (19h + 47h → `X:\path`) | drive root `X:\` | ✅ |
| `chmod` | `<sys/stat.h>` | `chmod.c` | `sys_chmod` (43h/01 set‑attr) | F_ATTRIB 30 (R/O bit) | ✅ |
| `utime` | `<utime.h>` | `utime.c` | open(RDONLY) + `sys_setfiletime` (57h/01) | ⛔ ENOSYS (base) | ✅ |

**Status (2026‑07‑23): ✅ DONE.** Cross‑OS `STATDIR` (opendir/readdir/chmod/
getcwd) 20/20 both; Osiris‑only `MKDIRP` (mkdir/rmdir/chdir/getcwd/utime) 12/12.
Each Osiris find call sets its own DTA (1Ah), so interleaved `readdir`+`stat`
works; CP/M keeps a single BDOS search state, so only one scan is active at a
time and `readdir` interleaved with other directory ops is unsupported there.
`readdir` dedups CP/M continuation extents (reports extent‑0 only). `utime` opens
read‑only so `close` cannot refresh the timestamp `57h/01` just set. Tests:
[`tests/lockstep/statdir.c`](../tests/lockstep/statdir.c) (cross‑OS) and
[`tests/lockstep/mkdirp.c`](../tests/lockstep/mkdirp.c) (Osiris‑only).

---

## P4 — Environment: `setenv` family + `environ` / `envp`

**Why:** `getenv` is real on Osiris (DOS 64h.00), but the write side and the
`environ`/`envp` array are missing. crt0 currently pushes an **empty** `c_envp`
to `main`, and there is no `environ` global, so portable code that iterates the
environment sees nothing.

| Function | Header | Impl | Osiris | CP/M | Status |
|---|---|---|---|---|:--:|
| `getenv` | `<stdlib.h>` | `getenv.c` | `sys_getenv` (64h.00) | ⛔ NULL | ✅ |
| `setenv` | `<stdlib.h>` | `setenv.c` | `sys_setenv` (64h.01 SET) | ⛔ −1 | ✅ |
| `putenv` | `<stdlib.h>` | `setenv.c` | split `NAME=VALUE` → `sys_setenv` | ⛔ −1 | ✅ |
| `unsetenv` | `<stdlib.h>` | `unsetenv.c` | `sys_setenv` empty value (delete) | ⛔ no-op | ✅ |
| `clearenv` | `<stdlib.h>` | `unsetenv.c` | iterate GETBLK, delete each | ⛔ no-op | ✅ |
| `environ` (global) | `<unistd.h>` | `environ.c` | `envbuild` from 64h.02 GETBLK | empty `{ NULL }` | ✅ |
| `envp` in `main` | crt0 | `environ.c` + both crt0s | crt0 `jsr _envbuild` → envp | empty `{ NULL }` | ✅ |

**Status (2026‑07‑23): ✅ DONE.** `ENVTEST` 11/11 on both OSes (dual‑target
invariants). Both crt0s call `envbuild` before `main`, so `envp == environ` — a
fixed static array (no malloc) rebuilt from the OS block. `getenv` stays on the
live 64h.00 lookup; `setenv`/`unsetenv` go through 64h.01, which **reallocates**
the block, so `environ` is rebuilt after each change; children inherit via EXEC.
Deviation: the OS treats an empty value as a delete, so `setenv(name, "")` removes
the variable. CP/M‑68K has no environment — the whole family is a documented
no‑op/failure and `environ` is `{ NULL }`. Test:
[`tests/lockstep/envtest.c`](../tests/lockstep/envtest.c).

**Notes.** DOS‑style env is process‑local and fixed‑size in the PSP: `setenv`
affects this process and children it `EXEC`s, **never** the parent shell (this
is expected, but SET can fail when the block is full → needs a real error path).
Decide whether `environ` is built lazily on first use or eagerly in crt0
(lazy avoids startup cost for programs that never touch it).

---

## P5 — Native syscall headers + `<conio.h>`

**Why:** ship a direct‑syscall escape hatch (the Borland/Watcom `intdos()` /
CP/M `bdos()` pattern) so users reach the OS for services we haven't wrapped,
plus the console primitives a DOS‑like target is expected to have. This is what
makes the platform usable *before* the libc is exhaustive.

### `<osiris.h>` — direct DOS access

| Symbol | Purpose | Osiris | Status |
|---|---|---|:--:|
| `intdos` (+ `struct DOSREGS`) | generic DOS‑call escape hatch | `sys_doscall` trampoline (loads D0-D3/A0/A1, traps, returns regs + carry) | ✅ |
| `_dos_getdiskfree` | free space (bytes) on a drive | 36h | ✅ |
| `_dos_getdrive` / `_dos_setdrive` | current drive | 19h / 0Eh | ✅ |
| dir scan / attrs / RTC | findfirst, getfileattr, getdate | *reachable via `<dirent.h>`/`<sys/stat.h>`/`<time.h>`, or `intdos` directly* | ✅ (POSIX) |
| `int86` | x86‑style interrupt call | ⛔ n/a — m68k has one DOS trap; use `intdos` | ⛔ |

### `<cpm.h>` — direct BDOS access

| Symbol | Purpose | CP/M | Status |
|---|---|---|:--:|
| `bdos(func, param)` | BDOS escape hatch (wraps `cpm_bdos`) | trap #2 | ✅ |
| `bios(func, …)` | BIOS call | ❌ deferred — CP/M‑68K BIOS is not a fixed jump table; niche from C | ❌ |

### `<conio.h>` — console primitives

| Function | Purpose | Osiris | CP/M | Status |
|---|---|---|---|:--:|
| `getch` / `getche` | raw key (no/echo) | `sys_conin` (07h) | C_RAWIO 6 (poll) | ✅ |
| `kbhit` | key‑ready poll | `sys_constat` (0Bh) | C_STAT 11 | ✅ |
| `putch` / `cputs` | raw char/string out | `sys_write(1,…)` | `sys_write(1,…)` | ✅ |
| `clrscr` / `gotoxy` | screen control | ANSI/VT100 escapes | ANSI/VT100 | ✅ |

**Status (2026‑07‑23): ✅ DONE.** Cross‑OS `CONIOT` 3/3 both; Osiris‑only
`OSPROBE` 3/3 (intdos + `_dos_*`); CP/M‑only `CPMPROBE` 2/2 (bdos round‑trip);
full lockstep 16/16 both. The `sys_doscall` trampoline captures the carry flag
(the standard DOS error indicator) plus D0-D3/A0/A1; 36h returns via `d1_ok_dt`,
so D2/D3 are valid returns. Native headers are OS‑specific but link on both OSes
via opposite‑OS stubs (`sys_doscall` → −1 on CP/M; `cpm_bdos` → −1 on Osiris) and
are dead‑stripped unless used. `clrscr`/`gotoxy` assume an ANSI/VT100 console
(Osiris ANSI.SYS). Tests: [`tests/lockstep/coniot.c`](../tests/lockstep/coniot.c),
[`tests/lockstep/osprobe.c`](../tests/lockstep/osprobe.c),
[`tests/lockstep/cpmprobe.c`](../tests/lockstep/cpmprobe.c).

---

## P6 — Conformance polish: errno mapping, math errno, locale

**Why:** cross‑cutting quality. Real programs branch on `ENOENT` vs `EACCES`;
math code checks `errno` for `EDOM`/`ERANGE`; locale‑aware code must at least
link against the "C" locale.

| Workstream | Impl | Notes | Status |
|---|---|---|:--:|
| Central errno mapping | `libc/core/oserr.c` (`__oserr_to_errno`) | `sys_lasterror` (DOS 59h) → `__oserrno()`; open/creat/unlink/rename/mkdir/rmdir/chdir route failures through it | ✅ |
| Math sets `errno` (EDOM/ERANGE) | `math.h` base fns | `sqrt`<0 / `log`≤0 / `acos`/`asin` \|x\|>1 / `fmod` y==0 → EDOM; `exp`/`pow` overflow → ERANGE; Tier2 fns already set errno | ✅ |
| `setlocale` | `locale.c` | "C"/"POSIX"/"" → `"C"`, else `NULL`; query returns `"C"` | ✅ |
| `localeconv` | `locale.c` | static "C" `struct lconv` (`.` decimal point, rest CHAR_MAX/empty) | ✅ |
| `<locale.h>` | header | `LC_*` macros, `struct lconv` | ✅ |

**Status (2026‑07‑24): ✅ DONE.** Cross‑OS `POLISH` 13/13 both; full lockstep
17/17; tier2/tier2f unregressed (56/56). errno mapping uses Osiris DOS 59h (the
latched code → errno via the MS‑DOS 5.0 table); CP/M has no extended‑error latch,
so failures default to `ENOENT`. Math errno covers the **double** base functions
(long double inherits via the double wrappers; the float variants bind directly
to libm and do not set errno). Only the "C" locale is supported. Test:
[`tests/lockstep/polish.c`](../tests/lockstep/polish.c).

---

## P7 — Ubiquitous conveniences

**Why:** not standard, but present in essentially every toolchain; you'll want
`getopt` the moment your own tools grow flags, and the BSD string helpers show
up constantly in portable code.

| Function | Header | Impl | Status |
|---|---|---|:--:|
| `getopt` | `<unistd.h>` / `<getopt.h>` | `getopt.c` | ✅ |
| `getopt_long` | `<getopt.h>` | `getopt.c` | ✅ |
| `alloca` | `<alloca.h>` | compiler builtin (header is decl‑free) | ✅ |
| `reallocarray` | `<stdlib.h>` | `reallocarray.c` | ✅ |
| `qsort_r` | `<stdlib.h>` | `qsort.c` (GNU arg order) | ✅ |
| `strlcpy` / `strlcat` | `<string.h>` ext | `strlcpy.c` | ✅ |
| `strsep` | `<string.h>` ext | `strsep.c` | ✅ |
| `strcasestr` | `<strings.h>` | `strcasestr.c` | ✅ |
| `memmem` | `<string.h>` ext | `memmem.c` | ✅ |
| `itoa` / `utoa` / `ltoa` / `ultoa` | `<stdlib.h>` ext | `itoa.c` | ✅ |
| `err`/`errx`/`warn`/`warnx` (+ `v*`, get/setprogname) | `<err.h>` | `err.c` | ✅ |

**Status (2026‑07‑24): ✅ DONE.** Cross‑OS `CONVTEST` 26/26 both; full lockstep
18/18. All pure C (no seams). `getopt` uses POSIX ordering (stops at the first
non‑option, no argv permutation); `getopt_long` handles `--name` and
`--name=value`. `alloca` is a compiler builtin, so `<alloca.h>` is intentionally
decl‑free (a redeclaration would clash with the builtin's `void *(int)` type).
`qsort_r` uses the GNU comparator order `cmp(a, b, arg)`. `err`/`warn` prefix with
`getprogname()` (empty by default → no prefix). Test:
[`tests/lockstep/convtest.c`](../tests/lockstep/convtest.c).

---

## P8 — C11/C17 hosted additions + `<fenv.h>` stubs

**Why:** several modern‑standard bits are cheap and increasingly assumed by
portable code. (C68K already ships the C11 *freestanding* headers
`stdalign.h`/`stdatomic.h`/`stdnoreturn.h`.) `<threads.h>` is intentionally out
of scope — these OSes are single‑threaded.

| Function / Header | Header | Impl | Notes | Status |
|---|---|---|---|:--:|
| `aligned_alloc` | `<stdlib.h>` | `aligned_alloc.c` | over libheap | ✅ |
| `posix_memalign` | `<stdlib.h>` | `aligned_alloc.c` | POSIX sibling | ✅ |
| `at_quick_exit` | `<stdlib.h>` | `exit.c` | register handlers | ✅ |
| `quick_exit` | `<stdlib.h>` | `exit.c` | run them, then `_Exit` | ✅ |
| `timespec_get` | `<time.h>` | `time.c` | `TIME_UTC` via `sys_time` | ✅ |
| `<uchar.h>` (`char16_t`/`char32_t`, `mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb`) | `<uchar.h>` | `uchar.c` | UTF‑8 ↔ UTF‑16/32; C locale | ✅ |
| `<fenv.h>` stubs | `<fenv.h>` | `fenv.c` | fixed round‑to‑nearest soft‑float: `fegetround`→`FE_TONEAREST`, `fesetround` no‑op, exception ops unsupported | ✅ |

> **Status (both targets):** shipped and verified by `C11TEST` (18/18 on Osiris
> and CP/M‑68K, wired into `run-lockstep.ps1`).
> - `aligned_alloc`/`posix_memalign`: the libheap SOA allocator already returns
> ≥ 8‑byte (`max_align_t`) blocks, so alignments ≤ 8 forward to `malloc` and stay
> `free()`‑compatible; a stricter alignment returns `NULL`/`ENOMEM` (no over‑aligned
> heap primitive, and none is needed on the 68000). `aligned_alloc` also enforces
> the C11 `size % alignment == 0` rule (`EINVAL`).
> - `<uchar.h>`: full UTF‑8 ↔ UTF‑16/UTF‑32 with overlong/surrogate/out‑of‑range
> rejection; `mbrtoc16` emits a surrogate pair across two calls (2nd returns
> `(size_t)-3`) and `c16rtomb` holds a high surrogate until its low half.
> - `<fenv.h>`: conforming‑but‑inert — exception ops are no‑ops and only
> `FE_TONEAREST` can be selected (`math_errhandling` is `MATH_ERRNO`).

---

## Cross‑target facility matrix (quick reference)

| Facility | Osiris | CP/M‑68K |
|---|:--:|:--:|
| File descriptors / handles | ✅ (predefined 0–2) | ✅ (libc fd‑table over FCB) |
| Directory scan | ✅ 4Eh/4Fh | ✅ F_SFIRST/F_SNEXT |
| Subdirectories (`mkdir`/`chdir`) | ✅ 39h/3Bh | ⛔ user areas only |
| Environment (`getenv`/`setenv`) | ✅ 64h | ⛔ none |
| Child processes (`system`/`exec`) | ✅ 4Bh | ⛔ none |
| Per‑file timestamp | ✅ 57h | ⚠️ BDOSEXT only |
| Raw console (`conio`) | ✅ 06h–0Bh | ✅ C_RAWIO 6 |

---

## Suggested execution order

Land P1 first (mostly wrappers over existing seams), then P2/P3 together (they
share the find‑first/next and attribute plumbing), then P4. P5–P8 can proceed in
parallel once the seam pattern from P1–P4 is established. Update the dashboard
and `c99-conformance.md` extensions table as each row flips to ✅.
