/*
 * cpm.c --- c68k CP/M-68K syscall seam: the sys_* functions libc calls,
 * implemented over BDOS (TRAP #2) via the asm cpm_bdos primitive.
 *
 * CP/M has no file handles: files are FCB + 128-byte records moved through a
 * DMA buffer. This shim keeps a small table mapping an fd (>=3) to an FCB and
 * a 128-byte record buffer, and translates byte-granular sys_read/sys_write
 * into whole-record BDOS reads/writes so libc's stdio byte stream just works.
 * fds 0/1/2 route to the console (BDOS 6, raw I/O). exit()/sbrk live in
 * cpm_sys.a68.
 */

#include <ctype.h>

extern long cpm_bdos(int func, long param);

/* BDOS function codes. */
#define C_RAWIO 6
#define F_OPEN 15
#define F_CLOSE 16
#define F_DELETE 19
#define F_READ 20
#define F_WRITE 21
#define F_MAKE 22
#define F_RENAME 23
#define F_DMAOFF 26
#define F_READRAND 33
#define F_WRITERAND 34
#define F_SIZE 35
#define T_GET 105 /* Worm CP/M-68K BDOS clock extension (BDOSEXT) */

/* FCB field offsets. */
#define FCB_DRIVE 0
#define FCB_NAME 1
#define FCB_TYPE 9
#define FCB_CR 32
#define FCB_SIZE 36

#define NFILE 8
#define RECSZ 128

typedef struct {
  int used;
  int writing;
  int eof;
  int recpos; /* read: next byte in rec; write: bytes buffered */
  int reccnt; /* read: valid bytes in rec */
  long pos;   /* absolute byte offset of the next sys_read/sys_write byte */
  unsigned char fcb[FCB_SIZE];
  unsigned char rec[RECSZ];
} CpmFile;

static CpmFile _cpmf[NFILE];

/* Parse an ASCIIZ path ("D:NAME.EXT") into a zeroed FCB: optional drive,
   uppercase 8.3 name, space-padded. */
static void parse_fcb(unsigned char *fcb, const char *path) {
  for (int i = 0; i < FCB_SIZE; i++)
    fcb[i] = 0;

  if (path[0] && path[1] == ':') {
    fcb[FCB_DRIVE] = (unsigned char)(toupper((unsigned char)path[0]) - 'A' + 1);
    path += 2;
  }

  int i = 0;
  while (i < 8 && *path && *path != '.')
    fcb[FCB_NAME + i++] = (unsigned char)toupper((unsigned char)*path++);
  while (i < 8)
    fcb[FCB_NAME + i++] = ' ';
  while (*path && *path != '.')
    path++;
  if (*path == '.')
    path++;

  i = 0;
  while (i < 3 && *path)
    fcb[FCB_TYPE + i++] = (unsigned char)toupper((unsigned char)*path++);
  while (i < 3)
    fcb[FCB_TYPE + i++] = ' ';
}

static CpmFile *alloc_slot(int *idx) {
  for (int i = 0; i < NFILE; i++)
    if (!_cpmf[i].used) {
      *idx = i;
      return &_cpmf[i];
    }
  return 0;
}

int sys_open(const char *path, int mode) {
  (void)mode;
  int i;
  CpmFile *f = alloc_slot(&i);
  if (!f)
    return -1;
  parse_fcb(f->fcb, path);
  f->fcb[FCB_CR] = 0;
  if ((cpm_bdos(F_OPEN, (long)f->fcb) & 0xFF) == 0xFF)
    return -1;
  f->used = 1;
  f->writing = 0;
  f->eof = 0;
  f->recpos = 0;
  f->reccnt = 0;
  f->pos = 0;
  return 3 + i;
}

int sys_creat(const char *path, int attr) {
  (void)attr;
  int i;
  CpmFile *f = alloc_slot(&i);
  if (!f)
    return -1;
  parse_fcb(f->fcb, path);
  cpm_bdos(F_DELETE, (long)f->fcb); /* truncate: remove any existing file */
  parse_fcb(f->fcb, path);
  f->fcb[FCB_CR] = 0;
  if ((cpm_bdos(F_MAKE, (long)f->fcb) & 0xFF) == 0xFF)
    return -1;
  f->used = 1;
  f->writing = 1;
  f->eof = 0;
  f->recpos = 0;
  f->reccnt = 0;
  f->pos = 0;
  return 3 + i;
}

