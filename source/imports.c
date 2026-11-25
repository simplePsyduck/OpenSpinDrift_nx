/* imports.c -- resolves libmain.so's dynamic imports against newlib, the
 * Switch SDL2, mesa GLES2, and our shims. MIT license; see LICENSE.
 *
 * libmain.so is the game's own Android ARM64 library (built against its own
 * libSDL2.so). The libSDL2.so from the game files is not loaded: Android SDL2
 * is a thin client of SDLActivity's Java glue, which isn't available here.
 * Instead the standard SDL2 API it imports is bound to the Switch build of
 * SDL2, which provides the same ABI plus a native window/HID/audio backend.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <switch.h>
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

#include "so_util.h"
#include "util.h"
#include "libc_shim.h"
#include "paths.h"
#include "controls.h"

/* real libc/gcc symbols whose addresses we forward verbatim */
extern uintptr_t __cxa_atexit;
extern uintptr_t __stack_chk_fail;

static int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
  static char string[1024];
  va_list list;
  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);
  debugPrintf("%s: %s\n", tag ? tag : "android", string);
  return 0;
}

static int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list list) {
  (void)prio;
  static char string[1024];
  vsnprintf(string, sizeof(string), fmt, list);
  debugPrintf("%s: %s\n", tag ? tag : "android", string);
  return 0;
}

/* --- TLS for game code on SDL-created threads ------------------------------
 *
 * The game is bionic code: every function prologue reads its stack-protector
 * canary from tpidr_el0+0x28. SDL's Switch backend creates its audio thread
 * (and SDL_CreateThread threads) without setting tpidr_el0, so the game's mixer
 * faulted at address 0x28 the moment audio playback started. Give those threads
 * the same guard block our main/game thread uses. */

static SDL_AudioCallback s_audio_callback = NULL;
static void *s_audio_userdata = NULL;

static void SDLCALL nx_audio_callback(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  tls_setup_guard();
  if (s_audio_callback)
    s_audio_callback(s_audio_userdata, stream, len);
}

static Uint32 SDL_OpenAudioDevice_tls(const char *dev, int iscapture,
                                      const SDL_AudioSpec *want, SDL_AudioSpec *got,
                                      int allowed) {
  SDL_AudioSpec desired;

  if (want) {
    desired = *want;
    if (!iscapture && want->callback) {
      s_audio_callback = want->callback;
      s_audio_userdata = want->userdata;
      desired.callback = nx_audio_callback;
      desired.userdata = NULL; /* wrapper uses the saved callback/userdata */
    }
    want = &desired;
  }

  return SDL_OpenAudioDevice(dev, iscapture, want, got, allowed);
}

/* --- button remapping (controls.ini) ----------------------------------------
 *
 * controls.c owns the mapping table: which physical pad buttons press each
 * game (Xbox) button. The built-in default swaps A/B and X/Y so a Switch pad
 * matches the game's layout, and controls.ini next to the .nro can override
 * any line and list several physical buttons for one game button. */

static Uint8 SDL_GameControllerGetButton_remap(SDL_GameController *gamecontroller,
                                               SDL_GameControllerButton button) {
  uint32_t mask = controls_sources((int)button);

  while (mask) {
    int physical = __builtin_ctz(mask);
    if (SDL_GameControllerGetButton(gamecontroller, (SDL_GameControllerButton)physical))
      return 1;
    mask &= mask - 1;
  }
  return 0;
}

static int SDL_PollEvent_remap(SDL_Event *event) {
  for (int i = 0; i < 32; i++) {
    if (!SDL_PollEvent(event))
      return 0;
    if (!event)
      return 1;

    if (event->type != SDL_CONTROLLERBUTTONDOWN &&
        event->type != SDL_CONTROLLERBUTTONUP)
      return 1;

    int logical = controls_logical((int)event->cbutton.button);
    if (logical < 0)
      continue; /* physical button that no game button uses */

    event->cbutton.button = (Uint8)logical;
    return 1;
  }
  return 0;
}

typedef struct {
  SDL_ThreadFunction fn;
  void *data;
} NxThreadStart;

static int SDLCALL nx_thread_entry(void *arg) {
  NxThreadStart ts = *(NxThreadStart *)arg;
  free(arg);
  tls_setup_guard();
  return ts.fn(ts.data);
}

