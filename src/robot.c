/* Deadfall SNES port - robot "Clanky" (port of RobotEnemy.js). */
#include "robot.h"
#include "config.h"
#include "balance.h"
#include "world.h"
#include "ai.h"
#include "audio.h"

#define ZAP_ANIM_FRAMES  9    /* 150ms per zap frame (4 frames) */
#define EYE_FRAMES       12   /* 200ms eye pulse */

static const s8 DIRDX[4] = {-1, 1, 0, 0};   /* LEFT,RIGHT,UP,DOWN */
static const s8 DIRDY[4] = { 0, 0,-1, 1};

static s16 iabs(s16 v) { return v < 0 ? (s16)(-v) : v; }

/* ---- movement (same interpolation/wrap as the enemy) ---- */
static void robot_apply_move(Robot *r, s8 dx, s8 dy) {
    if (dx < 0)      r->direction = DIR_LEFT;
    else if (dx > 0) r->direction = DIR_RIGHT;
    else if (dy < 0) r->direction = DIR_UP;
    else if (dy > 0) r->direction = DIR_DOWN;
    r->start_pixel_x = r->pixel_x;
    r->start_pixel_y = r->pixel_y;
    r->target_x = (u8)(r->x + dx);
    r->target_y = (u8)(r->y + dy);
    r->is_moving = TRUE;
    r->move_timer = (u8)r->move_delay;
}

static void robot_update_movement(Robot *r) {
    if (r->move_timer) r->move_timer--;
    if (r->move_timer == 0) {
        s8 tx = (s8)r->target_x, ty = (s8)r->target_y;
        if (tx < 0)               { r->section_col = (u8)((r->section_col + game.world_cols - 1) % game.world_cols); r->x = GRID_COLS - 1; }
        else if (tx >= GRID_COLS) { r->section_col = (u8)((r->section_col + 1) % game.world_cols);                   r->x = 0; }
        else                        r->x = (u8)tx;
        if (ty < 0)               { r->section_row = (u8)((r->section_row + game.world_rows - 1) % game.world_rows); r->y = GRID_ROWS - 1; }
        else if (ty >= GRID_ROWS) { r->section_row = (u8)((r->section_row + 1) % game.world_rows);                   r->y = 0; }
        else                        r->y = (u8)ty;
        r->pixel_x = (s16)(r->x * TILE_SIZE);
        r->pixel_y = (s16)(r->y * TILE_SIZE);
        r->target_x = r->x; r->target_y = r->y;
        r->is_moving = FALSE;
    } else {
        s16 elapsed = (s16)(r->move_delay - r->move_timer);
        s16 tpx = (s16)((s8)r->target_x * TILE_SIZE);
        s16 tpy = (s16)((s8)r->target_y * TILE_SIZE);
        r->pixel_x = (s16)(r->start_pixel_x + (s16)((tpx - r->start_pixel_x) * elapsed) / (s16)r->move_delay);
        r->pixel_y = (s16)(r->start_pixel_y + (s16)((tpy - r->start_pixel_y) * elapsed) / (s16)r->move_delay);
    }
}

static void robot_move_towards(Robot *r) {
    Player *p = &game.player;
    u8 same, i, found = 0;
    s8 best_dx = 0, best_dy = 0;
    u16 best = 0xFFFF;
    static const s8 dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

    if (!p->alive) return;
    same = (u8)(p->section_row == r->section_row && p->section_col == r->section_col);

    for (i = 0; i < 4; i++) {
        s8 dx = dirs[i][0], dy = dirs[i][1], nx, ny;
        u16 d, score;
        if (!ai_can_move(r->section_row, r->section_col, r->x, r->y, dx, dy)) continue;
        nx = (s8)(r->x + dx); ny = (s8)(r->y + dy);
        if (same) {
            u8 bd = ai_dist((u8)nx, (u8)ny);   /* shared player distance field */
            d = (bd != 255) ? bd : (u16)(iabs((s16)(nx - p->x)) + iabs((s16)(ny - p->y)));
        } else {
            s16 egx = (s16)(r->section_col * GRID_COLS + ((nx >= 0 && nx < GRID_COLS) ? nx : r->x));
            s16 egy = (s16)(r->section_row * GRID_ROWS + ((ny >= 0 && ny < GRID_ROWS) ? ny : r->y));
            s16 pgx = (s16)(p->section_col * GRID_COLS + p->x);
            s16 pgy = (s16)(p->section_row * GRID_ROWS + p->y);
            d = (u16)(iabs(ai_wrapped_diff(pgx, egx, (s16)(game.world_cols * GRID_COLS))) +
                      iabs(ai_wrapped_diff(pgy, egy, (s16)(game.world_rows * GRID_ROWS))));
        }
        score = (u16)(d * 2 + (ai_rng() & 1));
        if (score < best) { best = score; best_dx = dx; best_dy = dy; found = 1; }
    }
    if (found) robot_apply_move(r, best_dx, best_dy);
}