int sys_write(int fd, const void *buf, int n) {
  const unsigned char *p = buf;
  if (fd < 3) {
    for (int i = 0; i < n; i++)
      cpm_bdos(C_RAWIO, p[i]);
    return n;
  }
  CpmFile *f = &_cpmf[fd - 3];
  for (int i = 0; i < n; i++) {
    f->rec[f->recpos++] = p[i];
    if (f->recpos == RECSZ) {
      cpm_bdos(F_DMAOFF, (long)f->rec);
      if (cpm_bdos(F_WRITE, (long)f->fcb) != 0) {
        f->pos += i;
        return i;
      }
      f->recpos = 0;
    }
  }
  f->pos += n;
  return n;
}

int sys_read(int fd, void *buf, int n) {
  unsigned char *p = buf;
  if (fd < 3) {
    for (int i = 0; i < n; i++)
      p[i] = (unsigned char)cpm_bdos(C_RAWIO, 0xFF);
    return n;
  }
  CpmFile *f = &_cpmf[fd - 3];
  int got = 0;
  for (; got < n; got++) {
    if (f->recpos >= f->reccnt) {
      if (f->eof)
        break;
      cpm_bdos(F_DMAOFF, (long)f->rec);
      if (cpm_bdos(F_READ, (long)f->fcb) != 0) {
        f->eof = 1;
        break;
      }
      f->reccnt = RECSZ;
      f->recpos = 0;
    }
    p[got] = f->rec[f->recpos++];
  }
  f->pos += got;
  return got;
}

int sys_close(int fd) {
  if (fd < 3)
    return 0;
  CpmFile *f = &_cpmf[fd - 3];
  if (!f->used)
    return -1;
  if (f->writing && f->recpos > 0) {
    for (int i = f->recpos; i < RECSZ; i++)
      f->rec[i] = 0x1A; /* ^Z pad the last text record */
    cpm_bdos(F_DMAOFF, (long)f->rec);
    cpm_bdos(F_WRITE, (long)f->fcb);
    f->recpos = 0;
  }
  cpm_bdos(F_CLOSE, (long)f->fcb);
  f->used = 0;
  return 0;
}

/* Byte-granular seek over CP/M's record files: track an absolute byte position
 * and reposition with the random-record BDOS calls.  whence is SEEK_SET(0),
 * SEEK_CUR(1), SEEK_END(2).  SEEK_END is record-granular (CP/M stores no exact
 * byte length).  Repositioning loads the target record via F_READRAND; reads
 * within that 128-byte record then work, and the stdio layer's own buffer
 * absorbs the rest. */
long sys_seek(int fd, long off, int whence) {
  if (fd < 3)
    return -1;
  CpmFile *f = &_cpmf[fd - 3];
  long target;
  if (whence == 1)
    target = f->pos + off; /* SEEK_CUR */
  else if (whence == 2) { /* SEEK_END (record-granular) */
    f->fcb[FCB_CR] = 0;
    cpm_bdos(F_SIZE, (long)f->fcb);
    long recs = (long)f->fcb[33] | ((long)f->fcb[34] << 8) | ((long)f->fcb[35] << 16);
    target = recs * RECSZ + off;
  } else
    target = off; /* SEEK_SET */
  if (target < 0)
    return -1;
  /* Persist a buffered partial write record before seeking away from it, so a
     write-then-rewind-then-read (update / tmpfile) does not lose it. */
  if (f->writing && f->recpos > 0) {
    long crec = (f->pos - f->recpos) / RECSZ;
    for (int i = f->recpos; i < RECSZ; i++)
      f->rec[i] = 0x1A; /* pad the tail (text ^Z; harmless past the data here) */
    f->fcb[33] = (unsigned char)(crec & 0xFF);
    f->fcb[34] = (unsigned char)((crec >> 8) & 0xFF);
    f->fcb[35] = (unsigned char)((crec >> 16) & 0xFF);
    cpm_bdos(F_DMAOFF, (long)f->rec);
    cpm_bdos(F_WRITERAND, (long)f->fcb);
    f->recpos = 0;
  }
  if (target != f->pos) {
    long rec = target / RECSZ;
    f->fcb[33] = (unsigned char)(rec & 0xFF);
    f->fcb[34] = (unsigned char)((rec >> 8) & 0xFF);
    f->fcb[35] = (unsigned char)((rec >> 16) & 0xFF);
    cpm_bdos(F_DMAOFF, (long)f->rec);
    if (cpm_bdos(F_READRAND, (long)f->fcb) == 0) {
      f->reccnt = RECSZ;
      f->recpos = (int)(target % RECSZ);
    } else {
      f->reccnt = 0; /* at/after EOF -> next read yields EOF */
      f->recpos = 0;
    }
    f->eof = 0;
    f->pos = target;
  }
  return f->pos;
}

