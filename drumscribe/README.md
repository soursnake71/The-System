# Drum Scribe

Paste a YouTube, Spotify or SoundCloud link (or open an audio file) and get back a MIDI file with **every drum hit and
every bass note**: unquantized (the player's exact timing), with note lengths and velocities, every piece of the kit,
303-style bass slides, a tempo map that follows the band, and **the maths of the groove** (swing, push and pull,
microtiming per instrument, tempo drift, syncopation, a 16-step groove template).

| Part | Where | What it is |
|---|---|---|
| Engine | `drumscribe/core` (C++17) | does all the analysis; runs on your computer as `drumscribe-server` (or `drumscribe-cli`) |
| Android app | `android/` flavor `scribe` → `drum-scribe.apk` | paste a link, or Share a song from YouTube / Spotify to it |
| VST3 / AU plugin + standalone app | `plugins/drumscribe` → *Omega Drum Scribe* | in your DAW: reads the project tempo, drag the MIDI onto a track |
| Browser page | served by the engine at `http://<computer>:8765/` | the same thing from any browser |
| Reference prototype | `drumscribe/engine` (Python) | the first version; the C++ is checked against it |

Downloads: the `drumscribe-latest` release (built by `.github/workflows/drumscribe.yml`).

## How it works

1. **Audio**: YouTube / SoundCloud through `yt-dlp`. Spotify's audio is DRM-protected and can't be downloaded, so the
   engine reads the track's title, artist and length from its page and takes the YouTube upload whose length matches
   (official / "Topic" uploads preferred, live / cover / remix versions avoided). Decoding is `ffmpeg`.
2. **Separation**: Demucs v4 (`htdemucs_ft`, Meta) takes the drums and the bass out of the mix (in C++ with
   [demucs.cpp](https://github.com/sevagh/demucs.cpp)); MDX23C DrumSep (aufr33 & jarredou) splits the drums into kick,
   snare, toms, hi-hat, ride and crash (exported to ONNX, run with ONNX Runtime).
3. **Drums** (`src/drums.cpp`): candidate hits from spectral flux, then the attack found on the waveform itself to the
   sample (measured from whatever was still ringing underneath), bleed between the stems rejected by level *and*
   spectral shape, then the pieces: closed / pedal / open hi-hat, up to six toms by pitch, ride bow / bell, two
   crashes, snare / side stick. Lengths: until the sound dies away, is choked (hats) or is struck again.
4. **Bass** (`src/bass.cpp`): pitch tracking, note onsets / releases, slides (a glide with no new attack: the 303
   slide), bends, accents.
5. **Beats** (`src/beats.cpp`): beat and downbeat tracking from the hits, following tempo drift and tempo changes.
6. **Groove** (`src/groove.cpp`): the numbers and a plain-English summary.
7. **MIDI** (`src/midi.cpp`): written from scratch: format 1, 960 PPQ, a conductor track (tempo change on every beat,
   time signature, bar markers, the groove summary as text, GM System On SysEx), drums on channel 10, bass on channel 2
   with pitch bend (range set to 12 by RPN 0), portamento and legato overlaps for slides. Every note is converted
   through the tempo map, so it lands on its exact original time (within half a tick).

### Tempo in your DAW
- DAWs that read file tempos (Logic, Cubase, Reaper, Studio One, Pro Tools, Bitwig): use the main file; the grid
  follows the band, tempo changes included.
- Ableton Live and FL Studio ignore file tempos: use the *constant tempo* file and set the project to the tempo shown.
  The plugin does this for you: it reads the project tempo and asks for the MIDI at exactly that tempo.

## Accuracy (exact ground truth, `ctest`)

Drums (`tests/test_drums.cpp`; a synthetic drummer with human timing, ghost notes, flams, fills, open / pedal hats,
ride bell, two crashes, side stick, perfect stems with -26 dB bleed between them), three seeds:

| | F-measure | attack error mean | attack error p95 | note length error (median) |
|---|---|---|---|---|
| all pieces | 0.978 – 0.983 | 0.24 – 0.48 ms | 0.95 – 3.08 ms | 2.8 – 3.0 ms |

Open / closed hats, crash 1 / 2, ride bell: F = 1.00 on every seed. Known weak spots: floor-tom false hits from other
drums' bleed, tom note lengths cut short when other drums bleed into the tom stem.

**Real recordings**: [MDB Drums](https://github.com/CarlSouthall/MDBDrums) (MedleyDB drum tracks, hand-annotated), the
drum track through DrumSep, then the C++ detector (`tools/eval_mdb.cpp`), F at ±50 ms:

| track | all | kick | snare | hi-hat (open + closed) |
|---|---|---|---|---|
| Disco | 0.97 | 1.00 | 0.94 | 0.98 |
| 80s Rock | 0.97 | 0.99 | 0.97 | – |
| Rock | 0.88 | 1.00 | 0.92 | 0.83 |
| Funk Jazz | 0.86 | 0.97 | 0.96 | 0.92 |
| Punk | 0.70 | 0.90 | 0.96 | 0.60 |

Hits land a median 2.5 – 4.5 ms before the hand annotations (which are themselves good to a few ms; the engine marks the
very start of the attack). Weakest: cymbal-heavy punk, where DrumSep puts crash wash into the hi-hat and ride stems.

Through the real separation models (Python prototype, a synthetic song with bass and keys): kick 0.99, snare 0.91,
toms 1.00, with sub-millisecond timing. Synthetic cymbals aren't recognised by DrumSep (it was trained on real drums),
so cymbals are measured on real recordings instead (MDB Drums), see below.

Bass (`tests/test_bass.cpp`, 4 seeds each, exact ground truth): 303 acid line with slides and accents F 0.995 (onsets
0.6 ms, slides found 95%, no false ones); fingered bass F 1.000 (0.3 ms); both with kick bleed and noise F 0.94 – 0.96;
compressed picked legato F 0.96. Real songs (MDB, `tools/eval_bass_real.cpp`, transcribed pitch vs. an independent
pitch track of the isolated bass, and the isolated vs. the Demucs-separated bass): Rock, Punk, Funk Jazz, 80s Rock:
96 – 99% of the time within 50 cents, octave errors 0.3 – 2%, separated-vs-isolated agreement F 0.75 – 0.87. Disco
(chromatic glisses, trills, muted ghost notes) is the weak one: F 0.65, its glisses written as chains of slides.

Beats / groove / MIDI (`test_beats`, `test_groove`, `test_midi`): beat F 1.000 on 15 cases (drift, sudden tempo
change, ritardando, shuffle, 3/4, 6/8, 78 – 174 BPM); swing, snare lag, accents, bass-kick offset recovered to within
a few tenths of a percent / ms; every MIDI note within half a tick of its time through the tempo map (150 checks).

## Run the engine

```
cmake -S drumscribe/core -B build-ds -DCMAKE_BUILD_TYPE=Release
cmake --build build-ds -j
drumscribe/core/tools/get_models.sh          # once: downloads and converts the separation models
build-ds/drumscribe-server                   # prints the address for your phone
build-ds/drumscribe-cli "https://www.youtube.com/watch?v=..." -o out/
```
Needs `ffmpeg` and `yt-dlp` on the PATH. If YouTube asks to sign in, start the engine with
`DRUMSCRIBE_COOKIES_BROWSER=chrome` (or firefox, edge...). `DRUMSCRIBE_TOKEN=...` makes clients send a token.
A GPU isn't needed; on a 4-core laptop CPU separation takes a few times the song's length.
