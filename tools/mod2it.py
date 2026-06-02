#!/usr/bin/env python3
"""Convert ProTracker .mod modules to Impulse Tracker .it (for PVSnesLib smconv).

smconv only eats .it, but free chiptune archives (ModArchive etc.) are full of
.mod files that use real sampled instruments -> they sound good. This parses a
4/6/8-channel ProTracker MOD and re-emits it as .it via tools/make_it.py.

Translated: samples (8-bit -> 16-bit, loop, finetune via C5Speed), notes
(period -> IT note), volume (Cxx -> volume column), and the structural/common
effects (speed/tempo F, arpeggio 0xy, position-jump B, pattern-break D). Less
common effects (portamento/vibrato/volume-slide) are dropped - notes, samples,
rhythm and volume carry through, so it still sounds like the song.

  python3 mod2it.py in.mod out.it
  python3 mod2it.py --selftest        # build+convert a synthetic mod
"""
import sys
import struct
import numpy as np
from make_it import write_it

# ProTracker period table, finetune 0, notes C-1..B-3 (36). IT note = idx + 48.
PERIODS = [
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
]
TAGS_CH = {b"M.K.": 4, b"M!K!": 4, b"FLT4": 4, b"4CHN": 4,
           b"6CHN": 6, b"8CHN": 8, b"FLT8": 8, b"OCTA": 8}


def period_to_note(p):
    if p == 0:
        return None
    best = min(range(36), key=lambda i: abs(PERIODS[i] - p))
    return best + 48                      # IT note (C-1 -> C-4=48 ... )


def parse_mod(data):
    title = data[0:20].split(b"\x00")[0].decode("latin1", "ignore")
    samples = []
    off = 20
    for _ in range(31):
        name = data[off:off+22]
        length = struct.unpack(">H", data[off+22:off+24])[0] * 2
        ft = data[off+24] & 0x0F
        finetune = ft - 16 if ft >= 8 else ft        # signed -8..7
        vol = data[off+25]
        loop_start = struct.unpack(">H", data[off+26:off+28])[0] * 2
        loop_len = struct.unpack(">H", data[off+28:off+30])[0] * 2
        samples.append({"len": length, "finetune": finetune, "vol": vol,
                        "loop_start": loop_start, "loop_len": loop_len})
        off += 30
    song_len = data[off]
    off += 2                                          # skip restart byte
    order = list(data[off:off+128]); off += 128
    tag = data[off:off+4]; off += 4
    channels = TAGS_CH.get(tag, 4)
    npat = max(order[:song_len]) + 1 if song_len else 0

    patterns = []
    for _ in range(npat):
        rows = []
        for r in range(64):
            cells = []
            for c in range(channels):
                b0, b1, b2, b3 = data[off], data[off+1], data[off+2], data[off+3]
                off += 4
                period = ((b0 & 0x0F) << 8) | b1
                smp = (b0 & 0xF0) | (b2 >> 4)
                eff = b2 & 0x0F
                par = b3
                cells.append((period, smp, eff, par))
            rows.append(cells)
        patterns.append(rows)

    # sample PCM follows the patterns
    for s in samples:
        raw = data[off:off + s["len"]]; off += s["len"]
        pcm = np.frombuffer(raw, dtype=np.int8).astype(np.int16) * 256
        s["pcm"] = pcm
    return title, samples, order[:song_len], patterns, channels


