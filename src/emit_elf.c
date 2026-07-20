// emit_elf.c --- c68k integrated assembler + ELF32-BE object emitter.
//
// P8: turns the compiler's OWN Motorola-syntax assembly text (the fixed,
// known subset that codegen68k.c emits) directly into an ELF32 big-endian
// MC68000 relocatable object -- with no external assembler. codegen stays
// the instruction-*selection* layer; this file is the *encoder* (mnemonic
// text -> binary opcodes + ELF relocation records), exactly the split
// architecture.md 8 describes.
//
// Not a general m68k assembler: it handles precisely the directives,
// addressing modes and instructions c68k generates. Anything outside that
// set is an internal error (surfaces coverage gaps during bring-up).
//
// Branches are always emitted .W (fixed 4-byte size) so section layout is
// single-pass; local branch displacements are patched in a fixup pass. This
// is not byte-identical to asm68K (which relaxes to .S and peepholes
// jsr->bsr / adda->addq) -- equivalence is validated by linking + running.

#include "chibicc.h"

// ------------------------------------------------------------------ buffers
typedef struct {
  uint8_t *data;
  int len, cap;
} Buf;

static void buf_need(Buf *b, int n) {
  if (b->len + n <= b->cap)
    return;
  b->cap = MAX(b->cap ? b->cap * 2 : 256, b->len + n);
  b->data = realloc(b->data, b->cap);
}
static void b8(Buf *b, int v) {
  buf_need(b, 1);
  b->data[b->len++] = (uint8_t)v;
}
static void b16(Buf *b, int v) { // big-endian
  b8(b, v >> 8);
  b8(b, v);
}
static void b32(Buf *b, uint32_t v) { // big-endian
  b8(b, v >> 24);
  b8(b, v >> 16);
  b8(b, v >> 8);
  b8(b, v);
}
static void buf_align(Buf *b, int a) {
  while (b->len % a)
    b8(b, 0);
}

// Append a NUL-terminated string to a buffer; return its starting offset.
static int add_str(Buf *b, char *s) {
  int o = b->len;
  for (char *c = s; *c; c++)
    b8(b, *c);
  b8(b, 0);
  return o;
}

// ------------------------------------------------------------------ tables
// Section indices are fixed for 1..4 so symbols can reference them directly.
enum { S_NULL, S_TEXT, S_DATA, S_BSS, S_RODATA };

// ELF constants
#define ET_REL 1
#define EM_68K 4
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_WRITE 1
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define R_68K_32 1
#define R_68K_PC16 4

typedef struct {
  char *name;
  int shndx;      // SHN_UNDEF / section index / SHN_ABS
  uint32_t value; // offset within section
  uint32_t size;
  int bind;       // STB_*
  int nameoff;    // .strtab offset (filled at write)
  int index;      // final symtab index (filled after ordering)
} Sym;

typedef struct {
  int sec;       // section the fixup patches (S_TEXT / S_DATA)
  uint32_t off;  // offset within that section of the 4-byte field
  char *sym;     // referenced symbol name
  int type;      // R_68K_*
  int32_t add;   // addend
} Rel;

typedef struct {
  uint32_t off;  // .text offset of the 16-bit displacement word
  char *label;   // target local label
} Fix;

static Buf text, data, rodata;
static uint32_t bss_size;
static int cur_sec;

static Sym *syms;
static int nsym, capsym;
static Rel *rels;
static int nrel, caprel;
static Fix *fixes;
static int nfix, capfix;

static StringArray public_names;
static StringArray extern_names;

// ------------------------------------------------------------------ symbols
static Sym *sym_find(char *name) {
  for (int i = 0; i < nsym; i++)
    if (!strcmp(syms[i].name, name))
      return &syms[i];
  return NULL;
}

static Sym *sym_intern(char *name) {
  Sym *s = sym_find(name);
  if (s)
    return s;
  if (nsym == capsym) {
    capsym = capsym ? capsym * 2 : 64;
    syms = realloc(syms, capsym * sizeof(Sym));
  }
  s = &syms[nsym++];
  memset(s, 0, sizeof(*s));
  s->name = name;
  s->shndx = SHN_UNDEF;
  return s;
}

static void sym_define(char *name, int shndx, uint32_t value) {
  Sym *s = sym_intern(name);
  s->shndx = shndx;
  s->value = value;
}

static bool in_list(StringArray *a, char *name) {
  for (int i = 0; i < a->len; i++)
    if (!strcmp(a->data[i], name))
      return true;
  return false;
}

static char *xstrndup(char *p, int n);

static void add_reloc(int sec, uint32_t off, char *sym, int type, int32_t add) {
  sym_intern(sym); // ensure the referenced symbol exists in the table
  if (nrel == caprel) {
    caprel = caprel ? caprel * 2 : 64;
    rels = realloc(rels, caprel * sizeof(Rel));
  }
  rels[nrel++] = (Rel){sec, off, sym, type, add};
}

