/* Deadfall SNES port - title attract mode (port of TitleScene.js's idle intro).
 *
 * A black-screen cinematic that shows off the cast, one at a time, with a name
 * label and a small demo of what each does:
 *   FLUX   (player) walks in, mines a gem (3 hits -> gone), walks off
 *   GLOOP  (enemy)  walks across
 *   CLANKY (robot)  walks in, fires a zap, walks off
 * then it ends and main.c returns to the title. Any button press exits early.
 *
 * Everything reuses the in-game OBJ sprites (already resident in VRAM after
 * render_attract_begin reloads them). Names are HUD-font text on BG3. The OAM is
 * auto-DMA'd each vblank by the library, so we just oamSet the active sprites and
 * hide the rest. See render.c for the OBJ tile layout (gfx = anim*32 + col*2). */
#include <snes.h>
#include "config.h"      /* ANIM_BOB_FRAMES, PAD_*, DIR_* */
#include "balance.h"     /* ROBOT_ZAP_RANGE */
#include "render.h"      /* OBJN/OBJPAL/OAM constants + render_text / render_attract_begin */
#include "audio.h"       /* audio_sfx */
#include "audio_sfx.h"   /* SFX_* */
#include "game.h"        /* game_map_dirty */
#include "sram.h"        /* save_hiscore: shown across the top of the cinematic */
#include "attract.h"

/* ---- layout / pacing ---- */
#define CHAR_Y      96      /* sprite top: 16px sprite centred ~vertical middle */
#define CENTER_X    120     /* a 16px sprite centred horizontally (256-16)/2     */
#define NAME_ROW    16      /* BG3 text row for the name label (just below char) */
#define WALK_SPD    1       /* px/frame: deliberate showcase pace (~4.3s/cross)  */
#define MINE_STEP   24      /* frames per mining hit (3 hits to destroy)         */
#define CRUSH_DUR   (MINE_STEP * 3)   /* frames to crush one object (rock or gem) */
#define ZAP_LEN     60      /* frames Clanky holds the zap (~1s)                 */

/* Flux's crush demo: he stops left of a rock, with a gem one tile beyond it.
 * He crushes the rock, steps into its spot, then crushes the gem. */
#define FLUX_STOP   100
#define ROCK_X      (FLUX_STOP + 16)
#define GEM_X       (FLUX_STOP + 32)

/* ---- OAM slots (reuse the gameplay slots; everything else is hidden) ---- */
#define A_OAM_CHAR  OAM_PLAYER          /* 0:  the one walking character          */
#define A_OAM_GEM   OAM_FALL_BASE       /* 10: the gem Flux crushes               */
#define A_OAM_ROCK  (OAM_FALL_BASE + 1) /* 11: the rock Flux crushes first        */
#define A_OAM_ZAP   OAM_ZAP_BASE        /* 7:  Clanky's bolt (3 segments, slots 7-9) */

/* Right-facing frame tiles (column index: player R=4, enemy/robot R=dir 1). */
#define FLUX_GFX(anim)   (u16)((anim) * 32 + 4 * 2)
#define GLOOP_GFX(anim)  (u16)(OBJN_ENEMY + (anim) * 32 + 1 * 2)
#define CLANKY_GFX(row)  (u16)(OBJN_ROBOT + (row) * 32 + 1 * 2)
#define GEM_GFX(dmg)     (u16)(OBJN_GEM_FALL + ((dmg) > 2 ? 2 : (dmg)) * 2)
#define ROCK_GFX(dmg)    (u16)(OBJN_BLOCK_ATTRACT + ((dmg) > 2 ? 2 : (dmg)) * 2)  /* mineable block */

static const u8 eyeseq[4] = {0, 1, 2, 1};   /* robot eye brightness rows (dim/normal/bright) */

enum { A_FLUX_IN, A_FLUX_ROCK, A_FLUX_STEP, A_FLUX_GEM, A_FLUX_OUT,
       A_GLOOP, A_CLANKY_IN, A_CLANKY_ZAP, A_CLANKY_OUT, A_DONE };

