#!/bin/bash
# Omega REV7 source zip: everything needed to build the plugin (the shared plugin tree, the REV7 files, the tests,
# the installers) from the git checkout, with build steps on top.
# usage: make-rev7-source.sh <output.zip>     (run from anywhere inside the repository)
set -euo pipefail
out="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; top="$(git rev-parse --show-toplevel)"; work="$(mktemp -d)"; dir="$work/omega-rev7-source"
mkdir -p "$dir"
git -C "$top" archive --format=tar HEAD plugins | tar -x -C "$dir"
cat > "$dir/BUILD.md" <<'MD'
# Omega REV7: building from source

Needs CMake 3.22+ and a C++17 compiler. JUCE 8.0.4 is downloaded on the first configure.
Linux also needs: `libasound2-dev libx11-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev`.

    cmake -S plugins -B build -DCMAKE_BUILD_TYPE=Release -DOMEGA_ONLY=Rev7
    cmake --build build --config Release --target OmegaRev7_VST3 rev7_test
    build/rev7_test                      # renders and checks every preset

The plugin lands in `build/OmegaRev7_artefacts/Release/VST3/Omega REV7.vst3`. Copy it to your VST3 folder:
Windows `C:\Program Files\Common Files\VST3`, Mac `/Library/Audio/Plug-Ins/VST3`, Linux `~/.vst3`.

Where things are: `plugins/dsp/Rev7.h` (engine), `plugins/src/Rev7.cpp` (parameters, factory and artist presets),
`plugins/src/Rev7Library.inc` (the 100-preset library), `plugins/src/Rev7Editor.cpp` (front panel and dice),
`plugins/README.md` (how the model works), `plugins/installer/` (installer scripts).
MD
rm -f "$out"; (cd "$work" && zip -qr "$out" omega-rev7-source)
echo "made $out"
