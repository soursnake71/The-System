# How the owner wants every plugin and app laid out

This is the owner's standing design for **all** Omega plugins and their phone apps. It came out of RX5 (`five/`,
`plugins/five/`) and Omega 110 (`dr110/`, on branch `claude/omega-linn`), and the owner asked for it to be kept as
"how I want my plugins". Build every new machine this way and bring old ones to it. Reference pictures are in
`docs/layout/` (made at the owner's phone size, 889 x 415 CSS px landscape, and the S23 portrait size, 360 x 708).

## 1. The machine is the faceplate
- The front of the real hardware, drawn as hardware (or the owner's photo): faders, keys, LCD, pads, printed legend.
  Brushed metal, engraved boxes, raised keys with shadows, knurled knobs, wooden cheeks. No flat web-form look.
- It fills as much of the screen as it can. On phones the plain parts at the top (ribs above the fader caps) are
  cropped, fader travel is shortened, and the main part of the panel (display, keys, pads) is moved up into any empty
  band, so the machine starts at the top edge of the screen and the important part is big (RX5: `FRONT_UP`, compact).
- Printed writing never overlaps: each column of a legend is measured and squeezed sideways to fit the room it has
  (phones without a narrow font draw text wider). Check with a wide font before shipping.

## 2. A rail of page keys on the side (like Omega 110)
- Hardware keys in a rail: **FOLD**, **MACHINE**, then one key per group of commands (RX5: VOICE, KEYS, PATTERN,
  SONG, MIXER, FX, MIDI, FILES), then **ZOOM**.
- Landscape: the rail is one **slim** column down the left (about 8-10 % of the width; two columns only on very
  short screens), the machine or the open page beside it. Portrait: the machine on top, the rail as two rows of keys
  under it, the page under that (MACHINE shows the LCD and its keys big there).
- The rail stays on top of everything (so ZOOM can always be switched off) and the lens keeps clear of it.

## 3. Commands fold open on pages
- Every command the LCD / menus have is also on a page, big enough to read and tap: one row per job with the job
  number and name, the value in an LCD-style box, and keys: ◀ ▶ (steps inside a job), − + (the value), NO / YES
  (actions). Build the pages from the same items the LCD menus use, so nothing is written twice.
- The page header shows what the rows act on (voice, key, pattern, song, channel) with ◀ ▶.
- **Nothing ever scrolls.** What doesn't fit goes on numbered pages: ◀ 1 2 3 ▶ in the header, and swipe. A heading
  never ends a page. Lay out again after the page keys are in (they can make the header taller).
- **FOLD** hides the rail and pages: the bare machine fills the screen, with a small **EXPAND** key in the corner.
  The plugin window gets narrower when folded (fixed aspect ratio either way). The choice is remembered.

## 4. A real mixer
- The MIXER page is a console, not a list: a strip per channel side by side, each with the channel name and number,
  a **M**ute key, **PAN**, **REV** send and **DLY** send knobs (drag up/down, double-tap resets, value under each),
  a vertical **fader** with a **level meter** beside it, and the level readout.
- A **MASTER** strip at the end (click, reverb return, delay mix, master volume, master meter).
- Strips that don't fit side by side (at least ~54 px each) go on the next page; on a phone in landscape all 12 plus
  master fit on one page, in portrait 5-6 per page.

## 5. ZOOM: a magnifier for everything
- The ZOOM key switches it on. A tap on the machine magnifies that spot **3x** in a lens instead of pressing it.
- **Move it with a finger**: keep the finger down and drag, and the lens follows, showing what is under the finger.
  The lens has a **grip bar** on top ("drag to move · tap to use"): dragging it moves the lens like a real
  magnifying glass (it shows what is under its own centre).
- What is pressed, dragged or turned **inside** the lens is the real control (keys, pads, faders, knobs, menus).
  A tap outside closes it; ZOOM again switches it off.
- The printed legend also has its own column magnifier: a tap on it shows that column big, a tap on a line opens
  that job.
- Shared code: apps `loupe.js` (`OmegaLoupe.attach({root, button, avoid})`), plugins `plugins/common/Loupe.h`
  (`omega::Loupe` over the editor, `target` = the visible view). RX5 has its own built into `app.js` / `FiveEditor`.

## 6. Both orientations, phone first
- The owner's phone is a Samsung Galaxy S23 class screen: about 889 x 415 CSS px landscape in the app; also test
  732 x 336, 780 x 360, 360 x 708 and 360 x 780.
- The Android app turns with the phone's sensor (`android:screenOrientation="fullSensor"`), portrait and landscape,
  even with rotation lock on. It runs full screen and keeps clear of the camera cutout.
- Before shipping, run the Playwright check at those sizes in both orientations: no document or page overflow, no
  clipped rows or keys, every page, page flipping, FOLD / EXPAND, the mixer (fader, mute, knob), the lens (tap does
  not press, pressing in the lens acts, follow-drag, grip), and look at the screenshots.

## 7. The plugin is the same machine
- The plugin editor has the same faceplate, rail, pages, mixer and ZOOM as the app (JUCE, drawn at the faceplate's
  own size and scaled, fixed aspect, resizable). A headless UI test drives it (RX5: `five_proc_test`; 808:
  `eight_ui_test`) and writes pictures.
- Deliver the APK (and VST3 / installers / source when asked) as files in the chat, from the CI release.
