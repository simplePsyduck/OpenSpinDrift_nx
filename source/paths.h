/* paths.h -- resolves the folder the .nro was launched from and exposes it to
 * the game through SDL's path APIs. MIT license; see LICENSE.
 */

#ifndef __PATHS_H__
#define __PATHS_H__

/* Works out the NRO folder (argv/env/cwd/fallback) and remembers it. */
void paths_init(int argc, char **argv);

/* Absolute device path of the folder holding the .nro, no trailing slash. */
const char *paths_base(void);

/* SDL_GetBasePath / SDL_GetPrefPath / SDL_AndroidGetExternalStoragePath
 * replacements. The first two return malloc'd strings (SDL_free-compatible). */
char *nx_sdl_get_base_path(void);
char *nx_sdl_get_pref_path(const char *org, const char *app);
char *nx_sdl_get_external_storage_path(void);

#endif