static void add_fixup(uint32_t off, char *label) {
  if (nfix == capfix) {
    capfix = capfix ? capfix * 2 : 64;
    fixes = realloc(fixes, capfix * sizeof(Fix));
  }
  // Own the label: callers pass a pointer into the transient line buffer, but
  // fixups are resolved after the whole file has been streamed.
  fixes[nfix++] = (Fix){off, xstrndup(label, (int)strlen(label))};
}

static Buf *cur_buf(void) {
  switch (cur_sec) {
  case S_TEXT:
    return &text;
  case S_DATA:
    return &data;
  case S_RODATA:
    return &rodata;
  }
  error("emit_elf: data emitted with no active section");
}

// ------------------------------------------------------------------ lexing
static char *xstrndup(char *p, int n) {
  char *q = malloc(n + 1);
  memcpy(q, p, n);
  q[n] = 0;
  return q;
}

// Trim leading/trailing ASCII whitespace in place (returns start).
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  int n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
    s[--n] = 0;
  return s;
}

static bool is_reg_char(int c) {
  return isalnum(c) || c == '_' || c == '.' || c == '$';
}

// Parse a signed integer that may be $hex or decimal (with optional sign).
static int64_t parse_num(char *s) {
  bool neg = false;
  if (*s == '+')
    s++;
  else if (*s == '-') {
    neg = true;
    s++;
  }
  int64_t v;
  if (*s == '$')
    v = (int64_t)strtoull(s + 1, NULL, 16);
  else
    v = (int64_t)strtoull(s, NULL, 10);
  return neg ? -v : v;
}

// ------------------------------------------------------------------ EA
typedef struct {
  int mode, reg;
  int next;        // extension words to emit (0/1/2)
  uint32_t ext[2]; // big-endian words, most-significant first
  bool reloc;      // abs.L address field needs an R_68K_32
  char *sym;
  int32_t add;
  bool is_imm;
  int64_t imm;
} EA;

static int reg_num(char *s) {
  // s is "d0".."d7", "a0".."a7", "sp"(=a7), "pc"
  if (!strcmp(s, "sp"))
    return 7;
  if (s[0] == 'd' || s[0] == 'a')
    return s[1] - '0';
  error("emit_elf: bad register '%s'", s);
}

// Parse one operand into an EA. `size` in bytes (1/2/4) governs immediate
// extension-word width.
static EA parse_ea(char *s, int size) {
  EA e;
  memset(&e, 0, sizeof(e));
  s = trim(s);

  // immediate: #imm
  if (s[0] == '#') {
    int64_t v = parse_num(s + 1);
    e.mode = 7;
    e.reg = 4;
    e.is_imm = true;
    e.imm = v;
    if (size == 4) {
      e.next = 2;
      e.ext[0] = (uint32_t)v >> 16 & 0xffff;
      e.ext[1] = (uint32_t)v & 0xffff;
    } else {
      e.next = 1;
      e.ext[0] = (uint32_t)v & (size == 1 ? 0xff : 0xffff);
    }
    return e;
  }

  // predecrement: -(An)
  if (s[0] == '-' && s[1] == '(') {
    char *p = s + 2;
    char *q = p;
    while (*q && *q != ')')
      q++;
    e.mode = 4;
    e.reg = reg_num(xstrndup(p, q - p));
    return e;
  }

  // indirect / postincrement: (An) or (An)+
  if (s[0] == '(') {
    char *p = s + 1;
    char *q = p;
    while (*q && *q != ')')
      q++;
    e.reg = reg_num(xstrndup(p, q - p));
    e.mode = (q[1] == '+') ? 3 : 2;
    return e;
  }

  // register direct: dN / aN / sp
  if ((s[0] == 'd' || s[0] == 'a') && isdigit((unsigned char)s[1]) && !s[2]) {
    e.mode = (s[0] == 'd') ? 0 : 1;
    e.reg = s[1] - '0';
    return e;
  }
  if (!strcmp(s, "sp")) {
    e.mode = 1;
    e.reg = 7;
    return e;
  }

  // displacement: disp(An)  e.g. 12(a6), -4(a6)
  {
    char *paren = strchr(s, '(');
    if (paren) {
      char *disp = xstrndup(s, paren - s);
      char *p = paren + 1;
      char *q = p;
      while (*q && *q != ')')
        q++;
      e.mode = 5;
      e.reg = reg_num(xstrndup(p, q - p));
      e.next = 1;
      e.ext[0] = (uint32_t)(int32_t)parse_num(disp) & 0xffff;
      return e;
    }
  }

  // absolute label -> abs.L (mode 7 reg 1), 32-bit address via R_68K_32
  {
    char *plus = NULL;
    for (char *p = s + 1; *p; p++)
      if (*p == '+' || *p == '-') {
        plus = p;
        break;
      }
    e.mode = 7;
    e.reg = 1;
    e.next = 2;
    e.reloc = true;
    if (plus) {
      e.sym = xstrndup(s, plus - s);
      e.add = (int32_t)parse_num(plus);
    } else {
      e.sym = xstrndup(s, strlen(s));
      e.add = 0;
    }
    e.ext[0] = e.ext[1] = 0;
    return e;
  }
}

