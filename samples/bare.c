/* Bare native-link probe: uses only the crt0/seam (sys_write), no libc. */
extern int sys_write(int fd, const void *buf, int n);
int main(void) {
  sys_write(1, "BARE NATIVE OK\r\n", 16);
  return 0;
}
