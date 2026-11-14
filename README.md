# The Omega System

A small phone DAW: 16-pad drum rack with MPC-style real-time recording, sample chopping, instrument tracks with an in-key grid, warped audio tracks from a USB-C interface, scenes and song mode, MIDI clock sync with an MPC, automation, effects (EQ, filter, compressor, saturator, redux, chorus, auto pan, sidechain duck, reverb, delay), SP-1200 12-bit mode, drum machine kits (TR-909, TR-505, LinnDrum LM-2, Oberheim DMX, Boss DR-110, Simmons SDS-V), a master EQ with EQ matching, a talking-machine text-to-speech for the Star Screamer 350 vocoder, the Wheel of Steel turntable with touch and MIDI scratching, an FX rack (Lexicon PrimeTime, Deltalab Effectron, Eventide H910, MXR 113, Lexicon 224 / AMS RMX16), a Prophet-5 model, a TB-303 model, a Moog Taurus model, a Solina String Ensemble model, an SVC-350 vocoder model, stem separation and WAV export.

## Put it online with GitHub Pages
1. Create a GitHub repo and upload these files to it (or push them).
2. In the repo, open Settings, then Pages. Under "Branch", pick `main` and `/ (root)`, then Save.
3. After a minute, open `https://<your-username>.github.io/<repo>/` in Chrome on your phone.
4. In Chrome's menu, tap "Add to Home screen".

## Jam-On Rig (`/jamon/`)
A separate app with the gear from Newcleus's *Jam On Revenge*: TR-808, Oberheim DMX, TB-303, Sequential Pro-One, SH-101, Juno-60, Roland RS-09, a Fender Precision bass (physical model) and an Electro-Harmonix vocoder (EH-0300 model) driven by text to speech or a live mic. Open `https://<your-username>.github.io/<repo>/jamon/` in Chrome and Add to Home screen.
- Seven arrangements in the style of the album's tracks (original patterns, not transcriptions), presets per machine, arpeggiators, a step grid / piano roll per machine.
- MIDI in (USB or Bluetooth keyboards, by channel or to the machine on screen), MIDI file import and export, WAV export and live recording.
- Mix FX: Lexicon PrimeTime, Deltalab Effectron, Eventide H910, Lexicon 224 / AMS RMX16 and a gated room as sends; MXR Phase 90, EHX Small Stone, EHX Electric Mistress, A/DA Flanger and UREI 1176 as inserts; a cymbal bus; dub throws and drops.
- Full MIDI: notes, velocity, sustain, pitch bend, mod wheel, CC7 / CC10, program change, all notes off, MIDI Learn for every control, clock in (tempo, start, stop, continue, song position), clock and notes out.
- Pictures for the About screen go in `jamon/art/` (cover.jpg, band.jpg).
- Band centres for the vocoder are the published EH-0300 list; 808 voice frequencies follow published circuit analyses (Werner, Abel & Smith, DAFx-14, and others).

## Omega Twelve (`/twelve/`): 12-bit sampler emulator
A vintage sampler in your phone or your DAW. Import any audio or video file, pick part of it, and run it through the E-mu SP-1200 or SP-12 simulated at circuit level (the service-manual circuits for the anti-alias filter, outputs 3-6, the SSM2044 and the network that sweeps it, plus the 8-channel DAC timing and the 7-bit pitch counter), checked against recordings of a real SP-12 (`twelve/VALIDATION.md`), or 16 other samplers (MPC60, MPC3000, S612, S950, S1000, RZ-1, SK-1, Emax, EPS, EPS 16+, Mirage, DSS-1, S-550, SU700, TX16W, Clean) with 16 filter models, a driven preamp, alias in semitones on any machine, filter on/off, input/output link, bypass and VU meters.
- Full MIDI (notes, velocity, pitch bend, program change, MIDI learn for every control, clock), and a live TV screen (Cast, or a TV window).
- 540 presets in 27 families, plus a random settings generator and your own presets.
- Tuning (semitones, cents, A4 reference, transpose to a key), lo-fi (vinyl crackle, tape hiss, wow, flutter, tape saturation, vinyl age, mains hum).
- Tempo and key detection, warp to a BPM (stretch keeping pitch, or speed it like the hardware), timestretch: smooth, or cyclic S950 / S1000 style in Classic (stretched, synced to the bar in the plugin) or Warp (length kept, cycles pitched up), with grain down to half a millisecond; the plugin does it live on its input too.
- Select, zoom, truncate, normalize, reverse, undo, resample (bounce what you hear back in, no export needed), 16 multi-pitch pads and MIDI, STOP ALL.
- Master out: EQ matching to a reference track (or a typical mix), 5-band parametric EQ, compressor, gain, look-ahead limiter, Auto master to a loudness target.
- Export WAV (16/24/32-bit float), AIFF, FLAC, MP3, and Opus/OGG/M4A via the device codec.
- Android app: the `twelve-latest` release (`omega-twelve.apk`, built by `.github/workflows/twelve-apk.yml`). Web: `https://<your-username>.github.io/<repo>/twelve/`.
- VST3 (and AU on Mac, plus a standalone app): Omega Twelve (effect: live input through the sampler, and samples from MIDI) and Omega Twelve Instrument, in the `vst3-latest` release. The plugin's engine (`plugins/twelve/Twelve.h`) is a line-for-line port of `twelve/engine.js`; `twelve/tools/compare.mjs` checks they render the same samples.

