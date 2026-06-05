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
from PIL import Image, ImageEnhance

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
    """RGBA -> indexed 'P' image. Index 0 = transparent (black); art uses 1..n-1.
    Transparent pixels are neutralized to the mean opaque colour BEFORE quantizing
    so the (usually black) transparent region doesn't steal a palette slot -- all
    ncolors-1 entries then go to the actual sprite, giving a smoother gradient."""
    rgba = np.array(img.convert("RGBA"))
    alpha = rgba[:, :, 3]
    opaque = alpha >= 128
    rgb_arr = rgba[:, :, :3].copy()
    if opaque.any():
        rgb_arr[~opaque] = rgb_arr[opaque].mean(axis=0).astype(np.uint8)
    rgb = Image.fromarray(rgb_arr, "RGB")
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
# (metatiles, color budget, reserved colors). The gem reserves a pure WHITE so
# its specular highlight + the moving sparkle-star frames have a true-white target
# to map onto (otherwise median-cut spends all 5 slots on the hue and the shimmer
# flattens to a faint tint). +transp = 16.
# block+boulder: 10 median colours, NO reserved white (their highlights are tinted
# near-white, not pure). gem: reserve pure WHITE for the sparkle stars + 4 body.
# Block and boulder get SEPARATE colour budgets (5 each) -- they used to share 10,
# but now that the block is a saturated accent and the boulder is its own natural
# gray, a shared median-cut spent the budget on the block's colours and TINTED the
# gray boulder (e.g. a pink boulder on a magenta level). Splitting keeps the
# boulder's grays its own. gem: reserve pure WHITE for the sparkle + 4 body.
# block reserves a neutral NEAR-BLACK so the crisp bevel's bottom/right shadow maps
# to true near-black on every hue (not idx0, which is transparent on a BG layer).
PAL0_SUBGROUPS = [([1, 2, 3], 5, [(18, 18, 20)]), ([7], 5, []), ([4, 5, 6], 5, [(255, 255, 255)])]
# pal1 also budgeted: the blue markers dominate by pixel count, so the lone GOLD
# extra-life star gets a reserved share or it nearest-maps to silver.
# spawn-glow frame 2 (mt20) joins the spawn group; extra-life anim frames (mt21-23)
# join the gold-star group, so the animation frames share the marker palette.
PAL1_SUBGROUPS = [([8, 9, 10, 11, 12, 13, 15, 20], 10), ([14, 21, 22, 23], 3)]  # (blue markers, gold star); +transp/white/black = 16
# Metatiles 16,17 = block shatter (frames 3,4); 18,19 = gem shatter. They live in
# pal0 (block/gem material) but are NOT given their own palette budget -- they're
# nearest-mapped to the static block/gem colors (a brief destruction flash), so
# the persistent block/gem/boulder keep their full color fidelity.
# 24-27 = gem glitter frames -> pal0 (gem material), indexed against (not budgeted into) it.
PAL0_CRUSH = [16, 17, 18, 19, 24, 25, 26, 27]
NMETA = 28
# Per-metatile sub-palette for render.c (mirrors the grouping above).
MT_PAL = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0]


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


# Per-level gem/block/boulder colour is now derived from each level's background
# (see ACCENT_HUES + colorize_obj below): all three are recoloured to one accent
# hue that contrasts the backdrop, with deep-shadow/white-highlight pop. (This
# supersedes the old per-level block multiply-tint + pre-coloured boulder frame.)


