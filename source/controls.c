/* controls.c -- button remapping loaded from controls.ini. MIT license; see
 * LICENSE.
 *
 * controls.ini sits next to the .nro and looks like:
 *
 *   [controls]
 *   a = b          ; physical Switch B presses the game's Xbox A
 *   b = a, y       ; Switch A or Switch Y both press the game's Xbox B
 *   x = y
 *   y = x
 *
 * The left side is a game (Xbox) button, the right side is one or more
 * physical pad buttons. Unmentioned game buttons keep the built-in defaults
 * (A/B and X/Y swapped, everything else one-to-one).
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <SDL2/SDL.h>

#include "controls.h"
#include "util.h"

_Static_assert(SDL_CONTROLLER_BUTTON_MAX == CONTROLS_BUTTON_MAX,
               "controls table is out of sync with SDL");

static const char *const s_names[CONTROLS_BUTTON_MAX] = {
  "a", "b", "x", "y",
  "back", "guide", "start",
  "leftstick", "rightstick",
  "leftshoulder", "rightshoulder",
  "dpup", "dpdown", "dpleft", "dpright",
  "misc1",
  "paddle1", "paddle2", "paddle3", "paddle4",
  "touchpad",
};

typedef struct {
  const char *alias;
  int button;
} ButtonAlias;

static const ButtonAlias s_aliases[] = {
  { "lb",       SDL_CONTROLLER_BUTTON_LEFTSHOULDER  },
  { "rb",       SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
  { "l",        SDL_CONTROLLER_BUTTON_LEFTSHOULDER  },
  { "r",        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
  { "ls",       SDL_CONTROLLER_BUTTON_LEFTSTICK     },
  { "rs",       SDL_CONTROLLER_BUTTON_RIGHTSTICK    },
  { "l3",       SDL_CONTROLLER_BUTTON_LEFTSTICK     },
  { "r3",       SDL_CONTROLLER_BUTTON_RIGHTSTICK    },
  { "select",   SDL_CONTROLLER_BUTTON_BACK          },
  { "minus",    SDL_CONTROLLER_BUTTON_BACK          },
  { "menu",     SDL_CONTROLLER_BUTTON_START         },
  { "plus",     SDL_CONTROLLER_BUTTON_START         },
  { "home",     SDL_CONTROLLER_BUTTON_GUIDE         },
  { "up",       SDL_CONTROLLER_BUTTON_DPAD_UP       },
  { "down",     SDL_CONTROLLER_BUTTON_DPAD_DOWN     },
  { "left",     SDL_CONTROLLER_BUTTON_DPAD_LEFT     },
  { "right",    SDL_CONTROLLER_BUTTON_DPAD_RIGHT    },
  { "share",    SDL_CONTROLLER_BUTTON_MISC1         },
  { "capture",  SDL_CONTROLLER_BUTTON_MISC1         },
  { "touch",    SDL_CONTROLLER_BUTTON_TOUCHPAD      },
};

/* game button -> physical buttons that press it */
static uint32_t s_sources[CONTROLS_BUTTON_MAX];
/* physical button -> game button it is reported as, -1 if unused */
static int s_logical[CONTROLS_BUTTON_MAX];

static const char s_default_file[] =
  "# OpenSpinDrift_nx controls\n"
  "#\n"
  "# The game uses an Xbox controller layout. Each line maps one Xbox (game)\n"
  "# button to the Switch button(s) that press it:\n"
  "#\n"
  "#     <xbox button> = <switch button> [more switch buttons ...]\n"
  "#\n"
  "# Separate several Switch buttons with spaces or commas to let any of them\n"
  "# press the same Xbox button. Use \"none\" to disable an Xbox button.\n"
  "#\n"
  "# Xbox (left side)   Switch (right side)\n"
  "#   a                b          south face button\n"
  "#   b                a          east face button\n"
  "#   x                y          west face button\n"
  "#   y                x          north face button\n"
  "#   back             minus      the \"-\" button\n"
  "#   start            plus       the \"+\" button\n"
  "#   guide            home\n"
  "#   leftstick        l3         left stick click\n"
  "#   rightstick       r3         right stick click\n"
  "#   leftshoulder     l\n"
  "#   rightshoulder    r\n"
  "#   dpup             up         d-pad\n"
  "#   dpdown           down\n"
  "#   dpleft           left\n"
  "#   dpright          right\n"
  "#   misc1            capture    the Capture button\n"
  "#\n"
  "# The lines below are one-to-one except A/B and X/Y, which are swapped so the\n"
  "# Switch face buttons sit in the same position as the Xbox labels on screen.\n"
  "# ZL/ZR are analog triggers that the game reads directly and are not remapped\n"
  "# by this file.\n"
  "\n"
  "[controls]\n"
  "a = b\n"
  "b = a\n"
  "x = y\n"
  "y = x\n"
  "back = minus\n"
  "start = plus\n"
  "guide = home\n"
  "leftstick = l3\n"
  "rightstick = r3\n"
  "leftshoulder = l\n"
  "rightshoulder = r\n"
  "dpup = up\n"
  "dpdown = down\n"
  "dpleft = left\n"
  "dpright = right\n"
  "misc1 = capture\n";

