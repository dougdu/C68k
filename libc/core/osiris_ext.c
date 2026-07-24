#include <osiris.h>
#include <string.h>
#include "libc_internal.h"

int intdos(struct DOSREGS *r) { return (int)sys_doscall(r); }

int _dos_getdrive(void) {
  struct DOSREGS r;
  memset(&r, 0, sizeof r);
  r.d0 = 0x1900; /* 19h current disk -> AL */
  sys_doscall(&r);
  return (int)(r.d0 & 0xFF);
}

int _dos_setdrive(int drive) {
  struct DOSREGS r;
  memset(&r, 0, sizeof r);
  r.d0 = 0x0E00; /* 0Eh select disk */
  r.d3 = (unsigned long)(drive & 0xFF);
  sys_doscall(&r);
  return (int)(r.d0 & 0xFF); /* AL = number of drives */
}

long _dos_getdiskfree(int drive) {
  struct DOSREGS r;
  memset(&r, 0, sizeof r);
  r.d0 = 0x3600; /* 36h free space */
  r.d3 = (unsigned long)(drive & 0xFF);
  sys_doscall(&r);
  if (r.cflag)
    return -1;
  unsigned long spc = r.d0 & 0xFFFF; /* sectors per cluster */
  unsigned long fc = r.d1 & 0xFFFF;  /* free clusters */
  unsigned long bps = r.d2 & 0xFFFF; /* bytes per sector */
  return (long)(fc * spc * bps);
}
