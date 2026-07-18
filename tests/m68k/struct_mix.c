// c68k-expect: 42
struct P { int x, y; };
static int f(int a, struct P p, int b){ return a + p.x + p.y + b; }
int main(void){ struct P p; p.x=10; p.y=20; return f(5, p, 7); }
