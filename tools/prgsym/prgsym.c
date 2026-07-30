/* prgsym.c -- extract a sid68k-format symbol map from a linked ELF32-BE
 * m68k executable (an Osiris .PRG or a CP/M-68K-bound ELF), and -- in a
 * later revision -- strip its symbol table.
 *
 * The emitted ".sym" file is the mkdri Phase 6 symbol-map format that
 * sid68k's `Y <file>[,<bias>]` command loads (see
 * worm68k/68kTools/mkdri/MKDRI_SPEC.md sec. 6 and SymFile.cpp):
 *
 *     # prgsym symbol map
 *     # format: <hex_addr> <T|D|B|A> <name>
 *     0004373C T _assemble_to_elf
 *     00061998 B _data
 *
 * One symbol per line: 8-hex-uppercase address, a type letter, the name.
 * Lines beginning with '#' are comments.  Type letter follows the section
 * the symbol lives in: T=.text/.rodata/.init/.fini, D=.data/.sdata,
 * B=.bss/.sbss, A=absolute (SHN_ABS).  Undefined symbols, empty names,
 * asm68K local labels (names beginning with '$'), and asm68K internal
 * predefined symbols (names beginning with '@', e.g. @CatStr) are filtered
 * out.  The addresses are the symbol's link-time VA; sid68k adds the runtime
 * load base with `Y file,<bias>` (bias defaults to 0, e.g. CP/M-68K TPA loads).
 *
 * Portable C99 -- builds with cl / gcc / clang on the host and with c68k
 * itself for Osiris / CP/M-68K.  ELF fields are read byte-at-a-time in
 * big-endian order, so the tool runs identically on a little-endian host
 * and a big-endian m68k target.  The whole input is read into memory; on
 * the 64 KB CP/M-68K TPA that bounds the input to small .68K images (the
 * host build handles the multi-hundred-KB self-host compiler).
 *
 * Build (host):   cl /nologo /std:c11 /O2 prgsym.c        (MSVC)
 *                 cc -std=c99 -O2 -o prgsym prgsym.c      (gcc/clang)
 * Build (native): c68k -O2 -c prgsym.c -o PRGSYM.O ; link with libc
 *
 * Usage:  prgsym extract <input> [-o <output.sym>] [-v]
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PRGSYM_VERSION "0.1.0"

/* ---- ELF32 on-disk constants (subset) ---- */
enum {
  ELFCLASS32 = 1,
  ELFDATA2MSB = 2,
  ET_EXEC = 2,
  ET_DYN = 3,
  EM_68K = 4,
  SHT_SYMTAB = 2,
  SHT_STRTAB = 3,
  STT_SECTION = 3,
  STT_FILE = 4,
  SHN_UNDEF = 0,
  SHN_ABS = 0xFFF1,
  SHN_COMMON = 0xFFF2
};

/* ELF header field offsets (Elf32_Ehdr, 52 bytes). */
enum {
  OFF_E_TYPE = 16, OFF_E_MACHINE = 18, OFF_E_SHOFF = 32,
  OFF_E_SHENTSIZE = 46, OFF_E_SHNUM = 48, OFF_E_SHSTRNDX = 50,
  SZ_EHDR32 = 52
};
/* Section header field offsets (Elf32_Shdr, 40 bytes). */
enum {
  OFF_SH_NAME = 0, OFF_SH_TYPE = 4, OFF_SH_OFFSET = 16, OFF_SH_SIZE = 20,
  OFF_SH_LINK = 24, OFF_SH_ENTSIZE = 36, SZ_SHDR32 = 40
};
/* Symbol table entry field offsets (Elf32_Sym, 16 bytes). */
enum {
  OFF_ST_NAME = 0, OFF_ST_VALUE = 4, OFF_ST_INFO = 12, OFF_ST_SHNDX = 14,
  SZ_SYM = 16
};

/* ---- big-endian accessors (endian-independent of the host) ---- */
static uint16_t be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* True when [off, off+need) lies within a buffer of `total` bytes and does
 * not wrap.  Guards every access into the untrusted input. */
static int in_bounds(uint32_t off, uint32_t need, size_t total) {
  return (size_t)off <= total && (size_t)need <= total - (size_t)off;
}

/* Bounded C-string fetch from a string table region. */
static const char *strtab_cstr(const uint8_t *buf, uint32_t base,
                               uint32_t size, uint32_t off) {
  if (off >= size)
    return "";
  return (const char *)(buf + base + off);
}

