/*
 * c99test.c --- C99 language conformance battery (lockstep, both OSes).
 * Exercises the front-end features called out in P6 against the m68k code
 * generator. Prints "C99 PASS n/n" when every check holds.
 */
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
static void check(int cond, const char *what) {
  if (cond)
    g_pass++;
  else {
    g_fail++;
    printf("FAIL: %s\n", what);
  }
}

/* enums */
enum Color { RED, GREEN = 5, BLUE };

/* typedef + struct */
typedef struct Pt {
  int x, y;
} Pt;

/* nested aggregate */
struct Mixed {
  Pt p;
  int a[3];
  char name[4];
};

/* bitfields (big-endian ILP32) */
struct Bits {
  unsigned a : 3;
  unsigned b : 5;
  unsigned c : 8;
  int s : 4; /* signed bitfield */
};

/* flexible array member */
struct Flex {
  int n;
  char data[];
};

static int apply(int (*fn)(int, int), int a, int b) { return fn(a, b); }
static int addf(int a, int b) { return a + b; }
static int mulf(int a, int b) { return a * b; }

int main(void) {
  /* --- enums --- */
  check(RED == 0 && GREEN == 5 && BLUE == 6, "enum");

  /* --- designated initializers (struct) --- */
  {
    Pt p = {.y = 20, .x = 22};
    check(p.x == 22 && p.y == 20, "desig-struct");
  }
  /* --- designated initializers (array) --- */
  {
    int arr[6] = {[1] = 10, [3] = 30, [5] = 50};
    check(arr[0] == 0 && arr[1] == 10 && arr[3] == 30 && arr[5] == 50,
          "desig-array");
  }
  /* --- nested aggregate init --- */
  {
    struct Mixed m = {{1, 2}, {3, 4, 5}, "hi"};
    check(m.p.x == 1 && m.p.y == 2 && m.a[2] == 5 && m.name[0] == 'h' &&
              m.name[1] == 'i' && m.name[2] == 0,
          "nested-init");
  }
  /* --- compound literals --- */
  {
    Pt p = (Pt){7, 35};
    check(p.x + p.y == 42, "compound-lit");
  }
  {
    int *a = (int[]){10, 20, 12};
    check(a[0] + a[1] + a[2] == 42, "compound-lit-array");
  }

  /* --- _Bool --- */
  {
    _Bool t = 5, f = 0;
    check(t == 1 && f == 0 && sizeof(_Bool) == 1, "bool");
  }

  /* --- bitfields (write/read round-trip; big-endian) --- */
  {
    struct Bits bf;
    bf.a = 5;   /* 3 bits -> 5 */
    bf.b = 20;  /* 5 bits -> 20 */
    bf.c = 200; /* 8 bits -> 200 */
    bf.s = -3;  /* signed 4-bit -> -3 */
    check(bf.a == 5 && bf.b == 20 && bf.c == 200 && bf.s == -3, "bitfield");
    bf.a = 9; /* overflow 3 bits -> 1 */
    check(bf.a == 1, "bitfield-trunc");
  }

  /* --- flexible array member --- */
  {
    static char storage[sizeof(struct Flex) + 4];
    struct Flex *fx = (struct Flex *)storage;
    fx->n = 3;
    fx->data[0] = 10;
    fx->data[1] = 20;
    fx->data[2] = 12;
    check(fx->n == 3 && fx->data[0] + fx->data[1] + fx->data[2] == 42,
          "flex-array");
  }

  /* --- compound assignment + inc/dec --- */
  {
    int x = 10;
    x += 5;
    x *= 2;
    x -= 3;
    x /= 3;
    x <<= 2;
    x |= 1;
    check(x == 37, "compound-assign");
  }
  {
    int i = 5, a, b;
    a = i++;
    b = ++i;
    check(a == 5 && b == 7 && i == 7, "inc-dec");
  }

  /* --- multi-dimensional arrays --- */
  {
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
    check(m[0][0] == 1 && m[1][2] == 6 && m[1][0] == 4, "2d-array");
  }

  /* --- function pointers --- */
  {
    int (*fp)(int, int) = addf;
    check(fp(40, 2) == 42 && apply(mulf, 6, 7) == 42, "fn-pointer");
  }

  /* --- ternary/comma/sizeof --- */
  {
    int x = (1, 2, 42);
    int y = sizeof(int) + sizeof(long long) + sizeof(char);
    check(x == 42 && y == 13, "comma-sizeof");
  }

  /* --- switch with fallthrough + goto --- */
  {
    int r = 0, v = 2;
    switch (v) {
    case 1:
      r += 1;
    case 2:
      r += 10;
    case 3:
      r += 100;
      break;
    default:
      r = -1;
    }
    check(r == 110, "switch-fallthrough");
  }

  /* --- char/string escapes --- */
  {
    char s[] = "a\tb\nc\\d\"e";
    check(s[1] == '\t' && s[3] == '\n' && s[5] == '\\' && s[7] == '"',
          "escapes");
  }

  /* --- const + pointer const --- */
  {
    const int ci = 42;
    const int *p = &ci;
    check(*p == 42, "const");
  }

  if (g_fail == 0)
    printf("C99 PASS %d/%d\n", g_pass, g_pass);
  else
    printf("C99 FAIL %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail;
}
