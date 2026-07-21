#include "chibicc.h"

#define C68K_VERSION "0.1.0"

typedef enum {
  FILE_NONE, FILE_C, FILE_ASM, FILE_OBJ, FILE_AR, FILE_DSO,
} FileType;

// Target OS. The code generator and the emitted object are identical for both
// OSes -- the platform split lives in the libc backend and the link step -- so
// a -target only selects the predefined macros, letting one source tree #ifdef.
typedef enum { TGT_NONE, TGT_OSIRIS, TGT_CPM } TargetOS;

StringArray include_paths;
bool opt_fcommon = true;
bool opt_fpic;
bool opt_ffreestanding;
bool opt_integrated_as;
int opt_level;
bool opt_Werror;
bool opt_g;

static FileType opt_x;
static StringArray opt_include;
static bool opt_E;
static bool opt_M;
static bool opt_MD;
static bool opt_MMD;
static bool opt_MP;
static bool opt_S;
static bool opt_c;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static bool opt_static;
static bool opt_shared;
static char *opt_MF;
static char *opt_MT;
static char *opt_o;
static TargetOS opt_target;

static StringArray ld_extra_args;
static StringArray std_include_paths;

char *base_file;
static char *output_file;

static StringArray input_paths;
static StringArray tmpfiles;

static void usage(int status) {
  FILE *out = status ? stderr : stdout;
  fprintf(out,
      "c68k %s -- a C99 compiler for the Motorola 68000\n"
      "usage: c68k [options] file...\n"
      "\n"
      "  -c              compile and assemble to an object (.o); do not link\n"
      "  -S              compile to 68000 assembly (.s)\n"
      "  -E              preprocess only\n"
      "  -o <path>       write output to <path> ('-' = stdout)\n"
      "  -I <dir>        add <dir> to the include search path\n"
      "  -D <m>[=v]      define macro <m> (default value 1)\n"
      "  -U <m>          undefine macro <m>\n"
      "  -include <f>    process <f> as if '#include \"<f>\"' came first\n"
      "  -target <os>    predefine target macros: 'osiris' or 'cpm'\n"
      "  -O0..-O3, -Os   optimization level (-O0 default; >=1 enables the tier)\n"
      "  -g              emit DWARF debug info (integrated assembler)\n"
      "  -Werror         treat warnings as errors\n"
      "  -ffreestanding  freestanding environment (__STDC_HOSTED__=0)\n"
      "  -fpic, -fPIC    position-independent code\n"
      "  --version       print version and exit\n"
      "  --help          print this help and exit\n"
      "\n"
      "The emitted object is OS-neutral; choose the executable container at link\n"
      "time (osiris-prg.ld for a .PRG, or cpm68k.ld + mkdri for a .68K).\n"
      "Based on chibicc (MIT); see src/CHIBICC-LICENSE.\n",
      C68K_VERSION);
  exit(status);
}

static void version(void) {
  printf("c68k %s\n", C68K_VERSION);
  exit(0);
}

static TargetOS parse_target(char *s) {
  if (!strcmp(s, "osiris") || !strcmp(s, "os68k") || !strcmp(s, "os/68k"))
    return TGT_OSIRIS;
  if (!strcmp(s, "cpm") || !strcmp(s, "cpm68k") || !strcmp(s, "cpm-68k"))
    return TGT_CPM;
  error("<command line>: unknown -target: %s (want 'osiris' or 'cpm')", s);
}

static bool take_arg(char *arg) {
  char *x[] = {
    "-o", "-I", "-idirafter", "-include", "-x", "-MF", "-MT", "-Xlinker",
    "-target", "--target",
  };

  for (int i = 0; i < sizeof(x) / sizeof(*x); i++)
    if (!strcmp(arg, x[i]))
      return true;
  return false;
}

