/* Deadfall SNES port - multi-section world grid with wrapping (torus).
 * Port of ../cubed/src/CubeWorld.js. */
#ifndef WORLD_H
#define WORLD_H

#include "types.h"

/* Load level (1..10): copy data into RAM, reset damage, scan spawn/portal/
 * robot points, count accessible gems, set current section to the portal. */
void world_load_level(u8 level);

u8   world_section_index(u8 row, u8 col);        /* row*world_cols + col */
u8  *world_grid_at(u8 row, u8 col);              /* &sections[idx][0][0]  */

/* Tile access in an explicit section. */
u8   world_tile_at(u8 row, u8 col, u8 x, u8 y);
void world_set_tile_at(u8 row, u8 col, u8 x, u8 y, u8 t);

/* Tile access in the current (active) section. */
u8   world_get_tile(u8 x, u8 y);
void world_set_tile(u8 x, u8 y, u8 t);

void world_switch_section(u8 row, u8 col);
void world_adjacent(u8 dir, SectionRef *out);    /* wrapped neighbor of current */

/* Where does a tile pushed to (px,py) by (dx,dy) land when it crosses a
 * section edge? Fills sec/ox/oy and returns TRUE if a valid cross target. */
u8   world_cross_push_target(s8 px, s8 py, s8 dx, s8 dy,
                             SectionRef *sec, u8 *ox, u8 *oy);

/* Damage model (3 hits to destroy). Returns TRUE when the tile is destroyed. */
u8   world_damage_tile(u8 x, u8 y);
u8   world_get_damage(u8 x, u8 y);
void world_set_damage(u8 x, u8 y, u8 d);
void world_clear_damage(u8 x, u8 y);

u16  world_count_gems(void);                     /* over accessible sections */

#endif /* WORLD_H */
