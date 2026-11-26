# Omega Five — build spec (shared contract for every part)

A 12-bit PCM digital rhythm programmer in the style of the late-80s Yamaha RX5 (the faceplate photo in the
session is the layout reference). Web app in `five/`, Android APK flavor `five`, VST3/AU/Standalone plugin in
`plugins/five/`. Product name **Omega Five** everywhere (no third-party logos; model names like "RX5", "REV7",
"TC 2290" may appear only as "-style" descriptions in docs/presets). **Drums are never synthesized.** Every
voice is a sample: the factory kit is recordings of a real 1986 RX5 (`five/kit/`, 25 kHz, the hardware's native
rate), played through a model of the RX5's 12-bit 25 kHz playback path and DAC. Users may load their own
WAV/AIFF/kits into any voice ("voice cartridge"), converted to 25 kHz unless they choose to keep the rate.

## Files and owners
| file | what |
|---|---|
| `five/engine.js` | DSP: sample ROM (no synthesis), 12-bit playback path, EG, mixer, REV7-style reverb, TC-style delay. Pure JS, no DOM. UMD-ish: `export` ES module. |
| `five/seq.js` | Data model: patterns, songs, chains, quantize/swing, step/real-time write, randomize, cartridge (bank) files, SMF import/export. Pure JS, no DOM. ES module. |
| `five/rxsysex.js` | SysEx: framing/assembler, .syx files, RX5 bulk-dump librarian (Yamaha F0 43), Omega F0 7D messages, identity. Pure JS. |
| `five/rxcart.js` | RX5 Waveform Data Cartridge bank images (128 KB / 256 KB `.bin`): parse to voices, build from voices. Pure JS. |
| `five/app.js`, `five/index.html` | Faceplate UI, LCD menus, AudioWorklet host, Web MIDI (+ Android shim), file I/O. |
| `five/worklet.js` | AudioWorkletProcessor that imports `engine.js` and runs the sequencer clock sample-accurately. |
| `five/sw.js`, `five/manifest.webmanifest`, icons | PWA. |
| `five/tools/test.mjs` | Node tests for engine + seq (`node five/tools/test.mjs`). |
| `five/tools/render.mjs` | Renders reference WAV/float32 output from engine.js for the C++ comparison. |
| `plugins/five/*` | JUCE plugin: `Five.h` (line-for-line C++ port of engine.js), `FiveSeq.h` / `FiveSysex.h` / `FiveCart.h` (ports of seq.js, rxsysex.js, rxcart.js), `FivePlugin.*` (processor), `FiveEditor.*` (the faceplate and app.js's menus), `test.cpp` (renders the same as render.mjs: `tools/compare.mjs`), `seqtest.cpp` (`tools/interop.mjs`), `proctest.cpp` (processor and panel). The factory kit (`kit/*.wav`, `kit.json` with the library table written by `tools/kit-json.mjs`) is compiled in. |
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

### Voice ROM (engine.js) — samples only, no synthesis
Voices start empty (silent). `engine.loadRom(v, Float32Array, sr = 25000, {bits, rec})` / `setUserSample` fill a
voice's ROM; the factory kit is loaded at startup from `five/kit/` using `FACTORY_MAP` (voice → file). `rec:true`
marks recordings that already passed through a real RX5 DAC + mixer: they skip the 12-bit requantize and the
output-stage saturation but keep the 25 kHz read / ZOH / reconstruction filter, so repitching behaves like the
hardware. The 27 other factory recordings (cartridge voices) form the waveform library for KEY ASSIGN.

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
- Chains: 3 chains (`NUM_CHAINS`) of up to 999 songs (`CHAIN_MAX`); songs up to 999 parts (`SONG_MAX`).
- Key-assign set-ups: `state.setups[3]` (`newSetup`), current `state.setup`; each `{name, keys:[24 × {voice, pitch (cents),
  level ±31, attack ±63, decay ±63, acc1, acc2 (level offsets ±31, default 2/4 = +3/+6 dB), out 0..11}]}`.
- Transport: play pattern / play song, tempo 40–250 BPM, start/stop/continue, int / MIDI / tape(=ext clock off) / ext sync flags.
- `Seq.tick(fromTick, toTick) → events` and a `Player` class with `advance(nSamples, sr, bpm) → [{offset, v, vel, acc}]` +
  song position; this is what worklet.js and the C++ port use.
  Details (seq.js is the reference): bar = `beats * 96/unit` ticks; swing delays odd grid steps by
  `round(g * (2*swing/100 - 1))` ticks (applied at record time, stored in the event tick); pattern `volume[ch]` 0..31
  and song `vol` steps scale vel (`round(vel*level/31)`, 0 = mute); `rep {from, count}` = steps `[from, rep)` play
  `count` times in total; tick k fires at sample `ceil(k * sr*60/(bpm*24))` from start; clicks are
  `{offset, v:-1, click:2 (downbeat)|1}`; SPP unit = 6 ticks. SMF export uses division 96 (ticks*4), note length 1/16;
  import snaps any division to 24 PPQN. Accents round-trip via a sequencer-specific meta `FF 7F 06 7D 4F 35 note acc vel`
  before the accented note-on (note-on velocity = `min(127, round(vel * [1, 1.413, 1.995][acc]))`).
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
  and time signature. `exportSMF(state, {what:'pattern'|'song', n, map}) → Uint8Array` (format 1: tempo track + drum
  track on ch 10, GM notes by default, velocity incl. accents).
- **MIDI in/out** (app and plugin): notes → voices by the note map (`noteMap[v]`, default **`RX5_NOTES`** = the hardware's
  Job #05 Initialize Note table; `NOTE_PRESETS = {rx5, gm}`; editable in MIDI menu). Shared logic in seq.js:
  `midiIn(state, player, bytes) → actions`, `applyMidiActions(player, actions, offset)`, `midiOutForEvent(state, ev)`,
  `noteOffBytes` (8n v=64), `songSelectBytes`; settings `state.midi` (`newMidi()`): rxCh, txCh, voiceTxCh[24], channelMsg,
  clockIn/Out, notesOut, deviceNo (0 = OFF, 1..16), tunable + tunableVoice (Tunable Note mode: notes 36..96, 60 = original
  pitch; program change only here), egVel, gateMs, activeSense, songSelect. SMF import `map:'auto'|'rx5'|'gm'|'state'`,
  export `map:'gm'` (default) `|'rx5'|'state'`. Clock in (24 ppq → tempo, start/stop/continue/SPP) only when synced
  to MIDI, clock out, notes out of the sequencer, Song Select in/out. Velocity → level. No control changes, pitch bend
  or aftertouch (the RX5 chart has none). SysEx: see below.

## SysEx (five/rxsysex.js, ported to plugins/five/FiveSysex.h)
The RX5's own bulk-dump byte layout is **not public** (RX5-MIDI-NOTES.md §0), so hardware dumps are handled by a
faithful **librarian**, not a decoder:
- `SysexAssembler` (`push(bytes) → [F0..F7 messages]`, split packets, realtime F8–FF passed to `onRealtime`/`.realtime`,
  new F0 or other status aborts a partial message), `parseSyx` / `writeSyx` for `.syx` files.
- Yamaha `F0 43 sn …`: device filter (n = deviceNo−1; deviceNo 0 = OFF accepts all), stored verbatim in
  `state.rxBulk = {received, messages:[base64], status, bytes, count}` via one long-lived `BulkReceiver` (gap > 2 s =
  new dump), retransmitted byte-identical by `transmitBulk(state)` (TRANSMIT BULK, and as the answer to an
  `F0 43 2n … F7` request), saved/loaded as `.syx` (`rxBulkToSyx`, `loadSyx`), kept in `.o5cart` and the full Omega dump.
  `yamahaChecksum`, `verifyYamahaChecksum(msg) → 'ok'|'bad'|'unknown-layout'` (layout `F0 43 0n ff bh bl data cs F7`);
  'bad' is shown as "checksum error!". `dumpRequest(deviceNo, formatByte)` = `F0 43 2n ff F7`, `ff` unverified for the RX5.
- Omega Five messages `F0 7D 4F 35 <cmd> … cs F7` (cs = Yamaha checksum of cmd..data): 01 full cart, 02 pattern, 03 song,
  04 voice+channel params, 05 fx, 06 single parameter change, 07 dump request, 08 identity. The legacy seq.js
  `sysexDump` form (no cmd byte, packed JSON starts `00 7B`) is still emitted by `sysexDump` and accepted as cmd 01.
  Byte layouts are in the header comment of rxsysex.js.
- Universal identity: `F0 7E 7F 06 01 F7` → `F0 7E dev 06 02 7D 4F 35 00 05 00 01 00 00 F7`.
- One dispatcher: `handleSysex(state, msg, {receiver, now}) → {kind, reply:[messages], message, …}`.

## Waveform cartridge banks (five/rxcart.js)
`parseBank(u8)` reads 128 KB RX5 bank images (also 256/512 KB multi-bank images): header, 32-byte voice entries,
big-endian 16-bit byte-sum checksum at 0x3FE, 8-bit or 12-bit (RX5USB packing, 1 latency byte) PCM at 25 kHz, entry
fields mapped to engine voice params. `buildBank(voices, {bankId, fill})` writes one bank (max 30 voices, 256-byte
aligned PCM). parse → build reproduces real bank images byte-for-byte (checked on the RX5USB bank files).

## Hardware fidelity
Matches the RX5 (sources in RX5-MIDI-NOTES.md):
- 24 PPQN (96 clocks per 4/4 bar), the quantize set, 100 patterns × up to 99 bars, 20 songs × up to 999 parts,
  3 chains, 3 key-assign set-ups, tempo 40–250, 12 output channels with key-pair choke, 24 voices.
- Default MIDI notes = Job #05 Initialize Note; note on 9n v 1–127, note off 8n v 64 after the Job #07 gate time;
  Song Select 0–19 in/out; Song Position in; clock + start/stop/continue in/out; Active Sensing in/out; program change
  only in Tunable Note mode; Tunable Note mode over notes 36–96; device number OFF/1–16; voice ranges (pitch +24/−36 st,
  D1L 0–60, gate 0.1–6.5 s, bend rate 0–60, bend range ±60 st, level 0–31, accent offsets ±31) as engine `RANGES`.
- Waveform cartridge bank format and checksum (reverse engineered by RX5USB, verified on factory banks).
Unknown / emulated:
- The RX5 bulk-dump and dump-request bytes: librarian only (store/retransmit verbatim, generic Yamaha checksum check).
  With device number OFF we accept every Yamaha dump (the hardware refuses bulk I/O when OFF).
- No parameter-change SysEx exists on the RX5; Omega messages (F0 7D) are our own. RAM4 and cassette formats unknown.
- Chain length: the manual says both 90 and 999 steps; we allow 999. Swing: the RX5 offers five settings 54–71 % at
  1/8 and 1/16 only; we allow 50–75 % at any grid. Time signature: we support x/4, x/8, x/16 with numerator 1–32
  (hardware: 1–99 and more denominators). Program change meaning in Tunable mode is a guess (selects the voice).
- Cart entry bytes 16/18 (RR vs D1L), 19 (gate code; 92 taken as "off"), 20–22 and 24, and pitch byte mapping are
  RX5USB's interpretations; unknown bytes are preserved on rebuild. Sample companding is treated as linear PCM.
- Sounds are recordings of a real RX5 at its native 25 kHz; the EG curves, 12-bit path and DAC filter are models.
- Set-up data is stored but per-voice MIDI notes/tx channels are global (`noteMap`, `midi.voiceTxCh`), not per set-up.

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
