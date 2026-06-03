/* Deadfall SNES port - difficulty scaling (from GameBalance.js).
 * All delays are in frames @ ~60 fps. */
#ifndef BALANCE_H
#define BALANCE_H

#include "types.h"

/* Master enemy speed multiplier 1.4x, applied as 14/10. */
#define SPEED_MULT_NUM  14
#define SPEED_MULT_DEN  10

/* Pre-multiplier base/min from GameBalance.js (650ms / 285ms),
 * then *1.4 -> frames:  650ms=39f, *1.4=55f ; 285ms=17f, *1.4=24f. */
#define ENEMY_BASE_DELAY_FRAMES   37   /* level 1  (~1.5x faster; ratios vs player kept) */
#define ENEMY_MIN_DELAY_FRAMES    16   /* level 10 (still slower than the player)        */

/* Robot moves 1.5x slower than enemies (3/2). */
#define ROBOT_SPEED_NUM   3
#define ROBOT_SPEED_DEN   2

/* Robot zap. */
#define ROBOT_ZAP_COOLDOWN_FRAMES  120  /* 2000ms */
#define ROBOT_ZAP_CHARGE_FRAMES    24   /* 400ms  */
#define ROBOT_ZAP_RANGE            3    /* tiles  */

/* Spawn / respawn (10000ms*1.4=14000ms=840f). */
#define ENEMY_RESPAWN_DELAY_FRAMES 840
#define ENEMY_RESPAWN_STAGGER      30   /* 500ms  */
#define ENEMY_INITIAL_SPAWN_FRAMES 60   /* 1000ms */
#define ENEMY_SPAWN_STAGGER_FRAMES 300  /* 5000ms */

u16 get_enemy_move_delay(u8 level);  /* frames */
u16 get_robot_move_delay(u8 level);  /* frames */
u16 get_enemy_respawn_delay(void);   /* frames */

#endif /* BALANCE_H */
