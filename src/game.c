/* Deadfall SNES port - core game loop (subset of GameScene.js).
 * Implemented so far: movement, horizontal push, mining (block/gem/extra-life
 * block), animated gravity, gem collection, extra-life pickup, instant section
 * transitions, player death by falling tile, basic level advance.
 * TODO next: enemies, robot/zap, sliding transitions, HUD, alarm, scoring fx. */
#include <snes.h>
#include "game.h"
#include "config.h"
#include "levels.h"
#include "world.h"
#include "player.h"
#include "enemy.h"
#include "audio.h"
#include "robot.h"
#include "ai.h"
#include "balance.h"
#include "gravity.h"
#include "render.h"

u8 game_map_dirty;

static u8 trans_hold;   /* slide: hold one aligned frame before committing (vblank scroll lag) */

static void check_falls_crush(u8 include_player);   /* fwd; player crush is usually per-frame */
static void check_section_cleared(void);            /* fwd; flashes "SECTION CLEARED" */

/* ---- smooth falling tiles ---------------------------------------------------
 * Gravity settles the grid instantly (logic), but each tile that moved slides
 * down as a 16x16 OBJ from its origin to its landing cell. The landing cell is
 * blanked on BG1 during the slide and redrawn when the tile arrives. */
typedef struct { u8 active, type, col, to_y, dmg; s16 py, py_end; } FallAnim;
static FallAnim fall_anim[MAX_FALL_ANIMS];
static u8 fall_anim_n;
#define FALL_DY 5                /* px/frame -- faster falling to match the original feel */

/* A pushed gem/boulder slides one cell as a 16x16 OBJ, synced with the player's
 * own move (same PLAYER_MOVE_FRAMES), so it glides instead of popping. The grid
 * already holds it at the destination (player_move); its BG cell is blanked
 * during the slide and redrawn on arrival. Only one push is ever in flight (the
 * player can't move again until its move finishes). */
static struct { u8 active, gx, gy, type, dmg; s16 sx, ex; u8 timer; } push_anim;

static u8 crush_pending;         /* a destroy is mid-shatter -> run gravity when it ends */
static u8 crush_completed;        /* a mine just destroyed a tile -> lock mining until the
                                   * mine button is released+re-pressed (original crushCompleted),
                                   * so holding mine can't auto-chew through a wall */
static u8 elife_idx, elife_timer; /* extra-life pickup animation cursor */
static u8 glitter_timer, glitter_phase;   /* gem glitter cycle */
static u8 push_grav_timer;                /* deferred gravity after a push (200ms grace) */

/* Land any still-falling tiles immediately (draw on BG1) and clear their OBJ. */
static void finalize_falls(void) {
    u8 i;
    for (i = 0; i < fall_anim_n; i++)
        if (fall_anim[i].active) render_set_cell(fall_anim[i].col, fall_anim[i].to_y);
    fall_anim_n = 0;
    render_falls_hide();
    game.gravity_resolving = 0;
    game_map_dirty = 1;
}

static void start_gravity(void) {
    u8 i;
    finalize_falls();            /* land leftovers from any prior fall first */
    game.fall_count = gravity_settle(game.fall_tiles, SECTION_TILES);  /* grid -> final */
    check_falls_crush(0);        /* crush enemies on settle; the player is crushed
                                  * per-frame as the tiles fall, so you can escape */
    fall_anim_n = 0;
    {
    u8 cidx = world_section_index(game.cur_row, game.cur_col);  /* gravity carried damage to (to) */
    for (i = 0; i < game.fall_count; i++) {
        FallTile *f = &game.fall_tiles[i];
        render_set_cell(f->from_x, f->from_y);          /* origin is now empty */
        if (fall_anim_n < MAX_FALL_ANIMS) {
            FallAnim *a = &fall_anim[fall_anim_n++];
            a->active = 1; a->type = f->type; a->col = f->to_x; a->to_y = f->to_y;
            a->dmg = (f->type == TILE_GEM) ? game.damage[cidx][f->to_y][f->to_x] : 0;
            a->py = (s16)(f->from_y * TILE_SIZE);
            a->py_end = (s16)(f->to_y * TILE_SIZE);
            render_clear_cell(f->to_x, f->to_y);        /* hide landed tile until it slides in */
        } else {
            render_set_cell(f->to_x, f->to_y);          /* overflow: no anim, draw final now */
        }
    }
    }
    if (game.fall_count) audio_sfx(SFX_FALL);
    game.gravity_resolving = (u8)(fall_anim_n > 0);
    game_map_dirty = 1;
}

/* Player's current visual cell (rounded from pixel position). */
static void player_cell(u8 *cx, u8 *cy) {
    *cx = (u8)((game.player.pixel_x + (TILE_SIZE / 2)) / TILE_SIZE);
    *cy = (u8)((game.player.pixel_y + (TILE_SIZE / 2)) / TILE_SIZE);
}

/* Find the level's (single) enemy spawn point across sections. */
static u8 find_spawn(u8 *sx, u8 *sy, u8 *srow, u8 *scol) {
    u8 r, c, idx;
    for (r = 0; r < game.world_rows; r++)
        for (c = 0; c < game.world_cols; c++) {
            idx = world_section_index(r, c);
            if (game.spawn_count[idx] > 0) {
                *sx = game.spawn_points[idx][0].x; *sy = game.spawn_points[idx][0].y;
                *srow = r; *scol = c; return TRUE;
            }
        }
    return FALSE;
}

/* Falling tiles crush whoever they land on/pass through (player -> death,
 * enemy -> +500). Robots are immune (handled elsewhere). */
static void check_falls_crush(u8 include_player) {
    u8 i, j, cx, cy;

    if (include_player && game.player.alive && !game.death_pending) {
        u8 gx = game.player.x, gy = game.player.y;   /* grid/target cell */
        player_cell(&cx, &cy);                        /* visual (pixel) cell */
        for (i = 0; i < game.fall_count; i++) {
            FallTile *f = &game.fall_tiles[i];
            if (f->type == TILE_EXTRA_LIFE) continue;
            /* crush if a tile lands on / passes through the player's grid cell OR
             * its visual cell (the visual lags the grid while the player is
             * mid-move into a tile's landing spot, so check both). */
            if ((f->to_x == cx && f->to_y == cy) ||
                (f->to_x == gx && f->to_y == gy) ||
                (f->to_x == cx && f->from_y < cy && f->to_y >= cy) ||
                (f->to_x == gx && f->from_y < gy && f->to_y >= gy)) {
                player_die();
                break;
            }
        }
    }

    for (j = 0; j < game.enemy_count; j++) {
        Enemy *e = &game.enemies[j];
        u8 evx, evy;
        if (!e->alive) continue;
        if (e->section_row != game.cur_row || e->section_col != game.cur_col) continue;
        evx = (u8)((e->pixel_x + (TILE_SIZE / 2)) / TILE_SIZE);
        evy = (u8)((e->pixel_y + (TILE_SIZE / 2)) / TILE_SIZE);
        for (i = 0; i < game.fall_count; i++) {
            FallTile *f = &game.fall_tiles[i];
            if (f->type == TILE_EXTRA_LIFE) continue;
            if ((f->to_x == e->x && f->to_y == e->y) ||
                (f->to_x == evx && f->to_y == evy) ||
                (f->to_x == e->x && f->from_y < e->y && f->to_y >= e->y) ||
                (e->is_moving && f->to_x == (u8)(s8)e->target_x && f->to_y == (u8)(s8)e->target_y)) {
                enemy_die(e);
                game.score += SCORE_CRUSH_KILL;
                game.enemies_killed++;
                audio_sfx(SFX_ENEMYKILL);
                break;
            }
        }
    }
}

