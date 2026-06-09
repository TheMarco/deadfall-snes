/* Deadfall SNES port - entry point + scene/state machine.
 *   TITLE -> PLAY -> (lose all lives) GAME OVER -> continue or TITLE
 *                 -> (clear level 10)  VICTORY  -> TITLE
 * Title/Game-over/Victory are HUD-font text screens (real title art arrives
 * with the background pipeline); PLAY is the full game loop in game.c.
 */
#include <snes.h>
#include "config.h"
#include "types.h"
#include "game.h"
#include "render.h"
#include "audio.h"
#include "attract.h"
#include "ai.h"
#include "sram.h"

GameState game;

enum { SC_LOGO, SC_TITLE, SC_PLAY, SC_GAMEOVER, SC_VICTORY, SC_ATTRACT, SC_RESUME };

/* Boot LogoScene physics (port of LogoScene.js): the studio logo drops in under
 * gravity, bounces twice (crush SFX each impact), settles, holds ~3s, fades to the
 * title. Positions are 8.8 fixed-point screen pixels (logo top edge). */
#define LOGO_START     (-48)   /* start fully above the visible top */
#define LOGO_TARGET     92     /* landed top edge (logo centred on the 224px screen) */
#define LOGO_GRAV       105    /* gravity per frame, 8.8 -- snappier drop + bounce */
#define LOGO_DAMP       102    /* bounce keeps 0.4 of speed (102/256) */
#define LOGO_MAXBOUNCE  2
#define LOGO_HOLD       84     /* ~1.4s settled before fading */
#define LOGO_FADE       18     /* fade-to-title frames */
#define ATTRACT_IDLE_FRAMES 900 /* ~15s of no input on the title -> attract mode (matches JS) */

static void scene_title(u8 reload_music) {
    render_hide_sprites();
    render_clear_screen();                  /* clear leftover logo sprites/BG3 (Mode 3 set below) */
    render_show_title();                    /* Mode-3 8bpp title image; un-blanks showing the title */
    if (reload_music)                       /* attract returns here with the title theme STILL
                                             * playing -> skip the reload so the loop is seamless */
        audio_music_intro();                /* AFTER show_title so the ~1s SPC load freezes on the title */
    game_map_dirty = 1;
}

static void scene_gameover(void) {
    render_load_font(1);                     /* transparent font: text floats on the backdrop */
    render_hide_sprites();
    render_clear_screen();
    render_text(11, 10, "GAME OVER");
    render_text(10, 13, "SCORE");  render_num(16, 13, game.score, 6);
    render_text(10, 15, "BEST");   render_num(16, 15, save_hiscore, 6);
    if (game.continues > 0) {
        render_text(7, 18, "START TO CONTINUE");
        render_text(12, 20, "LEFT:"); render_num(18, 20, game.continues, 1);
    } else {
        render_text(8, 18, "START FOR TITLE");
    }
    game_map_dirty = 1;
}

/* Resume screen (SELECT on the title while a run is suspended in SRAM): shows
 * the checkpoint -- level, score, lives, continues -- and offers to pick it up.
 * Same text-scene plumbing as the game-over screen; the title theme keeps
 * playing underneath. The run carries its OWN lives/continues, so resuming is
 * a between-sessions break, never a way around the 3-continue budget. */
static void scene_resume(void) {
    render_load_font(1);                     /* transparent font on the black backing */
    render_hide_sprites();
    render_clear_screen();
    render_text(9, 7, "SUSPENDED GAME");
    render_text(9, 11, "LEVEL");     render_num(17, 11, save_run_level, 2);
    render_text(9, 13, "SCORE");     render_num(17, 13, save_run_score, 6);
    render_text(9, 15, "LIVES");     render_num(17, 15, save_run_lives, 1);
    render_text(9, 17, "CONTINUES"); render_num(19, 17, save_run_continues, 1);
    render_text(8, 20, "START TO RESUME");
    render_text(8, 22, "SELECT FOR TITLE");
    game_map_dirty = 1;
}

/* Victory -> multi-stage credits sequence (port of CreditsScene), auto-advancing.
 * Stage 4 holds and waits for START -> title. */
