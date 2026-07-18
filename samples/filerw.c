#include <stdio.h>
#include <string.h>

/* Write a file, read it back, and echo it -- exercises the Osiris handle
   file system through the libc stdio layer. */
int main(void) {
  const char *msg = "c68k file I/O works\n";

  FILE *f = fopen("TEST.TXT", "w");
  if (!f) {
    puts("fopen w failed");
    return 1;
  }
  fwrite(msg, 1, strlen(msg), f);
  fclose(f);

  f = fopen("TEST.TXT", "r");
  if (!f) {
    puts("fopen r failed");
    return 1;
  }
  char buf[64];
  char *p = fgets(buf, sizeof buf, f);
  fclose(f);

  if (p)
    fputs(buf, stdout);
  else
    puts("fgets failed");
  return 0;
}
