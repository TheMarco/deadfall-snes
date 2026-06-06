#!/usr/bin/env python3
"""Equivalence test for the gravity_settle() optimisation (src/gravity.c).

The SNES build replaced the original O(rows^2 * cols) "bubble" settle (a full
208-cell re-scan per row of fall) with a single bottom-up O(rows*cols) column
compaction. This script proves the rewrite is behaviourally identical: for many
random section grids it checks that BOTH algorithms produce the exact same
  - final tile grid,
  - final mining-damage grid (gems carry partial damage as they fall),
  - set of fall records (type, from_x, from_y, to_x, to_y).

The subtle case it guards: a faller passing through a "fillable" but non-empty
cell (TILE_ROBOT_SPAWN = 6) -- the bubble overwrites it to EMPTY as the tile
bubbles through, so the compaction must clear the whole fall path, not just the
origin. (The first naive compaction failed exactly here.)

Run: python3 tools/test_gravity_equiv.py     (exits non-zero on any mismatch)
Mirrors include/config.h: GRID_ROWS=13, GRID_COLS=16, and the tile constants.
"""
import random
import copy
import sys

ROWS, COLS = 13, 16
FALL = {2, 3, 8}          # TILE_GEM, TILE_BOULDER, TILE_EXTRA_LIFE  (IS_FALLABLE)
FILL = {0, 6}             # TILE_EMPTY, TILE_ROBOT_SPAWN             (IS_FILLABLE)
SOLIDS = [1, 4, 5, 7]     # representative non-fallable, non-fillable barriers
GEM = 2

isf = lambda t: t in FALL
isfill = lambda t: t in FILL


def settle_bubble(g, d):
    """Original algorithm: drop every faller one cell per full-grid pass."""
    origin = [[y for _ in range(COLS)] for y in range(ROWS)]
    moved = True
    while moved:
        moved = False
        for y in range(ROWS - 2, -1, -1):
            for x in range(COLS):
                t = g[y][x]
                if not isf(t):
                    continue
                if not isfill(g[y + 1][x]):
                    continue
                g[y + 1][x] = t
                g[y][x] = 0
                if t == GEM:
                    dd = d[y][x]
                    d[y][x] = 0
                    if dd:
                        d[y + 1][x] = dd
                origin[y + 1][x] = origin[y][x]
                moved = True
    rec = []
    for y in range(ROWS):
        for x in range(COLS):
            t = g[y][x]
            if isf(t) and origin[y][x] != y:
                rec.append((t, x, origin[y][x], x, y))
    return rec


def settle_compact(g, d):
    """Optimised algorithm: one bottom-up compaction pass per column."""
    rec = []
    for x in range(COLS):
        write = ROWS - 1
        for y in range(ROWS - 1, -1, -1):
            t = g[y][x]
            if isf(t):
                if write != y:
                    g[write][x] = t
                    for c in range(y, write):      # clear origin + the fall path
                        g[c][x] = 0
                    if t == GEM:
                        dd = d[y][x]
                        d[y][x] = 0
                        if dd:
                            d[write][x] = dd
                    rec.append((t, x, y, x, write))
                write -= 1
            elif not isfill(t):
                write = y - 1
    return rec


def main():
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 300000
    random.seed(11)
    bad = 0
    pool = [0, 0, 0, 6, 2, 3, 8] + SOLIDS          # weight empties/fallers a bit
    for trial in range(trials):
        g = [[random.choice(pool) for _ in range(COLS)] for _ in range(ROWS)]
        d = [[(random.randint(1, 2) if g[y][x] == GEM and random.random() < 0.4 else 0)
              for x in range(COLS)] for y in range(ROWS)]
        g1, d1 = copy.deepcopy(g), copy.deepcopy(d)
        g2, d2 = copy.deepcopy(g), copy.deepcopy(d)
        r1 = settle_bubble(g1, d1)
        r2 = settle_compact(g2, d2)
        if g1 != g2 or d1 != d2 or sorted(r1) != sorted(r2):
            bad += 1
            if bad <= 3:
                print(f"MISMATCH trial {trial}: grid={g1 == g2} dmg={d1 == d2} "
                      f"rec={sorted(r1) == sorted(r2)}")
    print(f"trials={trials} mismatches={bad}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
