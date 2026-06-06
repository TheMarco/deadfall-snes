/* Deadfall SNES port - mortal enemy (port of BaseEnemy.js + Enemy.js).
 * Frame-based timing; one tile slide takes move_delay frames (continuous chase,
 * matching JS where moveDuration == moveDelay). */
#include "enemy.h"
#include "config.h"
#include "balance.h"
#include "world.h"
#include "ai.h"

static s16 iabs(s16 v) { return v < 0 ? (s16)(-v) : v; }

/* clustering penalty (raw; scaled by caller). */
static u16 separation_penalty(Enemy *e, s8 nx, s8 ny, Enemy *all, u8 count) {
    u16 pen = 0;
    u8 i;
    for (i = 0; i < count; i++) {
        Enemy *o = &all[i];
        s16 ox, oy, d;
        if (o == e || !o->alive) continue;
        if (o->section_row != e->section_row || o->section_col != e->section_col) continue;
        ox = (s16)(s8)(o->is_moving ? o->target_x : o->x);
        oy = (s16)(s8)(o->is_moving ? o->target_y : o->y);
        d = (s16)(iabs((s16)(nx - ox)) + iabs((s16)(ny - oy)));
        if (d < 3) pen = (u16)(pen + (3 - d));
    }
    return pen;
}

static void enemy_apply_move(Enemy *e, s8 dx, s8 dy) {
    if (dx < 0)      e->direction = DIR_LEFT;
    else if (dx > 0) e->direction = DIR_RIGHT;
    else if (dy < 0) e->direction = DIR_UP;
    else if (dy > 0) e->direction = DIR_DOWN;

    e->start_pixel_x = e->pixel_x;
    e->start_pixel_y = e->pixel_y;
    e->target_x = (u8)(e->x + dx);    /* may encode -1 (255) or GRID (16) */
    e->target_y = (u8)(e->y + dy);
    e->is_moving = TRUE;
    e->move_timer = (u8)e->move_delay;
    /* Precompute the per-frame slide step ONCE (8.8 fixed point). The 65816 has
     * no divide instruction, so doing this here -- not in the per-frame loop --
     * replaces ~2 software divides/frame with a single add+shift. The delta is
     * always +-1 tile, so the step easily fits s16. */
    e->mv_accx = 0; e->mv_accy = 0;
    e->mv_stepx = (s16)(((s16)(dx * TILE_SIZE) << 8) / (s16)e->move_delay);
    e->mv_stepy = (s16)(((s16)(dy * TILE_SIZE) << 8) / (s16)e->move_delay);
}

static void enemy_move_towards_player(Enemy *e, Enemy *all, u8 count) {
    Player *p = &game.player;
    u8 same, i, found = 0, allow_cross;
    s8 best_dx = 0, best_dy = 0;
    u16 best = 0xFFFF;
    static const s8 dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

    if (!p->alive) return;
    same = (u8)(p->section_row == e->section_row && p->section_col == e->section_col);

    /* Stickiness: an enemy already in the player's section pursues WITHIN it (via
     * the BFS field) and won't step across a boundary -- so a still player doesn't
     * get an enemy wandering out and circling back. A cross-out is only permitted
     * as a LAST RESORT (boxed in with no in-section move). When the enemy is in a
     * DIFFERENT section, crossing toward the player is the whole point -> allowed. */
    allow_cross = (u8)(!same);
    do {
    for (i = 0; i < 4; i++) {
        s8 dx = dirs[i][0], dy = dirs[i][1];
        s8 nx, ny;
        u16 d, score;
        u8 rev;
        if (!ai_can_move(e->section_row, e->section_col, e->x, e->y, dx, dy)) continue;
        nx = (s8)(e->x + dx); ny = (s8)(e->y + dy);
        if (!allow_cross && (nx < 0 || nx >= GRID_COLS || ny < 0 || ny >= GRID_ROWS))
            continue;     /* keep a same-section enemy inside the player's section */
        /* No hard enemy-vs-enemy block here -- the JS AI lets enemies share/pass
         * cells and only DISCOURAGES crowding via the separation penalty below. */

        if (same) {
            u8 bd = ai_dist((u8)nx, (u8)ny);   /* shared player distance field */
            d = (bd != 255) ? bd
              : (u16)(iabs((s16)(nx - p->x)) + iabs((s16)(ny - p->y)));
        } else {
            s16 egx = (s16)(e->section_col * GRID_COLS + ((nx >= 0 && nx < GRID_COLS) ? nx : e->x));
            s16 egy = (s16)(e->section_row * GRID_ROWS + ((ny >= 0 && ny < GRID_ROWS) ? ny : e->y));
            s16 pgx = (s16)(p->section_col * GRID_COLS + p->x);
            s16 pgy = (s16)(p->section_row * GRID_ROWS + p->y);
            s16 xd = ai_wrapped_diff(pgx, egx, (s16)(game.world_cols * GRID_COLS));
            s16 yd = ai_wrapped_diff(pgy, egy, (s16)(game.world_rows * GRID_ROWS));
            d = (u16)(iabs(xd) + iabs(yd));
        }

        /* Anti-oscillation: penalise reversing the last move (e->direction holds it
         * until enemy_apply_move overwrites it below) so the enemy commits to a
         * heading instead of ping-ponging across a boundary on a wrapping world. */
        rev = (u8)((e->direction == DIR_LEFT  && dx > 0) ||
                   (e->direction == DIR_RIGHT && dx < 0) ||
                   (e->direction == DIR_UP    && dy > 0) ||
                   (e->direction == DIR_DOWN  && dy < 0));

        /* score = dist*2 + penalty*3 (=*1.5*2) + jitter(0..1) + reverse bias */
        score = (u16)(d * 2 + separation_penalty(e, nx, ny, all, count) * 3 + (ai_rng() & 1)
                      + (rev ? ENEMY_REVERSE_PENALTY : 0));
        if (score < best) { best = score; best_dx = dx; best_dy = dy; found = 1; }
    }
    if (found || allow_cross) break;   /* got a move, or already tried with cross-out */
    allow_cross = 1;                   /* same-section but boxed in -> allow a cross-out */
    } while (1);
    if (found) enemy_apply_move(e, best_dx, best_dy);
}