static void add_default_include_paths(char *argv0) {
  // We expect that chibicc-specific include files are installed
  // to ./include relative to argv[0].
  strarray_push(&include_paths, format("%s/include", dirname(strdup(argv0))));

  // Add standard include paths.
  strarray_push(&include_paths, "/usr/local/include");
  strarray_push(&include_paths, "/usr/include/x86_64-linux-gnu");
  strarray_push(&include_paths, "/usr/include");

  // Keep a copy of the standard include paths for -MMD option.
  for (int i = 0; i < include_paths.len; i++)
    strarray_push(&std_include_paths, include_paths.data[i]);
}

static void define(char *str) {
  char *eq = strchr(str, '=');
  if (eq)
    define_macro(strndup(str, eq - str), eq + 1);
  else
    define_macro(str, "1");
}

static FileType parse_opt_x(char *s) {
  if (!strcmp(s, "c"))
    return FILE_C;
  if (!strcmp(s, "assembler"))
    return FILE_ASM;
  if (!strcmp(s, "none"))
    return FILE_NONE;
  error("<command line>: unknown argument for -x: %s", s);
}

static char *quote_makefile(char *s) {
  char *buf = calloc(1, strlen(s) * 2 + 1);

  for (int i = 0, j = 0; s[i]; i++) {
    switch (s[i]) {
    case '$':
      buf[j++] = '$';
      buf[j++] = '$';
      break;
    case '#':
      buf[j++] = '\\';
      buf[j++] = '#';
      break;
    case ' ':
    case '\t':
      for (int k = i - 1; k >= 0 && s[k] == '\\'; k--)
        buf[j++] = '\\';
      buf[j++] = '\\';
      buf[j++] = s[i];
      break;
    default:
      buf[j++] = s[i];
      break;
    }
  }
  return buf;
}

