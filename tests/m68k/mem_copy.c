// c68k-expect: 42
void *memcpy(void*,const void*,unsigned); void *memset(void*,int,unsigned);
int main(void){ char b[8]; memset(b,0,8); char s[4]; s[0]=10;s[1]=20;s[2]=12;s[3]=0; memcpy(b,s,4); return b[0]+b[1]+b[2]+b[3]; }