// Emit an EA's extension words into .text (recording a reloc for abs.L).
static void emit_ext(EA *e) {
  if (e->reloc) {
    add_reloc(S_TEXT, text.len, e->sym, R_68K_32, e->add);
    b16(&text, 0);
    b16(&text, 0);
    return;
  }
  for (int i = 0; i < e->next; i++)
    b16(&text, e->ext[i]);
}

// ------------------------------------------------------------------ encode
// MOVE size field: byte=01, word=11, long=10.
static int move_size_field(int sz) { return sz == 1 ? 1 : sz == 2 ? 3 : 2; }
// Standard size field: byte=00, word=01, long=10.
static int std_size_field(int sz) { return sz == 1 ? 0 : sz == 2 ? 1 : 2; }

static void enc_move(int sz, EA *src, EA *dst) {
  int op = (0 << 14) | (move_size_field(sz) << 12) | (dst->reg << 9) |
           (dst->mode << 6) | (src->mode << 3) | src->reg;
  b16(&text, op);
  emit_ext(src);
  emit_ext(dst);
}

// ADD/SUB/AND/OR family, register/EA form with Dn.
// base: ADD=0xd,SUB=0x9,AND=0xc,OR=0x8 (top nibble). dir: 0 => <ea>,Dn (opmode
// 0SS), 1 => Dn,<ea> (opmode 1SS).
static void enc_alu(int topnib, int sz, int dn, EA *ea, int dir) {
  int opmode = (dir << 2) | std_size_field(sz);
  int op = (topnib << 12) | (dn << 9) | (opmode << 6) | (ea->mode << 3) | ea->reg;
  b16(&text, op);
  emit_ext(ea);
}

// Immediate-to-EA: ANDI/ORI/EORI/SUBI/ADDI/CMPI.
// code: ORI=0,ANDI=1,SUBI=2,ADDI=3,EORI=5,CMPI=6 (bits 11-9 of 0000 xxx0 ...).
static void enc_imm(int code, int sz, int64_t imm, EA *ea) {
  int op = (0 << 12) | (code << 9) | (std_size_field(sz) << 6) |
           (ea->mode << 3) | ea->reg;
  b16(&text, op);
  if (sz == 4)
    b32(&text, (uint32_t)imm);
  else
    b16(&text, (uint32_t)imm & (sz == 1 ? 0xff : 0xffff));
  emit_ext(ea);
}

static int cond_code(char *cc) {
  // returns the 4-bit condition for Bcc/Scc/DBcc mnem  (after stripping b/s/db)
  if (!strcmp(cc, "ra") || !strcmp(cc, "t"))
    return 0;
  if (!strcmp(cc, "f"))
    return 1;
  if (!strcmp(cc, "hi"))
    return 2;
  if (!strcmp(cc, "ls"))
    return 3;
  if (!strcmp(cc, "cc") || !strcmp(cc, "hs"))
    return 4;
  if (!strcmp(cc, "cs") || !strcmp(cc, "lo"))
    return 5;
  if (!strcmp(cc, "ne"))
    return 6;
  if (!strcmp(cc, "eq"))
    return 7;
  if (!strcmp(cc, "vc"))
    return 8;
  if (!strcmp(cc, "vs"))
    return 9;
  if (!strcmp(cc, "pl"))
    return 10;
  if (!strcmp(cc, "mi"))
    return 11;
  if (!strcmp(cc, "ge"))
    return 12;
  if (!strcmp(cc, "lt"))
    return 13;
  if (!strcmp(cc, "gt"))
    return 14;
  if (!strcmp(cc, "le"))
    return 15;
  error("emit_elf: unknown condition '%s'", cc);
}

// Split "mnem.sz" -> base + size(bytes, 0 if none).
static void split_size(char *m, char **base, int *sz) {
  char *dot = strrchr(m, '.');
  if (dot && dot[1] && !dot[2]) {
    *sz = dot[1] == 'b' ? 1 : dot[1] == 'w' ? 2 : dot[1] == 'l' ? 4 : 0;
    if (*sz) {
      *base = xstrndup(m, dot - m);
      return;
    }
  }
  *base = m;
  *sz = 0;
}

