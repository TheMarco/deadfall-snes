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
