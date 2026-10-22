#!/bin/bash
# Assembles a browser preview of the plugin UI (with the simulated engine and demo audio).
# Usage: tools/preview/build_preview.sh <output dir>
# Then serve it, e.g.  python3 -m http.server --directory <output dir>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/preview}"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$ROOT/ui/." "$OUT/"
mkdir -p "$OUT/demo"
cp "$ROOT/preview/demo/"*.mp3 "$OUT/demo/"
find "$OUT" -name "*.txt" -delete
echo "Preview written to $OUT"
