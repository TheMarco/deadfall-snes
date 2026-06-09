/* Deadfall SNES port - shared enemy/robot movement primitives (BaseEnemy.js). */
#include "ai.h"
#include "config.h"
#include "world.h"

#define IS_OPEN(t) ((t) == TILE_EMPTY)

static u16 rng_state = 0x2D9F;

/* Mix real entropy into the LFSR (the vblank count when the player pressed
 * START -- human timing). Without this every playthrough keeps the boot
 * constant and enemies make identical "random" choices game after game. Never
 * let the state hit 0: a zeroed xorshift is stuck at 0 forever. */
void ai_rng_seed(u16 s) {
    rng_state ^= s;
    if (rng_state == 0) rng_state = 0x2D9F;
}

u16 ai_rng(void) {
    rng_state ^= (u16)(rng_state << 7);
    rng_state ^= (u16)(rng_state >> 9);
    rng_state ^= (u16)(rng_state << 8);
    return rng_state;
}

s16 ai_wrapped_diff(s16 target, s16 current, s16 max) {
    s16 diff = (s16)(target - current);
    s16 half = (s16)(max / 2);
    if (diff > half || (diff == half && diff > 0)) diff = (s16)(diff - max);
    if (diff < (s16)(-half)) diff = (s16)(diff + max);
    return diff;
}

u8 ai_can_move(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy) {
    s8 nx = (s8)(x + dx), ny = (s8)(y + dy);
    u8 r = srow, c = scol, dxc, dyc;

    if (nx < 0 || nx >= GRID_COLS || ny < 0 || ny >= GRID_ROWS) {
        dxc = (u8)nx; dyc = (u8)ny;
        if (nx < 0)               { c = (u8)((c + game.world_cols - 1) % game.world_cols); dxc = GRID_COLS - 1; }
        else if (nx >= GRID_COLS) { c = (u8)((c + 1) % game.world_cols);                   dxc = 0; }
        if (ny < 0)               { r = (u8)((r + game.world_rows - 1) % game.world_rows); dyc = GRID_ROWS - 1; }
        else if (ny >= GRID_ROWS) { r = (u8)((r + 1) % game.world_rows);                   dyc = 0; }
        return IS_OPEN(world_tile_at(r, c, dxc, dyc));
    }
    return IS_OPEN(game.sections[world_section_index(r, c)][ny][nx]);
}

/* Normalise a single-axis step from (srow,scol,x,y) by (dx,dy) into the
 * destination cell's section + local coords (torus wrap). */
static void ai_dest_cell(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy,
                         u8 *tr, u8 *tc, u8 *tx, u8 *ty) {
    s8 nx = (s8)(x + dx), ny = (s8)(y + dy);
    *tr = srow; *tc = scol;
    if (nx < 0)               { *tc = (u8)((scol + game.world_cols - 1) % game.world_cols); nx = GRID_COLS - 1; }
    else if (nx >= GRID_COLS) { *tc = (u8)((scol + 1) % game.world_cols);                   nx = 0; }
    if (ny < 0)               { *tr = (u8)((srow + game.world_rows - 1) % game.world_rows); ny = GRID_ROWS - 1; }
    else if (ny >= GRID_ROWS) { *tr = (u8)((srow + 1) % game.world_rows);                   ny = 0; }
    *tx = (u8)nx; *ty = (u8)ny;
}

/* Does an actor occupy cell (tr,tc,tx,ty) -- either standing on it, or (if moving)
 * sliding into it? The actor's target may be stored out-of-bounds, so normalise it. */
static u8 actor_on(u8 esr, u8 esc, u8 ex, u8 ey, u8 moving, s8 etx, s8 ety,
                   u8 tr, u8 tc, u8 tx, u8 ty) {
    if (esr == tr && esc == tc && ex == tx && ey == ty) return 1;
    if (moving) {
        u8 mr = esr, mc = esc; s8 nx = etx, ny = ety;
        if (nx < 0)               { mc = (u8)((esc + game.world_cols - 1) % game.world_cols); nx = GRID_COLS - 1; }
        else if (nx >= GRID_COLS) { mc = (u8)((esc + 1) % game.world_cols);                   nx = 0; }
        if (ny < 0)               { mr = (u8)((esr + game.world_rows - 1) % game.world_rows); ny = GRID_ROWS - 1; }
        else if (ny >= GRID_ROWS) { mr = (u8)((esr + 1) % game.world_rows);                   ny = 0; }
        if (mr == tr && mc == tc && (u8)nx == tx && (u8)ny == ty) return 1;
    }
    return 0;
}

