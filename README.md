# Deadfall — SNES port

A faithful SNES cartridge port of **Deadfall** (the full Phaser 3 game in `../cubed`),
built in C with [PVSnesLib](https://github.com/alekmaul/pvsneslib). Runs on real SNES
hardware and emulators (tested in OpenEmu).

## Status

Playable: movement / push (with the 400 ms push-delay) / mining / animated gravity
with smooth falling tiles / crush / enemies (BFS) / robot + lightning / section
transitions with a parallax scrolling background (4-direction slide) / HUD / SFX /
chiptune music converted from MIDI.

## Build

Requires PVSnesLib with `PVSNESLIB_HOME` set.

```sh
make            # builds deadfall.sfc
make run        # build + deploy into OpenEmu (tools/run_openemu.sh)
make clean
```

The generated SNES assets (`res/*.pic/.pal/.map/.it`, `src/levels.c`) are committed so
the ROM builds without re-running the asset pipeline. To regenerate assets you also need
the source game at `../cubed` and the tools in `tools/` (see `tools/build_gfx.py`,
`tools/make_bg.py`, `tools/mid2it.py`, `tools/convert_levels.py`).

## Layout

- `src/`, `include/` — C source (game logic + rendering + audio)
- `hdr.asm`, `data.asm` — LoROM header/vectors + incbin'd assets
- `res/` — converted graphics / audio
- `tools/` — Python/asset-conversion pipeline
- `Makefile`