/* ---- zap detection ---- */
static u8 dir_towards(Robot *r, u8 tx, u8 ty) {
    s8 dx = (s8)(tx - r->x), dy = (s8)(ty - r->y);
    if (dx < 0) return DIR_LEFT;
    if (dx > 0) return DIR_RIGHT;
    if (dy < 0) return DIR_UP;
    return DIR_DOWN;
}

static u8 los_clear(Robot *r, u8 fx, u8 fy, u8 tx, u8 ty) {
    u8 idx = world_section_index(r->section_row, r->section_col);
    s8 sx = (tx > fx) ? 1 : ((tx < fx) ? -1 : 0);
    s8 sy = (ty > fy) ? 1 : ((ty < fy) ? -1 : 0);
    s8 cx = (s8)(fx + sx), cy = (s8)(fy + sy);
    while (cx != (s8)tx || cy != (s8)ty) {
        if (cx < 0 || cx >= GRID_COLS || cy < 0 || cy >= GRID_ROWS) return FALSE;
        if (game.sections[idx][cy][cx] != TILE_EMPTY) return FALSE;
        cx += sx; cy += sy;
    }
    return TRUE;
}

static u8 can_zap(Robot *r) {
    Player *p = &game.player;
    u8 rx, ry, k, n;
    if (!p->alive) return FALSE;
    if (p->section_row != r->section_row || p->section_col != r->section_col) return FALSE;
    rx = r->is_moving ? (u8)(s8)r->target_x : r->x;
    ry = r->is_moving ? (u8)(s8)r->target_y : r->y;
    n = p->is_moving ? 2 : 1;
    for (k = 0; k < n; k++) {
        u8 px = (k == 0) ? p->x : (u8)((p->start_pixel_x >= 0 ? p->start_pixel_x : 0) / TILE_SIZE);
        u8 py = (k == 0) ? p->y : (u8)((p->start_pixel_y >= 0 ? p->start_pixel_y : 0) / TILE_SIZE);
        s8 dx = (s8)(px - rx), dy = (s8)(py - ry);
        u8 dist;
        if (dx != 0 && dy != 0) continue;
        dist = (u8)(iabs(dx) + iabs(dy));
        if (dist == 0 || dist > ROBOT_ZAP_RANGE) continue;
        if (los_clear(r, rx, ry, px, py)) return TRUE;
    }
    return FALSE;
}

static void robot_fire(Robot *r) {
    u8 idx = world_section_index(r->section_row, r->section_col);
    s8 dx = DIRDX[r->locked_direction], dy = DIRDY[r->locked_direction];
    u8 actual = ROBOT_ZAP_RANGE, i;

    r->is_zapping = TRUE;
    r->just_fired = TRUE;
    r->zap_anim_frame = 0;
    r->zap_anim_timer = ZAP_ANIM_FRAMES;
    r->cooldown_timer = ROBOT_ZAP_COOLDOWN_FRAMES;
    r->zap_direction = r->locked_direction;
    audio_sfx(SFX_ZAP);

    for (i = 1; i <= ROBOT_ZAP_RANGE; i++) {
        s8 cx = (s8)(r->x + dx * i), cy = (s8)(r->y + dy * i);
        u8 t;
        if (cx < 0 || cx >= GRID_COLS || cy < 0 || cy >= GRID_ROWS) { actual = (u8)(i - 1); break; }
        t = game.sections[idx][cy][cx];
        if (t == TILE_BOULDER) { actual = (u8)(i - 1); break; }     /* beam stops at boulder */
        if (t == TILE_BLOCK || t == TILE_GEM || t == TILE_EXTRA_LIFE_BLK || t == TILE_EXTRA_LIFE) {
            game.sections[idx][cy][cx] = TILE_EMPTY;                /* destroyed, NOT collected */
            game.damage[idx][cy][cx] = 0;
        }
    }
    r->zap_distance = actual;
}

