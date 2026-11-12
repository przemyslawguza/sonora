# Sonora

Audacious-style audio player (Qt 6 + GStreamer, C++17).

* **Transport buttons** like Audacious: Previous, Play, Pause, Stop, Next, Open, plus Repeat and Shuffle toggles.
  Icons are drawn by the program itself (palette colour, framed buttons), so they are visible with any theme.
  Play on a playing track restarts it; Previous/Next/Shuffle/Repeat stay inside the filtered playlist.
* **Adding / importing** (File menu, the arrow next to the Open button, right-click in a list, drag & drop):
  Add Files (Ctrl+O), Add Folder (Ctrl+Shift+O, recursive, natural sort), Add URL (Ctrl+U),
  Import Playlist as New (Ctrl+I; one new playlist per chosen file) and Import Playlist into Current.
  Supports M3U / M3U8 / PLS, relative paths are resolved against the playlist file.
* **Playlist manager** (View > Playlist manager, Ctrl+Y; shown by default, dockable): lists all playlists with
  their entry counts (`shown / total` while a filter is active); the playing one is marked.
  Click = show it, double-click = play it, right-click = rename / close / new / import.
* **Multiple playlists** as tabs: `+` button / Ctrl+T new, Ctrl+W close, F2 or double-click a tab to rename,
  drag tabs to reorder, right-click a tab or an item for more (e.g. *Copy selected to playlist*).
  File > Import/Export playlist (M3U). All playlists, names, order and the active tab are restored on start.
  The tab of the playlist that is currently playing shows a play icon; you can browse other tabs meanwhile.
* **Per-channel level meter** on the left: peak bar, RMS strip, peak-hold; range configurable (default -60 ... +1 dB).
  The bar count follows the stream (mono, stereo, surround).
* **Spectrum analyser**, visible by default (View > Spectrum analyser to hide): 56 log-spaced bars,
  ~30 Hz - 20 kHz, peak caps.
* **Playlist filter**, one per playlist (Ctrl+F): words are AND-matched, case-insensitive, against title and path.
  While a filter is active the list shows only matches and Next / Previous / auto-advance / Repeat stay inside
  the filtered set of the playing playlist. Enter plays the first match, Esc clears the filter.
* Drag & drop files/folders (added to the visible playlist), `sonora file-or-dir ...` on the command line.


## Defaults

All defaults are defined in one place, `src/Config.h` (`load()` in `Config.cpp` falls back to them, and the
Preferences texts are generated from them).

| Setting | Default |
|---|---|
| Spectrum analyser | 0 - 22 kHz linear, 224 bars, 4096 FFT bands, display -60 ... -20 dB |
| Level meters | -60 ... +1 dB, 0.33 dB per LED (about 186 LEDs), 10 px per channel, true-peak on |
| Meter / spectrum delay | 0 ms (one shared setting, Preferences > *Synchronisation*) |

Saved user settings override defaults: delete `~/.config/sonora/` to see new defaults.

## Features

* **Sound service** (File > Preferences > *Audio output*): Automatic, PipeWire, PulseAudio, ALSA, JACK, OSS ...
  (only installed ones are listed), output device per service, test tone. *Audio advanced*: buffer / latency time
  and a custom GStreamer sink (e.g. `alsasink device=hw:1,0`). Changing the output while playing keeps the position.
* **Spectrum analyser**: frequency range, linear/log axis, bar count, dB range and FFT size are configurable
  (Preferences > *Spectrum*); the FFT size takes effect from the next track.
* **LED level meters**: narrow LED ladder (green, yellow, orange, red above 0 dB, peak-hold LED, overs held 3 s);
  range, width per channel, dB per LED and the scale are configurable (Preferences > *Level meters*). The LED grid is
  anchored at 0 dB, so a red LED starting at exactly 0 dB exists for any LED step.
* **True-peak (inter-sample) metering**: the meter measures a copy of the signal oversampled to 192 kHz (at least 4x
  for 44.1/48 kHz material, as in ITU-R BS.1770), so the red LED lights when the waveform between two samples goes
  above 0 dB. Switch in Preferences > Level meters.
* **Synchronisation delay**: a single delay (0 - 2000 ms, Preferences > *Synchronisation*) applied to the level meters
  *and* the spectrum analyser together, to compensate the sound card / Bluetooth output latency so the pictures match
  what you hear. It can be changed while playing.
