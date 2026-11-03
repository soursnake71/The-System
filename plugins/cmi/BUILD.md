# Building Omega CMI

Everything is C++17. The engine (`core/`) is plain C++ with no dependencies and is shared by all three builds.

| Build | Folder | Needs | Command |
|---|---|---|---|
| VST3 (+ AU on Mac) and standalone app | `plugins/cmi` | CMake 3.22+, a C++17 compiler; JUCE 8 is fetched | `cmake -S plugins/cmi -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target OmegaCMI_All` |
| Android APK (native, AAudio) | `android-cmi` | JDK 17, Gradle 8, Android SDK 35 and NDK 27 | `gradle -p android-cmi assembleDebug` (APK in `android-cmi/app/build/outputs/apk/debug/`) |
| Headless Linux (ALSA, no GUI) | `plugins/cmi/headless` | g++ or clang++, ALSA headers | `make -C plugins/cmi/headless` |

- Linux VST3 needs: `libasound2-dev libx11-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev`.
- Windows: Visual Studio 2022 with "Desktop development with C++"; open a Developer prompt and use the CMake command above.
- Results: `build/OmegaCMI_artefacts/Release/VST3/Omega CMI.vst3` and `.../Standalone/`. Copy the .vst3 to
  `C:\Program Files\Common Files\VST3` (Windows), `/Library/Audio/Plug-Ins/VST3` (Mac) or `~/.vst3` (Linux).
- The Android project finds the engine at `../plugins/cmi/core`, so keep the two folders side by side.
- Tests: `build/cmi_test <dir>` (card maths, every voice, pitch, Page R, disk reader), `cmi_snapshot <dir> [disk.imd ...]`.

## Layout
- `core/Cmi.h` channel card model (pitch registers, SSM2045 filter, envelope rate multiplier, band-limited DAC)
- `core/Engine.h` the 16 channel cards, voice allocation, Page R sequencer
- `core/Library.h` the synthesised voices, user voices, sample import, WAV reader
- `core/Disk.h` CMI disk images (IMD / IMG, QDOS) and .VC voice files
- `CmiPlugin.*`, `CmiEditor.*` the JUCE plugin and its screen; `headless/` the ALSA program; `tools/` tests
- `../../android-cmi/` the Android app: `cpp/native.cpp` (AAudio + JNI), `java/.../CmiView.java` (the screen)