static void parse_args(int argc, char **argv) {
  // Make sure that all command line options that take an argument
  // have an argument.
  for (int i = 1; i < argc; i++)
    if (take_arg(argv[i]))
      if (!argv[++i])
        usage(1);

  StringArray idirafter = {};

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-###")) {
      opt_hash_hash_hash = true;
      continue;
    }

    if (!strcmp(argv[i], "-cc1")) {
      opt_cc1 = true;
      continue;
    }

    if (!strcmp(argv[i], "--help"))
      usage(0);

    if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v"))
      version();

    if (!strcmp(argv[i], "-target") || !strcmp(argv[i], "--target")) {
      opt_target = parse_target(argv[++i]);
      continue;
    }

    if (!strncmp(argv[i], "-target=", 8)) {
      opt_target = parse_target(argv[i] + 8);
      continue;
    }

    if (!strncmp(argv[i], "--target=", 9)) {
      opt_target = parse_target(argv[i] + 9);
      continue;
    }

    if (!strcmp(argv[i], "-o")) {
      opt_o = argv[++i];
      continue;
    }

    if (!strncmp(argv[i], "-o", 2)) {
      opt_o = argv[i] + 2;
      continue;
    }

    if (!strcmp(argv[i], "-S")) {
      opt_S = true;
      continue;
    }

    if (!strcmp(argv[i], "-fcommon")) {
      opt_fcommon = true;
      continue;
    }

    if (!strcmp(argv[i], "-fno-common")) {
      opt_fcommon = false;
      continue;
    }

    if (!strcmp(argv[i], "-ffreestanding")) {
      opt_ffreestanding = true;
      continue;
    }

    if (!strcmp(argv[i], "-c")) {
      opt_c = true;
      continue;
    }

    if (!strcmp(argv[i], "-E")) {
      opt_E = true;
      continue;
    }

    if (!strncmp(argv[i], "-I", 2)) {
      strarray_push(&include_paths, argv[i] + 2);
      continue;
    }

    if (!strcmp(argv[i], "-D")) {
      define(argv[++i]);
      continue;
    }

    if (!strncmp(argv[i], "-D", 2)) {
      define(argv[i] + 2);
      continue;
    }

    if (!strcmp(argv[i], "-U")) {
      undef_macro(argv[++i]);
      continue;
    }

    if (!strncmp(argv[i], "-U", 2)) {
      undef_macro(argv[i] + 2);
      continue;
    }

    if (!strcmp(argv[i], "-include")) {
      strarray_push(&opt_include, argv[++i]);
      continue;
    }

    if (!strcmp(argv[i], "-x")) {
      opt_x = parse_opt_x(argv[++i]);
      continue;
    }

    if (!strncmp(argv[i], "-x", 2)) {
      opt_x = parse_opt_x(argv[i] + 2);
      continue;
    }

    if (!strncmp(argv[i], "-l", 2) || !strncmp(argv[i], "-Wl,", 4)) {
      strarray_push(&input_paths, argv[i]);
      continue;
    }

    if (!strcmp(argv[i], "-Xlinker")) {
      strarray_push(&ld_extra_args, argv[++i]);
      continue;
    }

    if (!strcmp(argv[i], "-s")) {
      strarray_push(&ld_extra_args, "-s");
      continue;
    }

    if (!strcmp(argv[i], "-M")) {
      opt_M = true;
      continue;
    }

    if (!strcmp(argv[i], "-MF")) {
      opt_MF = argv[++i];
      continue;
    }

    if (!strcmp(argv[i], "-MP")) {
      opt_MP = true;
      continue;
    }

    if (!strcmp(argv[i], "-MT")) {
      if (opt_MT == NULL)
        opt_MT = argv[++i];
      else
        opt_MT = format("%s %s", opt_MT, argv[++i]);
      continue;
    }

    if (!strcmp(argv[i], "-MD")) {
      opt_MD = true;
      continue;
    }

    if (!strcmp(argv[i], "-MQ")) {
      if (opt_MT == NULL)
        opt_MT = quote_makefile(argv[++i]);
      else
        opt_MT = format("%s %s", opt_MT, quote_makefile(argv[++i]));
      continue;
    }

    if (!strcmp(argv[i], "-MMD")) {
      opt_MD = opt_MMD = true;
      continue;
    }

    if (!strcmp(argv[i], "-fpic") || !strcmp(argv[i], "-fPIC")) {
      opt_fpic = true;
      continue;
    }

    if (!strcmp(argv[i], "-fintegrated-as")) {
      opt_integrated_as = true;
      continue;
    }

    if (!strcmp(argv[i], "-fno-integrated-as")) {
      opt_integrated_as = false;
      continue;
    }

    if (!strcmp(argv[i], "-cc1-input")) {
      base_file = argv[++i];
      continue;
    }

    if (!strcmp(argv[i], "-cc1-output")) {
      output_file = argv[++i];
      continue;
    }

    if (!strcmp(argv[i], "-idirafter")) {
      strarray_push(&idirafter, argv[i++]);
      continue;
    }

    if (!strcmp(argv[i], "-static")) {
      opt_static = true;
      strarray_push(&ld_extra_args, "-static");
      continue;
    }

    if (!strcmp(argv[i], "-shared")) {
      opt_shared = true;
      strarray_push(&ld_extra_args, "-shared");
      continue;
    }

    if (!strcmp(argv[i], "-L")) {
      strarray_push(&ld_extra_args, "-L");
      strarray_push(&ld_extra_args, argv[++i]);
      continue;
    }

    if (!strncmp(argv[i], "-L", 2)) {
      strarray_push(&ld_extra_args, "-L");
      strarray_push(&ld_extra_args, argv[i] + 2);
      continue;
    }

    if (!strcmp(argv[i], "-hashmap-test")) {
      hashmap_test();
      exit(0);
    }

    // -Werror turns warnings into hard errors (see warn_tok). Other -W* flags
    // are accepted and ignored (chibicc's warning set is minimal).
    if (!strcmp(argv[i], "-Werror")) {
      opt_Werror = true;
      continue;
    }

    // Optimization level. -O0 = naive stack-machine codegen (the default);
    // -O and -O1..-O3/-Os/-Ofast enable the P12 back-end optimizations
    // (immediate-operand selection, strength reduction, peephole). Anything
    // above 1 is currently treated as 1.
    if (!strncmp(argv[i], "-O", 2)) {
      char *p = argv[i] + 2;
      if (*p == '\0' || !strcmp(p, "fast"))
        opt_level = 1;
      else if (p[0] >= '0' && p[0] <= '9')
        opt_level = (p[0] == '0') ? 0 : 1;
      else if (!strcmp(p, "s") || !strcmp(p, "z"))
        opt_level = 1;
      continue;
    }

    // Debug info. -g emits DWARF line/symbol info (via the integrated
    // assembler); -g0 explicitly disables it. Other -g variants map to on.
    if (!strncmp(argv[i], "-g", 2)) {
      opt_g = strcmp(argv[i], "-g0") != 0;
      continue;
    }

    // These options are ignored for now.
    if (!strncmp(argv[i], "-W", 2) ||
        !strncmp(argv[i], "-std=", 5) ||
        !strcmp(argv[i], "-fno-builtin") ||
        !strcmp(argv[i], "-fno-omit-frame-pointer") ||
        !strcmp(argv[i], "-fno-stack-protector") ||
        !strcmp(argv[i], "-fno-strict-aliasing") ||
        !strcmp(argv[i], "-m64") ||
        !strcmp(argv[i], "-mno-red-zone") ||
        !strcmp(argv[i], "-w"))
      continue;

    if (argv[i][0] == '-' && argv[i][1] != '\0')
      error("unknown argument: %s", argv[i]);

    strarray_push(&input_paths, argv[i]);
  }

  for (int i = 0; i < idirafter.len; i++)
    strarray_push(&include_paths, idirafter.data[i]);

  // Compiler-identity and target predefined macros. __c68k__ lets code detect
  // this compiler; -target adds the OS macro so one source tree can #ifdef.
  define_macro("__c68k__", "1");
  if (opt_target == TGT_OSIRIS)
    define_macro("__osiris__", "1");
  else if (opt_target == TGT_CPM)
    define_macro("__CPM68K__", "1");

  if (input_paths.len == 0)
    error("no input files");

  // -E implies that the input is the C macro language.
  if (opt_E)
    opt_x = FILE_C;
}

