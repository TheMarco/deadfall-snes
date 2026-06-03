#!/usr/bin/env python3
"""Generate per-level BG2 parallax textures: res/bgtex_1..10.{pic,map,pal}.

Level 1 reuses the original, user-approved texture (res/bgtex.* from test3.png).
Levels 2..10 are built from bg/bg2.png .. bg/bg10.png. Each is a 256x256 seamless
texture quantized to 96 colors (6 BG sub-palettes) and <=1023 unique 8x8 tiles, so
its .pic is < 32768 bytes (one LoROM bank) and fills BG2 VRAM. Same params as the
original single bgtex so all levels look consistent. Pure-Python (no gfx2snes).
"""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_bg

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
RES = os.path.join(PROJ, "res")
BG = os.path.join(PROJ, "bg")

PARAMS = dict(base_pal=2, npal=6, budget=1024, merge_to=1022)

# Wash every backdrop toward 50% gray so it reads as more 'distant'. 0.0 = off;
# 0.3 = the gray overlay is 30% opaque (orig 70% / gray 30%). Tune to taste.
GRAY = 0.35

# Level 1 source = test3.png (repo root); levels 2-10 = bg/bg2..bg10.png.
SOURCES = {1: os.path.join(PROJ, "test3.png")}
for _n in range(2, 11):
    SOURCES[_n] = os.path.join(BG, "bg%d.png" % _n)


def main():
    print("gray wash = %.0f%%" % (GRAY * 100))
    for n in range(1, 11):
        src = SOURCES[n]
        if not os.path.exists(src):
            print("  MISSING %s" % src)
            continue
        make_bg.convert(src, os.path.join(RES, "bgtex_%d" % n), gray=GRAY, **PARAMS)

    print("\npic sizes (must be < 32768 = one LoROM bank):")
    ok = True
    for n in range(1, 11):
        p = os.path.join(RES, "bgtex_%d.pic" % n)
        if os.path.exists(p):
            sz = os.path.getsize(p)
            flag = "OK" if sz < 32768 else "*** OVER 32KB ***"
            if sz >= 32768:
                ok = False
            print("  bgtex_%d.pic %6d  %s" % (n, sz, flag))
        else:
            print("  bgtex_%d.pic MISSING" % n)
            ok = False
    print("ALL OK" if ok else "PROBLEMS -- see above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
