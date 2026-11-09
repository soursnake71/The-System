# Omega CMI crash test

Run from this folder. `LIB` is a folder of CMI disk images (the IIx library's IMG folder: DRUMS.img, STRINGS.img ...).

- Every reader of outside data (disk images, HFE, .VC, WAV, MIDI, kits, the library cache, pen drawings, names),
  fuzzed under AddressSanitizer and UndefinedBehaviorSanitizer. `corpus/` holds seed files, each with a first byte
  that picks the reader (0 disk, 1 HFE, 2 VC, 3 WAV, 4 MIDI, 5 kit text, 6 catalogue, 7 pen, 8 names):

      g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I../../.. -o fuzz fuzz.cpp driver.cpp -lpthread
      ./fuzz corpus 150 4          # 150 s on the MIDI reader

- Random actions (pads, kits, presets, the dice, round robin, the sample editor and chopping, songs, recording, the
  grid, imports, resets, MIDI, rendering), one thread, ASan + UBSan:

      g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I../../.. -o stress stress.cpp -lpthread
      ./stress "$LIB" 180 1        # 180 s, seed 1

- The same with the audio thread rendering all the time beside them, ThreadSanitizer:

      g++ -std=c++17 -O1 -g -fsanitize=thread -I../../.. -o tstress tstress.cpp -lpthread
      ./tstress "$LIB" 150 7

- The plugin: `CMI_STATE_FUZZ=400 cmi_snapshot out` loads its project data damaged 400 ways and draws every page.
