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

## Sky Sequencer (`/sky/`)
Turns live air traffic into music. Pick a place (or use your location) and it watches the planes within a radius, works out what each one is doing from its last few minutes of track, and plays it in time:
- **Circling** (holding stacks, helicopter orbits) plays an arpeggio: turn rate sets the speed, turn direction sets up or down, orbit size sets the range.
- **Climbing** and **descending** planes play lines whose pitch is their altitude.
- **Cruising** traffic holds chord tones on a pad.
- **Low & close** planes play percussion, and every new arrival plays one hit.
- **Sky grid**: the area is a 16 × 8 grid that collects heat as planes pass through it over a window you choose (1 to 60 minutes). It is read as a step sequencer, west to east.
- An emergency squawk shifts the key up a fourth until it clears.

Each lane has its own sound and MIDI channel, so every behaviour can drive a different synth (USB MIDI in the Android app, Web MIDI in Chrome). Record to a .mid file with one track per lane.

Where: it finds you from your internet connection the first time it opens. You can also use GPS (phone), search any place, address or airport by name (OpenStreetMap), type coordinates (`51.47, -0.45` or `51.47N 0.45W`), paste a Google or Apple Maps link, or pick a busy airport.

Live feeds: adsb.lol, airplanes.live, adsb.fi, OpenSky Network, or your own RTL-SDR receiver running readsb / dump1090. Browsers block the public feeds, so the web version plays a simulated sky unless the receiver is on the same computer; the Android app and the plugin get live traffic. The APK is built by the "Sky Sequencer APK" workflow and attached to the `sky-latest` release.

### In a DAW (VST3 / AU / standalone, `plugin/`)
The same sequencer as a JUCE instrument plugin. It locks to the DAW's tempo and plays while the transport runs (or always, on its own tempo). Its built-in sounds work in every DAW, including Ableton, and each lane also sends MIDI on its own channel for DAWs that route plugin MIDI out (Reaper, Bitwig, Cubase, FL Studio, Logic). Location and feed are saved with the project. The "Sky Sequencer plugin" workflow builds Windows, macOS (VST3 + AU) and Linux versions, checks them with pluginval, and attaches zips to the `sky-plugin-latest` release; see `plugin/INSTALL.txt`. To build it yourself: `cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` (JUCE is downloaded automatically; JUCE is free for open-source use under the AGPLv3).

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
