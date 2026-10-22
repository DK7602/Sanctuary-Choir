#!/bin/bash
# Builds "Sanctuary Choir.pkg" that installs the AU, VST3, standalone app and Sanctuary Voices.
# Usage: packaging/macos/build_pkg.sh <folder with the bundles> <version> <output dir>
set -euo pipefail
SRC="$1"; VERSION="$2"; OUT="$3"
WORK=$(mktemp -d)
ID=com.sanctuaryaudio.sanctuarychoir

stage() { # name, bundle, install location
  mkdir -p "$WORK/$1/root"
  cp -R "$SRC/$2" "$WORK/$1/root/"
  pkgbuild --root "$WORK/$1/root" --identifier "$ID.$1" --version "$VERSION" \
           --install-location "$3" "$WORK/$1.pkg"
}
stage au "Sanctuary Choir.component" "/Library/Audio/Plug-Ins/Components"
stage vst3 "Sanctuary Choir.vst3" "/Library/Audio/Plug-Ins/VST3"
stage app "Sanctuary Choir.app" "/Applications"
stage voices "SanctuaryVoices" "/Library/Application Support/Sanctuary Choir/Voices"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Sanctuary Choir</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="au"/><line choice="vst3"/><line choice="app"/><line choice="voices"/>
  </choices-outline>
  <choice id="au" title="Audio Unit (Logic, GarageBand)"><pkg-ref id="$ID.au"/></choice>
  <choice id="vst3" title="VST3 (Ableton, Cubase, Studio One, Reaper...)"><pkg-ref id="$ID.vst3"/></choice>
  <choice id="app" title="Standalone app"><pkg-ref id="$ID.app"/></choice>
  <choice id="voices" title="Sanctuary Voices (real recorded singers, 13 MB)"><pkg-ref id="$ID.voices"/></choice>
  <pkg-ref id="$ID.au" version="$VERSION">au.pkg</pkg-ref>
  <pkg-ref id="$ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VERSION">app.pkg</pkg-ref>
  <pkg-ref id="$ID.voices" version="$VERSION">voices.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$OUT"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/Sanctuary Choir.pkg"
rm -rf "$WORK"
echo "Built $OUT/Sanctuary Choir.pkg"
