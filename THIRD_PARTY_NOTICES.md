# Third-party notices

GhostReel is a small front-end. All media handling is done by **libVLC**, built
from the official VLC source release, and the libraries VLC's own contrib system
builds alongside it. GhostReel's own code is MIT (see `LICENSE`). The components
below keep their own licenses. Full license texts ship in the `licenses/` folder
of every binary release.

## VLC media player / libVLC — VideoLAN

- **Version:** 3.0.24 "Vetinari"
- **Source:** https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz
  (sha256 `e7cab503d1d7d5849b89d2cf0e1ee60d0ef6d012407791b644b9cfc0cc225fdf`),
  from https://www.videolan.org/vlc/download-sources.html
- **License:** `libvlc.dll` and `libvlccore.dll` are LGPL-2.1-or-later. Plugins
  are LGPL-2.1-or-later or GPL-2.0-or-later, depending on the module.
- **Copyright:** © 1996-2026 the VideoLAN team and VLC authors (see `licenses/vlc/AUTHORS.txt`)
- **Website:** https://www.videolan.org/

VLC is a trademark of VideoLAN. GhostReel is an independent project and is **not
affiliated with or endorsed by VideoLAN**.

## Libraries built by VLC's contrib system

| Library | Version | License |
|---|---|---|
| FFmpeg (libavcodec, libavformat, libavutil, libswscale) | 8.1.2 | LGPL-2.1-or-later (built **without** `--enable-gpl` / nonfree) |
| dav1d | 1.5.4 | BSD-2-Clause |
| libass | 0.17.5 | ISC |
| FreeType | 2.14.3 | FreeType License (FTL) |
| HarfBuzz | 14.2.1 | MIT ("Old MIT") |
| GNU FriBidi | 1.0.12 | LGPL-2.1-or-later |
| libebml | 1.4.6 | LGPL-2.1-or-later |
| libmatroska | 1.7.2 | LGPL-2.1-or-later |
| libogg | 1.3.6 | BSD-3-Clause |
| libdvbpsi | 1.3.3 | LGPL-2.1-or-later |
| zlib | 1.3.2 | Zlib |
| OpenJPEG | 2.5.4 | BSD-2-Clause |
| libgsm | 1.0.13 | TU Berlin permissive license |
| libpng | 1.6.58 | libpng License v2 |
| libxml2 | 2.15.3 | MIT |
| fontconfig | 2.12.3 | MIT-style |
| GNU libiconv | 1.17 | LGPL-2.1-or-later |
| mingw-w64 winpthreads | 14.0.0 | MIT / BSD-3-Clause |

FreeType: *Portions of this software are copyright © 2026 The FreeType Project
(www.freetype.org). All rights reserved.*

## Getting the source (LGPL / GPL)

The exact source for every binary release can be rebuilt from this repository:

1. `scripts/vlc-version.sh` pins the VLC tarball and its checksum.
2. `scripts/build-libvlc.sh` shows the contrib packages and the `configure` flags used.
3. VLC's contrib system downloads each library's upstream source tarball. Their
   URLs and checksums are in `contrib/src/<package>/rules.mak` and `SHA512SUMS`
   inside the VLC source.

`.\build.ps1` rebuilds everything inside VideoLAN's official
`registry.videolan.org/vlc-debian-win64-3.0` toolchain image. You can replace
`libvlc.dll`, `libvlccore.dll` or any plugin in a release with your own build.
