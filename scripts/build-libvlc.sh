#!/bin/bash
# Build minimal libVLC (local file playback only) for win64 inside the
# VideoLAN vlc-debian-win64-3.0 image. Build tree lives in /work (volume).
set -euo pipefail
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPTS/vlc-version.sh"
JOBS=${JOBS:-$(nproc)}

bash "$SCRIPTS/fetch-vlc.sh"
VLC=/work/vlc-${VLC_VERSION}
cd "$VLC"

# ---- contribs: only what the stripped player needs --------------------------
CONTRIB_PKGS=".ffmpeg .dav1d .ass .freetype2 .fribidi .harfbuzz .zlib .speexdsp .ebml .matroska .ogg .dvbpsi"
mkdir -p contrib/win64
cd contrib/win64
if [ ! -f config.mak ]; then
    ../bootstrap --host=$HOST_TRIPLET --disable-disc --disable-net --disable-sout
fi
make -j"$JOBS" $CONTRIB_PKGS
cd "$VLC"

# ---- libvlc ------------------------------------------------------------------
[ -f configure ] || ./bootstrap
mkdir -p win64
cd win64
if [ ! -f config.status ]; then
    ../configure --host=$HOST_TRIPLET --prefix=/work/install \
        --disable-vlc --disable-qt --disable-skins2 --disable-lua --disable-nls \
        --disable-sout --disable-vlm --disable-addonmanagermodules --disable-update-check \
        --disable-dvdread --disable-dvdnav --disable-bluray --disable-dshow \
        --disable-smb --disable-dsm --disable-upnp --disable-microdns --disable-chromecast \
        --disable-libgcrypt --disable-gnutls --disable-taglib --disable-goom --disable-projectm \
        --disable-vcd --disable-libcddb --disable-screen --disable-live555 --disable-a52 \
        --disable-faad --disable-mad --disable-mpg123 --disable-libmpeg2 --disable-vorbis \
        --disable-theora --disable-opus --disable-flac --disable-png \
        --disable-jpeg --disable-libxml2 --disable-archive --disable-sftp --disable-nfs \
        --disable-srt --disable-shout --disable-zvbi --disable-telx --disable-kate \
        --disable-tiger --disable-caca --disable-sdl-image --disable-gme --disable-sid \
        --disable-mod --disable-mpc --disable-crystalhd --disable-dc1394 --disable-dv1394 \
        --disable-linsys --disable-decklink --disable-ncurses --disable-medialibrary \
        --disable-fluidsynth --disable-fluidlite --disable-aribsub --disable-aribb25 \
        --disable-chromaprint --disable-twolame --disable-x264 --disable-x26410b \
        --disable-x265 --disable-vpx --disable-aom --disable-gles2 --disable-glspectrum \
        --enable-avcodec --enable-merge-ffmpeg --enable-swscale --enable-libass \
        --enable-freetype --enable-dav1d \
        --disable-debug --enable-optimizations
fi
make -j"$JOBS"
rm -rf /work/install
make install
$HOST_TRIPLET-strip --strip-unneeded /work/install/bin/*.dll /work/install/bin/*.exe || true
find /work/install/lib/vlc/plugins -name '*.dll' -exec $HOST_TRIPLET-strip --strip-unneeded {} +
echo "libvlc installed to /work/install"