def apply_tint(img, tint):
    """Phaser setTint: multiply each RGB channel by tint/255 (alpha untouched)."""
    a = np.array(img.convert("RGBA"), dtype=np.uint16)
    tr, tg, tb = (tint >> 16) & 0xFF, (tint >> 8) & 0xFF, tint & 0xFF
    a[:, :, 0] = (a[:, :, 0] * tr) // 255
    a[:, :, 1] = (a[:, :, 1] * tg) // 255
    a[:, :, 2] = (a[:, :, 2] * tb) // 255
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def boost(img, sat=1.65, contrast=1.13, bright=1.0):
    """Make a tile 'pop' more: raise saturation + a touch of contrast (alpha
    preserved). Used on the gameplay objects so they stand out against the faded,
    gray-washed background. `bright`<1 DARKENS while saturating ('deepen') -- use
    it for colours like the player's pink whose channels are already near max, so
    saturation alone would clamp two channels (-> magenta) instead of enriching."""
    a = img.convert("RGBA")
    alpha = a.getchannel("A")
    rgb = ImageEnhance.Color(a.convert("RGB")).enhance(sat)
    if contrast != 1.0:
        rgb = ImageEnhance.Contrast(rgb).enhance(contrast)
    if bright != 1.0:
        rgb = ImageEnhance.Brightness(rgb).enhance(bright)
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