#define CREDITS_STAGES   4
#define CREDITS_DWELL    200    /* ~3.3s per stage */
static void scene_credits(u8 stage) {
    render_load_font(1);                     /* transparent font: text floats on the backdrop */
    render_hide_sprites();
    render_clear_screen();
    switch (stage) {
        case 0:
            render_text(11, 9,  "YOU DID IT");
            render_text(9,  12, "FLUX IS SAVED");
            render_text(9,  16, "FINAL SCORE"); render_num(21, 16, game.score, 6);
            break;
        case 1:
            render_text(12, 10, "DEADFALL");
            render_text(11, 13, "A GAME BY");
            render_text(4,  16, "MARCO VAN HYLCKAMA VLIEG");
            break;
        case 2:
            render_text(7,  12, "COPYRIGHT 2025-2026");
            render_text(7,  15, "ALL RIGHTS RESERVED");
            break;
        default:
            render_text(9,  11, "FOLLOW ME ON X");
            render_text(8,  14, "X.COM/AIANDDESIGN");
            render_text(10, 19, "PRESS  START");
            break;
    }
    game_map_dirty = 1;
}

int main(void) {
    u8 scene = SC_LOGO;  /* boot: studio logo (render_init already staged it on BG2) */
    u16 pad_prev = 0;    /* for manual edge detection (see below) */
    u8  cr_stage = 0;    /* credits sequence stage */
    u16 cr_timer = 0;    /* frames until the next credits stage */
    u16 idle = 0;        /* frames of no input on the title -> attract mode */
    s16 logo_y = (s16)(LOGO_START << 8);  /* LogoScene state (8.8 fixed) */
    s16 logo_v = 0;
    u8  logo_state = 0;  /* 0=falling, 1=settled/hold, 2=fading */
    u8  logo_bounce = 0;
    u16 logo_timer = 0;

    audio_init();        /* spcBoot - must run before consoleInit enables NMI */
    render_init();       /* boots into render_show_logo (logo on black, above screen) */
    audio_load();        /* soundbank + resident SFX effects */
    save_load();         /* battery SRAM: high score + suspended run (defaults if blank) */
    game.alarm_active = 0;   /* WRAM isn't zeroed at boot; render_apply_alarm reads this
                              * before game_init runs, so a garbage value tinted the title red */
    game_map_dirty = 0;      /* likewise: don't trigger a stale flush during the LogoScene */
    render_logo_reset();     /* clear sparkle/particle pools (WRAM not zeroed at boot) */
    /* Install the vblank map-upload hook now -- AFTER render_init's heavy setup
     * DMA and after the dirty flags are cleared, so the NMI can't fire a stray
     * DMA on top of init or upload a stale map during the logo. */
    nmiSet(render_vblank);

    while (1) {
        /* Edge-detect newly-pressed keys from the CURRENT pad state. We do NOT
         * use padsDown()/pad_keysdown[] here: this PVSnesLib build's auto-joypad
         * ISR updates pad_keys[] (padsCurrent works -- the gameplay loop relies on
         * it) but does not reliably maintain pad_keysdown[], so padsDown() stayed
         * 0 and "PRESS START" never fired (the bug was hidden while OpenEmu kept
         * auto-resuming a save state past the title). */
        u16 cur  = (u16)(padsCurrent(0) & PAD_BUTTONS);  /* drop the controller-signature
                                                          * nibble (see PAD_BUTTONS) */
        u16 down = (u16)(cur & ~pad_prev);
        pad_prev = cur;

        switch (scene) {
            case SC_LOGO:
                render_logo_particles();               /* advance the landing burst every frame */
                if (logo_state == 0) {                 /* falling under gravity */
                    logo_v = (s16)(logo_v + LOGO_GRAV);
                    logo_y = (s16)(logo_y + logo_v);
                    if (logo_y >= (LOGO_TARGET << 8)) {
                        logo_y = (s16)(LOGO_TARGET << 8);
                        audio_sfx(SFX_CRUSH);          /* crush on every impact (bounce + land) */
                        render_logo_burst(128, (s16)(LOGO_TARGET + 40), 14);  /* spray from logo bottom */
                        if (logo_bounce < LOGO_MAXBOUNCE) {
                            logo_v = (s16)(-(((s32)logo_v * LOGO_DAMP) >> 8));   /* bounce up */
                            logo_bounce++;
                        } else { logo_v = 0; logo_state = 1; logo_timer = 0; }  /* settled */
                    }
                    render_logo_pos((s16)(logo_y >> 8));
                } else if (logo_state == 1) {          /* settled, hold ~3s + twinkle */
                    render_logo_sparkles(2);           /* 2 new sparkles every frame (dense) */
                    if (++logo_timer >= LOGO_HOLD) { logo_state = 2; logo_timer = LOGO_FADE; }
                } else {                               /* fading out -> title */
                    render_logo_sparkles(0);           /* keep the live sparkles twinkling, no new ones */
                    if (logo_timer) { logo_timer--; setBrightness((u8)(15 * logo_timer / LOGO_FADE)); }
                    if (logo_timer == 0) { scene = SC_TITLE; scene_title(1); }
                }
                if ((down & PAD_START) && logo_state < 2) {   /* START skips to the fade */
                    logo_state = 2; logo_timer = LOGO_FADE;
                }
                break;

            case SC_TITLE:
                if (down & PAD_START) {
                    audio_sfx(SFX_MENUSEL);
                    ai_rng_seed(snes_vblank_count);  /* human START timing = real entropy */
                    audio_music_fadeout(45);   /* fade the title theme out before the level loads */
                    game_init();
                    scene = SC_PLAY;
                    idle = 0;
                } else if ((down & PAD_SELECT) && save_run_valid) {
                    audio_sfx(SFX_MENUSEL);    /* SELECT -> resume the suspended run */
                    scene = SC_RESUME;
                    scene_resume();
                    idle = 0;
                } else if (cur) {
                    idle = 0;                  /* any input resets the attract countdown */
                } else if (++idle >= ATTRACT_IDLE_FRAMES) {
                    idle = 0;
                    attract_begin();           /* 15s idle -> show off the cast */
                    scene = SC_ATTRACT;
                }
                break;

            case SC_RESUME:
                if (down & PAD_START) {
                    audio_sfx(SFX_MENUSEL);
                    ai_rng_seed(snes_vblank_count);
                    audio_music_fadeout(45);
                    game_resume();
                    scene = SC_PLAY;
                } else if (down & (PAD_SELECT | PAD_B)) {
                    audio_sfx(SFX_MENUMOVE);   /* back to the title (theme never stopped) */
                    scene = SC_TITLE; scene_title(0); idle = 0;
                }
                break;

            case SC_PLAY:
                game_update();
                if (game.is_game_over || game.is_victory) {
                    game.alarm_active = 0;            /* stop the vignette */
                    /* persist the final score (it grew past the last checkpoint);
                     * game.c already staged the run fields (pre-spent continue on
                     * game over, run cleared on victory / final game over). */
                    if (game.score > save_hiscore) save_hiscore = game.score;
                    if (game.is_victory) save_run_valid = 0;
                    save_commit();
                }
                if (game.is_game_over)   { audio_music_gameover(); scene = SC_GAMEOVER; scene_gameover(); }
                else if (game.is_victory){ audio_music_credits(); scene = SC_VICTORY;
                                           cr_stage = 0; cr_timer = CREDITS_DWELL; scene_credits(0); }
                break;

            case SC_GAMEOVER:
                if (down & PAD_START) {
                    if (game.continues > 0) { game_continue(); scene = SC_PLAY; }
                    else                    { scene = SC_TITLE; scene_title(1); }
                }
                break;

            case SC_VICTORY:
                if (cr_stage < CREDITS_STAGES - 1) {   /* auto-advance through the stages */
                    if (cr_timer) cr_timer--;
                    if (cr_timer == 0 || (down & PAD_START)) {
                        cr_stage++; cr_timer = CREDITS_DWELL;
                        if (down & PAD_START) audio_sfx(SFX_MENUSEL);
                        scene_credits(cr_stage);
                    }
                } else if (down & PAD_START) {          /* final stage -> back to title */
                    audio_stop(); scene = SC_TITLE; scene_title(1);
                }
                break;

            case SC_ATTRACT:
                /* idle character showcase; any press (or its natural end) -> title.
                 * Fade the attract canvas to black, swap scenes blanked (scene_title's
                 * render_clear_screen briefly runs Mode 1 with BG1 still pointing at
                 * the title's leftover 8bpp tiles -- garbage if shown), then fade the
                 * title back in. Replaces the old hard blank+cut. */
                if (attract_update(down)) {
                    u8 b;
                    for (b = 15; b != 0; b--) {            /* ~0.25s fade to black */
                        setBrightness((u8)(b - 1));
                        audio_process(); WaitForVBlank();
                    }
                    setScreenOff();
                    scene = SC_TITLE; scene_title(0); idle = 0;   /* 0: title theme never stopped */
                    setBrightness(0);                      /* show_title un-blanks at full */
                    for (b = 0; b <= 15; b++) {            /* ~0.25s fade the title in */
                        setBrightness(b);
                        audio_process(); WaitForVBlank();
                    }
                }
                break;
        }

        audio_process();   /* pump the sound engine every frame */
        WaitForVBlank();   /* the BG-map upload now happens in render_vblank (the NMI hook),
                            * so game_update isn't delayed by the DMA and the copy can't
                            * spill into active display. */
        render_apply_scroll();   /* re-assert scroll after the lib's NMI reset it */
        render_apply_alarm();    /* pulse the red alarm vignette (exit open) */
        render_minimap_blink();  /* blink the minimap exit dot (1 CGRAM write, no rebuild) */
    }
    return 0;
}
