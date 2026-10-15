#!/usr/bin/env bash
# Build an installable Debian/Ubuntu .deb package for Qddbconv
# Copyright (C) 2026 goberghen & Arena.ai Agent
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="1.1.0"
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
Depends: libc6, libstdc++6, libqt6widgets6, libqt6gui6, libqt6core6t64 | libqt6core6, libqt6concurrent6
Recommends: ffmpeg, flac, lame, opus-tools, vorbis-tools, wavpack
Maintainer: goberghen <goberghen@users.noreply.github.com>
Homepage: https://github.com/goberghen/qddbconv
Description: Standalone Qt6 audio converter inspired by DeaDBeeF
 Qddbconv is a multi-threaded Qt6 audio converter for Linux and KDE
 inspired by the converter plugin of DeaDBeeF, deadbeef-qt5, and Qmmp.
 It supports DeaDBeeF encoder presets, CUE sheet splitting, Title Formatting
 output path templates, DSP resampling, tag editing, and automatic cover art
 embedding.
EOF

chmod 0755 "$STAGE_DIR/DEBIAN"
chmod 0644 "$STAGE_DIR/DEBIAN/control"

OUT_DEB="${SCRIPT_DIR}/${PKG_NAME}.deb"
echo "[3/3] Building ${OUT_DEB}..."
dpkg-deb --root-owner-group --build "$STAGE_DIR" "$OUT_DEB"

rm -rf "$BUILD_DIR" "$STAGE_DIR"
echo "Successfully built: ${OUT_DEB}"
