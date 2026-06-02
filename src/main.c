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

GameState game;

enum { SC_TITLE, SC_PLAY, SC_GAMEOVER, SC_VICTORY };

static void scene_title(void) {
    render_hide_sprites();
    render_clear_screen();
    render_text(12, 11, "DEADFALL");
    render_text(8,  16, "PRESS  START");
    game_map_dirty = 1;
}

static void scene_gameover(void) {
    render_hide_sprites();
    render_clear_screen();
    render_text(11, 10, "GAME OVER");
    render_text(10, 13, "SCORE");  render_num(16, 13, game.score, 6);
    if (game.continues > 0) {
        render_text(7, 17, "START TO CONTINUE");
        render_text(12, 19, "LEFT:"); render_num(18, 19, game.continues, 1);
    } else {
        render_text(8, 17, "START FOR TITLE");
    }
    game_map_dirty = 1;
}

static void scene_victory(void) {
    render_hide_sprites();
    render_clear_screen();
    render_text(8,  10, "CONGRATULATIONS");
    render_text(10, 13, "YOU ESCAPED");
    render_text(10, 16, "SCORE");  render_num(16, 16, game.score, 6);
    render_text(8,  20, "PRESS  START");
    game_map_dirty = 1;
}

int main(void) {
    u8 scene = SC_TITLE;

    audio_init();        /* spcBoot - must run before consoleInit enables NMI */
    render_init();
    audio_load();        /* soundbank + resident SFX effects */
    scene_title();

    while (1) {
        u16 down = padsDown(0);

        switch (scene) {
            case SC_TITLE:
                if (down & PAD_START) { audio_sfx(SFX_MENUSEL); game_init(); scene = SC_PLAY; }
                break;

            case SC_PLAY:
                game_update();
                if (game.is_game_over)   { audio_stop(); scene = SC_GAMEOVER; scene_gameover(); }
                else if (game.is_victory){ audio_music_credits(); scene = SC_VICTORY; scene_victory(); }
                break;

            case SC_GAMEOVER:
                if (down & PAD_START) {
                    if (game.continues > 0) { game_continue(); scene = SC_PLAY; }
                    else                    { scene = SC_TITLE; scene_title(); }
                }
                break;

            case SC_VICTORY:
                if (down & PAD_START) { audio_stop(); scene = SC_TITLE; scene_title(); }
                break;
        }

        audio_process();   /* pump the sound engine every frame */
        WaitForVBlank();
        render_apply_scroll();   /* write slide scroll in vblank -> no tearing */
        if (game_map_dirty) { render_flush_map(); game_map_dirty = 0; }
    }
    return 0;
}
