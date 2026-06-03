/* Deadfall SNES port - rendering (Mode 1: BG1 playfield + OBJ entities). */
#ifndef RENDER_H
#define RENDER_H

#include "types.h"

/* VRAM word-address layout. BG2 background can use up to ~960 tiles (26KB),
 * so it sits at 0x0000 and BG1/OBJ/maps are placed after it. */
#define VRAM_BG2_TILES  0x0000   /* level background 4bpp tiles (<=1000) 0x0000-0x3E80 */
#define VRAM_BG1_TILES  0x4000   /* gameplay 4bpp tiles (64), tiles 0..63 */
#define VRAM_BG1_HUD    0x4400   /* HUD glyph tiles 64..127               */
#define VRAM_BG1_MAP    0x4800   /* BG1 tilemap screen 0 (SC_32x32, or SC_64x32 base) */
#define VRAM_BG1_MAP2   0x4C00   /* BG1 tilemap screen 1 (adjacent section during a slide) */
#define VRAM_BG2_MAP    0x5800   /* BG2 32x32 tilemap (moved out of the 256x256 tile region) */
#define BG2_PAL         2        /* BG2 uses sub-palettes 2..7 (96 colors, CGRAM 32..127) */
#define HUD_TILE_BASE   64       /* BG1 tile index where the HUD font starts */
#define HUD_PAL         1        /* BG sub-palette 1 (white), CGRAM 16..31    */
#define VRAM_BG3_TILES  0x5000   /* HUD/text 2bpp font (BG3), tiles 0..63    */
#define VRAM_BG3_MAP    0x5400   /* BG3 32x32 tilemap (HUD + scene text)     */
#define BG3_TEXT_PAL    4        /* 2bpp sub-palette 4 -> CGRAM 16..19 (white@17) */
#define HUD_ICON_LIFE   64       /* BG3 tile 64: custom life (heart) glyph, past the font (0..63) */
#define VRAM_OBJ_TILES  0x6000   /* OBJ base: player tiles 0..63         */
#define VRAM_OBJ_ENEMY  0x6400   /* enemy tiles 64..127                  */
#define VRAM_OBJ_ROBOT  0x6800   /* robot tiles 128..223                 */
#define VRAM_OBJ_ZAPH   0x6E00   /* horizontal zap tiles 224..287 (64)   */
#define VRAM_OBJ_ZAPV   0x7200   /* vertical zap tiles 288..351 (64)     */
#define VRAM_OBJ_PDEATH 0x7600   /* player death anim tiles 352..383 (32) */
#define VRAM_OBJ_EDEATH 0x7800   /* enemy death anim tiles 384..415 (32)  */
#define VRAM_OBJ_FALLS  0x7A00   /* falling gem/boulder/extra-life 416..447 */

/* OBJ tile-name bases (16-wide grid) and palette numbers. */
#define OBJN_PLAYER   0
#define OBJN_ENEMY    64
#define OBJN_ROBOT    128
#define OBJN_ZAPH     224
#define OBJN_ZAPV     288
#define OBJN_PDEATH   352
#define OBJN_EDEATH   384
#define OBJN_GEM_FALL     416   /* (VRAM_OBJ_FALLS-VRAM_OBJ_TILES)/16; sheet frame 0 */
#define OBJN_BOULDER_FALL 418   /* sheet frame 1 (fc*2) */
#define OBJN_ELIFE_FALL   420   /* sheet frame 2 */
#define OBJPAL_PLAYER 0
#define OBJPAL_ENEMY  1
#define OBJPAL_ROBOT  2
#define OBJPAL_ZAPH   3
#define OBJPAL_ZAPV   4
#define OBJPAL_PDEATH 5
#define OBJPAL_EDEATH 6
#define OBJPAL_FALLS  7

/* OAM slot ids (each entry is 4 bytes). */
#define OAM_PLAYER     0
#define OAM_ENEMY_BASE 1     /* slots 1..5 */
#define OAM_ROBOT      6
#define OAM_ZAP_BASE   7     /* slots 7..9 (up to ROBOT_ZAP_RANGE segments) */
#define OAM_FALL_BASE  10    /* slots 10..(10+MAX_FALL_ANIMS-1): smooth falling tiles */
#define MAX_FALL_ANIMS 16

/* BG1 metatile indices (each = 4 sequential 8x8 tiles). 20 metatiles total. */
#define MT_EMPTY       0
#define MT_BLOCK0      1
#define MT_GEM0        4
#define MT_BOULDER     7
#define MT_PORTAL0     8
#define MT_SPAWN0      12
#define MT_EXTRALIFE   14
#define MT_ROBOTSPN    15
#define MT_BLOCK_CRUSH 16   /* block shatter frames 3,4 (16,17): destruction anim */
#define MT_GEM_CRUSH   18   /* gem shatter   frames 3,4 (18,19): destruction anim */
#define NB_METATILES   20

void render_init(void);        /* Mode 1, load gfx/palettes, build+show 1st section */
void render_set_background(void);  /* load the current section's BG2 (caller force-blanked) */
void render_bg2_reset(void);       /* reset the per-section BG2 phase (level load / respawn) */
void render_load_background(void); /* swap BG2 to current section (handles forced blank)     */
void render_load_gameplay_tiles(u8 level); /* load level's gem/boulder/block tiles+palettes (forced-blank) */
void render_show_title(void);      /* put the DEADFALL logo image on BG2 (title scene)        */
void render_build_map(void);        /* full rebuild from the current section (load/transition) */
void render_slide_begin(u8 dir, u8 adj_row, u8 adj_col); /* stage adjacent section, go 64-wide */
void render_slide_scroll(u16 cam);  /* shadow BG1+BG2 scroll during a slide (applied in vblank) */
void render_apply_scroll(void);     /* write shadowed scroll to PPU regs; call in vblank */
void render_slide_player(u16 cam);  /* draw player entering the new section during a slide */
void render_slide_end(void);        /* back to single-screen, scroll 0 */
void render_set_cell(u8 gx, u8 gy); /* update just one grid cell's 4 BG entries (cheap)       */
void render_crush_cell(u8 gx, u8 gy, u8 mt); /* draw an explicit metatile (shatter frame) at a cell */
void render_clear_cell(u8 gx, u8 gy); /* blank a grid cell's BG entries (tile is in-flight as OBJ) */
void render_fall(u8 slot, u8 type, u16 px, u16 py); /* draw a falling tile OBJ at screen pixel */
void render_falls_hide(void);       /* hide all falling-tile OBJ slots                         */
void render_flush_map(void);        /* DMA the RAM tilemap to VRAM (call in vblank)            */
void render_player(void);      /* place the player sprite from its pixel position     */
void render_enemies(void);     /* place/hide enemy sprites (active section only)      */
void render_robot(void);       /* place/hide the robot sprite (active section only)   */
void render_lightning(void);   /* draw/hide the zap beam segments                     */
u8   render_hud(void);         /* redraw HUD into BG1 rows 1-2 if changed; TRUE if so */
void render_minimap(void);     /* update the monochrome section minimap (top-right)  */
void render_minimap_reset(void); /* drop minimap cache so it rebuilds (level load)    */
void render_clear_screen(void);            /* clear the whole BG1 tilemap (text scenes) */
void render_text(u8 x, u8 y, const char *s); /* HUD-font text anywhere on BG1           */
void render_num(u8 x, u8 y, u32 val, u8 digits); /* HUD-font fixed-width number          */
void render_hide_sprites(void);            /* hide all entity OBJ slots                 */

#endif /* RENDER_H */
