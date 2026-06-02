#!/usr/bin/env bash
# Convert Deadfall PNG art -> SNES tile/palette data in res/  (needs gfx2snes).
# Usage: bash tools/convert_assets.sh [path-to-cubed]   (default ../cubed)
#
# Sprite layout decisions (cubed-snes):
#   BG1 gameplay tiles : 4bpp, 16x16 metatiles (block/gem/boulder/portal/spawn/extralife)
#   OBJ entity sprites : 4bpp, 16x16 (player/enemy/robot/death/lightning/bonus/extralife)
#   BG3 HUD font       : 2bpp, 8x8
# gfx2snes flags mirror the proven df-snes pipeline:
#   tiles : -gs 16 -pc 16 -pe 0      sprites: -s 16 -pc 16 -pe 0      font(2bpp): -gs 8 -pc 4 -pe 0
set -euo pipefail

CUBED="${1:-../cubed}"
SPR="$CUBED/public/sprites"
FONT="$CUBED/public/fonts"
OUT="res"
mkdir -p "$OUT"

if ! command -v gfx2snes >/dev/null 2>&1; then
  echo "ERROR: gfx2snes not on PATH. Install PVSnesLib and add devkitsnes/tools to PATH." >&2
  exit 1
fi

tile()  { echo ">> tile  $1"; gfx2snes -i "$SPR/$1.png"  -o "$OUT/$2" -gs 16 -pc 16 -pe 0; }
sprite(){ echo ">> spr   $1"; gfx2snes -i "$SPR/$1.png"  -o "$OUT/$2" -s 16  -pc 16 -pe 0; }

# ---- BG1 gameplay tiles (per-level gem/boulder selected at runtime) ----
tile block      block
for i in $(seq 1 10); do tile "gem-$i" "gem$i"; done
tile iron-sprite        iron
tile portal             portal
tile spawn-point        spawn
tile robot-spawn-point  robotspawn
tile extralife          extralife

# ---- OBJ entity sprites ----
sprite player        spr_player
sprite player-death  spr_player_death
sprite enemy         spr_enemy
sprite enemy-death   spr_enemy_death
sprite enemy-robot   spr_robot
sprite lightning     spr_lightning
sprite bonus         spr_bonus

# ---- BG3 HUD font (2bpp 8x8) + small life icon ----
echo ">> font  thick_8x8"
gfx2snes -i "$FONT/thick_8x8.png" -o "$OUT/hud_font" -gs 8 -pc 4 -pe 0
echo ">> icon  player-8"
gfx2snes -i "$SPR/player-8.png"   -o "$OUT/player8"  -gs 8 -pc 4 -pe 0

echo "=== done; outputs in $OUT/ ==="
ls -la "$OUT/"