* **Playlist columns**: title, artist, album, year, time, file type; right-click the header to show/hide columns,
  click it to sort (asc / desc / original). The filter also matches artist, album and year.
* **Metadata tooltip** when the mouse rests on a track: artist, album artist, album, year, track/disc, genre, composer,
  length, format/codec, bitrate, sample rate, channels/bit depth, file size, path, comment. Tags are read in a
  background thread (GstDiscoverer) and cached on disk, so the next start is instant.
* **Time**: elapsed / total; click the time (or View > Show remaining time) for the remaining time. Playlist manager
  also shows each playlist's total length.
* **View > Dark mode**.
* **Keys**: **Space** play/pause (Audacious-like: Z previous, X play, C pause, V stop, B next, Ctrl+Left/Right seek),
  the keyboard's **Play/Pause, Stop, Next, Previous** keys work while the window is focused, and - through MPRIS2 over
  D-Bus - also system-wide when the window is in the background (GNOME, KDE, ...). Typing in the filter box is not
  affected.
* **Fast start**: the window is painted first; GStreamer initialises in a parallel thread, the playback pipeline (and
  the chosen sound service / device) is created when the first track starts, restoring playlists does not touch the
  disk up front, and background tag reading starts ~1.5 s after the window is up (at most 4 files in flight).
  `SONORA_PROFILE=1 sonora` prints start-up timings.

## Build

    sudo apt install build-essential debhelper cmake pkgconf qt6-base-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    cmake -S . -B build && cmake --build build -j && ./build/sonora

## Debian package (Debian 12 "bookworm" / 13 "trixie")

    ./build-deb.sh            # -> ../sonora_0.3.4_<arch>.deb

No architecture-specific code: run the same command on an arm64 or amd64 machine / chroot / container
(`sbuild`, `pbuilder`, or Docker `arm64v8/debian:12` with QEMU). Cross-compiling with
`dpkg-buildpackage -aarm64` additionally needs the Qt 6 host tools (`QT_HOST_PATH`); native builds are simpler.

Runtime: the `level` and `spectrum` elements are in `gstreamer1.0-plugins-good`; codecs come from
`-good`, `-ugly`, `-bad`, `-libav`. Display range of the analyser is set by `kFloorDb` / `kCeilDb`
in `src/SpectrumAnalyzer.h`.

---
# Sonora

Audacious-style audio player (Qt 6 + GStreamer, C++17).

* **Transport buttons** like Audacious: Previous, Play, Pause, Stop, Next, Open, plus Repeat and Shuffle toggles.
  Icons are drawn by the program itself (palette colour, framed buttons), so they are visible with any theme.
  Play on a playing track restarts it; Previous/Next/Shuffle/Repeat stay inside the filtered playlist.
* **Adding / importing** (File menu, the arrow next to the Open button, right-click in a list, drag & drop):
  Add Files (Ctrl+O), Add Folder (Ctrl+Shift+O, recursive, natural sort), Add URL (Ctrl+U),
  Import Playlist as New (Ctrl+I; one new playlist per chosen file) and Import Playlist into Current.
  Supports M3U / M3U8 / PLS, relative paths are resolved against the playlist file.
* **Playlist manager** (View > Playlist manager, Ctrl+Y; shown by default, dockable): lists all playlists with
  their entry counts (`shown / total` while a filter is active); the playing one is marked.
  Click = show it, double-click = play it, right-click = rename / close / new / import.
* **Multiple playlists** as tabs: `+` button / Ctrl+T new, Ctrl+W close, F2 or double-click a tab to rename,
  drag tabs to reorder, right-click a tab or an item for more (e.g. *Copy selected to playlist*).
  File > Import/Export playlist (M3U). All playlists, names, order and the active tab are restored on start.
  The tab of the playlist that is currently playing shows a play icon; you can browse other tabs meanwhile.
* **Per-channel level meter** on the left: peak bar, RMS strip, peak-hold; scale -90 dB ... +3 dB.
  The bar count follows the stream (mono, stereo, surround).
* **Spectrum analyser**, visible by default (View > Spectrum analyser to hide): 56 log-spaced bars,
  ~30 Hz - 20 kHz, peak caps.
