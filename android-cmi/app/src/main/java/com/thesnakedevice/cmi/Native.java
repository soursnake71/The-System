package com.thesnakedevice.cmi;

/** The C++ engine (plugins/cmi/core, the same code as the VST3) through JNI. */
final class Native {
    static { System.loadLibrary("omegacmi"); }
    private Native() {}

    // parameter indices (native.cpp)
    static final int LEVEL = 0, FILTER = 1, ATTACK = 2, DAMP = 3, SUSTAIN = 4, VIB = 5, VIBRATE = 6, VIBDELAY = 7, PORTA = 8, VELVOL = 9, VELFILT = 10,
            TRANSPOSE = 11, FINE = 12, LOOP = 13, SEG = 14, POLY = 15, BEND = 16, MASTER = 17, HISS = 18, SPREAD = 19, TEMPO = 20, SWING = 21, SONGMODE = 22;
    static final int TRACKS = 8, STEPS = 16, NPAT = 16, SONG_MAX = 64, USER_SLOTS = 200, NCH = 16;

    static native void start();
    static native void stop();
    static native void midi(byte[] data, int off, int len);
    static native void noteOn(int note, int vel);
    static native void noteOff(int note);
    static native void allOff();
    static native void preview(int track);

    static native int soundCount();
    static native int builtInCount();
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

    static native byte[] saveState();
    static native void loadState(byte[] data);
}
