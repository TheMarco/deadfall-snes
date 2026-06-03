/* Deadfall SNES port - rendering. Mode 1: BG1 = playfield grid, OBJ = entities.
 * The BG1 tilemap is generated at runtime from the active section's grid. */
#include <snes.h>
#include "render.h"
#include "world.h"
#include "balance.h"
#include "game.h"      /* game_map_dirty (set when the minimap/HUD changes) */

/* PVSnesLib shadows of the screen-designation registers. */
extern u8 videoMode;     /* mirror of REG_TM (main screen) */
extern u8 videoModeSub;  /* mirror of REG_TS (sub screen)  */

/* Converted graphics (see data.asm). */
/* Per-level BG1 gameplay tilesets (gem/boulder/block vary per level). */
extern char bg_tiles_1_pic, bg_tiles_1_picend, bg_tiles_1_pal;
extern char bg_tiles_2_pic, bg_tiles_2_picend, bg_tiles_2_pal;
extern char bg_tiles_3_pic, bg_tiles_3_picend, bg_tiles_3_pal;
extern char bg_tiles_4_pic, bg_tiles_4_picend, bg_tiles_4_pal;
extern char bg_tiles_5_pic, bg_tiles_5_picend, bg_tiles_5_pal;
extern char bg_tiles_6_pic, bg_tiles_6_picend, bg_tiles_6_pal;
extern char bg_tiles_7_pic, bg_tiles_7_picend, bg_tiles_7_pal;
extern char bg_tiles_8_pic, bg_tiles_8_picend, bg_tiles_8_pal;
extern char bg_tiles_9_pic, bg_tiles_9_picend, bg_tiles_9_pal;
extern char bg_tiles_10_pic, bg_tiles_10_picend, bg_tiles_10_pal;
extern char spr_player_pic, spr_player_picend, spr_player_pal;
extern char spr_enemy_pic, spr_enemy_picend, spr_enemy_pal;
extern char spr_robot_pic, spr_robot_picend, spr_robot_pal;
extern char spr_zap_h_pic, spr_zap_h_picend, spr_zap_h_pal;
extern char spr_zap_v_pic, spr_zap_v_picend, spr_zap_v_pal;
extern char spr_pdeath_pic, spr_pdeath_picend, spr_pdeath_pal;
extern char spr_edeath_pic, spr_edeath_picend, spr_edeath_pal;
extern char spr_falls_pic, spr_falls_picend, spr_falls_pal;  /* falling gem/boulder/1-up */
extern char hud_font_pic, hud_font_picend, hud_font_pal;
extern char hud_font2_pic, hud_font2_picend;   /* 2bpp font for BG3 text layer */
extern char hud_font2_pal;                     /* BG3 sub-palette: white text on black */

/* Seamless repeating background texture (128x96 = 16x12 tiles), tiled across
 * BG2 for every level. Tiny + resident, so the background scrolls cleanly. */
/* Per-level BG2 parallax textures (256x256). render_load_tileset swaps in the
 * current level's via bgtex_select(). Level 1 == the original test3 texture. */
extern char bgtex_1_pic, bgtex_1_picend, bgtex_1_map, bgtex_1_pal;
extern char bgtex_2_pic, bgtex_2_picend, bgtex_2_map, bgtex_2_pal;
extern char bgtex_3_pic, bgtex_3_picend, bgtex_3_map, bgtex_3_pal;
extern char bgtex_4_pic, bgtex_4_picend, bgtex_4_map, bgtex_4_pal;
extern char bgtex_5_pic, bgtex_5_picend, bgtex_5_map, bgtex_5_pal;
extern char bgtex_6_pic, bgtex_6_picend, bgtex_6_map, bgtex_6_pal;
extern char bgtex_7_pic, bgtex_7_picend, bgtex_7_map, bgtex_7_pal;
extern char bgtex_8_pic, bgtex_8_picend, bgtex_8_map, bgtex_8_pal;
extern char bgtex_9_pic, bgtex_9_picend, bgtex_9_map, bgtex_9_pal;
extern char bgtex_10_pic, bgtex_10_picend, bgtex_10_map, bgtex_10_pal;
extern char title_pic, title_picend, title_map, title_pal;   /* title-screen BG2 image */
#define BGTEX_W 32   /* texture width in tiles  */
#define BGTEX_H 32   /* texture height in tiles (256x256 -> fills the 32-tile map exactly) */

/* Per-section BG2 parallax/phase: each section shifts the seamless background by
 * BG2_PHASE_STEP (a non-divisor of 256) so adjacent sections look different. The
 * background scrolls less than the playfield during a slide (parallax). */
#define BG2_PHASE_STEP 96
static u16 bg2_cur_x, bg2_cur_y;        /* current section's BG2 scroll (accumulates) */
static u16 slide_bg2_ox, slide_bg2_oy;  /* BG2 scroll captured at slide start */
static s16 slide_bg2_dx, slide_bg2_dy;  /* BG2 scroll delta across the slide */

/* Custom HUD life-icon glyph (white heart) as a 2bpp 8x8 SNES tile: row-
 * interleaved planes, plane0 = the heart shape (index 1 = white), plane1 =
 * ~plane0 so every other pixel is index 2 (black), matching the opaque font. */
static const u8 hud_life_tile[16] = {
    0xD8, 0x27,   /* ##.##... */
    0xF8, 0x07,   /* #####... */
    0xF8, 0x07,   /* #####... */
    0xF8, 0x07,   /* #####... */
    0x70, 0x8F,   /* .###.... */
    0x20, 0xDF,   /* ..#..... */
    0x00, 0xFF,   /* ........ */
    0x00, 0xFF    /* ........ */
};

/* ---- HUD minimap (monochrome, top-right of the bar) ----
 * A world_cols x world_rows grid of section squares on BG3, white-on-black to
 * match the HUD font: current section = hollow ring ("you are here"); a section
 * that still holds gems = solid square; the exit section while the alarm is up =
 * blinking solid; empty / out-of-world = blank. The 2x2 tiles (VRAM 65..68) are
 * rebuilt on change and DMA'd in vblank (render_flush_map). */
#define MM_TILE0  (HUD_ICON_LIFE + 1)   /* BG3 tiles 65..68 */
#define MM_COL    30                     /* bg3map column of the 2-wide minimap */
#define MM_CELL   4                      /* px pitch per section (3x3 square + 1 gap) */
/* CGRAM 19 (BG3 sub-pal 4 index3) reclaimed as the minimap grey (#888888 BGR555). */
static const u8 mm_grey_cgram[2] = { 0x31, 0x46 };
static u8  mm_tiles[64];                 /* 4 tiles x 16 bytes; built here, DMA'd in vblank */
static u8  mm_has_gems[MAX_SECTIONS];    /* per-section "gems remain" (round-robin scanned) */
static u8  mm_scan;                      /* round-robin scan cursor */
static u16 mm_blink;                     /* exit-blink phase counter */
static u8  mm_dirty;                     /* tiles changed -> DMA in render_flush_map */
static u32 mm_sig = 0xFFFFFFFF;          /* last-drawn signature (skip rebuild if same) */

