// c68k-expect: 20
static int f(int n){ switch(n){ case 1: return 10; case 2: return 20; default: return 99; } }
int main(void){ return f(2); }
