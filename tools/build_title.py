#!/usr/bin/env python3
"""Build the title-screen BG2 image FAITHFULLY from title32.png (32-colour pixel
art). The photo-oriented make_bg (median-cut) shifted hand-picked colours (reds
drifted to purple); this instead preserves the EXACT colours: it packs the <=32
colours into BG2 sub-palettes and gives each 8x8 tile a sub-palette that contains
its colours. The only loss is in the few tiles that use >15 non-black colours
(SNES limit is 16 colours per 8x8 tile), reduced per-tile to their most-used 15.
Outputs res/title.{pic,map,pal}; pure-Python (no gfx2snes)."""
import os, sys
import numpy as np
from collections import Counter
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
RES = os.path.join(PROJ, "res")
SRC = os.path.join(PROJ, "title24.png")
BASE_PAL = 2          # BG2 sub-palettes 2..7 (render loads title.pal at CGRAM 32)
NPAL = 6
BLACK = (0, 0, 0)


def enc_tile(t):
    """8x8 index array -> 32-byte SNES 4bpp planar tile."""
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


def main():
    im = Image.open(SRC).convert("RGB")
    pad = Image.new("RGB", (256, 256), BLACK)
    pad.paste(im, (0, 0))                  # logo at top; bottom 32px off-screen
    arr = np.array(pad)

    # ---- Pass 1: per-tile non-black colour set (cap 15). Black is index 0
    # (transparent) and shows the black BG3 behind BG2, so all 15 palette slots
    # stay for real colours. ----
    tiles_cols = []
    for ty in range(32):
        for tx in range(32):
            blk = arr[ty*8:ty*8+8, tx*8:tx*8+8].reshape(-1, 3)
            cnt = Counter(tuple(p) for p in blk.tolist() if tuple(p) != BLACK)
            tiles_cols.append([c for c, _ in cnt.most_common()][:15])

    # ---- Pass 2a: build NPAL sub-palettes. SET-COVER prioritising tiles that
    # introduce NEW colours (so every colour, incl. the rare gems, gets a home),
    # then cheapest-completion fill to make as many tiles exactly representable as
    # possible. ----
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

    # ---- Pass 2b: assign each tile to the palette with MIN PIXEL ERROR. Counting
    # shared colours is NOT enough -- a yellow-gem tile must pick the palette that
    # actually contains its yellow, even if another palette shares more of its rock
    # colours. Minimising real pixel error preserves the distinct gem colours. ----
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
        if c == BLACK:    return 0               # transparent -> black BG3 behind
        return int(((np.array(c) - parr[pi]) ** 2).sum(1).argmin()) + 1   # nearest

    # ---- Pass 3: build deduped tiles (index patterns) + tilemap ----
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

    open(os.path.join(RES, "title.pic"), "wb").write(pic)
    open(os.path.join(RES, "title.map"), "wb").write(mapdata)
    open(os.path.join(RES, "title.pal"), "wb").write(paldata)
    used = sum(1 for p in palettes if p)
    print("title: %d unique tiles, pic %dB %s, %d/%d sub-palettes" %
          (len(order), len(pic), "OK" if len(pic) < 32768 else "*** OVER 32KB ***", used, NPAL))
    return 0 if len(pic) < 32768 else 1


if __name__ == "__main__":
    sys.exit(main())
