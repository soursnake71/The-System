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

## MKV to MP4 (`/mkv2mp4/`)
A separate Android app that converts MKV files into MP4s that play on Google TV and Chromecast. Get `mkv-to-mp4.apk` from the "MKV to MP4 (latest)" release and open it on your phone (or sideload it onto the TV).
- Tap Choose MKV files (several at once is fine), or share / "Open with" an MKV from a file manager.
- Convert a whole folder: every MKV in it and its subfolders is converted. Files already in the output folder are skipped, so re-running a folder only does the new ones.
- Save to: Movies/MKV to MP4 unless you pick another output folder (internal storage, SD card, USB drive...). The setting is remembered.
- Streaming limit (6, 10 or 20 Mbps, or none; 10 is the default): video over the limit is re-encoded to H.264 under it so it doesn't stutter on the dongle. The limit is what decides the picture most: use 20 Mbps if your Wi-Fi is strong.
- H.264 and HEVC video under the limit is copied as is, so most files take a minute or two with no quality loss. Other video (VP9, 10-bit H.264, MPEG-4...) is re-encoded to H.264, at most 1080p, in software with x264 (Quality, the default) or with the phone's hardware encoder (Fast: quicker but softer; falls back to x264 if the phone's encoder can't do it).
- HDR video (HDR10, HLG) that has to be re-encoded is tone-mapped to normal colour, so it doesn't come out grey and washed out.
- Audio becomes AAC (AC-3, DTS, TrueHD, FLAC, Opus...), keeping 5.1. Subtitles are left out unless Keep subtitles is on; then text subtitles (SRT, ASS) are kept, while picture subtitles (PGS, VobSub) can't go in an MP4 and are still left out.
- The MP4 index is at the front of the file (faststart), so it starts playing straight away when streamed.
- Max compatibility also turns HEVC into H.264 and mixes audio to stereo, for older dongles. It is much slower.
- Uses FFmpeg via [ffmpeg-kit](https://github.com/sk3llo/ffmpeg_kit_flutter_android) (GPL build).

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
