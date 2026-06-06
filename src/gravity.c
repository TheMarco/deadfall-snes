/* Deadfall SNES port - gravity resolution (port of GravitySystem.js). */
#include "gravity.h"
#include "world.h"

#define IS_FALLABLE(t) ((t) == TILE_GEM || (t) == TILE_BOULDER || (t) == TILE_EXTRA_LIFE)
#define IS_FILLABLE(t) ((t) == TILE_EMPTY || (t) == TILE_ROBOT_SPAWN)

u8 gravity_step(void) {
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    u8 moved = 0;
    s8 y;
    u8 x, t, b;

    game.fall_count = 0;

    /* bottom-to-top so a stacked column shifts down one cell per pass */
    for (y = GRID_ROWS - 2; y >= 0; y--) {
        for (x = 0; x < GRID_COLS; x++) {
            t = game.sections[idx][y][x];
            if (!IS_FALLABLE(t)) continue;
            b = game.sections[idx][y + 1][x];
            if (!IS_FILLABLE(b)) continue;

            game.sections[idx][y + 1][x] = t;
            game.sections[idx][y][x] = TILE_EMPTY;

            if (t == TILE_GEM) {            /* carry partial damage down */
                u8 d = game.damage[idx][y][x];
                game.damage[idx][y][x] = 0;
                if (d) game.damage[idx][y + 1][x] = d;
            }

            if (game.fall_count < SECTION_TILES) {
                FallTile *f = &game.fall_tiles[game.fall_count++];
                f->type = t; f->from_x = x; f->from_y = (u8)y;
                f->to_x = x; f->to_y = (u8)(y + 1);
            }
            moved = 1;
        }
    }
    return moved;
}

/* Settle the current section's gravity in ONE bottom-up pass per column (column
 * compaction), emitting a fall record per tile that dropped. This replaces an
 * O(rows^2 * cols) bubble (a full 208-cell re-scan per row of fall, each access a
 * software idx*208 multiply on the 65816) that spiked a whole frame on big
 * cascades. Hoisting the section base pointer drops the per-access multiply too.
 * Verified identical to the old bubble (final grid + damage + fall-record set)
 * over 300k random grids -- see tools/test_gravity_equiv.py. */
u8 gravity_settle(FallTile *out, u8 max) {
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    u8 (*sec)[GRID_COLS] = game.sections[idx];   /* hoist: no idx*208 per access */
    u8 (*dmg)[GRID_COLS] = game.damage[idx];
    u8 x, t, n = 0;
    s8 y, write, c;

    for (x = 0; x < GRID_COLS; x++) {
        write = GRID_ROWS - 1;                   /* lowest slot a faller can take  */
        for (y = GRID_ROWS - 1; y >= 0; y--) {
            t = sec[y][x];
            if (IS_FALLABLE(t)) {
                if (write != y) {                /* tile drops y -> write          */
                    sec[write][x] = t;
                    for (c = y; c < write; c++)  /* clear origin + the fall path    */
                        sec[c][x] = TILE_EMPTY;   /* (matches the old per-cell bubble:
                                                   * a tile overwrites fillables it
                                                   * passes through, e.g. ROBOT_SPAWN) */
                    if (t == TILE_GEM) {          /* carry partial mining damage down */
                        u8 d = dmg[y][x];
                        dmg[y][x] = 0;
                        if (d) dmg[write][x] = d;
                    }
                    if (n < max) {
                        out[n].type = t;
                        out[n].from_x = x; out[n].from_y = (u8)y;
                        out[n].to_x = x;   out[n].to_y = (u8)write;
                        n++;
                    }
                }
                write--;                         /* next faller stacks just above   */
            } else if (!IS_FILLABLE(t)) {         /* solid barrier: stack rests on it */
                write = (s8)(y - 1);
            }
            /* else: fillable gap -- leave it; a faller from above clears it if it
             * falls through (handled by the fall-path clear), else it stays. */
        }
    }
    return n;
}
