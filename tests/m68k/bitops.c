// c68k-expect: 42
int main(void){ int a=0x2A; int b=(a&0x0F)|(a&0x30); int c=b^0; return (c<<1)>>1; }
