#ifndef _LIBGEN_H
#define _LIBGEN_H

/* POSIX pathname components.  dirname() is used by the preprocessor to
 * resolve #include paths relative to the including file; basename() is used
 * by the driver.  Both may modify the passed string (POSIX semantics). */
char *dirname(char *path);
char *basename(char *path);

#endif /* _LIBGEN_H */