/* 32x32 tilemap buffers (entry = tile number | palette<<10 | flips). */
static u16 bg1map[32 * 32];
static u16 bg2map[32 * 32];
static u16 bg3map[32 * 32];   /* HUD + scene text (BG3, fixed, high priority) */
static u16 bg1map2[32 * 32];  /* BG1 screen 1: adjacent section, staged during a slide */

/* Slide state. The staging (start) and commit (end) VRAM writes are deferred to
 * render_flush_map (which runs in vblank) so the screen never blanks -> no flash. */
static s16 slide_stage_x;     /* screen-x where the new section sits (256=right, 0=left) */
static u8  slide_pending;     /* 0 none, 1 stage adjacent (go 64-wide), 2 commit (back to 32) */
static u8  slide_dir;

/* Scroll shadow: written during the frame, applied to the PPU regs in vblank
 * (render_apply_scroll) so the scroll never changes mid-screen -> no tearing. */
static u16 scr_bg1x, scr_bg1y, scr_bg2x, scr_bg2y;
static u8  scroll_dirty;

/* Each gameplay metatile's BG sub-palette bits (palette<<10 for the tilemap):
 * pal 0 = block/boulder/gem (CGRAM 0-15), pal 1 = portal/spawn/extra-life/
 * robot-spawn (CGRAM 16-31). The two-palette split lets gems stay vivid red
 * while the blue/gold markers keep their own colors. Kept in sync with
 * tools/build_gfx.py MT_PAL (grouping by hue). */
static const u16 mt_palbits[NB_METATILES] = {
    0, 0, 0, 0,                    /* empty, block x3            -> pal 0 */
    0, 0, 0,                       /* gem x3                     -> pal 0 */
    0,                             /* boulder                    -> pal 0 */
    1 << 10, 1 << 10, 1 << 10, 1 << 10,   /* portal x4           -> pal 1 */
    1 << 10, 1 << 10,             /* spawn x2                    -> pal 1 */
    1 << 10, 1 << 10,            /* extra-life, robot-spawn      -> pal 1 */
    0, 0,                          /* block shatter 3,4          -> pal 0 */
    0, 0                           /* gem shatter   3,4          -> pal 0 */
};

/* Map a tile type (+ damage) to its BG1 metatile index. */
static u8 metatile_for(u8 t, u8 dmg) {
    switch (t) {
        case TILE_BLOCK:          return (u8)(MT_BLOCK0 + (dmg > 2 ? 2 : dmg));
        case TILE_GEM:            return (u8)(MT_GEM0   + (dmg > 2 ? 2 : dmg));
        case TILE_BOULDER:        return MT_BOULDER;
        case TILE_SPAWN:          return MT_SPAWN0;
        case TILE_PORTAL:         return MT_PORTAL0;       /* inactive frame */
        case TILE_ROBOT_SPAWN:    return MT_ROBOTSPN;
        case TILE_EXTRA_LIFE_BLK: return (u8)(MT_BLOCK0 + (dmg > 2 ? 2 : dmg)); /* looks like a block */
        case TILE_EXTRA_LIFE:     return MT_EXTRALIFE;
        default:                  return MT_EMPTY;
    }
}

/* Update one 16x16 grid cell's four 8x8 BG entries from the current section. */
void render_set_cell(u8 gx, u8 gy) {
    u8 idx = world_section_index(game.cur_row, game.cur_col);
    u8 t   = game.sections[idx][gy][gx];
    u8 mt  = metatile_for(t, game.damage[idx][gy][gx]);
    u16 base = (u16)(mt * 4);
    u16 pb  = mt_palbits[mt];                            /* sub-palette bits */
    u16 col = (u16)(gx * 2);
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + gy * 2);  /* below the HUD/margin */
    bg1map[row * 32 + col]           = base | pb;             /* TL */
    bg1map[row * 32 + col + 1]       = (u16)(base + 1) | pb;  /* TR */
    bg1map[(row + 1) * 32 + col]     = (u16)(base + 2) | pb;  /* BL */
    bg1map[(row + 1) * 32 + col + 1] = (u16)(base + 3) | pb;  /* BR */
}

/* Draw an explicit metatile (e.g. a shatter frame 16-19) into one grid cell,
 * regardless of the world tile there -- used by the crush/destruction animation,
 * which overlays the shatter frames after the world cell is already cleared. */
void render_crush_cell(u8 gx, u8 gy, u8 mt) {
    u16 base = (u16)(mt * 4);
    u16 pb  = mt_palbits[mt];
    u16 col = (u16)(gx * 2);
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + gy * 2);
    bg1map[row * 32 + col]           = base | pb;
    bg1map[row * 32 + col + 1]       = (u16)(base + 1) | pb;
    bg1map[(row + 1) * 32 + col]     = (u16)(base + 2) | pb;
    bg1map[(row + 1) * 32 + col + 1] = (u16)(base + 3) | pb;
}

/* Blank a grid cell's four BG1 entries (the tile is shown as a falling OBJ
 * instead). Does NOT touch the world grid -- render_set_cell redraws it on land. */
void render_clear_cell(u8 gx, u8 gy) {
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + gy * 2);
    u16 col = (u16)(gx * 2);
    bg1map[row * 32 + col]           = 0;
    bg1map[row * 32 + col + 1]       = 0;
    bg1map[(row + 1) * 32 + col]     = 0;
    bg1map[(row + 1) * 32 + col + 1] = 0;
}

void render_build_map(void) {
    u8 gx, gy;
    u16 i;
    for (i = (PLAYFIELD_OFFSET_Y >> 3) * 32; i < 32 * 32; i++) bg1map[i] = 0; /* clear playfield rows (keep HUD/margin) */
    for (gy = 0; gy < GRID_ROWS; gy++)
        for (gx = 0; gx < GRID_COLS; gx++)
            render_set_cell(gx, gy);
}

