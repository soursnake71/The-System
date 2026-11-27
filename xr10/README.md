# Omega XR10

A 16-bit PCM drum machine modelled on the Akai XR10's front panel and way of working, with the Yamaha RX5's
easy per-sound editing added. One C++ engine (`core/`) runs both the VST3 plugin and, compiled to WebAssembly,
the Android / web app, so the two sound and behave the same and share memory files.

## What it does
- **The XR10 panel**: the photo is the instrument. 64 sounds, 50 preset rhythms (ROCK 1 … TRIBAL), each with
  VARIATION A/B/C, FILL IN A/B/C, INTRO and ENDING; 49 user patterns (51–99), 20 songs, 4 pad banks of 15 pads,
  ACCENT, TIMING CORRECT, ERASE, EDIT, BREAK/CONTINUE, the 2×16 LCD, the TEMPO/DATA knob, the keypad and the
  Pattern / Song / Sound / Utility modes. 8 voices like the XR10 (up to 32 in Utility), and the effect send.
- **From the RX5**: every sound has its own level, pan, tune, fine, bend (pitch sweep), attack, decay, tone, reverse,
  loop, effect send, choke group and velocity sensitivity, on sliders with the waveform shown; direct instrument keys
  for all 64 sounds; a step grid of the part you're on; a song list you edit step by step; your own samples on any sound.
- Effects: room, hall, plate, gated reverb, ping-pong delay, or "external" (the plugin's FX Send output bus).
- MIDI: General MIDI drum notes (or note 36 = sound 1 / pad 1), program change picks the pattern, start / stop.
  The plugin follows the host's transport and tempo.

## About the sounds
No Akai ROM data is used or included. The 64 sounds are synthesized (`core/sounds.h`) to match the character of the names
on the XR10's panel, then stored like the hardware's ROM (mono, 44.1 kHz, 16-bit). The preset rhythms are original
grooves in the named styles (`core/styles.h`), not transcriptions of the factory patterns. Load your own samples to
replace any sound.

## Files
- `core/xr10.h` the machine (voices, effects, sequencer, LCD menus), `core/sounds.h` the sounds, `core/styles.h` the
  rhythms, `core/xmath.h` math without the C library, `core/wasm.cpp` the WebAssembly exports.
- `index.html`, `app.js`, `worklet.js`, `xr10.wasm` the app. `tools/build-wasm.sh` rebuilds `xr10.wasm` (clang + wasm-ld).
- `../plugins/xr10/` the VST3 / AU / standalone. `../android/app/src/xr10/` the Android app (the `xr10` flavor).

## Tests
- `g++ -O2 -std=c++17 xr10/tools/test.cpp -o xr10_test && ./xr10_test <dir>`: sounds, presets, fills, endings, recording,
  songs and state (writes WAVs to listen to).
- `NODE_PATH=$(npm root -g) node xr10/tools/ui-test.mjs <dir>`: the app in headless Chromium.
- `plugins/tools/hosttest/xr10.cpp`: loads the built .vst3 like a DAW. `xr10_shot` (in `plugins/`) saves pictures of the window.
