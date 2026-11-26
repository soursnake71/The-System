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
| RX5 (RX5-style drum machine, also a standalone app) | instrument with MIDI in / out, 12 optional individual outputs |

Download: the `vst3-latest` release has a zip for Windows, Mac and Linux, built by `.github/workflows/vst3.yml`.

Build yourself: `cmake -S plugins -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`.
Parameter tables come from the app: `node plugins/tools/gen.mjs` regenerates `gen/tables.inc`.

Omega Twelve lives in `twelve/` and doesn't use the shared Spec/Editor: it has its own sample editor, presets (generated from the
app by `node twelve/tools/gen.mjs`), master chain and file export. Tests: `twelve_test` + `node twelve/tools/compare.mjs`
(C++ against the app's engine), `twelve_proc_test` (sampler, MIDI, exports, analysis, state), and `tools/hosttest` (`twelve_host_test`
loads the built .vst3 like a DAW).

RX5 lives in `five/` (its own release: `.github/workflows/five-release.yml`, built with `-DOMEGA_ONLY=Five`). `Five.h` is the engine
(a line-for-line port of `five/engine.js`), `FiveSeq.h` / `FiveSysex.h` / `FiveCart.h` the sequencer, SysEx and cartridge (ports of
`seq.js`, `rxsysex.js`, `rxcart.js`), `FivePlugin` / `FiveEditor` the processor and the faceplate (the menus of `five/app.js`). The
factory kit (`five/kit/*.wav` and the library table in `kit.json`) is compiled in. Tests: `five_test` + `node five/tools/compare.mjs`
(engine, sample by sample), `five_seq_test` + `node five/tools/interop.mjs` (cartridges, randomize, SMF, SysEx, RX5 banks, the Player's
events), and `five_proc_test` (pads, MIDI, sequencer, host sync, SysEx replies, individual outputs, automation, every file job, state,
the panel; writes a picture of it).
