# Notes for Claude: what the owner of this repo wants

## Omega XR10 (`xr10/`, `plugins/xr10/`, Android flavor `xr10`)
- **Circuit model, always.** Model the real hardware's circuit from its service manual and schematics (component values,
  converters, clocks, filters, gain stages), not a generic approximation. Say plainly what's taken from documents and what's
  estimated, and keep looking for sources that pin the estimates down (sample rates, timings). Sources so far: the XR10
  service manual (archive.org `akai-xr-10-service-manual`), Music Technology's review (June 1990).
- **"The extra stuff from the RX5" means a fold-out panel, an effects rack and a mixer** (as on the RX5 setup: a panel
  that folds out from the machine, holding a mixer with a strip per sound and a rack of outboard effects). It does NOT
  mean RX5 sounds or RX5 per-voice parameters. Don't label things "RX5" in the UI: it reads as if sounds came from
  another machine. Extras that aren't on the XR10 are labelled "extra".
- The faceplate is the owner's photo (`xr10/faceplate.jpg`): straightened, cropped to the machine, no white border. It
  must fit on a phone screen in landscape.
- The owner's XR-10 samples are in `xr10/samples/` (mapped in `map.txt`). They are recordings from the machine's
  outputs, so make-rom takes the output circuit back out before building `xr10/rom.bin`; `tools/check-samples.cpp`
  checks the circuit model gives the recordings back. Their 16.6 kHz edge backs the 33.33 kHz machine rate.
  TOM3/TOM4 and SQUEAK L/H have no recordings yet.
- MIDI files must import and export as drums.
- Wanted features: a random beat and a random sound button; effects easy to find, on by default.
- Each APK build bumps `BUILD` in `xr10/app.js` and the `xr10` flavor's versionCode in `android/app/build.gradle`, so the
  owner can see which build they have. Send the APK with every change.

## Standing rule: how every plugin and app is laid out (the owner's design)
- Every Omega plugin and phone app follows `PLUGIN-LAYOUT.md` (pictures in `docs/layout/`): the machine as hardware
  filling the screen with its main part moved up, a slim rail of page keys on the side (FOLD, MACHINE, a key per
  command group, ZOOM), every command folded open on big pages that never scroll (numbered pages instead), a real
  mixer console (a strip per channel: mute, pan / rev / dly knobs, fader with meter; a master strip), and ZOOM: a 3x
  lens that follows the finger, has a grip bar, and lets you use what it shows. Portrait and landscape (the Android
  app turns with the sensor). Check at the owner's phone sizes before shipping.
- XR10: `xr10/layout.js` does this for the app (rail, pages poured into numbered sheets, FOLD), `xr10/loupe.js` is ZOOM;
  the plugin's rail and views are in `plugins/xr10/XR10Editor.cpp` (`xr10_shot` checks them and writes pictures).

## UI rules (all apps and plugins)
- **No scrolling menus, ever.** Every menu, dropdown or picker shows all of its choices at once (a grid of buttons,
  several columns if needed), never a list you have to scroll. In web apps don't use native `<select>` popups for
  long lists; in JUCE give popup menus enough columns to fit.
- The owner's phone is a **Samsung Galaxy S23** (about 360×780 CSS px in portrait, 780×360 in landscape). Everything
  must fit and be tappable on it.

## General
- The owner writes short messages from a phone. Act on them; ask only when it's truly ambiguous.
- Plugins are JUCE VST3 (AU on Mac) built by GitHub Actions; Android apps are WebView wrappers in `android/`.
