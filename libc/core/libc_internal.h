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
#include <stdio.h>  /* FILE, for the shared stream table and _psink sink */
#include <stdarg.h> /* va_list, for _vformat */

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
extern int sys_heapavail(void); /* bytes from the break to the arena top (libheap malloc) */

/* ---- libheap allocator scratch-arena state -----------------------------
 * Shared between malloc.c (defines them, routes malloc/free/realloc) and
 * heap_arena.c (opens/destroys the arena in __heap_mark/__heap_release). */
extern void *_heap_machine;  /* the machine heap, once created */
extern void *_heap_arena;    /* open scratch arena, or NULL */
extern char *_heap_arena_lo; /* arena block address range: [lo, hi) */
extern char *_heap_arena_hi;
void *_heap_machine_get(void); /* lazily create/return the machine heap */

/* soft-float runtime helpers (libm / libieee754d), used by %f/%e/%g and strtod. */
extern long fpdtol(double);
extern double floord(double);
extern double atod(const char *s);

/* ---- shared stdio state -------------------------------------------------
 * The process stream table lives in one cohesive core object (stdio_core.c);
 * fopen/open_memstream claim slots, fflush(NULL) walks it, and stdin/stdout/
 * stderr alias its first three entries.  Declared here so each split stdio
 * function is its own dead-strippable object yet shares the one table. */
#define NSTREAM 11
extern FILE _streams[NSTREAM];

/* ---- printf/scanf formatting core (vformat.c) ---------------------------
 * Every printf-family entry point builds a _psink (either a FILE sink or a
 * bounded buffer) and calls _vformat; keeping the engine in one object lets
 * the thin printf/sprintf/... wrappers strip independently. */
typedef struct {
  FILE *fp;
  char *buf;
  int cap;
  int len;
} _psink;
int _vformat(_psink *s, const char *fmt, va_list ap);

#endif /* LIBC_INTERNAL_H */
