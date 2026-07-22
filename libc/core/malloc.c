#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc_internal.h"

/* =====================================================================
 * <stdlib.h> allocator -- thin shims over the vendored SOA heap
 * (lib/heap/libheap.a).  free() really reclaims, matching every other
 * real-world C library.
 *
 * The machine heap is created lazily on first use over the whole sbrk arena
 * the crt0 seam reserved (c_heapbase/c_heaplen, exposed via sys_heapavail()).
 * malloc/calloc draw from the "current" heap -- normally the machine heap, but
 * a scratch arena while one is open (heap_arena.c); free/realloc find a
 * block's owning heap by address range, so pointers that cross the arena
 * boundary are always freed against the heap that owns them.
 * ===================================================================== */

/* libheap public entry points (C stack ABI; asm _HeapXxx == C HeapXxx). */
extern void *GetMachineHeap(void);
extern void *MachineHeapInitialize(long lOptions, void *lStart, long lSize);
extern void *HeapAlloc(void *hHeap, long lFlags, long lBytes);
extern int HeapFree(void *hHeap, void *pMem);
extern void *HeapReAlloc(void *hHeap, long lFlags, void *pMem, long lBytes);

/* Scratch-arena routing state, shared with heap_arena.c.  The mark/release
 * arena lives in its own object so a program that only calls malloc never
 * drags in HeapCreate/HeapDestroy/HeapCompact.  While _heap_arena is NULL the
 * machine heap serves every request; while it is set, new allocations draw
 * from it and any pointer in [_heap_arena_lo, _heap_arena_hi) belongs to it. */
void *_heap_machine = 0; /* the machine heap, once created */
void *_heap_arena = 0;   /* open scratch arena, or NULL */
char *_heap_arena_lo = 0;
char *_heap_arena_hi = 0;

/* Lazily create the machine heap over the remaining sbrk arena, once. */
void *_heap_machine_get(void) {
  if (_heap_machine)
    return _heap_machine;
  void *h = GetMachineHeap();
  if (h) {
    _heap_machine = h;
    return h;
  }
  char *base = (char *)sys_sbrk(0); /* start of the free arena */
  int avail = sys_heapavail();      /* bytes from the break to the arena top */
  if (avail <= 0)
    return NULL;
  if (sys_sbrk(avail) == (void *)-1) /* hand the whole arena to libheap */
    return NULL;
  _heap_machine = MachineHeapInitialize(0, base, avail);
  return _heap_machine;
}

/* Heap that new allocations draw from: the open scratch arena, else machine. */
static void *cur_heap(void) {
  return _heap_arena ? _heap_arena : _heap_machine_get();
}

/* Owning heap of an existing block: the arena if the pointer lies within its
 * one contiguous block, else the machine heap. */
static void *owner_heap(void *p) {
  if (_heap_arena && (char *)p >= _heap_arena_lo && (char *)p < _heap_arena_hi)
    return _heap_arena;
  return _heap_machine_get();
}

void *malloc(size_t n) {
  if (n == 0)
    n = 1; /* return a unique, freeable pointer */
  void *h = cur_heap();
  void *p = h ? HeapAlloc(h, 0, (long)n) : NULL;
  if (!p)
    errno = ENOMEM;
  return p;
}

void free(void *p) {
  if (p)
    HeapFree(owner_heap(p), p);
}

void *calloc(size_t nmemb, size_t size) {
  if (size && nmemb > (size_t)-1 / size) { /* multiplication overflow */
    errno = ENOMEM;
    return NULL;
  }
  size_t n = nmemb * size;
  void *p = malloc(n);
  if (p)
    memset(p, 0, n);
  return p;
}

void *realloc(void *p, size_t n) {
  if (!p)
    return malloc(n);
  if (n == 0) {
    free(p);
    return NULL;
  }
  void *h = owner_heap(p);
  void *q = h ? HeapReAlloc(h, 0, p, (long)n) : NULL;
  if (!q)
    errno = ENOMEM; /* HeapReAlloc leaves the original block intact on failure */
  return q;
}

/* __heap_mark/__heap_release live in heap_arena.c so this object stays free of
 * HeapCreate/HeapDestroy/HeapCompact for the common malloc-only program. */

