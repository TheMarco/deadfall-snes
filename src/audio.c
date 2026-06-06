/* Deadfall SNES port - audio engine (PVSnesLib snesmod, SPC700).
 *
 * One soundbank: MOD_SFX (the effects bank, samples = the 12 gameplay SFX)
 * plus the music modules that follow it in AUDIOFILES. Effects are global
 * (loaded once via spcLoadEffect) and play over whatever music module is
 * loaded. SFX are sampled at 8kHz, so spcEffect pitch = 2 (=8kHz) plays them
 * at their natural rate. */
#include <snes.h>
#include "audio.h"
#include "config.h"          /* MAX_LEVELS */
#include "music_layout.h"    /* MUSIC_CALM_START / MUSIC_FRANTIC_START (generated) */
#include "res/soundbank.h"
/* The soundbank spans several 32KB ROM banks (SOUNDBANK__0..); the count varies
 * with the music set, so the exact list of banks to register is auto-generated
 * from soundbank.asm into this header each build (tools/gen_soundbank_banks.py).
 * EVERY spanned bank must be registered: a missing one => link error, an
 * unregistered one => the linker discards it and its modules go silent. */
#include "soundbank_banks.h"    /* SPC_SET_ALL_BANKS() (generated) */

/* One-shot tracking: snesmod loops every module, so to play one ONCE we watch the
 * pattern position climb then drop (the loop wrap) and stop there (audio_process).
 * WRAM isn't zeroed at boot here, so these MUST be cleared in audio_init -- else a
 * garbage music_once would make audio_process poll spcGetMusicPosition before any
 * module plays. */
static u8 music_once = 0;
static u8 once_peak = 0;

void audio_init(void) {
    music_once = 0;                 /* statics aren't zero-initialised at boot */
    once_peak = 0;
    spcBoot();                      /* boot sm-spc onto the SPC700 (once) */
}

void audio_load(void) {
    u8 i;
    SPC_SET_ALL_BANKS();            /* register every soundbank ROM bank */
    spcStop();
    spcLoad(MOD_SFX);               /* the effects bank module */
    for (i = 0; i < SFX_COUNT; i++)
        spcLoadEffect(i);           /* resident effect samples in ARAM */
}

void audio_sfx(u8 idx) {
    spcEffect(2, idx, 15 * 16 + 8); /* pitch=2 (8kHz), vol=15, pan=centre */
}

/* Music playback volume (0..255). Lowered so music sits UNDER the SFX (which play
 * at full volume via spcEffect); raise/lower to taste. */
#define MUSIC_VOLUME 64

/* Load a module and start it at order `startpos`. spcLoad re-inits ARAM, so the
 * effect samples must be reloaded afterwards. BLOCKING + slowish (it uploads the
 * whole module + every SFX sample to the SPC) - call ONLY during level
 * transitions / scene changes, never mid-gameplay. The in-level calm<->frantic
 * swap goes through audio_music_frantic (a cheap spcPlay), not this. */
void audio_play_music(u8 module, u8 startpos) {
    u8 i;
    music_once = 0;                 /* default: loop (cleared for every track but gameover) */
    spcStop();
    spcLoad(module);
    for (i = 0; i < SFX_COUNT; i++) spcLoadEffect(i);
    spcPlay(startpos);
    spcSetModuleVolume(MUSIC_VOLUME);   /* duck music below the SFX */
}

/* Per-level themes: the 12 songs/*.zip AddmusicK scores rebuilt as snesmod
 * chiptunes (tools/build_songs.py). Indexed by level 1..10; the order matches
 * MUSICFILES in the Makefile, which fixes these MOD_* indices. */
static const u8 level_module[MAX_LEVELS] = {
    MOD_MUSIC_LEVEL1, MOD_MUSIC_LEVEL2, MOD_MUSIC_LEVEL3,  MOD_MUSIC_LEVEL4,
    MOD_MUSIC_LEVEL5, MOD_MUSIC_LEVEL6, MOD_MUSIC_LEVEL7,  MOD_MUSIC_LEVEL8,
    MOD_MUSIC_LEVEL9, MOD_MUSIC_LEVEL10,
};

void audio_music_level(u8 level) {
    if (level < 1) level = 1;
    if (level > MAX_LEVELS) level = MAX_LEVELS;
    /* Each level module embeds the frantic theme as its first section, so the
     * calm level theme lives at MUSIC_CALM_START (not order 0). */
    audio_play_music(level_module[level - 1], MUSIC_CALM_START);
}

/* All gems collected / exit open: the tense "Fatal Chase" theme. It's already
 * resident as the first section of the current level module, so we just jump the
 * order pointer there -- a cheap queued spcPlay, NOT a blocking spcLoad. This is
 * what kills the old freeze/slowdown when the exit opened. (A/B-tested: the music
 * swap is NOT the source of the frantic-phase stutter; that was the minimap
 * rebuild, fixed in render_minimap_blink.) */
void audio_music_frantic(void) {
    spcPlay(MUSIC_FRANTIC_START);
    spcSetModuleVolume(MUSIC_VOLUME);   /* spcPlay resets module volume; re-duck */
}
/* Victory/credits reuses the triumphant "Golden Anthem" (level 10's calm theme). */
void audio_music_credits(void)  { audio_play_music(MOD_MUSIC_LEVEL10, MUSIC_CALM_START); }
/* Game over: standalone module, played once from the top, then silence. */
void audio_music_gameover(void) {
    audio_play_music(MOD_MUSIC_GAMEOVER, 0);
    music_once = 1;
    once_peak = 0;
}

void audio_stop(void) { music_once = 0; spcStop(); }

void audio_process(void) {
    if (music_once) {               /* stop a one-shot track once its pattern wraps */
        u8 pos = spcGetMusicPosition();
        if (pos < 64) {             /* ignore implausible readings */
            if (pos > once_peak) once_peak = pos;             /* advancing */
            else if (once_peak && pos < once_peak) {          /* looped back -> stop */
                spcStop(); music_once = 0;
            }
        }
    }
    spcProcess();                   /* feed the sound engine each frame */
}
