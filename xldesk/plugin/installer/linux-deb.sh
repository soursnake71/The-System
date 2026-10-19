#!/bin/bash
# Builds xl-desk_<version>_amd64.deb (VST3 -> /usr/lib/vst3, app -> /usr/bin) and a .tar.gz with install.sh.
# Usage: linux-deb.sh <artefacts dir> <version> <out dir>
set -euo pipefail
ART="$1"; VER="$2"; OUT="$3"; W=$(mktemp -d); mkdir -p "$OUT"
mkdir -p "$W/deb/DEBIAN" "$W/deb/usr/lib/vst3" "$W/deb/usr/bin"
cp -R "$ART/VST3/XL-Desk.vst3" "$W/deb/usr/lib/vst3/"
cp "$ART/Standalone/XL-Desk" "$W/deb/usr/bin/xl-desk"
cat > "$W/deb/DEBIAN/control" <<CTRL
Package: xl-desk
Version: $VER
Architecture: amd64
Maintainer: Absolute Future Studios
Depends: libwebkit2gtk-4.1-0, libgtk-3-0, libasound2 | libasound2t64, libfreetype6, libcurl4 | libcurl4t64
Section: sound
Priority: optional
Description: XL-Desk analogue console (VST3 plugin and standalone app)
 A model of the SSL XL-Desk: VHD preamps, 500-series rack, bus compressor, monitoring, MIDI and MMC.
CTRL
dpkg-deb --build --root-owner-group "$W/deb" "$OUT/xl-desk_${VER}_amd64.deb"
mkdir -p "$W/tar/XL-Desk-$VER"; cp -R "$ART/VST3/XL-Desk.vst3" "$ART/Standalone/XL-Desk" "$W/tar/XL-Desk-$VER/"
cat > "$W/tar/XL-Desk-$VER/install.sh" <<'SH'
#!/bin/sh
# installs for the current user: VST3 in ~/.vst3, the app in ~/.local/bin
cd "$(dirname "$0")" && mkdir -p ~/.vst3 ~/.local/bin && rm -rf ~/.vst3/XL-Desk.vst3 && cp -R XL-Desk.vst3 ~/.vst3/ && cp XL-Desk ~/.local/bin/xl-desk && echo "XL-Desk installed: ~/.vst3/XL-Desk.vst3 and ~/.local/bin/xl-desk"
SH
chmod +x "$W/tar/XL-Desk-$VER/install.sh"
tar -C "$W/tar" -czf "$OUT/XL-Desk-$VER-Linux.tar.gz" "XL-Desk-$VER"
echo "built $OUT"
