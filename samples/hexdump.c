#include <stdio.h>

/* c68k hexdump -- a real Osiris / CP/M-68K utility built with the c68k SDK.
 *
 *   HEXDUMP <file>    dump the named file as offset / hex / ASCII
 *   HEXDUMP           self-test: write a known payload, then dump it back
 *
 * One source, two targets: the code generator is identical for Osiris (.PRG)
 * and CP/M-68K (.68K), so the self-test produces byte-identical output on both
 * OSes. This exercises the parts of the SDK a genuine tool relies on: argv
 * from the command tail, binary file I/O (fopen "rb"/"wb", fread, fwrite), and
 * the printf width / zero-pad / hex conversions in the c68k libc. */

#define COLS 16

static void dump_stream(FILE *f, const char *name) {
  unsigned char buf[COLS];
  unsigned long off = 0;
  size_t n;

  printf("hexdump of %s\n", name);
  while ((n = fread(buf, 1, COLS, f)) > 0) {
    printf("%08lx  ", off);
    for (size_t i = 0; i < COLS; i++) {
      if (i < n)
        printf("%02x ", (unsigned)buf[i]);
      else
        printf("   ");
      if (i == 7)
        putchar(' ');
    }
    printf(" |");
    for (size_t i = 0; i < n; i++) {
      unsigned char c = buf[i];
      putchar((c >= 0x20 && c < 0x7f) ? (int)c : '.');
    }
    printf("|\n");
    off += n;
  }
  printf("%lu bytes\n", off);
}

/* Self-test payload: a recognizable header followed by a walk of the printable
 * ASCII range. It is exactly PAYLEN = 128 bytes -- one full CP/M-68K record --
 * on purpose: CP/M files are record-granular (a short file reads back padded to
 * 128 bytes with 0x1A), whereas Osiris FAT12 stores the exact length. Filling a
 * whole record sidesteps that difference so the dump is byte-identical on both
 * OSes (the dual-target golden). See docs/sdk.md "CP/M record granularity". */
#define PAYLEN 128

static void make_payload(unsigned char *b) {
  static const char hdr[] = "c68k hexdump OK\n"; /* 16 bytes */
  int i;
  for (i = 0; i < 16; i++)
    b[i] = (unsigned char)hdr[i];
  for (; i < PAYLEN; i++)
    b[i] = (unsigned char)(0x20 + (i - 16) % 95); /* cycle 0x20..0x7e */
}

static int selftest(void) {
  const char *tmp = "HEXTEST.BIN";
  unsigned char payload[PAYLEN];
  make_payload(payload);

  FILE *f = fopen(tmp, "wb");
  if (!f) {
    puts("hexdump: cannot create HEXTEST.BIN");
    return 1;
  }
  fwrite(payload, 1, PAYLEN, f);
  fclose(f);

  f = fopen(tmp, "rb");
  if (!f) {
    puts("hexdump: cannot reopen HEXTEST.BIN");
    return 1;
  }
  dump_stream(f, tmp);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return selftest();

  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    printf("hexdump: cannot open %s\n", argv[1]);
    return 1;
  }
  dump_stream(f, argv[1]);
  fclose(f);
  return 0;
}
