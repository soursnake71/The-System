# Omega Synsonics

A circuit model of the **Mattel Synsonics Drums** (model 5281, 1981), stock or with the
[Circuitbenders](https://www.circuitbenders.co.uk/newsarchive/synsonics.html) mods. Both versions are in one app: the
**STOCK / BENT** switch at the top changes between them. Everything is C++ and shared:

| Part | What it is |
|---|---|
| `core/Engine.h` | the circuit model, U1's sequencer (rate buttons, three memories), state |
| `core/Canvas.h`, `core/Panel.h` | the front panel, drawn by a small software renderer, with multitouch input |
| `plugin/` | VST3 / AU instrument and standalone app (JUCE) |
| `android/` | Android app: a NativeActivity with AAudio, no Java |
| `tests/` | `syn_engine_test` (every voice, both modes, the sequencer, state) and `syn_panel_snapshot` (panel images) |

Download: the `synsonics-latest` release (`omega-synsonics.apk` and a zip per desktop OS), built by
`.github/workflows/synsonics.yml`.

Build it yourself:
- Plugin: `cmake -S synsonics -B build-syn -DCMAKE_BUILD_TYPE=Release && cmake --build build-syn --config Release`
- APK: `gradle -p synsonics/android assembleRelease` (Android SDK with NDK 27)
- Tests only, no JUCE: `cmake -S synsonics -B build-t -DSYN_PLUGIN=OFF && cmake --build build-t && build-t/syn_engine_test .`

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

The real machine forgets its memories at power-off; this one keeps them (in the plugin's saved state, and in the
app's storage on Android).

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
