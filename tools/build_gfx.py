#!/usr/bin/env python3
"""Build SNES-format graphics for the Deadfall port.

gfx2snes needs indexed (<=16-color) images; the source art is RGBA, so we
quantize here, reserving palette index 0 as transparent (color 0 of every
SNES palette is transparent for both BG and OBJ).

Outputs into res/:
  bg_tiles.png/.pic/.pal   16x256  - 16 gameplay 16x16 metatiles (BG1, 4bpp)
  spr_player.png/.pic/.pal 112x32  - player frames (OBJ, 4bpp, 16x16)

BG1 metatile order (matches render.h TILE_IDX_*):
  0 empty(transparent) 1-3 block dmg 4-6 gem dmg 7 boulder
  8-11 portal 12-13 spawn 14 extralife 15 robot-spawn
gfx2snes -gs16 emits each 16x16 block as 4 consecutive 8x8 tiles (TL,TR,BL,BR),
so game tile N occupies 8x8 tiles N*4..N*4+3.
"""
import math
import os
import subprocess
import sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
CUBED = os.path.join(PROJ, "..", "cubed")
SPR = os.path.join(CUBED, "public", "sprites")
RES = os.path.join(PROJ, "res")
os.makedirs(RES, exist_ok=True)

GFX2SNES = os.environ.get("GFX2SNES", "gfx2snes")


def load(name):
    return Image.open(os.path.join(SPR, name)).convert("RGBA")


def frame(sheet, col, row, w=16, h=16):
    return sheet.crop((col * w, row * h, col * w + w, row * h + h))


def quantize_keep0(img, ncolors=16):
    """RGBA -> indexed 'P' image. Index 0 = transparent (black); art uses 1..n-1."""
    rgba = np.array(img.convert("RGBA"))
    alpha = rgba[:, :, 3]
    opaque = alpha >= 128
    rgb = Image.fromarray(rgba[:, :, :3], "RGB")
    pal_img = rgb.quantize(colors=ncolors - 1, method=Image.Quantize.MEDIANCUT)
    pal = pal_img.getpalette()[: (ncolors - 1) * 3]
    idx = np.array(pal_img, dtype=np.uint8)            # 0..ncolors-2
    out = np.where(opaque, idx + 1, 0).astype(np.uint8)
    out_img = Image.fromarray(out, "P")
    full = [0, 0, 0] + pal
    full += [0] * (768 - len(full))
    out_img.putpalette(full)
    return out_img


# ---- BG1 gameplay tileset: TWO sub-palettes for richer color ----------------
# All 16 gameplay metatiles used to share ONE 16-color BG palette, so blocks,
# gems and boulders fought over 15 colors and came out muddy. The HUD moved to
# BG3, freeing BG sub-palette 1, so we split the tiles into two groups, each with
# its own sub-palette (assigned in the tilemap via pal<<10 in render.c).
#
# Grouping by HUE, not by role: the three "material" tiles the player stares at
# -- gem (red), block (gray), boulder (gray) -- coexist in ONE palette because
# gray is hue-neutral and only steals from white/black, never from the gem's red.
# The SATURATED BLUE/GOLD markers are what corrupt the gem's red (mediancut spends
# the budget on blue and the red nearest-maps to tan), so they get their own
# palette:
#   pal 0 (CGRAM 0-15, fully free):  block (3 dmg) + boulder + gem (3 dmg).
#       Colors are BUDGETED per sub-group (block+boulder vs gem) so the many gray
#       block pixels don't crowd out the gem's reds.
#   pal 1 (CGRAM 16-31, shared w/HUD): portal + spawn + extra-life + robot-spawn.
#       CGRAM 16/17/18 are the BG3 HUD's transparent/white/black, so pal1 reserves
#       index 0=transparent, 1=white, 2=black (matching the HUD) and gets 13 free
#       colors at 3..15 (the HUD font never uses 2bpp index 3, so CGRAM 19 is ours).
# We emit .pic/.pal directly (gfx2snes makes only one palette per image), matching
# the gs16 layout render.c expects: metatile N -> 8x8 tiles N*4..N*4+3 (TL,TR,BL,BR).
PAL0_SUBGROUPS = [([1, 2, 3, 7], 10), ([4, 5, 6], 5)]   # (block+boulder, gem); +transp = 16
# pal1 also budgeted: the blue markers dominate by pixel count, so the lone GOLD
# extra-life star gets a reserved share or it nearest-maps to silver.
PAL1_SUBGROUPS = [([8, 9, 10, 11, 12, 13, 15], 10), ([14], 3)]  # (blue markers, gold star); +transp/white/black = 16
# Per-metatile sub-palette for render.c (mirrors the grouping above).
MT_PAL = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1]


