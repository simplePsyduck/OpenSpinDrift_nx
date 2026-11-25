/* libc_shim.h -- bionic-compatible libc wrappers for libmain.so.
 * MIT license; see LICENSE.
 */

#ifndef __LIBC_SHIM_H__
#define __LIBC_SHIM_H__

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>

/* fortify */
void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen);
char *__strchr_chk_fake(const char *s, int c, size_t slen);
char *__strrchr_chk_fake(const char *s, int c, size_t slen);
size_t __strlen_chk_fake(const char *s, size_t slen);
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va);
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va);
int __open_2_fake(const char *path, int flags);

/* misc bionic */
void sincos_fake(double x, double *s, double *c);
void sincosf_fake(float x, float *s, float *c);
int stat_fake(const char *path, void *st);
void *readdir_fake(void *dirp);

/* stdio over fake __sF (bionic's std streams) */
extern uint8_t fake_sF[3][0x100];
size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f);
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f);
int fputc_fake(int c, FILE *f);
int fflush_fake(FILE *f);
int fclose_fake(FILE *f);
int ferror_fake(FILE *f);
int fprintf_fake(FILE *f, const char *fmt, ...);
int vfprintf_fake(FILE *f, const char *fmt, va_list va);
int fseek_fake(FILE *f, long off, int whence);
FILE *fopen_fake(const char *path, const char *mode);

#endif
