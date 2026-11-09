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
