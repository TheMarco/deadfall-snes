/* Deadfall SNES port - rendering. Mode 1: BG1 = playfield grid, OBJ = entities.
 * The BG1 tilemap is generated at runtime from the active section's grid. */
#include <snes.h>
#include "render.h"
#include "world.h"
#include "balance.h"

/* PVSnesLib shadows of the screen-designation registers. */
extern u8 videoMode;     /* mirror of REG_TM (main screen) */
extern u8 videoModeSub;  /* mirror of REG_TS (sub screen)  */

/* Converted graphics (see data.asm). */
extern char bg_tiles_pic, bg_tiles_picend, bg_tiles_pal;
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
extern char bgtex_pic, bgtex_picend, bgtex_map, bgtex_pal;
#define BGTEX_W 32   /* texture width in tiles  */
#define BGTEX_H 32   /* texture height in tiles (256x256 -> fills the 32-tile map exactly) */

/* Per-section BG2 parallax/phase: each section shifts the seamless background by
 * BG2_PHASE_STEP (a non-divisor of 256) so adjacent sections look different. The
 * background scrolls less than the playfield during a slide (parallax). */
#define BG2_PHASE_STEP 96
static u16 bg2_cur_x, bg2_cur_y;        /* current section's BG2 scroll (accumulates) */
static u16 slide_bg2_ox, slide_bg2_oy;  /* BG2 scroll captured at slide start */
static s16 slide_bg2_dx, slide_bg2_dy;  /* BG2 scroll delta across the slide */

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
    u16 col = (u16)(gx * 2);
    u16 row = (u16)((PLAYFIELD_OFFSET_Y >> 3) + gy * 2);  /* below the HUD/margin */
    bg1map[row * 32 + col]           = base;             /* TL */
    bg1map[row * 32 + col + 1]       = (u16)(base + 1);  /* TR */
    bg1map[(row + 1) * 32 + col]     = (u16)(base + 2);  /* BL */
    bg1map[(row + 1) * 32 + col + 1] = (u16)(base + 3);  /* BR */
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
            u16 b = (u16)(mt * 4), col = (u16)(gx * 2), row = (u16)(base + gy * 2);
            buf[row * 32 + col]           = b;
            buf[row * 32 + col + 1]       = (u16)(b + 1);
            buf[(row + 1) * 32 + col]     = (u16)(b + 2);
            buf[(row + 1) * 32 + col + 1] = (u16)(b + 3);
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

/* Load the seamless background texture into VRAM/CGRAM. Same for every level,
 * so only needed once (caller force-blanked for the tile DMA). */
static void render_load_tileset(void) {
    dmaCopyVram((u8 *)&bgtex_pic, VRAM_BG2_TILES, (u16)(&bgtex_picend - &bgtex_pic));
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    setPalette((u8 *)&bgtex_pal, BG2_PAL * 16, 96 * 2);   /* CGRAM 32..127 */
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
}

/* Fill the WHOLE BG2 map with the seamless texture tiled. Must cover all 32
 * rows (not just the playfield band): during a vertical slide BG2 scrolls, and
 * any unfilled rows would wrap black bands into view. BGTEX_H=16 divides 32, so
 * the texture wraps cleanly. Uniform across sections, so it just scrolls. */
void render_set_background(void) {
    u16 *tex = (u16 *)&bgtex_map;        /* 16x16 texture map entries */
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

/* Level load: force-blank, load the texture, fill the BG2 map. */
void render_load_background(void) {
    setScreenOff();
    render_load_tileset();
    render_set_background();
    render_flush_map();
    setScreenOn();
}

void render_init(void) {
    consoleInit();          /* sets up NMI/vblank, auto-joypad read, etc. */
    setBrightness(0);
    WaitForVBlank();

    oamInit();
    bgInitTileSet(0, (u8 *)&bg_tiles_pic, (u8 *)&bg_tiles_pal, 0,
                  (u16)(&bg_tiles_picend - &bg_tiles_pic), 16 * 2,
                  BG_16COLORS, VRAM_BG1_TILES);
    /* HUD glyph tiles live just after the gameplay tiles in the BG1 tileset,
     * and use BG sub-palette 1 (white) so they don't touch the gameplay palette */
    dmaCopyVram((u8 *)&hud_font_pic, VRAM_BG1_HUD, (u16)(&hud_font_picend - &hud_font_pic));
    setPalette((u8 *)&hud_font_pal, HUD_PAL * 16, 16 * 2);
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

    /* Fill HUD rows 0-1 with the opaque black space tile (tile 0 is solid index2)
     * at high priority, so the bar is a solid black strip over the texture. */
    for (i = 0; i < 32; i++) {
        u16 blk = (u16)((BG3_TEXT_PAL << 10) | 0x2000);
        bg3map[i] = blk; bg3map[32 + i] = blk;
    }

    hud_puts(0, 0, "SCORE");  hud_putnum(6, 0, game.score, 6);
    hud_puts(14, 0, "GEM");   hud_putnum(17, 0, game.gems_collected, 2);
    hud_putc(19, 0, '/');     hud_putnum(20, 0, game.total_gems, 2);
    hud_puts(24, 0, "LV");    hud_putnum(27, 0, game.current_level, 2);

    hud_puts(0, 1, "LIVES");  hud_putnum(6, 1, game.lives, 1);
    if (game.portal_active) hud_puts(10, 1, "EXIT OPEN");
    return TRUE;
}

/* ---- text scenes (title / game over / victory) on BG1 ---- */
void render_clear_screen(void) {
    u16 i;
    for (i = 0; i < 32 * 32; i++) { bg1map[i] = 0; bg3map[i] = 0; }
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