* **Playlist filter**, one per playlist (Ctrl+F): words are AND-matched, case-insensitive, against title and path.
  While a filter is active the list shows only matches and Next / Previous / auto-advance / Repeat stay inside
  the filtered set of the playing playlist. Enter plays the first match, Esc clears the filter.
* Drag & drop files/folders (added to the visible playlist), `sonora file-or-dir ...` on the command line.


## What's new in 0.3.2

* **Sound service** (File > Preferences > *Audio output*): Automatic, PipeWire, PulseAudio, ALSA, JACK, OSS ...
  (only installed ones are listed), output device per service, test tone. *Audio advanced*: buffer / latency time
  and a custom GStreamer sink (e.g. `alsasink device=hw:1,0`). Changing the output while playing keeps the position.
* **Spectrum analyser**: default **0 - 24 kHz**, **112 bars** (2x) and **2048 FFT bands** (2x precision); frequency range,
  linear/log axis, bar count, dB range and FFT size are configurable (Preferences > *Spectrum*).
* **LED level meters**: narrow LED ladder (green, yellow, orange, red above 0 dB, peak-hold LED); range, width per
  channel, dB per LED and the scale are configurable (Preferences > *Level meters*).
* **Playlist columns**: title, artist, album, year, time, file type; right-click the header to show/hide columns,
  click it to sort (asc / desc / original). The filter also matches artist, album and year.
* **Metadata tooltip** when the mouse rests on a track: artist, album artist, album, year, track/disc, genre, composer,
  length, format/codec, bitrate, sample rate, channels/bit depth, file size, path, comment. Tags are read in a
  background thread (GstDiscoverer) and cached on disk, so the next start is instant.
* **Time**: elapsed / total; click the time (or View > Show remaining time) for the remaining time. Playlist manager
  also shows each playlist's total length.
* **View > Dark mode**.
* **Keys**: **Space** play/pause (Audacious-like: Z previous, X play, C pause, V stop, B next, Ctrl+Left/Right seek),
  the keyboard's **Play/Pause, Stop, Next, Previous** keys work while the window is focused, and - through MPRIS2 over
  D-Bus - also system-wide when the window is in the background (GNOME, KDE, ...). Typing in the filter box is not
  affected.

## Build

    sudo apt install build-essential debhelper cmake pkgconf qt6-base-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    cmake -S . -B build && cmake --build build -j && ./build/sonora

## Debian package (Debian 12 "bookworm" / 13 "trixie")

    ./build-deb.sh            # -> ../sonora_0.3.2_<arch>.deb

No architecture-specific code: run the same command on an arm64 or amd64 machine / chroot / container
(`sbuild`, `pbuilder`, or Docker `arm64v8/debian:12` with QEMU). Cross-compiling with
`dpkg-buildpackage -aarm64` additionally needs the Qt 6 host tools (`QT_HOST_PATH`); native builds are simpler.

Runtime: the `level` and `spectrum` elements are in `gstreamer1.0-plugins-good`; codecs come from
`-good`, `-ugly`, `-bad`, `-libav`. Display range of the analyser is set by `kFloorDb` / `kCeilDb`
in `src/SpectrumAnalyzer.h`.
---
# Sonora

Audacious-style audio player (Qt 6 + GStreamer, C++17).

* **Transport buttons** like Audacious: Previous, Play, Pause, Stop, Next, Open, plus Repeat and Shuffle toggles.
  Icons are drawn by the program itself (palette colour, framed buttons), so they are visible with any theme.
  Play on a playing track restarts it; Previous/Next/Shuffle/Repeat stay inside the filtered playlist.
* **Adding / importing** (File menu, the arrow next to the Open button, right-click in a list, drag & drop):
  Add Files (Ctrl+O), Add Folder (Ctrl+Shift+O, recursive, natural sort), Add URL (Ctrl+U),
  Import Playlist as New (Ctrl+I; one new playlist per chosen file) and Import Playlist into Current.
  Supports M3U / M3U8 / PLS, relative paths are resolved against the playlist file.
* **Playlist manager** (View > Playlist manager, Ctrl+Y; shown by default, dockable): lists all playlists with
  their entry counts (`shown / total` while a filter is active); the playing one is marked.
  Click = show it, double-click = play it, right-click = rename / close / new / import.
