#!/usr/bin/env python3
"""Generate SNES chiptune modules (.it) for Deadfall + WAV previews.

Full chiptune band: driving bass, arpeggios, lead melody, soft pad, and a real
synthesised drum kit (punchy kick / snare / hat) on a groove. Hand-written
melodic phrases (not random) transposed to each track's key/mood.

  python3 build_music.py preview [names...]  -> res/<name>_preview.wav (listen!)
  python3 build_music.py                      -> all res/music_*.it for the ROM
"""
import os
import sys
import struct
import numpy as np
from make_it import write_it

SRC = os.path.join(os.path.dirname(__file__), "..", "res")
RES = SRC
WAVE_LEN = 64
C5_HZ = 523.25
C5_SPEED = int(round(WAVE_LEN * C5_HZ))
DRATE = 16000                       # drum-sample rate
np.random.seed(1234)                # deterministic builds


# ---- tonal waveforms (one cycle, looped) -----------------------------------
def _wave(kind):
    t = np.arange(WAVE_LEN) / WAVE_LEN
    if kind == "square":
        w = np.where(t < 0.5, 1.0, -1.0)
    elif kind == "pulse":               # 25% duty
        w = np.where(t < 0.25, 1.0, -1.0)
    elif kind == "triangle":
        w = 2.0 * np.abs(2.0 * (t - np.floor(t + 0.5))) - 1.0
    else:
        w = np.zeros(WAVE_LEN)
    return (w * 20000).astype(np.int16)


# ---- drum kit (one-shot samples, played at their natural pitch = note 60) ---
def _kick():
    n = int(0.11 * DRATE)
    t = np.arange(n) / DRATE
    f = 150.0 * np.exp(-t * 32) + 48.0          # pitch drop
    ph = np.cumsum(2 * np.pi * f / DRATE)
    body = np.sin(ph) * np.exp(-t * 26)
    click = np.zeros(n); click[:120] = np.random.uniform(-1, 1, 120) * np.exp(-np.arange(120) / 30)
    w = body + 0.3 * click
    return (np.clip(w, -1, 1) * 30000).astype(np.int16)


def _snare():
    n = int(0.13 * DRATE)
    t = np.arange(n) / DRATE
    noise = np.random.uniform(-1, 1, n) * np.exp(-t * 22)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 30) * 0.5
    w = 0.8 * noise + tone
    return (np.clip(w, -1, 1) * 24000).astype(np.int16)


def _hat():
    n = int(0.035 * DRATE)
    t = np.arange(n) / DRATE
    noise = np.random.uniform(-1, 1, n)
    noise = np.diff(np.concatenate([[0], noise]))      # crude high-pass -> bright
    w = noise * np.exp(-t * 90)
    return (np.clip(w, -1, 1) * 18000).astype(np.int16)


def instruments():
    return [
        {"name": "SQUARE",   "pcm": _wave("square"),   "rate": C5_SPEED, "loop": (0, WAVE_LEN)},
        {"name": "PULSE",    "pcm": _wave("pulse"),    "rate": C5_SPEED, "loop": (0, WAVE_LEN)},
        {"name": "TRIANGLE", "pcm": _wave("triangle"), "rate": C5_SPEED, "loop": (0, WAVE_LEN)},
        {"name": "KICK",     "pcm": _kick(),  "rate": DRATE, "loop": None},
        {"name": "SNARE",    "pcm": _snare(), "rate": DRATE, "loop": None},
        {"name": "HAT",      "pcm": _hat(),   "rate": DRATE, "loop": None},
    ]
SQUARE, PULSE, TRIANGLE, KICK, SNARE, HAT = 1, 2, 3, 4, 5, 6
DNOTE = 60                          # drums play at natural pitch

# ---- theory ----------------------------------------------------------------
MAJ = [0, 2, 4, 5, 7, 9, 11]
MIN = [0, 2, 3, 5, 7, 8, 10]
PROG_MIN = [0, 5, 2, 4]             # i  VI III v
PROG_MAJ = [0, 4, 5, 3]             # I  V  vi IV

# Hand-written 4-bar lead melodies as (scale-degree, row, ) per bar (16 rows/bar).
# degree indexes the diatonic scale (0=tonic, 7=octave). Phrases arc & resolve.
LEAD_MIN = [
    [(7, 0), (9, 4), (7, 6), (6, 8), (4, 12), (6, 14)],
    [(5, 0), (7, 4), (5, 6), (4, 8), (2, 12)],
    [(4, 0), (6, 4), (7, 8), (9, 10), (11, 12)],
    [(9, 0), (7, 4), (4, 8), (2, 10), (0, 12)],
]
LEAD_MAJ = [
    [(4, 0), (7, 4), (9, 6), (11, 8), (9, 12)],
    [(7, 0), (4, 4), (2, 8), (4, 12), (5, 14)],
    [(9, 0), (11, 4), (12, 8), (11, 12)],
    [(7, 0), (9, 4), (7, 6), (4, 8), (0, 12)],
]


def snote(root, scale, deg):
    o, i = divmod(deg, 7)
    return root + 12 * o + scale[i]