def shade_vertical(img, top=1.08, bot=0.5):
    """Top-lit vertical brightness gradient applied PER 16px frame row: brightest at
    the frame's top, darkening toward the bottom. The player art is FLAT (a uniformly
    bright saturated pink ball) which reads as a flat/intense blob; the enemy art is
    SHADED (light top -> dark bottom = a 3D sphere), which is why it looks nicer. This
    sculpts the flat ball into a lit sphere. Alpha preserved."""
    a = np.array(img.convert("RGBA")).astype(np.float32)
    for fy in range(a.shape[0]):
        ry = (fy % 16) / 15.0
        k = top + (bot - top) * ry
        a[fy, :, :3] = np.clip(a[fy, :, :3] * k, 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def bevel(img, hi=42, lo=56, crisp=False):
    """Top-left-lit bevel for depth: brighten the top/left-facing edges of the
    opaque shape and darken the bottom/right-facing edges (light from top-left).
    Shape-aware -- edges come from the alpha mask, so it follows a gem's facets or
    a block's square, not just the tile border.

    crisp=True (blocks): instead of adding/subtracting a flat amount (which on a
    bright/warm hue leaves the 'shadow' as dark brown, not black), blend the
    top/left edge toward WHITE and MULTIPLY the bottom/right edge down toward
    near-black -- so every block gets the same hard light/dark bevel regardless of
    its hue."""
    a = np.array(img.convert("RGBA"))
    rgb = a[:, :, :3].astype(np.float32)
    op = a[:, :, 3] >= 128
    p = np.pad(op, 1, constant_values=False)
    above, left = p[0:16, 1:17], p[1:17, 0:16]
    below, right = p[2:18, 1:17], p[1:17, 2:18]
    hi_mask = op & (~above | ~left)        # top/left-facing edge -> highlight
    lo_mask = op & (~below | ~right) & ~hi_mask   # bottom/right-facing -> shadow
    if crisp:
        # Single clean 1px bevel: light the top/left edge toward white and drive the
        # bottom/right edge to NEAR-BLACK (multiply, not subtract) so it reads crisp
        # on warm/bright hues too -- not the flat dark-brown the old subtract left.
        # (A 2px band read as a double outline, so keep it to one ring.)
        rgb[hi_mask] += (255.0 - rgb[hi_mask]) * 0.50   # top/left -> light
        rgb[lo_mask] *= 0.13                            # bottom/right -> near-black
    else:
        rgb[hi_mask] += hi
        rgb[lo_mask] -= lo
    a[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


# Per-level ACCENT hue (degrees). Each is ~complementary to that level's washed
# background dominant hue (computed from bg/bg*.png + test3.png, 35% gray wash),
# so the foreground CONTRASTS the backdrop instead of blending into its tint; the
# set is also spread out so no two levels read the same. Gem + block + boulder all
# use the level's accent (vary by saturation/value, below), for a coherent but
# high-contrast look. bg hues were ~ L1 316 L2 71 L3 gray L4 127 L5 247 L6 74
# L7 9 L8 24 L9 26 L10 38.
ACCENT_HUES = [165, 300, 35, 345, 55, 270, 195, 215, 160, 110]
GEM_HUES = ACCENT_HUES                      # back-compat alias

# Per-class colour so block / rock / gem are three DISTINCT materials (not one
# tint), all keyed to the level's accent hue but separated by saturation + value:
#   gem  = vivid jewel (high sat, full range, white sparkle)
#   block= a brighter, lightly-tinted matte STONE (low sat) -> the diggable terrain
#   rock = a darker NEAR-GRAY shiny stone (very low sat, hue nudged) -> immovable
# `smin` = saturation floor at peak brightness: block/rock keep a tint (never pure
# white); only the gem reaches smin=0, and only at its very brightest pixels (the
# sparkle stars, ~1.0 vs facets ~0.92), so the sparkle is the one true white.
# Rolloff is keyed to RAW luminance so that separation survives the contrast stretch.
GEM_PARAMS   = dict(sat=0.30, vlo=0.05, vhi=1.00, hl=0.62, hlpow=2.6, contrast=1.5, smin=0.0)
BLOCK_PARAMS = dict(sat=0.28, vlo=0.18, vhi=1.00, hl=0.50, hlpow=1.4, contrast=1.8, smin=0.12)
ROCK_PARAMS  = dict(sat=0.15, vlo=0.06, vhi=0.82, hl=0.50, hlpow=1.4, contrast=1.6, smin=0.10)
BLOCK_HUE_OFFSET = 90       # block hue = gem hue + 90 -> a DIFFERENT contrasting tint
                            # from the gem (not just a duller shade of the same colour),
                            # still ~90deg off the bg so it keeps contrast.
ROCK_HUE_OFFSET = 30        # (unused: boulders currently use their natural frame)


def colorize_obj(img, hue_deg, sat=0.97, vlo=0.05, vhi=1.0, hl=0.62, hlpow=2.6,
                 contrast=1.5, smin=0.0):
    """Recolour a GRAYSCALE sprite to one hue while keeping real light/dark range:
    value follows source luminance (deep shadow -> bright); saturation rolls from
    `sat` down toward `smin` above `hl` brightness so highlights read as a glint.
    smin=0 lets the very brightest pixels hit pure white (the gem sparkle); smin>0
    keeps a tint in the highlight (block/boulder). Rolloff uses RAW luminance so a
    contrast stretch can't collapse the facet/sparkle distinction. Alpha kept."""
    import colorsys
    a = np.array(img.convert("RGBA"))
    out = a.copy()
    h = (hue_deg % 360) / 360.0
    for y in range(a.shape[0]):
        for x in range(a.shape[1]):
            if a[y, x, 3] < 128:
                continue
            g = float(a[y, x, :3].mean()) / 255.0
            gc = min(1.0, max(0.0, 0.5 + (g - 0.5) * contrast))
            v = vlo + (vhi - vlo) * gc
            t = min(1.0, max(0.0, (g - hl) / (1.0 - hl)))      # rolloff on RAW lum
            s = smin + (sat - smin) * (1.0 - t ** hlpow)
            r, gg, b = colorsys.hsv_to_rgb(h, s, v)
            out[y, x, 0] = int(r * 255); out[y, x, 1] = int(gg * 255); out[y, x, 2] = int(b * 255)
    return Image.fromarray(out, "RGBA")


def colorize_gem(img, hue_deg):
    return colorize_obj(img, hue_deg, **GEM_PARAMS)


def boulder_master():
    """One clean grayscale boulder to recolour per level (the iron-sprite frames
    are an inconsistent set of pre-coloured rocks). Frame 1 has a good round shape
    + diagonal specular; desaturate it to luminance."""
    iron = load("iron-sprite.png")
    bm = np.array(frame(iron, 1, 0).convert("RGBA"))
    lum = (bm[:, :, :3] @ np.array([0.299, 0.587, 0.114])).astype(np.uint8)
    bm[:, :, 0] = bm[:, :, 1] = bm[:, :, 2] = lum
    return Image.fromarray(bm, "RGBA")


def gameplay_tiles(gem_img, block_img, boulder_img):
    """The 16 gameplay metatiles as 16x16 RGBA images (index 0 = transparent).
    gem_img / block_img / boulder_img = already-colourised per-level sheets
    (block_img is the 5-frame block sheet; boulder_img a single 16x16 rock)."""
    block = block_img;  gem = gem_img;  iron = boulder_img
    portal = load("portal.png"); spawn = load("spawn-point.png")
    rspawn = load("robot-spawn-point.png"); elife = load("extralife.png")
    tiles = [None] * 28
    tiles[0] = Image.new("RGBA", (16, 16), (0, 0, 0, 0))    # empty/transparent
    # block/gem damage states = frames 0,1,2 (intact -> cracked), matching the
    # original's static damage; the shatter frames 3,4 are the crush animation.
    tiles[1] = frame(block, 0, 0); tiles[2] = frame(block, 1, 0); tiles[3] = frame(block, 2, 0)
    tiles[4] = frame(gem, 0, 0);   tiles[5] = frame(gem, 1, 0);   tiles[6] = frame(gem, 2, 0)
    tiles[7] = frame(iron, 0, 0)
    tiles[8] = frame(portal, 0, 0); tiles[9] = frame(portal, 2, 0)
    tiles[10] = frame(portal, 4, 0); tiles[11] = frame(portal, 6, 0)
    tiles[12] = frame(spawn, 0, 0); tiles[13] = frame(spawn, 1, 0)
    tiles[14] = frame(elife, 0, 0)
    tiles[15] = frame(rspawn, 0, 0)
    # crush (destruction) animation frames -- shatter; played on destroy.
    tiles[16] = frame(block, 3, 0); tiles[17] = frame(block, 4, 0)   # block shatter
    tiles[18] = frame(gem, 3, 0);   tiles[19] = frame(gem, 4, 0)     # gem shatter
    # spawn-point glow frame 2 + extra-life pickup frames 1,2,3 (animation frames).
    tiles[20] = frame(spawn, 2, 0)
    tiles[21] = frame(elife, 1, 0); tiles[22] = frame(elife, 2, 0); tiles[23] = frame(elife, 3, 0)
    # gem glitter frames (gem sheet row 1, cols 0-3) -- the periodic sparkle.
    tiles[24] = frame(gem, 0, 1); tiles[25] = frame(gem, 1, 1)
    tiles[26] = frame(gem, 2, 1); tiles[27] = frame(gem, 3, 1)
    for i in range(1, 28):              # all game objects + shatter pop vs the faded bg
        tiles[i] = boost(tiles[i])
    for i in (1, 2, 3):                 # blocks: CRISP bevel (light top/left, near-black btm/right)
        tiles[i] = bevel(tiles[i], crisp=True)
    for i in (4, 5, 6, 7):              # gem + boulder: soft depth bevel
        tiles[i] = bevel(tiles[i])
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
    for sub_mts, budget, reserved in PAL0_SUBGROUPS:
        palette += list(reserved) + _mediancut(tiles, sub_mts, budget - len(reserved))
        mts += sub_mts
    palette = (palette + [(0, 0, 0)] * 16)[:16]
    mts += PAL0_CRUSH      # shatter frames: indexed against (not budgeted into) the palette
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
    boulder + block recoloured to ACCENT_HUES[level-1] (contrasts the bg). The
    marker tiles (pal1) are identical across levels. Default out_base = bg_tiles_<level>."""
    if out_base is None:
        out_base = "bg_tiles_%d" % level
    hue = ACCENT_HUES[level - 1]
    gem_img = colorize_obj(Image.open(os.path.join(PROJ, "gem-fixed.png")).convert("RGBA"),
                           hue, **GEM_PARAMS)
    block_img = colorize_obj(load("block.png"), hue + BLOCK_HUE_OFFSET, **BLOCK_PARAMS)
    # Boulders keep their OWN natural look (the per-level iron-sprite frame), NOT
    # the level accent -- so they read as a distinct material from the stone blocks.
    # (colorize_obj + ROCK_PARAMS kept around for a future "interesting" boulder tint.)
    boulder_img = frame(load("iron-sprite.png"), level - 1, 0)
    tiles = gameplay_tiles(gem_img, block_img, boulder_img)
    tiles[0] = Image.new("RGBA", (16, 16), (0, 0, 0, 0))   # ensure empty stays transparent
    g0, pal0 = build_pal0(tiles)
    g1, pal1 = build_pal1(tiles)
    g0[0] = np.zeros((16, 16), np.uint8)                   # empty metatile -> all transparent
    idxmap = {**g0, **g1}

    pic = bytearray()
    for mt in range(NMETA):
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


def build_obj(sheet_rgba, base, colors=16, do_boost=False):
    """OBJ sprite sheet: pad to 128px (16-tile) width and emit raster 8x8 tiles
    so 16x16 hardware sprites address tiles as N,N+1,N+16,N+17. A 16x16 frame at
    sheet column/row (fc,fr) lives at base tile = fr*32 + fc*2. do_boost raises
    saturation so the sprite pops like the gameplay tiles."""
    if do_boost:
        sheet_rgba = boost(sheet_rgba)
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
    # Player: user-redrawn art (player-fixed.png in the project root) -- already properly
    # shaded with a dark outline, so it takes the standard saturation pop cleanly (no flat
    # ball, no shading hack needed). do_boost=True = the original vivid treatment.
    build_obj(Image.open(os.path.join(PROJ, "player-fixed.png")).convert("RGBA"),
              "spr_player", do_boost=True)

    print(">> enemy sprite sheet")
    build_obj(load("enemy.png"), "spr_enemy", do_boost=True)

    print(">> player + enemy death animations (5 frames each)")
    build_obj(load("player-death.png"), "spr_pdeath", do_boost=True)
    build_obj(load("enemy-death.png"), "spr_edeath", do_boost=True)

    print(">> robot sprite sheet (4 dirs x 3 eye states)")
    build_obj(load("enemy-robot.png"), "spr_robot", do_boost=True)

    print(">> lightning beam (real art, horizontal + vertical, 4 frames x 3 segs)")
    build_obj(make_lightning(horizontal=True),  "spr_zap_h")
    build_obj(make_lightning(horizontal=False), "spr_zap_v")

    print(">> HUD glyph font (4bpp, ASCII 32-95)")
    build_obj(make_hud_font(), "hud_font")

    print(">> falling-tile OBJ per level (gem damage 0/1/2 + boulder + extra-life)")
    iron = load("iron-sprite.png"); elife = load("extralife.png")
    gemsrc = Image.open(os.path.join(PROJ, "gem-fixed.png")).convert("RGBA")
    for lvl in range(1, 11):
        gem = colorize_obj(gemsrc, ACCENT_HUES[lvl - 1], **GEM_PARAMS)
        falls = Image.new("RGBA", (80, 16), (0, 0, 0, 0))
        falls.paste(frame(gem, 0, 0), (0, 0))                 # gem damage 0 (intact)
        falls.paste(frame(gem, 1, 0), (16, 0))                # gem damage 1 (cracked)
        falls.paste(frame(gem, 2, 0), (32, 0))                # gem damage 2 (more cracked)
        falls.paste(frame(iron, lvl - 1, 0), (48, 0))         # boulder: own natural frame (untinted)
        falls.paste(frame(elife, 0, 0), (64, 0))
        build_obj(falls, "spr_falls_%d" % lvl, do_boost=True)

    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