static void encode_insn(char *mnem, char *o1, char *o2) {
  char *base;
  int sz;
  split_size(mnem, &base, &sz);

  // move / movea
  if (!strcmp(base, "move") || !strcmp(base, "movea")) {
    EA s = parse_ea(o1, sz), d = parse_ea(o2, sz);
    enc_move(sz, &s, &d);
    return;
  }
  if (!strcmp(base, "moveq")) {
    int64_t v = parse_num(o1 + 1); // skip '#'
    EA d = parse_ea(o2, 4);
    b16(&text, (7 << 12) | (d.reg << 9) | ((int)v & 0xff));
    return;
  }
  if (!strcmp(base, "lea")) {
    EA s = parse_ea(o1, 4);
    EA d = parse_ea(o2, 4); // An
    b16(&text, (4 << 12) | (d.reg << 9) | (7 << 6) | (s.mode << 3) | s.reg);
    emit_ext(&s);
    return;
  }

  // ALU register/immediate forms
  if (!strcmp(base, "add") || !strcmp(base, "sub") || !strcmp(base, "and") ||
      !strcmp(base, "or") || !strcmp(base, "eor") || !strcmp(base, "cmp")) {
    EA s = parse_ea(o1, sz), d = parse_ea(o2, sz);
    if (s.is_imm) {
      // immediate -> use ADDI/SUBI/ANDI/ORI/EORI/CMPI
      int code = !strcmp(base, "or")    ? 0
                 : !strcmp(base, "and") ? 1
                 : !strcmp(base, "sub") ? 2
                 : !strcmp(base, "add") ? 3
                 : !strcmp(base, "eor") ? 5
                                        : 6; // cmp
      enc_imm(code, sz, s.imm, &d);
      return;
    }
    // register/EA form: <ea>,Dn (dir 0) for add/sub/and/or/cmp;
    // eor is Dn,<ea> only (dir 1).
    if (!strcmp(base, "eor")) {
      int op = (0xb << 12) | (s.reg << 9) | ((4 | std_size_field(sz)) << 6) |
               (d.mode << 3) | d.reg;
      b16(&text, op);
      emit_ext(&d);
      return;
    }
    int topnib = !strcmp(base, "add")   ? 0xd
                 : !strcmp(base, "sub") ? 0x9
                 : !strcmp(base, "and") ? 0xc
                 : !strcmp(base, "or")  ? 0x8
                                        : 0xb; // cmp
    enc_alu(topnib, sz, d.reg, &s, 0);
    return;
  }

  // adda/suba (address-reg dest); optimize small imm to addq/subq
  if (!strcmp(base, "adda") || !strcmp(base, "suba")) {
    EA s = parse_ea(o1, sz), d = parse_ea(o2, sz);
    bool issub = base[0] == 's';
    if (s.is_imm && s.imm >= 1 && s.imm <= 8) {
      int data = (int)s.imm & 7; // 8 -> 0
      int op = (5 << 12) | (data << 9) | (issub << 8) | (std_size_field(sz) << 6) |
               (1 << 3) | d.reg; // dst = An
      b16(&text, op);
      return;
    }
    int topnib = issub ? 0x9 : 0xd;
    int opmode = (sz == 4) ? 7 : 3; // .l=111, .w=011 (An dest)
    int op = (topnib << 12) | (d.reg << 9) | (opmode << 6) | (s.mode << 3) | s.reg;
    b16(&text, op);
    emit_ext(&s);
    return;
  }

  if (!strcmp(base, "addx") || !strcmp(base, "subx")) {
    EA s = parse_ea(o1, sz), d = parse_ea(o2, sz); // Dy,Dx
    int topnib = base[0] == 's' ? 0x9 : 0xd;
    int op = (topnib << 12) | (d.reg << 9) | (1 << 8) | (std_size_field(sz) << 6) |
             s.reg;
    b16(&text, op);
    return;
  }

  if (!strcmp(base, "andi") || !strcmp(base, "ori") || !strcmp(base, "eori")) {
    EA s = parse_ea(o1, sz), d = parse_ea(o2, sz);
    int code = !strcmp(base, "ori") ? 0 : !strcmp(base, "andi") ? 1 : 5;
    enc_imm(code, sz, s.imm, &d);
    return;
  }

  if (!strcmp(base, "ext")) {
    EA d = parse_ea(o1, sz);
    // ext.w = 0100 1000 10 000 reg ; ext.l = 0100 1000 11 000 reg
    int op = (0x48 << 8) | ((sz == 4 ? 3 : 2) << 6) | d.reg;
    b16(&text, op);
    return;
  }

  if (!strcmp(base, "neg") || !strcmp(base, "negx") || !strcmp(base, "not") ||
      !strcmp(base, "tst") || !strcmp(base, "clr")) {
    EA d = parse_ea(o1, sz);
    int hi = !strcmp(base, "negx")  ? 0x40
             : !strcmp(base, "clr") ? 0x42
             : !strcmp(base, "neg") ? 0x44
             : !strcmp(base, "not") ? 0x46
                                    : 0x4a; // tst
    int op = (hi << 8) | (std_size_field(sz) << 6) | (d.mode << 3) | d.reg;
    b16(&text, op);
    emit_ext(&d);
    return;
  }

  // shifts: asl/asr/lsl/lsr  --  register form (Dn,Dm) or immediate (#1..8,Dm)
  if (!strcmp(base, "asl") || !strcmp(base, "asr") || !strcmp(base, "lsl") ||
      !strcmp(base, "lsr")) {
    int dir = (base[1] == 's' && base[2] == 'l') ? 1 : 0; // asl/lsl left
    int type = base[0] == 'a' ? 0 : 1;                    // arithmetic/logical
    EA d = parse_ea(o2, sz);
    int cnt, ir;
    if (o1[0] == '#') {
      cnt = (int)parse_num(o1 + 1) & 7; // immediate 1..8 (8 -> 0)
      ir = 0;
    } else {
      cnt = parse_ea(o1, sz).reg; // count register
      ir = 1;
    }
    int op = (0xe << 12) | (cnt << 9) | (dir << 8) | (std_size_field(sz) << 6) |
             (ir << 5) | (type << 3) | d.reg;
    b16(&text, op);
    return;
  }

  if (!strcmp(base, "jsr")) {
    EA s = parse_ea(o1, 4);
    b16(&text, (0x4e << 8) | (0x80) | (s.mode << 3) | s.reg); // 0100 1110 10 ea
    emit_ext(&s);
    return;
  }
  if (!strcmp(base, "jmp")) {
    EA s = parse_ea(o1, 4);
    b16(&text, (0x4e << 8) | (0xc0) | (s.mode << 3) | s.reg); // 0100 1110 11 ea
    emit_ext(&s);
    return;
  }

  if (!strcmp(base, "link")) {
    EA a = parse_ea(o1, 4);
    int64_t disp = parse_num(o2 + 1); // #disp
    b16(&text, (0x4e50) | a.reg);     // 0100 1110 0101 0 reg
    b16(&text, (uint32_t)disp & 0xffff);
    return;
  }
  if (!strcmp(base, "unlk")) {
    EA a = parse_ea(o1, 4);
    b16(&text, 0x4e58 | a.reg);
    return;
  }
  if (!strcmp(base, "rts")) {
    b16(&text, 0x4e75);
    return;
  }
  if (!strcmp(base, "nop")) {
    b16(&text, 0x4e71);
    return;
  }

  // branches: bra/bcc  (always .W), dbra
  if (!strcmp(base, "bra") || (base[0] == 'b' && strlen(base) == 3)) {
    char *cc = !strcmp(base, "bra") ? "ra" : xstrndup(base + 1, 2);
    int c = cond_code(cc);
    b16(&text, (6 << 12) | (c << 8) | 0x00); // Bcc.W
    add_fixup(text.len, trim(o1));
    b16(&text, 0);
    return;
  }
  if (!strcmp(base, "dbra") || !strcmp(base, "dbf")) {
    EA d = parse_ea(o1, 2);
    b16(&text, 0x51c8 | d.reg); // DBF Dn
    add_fixup(text.len, trim(o2));
    b16(&text, 0);
    return;
  }

  // Scc Dn
  if (base[0] == 's' && strlen(base) >= 3) {
    int c = cond_code(base + 1);
    EA d = parse_ea(o1, 1);
    b16(&text, (5 << 12) | (c << 8) | (3 << 6) | (d.mode << 3) | d.reg);
    return;
  }

  error("emit_elf: unhandled instruction '%s'", mnem);
}

