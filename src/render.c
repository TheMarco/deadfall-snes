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
extern char spr_falls_1_pic, spr_falls_1_picend, spr_falls_1_pal;   /* per-level falling gem/boulder/1-up */
extern char spr_falls_2_pic, spr_falls_2_picend, spr_falls_2_pal;
extern char spr_falls_3_pic, spr_falls_3_picend, spr_falls_3_pal;
extern char spr_falls_4_pic, spr_falls_4_picend, spr_falls_4_pal;
extern char spr_falls_5_pic, spr_falls_5_picend, spr_falls_5_pal;
extern char spr_falls_6_pic, spr_falls_6_picend, spr_falls_6_pal;
extern char spr_falls_7_pic, spr_falls_7_picend, spr_falls_7_pal;
extern char spr_falls_8_pic, spr_falls_8_picend, spr_falls_8_pal;
extern char spr_falls_9_pic, spr_falls_9_picend, spr_falls_9_pal;
extern char spr_falls_10_pic, spr_falls_10_picend, spr_falls_10_pal;
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
extern char logo_pic, logo_picend, logo_map, logo_pal;       /* boot studio-logo BG2 image (512-tall) */
extern char kitty_1_pic, kitty_1_picend, kitty_1_map, kitty_1_pal;   /* level-complete kitties 1-9 */
extern char kitty_2_pic, kitty_2_picend, kitty_2_map, kitty_2_pal;
extern char kitty_3_pic, kitty_3_picend, kitty_3_map, kitty_3_pal;
extern char kitty_4_pic, kitty_4_picend, kitty_4_map, kitty_4_pal;
extern char kitty_5_pic, kitty_5_picend, kitty_5_map, kitty_5_pal;
extern char kitty_6_pic, kitty_6_picend, kitty_6_map, kitty_6_pal;
extern char kitty_7_pic, kitty_7_picend, kitty_7_map, kitty_7_pal;
extern char kitty_8_pic, kitty_8_picend, kitty_8_map, kitty_8_pal;
extern char kitty_9_pic, kitty_9_picend, kitty_9_map, kitty_9_pal;
#define LOGO_CANVAS_Y 200  /* BG2 canvas Y of the logo top (must match build_logo.py LOGO_Y) */
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
    0x66, 0x99,   /* .##..##. */
    0xFF, 0x00,   /* ######## */
    0xFF, 0x00,   /* ######## */
    0xFF, 0x00,   /* ######## */
    0x7E, 0x81,   /* .######. */
    0x3C, 0xC3,   /* ..####.. */
    0x18, 0xE7,   /* ...##... */
    0x00, 0xFF    /* ........ */
};
/* A colon glyph shifted ~3px right (dots at x3-4) -> used only for the lives ':'
 * so there's a few px of space between the full-width heart and the colon. */
#define HUD_COLON_TILE 75
static const u8 hud_colon_tile[16] = {
    0x00, 0xFF,   /* ........ */
    0x18, 0xE7,   /* ...##... */
    0x18, 0xE7,   /* ...##... */
    0x00, 0xFF,   /* ........ */
    0x18, 0xE7,   /* ...##... */
    0x18, 0xE7,   /* ...##... */
    0x00, 0xFF,   /* ........ */
    0x00, 0xFF    /* ........ */
};

/* Enemy edge-warning strips (the original draws a red 3px gradient on a threatened
 * playfield edge; red is palette-blocked here so this is a white 3px strip). Two
 * 2bpp tiles: a TOP strip (white rows 0-2) used for up/down (V-flip = down) and a
 * LEFT strip (white cols 0-2) for left/right (H-flip = right). */
static const u8 edge_top_tile[16]  = { 0xFF,0,0xFF,0,0xFF,0, 0,0, 0,0, 0,0, 0,0, 0,0 };
static const u8 edge_left_tile[16] = { 0xE0,0,0xE0,0,0xE0,0,0xE0,0,0xE0,0,0xE0,0,0xE0,0,0xE0,0 };
#define EDGE_TILE_TOP   73     /* past the 8-tile (4x2) minimap at 65..72 */
#define EDGE_TILE_LEFT  74

/* HUD bar's lower edge. The BG3 text layer is nudged up 2px (HUD_BG3_VOFS), which
 * lifts the bar's black off the bottom 2px and would otherwise reveal the BG2
 * texture there. Fix it on BG3 itself: a high-prio tile on bg3map row 2 -- which
 * the 2px scroll places at screen y14..21 -- that is BLACK in its top rows and
 * TRANSPARENT below. The black backs the gap (y14..); the transparent part lets
 * the playfield (y16+) show. Being on the (non-scrolling) BG3 layer it stays put
 * during section slides, so no black strip slides with the playfield. 2bpp,
 * sub-pal 4 index2 = black. Black top 3 rows -> bar bottom at screen y16 (one
 * line below the minimap); transparent rows 3-7 keep the playfield visible. */
#define HUD_HALFBAR_TILE 76
static const u8 hud_halfbar_tile[16] = {
    0x00,0xFF, 0x00,0xFF, 0x00,0xFF, 0,0, 0,0, 0,0, 0,0, 0,0
};

/* ---- HUD minimap (4-COLOUR sprite, top-right corner) ----
 * Drawn as a 16x16 OBJ (not BG3, which is 2bpp = too few colours) so it can show
 * the original's 4 states: black bg, DARK-GREY out-of-world, LIGHT-GREY has-gems,
 * WHITE current, GREEN cleared. A 4x4 grid of 3x3 filled cells. Built into a 4bpp
 * sprite, DMA'd in vblank; OBJ palette 4 freed by sharing the zap palette. */
