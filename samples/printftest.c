#include <stdio.h>

int main(void) {
  printf("int=%d hex=%x str=%s char=%c\n", 42, 255, "abc", 'Z');
  printf("pad=[%5d] zero=[%04d] left=[%-5d] neg=%d\n", 42, 42, 42, -7);
  long long big = 1000000000000LL;
  printf("ll=%lld u=%u\n", big, 4000000000U);
  return 0;
}
