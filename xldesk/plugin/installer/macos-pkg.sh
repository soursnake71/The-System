#!/bin/bash
# Builds XL-Desk-<version>-macOS.pkg: VST3 -> /Library/Audio/Plug-Ins/VST3, app -> /Applications,
# AAX (if built) -> /Library/Application Support/Avid/Audio/Plug-Ins.
# Usage: macos-pkg.sh <artefacts dir> <version> <out dir> [Developer ID Installer identity]
set -euo pipefail
ART="$1"; VER="$2"; OUT="$3"; SIGN="${4:-}"
W=$(mktemp -d); mkdir -p "$OUT"
comp () { # id, payload, install location
  mkdir -p "$W/$1/root"; cp -R "$2" "$W/$1/root/"
  pkgbuild --root "$W/$1/root" --identifier "com.absolutefuturestudios.xldesk.$1" --version "$VER" --install-location "$3" "$W/$1.pkg"
}
comp vst3 "$ART/VST3/XL-Desk.vst3" "/Library/Audio/Plug-Ins/VST3"
comp app "$ART/Standalone/XL-Desk.app" "/Applications"
CHOICES='<line choice="vst3"/><line choice="app"/>'
DEFS='<choice id="vst3" title="VST3 plugin"><pkg-ref id="com.absolutefuturestudios.xldesk.vst3"/></choice><choice id="app" title="Standalone app"><pkg-ref id="com.absolutefuturestudios.xldesk.app"/></choice>'
REFS="<pkg-ref id=\"com.absolutefuturestudios.xldesk.vst3\">vst3.pkg</pkg-ref><pkg-ref id=\"com.absolutefuturestudios.xldesk.app\">app.pkg</pkg-ref>"
if [ -d "$ART/AAX/XL-Desk.aaxplugin" ]; then
  comp aax "$ART/AAX/XL-Desk.aaxplugin" "/Library/Application Support/Avid/Audio/Plug-Ins"
  CHOICES="$CHOICES<line choice=\"aax\"/>"; DEFS="$DEFS<choice id=\"aax\" title=\"AAX plugin (Pro Tools)\"><pkg-ref id=\"com.absolutefuturestudios.xldesk.aax\"/></choice>"
  REFS="$REFS<pkg-ref id=\"com.absolutefuturestudios.xldesk.aax\">aax.pkg</pkg-ref>"
fi
cat > "$W/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>XL-Desk $VER</title>
  <organization>com.absolutefuturestudios</organization>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>$CHOICES</choices-outline>
  $DEFS
  $REFS
</installer-gui-script>
XML
ARGS=(--distribution "$W/dist.xml" --package-path "$W")
[ -n "$SIGN" ] && ARGS+=(--sign "$SIGN")
productbuild "${ARGS[@]}" "$OUT/XL-Desk-$VER-macOS.pkg"
echo "built $OUT/XL-Desk-$VER-macOS.pkg"
