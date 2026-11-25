/* config.h -- OpenSpinDrift_nx wrapper configuration.
 * MIT license; see LICENSE.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/* The game library the wrapper loads from its own folder. It is provided by
 * the user (extracted from their game files); nothing game-related ships here. */
#define SO_NAME    "libmain.so"
#define NRO_NAME   "OpenSpinDrift_nx.nro"

/* Fallback when the loader gives us no usable .nro path. */
#define DEFAULT_BASE "sdmc:/switch/OpenSpinDrift_nx"

#define LOG_NAME   "OpenSpinDrift_nx.log"
#define DEBUG_LOG  1

#endif