void render_flush_map(void) {
    /* Upload the regenerated minimap glyph tiles (VRAM tiles 65..68) in vblank. */
    if (mm_dirty) {
        dmaCopyVram(mm_tiles, (u16)(VRAM_BG3_TILES + MM_TILE0 * 8), 64);
        mm_dirty = 0;
    }
    /* Slide staging/commit happen here (in vblank) so the screen never blanks. */
    if (slide_pending == 1) {                 /* stage adjacent section, enlarge map */
        u8 vertical = (slide_dir == DIR_UP || slide_dir == DIR_DOWN);
        /* RIGHT/DOWN: new section sits in screen1 (right/bottom), old stays in
         * screen0. LEFT/UP: new -> screen0, old -> screen1. (screen1 is at
         * VRAM_BG1_MAP2 = base+0x400 words for both SC_64x32 and SC_32x64.) */
        if (slide_dir == DIR_RIGHT || slide_dir == DIR_DOWN) {
            dmaCopyVram((u8 *)bg1map2, VRAM_BG1_MAP2, 0x800);   /* new -> screen1 */
        } else {                              /* LEFT/UP: new -> screen0, old -> screen1 */
            dmaCopyVram((u8 *)bg1map,  VRAM_BG1_MAP2, 0x800);
            dmaCopyVram((u8 *)bg1map2, VRAM_BG1_MAP,  0x800);
        }
        bgSetMapPtr(0, VRAM_BG1_MAP, vertical ? SC_32x64 : SC_64x32);
        dmaCopyVram((u8 *)bg3map, VRAM_BG3_MAP, 0x800);
        slide_pending = 0;
        return;
    }
    if (slide_pending == 2) {                 /* commit: new section -> screen0, single-screen */
        dmaCopyVram((u8 *)bg1map, VRAM_BG1_MAP, 0x800);
        dmaCopyVram((u8 *)bg3map, VRAM_BG3_MAP, 0x800);
        bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
        bgSetScroll(0, 0, 0);
        bgSetScroll(1, bg2_cur_x, bg2_cur_y);   /* settle BG2 at the new section phase */
        slide_pending = 0;
        return;
    }
    dmaCopyVram((u8 *)bg1map, VRAM_BG1_MAP, 0x800);   /* 1024 entries * 2 bytes */
    dmaCopyVram((u8 *)bg3map, VRAM_BG3_MAP, 0x800);   /* HUD/text layer        */
}

/* Build an arbitrary section's playfield metatiles into a tilemap buffer. */
static void render_build_section_to(u16 *buf, u8 sr, u8 sc) {
    u8 idx = world_section_index(sr, sc);
    u8 base = (u8)(PLAYFIELD_OFFSET_Y >> 3);
    u8 gx, gy;
    u16 i;
    for (i = base * 32; i < 32 * 32; i++) buf[i] = 0;
    for (gy = 0; gy < GRID_ROWS; gy++)
        for (gx = 0; gx < GRID_COLS; gx++) {
            u8 t  = game.sections[idx][gy][gx];
            u8 mt = metatile_for(t, game.damage[idx][gy][gx]);
            u16 b = (u16)(mt * 4), pb = mt_palbits[mt];
            u16 col = (u16)(gx * 2), row = (u16)(base + gy * 2);
            buf[row * 32 + col]           = b | pb;
            buf[row * 32 + col + 1]       = (u16)(b + 1) | pb;
            buf[(row + 1) * 32 + col]     = (u16)(b + 2) | pb;
            buf[(row + 1) * 32 + col + 1] = (u16)(b + 3) | pb;
        }
}

/* ---- section slide (BG1 playfield + BG2 texture scroll), horizontal OR vertical
 * The old section stays in BG1 screen 0; the new section is staged in screen 1
 * (a 64-wide map for L/R, 64-tall for U/D), and the camera (BG1+BG2 scroll on
 * the slide axis) pans across. BG3 (HUD) stays fixed. The seamless texture wraps
 * (256 is a multiple of its 128px period, so resetting scroll afterwards never
 * jumps), so BG2 just scrolls. Staging happens in render_flush_map (vblank) so
 * there's no screen blank. slide_stage_x holds the new section's origin on the
 * slide axis (256 for RIGHT/DOWN where it enters from the far side, else 0). */
void render_slide_begin(u8 dir, u8 adj_row, u8 adj_col) {
    render_build_section_to(bg1map2, adj_row, adj_col);   /* new section -> screen1 buffer */
    slide_dir = dir;
    slide_stage_x = (dir == DIR_RIGHT || dir == DIR_DOWN) ? 256 : 0;
    /* BG2 shifts by one phase step toward the new section (parallax + variety). */
    slide_bg2_ox = bg2_cur_x; slide_bg2_oy = bg2_cur_y;
    slide_bg2_dx = (dir == DIR_RIGHT) ? BG2_PHASE_STEP : (dir == DIR_LEFT) ? -BG2_PHASE_STEP : 0;
    slide_bg2_dy = (dir == DIR_DOWN)  ? BG2_PHASE_STEP : (dir == DIR_UP)   ? -BG2_PHASE_STEP : 0;
    slide_pending = 1;                     /* render_flush_map stages it next vblank */
}

void render_slide_scroll(u16 cam) {
    /* BG2 progresses 0..256 over the whole slide regardless of direction. */
    u16 p = (slide_dir == DIR_LEFT || slide_dir == DIR_UP) ? (u16)(256 - cam) : cam;
    scr_bg2x = (u16)(slide_bg2_ox + (s16)(((s32)slide_bg2_dx * p) / 256));
    scr_bg2y = (u16)(slide_bg2_oy + (s16)(((s32)slide_bg2_dy * p) / 256));
    if (slide_dir == DIR_UP || slide_dir == DIR_DOWN) { scr_bg1x = 0;   scr_bg1y = cam; }
    else                                              { scr_bg1x = cam; scr_bg1y = 0;   }
    scroll_dirty = 1;   /* registers written in vblank (render_apply_scroll) -> no tearing */
}

/* Apply the shadowed scroll to the PPU registers. MUST be called in vblank
 * (right after WaitForVBlank) so the scroll never changes mid-frame. */
void render_apply_scroll(void) {
    if (!scroll_dirty) return;
    bgSetScroll(0, scr_bg1x, scr_bg1y);
    bgSetScroll(1, scr_bg2x, scr_bg2y);
    scroll_dirty = 0;
}