def enc_tile(t):
    """8x8 index array -> 32-byte SNES 4bpp planar tile (planes 0/1 then 2/3)."""
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


# Per-level look matches the original (GameScene.js): the gem is a different
# sprite per level (gem-<level>.png), the boulder a different frame of
# iron-sprite.png (frame = level-1), and the block is tinted by a per-level
# multiply colour (GameScene.js blockTints[]).
BLOCK_TINTS = [
    0xB0B0D0,  # L1  cool blue-gray
    0xD0B0B0,  # L2  warm red-gray
    0xB0D0B0,  # L3  cool green-gray
    0xD0D0B0,  # L4  warm yellow-gray
    0xC0B0D0,  # L5  purple-gray
    0xB0D0D0,  # L6  cyan-gray
    0xD0C0B0,  # L7  orange-gray
    0xB0C0D0,  # L8  sky blue-gray
    0xD0B0C0,  # L9  pink-gray
    0xC0D0B0,  # L10 lime-gray
]


def apply_tint(img, tint):
    """Phaser setTint: multiply each RGB channel by tint/255 (alpha untouched)."""
    a = np.array(img.convert("RGBA"), dtype=np.uint16)
    tr, tg, tb = (tint >> 16) & 0xFF, (tint >> 8) & 0xFF, tint & 0xFF
    a[:, :, 0] = (a[:, :, 0] * tr) // 255
    a[:, :, 1] = (a[:, :, 1] * tg) // 255
    a[:, :, 2] = (a[:, :, 2] * tb) // 255
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def gameplay_tiles(gem_sheet="gem-1.png", iron_col=0, block_tint=None):
    """The 16 gameplay metatiles as 16x16 RGBA images (index 0 = transparent)."""
    block = load("block.png");  gem = load(gem_sheet);  iron = load("iron-sprite.png")
    if block_tint is not None:
        block = apply_tint(block, block_tint)
    portal = load("portal.png"); spawn = load("spawn-point.png")
    rspawn = load("robot-spawn-point.png"); elife = load("extralife.png")
    tiles = [None] * 16
    tiles[0] = Image.new("RGBA", (16, 16), (0, 0, 0, 0))    # empty/transparent
    tiles[1] = frame(block, 0, 0); tiles[2] = frame(block, 2, 0); tiles[3] = frame(block, 4, 0)
    tiles[4] = frame(gem, 0, 0);   tiles[5] = frame(gem, 2, 0);   tiles[6] = frame(gem, 4, 0)
    tiles[7] = frame(iron, iron_col, 0)
    tiles[8] = frame(portal, 0, 0); tiles[9] = frame(portal, 2, 0)
    tiles[10] = frame(portal, 4, 0); tiles[11] = frame(portal, 6, 0)
    tiles[12] = frame(spawn, 0, 0); tiles[13] = frame(spawn, 1, 0)
    tiles[14] = frame(elife, 0, 0)
    tiles[15] = frame(rspawn, 0, 0)
    return tiles


def _mediancut(tiles, mt_idxs, n):
    """n representative RGB colors from the opaque pixels of the given metatiles."""
    sheet = Image.new("RGBA", (16, 16 * len(mt_idxs)), (0, 0, 0, 0))
    for k, mt in enumerate(mt_idxs):
        sheet.paste(tiles[mt], (0, k * 16))
    rgba = np.array(sheet)
    rgb = rgba[:, :, :3].copy()
    rgb[rgba[:, :, 3] < 128] = 0                      # ignore transparent pixels
    pal_img = Image.fromarray(rgb, "RGB").quantize(colors=n + 1, method=Image.Quantize.MEDIANCUT)
    pal = pal_img.getpalette()
    cols = [tuple(pal[i * 3:i * 3 + 3]) for i in range(n + 1)]
    # drop the entry closest to pure black (the masked transparent pixels)
    drop = min(range(len(cols)), key=lambda i: sum(c * c for c in cols[i]))
    return [c for i, c in enumerate(cols) if i != drop][:n]


def _index_tiles(tiles, mt_idxs, palette):
    """Nearest-map each metatile's opaque pixels over palette[1:]; transparent->0."""
    cand = np.array(palette[1:], dtype=np.int32)
    res = {}
    for mt in mt_idxs:
        rgba = np.array(tiles[mt]); opaque = rgba[:, :, 3] >= 128
        flat = rgba[:, :, :3].reshape(-1, 3).astype(np.int32)
        nearest = 1 + ((flat[:, None, :] - cand[None, :, :]) ** 2).sum(2).argmin(1)
        out = np.where(opaque.reshape(-1), nearest, 0).reshape(16, 16).astype(np.uint8)
        res[mt] = out
    return res


