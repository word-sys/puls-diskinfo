#!/bin/bash
#
# build-appimage.sh — Build an AppImage for Puls DiskInfo
#
# Usage: ./packaging/build-appimage.sh
#
# Requires: meson, ninja, wget (for downloading linuxdeploy)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APP_DIR="$BUILD_DIR/AppDir"
DIST_DIR="$PROJECT_DIR/dist"
ARCH="${ARCH:-x86_64}"

echo "══════════════════════════════════════════════════════════"
echo "  Puls DiskInfo — AppImage Builder"
echo "══════════════════════════════════════════════════════════"

# Clean previous build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR" "$DIST_DIR"

# ── Step 1: Build with Meson ─────────────────────────────────
echo ""
echo "▸ Configuring build..."
meson setup "$BUILD_DIR/meson" "$PROJECT_DIR" \
    --prefix=/usr \
    --buildtype=release \
    -Dstrip=true

echo "▸ Compiling..."
meson compile -C "$BUILD_DIR/meson"

echo "▸ Installing to AppDir..."
DESTDIR="$APP_DIR" meson install -C "$BUILD_DIR/meson"

# ── Step 2: Download linuxdeploy tools ───────────────────────
echo ""
echo "▸ Downloading linuxdeploy..."
TOOLS_DIR="$BUILD_DIR/tools"
mkdir -p "$TOOLS_DIR"

if [ ! -f "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" ]; then
    wget -q -O "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
    chmod +x "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
fi

if [ ! -f "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh" ]; then
    wget -q -O "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh" \
        "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
    chmod +x "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"
fi

# ── Step 3: Create AppImage ──────────────────────────────────
echo ""
echo "▸ Creating AppImage..."

# Copy icon if not present
ICON_SRC="$PROJECT_DIR/data/io.github.puls.diskinfo.svg"
ICON_DST="$APP_DIR/usr/share/icons/hicolor/scalable/apps/io.github.puls.diskinfo.svg"
if [ -f "$ICON_SRC" ]; then
    mkdir -p "$(dirname "$ICON_DST")"
    cp "$ICON_SRC" "$ICON_DST"
fi

# Use a fallback PNG icon if SVG not available
if [ ! -f "$ICON_SRC" ]; then
    echo "▸ No SVG icon found, using fallback icon name..."
fi

export DEPLOY_GTK_VERSION=4
export PATH="$TOOLS_DIR:$PATH"

cd "$BUILD_DIR"

"$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" \
    --appdir "$APP_DIR" \
    --plugin gtk \
    --output appimage \
    --desktop-file "$APP_DIR/usr/share/applications/io.github.puls.diskinfo.desktop" \
    || {
        echo ""
        echo "⚠ linuxdeploy failed. Trying without GTK plugin..."
        "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" \
            --appdir "$APP_DIR" \
            --output appimage \
            --desktop-file "$APP_DIR/usr/share/applications/io.github.puls.diskinfo.desktop"
    }

# ── Step 4: Move to dist ─────────────────────────────────────
echo ""
echo "▸ Collecting output..."

APPIMAGE_FILE=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -1)
if [ -n "$APPIMAGE_FILE" ]; then
    VERSION=$(meson introspect "$BUILD_DIR/meson" --projectinfo | grep -o '"version": "[^"]*"' | cut -d'"' -f4)
    FINAL_NAME="Puls_DiskInfo-${VERSION:-1.0.0}-$ARCH.AppImage"
    mv "$APPIMAGE_FILE" "$DIST_DIR/$FINAL_NAME"
    echo ""
    echo "✓ AppImage created: dist/$FINAL_NAME"
else
    echo "✗ AppImage file not found!"
    exit 1
fi

echo ""
echo "══════════════════════════════════════════════════════════"
echo "  Build complete!"
echo "══════════════════════════════════════════════════════════"
