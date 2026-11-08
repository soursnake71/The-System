package com.thesnakedevice.cmi;

/** The C++ engine (plugins/cmi/core, the same code as the VST3) through JNI. */
final class Native {
    static { System.loadLibrary("omegacmi"); }
    private Native() {}

    // parameter indices (native.cpp)
    static final int LEVEL = 0, FILTER = 1, ATTACK = 2, DAMP = 3, SUSTAIN = 4, VIB = 5, VIBRATE = 6, VIBDELAY = 7, PORTA = 8, VELVOL = 9, VELFILT = 10,
            TRANSPOSE = 11, FINE = 12, LOOP = 13, SEG = 14, POLY = 15, BEND = 16, MASTER = 17, HISS = 18, SPREAD = 19, TEMPO = 20, SWING = 21, SONGMODE = 22, BLEND = 23, BLENDON = 24;
    static final int TRACKS = 8, STEPS = 16, NPAT = 16, SONG_MAX = 64, USER_SLOTS = 255, NCH = 16;

    static native void start();
    static native void stop();
    static native void midi(byte[] data, int off, int len);
    static native void noteOn(int note, int vel);
    static native void noteOff(int note);
    static native void allOff();
    static native void preview(int track);

    static native int soundCount();
    /** name, title, category, rate, root, pages, loopStart, loopEnd, mode (tab separated) */
    static native String soundInfo(int i);
    static native int wave(int i, byte[] out);
    static native int getSound();
    static native void loadSound(int i);
    static native float getParam(int i);
    static native void setParam(int i, float v);
    static native float envTimeOf(float v);

    static native void seqRun(boolean on);
    static native boolean seqRunning();
    static native int seqStep();
    static native int seqPat();
    static native int seqBar();
    static native int getPattern();
    static native void setPattern(int p);
    /** vel | (note & 0xFF) << 8 */
    static native int getStep(int p, int t, int s);
    static native void setStep(int p, int t, int s, int vel, int note);
    static native void copyPattern(int from, int to);
    static native int trackSound(int t);
    static native void setTrackSound(int t, int s);
    static native boolean trackAudible(int t);
    static native void setTrackAudible(int t, boolean a);
    static native int songLen();
    static native void setSongLen(int n);
    static native int songAt(int i);
    static native void setSongAt(int i, int p);
    static native void channelEnv(int[] out);

    static native String loadSample(float[] mono, int rate, String name, int slot, int root, boolean loop, int loopStart, int loopEnd);
    /** a WAV file's bytes: its smpl chunk's root key and loop are used */
    static native String loadWav(byte[] wav, String name, int slot, int root, boolean loop);
    /** a CMI disk image: all its voices into the free user voices; returns the count, or an error message */
    static native String loadDisk(byte[] image, String fileName);
    /** a CMI .VC voice file into one slot; "" or the error */
    static native String loadVc(byte[] vc, String name, String from, int slot);
    static native int firstFreeUser();
    static native int userCount();
    static native void clearUser(int k);
    static native boolean userLoaded(int k);
    static native int userSlot(int k);

    // MIDI songs (Page R)
    static final int GM_KIT = -2;
    static native String importMidi(byte[] midi, String name);
    static native int songCount();
    static native String songName(int k);
    static native void selectSongAt(int k);   // -1: the Page R patterns
    static native int currentSong();
    static native void removeSong();
    /** name, bpm, num, den, length (beats), beat now (tab separated) */
    static native String songInfo();
    /** track name, voice, used, muted */
    static native String songTrack(int t);
    static native void setSongVoice(int t, int voice);
    static native void setSongMute(int t, boolean muted);
    static native void gmVoices();
    static native void songPlay(boolean on);
    static native boolean songRunning();
    static native boolean songLoop();
    static native void setSongLoop(boolean on);
    /** four ints per note: track, start, end (1/10000 of the song), pitch in the track's range (0..1000) */
    static native int[] songNotes();
    static final int SONG_TRK = 16;
    /** the song (track -1) or one track as a 16-bit WAV, written to this file descriptor (closed when done) */
    static native String exportSongWav(int fd, int track);
    static native String exportSongMidi(int fd);
    static native int songTrackCount();
    /** five ints per note: track, start, length (1/480 beat), pitch, velocity */
    static native int[] songNoteList();
    static native void setSongNoteList(int[] notes);
    static native boolean undoSongEdit();
    /** the grid: a note on a song track for a moment */
    static native void auditionSong(int track, int note);
    /** the grid's edits on notes (5 ints each) and a selection; returns 6 ints per note (the 5 and selected 0/1). See native.cpp for ops */
    static native int[] gridOp(int[] notes, boolean[] sel, int track, int op, float a, float b, float c, float d, boolean e);
    static native int clipboardSize();
    static native void songPlayFrom(double beat);

