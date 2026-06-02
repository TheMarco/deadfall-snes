#!/usr/bin/env python3
"""Convert a background image into a SNES 4bpp BG: tileset + tilemap + multi
sub-palettes, the "digitized photo" approach real SNES games used.

- No dithering (flat areas stay flat).
- 6 sub-palettes (96 colors): tiles are k-means clustered by colour, each
  cluster gets its own 15-colour palette (index 0 reserved transparent).
- Exact tile dedup with H/V flips (NO lossy merge -> no "chunking"); optional
  light merge only if the unique count exceeds `budget`.

Outputs <out>.pic (4bpp tiles), <out>.map (u16: tile|pal<<10|flips),
<out>.pal (npal*16 BGR555 colours), <out>_preview.png.

Usage: make_bg.py <src> <out> [base_pal=2] [npal=6] [budget=960] [crop WxH+X+Y]
"""
import sys, os
import numpy as np
from PIL import Image


def enc_tile(t):
    b = bytearray(32)
    for y in range(8):
        p0 = p1 = p2 = p3 = 0
        for x in range(8):
            v = int(t[y, x]); bit = 7 - x
            p0 |= (v & 1) << bit; p1 |= ((v >> 1) & 1) << bit
            p2 |= ((v >> 2) & 1) << bit; p3 |= ((v >> 3) & 1) << bit
        b[y*2] = p0; b[y*2+1] = p1; b[16+y*2] = p2; b[16+y*2+1] = p3
    return bytes(b)


