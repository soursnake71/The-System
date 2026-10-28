#!/bin/bash
# macOS installer for SPQuencer1200: a .pkg that puts the VST3 in /Library/Audio/Plug-Ins/VST3 and the AU in
# /Library/Audio/Plug-Ins/Components, each as its own choice. CI runs it on the Mac runner:
#   make-mac-pkg.sh <folder holding SPQuencer1200.vst3 and SPQuencer1200.component> <version> <output folder>
set -euo pipefail
SRC="$1"; VER="$2"; OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/vst3/root" "$WORK/au/root" "$WORK/scripts" "$WORK/res" "$OUT"
cp -R "$SRC/SPQuencer1200.vst3" "$WORK/vst3/root/"
cp -R "$SRC/SPQuencer1200.component" "$WORK/au/root/"

# after installing the AU, make macOS rescan Audio Units so Logic and GarageBand see it straight away
cat > "$WORK/scripts/postinstall" <<'SH'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SH
chmod +x "$WORK/scripts/postinstall"

pkgbuild --root "$WORK/vst3/root" --identifier com.thesnakedevice.omega.spquencer1200.vst3 --version "$VER" \
  --install-location /Library/Audio/Plug-Ins/VST3 "$WORK/SPQuencer1200-VST3.pkg"
pkgbuild --root "$WORK/au/root" --identifier com.thesnakedevice.omega.spquencer1200.au --version "$VER" \
  --scripts "$WORK/scripts" --install-location /Library/Audio/Plug-Ins/Components "$WORK/SPQuencer1200-AU.pkg"

cp "$HERE/mac-welcome.html" "$HERE/mac-conclusion.html" "$WORK/res/"
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>SPQuencer1200 $VER</title>
  <welcome file="mac-welcome.html" mime-type="text/html"/>
  <conclusion file="mac-conclusion.html" mime-type="text/html"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plugin" description="For Ableton Live, Bitwig, Cubase, Studio One, Reaper and other VST3 hosts.">
    <pkg-ref id="com.thesnakedevice.omega.spquencer1200.vst3"/>
  </choice>
  <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand and other Audio Unit hosts.">
    <pkg-ref id="com.thesnakedevice.omega.spquencer1200.au"/>
  </choice>
  <pkg-ref id="com.thesnakedevice.omega.spquencer1200.vst3" version="$VER">SPQuencer1200-VST3.pkg</pkg-ref>
  <pkg-ref id="com.thesnakedevice.omega.spquencer1200.au" version="$VER">SPQuencer1200-AU.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/res" --package-path "$WORK" \
  "$OUT/SPQuencer1200-$VER.pkg"
pkgutil --check-signature "$OUT/SPQuencer1200-$VER.pkg" || true   # unsigned: reports "no signature"
ls -la "$OUT/SPQuencer1200-$VER.pkg"
