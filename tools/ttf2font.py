#!/usr/bin/env python3
"""Render a TrueType/OpenType font into Deadfall's BG3 2bpp text fonts.

64 glyphs, ascii 32..95, 8x8 tiles, 2bpp, white letter = colour 1. Two variants
are written so the renderer can swap them by context:
  res/hud_font2.pic   opaque-black background (colour 2)  -> gameplay / HUD bar
  res/hud_font2t.pic  transparent background (colour 0)   -> text scenes + banners
That keeps the HUD a solid bar while GAME OVER / title / credits text floats on the
backdrop with no box. Glyphs the font lacks fall back to the current hud_font2.pic
tile (the letter shape is re-coloured to the chosen background).

Usage: python3 tools/ttf2font.py <font> [size] [--preview]
"""
import os, sys
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
OUT = os.path.join(RES, "hud_font2.pic")      # opaque  (gameplay / HUD)
OUT_T = os.path.join(RES, "hud_font2t.pic")   # transparent (text scenes)
THRESH = 100          # antialias -> 1-bit cutoff
BASELINE_Y = 7        # glyph baseline row inside the 8x8 cell


def glyph_rows(font, ch, size):
    """8x8 1-bit rows for ch: baseline-aligned vertically, INK-centred horizontally."""
    W = 16
    im = Image.new("L", (W, 8), 0)
    dr = ImageDraw.Draw(im)
    dr.text((4, BASELINE_Y), ch, fill=255, font=font, anchor="ls")
    px = im.load()
    cols = [x for x in range(W) if any(px[x, y] > THRESH for y in range(8))]
    if not cols:
        return [[0] * 8 for _ in range(8)], 0
    c0, c1 = min(cols), max(cols)
    w = c1 - c0 + 1
    off = (8 - w) // 2 - c0
    rows = []
    for y in range(8):
        row = [0] * 8
        for x in cols:
            if px[x, y] > THRESH and 0 <= x + off < 8:
                row[x + off] = 1
        rows.append(row)
    return rows, w


def tile_to_rows(tile16):
    """Decode a 2bpp 8x8 tile (16 bytes) to letter(1)/other(0) rows via plane 0."""
    return [[1 if (tile16[y * 2] >> (7 - x)) & 1 else 0 for x in range(8)] for y in range(8)]


def build(ttf, size, transparent=False):
    font = ImageFont.truetype(ttf, size)
    old = open(OUT, "rb").read() if os.path.exists(OUT) else b"\x00" * 1024
    notdef, _ = glyph_rows(font, "א", size)   # a glyph no Latin font has -> .notdef ref
    bg = 0 if transparent else 2                   # glyph background colour index
    out = bytearray()
    rows_by_code = {}
    fellback = []
    overflow = []
    for code in range(32, 96):
        ch = chr(code)
        if ch == " ":
            rows = [[0] * 8 for _ in range(8)]
        else:
            rows, w = glyph_rows(font, ch, size)
            if rows == notdef or not any(any(r) for r in rows):   # missing in this font
                rows = tile_to_rows(old[(code - 32) * 16:(code - 32) * 16 + 16])
                fellback.append(ch)
            elif w > 8:
                overflow.append(ch)
        rows_by_code[code] = rows
        for y in range(8):
            p0 = p1 = 0
            for x in range(8):
                col = 1 if rows[y][x] else bg      # letter=1, background=bg (0 transp / 2 black)
                if col & 1:
                    p0 |= 0x80 >> x
                if col & 2:
                    p1 |= 0x80 >> x
            out.append(p0)
            out.append(p1)
    return out, rows_by_code, overflow, fellback


def preview(rows_by_code, text):
    lines = ["" for _ in range(8)]
    for ch in text.upper():
        rows = rows_by_code.get(ord(ch), [[0] * 8] * 8)
        for y in range(8):
            lines[y] += "".join("#" if rows[y][x] else "." for x in range(8)) + " "
    print("\n".join(lines))


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: ttf2font.py <font> [size] [--preview]")
    ttf = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 14
    if "--preview" in sys.argv:
        _, rows, overflow, fellback = build(ttf, size, True)
        preview(rows, "DEADFALL"); print()
        preview(rows, "GEM:01/9"); print()
        preview(rows, "MWX.-2026")
        print(f"\nsize={size}  overflow(>8px): {overflow or 'none'}  "
              f"fell-back: {''.join(fellback) or 'none'}")
        return
    # opaque first (its '/' fallback then feeds the transparent build), then transparent
    op, _, _, fb = build(ttf, size, False)
    open(OUT, "wb").write(op)
    tr, _, _, _ = build(ttf, size, True)
    open(OUT_T, "wb").write(tr)
    print(f"wrote {OUT} (opaque) + {OUT_T} (transparent), {len(op)} bytes each, "
          f"@ size {size}; fell back to old font for: {''.join(fb) or 'none'}")


if __name__ == "__main__":
    main()
