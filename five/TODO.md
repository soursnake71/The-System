# Omega Five: what's left (handoff for the next session)

Work directly, no agent swarm (usage).

1. Check `node five/tools/test.mjs` is green (one rx test was edited at the end: device OFF refuses Yamaha bulk).
2. VST3/AU/Standalone plugin (`plugins/five/`): `Five.h` and `FiveSeq.h` are partial C++ ports written before the
   engine went samples-only. Remove `makeVoiceSample` from `Five.h`/`test.cpp` and mirror engine.js
   (empty voices, `FACTORY_MAP`, `path 'rx'|'rec'`, `REC_EQ`, `isRec`, slot `rec` flag, output stage).
   Then write FivePlugin/FiveEditor (faceplate like five/index.html, fixed aspect, resizable), embed five/kit/*.wav
   with juce_add_binary_data, add the OmegaFive block + `five_test` to plugins/CMakeLists.txt, and
   `five/tools/compare.mjs` (five_test vs render.mjs, loading five/kit). JUCE: fetch 8.0.4 (CMake does it).
3. `.github/workflows/five-release.yml` expects target OmegaFive and `five_test`; it fails until step 2 is done.
4. Small gaps: the cart file doesn't save `state.library`; per-voice waveform picks aren't in the cart;
   key-assign output channel only affects live pads; no folder picker for LOAD KIT; Bitwig pad GAIN not applied.