static FILE *open_file(char *path) {
  if (!path || strcmp(path, "-") == 0)
    return stdout;

  FILE *out = fopen(path, "w");
  if (!out)
    error("cannot open output file: %s: %s", path, strerror(errno));
  return out;
}

// Case-insensitive suffix test.  The Osiris/CP/M-68K filesystems are
// case-folding (filenames arrive uppercase, e.g. FOO.C / FOO.O), so file-type
// detection must accept an uppercase extension as readily as a lowercase one.
static bool endswith_ci(char *p, char *q) {
  int len1 = strlen(p);
  int len2 = strlen(q);
  if (len1 < len2)
    return false;
  p += len1 - len2;
  for (int i = 0; i < len2; i++) {
    char a = p[i], b = q[i];
    if (a >= 'A' && a <= 'Z')
      a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z')
      b += 'a' - 'A';
    if (a != b)
      return false;
  }
  return true;
}

// Replace file extension
static char *replace_extn(char *tmpl, char *extn) {
  char *filename = basename(strdup(tmpl));
  char *dot = strrchr(filename, '.');
  if (dot)
    *dot = '\0';
  return format("%s%s", filename, extn);
}

static void cleanup(void) {
  for (int i = 0; i < tmpfiles.len; i++)
    unlink(tmpfiles.data[i]);
}

static char *create_tmpfile(void) {
#ifdef C68K_SELFHOST
  // The native compiler compiles one .c per invocation, so a fixed name for
  // the intermediate assembly is sufficient (between cc1() and assemble_to_elf).
  char *path = "CC_TMP.S";
#elif defined(_WIN32)
  // On Windows asm68K reads a leading '/' as a switch, so temp source paths
  // must be native (backslashed, under %TMP%). _tempnam honors %TMP% and
  // returns a Windows path; a `.a68` extension keeps it a conventional source.
  char *base = _tempnam(NULL, "c68k");
  if (!base)
    error("failed to create a temporary file");
  char *path = format("%s.a68", base);
  free(base);
  FILE *fp = fopen(path, "wb");
  if (!fp)
    error("cannot open temporary file: %s", path);
  fclose(fp);
#else
  char *path = strdup("/tmp/chibicc-XXXXXX");
  int fd = mkstemp(path);
  if (fd == -1)
    error("mkstemp failed: %s", strerror(errno));
  close(fd);
#endif

  strarray_push(&tmpfiles, path);
  return path;
}

