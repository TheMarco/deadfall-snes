/* Deadfall SNES port - shared enemy/robot movement primitives (BaseEnemy.js). */
#ifndef AI_H
#define AI_H

#include "types.h"

u16 ai_rng(void);                                  /* xorshift jitter for chasing */
s16 ai_wrapped_diff(s16 target, s16 current, s16 max);  /* shortest torus delta   */

/* Can an actor in section (srow,scol) at (x,y) step (dx,dy)? Destination tile
 * (with section wrap) must be empty. */
u8 ai_can_move(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy);

/* Entity-vs-entity blocking: enemies and the robot are solid to EACH OTHER (but
 * enemies still pass through each other, per the JS AI). Used to skip a candidate
 * step that would overlap the other actor's cell (or the cell it is sliding into). */
u8 ai_robot_at_step(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy);  /* robot blocks an enemy */
u8 ai_enemy_at_step(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy);  /* an enemy blocks the robot */

/* Distance field flooded from (tx,ty) over a section's empty tiles, computed
 * incrementally so no single frame spikes: begin() seeds it, work() expands up
 * to `budget` cells per call (call every frame). All enemies in that section
 * then read distances via ai_dist() cheaply. */
void ai_distfield_begin(u8 srow, u8 scol, u8 tx, u8 ty);
void ai_distfield_work(u8 budget);

/* Distance from the field's target to (x,y); 255 if unreachable / not yet
 * flooded (caller falls back to Manhattan). */
u8 ai_dist(u8 x, u8 y);

#endif /* AI_H */
