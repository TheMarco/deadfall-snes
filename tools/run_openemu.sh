#!/usr/bin/env bash
# Deploy the freshly built ROM into OpenEmu's library (overwriting the existing
# 'deadfall' entry, so no duplicate imports) and bring OpenEmu to the front.
# After this, double-click "deadfall" in OpenEmu to (re)launch the new build.
set -e
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
ROM="$PROJ/deadfall.sfc"
LIBDIR="$HOME/Library/Application Support/OpenEmu/Game Library/roms/Super Nintendo (SNES)"
LIB="$LIBDIR/deadfall.sfc"

if [ ! -f "$ROM" ]; then echo "No $ROM — run 'make' first." >&2; exit 1; fi

# OpenEmu auto-resumes a save state when you relaunch a game. After a rebuild the
# ROM layout shifts (assets grow, dead sections get discarded), so resuming an
# old state lands on moved code/data and FREEZES (looks like "Start does nothing").
# Clear the auto-save-state so each deploy cold-boots the new ROM. It's only an
# auto-state of this dev ROM and OpenEmu recreates it next play session.
AUTOSTATE="$HOME/Library/Application Support/OpenEmu/Save States/SuperNES/deadfall/Auto Save State.oesavestate"
if [ -e "$AUTOSTATE" ]; then
    rm -rf "$AUTOSTATE"
    echo "Cleared stale OpenEmu auto-save-state (forces a cold boot of the new ROM)."
fi

if [ -f "$LIB" ]; then
    cp -f "$ROM" "$LIB"
    echo "Updated OpenEmu copy: $LIB"
    open -a OpenEmu
    echo "OpenEmu is up — double-click 'deadfall' to (re)launch with this build."
else
    echo "No OpenEmu copy yet — importing $ROM into OpenEmu..."
    open -a OpenEmu "$ROM"
    echo "Imported. Double-click 'deadfall' in OpenEmu to play; future builds auto-update it."
fi