* **Multiple playlists** as tabs: `+` button / Ctrl+T new, Ctrl+W close, F2 or double-click a tab to rename,
  drag tabs to reorder, right-click a tab or an item for more (e.g. *Copy selected to playlist*).
  File > Import/Export playlist (M3U). All playlists, names, order and the active tab are restored on start.
  The tab of the playlist that is currently playing shows a play icon; you can browse other tabs meanwhile.
* **Per-channel level meter** on the left: peak bar, RMS strip, peak-hold; scale -90 dB ... +3 dB.
  The bar count follows the stream (mono, stereo, surround).
* **Spectrum analyser**, visible by default (View > Spectrum analyser to hide): 56 log-spaced bars,
  ~30 Hz - 20 kHz, peak caps.
* **Playlist filter**, one per playlist (Ctrl+F): words are AND-matched, case-insensitive, against title and path.
  While a filter is active the list shows only matches and Next / Previous / auto-advance / Repeat stay inside
  the filtered set of the playing playlist. Enter plays the first match, Esc clears the filter.
* Drag & drop files/folders (added to the visible playlist), `sonora file-or-dir ...` on the command line.


## What's new in 0.3.1

* **Sound service** (File > Preferences > *Audio output*): Automatic, PipeWire, PulseAudio, ALSA, JACK, OSS ...
  (only installed ones are listed), output device per service, test tone. *Audio advanced*: buffer / latency time
  and a custom GStreamer sink (e.g. `alsasink device=hw:1,0`). Changing the output while playing keeps the position.
* **Spectrum analyser**: default **0 - 24 kHz**, **112 bars** (2x) and **2048 FFT bands** (2x precision); frequency range,
  linear/log axis, bar count, dB range and FFT size are configurable (Preferences > *Spectrum*).
* **LED level meters**: narrow LED ladder (green, yellow, orange, red above 0 dB, peak-hold LED); range, width per
  channel, dB per LED and the scale are configurable (Preferences > *Level meters*).
* **Playlist columns**: title, artist, album, year, time, file type; right-click the header to show/hide columns,
  click it to sort (asc / desc / original). The filter also matches artist, album and year.
* **Metadata tooltip** when the mouse rests on a track: artist, album artist, album, year, track/disc, genre, composer,
  length, format/codec, bitrate, sample rate, channels/bit depth, file size, path, comment. Tags are read in a
  background thread (GstDiscoverer) and cached on disk, so the next start is instant.
* **Time**: elapsed / total; click the time (or View > Show remaining time) for the remaining time. Playlist manager
  also shows each playlist's total length.
* **View > Dark mode**.
* **Keys**: **Space** play/pause (Audacious-like: Z previous, X play, C pause, V stop, B next, Ctrl+Left/Right seek),
  the keyboard's **Play/Pause, Stop, Next, Previous** keys work while the window is focused, and - through MPRIS2 over
  D-Bus - also system-wide when the window is in the background (GNOME, KDE, ...). Typing in the filter box is not
  affected.

## Build

    sudo apt install build-essential debhelper cmake pkgconf qt6-base-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    cmake -S . -B build && cmake --build build -j && ./build/sonora

## Debian package (Debian 12 "bookworm" / 13 "trixie")

    ./build-deb.sh            # -> ../sonora_0.3.1_<arch>.deb

No architecture-specific code: run the same command on an arm64 or amd64 machine / chroot / container
(`sbuild`, `pbuilder`, or Docker `arm64v8/debian:12` with QEMU). Cross-compiling with
`dpkg-buildpackage -aarm64` additionally needs the Qt 6 host tools (`QT_HOST_PATH`); native builds are simpler.

Runtime: the `level` and `spectrum` elements are in `gstreamer1.0-plugins-good`; codecs come from
`-good`, `-ugly`, `-bad`, `-libav`. Display range of the analyser is set by `kFloorDb` / `kCeilDb`
in `src/SpectrumAnalyzer.h`.
---
# Sonora

Audacious-style audio player (Qt 6 + GStreamer, C++17).

* **Transport buttons** like Audacious: Previous, Play, Pause, Stop, Next, Open, plus Repeat and Shuffle toggles.
  Icons are drawn by the program itself (palette colour, framed buttons), so they are visible with any theme.
  Play on a playing track restarts it; Previous/Next/Shuffle/Repeat stay inside the filtered playlist.
