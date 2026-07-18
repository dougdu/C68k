/*
 * coretest.c --- self-checking lockstep battery for the P2-P5 language/runtime
 * surface (the console-output analogue of the bare-metal tests/m68k suite).
 * Runs on both Osiris (.PRG) and CP/M-68K (.68K); prints "SUITE PASS n/n" when
 * every check holds. tools/run-lockstep.ps1 asserts that line on both OSes.
 */
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

static void check(int cond, const char *what) {
  if (cond) {
    g_pass++;
  } else {
    g_fail++;
    printf("FAIL: %s\n", what);
  }
}

struct Pt {
  int x, y;
};
struct Nest {
  struct Pt a;
  int tag;
};
union U {
  int i;
  unsigned char b[4];
};

static int sum_pt(struct Pt p) { return p.x + p.y; }
static struct Pt mk_pt(int a, int b) {
  struct Pt p;
  p.x = a;
  p.y = b;
  return p;
}
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static int addall(int a, struct Pt p, int b) { return a + p.x + p.y + b; }

int main(void) {
  /* integer arithmetic */
  check(10 + 32 == 42, "add");
  check(100 - 58 == 42, "sub");
  check(6 * 7 == 42, "mul");
  check(1000 / 17 == 58, "div");
  check(1000 % 17 == 14, "mod");
  check(-7 / 2 == -3, "sdiv");
  check(-7 % 2 == -1, "smod");

  /* bitwise + shifts */
  check((0x2A & 0x0F) == 0x0A, "and");
  check((0x20 | 0x0A) == 0x2A, "or");
  check((0xFF ^ 0xD5) == 0x2A, "xor");
  check((~0 & 0xFF) == 0xFF, "not");
  check((1 << 5) == 32, "shl");
  check((1024 >> 4) == 64, "shr");
  check(((unsigned)0x80000000u >> 28) == 8, "ushr");
  check((-256 >> 4) == -16, "ashr");

  /* comparisons + logic + ternary */
  check((3 < 5) && (5 <= 5) && (7 > 2) && (2 >= 2), "cmp");
  check(!(0) && !!(5), "logical");
  check((5 ? 42 : 0) == 42, "ternary");

  /* control flow */
  {
    int s = 0, i;
    for (i = 0; i < 10; i++)
      s += i;
    check(s == 45, "for");
  }
  {
    int n = 1, c = 0;
    while (n < 100) {
      n *= 2;
      c++;
    }
    check(c == 7, "while");
  }
  {
    int n = 0, s = 0;
    do {
      s += n++;
    } while (n < 5);
    check(s == 10, "do");
  }
  {
    int r, v = 2;
    switch (v) {
    case 1:
      r = 10;
      break;
    case 2:
      r = 20;
      break;
    default:
      r = 99;
    }
    check(r == 20, "switch");
  }
  check(fib(10) == 55, "recursion");

  /* pointers + arrays */
  {
    int x = 5;
    int *p = &x;
    *p = 42;
    check(x == 42, "pointer");
  }
  {
    int a[4] = {1, 2, 3, 4};
    check(a[0] + a[3] == 5 && *(a + 2) == 3, "array");
  }

  /* structs + unions */
  {
    struct Pt p;
    p.x = 40;
    p.y = 2;
    check(sum_pt(p) == 42, "struct-arg");
  }
  {
    struct Pt p = mk_pt(40, 2);
    check(p.x + p.y == 42, "struct-ret");
  }
  {
    struct Pt p;
    p.x = 10;
    p.y = 20;
    check(addall(5, p, 7) == 42, "struct-mixed");
  }
  {
    struct Nest n;
    n.a.x = 30;
    n.a.y = 12;
    n.tag = 7;
    check(n.a.x + n.a.y == 42 && n.tag == 7, "nested-struct");
  }
  {
    union U u;
    u.i = 0;
    u.b[0] = 1; /* big-endian: high byte */
    check(u.i == 0x01000000, "union-endian");
  }

  /* long long */
  {
    long long a = 0x100000000LL;
    check((int)(a + 42) == 42, "ll-add");
  }
  {
    long long a = 1000000000000LL;
    check((int)(a / 1000000LL) == 1000000, "ll-div");
  }
  {
    long long a = 5000000000LL, b = 4000000000LL;
    check(a > b, "ll-cmp");
  }
  {
    long long a = 1LL << 40;
    check((int)(a >> 32) == 256, "ll-shift");
  }

  /* float / double */
  {
    float f = 6.0f;
    check((int)(f * 7.0f) == 42, "float-mul");
  }
  {
    double d = 355.0 / 113.0;
    check((int)(d * 100.0) == 314, "double-div");
  }
  {
    int i = 42;
    float f = i;
    check((int)f == 42, "int-to-float");
  }

  /* string */
  check(strlen("hello") == 5, "strlen");
  check(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0, "strcmp");
  {
    char buf[8];
    strcpy(buf, "hi");
    check(buf[0] == 'h' && buf[2] == 0, "strcpy");
  }

  /* printf round-trip via snprintf */
  {
    char buf[32];
    snprintf(buf, sizeof buf, "%d/%x/%lld", 42, 255, 1000000000000LL);
    check(strcmp(buf, "42/ff/1000000000000") == 0, "snprintf");
  }

  if (g_fail == 0)
    printf("SUITE PASS %d/%d\n", g_pass, g_pass);
  else
    printf("SUITE FAIL %d/%d\n", g_pass, g_pass + g_fail);
  return g_fail;
}
