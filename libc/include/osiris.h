#ifndef _OSIRIS_H
#define _OSIRIS_H

/* Direct Osiris DOS (TRAP #1) access -- an escape hatch to services the
 * portable library does not wrap.  Osiris-only: on CP/M-68K there is no DOS
 * trap, so intdos() returns -1 and the _dos_* helpers report failure.
 *
 * The register block mirrors the DOS call convention (abi-68k.md 3): the
 * function selector goes in d0 (AH high byte, AL low byte), word params in
 * d1/d2/d3 (BX/CX/DX), pointers in a0/a1.  cflag is the carry flag on return
 * (nonzero = error). */
struct DOSREGS {
  unsigned long d0, d1, d2, d3;
  void *a0, *a1;
  long cflag;
};

int intdos(struct DOSREGS *r);
long _dos_getdiskfree(int drive); /* free bytes on drive (0 = default), or -1 */
int _dos_getdrive(void);          /* current drive, 0 = A */
int _dos_setdrive(int drive);     /* select drive (0 = A); returns #drives */

#endif /* _OSIRIS_H */
