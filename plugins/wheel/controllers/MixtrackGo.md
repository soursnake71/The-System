# Numark MixTrack Go — MIDI map used by Omega Wheel of Steel

Source: a djay 5.6.7 mapping file for the controller posted on the Algoriddim community forum
(https://community.algoriddim.com/t/numark-mixtrack-go-custom-midi-mapping/42793). Numark publishes no MIDI table.
Channels below are 1-based (the file stores 0-based). "Note" = note on/off (note-on velocity 0 = release), "CC" = control change.
USB / Bluetooth endpoint names: "Numark Mixtrack GO", "Numark MixTrack Go Bluetooth", "Numark MixTrack Go"; USB id 15E4:005B.

| Control | Message | Omega Wheel of Steel |
|---|---|---|
| Deck A / B channel | ch 1 / ch 2 | |
| Play | Note 0 | play / pause (LED: note 0, 1 = off/dim, 127 = on) |
| Cue | Note 1 | CDJ cue (LED note 1) |
| Sync | Note 2 | sync toggle (LED note 2) |
| Shift + Sync | Note 3 | sync off |
| Shift + Play | Note 4 | censor (hold) |
| Shift + Cue | Note 5 | jump to start |
| Jog wheel (top, vinyl mode) | CC 6, relative (1..63 forward, 65..127 back; two's complement) | scratch (vinyl on) / nudge (vinyl off) |
| Jog wheel touch | Note 6 (assumed, Numark convention; not in the file) | hand on record |
| Shift + jog | CC 43, relative | fast seek |
| Vinyl (scratch mode) button | Note 80 | toggle vinyl / nudge jog mode |
| Pitch fader | CC 9 (MSB) + CC 41 (LSB, 14-bit) | pitch (top = slower, like Technics; direction to verify) |
| Shift + pitch? / sync shift | Note 84 | pitch reset |
| Channel level | CC 22 | channel fader |
| Filter / Low knob | CC 26 | filter (or low EQ when the FILTER/LOW toggle is on) |
| PFL (headphone cue) | Note 27 | PFL toggle (LED note 27) |
| Acapella / Instrumental (stems) | Note 70 / 72 (shift: 71 / 73) | FX on / slip (no stems in this engine) |
| Shift | Note 32 on ch 1 / 2, note 50 on ch 16 (assumed shift; "suppressAutoSelect" in the file) | shift layer |
| Pads (per deck) | ch 5 (A) / ch 6 (B): Note 20–23; with shift Note 28–31 | pad slots 1–4 / 5–8 (cue mode: shift + pad clears cue 1–4) |
| Pad mode button | ch 5 / 6 Note 0 | next pad mode |
| Pad mode LEDs | ch 5 / 6 Note 1 hot cue, 2 loop, 3 sampler, 4 stems | lit for cues / loops / rolls / transport |
| Crossfader | ch 16 CC 8 | crossfader |
| Browse knob | ch 16 CC 0 relative (shift: CC 1) | library cursor |
| Browse push | ch 16 Note 7 | open / enter crate |
| Load A / Load B | ch 16 Note 2 / 3 | load selected track on A / B |
| Filter/Low mode toggle | ch 16 Note 72 | knob = filter / low EQ |
| Jog pitch-bend mode | ch 16 Note 73 | vinyl mode for both decks |
| Fade FX on / next | ch 16 Note 70 / 71 | echo on both decks / next FX type |
| Main / Cue level | ch 16 CC 10 / 12 | hardware (ignored) |
