# Deadfall (SNES)

A Super Nintendo port of **Deadfall**, my game — play the original at
**[deadfall.ai-created.com](https://deadfall.ai-created.com)**.

This is a faithful, from-scratch reimplementation of the full game as a real SNES
cartridge image (`deadfall.sfc`) in C using [PVSnesLib](https://github.com/alekmaul/pvsneslib).
It runs on emulators (tested in OpenEmu) and real hardware.

**▶ Play it in your browser, download the ROM, and read the full write-up:**
**[ai-created.com/stories/deadfall-snes-port](https://www.ai-created.com/stories/deadfall-snes-port)**

**This SNES version is fully free and open source.** You can also build it yourself
with the instructions below.

## Status

**Complete and in beta.** The full game is playable end to end across all 10 levels:
tile-locked movement with input buffering, push (with the original's hold-to-push delay),
mining with crush screen-shake feedback, animated gravity with smooth falling tiles and
crush, BFS enemies, the zapping robot, 4-direction section transitions over a parallax
scrolling background, an SMAS-style iris wipe between levels, HUD with a live minimap,
title screen, attract mode, death cinematic, BRR sound effects, and a full chiptune
soundtrack.

Quality-of-life features:

- **Pause** — START pauses/resumes during play (music ducks, par clock stops).
- **Battery save (SRAM)** — the high score persists, and a run in progress is
  checkpointed at every level start: power off, then press **SELECT on the title** to
  resume where you left off, with the same lives and continues. The save carries the
  run's whole continue budget, so resuming is a break, never an extra continue — after
  the third continue the run is erased and it's back to level 1.
- **FastROM** (3.58 MHz) cartridge image with a valid header checksum.
- Enemy RNG is seeded from the frame you press START, so playthroughs differ.

## Build

You'll need [PVSnesLib](https://github.com/alekmaul/pvsneslib) installed with the
`PVSNESLIB_HOME` environment variable pointing at it.

```sh
make            # builds deadfall.sfc
make run        # build, then deploy into OpenEmu (tools/run_openemu.sh)
make clean
```

Then load `deadfall.sfc` in any SNES emulator (Mesen-S, bsnes, snes9x, OpenEmu) or
flash it to a cartridge.

The generated SNES assets (`res/*.pic` / `.pal` / `.map` / `.it`, and `src/levels.c`)
are committed, so the ROM builds without re-running the asset pipeline. Regenerating
assets from the original art additionally requires the source game and the Python tools
in `tools/` (`build_gfx.py`, `make_bg.py`, `build_songs.py`, `convert_levels.py`).

## Project layout

| Path | What |
|------|------|
| `src/`, `include/` | C source — game logic, rendering, audio |
| `hdr.asm`, `data.asm` | LoROM header/vectors + incbin'd assets |
| `res/` | converted graphics & audio |
| `tools/` | Python asset-conversion pipeline |
| `Makefile` | build |

## License

The Deadfall game and original assets are © their author (me). This SNES port's
source code is released as free and open source (MIT) — see `LICENSE`.