// ------------------------------------------------------------------ line
static bool starts(char *s, char *pfx) { return !strncmp(s, pfx, strlen(pfx)); }

static void do_data_dir(char *first, char *rest) {
  // first (uppercased) is DC.B / DC.L / DS.B; rest is the value/expr.
  char up[8] = {0};
  for (int i = 0; first[i] && i < 7; i++)
    up[i] = toupper((unsigned char)first[i]);

  Buf *b = cur_buf();
  if (!strcmp(up, "DC.B")) {
    b8(b, (int)parse_num(trim(rest)) & 0xff);
  } else if (!strcmp(up, "DC.W")) {
    b16(b, (int)parse_num(trim(rest)) & 0xffff);
  } else if (!strcmp(up, "DC.L")) {
    // may be `sym+add` (reloc) or a bare number
    char *r = trim(rest);
    if (isalpha((unsigned char)r[0]) || r[0] == '_') {
      char *plus = NULL;
      for (char *p = r + 1; *p; p++)
        if (*p == '+' || *p == '-') {
          plus = p;
          break;
        }
      char *name = plus ? xstrndup(r, plus - r) : xstrndup(r, strlen(r));
      int32_t add = plus ? (int32_t)parse_num(plus) : 0;
      add_reloc(cur_sec, b->len, name, R_68K_32, add);
      b32(b, 0);
    } else {
      b32(b, (uint32_t)parse_num(r));
    }
  } else {
    error("emit_elf: unknown data directive '%s'", first);
  }
}