def build_pal0(tiles):
    """pal0 = block+boulder+gem, colors BUDGETED per sub-group so the gem's reds
    survive the many gray block pixels. Returns ({mt: idx}, [16 RGB])."""
    palette = [(0, 0, 0)]
    mts = []
    for sub_mts, budget in PAL0_SUBGROUPS:
        palette += _mediancut(tiles, sub_mts, budget)
        mts += sub_mts
    palette = (palette + [(0, 0, 0)] * 16)[:16]
    return _index_tiles(tiles, mts, palette), palette


def build_pal1(tiles):
    """pal1 = markers. Reserve idx 0/1/2 = transparent/white/black (the BG3 HUD
    shares CGRAM 16/17/18); 13 quantized colors at indices 3..15, BUDGETED per
    sub-group so the gold star isn't crowded out by the blue markers."""
    palette = [(0, 0, 0), (255, 255, 255), (0, 0, 0)]
    mts = []
    for sub_mts, budget in PAL1_SUBGROUPS:
        palette += _mediancut(tiles, sub_mts, budget)
        mts += sub_mts
    palette = (palette + [(0, 0, 0)] * 16)[:16]
    return _index_tiles(tiles, mts, palette), palette


def build_bg_tiles(level=1, out_base=None):
    """Write <out_base>.pic (64 4bpp tiles) + <out_base>.pal (32 BGR555 colors =
    pal0 then pal1) for the given level: per-level gem sprite (gem-<level>.png),
    boulder frame (level-1), and block multiply-tint (BLOCK_TINTS[level-1]). The
    marker tiles (pal1) are identical across levels. Default out_base = bg_tiles_<level>."""
    if out_base is None:
        out_base = "bg_tiles_%d" % level
    tiles = gameplay_tiles("gem-%d.png" % level, iron_col=level - 1,
                           block_tint=BLOCK_TINTS[level - 1])
    tiles[0] = Image.new("RGBA", (16, 16), (0, 0, 0, 0))   # ensure empty stays transparent
    g0, pal0 = build_pal0(tiles)
    g1, pal1 = build_pal1(tiles)
    g0[0] = np.zeros((16, 16), np.uint8)                   # empty metatile -> all transparent
    idxmap = {**g0, **g1}

    pic = bytearray()
    for mt in range(16):
        a = idxmap[mt]
        for sy, sx in ((0, 0), (0, 8), (8, 0), (8, 8)):        # TL, TR, BL, BR
            pic += enc_tile(a[sy:sy + 8, sx:sx + 8])
    with open(os.path.join(RES, out_base + ".pic"), "wb") as f:
        f.write(pic)
    with open(os.path.join(RES, out_base + ".pal"), "wb") as f:
        for pal in (pal0, pal1):
            for c in pal:
                v = bgr555(c)
                f.write(bytes([v & 0xFF, (v >> 8) & 0xFF]))
    print("    -> %s.pic (%dB), %s.pal (64B, 2 palettes)  OK" % (out_base, len(pic), out_base))
    return idxmap, pal0, pal1


def run_gfx2snes(idx_png, gs=16, colors=16):
    """Run gfx2snes on an indexed PNG; outputs <base>.pic/.pal next to it."""
    # gfx2snes wants flag values attached (e.g. -gs16, -fpng, -pc16).
    # -mR! disables tile reduction so tiles stay at deterministic positions
    # (we index BG/OBJ tiles as metatile*4 + subtile, no tilemap).
    args = [GFX2SNES, "-fpng", "-gs%d" % gs, "-pc%d" % colors,
            "-po%d" % colors, "-m!", "-mR!", "-n", "-q", idx_png]
    print("   ", " ".join(args))
    subprocess.run(args, check=True)


def build_one(strip_img, base, gs=16, colors=16):
    png = os.path.join(RES, base + ".png")
    strip_img.save(png)
    run_gfx2snes(png, gs=gs, colors=colors)
    pic = os.path.join(RES, base + ".pic")
    pal = os.path.join(RES, base + ".pal")
    ok = os.path.exists(pic) and os.path.exists(pal)
    print(f"    -> {base}.pic ({os.path.getsize(pic) if ok else '?'}B), "
          f"{base}.pal ({os.path.getsize(pal) if ok else '?'}B)  {'OK' if ok else 'MISSING'}")
    return ok


