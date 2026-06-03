/* Deadfall SNES port - data model (structs shared across modules). */
#ifndef TYPES_H
#define TYPES_H

#include <snes.h>        /* PVSnesLib: u8/u16/s8/s16/u32/s32, hw API */
#include "config.h"

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* One section's tile grid (working copy lives in WRAM). */
typedef u8 SectionGrid[GRID_ROWS][GRID_COLS];

typedef struct { u8 x, y; } Point;
typedef struct { u8 row, col; } SectionRef;

/* Level configuration row (see tools/convert_levels.py / levels.c). */
typedef struct {
    u8 level;
    u8 world_cols;
    u8 world_rows;
    u8 enemies;
    u8 gem_sprite;      /* 1..10 -> per-level gem & boulder art/palette  */
    u8 robots;
    u8 robot_x, robot_y;
    u8 robot_row, robot_col;
} LevelConfig;

/* ---- Player ---- */
typedef struct {
    u8  x, y;                    /* grid cell                            */
    s16 pixel_x, pixel_y;        /* pixel pos (for smooth interpolation) */
    u8  section_row, section_col;
    u8  alive;
    u8  direction;
    u8  start_x, start_y, start_row, start_col;   /* respawn point       */

    u8  is_moving;
    u8  move_timer;              /* frames remaining in current step     */
    u8  target_x, target_y;
    s16 start_pixel_x, start_pixel_y;

    u8  anim_frame;             /* bob 0/1 */
    u8  anim_timer;
    u8  idle_col;               /* idle look-around column 0/1/2, or 0xFF = use facing */
} Player;

/* ---- Mortal enemy ---- */
typedef struct {
    u8  x, y;
    s16 pixel_x, pixel_y;
    u8  section_row, section_col;
    u8  alive;
    u8  direction;

    u8  spawn_x, spawn_y, spawn_row, spawn_col;

    u8  is_moving;
    u8  move_timer;
    u8  target_x, target_y;
    s16 start_pixel_x, start_pixel_y;
    s16 mv_accx, mv_accy;       /* 8.8 fixed-point slide accumulator     */
    s16 mv_stepx, mv_stepy;     /* 8.8 per-frame increment (divide-free)  */
    u16 move_delay;             /* frames between moves (level-scaled)   */
    u16 last_move_counter;

    u16 respawn_timer;          /* absolute frame to (re)spawn at        */
    u8  respawn_pending;
    u8  initial_spawn_pending;
    u8  enemy_index;            /* for stagger                           */

    u8  anim_frame;
    u8  anim_timer;

    u8  dying;                  /* playing death anim (not alive, not yet respawning) */
    u8  death_timer;            /* frames left in the death animation */
} Enemy;

/* ---- Robot "Clanky" (invincible, zaps) ---- */
typedef struct {
    u8  x, y;
    s16 pixel_x, pixel_y;
    u8  section_row, section_col;
    u8  alive;
    u8  direction;

    u8  is_moving;
    u8  move_timer;
    u8  target_x, target_y;
    s16 start_pixel_x, start_pixel_y;
    s16 mv_accx, mv_accy;       /* 8.8 fixed-point slide accumulator     */
    s16 mv_stepx, mv_stepy;     /* 8.8 per-frame increment (divide-free)  */
    u16 move_delay;

    /* zap state machine (frame-countdown timers) */
    u16 cooldown_timer;         /* frames until next zap (0 = ready)     */
    u8  has_locked_on;
    u8  locked_direction;
    u8  is_charging;
    u16 charge_timer;           /* frames left to charge                 */
    u8  is_zapping;
    u8  just_fired;             /* set the frame a zap fires             */
    u8  zap_direction;
    u8  zap_distance;           /* actual tiles (stops at boulder)       */
    u8  zap_gems;               /* gems destroyed by the last zap (-> collected) */
    u8  zap_anim_frame;
    u8  zap_anim_timer;

    u8  eye_index;              /* index into [0,1,2,1] pulse sequence    */
    u8  eye_timer;
} Robot;

