#!/bin/bash
# Omega REV7 package for Debian / Ubuntu: the VST3 in /usr/lib/vst3.
# usage: make-rev7-deb.sh <folder with the built "Omega REV7.vst3"> <version> <output.deb>
set -euo pipefail
src="$1"; ver="$2"; out="$3"; root="$(mktemp -d)/omega-rev7"
mkdir -p "$root/DEBIAN" "$root/usr/lib/vst3"
cp -R "$src/Omega REV7.vst3" "$root/usr/lib/vst3/"
cat > "$root/DEBIAN/control" <<CTRL
Package: omega-rev7
Version: $ver
Section: sound
Priority: optional
Architecture: amd64
Maintainer: The Omega System <noreply@github.com>
Depends: libasound2 | libasound2t64, libfreetype6, libfontconfig1, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1
Description: Omega REV7 digital reverberator (VST3)
 A model of the Yamaha REV7 (1985): the 31.25 kHz / 16-bit converter path from the
 service manual, every program type, 144 presets, and a front panel like the unit's.
CTRL
find "$root" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group --build "$root" "$out"
echo "made $out"
