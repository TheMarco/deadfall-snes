/* Deadfall SNES port - battery-backed SRAM save (high score + suspended run). */
#ifndef SRAM_H
#define SRAM_H

#include "types.h"

extern u32 save_hiscore;        /* best score ever achieved (0 on a fresh cart) */

/* The suspended run: lets the player power off and pick the run back up at the
 * start of the level they were on -- WITH its remaining lives and continues, so
 * persistence can never hand out more than the normal 3 continues. The run is
 * erased on a final game over (0 continues) and on victory. */
extern u8  save_run_valid;      /* 1 = a run is suspended and can be resumed   */
extern u8  save_run_level;      /* level the run resumes at (1..MAX_LEVELS)    */
extern u32 save_run_score;      /* score at that level's start                 */
extern u8  save_run_lives;      /* lives remaining                             */
extern u8  save_run_continues;  /* continues remaining                         */

void save_load(void);           /* read+validate SRAM; defaults on fresh/corrupt data */
void save_commit(void);         /* write the current save_* values back to SRAM       */

#endif /* SRAM_H */
