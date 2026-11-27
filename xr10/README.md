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

## The circuit model
Modelled from the Akai XR10 service manual (main schematic L400802M, block diagram L400851M, parts list, specifications),
archive.org item `akai-xr-10-service-manual`:
- **Voices and digital mixer** (`core/xr10.h`): 8 voices fetched by DMA from 16-bit ROM at the machine rate with no
  interpolation; the TMW4008 gate array's digital envelope (hold, decay), 5-bit level / pan (L15–R15) / effect send,
  pitch in 100-cent steps (±25) and 6.25-cent fine steps (−8..+7), sweep (pitch envelope: decay, depth, up/down), reverse,
  velocity feel; the mixed L, R and EFFECT signals quantized to 16 bits for the DACs.
- **Output stage** (`core/analog.h`): LC7881 16-bit DACs as a zero-order hold, M5223L followers, the 4.7 k / 680 p /
  BL-30VF / 2.7 n / 2.7 n LC low-pass (fc 12 kHz), the C69 / R3 coupling, the M5216L inverting amps (330 k ∥ 47 p over 47 k,
  ×7) with VOLUME (VR2) across the feedback resistor and the EFFECT VR (VR1 100 k A) in series with it, op-amp clipping on
  the 12 V supply, and the 1.2 k / 4.7 n / 47 k output network. Run at 4× the host rate, then decimated. Calibrated so a
  full-scale tone gives the manual's 8.2 dBm at the outputs.
- **Estimated** (not printed in the manual): the machine's sample rate (31.25 kHz = 12 MHz / 384, from the clock and the
  12 kHz filters), the inductances inside the BL-30VF (solved for its printed 12 kHz), VR2's value and taper, the times
  behind the 0–31 envelope and sweep steps, the mixer's headroom, and the pan law. Recordings of a real XR10 would pin these down.
- Extras that aren't on the XR10 are marked: attack, tone, loop and choke group per sound, and the internal reverb / delay
  (the hardware sends to an outboard effect from its EFFECT jack, which is the plugin's FX Send bus).

## About the sounds
The sound ROM is `rom.bin`, built from the files in `samples/` (see `samples/README.md`) by `tools/make-rom.cpp`; the release
workflow rebuilds it whenever samples are pushed. Any sound without a sample is synthesized (`core/sounds.h`) to match the
character of its name, and stored like the hardware's ROM (mono, 44.1 kHz, 16-bit). The preset rhythms are original
grooves in the named styles (`core/styles.h`), not transcriptions of the factory patterns. Load your own samples to
replace any sound.

## MIDI files
Import a drum MIDI file (type 0 or 1, General MIDI drum notes, channel 10 if the file uses it) into the current part, up to
8 bars, with its tempo and time signature; export the part or the whole song as type-0 MIDI on channel 10. The notes follow
the MIDI note map in Setup; with General MIDI, the sounds that have no GM note of their own use 24–33 (kicks), 88–95
(snares 3–10), 96–104 (toms 2–4) and 105 (hits), so every sound comes back as itself. In the plugin you can also drag the
part or song straight onto a DAW track, and drop .mid files on the window.

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
