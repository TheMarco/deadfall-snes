/* Deadfall SNES port - battery-backed SRAM save (high score + suspended run).
 *
 * A 17-byte record at SRAM offset 0:
 *   bytes 0-3   magic "DFSV"
 *   bytes 4-7   high score, u32 little-endian
 *   byte  8     run valid (0/1)
 *   byte  9     run level (1..MAX_LEVELS)
 *   bytes 10-13 run score (at level start), u32 little-endian
 *   byte  14    run lives
 *   byte  15    run continues
 *   byte  16    checksum: (sum of bytes 0-15) ^ 0xA5
 * Fresh carts/emulators power up with random or 0xFF-filled SRAM, so the magic
 * AND checksum must both match or the record is treated as blank. The header
 * (hdr.asm) declares CARTRIDGETYPE $02 / SRAMSIZE $01 (2 KB, battery). */
#include <snes.h>
#include "sram.h"
#include "config.h"

u32 save_hiscore;
u8  save_run_valid;
u8  save_run_level;
u32 save_run_score;
u8  save_run_lives;
u8  save_run_continues;

#define SAVE_SIZE 17

static u8 save_checksum(u8 *b) {
    u8 i, s = 0;
    for (i = 0; i < SAVE_SIZE - 1; i++) s = (u8)(s + b[i]);
    return (u8)(s ^ 0xA5);
}

static void save_defaults(void) {
    save_hiscore = 0;
    save_run_valid = 0;
    save_run_level = 1;
    save_run_score = 0;
    save_run_lives = START_LIVES;
    save_run_continues = MAX_CONTINUES;
}

void save_load(void) {
    u8 b[SAVE_SIZE];
    save_defaults();             /* defaults first: WRAM isn't zeroed at boot */
    consoleLoadSram(b, SAVE_SIZE);
    if (b[0] != 'D' || b[1] != 'F' || b[2] != 'S' || b[3] != 'V') return;
    if (save_checksum(b) != b[SAVE_SIZE - 1]) return;
    save_hiscore = (u32)b[4] | ((u32)b[5] << 8) | ((u32)b[6] << 16) | ((u32)b[7] << 24);
    save_run_valid = (u8)(b[8] == 1);
    save_run_level = b[9];
    save_run_score = (u32)b[10] | ((u32)b[11] << 8) | ((u32)b[12] << 16) | ((u32)b[13] << 24);
    save_run_lives = b[14];
    save_run_continues = b[15];
    /* sanity-clamp the run; anything out of range invalidates it */
    if (save_run_level < 1 || save_run_level > MAX_LEVELS ||
        save_run_lives < 1 || save_run_lives > 9 ||
        save_run_continues > MAX_CONTINUES) {
        save_run_valid = 0;
        save_run_level = 1;
        save_run_score = 0;
        save_run_lives = START_LIVES;
        save_run_continues = MAX_CONTINUES;
    }
}

void save_commit(void) {
    u8 b[SAVE_SIZE];
    b[0] = 'D'; b[1] = 'F'; b[2] = 'S'; b[3] = 'V';
    b[4] = (u8)save_hiscore;
    b[5] = (u8)(save_hiscore >> 8);
    b[6] = (u8)(save_hiscore >> 16);
    b[7] = (u8)(save_hiscore >> 24);
    b[8] = save_run_valid;
    b[9] = save_run_level;
    b[10] = (u8)save_run_score;
    b[11] = (u8)(save_run_score >> 8);
    b[12] = (u8)(save_run_score >> 16);
    b[13] = (u8)(save_run_score >> 24);
    b[14] = save_run_lives;
    b[15] = save_run_continues;
    b[SAVE_SIZE - 1] = save_checksum(b);
    consoleCopySram(b, SAVE_SIZE);
}
