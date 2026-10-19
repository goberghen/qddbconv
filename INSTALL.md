# Building, Installing, and Configuring Dependencies for Qddbconv

This document explains:
1. How to install runtime codecs, **FluidSynth**, **TiMidity++**, and build dependencies on **Linux** (Debian/Ubuntu/Mint, Fedora & derivatives, openSUSE, Arch, Void) and **Windows**.
2. Where to download and how to configure popular **General MIDI `.sf2` SoundFonts** (**FluidR3_GM** and **Roland SC-55 mk2**).
3. How to build **Qddbconv** from source or generate `.deb`, `.AppImage`, and `win64.zip` packages.

---

## 1. Runtime & Build Dependencies Overview

### Build Dependencies (when compiling from source)
- A C++17 compiler (`g++` or `clang++`) and 32-bit C multilib compiler (`gcc-multilib` on Linux x86_64 or `i686-w64-mingw32-gcc` for the optional 32-bit `qddbconv-smaf32` helper)
- `cmake` (>= 3.16) or `qmake6`
- Qt 6 Base development headers (`qt6-base-dev`: `Qt6Widgets`, `Qt6Gui`, `Qt6Core`, `Qt6Concurrent`)

### Runtime Dependencies
- **Qt 6 Core & Image Format Plugins:**
  - `libqt6widgets6`, `libqt6gui6`, `libqt6core6t64` (or `libqt6core6`):
    - **PNG** (`libpng16`) and **BMP** support are compiled directly into `Qt6Gui` (`libQt6Gui.so.6` on Linux, `Qt6Gui.dll` on Windows).
    - **JPEG** (`libjpeg-turbo`), **GIF**, and **ICO** plugins (`libqjpeg`, `libqgif`, `libqico`) are included in `libqt6gui6`.
  - `qt6-svg-plugins` (`libqsvg`, `libqsvgicon`) — required for SVG application & toolbar icons.
  - `qt6-image-formats-plugins` (`libqwebp`, `libqtiff`, `libqtga`, `libqwbmp`, `libqicns`) — required for WebP, TIFF, TGA, WBMP, and ICNS album cover art (with automatic FFmpeg-to-PNG fallback if a plugin is absent).
- **Core Audio Probing, Decoding, Resampling & Tagging Engine:**
  - `ffmpeg` (provides `ffmpeg` and `ffprobe`)
- **Recommended Standalone CLI Encoders (with automatic FFmpeg fallback if absent):**
  - `flac` (`flac`), `lame` (`lame`), `opus-tools` (`opusenc`), `vorbis-tools` (`oggenc`), `wavpack` (`wavpack`)
- **MIDI & Yamaha SMAF (`.mid` / `.mmf`) Synthesizers:**
  - `fluidsynth` (**FluidSynth**) — real-time SoundFont (`.sf2` / `.sf3`) synthesizer (preferred default in `Auto` mode when a `.sf2` SoundFont is present)
  - `timidity` (**TiMidity++**) — SoundFont (`.sf2`) and GUS patch (`timidity.cfg` / `freepats`) synthesizer (automatic fallback or selectable primary backend)
- **Optional Tracker / Chiptune Helpers & Additional Encoders:**
  - `openmpt123` or `xmp` (fallback decoder for tracker modules `.mod`, `.xm`, `.it`, `.s3m`, `.mptm` when FFmpeg lacks `libopenmpt`)
  - `gme2wav` / `libgme` (fallback decoder for console chiptunes `.spc`, `.vgm`, `.nsf`, `.gbs`, `.hes`, `.ay`, `.sap`, `.kss`)
  - `faac`, `fdkaac`, `twolame`, `musepack-tools` (`mpcenc`), `ttaenc`

---

## 2. Installing Dependencies on Linux Distributions

### Debian / Ubuntu / Linux Mint / Pop!_OS / KDE neon
```bash
sudo apt update

# Runtime dependencies (FFmpeg, CLI codecs, FluidSynth, TiMidity++, FluidR3 GM SoundFont, Qt6 plugins):
sudo apt install ffmpeg flac lame opus-tools vorbis-tools wavpack \
  fluidsynth fluid-soundfont-gm fluid-soundfont-gs \
  timidity freepats openmpt123 xmp \
  qt6-svg-plugins qt6-image-formats-plugins

# Optional extra encoders:
sudo apt install faac libfdk-aac-dev twolame musepack-tools

# Build dependencies (for compiling qddbconv and qddbconv-smaf32 from source):
sudo apt install build-essential gcc-multilib g++-multilib cmake \
  qt6-base-dev qt6-base-dev-tools
```

