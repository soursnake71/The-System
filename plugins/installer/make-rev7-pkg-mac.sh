#!/bin/bash
# Omega REV7 installer for macOS: one .pkg with the VST3 and the Audio Unit.
# usage: make-rev7-pkg-mac.sh <folder with "Omega REV7.vst3" and "Omega REV7.component"> <version> <output.pkg>
set -euo pipefail
src="$1"; ver="$2"; out="$3"; work="$(mktemp -d)"
mkdir -p "$work/vst3/Library/Audio/Plug-Ins/VST3" "$work/au/Library/Audio/Plug-Ins/Components"
cp -R "$src/Omega REV7.vst3" "$work/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$src/Omega REV7.component" "$work/au/Library/Audio/Plug-Ins/Components/"
# ad-hoc signature (no paid Apple developer ID): right-click > Open on the installer the first time
for b in "$work"/*/Library/Audio/Plug-Ins/*/*; do codesign --force --deep -s - "$b"; done
for part in vst3 au; do
  pkgbuild --analyze --root "$work/$part" "$work/$part.plist"
  i=0; while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$work/$part.plist" 2>/dev/null; do i=$((i + 1)); done
  pkgbuild --root "$work/$part" --component-plist "$work/$part.plist" --identifier "com.thesnakedevice.omega.rev7.$part" --version "$ver" --install-location / "$work/$part.pkg"
done
cat > "$work/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Omega REV7 $ver</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline><line choice="vst3"/><line choice="au"/></choices-outline>
  <choice id="vst3" title="VST3 plugin" description="Omega REV7 for VST3 hosts"><pkg-ref id="com.thesnakedevice.omega.rev7.vst3"/></choice>
  <choice id="au" title="Audio Unit plugin" description="For Logic, GarageBand and other AU hosts"><pkg-ref id="com.thesnakedevice.omega.rev7.au"/></choice>
  <pkg-ref id="com.thesnakedevice.omega.rev7.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="com.thesnakedevice.omega.rev7.au">au.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$work/dist.xml" --package-path "$work" "$out"
echo "made $out"