static void enemy_update_movement(Enemy *e) {
    if (e->move_timer) e->move_timer--;
    if (e->move_timer == 0) {
        s8 tx = (s8)e->target_x, ty = (s8)e->target_y;
        if (tx < 0)               { e->section_col = (u8)((e->section_col + game.world_cols - 1) % game.world_cols); e->x = GRID_COLS - 1; }
        else if (tx >= GRID_COLS) { e->section_col = (u8)((e->section_col + 1) % game.world_cols);                   e->x = 0; }
        else                        e->x = (u8)tx;
        if (ty < 0)               { e->section_row = (u8)((e->section_row + game.world_rows - 1) % game.world_rows); e->y = GRID_ROWS - 1; }
        else if (ty >= GRID_ROWS) { e->section_row = (u8)((e->section_row + 1) % game.world_rows);                   e->y = 0; }
        else                        e->y = (u8)ty;
        e->pixel_x = (s16)(e->x * TILE_SIZE);
        e->pixel_y = (s16)(e->y * TILE_SIZE);
        e->target_x = e->x; e->target_y = e->y;
        e->is_moving = FALSE;
    } else {
        /* Divide-free slide: accumulate the precomputed 8.8 step (no per-frame
         * multiply/divide). Sub-pixel rounding is <=1px and the snap above lands
         * the enemy exactly on its cell, so it's visually identical. */
        e->mv_accx = (s16)(e->mv_accx + e->mv_stepx);
        e->mv_accy = (s16)(e->mv_accy + e->mv_stepy);
        e->pixel_x = (s16)(e->start_pixel_x + (e->mv_accx >> 8));
        e->pixel_y = (s16)(e->start_pixel_y + (e->mv_accy >> 8));
    }
}

static void enemy_try_respawn(Enemy *e) {
    u8 t = world_tile_at(e->spawn_row, e->spawn_col, e->spawn_x, e->spawn_y);
    Player *p = &game.player;
    if (t != TILE_EMPTY && t != TILE_SPAWN) return;          /* occupied, wait */
    if (p->section_row == e->spawn_row && p->section_col == e->spawn_col &&
        p->x == e->spawn_x && p->y == e->spawn_y) return;     /* player there */

    e->x = e->spawn_x; e->y = e->spawn_y;
    e->pixel_x = (s16)(e->spawn_x * TILE_SIZE);
    e->pixel_y = (s16)(e->spawn_y * TILE_SIZE);
    e->target_x = e->spawn_x; e->target_y = e->spawn_y;
    e->section_row = e->spawn_row; e->section_col = e->spawn_col;
    e->is_moving = FALSE;
    e->alive = TRUE;
    e->dying = 0;
    e->initial_spawn_pending = FALSE;
    e->respawn_pending = FALSE;
}

void enemy_reset(Enemy *e, u8 index, u8 sx, u8 sy, u8 srow, u8 scol,
                 u16 move_delay, u16 initial_delay) {
    e->alive = FALSE;
    e->is_moving = FALSE;
    e->spawn_x = sx; e->spawn_y = sy; e->spawn_row = srow; e->spawn_col = scol;
    e->x = sx; e->y = sy;
    e->pixel_x = (s16)(sx * TILE_SIZE); e->pixel_y = (s16)(sy * TILE_SIZE);
    e->target_x = sx; e->target_y = sy;
    e->section_row = srow; e->section_col = scol;
    e->direction = DIR_DOWN;
    e->move_delay = move_delay;
    e->enemy_index = index;
    e->initial_spawn_pending = TRUE;
    e->respawn_pending = FALSE;
    e->respawn_timer = initial_delay;       /* countdown to first spawn */
    e->anim_frame = 0;
    e->anim_timer = ANIM_BOB_FRAMES;
    e->dying = 0;
    e->death_timer = 0;
}

void enemy_update(Enemy *e, Enemy *all, u8 count) {
    if (e->alive) {
        if (e->anim_timer) e->anim_timer--;
        if (e->anim_timer == 0) { e->anim_frame ^= 1; e->anim_timer = ANIM_BOB_FRAMES; }
    }

    if (!e->alive) {
        if (e->dying) {                       /* play death anim, then fall through to respawn wait */
            if (e->death_timer) e->death_timer--;
            if (e->death_timer == 0) e->dying = 0;
        }
        if (e->respawn_timer) e->respawn_timer--;
        else                  enemy_try_respawn(e);
        return;
    }

    if (e->is_moving) enemy_update_movement(e);
    if (!e->is_moving) enemy_move_towards_player(e, all, count);
}

void enemy_die(Enemy *e) {
    if (!e->alive) return;                 /* don't restart the death anim */
    e->alive = FALSE;
    e->is_moving = FALSE;
    e->dying = 1;
    e->death_timer = DEATH_ANIM_FRAMES * DEATH_ANIM_COUNT;
    e->respawn_timer = (u16)(ENEMY_RESPAWN_DELAY_FRAMES + e->enemy_index * ENEMY_RESPAWN_STAGGER);
    e->respawn_pending = TRUE;
}

u8 enemy_on_active_section(Enemy *e) {
    return (u8)(e->section_row == game.cur_row && e->section_col == game.cur_col);
}
