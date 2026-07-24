#include <errno.h>
#include "libc_internal.h"

/* Map a DOS/OS error code (from sys_lasterror / DOS 59h) to a POSIX errno.
 * These are the MS-DOS 5.0 codes Osiris latches; CP/M-68K has no extended-error
 * latch and reports 0, which we treat as "not found" -- the common file-op
 * failure there. */
int __oserr_to_errno(int code) {
  switch (code) {
  case 0:  /* CP/M / no latched code */
  case 2:  /* file not found */
  case 3:  /* path not found */
  case 18: /* no more files */
    return ENOENT;
  case 4: /* too many open files */
    return EMFILE;
  case 5:  /* access denied */
  case 16: /* attempt to remove the current directory */
  case 32: /* sharing violation */
  case 33: /* lock violation */
  case 82: /* cannot make directory */
    return EACCES;
  case 6: /* invalid handle */
    return EBADF;
  case 7: /* memory control blocks destroyed */
  case 8: /* insufficient memory */
  case 9: /* invalid memory block address */
    return ENOMEM;
  case 15: /* invalid drive */
  case 20: /* unknown unit */
    return ENXIO;
  case 17: /* not same device */
    return EXDEV;
  case 19: /* write-protected disk */
    return EROFS;
  case 80: /* file already exists */
    return EEXIST;
  case 1:  /* invalid function */
  case 10: /* invalid environment */
  case 11: /* invalid format */
  case 12: /* invalid access code */
  case 13: /* invalid data */
    return EINVAL;
  default: /* 21 not ready, 23-31 media/hardware, 83 critical error, ... */
    return EIO;
  }
}

int __oserrno(void) { return __oserr_to_errno(sys_lasterror()); }
