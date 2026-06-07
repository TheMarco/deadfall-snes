/* Deadfall SNES port - title-screen attract mode (character showcase).
 * Port of TitleScene.js's idle intro: after ~15s of no input on the title, the
 * cast (FLUX, GLOOP, CLANKY) parades across a black screen with name labels and
 * little mining / zapping demos, then it loops back to the title. */
#ifndef ATTRACT_H
#define ATTRACT_H

#include <snes.h>

void attract_begin(void);        /* enter attract mode: set up the canvas + first character */
u8   attract_update(u16 down);   /* advance one frame; returns 1 when finished OR interrupted */

#endif /* ATTRACT_H */
