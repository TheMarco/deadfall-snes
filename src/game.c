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

/* ---- smooth falling tiles ---------------------------------------------------
 * Gravity settles the grid instantly (logic), but each tile that moved slides
 * down as a 16x16 OBJ from its origin to its landing cell. The landing cell is
 * blanked on BG1 during the slide and redrawn when the tile arrives. */
typedef struct { u8 active, type, col, to_y; s16 py, py_end; } FallAnim;
static FallAnim fall_anim[MAX_FALL_ANIMS];
static u8 fall_anim_n;
#define FALL_DY 3                /* px/frame (~90ms per cell, near the JS 100ms) */

static u8 crush_pending;         /* a destroy is mid-shatter -> run gravity when it ends */

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
    for (i = 0; i < game.fall_count; i++) {
        FallTile *f = &game.fall_tiles[i];
        render_set_cell(f->from_x, f->from_y);          /* origin is now empty */
        if (fall_anim_n < MAX_FALL_ANIMS) {
            FallAnim *a = &fall_anim[fall_anim_n++];
            a->active = 1; a->type = f->type; a->col = f->to_x; a->to_y = f->to_y;
            a->py = (s16)(f->from_y * TILE_SIZE);
            a->py_end = (s16)(f->to_y * TILE_SIZE);
            render_clear_cell(f->to_x, f->to_y);        /* hide landed tile until it slides in */
        } else {
            render_set_cell(f->to_x, f->to_y);          /* overflow: no anim, draw final now */
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
                audio_sfx(SFX_ENEMYKILL);
                break;
            }
        }
    }
}

static void load_level(u8 n) {
    u8 sx, sy, sr, sc, i;
    u16 md;

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
    fall_anim_n = 0;              /* drop any in-flight falling-tile anims */
    for (i = 0; i < 16; i++) game.cell_anims[i].active = 0;  /* drop shatter anims */
    crush_pending = 0;
    game.gravity_resolving = 0;
    render_falls_hide();
    render_minimap_reset();       /* rebuild the minimap for the new level's world */
    render_clear_screen();        /* wipe any leftover scene text (e.g. title) from BG3 */
    render_bg2_reset();           /* start section = BG2 phase 0 */
    render_build_map();
    render_load_background();     /* load the current section's backdrop (forced blank) */
    audio_music_level(n);        /* this level's chiptune theme */
    game_map_dirty = 1;
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
        if (game.gems_collected >= game.total_gems) {
            game.portal_active = 1;
            game.alarm_active = 1;
            audio_sfx(SFX_PORTAL);          /* exit opens */
        } else {
            audio_sfx(SFX_COLLECT);
        }
    } else if (tile == TILE_EXTRA_LIFE_BLK) {
        world_set_tile(mx, my, TILE_EXTRA_LIFE);   /* reveal the 1-up (after the shatter) */
        audio_sfx(SFX_CRUSH);
    } else {
        world_set_tile(mx, my, TILE_EMPTY);        /* plain block destroyed */
        audio_sfx(SFX_CRUSH);
    }
    world_clear_damage(mx, my);
    /* play the shatter; gravity is deferred until it finishes (crush_anims_update) */
    crush_anim_add(mx, my, base_mt);
    crush_pending = 1;
    /* block walking into the just-cleared cell until gravity settles (crush safety) */
    game.crushed_x = mx; game.crushed_y = my;
    game.crush_safety_timer = CRUSH_SAFETY_FRAMES;
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

    if (r.section_blocked) audio_sfx(SFX_NOENTRY);

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
        if (!r.cross_push && (p->x + dx) >= 0 && (p->x + dx) < GRID_COLS)
            render_set_cell((u8)(p->x + dx), p->y);   /* tile's new cell */
        game_map_dirty = 1;
        start_gravity();
    } else if (r.collected_extra_life) {
        world_set_tile(p->x, p->y, TILE_EMPTY);
        render_set_cell(p->x, p->y);
        if (game.lives < 9) game.lives++;
        audio_sfx(SFX_EXTRALIFE);
        game_map_dirty = 1;
    }
}

