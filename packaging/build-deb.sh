#!/bin/bash
#
# build-deb.sh — Build a .deb package for Puls DiskInfo
#
# Usage: ./packaging/build-deb.sh
#
# Requires: dpkg-buildpackage, debhelper, meson, ninja-build
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$PROJECT_DIR/dist"

echo "══════════════════════════════════════════════════════════"
echo "  Puls DiskInfo — Debian Package Builder"
echo "══════════════════════════════════════════════════════════"

# Check for required tools
for cmd in dpkg-buildpackage meson ninja; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "Error: $cmd is not installed."
        echo "Install with: sudo apt install build-essential debhelper meson ninja-build"
        exit 1
    fi
done

mkdir -p "$DIST_DIR"

# ── Build ─────────────────────────────────────────────────────
echo ""
echo "▸ Building .deb package..."
cd "$PROJECT_DIR"

dpkg-buildpackage -us -uc -b --no-check-builddeps 2>&1 || {
    echo ""
    echo "⚠ dpkg-buildpackage failed. Trying with --no-check-builddeps..."
    dpkg-buildpackage -us -uc -b --no-check-builddeps
}

# ── Collect output ────────────────────────────────────────────
echo ""
echo "▸ Collecting .deb files..."

# dpkg-buildpackage places .deb in parent directory
DEB_FILE=$(find "$PROJECT_DIR/.." -maxdepth 1 -name "puls-diskinfo_*.deb" -type f | head -1)

if [ -n "$DEB_FILE" ]; then
    cp "$DEB_FILE" "$DIST_DIR/"
    echo "✓ Package created: dist/$(basename "$DEB_FILE")"
else
    echo "✗ .deb file not found!"
    exit 1
fi

echo ""
echo "══════════════════════════════════════════════════════════"
echo "  Build complete!"
echo "══════════════════════════════════════════════════════════"
