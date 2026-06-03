#!/usr/bin/env python3
"""PPU-faithful check of the new 2-palette gameplay tileset: decode the REAL
res/bg_tiles.pic + .pal exactly as the SNES will (metatile N -> tiles N*4..+3,
structural metatiles use sub-palette 0, special use sub-palette 1), and lay it
next to the original art. Writes tools/verify_bg_tiles.png. No emulator needed.
"""
import os, numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
RES = os.path.join(PROJ, "res")
SPR = os.path.join(PROJ, "..", "cubed", "public", "sprites")

GROUP0_MT = {0, 1, 2, 3, 4, 5, 6, 7}   # block+boulder+gem -> pal0; markers (>=8) -> pal1
NAMES = {1: "block0", 2: "block1", 3: "block2", 4: "gem0", 5: "gem1", 6: "gem2",
         7: "boulder", 8: "portal", 12: "spawn", 14: "elife", 15: "rspawn"}


def load_pal(path):
    b = open(path, "rb").read()
    out = []
    for i in range(0, len(b), 2):
        c = b[i] | (b[i + 1] << 8)
        r, g, bl = c & 31, (c >> 5) & 31, (c >> 10) & 31
        out.append((r * 255 // 31, g * 255 // 31, bl * 255 // 31))
    return out


def decode_4bpp(path):
    d = open(path, "rb").read()
    tiles = []
    for t in range(len(d) // 32):
        o = t * 32; tile = np.zeros((8, 8), np.uint8)
        for y in range(8):
            p0, p1 = d[o + y * 2], d[o + y * 2 + 1]
            p2, p3 = d[o + 16 + y * 2], d[o + 16 + y * 2 + 1]
            for x in range(8):
                b = 7 - x
                tile[y, x] = ((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | \
                             (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3)
        tiles.append(tile)
    return tiles


def metatile_rgb(tiles, pal, mt, bg=(40, 40, 48)):
    """Render metatile mt (4 tiles) through its palette over a bg (to show transp)."""
    cell = np.zeros((16, 16, 3), np.uint8); cell[:] = bg
    sub = [(0, 0), (0, 8), (8, 0), (8, 8)]
    for k, (oy, ox) in enumerate(sub):
        tl = tiles[mt * 4 + k]
        for y in range(8):
            for x in range(8):
                v = tl[y, x]
                if v == 0:
                    continue
                cell[oy + y, ox + x] = pal[v]
    return cell


def main():
    fullpal = load_pal(os.path.join(RES, "bg_tiles.pal"))
    pal0, pal1 = fullpal[0:16], fullpal[16:32]
    tiles = decode_4bpp(os.path.join(RES, "bg_tiles.pic"))

    show = [1, 2, 3, 4, 5, 6, 7, 8, 12, 14, 15]
    scale, cs, gap = 6, 16 * 6, 6

    # originals
    def frame(sheet, col, row):
        return sheet.crop((col * 16, row * 16, col * 16 + 16, row * 16 + 16))
    src = {
        1: frame(Image.open(f"{SPR}/block.png").convert("RGBA"), 0, 0),
        2: frame(Image.open(f"{SPR}/block.png").convert("RGBA"), 2, 0),
        3: frame(Image.open(f"{SPR}/block.png").convert("RGBA"), 4, 0),
        4: frame(Image.open(f"{SPR}/gem-1.png").convert("RGBA"), 0, 0),
        5: frame(Image.open(f"{SPR}/gem-1.png").convert("RGBA"), 2, 0),
        6: frame(Image.open(f"{SPR}/gem-1.png").convert("RGBA"), 4, 0),
        7: frame(Image.open(f"{SPR}/iron-sprite.png").convert("RGBA"), 0, 0),
        8: frame(Image.open(f"{SPR}/portal.png").convert("RGBA"), 0, 0),
        12: frame(Image.open(f"{SPR}/spawn-point.png").convert("RGBA"), 0, 0),
        14: frame(Image.open(f"{SPR}/extralife.png").convert("RGBA"), 0, 0),
        15: frame(Image.open(f"{SPR}/robot-spawn-point.png").convert("RGBA"), 0, 0),
    }

    W = 70 + len(show) * (cs + gap) + gap
    H = 18 + 2 * (cs + gap) + gap
    canvas = Image.new("RGB", (W, H), (24, 24, 28))
    d = ImageDraw.Draw(canvas)
    for ci, mt in enumerate(show):
        x = 70 + gap + ci * (cs + gap)
        d.text((x, 4), NAMES[mt], fill=(150, 200, 230))
        # original
        o = src[mt].convert("RGBA"); bg = Image.new("RGBA", (16, 16), (40, 40, 48, 255))
        bg.alpha_composite(o)
        canvas.paste(bg.convert("RGB").resize((cs, cs), Image.NEAREST), (x, 18))
        # SNES decode
        pal = pal0 if mt in GROUP0_MT else pal1
        rgb = metatile_rgb(tiles, pal, mt)
        canvas.paste(Image.fromarray(rgb).resize((cs, cs), Image.NEAREST), (x, 18 + cs + gap))
    d.text((4, 18 + cs // 2), "ORIGINAL", fill=(230, 230, 120))
    d.text((4, 18 + cs + gap + cs // 2), "SNES 2pal", fill=(120, 230, 150))
    out = os.path.join(HERE, "verify_bg_tiles.png")
    canvas.save(out)
    print("wrote", out)
    print("pal0:", pal0)
    print("pal1:", pal1)


if __name__ == "__main__":
    main()
