# c68k — Documentation

This folder holds the design documents and manuals for **c68k**, a C99 compiler for the MC68000 that
targets both **Osiris DOS** and **CP/M-68K**.

**Using c68k?** Start with the [User's Manual](users-manual.md); keep the
[Programmer's Reference Manual](reference-manual.md) open for the language model, ABI, and the full
library. **Understanding or contributing to the compiler?** Read the design docs (1–3) in order.

| # | Document | What it covers |
| --- | --- | --- |
| 1 | [architecture.md](architecture.md) | The whole design: goals & non-goals, the chibicc basis and what we keep vs. replace, the two OS targets and the dual-target strategy, the single-source self-hosting compiler, the 68000 code generator & code model (ABI, type model, PIE), the C-library platform split, the build & test pipeline, the cross-compiler as a maintained deliverable, repository layout, key decisions, and risks. |
| 2 | [libc-and-toolchain.md](libc-and-toolchain.md) | The standard C library structure (core + per-OS backend + `crt0`), the ~15-function **syscall seam** mapped to Osiris DOS (`TRAP #1`) and CP/M-68K BDOS (`TRAP #2`), the runtime support library (soft-float, 64-bit `long long`, integer helpers, `libm`), and the **native toolchain**: the assembler question, the `LINK`/`LIB` ports, and `mkdri` conversion. |
| 3 | [implementation-plan.md](implementation-plan.md) | The phased build plan **P0–P13** with a live **progress dashboard**, per-phase exit criteria, and the milestone list. This is the document that tracks progress. |
| 4 | [users-manual.md](users-manual.md) | **User's Manual** — installing the SDK, quick start, the compiler driver and every switch, optimization, debugging with `-g`, building and running for each OS, the toolchain tools, testing, limitations, and troubleshooting. |
| 5 | [reference-manual.md](reference-manual.md) | **Programmer's Reference Manual** — the language & ILP32 type model, the calling convention/ABI, the ELF object format, the driver, the optimizations, **every supported standard-library function**, the syscall seam, the runtime helpers, the toolchain, and the Osiris/CP/M-68K platform table. |
| 6 | [sdk.md](sdk.md) | **SDK quickstart** for third-party programs: how to compile with `c68k`, the driver options and predefined macros (`__c68k__`/`__osiris__`/`__CPM68K__`), the per-OS link recipes (`.PRG` / `.68K`), and a worked one-source, two-target `hello` example. |

## External references (consumed as-is)

These live in the sibling repos and are the normative contracts c68k targets:

- **Osiris** (`osiris/`): `docs/abi-68k.md` (the 68000 `TRAP` ABI), `docs/dos-api.md` (the DOS
  system calls), `docs/architecture.md` (§12 program loader), `sdk/docs/prg-format.md` (the `.PRG`
  format & loader contract), `ld/osiris-prg.ld` (the linker script), `include/osiris.inc`
  (equates), and the native `LINK.PRG` / `LIB.PRG` tools.
- **worm68k** (`worm68k/`): `mkdri` (the ELF→DRI `.68K` converter) and `cpm68k.ld` (the CP/M-68K
  linker script), plus the CP/M-68K system under `sim68k`.
- **chibicc** (upstream): the C front end this project derives from.

## Conventions

- Status lines carry a **Draft N.N** version and a date; the changelog at the foot of each document
  records revisions.
- Progress is tracked **only** in [implementation-plan.md](implementation-plan.md) — other
  documents describe the design, not the state.
