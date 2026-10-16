#!/usr/bin/env bash
# Build a portable x86_64 AppImage bundle for Qddbconv (with KDE Breeze style & XDG Desktop Portal native file dialogs)
# Copyright (C) 2026 goberghen & Arena.ai Agent
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="1.2.0"
APPIMAGE_NAME="Qddbconv-${VERSION}-x86_64.AppImage"
BUILD_DIR="/tmp/qddbconv-appimage-build"
APPDIR="/tmp/Qddbconv.AppDir"
RUNTIME_BIN="/tmp/runtime-x86_64"
SQFS_FILE="/tmp/qddbconv-appdir.sqfs"

rm -rf "$BUILD_DIR" "$APPDIR" "$SQFS_FILE"
mkdir -p "$BUILD_DIR" \
         "$APPDIR/usr/bin" \
         "$APPDIR/usr/lib" \
         "$APPDIR/usr/plugins/platformthemes" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
         "$APPDIR/usr/share/man/man1" \
         "$APPDIR/usr/share/doc/qddbconv"

echo "[1/5] Compiling qddbconv (Release)..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j"$(nproc)"
strip --strip-unneeded "$BUILD_DIR/qddbconv"

echo "[2/5] Populating AppDir..."
cp "$BUILD_DIR/qddbconv" "$APPDIR/usr/bin/qddbconv"
cp "$SCRIPT_DIR/qddbconv.desktop" "$APPDIR/qddbconv.desktop"
cp "$SCRIPT_DIR/qddbconv.desktop" "$APPDIR/usr/share/applications/qddbconv.desktop"
cp "$SCRIPT_DIR/qddbconv.svg" "$APPDIR/qddbconv.svg"
cp "$SCRIPT_DIR/qddbconv.svg" "$APPDIR/.DirIcon"
cp "$SCRIPT_DIR/qddbconv.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/qddbconv.svg"
cp "$SCRIPT_DIR/qddbconv.1" "$APPDIR/usr/share/man/man1/qddbconv.1"
gzip -9n -c "$SCRIPT_DIR/qddbconv.1" > "$APPDIR/usr/share/man/man1/qddbconv.1.gz"
cp "$SCRIPT_DIR/README.md" "$SCRIPT_DIR/INSTALL.md" "$SCRIPT_DIR/CHANGELOG.md" "$SCRIPT_DIR/LICENSE" "$APPDIR/usr/share/doc/qddbconv/"

cat > "$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
EOF

echo "[3/5] Bundling Qt6, XDG Desktop Portal & KDE Breeze style plugins..."
QT_PLUGIN_ROOT="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"

for subdir in platforms styles imageformats iconengines xcbglintegrations wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client platforminputcontexts; do
    if [ -d "${QT_PLUGIN_ROOT}/${subdir}" ]; then
        cp -r "${QT_PLUGIN_ROOT}/${subdir}" "$APPDIR/usr/plugins/"
    fi
done

# Bundle ONLY libqxdgdesktopportal.so in platformthemes (provides native KDE file dialogs
# with drives/mounts/Places over D-Bus without loading KIO/kioworker in-process)
if [ ! -f "${QT_PLUGIN_ROOT}/platformthemes/libqxdgdesktopportal.so" ]; then
    echo "ERROR: ${QT_PLUGIN_ROOT}/platformthemes/libqxdgdesktopportal.so not found (install qt6-xdgdesktopportal-platformtheme)." >&2
    exit 1
fi
cp "${QT_PLUGIN_ROOT}/platformthemes/libqxdgdesktopportal.so" "$APPDIR/usr/plugins/platformthemes/"

if [ ! -f "$APPDIR/usr/plugins/styles/breeze6.so" ]; then
    echo "ERROR: breeze6.so not found in ${QT_PLUGIN_ROOT}/styles (install kde-style-breeze)." >&2
    exit 1
fi

if [ -d "${QT_PLUGIN_ROOT}/kf6/kwindowsystem" ]; then
    mkdir -p "$APPDIR/usr/plugins/kf6"
    cp -r "${QT_PLUGIN_ROOT}/kf6/kwindowsystem" "$APPDIR/usr/plugins/kf6/"
fi

