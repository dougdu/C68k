#include <stdio.h>
#include "libc_internal.h"

/* =====================================================================
 * stdio core state -- the process stream table plus the standard streams.
 * A cohesive object (Phase 4c): fopen/open_memstream claim slots here,
 * fflush(NULL) walks it, and stdin/stdout/stderr alias the first three
 * entries.  It carries no code, so referencing stdout for a printf does not
 * drag fopen/fread/etc. into the link.
 * ===================================================================== */
FILE _streams[NSTREAM] = {
    {0, _SF_READ | _SF_USED, 0, 0, {0}},                  /* stdin  */
    {1, _SF_WRITE | _SF_WRITING | _SF_USED, 0, 0, {0}},   /* stdout */
    {2, _SF_WRITE | _SF_WRITING | _SF_USED, 0, 0, {0}},   /* stderr */
};
FILE *stdin = &_streams[0];
FILE *stdout = &_streams[1];
FILE *stderr = &_streams[2];
