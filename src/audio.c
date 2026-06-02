/* Deadfall SNES port - audio engine (PVSnesLib snesmod, SPC700).
 *
 * One soundbank: MOD_SFX (the effects bank, samples = the 12 gameplay SFX)
 * plus the music modules that follow it in AUDIOFILES. Effects are global
 * (loaded once via spcLoadEffect) and play over whatever music module is
 * loaded. SFX are sampled at 8kHz, so spcEffect pitch = 2 (=8kHz) plays them
 * at their natural rate. */
#include <snes.h>
#include "audio.h"
#include "res/soundbank.h"

/* LoROM 32KB banks: the ~46KB soundbank spans two banks -> register both,
 * reverse order (see PVSnesLib musicGreaterThan32k example). */
extern char SOUNDBANK__0, SOUNDBANK__1;

void audio_init(void) {
    spcBoot();                      /* boot sm-spc onto the SPC700 (once) */
}

void audio_load(void) {
    u8 i;
    spcSetBank(&SOUNDBANK__1);
    spcSetBank(&SOUNDBANK__0);
    spcStop();
    spcLoad(MOD_SFX);               /* the effects bank module */
    for (i = 0; i < SFX_COUNT; i++)
        spcLoadEffect(i);           /* resident effect samples in ARAM */
}

void audio_sfx(u8 idx) {
    spcEffect(2, idx, 15 * 16 + 8); /* pitch=2 (8kHz), vol=15, pan=centre */
}

/* Switch the playing module. spcLoad re-inits ARAM, so the effect samples must
 * be reloaded afterwards. Blocking + slowish - call during level transitions. */
void audio_play_music(u8 module) {
    u8 i;
    spcStop();
    spcLoad(module);
    for (i = 0; i < SFX_COUNT; i++) spcLoadEffect(i);
    spcPlay(0);
}

/* All music points at the test.mid module for now (faithfulness proof). When
 * real per-level modules are chosen, map level -> its module here. */
void audio_music_level(u8 level) { (void)level; audio_play_music(MOD_MUSIC_TEST); }
void audio_music_frantic(void)   { audio_play_music(MOD_MUSIC_TEST); }
void audio_music_credits(void)   { audio_play_music(MOD_MUSIC_TEST); }

void audio_stop(void) { spcStop(); }

void audio_process(void) {
    spcProcess();                   /* feed the sound engine each frame */
}
