// c68k-expect: 255
int main(void){ unsigned long long a=0x000000FF00000000ULL, b=0x100000000ULL; return (int)(a/b); }
