# GhostReel

**A ghost of a video player.** GhostReel plays your downloaded videos in a
borderless, always-on-top window that floats over everything else. Fade it out
or make it click-through, and keep working underneath.

Powered by a stripped-down **[libVLC](https://www.videolan.org/vlc/libvlc.html)**
compiled from the official [VLC source](https://www.videolan.org/vlc/download-sources.html).
Only local-file playback is kept, so the whole app is about 35 MB.

> GhostReel is not affiliated with VideoLAN. See [Credits](#credits).

## Features

- **Always on top, borderless.** Drag anywhere to move it and drag the edges to
  resize. The window keeps the video's aspect ratio; hold **Shift** to resize freely.
- **Opacity** from 10% to 100%: Ctrl + mouse wheel, hotkeys, or the right-click menu.
- **Click-through mode.** Mouse clicks go to the app underneath while the video keeps playing.
- **Playlist.** Open files or whole folders (scanned recursively, sorted by name),
  drag and drop, loop and shuffle.
- **Hardware decoding** with D3D11VA or DXVA2, falling back to software.
- **Subtitles** embedded in MKV/MP4 files (via libass).
- Remembers window position, opacity, volume, loop and shuffle.
- Formats: mp4, m4v, mkv, webm, avi, mov, wmv, flv, ts, m2ts, mpg, ogv, 3gp, gif, …

## Controls

Global hotkeys work even when GhostReel is click-through or doesn't have focus:

| Hotkey | Action |
|---|---|
| `Ctrl+Alt+T` | Toggle click-through |
| `Ctrl+Alt+Up` / `Down` | Opacity ±5% |
| `Ctrl+Alt+Space` | Play / pause |
| `Ctrl+Alt+Right` / `Left` | Next / previous video |
| `Ctrl+Alt+Shift+Right` / `Left` | Seek ±10 s |
| `Ctrl+Alt+H` | Hide / show (pauses while hidden) |
| `Ctrl+Alt+Q` | Quit |

Mouse (when not click-through):

| Input | Action |
|---|---|
| Drag | Move window |
| Drag edge / corner | Resize |
| Wheel | Volume |
| Ctrl + wheel | Opacity |
| Double-click | Play / pause |
| Right-click | Menu (open files/folder, loop, shuffle, opacity, …) |
| Drop files | Add to playlist (Shift+drop replaces it) |

Keyboard (when focused): `Space` pause, `←/→` seek 5 s, `↑/↓` volume, `N`/`P`
next/previous, `M` mute, `O` open.

## Install

Download the latest `GhostReel-*-win64.zip` from
[Releases](../../releases), unzip it anywhere and run `GhostReel.exe`. Nothing
gets installed. Settings live in `%APPDATA%\GhostReel\settings.ini`.

You can also pass files or folders on the command line:

```
GhostReel.exe "D:\Videos\clip.mp4" "D:\Videos\Folder"
```

## Building from source

Requirements: Windows 10/11, Docker Desktop (WSL2 backend), PowerShell 7.

```powershell
.\build.ps1                 # first build: libVLC + contribs from source (long), then the app
.\build.ps1 -AppOnly        # rebuild GhostReel.exe + dist\ (seconds)
.\build.ps1 -RebuildLibvlc  # reconfigure / recompile libVLC
.\build.ps1 -FullPlugins    # dist\ with every plugin (for finding which ones are needed)
```

Output goes to `dist\`. Everything compiles inside VideoLAN's own mingw-w64
toolchain image (`registry.videolan.org/vlc-debian-win64-3.0`), so no compiler
is needed on the host. The build tree is cached in the Docker volume
`ghostreel-vlc-work`.

### How it's stripped

1. **Contribs:** only FFmpeg, dav1d, libass (with FreeType, HarfBuzz and FriBidi),
   libebml/libmatroska, libogg, libdvbpsi, speexdsp and zlib are built. See
   `scripts/build-libvlc.sh`.
2. **configure:** about 70 `--disable-*` flags remove the VLC executable, Qt,
   skins, Lua, network access, disc support, stream output and many codecs.
3. **Plugin whitelist:** `plugins.txt` lists the 61 plugins that are kept;
   everything else is left out of `dist\`.
4. **Rendering:** libVLC decodes into memory (`vmem`, RV32), and GhostReel draws
   the frames with GDI into a layered window. That's what makes per-window
   opacity and click-through work reliably; no Direct3D video output plugins
   are needed.

Debug logging: set `GHOSTREEL_LOG=1` before launching. libVLC's log is written
to `ghostreel.log` next to the exe.

## Project layout

```
src/main.c          window, hotkeys, menu, drag/resize, painting
src/player.c        libVLC wrapper: vmem callbacks, playlist, events
src/settings.c      INI persistence
scripts/            fetch + build libVLC, build app, assemble dist
plugins.txt         plugin whitelist
docker/Dockerfile   VideoLAN toolchain image
build.ps1           host entry point
```

## Credits

GhostReel exists thanks to **[VLC media player](https://www.videolan.org/) and
libVLC by VideoLAN** and the VLC contributors. Every bit of demuxing, decoding
and audio output is theirs. If you find this useful, consider
[donating to VideoLAN](https://www.videolan.org/contribute.html).

Also built on FFmpeg, dav1d, libass, FreeType, HarfBuzz, FriBidi, libebml,
libmatroska, libogg, libdvbpsi, zlib and others. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for versions, licenses and
how to get the corresponding source.

VLC is a trademark of VideoLAN. GhostReel is not affiliated with or endorsed by
VideoLAN.

## License

GhostReel source code: [MIT](LICENSE). Binary releases bundle libVLC
(LGPL-2.1+), VLC plugins (LGPL-2.1+ / GPL-2.0+) and third-party libraries under
their own licenses. Full license texts are in `licenses/` inside each release.