#define HUD_BG3_VOFS 2                   /* nudge the whole BG3 (HUD + text) layer up 2px */
#define MM_COL    30                     /* bg3map cols 30-31 left transparent for the OBJ */
#define MM_OBJ_X  240                    /* screen x of the 16x16 minimap sprite (top-right) */
#define MM_OBJ_Y  0
#define MM_CELL   3                      /* px per section cell (2px dot + 1px gap) */
#define MM_DOT    2                      /* filled dot size (2x2 = 4px) */
/* minimap OBJ palette: 0 transp, 1 black(bg/gaps), 2 dark-grey(out-of-world),
 * 3 light-grey(gems), 4 white(current), 5 green(cleared). BGR555, little-endian. */
static const u8 mm_obj_pal[32] = {
    0x00,0x00, 0x00,0x00, 0x08,0x21, 0x94,0x52,
    0xFF,0x7F, 0x00,0x03, 0x00,0x00, 0x00,0x00,
    0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,
    0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00
};
static u8  mm_obj_tiles[128];            /* 4 4bpp tiles (TL,TR,BL,BR); DMA'd in vblank */
static u8  mm_px[16][16];                /* 16x16 colour-index build buffer */
static u8  mm_has_gems[MAX_SECTIONS];    /* per-section "gems remain" (round-robin scanned) */
static u8  mm_scan;                      /* round-robin scan cursor */
static u16 mm_blink;                     /* phase counter */
static u8  mm_dirty = 0;                  /* tiles changed -> DMA in render_flush_map (boot-safe) */
static u32 mm_sig = 0xFFFFFFFF;          /* last-drawn signature (skip rebuild if same) */
static u8  mm_last_cur = 0xFF;            /* last current-section index (gate the sig rebuild) */

/* 32x32 tilemap buffers (entry = tile number | palette<<10 | flips). */
static u16 bg1map[32 * 32];
static u16 bg2map[32 * 32];
static u16 bg3map[32 * 32];   /* HUD + scene text (BG3, fixed, high priority) */
static u8  bg3_dirty = 1;     /* bg3map changed -> upload it; else skip the 2KB DMA.
                               * The HUD/text layer is static most frames, so this
                               * halves the per-frame VRAM DMA (4KB->2KB) and keeps
                               * the upload inside vblank (no tearing / no late frame). */
static u16 bg1map2[32 * 32];  /* BG1 screen 1: adjacent section, staged during a slide */
/* Cached flat base pointers for the CURRENT section's grid + damage, so the hot
 * render_set_cell() avoids the world_section_index() variable-multiply and the
 * [idx][y][x] stride math on every call. Refreshed whenever cur_row/cur_col change
 * (render_build_map runs on every section change; the guard in render_set_cell is a
 * safety net). Sentinel 0xFF forces the first refresh even with WRAM not zeroed. */
static u8 *sc_grid, *sc_dmg;
static u8  sc_last_r = 0xFF, sc_last_c = 0xFF;
static u8  edge_last_mask;    /* last drawn enemy edge-warning mask (reset on screen clear) */

/* Slide state. The staging (start) and commit (end) VRAM writes are deferred to
 * render_flush_map (which runs in vblank) so the screen never blanks -> no flash. */
static s16 slide_stage_x;     /* screen-x where the new section sits (256=right, 0=left) */
static u8  slide_pending = 0; /* 0 none, 1 stage adjacent (go 64-wide), 2 commit (back to 32) */
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
    0, 0,                          /* gem shatter   3,4          -> pal 0 */
    1 << 10,                       /* spawn glow frame 2         -> pal 1 */
    1 << 10, 1 << 10, 1 << 10,    /* extra-life anim frames 1-3 -> pal 1 */
    0, 0, 0, 0                      /* gem glitter frames 24-27   -> pal 0 */
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
    u8  off, mt;
    u16 base, pb, row, *m;
    if (sc_last_r != game.cur_row || sc_last_c != game.cur_col) {   /* section changed */
        u8 idx = world_section_index(game.cur_row, game.cur_col);
        sc_grid = &game.sections[idx][0][0];
        sc_dmg  = &game.damage[idx][0][0];
        sc_last_r = game.cur_row; sc_last_c = game.cur_col;
    }
    off  = (u8)((gy << 4) + gx);                         /* flat section offset (GRID_COLS=16) */
    mt   = metatile_for(sc_grid[off], sc_dmg[off]);
    base = (u16)(mt * 4);
    pb   = mt_palbits[mt];                               /* sub-palette bits */
    row  = (u16)((PLAYFIELD_OFFSET_Y >> 3) + (gy << 1));
    m    = &bg1map[((u16)row << 5) + ((u16)gx << 1)];    /* one index; +32 = next tile row */
    m[0]  = base | pb;            m[1]  = (u16)(base + 1) | pb;   /* TL, TR */
    m[32] = (u16)(base + 2) | pb; m[33] = (u16)(base + 3) | pb;   /* BL, BR */
}

/* Draw an explicit metatile (e.g. a shatter frame 16-19) into one grid cell,
 * regardless of the world tile there -- used by the crush/destruction animation,
 * which overlays the shatter frames after the world cell is already cleared. */
void render_crush_cell(u8 gx, u8 gy, u8 mt) {
    u16 base = (u16)(mt * 4);
    u16 pb  = mt_palbits[mt];
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + (gy << 1));
    u16 *m  = &bg1map[((u16)row << 5) + ((u16)gx << 1)];
    m[0]  = base | pb;            m[1]  = (u16)(base + 1) | pb;
    m[32] = (u16)(base + 2) | pb; m[33] = (u16)(base + 3) | pb;
}

/* Blank a grid cell's four BG1 entries (the tile is shown as a falling OBJ
 * instead). Does NOT touch the world grid -- render_set_cell redraws it on land. */