#ifndef C68K_SELFHOST
static void run_subprocess(char **argv) {
  // If -### is given, dump the subprocess's command line.
  if (opt_hash_hash_hash) {
    fprintf(stderr, "%s", argv[0]);
    for (int i = 1; argv[i]; i++)
      fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");
  }

  // Spawn the child and wait (fork/exec on POSIX, _spawnvp on Windows).
  if (spawn_and_wait(argv) != 0)
    exit(1);
}

static void run_cc1(int argc, char **argv, char *input, char *output) {
  char **args = calloc(argc + 10, sizeof(char *));
  memcpy(args, argv, argc * sizeof(char *));
  args[argc++] = "-cc1";

  if (input) {
    args[argc++] = "-cc1-input";
    args[argc++] = input;
  }

  if (output) {
    args[argc++] = "-cc1-output";
    args[argc++] = output;
  }

  run_subprocess(args);
}
#endif // !C68K_SELFHOST

// Print tokens to stdout. Used for -E.
static void print_tokens(Token *tok) {
  FILE *out = open_file(opt_o ? opt_o : "-");

  int line = 1;
  for (; tok->kind != TK_EOF; tok = tok->next) {
    if (line > 1 && tok->at_bol)
      fprintf(out, "\n");
    if (tok->has_space && !tok->at_bol)
      fprintf(out, " ");
    fprintf(out, "%.*s", tok->len, tok->loc);
    line++;
  }
  fprintf(out, "\n");
}

static bool in_std_include_path(char *path) {
  for (int i = 0; i < std_include_paths.len; i++) {
    char *dir = std_include_paths.data[i];
    int len = strlen(dir);
    if (strncmp(dir, path, len) == 0 && path[len] == '/')
      return true;
  }
  return false;
}

// If -M options is given, the compiler write a list of input files to
// stdout in a format that "make" command can read. This feature is
// used to automate file dependency management.
static void print_dependencies(void) {
  char *path;
  if (opt_MF)
    path = opt_MF;
  else if (opt_MD)
    path = replace_extn(opt_o ? opt_o : base_file, ".d");
  else if (opt_o)
    path = opt_o;
  else
    path = "-";

  FILE *out = open_file(path);
  if (opt_MT)
    fprintf(out, "%s:", opt_MT);
  else
    fprintf(out, "%s:", quote_makefile(replace_extn(base_file, ".o")));

  File **files = get_input_files();

  for (int i = 0; files[i]; i++) {
    if (opt_MMD && in_std_include_path(files[i]->name))
      continue;
    fprintf(out, " \\\n  %s", files[i]->name);
  }

  fprintf(out, "\n\n");

  if (opt_MP) {
    for (int i = 1; files[i]; i++) {
      if (opt_MMD && in_std_include_path(files[i]->name))
        continue;
      fprintf(out, "%s:\n\n", quote_makefile(files[i]->name));
    }
  }
}

static Token *must_tokenize_file(char *path) {
  Token *tok = tokenize_file(path);
  if (!tok)
    error("%s: %s", path, strerror(errno));
  return tok;
}

static Token *append_tokens(Token *tok1, Token *tok2) {
  if (!tok1 || tok1->kind == TK_EOF)
    return tok2;

  Token *t = tok1;
  while (t->next->kind != TK_EOF)
    t = t->next;
  t->next = tok2;
  return tok1;
}

