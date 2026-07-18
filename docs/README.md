# c68k — Documentation

This folder holds the normative design documents for **c68k**, a C99 compiler for the MC68000 that
targets both **Osiris DOS** and **CP/M-68K**. Read them in this order.

| # | Document | What it covers |
| --- | --- | --- |
| 1 | [architecture.md](architecture.md) | The whole design: goals & non-goals, the chibicc basis and what we keep vs. replace, the two OS targets and the dual-target strategy, the single-source self-hosting compiler, the 68000 code generator & code model (ABI, type model, PIE), the C-library platform split, the build & test pipeline, the cross-compiler as a maintained deliverable, repository layout, key decisions, and risks. |
| 2 | [libc-and-toolchain.md](libc-and-toolchain.md) | The standard C library structure (core + per-OS backend + `crt0`), the ~15-function **syscall seam** mapped to Osiris DOS (`TRAP #1`) and CP/M-68K BDOS (`TRAP #2`), the runtime support library (soft-float, 64-bit `long long`, integer helpers, `libm`), and the **native toolchain**: the assembler question, the `LINK`/`LIB` ports, and `mkdri` conversion. |
| 3 | [implementation-plan.md](implementation-plan.md) | The phased build plan **P0–P13** with a live **progress dashboard**, per-phase exit criteria, and the milestone list. This is the document that tracks progress. |

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