/* Par time in ms for the current level (port of calculateParTime): scan all
 * sections for gems/blocks, estimate travel + mining time, scale by section
 * count and enemy/robot count. Integer math; multipliers are x100 fixed-point. */
static u32 compute_par_ms(void) {
    u16 gems = 0, blocks = 0, nsec, smul, emul, est_blocks, travel;
    u8 r, c, gx, gy, idx;
    u32 base, par;
    for (r = 0; r < game.world_rows; r++)
        for (c = 0; c < game.world_cols; c++) {
            idx = world_section_index(r, c);
            for (gy = 0; gy < GRID_ROWS; gy++)
                for (gx = 0; gx < GRID_COLS; gx++) {
                    u8 t = game.sections[idx][gy][gx];
                    if (t == TILE_GEM) gems++;
                    else if (t == TILE_BLOCK) blocks++;
                }
        }
    nsec = (u16)(game.world_rows * game.world_cols);
    est_blocks = (u16)((u32)blocks * 7 / 100);          /* floor(blocks * 0.07) */
    travel = (u16)(gems * 4 + (nsec ? (nsec - 1) : 0) * 10);
    base = (u32)gems * 3 * 350 + (u32)est_blocks * 3 * 350 + (u32)travel * 220 + 2000;
    switch (nsec) {                                      /* section multiplier x100 */
        case 4:  smul = 100; break;
        case 6:  smul = 130; break;
        case 9:  smul = 175; break;
        case 12: smul = 210; break;
        case 16: smul = 270; break;
        default: smul = (u16)(100 + (nsec - 4) * 14); break;
    }
    emul = (u16)(100 + game.cfg.enemies * 3 + game.cfg.robots * 2);  /* enemy mult x100 */
    par = base * smul / 100;
    par = par * emul / 100;
    return par;
}

/* Time bonus (port of calculateTimeBonus): 5000 at/under par, -50/sec over,
 * floored at 0, rounded to nearest 10. */
static u16 compute_time_bonus(void) {
    u16 ef = (u16)(game.frame - game.level_start_frame);   /* elapsed frames */
    u32 elapsed_ms = (u32)ef * 1000 / 60;
    u16 bonus;
    if (elapsed_ms <= game.level_par_ms) {
        bonus = 5000;
    } else {
        u32 ded = (elapsed_ms - game.level_par_ms) / 1000 * 50;
        bonus = (ded >= 5000) ? 0 : (u16)(5000 - ded);
    }
    return (u16)((bonus + 5) / 10 * 10);
}

static void load_level(u8 n) {
    u8 sx, sy, sr, sc, i;
    u16 md;

    setScreenOff();          /* blank the title/banner for the build -- the wipe is reserved
                              * for entering gameplay (render_wipe_in below), not for the
                              * non-gameplay screen we're leaving */
    render_load_font(0);     /* gameplay uses the OPAQUE font -> HUD reads as a solid bar */
    world_load_level(n);
    player_set_spawn(game.portal.x, game.portal.y, game.portal_row, game.portal_col);

    /* spawn enemies (all share the single spawn point; staggered) */
    game.enemy_count = game.cfg.enemies;
    if (game.enemy_count > MAX_ENEMIES) game.enemy_count = MAX_ENEMIES;
    if (find_spawn(&sx, &sy, &sr, &sc)) {
        md = get_enemy_move_delay(n);
        for (i = 0; i < game.enemy_count; i++)
            enemy_reset(&game.enemies[i], i, sx, sy, sr, sc, md,
                        (u16)(ENEMY_INITIAL_SPAWN_FRAMES + i * ENEMY_SPAWN_STAGGER_FRAMES));
    } else {
        game.enemy_count = 0;
    }

    /* spawn the robot from LevelConfig (NOT from tile-6 markers) */
    game.robot_count = game.cfg.robots;
    if (game.robot_count > MAX_ROBOTS) game.robot_count = MAX_ROBOTS;
    if (game.robot_count > 0)
        robot_reset(&game.robots[0], game.cfg.robot_x, game.cfg.robot_y,
                    game.cfg.robot_row, game.cfg.robot_col, get_robot_move_delay(n));

    game.fall_count = gravity_settle(game.fall_tiles, SECTION_TILES);  /* no crush at spawn */
    game.portal_active = 0;
    game.alarm_active = 0;
    game.gravity_resolving = 0;
    game.mine_cooldown = 0;
    game.push_pending = 0;
    game.crush_safety_timer = 0;
    game.death_pending = 0;
    game.transitioning = 0;      /* clear stale section-transition state -- WRAM isn't zeroed
                                  * at boot, so a garbage value here ran the brightness-fade on
                                  * the first level (the pre-existing "game start" flash) */
    game.trans_phase = 0;
    game.trans_timer = 0;
    fall_anim_n = 0;              /* drop any in-flight falling-tile anims */
    push_anim.active = 0; render_push_hide();   /* drop any in-flight push slide */
    for (i = 0; i < 16; i++) game.cell_anims[i].active = 0;  /* drop shatter anims */
    crush_pending = 0;
    crush_completed = 0;
    game.gravity_resolving = 0;
    game.is_level_complete = 0;
    game.lc_timer = 0;
    game.portal_frame = 0;
    game.portal_timer = 0;
    game.spawn_glow_index = 0;
    game.spawn_glow_timer = 0;
    game.flash_timer = 0;
    game.sections_cleared = 0;
    game.player.idle_col = 0xFF;
    elife_idx = 0;
    elife_timer = 0;
    glitter_timer = 0;
    glitter_phase = 0;
    push_grav_timer = 0;
    game.blocks_crushed = 0;
    game.enemies_killed = 0;
    game.level_start_frame = game.frame;
    game.level_par_ms = compute_par_ms();   /* needs sections loaded (done above) */
    render_falls_hide();
    render_minimap_reset();       /* rebuild the minimap for the new level's world */
    render_clear_screen();        /* wipe any leftover scene text (e.g. title) from BG3 */
    render_bg2_reset();           /* start section = BG2 phase 0 */
    render_build_map();
    audio_music_level(n);        /* this level's chiptune theme -- the blocking SPC upload
                                  * runs here while the wipe holds the screen blanked */
    render_load_background();     /* load the current section's backdrop (forced blank) */
    setScreenOff();               /* render_load_background re-enabled the screen; keep it
                                   * blanked until the wipe reveals (no sharp pre-flash) */
    game_map_dirty = 1;
    render_wipe_in();            /* SMAS diamond wipe: the new level ripples in from black */
}

