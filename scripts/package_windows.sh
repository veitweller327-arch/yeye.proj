#!/usr/bin/env bash
# Builds the Windows plugin (cross-compiled with Zig) and packages:
#   dist/TimingShiftFX-<ver>-Windows-Setup.exe   (double-click installer, NSIS)
#   dist/TimingShiftFX-<ver>-Windows.zip         (bundle + Install/Uninstall .bat)
# Requirements (Linux): pip install ziglang ; apt install nsis zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="1.2.0"
BUILD="$ROOT/build-win"
DIST="$ROOT/dist"
"$ROOT/scripts/build_windows_cross.sh" "$BUILD"
cp "$ROOT/installer/Info.plist" "$BUILD/TimingShiftFX.ofx.bundle/Contents/Info.plist"
mkdir -p "$DIST"
makensis -V2 -DVERSION="$VER" -DBUNDLE_DIR="$BUILD/TimingShiftFX.ofx.bundle" \
  -DOUT_FILE="$DIST/TimingShiftFX-$VER-Windows-Setup.exe" "$ROOT/installer/TimingShiftFX.nsi"
STAGE="$BUILD/zip/TimingShiftFX-$VER-Windows"
rm -rf "$BUILD/zip" && mkdir -p "$STAGE"
cp -r "$BUILD/TimingShiftFX.ofx.bundle" "$STAGE/"
cp "$ROOT/installer/"*.bat "$STAGE/"
cp "$ROOT/docs/INSTALL-Windows.txt" "$STAGE/LIES-MICH-Installation.txt"
rm -f "$DIST/TimingShiftFX-$VER-Windows.zip"
(cd "$BUILD/zip" && zip -qr "$DIST/TimingShiftFX-$VER-Windows.zip" "TimingShiftFX-$VER-Windows")
ls -la "$DIST"
