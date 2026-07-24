#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno;

#define ENOENT 2   /* No such file or directory */
#define EIO 5      /* I/O error */
#define ENXIO 6    /* No such device or address */
#define EBADF 9    /* Bad file descriptor */
#define ENOMEM 12  /* Out of memory */
#define EACCES 13  /* Permission denied */
#define EBUSY 16   /* Device or resource busy */
#define EEXIST 17  /* File exists */
#define EXDEV 18   /* Cross-device link */
#define EINVAL 22  /* Invalid argument */
#define EMFILE 24  /* Too many open files */
#define ENOTTY 25  /* Inappropriate I/O control operation */
#define ENOSPC 28  /* No space left on device */
#define EROFS 30   /* Read-only file system */
#define EDOM 33    /* Numerical argument out of domain */
#define ERANGE 34  /* Numerical result out of range */
#define ENOSYS 38  /* Function not implemented */
#define EILSEQ 84  /* Illegal byte sequence */

#endif /* _ERRNO_H */
