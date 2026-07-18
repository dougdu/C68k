// c68k-expect: 30
int g=10;
static int dbl(int x){return x+x;}
int main(void){ g=dbl(g)+g; return g; }
