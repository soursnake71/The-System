# Notes for working on this repo

## The owner's phone and the screen rules (standing rules, always apply)
- The owner uses a **Samsung Galaxy S23**: 360 x 780 CSS px (DPR 3). In the Android app (system bars inset) that is about
  **732 x 336 landscape** and **360 x 708 portrait**; in a browser up to 780 x 360 / 360 x 780. Design and test for these first.
- **No scrolling, ever** - not the page, not inside a page. Content that doesn't fit goes on numbered pages with page
  buttons (and swipe). Controls keep a playable size (knobs ~40-46 px) instead of being shrunk to fit.
- Must work in **both portrait and landscape**.
- It must **look like real hardware**, like the owner's photos (DR-110 front panel, the AL-110 mod panel): brushed metal
  plates with screws, engraved section boxes, knurled shaded knobs, raised keys with shadows, wooden end cheeks, rack units
  with glowing displays. No flat web-form look.
- Before shipping UI, run the Playwright check at the S23 sizes in both orientations: no document or page-body overflow,
  everything visible inside the viewport, and look at the screenshots.
- Omega 110 (`dr110/`): `layout()` / `paginate()` in `dr110/omega.js` place the rail, the machine and the page; a page's
  direct children (or children of `.flat` containers) are the blocks that get paged; `.headBlock` goes in the title bar.

## Omega 110
- `dr110/engine.js` (JS) and `plugins/dr110/DR110.h` (C++) are line-for-line ports: change both, then run
  `node dr110/tools/compare.mjs <build>/dr110_test <dir>`.
- Presets live in `dr110/presets.js`; after changing them run `node dr110/tools/gen-presets.mjs` (writes `plugins/dr110/Presets.inc`).
- CI (`.github/workflows/dr110-release.yml`) builds the APK and the VST3/AU and publishes them to the `dr110-latest` release.
  Windows builds with MSVC: avoid init-captures of non-copyable/private-member types in lambdas.

## Status and next steps (saved 2026-11-09)
- Branch `ccr-d6dcdf8f-mbyiou`. Latest app build: commit 54906ac (no scrolling, page buttons, hardware look, S23 layout).
  CI publishes `omega-110.apk` and `Omega-110-{Windows,Mac,Linux}.zip` to the `dr110-latest` release.
- The plugin (VST3/AU) still has the earlier editor look (tabs VOICE MOD / MIXER / PAD EQ / REV 7 / MASTER, FOLD/EXPAND,
  presets, dice); it works and passes the host test, but doesn't yet match the app's new hardware look.
- A four-agent "swarm" was started and stopped by the owner before it changed anything. Its plan, still open:
  1. App: fewer page flips on S23 landscape (VOICE MOD, FX, FILES, HELP to 2 pages max), even more hardware-like
     (closer to the AL-110 mod panel photo), the preset module and rail as part of the hardware.
  2. Plugin editor: wooden cheeks, brushed metal with screws, knurled knobs, rack-style REV 7 / TC delay, mixer as a console.
  3. Sound: audit all 100 presets and 200 dice rolls for weak / broken / duplicate ones; keep the stock fitted sound.
  4. Bug hunt at S23 sizes: patterns, songs, MIDI (note off, clock, program change), import/export, presets, fold, persistence.
- Owner's feedback to keep in mind: no scrolling ever; it must look like the real hardware in the photos; make it easy
  to make music on a phone. Deliver APK (and VST3 / source when asked) as files in the chat.

## Omega Traks (Drumtraks)
- `traks/engine.js` + `traks/machine.js` (JS) and `plugins/traks/Traks.h` + `Machine.h` (C++) are line-for-line ports: change both, then
  build `traks_test` and run `node traks/tools/compare.mjs <build>/traks_test <dir>` (audio, displays after every key, memory).
- machine.js is the firmware: the panel's keys, the displays, patterns / songs, the sequencer. The app runs it in the worklet; the plugin on its audio thread.
- The panel layout (plate units 2000 x 916 + 60-unit wooden cheeks) is the same numbers in `traks/faceplate.js` and `TraksEditor.cpp` (namespace lay).
- ROM: `node traks/tools/make-rom.mjs` writes `traks/rom/traks.rom` (original synthesized sounds, 8-bit mu-law, 25 kHz). Presets: `node traks/tools/gen-presets.mjs`.
- The logo on the panel reads "omegatraks" (the Omega's, like the 110's OMEGA) in the place and style of the hardware's.
- CI: `.github/workflows/traks-release.yml` -> release `traks-latest` (omega-traks.apk, Omega-Traks-{Windows,Mac,Linux}.zip).