static u8  st;        /* current sub-state */
static u16 t;         /* frames elapsed in this sub-state */
static s16 cx;        /* walking character x */
static u8  bob;       /* 0/1 animation toggle for walk frames */
static u8  eyei;      /* robot eye-pulse index */

/* Draw a 16x16 OBJ at (x,CHAR_Y); hide it if x is off either side. */
static void spr_show(u8 slot, s16 x, u16 gfx, u8 pal) {
    u16 id = (u16)(slot * 4);
    if (x <= -16 || x >= 256) { oamSetVisible(id, OBJ_HIDE); return; }
    oamSet(id, (u16)x, CHAR_Y, 3, 0, 0, gfx, pal);
    oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
}

/* Centre a name label on NAME_ROW (clears the row first). */
static void name_show(const char *s) {
    u8 n = 0, x0;
    while (s[n]) n++;
    x0 = (u8)((32 - n) >> 1);
    render_text(0, NAME_ROW, "                                ");   /* 32 spaces: clear row */
    render_text(x0, NAME_ROW, s);
    game_map_dirty = 1;     /* render_text marked bg3 dirty; this triggers the vblank flush */
}

void attract_begin(void) {
    render_attract_begin();          /* black canvas; reload font + sprites the title clobbered */
    st = A_FLUX_IN; t = 0; cx = 0; bob = 0; eyei = 0;
    if (save_hiscore) {              /* the battery-backed best, up top for the whole show */
        render_text(10, 3, "BEST");
        render_num(15, 3, save_hiscore, 6);
    }
    name_show("FLUX");
}

