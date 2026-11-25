/* paths.c -- resolves the folder the .nro was launched from and exposes it to
 * the game through SDL's path APIs. MIT license; see LICENSE.
 *
 * The game (libmain.so) uses SDL_AndroidGetExternalStoragePath() as its data
 * folder (it chdir()s there, writes log.txt and creates save/) and
 * SDL_GetBasePath() to find assets/ next to its executable. On Android both
 * point into the app's storage; on Switch we point both at the folder holding
 * OpenSpinDrift_nx.nro so the user can drop the .so and assets/ next to it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "paths.h"

static char s_base[512] = DEFAULT_BASE;

const char *paths_base(void) {
  return s_base;
}

/* does dir look like the NRO folder? */
static int dir_has_game(const char *dir) {
  char path[600];
  struct stat st;

  snprintf(path, sizeof path, "%s/%s", dir, SO_NAME);
  if (stat(path, &st) == 0)
    return 1;

  snprintf(path, sizeof path, "%s/%s", dir, NRO_NAME);
  return stat(path, &st) == 0;
}

static int is_dir(const char *dir) {
  struct stat st;
  return stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
}

static void strip_trailing_slashes(char *dir) {
  size_t n = strlen(dir);
  while (n > 1 && dir[n - 1] == '/')
    dir[--n] = '\0';
}

/* Turn "sdmc:/switch/Game/Game.nro" (or "/switch/Game/Game.nro") into the
 * folder part, as an absolute sdmc: path. Returns 0 if there is no folder. */
static int base_from_nro_path(const char *path, char *out, size_t out_size) {
  const char *fwd, *back, *slash;
  size_t len;

  if (!path || !*path)
    return 0;

  fwd  = strrchr(path, '/');
  back = strrchr(path, '\\');
  slash = (fwd && back) ? (fwd > back ? fwd : back) : (fwd ? fwd : back);
  if (!slash)
    return 0;

  len = (size_t)(slash - path);
  if (len == 0 || len >= out_size)
    return 0;

  if (path[0] == '/') {
    /* the loader may hand us a device-less absolute path */
    if (len + 6 >= out_size)
      return 0;
    snprintf(out, out_size, "sdmc:%.*s", (int)len, path);
  } else {
    snprintf(out, out_size, "%.*s", (int)len, path);
  }

  strip_trailing_slashes(out);
  return out[0] != '\0';
}

void paths_init(int argc, char **argv) {
  char candidate[512];
  char cwd[512];

  if ((!argv || argc < 1) && envHasArgv()) {
    argv = (char **)envGetArgv();
    argc = 0;
    while (argv && argv[argc])
      argc++;
  }

  /* The loader passes the .nro path as argv[0] (hbmenu, title override and
   * forwarders all do). Prefer it even if the files are not there yet, so a
   * missing game file is reported against the right folder. */
  for (int i = 0; argv && i < argc && argv[i]; i++) {
    if (base_from_nro_path(argv[i], candidate, sizeof candidate)) {
      strlcpy(s_base, candidate, sizeof s_base);
      return;
    }
  }

  /* No usable argv: if we were started inside the game folder, use it. */
  if (getcwd(cwd, sizeof cwd) && dir_has_game(cwd)) {
    strip_trailing_slashes(cwd);
    strlcpy(s_base, cwd, sizeof s_base);
    return;
  }

  if (is_dir(DEFAULT_BASE))
    strlcpy(s_base, DEFAULT_BASE, sizeof s_base);
}

/* SDL_GetBasePath contract: heap string with a trailing separator. The game
 * appends "assets" / "openspindrift.cfg" directly to it. */
char *nx_sdl_get_base_path(void) {
  const size_t len = strlen(s_base);
  char *path = malloc(len + 2);
  if (!path)
    return NULL;

  memcpy(path, s_base, len);
  path[len] = '/';
  path[len + 1] = '\0';
  return path;
}

/* SDL_GetPrefPath contract: heap string with a trailing separator, folder
 * created. Save data stays next to the .nro. */
char *nx_sdl_get_pref_path(const char *org, const char *app) {
  (void)org;
  (void)app;

  const size_t len = strlen(s_base);
  char *path = malloc(len + 7);
  if (!path)
    return NULL;

  memcpy(path, s_base, len);
  memcpy(path + len, "/save", 6);
  mkdir(path, 0777);

  path[len + 5] = '/';
  path[len + 6] = '\0';
  return path;
}

/* SDL_AndroidGetExternalStoragePath contract: heap string without a trailing
 * separator. The game chdir()s here and creates save/ under it. */
char *nx_sdl_get_external_storage_path(void) {
  const size_t len = strlen(s_base);
  char *path = malloc(len + 1);
  if (!path)
    return NULL;

  memcpy(path, s_base, len + 1);
  return path;
}
