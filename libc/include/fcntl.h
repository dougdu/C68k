#ifndef _FCNTL_H
#define _FCNTL_H

/* POSIX file control.  open()/creat() translate these flags onto the Osiris
 * DOS (3Dh/3Ch) and CP/M-68K (F_OPEN/F_MAKE) syscall seam.  DOS has no
 * permission bits beyond a read-only attribute, so the open() mode argument
 * is accepted for source compatibility but otherwise ignored. */

/* Access mode -- the low two bits of the flags word. */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3

/* Open flags. */
#define O_CREAT 0x0100  /* create the file if it does not exist */
#define O_TRUNC 0x0200  /* truncate to zero length on open */
#define O_APPEND 0x0400 /* seek to end before each write */
#define O_EXCL 0x0800   /* with O_CREAT, fail if the file exists */
#define O_BINARY 0x1000 /* no Ctrl-Z text-EOF translation (CP/M) */
#define O_TEXT 0x2000   /* text stream (Ctrl-Z is EOF on CP/M) */

/* fcntl() commands. */
#define F_DUPFD 0 /* duplicate the descriptor */
#define F_GETFD 1 /* get descriptor flags */
#define F_SETFD 2 /* set descriptor flags */
#define F_GETFL 3 /* get status flags */
#define F_SETFL 4 /* set status flags */

int open(const char *path, int flags, ...);
int creat(const char *path, int mode);
int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H */
