/* crash.c -- crash / exit diagnostics for OpenSpinDrift_nx.
 *
 * libnx calls __libnx_exception_handler() with a ThreadExceptionDump when the
 * process faults. The handler must not use newlib (stdio locks/heap may be in
 * an unknown state), so it writes a dedicated crash.log through libnx's raw
 * fsp-srv API and mirrors the lines to svcOutputDebugString.
 *
 * MIT license; see LICENSE.
 */

#include <switch.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "crash.h"
#include "util.h"

__attribute__((aligned(16))) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

static FsFile s_file;
static int    s_ok = 0;
static s64    s_off = 0;

static const char *s_mod_name = "libmain.so";
static uintptr_t   s_mod_base = 0;
static uintptr_t   s_mod_size = 0;

void crash_init(const char *base) {
  char path[512];
  const char *p = base;

  if (!strncmp(p, "sdmc:", 5))
    p += 5;
  snprintf(path, sizeof(path), "%s/crash.log", p);

  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  if (!fs)
    return;

  fsFsCreateFile(fs, path, 0, 0);
  if (R_SUCCEEDED(fsFsOpenFile(fs, path, FsOpenMode_Write, &s_file))) {
    fsFileSetSize(&s_file, 0);
    s_ok = 1;
    s_off = 0;
  }
}

void crash_set_module(const char *name, uintptr_t base) {
  s_mod_name = name;
  s_mod_base = base;
  s_mod_size = 1u << 30; /* coarse: everything above the base is the module */
}

static void emit(const char *s, int n) {
  if (n <= 0)
    return;
  svcOutputDebugString(s, (size_t)n);
  if (s_ok) {
    fsFileSetSize(&s_file, s_off + n);
    if (R_SUCCEEDED(fsFileWrite(&s_file, s_off, s, (u64)n, FsWriteOption_Flush)))
      s_off += n;
  }
}

static void emit_str(const char *s) {
  int n = 0;
  while (s[n]) n++;
  emit(s, n);
}

static void emit_hex(const char *label, u64 v) {
  static const char hexd[] = "0123456789abcdef";
  char line[48];
  int n = 0;

  while (label[n] && n < 16) { line[n] = label[n]; n++; }
  line[n++] = '0';
  line[n++] = 'x';
  for (int i = 15; i >= 0; i--)
    line[n++] = hexd[(v >> (i * 4)) & 0xf];
  line[n++] = '\n';
  emit(line, n);
}

static int readable(uintptr_t addr, size_t len) {
  if (!addr || addr < 0x1000)
    return 0;
  uintptr_t a = addr, end = addr + len;
  if (end < a)
    return 0;
  while (a < end) {
    MemoryInfo mi;
    u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a)))
      return 0;
    if (mi.type == MemType_Unmapped || !(mi.perm & Perm_R))
      return 0;
    uintptr_t block_end = (uintptr_t)mi.addr + mi.size;
    if (block_end <= a)
      return 0;
    a = block_end;
  }
  return 1;
}

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
  emit_str("\n[crash] ===== CPU FAULT =====\n");
  emit_hex("desc", ctx->error_desc);
  emit_hex("pc  ", ctx->pc.x);
  emit_hex("lr  ", ctx->lr.x);
  emit_hex("sp  ", ctx->sp.x);
  emit_hex("fp  ", ctx->fp.x);
  emit_hex("far ", ctx->far.x);
  emit_hex("esr ", ctx->esr);

  if (s_mod_base && ctx->pc.x >= s_mod_base && ctx->pc.x < s_mod_base + s_mod_size)
    emit_hex("pc - base", ctx->pc.x - s_mod_base);
  if (s_mod_base && ctx->lr.x >= s_mod_base && ctx->lr.x < s_mod_base + s_mod_size)
    emit_hex("lr - base", ctx->lr.x - s_mod_base);
  emit_hex("mod base", s_mod_base);

  for (int i = 0; i < 8; i++) {
    char label[8];
    label[0] = 'x';
    label[1] = (char)('0' + i);
    label[2] = ' ';
    label[3] = ' ';
    label[4] = ' ';
    label[5] = 0;
    emit_hex(label, ctx->cpu_gprs[i].x);
  }

  emit_str("[crash] backtrace:\n");
  uintptr_t fp = (uintptr_t)ctx->fp.x;
  for (int d = 0; d < 8 && fp; d++) {
    if (!readable(fp, 16))
      break;
    uintptr_t next = ((const uintptr_t *)fp)[0];
    uintptr_t ret  = ((const uintptr_t *)fp)[1];
    if (!ret)
      break;
    if (s_mod_base && ret >= s_mod_base && ret < s_mod_base + s_mod_size)
      emit_hex("  " , ret - s_mod_base);
    else
      emit_hex("  ", ret);
    if (next <= fp)
      break;
    fp = next;
  }

  emit_str("[crash] ======================\n");

  svcBreak(BreakReason_Panic, 0, 0);
  for (;;)
    svcSleepThread(1000000000ULL);
}

void crash_exit_marker(void) {
  debugPrintf("[boot] the game called exit()\n");
}
