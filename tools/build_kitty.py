#!/usr/bin/env python3
"""Build the per-level 'level-up kitty' BG2 images for the level-complete screen,
from level-up-kitty1..9.png (34x34). Same faithful per-tile sub-palette packing as
build_title.py/build_logo.py: each kitty is centred near the top of a 256x256 black
canvas (above the LEVEL N COMPLETE text on BG3). Outputs res/kitty_N.{pic,map,pal}
for N=1..9; pure-Python (no gfx2snes)."""
import os, sys
import numpy as np
from collections import Counter
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
RES = os.path.join(PROJ, "res")
SRC = os.path.join(PROJ, "..", "cubed", "public", "sprites")
BASE_PAL = 2          # BG2 sub-palettes 2..7
NPAL = 6
BLACK = (0, 0, 0)
KITTY_X = (256 - 34) // 2   # centred horizontally
KITTY_Y = 18                # near the top (above the BG3 text)


def enc_tile(t):
    b = bytearray(32)
    for y in range(8):
        p0 = p1 = p2 = p3 = 0
        for x in range(8):
            v = int(t[y, x]); bit = 7 - x
            p0 |= (v & 1) << bit;        p1 |= ((v >> 1) & 1) << bit
            p2 |= ((v >> 2) & 1) << bit; p3 |= ((v >> 3) & 1) << bit
        b[y * 2] = p0; b[y * 2 + 1] = p1; b[16 + y * 2] = p2; b[16 + y * 2 + 1] = p3
    return bytes(b)


def bgr555(c):
    r, g, b = int(c[0]) >> 3, int(c[1]) >> 3, int(c[2]) >> 3
    return r | (g << 5) | (b << 10)


def convert(src_png, out_prefix):
    kit = Image.open(src_png).convert("RGBA")
    bg = Image.new("RGBA", (256, 256), (0, 0, 0, 255))
    bg.paste(kit, (KITTY_X, KITTY_Y), kit)
    arr = np.array(bg.convert("RGB"))

    tiles_cols = []
    for ty in range(32):
        for tx in range(32):
            blk = arr[ty*8:ty*8+8, tx*8:tx*8+8].reshape(-1, 3)
            cnt = Counter(tuple(p) for p in blk.tolist() if tuple(p) != BLACK)
            tiles_cols.append([c for c, _ in cnt.most_common()][:15])

    setc = Counter(frozenset(c) for c in tiles_cols if c)
    sets = list(setc.items())
    uc = set(c for cols in tiles_cols for c in cols)
    palettes = []
    for _ in range(NPAL):
        pal = set()
        while True:
            best, bs = None, -1
            for s, n in sets:
                if len(pal | s) <= 15 and not s <= pal:
                    sc = len((s - pal) & uc) * 100000 + n
                    if sc > bs:
                        bs, best = sc, s
            if best is None or bs < 0:
                break
            pal |= best; uc -= best
        palettes.append(list(pal))
    while len(palettes) < NPAL:
        palettes.append([])

    def _covered(cols):
        return any(set(cols) <= set(p) for p in palettes)
    while True:
        b = None
        for cols in tiles_cols:
            if not cols or _covered(cols):
                continue
            cs = set(cols)
            for pi, p in enumerate(palettes):
                m = cs - set(p)
                if len(p) + len(m) <= 15 and (b is None or len(m) < b[0]):
                    b = (len(m), pi, list(m))
        if b is None:
            break
        palettes[b[1]].extend(b[2])

    parr = [np.array(p, np.int32) if p else np.zeros((1, 3), np.int32) for p in palettes]

    def _tile_err(blk, pi):
        nb = blk.reshape(-1, 3); nb = nb[(nb != 0).any(1)]
        if len(nb) == 0:
            return 0
        return int(((nb[:, None, :].astype(np.int32) - parr[pi][None, :, :]) ** 2).sum(2).min(1).sum())

    tile_pal = []
    for ti in range(1024):
        ty, tx = divmod(ti, 32)
        blk = arr[ty*8:ty*8+8, tx*8:tx*8+8]
        tile_pal.append(min(range(NPAL), key=lambda i: _tile_err(blk, i)))

    pmap = [{BLACK: 0, **{c: i + 1 for i, c in enumerate(p)}} for p in palettes]

    def idx_for(c, pi):
        if c in pmap[pi]: return pmap[pi][c]
        if c == BLACK:    return 0
        return int(((np.array(c) - parr[pi]) ** 2).sum(1).argmin()) + 1

    uniq = {}; order = []; mapdata = bytearray()
    for ti in range(1024):
        ty, tx = divmod(ti, 32); pi = tile_pal[ti]
        blk = arr[ty*8:ty*8+8, tx*8:tx*8+8]
        idxt = np.zeros((8, 8), np.uint8)
        for y in range(8):
            for x in range(8):
                idxt[y, x] = idx_for(tuple(blk[y, x]), pi)
        key = idxt.tobytes()
        if key not in uniq:
            uniq[key] = len(order); order.append(idxt)
        ent = uniq[key] | ((BASE_PAL + pi) << 10)
        mapdata += bytes([ent & 0xFF, (ent >> 8) & 0xFF])

    pic = bytearray()
    for idxt in order:
        pic += enc_tile(idxt)
    paldata = bytearray()
    for p in palettes:
        entries = ([BLACK] + p + [BLACK] * 16)[:16]
        for c in entries:
            v = bgr555(c); paldata += bytes([v & 0xFF, (v >> 8) & 0xFF])

    open(os.path.join(RES, out_prefix + ".pic"), "wb").write(pic)
    open(os.path.join(RES, out_prefix + ".map"), "wb").write(mapdata)
    open(os.path.join(RES, out_prefix + ".pal"), "wb").write(paldata)
    return len(order), len(pic)


def main():
    total = 0
    for n in range(1, 10):
        src = os.path.join(SRC, "level-up-kitty%d.png" % n)
        nt, npic = convert(src, "kitty_%d" % n)
        total += npic
        print("kitty_%d: %d tiles, pic %dB" % (n, nt, npic))
    print("total kitty pic bytes: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