static SDL_Thread *SDL_CreateThread_tls(SDL_ThreadFunction fn, const char *name,
                                        void *data) {
  NxThreadStart *ts = malloc(sizeof(*ts));
  if (!ts)
    return NULL;
  ts->fn = fn;
  ts->data = data;

  SDL_Thread *thread = SDL_CreateThread(nx_thread_entry, name, ts);
  if (!thread)
    free(ts);
  return thread;
}

static const DynLibFunction dynlib_functions[] = {
  /* --- bionic runtime / stdio backing --- */
  { "__sF", (uintptr_t)&fake_sF },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&ret0 },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "_Exit", (uintptr_t)&_Exit },
  { "abort", (uintptr_t)&abort },
  { "exit", (uintptr_t)&exit },

  /* --- fortify (_chk) wrappers --- */
  { "__memcpy_chk", (uintptr_t)&__memcpy_chk_fake },
  { "__memset_chk", (uintptr_t)&__memset_chk_fake },
  { "__strchr_chk", (uintptr_t)&__strchr_chk_fake },
  { "__strrchr_chk", (uintptr_t)&__strrchr_chk_fake },
  { "__strlen_chk", (uintptr_t)&__strlen_chk_fake },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_fake },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_fake },
  { "__open_2", (uintptr_t)&__open_2_fake },

  /* --- bionic logging --- */
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },

  /* --- file system --- */
  { "access", (uintptr_t)&access },
  { "chdir", (uintptr_t)&chdir },
  { "close", (uintptr_t)&close },
  { "closedir", (uintptr_t)&closedir },
  { "dup2", (uintptr_t)&dup2 },
  { "fileno", (uintptr_t)&fileno },
  { "fsync", (uintptr_t)&fsync },
  { "getenv", (uintptr_t)&getenv },
  { "mkdir", (uintptr_t)&mkdir },
  { "open", (uintptr_t)&open },
  { "opendir", (uintptr_t)&opendir },
  { "readdir", (uintptr_t)&readdir_fake },
  { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename },
  { "rmdir", (uintptr_t)&rmdir },
  { "stat", (uintptr_t)&stat_fake },

  /* --- stdio --- */
  { "fopen", (uintptr_t)&fopen_fake },
  { "fclose", (uintptr_t)&fclose_fake },
  { "fread", (uintptr_t)&fread_fake },
  { "fwrite", (uintptr_t)&fwrite_fake },
  { "fseek", (uintptr_t)&fseek_fake },
  { "fflush", (uintptr_t)&fflush_fake },
  { "ferror", (uintptr_t)&ferror_fake },
  { "fgets", (uintptr_t)&fgets },
  { "fprintf", (uintptr_t)&fprintf_fake },
  { "fputc", (uintptr_t)&fputc_fake },
  { "vfprintf", (uintptr_t)&vfprintf_fake },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "sscanf", (uintptr_t)&sscanf },

  /* --- memory --- */
  { "malloc", (uintptr_t)&malloc },
  { "calloc", (uintptr_t)&calloc },
  { "realloc", (uintptr_t)&realloc },
  { "free", (uintptr_t)&free },

  /* --- mem/str --- */
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcpy", (uintptr_t)&strcpy },
  { "strlen", (uintptr_t)&strlen },
  { "strncmp", (uintptr_t)&strncmp },
  { "strrchr", (uintptr_t)&strrchr },
  { "strspn", (uintptr_t)&strspn },
  { "strstr", (uintptr_t)&strstr },
  { "strtol", (uintptr_t)&strtol },
  { "strtoul", (uintptr_t)&strtoul },
  { "atoi", (uintptr_t)&atoi },
  { "atol", (uintptr_t)&atol },
  { "atof", (uintptr_t)&atof },
  { "qsort", (uintptr_t)&qsort },

  /* --- math --- */
  { "atan2f", (uintptr_t)&atan2f },
  { "cos", (uintptr_t)&cos },
  { "exp2", (uintptr_t)&exp2 },
  { "fmod", (uintptr_t)&fmod },
  { "log", (uintptr_t)&log },
  { "pow", (uintptr_t)&pow },
  { "sin", (uintptr_t)&sin },
  { "sincos", (uintptr_t)&sincos_fake },
  { "sincosf", (uintptr_t)&sincosf_fake },

  /* --- time --- */
  { "localtime", (uintptr_t)&localtime },
  { "time", (uintptr_t)&time },

  /* --- setjmp/longjmp (must bind to the real newlib symbols) --- */
  { "setjmp", (uintptr_t)&setjmp },
  { "longjmp", (uintptr_t)&longjmp },

  /* --- SDL2 paths: point at the folder holding the .nro --- */
  { "SDL_GetBasePath", (uintptr_t)&nx_sdl_get_base_path },
  { "SDL_GetPrefPath", (uintptr_t)&nx_sdl_get_pref_path },
  { "SDL_AndroidGetExternalStoragePath", (uintptr_t)&nx_sdl_get_external_storage_path },

  /* --- SDL2 (Switch build) --- */
  { "SDL_CloseAudioDevice", (uintptr_t)&SDL_CloseAudioDevice },
  { "SDL_CreateMutex", (uintptr_t)&SDL_CreateMutex },
  { "SDL_CreateSemaphore", (uintptr_t)&SDL_CreateSemaphore },
  { "SDL_CreateThread", (uintptr_t)&SDL_CreateThread_tls },
  { "SDL_CreateWindow", (uintptr_t)&SDL_CreateWindow },
  { "SDL_Delay", (uintptr_t)&SDL_Delay },
  { "SDL_DestroyWindow", (uintptr_t)&SDL_DestroyWindow },
  { "SDL_DetachThread", (uintptr_t)&SDL_DetachThread },
  { "SDL_GL_CreateContext", (uintptr_t)&SDL_GL_CreateContext },
  { "SDL_GL_DeleteContext", (uintptr_t)&SDL_GL_DeleteContext },
  { "SDL_GL_ExtensionSupported", (uintptr_t)&SDL_GL_ExtensionSupported },
  { "SDL_GL_GetDrawableSize", (uintptr_t)&SDL_GL_GetDrawableSize },
  { "SDL_GL_GetProcAddress", (uintptr_t)&SDL_GL_GetProcAddress },
  { "SDL_GL_GetSwapInterval", (uintptr_t)&SDL_GL_GetSwapInterval },
  { "SDL_GL_SetAttribute", (uintptr_t)&SDL_GL_SetAttribute },
  { "SDL_GL_SetSwapInterval", (uintptr_t)&SDL_GL_SetSwapInterval },
  { "SDL_GL_SwapWindow", (uintptr_t)&SDL_GL_SwapWindow },
  { "SDL_GameControllerClose", (uintptr_t)&SDL_GameControllerClose },
  { "SDL_GameControllerGetAttached", (uintptr_t)&SDL_GameControllerGetAttached },
  { "SDL_GameControllerGetAxis", (uintptr_t)&SDL_GameControllerGetAxis },
  { "SDL_GameControllerGetBindForAxis", (uintptr_t)&SDL_GameControllerGetBindForAxis },
  { "SDL_GameControllerGetButton", (uintptr_t)&SDL_GameControllerGetButton_remap },
  { "SDL_GameControllerGetJoystick", (uintptr_t)&SDL_GameControllerGetJoystick },
  { "SDL_GameControllerMapping", (uintptr_t)&SDL_GameControllerMapping },
  { "SDL_GameControllerName", (uintptr_t)&SDL_GameControllerName },
  { "SDL_GameControllerOpen", (uintptr_t)&SDL_GameControllerOpen },
  { "SDL_GetCPUCount", (uintptr_t)&SDL_GetCPUCount },
  { "SDL_GetCurrentDisplayMode", (uintptr_t)&SDL_GetCurrentDisplayMode },
  { "SDL_GetDesktopDisplayMode", (uintptr_t)&SDL_GetDesktopDisplayMode },
  { "SDL_GetDisplayDPI", (uintptr_t)&SDL_GetDisplayDPI },
  { "SDL_GetDisplayUsableBounds", (uintptr_t)&SDL_GetDisplayUsableBounds },
  { "SDL_GetError", (uintptr_t)&SDL_GetError },
  { "SDL_GetKeyFromName", (uintptr_t)&SDL_GetKeyFromName },
  { "SDL_GetKeyName", (uintptr_t)&SDL_GetKeyName },
  { "SDL_GetMouseState", (uintptr_t)&SDL_GetMouseState },
  { "SDL_GetPerformanceCounter", (uintptr_t)&SDL_GetPerformanceCounter },
  { "SDL_GetPerformanceFrequency", (uintptr_t)&SDL_GetPerformanceFrequency },
  { "SDL_GetTicks", (uintptr_t)&SDL_GetTicks },
  { "SDL_GetTouchDeviceType", (uintptr_t)&SDL_GetTouchDeviceType },
  { "SDL_GetWindowBordersSize", (uintptr_t)&SDL_GetWindowBordersSize },
  { "SDL_GetWindowDisplayIndex", (uintptr_t)&SDL_GetWindowDisplayIndex },
  { "SDL_GetWindowFlags", (uintptr_t)&SDL_GetWindowFlags },
  { "SDL_GetWindowPosition", (uintptr_t)&SDL_GetWindowPosition },
  { "SDL_GetWindowSize", (uintptr_t)&SDL_GetWindowSize },
  { "SDL_Init", (uintptr_t)&SDL_Init },
  { "SDL_IsGameController", (uintptr_t)&SDL_IsGameController },
  { "SDL_JoystickGetButton", (uintptr_t)&SDL_JoystickGetButton },
  { "SDL_JoystickGetDeviceInstanceID", (uintptr_t)&SDL_JoystickGetDeviceInstanceID },
  { "SDL_JoystickInstanceID", (uintptr_t)&SDL_JoystickInstanceID },
  { "SDL_LockAudioDevice", (uintptr_t)&SDL_LockAudioDevice },
  { "SDL_LockMutex", (uintptr_t)&SDL_LockMutex },
  { "SDL_NumJoysticks", (uintptr_t)&SDL_NumJoysticks },
  { "SDL_OpenAudioDevice", (uintptr_t)&SDL_OpenAudioDevice_tls },
  { "SDL_PauseAudioDevice", (uintptr_t)&SDL_PauseAudioDevice },
  { "SDL_PollEvent", (uintptr_t)&SDL_PollEvent_remap },
  { "SDL_Quit", (uintptr_t)&SDL_Quit },
  { "SDL_RWFromFP", (uintptr_t)&SDL_RWFromFP },
  { "SDL_RWFromFile", (uintptr_t)&SDL_RWFromFile },
  { "SDL_RWclose", (uintptr_t)&SDL_RWclose },
  { "SDL_RWread", (uintptr_t)&SDL_RWread },
  { "SDL_RWseek", (uintptr_t)&SDL_RWseek },
  { "SDL_RWsize", (uintptr_t)&SDL_RWsize },
  { "SDL_RestoreWindow", (uintptr_t)&SDL_RestoreWindow },
  { "SDL_SemPost", (uintptr_t)&SDL_SemPost },
  { "SDL_SemWait", (uintptr_t)&SDL_SemWait },
  { "SDL_SetHint", (uintptr_t)&SDL_SetHint },
  { "SDL_SetWindowFullscreen", (uintptr_t)&SDL_SetWindowFullscreen },
  { "SDL_SetWindowMinimumSize", (uintptr_t)&SDL_SetWindowMinimumSize },
  { "SDL_SetWindowPosition", (uintptr_t)&SDL_SetWindowPosition },
  { "SDL_SetWindowSize", (uintptr_t)&SDL_SetWindowSize },
  { "SDL_ShowCursor", (uintptr_t)&SDL_ShowCursor },
  { "SDL_ShowSimpleMessageBox", (uintptr_t)&SDL_ShowSimpleMessageBox },
  { "SDL_UnlockAudioDevice", (uintptr_t)&SDL_UnlockAudioDevice },
  { "SDL_UnlockMutex", (uintptr_t)&SDL_UnlockMutex },
  { "SDL_WasInit", (uintptr_t)&SDL_WasInit },
  { "SDL_free", (uintptr_t)&SDL_free },
  { "SDL_memset", (uintptr_t)&SDL_memset },
  { "SDL_strcasecmp", (uintptr_t)&SDL_strcasecmp },
  { "SDL_strncasecmp", (uintptr_t)&SDL_strncasecmp },

  /* --- GLES2 (mesa) --- */
  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glCompileShader", (uintptr_t)&glCompileShader },
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFinish", (uintptr_t)&glFinish },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
  { "glGetShaderPrecisionFormat", (uintptr_t)&glGetShaderPrecisionFormat },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glLinkProgram", (uintptr_t)&glLinkProgram },
  { "glPixelStorei", (uintptr_t)&glPixelStorei },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glShaderSource", (uintptr_t)&glShaderSource },
  { "glTexImage2D", (uintptr_t)&glTexImage2D },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
  { "glUniform1f", (uintptr_t)&glUniform1f },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform2f", (uintptr_t)&glUniform2f },
  { "glUniform3f", (uintptr_t)&glUniform3f },
  { "glUniform4f", (uintptr_t)&glUniform4f },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewport },
};

static const size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);

void resolve_imports(so_module *mod) {
  so_relocate(mod);
  so_resolve(mod, (DynLibFunction *)dynlib_functions, (int)dynlib_numfunctions, 1);
}