def make_hud_font():
    """Build a 4bpp 8x8 HUD glyph set from the game's thick_8x8 BMFont, indexed
    so tile (ascii-32) == glyph for that ASCII code, for ASCII 32..95 (space,
    digits, punctuation, A-Z). Returns a 128x32 sheet (64 glyphs, 16 per row).
    Glyphs are 2-colour (index 0 transparent, index 1 white) so they render in
    BG palette 1 without touching the gameplay palette."""
    import re
    fdir = os.path.join(CUBED, "public", "fonts")
    # thick_8x8.png is a corrupted regeneration (stray pixels in L, I, M, ...);
    # thick_8x8OLD.png is the clean original and shares the same XML layout.
    atlas = Image.open(os.path.join(fdir, "thick_8x8OLD.png")).convert("RGBA")
    xml = open(os.path.join(fdir, "thick_8x8.xml")).read()
    pos = {}
    for m in re.finditer(r'<char id="(\d+)" x="(\d+)" y="(\d+)"', xml):
        pos[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    sheet = Image.new("RGBA", (128, 32), (0, 0, 0, 0))
    for c in range(32, 96):
        if c not in pos:
            continue
        ax, ay = pos[c]
        glyph = atlas.crop((ax, ay, ax + 8, ay + 8))
        t = c - 32
        sheet.paste(glyph, ((t % 16) * 8, (t // 16) * 8))
    return sheet


def make_lightning(horizontal):
    """Pack the real lightning.png bolt for OBJ use. lightning.png is 192x16 =
    4 anim frames of 48x16 (= 3 tiles, the max zap range). Each frame is split
    into three 16x16 segments; for vertical zaps the frame is rotated 90 deg.
    The 12 segments (4 frames x 3) are packed into a 128x32 sheet at linear
    index L = frame*3 + seg -> grid (col L%8, row L//8); render maps back via
    tile = (L>>3)*32 + (L&7)*2. Returns the 128x32 sheet."""
    light = load("lightning.png")
    sheet = Image.new("RGBA", (128, 32), (0, 0, 0, 0))
    for f in range(4):
        frame = light.crop((f * 48, 0, f * 48 + 48, 16))     # 48x16
        if not horizontal:
            frame = frame.transpose(Image.Transpose.ROTATE_90)  # -> 16x48
        for s in range(3):
            seg = frame.crop((s * 16, 0, s * 16 + 16, 16)) if horizontal \
                  else frame.crop((0, s * 16, 16, s * 16 + 16))
            L = f * 3 + s
            sheet.paste(seg, ((L % 8) * 16, (L // 8) * 16))
    return sheet


def build_obj(sheet_rgba, base, colors=16):
    """OBJ sprite sheet: pad to 128px (16-tile) width and emit raster 8x8 tiles
    so 16x16 hardware sprites address tiles as N,N+1,N+16,N+17. A 16x16 frame at
    sheet column/row (fc,fr) lives at base tile = fr*32 + fc*2."""
    w, h = sheet_rgba.size
    canvas = Image.new("RGBA", (128, h), (0, 0, 0, 0))
    canvas.paste(sheet_rgba, (0, 0))
    idx = quantize_keep0(canvas, colors)
    return build_one(idx, base, gs=8, colors=colors)


def main():
    print(">> BG1 gameplay tilesets, per level (gem-N + tinted block + boulder frame N-1)")
    for lvl in range(1, 11):
        build_bg_tiles(lvl)

    print(">> player sprite sheet (128-wide OBJ layout)")
    build_obj(load("player.png"), "spr_player")

    print(">> enemy sprite sheet")
    build_obj(load("enemy.png"), "spr_enemy")

    print(">> player + enemy death animations (5 frames each)")
    build_obj(load("player-death.png"), "spr_pdeath")
    build_obj(load("enemy-death.png"), "spr_edeath")

    print(">> robot sprite sheet (4 dirs x 3 eye states)")
    build_obj(load("enemy-robot.png"), "spr_robot")

    print(">> lightning beam (real art, horizontal + vertical, 4 frames x 3 segs)")
    build_obj(make_lightning(horizontal=True),  "spr_zap_h")
    build_obj(make_lightning(horizontal=False), "spr_zap_v")

    print(">> HUD glyph font (4bpp, ASCII 32-95)")
    build_obj(make_hud_font(), "hud_font")

    print(">> falling-tile OBJ (gem/boulder/extra-life idle, for smooth gravity)")
    falls = Image.new("RGBA", (48, 16), (0, 0, 0, 0))
    falls.paste(frame(load("gem-1.png"), 0, 0), (0, 0))
    falls.paste(frame(load("iron-sprite.png"), 0, 0), (16, 0))
    falls.paste(frame(load("extralife.png"), 0, 0), (32, 0))
    build_obj(falls, "spr_falls")

    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
