#!/usr/bin/env bash
# Build an installable Debian/Ubuntu .deb package for Qddbconv
# Copyright (C) 2026 goberghen & Arena.ai Agent
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(sed -n 's/^project(qddbconv VERSION \([0-9.]*\).*/\1/p' "$SCRIPT_DIR/CMakeLists.txt")"
VERSION="${VERSION:-1.2.1}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
PKG_NAME="qddbconv_${VERSION}_${ARCH}"
BUILD_DIR="/tmp/qddbconv-deb-build"
STAGE_DIR="/tmp/${PKG_NAME}"

rm -rf "$BUILD_DIR" "$STAGE_DIR"
mkdir -p "$BUILD_DIR" "$STAGE_DIR/DEBIAN"

echo "[1/3] Compiling qddbconv..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j"$(nproc)"
strip --strip-unneeded "$BUILD_DIR/qddbconv"
if [ -f "$BUILD_DIR/qddbconv-smaf32" ]; then
    strip --strip-unneeded "$BUILD_DIR/qddbconv-smaf32" || true
fi

echo "[2/3] Staging package files..."
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"
if [ -f "$STAGE_DIR/usr/share/man/man1/qddbconv.1" ]; then
    gzip -9n -f "$STAGE_DIR/usr/share/man/man1/qddbconv.1"
fi

INSTALLED_KB="$(du -sk "$STAGE_DIR/usr" | awk '{print $1}')"

cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: qddbconv
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_KB}
Depends: libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 11), libqt6core6t64 | libqt6core6, libqt6gui6, libqt6widgets6, libpng16-16t64 | libpng16-16, libjpeg62-turbo | libjpeg8, qt6-svg-plugins, qt6-image-formats-plugins, ffmpeg
Recommends: flac, lame, opus-tools, vorbis-tools, wavpack, qt6-qpa-plugins, qt6-wayland, qt6-xdgdesktopportal-platformtheme
Suggests: timidity | fluidsynth, openmpt123 | xmp, faac, fdkaac, twolame, musepack-tools
Maintainer: goberghen <goberghen@users.noreply.github.com>
Homepage: https://github.com/goberghen/qddbconv
Description: Standalone Qt6 audio converter inspired by DeaDBeeF
 Qddbconv is a multi-threaded Qt6 audio converter for Linux and KDE
 inspired by the converter plugin of DeaDBeeF, deadbeef-qt5, and Qmmp.
 It supports DeaDBeeF encoder presets, CUE sheet splitting, Title Formatting
 output path templates, DSP resampling, tag editing, and automatic cover art
 embedding (PNG, JPEG, WebP, BMP, GIF, TIFF).
EOF

chmod 0755 "$STAGE_DIR/DEBIAN"
chmod 0644 "$STAGE_DIR/DEBIAN/control"

OUT_DEB="${SCRIPT_DIR}/${PKG_NAME}.deb"
echo "[3/3] Building ${OUT_DEB}..."
dpkg-deb --root-owner-group --build "$STAGE_DIR" "$OUT_DEB"

rm -rf "$BUILD_DIR" "$STAGE_DIR"
echo "Successfully built: ${OUT_DEB}"
