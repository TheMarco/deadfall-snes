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

u8 gravity_settle(FallTile *out, u8 max) {
    static u8 origin[GRID_ROWS][GRID_COLS];
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    u8 moved = 1, x, t, n = 0;
    s8 y;

    for (y = 0; y < GRID_ROWS; y++)
        for (x = 0; x < GRID_COLS; x++)
            origin[y][x] = (u8)y;

    while (moved) {
        moved = 0;
        for (y = GRID_ROWS - 2; y >= 0; y--) {
            for (x = 0; x < GRID_COLS; x++) {
                t = game.sections[idx][y][x];
                if (!IS_FALLABLE(t)) continue;
                if (!IS_FILLABLE(game.sections[idx][y + 1][x])) continue;

                game.sections[idx][y + 1][x] = t;
                game.sections[idx][y][x] = TILE_EMPTY;
                if (t == TILE_GEM) {
                    u8 d = game.damage[idx][y][x];
                    game.damage[idx][y][x] = 0;
                    if (d) game.damage[idx][y + 1][x] = d;
                }
                origin[y + 1][x] = origin[y][x];
                moved = 1;
            }
        }
    }

    /* emit one record per tile that ended below where it started */
    for (y = 0; y < GRID_ROWS; y++) {
        for (x = 0; x < GRID_COLS; x++) {
            t = game.sections[idx][y][x];
            if (IS_FALLABLE(t) && origin[y][x] != (u8)y && n < max) {
                out[n].type = t;
                out[n].from_x = x; out[n].from_y = origin[y][x];
                out[n].to_x = x;   out[n].to_y = (u8)y;
                n++;
            }
        }
    }
    return n;
}
