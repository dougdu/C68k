#ifndef _ALLOCA_H
#define _ALLOCA_H

/* alloca(size) allocates `size` bytes in the caller's stack frame; the storage
 * is released automatically when the function returns.  It is a compiler
 * builtin on c68k (the parser recognises `alloca` directly and also lowers VLAs
 * to it), so no library declaration is required -- this header exists for
 * source compatibility with code that includes it. */

#endif /* _ALLOCA_H */
