/* crash.h -- crash / exit diagnostics. MIT license; see LICENSE. */

#ifndef __CRASH_H__
#define __CRASH_H__

#include <stdint.h>

/* Opens <base>/crash.log and enables the libnx user exception handler. */
void crash_init(const char *base);

/* Tells the exception handler where libmain.so was mapped. */
void crash_set_module(const char *name, uintptr_t base);

/* atexit() marker: logs when the game itself calls exit(). */
void crash_exit_marker(void);

#endif