/* A tile that moved during gravity resolution (for crush checks). */
typedef struct {
    u8 type;
    u8 from_x, from_y;
    u8 to_x, to_y;
} FallTile;

/* Active gem-destruction / generic cell animation overlay. */
typedef struct {
    u8 active;
    u8 x, y, row, col;
    u8 kind;        /* 0 = gem crush (collect), 1 = block break */
    u8 stage;
    u8 timer;
} CellAnim;

/* ---- Whole-game state (single instance in WRAM) ---- */
typedef struct {
    u8 current_level;            /* 1..10 */
    LevelConfig cfg;

    u8 world_cols, world_rows;
    u8 cur_row, cur_col;         /* active section */
    SectionGrid sections[MAX_SECTIONS];
    u8 damage[MAX_SECTIONS][GRID_ROWS][GRID_COLS];

    Player player;
    Enemy  enemies[MAX_ENEMIES];
    u8     enemy_count;
    Robot  robots[MAX_ROBOTS];
    u8     robot_count;

    /* spawn / portal bookkeeping */
    Point spawn_points[MAX_SECTIONS][MAX_SPAWN_POINTS];
    u8    spawn_count[MAX_SECTIONS];
    Point robot_spawns[MAX_SECTIONS][MAX_ROBOT_SPAWNS];
    u8    robot_spawn_count[MAX_SECTIONS];
    Point portal;
    u8    portal_row, portal_col, has_portal;

    /* progress */
    u32 score;
    u8  lives;
    u8  continues;
    u16 total_gems;
    u16 gems_collected;

    /* per-level stats + time bonus (shown on the level-complete banner) */
    u16 level_start_frame;       /* game.frame when the level began            */
    u32 level_par_ms;            /* computed par time in ms (calculateParTime)  */
    u16 blocks_crushed;          /* blocks destroyed this level                 */
    u16 enemies_killed;          /* enemies the player killed this level        */
    u16 time_bonus;             /* awarded on level complete                    */
    u16 lc_timer;                /* level-complete banner countdown (frames)    */

    /* transient on-screen flash banner (SECTION CLEARED / NO ENTRY) */
    u8  flash_timer;             /* frames remaining (0 = none)                */
    u16 sections_cleared;        /* bitmask of sections that already flashed   */

    /* flags */
    u8 portal_active;
    u8 alarm_active;
    u8 is_paused;
    u8 is_game_over;
    u8 is_victory;
    u8 is_level_complete;

    /* death / portal / spawn animation */
    u8 death_pending, death_frame, death_timer;
    u8 portal_frame, portal_timer;
    u8 spawn_glow_index, spawn_glow_timer;

    /* gravity animation */
    u8 gravity_resolving;
    u8 gravity_timer;
    FallTile fall_tiles[SECTION_TILES]; /* falls recorded in one step/settle */
    u8 fall_count;

    CellAnim cell_anims[16];

    /* section transition (fade-out -> switch -> fade-in) */
    u8  transitioning;
    u8  trans_dir;
    u8  trans_timer;
    u8  trans_phase;              /* 0 = fading out, 1 = fading in */
    u8  trans_entry_x, trans_entry_y;

    /* input */
    u16 pad_cur, pad_prev, pad_pressed;
    u8  mine_cooldown;

    /* push-delay: hold a direction against a gem/boulder for PUSH_DELAY_FRAMES
     * before it actually shoves (so you can crush/mine or tap past it). */
    u8  push_pending;
    s8  push_dx, push_dy;
    u8  push_timer;

    /* crush safety: after mining destroys a tile, block moving INTO that cell
     * for CRUSH_SAFETY_FRAMES so gravity can settle (else you walk into a fall). */
    u8  crushed_x, crushed_y;
    u8  crush_safety_timer;

    /* misc timers */
    u16 frame;
    u8  alarm_timer, alarm_on;
} GameState;

extern GameState game;

#endif /* TYPES_H */
