// c68k-expect: 42
struct P { int x, y; };
static int sum(struct P p){ return p.x + p.y; }
int main(void){ struct P p; p.x=40; p.y=2; return sum(p); }
