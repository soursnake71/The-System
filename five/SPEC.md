# Omega Five — build spec (shared contract for every part)

A 12-bit PCM digital rhythm programmer in the style of the late-80s Yamaha RX5 (the faceplate photo in the
session is the layout reference). Web app in `five/`, Android APK flavor `five`, VST3/AU/Standalone plugin in
`plugins/five/`. Product name **Omega Five** everywhere (no third-party logos; model names like "RX5", "REV7",
"TC 2290" may appear only as "-style" descriptions in docs/presets). **No factory samples are shipped**: every
voice is synthesized by circuit/physical models, then run through a model of the RX5-style 12-bit
companded playback path. Users may load their own WAV/AIFF into any voice ("voice cartridge").

## Files and owners
| file | what |
|---|---|
| `five/engine.js` | DSP: voice models, 12-bit playback path, EG, mixer, REV7-style reverb, TC-style delay. Pure JS, no DOM. UMD-ish: `export` ES module. |
| `five/seq.js` | Data model: patterns, songs, chains, quantize/swing, step/real-time write, randomize, cartridge (bank) files, SMF import/export. Pure JS, no DOM. ES module. |
| `five/app.js`, `five/index.html` | Faceplate UI, LCD menus, AudioWorklet host, Web MIDI (+ Android shim), file I/O. |
| `five/worklet.js` | AudioWorkletProcessor that imports `engine.js` and runs the sequencer clock sample-accurately. |
| `five/sw.js`, `five/manifest.webmanifest`, icons | PWA. |
| `five/tools/test.mjs` | Node tests for engine + seq (`node five/tools/test.mjs`). |
| `five/tools/render.mjs` | Renders reference WAV/float32 output from engine.js for the C++ comparison. |
| `plugins/five/*` | JUCE plugin: `Five.h` (line-for-line C++ port of engine.js), `FiveSeq.h` (port of the seq.js playback/data needed in the plugin), `FivePlugin.*`, `FiveEditor.*`, `test.cpp` (renders the same as render.mjs). |
| `five/tools/compare.mjs` | Runs `build/five_test` and render.mjs and checks max abs diff < 1e-4. |
| `android/app/src/five/**` | `FiveActivity.java` (copy of TwelveActivity pattern: bundled assets, AndroidMidi bridge, save to Downloads/Omega Five), manifest, strings, icons. |
| `.github/workflows/five-release.yml` | Windows/Mac/Linux plugins + installers, APK, release `omega-five`. |

Nobody commits; the coordinator commits.

## Voices (24, two per pad; pad A–L = "1st" voice, M–X = "2nd" voice of the 12 channels)
Index `v` 0..23. Channel `ch = v % 12` (fader/output). Names (LCD, max 8 chars):
```
 0 BD 1     1 SD 1     2 RIM 1    3 TOM 1A   4 TOM 2A   5 TOM 3A   6 TOM 4A   7 HH CLS   8 RIDE     9 CRASH   10 TAMB    11 SHAKER
12 BD 2    13 SD 2    14 RIM 2    15 TOM 1B  16 TOM 2B  17 TOM 3B  18 TOM 4B  19 HH OPN  20 RIDE CP  21 CHINA  22 CLAPS   23 COWBELL
```
Channel names (fader labels, as on the panel): `BD SD RIM TOM1 TOM2 TOM3 TOM4 HIHAT RIDE CRASH TMBRN/CLAPS SHKR/CWBEL`.
Hi-hat choke: voice 7 (closed) chokes 19 (open) — they share channel 7 (one channel = one voice playing at a time,
like the hardware: retrigger on the same channel cuts the previous note with a 2 ms fade).

### Voice generation (engine.js)
`engine.makeVoiceSample(v, sr)` returns a `Float32Array` (the "ROM": rendered once at 25 kHz-ish native rate
`ROM_SR = 25000`, then played back by the playback path). Each voice is a small circuit-style model with a fixed
seed (deterministic; use a 32-bit xorshift PRNG `rng(seed)` in both JS and C++ — never Math.random in DSP):
- BD: bridged-T / damped resonator with pitch-swept sine (exp sweep), click transient, soft clip (tanh).
- SD: two damped modes (~180/330 Hz) + band-passed noise snares (2-pole SVF), separate envelopes.
- RIM: high-Q bandpass ping (~1.7 kHz) + short noise click.
- TOMs: damped sine with downward pitch bend, 4 pitches per set; B set darker/longer.
- HH: 6 square oscillators at the 808-style metallic ratios → 2 bandpass + highpass, closed short/open long.
- RIDE/CRASH/CHINA/RIDE CP: metallic square bank (ring-mod pairs) + noise, long exp decays, different BP centres.
- TAMB/SHAKER: filtered noise bursts with jingle (comb) / grains.
- CLAPS: 3–4 noise bursts 10 ms apart + reverb tail through BP 1 kHz.
- COWBELL: two squares 540/800 Hz through BP, fast decay.
Exact numbers are up to engine.js; C++ must match engine.js sample-for-sample (same float ops order; use
`Math.fround`-free double math in JS and `double` in C++; output stored as float32).

