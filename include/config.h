/* Deadfall SNES port - compile-time configuration and constants.
 * Mirrors ../cubed/src/constants.js and GameBalance.js, converted to the
 * frame-based timing used on NTSC SNES (~60 fps). */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- Display (SNES native, exact match to the source game) ---- */
#define GAME_WIDTH          256
#define GAME_HEIGHT         224
#define TILE_SIZE           16

/* ---- Playfield grid: 16 cols x 13 rows ---- */
#define GRID_COLS           16
#define GRID_ROWS           13
#define SECTION_TILES       (GRID_ROWS * GRID_COLS)   /* 208 */
#define PLAYFIELD_WIDTH     (GRID_COLS * TILE_SIZE)   /* 256 */
#define PLAYFIELD_HEIGHT    (GRID_ROWS * TILE_SIZE)   /* 208 */
#define UI_HEIGHT           (GAME_HEIGHT - PLAYFIELD_HEIGHT) /* 16 */
#define HUD_TOP_MARGIN      0     /* no margin: HUD(16) + playfield(208) = 224 exactly */
#define PLAYFIELD_OFFSET_X  0
#define PLAYFIELD_OFFSET_Y  (UI_HEIGHT + HUD_TOP_MARGIN)  /* 16: HUD bar only, full fit */

/* ---- World: up to 4x4 sections, wrapping (torus) ---- */
#define MAX_WORLD_COLS      4
#define MAX_WORLD_ROWS      4
#define MAX_SECTIONS        (MAX_WORLD_COLS * MAX_WORLD_ROWS) /* 16 */

/* ---- Tile types (identical codes to constants.js) ---- */
#define TILE_EMPTY          0
#define TILE_BLOCK          1   /* mineable, structural (never falls)        */
#define TILE_GEM            2   /* mineable->collected, falls, pushable      */
#define TILE_BOULDER        3   /* indestructible, falls, pushable           */
#define TILE_SPAWN          4   /* enemy spawn point                         */
#define TILE_PORTAL         5   /* player start + level exit                 */
#define TILE_ROBOT_SPAWN    6   /* robot spawn (acts as empty for gravity)   */
#define TILE_EXTRA_LIFE_BLK 7   /* block that reveals an extra life          */
#define TILE_EXTRA_LIFE     8   /* revealed extra life (walk-through collect) */

#define TILE_HITS_TO_DESTROY 3
#define GEM_HITS_TO_DESTROY  3

/* ---- Entity counts ---- */
#define MAX_ENEMIES         5
#define MAX_ROBOTS          1
#define MAX_SPAWN_POINTS    4
#define MAX_ROBOT_SPAWNS    2
#define MAX_LEVELS          10
#define START_LIVES         3
#define MAX_CONTINUES       3

/* ---- Directions ---- */
#define DIR_LEFT    0
#define DIR_RIGHT   1
#define DIR_UP      2
#define DIR_DOWN    3

/* ---- Scoring (from GameScene.js) ---- */
#define SCORE_GEM           100
#define SCORE_CRUSH_KILL    500
#define SCORE_ZAP_KILL      1000

/* ---- Timing in frames @60fps  (round(ms / 16.667)) ---- */
#define PLAYER_MOVE_FRAMES  8    /* divides TILE_SIZE(16) -> exactly 2px/frame, no integer-
                                  * division stepping; ~1.6x faster than the 220ms original */
#define ANIM_BOB_FRAMES     12   /* 200ms bob toggle                     */
#define GRAVITY_STEP_FRAMES 6    /* 100ms per animated fall step         */
#define MINE_REPEAT_FRAMES  14   /* snappier repeat-mine                 */
#define PUSH_DELAY_FRAMES   16   /* snappier push                        */
#define CRUSH_SAFETY_FRAMES 30   /* 500ms block on moving into a just-mined cell */
#define DEATH_ANIM_FRAMES   7    /* 120ms per death frame (5 frames)     */
#define DEATH_ANIM_COUNT    5
#define DEATH_SEQ_FRAMES    (DEATH_ANIM_FRAMES * DEATH_ANIM_COUNT + 6) /* +100ms tail before respawn */
#define CRUSH_ANIM_FRAMES   12   /* 200ms per shatter stage (matches JS)  */
#define CRUSH_ANIM_STAGES   2    /* shatter frames 3,4 -> then gravity     */
#define LC_BANNER_FRAMES    300  /* 5s level-complete stats banner (JS delayedCall 5000) */
#define PORTAL_ANIM_FRAMES  9    /* 150ms per portal/glitter frame       */
#define SPAWN_ANIM_FRAMES   12   /* 200ms spawn glow                     */
#define TRANSITION_FRAMES   36   /* 600ms section slide                  */
#define TRANS_FADE_FRAMES   12   /* per fade phase (out/in) -> 24f section fade */
#define GLITTER_INTERVAL    180  /* 3000ms between gem glitters          */
#define ALARM_PULSE_FRAMES  30   /* 500ms vignette pulse half-cycle      */

/* ---- BFS pathfinding budget ---- */
#define BFS_MAX_DEPTH       15
#define BFS_QUEUE_SIZE      256

/* Anti-oscillation: bias an enemy AGAINST immediately undoing its last move, so a
 * still player doesn't get an enemy ping-ponging across a section boundary (the
 * greedy wrapped-Manhattan heuristic is ambiguous on small wrapping worlds). In
 * score units (distance is *2), so 3 ~= 1.5 tiles: a reverse is taken only when it
 * is clearly better (e.g. the player is right behind), never to break a near-tie.
 * Set to 0 to get the exact JS behaviour back. */
#define ENEMY_REVERSE_PENALTY 3

/* ---- Fixed point (8.8) for sub-pixel movement ---- */
#define FP_SHIFT    8
#define FP_ONE      (1 << FP_SHIFT)
#define FP_HALF     (FP_ONE >> 1)

/* ---- SNES joypad bits (match PVSnesLib pad values) ---- */
#define PAD_R       0x0010
#define PAD_L       0x0020
#define PAD_X       0x0040
#define PAD_A       0x0080
#define PAD_RIGHT   0x0100
#define PAD_LEFT    0x0200
#define PAD_DOWN    0x0400
#define PAD_UP      0x0800
#define PAD_START   0x1000
#define PAD_SELECT  0x2000
#define PAD_Y       0x4000
#define PAD_B       0x8000

#endif /* CONFIG_H */