int sys_unlink(const char *path) {
  unsigned char fcb[FCB_SIZE];
  parse_fcb(fcb, path);
  return ((cpm_bdos(F_DELETE, (long)fcb) & 0xFF) == 0xFF) ? -1 : 0;
}

/* CP/M rename FCB: old name in bytes 0..11, new name in bytes 16..27 (a
   second FCB whose drive byte lives at offset 16). BDOS 23 returns 0xFF if
   the old file is absent. */
int sys_rename(const char *oldp, const char *newp) {
  unsigned char fcb[FCB_SIZE];
  unsigned char nf[FCB_SIZE];
  parse_fcb(fcb, oldp);
  parse_fcb(nf, newp);
  for (int i = 0; i < 12; i++)
    fcb[16 + i] = nf[i]; /* new drive(16), name(17..24), type(25..27) */
  fcb[16] = fcb[0];      /* CP/M renames within a single drive */
  return ((cpm_bdos(F_RENAME, (long)fcb) & 0xFF) == 0xFF) ? -1 : 0;
}

/* CP/M-68K has no process environment, so every getenv() lookup misses.
   (Osiris provides a real environment via DOS 64h in osiris_sys.a68.) */
char *sys_getenv(const char *name) {
  (void)name;
  return 0;
}

/* CP/M-68K has no EXEC / child-process facility, so system() can never run a
   command here.  (system() only reaches this when a command was requested and
   COMSPEC exists; on CP/M COMSPEC is NULL, so this is effectively unreachable,
   but it must link.)  Osiris runs COMSPEC via DOS 4Bh in osiris_sys.a68. */
int sys_exec(const char *path, void *parmblk) {
  (void)path;
  (void)parmblk;
  return -1;
}

/* ---- <time.h> clock seam --------------------------------------------
 * BDOS 105 (T_GET, Worm BDOSEXT) fills an 8-byte DAT block: a big-endian
 * word of days-since-1-JAN-1978, then byte hour/min/sec. Convert the day
 * count to a Gregorian date with the core's shared calendar helper and
 * hand libc a broken-down struct, identical to the Osiris backend. */
struct __sysdt {
  long year, mon, mday, hour, min, sec;
};
extern void __civil_from_days(long z, int *py, int *pm, int *pd);

#define DAYS_1970_TO_1978 2922L /* 8 years, incl. leaps 1972 + 1976 */

int sys_time(struct __sysdt *dt) {
  unsigned char dat[8];
  for (int i = 0; i < 8; i++)
    dat[i] = 0;
  long rc = cpm_bdos(T_GET, (long)dat);
  long days1978 = ((long)dat[0] << 8) | dat[1]; /* big-endian word */
  int y, m, d;
  __civil_from_days(days1978 + DAYS_1970_TO_1978, &y, &m, &d);
  dt->year = y;
  dt->mon = m;
  dt->mday = d;
  dt->hour = dat[2];
  dt->min = dat[3];
  dt->sec = dat[4];
  return (rc == 0) ? 0 : -1;
}
