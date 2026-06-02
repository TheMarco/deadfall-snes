# Deadfall SNES port - build via PVSnesLib.
# If PVSNESLIB_HOME isn't exported, fall back to the marker the installer wrote.
PVSNESLIB_HOME ?= $(shell cat .pvsneslib_home 2>/dev/null)

ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error PVSNESLIB_HOME is not set. Run: bash tools/install_pvsneslib.sh (with network), \
then `export PVSNESLIB_HOME=...` or rely on the .pvsneslib_home marker file.)
endif

# Audio: smconv builds res/soundbank.{asm,h,bnk} from these .it modules and
# snes_rules auto-links soundbank.obj. The SFX bank MUST be first so its samples
# are the global effects (spcLoadEffect/spcEffect); music modules follow.
# Music = test.mid converted to a chiptune module by tools/mid2it.py (real
# composition played with synth voices; channel 9 -> drum one-shots). Generated
# music_*.it / music_demo.it are kept in res/ but unused here.
AUDIOFILES := res/sfx.it res/music_test.it
export SOUNDBANK := res/soundbank

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

# smconv flags: soundbank mode, output base, verbose, bank 5, check effect size
SMCONVFLAGS := -s -o $(SOUNDBANK) -V -b 5 -f

# .incbin dependencies aren't auto-tracked: reassemble ROM data whenever any
# converted asset changes, so regenerating res/ always lands in the ROM.
data.obj: data.asm hdr.asm $(wildcard res/*.pic) $(wildcard res/*.pal) $(wildcard res/*.map)

.PHONY: all rom run bitmaps gfx levels clean distclean

export ROMNAME := deadfall

# Our headers live in include/
CFLAGS += -I$(CURDIR)/include

# Default target: build with the project-local portable `sed` shim on PATH
# (snes_rules uses GNU-style `sed -i`, which macOS BSD sed rejects). Re-invoking
# make is the reliable way to get the shim into the recipe shells' environment.
all:
	@PATH="$(CURDIR)/tools/bin:$$PATH" $(MAKE) --no-print-directory rom

rom: $(ROMNAME).sfc

# Build, then deploy into OpenEmu's library and open OpenEmu.
run: all
	@bash tools/run_openemu.sh

# Regenerate level data from the JS source (host python; no toolchain needed).
levels:
	python3 tools/convert_levels.py

# Convert PNG art -> SNES tiles/palettes in res/ (needs gfx2snes on PATH).
gfx:
	bash tools/convert_assets.sh ../cubed

clean: cleanBuildRes cleanRom cleanGfx cleanAudio
	@rm -f src/*.ps src/*.asp src/*.asm src/*.obj
	@rm -f *.obj linkfile

distclean: clean
	@rm -f res/*.pic res/*.pal res/*.map res/*.inc res/*_data.as
