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

## Omega CMI (`plugins/cmi/`, `android-cmi/`): Fairlight CMI Series II model
A circuit-level model of the Fairlight CMI Series II channel cards, as a VST3 instrument (AU on Mac, plus a standalone app) and a native Android app. Both run the same C++ engine (`plugins/cmi/core`).
- Modelled from the CMI mainframe service manual as captured by MAME's CMI-01A driver: the 34.291712 MHz master oscillator and master tune divider; each channel's 10-bit pitch register and octave divider (the pitch *is* the sample clock, no interpolation); 16 KB of 8-bit wave RAM in 128 pages of 128 samples (loops and segments change on page boundaries); the 8-bit DAC's staircase (band-limited exactly, images and all); the SSM2045 dual two-pole filter whose cutoff follows the octave register, so it tracks the keyboard an octave at a time; the 8-bit envelope counter clocked through its rate multiplier (the CMI's piecewise-exponential attacks and decays); zero-cross-gated envelope loads; the envelope x volume multiplying DACs; the output coupling, summing amp and noise floor.
- 51 library voices with the famous ones: the orchestra stab, the breathy "aah" choir, the low male choir, breaking glass, pizzicato strings, shakuhachi, pan pipes, kalimba, marimba, tubular bells, koto, sitar, harpsichord, clav, grainy piano, slap and fretless bass, the TV-theme lead guitar, timpani, gong, crowd "hey", the Page R drum kit (KICK1, SNARE1, CROSSTIX, HIHAT2, HHOPEN1, CLAPGATE, BASSGT...) and Page 4 waveform voices (harmonics drawn per segment). The original Fairlight library recordings are copyrighted, so none is included: every voice is synthesised from scratch at start-up by physical and spectral models and stored the way the CMI stored sounds (8-bit, up to 32 kHz, 16 KB, page-aligned loops).
- Screens: Page 2 (library), Page 7 (control bar graphs), Page D (the waveform in 3D, the sixteen channel cards), Page 8 (sample any audio file into eight user voices), Page R (the real-time composer: 8 tracks x 16 steps, 16 patterns, song mode, swing, synced to the host's transport or its own clock; pattern 1 is the one on the famous screen). Type commands on the COMMAND line (PLAY, STOP, LOAD ORCHHIT, SPEED 110, PATTERN 3, PAGE 7).
- MIDI: notes, velocity, pitch bend, mod wheel (vibrato), sustain, program change (library voices); "Multi MIDI" plays Page R tracks 1-8 on channels 2-9.
- Downloads: the `omega-cmi` release (`Omega-CMI-Windows/Mac/Linux.zip`, `omega-cmi.apk`), built by `.github/workflows/cmi-release.yml`. Tests: `cmi_test` (card maths, every voice, pitch, Page R, host sync) and `cmi_snapshot` (the plugin driven directly, state, a picture of every page).

## Using a USB-C interface
Plug it in, open the Audio tab and tap Enable input. When Chrome asks, allow the microphone. Pick the interface under Setup, then Device if it isn't the default.

## Credits
- SP-1200 audio path data (filter prototypes, pitch table, converter behaviour) from [libmd12](https://github.com/Mudb0y/libmd12) (MIT).
- SVC-350 vocoder modeled from the Roland SVC-350 service notes (Dec. 8, 1979), [archive.org](https://archive.org/details/roland_SVC-350_SERVICE_NOTES).
- Solina String Ensemble modeled from the Eminent/ARP Solina schematic set, [archive.org](https://archive.org/details/ARP_Solina_Schematics).
- Prophet-5 modeled from the Sequential Circuits Prophet-5 Rev 3 service and owner's manuals, [archive.org](https://archive.org/details/synthmanual-sequential-circuits-prophet-5-service-manual).
- TB-303 stock-circuit behaviour from Robin Whittle's Devil Fish documentation, [firstpr.com.au](https://www.firstpr.com.au/rwi/dfish/).
- Moog Taurus modeled from the Taurus Model 205A owner's & service manual and schematic, [archive.org](https://archive.org/details/Moog_Taurus_Model_205A_Owner_s_Service_Manual).
- Fairlight CMI channel card behaviour from MAME's CMI-01A and CMI drivers (BSD-3-Clause; Phil Bennett and contributors), calibrated against the CMI Mainframe Service Manual. Fairlight and CMI are trademarks of their owners; Omega CMI is an independent model and contains none of the Fairlight library.
- Drum kits and FX rack modeled from published circuit analyses and service documentation (Electric Druid, Sound On Sound, MAME's LinnDrum and DMX drivers, the PrimeTime, Effectron II, H910, MXR 113 and RMX16 manuals, Dattorro 1997). No factory samples are included.
