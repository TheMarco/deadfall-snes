/* Deadfall SNES port - difficulty scaling.
 * Mirrors getEnemyMoveDelay/getRobotMoveDelay in ../cubed/src/GameBalance.js:
 *   t = (level-1)/9 ; delay = BASE - t*(BASE-MIN)   (the *1.4 is already
 *   folded into the frame constants in balance.h). */
#include "balance.h"

u16 get_enemy_move_delay(u8 level) {
    u8  lv = level;
    u16 range, drop;

    if (lv < 1)  lv = 1;
    if (lv > 10) lv = 10;

    range = ENEMY_BASE_DELAY_FRAMES - ENEMY_MIN_DELAY_FRAMES; /* 31 */
    drop  = (u16)((u16)(lv - 1) * range) / 9;                 /* t*(BASE-MIN) */
    return (u16)(ENEMY_BASE_DELAY_FRAMES - drop);
}

u16 get_robot_move_delay(u8 level) {
    return (u16)(get_enemy_move_delay(level) * ROBOT_SPEED_NUM) / ROBOT_SPEED_DEN;
}

u16 get_enemy_respawn_delay(void) {
    return ENEMY_RESPAWN_DELAY_FRAMES;
}
