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

## XL-Desk (`/xldesk/`)
A working model of the Solid State Logic XL-Desk analogue console, made for Absolute Future Studios. Open `https://<your-username>.github.io/<repo>/xldesk/` in Chrome and Add to Home screen.
- 16 dual-input mono channels (VHD preamps with 2nd/3rd harmonic drive on 1-8), 4 stereo channels, 2 stereo returns, Mix A-D with B/C/D to A, stereo cue, two auxes, 500 and INS inserts, CHOP direct outs, solo-in-place / AFL / solo safe.
- 16 + 2 slot 500 rack, fully loaded with SSL VHD Pre modules (fed from 500 SLOT IN, brought in with the channel's 500 button) and the G Series Bus Compressor over Mix A. Swap any slot to SSL E Series EQ (Brown 02 / Black 242) or E Series Dynamics; modules are versioned so future ones and new revisions drop in. Mix buses steal slot pairs 9-16 like the desk.
- Monitoring: three speaker sets with bass management, MINI level trims, dim, mono, foldback A/B with talkback, headphones, listen mic and its compressor, all 12 rear DIP switches.
- Tape tab plays your stems into the DAW inputs; print any outputs to 24-bit WAV. Multichannel interfaces: patch inputs and outputs on the Rear panel tab.
- Full MIDI: every control on a fixed CC map plus MIDI Learn, feedback to the controller, 14-bit pitch-bend faders, program change snapshots, and MMC in and out (transport and locate).
- Also a VST3 plugin (Windows, macOS, Linux) with the same interface and DSP: the rear panel becomes its buses, every control is automatable, MIDI and MMC work with the window closed. See `xldesk/plugin/README.md`; builds are on the xldesk-vst3-latest release.
- Switches light red when engaged; fader scales are the ones printed on the desk; bus bargraphs are true-peak.
- Built from SSL's XL-Desk Owner's Manual (82BMLM01B), the VHD Pre module guide, the G Comp, E EQ and E Dynamics 500 module guides, and Sound On Sound's review. The Help tab lists every source and every value that had to be estimated.

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
