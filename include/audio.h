/* Deadfall SNES port - audio (snesmod / SPC700). */
#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"
#include "audio_sfx.h"   /* SFX_* effect indices (generated) */

void audio_init(void);      /* spcBoot - MUST be called first in main() */
void audio_load(void);      /* set soundbank, load SFX bank + all effects */
void audio_sfx(u8 idx);     /* trigger a sound effect (SFX_* index)       */
void audio_process(void);   /* pump the sound engine - call every frame   */
void audio_play_music(u8 module);  /* load+play a module (reloads effects) */
void audio_music_level(u8 level);  /* play level n's theme (n=1..10)        */
void audio_music_frantic(void);    /* portal theme (all gems collected)     */
void audio_music_credits(void);    /* victory/credits theme                 */
void audio_music_gameover(void);   /* game-over theme                       */
void audio_stop(void);             /* stop music (effects stay playable)    */

#endif /* AUDIO_H */
