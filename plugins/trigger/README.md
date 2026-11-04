# Omega Drum Trigger

Audio to MIDI for drum sample replacement, built for fast metal drumming: double kick 32nds, blast beats, hammer
blasts, and kick/snare mics full of bleed. One instance per close mic (kick, snare, toms). VST3 for Windows,
Mac and Linux, plus AU on Mac.

## Getting it
- From this branch: the **Omega Drum Trigger** workflow run (`.github/workflows/drum-trigger.yml`) has the zip
  `Omega-Drum-Trigger-Windows` under *Artifacts*. From `main`: the `vst3-latest` release (Omega-Plugins-Windows.zip).
- Unzip and copy `Omega Drum Trigger.vst3` into `C:\Program Files\Common Files\VST3`, then rescan plugins in the DAW.
- Build it yourself: `cmake -S plugins -B build -DOMEGA_ONLY=Trigger && cmake --build build --config Release --target OmegaTrigger_VST3`.

## Quick start
1. Put it on the kick track (an insert on the close mic). Press **KICK** (or SNARE / TOM).
2. Play the song for a few bars with soft and loud hits, press **LEARN THRESHOLD**. It finds the gap between the
   real hits and the bleed and puts the threshold in it. Or drag the dashed threshold line in the display.
3. Watch the display: every real hit should get a green line, and nothing else should. Raise the threshold if bleed gets
   lines; lower it (or Sensitivity) if a soft hit has none.
4. Get the MIDI out, whichever suits your DAW:
   - **Analyze file** (the most foolproof): bounce/export the kick track from bar 1, press ANALYZE FILE, pick
     the file, then drag the **DRAG** box onto a MIDI track at bar 1 (or SAVE .MID). It covers the whole song in one go,
     with the same detector.
   - **Hit recorder**: with REC on, play the song (or a section; playing a section again replaces it, it doesn't
     double it). Then drag **DRAG MIDI** to a MIDI track at bar 1. Notes sit at their song positions, with the
     song's tempo.
   - **Live MIDI out**: route the plugin's MIDI output to your sampler (Reaper, Cubase, Studio One, Bitwig and
     FL Studio can do this). The notes go out at the hit's real start; the plugin reports its 8.5 ms latency
     and the DAW compensates it.

## Controls
| Control | What it does |
|---|---|
| Threshold | How loud (in the detection band) a hit must be. LEARN THRESHOLD sets it from what it has heard. |
| Sensitivity | How far a hit must jump above what the drum was already doing (its own ring-out, bleed). Lower catches soft hits right after loud ones; higher ignores ringing. Kick 9 dB, snare 4.5 dB. |
| Retrigger | Shortest time between two hits. Kick 22 ms (fine for 32nds at 270 BPM), snare 30 ms. |
| High-pass / Low-pass | The detection band. Kick listens from 600 Hz up: the beater attack, which is what tells fast double-kick strokes apart (the boom of one stroke overlaps the next). Snare 150 Hz - 6 kHz: crack and wires, below most cymbal wash. |
| Sidechain + Bleed level | Feed another mic into the plugin's sidechain input (the snare mic on the kick instance, the hat mic on the snare). A hit is dropped as bleed when it jumped here at least *Bleed level* dB less than on the sidechain. Set it a few dB above how loud the other drum leaks into this mic (e.g. snare leaks into the kick mic at -22 dB: set -17). Hits played together with the other drum (hammer blasts) stay. Bleed shows as red lines. |
| Note, Channel, Length | The MIDI note (kick 36, snare 38, tom 45), channel, note length. |
| Vel ceiling, Vel range, Dynamics | A hit at the ceiling or louder is velocity 127; Vel range below it is velocity 1. Dynamics 0% sends everything at 127. |
| Monitor | What you hear: the input (delayed to line up with the MIDI), the detection band (to tune the filters by ear), clicks on every hit (to check the trigger by ear), or nothing. |
| Rec nudge | Moves recorded notes earlier or later, for a DAW that reports its position to plugins differently. Not needed for Analyze file. |

## How it decides (and how it's tested)
The detector (`Detector.h`, plain C++) works on the detection band: a fast envelope that holds each peak for half a
period of the lowest frequency it lets through, a slow envelope of that, and a hit when the fast one is over the
threshold and has jumped above the slow one by the sensitivity. It then looks 7 ms ahead to measure the hit and to
place the note on the foot of the attack rather than where the threshold was crossed, keeping bleed that landed
just before the hit out of it. Full description at the top of `Detector.h`.

`test.cpp` synthesizes close-mic metal tracks with known hit positions: double kick 16ths at 180-280 BPM, 32nds at 200 and
240 BPM (31 ms apart), 14 dB dynamics, traditional blasts at 250 and 300 BPM on kick and snare mics, hammer blasts,
snare under loud hats, brutal snare bleed, with snare, cymbal, ride and kick bleed in every track, at 44.1, 48 and 96 kHz,
in the plugin's streaming mode (random block sizes) and the file analysis (which must agree exactly). Results over 12 sets of
random performances (about 37,000 hits; the last 5 sets not used for tuning): **0 missed hits, 0 false notes**, 0.1-0.3% of
notes placed 1-2 ms from the real start (all of them the softest snares of 300 BPM blasts, rising out of the previous
snare's ring-out), the rest within 1 ms (kick: typically within 0.05 ms). `tools/hosttest/trigger.cpp` loads the
built .vst3 like a DAW and checks the MIDI it sends; `proctest.cpp` checks the recorder, Analyze file and the .mid files.

Synthesized drums aren't real recordings. On a real track, check the green lines over the hardest passage before
committing the MIDI, and keep the original track for the few spots a human ear disagrees with.
