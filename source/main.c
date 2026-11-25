/* main.c -- OpenSpinDrift Nintendo Switch wrapper entry point.
 *
 * Runs the game's own ARM64 Android library (libmain.so) natively: it is
 * loaded here, its SDL2/GLES2/libc imports are bound to the Switch build of
 * SDL2, mesa GLES2 and the shims, and its SDL_main entry point is called the
 * way SDLActivity's Java side would call it. No game code or data is included
 * in this wrapper; the .so and assets/ are provided by the user.
 *
 * MIT license; see LICENSE.
 */

#define SDL_MAIN_HANDLED

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "paths.h"
#include "util.h"
#include "error.h"
#include "crash.h"
#include "so_util.h"
#include "imports.h"
#include "controls.h"

static void *heap_so_base = NULL;
static size_t heap_so_limit = 0;

so_module game_mod;

/* libmain.so LOAD zone is ~11MB; keep generous headroom for the loader. */
#define SO_HEAP_RESERVE (64 * 1024 * 1024)

void __libnx_initheap(void) {
  void *addr;
  size_t size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  size_t so_reserve = SO_HEAP_RESERVE;
  if (so_reserve > size / 2)
    so_reserve = size / 2;

  extern char *fake_heap_start;
  extern char *fake_heap_end;
  size_t fake_heap_size = size - so_reserve;
  fake_heap_start = (char *)addr;
  fake_heap_end   = (char *)addr + fake_heap_size;

  heap_so_base = (char *)addr + fake_heap_size;
  heap_so_base = (void *)ALIGN_MEM((uintptr_t)heap_so_base, 0x1000);
  heap_so_limit = (char *)addr + size - (char *)heap_so_base;
}

static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77)) fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78)) fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73)) fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE) fatal_error("Own process handle is unavailable.");
}

static void check_data(void) {
  struct stat st;

  if (stat(SO_NAME, &st) < 0)
    fatal_error("Could not find\n%s\nin\n%s\n\n"
                "Copy the game's libmain.so next to the NRO.",
                SO_NAME, paths_base());

  if (stat("assets", &st) < 0 || !S_ISDIR(st.st_mode))
    fatal_error("Could not find the assets folder in\n%s\n\n"
                "Copy the game's assets folder next to the NRO.",
                paths_base());
}

static int (*e_SDL_main)(int argc, char **argv) = NULL;

static Thread s_game_thread;
static volatile int s_game_running = 1;

static void game_thread_fn(void *arg) {
  (void)arg;
  tls_setup_guard(); /* engine reads its stack canary from tpidr_el0+0x28 */

  char arg0[] = "OpenSpinDrift_nx";
  char *argv[] = { arg0, NULL };
  e_SDL_main(1, argv);
  s_game_running = 0;
}

int main(int argc, char **argv) {
  cpu_boost(1);

  paths_init(argc, argv);
  if (chdir(paths_base()) != 0)
    fatal_error("Could not open the NRO folder:\n%s", paths_base());

  util_open_log(LOG_NAME);
  crash_init(paths_base());
  atexit(crash_exit_marker);
  debugPrintf("[boot] OpenSpinDrift_nx: data folder %s\n", paths_base());

  check_syscalls();
  timeInitialize(); /* newlib time()/localtime() used by the game */
  check_data();
  controls_init(paths_base());

  setenv("SDL_ACCELEROMETER_AS_JOYSTICK", "0", 1);
  SDL_SetMainReady();

  char so_path[512];
  snprintf(so_path, sizeof(so_path), "%s/%s", paths_base(), SO_NAME);

  debugPrintf("[boot] loading %s\n", so_path);
  if (so_load(&game_mod, so_path, heap_so_base, heap_so_limit) < 0)
    fatal_error("Could not load\n%s", so_path);

  resolve_imports(&game_mod);

  /* resolve the entry point before so_finalize() maps the code */
  e_SDL_main = (void *)so_try_find_addr_rx(&game_mod, "SDL_main");
  if (!e_SDL_main)
    fatal_error("%s does not export SDL_main.\n"
                "This is not the expected game library.", SO_NAME);

  so_finalize(&game_mod);
  so_flush_caches(&game_mod);
  crash_set_module("libmain.so", (uintptr_t)game_mod.load_virtbase);

  tls_setup_guard();
  so_execute_init_array(&game_mod);
  so_free_temp(&game_mod);

  debugPrintf("[boot] starting SDL_main\n");
  if (R_FAILED(threadCreate(&s_game_thread, game_thread_fn, NULL, NULL,
                            8 * 1024 * 1024, 0x2C, -2)))
    fatal_error("Could not create the game thread.");
  threadStart(&s_game_thread);

  cpu_boost(0);

  while (appletMainLoop() && s_game_running)
    svcSleepThread(10 * 1000 * 1000);

  debugPrintf("[boot] game exited (SDL_main returned)\n");

  /* Exit through libnx: the engine's threads may still hold the SD card. */
  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
