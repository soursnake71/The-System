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

## Dragonstomper 3D (`/dragonstomper/`)
Starpath's 1982 Supercharger adventure, reverse engineered from the original tapes and rebuilt as a realistic desktop 3D action game with three.js. Open `https://<your-username>.github.io/<repo>/dragonstomper/` in a desktop browser, or install it on Windows.
- **Windows installer:** download `Dragonstomper-Setup.exe` from the `dragonstomper-latest` release and run it. It installs per user (no admin needed), adds Start menu and desktop shortcuts, and has an uninstaller. It isn't code-signed, so SmartScreen may warn: choose *More info → Run anyway*. The app is an Electron wrapper in `dragonstomper/desktop/` (`npm run dist` builds the installer; GitHub Actions builds it on every change).
- **Android APK:** `Dragonstomper.apk` from the same release. The game is bundled inside, so it plays offline; it's landscape, fullscreen, and also appears on Android TV. Play it with the on-screen touch controls (a movement stick under your left thumb, drag on the right to look, and buttons to attack, block, jump, dodge and search), or with an Xbox or PlayStation controller over Bluetooth or USB. Back pauses; on the title screen press Back twice to quit. It starts at Medium quality and 75% resolution, which you can change in Settings. Allow installs from unknown sources when Android asks. Source: `dragonstomper/android/` (`gradle assembleRelease`).
- Third-person over-the-shoulder camera, or first person (V / R3). Real-time sword combat (combo, shield block, dodge roll, stamina) where every hit and every point of damage is still rolled with the ROM's own dice and monster stats.
- PBR rendering: Poly Haven scanned textures and models, HDR sky lighting, ACES tone mapping, soft shadows, ambient occlusion (GTAO), bloom, SMAA, GPU grass and procedural trees. Quality presets Low to Ultra; render resolution up to 4K (3840×2160), or supersampled to 4K on a smaller screen.
- Keyboard and mouse, touch controls on phones and tablets, or an Xbox or PlayStation controller (standard Gamepad API mapping, button prompts follow the pad you use, menus work with the d-pad).
- Three parts as on the tapes: the Enchanted Countryside and its bridge guard, the Village (shops, hospital, warriors for hire), and the Dragon's Cave with the dragon in its lair. Saves in the browser.
- Characters walk, run and idle with motion-captured animation retargeted onto their sculpted skeletons; attacks, blocks and deaths are procedural. Light shafts outdoors, a golden-hour village, torch-lit dust in the cave and a particle fire breath.
- Recorded sound effects (footsteps on grass, stone, wood, armour and weapon impacts, coins, doors) layered with synthesised creature voices and ambience, plus music for each region, the dragon fight, death and victory.
- Credits: three.js (MIT); ez-tree (MIT); textures, HDRIs and models from Poly Haven (CC0); locomotion clips from the three.js "Xbot" example (Mixamo); sound effects from Kenney's RPG Audio and Impact Sounds (CC0); music by RandomMind via OpenGameArt (CC0).
- `dragonstomper/tools/` has the extractor, a small Supercharger emulator and a disassembler; `dragonstomper/REVERSE_ENGINEERING.md` has the notes. The ROM is not included: run `python3 dragonstomper/tools/extract.py <your dragonstomper.zip>` to rebuild `data.js`.

## Using a USB-C interface
Plug it in, open the Audio tab and tap Enable input. When Chrome asks, allow the microphone. Pick the interface under Setup, then Device if it isn't the default.

## Credits
- SP-1200 audio path data (filter prototypes, pitch table, converter behaviour) from [libmd12](https://github.com/Mudb0y/libmd12) (MIT).
- SVC-350 vocoder modeled from the Roland SVC-350 service notes (Dec. 8, 1979), [archive.org](https://archive.org/details/roland_SVC-350_SERVICE_NOTES).
- Solina String Ensemble modeled from the Eminent/ARP Solina schematic set, [archive.org](https://archive.org/details/ARP_Solina_Schematics).
- Prophet-5 modeled from the Sequential Circuits Prophet-5 Rev 3 service and owner's manuals, [archive.org](https://archive.org/details/synthmanual-sequential-circuits-prophet-5-service-manual).
- TB-303 stock-circuit behaviour from Robin Whittle's Devil Fish documentation, [firstpr.com.au](https://www.firstpr.com.au/rwi/dfish/).
- Moog Taurus modeled from the Taurus Model 205A owner's & service manual and schematic, [archive.org](https://archive.org/details/Moog_Taurus_Model_205A_Owner_s_Service_Manual).
- Drum kits and FX rack modeled from published circuit analyses and service documentation (Electric Druid, Sound On Sound, MAME's LinnDrum and DMX drivers, the PrimeTime, Effectron II, H910, MXR 113 and RMX16 manuals, Dattorro 1997). No factory samples are included.
