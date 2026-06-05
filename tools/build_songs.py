#!/usr/bin/env python3
"""Convert every song in songs/ into a snesmod chiptune module in res/.

Two sources, both producing synth-voice .it modules the existing engine plays
(so SFX keep working):
  - songs/<name>.mid  -> mid2it (real MIDI composition; PREFERRED)
  - songs/*.zip MML   -> mml2it (AddmusicK score ripped from an .spc)

A songs/<out>.mid OVERRIDES the .zip for that output (e.g. songs/level1.mid wins
over ct1.zip). Output names map to the in-game roles wired in src/audio.c:

  ct2..ct10  -> music_level2..music_level10   (per-level themes)
  ct-portal  -> music_portal                  (all gems collected / exit open)
  ct-gameover-> music_gameover                 (game over screen)
  songs/levelN.mid -> music_levelN            (real MIDI, overrides the ct rip)

Victory/credits reuses Golden Anthem (level10) in audio.c -- no separate module.

Run: python3 tools/build_songs.py     (called by `make songs`)
"""
import os
import sys
import glob
import zipfile
import tempfile
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SONGS = os.path.join(ROOT, "songs")
sys.path.insert(0, HERE)
from mml2it import build_module

# (zip basename, output module name). level1 is now sourced from songs/level1.mid
# (a real MIDI) rather than the ct1 rip, so ct1 is dropped here.
TRACKS = [
    ("ct2", "level2"), ("ct3", "level3"), ("ct4", "level4"),
    ("ct5", "level5"), ("ct6", "level6"), ("ct7", "level7"), ("ct8", "level8"),
    ("ct9", "level9"), ("ct10", "level10"),
    ("ct-portal", "portal"), ("ct-gameover", "gameover"),
]

# Optional per-track BPM overrides if the t-value heuristic sounds off when you
# audition res/<name>_preview.wav. e.g. {"level7": 150}
BPM_OVERRIDE = {}


def main():
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        # MIDI sources first; a songs/<out>.mid replaces the matching .zip below.
        mids = {os.path.splitext(os.path.basename(m))[0]: m
                for m in glob.glob(os.path.join(SONGS, "*.mid"))}
        for out, mid in sorted(mids.items()):
            print(f"[MIDI] {out} <- {os.path.basename(mid)}")
            subprocess.run([sys.executable, os.path.join(HERE, "mid2it.py"), mid, out],
                           check=True)
        for zname, out in TRACKS:
            if out in mids:
                continue                      # a MIDI already produced this module
            zp = os.path.join(SONGS, f"{zname}.zip")
            if not os.path.exists(zp):
                print(f"!! missing {zp}"); continue
            dest = os.path.join(tmp, zname)
            with zipfile.ZipFile(zp) as z:
                z.extractall(dest)
            txts = glob.glob(os.path.join(dest, "**", "*.txt"), recursive=True)
            if not txts:
                print(f"!! no .txt MML in {zname}.zip"); continue
            total += build_module(sorted(txts)[0], out, BPM_OVERRIDE.get(out))
    print(f"\nbuilt modules ({len(mids)} from MIDI), {total/1024:.1f}KB of MML .it")


if __name__ == "__main__":
    main()
