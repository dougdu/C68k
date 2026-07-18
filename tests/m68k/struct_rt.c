// c68k-expect: 42
struct P { int x, y; };
static struct P add1(struct P p){ p.x=p.x+1; p.y=p.y+1; return p; }
int main(void){ struct P p; p.x=20; p.y=20; struct P q=add1(p); return q.x+q.y; }