static void set_defaults(void) {
  for (int i = 0; i < CONTROLS_BUTTON_MAX; i++)
    s_sources[i] = 1u << i;

  /* Switch A/B and X/Y are physically opposite the Xbox face buttons. */
  uint32_t tmp = s_sources[SDL_CONTROLLER_BUTTON_A];
  s_sources[SDL_CONTROLLER_BUTTON_A] = s_sources[SDL_CONTROLLER_BUTTON_B];
  s_sources[SDL_CONTROLLER_BUTTON_B] = tmp;

  tmp = s_sources[SDL_CONTROLLER_BUTTON_X];
  s_sources[SDL_CONTROLLER_BUTTON_X] = s_sources[SDL_CONTROLLER_BUTTON_Y];
  s_sources[SDL_CONTROLLER_BUTTON_Y] = tmp;
}

static int parse_button(const char *s, size_t len) {
  char name[32];

  if (len == 0 || len >= sizeof name)
    return -1;
  for (size_t i = 0; i < len; i++)
    name[i] = (char)tolower((unsigned char)s[i]);
  name[len] = '\0';

  for (int i = 0; i < CONTROLS_BUTTON_MAX; i++)
    if (strcmp(name, s_names[i]) == 0)
      return i;

  for (size_t i = 0; i < sizeof s_aliases / sizeof s_aliases[0]; i++)
    if (strcmp(name, s_aliases[i].alias) == 0)
      return s_aliases[i].button;

  return -1;
}

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1]))
    *--end = '\0';
  return s;
}

/* Returns the number of applied lines, or -1 if the file could not be read. */
static int load_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  char line[512];
  int applied = 0;

  while (fgets(line, sizeof line, f)) {
    char *comment = strpbrk(line, "#;");
    if (comment)
      *comment = '\0';

    char *text = trim(line);
    if (!*text || *text == '[')
      continue;

    char *sep = strchr(text, '=');
    if (!sep)
      sep = strchr(text, ':');
    if (!sep)
      continue;
    *sep = '\0';

    char *lhs = trim(text);
    char *rhs = trim(sep + 1);
    int logical = parse_button(lhs, strlen(lhs));
    if (logical < 0) {
      debugPrintf("[controls] unknown game button \"%s\"\n", lhs);
      continue;
    }

    uint32_t mask = 0;
    int valid = 0;
    if (strcasecmp(rhs, "none") != 0) {
      for (char *p = rhs; *p;) {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
          p++;
        char *start = p;
        while (*p && !isspace((unsigned char)*p) && *p != ',')
          p++;
        if (p == start)
          continue;

        int physical = parse_button(start, (size_t)(p - start));
        if (physical < 0) {
          char name[32];
          size_t len = (size_t)(p - start);
          if (len >= sizeof name)
            len = sizeof name - 1;
          memcpy(name, start, len);
          name[len] = '\0';
          debugPrintf("[controls] unknown physical button \"%s\"\n", name);
          continue;
        }
        mask |= 1u << physical;
        valid++;
      }

      if (valid == 0) {
        debugPrintf("[controls] \"%s\" lists no valid physical button; "
                    "keeping its default\n", lhs);
        continue;
      }
    }

    s_sources[logical] = mask;
    applied++;
  }

  fclose(f);
  return applied;
}

static void write_default_file(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) {
    debugPrintf("[controls] could not create %s, using built-in defaults\n", path);
    return;
  }
  fwrite(s_default_file, 1, sizeof s_default_file - 1, f);
  fclose(f);
}

void controls_init(const char *dir) {
  char path[600];
  int applied;

  set_defaults();

  snprintf(path, sizeof path, "%s/controls.ini", dir);
  applied = load_file(path);
  if (applied < 0) {
    write_default_file(path);
    debugPrintf("[controls] created %s with default (swapped) face buttons\n", path);
  } else {
    debugPrintf("[controls] %s: %d mapping(s)\n", path, applied);
  }

  /* Physical press -> the first game button that lists it. A physical button
   * that no game button lists is left unmapped (-1) and its events are
   * dropped. */
  for (int p = 0; p < CONTROLS_BUTTON_MAX; p++)
    s_logical[p] = -1;
  for (int l = 0; l < CONTROLS_BUTTON_MAX; l++)
    for (int p = 0; p < CONTROLS_BUTTON_MAX; p++)
      if ((s_sources[l] & (1u << p)) && s_logical[p] < 0)
        s_logical[p] = l;
}

uint32_t controls_sources(int logical) {
  if (logical < 0 || logical >= CONTROLS_BUTTON_MAX)
    return 0;
  return s_sources[logical];
}

int controls_logical(int physical) {
  if (physical < 0 || physical >= CONTROLS_BUTTON_MAX)
    return -1;
  return s_logical[physical];
}