    // the drum kit of the selected song
    static native int[] kitNotes(boolean all);
    /** voice chosen (or ""), level, pan, tune, mute, solo, voice playing it, GM name (tab separated) */
    static native String kitPiece(int note);
    static native void setKitPiece(int note, float level, float pan, float tune, boolean mute, boolean solo);
    static native void setKitVoice(int note, int sound);
    static native void setKitCatalogVoice(int note, int catIndex);
    static native void auditionKit(int note);
    static native void resetKit();
    static native float kitMeter(int note);
    // recording
    static native String recStart(int track, double from, boolean countIn, double quant, boolean replace);
    static native void recPoll();
    static native String recStop();
    static native boolean recording();
    static native boolean clickOn();
    static native void setClick(boolean on);
    // the light pen
    static native float[] penGet();
    static native void penLine(int box, int x0, float v0, int x1, float v1);
    static native void penOp(int box, int op);
    static native void penLive();
    static native boolean penIsLive();
    static native void setPenLive(boolean on);
    static native int penSlotNow();
    static native String penSaveAs();
    static native boolean penFromKeyboard();
    static native String penExportWav(int fd);
    static native String penState();
    static native void setPenState(String s);
    static native void newSong();
    static native String songWhy(int t);

    // mixer: strips 0 keyboard, 1..8 Page R tracks, 9..24 song tracks; what 0 fader, 1 pan (-1 the track's own), 2 mute, 3 solo, 4 meter
    static final int STRIPS = 25;
    static native float mixGet(int strip, int what);
    static native void mixSet(int strip, int what, float v);
    static native float masterMeter();

    // the effects section: a REV7 on the mixer's aux send (mixer strip FX_STRIP is its return; mixGet/mixSet what 5: a send)
    static final int FX_STRIP = 25;
    static native int fxCount();
    /** id, name, min, max, step, toggle 1/0, choices ("label=value|..."), shown 1/0 (tab separated) */
    static native String fxParam(int i);
    static native float fxGet(int i);
    static native void fxSet(int i, float v);
    static native String fxFormat(int i);
    static native boolean fxRelevant(int i);
    static native int fxPresetCount();
    static native String fxPresetName(int k);
    static native void fxLoadPreset(int k);
    static native int fxCurrentPreset();
    /** the dice: a random preset, or (settings) a random program with rolled settings; returns what it rolled */
    static native String fxDice(boolean settings);
    static native void fxNoPreset();
    static native String fxProgName(int n);
    static native boolean fxOn();
    static native void setFxOn(boolean on);
    static native String fxState();
    static native void setFxState(String s);
    static native int removeDuplicateSongs();
    static native String saveDrawn(float[] start, float[] end, boolean asNew);
    static native boolean hasDrawn();

    // the CMI library (catalogue of the library folder's disks and .VC files)
    static native void catalogClear();
    /** which scan pass reads a library file: 0 none, 1 HFE (first when hfeFirst), 2 other disk images, 3 .VC */
    static native int libraryRole(String fileName, boolean hfeFirst);
    static native int catalogHfeDisks();
    static native int catalogHfeSetAside();
    static native void catalogAdd(byte[] file, String fileName, String folder);
    static native int catalogCount();
    static native int catalogDisks();
    /** "NAME\tDISK\tlooped" per voice, a line each */
    static native String catalogList();
    static native int connectToLibrary();
    static native void useCatalogVoice(int track, int catIndex);
    /** a library voice into the user voices (reused if there): its sound number, or -1 */
    static native int loadCatalogVoice(int catIndex);
    /** Page R's tracks and the keyboard get CMI voices from the library when they have none */
    static native void connectDefaults();
    /** an empty voice (nothing loaded there) */
    static native boolean isBlankSound(int i);

    // factory presets (Page R): the IIx factory library, each found in the library folder
    static native int presetCount();
    /** name, disk file, category, note, found 1/0, matched by hand 1/0, where found (tab separated) */
    static native String presetInfo(int n);
    /** a char per preset: '1' found, '0' missing, '*' matched by hand */
    static native String presetFound();
    static native boolean loadPreset(int n);
    static native int currentPreset();
    static native void choosePresetVoice(int n, int catIndex);
    static native String getPresetChoices();
    static native void setPresetChoices(String text);
    static native void rememberKit(int track);
    static native String getRemembered();
    static native void setRemembered(String text);

    /** a song file (name without extension): its place in the song list if loaded, else -1 */
    static native int songLoadedAs(String fileBase);

    static native byte[] saveState();
    static native void loadState(byte[] data);
}
