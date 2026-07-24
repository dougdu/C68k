#include <stdlib.h>
#include <errno.h>

/* The libheap SOA allocator returns pointers aligned to at least 8 bytes
 * (SOA_MIN_CLASS), which already covers max_align_t on m68k.  A pointer from
 * aligned_alloc must stay freeable with free(), and the heap has no
 * over-aligned primitive, so a stricter alignment than malloc's guarantee
 * cannot be honored (there is no hardware need for one on the 68000). */
#define MALLOC_ALIGN 8

void *aligned_alloc(size_t alignment, size_t size) {
  if (alignment == 0 || (alignment & (alignment - 1)) ||
      (size % alignment) != 0) {
    errno = EINVAL;
    return 0;
  }
  if (alignment > MALLOC_ALIGN) {
    errno = ENOMEM;
    return 0;
  }
  return malloc(size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
    return EINVAL;
  if (alignment > MALLOC_ALIGN)
    return ENOMEM;
  void *p = malloc(size ? size : 1);
  if (!p)
    return ENOMEM;
  *memptr = p;
  return 0;
}
