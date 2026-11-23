package com.thesnakedevice.wheel;

import android.content.SharedPreferences;
import android.os.SystemClock;

/**
 * The control state shared by the touch screen and MIDI: a mirror of every engine setting (pushed to the engine with
 * Native.set and saved in SharedPreferences), the pad mode, the MIDI focus deck, the pads (same actions as the plugin's
 * WheelPlugin.cpp padActions) and the MIDI maps (MPC pads, the MPC Sample's control surface, the Numark MixTrack Go
 * (plugins/wheel/controllers/MixtrackGo.md), CC 1 / pitch bend = crossfader).
 * Settings may be written from the MIDI thread and the UI thread; values are single floats so no locking is needed.
 */
final class Ctl {
    static final int N_IDS = 72;
    static final double REV_SECONDS = 1.8;            // one platter turn at 33 1/3
    static final double ENC_TICKS_PER_REV = 32;       // the MPC Sample's big encoder
    static final long HAND_RELEASE_MS = 60;           // the hand lifts this long after the encoder stops

    static final int MODE_CUES = 0, MODE_LOOPS = 1, MODE_ROLLS = 2, MODE_TRANSPORT = 3;
    static final String[] MODE_NAMES = { "HOT CUE", "AUTO LOOP", "ROLL", "TRANSPORT" };
    static final String[] MODE_SHORT = { "CUES", "LOOP", "ROLL", "TRANS" };
    static final double[] LOOP_BEATS = { .25, .5, 1, 2, 4, 8, 16, 32 };
    static final double[] ROLL_BEATS = { 1. / 32, 1. / 16, 1. / 8, .25, .5, 1, 2, 4 };
    static final String[] LOOP_NAMES = { "1/4", "1/2", "1", "2", "4", "8", "16", "32" };
    static final String[] ROLL_NAMES = { "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4" };
    static final String[] TRANSPORT_NAMES = { "PLAY", "CUE", "SYNC", "CENSOR", "NUDGE -", "NUDGE +", "LOOP IN", "OUT/EXIT" };
    static final String[] FX_NAMES = { "FX OFF", "ECHO", "PING-PONG", "REVERB", "FLANGER", "PHASER", "CRUSH", "GATE" };
    /** What D_FX_PARAM does for each effect (Engine.cpp: echo / flanger / phaser feedback, reverb size, crush depth, gate chop). */
    static final String[] FX_PARAM_NAMES = { "FX PARAM", "FEEDBACK", "FEEDBACK", "SIZE", "FEEDBACK", "FEEDBACK", "CRUSH", "CHOP" };
    static final double[] FX_BEATS = { 1. / 16, 1. / 8, .25, .5, .75, 1, 2, 4, 8 };
    static final String[] FX_BEAT_NAMES = { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8" };
    static final float[] RANGES = { .08f, .16f, .50f };

    // the MPC Sample's control surface (channel 10 pads, channel 1 buttons / knobs / fader / encoder)
    static final int[] SAMPLE_PAD_NOTES = { 37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53 };
    static final int SB_KNOB_FX = 0, SB_PAD_FX = 1, SB_SAMPLE = 6, SB_SEQ = 7, SB_ERASE = 9, SB_NOTE_REPEAT = 11, SB_B1 = 12,
            SB_B2 = 13, SB_B3 = 14, SB_PAD_BANK = 35, SB_SAMPLE_SELECT = 36, SB_MUTE = 43, SB_SHIFT = 49, SB_TAP_TEMPO = 53,
            SB_PLUS = 54, SB_MINUS = 55, SB_LOOP = 74, SB_ENC_PRESS = 111, SB_PLAY = 82, SB_STOP = 81;
    static final int SCC_K1 = 16, SCC_K2 = 17, SCC_K3 = 18, SCC_MAIN = 19, SCC_FADER = 20, SCC_ENCODER = 100;
    static final int PAD_BASE = 37;

    // ---- Numark MixTrack Go (plugins/wheel/controllers/MixtrackGo.md; channels 1-based: decks 1 / 2, pads 5 / 6, global 16)
    /**
     * Jog ticks (CC 6 steps) per platter turn. Numark publishes nothing for the MixTrack Go; 1011 is what Numark's
     * other jog wheels send (Mixxx's MixTrack Pro 3 / Platinum mappings). Unverified: the "Jog sensitivity" setting
     * in SETUP scales it.
     */
    static final double MT_JOG_TICKS_PER_TURN = 1011;
    /** Seconds pushed per jog turn with vinyl off (nudge), and per turn of shift + jog (fast seek). */
    static final double MT_NUDGE_PER_TURN = .25, MT_SEEK_PER_TURN = 8;
    static final float[] JOG_SENS = { .25f, .5f, .75f, 1f, 1.5f, 2f, 3f, 4f };
    static final int MT_CH_A = 1, MT_CH_B = 2, MT_CH_PADS_A = 5, MT_CH_PADS_B = 6, MT_CH_GLOBAL = 16;
    static final int MT_PLAY = 0, MT_CUE = 1, MT_SYNC = 2, MT_SYNC_OFF = 3, MT_CENSOR = 4, MT_TO_START = 5, MT_JOG_TOUCH = 6,
            MT_PFL = 27, MT_SHIFT = 32, MT_FX = 70, MT_SLIP = 72, MT_VINYL = 80, MT_PITCH_RESET = 84;
    static final int MT_CC_JOG = 6, MT_CC_PITCH = 9, MT_CC_PITCH_LSB = 41, MT_CC_LEVEL = 22, MT_CC_FILTER = 26, MT_CC_JOG_SHIFT = 43;
    static final int MT_PAD_MODE = 0, MT_PAD_FIRST = 20, MT_PAD_SHIFT_FIRST = 28, MT_PAD_MODE_LED_FIRST = 1;
    static final int MT_G_LOAD_A = 2, MT_G_LOAD_B = 3, MT_G_BROWSE_PUSH = 7, MT_G_SHIFT = 50, MT_G_FX = 70, MT_G_FX_NEXT = 71,
            MT_G_FILTER_LOW = 72, MT_G_VINYL = 73;
    static final int MT_G_CC_BROWSE = 0, MT_G_CC_BROWSE_SHIFT = 1, MT_G_CC_XFADER = 8, MT_G_CC_MAIN = 10, MT_G_CC_CUE = 12;
    static final int MT_BROWSE_PAGE = 10;

    /** Where the engine is reached: {@link Native} in the app, a recorder in the tests. */
    interface Engine {
        void cmd(int type, int deck, int i, double v, boolean on);
        void set(int id, float value);
        void status(int deck, double[] out);
        void trackInfo(int deck, double[] out7);
        void cues(int deck, double[] t, int[] rgb);
    }
    static final Engine NATIVE = new Engine() {
        @Override public void cmd(int type, int deck, int i, double v, boolean on) { Native.cmd(type, deck, i, v, on); }
        @Override public void set(int id, float value) { Native.set(id, value); }
        @Override public void status(int deck, double[] out) { Native.status(deck, out); }
        @Override public void trackInfo(int deck, double[] out7) { Native.trackInfo(deck, out7); }
        @Override public void cues(int deck, double[] t, int[] rgb) { Native.cues(deck, t, rgb); }
    };
    static volatile Engine engine = NATIVE;

    /** What MIDI asks of the screen (called on the MIDI thread; the activity posts to the UI thread). */
    interface Browser {
        void toggle();
        void move(int delta);
        void select(int deck);
        void back();
        boolean isOpen();
        /** Browse push (MixTrack Go): in the crates, go into the tracks; in the tracks, nothing (LOAD A / B loads). */
        void enter();
        /** LOAD A / B (MixTrack Go): loads the selected track on the deck; the library stays as it is. */
        void load(int deck);
    }

    private final float[] v = new float[N_IDS];
    private SharedPreferences prefs;
    volatile boolean dirty;
    volatile int padMode = MODE_CUES;
    volatile int focus = 0;
    volatile boolean shift, erase, sampleSeen;
    /** A MixTrack Go is connected (its name) or has been heard (its messages). */
    volatile boolean mixtrackSeen;
    /** MixTrack Go state: vinyl (jog scratches) per deck, shift per deck / global, the FILTER / LOW knob mode, jog touch. */
    final boolean[] vinyl = { true, true };
    private final boolean[] mtShift = new boolean[2], jogTouch = new boolean[2];
    private volatile boolean mtShiftG;
    volatile boolean filterLow;
    private final int[] pitchMsb = { -1, -1 }, pitchLsb = { -1, -1 };
    /** Jog sensitivity (SETUP): ticks per turn = MT_JOG_TICKS_PER_TURN / jogSens. */
    volatile float jogSens = 1f;
    volatile Browser browser;
    volatile long lastMidiMs;
    /** Seconds of track across the waveforms (pinch zoom); 0 = automatic from the screen width. */
    volatile float waveZoom;
    static final float ZOOM_MIN = 2, ZOOM_MAX = 32;

    static int did(int deck, int k) { return deck * 32 + k; }   // Native.deckId
    static int gid(int k) { return 64 + k; }                    // Native.globalId

    private static boolean known(int id) {
        if (id >= 64) return id - 64 <= Native.G_CUE_GAIN;
        return id % 32 <= Native.D_FX_ON;
    }

    /** Reads the engine's defaults, replaces them with the saved values and pushes everything to the engine. */
    void init(SharedPreferences sp) {
        prefs = sp;
        for (int id = 0; id < N_IDS; ++id) {
            if (!known(id)) continue;
            float def;
            try { def = Native.get(id); } catch (Throwable t) { def = 0; }
            if (Float.isNaN(def)) def = 0;
            v[id] = sp.getFloat("p" + id, def);
        }
        for (int d = 0; d < 2; ++d) {
            int r = did(d, Native.D_RANGE);
            if (v[r] < .04f) v[r] = .08f;
            int fx = did(d, Native.D_FX_BEATS);
            if (v[fx] <= 0) v[fx] = .75f;
            v[did(d, Native.D_FX_ON)] = 0;               // effects start off
            int type = did(d, Native.D_FX_TYPE);
            if (Math.round(v[type]) <= 0) v[type] = 1;    // echo, so FX ON does something out of the box
        }
        padMode = Math.max(0, Math.min(3, sp.getInt("padMode", MODE_CUES)));
        for (int d = 0; d < 2; ++d) vinyl[d] = sp.getBoolean("vinyl" + d, true);
        jogSens = Math.max(JOG_SENS[0], Math.min(JOG_SENS[JOG_SENS.length - 1], sp.getFloat("jogSens", 1f)));
        float z = sp.getFloat("waveZoom", 0);
        waveZoom = z <= 0 ? 0 : Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, z));
        for (int id = 0; id < N_IDS; ++id) if (known(id)) engine.set(id, v[id]);
    }

    void save() {
        if (prefs == null || !dirty) return;
        SharedPreferences.Editor e = prefs.edit();
        for (int id = 0; id < N_IDS; ++id) if (known(id)) e.putFloat("p" + id, v[id]);
        e.putInt("padMode", padMode);
        e.putFloat("waveZoom", waveZoom);
        for (int d = 0; d < 2; ++d) e.putBoolean("vinyl" + d, vinyl[d]);
        e.putFloat("jogSens", jogSens);
        e.apply();
        dirty = false;
    }

    float get(int id) { return v[id]; }
    float get(int deck, int k) { return v[did(deck, k)]; }
    boolean on(int deck, int k) { return v[did(deck, k)] > .5f; }

    void set(int id, float value) {
        if (v[id] == value) return;
        v[id] = value;
        engine.set(id, value);
        dirty = true;
    }
    void set(int deck, int k, float value) { set(did(deck, k), value); }
    void toggle(int deck, int k) { set(deck, k, on(deck, k) ? 0f : 1f); }
    void setPadMode(int m) { padMode = ((m % 4) + 4) % 4; dirty = true; }
    void setWaveZoom(float seconds) { waveZoom = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, seconds)); dirty = true; }
    void setVinyl(int deck, boolean on) { vinyl[deck] = on; dirty = true; }
    /** SETUP's jog sensitivity button: the next step (wraps). */
    void nextJogSens() {
        int i = 0;
        for (int k = 0; k < JOG_SENS.length; ++k) if (Math.abs(JOG_SENS[k] - jogSens) < .01f) i = k;
        jogSens = JOG_SENS[(i + 1) % JOG_SENS.length];
        dirty = true;
    }
    double jogTicksPerTurn() { return MT_JOG_TICKS_PER_TURN / Math.max(.05f, jogSens); }

    // ---- EQ knob mapping: 0 = kill (-26 dB), .5 = 0 dB, 1 = +6 dB
    static float eqDb(float n) { return n < .5f ? -26f * (1f - 2f * n) : 6f * (2f * n - 1f); }
    static float eqNorm(float db) { return db < 0 ? .5f - (-db / 26f) * .5f : .5f + db / 12f; }
    static float trimDb(float n) { return -12f + 24f * n; }
    static float trimNorm(float db) { return (db + 12f) / 24f; }
    static float masterDb(float n) { return -24f + 30f * n; }
    static float masterNorm(float db) { return (db + 24f) / 30f; }

    static void cmd(int type, int deck, int i, double val, boolean on) { engine.cmd(type, deck, i, val, on); }

    // ---- the hand on the record: the screen's platters / waveforms and the MIDI encoder share it
    private final boolean[][] hand = new boolean[2][3];   // [deck][source]: 0 platter, 1 waveform, 2 MIDI
    private final long[] midiMove = new long[2];
    static final int HAND_PLATTER = 0, HAND_WAVE = 1, HAND_MIDI = 2;

    synchronized void touch(int deck, int src, boolean on) {
        boolean before = hand[deck][0] || hand[deck][1] || hand[deck][2];
        hand[deck][src] = on;
        boolean after = hand[deck][0] || hand[deck][1] || hand[deck][2];
        if (before != after) cmd(Native.C_TOUCH, deck, 0, 0, after);
    }
    synchronized boolean handOn(int deck) { return hand[deck][0] || hand[deck][1] || hand[deck][2]; }
    void move(int deck, double seconds) { if (seconds != 0) cmd(Native.C_MOVE, deck, 0, seconds, true); }

    void encoderScratch(int deck, double revs) {
        touch(deck, HAND_MIDI, true);
        midiMove[deck] = SystemClock.uptimeMillis();
        move(deck, revs * REV_SECONDS);
    }

    /** Called every frame: lifts the encoder's hand once it has stopped. */
    void tick() {
        long now = SystemClock.uptimeMillis();
        for (int d = 0; d < 2; ++d) {
            boolean h;
            synchronized (this) { h = hand[d][HAND_MIDI] && !jogTouch[d]; }
            if (h && now - midiMove[d] > HAND_RELEASE_MS) touch(d, HAND_MIDI, false);
        }
    }

    // ---- pads: slot 0..7 of a deck; ctx 0 = screen, 1 = MIDI (the mode a pad was pressed in is kept until release)
    private final int[][][] heldMode = new int[2][2][8];
    { for (int[][] a : heldMode) for (int[] b : a) java.util.Arrays.fill(b, -1); }
    private final double[] st = new double[Native.ST_COUNT];

    void padAction(int deck, int s, boolean down, float vel, int mode) {
        switch (mode) {
            case MODE_CUES: cmd(Native.C_HOT_CUE, deck, s, 0, down); break;
            case MODE_LOOPS: if (down) cmd(Native.C_AUTO_LOOP, deck, 0, LOOP_BEATS[s], true); break;
            case MODE_ROLLS: cmd(Native.C_ROLL, deck, 0, ROLL_BEATS[s], down); break;
            default:
                switch (s) {
                    case 0: if (down) cmd(Native.C_PLAY_PAUSE, deck, 0, 0, true); break;
                    case 1: cmd(Native.C_CUE, deck, 0, 0, down); break;
                    case 2: if (down) cmd(Native.C_SYNC, deck, 0, 0, true); break;
                    case 3: cmd(Native.C_CENSOR, deck, 0, 0, down); break;
                    case 4: cmd(Native.C_NUDGE, deck, 0, down ? -(.25 + .75 * vel) : 0, down); break;
                    case 5: cmd(Native.C_NUDGE, deck, 0, down ? (.25 + .75 * vel) : 0, down); break;
                    case 6: if (down) cmd(Native.C_LOOP_IN, deck, 0, 0, true); break;
                    default:
                        if (down) {
                            boolean looping;
                            synchronized (st) { engine.status(deck, st); looping = st[Native.ST_LOOPING] > .5; }
                            cmd(looping ? Native.C_LOOP_EXIT : Native.C_LOOP_OUT, deck, 0, 0, true);
                        }
                        break;
                }
        }
    }

    synchronized void pad(int ctx, int deck, int slot, boolean down, float vel) {
        if (slot < 0 || slot > 7 || deck < 0 || deck > 1) return;
        int[] held = heldMode[ctx][deck];
        int mode;
        if (down) {
            mode = padMode;
            if (held[slot] >= 0) padAction(deck, slot, false, 0, held[slot]);
            held[slot] = mode;
        } else {
            mode = held[slot];
            if (mode < 0) return;
            held[slot] = -1;
        }
        padAction(deck, slot, down, Math.max(0f, Math.min(1f, vel)), mode);
    }
    /** A held hot cue pad turned into "clear": releases it without the up action. */
    synchronized void padForget(int ctx, int deck, int slot) { heldMode[ctx][deck][slot] = -1; }
    /** The pad is held down (for the MixTrack Go's pad LEDs). */
    synchronized boolean padHeld(int ctx, int deck, int slot) { return heldMode[ctx][deck][slot] >= 0; }

    void releaseAllPads(int ctx) { for (int d = 0; d < 2; ++d) for (int s = 0; s < 8; ++s) pad(ctx, d, s, false, 0); }

    // MPC 4x4: pad index = row * 4 + column, row 0 at the bottom; columns 0-1 deck A, 2-3 deck B
    static int padDeck(int p) { return (p % 4) < 2 ? 0 : 1; }
    static int padSlot(int p) { return (p / 4) * 2 + (p % 2); }
    static int padIndexOf(int deck, int slot) { return (slot / 2) * 4 + deck * 2 + slot % 2; }
    static int samplePad(int note) { for (int i = 0; i < 16; ++i) if (SAMPLE_PAD_NOTES[i] == note) return i; return -1; }

    // ---- MIDI (channel 1..16)
    /** Only a MixTrack Go sends these: the crossfader (CC 8 on channel 16) and the pads (notes 20-31 on channels 5 / 6). */
    static boolean mixtrackSignature(boolean cc, int ch, int num) {
        if (cc) return ch == MT_CH_GLOBAL && num == MT_G_CC_XFADER;
        return (ch == MT_CH_PADS_A || ch == MT_CH_PADS_B) && num >= MT_PAD_FIRST && num <= MT_PAD_SHIFT_FIRST + 3;
    }

    /** A message from a device of unknown kind: the MixTrack Go map once its messages have been heard, else the general map. */
    void midiNote(int ch, int note, int vel, boolean down) {
        if (!mixtrackSeen && down && mixtrackSignature(false, ch, note)) mixtrackSeen = true;
        if (mixtrackSeen && isMixtrackChannel(ch)) { mixtrackNote(ch, note, vel, down); return; }
        otherNote(ch, note, vel, down);
    }

    void midiCc(int ch, int cc, int val) {
        if (!mixtrackSeen && mixtrackSignature(true, ch, cc)) mixtrackSeen = true;
        if (mixtrackSeen && isMixtrackChannel(ch)) { mixtrackCc(ch, cc, val); return; }
        otherCc(ch, cc, val);
    }

    static boolean isMixtrackChannel(int ch) {
        return ch == MT_CH_A || ch == MT_CH_B || ch == MT_CH_PADS_A || ch == MT_CH_PADS_B || ch == MT_CH_GLOBAL;
    }

    /** A message from a device that is not a MixTrack Go (MPC, MPC Sample, anything else). */
    void otherNote(int ch, int note, int vel, boolean down) {
        lastMidiMs = SystemClock.uptimeMillis();
        if (!sampleSeen && ch == 10 && down && samplePad(note) >= 0) sampleSeen = true;
        if (sampleSeen && sampleNote(ch, note, vel, down)) return;
        float fv = down ? vel / 127f : 0f;
        if (ch == 2 || ch == 3) { pad(1, ch - 2, note % 8, down, fv); return; }
        if (ch == 10) return;
        int rel = note - PAD_BASE;
        if (rel >= 0 && rel < 16) pad(1, padDeck(rel), padSlot(rel), down, fv);
        else if (rel >= 16 && rel < 20 && down) setPadMode(rel - 16);
    }

    void otherCc(int ch, int cc, int val) {
        lastMidiMs = SystemClock.uptimeMillis();
        if (!sampleSeen && ch == 1 && cc == SCC_ENCODER) sampleSeen = true;
        if (sampleSeen && ch == 1 && sampleCc(cc, val)) return;
        if (cc == 120 || cc == 123) { releaseAllPads(1); return; }
        if (cc == 1) set(gid(Native.G_XFADER), val / 127f * 2f - 1f);   // out of the box a fader is the crossfader
    }

    void midiPitchBend(int ch, int value14) {
        lastMidiMs = SystemClock.uptimeMillis();
        set(gid(Native.G_XFADER), Math.max(-1f, Math.min(1f, value14 / 16383f * 2f - 1f)));
    }

    private boolean sampleCc(int cc, int val) {
        int f = focus;
        boolean sh = shift;
        float n = val / 127f;
        switch (cc) {
            case SCC_FADER: set(gid(Native.G_XFADER), n * 2f - 1f); return true;
            case SCC_MAIN: set(gid(Native.G_MASTER), masterDb(n)); return true;
            case SCC_K1:
                if (sh) set(f, Native.D_HI, eqDb(n)); else set(f, Native.D_FILTER, n * 2f - 1f);
                return true;
            case SCC_K2: set(f, sh ? Native.D_MID : Native.D_LOW, eqDb(n)); return true;
            case SCC_K3:
                if (sh) set(f, Native.D_FADER, n); else set(f, Native.D_PITCH, n * 2f - 1f);
                return true;
            case SCC_ENCODER: {
                int dl = val < 64 ? val : val - 128;
                Browser b = browser;
                if (b != null && b.isOpen()) b.move(dl);
                else if (sh) cmd(Native.C_BEND, f, 0, dl * .01, true);
                else encoderScratch(f, dl / ENC_TICKS_PER_REV);
                return true;
            }
            default: return cc != 1 && cc != 120 && cc != 123;   // other surface CCs: ignore
        }
    }

    private boolean sampleNote(int ch, int note, int vel, boolean down) {
        if (ch == 10) {
            int k = samplePad(note);
            if (k < 0) return false;
            if (erase && padMode == MODE_CUES) {
                if (down) cmd(Native.C_CLEAR_CUE, padDeck(k), padSlot(k), 0, true);
                return true;
            }
            pad(1, padDeck(k), padSlot(k), down, down ? vel / 127f : 0f);
            return true;
        }
        if (ch != 1) return false;
        int f = focus;
        boolean sh = shift;
        Browser b = browser;
        switch (note) {
            case SB_SHIFT: shift = down; return true;
            case SB_ERASE: erase = down; return true;
            case SB_B1: if (down) focus = 0; return true;
            case SB_B2: if (down) focus = 1; return true;
            case SB_B3: if (down) cmd(Native.C_SYNC, f, 0, 0, true); return true;
            case SB_PLAY: if (down) cmd(Native.C_PLAY_PAUSE, f, 0, 0, true); return true;
            case SB_STOP: cmd(Native.C_CUE, f, 0, 0, down); return true;
            case SB_PAD_BANK: if (down) setPadMode(padMode + (sh ? 3 : 1)); return true;
            case SB_LOOP:
                if (down) {
                    if (sh) {
                        boolean looping;
                        synchronized (st) { engine.status(f, st); looping = st[Native.ST_LOOPING] > .5; }
                        cmd(looping ? Native.C_LOOP_EXIT : Native.C_RELOOP, f, 0, 0, true);
                    } else cmd(Native.C_AUTO_LOOP, f, 0, 4, true);
                }
                return true;
            case SB_MINUS:
                if (sh) { if (down) cmd(Native.C_LOOP_HALVE, f, 0, 0, true); }
                else cmd(Native.C_NUDGE, f, 0, down ? -1 : 0, down);
                return true;
            case SB_PLUS:
                if (sh) { if (down) cmd(Native.C_LOOP_DOUBLE, f, 0, 0, true); }
                else cmd(Native.C_NUDGE, f, 0, down ? 1 : 0, down);
                return true;
            case SB_NOTE_REPEAT: cmd(Native.C_CENSOR, f, 0, 0, down); return true;
            case SB_KNOB_FX: if (down) toggle(f, Native.D_FX_ON); return true;
            case SB_PAD_FX: if (down) toggle(f, Native.D_SLIP); return true;
            case SB_MUTE: if (down) toggle(f, Native.D_PFL); return true;
            case SB_TAP_TEMPO: if (down) toggle(f, Native.D_KEYLOCK); return true;
            case SB_SAMPLE: case SB_SAMPLE_SELECT: if (down && b != null) b.toggle(); return true;
            case SB_ENC_PRESS:
                if (down && b != null) { if (b.isOpen()) b.select(f); else b.toggle(); }
                return true;
            case SB_SEQ: if (down && b != null && b.isOpen()) b.back(); return true;
            default: return true;   // every other surface button: nothing (never a pad)
        }
    }

    // ---- Numark MixTrack Go
    boolean mtShift(int deck) { return mtShiftG || mtShift[deck]; }
    static int rel7(int val) { return val < 64 ? val : val - 128; }   // relative CC: two's complement

    /** A note from a MixTrack Go (channel 1..16). */
    void mixtrackNote(int ch, int note, int vel, boolean down) {
        lastMidiMs = SystemClock.uptimeMillis();
        mixtrackSeen = true;
        if (ch == MT_CH_A || ch == MT_CH_B) mtDeckNote(ch - MT_CH_A, note, down);
        else if (ch == MT_CH_PADS_A || ch == MT_CH_PADS_B) mtPadNote(ch - MT_CH_PADS_A, note, vel, down);
        else if (ch == MT_CH_GLOBAL) mtGlobalNote(note, down);
    }

    /** A control change from a MixTrack Go. */
    void mixtrackCc(int ch, int cc, int val) {
        lastMidiMs = SystemClock.uptimeMillis();
        mixtrackSeen = true;
        if (cc == 120 || cc == 123) { releaseAllPads(1); return; }
        if (ch == MT_CH_A || ch == MT_CH_B) mtDeckCc(ch - MT_CH_A, cc, val);
        else if (ch == MT_CH_GLOBAL) mtGlobalCc(cc, val);
    }

    private void mtDeckNote(int d, int note, boolean down) {
        switch (note) {
            case MT_PLAY: if (down) cmd(Native.C_PLAY_PAUSE, d, 0, 0, true); break;
            case MT_CUE: cmd(Native.C_CUE, d, 0, 0, down); break;
            case MT_SYNC: if (down) cmd(Native.C_SYNC, d, 0, 0, true); break;
            case MT_SYNC_OFF: if (down) cmd(Native.C_SYNC_OFF, d, 0, 0, true); break;
            case MT_CENSOR: cmd(Native.C_CENSOR, d, 0, 0, down); break;
            case MT_TO_START: if (down) cmd(Native.C_SEEK, d, 0, 0, true); break;
            case MT_JOG_TOUCH:
                synchronized (this) { jogTouch[d] = down && vinyl[d]; }
                if (down) { if (vinyl[d]) { touch(d, HAND_MIDI, true); midiMove[d] = SystemClock.uptimeMillis(); } }
                else touch(d, HAND_MIDI, false);
                break;
            case MT_VINYL:
                if (down) {
                    setVinyl(d, !vinyl[d]);
                    if (!vinyl[d]) { synchronized (this) { jogTouch[d] = false; } touch(d, HAND_MIDI, false); }
                }
                break;
            case MT_PITCH_RESET: if (down) set(d, Native.D_PITCH, 0f); break;
            case MT_PFL: if (down) toggle(d, Native.D_PFL); break;
            case MT_FX: if (down) toggle(d, Native.D_FX_ON); break;
            case MT_SLIP: if (down) toggle(d, Native.D_SLIP); break;
            case MT_SHIFT: mtShift[d] = down; break;
            default: break;
        }
    }

    private void mtDeckCc(int d, int cc, int val) {
        float n = val / 127f;
        switch (cc) {
            case MT_CC_JOG:
                if (mtShift(d)) mtSeek(d, rel7(val));
                else mtJog(d, rel7(val));
                break;
            case MT_CC_JOG_SHIFT: mtSeek(d, rel7(val)); break;
            case MT_CC_PITCH:
                pitchMsb[d] = val;
                mtPitch(d);
                break;
            case MT_CC_PITCH_LSB:
                pitchLsb[d] = val;
                if (pitchMsb[d] >= 0) mtPitch(d);
                break;
            case MT_CC_LEVEL: set(d, Native.D_FADER, n); break;
            case MT_CC_FILTER:
                if (filterLow) set(d, Native.D_LOW, eqDb(n));
                else { float f = n * 2f - 1f; set(d, Native.D_FILTER, Math.abs(f) < .02f ? 0f : f); }
                break;
            default: break;
        }
    }

    /** Pitch fader: 14-bit when the LSB (CC 41) comes, MSB only otherwise. Top (0) = slower = -1. */
    private void mtPitch(int d) {
        float p = pitchLsb[d] >= 0 ? ((pitchMsb[d] << 7) | pitchLsb[d]) / 16383f : pitchMsb[d] / 127f;
        float v = Math.max(-1f, Math.min(1f, p * 2f - 1f));
        if (Math.abs(v) < .01f) v = 0f;   // centre detent
        set(d, Native.D_PITCH, v);
    }

    /** The jog's top: scratch in vinyl mode (the hand lands on the first tick, lifts 60 ms after the last), else nudge. */
    void mtJog(int d, int ticks) {
        if (ticks == 0) return;
        double turns = ticks / jogTicksPerTurn();
        if (vinyl[d]) encoderScratch(d, turns);
        else cmd(Native.C_BEND, d, 0, turns * MT_NUDGE_PER_TURN, true);
    }

    void mtSeek(int d, int ticks) {
        if (ticks != 0) cmd(Native.C_BEND, d, 0, ticks / jogTicksPerTurn() * MT_SEEK_PER_TURN, true);
    }

    private void mtPadNote(int d, int note, int vel, boolean down) {
        float fv = down ? vel / 127f : 0f;
        if (note == MT_PAD_MODE) { if (down) setPadMode(padMode + 1); return; }
        if (note >= MT_PAD_FIRST && note < MT_PAD_FIRST + 4) { pad(1, d, note - MT_PAD_FIRST, down, fv); return; }
        if (note >= MT_PAD_SHIFT_FIRST && note < MT_PAD_SHIFT_FIRST + 4) {
            int s = note - MT_PAD_SHIFT_FIRST;
            if (!down) pad(1, d, 4 + s, false, 0);                       // (an up without its down is ignored)
            else if (padMode == MODE_CUES) cmd(Native.C_CLEAR_CUE, d, s, 0, true);   // shift + pad: clear cue 1-4
            else pad(1, d, 4 + s, true, fv);
        }
    }

    private void mtGlobalNote(int note, boolean down) {
        Browser b = browser;
        switch (note) {
            case MT_G_SHIFT: mtShiftG = down; break;
            case MT_G_LOAD_A: case MT_G_LOAD_B: if (down && b != null) b.load(note == MT_G_LOAD_A ? 0 : 1); break;
            case MT_G_BROWSE_PUSH:
                if (!down || b == null) break;
                if (!b.isOpen()) b.toggle();
                else if (mtShiftG || mtShift[0] || mtShift[1]) b.back();
                else b.enter();
                break;
            case MT_G_FILTER_LOW: if (down) filterLow = !filterLow; break;
            case MT_G_VINYL:
                if (down) {
                    boolean on = !(vinyl[0] && vinyl[1]);
                    for (int d = 0; d < 2; ++d) {
                        setVinyl(d, on);
                        if (!on) { synchronized (this) { jogTouch[d] = false; } touch(d, HAND_MIDI, false); }
                    }
                }
                break;
            case MT_G_FX:
                if (down) {
                    float on = on(0, Native.D_FX_ON) && on(1, Native.D_FX_ON) ? 0f : 1f;
                    set(0, Native.D_FX_ON, on);
                    set(1, Native.D_FX_ON, on);
                }
                break;
            case MT_G_FX_NEXT:
                if (down) {
                    int t = Math.max(0, Math.min(7, Math.round(get(0, Native.D_FX_TYPE))));
                    int next = t % 7 + 1;   // 1..7: never "off"
                    set(0, Native.D_FX_TYPE, next);
                    set(1, Native.D_FX_TYPE, next);
                }
                break;
            default: break;
        }
    }

    private void mtGlobalCc(int cc, int val) {
        Browser b = browser;
        switch (cc) {
            case MT_G_CC_XFADER: set(gid(Native.G_XFADER), val / 127f * 2f - 1f); break;
            case MT_G_CC_BROWSE: case MT_G_CC_BROWSE_SHIFT: {
                if (b == null) break;
                int dl = rel7(val) * (cc == MT_G_CC_BROWSE_SHIFT ? MT_BROWSE_PAGE : 1);
                if (!b.isOpen()) b.toggle();
                if (dl != 0) b.move(dl);
                break;
            }
            default: break;   // CC 10 / 12 (main / cue level) are hardware
        }
    }
}
