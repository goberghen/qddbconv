# Building and Installing Qddbconv

This document explains how to build and install **Qddbconv** from source, as well as how to generate a Debian/Ubuntu `.deb` package or a portable `.AppImage` bundle.

---

## 1. Prerequisites

### Build Dependencies
- A C++17 compiler (`g++` or `clang++`)
- `cmake` (>= 3.16) or `qmake6`
- Qt 6 Base development headers (`qt6-base-dev`: `Qt6Widgets`, `Qt6Gui`, `Qt6Core`, `Qt6Concurrent`)

### Runtime Dependencies
- **Qt 6 Core & Image Format Plugins:**
  - `libqt6widgets6`, `libqt6gui6`, `libqt6core6t64` (or `libqt6core6`):
    - **PNG** (`libpng16`) and **BMP** support are compiled directly into `Qt6Gui` (`libQt6Gui.so.6` on Linux, `Qt6Gui.dll` on Windows).
    - **JPEG** (`libjpeg-turbo`), **GIF**, and **ICO** plugins (`libqjpeg`, `libqgif`, `libqico`) are included in `libqt6gui6`.
  - `qt6-svg-plugins` (`libqsvg`, `libqsvgicon`) — required for SVG application & toolbar icons.
  - `qt6-image-formats-plugins` (`libqwebp`, `libqtiff`, `libqtga`, `libqwbmp`, `libqicns`) — required for WebP, TIFF, TGA, WBMP, and ICNS album cover art (with automatic FFmpeg-to-PNG fallback if a plugin is absent).
- **Audio Probing, Decoding, Resampling & Tagging Engine:**
  - `ffmpeg` (provides `ffmpeg` and `ffprobe`)
- **Recommended Standalone CLI Encoders (with automatic FFmpeg fallback if absent):**
  - `flac`, `lame`, `opus-tools` (`opusenc`), `vorbis-tools` (`oggenc`), `wavpack`
- **Optional Format Helpers & Additional Encoders:**
  - `timidity` or `fluidsynth` (for Standard MIDI `.mid` and Yamaha SMAF `.mmf` synthesis)
  - `openmpt123` or `xmp` (fallback decoder for tracker modules `.mod`, `.xm`, `.it`, `.s3m` when FFmpeg lacks `libopenmpt`)
  - `faac`, `fdkaac`, `twolame`, `musepack-tools`

### Installing Dependencies by Distribution

- **Debian / Ubuntu / Linux Mint / KDE neon:**
  ```bash
  sudo apt update
  sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools \
    qt6-svg-plugins qt6-image-formats-plugins \
    ffmpeg flac lame opus-tools vorbis-tools wavpack timidity openmpt123
  ```

- **Arch Linux / Manjaro / EndeavourOS:**
  ```bash
  sudo pacman -S --needed base-devel cmake qt6-base qt6-svg qt6-imageformats \
    ffmpeg flac lame opus-tools vorbis-tools wavpack timidity++ libopenmpt
  ```

- **Fedora:**
  ```bash
  sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtsvg qt6-qtimageformats \
    ffmpeg-free flac lame opus-tools vorbis-tools wavpack fluid-soundfont-gm fluidsynth openmpt123
  ```

- **openSUSE Tumbleweed / Leap:**
  ```bash
  sudo zypper install gcc-c++ cmake qt6-base-devel libQt6Svg6 qt6-imageformats \
    ffmpeg flac lame opus-tools vorbis-tools wavpack timidity openmpt123
  ```

---

## 2. Building and Installing from Source

### Option A: Using CMake (Recommended)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
```

This installs:
- `/usr/bin/qddbconv`
- `/usr/share/applications/qddbconv.desktop`
- `/usr/share/icons/hicolor/scalable/apps/qddbconv.svg`
- `/usr/share/doc/qddbconv/{README.md,INSTALL.md,CHANGELOG.md,LICENSE}`

### Option B: Using qmake6
```bash
qmake6 qddbconv.pro
make -j$(nproc)
sudo cp qddbconv /usr/local/bin/
```

---

## 3. Building a `.deb` Package (Debian / Ubuntu / KDE neon)

You can generate an installable `.deb` package using the included `build-deb.sh` script (or `cpack -G DEB`):

```bash
./build-deb.sh
```

Then install the resulting `.deb` package with `apt`:
```bash
sudo apt install ./qddbconv_*_amd64.deb
```

---

## 4. Building a Portable `.AppImage`

You can generate a self-contained `Qddbconv-<version>-x86_64.AppImage` bundle using the included `build-appimage.sh` script.

First ensure the AppImage packaging tools and Qt6 style/image/portal plugins are installed (on Debian/Ubuntu):
```bash
sudo apt install squashfs-tools patchelf kde-style-breeze \
  qt6-xdgdesktopportal-platformtheme qt6-svg-plugins \
  qt6-image-formats-plugins qt6-wayland
```

Then run:
```bash
./build-appimage.sh
```

Make the generated `.AppImage` executable and run it directly:
```bash
chmod +x Qddbconv-*-x86_64.AppImage
./Qddbconv-*-x86_64.AppImage
```
*(Note: If `libfuse2` is not installed on the target system, any AppImage can also be launched with `--appimage-extract-and-run`).*

---

## 5. Building a Portable 64-bit Windows Package (`Qddbconv-<version>-win64.zip`)

You can cross-compile a portable 64-bit Windows distribution from Linux using MinGW-w64 (`g++-mingw-w64-x86-64-posix`, `binutils-mingw-w64-x86-64`) and Qt 6 for Windows (`win64_mingw` + `qtimageformats` via `aqtinstall`):

```bash
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 p7zip-full zip
pip install aqtinstall
aqt install-qt windows desktop 6.8.2 win64_mingw -O /opt/qt6-win -m qtimageformats
./build-win64.sh
```

On Windows:
1. Extract `Qddbconv-<version>-win64.zip` to any folder.
2. Run `qddbconv.exe` — everything works out of the box with zero external dependencies (`Qt6` runtime DLLs with built-in PNG/BMP + `imageformats` plugins for JPEG, WebP, TIFF, GIF, ICO, TGA, WBMP, ICNS, and SVG, plus `ffmpeg.exe`, `ffprobe.exe`, `flac.exe`, `lame.exe`, `oggenc.exe`, `opusenc.exe`, `wavpack.exe`, `openmpt123.exe`, and `gme2wav.exe` are all bundled inside the folder).

