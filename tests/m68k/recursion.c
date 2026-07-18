// c68k-expect: 55
static int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(void){ return fib(10); }
