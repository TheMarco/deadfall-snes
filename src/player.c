/* Deadfall SNES port - player movement/mining (port of Player.js). */
#include "player.h"
#include "world.h"
#include "audio.h"

#define IS_SOLID(t) ((t) == TILE_BLOCK || (t) == TILE_BOULDER || \
                     (t) == TILE_GEM   || (t) == TILE_EXTRA_LIFE_BLK)

/* Begin a smooth move of the player to (nx,ny) in the current section. The grid
 * cell updates immediately; the pixel position interpolates over the move. */
static void start_move(u8 nx, u8 ny) {
    Player *p = &game.player;
    p->start_pixel_x = p->pixel_x;
    p->start_pixel_y = p->pixel_y;
    p->x = nx; p->y = ny;
    p->target_x = nx; p->target_y = ny;
    p->is_moving = TRUE;
    p->move_timer = PLAYER_MOVE_FRAMES;
}

void player_set_spawn(u8 x, u8 y, u8 row, u8 col) {
    Player *p = &game.player;
    p->start_x = x; p->start_y = y; p->start_row = row; p->start_col = col;
    p->x = x; p->y = y;
    p->pixel_x = (s16)(x * TILE_SIZE);
    p->pixel_y = (s16)(y * TILE_SIZE);
    p->target_x = x; p->target_y = y;
    p->section_row = row; p->section_col = col;
    p->alive = TRUE;
    p->is_moving = FALSE;
    p->move_timer = 0;
    p->direction = DIR_DOWN;
    p->anim_frame = 0; p->anim_timer = 0;
    world_switch_section(row, col);
}

void player_tick(void) {
    Player *p = &game.player;

    if (p->alive) {
        if (p->anim_timer == 0) p->anim_timer = ANIM_BOB_FRAMES;
        if (--p->anim_timer == 0) { p->anim_frame ^= 1; p->anim_timer = ANIM_BOB_FRAMES; }
    }

    if (!p->is_moving) return;

    if (p->move_timer > 0) p->move_timer--;
    if (p->move_timer == 0) {
        p->is_moving = FALSE;
        p->x = p->target_x; p->y = p->target_y;
        p->pixel_x = (s16)(p->x * TILE_SIZE);
        p->pixel_y = (s16)(p->y * TILE_SIZE);
    } else {
        s16 elapsed = (s16)(PLAYER_MOVE_FRAMES - p->move_timer);
        s16 tx = (s16)(p->target_x * TILE_SIZE);
        s16 ty = (s16)(p->target_y * TILE_SIZE);
        p->pixel_x = (s16)(p->start_pixel_x + (s16)((tx - p->start_pixel_x) * elapsed) / PLAYER_MOVE_FRAMES);
        p->pixel_y = (s16)(p->start_pixel_y + (s16)((ty - p->start_pixel_y) * elapsed) / PLAYER_MOVE_FRAMES);
    }
}

/* Would a move in (dx,dy) shove a gem/boulder? (horizontal only, target in
 * bounds and pushable). Mirrors the JS checkWouldPush so game.c can apply the
 * push-delay: you only push after holding the direction, not on a tap. */
u8 player_would_push(s8 dx, s8 dy) {
    Player *p = &game.player;
    s8 nx;
    u8 t;
    if (!p->alive || dy != 0) return FALSE;       /* push is horizontal only */
    nx = (s8)(p->x + dx);
    if (nx < 0 || nx >= GRID_COLS) return FALSE;  /* edge = transition, not a push */
    t = world_get_tile((u8)nx, p->y);
    return (u8)(t == TILE_GEM || t == TILE_BOULDER);
}

