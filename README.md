# Qddbconv Audio Converter v1.2.0

**Qddbconv** is a standalone, multi-threaded Qt6 audio converter for Linux/KDE inspired by the user experience and encoder preset architecture of **DeaDBeeF**, **deadbeef-qt5**, and **Qmmp**.

## Features

- **Native Qt6 / KDE HIG Interface:**
  - Single-window workflow with Converter Settings on the left and a live **Output File Preview** table on the right.
  - Full Menubar with **Dolphin-style Hamburger Menu toggle (`Ctrl+M`)** and customizable **Toolbar Text Position** (*Icons Only*, *Text Only*, *Text Alongside Icons*, *Text Under Icons*).
  - Drag & Drop support for audio files, folders, and `.cue` sheets directly from Dolphin.
- **100% Compatible with DeaDBeeF Encoder Presets:**
  - Ships with **28 built-in presets** covering DeaDBeeF, Qmmp, and FFmpeg encoders (*FLAC*, *MP3 Lame / FFmpeg*, *Opus 96–320 kbps*, *Ogg Vorbis*, *Ogg FLAC*, *AAC*, *ALAC*, *WavPack*, *MusePack*, *TTA*, *AC3*, *AIFF*, *RIFF WAV*, *Standard MIDI (.mid from .mmf)*, *File Copy*).
  - Reads and saves custom presets in `$XDG_DATA_HOME/qddbconv/presets/*.txt` (`~/.local/share/qddbconv/presets/*.txt` on Linux; `%LOCALAPPDATA%\qddbconv\presets\*.txt` or `<app_dir>\presets\*.txt` on Windows; customizable in *Settings -> Configure qddbconv...*), using the plain-text key-value format compatible with DeaDBeeF's converter presets.
- **Broad Format & CUE Sheet Support:**
  - Built-in CUE sheet parser that splits single-file album images (`FLAC`, `APE`, `WV`, `WAV`, `TAK`, `TTA`) into individual tracks with accurate `INDEX 01` timestamps and per-track metadata.
  - Decodes all lossless, lossy, surround, tracker, and chiptune/console formats supported by `ffmpeg` / `ffprobe`.
- **DSP & Sample Format Control:**
  - Output bit depths: *Keep source*, *8-bit*, *16-bit*, *24-bit*, *32-bit int*, *32-bit IEEE float*.
  - Resampler (DSP): *8000 Hz* to *384000 Hz* (plus any custom user-entered rate in Hz) and channel mixing (*Keep source*, *Stereo*, *Mono*).
- **Tag Editor & Automatic Cover Art Embedding:**
  - Built-in **Track Metadata & Properties** dialog (`Ctrl+I` or double-click) supporting single-track and batch tag editing, technical audio properties inspection, and cover art preview.
  - Automatically embeds cover art from source file tags or album folder images (`cover.jpg`, `folder.jpg`, `front.png`, etc.) into `MP3`, `FLAC`, `M4A/MP4`, `Ogg Vorbis`, and `Opus` (RFC 7845 `METADATA_BLOCK_PICTURE`), with optional downscaling of high-resolution scans.

---

## Supported Audio Formats

### Input Formats (Convert FROM)

Because **Qddbconv** uses `ffmpeg` / `ffprobe` for decoding plus a native `.cue` parser, it supports converting **from**:

- **Lossless Audio:**
  - `FLAC` (`.flac`, `.oga`), `Monkey's Audio` (`.ape`), `WavPack` (`.wv`), `True Audio` (`.tta`), `TAK` (`.tak`), `Apple Lossless / ALAC` (`.m4a`), `Shorten` (`.shn`), `OptimFROG` (`.ofr`, `.ofs`), `Windows Media Audio Lossless` (`.wma`)
- **Uncompressed PCM & DSD:**
  - `WAV / RIFF WAVE` (`.wav`, `.wave`), `Wave64` (`.w64`), `AIFF` (`.aiff`, `.aif`, `.aifc`), `AU / SND` (`.au`, `.snd`), `Apple Core Audio` (`.caf`), `Creative Voice` (`.voc`), `DSD` (`.dsf`, `.dff`)
- **Lossy & Surround Audio:**
  - `MP3` (`.mp3`), `MP2` (`.mp2`, `.mpa`), `Ogg Vorbis` (`.ogg`, `.oga`), `Opus` (`.opus`), `AAC / M4A / Audiobook` (`.m4a`, `.m4b`, `.aac`, `.mp4`, `.3gp`), `MusePack` (`.mpc`, `.mp+`, `.mpp`), `WMA` (`.wma`), `Dolby Digital / AC3 / E-AC3` (`.ac3`, `.eac3`), `DTS` (`.dts`), `Dolby TrueHD / MLP` (`.thd`, `.mlp`), `Speex` (`.spx`), `AMR / GSM` (`.amr`, `.awb`, `.gsm`), `RealAudio` (`.ra`, `.rm`), `Quite OK Audio` (`.qoa`), `Yamaha SMAF ADPCM` (`.mmf`), `Matroska / WebM Audio` (`.mka`, `.webm`)
