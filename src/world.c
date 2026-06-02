/* Deadfall SNES port - multi-section world grid (port of CubeWorld.js). */
#include "world.h"
#include "levels.h"

u8 world_section_index(u8 row, u8 col) {
    return (u8)(row * game.world_cols + col);
}

u8 *world_grid_at(u8 row, u8 col) {
    return &game.sections[world_section_index(row, col)][0][0];
}

u8 world_tile_at(u8 row, u8 col, u8 x, u8 y) {
    return game.sections[world_section_index(row, col)][y][x];
}

void world_set_tile_at(u8 row, u8 col, u8 x, u8 y, u8 t) {
    game.sections[world_section_index(row, col)][y][x] = t;
}

u8 world_get_tile(u8 x, u8 y) {
    return game.sections[world_section_index(game.cur_row, game.cur_col)][y][x];
}

void world_set_tile(u8 x, u8 y, u8 t) {
    game.sections[world_section_index(game.cur_row, game.cur_col)][y][x] = t;
}

void world_switch_section(u8 row, u8 col) {
    game.cur_row = row;
    game.cur_col = col;
}

void world_adjacent(u8 dir, SectionRef *out) {
    u8 r = game.cur_row, c = game.cur_col;
    switch (dir) {
        case DIR_LEFT:  c = (u8)((c + game.world_cols - 1) % game.world_cols); break;
        case DIR_RIGHT: c = (u8)((c + 1) % game.world_cols); break;
        case DIR_UP:    r = (u8)((r + game.world_rows - 1) % game.world_rows); break;
        case DIR_DOWN:  r = (u8)((r + 1) % game.world_rows); break;
        default: break;
    }
    out->row = r;
    out->col = c;
}

u8 world_cross_push_target(s8 px, s8 py, s8 dx, s8 dy,
                           SectionRef *sec, u8 *ox, u8 *oy) {
    u8 r = game.cur_row, c = game.cur_col;
    s8 x = px, y = py;
    (void)dx; (void)dy;

    if (px < 0)                 { c = (u8)((c + game.world_cols - 1) % game.world_cols); x = GRID_COLS - 1; }
    else if (px >= GRID_COLS)   { c = (u8)((c + 1) % game.world_cols);                   x = 0; }
    if (py < 0)                 { r = (u8)((r + game.world_rows - 1) % game.world_rows); y = GRID_ROWS - 1; }
    else if (py >= GRID_ROWS)   { r = (u8)((r + 1) % game.world_rows);                   y = 0; }

    sec->row = r; sec->col = c;
    *ox = (u8)x;  *oy = (u8)y;
    return TRUE;
}

u8 world_get_damage(u8 x, u8 y) {
    return game.damage[world_section_index(game.cur_row, game.cur_col)][y][x];
}

void world_set_damage(u8 x, u8 y, u8 d) {
    game.damage[world_section_index(game.cur_row, game.cur_col)][y][x] = d;
}

void world_clear_damage(u8 x, u8 y) {
    game.damage[world_section_index(game.cur_row, game.cur_col)][y][x] = 0;
}

u8 world_damage_tile(u8 x, u8 y) {
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    u8 d = (u8)(game.damage[idx][y][x] + 1);
    if (d >= TILE_HITS_TO_DESTROY) {
        game.damage[idx][y][x] = 0;
        return TRUE;            /* destroyed */
    }
    game.damage[idx][y][x] = d;
    return FALSE;
}

u16 world_count_gems(void) {
    u8 nsec = (u8)(game.world_cols * game.world_rows);
    u16 count = 0;
    u8 s, y, x;
    for (s = 0; s < nsec; s++)
        for (y = 0; y < GRID_ROWS; y++)
            for (x = 0; x < GRID_COLS; x++)
                if (game.sections[s][y][x] == TILE_GEM) count++;
    return count;
}

void world_load_level(u8 level) {
    const LevelConfig *cfg = &level_configs[level - 1];
    const u8 *src = level_data[level - 1];
    u8 nsec, s, y, x, r, c, t;

    game.current_level = level;
    game.cfg = *cfg;
    game.world_cols = cfg->world_cols;
    game.world_rows = cfg->world_rows;
    nsec = (u8)(game.world_cols * game.world_rows);

    /* Copy level data into the working RAM grid; clear damage. */
    for (s = 0; s < nsec; s++) {
        const u8 *sd = src + (u16)s * SECTION_TILES;
        for (y = 0; y < GRID_ROWS; y++)
            for (x = 0; x < GRID_COLS; x++) {
                game.sections[s][y][x] = sd[y * GRID_COLS + x];
                game.damage[s][y][x]   = 0;
            }
    }

    /* Scan spawn/portal/robot-spawn markers per section. */
    game.has_portal = FALSE;
    for (r = 0; r < game.world_rows; r++) {
        for (c = 0; c < game.world_cols; c++) {
            u8 idx = (u8)(r * game.world_cols + c);
            game.spawn_count[idx] = 0;
            game.robot_spawn_count[idx] = 0;
            for (y = 0; y < GRID_ROWS; y++) {
                for (x = 0; x < GRID_COLS; x++) {
                    t = game.sections[idx][y][x];
                    if (t == TILE_SPAWN && game.spawn_count[idx] < MAX_SPAWN_POINTS) {
                        game.spawn_points[idx][game.spawn_count[idx]].x = x;
                        game.spawn_points[idx][game.spawn_count[idx]].y = y;
                        game.spawn_count[idx]++;
                    } else if (t == TILE_PORTAL) {
                        if (!game.has_portal) {        /* first portal = spawn/exit */
                            game.portal.x = x; game.portal.y = y;
                            game.portal_row = r; game.portal_col = c;
                            game.has_portal = TRUE;
                        }
                    } else if (t == TILE_ROBOT_SPAWN &&
                               game.robot_spawn_count[idx] < MAX_ROBOT_SPAWNS) {
                        game.robot_spawns[idx][game.robot_spawn_count[idx]].x = x;
                        game.robot_spawns[idx][game.robot_spawn_count[idx]].y = y;
                        game.robot_spawn_count[idx]++;
                    }
                }
            }
        }
    }

    game.total_gems = world_count_gems();
    game.gems_collected = 0;

    /* Player starts on the portal section. */
    if (game.has_portal) world_switch_section(game.portal_row, game.portal_col);
    else                 world_switch_section(0, 0);
}
