# Omega Synsonics

A circuit model of the **Mattel Synsonics Drums** (model 5281, 1981), stock or with the
[Circuitbenders](https://www.circuitbenders.co.uk/newsarchive/synsonics.html) mods, with a step sequencer, a mixer,
a REV7-style reverb and a delay, and WAV / MP3 / MIDI import and export. Both versions are in one app: the
**STOCK / BENT** switch on the DRUMS page changes between them. Everything is C++ and shared:

| Part | What it is |
|---|---|
| `core/Circuit.h` | the voices, part by part from the schematic |
| `core/Engine.h` | the engine: voices, U1's sequencer (rate buttons, memories), 16 patterns and the song, the mixer, the sample pad, state |
| `core/Fx.h` | the REV7 model and the tempo-synced delay |
| `core/Files.h`, `core/Files.cpp` | WAV / MP3 / MIDI export (rendered offline) and import |
| `core/Canvas.h`, `core/Panel.h` | the five pages, drawn by a small software renderer, with multitouch input |
| `plugin/` | VST3 / AU instrument and standalone app (JUCE), with desktop file dialogs |
| `android/` | Android app: a NativeActivity with AAudio; one small Java class opens the system file picker |
| `third_party/` | minimp3 (MP3 decoding, public domain) and shine (MP3 encoding, LGPL 2) |
| `tests/` | `syn_engine_test` (every voice, both modes, the hardware sequencer), `syn_features_test` (grid, song, swing, mixer, REV7, delay, sample pad, files, state), `syn_panel_snapshot` (images of every page) |

Download: the `synsonics-latest` release (`omega-synsonics.apk` and a zip per desktop OS), built by
`.github/workflows/synsonics.yml`.

Build it yourself:
- Plugin: `cmake -S synsonics -B build-syn -DCMAKE_BUILD_TYPE=Release && cmake --build build-syn --config Release`
- APK: `gradle -p synsonics/android assembleRelease` (Android SDK with NDK 27)
- Tests only, no JUCE: `cmake -S synsonics -B build-t -DSYN_PLUGIN=OFF && cmake --build build-t && build-t/syn_engine_test . && build-t/syn_features_test .`

## Pages
Five tabs across the top: **DRUMS** (the unit), **GRID**, **MIXER**, **FX**, **FILE**.

**GRID: sequencer and grid editor.**
- 16 patterns. Patterns 1 to 3 are the front panel's three memories, so whatever you record with RECORD on the
  DRUMS page shows up here, and the other way round.
- Rows: tom 1, snare, cymbal, ACCENT (that step's cymbal plays as a closed hi-hat), tom 2, bass, and the sample pad.
  Tap a cell to set or clear it, and drag to paint. Tap a row name to hear that voice.
- Length goes up to 340 steps (the memory size of the real unit), changed by a step or a bar at a time. PAGE moves
  through it 16 steps at a time.
- COPY / PASTE / CLEAR work on whole patterns.
- PLAY, STOP, REC (overdub while playing), tempo, and swing (up to 40%).
- Picking another pattern while one plays queues it for the end of the current one (amber marker).
- SONG: tap a slot to put the selected pattern there; up to 48 slots on screen. With SONG on, PLAY plays the chain.

**MIXER.**
- A strip per voice (tom 1, snare, cymbal, tom 2, bass, sample): REV7 send, delay send, pan, mute, solo, fader,
  meter. Then the two effect returns and the master.
- The mixer takes each voice where it enters the U4 summing amps. Its defaults are the stock placement (tom 1 and
  snare left, cymbal and tom 2 right, bass centre) with the unit's 20% blend, so untouched it sounds exactly like the
  machine. Pushing faders still drives the U4 and LM386 stages.
- Sends are post-fader. Double-tap any knob or fader to reset it.

**FX: REV7 and delay.**
- REV7: 30 programs (PROG -/+). The display shows the number and the LCD the name, algorithm and reverb time.
  Knobs: reverb time (0.3 to 9.9 s), high and low decay ratios, initial delay and reverb delay (0 to 99.9 ms),
  diffusion (1 to 10), liveness, room size, a 3-band EQ on the effect, and the return.
- Delay: note values from 1/16 to 1/2, dotted and triplet included, synced to the tempo. Feedback, damping,
  ping-pong, return.
- A send matrix repeats every voice's two sends in one place.

**FILE: import and export.**
- Export the selected pattern or the song, 1, 2 or 4 times through:
  - **WAV**: 16-bit with dither, or 24-bit.
  - **MP3**: 256 kbps, 44.1 kHz.
  - **MIDI**: General MIDI drums on channel 10 at 96 PPQ, with the tempo. The sample pad is note C4 (60).
- WAV and MP3 are rendered offline through the whole circuit, mixer and effects, including the reverb tail.
- **Import MIDI**: drum notes go into the selected pattern, quantized to 16ths, and the file's tempo is taken.
  Files longer than 340 steps become 4-bar patterns from the selected one on, chained as the song.
- **Import WAV or MP3**: loads it onto the sample pad, with a tune control (±12 semitones). It plays from its grid
  row, its mixer strip and MIDI C4.
- On Android the system file picker opens (Downloads, Drive and so on). In the plugin it's a normal file dialog.
  Exports run in the background.

## Playing it
- **Pads**: tom tom 1 (top left), cymbal (top right), snare (bottom left), tom tom 2 (bottom right). Their LEDs light
  when each one sounds; the centre LED blinks the tempo.
- **ACCENT** (hold): closes the cymbal into a hi-hat (Q2 puts R32 across the cymbal's envelope cap).
- **Rate buttons** (fast, medium, slow in each group): repeat that drum on 16ths, 8ths or quarters at the tempo.
  Press again to stop it.
- **BASS ON/OFF**: the bass click on every beat. **SLOWER / FASTER**: tap for 1 BPM, hold to sweep (40 to 260 BPM).
- **RECORD**, then a **CYMBAL/MEMORY** button picks memory 1, 2 or 3; recording starts on the next beat. Play the pads or
  rate buttons (quantized to 16ths). **STOP** ends it and sets the loop length (whole beats, up to 85).
  **PLAYBACK**, then a memory button, loops it. **RECORD** while it plays adds to it (overdub). **STOP** also
  turns off every rate button.
- **Thumbwheels** at the top: tom tom 1 tune and volume (both on the side of the real unit). Drag sideways;
  double-tap a wheel or knob to reset it.
- **BENT** mode: hi-hat decay, hi-hat open/closed, snare decay, tom 1 decay, tom 1 click, tom 1 level/overdrive,
  tom 1 boing switch, tom 2 pitch, tom 2 decay, tom 2 level/overdrive. With the knobs at their default positions the
  bent unit sounds exactly like the stock one.
- **Trigger inputs** (plugin): MIDI notes. 35/36 bass, 37-40 snare, 42/44 closed hat, 46/49/51 cymbal, 41/43/45 tom 2,
  47/48/50 tom 1. Notes work in both modes.
- In the plugin, **Host Sync** (on by default) runs the clock at the host's tempo, lined up with its beats, while the host
  plays.

The real machine forgets its memories at power-off; this one keeps them, with the patterns, song, mixer, effects
and the sample pad (in the plugin's saved state, and in the app's storage on Android).

## How the model works
Sources: the service manual's schematic (Figure 4), its "Technical Description" of drum sound generator U3, and the
waveform charts for test points 1 to 40, all as cleaned up by Richard C.
([sdiy.org/richardc64/synsonic](http://www.sdiy.org/richardc64/synsonic/docs.html)). Part numbers below are the
schematic's.

**Noise.** U7 is an MM5837: a 17-stage shift register with feedback from stages 14 and 17, run here at about 110 kHz.
Its output (TP29, 5 V p-p) drives the gates of both JFETs.

**Cymbal and snare.** U3 pulses its envelope pin (pin 7 cymbal, pin 18 snare) to about 3 V for the length of the
trigger: 3.2 ms from the pads, 12 ms from memory playback (chart 20-23), so sequenced hits are a little fuller, as on
the real unit. The pin charges C6 10 µF (cymbal) or C9 2.2 µF (snare). The cap discharges through R33 8.2 kΩ / R35
12 kΩ into the drain of a 2N5638 (Q3 / Q4), whose source is grounded and whose gate gets the noise. The JFET is
simulated as a voltage-controlled resistor (pinch-off −3 V, 30 Ω on). It chops the drain voltage with the noise, and
the decay depends on how often it conducts, as in the circuit. The cymbal leaves the drain through C7 100 pF into
R34 120 kΩ, a 13 kHz high-pass, which is why it is "barely shaped white noise". The snare leaves through R36 390 kΩ,
plus the impact network C10 .01 µF / R37 120 kΩ, which differentiates the envelope into a click. With ACCENT, Q2
switches R32 5.6 kΩ across C6: the closed hi-hat.

**Tom toms.** U3 has a VCO and a VCA for each tom. C4 .1 µF and C5 .68 µF set the VCO range, and R30 tunes tom 1
(140 to 450 Hz). Tom 2 sits at 118 Hz. The VCA opens with the envelope on pin 5 / pin 14. The burst at TP32/TP34
dies well before that envelope does (charts 32-35), so the VCA opens only above about 1 V. Tom 1 has the "sweep-swoop":
its pitch goes up during the strike, then falls back. The envelope pin also drives an emitter follower (Q7 / Q8, with
C12 / C14 1 µF on the emitter), and C11 / C13 .01 µF into R39 / R43 220 kΩ differentiates it into the attack click.
That network is simulated node by node, so fast repeats give smaller clicks, as in the circuit. The tone leaves through
R38 / R42 220 kΩ.

**Bass.** U1 pulls TP36 down through R26 1 kΩ and D24, R27 120 kΩ / C30 .1 µF bring it back up, and C15 .1 µF
couples that step into R45 and R46 (220 kΩ to each channel). The result is the famous click (chart 36).

**Mix and outputs.** U4A/B are inverting summers (20 kΩ ∥ 470 pF, about 17 kHz), with snare and tom 1 on the left,
cymbal and tom 2 on the right, and bass on both. They clip at the ±9 V converter's rails. The blend matrix
R48/R50/R51/R52 sends about 20% of each side to the other. Then come the volume pot (R49), the C25/C17 coupling into
the LM386s (gain 20, soft clip on 9 V) and the R57/R55 line-out divider. The model runs at 2× oversampling below
60 kHz.

**U3 and U1 are custom chips** with no published internals. The model's choices for them are marked "U3 model" in the
code: the VCO waveform (a rounded triangle), the VCA threshold, the tom pitch sweep and its time constants, and the
source resistance of the envelope pins. U1's sequencer is likewise reconstructed from the panel and the manual's
notes, not from its firmware.

**Circuitbenders mods**, as listed on their page: the hi-hat decay replaces R32 (220 Ω to 68 kΩ). The snare decay
sets C9's capacitance (0.33 to 22 µF), and the tom decays set the envelope time. Tom 1 click scales the C11/R39 path
(0 to 4×). Level/overdrive lowers R38 / R42, so the toms drive U4 and the LM386 into clipping; all the distortion
comes from the circuit itself, as in their demos. The tom 1 boing switch lengthens the pitch sweep's decay, and tom 2
pitch spans 28 to 420 Hz (kick territory, which is how Circuitbenders use it).

## The REV7 model
What is documented about the Yamaha REV7 (1983) is followed. The sources are the [Sound On Sound review, November
1985](https://www.muzines.co.uk/articles/yamaha-rev-7-digital-reverberator/1195) and
[Yamaha Black Boxes](https://yamahablackboxes.com/collection/yamaha-rev7-reverb/).
- The input is summed to mono into one converter, and the two outputs carry slightly different reverb patterns.
- Reverb time is 0.3 to 9.9 s on the mid band, with separate high- and low-frequency decay.
- Initial delay and first reflection each run 0 to 99.9 ms. There are 10 diffusion steps, plus liveness and room size.
- The reverb bandwidth is 20 Hz to 12 kHz, and the 3-band EQ works on the effect only.
- The reverb's 78 dB dynamic range is modelled as a coarse output step, which gives long tails their grain.

The REV7's algorithms and factory program list aren't published (the one page that may list the programs wouldn't
load). So the engine is a model built on those specs:
- early-reflection taps,
- input diffusion,
- an 8-line feedback delay network with three-band decay.

The 30 programs are this app's own, in the families the reviews describe: halls, rooms, vocal, plates, early
reflections, gated, reverse, delays and flange/phase.
