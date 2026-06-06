#!/usr/bin/env python3
"""Render a TrueType font into Deadfall's BG3 2bpp text font (res/hud_font2.pic).

64 glyphs, ascii 32..95 (space .. '_'), 8x8 tiles, 2bpp. To match the existing
palette + HUD bar the glyphs are WHITE letter = colour 1 on an OPAQUE BLACK
background = colour 2 (so the fixed BG3 text layer still masks the playfield --
that's what makes the HUD read as a solid bar). Space (tile 0) is all colour 2.

Usage: python3 tools/ttf2font.py <font.ttf> [size] [--preview]
"""
import os, sys
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")
OUT = os.path.join(RES, "hud_font2.pic")
THRESH = 100          # antialias -> 1-bit cutoff


BASELINE_Y = 7        # glyph baseline row inside the 8x8 cell (caps grow upward from here)


def glyph_rows(font, ch, size):
    """8x8 1-bit rows for ch: baseline-aligned vertically (so caps and punctuation
    land at their natural heights) and INK-centred horizontally in the cell."""
    W = 16
    im = Image.new("L", (W, 8), 0)
    dr = ImageDraw.Draw(im)
    dr.text((4, BASELINE_Y), ch, fill=255, font=font, anchor="ls")  # left edge, baseline
    px = im.load()
    cols = [x for x in range(W) if any(px[x, y] > THRESH for y in range(8))]
    if not cols:
        return [[0] * 8 for _ in range(8)], 0
    c0, c1 = min(cols), max(cols)
    w = c1 - c0 + 1
    off = (8 - w) // 2 - c0                # shift so the ink is centred in 8px
    rows = []
    for y in range(8):
        row = [0] * 8
        for x in cols:
            if px[x, y] > THRESH and 0 <= x + off < 8:
                row[x + off] = 1
        rows.append(row)
    return rows, w


def tile_to_rows(tile16):
    """Decode a 2bpp 8x8 tile (16 bytes) back to letter(1)/bg(0) rows."""
    rows = []
    for y in range(8):
        p0 = tile16[y * 2]
        rows.append([1 if (p0 >> (7 - x)) & 1 else 0 for x in range(8)])
    return rows


def build(ttf, size):
    font = ImageFont.truetype(ttf, size)
    old = open(OUT, "rb").read() if os.path.exists(OUT) else b"\x00" * 1024
    notdef, _ = glyph_rows(font, "", size)   # missing-glyph reference (.notdef box)
    out = bytearray()
    rows_by_code = {}
    fellback = []
    overflow = []
    for code in range(32, 96):
        ch = chr(code)
        if ch == " ":
            rows = [[0] * 8 for _ in range(8)]                  # space -> all background
        else:
            rows, w = glyph_rows(font, ch, size)
            if rows == notdef or not any(any(r) for r in rows):  # missing in this TTF
                rows = tile_to_rows(old[(code - 32) * 16:(code - 32) * 16 + 16])
                fellback.append(ch)
            elif w > 8:
                overflow.append(ch)
        rows_by_code[code] = rows
        for y in range(8):
            p0 = p1 = 0
            for x in range(8):
                col = 1 if rows[y][x] else 2        # letter=1, opaque-black bg=2
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
        code = ord(ch)
        rows = rows_by_code.get(code, rows_by_code[ord("?")] if ord("?") in rows_by_code else [[0]*8]*8)
        for y in range(8):
            lines[y] += "".join("#" if rows[y][x] else "." for x in range(8)) + " "
    print("\n".join(lines))


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: ttf2font.py <font.ttf> [size] [--preview]")
    ttf = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 10
    out, rows, overflow, fellback = build(ttf, size)
    if "--preview" in sys.argv:
        preview(rows, "DEADFALL")
        print()
        preview(rows, "GEM:01/9")
        print()
        preview(rows, "MWX.-2026")
        print(f"\nsize={size}  overflow(>8px): {overflow or 'none'}  "
              f"fell-back-to-old: {''.join(fellback) or 'none'}")
    else:
        open(OUT, "wb").write(out)
        print(f"wrote {OUT} ({len(out)} bytes) from {os.path.basename(ttf)} @ size {size}; "
              f"fell back to old font for: {''.join(fellback) or 'none'}")


if __name__ == "__main__":
    main()
