# The F.A.R.P. — VST3 plugin

The F.A.R.P.'s instruments and vocoder as a Windows VST3 instrument (plus a standalone app).

- `Source/dsp/` — the sound engine in C++: the Omni / Omni-2 string machine with its synth, bass, chorus and
  DFX-84 effects, the P-5 model, E-II sampler, Prophet-5 Rev 3 and Six-Trak firmware emulation (Z80 + 8253),
  FM-4, PD-8, the ROM PCM player, and the VX-350 and Talker vocoders. It is a line-for-line port of the web
  app's engine and is checked against it sample by sample (`test/`).
- `Source/PluginProcessor.*` — the plugin: MIDI from the host, the vocoder's voice from the plugin input or
  the page's speech lines, and the settings saved with the project.
- `Source/PluginEditor.*` — the plugin window: the app's own screens in a WebView2 page.
- `web/` — `gen_plugin.py` builds `index.html` from the web app (`farp.html`) and `plugin_shim.js`.

No ROM data is included: the plugin reads your ROM zips from the folder you choose.

Built by GitHub Actions (`.github/workflows/build-vst3.yml`); the zip lands on the `farp-vst3-latest` release.
To build by hand on Windows: install the WebView2 SDK NuGet package, then
`cmake -S . -B build -A x64 -DJUCE_WEBVIEW2_PACKAGE_LOCATION=<folder with the package>` and
`cmake --build build --config Release`.
