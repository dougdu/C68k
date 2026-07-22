#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno;

#define ENOENT 2   /* No such file or directory */
#define EIO 5      /* I/O error */
#define EBADF 9    /* Bad file descriptor */
#define ENOMEM 12  /* Out of memory */
#define EACCES 13  /* Permission denied */
#define EEXIST 17  /* File exists */
#define EINVAL 22  /* Invalid argument */
#define EMFILE 24  /* Too many open files */
#define EDOM 33    /* Numerical argument out of domain */
#define ERANGE 34  /* Numerical result out of range */
#define EILSEQ 84  /* Illegal byte sequence */

#endif /* _ERRNO_H */
