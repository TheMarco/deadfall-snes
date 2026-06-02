#!/usr/bin/env python3
"""Convert all 10 level backgrounds to uniform 256x208 SNES BG2 backdrops.

Each level's full-world background image is cover-fit (scale-to-fill + centre
crop) into one 256x208 screen = 32x26 tiles (<=832 unique, fits the <=960 / 26KB
BG2 budget). One shared resident backdrop per level -> section transitions slide
the playfield over it with no reload. Outputs res/bg_<n>.{pic,map,pal}.
"""
import os
from make_bg import convert

SRC = "/Users/marcovhv/projects/GIT/cubed/public/backgrounds"
OUT = os.path.join(os.path.dirname(__file__), "..", "res")

total = 0
for n in range(1, 11):
    src = f"{SRC}/background-{n}.png"
    out = os.path.join(OUT, f"bg_{n}")
    tiles = convert(src, out, base_pal=2, npal=6, budget=960, fit=(256, 208))
    sz = os.path.getsize(out + ".pic")
    total += sz
    flag = "" if tiles <= 960 else "  *** OVER 960 ***"
    print(f"  level {n:2d}: {tiles:4d} tiles, {sz} B{flag}")
print(f"total bg tile data: {total} bytes ({total/1024:.0f} KB)")