static void cc1(void) {
  Token *tok = NULL;

  // Process -include option
  for (int i = 0; i < opt_include.len; i++) {
    char *incl = opt_include.data[i];

    char *path;
    if (file_exists(incl)) {
      path = incl;
    } else {
      path = search_include_paths(incl);
      if (!path)
        error("-include: %s: %s", incl, strerror(errno));
    }

    Token *tok2 = must_tokenize_file(path);
    tok = append_tokens(tok, tok2);
  }

  // Tokenize and parse.
  Token *tok2 = must_tokenize_file(base_file);
  tok = append_tokens(tok, tok2);
  tok = preprocess(tok);

  // If -M or -MD are given, print file dependencies.
  if (opt_M || opt_MD) {
    print_dependencies();
    if (opt_M)
      return;
  }

  // If -E is given, print out preprocessed C code as a result.
  if (opt_E) {
    print_tokens(tok);
    return;
  }

  Obj *prog = parse(tok);

  // Open a temporary output buffer.
  char *buf;
  size_t buflen;
  FILE *output_buf = open_memstream(&buf, &buflen);

  // Traverse the AST to emit assembly.
  codegen(prog, output_buf);
  fclose(output_buf);

  // Write the asembly text to a file.
  FILE *out = open_file(output_file);
  fwrite(buf, buflen, 1, out);
  fclose(out);
}

static void assemble(char *input, char *output) {
  // c68k emits Motorola-syntax assembly assembled by asm68K into an ELF32-BE
  // object. asm68K is not strict about the source extension, so the driver's
  // temp file is passed directly. (Slash switches; asm68K is a Windows tool.)
  if (opt_integrated_as) {
    // Integrated path: no external assembler (P8).
    assemble_to_elf(input, output);
    return;
  }
#ifndef C68K_SELFHOST
  char *cmd[] = {"asm68K", "/Cx", "/elf", "/c", "/nologo",
                 format("/Fo%s", output), input, NULL};
  run_subprocess(cmd);
#endif
}

#if !defined(_WIN32) && !defined(C68K_SELFHOST)
static char *find_file(char *pattern) {
  char *path = NULL;
  glob_t buf = {};
  glob(pattern, 0, NULL, &buf);
  if (buf.gl_pathc > 0)
    path = strdup(buf.gl_pathv[buf.gl_pathc - 1]);
  globfree(&buf);
  return path;
}
#endif // !_WIN32

// Returns true if a given file exists.
#ifdef C68K_SELFHOST
bool file_exists(char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  fclose(f);
  return true;
}
#else
bool file_exists(char *path) {
  struct stat st;
  return !stat(path, &st);
}
#endif

#if !defined(_WIN32) && !defined(C68K_SELFHOST)
static char *find_libpath(void) {
  if (file_exists("/usr/lib/x86_64-linux-gnu/crti.o"))
    return "/usr/lib/x86_64-linux-gnu";
  if (file_exists("/usr/lib64/crti.o"))
    return "/usr/lib64";
  error("library path is not found");
}

static char *find_gcc_libpath(void) {
  char *paths[] = {
    "/usr/lib/gcc/x86_64-linux-gnu/*/crtbegin.o",
    "/usr/lib/gcc/x86_64-pc-linux-gnu/*/crtbegin.o", // For Gentoo
    "/usr/lib/gcc/x86_64-redhat-linux/*/crtbegin.o", // For Fedora
  };

  for (int i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
    char *path = find_file(paths[i]);
    if (path)
      return dirname(path);
  }

  error("gcc library path is not found");
}

