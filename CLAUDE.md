# Rules for this repo

- Never synthesize drums when building a sampler or drum machine. Drum voices are always samples (recordings
  of the real machine, or the user's own); only the playback path (converters, filters, envelopes, effects)
  is modelled.

## Standing rule: how every plugin and app is laid out (the owner's design)
- Every Omega plugin and phone app follows `PLUGIN-LAYOUT.md` (pictures in `docs/layout/`): the machine as hardware
  filling the screen with its main part moved up, a slim rail of page keys on the side (FOLD, MACHINE, a key per
  command group, ZOOM), every command folded open on big pages that never scroll (numbered pages instead), a real
  mixer console (a strip per channel: mute, pan / rev / dly knobs, fader with meter; a master strip), and ZOOM: a 3x
  lens that follows the finger, has a grip bar, and lets you use what it shows. Portrait and landscape (the Android
  app turns with the sensor). Check at the owner's phone sizes before shipping.