void robot_reset(Robot *r, u8 x, u8 y, u8 row, u8 col, u16 move_delay) {
    r->x = x; r->y = y;
    r->pixel_x = (s16)(x * TILE_SIZE); r->pixel_y = (s16)(y * TILE_SIZE);
    r->target_x = x; r->target_y = y;
    r->section_row = row; r->section_col = col;
    r->alive = TRUE;
    r->direction = DIR_DOWN;
    r->is_moving = FALSE; r->move_timer = 0;
    r->move_delay = move_delay;
    r->cooldown_timer = 0;          /* allow immediate first zap */
    r->has_locked_on = FALSE; r->locked_direction = DIR_DOWN;
    r->is_charging = FALSE; r->charge_timer = 0;
    r->is_zapping = FALSE; r->just_fired = FALSE;
    r->zap_direction = DIR_DOWN; r->zap_distance = ROBOT_ZAP_RANGE;
    r->zap_anim_frame = 0; r->zap_anim_timer = 0;
    r->eye_index = 0; r->eye_timer = EYE_FRAMES;
}

void robot_update(Robot *r) {
    r->just_fired = FALSE;

    if (r->eye_timer) r->eye_timer--;
    if (r->eye_timer == 0) { r->eye_index = (u8)((r->eye_index + 1) & 3); r->eye_timer = EYE_FRAMES; }

    if (r->is_moving) robot_update_movement(r);

    if (r->is_zapping) {
        if (r->zap_anim_timer) r->zap_anim_timer--;
        if (r->zap_anim_timer == 0) {
            r->zap_anim_frame++;
            if (r->zap_anim_frame >= 4) { r->is_zapping = FALSE; r->zap_anim_frame = 0; }
            else r->zap_anim_timer = ZAP_ANIM_FRAMES;
        }
        return;                       /* don't move while zapping */
    }

    if (r->cooldown_timer) r->cooldown_timer--;

    /* lock on (detection runs even while moving) */
    if (!r->has_locked_on && !r->is_charging && r->cooldown_timer == 0 && can_zap(r)) {
        r->has_locked_on = TRUE;
        r->locked_direction = dir_towards(r, game.player.x, game.player.y);
        r->direction = r->locked_direction;
    }

    /* charge / fire only at a grid position */
    if (!r->is_moving && r->cooldown_timer == 0) {
        if (r->is_charging) {
            if (r->charge_timer) { r->charge_timer--; return; }
            robot_fire(r);
            r->is_charging = FALSE; r->has_locked_on = FALSE;
            return;
        } else if (r->has_locked_on) {
            r->is_charging = TRUE;
            r->charge_timer = ROBOT_ZAP_CHARGE_FRAMES;
            return;
        }
    }

    if (r->has_locked_on) return;     /* hold position until at grid cell */
    if (!r->is_moving) robot_move_towards(r);
}

u8 robot_on_active_section(Robot *r) {
    return (u8)(r->section_row == game.cur_row && r->section_col == game.cur_col);
}

u8 robot_zap_hits_px(Robot *r, s16 px, s16 py) {
    s16 len, lx, ly, lw, lh, pr, pb, lr, lb;
    if (!r->is_zapping) return FALSE;
    len = (s16)(r->zap_distance * TILE_SIZE);
    lx = r->pixel_x; ly = r->pixel_y; lw = TILE_SIZE; lh = TILE_SIZE;
    switch (r->zap_direction) {
        case DIR_RIGHT: lx = (s16)(r->pixel_x + TILE_SIZE); lw = len; break;
        case DIR_LEFT:  lx = (s16)(r->pixel_x - len);       lw = len; break;
        case DIR_DOWN:  ly = (s16)(r->pixel_y + TILE_SIZE); lh = len; break;
        case DIR_UP:    ly = (s16)(r->pixel_y - len);       lh = len; break;
        default: break;
    }
    pr = (s16)(px + TILE_SIZE); pb = (s16)(py + TILE_SIZE);
    lr = (s16)(lx + lw); lb = (s16)(ly + lh);
    return (u8)(!(pr <= lx || px >= lr || pb <= ly || py >= lb));
}