* **Adding / importing** (File menu, the arrow next to the Open button, right-click in a list, drag & drop):
  Add Files (Ctrl+O), Add Folder (Ctrl+Shift+O, recursive, natural sort), Add URL (Ctrl+U),
  Import Playlist as New (Ctrl+I; one new playlist per chosen file) and Import Playlist into Current.
  Supports M3U / M3U8 / PLS, relative paths are resolved against the playlist file.
* **Playlist manager** (View > Playlist manager, Ctrl+Y; shown by default, dockable): lists all playlists with
  their entry counts (`shown / total` while a filter is active); the playing one is marked.
  Click = show it, double-click = play it, right-click = rename / close / new / import.
* **Multiple playlists** as tabs: `+` button / Ctrl+T new, Ctrl+W close, F2 or double-click a tab to rename,
  drag tabs to reorder, right-click a tab or an item for more (e.g. *Copy selected to playlist*).
  File > Import/Export playlist (M3U). All playlists, names, order and the active tab are restored on start.
  The tab of the playlist that is currently playing shows a play icon; you can browse other tabs meanwhile.
* **Per-channel level meter** on the left: peak bar, RMS strip, peak-hold; scale -90 dB ... +3 dB.
  The bar count follows the stream (mono, stereo, surround).
* **Spectrum analyser**, visible by default (View > Spectrum analyser to hide): 56 log-spaced bars,
  ~30 Hz - 20 kHz, peak caps.
* **Playlist filter**, one per playlist (Ctrl+F): words are AND-matched, case-insensitive, against title and path.
  While a filter is active the list shows only matches and Next / Previous / auto-advance / Repeat stay inside
  the filtered set of the playing playlist. Enter plays the first match, Esc clears the filter.
* Drag & drop files/folders (added to the visible playlist), `sonora file-or-dir ...` on the command line.

## Build

    sudo apt install build-essential debhelper cmake pkgconf qt6-base-dev libgstreamer1.0-dev
    cmake -S . -B build && cmake --build build -j && ./build/sonora

## Debian package (Debian 12 "bookworm" / 13 "trixie")

    ./build-deb.sh            # -> ../sonora_0.2.2_<arch>.deb

No architecture-specific code: run the same command on an arm64 or amd64 machine / chroot / container
(`sbuild`, `pbuilder`, or Docker `arm64v8/debian:12` with QEMU). Cross-compiling with
`dpkg-buildpackage -aarm64` additionally needs the Qt 6 host tools (`QT_HOST_PATH`); native builds are simpler.

Runtime: the `level` and `spectrum` elements are in `gstreamer1.0-plugins-good`; codecs come from
`-good`, `-ugly`, `-bad`, `-libav`. Display range of the analyser is set by `kFloorDb` / `kCeilDb`
in `src/SpectrumAnalyzer.h`.

---
# Sonora

Audacious-style audio player (Qt 6 + GStreamer, C++17).

* **Per-channel level meter** on the left: peak bar, RMS strip, peak-hold; scale -90 dB … +3 dB.
  Works for mono, stereo and multichannel files (the bar count follows the stream).
* **Playlist filter** (Ctrl+F): words are AND-matched, case-insensitive, against title and full path.
  While a filter is active the list shows only matches and Next / Previous / auto-advance / Repeat
  stay inside the filtered set. If the playing item is not in the filter, Next starts at the first
  match. Enter in the filter box plays the first match, Esc clears the filter.
* Drag & drop files/folders, `sonora file-or-dir ...` on the command line, playlist restored on start.

## Build

    sudo apt install build-essential debhelper cmake pkgconf qt6-base-dev libgstreamer1.0-dev
    cmake -S . -B build && cmake --build build -j && ./build/sonora

## Debian package (Debian 12 "bookworm" / 13 "trixie")

    ./build-deb.sh            # -> ../sonora_0.1.0_<arch>.deb

The code has no architecture-specific parts, so run the same command on an arm64 or amd64
machine / chroot / container (e.g. `sbuild`, `pbuilder`, or Docker `arm64v8/debian:12` with QEMU).
Cross-compiling with `dpkg-buildpackage -aarm64` additionally needs Qt 6 host tools set up
(`QT_HOST_PATH`) - building natively per architecture is the simplest route.

Runtime codecs come from GStreamer plugins (`-good`, recommended `-ugly`, `-bad`, `-libav`).