/* Classify a section name into a sid68k type letter, or 0 to drop.
 * Mirrors mkdri's classifyBySectionName so the two producers agree. */
static char classify_section(const char *n) {
  if (!strcmp(n, ".text") || !strncmp(n, ".text.", 6) ||
      !strcmp(n, ".rodata") || !strncmp(n, ".rodata.", 8) ||
      !strcmp(n, ".init") || !strcmp(n, ".fini"))
    return 'T';
  if (!strcmp(n, ".data") || !strncmp(n, ".data.", 6) ||
      !strcmp(n, ".sdata") || !strncmp(n, ".sdata.", 7))
    return 'D';
  if (!strcmp(n, ".bss") || !strncmp(n, ".bss.", 5) ||
      !strcmp(n, ".sbss") || !strncmp(n, ".sbss.", 6) ||
      !strcmp(n, "COMMON"))
    return 'B';
  return 0;
}

typedef struct {
  uint32_t addr;
  char type;
  const char *name;
} Sym;

/* Sort by address ascending, then name, for a stable, readable map. */
static int sym_cmp(const void *a, const void *b) {
  const Sym *x = (const Sym *)a, *y = (const Sym *)b;
  if (x->addr < y->addr) return -1;
  if (x->addr > y->addr) return 1;
  return strcmp(x->name, y->name);
}

static uint8_t *slurp(const char *path, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  long sz;
  uint8_t *buf;
  if (!fp) {
    fprintf(stderr, "prgsym: cannot open '%s'\n", path);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0) {
    fprintf(stderr, "prgsym: cannot size '%s'\n", path);
    fclose(fp);
    return NULL;
  }
  rewind(fp);
  buf = (uint8_t *)malloc((size_t)sz ? (size_t)sz : 1);
  if (!buf) {
    fprintf(stderr, "prgsym: out of memory reading '%s'\n", path);
    fclose(fp);
    return NULL;
  }
  if ((size_t)sz && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    fprintf(stderr, "prgsym: read failed on '%s'\n", path);
    free(buf);
    fclose(fp);
    return NULL;
  }
  fclose(fp);
  *out_len = (size_t)sz;
  return buf;
}

