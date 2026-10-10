#!/usr/bin/env bash
# Cross-compiles the Windows x64 plugin on Linux/macOS using Zig as C++
# compiler (pip install ziglang). The result is a normal Windows DLL with the
# extension .ofx and no dependency on the Visual C++ Redistributable.
#
# On Windows itself, use Visual Studio + CMake instead (see docs/02-Bauen.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build-win}"
ZIG="${ZIG:-python3 -m ziglang}"
BUNDLE="$OUT/TimingShiftFX.ofx.bundle/Contents/Win64"
mkdir -p "$BUNDLE"
$ZIG c++ -target x86_64-windows-gnu -std=c++17 -O3 -shared \
  -fno-sanitize=undefined -DNDEBUG \
  -I"$ROOT/external/openfx/include" -I"$ROOT/src" \
  "$ROOT/src/TimingShiftCore.cpp" "$ROOT/src/TimingShiftPlugin.cpp" \
  -s \
  -o "$BUNDLE/TimingShiftFX.ofx"
rm -f "$BUNDLE"/*.lib "$BUNDLE"/*.pdb
echo "Built: $BUNDLE/TimingShiftFX.ofx"
