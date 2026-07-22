/*
 * heap_arena.c -- arena mark/release for the default libheap allocator.
 *
 * The self-host driver (src/main.c, C68K_SELFHOST) brackets a single-file
 * cc1 run with __heap_mark()/__heap_release().  cc1 is arena-style: it never
 * frees its tokens/AST/codegen, so rather than leak them onto the machine
 * heap we hand cc1 a private scratch heap and destroy it wholesale once the
 * assembly has been flushed to the temp .s on disk.  This is an O(1) bulk
 * reclaim of the whole compile arena, layered on the real, reclaiming heap.
 *
 * Kept in its own object so ordinary malloc/free/realloc users never link
 * HeapCreate/HeapDestroy/HeapCompact: only the compiler references
 * __heap_mark, so only the compiler pays for the arena machinery.
 */
#include <stddef.h>
#include "libc_internal.h"

/* libheap private-heap API (C stack ABI; asm _HeapXxx == C HeapXxx). */
extern void *HeapCreate(long lOptions, long lSize);
extern int HeapDestroy(void *hHeap);
extern long HeapCompact(void *hHeap); /* size of the largest free block */

/* heap.inc: a heap header stores its total actual size (bytes) at this
 * offset -- used to bound the arena's address range exactly. */
#define HH_SIZE_OFF 0x0C
/* Headroom left in the parent (machine) heap when carving the arena: covers
 * HeapCreate's own headers/alignment and keeps the machine heap non-empty. */
#define ARENA_SLACK 4096
/* Below this a split isn't worthwhile; cc1 just uses the machine heap. */
#define ARENA_MIN (16 * 1024)

/* Open a scratch arena over (almost) all free machine-heap space and route
 * subsequent allocations to it.  Returns the arena handle, or NULL if the
 * heap is too tight to split (cc1 then allocates straight from the machine
 * heap -- still correct, just without the bulk reclaim). */
void *__heap_mark(void) {
  if (_heap_arena)
    return _heap_arena; /* the driver never nests marks */
  void *m = _heap_machine_get();
  if (!m)
    return NULL;
  long freeb = HeapCompact(m); /* coalesce, then report the largest free block */
  long size = freeb - ARENA_SLACK;
  void *a = NULL;
  while (size >= ARENA_MIN) { /* back off until the carve fits */
    a = HeapCreate(0, size);
    if (a)
      break;
    size -= size / 4; /* shrink ~25% and retry */
  }
  if (!a)
    return NULL;
  _heap_arena = a;
  _heap_arena_lo = (char *)a;
  _heap_arena_hi = (char *)a + *(long *)((char *)a + HH_SIZE_OFF);
  return a;
}

/* Destroy the scratch arena, freeing every allocation cc1 made since the
 * matching __heap_mark back to the machine heap in one shot. */
void __heap_release(void *mark) {
  (void)mark;
  if (!_heap_arena)
    return;
  void *a = _heap_arena;
  _heap_arena = NULL;
  _heap_arena_lo = NULL;
  _heap_arena_hi = NULL;
  HeapDestroy(a);
}