void game_init(void) {
    game.score = 0;
    game.lives = START_LIVES;
    game.continues = MAX_CONTINUES;
    game.is_game_over = 0;
    game.is_victory = 0;
    load_level(1);          /* builds the level map; render_init() is done once in main() */
}

/* Resume the current level with fresh lives (used by the continue option). */
void game_continue(void) {
    if (game.continues == 0) return;
    game.continues--;
    game.lives = START_LIVES;
    game.score = 0;             /* a continue wipes the score, like the original */
    game.is_game_over = 0;
    load_level(game.current_level);
    game_map_dirty = 1;
}

/* ---- crush (destruction) animation -----------------------------------------
 * When mining destroys a block/gem, the original plays a 2-frame shatter (sheet
 * frames 3,4 -> metatiles 16/17 block, 18/19 gem) for ~200ms each, and only
 * THEN does gravity run. We reuse game.cell_anims[]; .kind holds the base
 * shatter metatile, so the rendered frame is (kind + stage). The deferred
 * gravity gives the player a head start to escape whatever was being held up
 * (the "time to get away" the original grants). */
static void crush_anim_add(u8 gx, u8 gy, u8 base_mt) {
    u8 i;
    for (i = 0; i < 16; i++) {
        CellAnim *a = &game.cell_anims[i];
        if (a->active) continue;
        a->active = 1; a->x = gx; a->y = gy;
        a->row = game.cur_row; a->col = game.cur_col;
        a->kind = base_mt;                       /* base shatter metatile (16 or 18) */
        a->stage = 0;
        a->timer = CRUSH_ANIM_FRAMES;
        render_crush_cell(gx, gy, base_mt);      /* show shatter frame 0 immediately */
        game_map_dirty = 1;
        return;
    }
    render_set_cell(gx, gy);                      /* no free slot -> just clear the cell */
}

/* Advance every active shatter; when the last one ends, release deferred gravity. */
static void crush_anims_update(void) {
    u8 i, any = 0, ended = 0;
    for (i = 0; i < 16; i++) {
        CellAnim *a = &game.cell_anims[i];
        if (!a->active) continue;
        if (a->timer) a->timer--;
        if (a->timer == 0) {
            a->stage++;
            if (a->stage >= CRUSH_ANIM_STAGES) {     /* shatter done -> final cell */
                a->active = 0;
                render_set_cell(a->x, a->y);          /* empty, or a revealed 1-up */
                game_map_dirty = 1;
                ended = 1;
            } else {
                a->timer = CRUSH_ANIM_FRAMES;
                render_crush_cell(a->x, a->y, (u8)(a->kind + a->stage));  /* frame 4 */
                game_map_dirty = 1;
            }
        }
        if (a->active) any = 1;
    }
    if (ended && !any && crush_pending) {            /* all shatters finished */
        crush_pending = 0;
        start_gravity();
    }
}

/* A gem that just FELL takes one hit of damage on landing (original
 * applyFallDamageToGems): 3 falls destroy it. A fall-destroyed gem counts as
 * collected (+score, may open the portal) and shatters; the shatter then
 * re-runs gravity (cascade), exactly like a mined gem. */
static void gem_fall_damage(u8 gx, u8 gy) {
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    if (game.sections[idx][gy][gx] != TILE_GEM) { render_set_cell(gx, gy); return; }
    if (world_damage_tile(gx, gy)) {                 /* reached 3 hits -> destroyed */
        world_set_tile(gx, gy, TILE_EMPTY);
        world_clear_damage(gx, gy);
        game.gems_collected++;
        game.score += SCORE_GEM;
        if (!game.portal_active && game.gems_collected >= game.total_gems) {
            game.portal_active = 1;
            game.alarm_active = 1;
            audio_music_frantic();          /* swap to the portal/alarm theme */
            audio_sfx(SFX_PORTAL);
        } else {
            audio_sfx(SFX_COLLECT);
        }
        crush_anim_add(gx, gy, MT_GEM_CRUSH);        /* shatter -> cascades gravity */
        crush_pending = 1;
        check_section_cleared();
    } else {
        render_set_cell(gx, gy);                     /* show the new damage frame */
    }
    game_map_dirty = 1;
}

/* Marker animations on the current section: the enemy spawn point glows on a
 * [0,1,2,1] cycle and a revealed extra-life pickup plays [0,1,2,3,2,1], both at
 * 200ms/frame (port of the spawn/extra-life sprite animations). */
static const u8 spawn_frames[4] = { MT_SPAWN0, 13, MT_SPAWN2, 13 };
static const u8 elife_frames[6] = { MT_EXTRALIFE, MT_ELIFE1, 22, 23, 22, 21 };

static void update_marker_anims(void) {
    u8 cidx = world_section_index(game.cur_row, game.cur_col);
    u8 k, gx, gy;

    if (game.spawn_glow_timer) game.spawn_glow_timer--;
    if (game.spawn_glow_timer == 0) {
        game.spawn_glow_index = (u8)((game.spawn_glow_index + 1) & 3);
        game.spawn_glow_timer = SPAWN_ANIM_FRAMES;
        for (k = 0; k < game.spawn_count[cidx]; k++)
            render_crush_cell(game.spawn_points[cidx][k].x, game.spawn_points[cidx][k].y,
                              spawn_frames[game.spawn_glow_index]);
        game_map_dirty = 1;
    }

    if (elife_timer) elife_timer--;
    if (elife_timer == 0) {
        u8 *g = &game.sections[cidx][0][0];
        u16 off = 0;
        u8 found = 0, mt;
        elife_idx = (u8)(elife_idx + 1); if (elife_idx >= 6) elife_idx = 0;
        elife_timer = SPAWN_ANIM_FRAMES;
        mt = elife_frames[elife_idx];
        for (gy = 0; gy < GRID_ROWS; gy++)
            for (gx = 0; gx < GRID_COLS; gx++, off++)
                if (g[off] == TILE_EXTRA_LIFE) { render_crush_cell(gx, gy, mt); found = 1; }
        if (found) game_map_dirty = 1;
    }
}

