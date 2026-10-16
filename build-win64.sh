#!/usr/bin/env bash
# Build portable 64-bit Windows package (Qddbconv-1.1.0-win64.zip)
# Uses MinGW-w64 (x86_64-w64-mingw32-g++) and Qt6 for Windows (win64_mingw)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_VER="$(sed -n 's/^project(qddbconv VERSION \([0-9.]*\).*/\1/p' "$SCRIPT_DIR/CMakeLists.txt")"
APP_VER="${APP_VER:-1.2.1}"
QT_WIN_DIR="${QT_WIN_DIR:-/opt/qt6-win/6.8.2/mingw_64}"
WIN_ENC_DIR="${WIN_ENC_DIR:-/opt/win-encoders/mingw64/bin}"
MOC_BIN="${MOC_BIN:-/usr/lib/qt6/libexec/moc}"
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
STRIP="${STRIP:-x86_64-w64-mingw32-strip}"

BUILD_DIR="/tmp/qddbconv-win64-build"
DIST_ROOT="/tmp/qddbconv-win64-dist"
PKG_DIR="$DIST_ROOT/Qddbconv-${APP_VER}-win64"
OUT_ZIP="${1:-$SCRIPT_DIR/../Qddbconv-${APP_VER}-win64.zip}"

rm -rf "$BUILD_DIR" "$DIST_ROOT"
mkdir -p "$BUILD_DIR" "$PKG_DIR"

echo "[1/4] Generating Qt6 MOC sources..."
"$MOC_BIN" "$SCRIPT_DIR/src/preset.h"     -o "$BUILD_DIR/moc_preset.cpp"
"$MOC_BIN" "$SCRIPT_DIR/src/mainwindow.h" -o "$BUILD_DIR/moc_mainwindow.cpp"

echo "[2/4] Compiling Windows resource file (qddbconv.rc)..."
(cd "$SCRIPT_DIR" && "$WINDRES" qddbconv.rc -O coff -o "$BUILD_DIR/qddbconv_res.o")

echo "[3/4] Cross-compiling qddbconv.exe with $CXX..."
CXXFLAGS=(
    -std=c++17 -O2 -Wall
    -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1
    -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB
    -I"$SCRIPT_DIR/src"
    -I"$QT_WIN_DIR/include"
    -I"$QT_WIN_DIR/include/QtWidgets"
    -I"$QT_WIN_DIR/include/QtGui"
    -I"$QT_WIN_DIR/include/QtConcurrent"
    -I"$QT_WIN_DIR/include/QtCore"
)

SRCS=(
    "$SCRIPT_DIR/src/main.cpp"
    "$SCRIPT_DIR/src/preset.cpp"
    "$SCRIPT_DIR/src/track.cpp"
    "$SCRIPT_DIR/src/smaf2mid.cpp"
    "$SCRIPT_DIR/src/converter.cpp"
    "$SCRIPT_DIR/src/mainwindow.cpp"
    "$BUILD_DIR/moc_preset.cpp"
    "$BUILD_DIR/moc_mainwindow.cpp"
)

OBJS=()
for src in "${SRCS[@]}"; do
    obj="$BUILD_DIR/$(basename "${src%.cpp}").o"
    "$CXX" "${CXXFLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

"$CXX" -mwindows -O2 \
    "${OBJS[@]}" "$BUILD_DIR/qddbconv_res.o" \
    -L"$QT_WIN_DIR/lib" \
    -lQt6Widgets -lQt6Gui -lQt6Concurrent -lQt6Core -lQt6EntryPoint \
    -lshell32 -lole32 -luuid -ladvapi32 -lws2_32 -lgdi32 -luser32 \
    -o "$PKG_DIR/qddbconv.exe"

"$STRIP" --strip-unneeded "$PKG_DIR/qddbconv.exe"

echo "[4/4] Assembling Qt6 runtime, plugins, and audio encoders into $OUT_ZIP..."
# Core Qt6 DLLs
for dll in Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Concurrent.dll Qt6Svg.dll; do
    cp -L "$QT_WIN_DIR/bin/$dll" "$PKG_DIR/"
done

# Matching MinGW-w64 GCC runtime DLLs (stripped)
for rtdll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    rtpath="$("$CXX" -print-file-name="$rtdll")"
    if [ -f "$rtpath" ]; then
        cp -L "$rtpath" "$PKG_DIR/$rtdll"
    else
        cp -L "$QT_WIN_DIR/bin/$rtdll" "$PKG_DIR/$rtdll"
    fi
    "$STRIP" --strip-unneeded "$PKG_DIR/$rtdll"
done

# Qt6 plugins (Note: PNG & BMP are statically compiled directly inside Qt6Gui.dll;
# JPEG, GIF, ICO, SVG, WebP, TIFF, TGA, WBMP, and ICNS are loaded from imageformats/)
mkdir -p "$PKG_DIR/platforms" "$PKG_DIR/styles" "$PKG_DIR/imageformats" "$PKG_DIR/iconengines"
cp -L "$QT_WIN_DIR/plugins/platforms/qwindows.dll" "$PKG_DIR/platforms/"
cp -L "$QT_WIN_DIR/plugins/styles/qmodernwindowsstyle.dll" "$PKG_DIR/styles/"
for imgp in qjpeg.dll qico.dll qgif.dll qsvg.dll qwebp.dll qtiff.dll qtga.dll qwbmp.dll qicns.dll; do
    if [ -f "$QT_WIN_DIR/plugins/imageformats/$imgp" ]; then
        cp -L "$QT_WIN_DIR/plugins/imageformats/$imgp" "$PKG_DIR/imageformats/"
    fi
done
cp -L "$QT_WIN_DIR/plugins/iconengines/qsvgicon.dll" "$PKG_DIR/iconengines/"

# Bundle CLI audio encoders, FFmpeg/FFprobe, and tracker/chiptune decoders if present in $WIN_ENC_DIR
if [ -d "$WIN_ENC_DIR" ]; then
    for f in \
        ffmpeg.exe ffprobe.exe openmpt123.exe gme2wav.exe \
        avcodec-61.dll avfilter-10.dll avformat-61.dll avutil-59.dll swresample-5.dll swscale-8.dll zlib1.dll \
        libopenmpt-0.dll libmpg123-0.dll libvorbisfile-3.dll \
        flac.exe metaflac.exe lame.exe oggenc.exe opusenc.exe wavpack.exe \
        libFLAC.dll libmp3lame-0.dll libogg-0.dll libopus-0.dll libopusenc-0.dll \
        libvorbis-0.dll libvorbisenc-2.dll libwavpack-1.dll libiconv-2.dll; do
        if [ -f "$WIN_ENC_DIR/$f" ]; then
            cp -L "$WIN_ENC_DIR/$f" "$PKG_DIR/"
            "$STRIP" --strip-unneeded "$PKG_DIR/$f" 2>/dev/null || true
        fi
    done
fi

cp "$SCRIPT_DIR/README.md" "$SCRIPT_DIR/INSTALL.md" "$SCRIPT_DIR/CHANGELOG.md" "$SCRIPT_DIR/LICENSE" "$SCRIPT_DIR/qddbconv.1" "$SCRIPT_DIR/qddbconv.svg" "$PKG_DIR/"

rm -f "$OUT_ZIP"
(cd "$DIST_ROOT" && zip -r -9 -q "$OUT_ZIP" "Qddbconv-${APP_VER}-win64")
rm -rf "$BUILD_DIR" "$DIST_ROOT"
echo "Successfully built: $OUT_ZIP"
