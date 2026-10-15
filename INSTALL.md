# Building and Installing Qddbconv

This document explains how to build and install **Qddbconv** from source, as well as how to generate a Debian/Ubuntu `.deb` package or a portable `.AppImage` bundle.

---

## 1. Prerequisites

### Build Dependencies
- A C++17 compiler (`g++` or `clang++`)
- `cmake` (>= 3.16) or `qmake6`
- Qt 6 Base development headers (`Qt6Widgets`, `Qt6Concurrent`)

### Runtime Dependencies
- `ffmpeg` (provides `ffmpeg` and `ffprobe` for decoding, resampling, and metadata tagging)
- Optional CLI encoders used by presets: `flac`, `lame`, `opus-tools` (`opusenc`), `vorbis-tools` (`oggenc`), `wavpack`, `musepack-tools`, `faac`

### Installing Dependencies by Distribution

- **Debian / Ubuntu / Linux Mint / KDE neon:**
  ```bash
  sudo apt update
  sudo apt install build-essential cmake qt6-base-dev ffmpeg flac lame opus-tools vorbis-tools wavpack
  ```

- **Arch Linux / Manjaro / EndeavourOS:**
  ```bash
  sudo pacman -S --needed base-devel cmake qt6-base ffmpeg flac lame opus-tools vorbis-tools wavpack
  ```

- **Fedora:**
  ```bash
  sudo dnf install gcc-c++ cmake qt6-qtbase-devel ffmpeg-free flac lame opus-tools vorbis-tools wavpack
  ```

- **openSUSE Tumbleweed / Leap:**
  ```bash
  sudo zypper install gcc-c++ cmake qt6-base-devel ffmpeg flac lame opus-tools vorbis-tools wavpack
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
- `/usr/share/doc/qddbconv/{README.md,INSTALL.md,LICENSE}`

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
sudo apt install ./qddbconv_1.1.0_amd64.deb
```

---

## 4. Building a Portable `.AppImage`

You can generate a self-contained `Qddbconv-1.1.0-x86_64.AppImage` bundle using the included `build-appimage.sh` script (requires `squashfs-tools` / `mksquashfs`):

```bash
./build-appimage.sh
```

Make the generated `.AppImage` executable and run it directly:
```bash
chmod +x Qddbconv-1.1.0-x86_64.AppImage
./Qddbconv-1.1.0-x86_64.AppImage
```
*(Note: If `libfuse2` is not installed on the target system, any AppImage can also be launched with `--appimage-extract-and-run`).*
