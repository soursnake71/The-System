#!/bin/bash
# Omega Twelve package for Debian / Ubuntu: VST3s in /usr/lib/vst3, the standalone app in /usr/bin with a menu entry.
# usage: make-deb.sh <folder with the built .vst3 bundles and the "Omega Twelve" binary> <version> <icon.png> <output.deb>
set -euo pipefail
src="$1"; ver="$2"; icon="$3"; out="$4"; root="$(mktemp -d)/omega-twelve"
mkdir -p "$root/DEBIAN" "$root/usr/lib/vst3" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/icons/hicolor/512x512/apps"
cp -R "$src"/*.vst3 "$root/usr/lib/vst3/"
install -m 755 "$src/Omega Twelve" "$root/usr/bin/omega-twelve"
install -m 644 "$icon" "$root/usr/share/icons/hicolor/512x512/apps/omega-twelve.png"
cat > "$root/usr/share/applications/omega-twelve.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Omega Twelve
Comment=12-bit sampler emulator (SP-1200 circuit model)
Exec=omega-twelve
Icon=omega-twelve
Categories=AudioVideo;Audio;Music;
DESK
cat > "$root/DEBIAN/control" <<CTRL
Package: omega-twelve
Version: $ver
Section: sound
Priority: optional
Architecture: amd64
Maintainer: The Omega System <noreply@github.com>
Depends: libasound2 | libasound2t64, libfreetype6, libfontconfig1, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1
Description: Omega Twelve 12-bit sampler emulator
 VST3 effect and instrument plus a standalone app: the E-mu SP-1200 / SP-12 audio path
 simulated at circuit level, 500 presets, lo-fi, timestretch, mastering and export.
CTRL
find "$root" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group --build "$root" "$out"
echo "made $out"
