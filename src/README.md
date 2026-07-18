# c68k compiler sources

This directory holds the c68k compiler: the **chibicc** C front end plus the
68000 back end that replaces chibicc's x86-64 code generator in later phases.

## Provenance & attribution

The files here are derived from **chibicc** by Rui Ueyama
(<https://github.com/rui314/chibicc>), used under the MIT License. The upstream
license text is preserved verbatim in [CHIBICC-LICENSE](CHIBICC-LICENSE); the
combined project license and attribution are in the repository-root
[LICENSE](../LICENSE).

| Item | Value |
| --- | --- |
| Upstream | https://github.com/rui314/chibicc |
| Imported commit | `90d1f7f` ("Make struct member access to work with `=` and `?:`") |
| Import phase | P0 --- imported as the known-good baseline |

At import the sources were byte-for-byte upstream; only their location changed
(compiler sources into `src/`, chibicc's freestanding builtin headers into
`../include/`, the conformance suite into `../tests/`). Retargeting to
big-endian ILP32 and the 68000 begins in P1/P2.

### P0 host-portability changes (the only deviation from upstream)

To build the cross-compiler on the maintainers' hosts (**Windows/MSVC** and
**macOS/Clang**) as well as Linux, P0 adds a small platform layer and makes the
driver call through it. This changes **no compiler behavior** (host/driver glue
only); on Linux the POSIX path is byte-for-byte chibicc's original logic, so the
x86-64 self-host stays intact.

| File | Change |
| --- | --- |
| `compat.h`, `compat.c` | **New (c68k)** platform layer: POSIX headers on Unix; MSVC shims (`spawn_and_wait`, `open_memstream`, `strndup`, `dirname`/`basename`, `mkstemp`, `ctime_r`, case-compare) on Windows. |
| `chibicc.h` | POSIX-only `#include`s routed through `compat.h`. |
| `main.c` | `run_subprocess` now calls `spawn_and_wait`; the Linux-only `glob`/`ld` path is `#ifdef`-guarded (Windows gets a stub). |

## Files (all from chibicc)

| File | Role |
| --- | --- |
| `chibicc.h` | Shared declarations |
| `tokenize.c` | Lexer / preprocessor tokenizer |
| `preprocess.c` | C preprocessor |
| `parse.c` | Parser + semantic analysis |
| `type.c` | Type system (retargeted to ILP32-BE in P1) |
| `codegen.c` | Code generator (replaced by `codegen68k.c` in P2) |
| `main.c` | Driver / option parsing |
| `hashmap.c`, `strings.c`, `unicode.c` | Support utilities |