static int do_extract(const char *inpath, const char *outpath, int verbose) {
  size_t total = 0;
  uint8_t *buf = slurp(inpath, &total);
  const uint8_t *sh;
  uint32_t e_shoff, e_shstrndx_off, shstr_off, shstr_size;
  uint16_t e_type, e_machine, e_shentsize, e_shnum, e_shstrndx;
  uint32_t symtab_off = 0, symtab_size = 0, symtab_ent = 0;
  uint32_t strtab_off = 0, strtab_size = 0;
  int have_symtab = 0;
  char *seccls = NULL;
  Sym *syms = NULL;
  uint32_t nsym = 0, nout = 0, i;
  FILE *fp;

  if (!buf)
    return 5;

  if (total < SZ_EHDR32 || buf[0] != 0x7F || buf[1] != 'E' ||
      buf[2] != 'L' || buf[3] != 'F') {
    fprintf(stderr, "prgsym: '%s' is not an ELF file\n", inpath);
    goto fail_fmt;
  }
  if (buf[4] != ELFCLASS32 || buf[5] != ELFDATA2MSB) {
    fprintf(stderr, "prgsym: only 32-bit big-endian ELF is supported\n");
    goto fail_fmt;
  }
  e_type = be16(buf + OFF_E_TYPE);
  e_machine = be16(buf + OFF_E_MACHINE);
  if (e_machine != EM_68K) {
    fprintf(stderr, "prgsym: not an m68k ELF (e_machine=%u)\n", e_machine);
    goto fail_fmt;
  }
  if (e_type != ET_EXEC && e_type != ET_DYN) {
    fprintf(stderr, "prgsym: not a linked executable (e_type=%u)\n", e_type);
    goto fail_fmt;
  }
  e_shoff = be32(buf + OFF_E_SHOFF);
  e_shentsize = be16(buf + OFF_E_SHENTSIZE);
  e_shnum = be16(buf + OFF_E_SHNUM);
  e_shstrndx = be16(buf + OFF_E_SHSTRNDX);
  if (e_shentsize != SZ_SHDR32 || e_shnum == 0 || e_shstrndx >= e_shnum) {
    fprintf(stderr, "prgsym: missing or malformed section headers "
                    "(stripped of section table?)\n");
    goto fail_fmt;
  }
  if (!in_bounds(e_shoff, (uint32_t)e_shnum * SZ_SHDR32, total)) {
    fprintf(stderr, "prgsym: section header table out of range\n");
    goto fail_fmt;
  }

  /* Section-header string table (for section names). */
  e_shstrndx_off = e_shoff + (uint32_t)e_shstrndx * SZ_SHDR32;
  shstr_off = be32(buf + e_shstrndx_off + OFF_SH_OFFSET);
  shstr_size = be32(buf + e_shstrndx_off + OFF_SH_SIZE);
  if (!in_bounds(shstr_off, shstr_size, total)) {
    fprintf(stderr, "prgsym: section string table out of range\n");
    goto fail_fmt;
  }

  /* First pass: classify every section by name; locate .symtab + .strtab. */
  seccls = (char *)calloc(e_shnum, 1);
  if (!seccls) {
    fprintf(stderr, "prgsym: out of memory\n");
    goto fail_io;
  }
  for (i = 0; i < e_shnum; i++) {
    uint32_t sh_name, sh_type, sh_link;
    const char *nm;
    sh = buf + e_shoff + i * SZ_SHDR32;
    sh_name = be32(sh + OFF_SH_NAME);
    sh_type = be32(sh + OFF_SH_TYPE);
    nm = strtab_cstr(buf, shstr_off, shstr_size, sh_name);
    seccls[i] = classify_section(nm);
    if (sh_type == SHT_SYMTAB && !have_symtab) {
      symtab_off = be32(sh + OFF_SH_OFFSET);
      symtab_size = be32(sh + OFF_SH_SIZE);
      symtab_ent = be32(sh + OFF_SH_ENTSIZE);
      sh_link = be32(sh + OFF_SH_LINK);
      if (sh_link < e_shnum) {
        const uint8_t *stsh = buf + e_shoff + sh_link * SZ_SHDR32;
        strtab_off = be32(stsh + OFF_SH_OFFSET);
        strtab_size = be32(stsh + OFF_SH_SIZE);
        have_symtab = 1;
      }
    }
  }

  if (!have_symtab) {
    fprintf(stderr, "prgsym: '%s' has no symbol table (already stripped?)\n",
            inpath);
    goto fail_fmt;
  }
  if (symtab_ent != SZ_SYM || !in_bounds(symtab_off, symtab_size, total) ||
      !in_bounds(strtab_off, strtab_size, total)) {
    fprintf(stderr, "prgsym: malformed symbol/string table\n");
    goto fail_fmt;
  }

  nsym = symtab_size / SZ_SYM;
  syms = (Sym *)malloc((nsym ? nsym : 1) * sizeof(Sym));
  if (!syms) {
    fprintf(stderr, "prgsym: out of memory\n");
    goto fail_io;
  }

  for (i = 1; i < nsym; i++) { /* i==0 is the STN_UNDEF placeholder */
    const uint8_t *sym = buf + symtab_off + i * SZ_SYM;
    uint32_t st_name = be32(sym + OFF_ST_NAME);
    uint32_t st_value = be32(sym + OFF_ST_VALUE);
    uint8_t st_info = sym[OFF_ST_INFO];
    uint16_t st_shndx = be16(sym + OFF_ST_SHNDX);
    uint8_t sttype = (uint8_t)(st_info & 0x0F);
    const char *nm;
    char letter;

    if (sttype == STT_SECTION || sttype == STT_FILE)
      continue;
    if (st_shndx == SHN_UNDEF)
      continue;
    nm = strtab_cstr(buf, strtab_off, strtab_size, st_name);
    /* Drop empty names, asm68K local labels ('$...'), and asm68K internal
     * predefined symbols ('@...', e.g. the @CatStr macro-string helpers it
     * spuriously emits into .symtab -- see the filed asm68K bug). */
    if (nm[0] == '\0' || nm[0] == '$' || nm[0] == '@')
      continue;

    if (st_shndx == SHN_ABS)
      letter = 'A';
    else if (st_shndx == SHN_COMMON)
      letter = 'B';
    else if (st_shndx < e_shnum)
      letter = seccls[st_shndx];
    else
      continue;
    if (letter == 0) /* symbol lives in a section we don't map */
      continue;

    syms[nout].addr = st_value;
    syms[nout].type = letter;
    syms[nout].name = nm;
    nout++;
  }

  qsort(syms, nout, sizeof(Sym), sym_cmp);

  fp = fopen(outpath, "w");
  if (!fp) {
    fprintf(stderr, "prgsym: cannot create '%s'\n", outpath);
    goto fail_io;
  }
  fprintf(fp, "# prgsym symbol map\n");
  fprintf(fp, "# format: <hex_addr> <T|D|B|A> <name>\n");
  for (i = 0; i < nout; i++)
    fprintf(fp, "%08lX %c %s\n", (unsigned long)syms[i].addr, syms[i].type,
            syms[i].name);
  if (fclose(fp) != 0) {
    fprintf(stderr, "prgsym: write failed on '%s'\n", outpath);
    goto fail_io;
  }

  if (verbose)
    fprintf(stderr, "prgsym: wrote %s: %lu symbols\n", outpath,
            (unsigned long)nout);

  free(syms);
  free(seccls);
  free(buf);
  return 0;

fail_fmt:
  free(syms);
  free(seccls);
  free(buf);
  return 2;
fail_io:
  free(syms);
  free(seccls);
  free(buf);
  return 5;
}