static void assemble_line(char *line) {
  char *s = trim(line);
  if (!*s || *s == '*' || *s == ';')
    return;

  // label with trailing colon (code label)
  int n = strlen(s);
  if (s[n - 1] == ':') {
    char *name = xstrndup(s, n - 1);
    if (cur_sec == S_BSS)
      sym_define(name, S_BSS, bss_size);
    else
      sym_define(name, cur_sec ? cur_sec : S_TEXT, cur_buf()->len);
    return;
  }

  // split first token
  char *p = s;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  char *first = xstrndup(s, p - s);
  char *rest = trim(p);

  // directives
  if (!strcmp(first, ".model"))
    return;
  if (!strcmp(first, ".code")) {
    cur_sec = S_TEXT;
    return;
  }
  if (!strcmp(first, ".data")) {
    cur_sec = S_DATA;
    return;
  }
  if (!strcmp(first, ".data?")) {
    cur_sec = S_BSS;
    return;
  }
  if (!strcmp(first, ".rodata")) {
    cur_sec = S_RODATA;
    return;
  }
  if (!strcmp(first, "END"))
    return;
  if (!strcmp(first, "ALIGN")) {
    int a = (int)parse_num(rest);
    if (cur_sec == S_BSS)
      bss_size = (bss_size + a - 1) / a * a;
    else
      buf_align(cur_buf(), a);
    return;
  }
  if (!strcmp(first, "PUBLIC")) {
    strarray_push(&public_names, xstrndup(rest, strlen(rest)));
    return;
  }
  if (!strcmp(first, "EXTERN")) {
    char *tok = strtok(rest, ",");
    while (tok) {
      char *t = trim(tok);
      strarray_push(&extern_names, xstrndup(t, (int)strlen(t)));
      tok = strtok(NULL, ",");
    }
    return;
  }

  // data directive with no label
  char fu[8] = {0};
  for (int i = 0; first[i] && i < 7; i++)
    fu[i] = toupper((unsigned char)first[i]);
  if (starts(fu, "DC.") || starts(fu, "DS.")) {
    if (starts(fu, "DS.")) {
      bss_size += (uint32_t)parse_num(trim(rest));
    } else {
      do_data_dir(first, rest);
    }
    return;
  }

  // instruction? (mnemonics are lowercase and begin with a letter)
  if (islower((unsigned char)first[0]) && !strchr(first, ':')) {
    char *o1 = rest, *o2 = NULL;
    char *comma = strchr(rest, ',');
    if (comma) {
      *comma = 0;
      o1 = trim(rest);
      o2 = trim(comma + 1);
    } else {
      o1 = trim(rest);
    }
    encode_insn(first, o1, o2);
    return;
  }

  // otherwise: data label (first) followed by DC/DS in rest
  {
    char *q = rest;
    while (*q && *q != ' ' && *q != '\t')
      q++;
    char *dir = xstrndup(rest, q - rest);
    char *val = trim(q);
    char du[8] = {0};
    for (int i = 0; dir[i] && i < 7; i++)
      du[i] = toupper((unsigned char)dir[i]);
    if (starts(du, "DS.")) {
      sym_define(first, S_BSS, bss_size);
      bss_size += (uint32_t)parse_num(val);
    } else if (starts(du, "DC.")) {
      sym_define(first, cur_sec ? cur_sec : S_DATA, cur_buf()->len);
      do_data_dir(dir, val);
    } else {
      error("emit_elf: cannot parse line: %s", s);
    }
  }
}

// ------------------------------------------------------------------ fixups
static void resolve_fixups(void) {
  for (int i = 0; i < nfix; i++) {
    Sym *t = sym_find(fixes[i].label);
    if (!t || t->shndx != S_TEXT)
      error("emit_elf: branch to undefined local label '%s'", fixes[i].label);
    int32_t disp = (int32_t)t->value - (int32_t)fixes[i].off;
    text.data[fixes[i].off] = (disp >> 8) & 0xff;
    text.data[fixes[i].off + 1] = disp & 0xff;
  }
}

// ------------------------------------------------------------------ ELF out
typedef struct {
  char *name;
  int type, flags, link, info, entsize, addralign;
  uint8_t *data;
  int size;
  uint32_t sh_offset;
  int nameoff;
} Shdr;

static void put32(FILE *f, uint32_t v) {
  fputc(v >> 24, f);
  fputc(v >> 16, f);
  fputc(v >> 8, f);
  fputc(v, f);
}
static void put16(FILE *f, int v) {
  fputc(v >> 8, f);
  fputc(v, f);
}

