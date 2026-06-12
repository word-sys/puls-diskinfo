#!/bin/bash
#
# build-binary.sh — Build a portable binary tarball for PULS DiskInfo
#
# Usage: ./packaging/build-binary.sh
#
# Produces a self-contained directory with binary, helper, data files,
# and an install script.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-binary"
DIST_DIR="$PROJECT_DIR/dist"
ARCH="${ARCH:-x86_64}"

# Get version from meson.build
VERSION=$(grep "version:" "$PROJECT_DIR/meson.build" | head -1 | grep -o "'[^']*'" | head -1 | tr -d "'")
VERSION="${VERSION:-1.0.0}"

PKG_NAME="puls-diskinfo-${VERSION}-linux-${ARCH}"
PKG_DIR="$BUILD_DIR/$PKG_NAME"

echo "══════════════════════════════════════════════════════════"
echo "  PULS DiskInfo — Binary Release Builder"
echo "  Version: $VERSION  Arch: $ARCH"
echo "══════════════════════════════════════════════════════════"

# Clean
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR" "$DIST_DIR" "$PKG_DIR"

# ── Step 1: Build ─────────────────────────────────────────────
echo ""
echo "▸ Configuring build..."
meson setup "$BUILD_DIR/meson" "$PROJECT_DIR" \
    --prefix=/usr \
    --buildtype=release \
    -Dstrip=true

echo "▸ Compiling..."
meson compile -C "$BUILD_DIR/meson"

# ── Step 2: Assemble package directory ────────────────────────
echo ""
echo "▸ Assembling release package..."

# Binary
cp "$BUILD_DIR/meson/src/puls-diskinfo" "$PKG_DIR/"

# Helper
cp "$BUILD_DIR/meson/helper/puls-diskinfo-helper" "$PKG_DIR/"

# Data files
mkdir -p "$PKG_DIR/data"
cp "$PROJECT_DIR/data/io.github.puls.diskinfo.desktop" "$PKG_DIR/data/"
cp "$PROJECT_DIR/data/io.github.puls.diskinfo.policy" "$PKG_DIR/data/"
[ -f "$PROJECT_DIR/data/io.github.puls.diskinfo.svg" ] && \
    cp "$PROJECT_DIR/data/io.github.puls.diskinfo.svg" "$PKG_DIR/data/" || true

# Docs
cp "$PROJECT_DIR/LICENSE" "$PKG_DIR/"
cp "$PROJECT_DIR/README.md" "$PKG_DIR/"

# ── Step 3: Create install script ─────────────────────────────
cat > "$PKG_DIR/install.sh" << 'INSTALL_EOF'
#!/bin/bash
#
# PULS DiskInfo Installer
#
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"

echo "Installing PULS DiskInfo to $PREFIX..."

# Check for root
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Install binary
install -Dm755 "$SCRIPT_DIR/puls-diskinfo" "$PREFIX/bin/puls-diskinfo"

# Install helper
install -Dm755 "$SCRIPT_DIR/puls-diskinfo-helper" "$PREFIX/libexec/puls-diskinfo-helper"

# Install desktop file
install -Dm644 "$SCRIPT_DIR/data/io.github.puls.diskinfo.desktop" \
    "$PREFIX/share/applications/io.github.puls.diskinfo.desktop"

# Install polkit policy
install -Dm644 "$SCRIPT_DIR/data/io.github.puls.diskinfo.policy" \
    "$PREFIX/share/polkit-1/actions/io.github.puls.diskinfo.policy"

# Update polkit policy helper path
sed -i "s|/usr/libexec/puls-diskinfo-helper|$PREFIX/libexec/puls-diskinfo-helper|g" \
    "$PREFIX/share/polkit-1/actions/io.github.puls.diskinfo.policy"

# Install icon if available
if [ -f "$SCRIPT_DIR/data/io.github.puls.diskinfo.svg" ]; then
    install -Dm644 "$SCRIPT_DIR/data/io.github.puls.diskinfo.svg" \
        "$PREFIX/share/icons/hicolor/scalable/apps/io.github.puls.diskinfo.svg"
    gtk-update-icon-cache -f "$PREFIX/share/icons/hicolor/" 2>/dev/null || true
fi

echo ""
echo "✓ PULS DiskInfo installed successfully!"
echo "  Binary:  $PREFIX/bin/puls-diskinfo"
echo "  Helper:  $PREFIX/libexec/puls-diskinfo-helper"
echo ""
echo "Run with: puls-diskinfo"
INSTALL_EOF

chmod +x "$PKG_DIR/install.sh"

# ── Step 3b: Uninstall script ─────────────────────────────────
cat > "$PKG_DIR/uninstall.sh" << 'UNINSTALL_EOF'
#!/bin/bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root (use sudo)."
    exit 1
fi

echo "Removing PULS DiskInfo from $PREFIX..."

rm -f "$PREFIX/bin/puls-diskinfo"
rm -f "$PREFIX/libexec/puls-diskinfo-helper"
rm -f "$PREFIX/share/applications/io.github.puls.diskinfo.desktop"
rm -f "$PREFIX/share/polkit-1/actions/io.github.puls.diskinfo.policy"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/io.github.puls.diskinfo.svg"

echo "✓ PULS DiskInfo removed."
UNINSTALL_EOF

chmod +x "$PKG_DIR/uninstall.sh"

# ── Step 4: Create tarball ────────────────────────────────────
echo ""
echo "▸ Creating tarball..."

cd "$BUILD_DIR"
tar czf "$DIST_DIR/$PKG_NAME.tar.gz" "$PKG_NAME"

echo ""
echo "✓ Binary release: dist/$PKG_NAME.tar.gz"
echo ""
echo "Contents:"
tar tzf "$DIST_DIR/$PKG_NAME.tar.gz" | head -20
echo ""
echo "══════════════════════════════════════════════════════════"
echo "  Build complete!"
echo "══════════════════════════════════════════════════════════"
