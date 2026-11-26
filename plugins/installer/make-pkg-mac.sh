#!/bin/bash
# Omega Twelve (or another Omega app) installer for macOS: one .pkg putting the VST3s, the AUs and the standalone app in place.
# usage: make-pkg-mac.sh <folder with the built .vst3, .component and .app> <version> <output.pkg> [name]
#   name defaults to "Omega Twelve"; package ids are com.thesnakedevice.omega.<last word, lower case>.vst3 / .au / .app
set -euo pipefail
src="$1"; ver="$2"; out="$3"; name="${4:-Omega Twelve}"; work="$(mktemp -d)"
id="com.thesnakedevice.omega.$(echo "${name##* }" | tr '[:upper:]' '[:lower:]')"
if [ "$name" = "Omega Twelve" ]; then vstdesc="Omega Twelve and Omega Twelve Instrument for VST3 hosts"; else vstdesc="$name for VST3 hosts"; fi
mkdir -p "$work/vst3/Library/Audio/Plug-Ins/VST3" "$work/au/Library/Audio/Plug-Ins/Components" "$work/app/Applications"
cp -R "$src"/*.vst3 "$work/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$src"/*.component "$work/au/Library/Audio/Plug-Ins/Components/"
cp -R "$src"/*.app "$work/app/Applications/"
# ad-hoc signature (no paid Apple developer ID): macOS asks once, right-click > Open on the installer
for b in "$work"/*/Library/Audio/Plug-Ins/*/* "$work"/app/Applications/*.app; do codesign --force --deep -s - "$b"; done
for part in vst3 au app; do
  # keep the bundles where they're installed even if a copy exists elsewhere
  pkgbuild --analyze --root "$work/$part" "$work/$part.plist"
  /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$work/$part.plist" 2>/dev/null || true
  i=1; while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$work/$part.plist" 2>/dev/null; do i=$((i + 1)); done
  pkgbuild --root "$work/$part" --component-plist "$work/$part.plist" --identifier "$id.$part" --version "$ver" --install-location / "$work/$part.pkg"
done
cat > "$work/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$name $ver</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline><line choice="vst3"/><line choice="au"/><line choice="app"/></choices-outline>
  <choice id="vst3" title="VST3 plugins" description="$vstdesc"><pkg-ref id="$id.vst3"/></choice>
  <choice id="au" title="Audio Unit plugins" description="For Logic, GarageBand and other AU hosts"><pkg-ref id="$id.au"/></choice>
  <choice id="app" title="Standalone app" description="$name in Applications"><pkg-ref id="$id.app"/></choice>
  <pkg-ref id="$id.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="$id.au">au.pkg</pkg-ref>
  <pkg-ref id="$id.app">app.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$work/dist.xml" --package-path "$work" "$out"
echo "made $out"
