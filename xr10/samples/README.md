# XR-10 samples go here

Put the sample files in this folder (WAV or AIFF, any rate, mono or stereo), named after the sounds on the panel:
`KICK1.wav` … `KICK10.wav`, `SNARE1` … `SNARE10`, `RIMSHOT1`, `RIMSHOT2`, `HAT CL`, `HAT MD`, `HAT OP`, `TOM1 L` … `TOM4 H`,
`CRASH1`, `CRASH2`, `RIDE`, `CHOKE`, `CLAPS`, `FINGER`, `CONGA1` … `CONGA4`, `VIBRASLAP`, `GUIRO1`, `GUIRO2`, `CABASA`,
`TAMBOURINE`, `HITS`, `TRIANGLE L`, `TRIANGLE H`, `COWBELL`, `CLAVES`, `WHISTLE`, `SQUEAK L`, `SQUEAK H`, `TIMBALES L`,
`TIMBALES H`, `AGOGO L`, `AGOGO H`. Case, spaces and a number in front (`07 KICK7.wav`) don't matter; a file called just
`23.wav` is sound 23.

When files are pushed here, the release workflow builds them into `xr10/rom.bin`, the sound ROM inside the APK and the
plugin. To do it yourself:

    g++ -O2 -std=c++17 xr10/tools/make-rom.cpp -o make-rom && ./make-rom xr10/samples xr10/rom.bin

It lists which file went to which sound. Sounds with no file stay synthesized (pre-rendered into rom.bin, so the app starts at once).
