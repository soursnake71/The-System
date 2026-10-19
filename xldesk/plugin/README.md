# XL-Desk VST3

The XL-Desk console as a VST3 plugin (plus a Standalone app) for Windows, macOS and Linux. The interface is the same page
as the web version (`../index.html`, shown in a WebView); the DSP is a C++ port of `../dsp/console.js` that matches it to
0.01 dB (`tests/engine_test.cpp`).

## Download and install
Every push builds, tests and packages it on GitHub Actions and publishes the **xldesk-latest** release:
- `XL-Desk-<ver>-Windows-Setup.exe`: installs the VST3 (`C:\Program Files\Common Files\VST3`) and the Standalone app.
- `XL-Desk-<ver>-macOS.pkg`: universal (Apple Silicon + Intel); VST3 to `/Library/Audio/Plug-Ins/VST3`, app to `/Applications`.
  Not notarised yet: right-click the .pkg and choose Open the first time.
- `xl-desk_<ver>_amd64.deb` / `XL-Desk-<ver>-Linux.tar.gz`: VST3 to `/usr/lib/vst3` (or `~/.vst3` via `install.sh`).
- `XL-Desk-<ver>-source.zip`: all the code.

## AAX (Pro Tools)
The build makes an AAX as well as soon as it has Avid's AAX SDK, which Avid only gives to registered developers
(developer.avid.com). Locally: `-DXL_AAX_SDK=/path/to/aax-sdk`. On GitHub: add a repository secret `AAX_SDK_URL` with a
download link to your SDK zip; the installers then include the AAX. Retail Pro Tools only loads AAX plugins signed with
PACE's wraptool (free through Avid, needs an iLok and a PACE account): run `installer/sign-aax.sh` on your machine.
Unsigned AAX builds load in Pro Tools Developer (ask devauth@avid.com for a free activation).

## Signing (optional)
Without paid certificates, Windows SmartScreen and macOS Gatekeeper warn on first install. With an Apple Developer ID,
pass its "Developer ID Installer" identity as the 4th argument of `installer/macos-pkg.sh` and notarise; on Windows,
sign the setup .exe with your Authenticode certificate (`signtool sign`).

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