/* Enemy edge-warnings: when the player is within 3 tiles of an edge AND a live
 * enemy/robot sits in the adjacent section within wrapped distance 5, flash a
 * strip on that edge (port of drawEnemyWarnings; the original's red is shown
 * white here since red is palette-blocked). */
static void update_edge_warnings(void) {
    Player *p = &game.player;
    static const s8 edr[4] = { -1, 1, 0, 0 };   /* up, down, left, right */
    static const s8 edc[4] = {  0, 0, -1, 1 };
    u8 nearedge[4], d, mask = 0;
    s16 pgx, pgy, totc, totr;
    /* The mask is forced to 0 on the blink-off phase, and stays 0 unless the player
     * is within 3 tiles of a section edge -- both are the common case. Bail out
     * BEFORE the per-enemy scan + its *13 multiplies/divides in those cases. */
    if (!((game.frame >> 3) & 1)) { render_edge_warn(0); return; }   /* blink off */
    nearedge[0] = (u8)(p->y <= 3);
    nearedge[1] = (u8)(p->y >= GRID_ROWS - 1 - 3);
    nearedge[2] = (u8)(p->x <= 3);
    nearedge[3] = (u8)(p->x >= GRID_COLS - 1 - 3);
    if (!(nearedge[0] | nearedge[1] | nearedge[2] | nearedge[3])) {
        render_edge_warn(0); return;                                /* not near any edge */
    }
    pgx = (s16)(p->section_col * GRID_COLS + p->x);
    pgy = (s16)(p->section_row * GRID_ROWS + p->y);
    totc = (s16)(game.world_cols * GRID_COLS);
    totr = (s16)(game.world_rows * GRID_ROWS);
    for (d = 0; d < 4; d++) {
        u8 ar, ac, i, hit = 0;
        if (!nearedge[d]) continue;
        ar = (u8)((p->section_row + edr[d] + game.world_rows) % game.world_rows);
        ac = (u8)((p->section_col + edc[d] + game.world_cols) % game.world_cols);
        for (i = 0; i < game.enemy_count && !hit; i++) {
            Enemy *e = &game.enemies[i];
            s16 xd, yd;
            if (!e->alive || e->section_row != ar || e->section_col != ac) continue;
            xd = ai_wrapped_diff(pgx, (s16)(e->section_col * GRID_COLS + e->x), totc);
            yd = ai_wrapped_diff(pgy, (s16)(e->section_row * GRID_ROWS + e->y), totr);
            if (xd < 0) xd = (s16)-xd;
            if (yd < 0) yd = (s16)-yd;
            if (xd + yd <= 5) hit = 1;
        }
        if (!hit && game.robot_count > 0 && game.robots[0].alive &&
            game.robots[0].section_row == ar && game.robots[0].section_col == ac) {
            Robot *rb = &game.robots[0];
            s16 xd = ai_wrapped_diff(pgx, (s16)(rb->section_col * GRID_COLS + rb->x), totc);
            s16 yd = ai_wrapped_diff(pgy, (s16)(rb->section_row * GRID_ROWS + rb->y), totr);
            if (xd < 0) xd = (s16)-xd;
            if (yd < 0) yd = (s16)-yd;
            if (xd + yd <= 5) hit = 1;
        }
        if (hit) mask |= (u8)(1 << d);
    }
    render_edge_warn(mask);   /* (blink-off already returned above) */
}

/* Gem glitter: every ~150ms advance a phase; each undamaged gem on the current
 * section sparkles (glitter frames 24-27) for 4 ticks out of a 32-tick cycle,
 * staggered by a position hash so they twinkle independently (port of the gem
 * glitter; damaged gems never glitter). Skipped while gravity resolves so it
 * doesn't fight falling tiles. */
#define GLITTER_TICK   9     /* ~150ms per glitter frame */
#define GLITTER_CYCLE  32    /* total cycle in ticks (power of 2 -> mask, no divide) */

static void update_glitter(void) {
    u8 cidx, gx, gy, drew = 0;
    u8 *grid, *dmg;
    u16 off = 0;
    if (game.gravity_resolving) return;
    if (glitter_timer) { glitter_timer--; return; }
    glitter_timer = GLITTER_TICK;
    glitter_phase = (u8)((glitter_phase + 1) & (GLITTER_CYCLE - 1));
    cidx = world_section_index(game.cur_row, game.cur_col);
    grid = &game.sections[cidx][0][0];
    dmg  = &game.damage[cidx][0][0];
    for (gy = 0; gy < GRID_ROWS; gy++)
        for (gx = 0; gx < GRID_COLS; gx++, off++) {
            u8 local;
            if (grid[off] != TILE_GEM || dmg[off]) continue;   /* only undamaged gems */
            local = (u8)((glitter_phase + gx * 5 + gy * 3) & (GLITTER_CYCLE - 1));
            /* Only redraw gems whose tile CHANGES this tick: the 4 sparkle frames
             * (local 0-3) and the one restore-to-normal tick on exit (local 4). Gems
             * in their long normal phase (local 5+) already show the right tile, so
             * skip them -- redrawing ALL ~40 every tick is what made glitter a hog. */
            if (local < 4)       { render_crush_cell(gx, gy, (u8)(MT_GEM_GLITTER + local)); drew = 1; }
            else if (local == 4) { render_set_cell(gx, gy); drew = 1; }
        }
    if (drew) game_map_dirty = 1;
}

/* After a gem is collected, flash "SECTION CLEARED" the first time the current
 * section runs out of gems (port of checkSectionCleared). */
static void check_section_cleared(void) {
    u8 cidx = world_section_index(game.cur_row, game.cur_col);
    u8 *g = &game.sections[cidx][0][0];
    u16 k;
    if (game.sections_cleared & (u16)(1 << cidx)) return;
    for (k = 0; k < SECTION_TILES; k++) if (g[k] == TILE_GEM) return;   /* gems remain */
    game.sections_cleared |= (u16)(1 << cidx);
    game.flash_timer = 90;             /* ~1.5s */
    render_flash("SECTION CLEARED");
    game_map_dirty = 1;
}

