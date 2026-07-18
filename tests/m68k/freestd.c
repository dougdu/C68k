// c68k-expect: 42
//
// Freestanding-mode validation. Uses ONLY the C99 freestanding headers
// (<stddef.h>, <stdint.h>, <limits.h>, <stdbool.h>, <stdarg.h>, <iso646.h>)
// and links against no libc -- just crt0 + the runtime lib. The harness
// compiles this with -ffreestanding, so __STDC_HOSTED__ must be 0.
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <iso646.h>

#if __STDC_HOSTED__ != 0
#error "expected freestanding mode (__STDC_HOSTED__ == 0)"
#endif

struct pair {
  char c;
  int32_t v;
};

static int vsum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int s = 0;
  for (int i = 0; i < n; i++)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}

int main(void) {
  int acc = 0;

  // <stdint.h> fixed-width values vs <limits.h> bounds.
  int32_t a = INT32_MAX;
  uint8_t b = UINT8_MAX;
  if (a == INT_MAX)
    acc += 10;
  if (b == UCHAR_MAX)
    acc += 5;

  // <limits.h> + sizeof, spelled with <iso646.h> operator words.
  if (CHAR_BIT == 8 and sizeof(long long) == 8)
    acc += 7;

  // <stdbool.h>.
  bool t = true, f = false;
  if (t and not f)
    acc += 8;

  // <stddef.h> offsetof (int aligns to 2 on m68k: c@0, pad@1, v@2).
  if (offsetof(struct pair, v) == 2)
    acc += 4;

  // <stdarg.h> variadic, no libc.
  if (vsum(3, 10, 20, 30) == 60)
    acc += 8;

  return acc; // 10 + 5 + 7 + 8 + 4 + 8 = 42
}
