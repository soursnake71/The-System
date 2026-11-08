package com.thesnakedevice.sid;

/** The C++ side: the SID engine, its C64 screen and the AAudio stream (cpp/jni.cpp). */
final class Native {
    static { System.loadLibrary("omegasid"); }
    static native void init(String state);
    static native void start();
    static native void stop();
    static native String saveState();
    /** How many text rows the screen has (25 on a C64; more on a tall phone). */
    static native void setRows(int rows);
    /** For the big controls: "page|selected id|name|value|kind|0..1|preset name|preset number". */
    static native String editState();
    /** 0 set the selected setting (0..1), 1 step it (+-1), 2 reset it, 3 preset +-1, 4 go to page, 5 page +-1. */
    static native void editAction(int action, float value);
    /** Draws the C64 screen, 320 x (rows x 8), into px (ARGB). */
    static native void render(int[] px);
    /** action 0 down, 1 move, 2 up; x, y in C64 screen pixels. */
    static native void touch(int action, float x, float y);
    static native void wheel(float x, float y, float steps);
    static native void noteOn(int note, float velocity);
    static native void noteOff(int note);
    static native void seqToggle();
    static native boolean seqRunning();
    static native void panic();
    static native void midi(byte[] data, int offset, int length);
    /** Messages for the MIDI devices: fills times (System.nanoTime), bytes (3 each), lengths; returns how many. */
    static native int midiOut(long[] times, byte[] bytes, int[] lengths);
    static native byte[] midiFileToSave();
    static native String midiFileName();
    /** The type of the file to save: audio/midi, audio/wav or audio/mpeg. */
    static native String saveMime();
    static native void midiFileLoaded(byte[] data);
    /** What the screen asked for: 0 nothing, 1 open a sample, 2 start recording, 3 stop recording, 4 save a .mid, 5 open a .mid. */
    static native int pollRequest();
    static native void setRecordingState(boolean recording, float seconds);
    /** A mono sound (UI thread): it becomes the sampler's source. */
    static native void setSample(float[] data, int rate, String name);
}