def convert(src, out, base_pal=2, npal=6, budget=960, crop=None, scale=1.0, fit=None, merge_to=None):
    img = Image.open(src).convert("RGB")
    if crop:
        img = img.crop(crop)
    if fit:                                # cover-fit the whole composition into
        fw, fh = fit                       # one screen (scale to fill, centre-crop)
        W0, H0 = img.size                  # -> uniform backdrop, all levels equal
        s = max(fw / W0, fh / H0)
        img = img.resize((max(fw, round(W0 * s)), max(fh, round(H0 * s))), Image.LANCZOS)
        W1, H1 = img.size
        l, t = (W1 - fw) // 2, (H1 - fh) // 2
        img = img.crop((l, t, l + fw, t + fh))
    elif scale != 1.0:                     # half-res "smooth slide" backdrop:
        W0, H0 = img.size                  # shrink the whole composition to one
        img = img.resize((max(8, int(W0 * scale)),   # screen so it fits one shared
                          max(8, int(H0 * scale))), Image.LANCZOS)  # resident tileset
    W, H = img.size
    W -= W % 8; H -= H % 8
    img = img.crop((0, 0, W, H))
    arr = np.array(img, dtype=np.int32)
    tw, th = W // 8, H // 8
    ntiles = tw * th

    # tile mean colours -> k-means into npal clusters
    means = np.array([arr[(i // tw) * 8:(i // tw) * 8 + 8,
                          (i % tw) * 8:(i % tw) * 8 + 8].reshape(-1, 3).mean(0)
                      for i in range(ntiles)], dtype=np.float32)
    lum = means @ np.array([0.299, 0.587, 0.114], np.float32)
    order = np.argsort(lum)
    centers = means[order[np.linspace(0, ntiles - 1, npal).astype(int)]].astype(np.float32)
    assign = np.zeros(ntiles, np.int32)
    for _ in range(16):
        d = ((means[:, None, :] - centers[None, :, :]) ** 2).sum(2)
        assign = d.argmin(1)
        for g in range(npal):
            sel = means[assign == g]
            if len(sel):
                centers[g] = sel.mean(0)

    # per-cluster 15-colour palette (median cut, no dither)
    pals = []
    for g in range(npal):
        tids = np.where(assign == g)[0]
        if len(tids) == 0:
            pals.append(np.zeros((15, 3), np.uint8)); continue
        px = np.concatenate([arr[(t // tw) * 8:(t // tw) * 8 + 8,
                                 (t % tw) * 8:(t % tw) * 8 + 8].reshape(-1, 3) for t in tids])
        q = Image.fromarray(px.astype(np.uint8).reshape(-1, 1, 3), "RGB").quantize(
            colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        gp = np.array(q.getpalette(), np.uint8)[:45]
        if len(gp) < 45:
            gp = np.concatenate([gp, np.zeros(45 - len(gp), np.uint8)])
        pals.append(gp.reshape(15, 3))

    def nearest(pix, pal):           # pix (64,3) -> indices 1..15
        d = ((pix[:, None, :] - pal[None, :, :].astype(np.int32)) ** 2).sum(2)
        return (d.argmin(1) + 1).astype(np.uint8)

    # quantise each tile to its cluster palette, then exact-dedup (with flips)
    reps = [np.zeros((8, 8), np.uint8)]   # tile 0 = transparent (HUD/margin rows)
    reppal = [base_pal]
    seen = {}
    mp = []
    for ty in range(th):
        for tx in range(tw):
            g = int(assign[ty * tw + tx])
            idx = nearest(arr[ty*8:ty*8+8, tx*8:tx*8+8].reshape(-1, 3), pals[g]).reshape(8, 8)
            cand = ((idx, 0, 0), (np.fliplr(idx), 1, 0),
                    (np.flipud(idx), 0, 1), (np.fliplr(np.flipud(idx)), 1, 1))
            hit = None
            for vt, hf, vf in cand:
                k = (g, vt.tobytes())
                if k in seen:
                    hit = (seen[k], hf, vf); break
            if hit is None:
                seen[(g, idx.tobytes())] = len(reps)
                reps.append(idx); reppal.append(base_pal + g)
                hit = (len(reps) - 1, 0, 0)
            mp.append((hit[0], base_pal + g, hit[1], hit[2]))

    # optional lossy merge: to slide between two sections both must live in VRAM
    # at once, so cap unique tiles by remapping the least-used tiles onto the
    # nearest kept tile (by rendered colour). Mild reduction -> mild "chunking".
    if merge_to and len(reps) > merge_to:
        usage = np.zeros(len(reps), np.int32)
        for ti, _, _, _ in mp:
            usage[ti] += 1
        feat = np.zeros((len(reps), 192), np.float32)
        for i, t in enumerate(reps):
            g = reppal[i] - base_pal
            flat = t.reshape(-1)
            for j in range(64):
                v = int(flat[j])
                feat[i, j*3:j*3+3] = (0, 0, 0) if v == 0 else pals[g][v - 1]
        keepset = set(int(x) for x in np.argsort(-usage)[:merge_to]); keepset.add(0)
        keep = sorted(keepset)
        kpos = {k: i for i, k in enumerate(keep)}
        kfeat = feat[keep]
        remap = {}
        for i in range(len(reps)):
            if i in keepset:
                remap[i] = (kpos[i], True)
            else:
                remap[i] = (int(((kfeat - feat[i]) ** 2).sum(1).argmin()), False)
        reps = [reps[k] for k in keep]
        reppal = [reppal[k] for k in keep]
        mp = [((remap[ti][0]), reppal[remap[ti][0]], hf if remap[ti][1] else 0,
               vf if remap[ti][1] else 0) for (ti, pal, hf, vf) in mp]

    # emit
    with open(out + ".pic", "wb") as f:
        for r in reps:
            f.write(enc_tile(r))
    with open(out + ".map", "wb") as f:
        for ti, pal, hf, vf in mp:
            e = (ti & 0x3FF) | ((pal & 7) << 10) | (hf << 14) | (vf << 15)
            f.write(bytes((e & 0xFF, (e >> 8) & 0xFF)))
    with open(out + ".pal", "wb") as f:
        for g in range(npal):
            f.write(bytes((0, 0)))               # index 0 transparent
            for i in range(15):
                # cast to int: numpy uint8 shifts overflow/truncate to 8 bits,
                # which silently drops green's high bits and all of blue (-> red).
                r, gg, b = int(pals[g][i][0]), int(pals[g][i][1]), int(pals[g][i][2])
                c = (r >> 3) | ((gg >> 3) << 5) | ((b >> 3) << 10)
                f.write(bytes((c & 0xFF, (c >> 8) & 0xFF)))

    # preview
    prev = np.zeros((H, W, 3), np.uint8)
    for i, (ti, pal, hf, vf) in enumerate(mp):
        t = reps[ti]
        if hf: t = np.fliplr(t)
        if vf: t = np.flipud(t)
        g = pal - base_pal
        ty, tx = divmod(i, tw)
        for y in range(8):
            for x in range(8):
                v = t[y, x]
                prev[ty*8+y, tx*8+x] = (0, 0, 0) if v == 0 else pals[g][v - 1]
    Image.fromarray(prev).resize((W*2, H*2), Image.NEAREST).save(out + "_preview.png")
    print(f"{out}: {tw}x{th} = {ntiles} cells, {len(reps)} unique tiles, {npal} palettes")
    return len(reps)


if __name__ == "__main__":
    src, out = sys.argv[1], sys.argv[2]
    base_pal = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    npal = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    budget = int(sys.argv[5]) if len(sys.argv) > 5 else 960
    scale = float(sys.argv[6]) if len(sys.argv) > 6 else 1.0
    crop = None
    if len(sys.argv) > 7:
        wh, xy = sys.argv[7].split("+", 1); w, h = wh.split("x"); x, y = xy.split("+")
        crop = (int(x), int(y), int(x)+int(w), int(y)+int(h))
    convert(src, out, base_pal, npal, budget, crop, scale)
