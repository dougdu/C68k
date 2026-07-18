// c68k --- host platform abstraction for the compiler driver.
//
// chibicc was written directly against POSIX (Linux). To let the c68k
// cross-compiler be built and maintained on the hosts we actually use ---
// Windows (MSVC) and macOS (Clang) --- this header supplies the small set of
// shims MSVC needs, while on POSIX hosts it simply pulls in the same standard
// headers chibicc included directly. Added in P0; it changes no compiler
// behavior (driver/host glue only).
//
// This is c68k code, not chibicc; see ../LICENSE and src/README.md.

#ifndef C68K_COMPAT_H
#define C68K_COMPAT_H

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows / MSVC
// ---------------------------------------------------------------------------
// Note: no <windows.h> --- a `noreturn` macro (below) collides with the
// `__declspec(noreturn)` inside the Win32 headers, so compat.c uses only the
// CRT. These are the small CRT headers the shims need.
#include <fcntl.h>
#include <io.h>
#include <process.h>

// C11 _Noreturn: MSVC's <stdnoreturn.h> is unreliable across versions, so map
// `noreturn` (used by chibicc's error() decls) to the MSVC intrinsic.
#ifndef noreturn
#define noreturn __declspec(noreturn)
#endif

// Case-insensitive compare + strdup live under underscore names on MSVC.
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#ifndef strdup
#define strdup _strdup
#endif

// Filesystem primitives chibicc calls by their POSIX names.
#define unlink _unlink
#define close _close

// Not provided by the MSVC CRT --- implemented in compat.c.
char *strndup(const char *s, size_t n);
int mkstemp(char *tmpl);
char *dirname(char *path);
char *basename(char *path);
char *ctime_r(const time_t *timep, char *buf);

// MSVC has no open_memstream(). We emulate it with a finalize-on-close temp
// stream so *ptr / *sizeloc are valid after fclose(), which is how chibicc uses
// it. fclose() is therefore routed through our finalizer.
FILE *open_memstream(char **ptr, size_t *sizeloc);
int c68k_fclose(FILE *fp);
#define fclose(f) c68k_fclose(f)

#else
// ---------------------------------------------------------------------------
// POSIX (Linux, macOS) --- the headers chibicc used directly
// ---------------------------------------------------------------------------
#include <glob.h>
#include <libgen.h>
#include <stdnoreturn.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

// Spawn a NULL-terminated argv and wait for it; returns 0 on success, non-zero
// otherwise. POSIX uses fork()+execvp()+wait(); Windows uses _spawnvp(_P_WAIT).
int spawn_and_wait(char **argv);

#endif // C68K_COMPAT_H