/* Draw the player at its new-section entry position relative to the camera. */
void render_slide_player(u16 cam) {
    Player *p = &game.player;
    s16 psx, psy;
    u16 col;
    if (slide_dir == DIR_UP || slide_dir == DIR_DOWN) {
        psx = (s16)(PLAYFIELD_OFFSET_X + game.trans_entry_x * TILE_SIZE);
        psy = (s16)(slide_stage_x + PLAYFIELD_OFFSET_Y + game.trans_entry_y * TILE_SIZE - cam);
        if (psy < PLAYFIELD_OFFSET_Y || psy > 223) { oamSetVisible(0, OBJ_HIDE); return; }
    } else {
        psx = (s16)(slide_stage_x + game.trans_entry_x * TILE_SIZE - cam);
        psy = (s16)(PLAYFIELD_OFFSET_Y + game.trans_entry_y * TILE_SIZE);
        if (psx < 0 || psx > 255) { oamSetVisible(0, OBJ_HIDE); return; }   /* off-screen while entering */
    }
    switch (p->direction) {
        case DIR_LEFT:  col = 3; break;
        case DIR_RIGHT: col = 4; break;
        case DIR_UP:    col = 5; break;
        default:        col = 6; break;
    }
    oamSet(0, (u16)psx, (u16)psy,
           3, 0, 0, (u16)(p->anim_frame * 32 + col * 2), OBJPAL_PLAYER);
    oamSetEx(0, OBJ_SMALL, OBJ_SHOW);
}

void render_slide_end(void) {
    bg2_cur_x = (u16)(slide_bg2_ox + slide_bg2_dx);   /* new section's BG2 phase */
    bg2_cur_y = (u16)(slide_bg2_oy + slide_bg2_dy);
    slide_pending = 2;   /* render_flush_map commits (screen0 + single-screen) next vblank */
}

/* Current level's BG2 texture (set by bgtex_select on each load). */
static u8  *cbg_pic, *cbg_picend, *cbg_pal;
static u16 *cbg_map;

/* Point the current-texture pointers at level N's bgtex (default: level 1, also
 * used for the title where game.current_level is still 0). */
static void bgtex_select(u8 level) {
    switch (level) {
        case 2:  cbg_pic=(u8*)&bgtex_2_pic;  cbg_picend=(u8*)&bgtex_2_picend;  cbg_map=(u16*)&bgtex_2_map;  cbg_pal=(u8*)&bgtex_2_pal;  break;
        case 3:  cbg_pic=(u8*)&bgtex_3_pic;  cbg_picend=(u8*)&bgtex_3_picend;  cbg_map=(u16*)&bgtex_3_map;  cbg_pal=(u8*)&bgtex_3_pal;  break;
        case 4:  cbg_pic=(u8*)&bgtex_4_pic;  cbg_picend=(u8*)&bgtex_4_picend;  cbg_map=(u16*)&bgtex_4_map;  cbg_pal=(u8*)&bgtex_4_pal;  break;
        case 5:  cbg_pic=(u8*)&bgtex_5_pic;  cbg_picend=(u8*)&bgtex_5_picend;  cbg_map=(u16*)&bgtex_5_map;  cbg_pal=(u8*)&bgtex_5_pal;  break;
        case 6:  cbg_pic=(u8*)&bgtex_6_pic;  cbg_picend=(u8*)&bgtex_6_picend;  cbg_map=(u16*)&bgtex_6_map;  cbg_pal=(u8*)&bgtex_6_pal;  break;
        case 7:  cbg_pic=(u8*)&bgtex_7_pic;  cbg_picend=(u8*)&bgtex_7_picend;  cbg_map=(u16*)&bgtex_7_map;  cbg_pal=(u8*)&bgtex_7_pal;  break;
        case 8:  cbg_pic=(u8*)&bgtex_8_pic;  cbg_picend=(u8*)&bgtex_8_picend;  cbg_map=(u16*)&bgtex_8_map;  cbg_pal=(u8*)&bgtex_8_pal;  break;
        case 9:  cbg_pic=(u8*)&bgtex_9_pic;  cbg_picend=(u8*)&bgtex_9_picend;  cbg_map=(u16*)&bgtex_9_map;  cbg_pal=(u8*)&bgtex_9_pal;  break;
        case 10: cbg_pic=(u8*)&bgtex_10_pic; cbg_picend=(u8*)&bgtex_10_picend; cbg_map=(u16*)&bgtex_10_map; cbg_pal=(u8*)&bgtex_10_pal; break;
        default: cbg_pic=(u8*)&bgtex_1_pic;  cbg_picend=(u8*)&bgtex_1_picend;  cbg_map=(u16*)&bgtex_1_map;  cbg_pal=(u8*)&bgtex_1_pal;  break;
    }
}

/* Load the current level's background texture into VRAM/CGRAM (caller
 * force-blanked for the ~32KB tile DMA). */
static void render_load_tileset(void) {
    bgtex_select(game.current_level);
    dmaCopyVram(cbg_pic, VRAM_BG2_TILES, (u16)(cbg_picend - cbg_pic));
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    setPalette(cbg_pal, BG2_PAL * 16, 96 * 2);   /* CGRAM 32..127 */
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
}

/* Load the current level's gameplay tileset (gem/boulder/block art + the two
 * sub-palettes) into VRAM_BG1_TILES / CGRAM 0-31. Caller must be force-blanked.
 * pal 1's reserved 0/1/2 = transparent/white/black keep the BG3 HUD intact. */
void render_load_gameplay_tiles(u8 level) {
    u8 *pic, *picend, *pal;
    switch (level) {
        case 2:  pic=(u8*)&bg_tiles_2_pic;  picend=(u8*)&bg_tiles_2_picend;  pal=(u8*)&bg_tiles_2_pal;  break;
        case 3:  pic=(u8*)&bg_tiles_3_pic;  picend=(u8*)&bg_tiles_3_picend;  pal=(u8*)&bg_tiles_3_pal;  break;
        case 4:  pic=(u8*)&bg_tiles_4_pic;  picend=(u8*)&bg_tiles_4_picend;  pal=(u8*)&bg_tiles_4_pal;  break;
        case 5:  pic=(u8*)&bg_tiles_5_pic;  picend=(u8*)&bg_tiles_5_picend;  pal=(u8*)&bg_tiles_5_pal;  break;
        case 6:  pic=(u8*)&bg_tiles_6_pic;  picend=(u8*)&bg_tiles_6_picend;  pal=(u8*)&bg_tiles_6_pal;  break;
        case 7:  pic=(u8*)&bg_tiles_7_pic;  picend=(u8*)&bg_tiles_7_picend;  pal=(u8*)&bg_tiles_7_pal;  break;
        case 8:  pic=(u8*)&bg_tiles_8_pic;  picend=(u8*)&bg_tiles_8_picend;  pal=(u8*)&bg_tiles_8_pal;  break;
        case 9:  pic=(u8*)&bg_tiles_9_pic;  picend=(u8*)&bg_tiles_9_picend;  pal=(u8*)&bg_tiles_9_pal;  break;
        case 10: pic=(u8*)&bg_tiles_10_pic; picend=(u8*)&bg_tiles_10_picend; pal=(u8*)&bg_tiles_10_pal; break;
        default: pic=(u8*)&bg_tiles_1_pic;  picend=(u8*)&bg_tiles_1_picend;  pal=(u8*)&bg_tiles_1_pal;  break;
    }
    dmaCopyVram(pic, VRAM_BG1_TILES, (u16)(picend - pic));
    setPalette(pal, 0, 16 * 2);            /* pal0 -> CGRAM 0-15  */
    setPalette(pal + 32, 16, 16 * 2);      /* pal1 -> CGRAM 16-31 */
    /* Reclaim CGRAM 19 (a minor ~90px marker accent) as the minimap's GREY so the
     * minimap can show white(you)/grey(gems) in the same BG3 sub-palette as the
     * white/black HUD text. Re-applied here because the pal1 load above set it. */
    setPalette((u8 *)mm_grey_cgram, 19, 2);
}