void render_clear_cell(u8 gx, u8 gy) {
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + (gy << 1));
    u16 *m  = &bg1map[((u16)row << 5) + ((u16)gx << 1)];
    m[0] = 0; m[1] = 0; m[32] = 0; m[33] = 0;
}

void render_build_map(void) {
    u8 gx, gy;
    u16 i;
    u8 idx = world_section_index(game.cur_row, game.cur_col);   /* refresh the section cache */
    sc_grid = &game.sections[idx][0][0];
    sc_dmg  = &game.damage[idx][0][0];
    sc_last_r = game.cur_row; sc_last_c = game.cur_col;
    for (i = (PLAYFIELD_OFFSET_Y >> 3) * 32; i < 32 * 32; i++) bg1map[i] = 0; /* clear playfield rows (keep HUD/margin) */
    for (gy = 0; gy < GRID_ROWS; gy++)
        for (gx = 0; gx < GRID_COLS; gx++)
            render_set_cell(gx, gy);
}

static void flush_map_impl(void) {
    /* Upload the regenerated minimap glyph tiles (VRAM tiles 65..68) in vblank. */
    if (mm_dirty) {     /* 16x16 OBJ: TL,TR on one tile row, BL,BR on the next (16-wide grid) */
        dmaCopyVram(mm_obj_tiles,      VRAM_OBJ_MINIMAP,            64);
        dmaCopyVram(mm_obj_tiles + 64, (u16)(VRAM_OBJ_MINIMAP + 16 * 16), 64);
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
    if (bg3_dirty) {                                  /* HUD/text rarely changes */
        dmaCopyVram((u8 *)bg3map, VRAM_BG3_MAP, 0x800);
        bg3_dirty = 0;
    }
}

/* Re-entrancy guard: transitions call render_flush_map() directly on the main
 * thread (screen off), while render_vblank() calls it from the NMI. If the NMI
 * fires mid-flush, it must not reconfigure the DMA channel underneath the main
 * thread -- so the second caller just bails. */
static volatile u8 flushing = 0;
void render_flush_map(void) {
    if (flushing) return;
    flushing = 1;
    flush_map_impl();
    flushing = 0;
}

/* VBlank ISR hook (installed via nmiSet): do the VRAM upload INSIDE vblank so it
 * always lands in the blanking window -- never spilling into active display
 * (the tearing) and never delaying the next game_update (which started the
 * stutter). vblank_flag is set only on non-lag frames, so on a frame where
 * game_update overran we skip the upload (the bg maps may be half-built) and
 * catch it next frame -- no torn/partial map. */
void render_vblank(void) {
    if (!vblank_flag) return;
    if (game_map_dirty || slide_pending || mm_dirty) {
        render_flush_map();
        game_map_dirty = 0;
    }
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
    /* Re-assert the HUD/text layer's 2px upward nudge EVERY vblank. Setting it
     * once in render_init didn't stick -- the per-frame vblank path runs after
     * the library's NMI, which leaves BG3's vertical offset back at 0. */
    bgSetScroll(2, 0, HUD_BG3_VOFS);
    if (!scroll_dirty) return;
    bgSetScroll(0, scr_bg1x, scr_bg1y);
    bgSetScroll(1, scr_bg2x, scr_bg2y);
    scroll_dirty = 0;
}

/* Alarm vignette: while the exit is open (game.alarm_active), pulse an additive
 * RED fixed-color over every layer via color math (REG_CGWSEL/CGADSUB/COLDATA),
 * on a triangle wave. Disables color math cleanly when the alarm ends, so normal
 * play is untouched. Call in vblank (PPU color regs). Port of the red vignette. */
void render_apply_alarm(void) {
    if (!game.alarm_active) {
        *((vuint8 *)0x2131) = 0x00;     /* CGADSUB off every frame -> no stale red tint */
        return;
    }
    {
        u8 t   = game.alarm_timer;                                /* 0..2*PULSE-1 */
        u8 tri = (t < ALARM_PULSE_FRAMES) ? t : (u8)(2 * ALARM_PULSE_FRAMES - t);
        u8 red = (u8)(2 + tri / 5);                               /* ~2..8 of 31  */
        *((vuint8 *)0x2130) = 0x00;                 /* CGWSEL: math always, fixed color */
        *((vuint8 *)0x2132) = 0x80;                 /* COLDATA B = 0 */
        *((vuint8 *)0x2132) = 0x40;                 /* COLDATA G = 0 */
        *((vuint8 *)0x2132) = (u8)(0x20 | red);     /* COLDATA R = red */
        *((vuint8 *)0x2131) = 0x37;                 /* CGADSUB add: BG1|BG2|BG3|OBJ|backdrop */
    }
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
    setPalette(pal + 32, 16, 16 * 2);      /* pal1 -> CGRAM 16-31 (markers intact; minimap is an OBJ now) */

    /* per-level falling-tile OBJ so the falling gem matches the level's gem colour */
    switch (level) {
        case 2:  pic=(u8*)&spr_falls_2_pic;  picend=(u8*)&spr_falls_2_picend;  pal=(u8*)&spr_falls_2_pal;  break;
        case 3:  pic=(u8*)&spr_falls_3_pic;  picend=(u8*)&spr_falls_3_picend;  pal=(u8*)&spr_falls_3_pal;  break;
        case 4:  pic=(u8*)&spr_falls_4_pic;  picend=(u8*)&spr_falls_4_picend;  pal=(u8*)&spr_falls_4_pal;  break;
        case 5:  pic=(u8*)&spr_falls_5_pic;  picend=(u8*)&spr_falls_5_picend;  pal=(u8*)&spr_falls_5_pal;  break;
        case 6:  pic=(u8*)&spr_falls_6_pic;  picend=(u8*)&spr_falls_6_picend;  pal=(u8*)&spr_falls_6_pal;  break;
        case 7:  pic=(u8*)&spr_falls_7_pic;  picend=(u8*)&spr_falls_7_picend;  pal=(u8*)&spr_falls_7_pal;  break;
        case 8:  pic=(u8*)&spr_falls_8_pic;  picend=(u8*)&spr_falls_8_picend;  pal=(u8*)&spr_falls_8_pal;  break;
        case 9:  pic=(u8*)&spr_falls_9_pic;  picend=(u8*)&spr_falls_9_picend;  pal=(u8*)&spr_falls_9_pal;  break;
        case 10: pic=(u8*)&spr_falls_10_pic; picend=(u8*)&spr_falls_10_picend; pal=(u8*)&spr_falls_10_pal; break;
        default: pic=(u8*)&spr_falls_1_pic;  picend=(u8*)&spr_falls_1_picend;  pal=(u8*)&spr_falls_1_pal;  break;
    }
    dmaCopyVram(pic, VRAM_OBJ_FALLS, (u16)(picend - pic));
    setPalette(pal, 128 + OBJPAL_FALLS * 16, 16 * 2);
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
/* Boot LogoScene: the studio logo on a black screen, dropped in via BG2 v-scroll.
 * Loads the logo onto BG2 (black elsewhere), BG1 empty + BG3 black backing. */
void render_show_logo(void) {
    setScreenOff();
    slide_pending = 0; mm_dirty = 0;   /* WRAM not zeroed at boot -> avoid a stale flush path */
    render_hide_sprites();
    dmaCopyVram((u8 *)&logo_pic, VRAM_BG2_TILES, (u16)(&logo_picend - &logo_pic));
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    setPalette((u8 *)&logo_pal, BG2_PAL * 16, 96 * 2);    /* CGRAM 32..127 */
    /* 64-tall map (SC_32x64, 0x800 words at 0x5800..0x6000, just under OBJ tiles) so
     * the logo hides fully above the screen and drops in without wrapping. */
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x64);
    dmaCopyVram((u8 *)&logo_map, VRAM_BG2_MAP, 0x1000);
    render_clear_screen();                                /* BG1 empty, BG3 black, minimap hidden */
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetScroll(0, 0, 0);
    render_flush_map();                                   /* push BG1(empty)+BG3(black) */
    bg2_cur_x = 0; bg2_cur_y = 0;
    /* Start the logo fully above the visible top (screen_y = -48) so boot is a
     * black screen; the LogoScene then drops it in. Apply now so screen-on never
     * shows it parked at canvas-Y. */
    scr_bg1x = 0; scr_bg1y = 0; scr_bg2x = 0;
    scr_bg2y = (u16)(LOGO_CANVAS_Y + 48);
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, scr_bg2y);
    scroll_dirty = 0;
    setScreenOn();
}

