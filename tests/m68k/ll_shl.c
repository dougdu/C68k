// c68k-expect: 256
int main(void){ long long a=1LL; a=a<<40; return (int)(a>>32); }
