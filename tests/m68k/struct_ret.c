// c68k-expect: 42
struct P { int x, y; };
static struct P mk(int a,int b){ struct P p; p.x=a; p.y=b; return p; }
int main(void){ struct P p = mk(40,2); return p.x + p.y; }