# Exclude base system libraries so host libraries (like libglib-2.0 -> libpcre2-8) never conflict
EXCLUDED_LIBS="^(linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread\.so|libresolv\.so|libstdc\+\+\.so|libgcc_s\.so|libGL|libEGL|libGLX|libGLdispatch|libvulkan|libdrm|libgbm|libX11\.so|libXext\.so|libxcb\.so|libwayland-client\.so|libwayland-cursor\.so|libwayland-egl\.so|libdbus-1\.so|libfontconfig\.so|libfreetype\.so|libharfbuzz\.so|libz\.so|libglib-2\.0\.so|libgobject-2\.0\.so|libgio-2\.0\.so|libgmodule-2\.0\.so|libpcre2-8\.so|libselinux\.so|libacl\.so|libcap\.so|libexpat\.so|libsystemd\.so|libmount\.so|libblkid\.so|libudev\.so|libkrb5|libk5crypto|libgssapi_krb5|libcom_err|libkeyutils)"

collect_deps() {
    local target="$1"
    ldd "$target" 2>/dev/null | awk '/=> \// {print $3}' | while read -r libpath; do
        local libname
        libname="$(basename "$libpath")"
        if ! echo "$libname" | grep -Eq "$EXCLUDED_LIBS"; then
            if [ ! -f "$APPDIR/usr/lib/$libname" ]; then
                cp -L "$libpath" "$APPDIR/usr/lib/$libname"
                chmod 0755 "$APPDIR/usr/lib/$libname"
            fi
        fi
    done
}

collect_deps "$APPDIR/usr/bin/qddbconv"
find "$APPDIR/usr/plugins" -name "*.so" -type f | while read -r plugin; do
    collect_deps "$plugin"
done

# Set relative RPATHs so bundled binaries/plugins always resolve bundled libraries via $ORIGIN
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/qddbconv" || true
    find "$APPDIR/usr/lib" -maxdepth 1 -name "*.so*" -type f | while read -r lib; do
        patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
    done
    find "$APPDIR/usr/plugins" -name "*.so" -type f | while read -r plug; do
        patchelf --set-rpath '$ORIGIN/../../lib:$ORIGIN/../../../lib:$ORIGIN/../lib' "$plug" 2>/dev/null || true
    done
fi

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
SELF="$(readlink -f "$0")"
HERE="${SELF%/*}"

if [ "${1:-}" = "--man" ]; then
    if command -v man >/dev/null 2>&1; then
        exec man "${HERE}/usr/share/man/man1/qddbconv.1"
    else
        exec "${PAGER:-less}" "${HERE}/usr/share/man/man1/qddbconv.1"
    fi
fi

# Notice: We do NOT export LD_LIBRARY_PATH or QT_PLUGIN_PATH here.
# Bundled ELF binaries use relative RPATH ($ORIGIN/../lib) and usr/bin/qt.conf
# so child processes (ffprobe, ffmpeg, encoders) keep a clean host environment.
# QT_QPA_PLATFORMTHEME=xdgdesktopportal delegates file dialogs over D-Bus to
# xdg-desktop-portal-kde (giving native KDE dialogs with drives/mounts/Places).
export PATH="${HERE}/usr/bin:${PATH:-}"
export MANPATH="${HERE}/usr/share/man:${MANPATH:-}"
export QT_QPA_PLATFORMTHEME="xdgdesktopportal"
export QT_STYLE_OVERRIDE="${QT_STYLE_OVERRIDE:-Breeze}"

exec "${HERE}/usr/bin/qddbconv" "$@"
EOF
chmod 0755 "$APPDIR/AppRun"

echo "[4/5] Fetching AppImage Type 2 static runtime..."
if [ ! -f "$RUNTIME_BIN" ]; then
    curl -fsSL -o "$RUNTIME_BIN" \
        "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
fi
chmod 0755 "$RUNTIME_BIN"

echo "[5/5] Creating SquashFS payload and assembling ${APPIMAGE_NAME}..."
mksquashfs "$APPDIR" "$SQFS_FILE" -root-owned -noappend -comp gzip -b 131072 >/dev/null
cat "$RUNTIME_BIN" "$SQFS_FILE" > "${SCRIPT_DIR}/${APPIMAGE_NAME}"
chmod 0755 "${SCRIPT_DIR}/${APPIMAGE_NAME}"

rm -rf "$BUILD_DIR" "$SQFS_FILE" "$APPDIR"
echo "Successfully built: ${SCRIPT_DIR}/${APPIMAGE_NAME}"
