#!/bin/bash
# Compile GhostReel.exe against the libvlc SDK in /work/install
set -euo pipefail
. "$(dirname "$0")/vlc-version.sh"
SRC=/src/src
OUT=/work/app
SDK=/work/install
mkdir -p "$OUT"
CC=$HOST_TRIPLET-gcc
WINDRES=$HOST_TRIPLET-windres
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -I$SDK/include"
$WINDRES -I"$SRC" "$SRC/resource.rc" -O coff -o "$OUT/resource.o"
$CC $CFLAGS -mwindows -o "$OUT/GhostReel.exe" \
    "$SRC/main.c" "$SRC/player.c" "$SRC/settings.c" "$OUT/resource.o" \
    -L"$SDK/lib" -lvlc -lgdi32 -luser32 -lshell32 -lole32 -luuid -lshlwapi -lcomctl32 \
    -static-libgcc -s
echo "built $OUT/GhostReel.exe"
