# Omega 808: session notes and to-do

Branch: `claude/roland-tr808-circuit-apk-5rfmud` · Session: https://claude.ai/code/session_01UXixvbLEZBqR8RE1q24scz · Started 2026-11-19

## The request
"Build me a circuit modeled roland tr-808 apk. Use the way the omega 110 works as a reference. Folds open to the circuit
modded sections. Lots of presets. Use the rev 7 and tc delay as the effects units. Its going to be a vts3 also. Use photo as
the faceplate. Incorporate the Egyptian lover trigger out trick. Full midi control. Able to load samples. Uses the classic
roland 808 accent function. Swarm."

## Decisions made
- **"Omega 110"**: there is no Omega 110 in the repo, so the reference is **Omega Twelve** (`twelve/`). Its pattern is a DOM-free JS engine, a web UI,
  an Android flavor bundling the web app offline (`TwelveActivity`), and a JUCE VST3 that is a line-for-line C++ port checked by `compare.mjs`.
  Waiting on the user to confirm or correct this.
- The app is named **Omega 808**. It lives in `eight/`, uses Android flavor `eight` (`com.thesnakedevice.eight`), the VST3 target `OmegaEight`, and the release `omega-808`.
- The faceplate is the user's photo, `eight/faceplate.jpg`, downscaled to 2000x1190, with hit areas and knob overlays on top.
- "Folds open" means a CSS 3D hinge lifts the photo panel to reveal a PCB-style circuit-mod board, with trimpots for `mod.*`, the REV7 + TC 2290 rack and the sample slots.
- **Effects**: Yamaha REV7-style reverb (`Rev7`, program list) and TC Electronic 2290-style delay (`TC2290`), both as sends.
- **Egyptian Lover trick**: the 808's rear TRIGGER OUT puts out 20 ms pulses that follow the CB, CP or AC track. He recorded that click as a drum sound.
  It's modelled as the `TRIG` voice: a pulse through AC coupling and mixer-input clipping, with the source picked by `mod.TRIG.src`. It also goes out as a MIDI trigger note.
- **Accent**: one AC track plus the global AC.level knob, which scales the trigger voltage of every voice on accented steps.
- "Swarm" means the build is split across parallel agents with file ownership. They follow `eight/CONTRACT.md`, which has the param keys, pattern format and worklet messages.

## Swarm status
| Agent | Owns | Status |
|---|---|---|
| A: DSP | `eight/fx.js`, `engine.js`, `seq.js`, `worklet.js`, `tools/test.mjs` | done |
| B: UI | `eight/index.html`, `app.js`, `sw.js`, manifest, icons | done |
| C: presets + MIDI | `eight/presets.js`, `midi.js`, `tools/presets-check.mjs` | done |
| D: Android + CI | `android/app/src/eight/**`, `build.gradle` flavor, `.github/workflows/eight-release.yml`, local APK build | done |
| E: VST3 | `plugins/eight/**`, CMake `OmegaEight`, `eight/tools/compare.mjs` | deferred (weekly budget) |

## To-do
- [x] Faceplate photo into `eight/faceplate.jpg`
- [x] Shared contract `eight/CONTRACT.md`
- [x] A: circuit-modelled voices (BD bridged-T per Werner/Abel/Smith DAFx-14, SD, toms/congas, RS/CL, CP/MA, CB, CY/OH/CH six-oscillator metal bank, TRIG), accent, sample slots, mixer
- [x] A: REV7 (16+ programs incl. gate / reverse gate) and TC 2290 (sync, ping-pong, ducking)
- [x] A: 808 sequencer: 12 basic + 4 intro/fill, A/AB/B, I/F variation, TAP, auto fill-in, 1st/2nd part, pre-scale, swing, MIDI clock follow
- [x] A: Node tests pass (`node eight/tools/test.mjs <dir>`)
- [x] B: photo faceplate with every control mapped, step LEDs, 808-style step entry, pad mode
- [x] B: fold-open circuit board panel, FX rack, sample load/clear, trigger-out section
- [x] B: preset browser, own presets, random, state saved, WAV export
- [x] C: 120+ kits, 150+ original rhythms, 12+ banks, 30+ FX presets, and the checker passes
- [x] C: full MIDI covering notes (GM map, velocity accent), learn for every CC, program change, clock in/out, notes out, trigger-out note, Android shim
- [x] D: Android `eight` flavor + `EightActivity` (offline assets, MIDI, sample picker, saving), icon and banner
- [x] D: APK built locally (`gradle -p android assembleEightDebug`), and the CI workflow `eight-release.yml`
- [ ] E: C++ port of engine/fx/seq (`plugins/eight/Eight.h`), JUCE instrument with the photo editor and fold-open mods panel, host-synced sequencer, MIDI in/out
- [ ] E: `compare.mjs` shows the C++ matching the JS, and the VST3 builds locally (JUCE 8.0.4 fetch works here)
- [ ] Integrate: Playwright check of the whole app, README section, commit + push each part
- [ ] Optional: a button in the main Omega app that opens Omega 808 (as SP-1200 opens Omega Twelve)
- [ ] Confirm "Omega 110" with the user

## Environment notes
- Network works for GitHub (JUCE) and dl.google.com (Android SDK). The SDK goes in `/opt/android-sdk`, and is lost when the container is reclaimed.
- Gradle is at `/opt/gradle`, Java 21 is installed, and Chromium + Playwright are preinstalled.
- Agents don't commit. The coordinator commits and pushes each part.

## Next session
- Run the VST3 port (agent E, above). The workflow's VST3 job does nothing until `plugins/eight` and the CMake target `OmegaEight` exist.
- Balance the voice levels: OH and CY come out quieter than the drums.
- In the circuit panel, `dly.sync` and `dly.pingpong` show as trimpots instead of toggles. The UI should read `step:1` and `opts`.
- Re-check the tom section's sample-slot layout with a screenshot, and have `worklet.js` read `processorOptions.init` for WAV export.
- Add a README section, and confirm "Omega 110" with the user.
- Every commit message lists its sources (the user asked for this).
