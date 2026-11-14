#!/bin/bash
# Omega DMX package for Debian / Ubuntu: the VST3 in /usr/lib/vst3, the standalone app in /usr/bin with a menu entry.
# usage: make-deb-dmx.sh <folder with "Omega DMX.vst3" and the "Omega DMX" binary> <version> <icon.png> <output.deb>
set -euo pipefail
src="$1"; ver="$2"; icon="$3"; out="$4"; root="$(mktemp -d)/omega-dmx"
mkdir -p "$root/DEBIAN" "$root/usr/lib/vst3" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/icons/hicolor/512x512/apps"
cp -R "$src"/*.vst3 "$root/usr/lib/vst3/"
install -m 755 "$src/Omega DMX" "$root/usr/bin/omega-dmx"
install -m 644 "$icon" "$root/usr/share/icons/hicolor/512x512/apps/omega-dmx.png"
cat > "$root/usr/share/applications/omega-dmx.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Omega DMX
Comment=Digital drum machine
Exec=omega-dmx
Icon=omega-dmx
Categories=AudioVideo;Audio;Music;
DESK
cat > "$root/DEBIAN/control" <<CTRL
Package: omega-dmx
Version: $ver
Section: sound
Priority: optional
Architecture: amd64
Maintainer: The Omega System <noreply@github.com>
Depends: libasound2 | libasound2t64, libfreetype6, libfontconfig1, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1
Description: Omega DMX digital drum machine
 VST3 instrument plus a standalone app: DMX recordings and three more kits,
 ten patterns per kit, song mode, swing, live record, host sync and MIDI.
CTRL
find "$root" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group --build "$root" "$out"
echo "made $out"
