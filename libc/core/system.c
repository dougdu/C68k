#include <stdlib.h>
#include "libc_internal.h"

/* DOS EXEC (4Bh) parameter block -- see osiris.inc EXEC_*.  Only env and the
 * command tail matter for mode 00h; env = 0 makes the child inherit the current
 * environment (so the spawned command processor keeps COMSPEC etc.). */
struct _exec_parm {
  void *env;     /* +0  environment block (0 = inherit) */
  void *cmdtail; /* +4  command tail: [length byte][chars]; the OS appends CR */
  void *fcb1;    /* +8  default FCB #1 (unused here) */
  void *fcb2;    /* +C  default FCB #2 (unused here) */
};

/* Run command through the command processor.  Osiris spawns COMSPEC with a
 * "/C <command>" tail (DOS EXEC) and returns the command's exit code; CP/M-68K
 * has no command processor, so any command fails.  system(NULL) reports whether
 * a processor is available (Osiris yes, CP/M no). */
int system(const char *command) {
  char *comspec = getenv("COMSPEC"); /* the command processor path, or NULL */

  if (!command)
    return comspec ? 1 : 0;
  if (!comspec)
    return -1; /* no command processor (CP/M-68K) */

  /* Build the "/C <command>" tail: a leading DOS length byte, then the chars
   * (clamped to the 127-byte PSP command tail; the OS supplies the CR). */
  char tail[130];
  int len = 0;
  const char *prefix = "/C ";
  for (const char *q = prefix; *q; q++)
    tail[1 + len++] = *q;
  for (const char *p = command; *p && len < 127; p++)
    tail[1 + len++] = *p;
  tail[0] = (char)len;

  struct _exec_parm parm;
  parm.env = 0; /* inherit -> the child keeps COMSPEC */
  parm.cmdtail = tail;
  parm.fcb1 = 0;
  parm.fcb2 = 0;

  return sys_exec(comspec, &parm);
}
