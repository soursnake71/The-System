# Omega plugins

The Omega System's instruments and effects as separate VST3 plugins (AU too on Mac), for any desktop DAW.
The sound engines are line-for-line C++ ports of the web app's own code (`index.html`); `tools/compare.mjs` runs the
app's code in Node and the C++ in `tools/dsp_test.cpp` on the same signal, and their levels match.

| Plugin | Type |
|---|---|
| Omega Prophet-5 | instrument |
| Omega Vox (Prophet-5 + vocoder + talking machine) | instrument |
| Omega Speech (talking machine) | instrument |
| Omega Vocoder 350 | effect (MIDI or sidechain carrier) |
| Omega Lexicon 224, PrimeTime, Effectron, DP4, Microverb, ADA Flanger, 1176, Phil Collins Sim, H910, MXR 113 | effects |
| Omega Bus Compressor, Resonance Tamer, Gate Stepper | effects |
| SPQuencer1200 | instrument with MIDI out: the SP-1200 sequencer (`../spquencer`) in a web view, played with the DAW |

Download: the `vst3-latest` release has a zip for Windows, Mac and Linux, and `SPQuencer1200-Setup-<version>.exe`, a Windows
installer that puts SPQuencer1200.vst3 in `C:\Program Files\Common Files\VST3` (built from `installer/SPQuencer1200.iss`
by `.github/workflows/vst3.yml`; it is not code-signed, so SmartScreen asks once: More info, then Run anyway).
For Mac there is `SPQuencer1200-<version>.pkg` (from `installer/make-mac-pkg.sh`), which installs the VST3 into
`/Library/Audio/Plug-Ins/VST3` and the Audio Unit into `/Library/Audio/Plug-Ins/Components`. It is not signed or notarized
with an Apple Developer ID, so the first time macOS blocks it: open System Settings > Privacy & Security and click
**Open Anyway** next to the SPQuencer1200 message (on macOS 14 and older, right-click the .pkg and choose Open).

Build yourself: `cmake -S plugins -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`.
Parameter tables come from the app: `node plugins/tools/gen.mjs` regenerates `gen/tables.inc`.

## SPQuencer1200
The editor is the `spquencer` web page, built into the plugin and shown in a web view (WebView2 on Windows, WKWebView on
Mac, WebKitGTK on Linux); `spquencer/Engine.h` plays it in time with the DAW. In Ableton Live: put SPQuencer1200 on a
MIDI track, then on a second MIDI track set **MIDI From** to that track / SPQuencer1200 and **MIDI To** the interface
port your SP-1200 is on, with monitoring In. Set the SP channel and notes in the plugin's MIDI / Hardware tab.
`tools/hosttest/spq_test.cpp` loads the built plugin like a host and checks the MIDI timing in each sync mode.
Linux builds need `libwebkit2gtk-4.1-dev libgtk-3-dev`; Windows builds need the Microsoft.Web.WebView2 NuGet package (see the workflow).
