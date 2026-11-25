/* libc_shim.c -- bionic<->newlib libc wrappers for libmain.so. Converting
 * wrappers where the ABIs differ; matches are forwarded from imports.c.
 * MIT license; see LICENSE.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "libc_shim.h"

/* fortify (_chk): ignore the object-size argument */
void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen) {
  (void)dstlen; return memcpy(dst, src, n);
}
void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen) {
  (void)dstlen; return memset(dst, c, n);
}
char *__strchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen; return strchr(s, c);
}
char *__strrchr_chk_fake(const char *s, int c, size_t slen) {
  (void)slen; return strrchr(s, c);
}
size_t __strlen_chk_fake(const char *s, size_t slen) {
  (void)slen; return strlen(s);
}
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen; return vsnprintf(s, maxlen, fmt, va);
}
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va) {
  (void)flag; (void)slen; return vsprintf(s, fmt, va);
}
int __open_2_fake(const char *path, int flags) {
  return open(path, flags, 0);
}

void sincos_fake(double x, double *s, double *c) { *s = sin(x); *c = cos(x); }
void sincosf_fake(float x, float *s, float *c) { *s = sinf(x); *c = cosf(x); }

/* struct stat conversion (bionic aarch64 layout) */
struct bionic_timespec { int64_t tv_sec; int64_t tv_nsec; };
struct bionic_stat {
  uint64_t st_dev, st_ino;
  uint32_t st_mode, st_nlink, st_uid, st_gid;
  uint64_t st_rdev, __pad1;
  int64_t st_size;
  int32_t st_blksize, __pad2;
  int64_t st_blocks;
  struct bionic_timespec st_atim, st_mtim, st_ctim;
  uint32_t __unused4, __unused5;
};

int stat_fake(const char *path, void *st) {
  struct stat in;
  if (stat(path, &in) != 0) return -1;
  struct bionic_stat *out = st;
  memset(out, 0, sizeof(*out));
  out->st_dev = in.st_dev; out->st_ino = in.st_ino;
  out->st_mode = in.st_mode; out->st_nlink = in.st_nlink;
  out->st_uid = in.st_uid; out->st_gid = in.st_gid;
  out->st_rdev = in.st_rdev; out->st_size = in.st_size;
  out->st_blksize = in.st_blksize; out->st_blocks = in.st_blocks;
  out->st_atim.tv_sec = in.st_atime;
  out->st_mtim.tv_sec = in.st_mtime;
  out->st_ctim.tv_sec = in.st_ctime;
  return 0;
}

/* dirent conversion (bionic dirent64 layout). Newlib's struct dirent has
 * d_name at offset 9; bionic's has d_ino, d_off, d_reclen, d_type and d_name
 * at offset 19. The game reads d_name at +0x13, so it must be converted. */
struct bionic_dirent {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[256];
};

void *readdir_fake(void *dirp) {
  static struct bionic_dirent out; /* not thread-safe, like the other ports */
  struct dirent *e = readdir((DIR *)dirp);
  if (!e)
    return NULL;
  memset(&out, 0, sizeof(out));
  out.d_ino = e->d_ino;
  out.d_reclen = sizeof(out);
  out.d_type = e->d_type;
  snprintf(out.d_name, sizeof(out.d_name), "%s", e->d_name);
  return &out;
}

/* stdio over the fake bionic __sF: libc++/SDL bind std streams to &__sF[N];
 * these absorb accesses to those fake FILEs and forward everything else. */
uint8_t fake_sF[3][0x100];

static int is_fake_file(const void *f) {
  const uint8_t *p = f, *base = (const uint8_t *)fake_sF;
  return p >= base && p < base + sizeof(fake_sF);
}

size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f) {
  if (!f || !ptr) return 0;
  if (is_fake_file(f)) return n;
  return fwrite(ptr, size, n, f);
}

size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f) {
  if (!f || !ptr || is_fake_file(f)) return 0;
  return fread(ptr, size, n, f);
}

int fputc_fake(int c, FILE *f) {
  if (!f) return -1;
  if (is_fake_file(f)) return c;
  return fputc(c, f);
}
int fflush_fake(FILE *f) {
  if (!f || is_fake_file(f)) return 0;
  return fflush(f);
}
int fclose_fake(FILE *f) {
  if (!f || is_fake_file(f)) return 0;
  return fclose(f);
}
int ferror_fake(FILE *f) {
  if (!f || is_fake_file(f)) return 0;
  return ferror(f);
}
int fprintf_fake(FILE *f, const char *fmt, ...) {
  if (!f || is_fake_file(f)) return 0;
  va_list va; va_start(va, fmt);
  int ret = vfprintf(f, fmt, va);
  va_end(va);
  return ret;
}
int vfprintf_fake(FILE *f, const char *fmt, va_list va) {
  if (!f || is_fake_file(f)) return 0;
  return vfprintf(f, fmt, va);
}
int fseek_fake(FILE *f, long off, int whence) {
  if (!f || is_fake_file(f)) return -1;
  return fseek(f, off, whence);
}

/* Big stream buffer: the engine issues many small reads/seeks against the
 * disc image and fsdev round trips dominate otherwise. */
FILE *fopen_fake(const char *path, const char *mode) {
  if (!path || !mode) return NULL;
  FILE *f = fopen(path, mode);
  if (f && strchr(mode, 'r'))
    setvbuf(f, NULL, _IOFBF, 64 * 1024);
  return f;
}
