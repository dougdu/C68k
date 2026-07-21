#include <stdlib.h>

/* rand/srand share the LCG state, so they stay in one object. */
static unsigned long _rand_state = 1;

void srand(unsigned seed) { _rand_state = seed; }

int rand(void) {
  _rand_state = _rand_state * 1103515245UL + 12345UL;
  return (int)((_rand_state >> 16) & 0x7FFF);
}