A voice can be replaced by a user sample (`engine.setUserSample(v, Float32Array, sr)`), resampled to ROM_SR.

### 12-bit playback path (per voice, "circuit model")
1. ROM sample at ROM_SR, quantized to 12-bit **floating-point companded** format: 12-bit mantissa, gain
   shift chosen per sample block of 64 (`exp = 0..3`, step = 2^-11 * 2^exp), reproducing the RX5-style noise floor.
2. Pitch: `pitch` param -1200..+1200 cents (coarse ±12 st, fine) + bend (rate, range) → phase increment;
   playback by linear interpolation **without** an anti-alias filter before the DAC (aliasing is part of the sound).
3. Reverse flag, loop flag (loops the last 30% of the sample when set; with EG it sustains).
4. EG (digital, RX5-style 5-segment): AR attack rate, D1R decay-1 rate, D1L decay-1 level, D2R decay-2 rate,
   RR release rate, gate time (in 1/96 clocks or ms, release starts after gate). Rates 0..99, levels 0..99.
5. Level 0..31 (voice level), accent: ACCENT1 +3 dB, ACCENT2 +6 dB, per-step velocity also stored.
6. DAC model: sample-and-hold at the output rate of 50 kHz (oversampled ZOH, then decimated), followed by a
   reconstruction filter: 3-pole Sallen-Key-ish lowpass @ 12 kHz (bilinear), then a gentle output coupling HPF
   (8 Hz) and a soft op-amp saturation `x / (1 + |x|^2.5)^(0.4)` at the output stage.
7. Pan per channel (-1..1), fader 0..1, reverb send, delay send.

### Effects
- **REV7-style reverb** (`engine.Rev7`): programs `HALL1, HALL2, ROOM1, ROOM2, VOCAL, PLATE1, PLATE2, ER1, ER2,
  GATE, REVERSE GATE` — early-reflection tapped delay patterns (per program), then a modulated FDN (8 lines,
  Householder), with params: `time` 0.3–30 s RT60, `hiRatio` 0.1–1 (HF decay), `delay` (predelay 0–200 ms),
  `diffusion` 0..10, `density` 0..100 %, `erLevel`, `revLevel`, `hpf`, `lpf`, `gateTime` (for GATE/REV GATE).
  Model the 31.25 kHz internal rate + 16-bit word with simple rate-reduction (sample-hold + filter) — "converter flavor".
- **TC-style delay** (`engine.TcDelay`, 2290-style): stereo, time L/R (1–2000 ms) or tempo sync (1/4, 1/8, 1/8., 1/16,
  1/4T), feedback 0–120 % with soft limiter in the loop, ping-pong, hi-cut/lo-cut in loop, modulation (rate, depth),
  ducking (threshold, release) keyed by the dry input, panorama spread, mix.
- Both on aux sends (sendRev, sendDly per channel), returns summed to master; master volume (VOLUME slider).

### Engine API (identical in JS and C++)
```js
const e = new Engine(sr);              // sr = host sample rate
e.trigger(v, vel /*0..127*/, accent /*0|1|2*/, time /*sample offset in block, 0 if now*/)
e.release(v)                           // note off (MIDI)
e.setVoiceParam(v, name, value)        // name ∈ pitch, level, ar, d1r, d1l, d2r, rr, gate, bendRate, bendRange, reverse, loop
e.setChannel(ch, {fader, pan, sendRev, sendDly, mute})
e.setFx('rev', name, value); e.setFx('dly', name, value)   // see above
e.setMaster({volume, click /*metronome level*/})
e.process(outL, outR, n)               // Float32Array, adds nothing, writes n samples
e.clickTick(accented)                  // metronome click (short 1 kHz/2 kHz blip)
e.getState() / e.setState(obj)         // all voice/channel/fx params as plain JSON
```
Default voice params: pitch 0, level 31, ar 99, d1r 0... (sensible per voice, defined in `VOICE_DEFAULTS`).

## Sequencer / data model (seq.js)
- Resolution **24 PPQN** = 1 tick per 1/96 note, exactly like the hardware and MIDI clock (1 tick per F8).
  Quantize ticks: 1/2=48, 1/4=24, 1/6=16, 1/8=12, 1/12=8, 1/16=6, 1/24=4, 1/32=3, 1/48=2, 1/96=1, OFF=1. Quantize values as on the panel: `1/2, 1/4, 1/6, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/96, OFF`
  (keypad: 7=1/32, 8=1/48, 9=OFF, 4=1/12, 5=1/16, 6=1/24, 1=1/4, 2=1/6, 3=1/8, 0=1/2).
- 100 patterns (00–99), each: `{name, beats /*numerator 1..32*/, unit /*denominator 4|8|16*/, bars 1..99, swing 50..75 %,
  events:[{t /*tick*/, v /*voice 0..23*/, vel, acc /*0|1|2*/}], volume:[12 channel levels]}`.
