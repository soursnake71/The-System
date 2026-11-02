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
    /** What the screen asked for: 0 nothing, 1 open a file, 2 start recording, 3 stop recording. */
    static native int pollRequest();
    static native void setRecordingState(boolean recording, float seconds);
    /** A mono sound (UI thread): it becomes the sampler's source. */
    static native void setSample(float[] data, int rate, String name);
}
