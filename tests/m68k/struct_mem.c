// c68k-expect: 42
struct M { char a; int b; short c; };
static int sum(struct M m){ return m.a + m.b + m.c; }
int main(void){ struct M m; m.a=2; m.b=30; m.c=10; return sum(m); }
