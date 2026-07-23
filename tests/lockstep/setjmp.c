/* <setjmp.h> conformance.  Per C99 7.13.2.1, setjmp may appear only as the
 * controlling expression of a selection/iteration statement or compared to a
 * constant -- never assigned to a variable -- so every use below is a switch
 * or an `if (... != k)`.  Prints "SETJMP PASS n/n" when every check holds. */
#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;
static int total, pass;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

static volatile int visited;

/* longjmp from a nested call: the discarded frame proves the stack unwinds. */
static void deep(void) {
  volatile char pad[64];
  pad[0] = 42;
  longjmp(env, pad[0]);
  CHECK(0); /* unreachable */
}

int main(void) {
  /* 1. direct setjmp is 0; longjmp(env,5) re-enters with 5 */
  visited = 0;
  switch (setjmp(env)) {
  case 0:
    visited = 1;
    longjmp(env, 5);
    CHECK(0); /* unreachable */
    break;
  case 5:
    CHECK(visited == 1);
    break;
  default:
    CHECK(0); /* wrong value */
  }

  /* 2. longjmp(env,0) must make setjmp yield 1 */
  visited = 0;
  switch (setjmp(env)) {
  case 0:
    visited = 1;
    longjmp(env, 0);
    CHECK(0); /* unreachable */
    break;
  case 1:
    CHECK(visited == 1);
    break;
  default:
    CHECK(0);
  }

  /* 3. longjmp out of a nested call (stack unwind) */
  visited = 0;
  switch (setjmp(env)) {
  case 0:
    visited = 1;
    deep();
    CHECK(0); /* unreachable */
    break;
  case 42:
    CHECK(visited == 1);
    break;
  default:
    CHECK(0);
  }

  /* 4. if/compare context: retry loop until the sentinel value */
  visited = 0;
  if (setjmp(env) != 99) {
    visited = visited + 1;
    if (visited < 3)
      longjmp(env, 1); /* loop again */
    longjmp(env, 99);  /* exit the loop */
  }
  CHECK(visited == 3);

  /* 5. switch controlling expression, multi-longjmp loop */
  visited = 0;
  switch (setjmp(env)) {
  case 0:
  case 1:
    visited = visited + 1;
    if (visited < 3)
      longjmp(env, 1);
    break;
  default:
    CHECK(0);
  }
  CHECK(visited == 3);

  /* 6. common idiom: assign the setjmp result, then loop on it */
  visited = 0;
  {
    volatile int rv;
    rv = setjmp(env);
    visited = visited + 1;
    if (rv == 0)
      longjmp(env, 7);
    if (rv == 7)
      longjmp(env, 8);
    CHECK(rv == 8 && visited == 3);
  }

  printf("SETJMP %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