/* Fill the WHOLE BG2 map with the seamless texture tiled. Must cover all 32
 * rows (not just the playfield band): during a vertical slide BG2 scrolls, and
 * any unfilled rows would wrap black bands into view. BGTEX_H=16 divides 32, so
 * the texture wraps cleanly. Uniform across sections, so it just scrolls. */
void render_set_background(void) {
    u16 *tex = cbg_map;                  /* current level's 32x32 texture map */
    u8 ty, tx;
    for (ty = 0; ty < 32; ty++)
        for (tx = 0; tx < 32; tx++)
            bg2map[ty * 32 + tx] = tex[(ty % BGTEX_H) * BGTEX_W + (tx % BGTEX_W)];
    dmaCopyVram((u8 *)bg2map, VRAM_BG2_MAP, 0x800);
    bgSetScroll(1, bg2_cur_x, bg2_cur_y);   /* current section's phase window */
}

/* Reset the BG2 phase to the section-0 window (on level load / respawn). */
void render_bg2_reset(void) {
    bg2_cur_x = 0; bg2_cur_y = 0;
    bgSetScroll(1, 0, 0);
}

/* Level load: force-blank, load this level's gameplay tiles + background. */
void render_load_background(void) {
    setScreenOff();
    render_load_gameplay_tiles(game.current_level);   /* per-level gem/boulder/block */
    render_load_tileset();
    render_set_background();
    /* Re-assert the single-screen, grid-aligned BG1 view -- exactly what the
     * section-slide commit (render_flush_map slide_pending==2) does. A freshly
     * loaded section must look identical to one arrived at via a slide; without
     * this the first section after a cold boot can render the playfield shifted
     * off the player's grid until you slide once. (Relying on render_init's
     * one-time setup isn't enough on the real hardware path -- the slide proves
     * these two writes are what align it, so we do them on every load too.) */
    slide_pending = 0;
    scroll_dirty  = 0;
    scr_bg1x = 0; scr_bg1y = 0;
    scr_bg2x = bg2_cur_x; scr_bg2y = bg2_cur_y;
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, bg2_cur_x, bg2_cur_y);
    render_flush_map();
    setScreenOn();
}

/* Title screen: put the DEADFALL logo image on BG2 (no scroll/parallax). BG1 is
 * left empty (caller cleared it) and BG3 carries "PRESS START". Force-blanked for
 * the tile DMA; the next level load swaps BG2 back to the playfield background. */
void render_show_title(void) {
    u16 *src = (u16 *)&title_map;
    u16 i;
    setScreenOff();
    dmaCopyVram((u8 *)&title_pic, VRAM_BG2_TILES, (u16)(&title_picend - &title_pic));
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    setPalette((u8 *)&title_pal, BG2_PAL * 16, 96 * 2);   /* CGRAM 32..127 */
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
    for (i = 0; i < 32 * 32; i++) bg2map[i] = src[i];     /* 1:1, not tiled */
    dmaCopyVram((u8 *)bg2map, VRAM_BG2_MAP, 0x800);
    bg2_cur_x = 0; bg2_cur_y = 0;
    scr_bg1x = 0; scr_bg1y = 0; scr_bg2x = 0; scr_bg2y = 0; scroll_dirty = 0;
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    render_flush_map();                                   /* push BG1(empty)+BG3(text) */
    setScreenOn();
}