def compose(root, minor, intensity):
    scale = MIN if minor else MAJ
    prog = PROG_MIN if minor else PROG_MAJ
    lead = LEAD_MIN if minor else LEAD_MAJ
    root4 = 48 + (root % 12)
    rows = 64
    cr = {}

    def put(r, ch, note, ins, vol):
        cr.setdefault(r, []).append((ch, note, ins, vol, None, None))

    busy = intensity > 0.55
    for bar in range(4):
        deg = prog[bar % 4]
        b0 = bar * 16
        rt = snote(root4, scale, deg)
        th = snote(root4, scale, deg + 2)
        fi = snote(root4, scale, deg + 4)

        # CH0 bass: driving eighths, root with a fifth lift (triangle = round/solid)
        bass_rows = [0, 4, 6, 8, 12, 14] if busy else [0, 6, 8, 14]
        for i, rr in enumerate(bass_rows):
            n = rt - 12 if (i % 4 != 3) else fi - 12
            put(b0 + rr, 0, n, TRIANGLE, 40)

        # CH1 arpeggio: chord tones every 2 rows, quiet bed so drums/lead sit on top
        arp = [rt, th, fi, th, fi, snote(root4, scale, deg + 6), fi, th]
        for k, rr in enumerate(range(0, 16, 2)):
            put(b0 + rr, 1, arp[k % len(arp)], SQUARE, 16)

        # CH2 lead melody (octave up)
        for d, rr in lead[bar % 4]:
            put(b0 + rr, 2, snote(root4, scale, d) + 12, SQUARE, 44)

        # CH3 kick+snare groove (dominant); CH4 hats
        put(b0 + 0, 3, DNOTE, KICK, 64)
        put(b0 + 8, 3, DNOTE, KICK, 64)
        if busy:
            put(b0 + 14, 3, DNOTE, KICK, 52)
        put(b0 + 4, 3, DNOTE, SNARE, 62)
        put(b0 + 12, 3, DNOTE, SNARE, 62)
        hat_rows = range(0, 16, 2) if intensity > 0.35 else (2, 6, 10, 14)
        for rr in hat_rows:
            put(b0 + rr, 4, DNOTE, HAT, 34 if rr % 4 == 0 else 22)

    return [(rows, cr)], [0, 0]


TRACKS = {
    "level1": (True, 108, 0.40), "level2": (True, 112, 0.48),
    "level3": (True, 116, 0.52), "level4": (False, 118, 0.55),
    "level5": (False, 122, 0.58), "level6": (True, 124, 0.62),
    "level7": (True, 128, 0.68), "level8": (True, 132, 0.74),
    "level9": (True, 136, 0.80), "level10": (True, 140, 0.88),
    "frantic": (True, 156, 0.97), "credits": (False, 100, 0.45),
}
ROOTS = {"level1": 9, "level2": 2, "level3": 7, "level4": 0, "level5": 5,
         "level6": 9, "level7": 4, "level8": 2, "level9": 7, "level10": 9,
         "frantic": 4, "credits": 0}


# ---- WAV preview (nearest-neighbour playback, mimics SNES) ------------------
def render_wav(samples, pats, orders, tempo, speed, path, out_rate=32000):
    rows, cr = pats[0]
    row_dur = 5.0 * speed / (2.0 * tempo)
    total_rows = sum(rows for _ in orders)
    buf = np.zeros(int(total_rows * row_dur * out_rate) + out_rate, np.float32)

    def play(note, smp, vol, start, dur):
        s = samples[smp - 1]
        pcm = s["pcm"].astype(np.float32)
        loop = s.get("loop")
        rate = s["rate"] * (2.0 ** ((note - 60) / 12.0))
        n = int(dur * out_rate)
        if n <= 0:
            return
        idx = np.arange(n) * (rate / out_rate)
        if loop:
            ls, le = loop
            si = (ls + np.mod(idx, le - ls)).astype(np.int32)
        else:
            keep = int(np.searchsorted(idx, len(pcm) - 1))
            n = min(n, keep) if keep > 0 else 0
            if n <= 0:
                return
            si = idx[:n].astype(np.int32)
        wave = pcm[si] * (vol / 64.0) / 32768.0
        env = np.ones(n)
        a = min(48, n // 4)
        if a > 0:
            env[:a] = np.linspace(0, 1, a); env[-a:] = np.linspace(1, 0, a)
        s0 = int(start * out_rate)
        buf[s0:s0 + n] += wave * env

    per = {}
    base = 0
    for _ in orders:
        for r in range(rows):
            for (ch, note, ins, vol, _c, _v) in cr.get(r, []):
                per.setdefault(ch, []).append((base + r, note, ins, vol))
        base += rows
    for ch, evs in per.items():
        evs.sort()
        for i, (rr, note, ins, vol) in enumerate(evs):
            end = evs[i + 1][0] if i + 1 < len(evs) else total_rows
            dur = min((end - rr) * row_dur, 1.4)
            play(note, ins, vol, rr * row_dur, dur)

    # mix like the SNES: fixed master gain + soft-clip (NOT peak-normalise, which
    # would squash the drum transients). vol columns set the real balance.
    mixed = np.tanh(buf * 0.55) / np.tanh(0.55) * 0.9
    pcm = np.clip(mixed * 32767, -32768, 32767).astype("<i2")
    with open(path, "wb") as f:
        data = pcm.tobytes()
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, out_rate, out_rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def build(name, write_file=True, preview=False):
    minor, tempo, inten = TRACKS[name]
    pats, orders = compose(ROOTS[name], minor, inten)
    smps = instruments()
    if preview:
        render_wav(smps, pats, orders, tempo, 6, os.path.join(RES, f"{name}_preview.wav"))
        print(f"  preview res/{name}_preview.wav ({'min' if minor else 'maj'} {tempo}bpm int={inten})")
    if write_file:
        n = write_it(os.path.join(RES, f"music_{name}.it"), f"DEADFALL {name.upper()}",
                     smps, patterns=pats, orders=orders, channels=5, speed=6, tempo=tempo)
        print(f"  {name:9s} {'min' if minor else 'maj'} {tempo}bpm -> music_{name}.it ({n}B)")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "preview":
        for nm in (sys.argv[2:] or ["level1", "level5", "frantic"]):
            build(nm, write_file=False, preview=True)
    else:
        for nm in TRACKS:
            build(nm)
