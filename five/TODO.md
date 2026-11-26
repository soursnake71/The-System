# RX5: status

Done (this file used to be the handoff list):

1. `node five/tools/test.mjs` is green (engine, seq, rx, kit; the kit test also checks `kit/kit.json`'s library table).
2. Plugin (`plugins/five/`): `Five.h` mirrors engine.js (samples only, `FACTORY_MAP`, `rec` path, `REC_EQ`, output stage) and
   renders bit-identical output (`node five/tools/compare.mjs <build>/five_test <dir>`). `FivePlugin` / `FiveEditor` are the
   processor and the faceplate (fixed aspect, resizable) with the app's menus; the factory kit is compiled in
   (`juce_add_binary_data`). `FiveSeq.h` / `FiveSysex.h` / `FiveCart.h` match the JS (`node five/tools/interop.mjs
   <build>/five_seq_test_artefacts/Release/five_seq_test <dir>`), and `five_proc_test` drives the processor and panel.
   `plugins/tools/hosttest/five.cpp` loads the built .vst3 like a DAW.
3. `.github/workflows/five-release.yml` builds OmegaFive and runs the three test programs on Linux.
4. Small gaps closed: the cartridge saves the kit library (`library`, PCM like `samples`) and the per-voice waveform picks
   (`state.wave`, was UI-only `U.wave`); key assign OUTPUT CH applies to sequenced and MIDI notes too (engine `trigger(.., ch)`,
   seq.js `outChannel`); LOAD KIT can pick a folder (CART 25, NO toggles files / folder); Bitwig pad GAIN is applied to the
   pad's sample (`loadKit(.., {padGain:false})` to skip).

5. Screens like Omega 110 (and its CLAUDE.md screen rules): a rail of page keys (FOLD / EXPAND, MACHINE, VOICE, KEYS,
   PATTERN, SONG, MIXER, FX, MIDI, FILES, ZOOM) beside the machine; each page shows that group's LCD commands big, on pages
   instead of scrolling; ZOOM is a loupe over the whole faceplate (tap inside it to use what it shows). App and plugin.
   The app is checked at the S23 sizes in both orientations (no overflow, every page, fold, the loupe).

Known differences between the plugin and the app:
- The plugin's LOAD KIT reads a folder, .zip or WAV/AIFF files, not .bwpreset.
- Key assign LEVEL / ATTACK / DECAY offsets and MULTI STEP level/attack/decay apply in the app only; the plugin applies the
  key's voice, pitch and output channel (MULTI STEP pitch too).
- EG VELOCITY (MIDI 06) is stored but not applied in either.