## Omega 7 (`/seven/`): 6-operator FM synthesizer
A Mark I-era FM synthesizer on your phone, in your DAW or on its own, played and programmed from a faceplate laid out like the 1983 instrument's (STORE, MEMORY PROTECT, OPERATOR SELECT, EDIT / COMPARE, MEMORY SELECT, FUNCTION, DATA ENTRY, YES / NO, the 32 numbered buttons, the 2 x 16 display, the algorithm chart).
- The hardware's signal path: six operators per voice with log-sine and exponential tables (14-bit operator outputs, 0.0235 dB envelope steps), the 32 algorithms with two-sample feedback, the envelope generators, pitch envelope, LFO (six waves, delay, key sync), keyboard level and rate scaling, velocity, fixed / ratio oscillators and detune, 16 voices, all at the original 49.096 kHz. Then the Mark I converter: a 12-bit DAC whose range switches in 6 dB steps (floating point, 12-bit mantissa), held between samples and smoothed by the output filter. FUNCTION 30 swaps in a clean 16-bit converter to compare.
- Voices: standard sysex. 32-voice bank dumps (4104 bytes, packed) and single voices (163 bytes) load and save, so banks from the original, librarians and other emulators work here and the other way round. Internal memory plus a cartridge slot (load any bank into it), memory protect, store, compare, edit recall, voice init, operator copy, naming.
- Programming like the original: EDIT plus buttons 1-32 for every voice parameter, OPERATOR SELECT, buttons 1-6 switch operators on and off; FUNCTION 1-24 as on the hardware (tune, poly / mono, bend, portamento, MIDI, controllers with pitch / amplitude / EG bias assign).
- Arpeggiator and function sequencer in the manner of the FM groovebox's v1.10 firmware: 8 arpeggio modes, 1-4 octaves, rate, gate, latch, swing; 16 patterns of up to 64 steps with chords, gate and ties, step and real-time recording, swing, chaining, key transpose, and four motion lanes that move any voice or function parameter step by step (smooth or stepped), recorded by moving the parameter while recording.
- Full MIDI: notes, velocity, bend, mod wheel, breath, foot, aftertouch, sustain, portamento, volume, expression, program change, mono / poly, all notes off; voice and bank dumps, parameter changes and dump requests in and out; clock in and out (the plugin follows the host's tempo and transport). MIDI files: play through the synth, import into a pattern, export a pattern (voice and motion included as sysex), record a performance to .mid.
- A die for a random playable voice (random algorithm, musical ratios and envelopes; STORE keeps it) and reset back to the stored voice; the dice can roll one part (ALL, ALG, FREQ, ENV, LFO, FX) as on the Omega 110; effects after the converter: a stereo chorus, then the Omega 110's TC 2290-style dynamic delay (time or tempo sync, feedback with high / low cut, modulation, ducking, ping-pong) and REV7-style reverb (Hall, Room, Vocal, Plate, Early Ref; time, HF ratio, pre-delay, diffusion, ER level, high cut), with starting points (FX, or FUNCTION 30 pushed again), all automatable in the plugin.
- 32 original voices (no factory ROM data).
- Android app: the `omega-seven` release (`omega-7.apk`, built by `.github/workflows/seven-release.yml`), offline, USB / Bluetooth MIDI with sysex. Web: `https://<your-username>.github.io/<repo>/seven/`.
- VST3 (and AU on Mac, plus a standalone app): Omega 7 in the same release. Its engine (`plugins/seven/Seven.h`) is a line-for-line port of `seven/engine.js` and `seven/seq.js`; `seven/tools/compare.mjs` checks they produce the same samples (bit for bit). VST3 hosts don't pass sysex to plugins: load banks from the plugin's MEMORY menu or FUNCTION 11.

## Using a USB-C interface
Plug it in, open the Audio tab and tap Enable input. When Chrome asks, allow the microphone. Pick the interface under Setup, then Device if it isn't the default.

## Credits
- Omega 7's envelope, LFO, pitch envelope and scaling curves follow the measurements in Music Synthesizer for Android (Google, Apache 2.0) and the published reverse engineering of the OPS / EGS chips.
- SP-1200 audio path data (filter prototypes, pitch table, converter behaviour) from [libmd12](https://github.com/Mudb0y/libmd12) (MIT).
- SVC-350 vocoder modeled from the Roland SVC-350 service notes (Dec. 8, 1979), [archive.org](https://archive.org/details/roland_SVC-350_SERVICE_NOTES).
- Solina String Ensemble modeled from the Eminent/ARP Solina schematic set, [archive.org](https://archive.org/details/ARP_Solina_Schematics).
- Prophet-5 modeled from the Sequential Circuits Prophet-5 Rev 3 service and owner's manuals, [archive.org](https://archive.org/details/synthmanual-sequential-circuits-prophet-5-service-manual).
- TB-303 stock-circuit behaviour from Robin Whittle's Devil Fish documentation, [firstpr.com.au](https://www.firstpr.com.au/rwi/dfish/).
- Moog Taurus modeled from the Taurus Model 205A owner's & service manual and schematic, [archive.org](https://archive.org/details/Moog_Taurus_Model_205A_Owner_s_Service_Manual).
- Drum kits and FX rack modeled from published circuit analyses and service documentation (Electric Druid, Sound On Sound, MAME's LinnDrum and DMX drivers, the PrimeTime, Effectron II, H910, MXR 113 and RMX16 manuals, Dattorro 1997). No factory samples are included.