void assemble_to_elf(char *inpath, char *outpath) {
  // reset state (the driver assembles one file per process, but be safe)
  memset(&text, 0, sizeof text);
  memset(&data, 0, sizeof data);
  memset(&rodata, 0, sizeof rodata);
  bss_size = 0;
  cur_sec = 0;
  nsym = nrel = nfix = 0;
  public_names = (StringArray){0};
  extern_names = (StringArray){0};

  // pass 1: assemble the .s streaming, one line at a time, into a single
  // reusable buffer. Two self-hosted (CP/M-68K) constraints drive this:
  //  1. Files are sequential 128-byte records with no random seek on a read
  //     handle (sys_seek is a stub), so fseek(SEEK_END)/ftell can't size the
  //     input -- it comes back 0 and the assembler would see nothing.
  //  2. libc malloc is a bump allocator (free is a no-op) and the front-end and
  //     assembler share one heap in the integrated -c process. Slurping the
  //     whole .s (parse.c's is ~640 KB) would not fit the ~600 KB TPA. Streaming
  //     keeps the assembler's transient footprint to one line; only the output
  //     sections and symbol/reloc tables accumulate. assemble_line copies every
  //     name it retains (labels, PUBLIC/EXTERN, branch fixups), so reusing one
  //     line buffer across lines is safe.
  FILE *in = fopen(inpath, "rb");
  if (!in)
    error("emit_elf: cannot open %s", inpath);
  size_t cap = 256, len = 0;
  char *line = malloc(cap);
  for (;;) {
    int c = fgetc(in);
    if (c == '\r')
      continue; // tolerate CRLF line endings
    // 0x1A (^Z) is CP/M's text-EOF pad in the last 128-byte record; treat it
    // as end-of-stream (assembler text never contains it). A no-op on hosts.
    if (c == '\n' || c == EOF || c == 0x1A) {
      if (len > 0) {
        line[len] = 0;
        assemble_line(line);
        len = 0;
      }
      if (c == '\n')
        continue;
      break;
    }
    if (len + 1 >= cap) {
      size_t ncap = cap * 2;
      char *nl = malloc(ncap);
      memcpy(nl, line, len);
      line = nl;
      cap = ncap; // old buffer leaks on the bump allocator; growth is rare
    }
    line[len++] = (char)c;
  }
  fclose(in);

  // pass 2: resolve local branch displacements
  resolve_fixups();

  // finalize symbol binding: global iff PUBLIC, EXTERN, or undefined.
  for (int i = 0; i < nsym; i++) {
    Sym *s = &syms[i];
    if (s->shndx == SHN_UNDEF || in_list(&public_names, s->name) ||
        in_list(&extern_names, s->name))
      s->bind = STB_GLOBAL;
    else
      s->bind = STB_LOCAL;
  }

  // order symbols: NULL, then locals, then globals; assign indices.
  // Build .strtab as we go.
  Buf strtab = {0};
  b8(&strtab, 0); // strtab[0] = '\0'
  Buf symtab = {0};
  // null symbol (index 0)
  for (int i = 0; i < 16; i++)
    b8(&symtab, 0);
  int symidx = 1;
  int first_global = -1;

  for (int pass = 0; pass < 2; pass++) {
    int want = pass == 0 ? STB_LOCAL : STB_GLOBAL;
    if (pass == 1)
      first_global = symidx;
    for (int i = 0; i < nsym; i++) {
      Sym *s = &syms[i];
      if (s->bind != want)
        continue;
      s->index = symidx++;
      s->nameoff = strtab.len;
      for (char *c = s->name; *c; c++)
        b8(&strtab, *c);
      b8(&strtab, 0);
      // Elf32_Sym: name, value, size, info, other, shndx
      b32(&symtab, s->nameoff);
      b32(&symtab, s->value);
      b32(&symtab, s->size);
      b8(&symtab, (s->bind << 4) | STT_NOTYPE);
      b8(&symtab, 0);
      b16(&symtab, s->shndx);
    }
  }
  if (first_global < 0)
    first_global = symidx;

  // build .rela.text / .rela.data
  Buf rela_text = {0}, rela_data = {0};
  for (int i = 0; i < nrel; i++) {
    Rel *r = &rels[i];
    Sym *s = sym_find(r->sym);
    if (!s)
      error("emit_elf: relocation against unknown symbol '%s'", r->sym);
    Buf *rb = (r->sec == S_DATA) ? &rela_data : &rela_text;
    b32(rb, r->off);
    b32(rb, (s->index << 8) | r->type);
    b32(rb, (uint32_t)r->add);
  }

  // Section header string table
  Buf shstr = {0};
  b8(&shstr, 0);

  // Assemble the section list in a fixed order. Indices must match S_*.
  Shdr sh[16];
  int nsh = 0;
  memset(sh, 0, sizeof sh);
  // 0: NULL
  sh[nsh++] = (Shdr){0};
  // 1: .text
  sh[nsh++] = (Shdr){".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, 0, 0,
                     4, text.data, text.len};
  // 2: .data
  sh[nsh++] = (Shdr){".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0, 0, 0, 4,
                     data.data, data.len};
  // 3: .bss
  sh[nsh++] = (Shdr){".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 0, 0, 0, 4, NULL,
                     (int)bss_size};
  // 4: .rodata
  sh[nsh++] = (Shdr){".rodata", SHT_PROGBITS, SHF_ALLOC, 0, 0, 0, 4, rodata.data,
                     rodata.len};
  int idx_rela_text = 0, idx_rela_data = 0;
  if (rela_text.len) {
    idx_rela_text = nsh;
    sh[nsh++] = (Shdr){".rela.text", SHT_RELA, 0, 0, S_TEXT, 12, 4,
                       rela_text.data, rela_text.len};
  }
  if (rela_data.len) {
    idx_rela_data = nsh;
    sh[nsh++] = (Shdr){".rela.data", SHT_RELA, 0, 0, S_DATA, 12, 4,
                       rela_data.data, rela_data.len};
  }
  int idx_symtab = nsh;
  sh[nsh++] = (Shdr){".symtab", SHT_SYMTAB, 0, 0, first_global, 16, 4,
                     symtab.data, symtab.len};
  int idx_strtab = nsh;
  sh[nsh++] =
      (Shdr){".strtab", SHT_STRTAB, 0, 0, 0, 0, 1, strtab.data, strtab.len};
  // patch symtab link -> strtab, rela link -> symtab
  sh[idx_symtab].link = idx_strtab;
  if (idx_rela_text)
    sh[idx_rela_text].link = idx_symtab;
  if (idx_rela_data)
    sh[idx_rela_data].link = idx_symtab;
  int idx_shstr = nsh;
  sh[nsh++] =
      (Shdr){".shstrtab", SHT_STRTAB, 0, 0, 0, 0, 1, NULL, 0}; // filled below

  // section-name string offsets
  for (int i = 0; i < nsh; i++)
    sh[i].nameoff = sh[i].name ? add_str(&shstr, sh[i].name) : 0;
  sh[idx_shstr].data = shstr.data;
  sh[idx_shstr].size = shstr.len;

  // lay out file offsets: ELF header (52), then each section's data, then SHT.
  uint32_t off = 52;
  for (int i = 0; i < nsh; i++) {
    if (sh[i].type == SHT_NOBITS || sh[i].size == 0 || i == 0) {
      sh[i].sh_offset = off;
      continue;
    }
    if (sh[i].addralign > 1)
      off = (off + sh[i].addralign - 1) / sh[i].addralign * sh[i].addralign;
    sh[i].sh_offset = off;
    off += sh[i].size;
  }
  uint32_t shoff = (off + 3) & ~3u;

  FILE *out = fopen(outpath, "wb");
  if (!out)
    error("emit_elf: cannot open %s for writing", outpath);

  // ELF header (Elf32_Ehdr, big-endian)
  fputc(0x7f, out);
  fputc('E', out);
  fputc('L', out);
  fputc('F', out);
  fputc(1, out); // EI_CLASS = ELFCLASS32
  fputc(2, out); // EI_DATA  = ELFDATA2MSB
  fputc(1, out); // EI_VERSION
  for (int i = 0; i < 9; i++)
    fputc(0, out); // pad EI_NIDENT
  put16(out, ET_REL);
  put16(out, EM_68K);
  put32(out, 1);          // e_version
  put32(out, 0);          // e_entry
  put32(out, 0);          // e_phoff
  put32(out, shoff);      // e_shoff
  put32(out, 0x1000000);  // e_flags (m68000)
  put16(out, 52);         // e_ehsize
  put16(out, 0);          // e_phentsize
  put16(out, 0);          // e_phnum
  put16(out, 40);         // e_shentsize
  put16(out, nsh);        // e_shnum
  put16(out, idx_shstr);  // e_shstrndx

  // section data
  uint32_t pos = 52;
  for (int i = 0; i < nsh; i++) {
    if (sh[i].type == SHT_NOBITS || sh[i].size == 0 || i == 0)
      continue;
    while (pos < sh[i].sh_offset) {
      fputc(0, out);
      pos++;
    }
    fwrite(sh[i].data, 1, sh[i].size, out);
    pos += sh[i].size;
  }
  while (pos < shoff) {
    fputc(0, out);
    pos++;
  }

  // section header table (Elf32_Shdr x nsh)
  for (int i = 0; i < nsh; i++) {
    put32(out, sh[i].nameoff);
    put32(out, sh[i].type);
    put32(out, sh[i].flags);
    put32(out, 0); // sh_addr
    put32(out, sh[i].sh_offset);
    put32(out, (i == 0) ? 0 : (uint32_t)sh[i].size);
    put32(out, sh[i].link);
    put32(out, sh[i].info);
    put32(out, sh[i].addralign);
    put32(out, sh[i].entsize);
  }

  fclose(out);
}
