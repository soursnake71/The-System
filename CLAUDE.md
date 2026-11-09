# Notes for working on this repo

## Screen fit (the owner's standing rule)
- Every app UI must fit the screen with **no page scrolling**, in **both portrait and landscape**, on phones
  (e.g. 915 x 380 landscape, 412 x 860 portrait CSS px) as well as tablets and desktops.
- Omega 110 (`dr110/`) does this with a fixed full-viewport layout: a toolbar on top and a stage below, where
  `layout()` in `dr110/omega.js` scales the machine and the open page to fit. Folded (EXPAND / FOLD) shows the machine
  alone at the largest size that fits. Don't add content that relies on scrolling the page.
- Check new UI on those phone sizes in both orientations before shipping (a Playwright check: no
  `scrollHeight > innerHeight`, every visible page inside the viewport).

## Omega 110
- `dr110/engine.js` (JS) and `plugins/dr110/DR110.h` (C++) are line-for-line ports: change both, then run
  `node dr110/tools/compare.mjs <build>/dr110_test <dir>`.
- Presets live in `dr110/presets.js`; after changing them run `node dr110/tools/gen-presets.mjs` (writes `plugins/dr110/Presets.inc`).
- CI (`.github/workflows/dr110-release.yml`) builds the APK and the VST3/AU and publishes them to the `dr110-latest` release.
  Windows builds with MSVC: avoid init-captures of non-copyable/private-member types in lambdas.