void render_init(void) {
    consoleInit();          /* sets up NMI/vblank, auto-joypad read, etc. */
    setBrightness(0);
    WaitForVBlank();

    oamInit();
    /* Gameplay tiles are per-level (gem/boulder/block differ). bgInitTileSet
     * establishes the BG1 gfx ptr + 16-color mode with level 1's tiles for the
     * title; render_load_gameplay_tiles() swaps tiles+palettes on each level
     * load. Two BG sub-palettes: pal 0 (block/boulder/gem) at CGRAM 0-15, pal 1
     * (portal/spawn/extra-life/robot-spawn) at CGRAM 16-31. The BG3 HUD shares
     * CGRAM 16-18 (transparent/white/black); pal 1 reserves those exact slots,
     * and the HUD palette is re-asserted below, so they coexist. */
    bgInitTileSet(0, (u8 *)&bg_tiles_1_pic, (u8 *)&bg_tiles_1_pal, 0,
                  (u16)(&bg_tiles_1_picend - &bg_tiles_1_pic), 16 * 2,
                  BG_16COLORS, VRAM_BG1_TILES);
    setPalette((u8 *)(&bg_tiles_1_pal) + 32, 16, 16 * 2);   /* pal1 -> CGRAM 16-31 */
    oamInitGfxSet((u8 *)&spr_player_pic, (u16)(&spr_player_picend - &spr_player_pic),
                  (u8 *)&spr_player_pal, 16 * 2, OBJPAL_PLAYER, VRAM_OBJ_TILES, OBJ_SIZE16_L32);
    /* enemy tiles share the same OBJ base, loaded just after the player tiles */
    dmaCopyVram((u8 *)&spr_enemy_pic, VRAM_OBJ_ENEMY, (u16)(&spr_enemy_picend - &spr_enemy_pic));
    setPalette((u8 *)&spr_enemy_pal, 128 + OBJPAL_ENEMY * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_robot_pic, VRAM_OBJ_ROBOT, (u16)(&spr_robot_picend - &spr_robot_pic));
    setPalette((u8 *)&spr_robot_pal, 128 + OBJPAL_ROBOT * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_zap_h_pic, VRAM_OBJ_ZAPH, (u16)(&spr_zap_h_picend - &spr_zap_h_pic));
    setPalette((u8 *)&spr_zap_h_pal, 128 + OBJPAL_ZAPH * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_zap_v_pic, VRAM_OBJ_ZAPV, (u16)(&spr_zap_v_picend - &spr_zap_v_pic));
    setPalette((u8 *)&spr_zap_v_pal, 128 + OBJPAL_ZAPV * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_pdeath_pic, VRAM_OBJ_PDEATH, (u16)(&spr_pdeath_picend - &spr_pdeath_pic));
    setPalette((u8 *)&spr_pdeath_pal, 128 + OBJPAL_PDEATH * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_edeath_pic, VRAM_OBJ_EDEATH, (u16)(&spr_edeath_picend - &spr_edeath_pic));
    setPalette((u8 *)&spr_edeath_pal, 128 + OBJPAL_EDEATH * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_falls_pic, VRAM_OBJ_FALLS, (u16)(&spr_falls_picend - &spr_falls_pic));
    setPalette((u8 *)&spr_falls_pal, 128 + OBJPAL_FALLS * 16, 16 * 2);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);

    /* BG2 = per-level shared background (behind the playfield). Load level 1's
     * tileset + section window for the title; load_level() swaps per level. */
    render_load_tileset();
    render_set_background();
    bgSetScroll(0, 0, 0);

    /* BG3 = fixed HUD/text layer (2bpp). Stays at scroll 0 while BG1/BG2 scroll
     * during section slides. High priority so text sits above everything. */
    dmaCopyVram((u8 *)&hud_font2_pic, VRAM_BG3_TILES, (u16)(&hud_font2_picend - &hud_font2_pic));
    /* BG3 2bpp sub-palette 4 (CGRAM 16-19): index1 white, index2 black so the
     * (now opaque) HUD font draws white text on a solid black bar. */
    setPalette((u8 *)&hud_font2_pal, BG3_TEXT_PAL * 4, 4 * 2);
    /* Custom life-icon glyph (BG3 tile 64): a white heart on the opaque-black bar
     * (= the original's life icon next to the lives count). 2bpp, row-interleaved
     * (even byte=plane0, odd=plane1); plane1=~plane0 makes the background index2
     * (black) like the rest of the opaque font. */
    dmaCopyVram((u8 *)hud_life_tile, (u16)(VRAM_BG3_TILES + HUD_ICON_LIFE * 8), 16);
    bgSetGfxPtr(2, VRAM_BG3_TILES);
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);
    bgSetScroll(2, 0, 0);

    setMode(BG_MODE1, 0);
    REG_BGMODE = 0x09;       /* mode 1 + BG3 high priority */

    /* Main screen = BG1 + BG2 + BG3 + OBJ; subscreen off. */
    videoMode = 0x17;        /* TM: BG1|BG2|BG3|OBJ */
    videoModeSub = 0x00;     /* TS: nothing */
    REG_TM = 0x17;
    *((vuint8 *)0x212D) = 0x00;   /* REG_TS */

    render_clear_screen();       /* clear VRAM tilemap while in forced blank */
    render_flush_map();
    setScreenOn();
}

void render_player(void) {
    Player *p = &game.player;
    u16 sx, sy, gfx;
    u8 col;

    sx = (u16)(PLAYFIELD_OFFSET_X + p->pixel_x);
    sy = (u16)(PLAYFIELD_OFFSET_Y + p->pixel_y);

    if (game.death_pending) {         /* death animation plays at the death spot */
        u8 fr = (u8)((DEATH_ANIM_FRAMES * DEATH_ANIM_COUNT - game.death_timer) / DEATH_ANIM_FRAMES);
        if (fr >= DEATH_ANIM_COUNT) fr = (u8)(DEATH_ANIM_COUNT - 1);
        oamSet(0, sx, sy, 3, 0, 0, (u16)(OBJN_PDEATH + fr * 2), OBJPAL_PDEATH);
        oamSetEx(0, OBJ_SMALL, OBJ_SHOW);
        return;
    }
    if (!p->alive) { oamSetVisible(0, OBJ_HIDE); return; }

    switch (p->direction) {           /* sheet columns: L3 R4 U5 D6 */
        case DIR_LEFT:  col = 3; break;
        case DIR_RIGHT: col = 4; break;
        case DIR_UP:    col = 5; break;
        default:        col = 6; break;   /* down */
    }
    gfx = (u16)(p->anim_frame * 32 + col * 2);   /* 128-wide OBJ layout */

    oamSet(0, sx, sy, 3, 0, 0, gfx, OBJPAL_PLAYER);
    oamSetEx(0, OBJ_SMALL, OBJ_SHOW);
}

/* Draw one falling gem/boulder/extra-life as a 16x16 OBJ at screen pixel (px,py). */
void render_fall(u8 slot, u8 type, u16 px, u16 py) {
    u16 name = (type == TILE_BOULDER)    ? OBJN_BOULDER_FALL :
               (type == TILE_EXTRA_LIFE) ? OBJN_ELIFE_FALL   : OBJN_GEM_FALL;
    u16 id = (u16)((OAM_FALL_BASE + slot) * 4);   /* oam id is sprite*4 (byte offset) */
    oamSet(id, px, py, 3, 0, 0, name, OBJPAL_FALLS);
    oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
}

void render_falls_hide(void) {
    u8 i;
    for (i = 0; i < MAX_FALL_ANIMS; i++)
        oamSetVisible((u16)((OAM_FALL_BASE + i) * 4), OBJ_HIDE);
}

void render_enemies(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        u16 id = (u16)((OAM_ENEMY_BASE + i) * 4);
        Enemy *e = &game.enemies[i];
        u8 here = (u8)(i < game.enemy_count &&
                       e->section_row == game.cur_row && e->section_col == game.cur_col);
        if (here && e->alive) {
            u16 sx = (u16)(PLAYFIELD_OFFSET_X + e->pixel_x);
            u16 sy = (u16)(PLAYFIELD_OFFSET_Y + e->pixel_y);
            u16 gfx = (u16)(OBJN_ENEMY + e->anim_frame * 32 + e->direction * 2);
            oamSet(id, sx, sy, 3, 0, 0, gfx, OBJPAL_ENEMY);
            oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
        } else if (here && e->dying) {
            u16 sx = (u16)(PLAYFIELD_OFFSET_X + e->pixel_x);
            u16 sy = (u16)(PLAYFIELD_OFFSET_Y + e->pixel_y);
            u8 fr = (u8)((DEATH_ANIM_FRAMES * DEATH_ANIM_COUNT - e->death_timer) / DEATH_ANIM_FRAMES);
            if (fr >= DEATH_ANIM_COUNT) fr = (u8)(DEATH_ANIM_COUNT - 1);
            oamSet(id, sx, sy, 3, 0, 0, (u16)(OBJN_EDEATH + fr * 2), OBJPAL_EDEATH);
            oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
        } else {
            oamSetVisible(id, OBJ_HIDE);
        }
    }
}