/* Position the logo at screen pixel-row `screen_y` (its top edge) via BG2 v-scroll
 * (the logo sits at BG2 canvas-Y = LOGO_CANVAS_Y). Applied in vblank like all scroll. */
void render_logo_pos(s16 screen_y) {
    scr_bg1x = 0; scr_bg1y = 0;
    scr_bg2x = 0; scr_bg2y = (u16)(s16)(LOGO_CANVAS_Y - screen_y);
    scroll_dirty = 1;
}

/* A single white pixel at the centre (8,8) of a 16x16 OBJ (white via minimap pal idx4).
 * Shared by the settled twinkle and the landing particle burst -- the original uses
 * 1-2px pixels, not stars. oamSet at (px-8,py-8) puts the pixel at (px,py). */
static const u8 spark_tiles[128] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static u8 lp_rng = 0x9D;
static u8 lp_rand(void) { lp_rng = (u8)(lp_rng * 37 + 17); return lp_rng; }

/* 32 points on the logo's perimeter ellipse (dx,dy from centre 128,112). */
static const s8 spark_ring[64] = {
    76,0,75,6,70,11,63,17,54,21,42,25,29,28,15,29,
    0,30,-15,29,-29,28,-42,25,-54,21,-63,17,-70,11,-75,6,
    -76,0,-75,-6,-70,-11,-63,-17,-54,-21,-42,-25,-29,-28,-15,-29,
    0,-30,15,-29,29,-28,42,-25,54,-21,63,-17,70,-11,75,-6
};

/* Settled-logo sparkles (LogoScene spawnSparkle/updateSparkles): one new sparkle every
 * ~5 frames at a RANDOM spot -- 40% inside the logo, 60% on its perimeter -- each drifting
 * and twinkling (fade in then out) over a random ~0.3-0.7s life, then gone. 1px white.
 * `spawn`!=0 adds one this frame; always advances + draws the live ones. Slots 31..42. */
#define MAX_SPARK 28
static struct { s16 x, y, vx, vy; u8 life, maxlife; } lspark[MAX_SPARK];