/* Returns 1 when the cinematic is over (or a button was pressed) -> back to title. */
u8 attract_update(u16 down) {
    if (down) return 1;              /* any press skips the attract loop */

    t++;
    if ((t % ANIM_BOB_FRAMES) == 0) bob ^= 1;

    switch (st) {
    case A_FLUX_IN:
        cx += WALK_SPD;
        spr_show(A_OAM_CHAR, cx, FLUX_GFX(bob), OBJPAL_PLAYER);
        spr_show(A_OAM_ROCK, ROCK_X, ROCK_GFX(0), OBJPAL_BLOCK_ATTRACT);   /* already on screen */
        spr_show(A_OAM_GEM,  GEM_X,  GEM_GFX(0),  OBJPAL_FALLS);
        if (cx >= FLUX_STOP) { cx = FLUX_STOP; st = A_FLUX_ROCK; t = 0; }
        break;

    case A_FLUX_ROCK: {                               /* crush the rock = mineable block (3 hits) */
        u8 dmg = (u8)(t / MINE_STEP);                 /* 0,1,2 then gone */
        spr_show(A_OAM_CHAR, cx, FLUX_GFX(0), OBJPAL_PLAYER);   /* facing right, mining */
        spr_show(A_OAM_GEM,  GEM_X, GEM_GFX(0), OBJPAL_FALLS);  /* gem waits its turn */
        if (t == MINE_STEP || t == MINE_STEP * 2 || t == CRUSH_DUR) audio_sfx(SFX_CRUSH);  /* block = CRUSH */
        if (dmg < 3) spr_show(A_OAM_ROCK, ROCK_X, ROCK_GFX(dmg), OBJPAL_BLOCK_ATTRACT);
        else         oamSetVisible((u16)(A_OAM_ROCK * 4), OBJ_HIDE);
        if (t >= CRUSH_DUR + 6) { oamSetVisible((u16)(A_OAM_ROCK * 4), OBJ_HIDE); st = A_FLUX_STEP; t = 0; }
        break; }

    case A_FLUX_STEP:                                 /* step into the rock's old spot */
        cx += WALK_SPD;
        spr_show(A_OAM_CHAR, cx, FLUX_GFX(bob), OBJPAL_PLAYER);
        spr_show(A_OAM_GEM,  GEM_X, GEM_GFX(0), OBJPAL_FALLS);
        if (cx >= ROCK_X) { cx = ROCK_X; st = A_FLUX_GEM; t = 0; }
        break;

    case A_FLUX_GEM: {                                /* crush the gem (3 hits) */
        u8 dmg = (u8)(t / MINE_STEP);                 /* 0,1,2 then gone */
        spr_show(A_OAM_CHAR, cx, FLUX_GFX(0), OBJPAL_PLAYER);
        if (t == MINE_STEP || t == MINE_STEP * 2) audio_sfx(SFX_CRUSH);   /* hits 1,2 */
        if (dmg < 3) {
            spr_show(A_OAM_GEM, GEM_X, GEM_GFX(dmg), OBJPAL_FALLS);
        } else {
            oamSetVisible((u16)(A_OAM_GEM * 4), OBJ_HIDE);
            if (t == CRUSH_DUR) audio_sfx(SFX_COLLECT);   /* the in-game gem-crush sound */
        }
        if (t >= CRUSH_DUR + 18) { oamSetVisible((u16)(A_OAM_GEM * 4), OBJ_HIDE); st = A_FLUX_OUT; t = 0; }
        break; }

    case A_FLUX_OUT:
        cx += WALK_SPD;
        spr_show(A_OAM_CHAR, cx, FLUX_GFX(bob), OBJPAL_PLAYER);
        if (cx >= 256) { cx = 0; st = A_GLOOP; t = 0; name_show("GLOOP"); }
        break;

    case A_GLOOP:
        cx += WALK_SPD;
        spr_show(A_OAM_CHAR, cx, GLOOP_GFX(bob), OBJPAL_ENEMY);
        if (cx >= 256) { cx = 0; eyei = 0; st = A_CLANKY_IN; t = 0; name_show("CLANKY"); }
        break;

    case A_CLANKY_IN:
        cx += WALK_SPD;
        if ((t % 12) == 0) eyei = (u8)((eyei + 1) & 3);
        spr_show(A_OAM_CHAR, cx, CLANKY_GFX(eyeseq[eyei]), OBJPAL_ROBOT);
        if (cx >= CENTER_X) { cx = CENTER_X; st = A_CLANKY_ZAP; t = 0; }
        break;

    case A_CLANKY_ZAP: {
        u8 zf = (u8)((t >> 2) & 3);                   /* lightning flicker frame 0..3 */
        u8 k;
        if ((t % 12) == 0) eyei = (u8)((eyei + 1) & 3);
        spr_show(A_OAM_CHAR, cx, CLANKY_GFX(eyeseq[eyei]), OBJPAL_ROBOT);
        if (t == 1) audio_sfx(SFX_ZAP);
        /* full horizontal bolt: ROBOT_ZAP_RANGE 16px segments extending right of
         * Clanky, same tile packing as render_lightning (L = frame*3 + segment). */
        for (k = 0; k < ROBOT_ZAP_RANGE; k++) {
            u8 L = (u8)(zf * 3 + k);
            spr_show((u8)(A_OAM_ZAP + k), (s16)(cx + 16 * (k + 1)),
                     (u16)(OBJN_ZAPH + (L >> 3) * 32 + (L & 7) * 2), OBJPAL_ZAPH);
        }
        if (t >= ZAP_LEN) {
            for (k = 0; k < ROBOT_ZAP_RANGE; k++) oamSetVisible((u16)((A_OAM_ZAP + k) * 4), OBJ_HIDE);
            st = A_CLANKY_OUT; t = 0;
        }
        break; }

    case A_CLANKY_OUT:
        cx += WALK_SPD;
        if ((t % 12) == 0) eyei = (u8)((eyei + 1) & 3);
        spr_show(A_OAM_CHAR, cx, CLANKY_GFX(eyeseq[eyei]), OBJPAL_ROBOT);
        if (cx >= 256) { st = A_DONE; t = 0; }
        break;

    case A_DONE:
    default:
        return 1;
    }
    return 0;
}