### Fedora & Derivatives (RHEL / AlmaLinux / Rocky Linux / Nobara / Ultramarine)
*(Note: Enable [RPM Fusion](https://rpmfusion.org/Configuration) if you want the full `ffmpeg` build and `lame`/`faac` packages, or use Fedora's default `ffmpeg-free` + `lame`.)*
```bash
# Optional: Enable RPM Fusion Free & Nonfree for full codec support
sudo dnf install \
  https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
  https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

# Runtime dependencies (codecs, FluidSynth, TiMidity++, FluidR3 GM SoundFont, Qt6 plugins):
sudo dnf install ffmpeg-free flac lame opus-tools vorbis-tools wavpack \
  fluidsynth fluid-soundfont-gm fluid-soundfont-gs \
  timidity++ fluid-soundfont-lite-patches openmpt123 xmp \
  qt6-qtsvg qt6-qtimageformats

# Build dependencies (including 32-bit glibc-devel.i686 for qddbconv-smaf32):
sudo dnf install gcc-c++ glibc-devel.i686 libgcc.i686 cmake qt6-qtbase-devel
```

### openSUSE (Tumbleweed & Leap)
*(Note: For unrestricted multimedia codecs on openSUSE, you can optionally enable the [Packman repository](https://en.opensuse.org/Additional_package_repositories#Packman) via `sudo zypper ar -cfp 90 https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/ packman && sudo zypper dup --from packman --allow-vendor-change`.)*
```bash
# Runtime dependencies (codecs, FluidSynth, TiMidity++, FluidR3 GM SoundFont, Qt6 plugins):
sudo zypper install ffmpeg flac lame opus-tools vorbis-tools wavpack \
  fluidsynth fluid-soundfont-gm fluid-soundfont-gs \
  timidity openmpt123 xmp \
  libQt6Svg6 qt6-imageformats

# Build dependencies (including 32-bit gcc-32bit for qddbconv-smaf32):
sudo zypper install gcc-c++ gcc-32bit glibc-devel-32bit cmake qt6-base-devel
```

### Arch Linux / Manjaro / EndeavourOS / Garuda
```bash
# Runtime dependencies (codecs, FluidSynth, TiMidity++, FluidR3 GM SoundFont, Qt6 plugins):
sudo pacman -S --needed ffmpeg flac lame opus-tools vorbis-tools wavpack \
  fluidsynth soundfont-fluid \
  timidity++ freepats-general-midi libopenmpt xmp \
  qt6-base qt6-svg qt6-imageformats

# Optional extra encoders:
sudo pacman -S --needed faac libfdk-aac twolame musepack-tools

# Build dependencies (enable [multilib] in /etc/pacman.conf for lib32-glibc):
sudo pacman -S --needed base-devel lib32-glibc lib32-gcc-libs cmake
```

### Void Linux
```bash
# Enable multilib repository if building 32-bit qddbconv-smaf32 helper:
sudo xbps-install -Sy void-repo-multilib

# Runtime dependencies (codecs, FluidSynth, TiMidity++, FluidR3 GM SoundFont, Qt6 plugins):
sudo xbps-install -S ffmpeg flac lame opus-tools vorbis-tools wavpack \
  fluidsynth fluid-soundfont-gm \
  timidity libopenmpt xmp \
  qt6-core qt6-widgets qt6-gui qt6-svg qt6-imageformats

# Build dependencies:
sudo xbps-install -S base-devel gcc-multilib cmake qt6-base-devel
```

---

## 3. Installing Codecs, FluidSynth, and TiMidity++ on Windows

### What Is Already Bundled in `Qddbconv-<version>-win64.zip`
The portable 64-bit Windows archive (`Qddbconv-<version>-win64.zip`) already bundles:
- `Qt6` runtime (`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`) + `imageformats` (`qjpeg`, `qwebp`, `qtiff`, `qgif`, `qico`, `qtga`, `qwbmp`, `qicns`, `qsvg`) + `iconengines` + `platforms` + `styles`
- `ffmpeg.exe` & `ffprobe.exe` (with `avcodec`, `avdevice`, `avfilter`, `avformat`, `avutil`, `swresample`, `swscale`)
- Standalone CLI encoders: `flac.exe`, `lame.exe`, `oggenc.exe`, `opusenc.exe`, `wavpack.exe`
- Tracker & chiptune helpers: `openmpt123.exe`, `gme2wav.exe`
- Native 32-bit SMAF DLL helper: `qddbconv-smaf32.exe`

You do **not** need to install FFmpeg, FLAC, LAME, Opus, Ogg Vorbis, WavPack, or OpenMPT separately when using the portable Windows `.zip` build.

### Installing FluidSynth on Windows (for `.mid` and WaveTable `.mmf` synthesis)
1. **Official GitHub Releases (Recommended):**
   - Download the latest `fluidsynth-*-win10-x64.zip` from the official FluidSynth releases page:
     - **https://github.com/FluidSynth/fluidsynth/releases**
   - Extract the archive (for example to `C:\fluidsynth` so `C:\fluidsynth\bin\fluidsynth.exe` exists, or copy `fluidsynth.exe` and its `.dll` files into your `Qddbconv` folder or `Qddbconv\tools\`).
2. **Or install via Package Manager (`winget` / `scoop` / `MSYS2`):**
   ```powershell
   # Using Scoop:
   scoop install fluidsynth

   # Using MSYS2 (ucrt64 / mingw64):
   pacman -S mingw-w64-x86_64-fluidsynth
   ```
3. **Qddbconv** automatically detects `fluidsynth.exe` in:
   - `<qddbconv_folder>\fluidsynth.exe` or `<qddbconv_folder>\tools\fluidsynth.exe`
   - `C:\fluidsynth\bin\fluidsynth.exe` or `C:\Program Files\FluidSynth\bin\fluidsynth.exe`
   - Any directory in `PATH`, or a custom path selected in **Settings -> Configure qddbconv... -> FluidSynth binary**.

### Installing TiMidity++ on Windows (optional alternative/fallback MIDI synthesizer)
1. **Official / Trusted Windows Builds:**
   - **SourceForge (TiMidity++ official project page & Win32 binaries):**
     - **https://sourceforge.net/projects/timidity/files/**
   - **OSDN / TiMidity++ CVS Windows builds (twsynth / timidity.exe):**
     - **https://timidity.sourceforge.net/**
2. Extract `timidity.exe` (and its `timidity.cfg` if using GUS patches) to `C:\timidity\timidity.exe`, `C:\Program Files\TiMidity++\timidity.exe`, or `<qddbconv_folder>\tools\timidity.exe`, or select its path in **Settings -> Configure qddbconv... -> TiMidity++ binary**.
   *(Tip: When you select any `.sf2` SoundFont in **Settings -> Configure qddbconv... -> SoundFont (.sf2) file**, **Qddbconv** automatically generates a temporary TiMidity++ config for `timidity.exe` that loads your `.sf2` SoundFont directly—no manual `timidity.cfg` editing needed!)*

### Optional Additional Windows CLI Encoders (if installing standalone)
If you ever want to update or install standalone Windows CLI codecs manually:
- **FFmpeg (`ffmpeg.exe`, `ffprobe.exe`):** https://www.gyan.dev/ffmpeg/builds/ or https://github.com/BtbN/FFmpeg-Builds/releases
- **FLAC (`flac.exe`):** https://xiph.org/flac/download.html or https://github.com/xiph/flac/releases
- **LAME (`lame.exe`) & Ogg Vorbis (`oggenc2.exe`) & Opus (`opusenc.exe`):** https://www.rarewares.org/
- **WavPack (`wavpack.exe`):** https://www.wavpack.com/downloads.html or https://github.com/dbry/WavPack/releases
- **OpenMPT (`openmpt123.exe`):** https://lib.openmpt.org/libopenmpt/download/

---

## 4. General MIDI SoundFonts (`.sf2`) — Trusted Download Sources

To synthesize Standard MIDI (`.mid`, `.kar`, `.rmi`) files or General MIDI WaveTable tracks inside Yamaha SMAF (`.mmf`) files with **FluidSynth** (or **TiMidity++**), you need at least one General MIDI `.sf2` SoundFont file.

### Recommended SoundFont 1: `FluidR3_GM.sf2` (Fluid Release 3 General MIDI — ~141 MB)
Created by Frank Wen, **FluidR3_GM** is the standard high-quality open-source General MIDI SoundFont shipped by major Linux distributions.
- **On Linux:** Install the `fluid-soundfont-gm` (Debian/Ubuntu/Fedora/openSUSE/Void) or `soundfont-fluid` (Arch) package as shown in Section 2. It installs to `/usr/share/sounds/sf2/FluidR3_GM.sf2` (or `/usr/share/soundfonts/FluidR3_GM.sf2`) and is auto-detected by **Qddbconv**.
- **Direct Download Links (for Windows or manual installation):**
  - **Musical Artifacts (`FluidR3_GM.sf2`):**
    - https://musical-artifacts.com/artifacts/738
  - **KeyMusician / SourceForge Mirror (`FluidR3_GM.sf2`):**
    - https://sourceforge.net/projects/pianobooster/files/pianobooster/0.6.4/FluidR3_GM.sf2/download
  - **Debian Upstream Archive (`fluid-soundfont_3.1.orig.tar.gz`):**
    - https://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont_3.1.orig.tar.gz

### Recommended SoundFont 2: `Roland SC-55 mk2` (`SC-55.sf2` — General MIDI / GS Classic Sound Canvas)
The **Roland Sound Canvas SC-55 / SC-55mkII** was the reference standard used when composing classic 1990s/2000s PC game MIDI soundtracks and General MIDI / WaveTable ringtones.
- **Trusted Download Sites for `SC-55` / `SC-55 mk2` SoundFonts (`.sf2`):**
  - **Musical Artifacts — Roland SC-55 / SC-55mkII SoundFont collection:**
    - https://musical-artifacts.com/artifacts?formats=sf2&q=sc-55
  - **Patch93's Roland SC-55 v2.2 / EmperorGrieferus SC-55 SoundFont (Internet Archive & Musical Artifacts):**
    - https://musical-artifacts.com/artifacts/1330 *(Roland SC-55 Presets)*
    - https://archive.org/details/SC55EmperorGrieferus *(EmperorGrieferus Roland SC-55 SoundFont)*
    - https://archive.org/details/patch93s-sc-55-soundfont-v2.2 *(Patch93's Roland SC-55 mkII SoundFont)*

### Other Trusted Sites for General MIDI `.sf2` SoundFonts
- **Musical Artifacts (curated open repository of `.sf2` / `.sf3` SoundFonts):**
  - https://musical-artifacts.com/artifacts?formats=sf2&tags=gm
- **GeneralUser GS by S. Christian Collins (~30 MB lightweight balanced GM/GS SoundFont):**
  - https://schristiancollins.com/generaluser.php
- ** MuseScore SoundFonts (`MuseScore_General.sf2` / `.sf3`):**
  - https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/

### How to Configure a `.sf2` SoundFont in Qddbconv
1. **Via the GUI (Recommended):**
   - Open **Settings -> Configure qddbconv...** (`Ctrl+Shift+,`).
   - Under **MIDI & SMAF Synthesis**, click **Browse...** next to **SoundFont (.sf2) file:** and select your downloaded `.sf2` file (e.g. `FluidR3_GM.sf2` or `SC-55.sf2`).
   - Verify that **Active MIDI backend:** shows your synthesizer and SoundFont file as ready, then click **OK**.
2. **Or via Automatic Folder Discovery (Zero-Config Portable Mode):**
   - Place any `.sf2` file inside `<qddbconv_folder>/soundfonts/` (or `C:\soundfonts\` on Windows, or `/usr/share/sounds/sf2/` on Linux). When **SoundFont (.sf2) file:** is left set to `Default`, **Qddbconv** automatically discovers and uses it.

---

## 5. Building and Installing Qddbconv from Source

### Option A: Using CMake (Recommended)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
```

This installs:
- `/usr/bin/qddbconv`
- `/usr/bin/qddbconv-smaf32` *(if 32-bit `gcc-multilib` support is available)*
- `/usr/share/applications/qddbconv.desktop`
- `/usr/share/icons/hicolor/scalable/apps/qddbconv.svg`
- `/usr/share/man/man1/qddbconv.1.gz`
- `/usr/share/doc/qddbconv/{README.md,INSTALL.md,CHANGELOG.md,LICENSE}`

### Option B: Using qmake6
```bash
qmake6 qddbconv.pro
make -j$(nproc)
sudo cp qddbconv /usr/local/bin/
```

---

## 6. Building Release Packages (`.deb`, `.AppImage`, `win64.zip`)

### Building a `.deb` Package (Debian / Ubuntu / Mint / KDE neon)
```bash
./build-deb.sh
sudo apt install ./qddbconv_*_amd64.deb
```

### Building a Portable `.AppImage`
Ensure AppImage packaging tools and Qt6 style/image/portal plugins are installed (on Debian/Ubuntu):
```bash
sudo apt install squashfs-tools patchelf kde-style-breeze \
  qt6-xdgdesktopportal-platformtheme qt6-svg-plugins \
  qt6-image-formats-plugins qt6-wayland
./build-appimage.sh
chmod +x Qddbconv-*-x86_64.AppImage
./Qddbconv-*-x86_64.AppImage
```

### Building a Portable 64-bit Windows Package (`Qddbconv-<version>-win64.zip`)
```bash
sudo apt install g++-mingw-w64-x86-64-posix g++-mingw-w64-i686-posix \
  binutils-mingw-w64-x86-64 p7zip-full zip
pip install aqtinstall
aqt install-qt windows desktop 6.8.2 win64_mingw -O /opt/qt6-win -m qtimageformats
./build-win64.sh
```