void render_robot(void) {
    static const u8 eyeseq[4] = {0, 1, 2, 1};   /* pulse sequence -> sprite row */
    Robot *r = &game.robots[0];
    u16 id = (u16)(OAM_ROBOT * 4);
    if (game.robot_count > 0 && r->alive &&
        r->section_row == game.cur_row && r->section_col == game.cur_col) {
        u16 sx = (u16)(PLAYFIELD_OFFSET_X + r->pixel_x);
        u16 sy = (u16)(PLAYFIELD_OFFSET_Y + r->pixel_y);
        u16 gfx = (u16)(OBJN_ROBOT + eyeseq[r->eye_index & 3] * 32 + r->direction * 2);
        oamSet(id, sx, sy, 3, 0, 0, gfx, OBJPAL_ROBOT);
        oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
    } else {
        oamSetVisible(id, OBJ_HIDE);
    }
}

void render_lightning(void) {
    Robot *r = &game.robots[0];
    u8 k, vertical, pal;
    s8 dx, dy;
    u16 gfx;

    for (k = 0; k < ROBOT_ZAP_RANGE; k++)
        oamSetVisible((u16)((OAM_ZAP_BASE + k) * 4), OBJ_HIDE);

    if (game.robot_count == 0 || !r->is_zapping) return;
    if (r->section_row != game.cur_row || r->section_col != game.cur_col) return;

    dx = (r->zap_direction == DIR_LEFT) ? -1 : (r->zap_direction == DIR_RIGHT) ? 1 : 0;
    dy = (r->zap_direction == DIR_UP)   ? -1 : (r->zap_direction == DIR_DOWN)  ? 1 : 0;
    vertical = (u8)(r->zap_direction == DIR_UP || r->zap_direction == DIR_DOWN);
    pal = vertical ? OBJPAL_ZAPV : OBJPAL_ZAPH;

    for (k = 0; k < r->zap_distance && k < ROBOT_ZAP_RANGE; k++) {
        u16 base = vertical ? OBJN_ZAPV : OBJN_ZAPH;
        u8  L = (u8)(r->zap_anim_frame * 3 + k);          /* frame*3 + segment */
        s16 bx = (s16)(r->pixel_x + dx * TILE_SIZE * (k + 1));
        s16 by = (s16)(r->pixel_y + dy * TILE_SIZE * (k + 1));
        u16 id = (u16)((OAM_ZAP_BASE + k) * 4);
        gfx = (u16)(base + (L >> 3) * 32 + (L & 7) * 2);  /* unpack tile index */
        oamSet(id, (u16)(PLAYFIELD_OFFSET_X + bx), (u16)(PLAYFIELD_OFFSET_Y + by),
               2, 0, 0, gfx, pal);
        oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
    }
}

/* ---- HUD on BG3 (the 16px top bar + scene text) ---- */
static void hud_putc(u8 x, u8 y, char c) {
    if (c < 32 || c > 95) c = 32;
    /* BG3 2bpp: tile = ascii-32, sub-palette 4 (white@CGRAM17). Bit13 (0x2000)
     * = per-tile priority: with REG_BGMODE bit3 set this lifts the glyph ABOVE
     * BG1/BG2 (without it, BG3 sits at the very back, behind the opaque BG2). */
    bg3map[y * 32 + x] = (u16)(c - 32) | (BG3_TEXT_PAL << 10) | 0x2000;
}
static void hud_puts(u8 x, u8 y, const char *s) {
    while (*s) hud_putc(x++, y, *s++);
}
static void hud_putnum(u8 x, u8 y, u32 val, u8 digits) {
    u8 i;
    for (i = 0; i < digits; i++) {
        hud_putc((u8)(x + digits - 1 - i), y, (char)('0' + (u8)(val % 10)));
        val /= 10;
    }
}

/* Returns TRUE if the HUD changed (and was redrawn into bg1map rows 0-1).
 * Only reformats on change so the (software, divide-less 65816) number
 * formatting and the map DMA don't run every frame. */
u8 render_hud(void) {
    static u32 ls = 0xFFFFFFFF;
    static u16 lg = 0xFFFF, lt = 0xFFFF;
    static u8  ll = 0xFF, lv = 0xFF, lp = 0xFF;
    u8 i;

    if (game.score == ls && game.gems_collected == lg && game.total_gems == lt &&
        game.lives == ll && game.current_level == lv && game.portal_active == lp)
        return FALSE;
    ls = game.score; lg = game.gems_collected; lt = game.total_gems;
    ll = game.lives; lv = game.current_level; lp = game.portal_active;

    /* Fill HUD rows 0-1 cols 0..29 with the opaque black space tile (tile 0 =
     * solid index2) at high priority -> a solid black strip over the texture.
     * Cols 30-31 are owned by the minimap (render_minimap), so leave them. */
    for (i = 0; i < MM_COL; i++) {
        u16 blk = (u16)((BG3_TEXT_PAL << 10) | 0x2000);
        bg3map[i] = blk; bg3map[32 + i] = blk;
    }

    /* Single line laid out like the original: SCORE:000000 GEM:c/t [heart]:lives.
     * Drawn on ROW 1 (the lower half of the 16px bar) so the emulator's top
     * overscan can't clip it (row 0 stays blank black padding). */
    hud_puts(1, 1, "SCORE:");  hud_putnum(7, 1, game.score, 6);
    hud_puts(14, 1, "GEM:");   hud_putnum(18, 1, game.gems_collected, 2);
    hud_putc(20, 1, '/');      hud_putnum(21, 1, game.total_gems, 2);
    /* life icon (custom heart glyph, tile 64) + ':' + lives count */
    bg3map[32 + 24] = (u16)(HUD_ICON_LIFE) | (BG3_TEXT_PAL << 10) | 0x2000;
    hud_putc(25, 1, ':');      hud_putnum(26, 1, game.lives, 1);
    return TRUE;
}

/* Stamp a 3x3 section square (solid, or a hollow ring) at minimap pixel (sx,sy).
 * sq[] = every opaque square pixel (-> plane0); wh[] = only the WHITE pixels (the
 * current/exit square) so plane1 = ~wh distinguishes white(idx1) from grey(idx3).
 * Each row is a u16, bit (15-x) = pixel x. */
