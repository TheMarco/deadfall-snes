/* Deadfall SNES port - player (port of Player.js). */
#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"

/* Result of an attempted move (filled via out-pointer; struct returns are
 * avoided for the old TCC-based 816 compiler). */
typedef struct {
    u8 transition;          /* crossed a section edge          */
    u8 dir;                 /* DIR_* of the transition         */
    u8 entry_x, entry_y;    /* entry cell in the new section    */
    u8 blocked;             /* move/push not possible           */
    u8 section_blocked;     /* adjacent-section entry blocked -> no-entry sfx */
    u8 pushed;              /* a gem/boulder was pushed         */
    u8 pushed_tile;
    u8 cross_push;          /* push crossed a section boundary  */
    u8 collected_extra_life;
} MoveResult;

void player_set_spawn(u8 x, u8 y, u8 row, u8 col);
void player_tick(void);                       /* per-frame bob + interpolation */
u8   player_would_push(s8 dx, s8 dy);         /* TRUE if moving (dx,dy) would shove a gem/boulder */
void player_move(s8 dx, s8 dy, MoveResult *r);
/* Mine block/gem in (dx,dy); returns TRUE if a mineable tile was hit and fills
 * out params (tile type, destroyed flag, target cell). */
u8   player_mine(s8 dx, s8 dy, u8 *out_tile, u8 *out_destroyed, u8 *out_x, u8 *out_y);
void player_transition_to(u8 row, u8 col, u8 entry_x, u8 entry_y);
void player_die(void);
void player_respawn(void);

#endif /* PLAYER_H */
