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
| Omega Twelve (12-bit sampler emulator, also a standalone app) | effect with MIDI in: live input through the sampler, samples played from MIDI |
| Omega Twelve Instrument | instrument (same, for hosts that only send MIDI to instruments) |
| Omega 110 (DR-110 circuit model, the DAW is the sequencer, 100 presets) | instrument, stereo mix + BD / SD / CP / HH-CY outputs |
| Omega Traks (Drumtraks circuit model with its whole front panel and sequencer, follows the DAW, 100 presets) | instrument (and standalone), stereo mix + CH1-CH6 outputs |

Download: the `vst3-latest` release has a zip for Windows, Mac and Linux, built by `.github/workflows/vst3.yml`.

Build yourself: `cmake -S plugins -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`.
Parameter tables come from the app: `node plugins/tools/gen.mjs` regenerates `gen/tables.inc`.

Omega Twelve lives in `twelve/` and doesn't use the shared Spec/Editor: it has its own sample editor, presets (generated from the
app by `node twelve/tools/gen.mjs`), master chain and file export. Tests: `twelve_test` + `node twelve/tools/compare.mjs`
(C++ against the app's engine), `twelve_proc_test` (sampler, MIDI, exports, analysis, state), and `tools/hosttest` (`twelve_host_test`
loads the built .vst3 like a DAW).

Omega 110 lives in `dr110/` (DR110.h is a port of `dr110/engine.js`; Presets.inc comes from `dr110/presets.js` via `node dr110/tools/gen-presets.mjs`). Tests: `dr110_test` + `node dr110/tools/compare.mjs`, and `dr110_host_test` in `tools/hosttest` (loads the .vst3, plays MIDI, checks every output, saves a picture of the editor).

Omega Traks lives in `traks/` (Traks.h and Machine.h are ports of `traks/engine.js` and `traks/machine.js`; it uses the 110's reverb, delay and filters from DR110.h; Presets.inc comes from `traks/presets.js` via `node traks/tools/gen-presets.mjs`; the ROM `traks/rom/traks.rom` from `node traks/tools/make-rom.mjs`). Tests: `traks_test` + `node traks/tools/compare.mjs`, `traks_ui_test` (pictures of the editor), and `traks_host_test` in `tools/hosttest`.
