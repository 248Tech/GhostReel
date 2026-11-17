#!/bin/bash
# Download + verify + extract VLC source into /work/vlc-$VLC_VERSION
set -euo pipefail
. "$(dirname "$0")/vlc-version.sh"
cd /work
TARBALL="vlc-${VLC_VERSION}.tar.xz"
if [ ! -f "$TARBALL" ]; then
    curl -fL --retry 3 -o "$TARBALL.part" "$VLC_URL"
    mv "$TARBALL.part" "$TARBALL"
fi
echo "${VLC_SHA256}  ${TARBALL}" | sha256sum -c -
if [ ! -d "vlc-${VLC_VERSION}" ]; then
    tar -xJf "$TARBALL"
fi
