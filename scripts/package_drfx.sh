#!/usr/bin/env bash
# Builds dist/TimingShiftGhost-<ver>.drfx - a DaVinci Resolve "bundle" that
# installs the Fusion version of the ghost effect with a double-click.
# A .drfx is a ZIP whose folders mirror Resolve's Templates folder:
#   Edit/Effects/<category>/<effect>.setting  -> Edit page > Effects
# Requirements: zip (and lua5.1 for the structural check).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="1.1.0"
SETTING="$ROOT/fusion/Timing Shift Ghost.setting"
if command -v lua5.1 >/dev/null; then lua5.1 "$ROOT/scripts/check_fusion_setting.lua" "$SETTING"; fi
STAGE="$(mktemp -d)"
mkdir -p "$STAGE/Edit/Effects/Timing Shift FX"
cp "$SETTING" "$STAGE/Edit/Effects/Timing Shift FX/"
mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/TimingShiftGhost-$VER.drfx"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" Edit)
rm -rf "$STAGE"
unzip -l "$OUT"
