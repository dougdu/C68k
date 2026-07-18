// c68k-expect: 42
int main(void){ int x=5; int *p=&x; *p=42; return x; }