void player_move(s8 dx, s8 dy, MoveResult *r) {
    Player *p = &game.player;
    s8 nx, ny;
    u8 t;
    SectionRef adj;

    r->transition = r->blocked = r->pushed = r->cross_push = 0;
    r->collected_extra_life = 0;
    r->section_blocked = 0;

    if (!p->alive) { r->blocked = 1; return; }
    if (dx < 0) p->direction = DIR_LEFT;
    else if (dx > 0) p->direction = DIR_RIGHT;
    else if (dy < 0) p->direction = DIR_UP;
    else if (dy > 0) p->direction = DIR_DOWN;

    nx = (s8)(p->x + dx);
    ny = (s8)(p->y + dy);

    /* ---- section edge -> transition (with wrap), blocked by solid tiles ---- */
    if (nx < 0) {
        world_adjacent(DIR_LEFT, &adj);
        t = world_tile_at(adj.row, adj.col, GRID_COLS - 1, p->y);
        if (IS_SOLID(t)) { r->blocked = r->section_blocked = 1; return; }
        r->transition = 1; r->dir = DIR_LEFT; r->entry_x = GRID_COLS - 1; r->entry_y = p->y; return;
    }
    if (nx >= GRID_COLS) {
        world_adjacent(DIR_RIGHT, &adj);
        t = world_tile_at(adj.row, adj.col, 0, p->y);
        if (IS_SOLID(t)) { r->blocked = r->section_blocked = 1; return; }
        r->transition = 1; r->dir = DIR_RIGHT; r->entry_x = 0; r->entry_y = p->y; return;
    }
    if (ny < 0) {
        world_adjacent(DIR_UP, &adj);
        t = world_tile_at(adj.row, adj.col, p->x, GRID_ROWS - 1);
        if (IS_SOLID(t)) { r->blocked = r->section_blocked = 1; return; }
        r->transition = 1; r->dir = DIR_UP; r->entry_x = p->x; r->entry_y = GRID_ROWS - 1; return;
    }
    if (ny >= GRID_ROWS) {
        world_adjacent(DIR_DOWN, &adj);
        t = world_tile_at(adj.row, adj.col, p->x, 0);
        if (IS_SOLID(t)) { r->blocked = r->section_blocked = 1; return; }
        r->transition = 1; r->dir = DIR_DOWN; r->entry_x = p->x; r->entry_y = 0; return;
    }

    t = world_get_tile((u8)nx, (u8)ny);

    /* ---- push gem/boulder horizontally only ---- */
    if ((t == TILE_GEM || t == TILE_BOULDER) && dy == 0) {
        s8 px = (s8)(nx + dx);
        s8 py = ny;
        if (px >= 0 && px < GRID_COLS) {
            if (world_get_tile((u8)px, (u8)py) == TILE_EMPTY) {
                world_set_tile((u8)px, (u8)py, t);
                world_set_tile((u8)nx, (u8)ny, TILE_EMPTY);
                if (t == TILE_GEM) {
                    u8 d = world_get_damage((u8)nx, (u8)ny);
                    world_clear_damage((u8)nx, (u8)ny);
                    if (d) world_set_damage((u8)px, (u8)py, d);
                }
                start_move((u8)nx, (u8)ny);
                r->pushed = 1; r->pushed_tile = t; return;
            }
            r->blocked = 1; return;
        } else {
            SectionRef sec; u8 ox, oy;
            world_cross_push_target(px, py, dx, dy, &sec, &ox, &oy);
            if (world_tile_at(sec.row, sec.col, ox, oy) == TILE_EMPTY) {
                world_set_tile_at(sec.row, sec.col, ox, oy, t);
                world_set_tile((u8)nx, (u8)ny, TILE_EMPTY);
                world_clear_damage((u8)nx, (u8)ny);
                start_move((u8)nx, (u8)ny);
                r->pushed = 1; r->pushed_tile = t; r->cross_push = 1; return;
            }
            r->blocked = 1; return;
        }
    }

    /* ---- solid (also blocks vertical into a gem/boulder) ---- */
    if (IS_SOLID(t)) { r->blocked = 1; return; }

    /* ---- extra life is walk-through (collected) ---- */
    if (t == TILE_EXTRA_LIFE) r->collected_extra_life = 1;

    start_move((u8)nx, (u8)ny);
}

u8 player_mine(s8 dx, s8 dy, u8 *out_tile, u8 *out_destroyed, u8 *out_x, u8 *out_y) {
    Player *p = &game.player;
    s8 tx, ty;
    u8 t;

    if (!p->alive) return FALSE;
    if (dx < 0) p->direction = DIR_LEFT;
    else if (dx > 0) p->direction = DIR_RIGHT;
    else if (dy < 0) p->direction = DIR_UP;
    else if (dy > 0) p->direction = DIR_DOWN;

    tx = (s8)(p->x + dx);
    ty = (s8)(p->y + dy);
    if (tx < 0 || tx >= GRID_COLS || ty < 0 || ty >= GRID_ROWS) return FALSE;

    t = world_get_tile((u8)tx, (u8)ty);
    if (t != TILE_GEM && t != TILE_BLOCK && t != TILE_EXTRA_LIFE_BLK) return FALSE;

    *out_tile = t;
    *out_destroyed = world_damage_tile((u8)tx, (u8)ty);
    *out_x = (u8)tx; *out_y = (u8)ty;
    return TRUE;
}

void player_transition_to(u8 row, u8 col, u8 entry_x, u8 entry_y) {
    Player *p = &game.player;
    world_switch_section(row, col);
    p->section_row = row; p->section_col = col;
    p->x = entry_x; p->y = entry_y;
    p->pixel_x = (s16)(entry_x * TILE_SIZE);
    p->pixel_y = (s16)(entry_y * TILE_SIZE);
    p->target_x = entry_x; p->target_y = entry_y;
    p->is_moving = FALSE;
    p->move_timer = 0;
}

void player_die(void) {
    if (game.player.alive) audio_sfx(SFX_DEATH);
    game.player.alive = FALSE;
    game.player.is_moving = FALSE;
}

void player_respawn(void) {
    Player *p = &game.player;
    p->x = p->start_x; p->y = p->start_y;
    p->pixel_x = (s16)(p->start_x * TILE_SIZE);
    p->pixel_y = (s16)(p->start_y * TILE_SIZE);
    p->target_x = p->start_x; p->target_y = p->start_y;
    p->is_moving = FALSE; p->move_timer = 0;
    p->section_row = p->start_row; p->section_col = p->start_col;
    p->alive = TRUE;
    world_switch_section(p->start_row, p->start_col);
}
