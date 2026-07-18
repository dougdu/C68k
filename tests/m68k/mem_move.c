// c68k-expect: 42
void *memmove(void*,const void*,unsigned);
int main(void){ char b[6]; b[0]=42;b[1]=1;b[2]=2;b[3]=3;b[4]=4;b[5]=5; memmove(b+1,b,5); return b[1]; }