static void do_mine(s8 dx, s8 dy) {
    u8 tile, destroyed, mx, my, base_mt;
    if (game.mine_cooldown) return;
    if (!player_mine(dx, dy, &tile, &destroyed, &mx, &my)) return;

    game.mine_cooldown = MINE_REPEAT_FRAMES;
    game_map_dirty = 1;
    if (!destroyed) { audio_sfx(SFX_CRUSH); render_set_cell(mx, my); return; }  /* a mining hit */

    base_mt = MT_BLOCK_CRUSH;
    if (tile == TILE_GEM) {
        world_set_tile(mx, my, TILE_EMPTY);
        base_mt = MT_GEM_CRUSH;
        game.gems_collected++;
        game.score += SCORE_GEM;
        if (!game.portal_active && game.gems_collected >= game.total_gems) {
            game.portal_active = 1;
            game.alarm_active = 1;
            audio_music_frantic();          /* swap to the portal/alarm theme */
            audio_sfx(SFX_PORTAL);          /* exit opens */
        } else {
            audio_sfx(SFX_COLLECT);
        }
    } else if (tile == TILE_EXTRA_LIFE_BLK) {
        world_set_tile(mx, my, TILE_EXTRA_LIFE);   /* reveal the 1-up (after the shatter) */
        audio_sfx(SFX_CRUSH);
    } else {
        world_set_tile(mx, my, TILE_EMPTY);        /* plain block destroyed */
        game.blocks_crushed++;
        audio_sfx(SFX_CRUSH);
    }
    world_clear_damage(mx, my);
    /* play the shatter; gravity is deferred until it finishes (crush_anims_update) */
    crush_anim_add(mx, my, base_mt);
    crush_pending = 1;
    crush_completed = 1;     /* lock mining until the mine button is released+re-pressed */
    /* block walking into the just-cleared cell until gravity settles (crush safety) */
    game.crushed_x = mx; game.crushed_y = my;
    game.crush_safety_timer = CRUSH_SAFETY_FRAMES;
    if (tile == TILE_GEM) check_section_cleared();
}

static void cancel_push(void) { game.push_pending = 0; }

static void do_move(s8 dx, s8 dy) {
    Player *p = &game.player;
    MoveResult r;

    /* CRUSH SAFETY: don't walk into a just-mined cell until gravity has settled,
     * or a tile falling into it would crush you on arrival. (JS crushSafetyDelay) */
    if (game.crush_safety_timer &&
        (s8)(p->x + dx) == (s8)game.crushed_x && (s8)(p->y + dy) == (s8)game.crushed_y)
        return;

    /* PUSH DELAY: walking into a gem/boulder doesn't shove it instantly. Hold the
     * direction for PUSH_DELAY_FRAMES first; tapping past, changing direction, or
     * pressing mine cancels it -> you can crush/mine the tile or stand beside it
     * without nudging it. (Mirrors GameScene.js pendingPush.) */
    if (player_would_push(dx, dy)) {
        p->direction = (dx < 0) ? DIR_LEFT : DIR_RIGHT;     /* face it while holding */
        if (game.push_pending && (game.push_dx != dx || game.push_dy != dy))
            game.push_pending = 0;                          /* direction changed -> restart */
        if (!game.push_pending) {
            game.push_pending = 1; game.push_dx = dx; game.push_dy = dy;
            game.push_timer = PUSH_DELAY_FRAMES;
            return;                                         /* wait before pushing */
        }
        if (game.push_timer) { game.push_timer--; return; } /* still waiting */
        game.push_pending = 0;                              /* delay elapsed -> push now */
    } else {
        game.push_pending = 0;                              /* not a push -> drop any pending */
    }

    player_move(dx, dy, &r);

    if (r.section_blocked) {
        audio_sfx(SFX_NOENTRY);
        game.flash_timer = 30;          /* ~0.5s "NO ENTRY" flash */
        render_flash("NO ENTRY");
        game_map_dirty = 1;
    }

    if (r.transition) {
        game.trans_dir = r.dir;
        game.trans_entry_x = r.entry_x;
        game.trans_entry_y = r.entry_y;
        game.transitioning = 1;
        {
            /* SLIDE (all 4 directions): switch+settle now, stage the new section
             * in the off-screen half of the enlarged BG1 map, then scroll the
             * camera across it (see game_update phase 2). L/R = 64-wide map,
             * U/D = 64-tall map; BG3 HUD stays fixed. */
            SectionRef adj;
            finalize_falls();          /* land any in-flight tiles before leaving the section */
            world_adjacent(r.dir, &adj);
            player_transition_to(adj.row, adj.col, r.entry_x, r.entry_y);
            game.fall_count = gravity_settle(game.fall_tiles, SECTION_TILES);
            check_falls_crush(1);     /* section already settled on entry -> instant */
            render_slide_begin(r.dir, game.cur_row, game.cur_col);
            game.trans_phase = 2;                  /* 2 = slide */
            game.trans_timer = TRANSITION_FRAMES;
            trans_hold = 0;
            game_map_dirty = 1;                    /* vblank flush stages the section (no blank) */
        }
    } else if (r.pushed) {
        render_set_cell(p->x, p->y);     /* tile vacated this cell */
        if (!r.cross_push && (p->x + dx) >= 0 && (p->x + dx) < GRID_COLS) {
            u8 bx = (u8)(p->x + dx);     /* the block's new cell (grid already holds it) */
            push_anim.active = 1;
            push_anim.gx = bx; push_anim.gy = p->y; push_anim.type = r.pushed_tile;
            push_anim.dmg = (r.pushed_tile == TILE_GEM) ? world_get_damage(bx, p->y) : 0;
            push_anim.sx = (s16)(p->x * TILE_SIZE);   /* slide from the block's OLD cell... */
            push_anim.ex = (s16)(bx * TILE_SIZE);     /* ...to its new cell, over the move  */
            push_anim.timer = PLAYER_MOVE_FRAMES;
            render_clear_cell(bx, p->y);              /* blank BG; the OBJ shows it sliding */
        }
        game_map_dirty = 1;
        push_grav_timer = 12;            /* gravity 200ms after a push (grace), like JS */
    } else if (r.collected_extra_life) {
        world_set_tile(p->x, p->y, TILE_EMPTY);
        render_set_cell(p->x, p->y);
        if (game.lives < 9) game.lives++;
        audio_sfx(SFX_EXTRALIFE);
        game_map_dirty = 1;
    }
}

/* Advance the pushed-block slide one frame: interpolate sx->ex exactly like
 * player_tick interpolates the player, draw the OBJ, and on the last frame stamp
 * the block onto BG1 and hide the OBJ. Call once per frame from game_update. */
static void update_push(void) {
    if (!push_anim.active) return;
    if (push_anim.timer) push_anim.timer--;
    if (push_anim.timer == 0) {
        push_anim.active = 0;
        render_set_cell(push_anim.gx, push_anim.gy);   /* block lands on BG1 */
        render_push_hide();
        game_map_dirty = 1;
    } else {
        s16 elapsed = (s16)(PLAYER_MOVE_FRAMES - push_anim.timer);
        s16 cx = (s16)(push_anim.sx +
                       (s16)((push_anim.ex - push_anim.sx) * elapsed) / PLAYER_MOVE_FRAMES);
        render_push(push_anim.type,
                    (u16)(PLAYFIELD_OFFSET_X + cx),
                    (u16)(PLAYFIELD_OFFSET_Y + push_anim.gy * TILE_SIZE),
                    push_anim.dmg);
    }
}

