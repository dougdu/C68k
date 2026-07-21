#ifndef LIBC_INTERNAL_H
#define LIBC_INTERNAL_H
/*
 * libc_internal.h --- private declarations shared across the split libc
 * translation units (Phase 4). NOT a public header: it declares the OS
 * syscall seam (asm _sys_*) and the soft-float runtime helpers the code
 * generator / libm provide. Each carved libc source includes this so it can
 * be compiled as its own object and archived into libc.a for dead-stripping.
 */
#include <stddef.h>

/* syscall seam --- asm PUBLIC _sys_* == c68k mangling of C sys_* (one '_'). */
extern int sys_write(int fd, const void *buf, int n);
extern int sys_read(int fd, void *buf, int n);
extern int sys_open(const char *path, int mode);
extern int sys_creat(const char *path, int attr);
extern int sys_close(int fd);
extern long sys_seek(int fd, long off, int whence);
extern int sys_unlink(const char *path);
extern void sys_exit(int code);
extern void *sys_sbrk(int delta);

/* soft-float runtime helpers (libm / libieee754d), used by %f/%e/%g and strtod. */
extern long fpdtol(double);
extern double floord(double);
extern double atod(const char *s);

#endif /* LIBC_INTERNAL_H */