- 20 songs (A–T style index 0..19): `{name, tempo, steps:[{type:'pat', n} | {type:'rep', from, count} | {type:'tempo', bpm} | {type:'vol', ch, level} | {type:'mark'}]}`.
- Chains: 4 chains of up to 99 songs.
- Transport: play pattern / play song, tempo 40–250 BPM, start/stop/continue, int / MIDI / tape(=ext clock off) / ext sync flags.
- `Seq.tick(fromTick, toTick) → events` and a `Player` class with `advance(nSamples, sr, bpm) → [{offset, v, vel, acc}]` +
  song position; this is what worklet.js and the C++ port use.
- **Randomize** (`randomize(state, seed, scope)`): scope `'all'` randomizes every voice param (musically bounded),
  fx programs/params, channel mix, and generates new patterns (genre-weighted kick/snare/hat probabilities, fills on
  last bar) and a song from them. Also `'pattern'`, `'voices'`, `'fx'`. Deterministic for a seed.
- **Cartridge**: the whole memory (patterns, songs, chains, voice params, fx, user samples as 16-bit PCM base64)
  saved/loaded as one file `*.o5cart` (JSON, gzip if `CompressionStream` exists — but loader accepts both). Also
  "CART" virtual slots in localStorage/IndexedDB (4 slots) in the app; in the plugin, cartridge files via file chooser.
- **MIDI files**: `importSMF(arrayBuffer, opts) → {patterns, song}` — reads format 0/1, maps GM drum notes (ch 10
  or any channel, option) to voices (GM map: 35/36→BD1/BD2, 38/40→SD1/SD2, 37→RIM1, 39→CLAPS, 41/43→TOM4A/B, 45/47→TOM3, 48/50→TOM2/1,
  42/44→HH CLS, 46→HH OPN, 51→RIDE, 53→RIDE CP, 49/57→CRASH, 52→CHINA, 54→TAMB, 70/82→SHAKER, 56→COWBELL), splits
  into bar-length patterns (dedupe identical bars → same pattern number), builds a song with repeats, takes tempo
  and time signature. `exportSMF(state, {what:'pattern'|'song', n}) → Uint8Array` (format 1: tempo track + drum
  track on ch 10, GM notes, velocity incl. accents).
- **MIDI in/out** (app and plugin): notes → voices by the note map (`noteMap[v]`, default GM above; editable in MIDI menu),
  receive/transmit channel, clock in (24 ppq → tempo, start/stop/continue/SPP), clock out, notes out of the
  sequencer, program change → pattern select, SysEx bulk dump = cartridge JSON wrapped in `F0 7D 4F 35 ... F7`
  (7-bit packed). Velocity → level, CC7 master volume.

## SysEx (five/rxsysex.js, ported to plugins/five/FiveSysex.h)
Hardware-format Yamaha bulk dumps (layout per `five/RX5-MIDI-NOTES.md`): transmit/receive all data, patterns,
songs, chains, voice parameters; dump requests; checksums; split/merged packets; device number filter. Unknown
or partial details are marked UNCERTAIN in the notes and handled by best-known layout. The full-memory
`F0 7D 4F 35 … F7` wrapper (seq.js) carries everything the hardware format can't (user samples, FX).
.syx files load/save in the app and the plugin; the plugin also sends/receives SysEx through the host's MIDI.

## Panel (index.html) — must fit any screen with **no scrolling** (fixed stage scaled to fit, like twelve/index.html)
Top: angled black heat-sink ribs; left sliders VOLUME, CLICK, TEMPO, DATA; right 12 channel faders
(BD … SHKR CWBEL). Mode keys with LEDs: PTN, SONG, EDIT VOICE, CAS, CART, MIDI, REV, DAMP; 2×16 yellow-green
LCD; MULTI; sync LEDs INT SYNC, MIDI SYNC, TAPE SYNC, EXT SYNC; RUN LED. Printed menu legend under the LCD
(SONG/EDIT SONG/EDIT VOICE/CHAIN/UTILITY/KEY ASSIGN/CASSETTE/CARTRIDGE/MIDI lists — clickable to jump to that item).
Middle: green ACCENT 1 / ACCENT 2; black function keys (PATTERN/SONG, REAL TIME WRITE/EDIT SONG, STEP WRITE/INSERT,
EDIT VOICE, KEY ASSIGN, QUANTIZE/DELETE, SWING/REPEAT, CLICK/TEMPO CHANGE, CHAIN, CASSETTE/CARTRIDGE, EDIT PATTERN/
VOLUME CHANGE, CLEAR, COPY, SYNC, MIDI, REVERSE, DAMP, TEMPO, UTILITY, JOB); cream keypad 0–9 with quantize
legends, red -1/NO and +1/YES; blue STOP/CONTINUE (SHIFT/COMPARE), red START (ENTER). Bottom: 24 pads A–X with
channel names. Added (styled to match, near the RX logo spot): **RANDOMIZE** button, **REV** and **DLY** keys that
open the REV7-style / TC-style pages on the LCD, plus IMPORT MIDI / EXPORT MIDI / WAV under the JOB menu.
The logo area reads "OMEGA" / "DIGITAL RHYTHM PROGRAMMER" / big outlined "FIVE".