void render_logo_sparkles(u8 spawn) {                /* spawn = how many to add this frame */
    u8 i, s;
    for (s = 0; s < spawn; s++) {
        for (i = 0; i < MAX_SPARK; i++) if (lspark[i].maxlife == 0) break;
        if (i >= MAX_SPARK) break;                   /* pool full */
        {
            s16 sx, sy;
            if ((lp_rand() % 5) < 2) {                       /* 40% inside the logo */
                sx = (s16)(128 + (s16)(lp_rand() % 120) - 60);
                sy = (s16)(112 + (s16)(lp_rand() % 36) - 18);
            } else {                                          /* 60% on the perimeter (+jitter) */
                u8 a = (u8)((lp_rand() & 31) * 2);
                sx = (s16)(128 + spark_ring[a]     + (s16)(lp_rand() % 9) - 4);
                sy = (s16)(112 + spark_ring[a + 1] + (s16)(lp_rand() % 9) - 4);
            }
            lspark[i].x = (s16)(sx << 8); lspark[i].y = (s16)(sy << 8);
            lspark[i].vx = (s16)((s16)(lp_rand() % 48) - 24);        /* gentle drift, 8.8/frame */
            lspark[i].vy = (s16)((s16)(lp_rand() % 48) - 24 - 16);   /* slight upward bias */
            lspark[i].life = 0;
            lspark[i].maxlife = (u8)(8 + (lp_rand() % 14));          /* ~0.13-0.36s (snappy twinkle) */
        }
    }
    for (i = 0; i < MAX_SPARK; i++) {
        u16 slot = (u16)((31 + i) * 4);
        u16 ml = lspark[i].maxlife, lf;
        if (!ml) { oamSetVisible(slot, OBJ_HIDE); continue; }
        lspark[i].x = (s16)(lspark[i].x + lspark[i].vx);
        lspark[i].y = (s16)(lspark[i].y + lspark[i].vy);
        lf = ++lspark[i].life;
        if (lf >= ml) { lspark[i].maxlife = 0; oamSetVisible(slot, OBJ_HIDE); continue; }
        /* twinkle: alpha fades in over first 30%, out over the rest -> show the bright middle */
        if (lf * 20 >= ml * 3 && lf * 20 <= ml * 13) {
            oamSet(slot, (u16)((lspark[i].x >> 8) - 8), (u16)((lspark[i].y >> 8) - 8),
                   2, 0, 0, (u16)OBJN_SPARKLE, OBJPAL_MINIMAP);
            oamSetEx(slot, OBJ_SMALL, OBJ_SHOW);
        } else oamSetVisible(slot, OBJ_HIDE);
    }
}

/* Landing particle burst (LogoScene.spawnLandingParticles): white pixels spray up from the
 * logo's bottom edge on each bounce, arc under gravity, flicker out. 8.8 fixed; slots 7..30. */
#define MAX_PART 24
static struct { s16 x, y, vx, vy; u8 life; } lpart[MAX_PART];

void render_logo_burst(s16 cx, s16 cy, u8 n) {
    u8 i, k;
    for (k = 0; k < n; k++) {
        for (i = 0; i < MAX_PART; i++) if (lpart[i].life == 0) break;
        if (i >= MAX_PART) return;
        lpart[i].x  = (s16)((cx + (s16)(lp_rand() % 130) - 65) << 8);  /* spread across the width */
        lpart[i].y  = (s16)(cy << 8);
        lpart[i].vx = (s16)(((s16)(lp_rand() % 160) - 80));           /* outward, 8.8 */
        lpart[i].vy = (s16)(-(s16)((lp_rand() % 110) + 40));          /* upward, 8.8 */
        lpart[i].life = (u8)(22 + (lp_rand() % 18));
    }
}

void render_logo_particles(void) {
    u8 i;
    for (i = 0; i < MAX_PART; i++) {
        u16 slot = (u16)((7 + i) * 4);
        if (lpart[i].life) {
            lpart[i].vy = (s16)(lpart[i].vy + 13);     /* gravity */
            lpart[i].x  = (s16)(lpart[i].x + lpart[i].vx);
            lpart[i].y  = (s16)(lpart[i].y + lpart[i].vy);
            lpart[i].life--;
            if (lpart[i].life > 6 || (lpart[i].life & 1)) {   /* flicker out at the end */
                oamSet(slot, (u16)((lpart[i].x >> 8) - 8), (u16)((lpart[i].y >> 8) - 8),
                       2, 0, 0, (u16)OBJN_SPARKLE, OBJPAL_MINIMAP);
                oamSetEx(slot, OBJ_SMALL, OBJ_SHOW);
            } else oamSetVisible(slot, OBJ_HIDE);
        } else oamSetVisible(slot, OBJ_HIDE);
    }
}

