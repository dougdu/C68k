#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc_internal.h"

/* =====================================================================
 * <stdlib.h> allocator -- a bump allocator over sys_sbrk kept as one
 * cohesive core object (it shares g_heap_top).  free is a no-op, but realloc
 * grows the top-of-heap block in place and __heap_mark/__heap_release give
 * arena semantics (used to reclaim the front-end on the tiny CP/M heap).
 * Phase 5 replaces this with the libheap shims.
 * ===================================================================== */
static char *g_heap_top = 0; /* end of the most-recent bump allocation */

void *malloc(size_t n) {
  size_t total = (n + 4 + 3) & ~(size_t)3; /* 4-byte size header, rounded */
  char *p = sys_sbrk((int)total);
  if (p == (char *)-1) {
    errno = ENOMEM;
    return NULL;
  }
  g_heap_top = p + total;
  *(size_t *)p = n;
  return p + 4;
}

/* free is a no-op except that realloc/__heap_release can reclaim the
 * top-of-heap block; a general free-list is unnecessary for the compiler's
 * allocate-mostly workload and would risk exposing latent use-after-free. */
void free(void *p) { (void)p; }

void *calloc(size_t nmemb, size_t size) {
  size_t n = nmemb * size;
  void *p = malloc(n);
  if (p)
    memset(p, 0, n);
  return p;
}

/* Grow/shrink the top-of-heap block in place; else copy to a fresh block.
 * The in-place case eliminates the doubling leak of growable arrays. */
void *realloc(void *p, size_t n) {
  if (!p)
    return malloc(n);
  char *hdr = (char *)p - 4;
  size_t old = *(size_t *)hdr;
  size_t oldtotal = (old + 4 + 3) & ~(size_t)3;
  if (hdr + oldtotal == g_heap_top) {
    size_t newtotal = (n + 4 + 3) & ~(size_t)3;
    if (newtotal != oldtotal &&
        sys_sbrk((int)(newtotal - oldtotal)) == (char *)-1)
      return NULL;
    g_heap_top = hdr + newtotal;
    *(size_t *)hdr = n;
    return p;
  }
  void *q = malloc(n);
  if (q)
    memcpy(q, p, old < n ? old : n);
  return q;
}

/* Arena mark/release over the bump heap. __heap_mark captures the current
 * break; __heap_release rolls it back, freeing everything allocated since.
 * Used by the native driver to reclaim the whole front-end (tokens, AST,
 * codegen buffer) after cc1 writes the .s and before the integrated
 * assembler runs -- so the assembler starts from a nearly empty heap. */
void *__heap_mark(void) { return sys_sbrk(0); }

void __heap_release(void *mark) {
  char *cur = sys_sbrk(0);
  if ((char *)mark >= cur)
    return;
  size_t back = (size_t)(cur - (char *)mark);
  sys_sbrk(-(int)back); /* negative delta rolls the break back */
  g_heap_top = (char *)mark;
}
