/* Deadfall SNES port - mortal enemy AI (port of BaseEnemy.js + Enemy.js). */
#ifndef ENEMY_H
#define ENEMY_H

#include "types.h"

/* (Re)initialise an enemy to its dead/pending-spawn state. All enemies of a
 * level share the single spawn point; index/move_delay/initial_delay stagger
 * their (re)spawns. */
void enemy_reset(Enemy *e, u8 index, u8 sx, u8 sy, u8 srow, u8 scol,
                 u16 move_delay, u16 initial_delay);

/* One frame of enemy logic: spawn timers, BFS chase, smooth movement.
 * Uses game.frame and game.player; `all`/`count` drive separation. */
void enemy_update(Enemy *e, Enemy *all, u8 count);

void enemy_die(Enemy *e);                 /* kill + schedule staggered respawn */
u8   enemy_on_active_section(Enemy *e);   /* is the enemy on the visible section? */

#endif /* ENEMY_H */
