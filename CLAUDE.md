# CLAUDE.md

Guidance for AI agents working in this repo. Read this before building or touching assets.

## What this is

A from-scratch SNES port of **Deadfall** (a Boulder-Dash-style game), written in C against
[PVSnesLib](https://github.com/alekmaul/pvsneslib). The build produces a 2 MB LoROM cartridge
image, `deadfall.sfc`, that runs on emulators (OpenEmu, Mesen-S, bsnes, snes9x) and real hardware.
The original game lives at https://deadfall.ai-created.com and the JS source it mirrors is in a
sibling `../cubed` tree (not in this repo).

## Build & run

```sh
make            # -> deadfall.sfc  (the `all` target re-invokes `make rom` with tools/bin on PATH)
make run        # build, then deploy into OpenEmu (tools/run_openemu.sh)
make clean      # remove build intermediates + ROM
make distclean  # also remove converted res/*.pic/.pal/.map
```

- Requires `PVSNESLIB_HOME`. If it isn't exported, the Makefile reads it from the
  `.pvsneslib_home` marker file (currently `/Users/marcovhv/pvsneslib/pvsneslib`).
- **macOS `sed` shim:** PVSnesLib's `snes_rules` uses GNU `sed -i`, which BSD/macOS `sed` rejects.
  The default `all` target puts `tools/bin` (a portable `sed` shim) on PATH and re-invokes make.
  Always build via `make` / `make all`, not `make rom` directly, or the build will fail on macOS.
- **Header deps are NOT tracked.** The Makefile only rebuilds C objects when the `.c` changes, not
  when an `include/*.h` it pulls in changes. After editing a header, force the affected source to
  recompile, e.g. `touch src/game.c && rm -f src/game.obj` before `make`.

## Releasing a beta / distribution ROM

There is a debug **level-skip** (tap **Y** → next level) gated behind `DEBUG_LEVEL_SKIP` in
`include/config.h`, used only by `src/game.c` (`#if DEBUG_LEVEL_SKIP`).

For a distribution build:
1. Set `DEBUG_LEVEL_SKIP` to `0` in `include/config.h`.
2. Force-recompile (header deps untracked): `touch src/game.c && rm -f src/game.obj`.
3. `make` — verify in an emulator that pressing **Y** in-game does nothing.
4. Distribute a clearly-named copy (e.g. `deadfall-beta.sfc`). `*.sfc` is gitignored, so ROMs are
   never committed — upload the file directly.

The default dev source keeps `DEBUG_LEVEL_SKIP 1`. Don't ship `deadfall.sfc` straight from a dev
build; a plain `make` re-enables the skip.

## Asset pipeline (committed outputs, regenerate on demand)

The generated SNES assets in `res/` (`*.pic` tiles, `*.pal` palettes, `*.map` tilemaps, `*.it`
music) and `src/levels.c` are **committed**, so the ROM builds without re-running the pipeline.
Assets are `.incbin`'d via `data.asm` (LoROM data) and `hdr.asm` (header/vectors).

Regeneration targets (need the Python tools in `tools/` and sometimes the `../cubed` source):

```sh
make levels   # tools/convert_levels.py  -> level data from the JS source (host python, no toolchain)
make songs    # tools/build_songs.py     -> res/music_*.it from songs/*.zip AddmusicK MML
make gfx      # tools/convert_assets.sh ../cubed -> PNG art into res/ tiles/pals (needs gfx2snes)
```

- **Music:** the 12 `songs/*.zip` MML scores are rebuilt into snesmod chiptune `.it` modules
  (`mml2it.py` → `build_songs.py`), played through snesmod so gameplay SFX keep working. SFX bank
  (`res/sfx.it`) MUST be first in `AUDIOFILES`; module order fixes the `MOD_*` indices in
  `src/audio.c`. Each level module bakes in the frantic/portal theme as a second looping section
  (see `include/music_layout.h`). MML tempo is scaled ×2.385.
- **PNG → SNES converters** (`tools/png2bg8.py`, `build_*.py`): when bit-packing palettes/pixels,
  cast numpy `uint8` to `int` BEFORE BGR555/shift math — `uint8` arithmetic silently overflows and
  produces an all-red palette.
- `make_bg.py`, `build_gfx.py`, `mid2it.py`, `convert_levels.py` etc. are the lower-level steps the
  `make` targets and `convert_assets.sh` orchestrate.

## Layout

| Path | What |
|------|------|
| `src/`, `include/` | C source — game logic (`game.c`, `player.c`, `enemy.c`, `ai.c`, `robot.c`, `gravity.c`, `world.c`, `levels.c`, `balance.c`), rendering (`render.c`), audio (`audio.c`), entry (`main.c`) |
| `include/config.h` | compile-time constants: grid, tile types, frame-based timing, pad bits, `DEBUG_LEVEL_SKIP` |
| `hdr.asm`, `data.asm` | LoROM header/vectors + `.incbin`'d assets (assets >32 KB are split across ROM banks) |
| `res/` | converted graphics & audio (committed build inputs) |
| `songs/` | source MML music (`*.zip`) |
| `tools/` | Python asset-conversion pipeline + `tools/bin` sed shim |
| `Makefile` | build |

## Conventions

- **Commits:** imperative, descriptive subject lines (see `git log`), no Conventional-Commits prefix.
  Co-author trailer: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Pushing:** `main` is the working branch; push only when asked.
- **Gitignored outputs:** `*.sfc`/`*.smc`/`*.sym` ROMs, `*.obj`, `src/*.asm`, `res/soundbank.*`,
  `include/soundbank_banks.h`, `linkfile`, asset previews — all regenerated, never committed.
- **Timing** is frame-based for NTSC ~60 fps (`round(ms / 16.667)`); see the constants in `config.h`.
