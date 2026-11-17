#!/bin/bash
# Assemble /src/dist: exe + libvlc + whitelisted plugins only.
# FULL_PLUGINS=1 copies every plugin (for the empirical module-discovery pass).
set -euo pipefail
SDK=/work/install
DIST=/src/dist
LIST=/src/plugins.txt
rm -rf "$DIST"
mkdir -p "$DIST/plugins"
cp /work/app/GhostReel.exe "$SDK/bin/libvlc.dll" "$SDK/bin/libvlccore.dll" "$DIST/"
cp "$SDK/lib/vlc/vlc-cache-gen.exe" "$DIST/"
PLUG=$SDK/lib/vlc/plugins
if [ "${FULL_PLUGINS:-0}" = 1 ]; then
    cp -r "$PLUG"/. "$DIST/plugins/"
else
    missing=0
    while read -r line; do
        line="${line%%#*}"; line="$(echo "$line" | tr -d '\r' | xargs)"
        [ -z "$line" ] && continue
        f="$PLUG/$line"
        if [ -f "$f" ]; then
            mkdir -p "$DIST/plugins/$(dirname "$line")"
            cp "$f" "$DIST/plugins/$line"
        else
            echo "warn: plugin not built: $line"; missing=$((missing+1))
        fi
    done < "$LIST"
    echo "missing plugins: $missing"
fi
find "$DIST/plugins" -name '*.la' -delete

# ---- licenses: project + VLC + every bundled contrib library ------------------
. "$(dirname "$0")/vlc-version.sh"
VLC=/work/vlc-${VLC_VERSION}
LIC="$DIST/licenses"
mkdir -p "$LIC/vlc"
cp /src/LICENSE "$DIST/LICENSE.txt"
cp /src/THIRD_PARTY_NOTICES.md "$DIST/"
cp "$VLC/COPYING" "$LIC/vlc/COPYING.GPL-2.0.txt"
cp "$VLC/COPYING.LIB" "$LIC/vlc/COPYING.LGPL-2.1.txt"
cp "$VLC/AUTHORS" "$LIC/vlc/AUTHORS.txt"
for d in "$VLC"/contrib/win64/*/; do
    pkg=$(basename "$d")
    case "$pkg" in amf|mingw64|utfcpp) continue;; esac  # headers only / toolchain
    files=$(find "$d" -maxdepth 2 -type f \( -iname 'COPYING*' -o -iname 'LICENSE*'             -o -iname 'LICENCE*' -o -iname 'FTL.TXT' -o -iname 'GPLv2.TXT' \) 2>/dev/null)
    [ -z "$files" ] && continue
    mkdir -p "$LIC/$pkg"
    for f in $files; do cp "$f" "$LIC/$pkg/$(basename "$f").txt"; done
done
echo "plugins: $(find "$DIST/plugins" -name '*.dll' | wc -l)  size: $(du -sh "$DIST" | cut -f1)"
