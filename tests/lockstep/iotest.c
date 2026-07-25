/* <stdio.h> additions: tmpnam, setvbuf/setbuf (mode + a caller-supplied
 * buffer), fgetpos/fsetpos (+ buffer-aware ftell), and freopen (both the path
 * form and the NULL-path in-place mode change).  Writes temp files on the local
 * drive and cleans up.  Prints "IOTEST PASS n/n" when every check holds. */
#include <stdio.h>
#include <string.h>

static int total, pass;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

int main(void) {
  char name[L_tmpnam], name2[L_tmpnam];

  /* tmpnam -> a usable, non-existent, 8.3-friendly name */
  CHECK(tmpnam(name) == name);
  CHECK(name[0] == 'T' && name[1] == 'M' && name[2] == 'P');

  /* write through a stream with unbuffered mode selected */
  FILE *fp = fopen(name, "wb");
  CHECK(fp != NULL);
  CHECK(setvbuf(fp, NULL, _IONBF, 0) == 0);
  CHECK(setvbuf(fp, NULL, 99, 0) == -1); /* bad mode rejected */
  CHECK(fwrite("0123456789", 1, 10, fp) == 10);
  CHECK(ftell(fp) == 10); /* buffer-aware position while writing */
  CHECK(fclose(fp) == 0);

  /* fgetpos / fsetpos round-trip on a buffered read stream */
  fp = fopen(name, "rb");
  CHECK(fp != NULL);
  CHECK(fgetc(fp) == '0');
  CHECK(fgetc(fp) == '1');
  fpos_t pos;
  CHECK(fgetpos(fp, &pos) == 0); /* logical offset 2 (not the fd's 10) */
  CHECK(ftell(fp) == 2);
  CHECK(fgetc(fp) == '2');
  CHECK(fgetc(fp) == '3');
  CHECK(fsetpos(fp, &pos) == 0); /* rewind to offset 2 */
  CHECK(fgetc(fp) == '2');       /* re-read the same byte */
  CHECK(ftell(fp) == 3);
  CHECK(fclose(fp) == 0);

  /* freopen: reuse one FILE object for a different file */
  tmpnam(name2);
  fp = fopen(name, "rb");
  CHECK(fp != NULL);
  FILE *fp2 = freopen(name2, "wb", fp); /* now writing name2 */
  CHECK(fp2 == fp);
  CHECK(fputs("hello", fp2) >= 0);
  CHECK(fclose(fp2) == 0);

  fp = fopen(name2, "rb");
  CHECK(fp != NULL);
  char b[8];
  CHECK(fread(b, 1, 5, fp) == 5);
  b[5] = 0;
  CHECK(!strcmp(b, "hello"));
  CHECK(fclose(fp) == 0);

  /* update mode ("wb+"): write, rewind within the same stream, read back */
  {
    FILE *u = fopen(name, "wb+");
    CHECK(u != NULL);
    CHECK(fwrite("update!", 1, 7, u) == 7);
    rewind(u);
    char ub[8];
    CHECK(fread(ub, 1, 7, u) == 7);
    ub[7] = 0;
    CHECK(!strcmp(ub, "update!"));
    CHECK(fclose(u) == 0);
  }

  /* setvbuf adopts a caller-supplied buffer: bytes buffered (no newline, under
     the buffer size) land in the caller's array before any flush -- proof the
     stream writes through it -- and still reach the file intact. */
  {
    FILE *sb = fopen(name, "wb");
    CHECK(sb != NULL);
    char mybuf[64];
    memset(mybuf, 0, sizeof mybuf);
    CHECK(setvbuf(sb, mybuf, _IOFBF, sizeof mybuf) == 0);
    CHECK(fwrite("hello", 1, 5, sb) == 5);   /* buffered into mybuf */
    CHECK(memcmp(mybuf, "hello", 5) == 0);   /* the caller buffer holds it */
    CHECK(fflush(sb) == 0);
    CHECK(fclose(sb) == 0);
    FILE *rb = fopen(name, "rb");
    CHECK(rb != NULL);
    char rbuf[8];
    CHECK(fread(rbuf, 1, 5, rb) == 5);
    rbuf[5] = 0;
    CHECK(!strcmp(rbuf, "hello"));
    CHECK(fclose(rb) == 0);
  }

  /* freopen(NULL, mode, fp): change an open stream's mode in place (this form
     previously returned NULL).  The fd is kept and the stream stays usable. */
  {
    FILE *rf = fopen(name, "rb"); /* name still holds "hello" */
    CHECK(rf != NULL);
    CHECK(fgetc(rf) == 'h');
    FILE *rf2 = freopen(NULL, "rb", rf);
    CHECK(rf2 == rf);
    rewind(rf2);
    char fb[8];
    CHECK(fread(fb, 1, 5, rf2) == 5);
    fb[5] = 0;
    CHECK(!strcmp(fb, "hello"));
    CHECK(fclose(rf2) == 0);
  }

  /* tmpfile: write, rewind, read back; removed automatically on close */
  {
    FILE *t = tmpfile();
    CHECK(t != NULL);
    CHECK(fwrite("hi!", 1, 3, t) == 3);
    rewind(t);
    char tb[4];
    CHECK(fread(tb, 1, 3, t) == 3);
    tb[3] = 0;
    CHECK(!strcmp(tb, "hi!"));
    CHECK(fclose(t) == 0);
  }

  /* printf %n: stores the running output length; emits nothing itself. The
     pointee width follows the length modifier (default int, hh/h/l/ll). */
  {
    char nb[16];
    int n = -1;
    CHECK(sprintf(nb, "abcd%n", &n) == 4 && n == 4);
    int mid = -1;
    CHECK(sprintf(nb, "AB%nCDE", &mid) == 5 && mid == 2); /* count mid-format */
    int n2 = -1;
    CHECK(snprintf(nb, sizeof nb, "%d%n!", 4200, &n2) == 5 && n2 == 4);
    long ln = -1;
    CHECK(sprintf(nb, "hi%ln", &ln) == 2 && ln == 2);
    short sn = -1;
    CHECK(sprintf(nb, "12345%hn", &sn) == 5 && sn == 5);
    signed char cn = -1;
    CHECK(sprintf(nb, "xyzzy%hhn", &cn) == 5 && cn == 5);
    long long lln = -1;
    CHECK(sprintf(nb, "%s%lln", "abcdef", &lln) == 6 && lln == 6);
  }

  remove(name);
  remove(name2);

  printf("IOTEST %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
