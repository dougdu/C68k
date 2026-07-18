// c68k-expect: 42
struct P { int a, b, c; };
int main(void){ struct P x; x.a=10; x.b=20; x.c=12; struct P y; y=x; return y.a+y.b+y.c; }
