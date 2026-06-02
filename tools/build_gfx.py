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


def compose_bg_tiles(gem_sheet="gem-1.png", iron_col=0):
    """16x256 strip of the 16 gameplay metatiles (transparent index 0)."""
    strip = Image.new("RGBA", (16, 16 * 16), (0, 0, 0, 0))

    block = load("block.png")
    gem = load(gem_sheet)
    iron = load("iron-sprite.png")
    portal = load("portal.png")
    spawn = load("spawn-point.png")
    rspawn = load("robot-spawn-point.png")
    elife = load("extralife.png")

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

    for i, t in enumerate(tiles):
        strip.paste(t, (0, i * 16))
    return strip


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
    print(">> BG1 gameplay tileset (level-1 gem/boulder)")
    bg = quantize_keep0(compose_bg_tiles("gem-1.png", 0), 16)
    build_one(bg, "bg_tiles")

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
