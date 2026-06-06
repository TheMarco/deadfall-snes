#!/usr/bin/env python3
"""Convert an indexed PNG into an 8bpp SNES background (Mode 3, BG1).

8bpp = 256 colours with NO per-tile palette limit, so the source colours are
kept EXACTLY 1:1 (unlike a 4bpp/Mode-1 background, which is capped at 16
colours per 8x8 tile). Outputs:
  res/<name>.pic   deduped 8bpp tiles (64 bytes each)
  res/<name>.map   32x32 tilemap (16-bit LE tile indices)
  res/<name>.pal   palette, BGR555 LE (one entry per used colour, CGRAM 0..)

Usage: python3 tools/png2bg8.py <image.png> [name]
"""
import os, sys, struct
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")


def grid_of(px, tx, ty):
    """8x8 tile as 64 palette indices, row-major."""
    return [px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)]


def hflip(g):
    return [g[y * 8 + (7 - x)] for y in range(8) for x in range(8)]


def vflip(g):
    return [g[(7 - y) * 8 + x] for y in range(8) for x in range(8)]


def enc_grid(g):
    """64-byte 8bpp tile from a grid: plane pairs (0,1)(2,3)(4,5)(6,7), row-interleaved."""
    out = bytearray()
    for pp in range(4):
        lob, hib = pp * 2, pp * 2 + 1
        for y in range(8):
            lo = hi = 0
            for x in range(8):
                p = g[y * 8 + x]
                if (p >> lob) & 1:
                    lo |= 0x80 >> x
                if (p >> hib) & 1:
                    hi |= 0x80 >> x
            out.append(lo)
            out.append(hi)
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: png2bg8.py <image.png> [name]")
    src = sys.argv[1]
    name = sys.argv[2] if len(sys.argv) > 2 else "title8"
    im = Image.open(src)
    if im.mode != "P":
        im = im.convert("P", palette=Image.ADAPTIVE, colors=256)
    pal = im.getpalette()
    W, H = im.size
    px = im.load()
    maxidx = max(px[x, y] for y in range(H) for x in range(W))
    ncol = maxidx + 1
    if ncol > 256:
        sys.exit(f"image uses {ncol} colours (>256) -- not 8bpp-representable")

    tiles, index, tmap = [], {}, []        # index: tuple(normal grid) -> tile number
    TW, TH = W // 8, H // 8
    for ty in range(32):
        for tx in range(32):
            if not (tx < TW and ty < TH):
                tmap.append(0)
                continue
            g = grid_of(px, tx, ty)
            entry = None
            for hf, vf in ((0, 0), (1, 0), (0, 1), (1, 1)):   # dedup incl. mirrored tiles
                k = g
                if hf:
                    k = hflip(k)
                if vf:
                    k = vflip(k)
                kt = tuple(k)
                if kt in index:
                    entry = index[kt] | (hf << 14) | (vf << 15)   # bit14=hflip, bit15=vflip
                    break
            if entry is None:
                index[tuple(g)] = len(tiles)
                tiles.append(enc_grid(g))
                entry = len(tiles) - 1
            tmap.append(entry)
    if len(tiles) > 1024:
        sys.exit(f"{len(tiles)} unique tiles (>1024 tilemap limit)")
    if len(tiles) * 64 > 32768:
        print(f"WARNING: {len(tiles)} tiles = {len(tiles)*64} B > one 32KB ROM bank", file=sys.stderr)

    pic = b"".join(tiles)
    open(os.path.join(RES, f"{name}.pic"), "wb").write(pic)
    open(os.path.join(RES, f"{name}.map"), "wb").write(
        b"".join(struct.pack("<H", t) for t in tmap))
    pb = bytearray()
    for i in range(ncol):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        pb += struct.pack("<H", ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
    open(os.path.join(RES, f"{name}.pal"), "wb").write(pb)
    print(f"{name}: {len(tiles)} tiles ({len(pic)} B), map {len(tmap)*2} B, "
          f"{ncol}-colour palette ({len(pb)} B)")


if __name__ == "__main__":
    main()