void game_update(void) {
    Player *p = &game.player;
    u16 cur;
    s8 dx = 0, dy = 0;
    u8 i;

    cur = padsCurrent(0);   /* pad_keys[] auto-updated each vblank by the lib */

    game.frame++;

    /* section transition: fade screen out, switch section at black, fade in.
     * Gameplay (input, enemies, gravity) is frozen for the ~24 frames. */
    if (game.transitioning) {
        if (game.trans_phase == 2) {                       /* SLIDE (H or V) */
            u16 cam, lin;
            if (game.trans_timer) game.trans_timer--;
            lin = (u16)((u32)256 * (TRANSITION_FRAMES - game.trans_timer) / TRANSITION_FRAMES);
            /* easeInOutQuad on the slide camera (the original eases; linear felt
             * abrupt at the ends). cam = 2t^2 for t<0.5, else 1-2(1-t)^2. */
            if (lin < 128) cam = (u16)((u32)2 * lin * lin / 256);
            else { u16 q = (u16)(256 - lin); cam = (u16)(256 - (u32)2 * q * q / 256); }
            /* LEFT/UP enter from the near side, so the camera runs 256 -> 0. */
            if (game.trans_dir == DIR_LEFT || game.trans_dir == DIR_UP) cam = (u16)(256 - cam);
            render_slide_scroll(cam);
            render_hide_sprites();                         /* clear all; re-show the entering ones */
            render_slide_player(cam);                      /* player entering the new section */
            render_slide_entities(cam);                    /* + that section's enemies/robot */
            /* Scroll is applied a frame late (vblank), so hold ONE frame at the
             * fully-aligned cam=256 before committing -- otherwise the commit
             * snaps the last ~8px (slide looked like it ended early + jumped). */
            if (game.trans_timer == 0) {
                if (trans_hold) {                          /* aligned frame shown -> commit now */
                    render_build_map();                    /* new (current) section -> bg1map */
                    render_slide_end();                    /* flag: flush commits screen0 + single-screen */
                    game.transitioning = 0;
                    trans_hold = 0;
                    game_map_dirty = 1;                    /* triggers the vblank commit flush */
                } else {
                    trans_hold = 1;                        /* display cam=256 for one more frame */
                }
            }
            return;
        }
        if (game.trans_timer) game.trans_timer--;
        if (game.trans_phase == 0) {                       /* fading out */
            setBrightness((u8)(15 * game.trans_timer / TRANS_FADE_FRAMES));
            if (game.trans_timer == 0) {                   /* at black: switch + new bg */
                SectionRef adj;
                world_adjacent(game.trans_dir, &adj);
                setScreenOff();                            /* forced blank for the bg DMA */
                player_transition_to(adj.row, adj.col, game.trans_entry_x, game.trans_entry_y);
                game.fall_count = gravity_settle(game.fall_tiles, SECTION_TILES);
                check_falls_crush(1);     /* section already settled on entry -> instant */
                render_build_map();
                render_set_background();                    /* new section's backdrop */
                render_flush_map();
                setBrightness(0);                           /* screen on at black; fade-in continues */
                game_map_dirty = 1;
                game.trans_phase = 1;
                game.trans_timer = TRANS_FADE_FRAMES;
            }
        } else {                                           /* fading in */
            setBrightness((u8)(15 * (TRANS_FADE_FRAMES - game.trans_timer) / TRANS_FADE_FRAMES));
            if (game.trans_timer == 0) { game.transitioning = 0; setBrightness(15); }
        }
        render_player(); render_enemies(); render_robot(); render_lightning();
        return;
    }

    /* level-complete banner: freeze gameplay for ~5s, then advance / win. */
    if (game.is_level_complete) {
        if (game.lc_timer) game.lc_timer--;
        if (game.lc_timer == 0) {
            game.is_level_complete = 0;
            if (game.current_level >= MAX_LEVELS) game.is_victory = 1;
            else load_level((u8)(game.current_level + 1));
        }
        return;                                  /* everything paused behind the banner */
    }

    /* tile-destruction shatter: advance any crush frames; releases the deferred
     * gravity when the last shatter finishes (the player's grace window). */
    crush_anims_update();

    /* smooth gravity: slide each in-flight tile down; draw it on BG1 when it lands */
    if (game.gravity_resolving) {
        u8 any = 0;
        for (i = 0; i < fall_anim_n; i++) {
            FallAnim *a = &fall_anim[i];
            if (!a->active) continue;
            a->py = (s16)(a->py + FALL_DY);
            if (a->py >= a->py_end) {                  /* landed */
                a->py = a->py_end;
                a->active = 0;
                if (a->type == TILE_GEM)               /* fallen gem takes 1 hit */
                    gem_fall_damage(a->col, a->to_y);
                else
                    render_set_cell(a->col, a->to_y);  /* draw the settled tile on BG1 */
                game_map_dirty = 1;
            } else {
                any = 1;
            }
        }
        /* A falling tile only kills the player when it actually REACHES them
         * (pixel overlap), so you get the fall's worth of time to run out from
         * under it -- matching the original. */
        if (game.player.alive && !game.death_pending) {
            s16 ppx = game.player.pixel_x, ppy = game.player.pixel_y;
            for (i = 0; i < fall_anim_n; i++) {
                FallAnim *a = &fall_anim[i];
                s16 fx, fy;
                if (!a->active || a->type == TILE_EXTRA_LIFE) continue;
                fx = (s16)((s16)(a->col * TILE_SIZE) - ppx); if (fx < 0) fx = (s16)(-fx);
                fy = (s16)(a->py - ppy);                     if (fy < 0) fy = (s16)(-fy);
                if (fx < TILE_SIZE && fy < TILE_SIZE) { player_die(); break; }
            }
        }
        /* Enemies are crushed the same per-frame way (pixel overlap as the tile
         * falls), NOT only on the up-front settle snapshot: enemies are almost
         * always moving, so a boulder/gem dropped onto a walking enemy must kill
         * it when the tile actually arrives, not just if it stood in the column
         * at mine-time. +500 indirect kill, like the original. (The dead enemy's
         * `alive` flag stops the up-front check from also counting it.) */
        {
            u8 j;
            for (j = 0; j < game.enemy_count; j++) {
                Enemy *e = &game.enemies[j];
                s16 epx, epy;
                if (!e->alive) continue;
                if (e->section_row != game.cur_row || e->section_col != game.cur_col) continue;
                epx = e->pixel_x; epy = e->pixel_y;
                for (i = 0; i < fall_anim_n; i++) {
                    FallAnim *a = &fall_anim[i];
                    s16 fx, fy;
                    if (!a->active || a->type == TILE_EXTRA_LIFE) continue;
                    fx = (s16)((s16)(a->col * TILE_SIZE) - epx); if (fx < 0) fx = (s16)(-fx);
                    fy = (s16)(a->py - epy);                     if (fy < 0) fy = (s16)(-fy);
                    if (fx < TILE_SIZE && fy < TILE_SIZE) {
                        enemy_die(e);
                        game.score += SCORE_CRUSH_KILL;
                        game.enemies_killed++;
                        audio_sfx(SFX_ENEMYKILL);
                        break;
                    }
                }
            }
        }
        if (!any) { render_falls_hide(); game.gravity_resolving = 0; }
    }

    if (game.mine_cooldown) game.mine_cooldown--;
    if (game.crush_safety_timer) game.crush_safety_timer--;
    if (game.flash_timer && --game.flash_timer == 0) {   /* flash banner expired */
        render_flash_clear();
        game_map_dirty = 1;
    }
    if (push_grav_timer && --push_grav_timer == 0) start_gravity();  /* deferred push gravity */

    /* direction (single axis; horizontal wins ties, matching simple input) */
    if (cur & PAD_LEFT)       dx = -1;
    else if (cur & PAD_RIGHT) dx = 1;
    else if (cur & PAD_UP)    dy = -1;
    else if (cur & PAD_DOWN)  dy = 1;

    if (!(cur & (PAD_Y | PAD_B | PAD_A | PAD_X)))
        crush_completed = 0;               /* mine button released -> re-arm mining */

    if (p->alive && !p->is_moving && !game.death_pending) {
        /* mine: hold any face button (A/B/X/Y) + a direction.
         * (Final scheme is Y to mine; accepting all four eases testing.) */
        if (cur & (PAD_Y | PAD_B | PAD_A | PAD_X)) {
            cancel_push();                 /* mining cancels a pending push */
            if ((dx || dy) && !crush_completed) do_mine(dx, dy);   /* one tile per press */
        } else if (dx || dy) {
            do_move(dx, dy);               /* handles the push-delay internally */
        } else {
            cancel_push();                 /* direction released -> cancel */
        }
    }

    /* idle look-around: standing still cycles columns [0,1,2,1] with the neutral
     * pose held ~2s and the glances 200ms each (port of the Player idle anim). */
    {
        static u8 idle_phase, idle_timer;
        static const u8 idle_cols[4] = { 0, 1, 2, 1 };
        u8 standing = (u8)(p->alive && !p->is_moving && !game.death_pending &&
                           !(dx || dy) && !(cur & (PAD_Y | PAD_B | PAD_A | PAD_X)));
        if (standing) {
            if (idle_timer) idle_timer--;
            if (idle_timer == 0) {
                idle_phase = (u8)((idle_phase + 1) & 3);
                idle_timer = (u8)(idle_phase == 0 ? 120 : 12);   /* 2000ms / 200ms */
            }
            p->idle_col = idle_cols[idle_phase];
        } else {
            idle_phase = 0; idle_timer = 120; p->idle_col = 0xFF;
        }
    }

    /* a transition just started this frame -> let the handler take over next
     * frame (don't render the new section's entities over the old playfield). */
    if (game.transitioning) return;

    player_tick();

    /* Shared BFS distance field from the player, recomputed ONLY when the
     * player's cell/section changes (a full flood-fill is ~1 frame of CPU, so
     * doing it every frame is far too slow; it's stable while the player is
     * between cells). */
    {
        static u8 lfx = 255, lfy = 255, lfr = 255, lfc = 255;
        if (p->alive && (p->x != lfx || p->y != lfy ||
                         p->section_row != lfr || p->section_col != lfc)) {
            u8 need = 0, j;
            for (j = 0; j < game.enemy_count; j++)
                if (game.enemies[j].alive &&
                    game.enemies[j].section_row == p->section_row &&
                    game.enemies[j].section_col == p->section_col) { need = 1; break; }
            if (!need && game.robot_count > 0 && game.robots[0].alive &&
                game.robots[0].section_row == p->section_row &&
                game.robots[0].section_col == p->section_col) need = 1;
            if (need)
                ai_distfield_begin(p->section_row, p->section_col, p->x, p->y);
            lfx = p->x; lfy = p->y; lfr = p->section_row; lfc = p->section_col;
        }
    }
    ai_distfield_work(32);   /* spread the flood: bounded work per frame */

    /* enemies: chase + move. Frozen while the player's death animation plays so
     * nothing walks over the dying player. */
    if (!game.death_pending)
        for (i = 0; i < game.enemy_count; i++)
            enemy_update(&game.enemies[i], game.enemies, game.enemy_count);

    /* player touches a living enemy on the same section -> death. PIXEL overlap
     * (AABB), not cell match, so death fires the instant the 16x16 sprites touch
     * -- player_die() freezes the player and the enemies freeze (above), so they
     * never visibly overlap/pass through. */
    if (p->alive && !game.death_pending) {
        for (i = 0; i < game.enemy_count; i++) {
            Enemy *e = &game.enemies[i];
            s16 ddx, ddy;
            if (!e->alive) continue;
            if (e->section_row != p->section_row || e->section_col != p->section_col) continue;
            ddx = (s16)(p->pixel_x - e->pixel_x); if (ddx < 0) ddx = (s16)(-ddx);
            ddy = (s16)(p->pixel_y - e->pixel_y); if (ddy < 0) ddy = (s16)(-ddy);
            if (ddx < TILE_SIZE && ddy < TILE_SIZE) {
                player_die();
                break;
            }
        }
    }

    /* robot: chase, zap, tile destruction, contact, zap hits */
    if (game.robot_count > 0) {
        Robot *rb = &game.robots[0];
        robot_update(rb);

        if (rb->just_fired) {           /* destroyed tiles in the beam -> redraw + settle */
            s8 zdx = (rb->zap_direction == DIR_LEFT) ? -1 : (rb->zap_direction == DIR_RIGHT) ? 1 : 0;
            s8 zdy = (rb->zap_direction == DIR_UP)   ? -1 : (rb->zap_direction == DIR_DOWN)  ? 1 : 0;
            u8 kk;
            for (kk = 1; kk <= rb->zap_distance; kk++) {
                s8 cx = (s8)(rb->x + zdx * kk), cy = (s8)(rb->y + zdy * kk);
                if (cx >= 0 && cx < GRID_COLS && cy >= 0 && cy < GRID_ROWS)
                    render_set_cell((u8)cx, (u8)cy);
            }
            /* zapped gems count as collected (toward the portal), like the original,
             * so a gem Clanky destroys can never soft-lock the gem total. */
            if (rb->zap_gems) {
                game.gems_collected = (u16)(game.gems_collected + rb->zap_gems);
                game.score += (u32)SCORE_GEM * rb->zap_gems;
                if (!game.portal_active && game.gems_collected >= game.total_gems) {
                    game.portal_active = 1;
                    game.alarm_active = 1;
                    audio_music_frantic();      /* swap to the portal/alarm theme */
                    audio_sfx(SFX_PORTAL);
                }
                check_section_cleared();
            }
            game_map_dirty = 1;
            start_gravity();
        }

        if (rb->is_zapping && rb->section_row == game.cur_row && rb->section_col == game.cur_col) {
            if (p->alive && !game.death_pending &&
                p->section_row == rb->section_row && p->section_col == rb->section_col &&
                robot_zap_hits_px(rb, p->pixel_x, p->pixel_y)) {
                player_die();
            }
            for (i = 0; i < game.enemy_count; i++) {        /* zap kills enemies (+1000) */
                Enemy *e = &game.enemies[i];
                if (!e->alive) continue;
                if (e->section_row != rb->section_row || e->section_col != rb->section_col) continue;
                if (robot_zap_hits_px(rb, e->pixel_x, e->pixel_y)) {
                    enemy_die(e);
                    game.score += SCORE_ZAP_KILL;
                    game.enemies_killed++;
                    audio_sfx(SFX_ENEMYKILL);
                }
            }
        }

        if (p->alive && !game.death_pending && rb->alive &&    /* robot contact = death */
            rb->section_row == p->section_row && rb->section_col == p->section_col) {
            u8 rvx = (u8)((rb->pixel_x + (TILE_SIZE / 2)) / TILE_SIZE);
            u8 rvy = (u8)((rb->pixel_y + (TILE_SIZE / 2)) / TILE_SIZE);
            u8 pcx, pcy;
            player_cell(&pcx, &pcy);
            if ((rb->x == p->x && rb->y == p->y) || (rvx == pcx && rvy == pcy))
                player_die();
        }
    }

    /* death -> short pause -> respawn / lose a life */
    if (!p->alive && !game.death_pending) {
        game.death_pending = 1;
        game.death_timer = DEATH_SEQ_FRAMES;   /* anim + 100ms tail before respawn */
    }
    if (game.death_pending) {
        if (game.death_timer) game.death_timer--;
        if (game.death_timer == 0) {
            game.death_pending = 0;
            finalize_falls();                     /* clear any falling-tile OBJ */
            if (game.lives > 1) {                 /* still have lives -> respawn */
                u8 sx, sy, sr, sc, k;
                game.lives--;
                player_respawn();
                /* send enemies (and the robot) back to their spawn points, like the
                 * original's respawnPlayer -> enemy.reset() for all enemies. */
                if (find_spawn(&sx, &sy, &sr, &sc)) {
                    u16 md = get_enemy_move_delay(game.current_level);
                    for (k = 0; k < game.enemy_count; k++)
                        enemy_reset(&game.enemies[k], k, sx, sy, sr, sc, md,
                                    (u16)(ENEMY_INITIAL_SPAWN_FRAMES + k * ENEMY_SPAWN_STAGGER_FRAMES));
                }
                if (game.robot_count > 0)
                    robot_reset(&game.robots[0], game.cfg.robot_x, game.cfg.robot_y,
                                game.cfg.robot_row, game.cfg.robot_col,
                                get_robot_move_delay(game.current_level));
                game.fall_count = gravity_settle(game.fall_tiles, SECTION_TILES);
                render_bg2_reset();           /* back at start section -> BG2 phase 0 */
                render_build_map();
                game_map_dirty = 1;
            } else {                              /* last life lost -> game over */
                game.lives = 0;
                game.is_game_over = 1;
            }
        }
    }

    if (game.alarm_active) {  /* drive the alarm-vignette pulse phase (render_apply_alarm) */
        game.alarm_timer++;
        if (game.alarm_timer >= 2 * ALARM_PULSE_FRAMES) game.alarm_timer = 0;
    }

    update_marker_anims();   /* spawn-point glow + extra-life pickup animation */
    update_edge_warnings();  /* flash a strip if enemies lurk in an adjacent section */
    update_glitter();        /* gem shimmer (bounded: only sparkling gems redraw) */

    /* animate the open exit: cycle the 4 portal frames while it's active and on
     * the current section (the original loops the portal sprite when open). */
    if (game.portal_active && game.has_portal &&
        game.portal_row == game.cur_row && game.portal_col == game.cur_col) {
        if (game.portal_timer) game.portal_timer--;
        if (game.portal_timer == 0) {
            /* cycle the ANIMATION frames only (MT_PORTAL0 = idle, shown when closed);
             * the open exit loops frames 1..3 and never returns to the idle frame. */
            game.portal_frame = (u8)(game.portal_frame >= 3 ? 1 : game.portal_frame + 1);
            game.portal_timer = PORTAL_ANIM_FRAMES;
            render_crush_cell(game.portal.x, game.portal.y,
                              (u8)(MT_PORTAL0 + game.portal_frame));
            game_map_dirty = 1;
        }
    }

    /* reach the active portal -> award the time bonus and show the level-complete
     * stats banner (the countdown at the top of game_update advances afterwards). */
    if (game.portal_active && !game.is_level_complete && !p->is_moving && p->alive &&
        p->section_row == game.portal_row && p->section_col == game.portal_col &&
        p->x == game.portal.x && p->y == game.portal.y) {
        game.time_bonus = compute_time_bonus();
        game.score += game.time_bonus;
        game.alarm_active = 0;          /* stop the alarm vignette for the banner */
        game.is_level_complete = 1;
        game.lc_timer = LC_BANNER_FRAMES;
        render_lc_banner();      /* sets up the whole level-complete screen + flushes */
        game_map_dirty = 0;      /* don't re-render entities/HUD over it this frame */
        return;
    }

    render_player();
    update_push();                       /* slide a just-pushed block (OBJ) into place */
    render_enemies();
    render_robot();
    render_lightning();
    if (game.gravity_resolving) {        /* draw the in-flight falling tiles */
        for (i = 0; i < MAX_FALL_ANIMS; i++) {
            if (i < fall_anim_n && fall_anim[i].active)
                render_fall((u8)i, fall_anim[i].type,
                            (u16)(PLAYFIELD_OFFSET_X + fall_anim[i].col * TILE_SIZE),
                            (u16)(PLAYFIELD_OFFSET_Y + fall_anim[i].py), fall_anim[i].dmg);
            else
                oamSetVisible((u16)((OAM_FALL_BASE + i) * 4), OBJ_HIDE);
        }
    }
    if (render_hud())             /* HUD lives in BG1 rows 0-1; redrawn only on change */
        game_map_dirty = 1;       /* upload the BG1 map when the HUD changed */
    render_minimap();             /* monochrome section minimap (top-right of the bar) */

}
