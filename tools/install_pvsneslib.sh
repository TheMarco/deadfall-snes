#!/usr/bin/env bash
# PVSnesLib installer for macOS (Apple Silicon via Rosetta or Intel).
# Run this with network access:  ! bash /Users/marcovhv/projects/GIT/cubed-snes/tools/install_pvsneslib.sh
#
# It downloads the latest PVSnesLib release, extracts it to ~/pvsneslib,
# detects PVSNESLIB_HOME, persists it to ~/.zshrc, and writes the value to
# cubed-snes/.pvsneslib_home so the build can pick it up.
set -euo pipefail

DEST="$HOME/pvsneslib"
PROJ="/Users/marcovhv/projects/GIT/cubed-snes"
TMP="$(mktemp -d)"
API="https://api.github.com/repos/alekmaul/pvsneslib/releases/latest"

echo ">> Querying latest PVSnesLib release..."
curl -sL "$API" -o "$TMP/rel.json"
TAG=$(python3 -c "import json;print(json.load(open('$TMP/rel.json')).get('tag_name','?'))")
echo "   Latest tag: $TAG"

echo ">> Available assets:"
python3 -c "import json;[print('   -',a['name']) for a in json.load(open('$TMP/rel.json'))['assets']]"

# Pick a macOS asset (darwin/macos/osx). Prefer arm64 if present, else any darwin.
URL=$(python3 - "$TMP/rel.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assets=d['assets']
def score(n):
    n=n.lower()
    s=0
    if 'darwin' in n or 'macos' in n or 'osx' in n or 'mac' in n: s+=10
    if 'arm' in n or 'aarch' in n or 'apple' in n: s+=2   # native if offered
    if n.endswith('.zip'): s+=1
    return s
cands=[(score(a['name']),a['name'],a['browser_download_url']) for a in assets]
cands=[c for c in cands if c[0]>=10]
cands.sort(reverse=True)
print(cands[0][2] if cands else "")
PY
)

if [ -z "$URL" ]; then
  echo "!! No macOS asset found automatically."
  echo "!! Pick the right .zip from the list above and run:"
  echo "     curl -L -o $TMP/pvs.zip <URL> && unzip -q $TMP/pvs.zip -d $DEST"
  echo "!! Then re-run this script's detection tail, or set PVSNESLIB_HOME by hand."
  exit 2
fi

echo ">> Downloading: $URL"
curl -L --retry 3 -o "$TMP/pvs.zip" "$URL"

echo ">> Extracting to $DEST ..."
rm -rf "$DEST"; mkdir -p "$DEST"
unzip -q "$TMP/pvs.zip" -d "$DEST"

# Locate the dir that contains devkitsnes/snes_rules
RULES=$(find "$DEST" -name snes_rules -path "*devkitsnes*" 2>/dev/null | head -1)
if [ -z "$RULES" ]; then
  # Some archives nest one level; search broadly for snes_rules
  RULES=$(find "$DEST" -name snes_rules 2>/dev/null | head -1)
fi
if [ -z "$RULES" ]; then
  echo "!! Could not find snes_rules under $DEST. Listing top level:"
  find "$DEST" -maxdepth 2 | head -40
  exit 3
fi
HOMEDIR=$(cd "$(dirname "$RULES")/.." && pwd)   # parent of devkitsnes
echo ">> PVSNESLIB_HOME = $HOMEDIR"

# Make tool binaries runnable on macOS (clear quarantine, set +x)
echo ">> Clearing quarantine + setting execute bits on binaries..."
xattr -dr com.apple.quarantine "$HOMEDIR" 2>/dev/null || true
find "$HOMEDIR/devkitsnes" -type f \( -name '*' \) -perm -u+r -exec sh -c '
  for f; do case "$(file -b "$f" 2>/dev/null)" in *executable*|*Mach-O*) chmod +x "$f";; esac; done
' _ {} + 2>/dev/null || true

# Persist for future shells
if ! grep -q "PVSNESLIB_HOME" "$HOME/.zshrc" 2>/dev/null; then
  {
    echo ""
    echo "# PVSnesLib (SNES dev toolchain)"
    echo "export PVSNESLIB_HOME=\"$HOMEDIR\""
    echo "export PATH=\"\$PVSNESLIB_HOME/devkitsnes/bin:\$PVSNESLIB_HOME/devkitsnes/tools:\$PATH\""
  } >> "$HOME/.zshrc"
  echo ">> Appended PVSNESLIB_HOME to ~/.zshrc"
fi

# Drop a marker the agent/build can read
echo "$HOMEDIR" > "$PROJ/.pvsneslib_home"

echo ">> Quick tool check:"
for t in 816-tcc wla-65816 wlalink gfx2snes smconv constify; do
  p=$(find "$HOMEDIR/devkitsnes" -name "$t" 2>/dev/null | head -1)
  printf '   %-12s %s\n' "$t" "${p:-MISSING}"
done

echo ""
echo ">> DONE. PVSNESLIB_HOME=$HOMEDIR"
echo ">> Wrote $PROJ/.pvsneslib_home"
echo ">> Open a new shell (or: export PVSNESLIB_HOME=\"$HOMEDIR\") and the build will work."
