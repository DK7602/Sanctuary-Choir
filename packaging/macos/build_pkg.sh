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
  local plist=()
  if [[ "$2" == *.component || "$2" == *.vst3 || "$2" == *.app ]]; then
    # Bundles are "relocatable" by default: Installer would update a copy it finds anywhere on
    # the disk (e.g. the unzipped download) instead of installing where we say.
    pkgbuild --analyze --root "$WORK/$1/root" "$WORK/$1.plist"
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$1.plist" >/dev/null 2>&1; do
      /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/$1.plist" >/dev/null 2>&1 || true
      /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/$1.plist"
      i=$((i + 1))
    done
    plutil -p "$WORK/$1.plist"
    if [ "$i" -gt 0 ]; then plist=(--component-plist "$WORK/$1.plist"); fi
  fi
  pkgbuild --root "$WORK/$1/root" ${plist[@]+"${plist[@]}"} --identifier "$ID.$1" --version "$VERSION" \
           --install-location "$3" "$WORK/$1.pkg"
}
stage au "Sanctuary Choir.component" "/Library/Audio/Plug-Ins/Components"
stage vst3 "Sanctuary Choir.vst3" "/Library/Audio/Plug-Ins/VST3"
stage app "Sanctuary Choir.app" "/Applications"
stage voices "SanctuaryVoices" "/Library/Application Support/Sanctuary Choir/Voices"
stage credits "ROOMS-CREDITS.txt" "/Library/Application Support/Sanctuary Choir"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Sanctuary Choir</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="au"/><line choice="vst3"/><line choice="app"/><line choice="voices"/><line choice="credits"/>
  </choices-outline>
  <choice id="au" title="Audio Unit (Logic, GarageBand)"><pkg-ref id="$ID.au"/></choice>
  <choice id="vst3" title="VST3 (Ableton, Cubase, Studio One, Reaper...)"><pkg-ref id="$ID.vst3"/></choice>
  <choice id="app" title="Standalone app"><pkg-ref id="$ID.app"/></choice>
  <choice id="voices" title="Sanctuary Voices (real recorded singers, about 90 MB)"><pkg-ref id="$ID.voices"/></choice>
  <choice id="credits" title="Credits for the rooms" visible="false" enabled="false" selected="true"><pkg-ref id="$ID.credits"/></choice>
  <pkg-ref id="$ID.au" version="$VERSION">au.pkg</pkg-ref>
  <pkg-ref id="$ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VERSION">app.pkg</pkg-ref>
  <pkg-ref id="$ID.voices" version="$VERSION">voices.pkg</pkg-ref>
  <pkg-ref id="$ID.credits" version="$VERSION">credits.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$OUT"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/Sanctuary Choir.pkg"
rm -rf "$WORK"
echo "Built $OUT/Sanctuary Choir.pkg"
