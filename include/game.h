/* Deadfall SNES port - top-level game loop/state machine (port of GameScene.js). */
#ifndef GAME_H
#define GAME_H

#include "types.h"

void game_init(void);     /* start a new game at level 1 */
void game_continue(void); /* resume the current level with fresh lives (uses a continue) */
void game_update(void);   /* one frame: input, logic, gravity, render entities */

/* Set by game_update when the BG1 tilemap needs rebuilding/uploading. */
extern u8 game_map_dirty;

#endif /* GAME_H */
