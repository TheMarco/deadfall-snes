/* Deadfall SNES port - robot "Clanky" (port of RobotEnemy.js). Invincible;
 * hunts, locks on, charges, then fires a cardinal lightning zap. */
#ifndef ROBOT_H
#define ROBOT_H

#include "types.h"

void robot_reset(Robot *r, u8 x, u8 y, u8 row, u8 col, u16 move_delay);
void robot_update(Robot *r);                 /* uses game.player/game.frame */
u8   robot_on_active_section(Robot *r);
/* TRUE if the active zap beam overlaps a 16x16 box at pixel (px,py) in the
 * robot's section (caller checks section). */
u8   robot_zap_hits_px(Robot *r, s16 px, s16 py);

#endif /* ROBOT_H */