static void run_linker(StringArray *inputs, char *output) {
  StringArray arr = {};

  strarray_push(&arr, "ld");
  strarray_push(&arr, "-o");
  strarray_push(&arr, output);
  strarray_push(&arr, "-m");
  strarray_push(&arr, "elf_x86_64");

  char *libpath = find_libpath();
  char *gcc_libpath = find_gcc_libpath();

  if (opt_shared) {
    strarray_push(&arr, format("%s/crti.o", libpath));
    strarray_push(&arr, format("%s/crtbeginS.o", gcc_libpath));
  } else {
    strarray_push(&arr, format("%s/crt1.o", libpath));
    strarray_push(&arr, format("%s/crti.o", libpath));
    strarray_push(&arr, format("%s/crtbegin.o", gcc_libpath));
  }

  strarray_push(&arr, format("-L%s", gcc_libpath));
  strarray_push(&arr, "-L/usr/lib/x86_64-linux-gnu");
  strarray_push(&arr, "-L/usr/lib64");
  strarray_push(&arr, "-L/lib64");
  strarray_push(&arr, "-L/usr/lib/x86_64-linux-gnu");
  strarray_push(&arr, "-L/usr/lib/x86_64-pc-linux-gnu");
  strarray_push(&arr, "-L/usr/lib/x86_64-redhat-linux");
  strarray_push(&arr, "-L/usr/lib");
  strarray_push(&arr, "-L/lib");

  if (!opt_static) {
    strarray_push(&arr, "-dynamic-linker");
    strarray_push(&arr, "/lib64/ld-linux-x86-64.so.2");
  }

  for (int i = 0; i < ld_extra_args.len; i++)
    strarray_push(&arr, ld_extra_args.data[i]);

  for (int i = 0; i < inputs->len; i++)
    strarray_push(&arr, inputs->data[i]);

  if (opt_static) {
    strarray_push(&arr, "--start-group");
    strarray_push(&arr, "-lgcc");
    strarray_push(&arr, "-lgcc_eh");
    strarray_push(&arr, "-lc");
    strarray_push(&arr, "--end-group");
  } else {
    strarray_push(&arr, "-lc");
    strarray_push(&arr, "-lgcc");
    strarray_push(&arr, "--as-needed");
    strarray_push(&arr, "-lgcc_s");
    strarray_push(&arr, "--no-as-needed");
  }

  if (opt_shared)
    strarray_push(&arr, format("%s/crtendS.o", gcc_libpath));
  else
    strarray_push(&arr, format("%s/crtend.o", gcc_libpath));

  strarray_push(&arr, format("%s/crtn.o", libpath));
  strarray_push(&arr, NULL);

  run_subprocess(arr.data);
}
#else  // _WIN32 or C68K_SELFHOST: no in-driver linker
// x86-64 linking drives GNU ld with Linux crt/libc paths (a POSIX-host
// facility).  On Windows the baseline runs only the front-end checks; native
// c68k does not link at all -- LINK.PRG / LINK.68K do, as a separate step.
static void run_linker(StringArray *inputs, char *output) {
  (void)inputs;
  (void)output;
  error("this c68k build does not link; compile with -c and link separately");
}
#endif

static FileType get_file_type(char *filename) {
  if (opt_x != FILE_NONE)
    return opt_x;

  if (endswith_ci(filename, ".a"))
    return FILE_AR;
  if (endswith_ci(filename, ".so"))
    return FILE_DSO;
  if (endswith_ci(filename, ".o"))
    return FILE_OBJ;
  if (endswith_ci(filename, ".c"))
    return FILE_C;
  if (endswith_ci(filename, ".s"))
    return FILE_ASM;

  error("<command line>: unknown file extension: %s", filename);
}

#ifdef C68K_SELFHOST
// Bump-heap arena mark/release (libc/core/libc.c), used to reclaim the
// front-end between cc1 and the integrated assembler on tiny heaps.
void *__heap_mark(void);
void __heap_release(void *mark);