/* Would an enemy stepping (dx,dy) land on the robot? Enemies pass through each
 * other (JS AI), but Clanky is solid, so they treat its cell (and the cell it is
 * sliding into) as blocked. */
u8 ai_robot_at_step(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy) {
    Robot *rb;
    u8 tr, tc, tx, ty;
    if (game.robot_count == 0) return 0;
    rb = &game.robots[0];
    if (!rb->alive) return 0;
    ai_dest_cell(srow, scol, x, y, dx, dy, &tr, &tc, &tx, &ty);
    return actor_on(rb->section_row, rb->section_col, rb->x, rb->y,
                    rb->is_moving, (s8)rb->target_x, (s8)rb->target_y, tr, tc, tx, ty);
}

/* Would the robot stepping (dx,dy) land on any live enemy? (Symmetric block so
 * Clanky never walks through Gloop either.) */
u8 ai_enemy_at_step(u8 srow, u8 scol, u8 x, u8 y, s8 dx, s8 dy) {
    u8 tr, tc, tx, ty, i;
    ai_dest_cell(srow, scol, x, y, dx, dy, &tr, &tc, &tx, &ty);
    for (i = 0; i < game.enemy_count; i++) {
        Enemy *e = &game.enemies[i];
        if (!e->alive) continue;
        if (actor_on(e->section_row, e->section_col, e->x, e->y,
                     e->is_moving, (s8)e->target_x, (s8)e->target_y, tr, tc, tx, ty)) return 1;
    }
    return 0;
}

/* Flat distance field (index = y*16+x) + generation stamp (never cleared,
 * just bumped). Flat arrays + shift indexing avoid 816-tcc's slow multiplies
 * in the BFS hot loop, which is critical on the multiply-less 65816. */
static u8 df[SECTION_TILES];
static u8 dfgen[SECTION_TILES];
static u8 cur_gen = 0;

/* resumable flood state */
static u8  fq[256];          /* queue of flat indices (each cell enqueued once) */
static u8  fhead, ftail;
static u8 *fsec;             /* section grid base (flat y*16+x)                 */
static u8  factive;

void ai_distfield_begin(u8 srow, u8 scol, u8 tx, u8 ty) {
    u8 p;
    cur_gen++;
    if (cur_gen == 0) { u16 i; for (i = 0; i < SECTION_TILES; i++) dfgen[i] = 0; cur_gen = 1; }
    fhead = ftail = 0; factive = 0;
    if (tx >= GRID_COLS || ty >= GRID_ROWS) return;
    fsec = &game.sections[world_section_index(srow, scol)][0][0];
    p = (u8)((ty << 4) + tx);
    df[p] = 0; dfgen[p] = cur_gen;
    fq[ftail++] = p;
    factive = 1;
}

void ai_distfield_work(u8 budget) {
    u8 *sec = fsec;
    u8 done = 0;
    if (!factive) return;
    while (fhead != ftail && done < budget) {
        u8 p = fq[fhead++], cd = df[p], x, y, nd;
        done++;
        if (cd >= BFS_MAX_DEPTH) continue;
        nd = (u8)(cd + 1);
        x = (u8)(p & 15); y = (u8)(p >> 4);
        if (x > 0)             { u8 _n=(u8)(p-1);  if (dfgen[_n]!=cur_gen && sec[_n]==TILE_EMPTY){ df[_n]=nd; dfgen[_n]=cur_gen; fq[ftail++]=_n; } }
        if (x < GRID_COLS - 1) { u8 _n=(u8)(p+1);  if (dfgen[_n]!=cur_gen && sec[_n]==TILE_EMPTY){ df[_n]=nd; dfgen[_n]=cur_gen; fq[ftail++]=_n; } }
        if (y > 0)             { u8 _n=(u8)(p-16); if (dfgen[_n]!=cur_gen && sec[_n]==TILE_EMPTY){ df[_n]=nd; dfgen[_n]=cur_gen; fq[ftail++]=_n; } }
        if (y < GRID_ROWS - 1) { u8 _n=(u8)(p+16); if (dfgen[_n]!=cur_gen && sec[_n]==TILE_EMPTY){ df[_n]=nd; dfgen[_n]=cur_gen; fq[ftail++]=_n; } }
    }
    if (fhead == ftail) factive = 0;
}

u8 ai_dist(u8 x, u8 y) {
    u8 p;
    if (x >= GRID_COLS || y >= GRID_ROWS) return 255;
    p = (u8)((y << 4) + x);
    if (dfgen[p] != cur_gen) return 255;
    return df[p];
}