static void mm_square(u16 *sq, u16 *wh, u8 sx, u8 sy, u8 ring, u8 white) {
    u8 dx, dy;
    for (dy = 0; dy < 3; dy++)
        for (dx = 0; dx < 3; dx++) {
            u16 bit;
            if (ring && dx == 1 && dy == 1) continue;   /* hollow centre */
            bit = (u16)(0x8000 >> (sx + dx));
            sq[sy + dy] |= bit;
            if (white) wh[sy + dy] |= bit;
        }
}

/* Rebuild the minimap when its state changes; refresh one section's gem flag per
 * call (round-robin) so the per-section scan stays cheap. Called every frame from
 * game_update. Sets mm_dirty (consumed by render_flush_map) + game_map_dirty when
 * the picture actually changed. */
void render_minimap(void) {
    u8  nsec = (u8)(game.world_rows * game.world_cols);
    u8  cur  = world_section_index(game.cur_row, game.cur_col);
    u8  por  = world_section_index(game.portal_row, game.portal_col);
    u8  blink_on = (u8)(game.portal_active && ((mm_blink >> 4) & 1));   /* ~16-frame phase */
    u16 sq[16], wh[16];     /* sq = any square pixel (plane0); wh = white pixels only */
    u32 sig;
    u8  r, c, idx, ox, oy, ry, tile, tx, ty;

    mm_blink++;

    /* round-robin: scan one section's grid for any remaining gem (linear, cheap) */
    if (nsec) {
        u8 s = (u8)(mm_scan % nsec);
        u8 *g = &game.sections[s][0][0];
        u8 has = 0, k;
        for (k = 0; k < SECTION_TILES; k++) if (g[k] == TILE_GEM) { has = 1; break; }
        mm_has_gems[s] = has;
        mm_scan = (u8)(s + 1);
    }

    /* signature = gem bitmap + current + portal-blink phase; skip if unchanged */
    sig = 0;
    for (r = 0; r < game.world_rows; r++)
        for (c = 0; c < game.world_cols; c++)
            if (mm_has_gems[world_section_index(r, c)]) sig |= (u32)(1UL << (r * 4 + c));
    sig = (sig << 8) | ((u32)cur << 2) | ((u32)blink_on << 1) | (game.portal_active ? 1 : 0);
    if (sig == mm_sig) return;
    mm_sig = sig;

    /* draw the grid, centred in the 16x16 area (smaller worlds dodge overscan):
     *   current section = WHITE solid (you are here)
     *   section with gems left = GREY solid
     *   cleared in-world section = GREY hollow ring (shows the world layout)
     *   exit section while the alarm is up = WHITE solid, blinking */
    for (ry = 0; ry < 16; ry++) { sq[ry] = 0; wh[ry] = 0; }
    ox = (u8)((16 - game.world_cols * MM_CELL) / 2);
    oy = (u8)((16 - game.world_rows * MM_CELL) / 2);
    for (r = 0; r < game.world_rows; r++)
        for (c = 0; c < game.world_cols; c++) {
            u8 sx = (u8)(ox + c * MM_CELL), sy = (u8)(oy + r * MM_CELL);
            idx = world_section_index(r, c);
            if (game.portal_active && idx == por) {        /* exit: white blink */
                if (blink_on) mm_square(sq, wh, sx, sy, 0, 1);
            } else if (idx == cur) {                        /* you are here: white */
                mm_square(sq, wh, sx, sy, 0, 1);
            } else if (mm_has_gems[idx]) {                  /* gems remain: grey solid */
                mm_square(sq, wh, sx, sy, 0, 0);
            } else {                                         /* cleared: grey ring */
                mm_square(sq, wh, sx, sy, 1, 0);
            }
        }

    /* pack the 16x16 into 4 BG3 tiles (TL,TR,BL,BR): plane0 = square pixels,
     * plane1 = ~white -> idx1 white, idx3 grey (CGRAM19), idx2 black background. */
    for (ty = 0; ty < 2; ty++)
        for (tx = 0; tx < 2; tx++) {
            tile = (u8)(ty * 2 + tx);
            for (ry = 0; ry < 8; ry++) {
                u8 p0 = (u8)((sq[ty * 8 + ry] >> (tx ? 0 : 8)) & 0xFF);
                u8 p1 = (u8)((wh[ty * 8 + ry] >> (tx ? 0 : 8)) & 0xFF);
                mm_tiles[tile * 16 + ry * 2]     = p0;
                mm_tiles[tile * 16 + ry * 2 + 1] = (u8)~p1;
            }
        }

    /* place the 4 tile refs (TL,TR,BL,BR) at rows 0-1, cols 30-31 */
    bg3map[0 * 32 + MM_COL]     = (u16)(MM_TILE0)     | (BG3_TEXT_PAL << 10) | 0x2000;
    bg3map[0 * 32 + MM_COL + 1] = (u16)(MM_TILE0 + 1) | (BG3_TEXT_PAL << 10) | 0x2000;
    bg3map[1 * 32 + MM_COL]     = (u16)(MM_TILE0 + 2) | (BG3_TEXT_PAL << 10) | 0x2000;
    bg3map[1 * 32 + MM_COL + 1] = (u16)(MM_TILE0 + 3) | (BG3_TEXT_PAL << 10) | 0x2000;
    mm_dirty = 1;
    game_map_dirty = 1;
}

/* Drop the minimap's cached state so it rebuilds on the next frame (level load /
 * respawn). Assume every section has gems until the round-robin scan corrects it. */
void render_minimap_reset(void) {
    u8 i;
    for (i = 0; i < MAX_SECTIONS; i++) mm_has_gems[i] = 1;
    mm_scan = 0; mm_blink = 0; mm_sig = 0xFFFFFFFF;
}

/* ---- text scenes (title / game over / victory) on BG1 ---- */
void render_clear_screen(void) {
    u16 i;
    /* BG3 clears to the opaque BLACK space tile (tile 0, BG3 text sub-palette,
     * NO priority bit -> sits BEHIND BG1/BG2). This gives the title screen a
     * black backing so the title image's transparent (index-0) areas read as
     * black. During gameplay/other scenes BG2 is opaque, so this stays hidden. */
    u16 blk = (u16)(BG3_TEXT_PAL << 10);
    for (i = 0; i < 32 * 32; i++) { bg1map[i] = 0; bg3map[i] = blk; }
}

void render_text(u8 x, u8 y, const char *s) {
    while (*s) { hud_putc(x++, y, *s++); }
}

void render_num(u8 x, u8 y, u32 val, u8 digits) {
    hud_putnum(x, y, val, digits);
}

void render_hide_sprites(void) {
    u8 i;
    for (i = 0; i < 16; i++) oamSetVisible((u16)(i * 4), OBJ_HIDE);
}