/* Clear both pools (WRAM isn't zeroed at boot -> stale particles would appear). */
void render_logo_reset(void) {
    u8 i;
    for (i = 0; i < MAX_SPARK; i++) lspark[i].maxlife = 0;
    for (i = 0; i < MAX_PART; i++)  lpart[i].life = 0;
}

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
    /* zapv shares ZAPH's palette (OBJPAL_ZAPV == 3), so slot 4 is free: load the
     * 4-colour minimap palette there. */
    setPalette((u8 *)mm_obj_pal, 128 + OBJPAL_MINIMAP * 16, 16 * 2);
    /* boot-logo sparkle star (16x16 OBJ, white via the minimap palette); 2nd tile row +0x100 */
    dmaCopyVram((u8 *)spark_tiles,      VRAM_OBJ_SPARKLE,            64);
    dmaCopyVram((u8 *)spark_tiles + 64, (u16)(VRAM_OBJ_SPARKLE + 0x100), 64);
    dmaCopyVram((u8 *)&spr_pdeath_pic, VRAM_OBJ_PDEATH, (u16)(&spr_pdeath_picend - &spr_pdeath_pic));
    setPalette((u8 *)&spr_pdeath_pal, 128 + OBJPAL_PDEATH * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_edeath_pic, VRAM_OBJ_EDEATH, (u16)(&spr_edeath_picend - &spr_edeath_pic));
    setPalette((u8 *)&spr_edeath_pal, 128 + OBJPAL_EDEATH * 16, 16 * 2);
    dmaCopyVram((u8 *)&spr_falls_1_pic, VRAM_OBJ_FALLS, (u16)(&spr_falls_1_picend - &spr_falls_1_pic));
    setPalette((u8 *)&spr_falls_1_pal, 128 + OBJPAL_FALLS * 16, 16 * 2);  /* swapped per level below */
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
    dmaCopyVram((u8 *)edge_top_tile,  (u16)(VRAM_BG3_TILES + EDGE_TILE_TOP * 8), 16);
    dmaCopyVram((u8 *)edge_left_tile, (u16)(VRAM_BG3_TILES + EDGE_TILE_LEFT * 8), 16);
    dmaCopyVram((u8 *)hud_colon_tile, (u16)(VRAM_BG3_TILES + HUD_COLON_TILE * 8), 16);
    dmaCopyVram((u8 *)hud_halfbar_tile, (u16)(VRAM_BG3_TILES + HUD_HALFBAR_TILE * 8), 16);  /* bar lower edge */
    bgSetGfxPtr(2, VRAM_BG3_TILES);
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);
    /* Nudge the whole BG3 layer (HUD bar + all scene text) up 2px: the font
     * glyphs are top-aligned in their tile, so on tile-row 1 the text sat at
     * y8-14 with a dead black row at y15. Scrolling up 2px lifts the text and
     * trims that extra black strip below the bar. */
    bgSetScroll(2, 0, HUD_BG3_VOFS);

    setMode(BG_MODE1, 0);
    REG_BGMODE = 0x09;       /* mode 1 + BG3 high priority */

    /* Main screen = BG1 + BG2 + BG3 + OBJ; subscreen off. */
    videoMode = 0x17;        /* TM: BG1|BG2|BG3|OBJ */
    videoModeSub = 0x00;     /* TS: nothing */
    REG_TM = 0x17;
    *((vuint8 *)0x212D) = 0x00;   /* REG_TS */
    *((vuint8 *)0x2131) = 0x00;   /* CGADSUB: color math OFF (no stray red tint at boot) */

    /* Boot straight into the LogoScene (studio logo on black) rather than flashing
     * the level-1 background that render_set_background staged above. */
    render_show_logo();
}

