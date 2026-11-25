/* controls.h -- button remapping loaded from controls.ini. MIT license; see
 * LICENSE.
 *
 * The game is an Xbox-layout title. The user's controls.ini (next to the .nro)
 * says which physical pad button(s) press each game button, so a Switch pad can
 * be made to behave like an Xbox one (A/B and X/Y swapped by default).
 */

#ifndef __CONTROLS_H__
#define __CONTROLS_H__

#include <stdint.h>

/* Number of SDL controller buttons we map (SDL_CONTROLLER_BUTTON_MAX). */
#define CONTROLS_BUTTON_MAX 21

/* Reads <dir>/controls.ini, writing a commented default file first if it does
 * not exist yet. Defaults (swapped A/B, X/Y) apply to any button the file does
 * not mention. Call once at boot, before the game starts. */
void controls_init(const char *dir);

/* Bitmask of physical buttons that press this game (Xbox) button. 0 means the
 * game button is disabled. */
uint32_t controls_sources(int logical);

/* Game (Xbox) button that a physical button press is reported as, or -1 when
 * no game button uses that physical button. */
int controls_logical(int physical);

#endif
