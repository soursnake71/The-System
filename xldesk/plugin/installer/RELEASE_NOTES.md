XL-Desk for Absolute Future Studios: a model of the SSL XL-Desk analogue console as a VST3 plugin and a standalone app.

**Install**
- **Windows:** run `XL-Desk-…-Windows-Setup.exe`. It installs the VST3 into `C:\Program Files\Common Files\VST3` and the standalone app. Windows may show a SmartScreen warning because the installer isn't code-signed: choose *More info → Run anyway*.
- **macOS (Apple Silicon and Intel):** open `XL-Desk-…-macOS.pkg`. The package isn't notarised, so the first time macOS blocks it: right-click the .pkg and choose *Open*, or allow it under *System Settings → Privacy & Security*. It installs the VST3 to `/Library/Audio/Plug-Ins/VST3` and the app to `/Applications`.
- **Linux:** `sudo apt install ./xl-desk_…_amd64.deb` (system-wide), or unpack the `.tar.gz` and run `install.sh` (your user only).
- The plain `…-VST3.zip` files hold just the plugin, if you'd rather copy it yourself.

**AAX (Pro Tools)** builds only when the repository has an `AAX_SDK_URL` secret pointing at your copy of Avid's AAX SDK. Those builds are unsigned: they load in Pro Tools Developer. For retail Pro Tools, sign them with `xldesk/plugin/installer/sign-aax.sh`, which needs PACE's wraptool and an iLok.

**Source code:** `XL-Desk-…-source.zip` contains the web version and the plugin. Build steps are in `plugin/README.md`.

Every build is checked by the engine test suite (levels, routing, solo, monitoring, modules, SSL's published noise and frequency-response specs) and by pluginval at its strictest setting on all three platforms.
