/* Deadfall SNES port - gravity (port of GravitySystem.js). */
#ifndef GRAVITY_H
#define GRAVITY_H

#include "types.h"

/* One animated step on the current section: each fallable tile (gem/boulder/
 * extra-life) drops one cell into empty/robot-spawn below. Fills
 * game.fall_tiles[]/fall_count for this step. Returns TRUE if anything moved. */
u8 gravity_step(void);

/* Settle the current section instantly, recording net falls (origin->final)
 * into out[] (up to max). Returns the number of fall records. Used on section
 * entry and initial load. */
u8 gravity_settle(FallTile *out, u8 max);

#endif /* GRAVITY_H */