void game_update(void) {
    Player *p = &game.player;
    u16 cur;
    s8 dx = 0, dy = 0;
    u8 i;

    cur = padsCurrent(0);   /* pad_keys[] auto-updated each vblank by the lib */

    game.frame++;

    /* DEBUG (level select): tap Y with NO direction held -> jump to the next
     * level (wraps 10->1), so each level's art is easy to check. Mining is
     * Y+direction, so this never interferes with it. Edge-detected so holding Y
     * jumps once. */
    {
        static u8 dbg_yprev = 0;
        u8 ynow = (u8)((cur & PAD_Y) != 0);
        u8 dirheld = (u8)((cur & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) != 0);
        if (ynow && !dbg_yprev && !dirheld && !game.transitioning &&
            !game.death_pending && p->alive) {
            dbg_yprev = ynow;
            load_level((u8)(game.current_level >= MAX_LEVELS ? 1 : game.current_level + 1));
            return;
        }
        dbg_yprev = ynow;
    }

    /* section transition: fade screen out, switch section at black, fade in.
     * Gameplay (input, enemies, gravity) is frozen for the ~24 frames. */
    if (game.transitioning) {
        if (game.trans_phase == 2) {                       /* SLIDE (H or V) */
            u16 cam;
            if (game.trans_timer) game.trans_timer--;
            cam = (u16)((u32)256 * (TRANSITION_FRAMES - game.trans_timer) / TRANSITION_FRAMES);
            /* LEFT/UP enter from the near side, so the camera runs 256 -> 0. */
            if (game.trans_dir == DIR_LEFT || game.trans_dir == DIR_UP) cam = (u16)(256 - cam);
            render_slide_scroll(cam);
            render_hide_sprites();                         /* enemies/robot hidden mid-slide */
            render_slide_player(cam);                      /* player entering the new section */
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
                render_set_cell(a->col, a->to_y);      /* draw the settled tile on BG1 */
                game_map_dirty = 1;
            } else {
                any = 1;
            }
        }
        /* A falling tile only kills the player when it actually REACHES them
         * (pixel overlap), so you get the fall's worth of time to run out from
         * under it -- matching the original. (Enemies are crushed up-front.) */
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
        if (!any) { render_falls_hide(); game.gravity_resolving = 0; }
    }

    if (game.mine_cooldown) game.mine_cooldown--;
    if (game.crush_safety_timer) game.crush_safety_timer--;

    /* direction (single axis; horizontal wins ties, matching simple input) */
    if (cur & PAD_LEFT)       dx = -1;
    else if (cur & PAD_RIGHT) dx = 1;
    else if (cur & PAD_UP)    dy = -1;
    else if (cur & PAD_DOWN)  dy = 1;

    if (p->alive && !p->is_moving && !game.death_pending) {
        /* mine: hold any face button (A/B/X/Y) + a direction.
         * (Final scheme is Y to mine; accepting all four eases testing.) */
        if (cur & (PAD_Y | PAD_B | PAD_A | PAD_X)) {
            cancel_push();                 /* mining cancels a pending push */
            if (dx || dy) do_mine(dx, dy);
        } else if (dx || dy) {
            do_move(dx, dy);               /* handles the push-delay internally */
        } else {
            cancel_push();                 /* direction released -> cancel */
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
        game.death_timer = DEATH_ANIM_FRAMES * DEATH_ANIM_COUNT;
    }
    if (game.death_pending) {
        if (game.death_timer) game.death_timer--;
        if (game.death_timer == 0) {
            game.death_pending = 0;
            finalize_falls();                     /* clear any falling-tile OBJ */
            if (game.lives > 1) {                 /* still have lives -> respawn */
                game.lives--;
                player_respawn();
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

    /* reach the active portal -> next level, or win after the last */
    if (game.portal_active && !p->is_moving && p->alive &&
        p->section_row == game.portal_row && p->section_col == game.portal_col &&
        p->x == game.portal.x && p->y == game.portal.y) {
        game.score += 1000;                       /* level-clear bonus */
        if (game.current_level >= MAX_LEVELS) game.is_victory = 1;
        else load_level((u8)(game.current_level + 1));
    }

    render_player();
    render_enemies();
    render_robot();
    render_lightning();
    if (game.gravity_resolving) {        /* draw the in-flight falling tiles */
        for (i = 0; i < MAX_FALL_ANIMS; i++) {
            if (i < fall_anim_n && fall_anim[i].active)
                render_fall((u8)i, fall_anim[i].type,
                            (u16)(PLAYFIELD_OFFSET_X + fall_anim[i].col * TILE_SIZE),
                            (u16)(PLAYFIELD_OFFSET_Y + fall_anim[i].py));
            else
                oamSetVisible((u16)((OAM_FALL_BASE + i) * 4), OBJ_HIDE);
        }
    }
    if (render_hud())             /* HUD lives in BG1 rows 0-1; redrawn only on change */
        game_map_dirty = 1;       /* upload the BG1 map when the HUD changed */
    render_minimap();             /* monochrome section minimap (top-right of the bar) */
}