- **CUE Sheets:**
  - `.cue` (with automatic splitting of multi-track `.flac`, `.ape`, `.wv`, `.wav`, `.tta`, `.tak`, `.mp3`, `.m4a` images)
- **Ringtones & Standard MIDI:**
  - `Yamaha SMAF` (`.mmf` — MA-1/MA-2/MA-3/MA-5 score + hybrid 4-bit Yamaha ADPCM voice/stream tracks)
  - `Standard MIDI` (`.mid`, `.midi`, `.kar`, `.rmi` — synthesized to `.wav`, `.mp3`, `.opus`, `.flac`, `.ogg`, etc. via `timidity` or `fluidsynth`)
- **Tracker Modules & Chiptune / Console Audio** *(when FFmpeg is built with `libopenmpt` / `libgme`)*:
  - Tracker: `.mod`, `.xm`, `.it`, `.s3m`, `.mptm`, `.stm`, `.med`, `.okt`, `.669`, `.far`, `.ult`, `.mtm`, `.amf`
  - Chiptune: `.spc` (SNES), `.vgm` / `.vgz` (Sega/Yamaha), `.nsf` / `.nsfe` (NES), `.gbs` (Game Boy), `.hes` (PC Engine), `.ay` (ZX Spectrum / Amstrad), `.sap` (Atari XL/XE), `.sid` (C64), `.kss` (MSX)

*(Note: **Qddbconv** includes a built-in native C++ Yamaha SMAF `.mmf` parser supporting MA-1/MA-2 HandyPhone Standard `0x00`, MA-3/MA-5 Mobile Standard Huffman `0x01`, and MA-3/MA-5 Mobile Standard Uncompressed `0x02`, plus embedded 4-bit Yamaha ADPCM `Mwa`/`Awa` streams. You can convert `.mmf` ringtones directly into **`.mid` Standard MIDI Files** using the built-in `Standard MIDI (from Yamaha SMAF .mmf)` preset, or into standard audio formats like `.wav`, `.mp3`, `.flac`, and `.opus`.)*

### Output Formats (Convert TO)

Out of the box via the **28 built-in presets** (plus any custom CLI encoder preset you add):

- **Lossless:**
  - `FLAC` (`.flac` — levels 5 & 8 via `flac`)
  - `Ogg FLAC` (`.ogg` — via `flac --ogg`)
  - `Apple Lossless / ALAC` (`.m4a` — via `ffmpeg -acodec alac`)
  - `WavPack` (`.wv` — via `wavpack`)
  - `True Audio` (`.tta` — via `ttaenc`)
- **Uncompressed PCM & MIDI:**
  - `MS RIFF WAV` (`.wav` — 8/16/24/32-bit int or 32-bit float via `ffmpeg`)
  - `Apple AIFF` (`.aiff` — via `ffmpeg`)
  - `Standard MIDI` (`.mid` — native C++ conversion from `.mmf` Yamaha SMAF ringtones)
- **Lossy:**
  - `MP3` (`.mp3` — CBR 128, VBR 192 / `-V2`, CBR 320 via `lame` or `ffmpeg`)
  - `Opus` (`.opus` — 96, 128, 160, 192, 256, 320 kbps via `opusenc` or `ffmpeg`)
  - `Ogg Vorbis` (`.ogg` — VBR 160 / `-q 5`, 192 / `-q 6`, 256 / `-q 8`, 320 / `-q 9` via `oggenc`)
  - `AAC / M4A` (`.m4a` — 256 kbps via `ffmpeg` or VBR 192 via `faac`)
  - `MusePack` (`.mpc` — via `mpcenc`)
  - `AC3 / Dolby Digital` (`.ac3` — 384 kbps via `ffmpeg`)
- **Any other format supported by FFmpeg or external CLI encoders** (e.g. `.w64`, `.caf`, `.eac3`, `.wma`, `.amr`, `.mmf`) by adding a custom preset in **Presets... (`Ctrl+P`)**.

---

## Building

See **[INSTALL.md](INSTALL.md)** for full dependency, build, `.deb`, and `.AppImage` instructions.

```bash
# Option 1: CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build

# Option 2: qmake6
qmake6 && make -j$(nproc)
```

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+O` | Add Files |
| `Ctrl+Shift+O` | Add Folder |
| `Ctrl+U` | Add CUE Sheet |
| `Ctrl+I` / Double-Click | Track Metadata & Properties |
| `Ctrl+Shift+R` | Reload Metadata from Disk |
| `Ctrl+Shift+F` | View Track in Folder |
| `Del` | Remove Selected Tracks |
| `Ctrl+L` | Clear Track Queue |
| `Ctrl+P` | Encoder Presets Manager |
| `Ctrl+Shift+,` | Configure Qddbconv |
| `Ctrl+M` | Toggle Menubar / Hamburger Menu |
| `F5` / `Ctrl+R` | Start Conversion |
| `Esc` | Cancel Active Conversion |
| `Ctrl+Q` | Quit |

## Copyright & License

Copyright (C) 2026 **goberghen** & **Arena.ai Agent**

This program is free software; you can redistribute it and/or modify it under the terms of the **GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version** (GPL-2.0-or-later).