// Native driver (Osiris / CP/M-68K): compile C to ELF objects in-process with
// the integrated emitter.  There is no subprocess, no external assembler, and
// no linker -- linking is a separate step (LINK.PRG / LINK.68K).
int main(int argc, char **argv) {
  atexit(cleanup);
  init_macros();
  opt_integrated_as = true;
  parse_args(argc, argv);
  if (opt_ffreestanding)
    define_macro("__STDC_HOSTED__", "0");
  add_default_include_paths(argv[0]);

  if (input_paths.len > 1 && opt_o && (opt_c || opt_S || opt_E))
    error("cannot specify '-o' with '-c'/'-S'/'-E' and multiple files");

  for (int i = 0; i < input_paths.len; i++) {
    char *input = input_paths.data[i];
    int ft = get_file_type(input);
    if (ft != FILE_C && ft != FILE_ASM)
      error("%s: native c68k compiles only C sources (link separately)", input);

    char *output;
    if (opt_o)
      output = opt_o;
    else if (opt_S)
      output = replace_extn(input, ".s");
    else
      output = replace_extn(input, ".o");

    base_file = input;

    // A pre-assembled .s input: run only the integrated assembler.
    if (ft == FILE_ASM) {
      assemble_to_elf(input, output);
      continue;
    }

    // -E / -M (preprocess) or -S (assembly): cc1 writes straight to the output.
    if (opt_E || opt_M || opt_S) {
      output_file = opt_S ? output : (opt_o ? opt_o : "-");
      cc1();
      continue;
    }

    // Default: compile to a temp .s, then assemble to an ELF .o.
    char *tmp = create_tmpfile();
    output_file = tmp;
    // The front-end (cc1) hands off to the integrated assembler entirely via
    // the .s file on disk, so all of cc1's memory (tokens/AST/codegen buffer)
    // is dead once it returns. On the tiny CP/M heap, mark the heap before
    // cc1 and release it after, so the assembler runs from a near-empty heap
    // instead of on top of the front-end's leaked allocations. Only safe for
    // a single-file compile: releasing would also free global macro/intern
    // state that a subsequent file would still need.
    void *heap_mark = (input_paths.len == 1) ? __heap_mark() : NULL;
    cc1();
    if (heap_mark)
      __heap_release(heap_mark);
    assemble_to_elf(tmp, output);
  }
  return 0;
}
#else
int main(int argc, char **argv) {
  atexit(cleanup);
  init_macros();
  parse_args(argc, argv);

  // -ffreestanding: the hosted library is not assumed present. This is set
  // here (not in init_macros) because the flag is only known after arg parse.
  if (opt_ffreestanding)
    define_macro("__STDC_HOSTED__", "0");

  if (opt_cc1) {
    add_default_include_paths(argv[0]);
    cc1();
    return 0;
  }

  if (input_paths.len > 1 && opt_o && (opt_c || opt_S | opt_E))
    error("cannot specify '-o' with '-c,' '-S' or '-E' with multiple files");

  StringArray ld_args = {};

  for (int i = 0; i < input_paths.len; i++) {
    char *input = input_paths.data[i];

    if (!strncmp(input, "-l", 2)) {
      strarray_push(&ld_args, input);
      continue;
    }

    if (!strncmp(input, "-Wl,", 4)) {
      char *s = strdup(input + 4);
      char *arg = strtok(s, ",");
      while (arg) {
        strarray_push(&ld_args, arg);
        arg = strtok(NULL, ",");
      }
      continue;
    }

    char *output;
    if (opt_o)
      output = opt_o;
    else if (opt_S)
      output = replace_extn(input, ".s");
    else
      output = replace_extn(input, ".o");

    FileType type = get_file_type(input);

    // Handle .o or .a
    if (type == FILE_OBJ || type == FILE_AR || type == FILE_DSO) {
      strarray_push(&ld_args, input);
      continue;
    }

    // Handle .s
    if (type == FILE_ASM) {
      if (!opt_S)
        assemble(input, output);
      continue;
    }

    assert(type == FILE_C);

    // Just preprocess
    if (opt_E || opt_M) {
      run_cc1(argc, argv, input, NULL);
      continue;
    }

    // Compile
    if (opt_S) {
      run_cc1(argc, argv, input, output);
      continue;
    }

    // Compile and assemble
    if (opt_c) {
      char *tmp = create_tmpfile();
      run_cc1(argc, argv, input, tmp);
      assemble(tmp, output);
      continue;
    }

    // Compile, assemble and link
    char *tmp1 = create_tmpfile();
    char *tmp2 = create_tmpfile();
    run_cc1(argc, argv, input, tmp1);
    assemble(tmp1, tmp2);
    strarray_push(&ld_args, tmp2);
    continue;
  }

  if (ld_args.len > 0)
    run_linker(&ld_args, opt_o ? opt_o : "a.out");
  return 0;
}
#endif // C68K_SELFHOST
