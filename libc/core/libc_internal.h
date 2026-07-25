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
extern int sys_rename(const char *oldp, const char *newp);
extern int sys_dup(int fd);              /* Osiris DOS 45h; -1 on CP/M (no fd dup) */
extern int sys_dup2(int oldfd, int newfd); /* Osiris DOS 46h; -1 on CP/M */
extern int sys_isatty(int fd);           /* 1 if fd is a console/char device */
extern int sys_access(const char *path); /* DOS attr word (bit0=read-only), or -1 if absent */
extern int sys_findfirst(const char *path, int attr, void *dta); /* DOS 4Eh find block; 0/-1 */
extern int sys_findnext(void *dta);      /* DOS 4Fh; 0/-1 */
extern int sys_getfiletime(int fd);      /* (date<<16)|time packed, or -1 */
extern int sys_setfiletime(int fd, long dtstamp); /* DOS 57h/01; 0/-1 */
extern long sys_fdsize(int fd);          /* file size in bytes (fstat), or -1 */
extern int sys_mkdir(const char *path);  /* DOS 39h; -1 on CP/M (no subdirs) */
extern int sys_rmdir(const char *path);  /* DOS 3Ah; -1 on CP/M */
extern int sys_chdir(const char *path);  /* DOS 3Bh; -1 on CP/M */
extern int sys_getcwd(char *buf);        /* fills "X:\\..."; 0/-1 */
extern int sys_chmod(const char *path, int attr); /* DOS 43h/01 (bit0=RO); 0/-1 */
extern char *sys_getenv(const char *name); /* Osiris DOS 64h; NULL on CP/M (no env) */
extern int sys_setenv(const char *name, const char *value); /* Osiris 64h.01; -1 on CP/M */
extern char *sys_getenvblk(void); /* Osiris 64h.02 env block; NULL on CP/M */
char **envbuild(void); /* (re)build environ[] from the OS env block; called by crt0 + setenv */
extern int sys_conin(void);       /* raw console char, no echo (blocking) */
extern int sys_constat(void);     /* 1 if a key is ready, else 0 */
extern long sys_doscall(void *r); /* Osiris TRAP #1 escape hatch; -1 on CP/M */
extern long cpm_bdos(int func, long param); /* CP/M TRAP #2; -1 stub on Osiris */
extern int sys_lasterror(void);   /* Osiris DOS 59h extended error code; 0 on CP/M */
extern int sys_getcountry(int code, void *buf); /* Osiris DOS 38h country block; -1 on CP/M */
extern int sys_getcolltab(void *buf); /* Osiris DOS 65h/06 collating weights (256 bytes); -1 on CP/M */
int __oserr_to_errno(int code);   /* map a DOS/OS error code to an errno value */
int __oserrno(void);              /* __oserr_to_errno(sys_lasterror()); for seam wrappers */
extern int sys_exec(const char *path, void *parmblk); /* Osiris DOS 4Bh EXEC; -1 on CP/M */
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
FILE *_fopen_fp(FILE *fp, const char *path, const char *mode); /* fopen/freopen core */

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

/* Numeric/float formatting primitives shared by the narrow (_vformat) and wide
 * (_vwformat) engines; defined in vformat.c. */
int _u64toa(unsigned long long v, int base, int upper, char *out);
int _fmt_fixed(double v, int prec, char *buf);
int _fmt_sci(double v, int prec, char *buf);
int _fmt_gen(double v, int prec, char *buf);
int _fmt_hex(double v, int prec, int upper, char *buf);

/* Wide formatted output (Tier B): the same engine over a wchar_t sink that is
 * either a wide-oriented FILE (fputwc) or a wchar_t buffer.  Numeric
 * conversions reuse the narrow fmt_* helpers (ASCII) and widen each digit. */
typedef struct {
  FILE *fp;
  wchar_t *buf;
  int cap;
  int len;
} _pwsink;
int _vwformat(_pwsink *s, const wchar_t *fmt, va_list ap);

/* ---- scanf/fscanf/sscanf core (vsscanf.c) -------------------------------
 * The scanner runs over a _scan source that is either a FILE stream
 * (scanf/fscanf) or a NUL-terminated string (sscanf), reading one character
 * at a time with a single character of pushback so each conversion consumes
 * exactly what it matches and leaves the rest for the next read. */
typedef struct {
  FILE *fp;      /* stream source, or NULL for a string */
  const char *s; /* string cursor, or NULL for a stream */
  long nread;    /* characters consumed (for %n and EOF detection) */
} _scan;
int _vscan(_scan *z, const char *fmt, va_list ap);

/* ---- wide scanf/fwscanf/swscanf core (vwscanf.c) ------------------------
 * Tier C: the wide scanner mirrors _vscan over a wchar_t source that is
 * either a wide-oriented FILE (fgetwc/ungetwc) or a NUL-terminated wchar_t
 * string.  %c/%s/%[ store a multibyte char array by default and a wchar_t
 * array under the l modifier.  In its own object so narrow-scanf programs
 * never link the wide engine (dead-stripping). */
typedef struct {
  FILE *fp;         /* stream source, or NULL for a wide string */
  const wchar_t *s; /* wide string cursor, or NULL for a stream */
  long nread;       /* wide characters consumed (for %n / EOF detection) */
} _wscan;
int _vwscan(_wscan *z, const wchar_t *fmt, va_list ap);

/* ---- LC_COLLATE collation weights (strcoll.c / locale.c) ----------------
 * NULL in the "C"/"POSIX" locale (and wherever no OS collating table exists,
 * e.g. CP/M-68K) -> strcoll is a byte compare / strxfrm an identity copy.  In
 * the native ("") locale on Osiris, setlocale loads the OS 65h AL=6 collating
 * sequence here: a 256-entry table mapping a byte value to its collation
 * weight.  Defined in strcoll.c (the consumer) so a strcoll-only program stays
 * self-contained. */
extern const unsigned char *_coll_weights;

#endif /* LIBC_INTERNAL_H */
