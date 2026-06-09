/* Deadfall SNES port - audio (snesmod / SPC700). */
#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"
#include "audio_sfx.h"   /* SFX_* effect indices (generated) */

void audio_init(void);      /* spcBoot - MUST be called first in main() */
void audio_load(void);      /* set soundbank, load SFX bank + all effects */
void audio_sfx(u8 idx);     /* trigger a sound effect (SFX_* index)       */
void audio_process(void);   /* pump the sound engine - call every frame   */
void audio_play_music(u8 module, u8 startpos);  /* BLOCKING load+play (reloads effects) */
void audio_music_level(u8 level);  /* play level n's theme (n=1..10)        */
void audio_music_frantic(void);    /* jump to frantic section (cheap spcPlay) */
void audio_music_credits(void);    /* victory/credits theme                 */
void audio_music_intro(void);      /* title-screen theme (loops)            */
void audio_music_levelfinished(void); /* level-complete jingle (plays once) */
void audio_music_gameover(void);   /* game-over theme                       */
void audio_music_fadeout(u8 frames); /* BLOCKING: ramp music to silence + stop */
void audio_music_pause(void);      /* mute the module (track keeps its position) */
void audio_music_resume(void);     /* restore the module volume after a pause     */
void audio_stop(void);             /* stop music (effects stay playable)    */

#endif /* AUDIO_H */
