// c68k-expect: 1234
int main(void){ long long a=1234567890123LL; long long b=-a; b=-b; return (int)(b/1000000000LL); }
