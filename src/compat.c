// c68k --- host platform abstraction implementation.
//
// See compat.h. POSIX branch reproduces chibicc's original fork/exec/wait
// behavior exactly (so the x86-64 self-host stays byte-identical); the Windows
// branch supplies MSVC equivalents. c68k code, not chibicc.

#include "chibicc.h"

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows / MSVC
// ---------------------------------------------------------------------------
// Note: deliberately no <windows.h> --- defining a `noreturn` macro (compat.h)
// clashes with the `__declspec(noreturn)` the Win32 headers contain. We use
// only the CRT here (_tempnam/remove) so no Win32 header is needed.

char *strndup(const char *s, size_t n) {
  size_t len = 0;
  while (len < n && s[len])
    len++;
  char *p = malloc(len + 1);
  if (!p)
    return NULL;
  memcpy(p, s, len);
  p[len] = '\0';
  return p;
}

// mkstemp() over the MSVC CRT: materialize the trailing "XXXXXX" of the
// template, then create the file exclusively.
int mkstemp(char *tmpl) {
  if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
    return -1;
  return _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
               _S_IREAD | _S_IWRITE);
}

// dirname() equivalent that accepts both '/' and '\\'. Operates in place on a
// writable copy (chibicc always passes strdup(argv0)).
char *dirname(char *path) {
  if (!path || !*path)
    return ".";

  size_t len = strlen(path);
  while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\'))
    path[--len] = '\0';

  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;

  if (!slash)
    return ".";
  if (slash == path) {
    path[1] = '\0';
    return path;
  }
  *slash = '\0';
  return path;
}

// basename() equivalent (accepts '/' and '\\'); returns a pointer into the
// caller's buffer. chibicc always passes a writable strdup() copy.
char *basename(char *path) {
  if (!path || !*path)
    return ".";

  size_t len = strlen(path);
  while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\'))
    path[--len] = '\0';

  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;

  return slash ? slash + 1 : path;
}

// ctime_r() over MSVC's ctime_s(). The buffer must hold at least 26 bytes;
// chibicc passes char[30].
char *ctime_r(const time_t *timep, char *buf) {
  if (ctime_s(buf, 26, timep) != 0)
    return NULL;
  return buf;
}

// --- open_memstream() emulation --------------------------------------------
// MSVC has neither open_memstream() nor fopencookie()/fmemopen(), so we back
// the stream with a temp file created in the user's temp dir (avoiding the
// admin-only C:\ behavior of tmpfile()), and finalize on close: read the whole
// file into a malloc'd, NUL-terminated buffer and publish it via *ptr/*sizeloc.
typedef struct MemStream {
  FILE *fp;
  char **ptr;
  size_t *sizeloc;
  char *path;
  struct MemStream *next;
} MemStream;

static MemStream *g_memstreams;

FILE *open_memstream(char **ptr, size_t *sizeloc) {
  char *path = _tempnam(NULL, "c68"); // honors %TMP%; heap-allocated
  if (!path)
    return NULL;

  FILE *fp = fopen(path, "w+b");
  if (!fp) {
    free(path);
    return NULL;
  }

  MemStream *ms = calloc(1, sizeof(*ms));
  ms->fp = fp;
  ms->ptr = ptr;
  ms->sizeloc = sizeloc;
  ms->path = path;
  ms->next = g_memstreams;
  g_memstreams = ms;

  *ptr = NULL;
  *sizeloc = 0;
  return fp;
}

// fclose() is #defined to this in compat.h; undo it here so we can call the
// real one. Non-memstream FILEs fall straight through.
#undef fclose
int c68k_fclose(FILE *fp) {
  for (MemStream **pp = &g_memstreams; *pp; pp = &(*pp)->next) {
    if ((*pp)->fp == fp) {
      MemStream *ms = *pp;
      fflush(fp);

      long len = ftell(fp);
      if (len < 0)
        len = 0;
      fseek(fp, 0, SEEK_SET);

      char *buf = malloc((size_t)len + 1);
      size_t got = buf ? fread(buf, 1, (size_t)len, fp) : 0;
      if (buf)
        buf[got] = '\0';

      *ms->ptr = buf;
      *ms->sizeloc = got;

      int rc = fclose(fp);
      remove(ms->path);
      free(ms->path);
      *pp = ms->next;
      free(ms);
      return rc;
    }
  }
  return fclose(fp);
}

int spawn_and_wait(char **argv) {
  intptr_t rc = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
  if (rc == -1) {
    fprintf(stderr, "exec failed: %s: %s\n", argv[0], strerror(errno));
    return 1;
  }
  return (int)rc;
}

#else
// ---------------------------------------------------------------------------
// POSIX (Linux, macOS) --- identical to chibicc's original run_subprocess body
// ---------------------------------------------------------------------------

int spawn_and_wait(char **argv) {
  if (fork() == 0) {
    execvp(argv[0], argv);
    fprintf(stderr, "exec failed: %s: %s\n", argv[0], strerror(errno));
    _exit(1);
  }

  int status;
  while (wait(&status) > 0)
    ;
  return status;
}

#endif