def _downsample(pcm, F):
    """Box-average decimate by F (crude anti-alias) -> shorter, lower-fi PCM."""
    if F <= 1 or len(pcm) < F:
        return pcm
    n = (len(pcm) // F) * F
    return (pcm[:n].astype(np.float32).reshape(-1, F).mean(1)).astype(np.int16)


def convert(mod_path, it_path, fit_pcm=None):
    data = open(mod_path, "rb").read()
    title, smps, order, patterns, channels = parse_mod(data)

    # optional: shrink sample-heavy modules to fit SNES ARAM by decimating all
    # samples by an integer factor (pitch preserved by scaling C5Speed/loops).
    if fit_pcm:
        total = sum(len(s.get("pcm", [])) for s in smps)
        if total > fit_pcm:
            import math
            F = math.ceil(total / fit_pcm)
            for s in smps:
                if len(s.get("pcm", [])) >= F:
                    s["pcm"] = _downsample(s["pcm"], F)
                    s["_dsF"] = F
                    s["loop_start"] //= F
                    s["loop_len"] //= F
            print(f"  (downsampled samples {F}x to fit ~{fit_pcm//1024}KB -> lo-fi)")

    # keep only non-empty samples; remap MOD sample number -> IT sample number
    it_samples, remap = [], {}
    for i, s in enumerate(smps, start=1):
        if len(s.get("pcm", [])) >= 2:
            c5 = int(round(8363 * (2.0 ** (s["finetune"] / 96.0)) / s.get("_dsF", 1)))
            loop = None
            if s["loop_len"] > 2:
                loop = (s["loop_start"], s["loop_start"] + s["loop_len"])
            remap[i] = len(it_samples) + 1
            it_samples.append({"name": f"S{i}", "pcm": s["pcm"], "rate": c5, "loop": loop})

    def eff_to_it(eff, par):
        """-> (vol_or_None, it_cmd_or_None, it_val)."""
        if eff == 0x0C:                                   # set volume
            return min(par, 64), None, None
        if eff == 0x0F:                                   # speed / tempo
            return (None, 1, par) if par < 0x20 else (None, 20, par)
        if eff == 0x00 and par:                           # arpeggio -> IT Jxy
            return None, 10, par
        if eff == 0x0B:                                   # position jump -> Bxx
            return None, 2, par
        if eff == 0x0D:                                   # pattern break -> Cxx (BCD row)
            return None, 3, (par >> 4) * 10 + (par & 0x0F)
        return None, None, None

    it_pats = []
    for rows in patterns:
        chrows = {}
        for r, cells in enumerate(rows):
            for ch, (period, smp, eff, par) in enumerate(cells):
                note = period_to_note(period)
                vol, cmd, val = eff_to_it(eff, par)
                its = remap.get(smp) if smp else None
                if note is None and its is None and vol is None and cmd is None:
                    continue
                chrows.setdefault(r, []).append((ch, note, its, vol, cmd, val))
        it_pats.append((64, chrows))

    if not order:
        order = [0]
    write_it(it_path, title or "MOD", it_samples, patterns=it_pats,
             orders=order, channels=channels, speed=6, tempo=125)
    nbytes = sum(len(s["pcm"]) for s in it_samples)
    print(f"{mod_path} -> {it_path}: '{title}' {channels}ch, {len(it_samples)} samples, "
          f"{len(it_pats)} patterns, {len(order)} orders, {nbytes//1024}KB PCM")


def _selftest():
    """Build a tiny valid 4-channel MOD (1 sample, 1 pattern) and convert it."""
    buf = bytearray()
    buf += b"SELFTEST".ljust(20, b"\x00")
    for i in range(31):
        h = bytearray(30)
        if i == 0:
            struct.pack_into(">H", h, 22, 16)     # length 16 words = 32 bytes
            h[25] = 64                             # volume
            struct.pack_into(">H", h, 28, 16)      # loop len = whole
        buf += h
    buf += bytes([1, 127]) + bytes([0] * 128) + b"M.K."
    # one pattern: a C note (period 428) on ch0 row0 with sample 1
    pat = bytearray(64 * 4 * 4)
    pat[0] = 0x10            # sample hi nibble=1, period hi=0
    pat[1] = 428 & 0xFF      # period lo (428 -> 0x1AC, hi nibble 1)
    pat[0] = 0x11            # period hi nibble = 1 -> period 0x1AC=428, sample=1
    buf += pat
    buf += (np.sin(np.linspace(0, 2 * np.pi, 32)) * 100).astype(np.int8).tobytes()
    open("/tmp/selftest.mod", "wb").write(buf)
    convert("/tmp/selftest.mod", "/tmp/selftest.it")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        _selftest()
    elif len(sys.argv) >= 3:
        fit = int(sys.argv[3]) * 1024 if len(sys.argv) > 3 else None
        convert(sys.argv[1], sys.argv[2], fit_pcm=fit)
    else:
        print(__doc__)
        sys.exit(1)
