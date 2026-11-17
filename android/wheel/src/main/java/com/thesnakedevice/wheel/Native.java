package com.thesnakedevice.wheel;

/**
 * The contract between the Android app (Java) and the native turntable engine (C++, src/main/cpp/wheel_jni.cpp, which
 * runs plugins/wheel/Engine.cpp: two decks, the mixer and beat FX, on an AAudio low-latency output stream).
 * Everything here is static; one engine per process. All methods are safe to call from any Java thread unless noted.
 */
public final class Native {
    static { System.loadLibrary("wheel"); }
    private Native() {}

    // ---- audio
    /** Opens the AAudio output (low latency, exclusive if possible, float stereo) and starts rendering. */
    public static native boolean start();
    public static native void stop();
    public static native int sampleRate();
    /** Output latency in ms as reported by AAudio (0 if unknown). */
    public static native double latencyMs();

    // ---- tracks
    /**
     * Puts a decoded track on a deck. Call from a background thread: it reads Serato data (hot cues with colours and
     * names, saved loops, beatgrid, BPM, key, title, artist) from the file through {@code fd} (a readable file
     * descriptor of the original file, or -1), then detects BPM + first beat and key where Serato gave none, builds
     * the 3-band waveform, and hands the track to the audio thread. Returns false on failure.
     * {@code title}/{@code artist} are fallbacks (e.g. from the file name) used when the tags have none.
     */
    public static native boolean loadTrack(int deck, float[] left, float[] right, int sampleRate, int fd, String path, String title, String artist);

    /** Tags only, for the library list: {title, artist, album, bpmText, keyText (Camelot like "8A" or ""), seratoFlag "1"/"0"}. */
    public static native String[] readTags(int fd, String fileName);
    /** Track paths stored in a Serato .crate file (UTF-16BE "ptrk" records), in order. */
    public static native String[] parseCrate(byte[] crateFile);

    /** Track info for the deck now: {bpm, firstBeat, key (0..23, -1), sampleRate, lengthSeconds, hasTrack 0/1, serial}. serial changes on every load. */
    public static native void trackInfo(int deck, double[] out7);
    /** Copies of the deck's waveform bands (150 columns a second, 0..255 as unsigned bytes): band 0 low, 1 mid, 2 high, 3 peak. */
    public static native byte[] wave(int deck, int band);
    /** Title and artist of the deck's track: {title, artist, path}. */
    public static native String[] trackText(int deck);

    // ---- live state (cheap, call every frame)
    public static final int ST_POS = 0, ST_RATE = 1, ST_ANGLE = 2, ST_BPM = 3, ST_PLAYING = 4, ST_TOUCHED = 5, ST_LOOPING = 6,
            ST_LOOP_A = 7, ST_LOOP_B = 8, ST_CUE = 9, ST_PEAK_L = 10, ST_PEAK_R = 11, ST_SHADOW = 12, ST_SYNCED = 13,
            ST_CENSOR = 14, ST_ROLLING = 15, ST_COUNT = 16;
    /** Fills out[ST_COUNT] (booleans as 0/1, times in seconds, angle in revolutions, peaks linear). */
    public static native void status(int deck, double[] out);
    /** Hot cues: t[8] seconds (-1 = empty), rgb[8] 0xRRGGBB. */
    public static native void cues(int deck, double[] t, int[] rgb);
    /** Mixer meters {deckA, deckB, masterL, masterR}, linear peak. */
    public static native void meters(float[] out4);

    // ---- commands (queued to the audio thread, lock-free; v / on / i meaning per command)
    public static final int C_PLAY_PAUSE = 0,  // toggle start/stop
            C_PLAY = 1,        // on
            C_CUE = 2,         // on = down/up (CDJ cue)
            C_HOT_CUE = 3,     // i = 0..7, on = down/up
            C_CLEAR_CUE = 4,   // i
            C_AUTO_LOOP = 5,   // v = beats (toggle)
            C_LOOP_IN = 6, C_LOOP_OUT = 7, C_RELOOP = 8, C_LOOP_EXIT = 9, C_LOOP_HALVE = 10, C_LOOP_DOUBLE = 11,
            C_LOOP_MOVE = 12,  // v = beats
            C_ROLL = 13,       // v = beats, on = held
            C_SAVED_LOOP = 14, // i
            C_TOUCH = 15,      // on = hand on the record
            C_MOVE = 16,       // v = seconds of record moved by the hand (1 platter turn = 1.8 s)
            C_BEND = 17,       // v = seconds pushed (jog edge)
            C_NUDGE = 18,      // v = -1..1 held, 0 = release
            C_CENSOR = 19,     // on
            C_SEEK = 20,       // v = seconds
            C_SYNC = 21,       // toggle sync (follows the other deck while on; matches tempo + phase at once)
            C_SYNC_OFF = 22,
            C_GRID_SHIFT = 23, // v = seconds (relative)
            C_GRID_BPM = 24;   // v = bpm (0 = the track's own)
    public static native void cmd(int type, int deck, int i, double v, boolean on);

    // ---- settings (applied on the audio thread, smoothed)
    /** Deck settings: id = deck * 32 + one of these. */
    public static final int D_PITCH = 0,  // -1..1 of the fader
            D_RANGE = 1,      // 0.08, 0.16, 0.50
            D_RPM45 = 2, D_KEYLOCK = 3, D_SLIP = 4, D_QUANTIZE = 5,   // 0/1
            D_START_TIME = 6, D_BRAKE_TIME = 7,                       // seconds
            D_TRIM = 8,       // dB -12..12
            D_HI = 9, D_MID = 10, D_LOW = 11,                        // dB -26 (kill)..+6
            D_FILTER = 12,    // -1 (LP) .. 0 .. 1 (HP)
            D_FADER = 13,     // 0..1
            D_PFL = 14,       // 0/1
            D_XF_ASSIGN = 15, // 0 A, 1 thru, 2 B
            D_FX_TYPE = 16,   // 0 off, 1 echo, 2 ping-pong, 3 reverb, 4 flanger, 5 phaser, 6 crush, 7 gate
            D_FX_BEATS = 17,  // beats 0.0625..8
            D_FX_DEPTH = 18, D_FX_PARAM = 19,                        // 0..1
            D_FX_ON = 20;     // 0/1
    /** Mixer settings: id = 64 + one of these. */
    public static final int G_XFADER = 0,  // -1 (A) .. 1 (B)
            G_XF_CURVE = 1, G_FADER_CURVE = 2,  // 0..1
            G_HAMSTER = 3,    // 0/1
            G_MASTER = 4,     // dB -24..6
            G_CUE_MIX = 5, G_CUE_GAIN = 6;
    public static int deckId(int deck, int k) { return deck * 32 + k; }
    public static int globalId(int k) { return 64 + k; }
    public static native void set(int id, float value);
    public static native float get(int id);
}