void render_player(void) {
    Player *p = &game.player;
    u16 sx, sy, gfx;
    u8 col;

    sx = (u16)(PLAYFIELD_OFFSET_X + p->pixel_x);
    sy = (u16)(PLAYFIELD_OFFSET_Y + p->pixel_y);

    if (game.death_pending) {         /* death animation plays at the death spot */
        u8 fr = (u8)((DEATH_SEQ_FRAMES - game.death_timer) / DEATH_ANIM_FRAMES);
        if (fr >= DEATH_ANIM_COUNT) fr = (u8)(DEATH_ANIM_COUNT - 1);
        oamSet(0, sx, sy, 3, 0, 0, (u16)(OBJN_PDEATH + fr * 2), OBJPAL_PDEATH);
        oamSetEx(0, OBJ_SMALL, OBJ_SHOW);
        return;
    }
    if (!p->alive) { oamSetVisible(0, OBJ_HIDE); return; }

    if (p->idle_col != 0xFF) {        /* standing still -> idle look-around cols 0-2 */
        col = p->idle_col;
    } else switch (p->direction) {    /* sheet columns: L3 R4 U5 D6 */
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
void render_fall(u8 slot, u8 type, u16 px, u16 py, u8 dmg) {
    u16 name = (type == TILE_BOULDER)    ? OBJN_BOULDER_FALL :
               (type == TILE_EXTRA_LIFE) ? OBJN_ELIFE_FALL   :
               (u16)(OBJN_GEM_FALL + (dmg > 2 ? 2 : dmg) * 2);   /* gem: show its damage frame */
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
    bg3_dirty = 1;
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
    /* Row 2 gets the half-black edge tile: the 2px up-nudge places it at screen
     * y14.., so its black top backs the gap below the bar while its transparent
     * lower rows leave the playfield visible. On BG3, so it never slides. */
    for (i = 0; i < MM_COL; i++)
        bg3map[64 + i] = (u16)(HUD_HALFBAR_TILE | (BG3_TEXT_PAL << 10) | 0x2000);

    /* Single line laid out like the original: SCORE:000000 GEM:c/t [heart]:lives.
     * Drawn on ROW 1 (the lower half of the 16px bar) so the emulator's top
     * overscan can't clip it (row 0 stays blank black padding). */
    hud_puts(1, 1, "SCORE:");  hud_putnum(7, 1, game.score, 6);
    hud_puts(14, 1, "GEM:");      /* compact c/t -- no padding gaps (the bar is cleared each redraw) */
    {
        u8 cx = 18;
        if (game.gems_collected >= 10) { hud_putnum(cx, 1, game.gems_collected, 2); cx = (u8)(cx + 2); }
        else                           { hud_putnum(cx, 1, game.gems_collected, 1); cx = (u8)(cx + 1); }
        hud_putc(cx, 1, '/'); cx = (u8)(cx + 1);
        if (game.total_gems >= 10) hud_putnum(cx, 1, game.total_gems, 2);
        else                       hud_putnum(cx, 1, game.total_gems, 1);
    }
    /* life icon (custom heart glyph, tile 64) + ':' + lives count */
    bg3map[32 + 24] = (u16)(HUD_ICON_LIFE)   | (BG3_TEXT_PAL << 10) | 0x2000;
    bg3map[32 + 25] = (u16)(HUD_COLON_TILE)  | (BG3_TEXT_PAL << 10) | 0x2000;  /* spaced colon */
    hud_putnum(26, 1, game.lives, 1);
    return TRUE;
}

/* Build the 4-colour minimap into a 16x16 OBJ sprite (4 4bpp tiles) and keep it
 * shown each frame. Refreshes one section's gem flag per call (round-robin). */
void render_minimap(void) {
    u8  nsec = (u8)(game.world_rows * game.world_cols);
    u8  cur  = world_section_index(game.cur_row, game.cur_col);
    u32 sig;
    u8  r, c, idx, x, y, tx, ty, scanned = 0;

    mm_blink++;

    /* keep the sprite on-screen every frame (so it survives render_hide_sprites) */
    oamSet((u16)(OAM_MINIMAP * 4), MM_OBJ_X, MM_OBJ_Y, 2, 0, 0,
           (u16)OBJN_MINIMAP, OBJPAL_MINIMAP);
    oamSetEx((u16)(OAM_MINIMAP * 4), OBJ_SMALL, OBJ_SHOW);

    /* round-robin gem scan, every 16th frame (was 4th: the 208-cell scan + the
     * multiply/32-bit-shift signature rebuild is a per-frame spike; 16 keeps the
     * minimap fresh while cutting that spike 4x). */
    if (nsec && (mm_blink & 15) == 0) {
        u8 s = (u8)(mm_scan % nsec);
        u8 *g = &game.sections[s][0][0];
        u8 has = 0, k;
        for (k = 0; k < SECTION_TILES; k++) if (g[k] == TILE_GEM) { has = 1; break; }
        mm_has_gems[s] = has;
        mm_scan = (u8)(s + 1);
        scanned = 1;
    }

    /* The signature only moves when the round-robin just rescanned a section, or
     * when the current section changed. On the other ~3/4 frames it is identical
     * to last frame, so skip the 16-iteration rebuild (which costs a per-section
     * world_section_index multiply + a 32-bit variable shift each). */
    if (!scanned && cur == mm_last_cur) return;
    mm_last_cur = cur;

    sig = 0;
    for (r = 0; r < game.world_rows; r++)
        for (c = 0; c < game.world_cols; c++)
            if (mm_has_gems[world_section_index(r, c)]) sig |= (u32)(1UL << (r * 4 + c));
    sig = (sig << 4) | cur;
    if (sig == mm_sig) return;
    mm_sig = sig;

    /* paint the 16x16 index buffer: black bg, then a 3x3 filled cell per section
     * (dark-grey out-of-world, light-grey has-gems, white current, green cleared). */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) mm_px[y][x] = 1;        /* black bg / gaps */
    for (r = 0; r < MAX_WORLD_ROWS; r++)
        for (c = 0; c < MAX_WORLD_COLS; c++) {
            u8 col, dx, dy;
            /* centre the grid; +1 rounds the odd leftover pixel to the TOP so
             * there's 1px LESS black below the dots than above (was 2 up / 3 down). */
            u8 off = (u8)((16 - ((MAX_WORLD_COLS - 1) * MM_CELL + MM_DOT) + 1) / 2);
            u8 sx = (u8)(c * MM_CELL + off), sy = (u8)(r * MM_CELL + off);
            if (r >= game.world_rows || c >= game.world_cols) {
                col = 2;                                  /* dark-grey: out of world */
            } else {
                idx = world_section_index(r, c);
                col = (idx == cur) ? 4 : mm_has_gems[idx] ? 3 : 5;  /* white / light-grey / green */
            }
            for (dy = 0; dy < MM_DOT; dy++)
                for (dx = 0; dx < MM_DOT; dx++) mm_px[sy + dy][sx + dx] = col;
        }

    /* encode the four 8x8 quadrants as 4bpp tiles (TL,TR,BL,BR) */
    for (ty = 0; ty < 2; ty++)
        for (tx = 0; tx < 2; tx++) {
            u8 *b = &mm_obj_tiles[(ty * 2 + tx) * 32];
            for (y = 0; y < 8; y++) {
                u8 p0 = 0, p1 = 0, p2 = 0, p3 = 0;
                for (x = 0; x < 8; x++) {
                    u8 v = mm_px[ty * 8 + y][tx * 8 + x], bit = (u8)(7 - x);
                    p0 |= (u8)((v & 1) << bit);        p1 |= (u8)(((v >> 1) & 1) << bit);
                    p2 |= (u8)(((v >> 2) & 1) << bit); p3 |= (u8)(((v >> 3) & 1) << bit);
                }
                b[y * 2] = p0; b[y * 2 + 1] = p1; b[16 + y * 2] = p2; b[16 + y * 2 + 1] = p3;
            }
        }
    mm_dirty = 1;
}

/* Drop the minimap's cached state so it rebuilds on the next frame (level load /
 * respawn). Assume every section has gems until the round-robin scan corrects it. */
void render_minimap_reset(void) {
    u8 i;
    for (i = 0; i < MAX_SECTIONS; i++) mm_has_gems[i] = 1;
    mm_scan = 0; mm_blink = 0; mm_sig = 0xFFFFFFFF; mm_last_cur = 0xFF;
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
    bg3_dirty = 1;
    edge_last_mask = 0;        /* edge-warning strips were just wiped */
    oamSetVisible((u16)(OAM_MINIMAP * 4), OBJ_HIDE);  /* no minimap on text scenes */
}

void render_text(u8 x, u8 y, const char *s) {
    while (*s) { hud_putc(x++, y, *s++); }
}

void render_num(u8 x, u8 y, u32 val, u8 digits) {
    hud_putnum(x, y, val, digits);
}

/* Level-complete stats banner: an opaque dark BG3 fill below the HUD with the
 * level number, time bonus, and per-level stats (port of showLevelComplete). */
/* Level-complete screen (port of showLevelComplete): a clean dark screen with the
 * per-level 'level-up kitty' (BG2 image) centred up top and LEVEL N COMPLETE + stats
 * (BG3) below. Replaces the old BG3-only banner that left the playfield showing. */
static char *const kitty_pic_tbl[9] = {
    &kitty_1_pic, &kitty_2_pic, &kitty_3_pic, &kitty_4_pic, &kitty_5_pic,
    &kitty_6_pic, &kitty_7_pic, &kitty_8_pic, &kitty_9_pic };
static char *const kitty_end_tbl[9] = {
    &kitty_1_picend, &kitty_2_picend, &kitty_3_picend, &kitty_4_picend, &kitty_5_picend,
    &kitty_6_picend, &kitty_7_picend, &kitty_8_picend, &kitty_9_picend };
static char *const kitty_map_tbl[9] = {
    &kitty_1_map, &kitty_2_map, &kitty_3_map, &kitty_4_map, &kitty_5_map,
    &kitty_6_map, &kitty_7_map, &kitty_8_map, &kitty_9_map };
static char *const kitty_pal_tbl[9] = {
    &kitty_1_pal, &kitty_2_pal, &kitty_3_pal, &kitty_4_pal, &kitty_5_pal,
    &kitty_6_pal, &kitty_7_pal, &kitty_8_pal, &kitty_9_pal };

void render_lc_banner(void) {
    u8  k = (u8)((game.current_level >= 1 && game.current_level <= 9)
                 ? game.current_level - 1 : 8);
    u16 *src = (u16 *)kitty_map_tbl[k];
    u16 i;
    setScreenOff();
    render_hide_sprites();
    /* kitty image -> BG2 */
    dmaCopyVram((u8 *)kitty_pic_tbl[k], VRAM_BG2_TILES,
                (u16)(kitty_end_tbl[k] - kitty_pic_tbl[k]));
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    setPalette((u8 *)kitty_pal_tbl[k], BG2_PAL * 16, 96 * 2);
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
    for (i = 0; i < 32 * 32; i++) bg2map[i] = src[i];
    dmaCopyVram((u8 *)bg2map, VRAM_BG2_MAP, 0x800);
    /* BG1 cleared (transparent), BG3 -> black backing + the text; minimap hidden */
    render_clear_screen();
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    hud_puts(9, 9, "LEVEL");      hud_putnum(15, 9, game.current_level, 2);
    hud_puts(18, 9, "COMPLETE");
    hud_puts(9, 13, "TIME BONUS"); hud_putnum(21, 13, game.time_bonus, 4);
    hud_puts(9, 15, "BLOCKS");     hud_putnum(21, 15, game.blocks_crushed, 3);
    hud_puts(9, 17, "ENEMIES");    hud_putnum(21, 17, game.enemies_killed, 3);
    render_flush_map();
    bg2_cur_x = 0; bg2_cur_y = 0;
    scr_bg1x = 0; scr_bg1y = 0; scr_bg2x = 0; scr_bg2y = 0; scroll_dirty = 0;
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    setScreenOn();
}

void render_hide_sprites(void) {
    u8 i;
    for (i = 0; i < 16; i++) oamSetVisible((u16)(i * 4), OBJ_HIDE);
    /* NOTE: the minimap (slot 26) is intentionally NOT hidden here -- this runs
     * every frame of a section slide, and the minimap should stay put. Scenes
     * hide it via render_clear_screen instead. */
}

/* Transient centred flash banner (e.g. SECTION CLEARED / NO ENTRY): a short
 * opaque-black BG3 strip on row 13 with white text. render_flash_clear restores
 * those cells to transparent so the playfield shows through again. */
static u8 flash_x0, flash_x1;
#define FLASH_ROW 13
void render_flash(const char *s) {
    u8 len = 0, x, x0;
    u16 blk = (u16)((BG3_TEXT_PAL << 10) | 0x2000);
    while (s[len]) len++;
    x0 = (u8)((32 - len) / 2);
    flash_x0 = (u8)(x0 ? x0 - 1 : 0);
    flash_x1 = (u8)(x0 + len + 1); if (flash_x1 > 32) flash_x1 = 32;
    for (x = flash_x0; x < flash_x1; x++) bg3map[FLASH_ROW * 32 + x] = blk;
    render_text(x0, FLASH_ROW, s);
}
void render_flash_clear(void) {
    u8 x;
    for (x = flash_x0; x < flash_x1; x++) bg3map[FLASH_ROW * 32 + x] = 0;
    bg3_dirty = 1;
}

/* Draw the enemy edge-warning strips for the given direction mask (bit0=up,
 * bit1=down, bit2=left, bit3=right) on BG3; edges not in the mask are cleared.
 * Only touches VRAM when the mask changes. */
void render_edge_warn(u8 mask) {
    u8 i;
    u8 top = (u8)(PLAYFIELD_OFFSET_Y >> 3);          /* first playfield tile row */
    u8 bot = (u8)(top + GRID_ROWS * 2 - 1);          /* last playfield tile row  */
    u16 P  = (u16)((BG3_TEXT_PAL << 10) | 0x2000);
    if (mask == edge_last_mask) return;
    edge_last_mask = mask;
    for (i = 0; i < 32; i++) {
        bg3map[top * 32 + i] = (mask & 1) ? (u16)(EDGE_TILE_TOP | P) : 0;
        bg3map[bot * 32 + i] = (mask & 2) ? (u16)(EDGE_TILE_TOP | P | 0x8000) : 0;  /* V-flip */
    }
    for (i = top; i <= bot; i++) {
        bg3map[i * 32 + 0]  = (mask & 4) ? (u16)(EDGE_TILE_LEFT | P) : 0;
        bg3map[i * 32 + 31] = (mask & 8) ? (u16)(EDGE_TILE_LEFT | P | 0x4000) : 0;  /* H-flip */
    }
    bg3_dirty = 1;
    game_map_dirty = 1;
}
