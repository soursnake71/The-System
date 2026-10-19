# XL-Desk VST3

The XL-Desk console as a VST3 plugin (plus a Standalone app) for Windows, macOS and Linux. The interface is the same page
as the web version (`../index.html`, shown in a WebView); the DSP is a C++ port of `../dsp/console.js` that matches it to
0.01 dB (`tests/engine_test.cpp`).

## Download
GitHub Actions builds every push (artifacts on the workflow run); pushes to `main` also publish the
**xldesk-vst3-latest** release. Unzip `XL-Desk.vst3` into your VST3 folder:
- Windows: `C:\Program Files\Common Files\VST3`
- macOS: `~/Library/Audio/Plug-Ins/VST3` (then `xattr -dr com.apple.quarantine XL-Desk.vst3`, the build is ad-hoc signed)
- Linux: `~/.vst3`

## Build
Needs CMake 3.22+, a C++17 compiler and Node (only to regenerate the spec header). JUCE 8.0.9 is fetched automatically.
```
cmake -S xldesk/plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```
- Windows: install the WebView2 SDK first, e.g. `nuget install Microsoft.Web.WebView2 -OutputDirectory C:\webview2`, and add `-DJUCE_WEBVIEW2_PACKAGE_LOCATION=C:/webview2`.
- Linux: `libasound2-dev libwebkit2gtk-4.1-dev libgtk-3-dev libcurl4-openssl-dev` and the usual X11 dev packages.
  `cmake/juce-linux-webview-fix.cmake` patches two lines of JUCE 8.0.9's Linux WebView helper, which otherwise drops
  messages bigger than 64 KB and crashes when the page loads.

## One spec for both versions
`../spec.js` lists every control, its range and default, every 500 module and the default MIDI map.
`node tools/gen-spec.js` turns it into `Source/Spec.generated.h`; CI fails if the header is out of date. Parameter order
is the VST3 parameter order: only ever append to it, or saved sessions will load into the wrong controls.

## Adding a 500 module or a new revision
1. Describe it in `XL.MOD` in `../spec.js` with a new type id (for a revision, e.g. `eeq2`, `rev: 2`), and list it in `XL.WHERE`.
2. Write its DSP: a class in `../dsp/console.js` (add to `MODULES`) and a `Module` subclass in `Source/Modules.cpp` (add one line to `buildRegistry()`).
3. `node tools/gen-spec.js`. The faceplate draws itself from the module's `layout`, and its controls use the slot's 16 generic parameters.
Old sessions keep their type id, so the earlier revision keeps loading as it was.
