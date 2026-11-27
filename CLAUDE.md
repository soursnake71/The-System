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
- The owner's real XR-10 samples are on their phone (`samples/xr-10`); when they arrive they go in `xr10/samples/` and
  are built into `xr10/rom.bin` by `xr10/tools/make-rom.cpp` (the release workflow does this).
- MIDI files must import and export as drums.
- Wanted features: a random beat and a random sound button; effects easy to find, on by default.
- Each APK build bumps `BUILD` in `xr10/app.js` and the `xr10` flavor's versionCode in `android/app/build.gradle`, so the
  owner can see which build they have. Send the APK with every change.

## UI rules (all apps and plugins)
- **No scrolling menus, ever.** Every menu, dropdown or picker shows all of its choices at once (a grid of buttons,
  several columns if needed), never a list you have to scroll. In web apps don't use native `<select>` popups for
  long lists; in JUCE give popup menus enough columns to fit.
- The owner's phone is a **Samsung Galaxy S23** (about 360×780 CSS px in portrait, 780×360 in landscape). Everything
  must fit and be tappable on it.

## General
- The owner writes short messages from a phone. Act on them; ask only when it's truly ambiguous.
- Plugins are JUCE VST3 (AU on Mac) built by GitHub Actions; Android apps are WebView wrappers in `android/`.
