# The F.A.R.P. for Android

The F.A.R.P. synth as an installable Android app (Android 8.0 or newer). It runs fully offline, fills the screen, keeps the screen awake while you play, and connects USB and Bluetooth MIDI keyboards automatically.

## Get the APK

1. Create a free GitHub account and a new **private** repository.
2. On the repository page choose **Add file → Upload files** and upload everything in this folder, including the hidden `.github` folder. (On a Mac, press Cmd+Shift+. in Finder to show it.)
3. Open the **Actions** tab. The build starts automatically and takes about 5 minutes.
4. When it shows a green tick, open the run and download **FARP-Android-APK** at the bottom. Unzip it to get `FARP.apk`.

You can do all of this from your phone's browser on github.com.

## Install on your phone

1. Copy `FARP.apk` to the phone (or download it there directly) and tap it.
2. Android asks to allow installs from that app (Chrome, Files or Drive). Allow it, then tap **Install**.
3. Play Protect may warn that the app is from an unknown developer. Tap **More details → Install anyway**. The warning appears because the app isn't on the Play Store.

## MIDI keyboards

- **USB:** connect the keyboard with a USB-C OTG cable or adapter. It's picked up automatically, and the MIDI screen shows "Connected: (name)."
- **Bluetooth MIDI:** pair it in a Bluetooth MIDI app first (for example "MIDI BLE Connect"). It then shows up like a USB keyboard.
- Notes, velocity, pitch bend, mod wheel, aftertouch, sustain pedal, program change, MIDI clock and CC Learn all work as in the web version.

## Notes

- The panel lettering uses web fonts that download when you're online. Offline, the app falls back to the phone's own fonts and works the same.
- Your saved patches, sequencer patterns, songs, pad settings, loaded samples, CC Learn map and last screen are stored on the phone.
- Export MIDI saves a .mid file straight into your Downloads folder. Import MIDI, Load sample and Load several open the phone's file picker (Load several lets you select many samples at once).