/* Replace the input's extension with ".sym" (or append if none). */
static char *default_output(const char *inpath) {
  size_t n = strlen(inpath);
  const char *dot = NULL, *p;
  char *out;
  for (p = inpath + n; p > inpath; ) {
    --p;
    if (*p == '/' || *p == '\\')
      break;
    if (*p == '.') {
      dot = p;
      break;
    }
  }
  {
    size_t base = dot ? (size_t)(dot - inpath) : n;
    out = (char *)malloc(base + 5);
    if (!out)
      return NULL;
    memcpy(out, inpath, base);
    memcpy(out + base, ".sym", 5);
  }
  return out;
}

static int usage(int status) {
  FILE *out = status ? stderr : stdout;
  fprintf(out,
      "prgsym %s -- ELF32-BE (m68k) symbol-map extractor for sid68k\n"
      "usage:\n"
      "  prgsym extract <input> [-o <output.sym>] [-v]\n"
      "\n"
      "  extract   write a sid68k `Y`-loadable symbol map (.sym) from a\n"
      "            linked ELF executable (Osiris .PRG / m68k ELF).\n"
      "  -o <f>    output path (default: <input> with a .sym extension)\n"
      "  -v        verbose (print the symbol count to stderr)\n"
      "  -h        this help\n"
      "  --version print version and exit\n",
      PRGSYM_VERSION);
  return status;
}

int main(int argc, char **argv) {
  const char *inpath = NULL, *outpath = NULL;
  char *outbuf = NULL;
  int verbose = 0, rc, i;

  if (argc < 2)
    return usage(1);
  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
    return usage(0);
  if (!strcmp(argv[1], "--version")) {
    printf("prgsym %s\n", PRGSYM_VERSION);
    return 0;
  }
  if (!strcmp(argv[1], "strip")) {
    fprintf(stderr, "prgsym: 'strip' is not implemented in this build\n");
    return 4;
  }
  if (strcmp(argv[1], "extract") != 0) {
    fprintf(stderr, "prgsym: unknown command '%s' (expected 'extract')\n",
            argv[1]);
    return usage(1);
  }

  for (i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "-o")) {
      if (++i >= argc)
        return usage(1);
      outpath = argv[i];
    } else if (!strncmp(argv[i], "-o", 2)) {
      outpath = argv[i] + 2;
    } else if (!strcmp(argv[i], "-v")) {
      verbose = 1;
    } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
      fprintf(stderr, "prgsym: unknown option '%s'\n", argv[i]);
      return usage(1);
    } else if (!inpath) {
      inpath = argv[i];
    } else {
      fprintf(stderr, "prgsym: only one input file is supported\n");
      return usage(1);
    }
  }

  if (!inpath) {
    fprintf(stderr, "prgsym: missing input file\n");
    return usage(1);
  }
  if (!outpath) {
    outbuf = default_output(inpath);
    if (!outbuf) {
      fprintf(stderr, "prgsym: out of memory\n");
      return 5;
    }
    outpath = outbuf;
  }

  rc = do_extract(inpath, outpath, verbose);
  free(outbuf);
  return rc;
}
