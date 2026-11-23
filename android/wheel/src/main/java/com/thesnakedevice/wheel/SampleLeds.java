package com.thesnakedevice.wheel;

import android.media.midi.MidiInputPort;
import android.os.Handler;

import java.io.IOException;

/**
 * Lights the MPC Sample's pads and buttons from the decks (USB MIDI, sent to the MPC Sample's own input port).
 *
 * <p>Pads: SysEx {@code F0 47 7F 5C 65 00 04 <pad> <R> <G> <B> F7}, colour components 0..127. The pad byte is the
 * pad's index 0..15 in the plugin's pad order: pad 1..16 of the MPC Sample, i.e. {@link Ctl#SAMPLE_PAD_NOTES}[i]
 * (notes 37 36 42 82 / 40 38 46 44 / 48 47 45 43 / 49 55 51 53), pad 1 bottom left, rows of four going up, left two
 * columns deck A, right two deck B ({@link Ctl#padDeck} / {@link Ctl#padSlot}). <b>Assumption:</b> the SysEx pad
 * number counts the pads the same way the MPC Sample numbers them (0 = pad 1, the one that sends note 37); this has
 * not been checked on the hardware. If the pads light mirrored, only {@link #sysexPad} needs to change.
 *
 * <p>Buttons: {@code B0 <note> <value>} (CC on channel 1 with the button's note number), 0 off, 1 dim, 127 full.
 *
 * <p>Runs on the MIDI handler thread every {@link #PERIOD_MS}; only what changed is sent, at most
 * {@link #MAX_MESSAGES} messages a tick (the rest follows on the next ticks).
 */
final class SampleLeds {
    static final long PERIOD_MS = 40;
    static final int MAX_MESSAGES = 24;
    static final int OFF = 0, DIM = 1, FULL = 127;
    /** The lit buttons, in the order {@link #buttonValues} fills. */
    static final int[] BUTTONS = { Ctl.SB_PLAY, Ctl.SB_B1, Ctl.SB_B2, Ctl.SB_B3, Ctl.SB_LOOP, Ctl.SB_KNOB_FX, Ctl.SB_PAD_FX, Ctl.SB_MUTE, Ctl.SB_TAP_TEMPO };

    private final Ctl ctl;
    private final Handler handler;
    private MidiInputPort port;
    private boolean scheduled;
    private final int[] sentPad = new int[16], wantPad = new int[16];
    private final int[] sentBtn = new int[BUTTONS.length], wantBtn = new int[BUTTONS.length];
    private final double[][] st = new double[2][Native.ST_COUNT], info = new double[2][7], cueT = new double[2][8];
    private final int[][] cueRgb = new int[2][8];

    SampleLeds(Ctl ctl, Handler handler) { this.ctl = ctl; this.handler = handler; }

    /** Starts lighting through this port (the handler thread only). Everything is sent once. */
    void attach(MidiInputPort p) {
        port = p;
        java.util.Arrays.fill(sentPad, -1);
        java.util.Arrays.fill(sentBtn, -1);
        if (!scheduled) { scheduled = true; handler.post(tick); }
    }

    void detach() { port = null; }

    boolean attached() { return port != null; }

    /** All pads and buttons dark (the app is closing). */
    void lightsOff() {
        MidiInputPort p = port;
        if (p == null) return;
        try {
            for (int i = 0; i < 16; ++i) p.send(padSysex(i, 0), 0, 12);
            for (int b : BUTTONS) p.send(buttonCc(b, OFF), 0, 3);
        } catch (IOException ignored) {}
    }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (port == null) { scheduled = false; return; }
            try { update(); } catch (Throwable ignored) {}
            handler.postDelayed(this, PERIOD_MS);
        }
    };

    private void update() {
        for (int d = 0; d < 2; ++d) {
            Native.status(d, st[d]);
            Native.trackInfo(d, info[d]);
            Native.cues(d, cueT[d], cueRgb[d]);
        }
        padColours(ctl.padMode, st, info, cueT, cueRgb, wantPad);
        int f = ctl.focus;
        buttonValues(f, st[f][Native.ST_PLAYING] > .5, st[f][Native.ST_SYNCED] > .5, st[f][Native.ST_LOOPING] > .5,
                ctl.on(f, Native.D_FX_ON), ctl.on(f, Native.D_SLIP), ctl.on(f, Native.D_PFL), ctl.on(f, Native.D_KEYLOCK), wantBtn);
        MidiInputPort p = port;
        int budget = MAX_MESSAGES;
        try {
            for (int i = 0; i < BUTTONS.length && budget > 0; ++i) {
                if (wantBtn[i] == sentBtn[i]) continue;
                p.send(buttonCc(BUTTONS[i], wantBtn[i]), 0, 3);
                sentBtn[i] = wantBtn[i];
                --budget;
            }
            for (int i = 0; i < 16 && budget > 0; ++i) {
                if (wantPad[i] == sentPad[i]) continue;
                p.send(padSysex(i, wantPad[i]), 0, 12);
                sentPad[i] = wantPad[i];
                --budget;
            }
        } catch (IOException e) {
            port = null;   // unplugged: MidiIn closes it
        }
    }

    // ---- messages
    /** The SysEx pad number for pad index i (0 = pad 1). See the class comment for the assumption. */
    static int sysexPad(int i) { return i; }

    /** Pad colour: rgb7 = 0x00RRGGBB with each component 0..127. */
    static byte[] padSysex(int pad, int rgb7) {
        return new byte[] { (byte) 0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, (byte) (sysexPad(pad) & 0x7f),
                (byte) ((rgb7 >> 16) & 0x7f), (byte) ((rgb7 >> 8) & 0x7f), (byte) (rgb7 & 0x7f), (byte) 0xF7 };
    }

    static byte[] buttonCc(int note, int value) { return new byte[] { (byte) 0xB0, (byte) (note & 0x7f), (byte) (value & 0x7f) }; }

    /** 0xRRGGBB (8 bits each) times k, as 7-bit components. */
    static int rgb7(int rgb, float k) {
        int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
        return (c7(r, k) << 16) | (c7(g, k) << 8) | c7(b, k);
    }
    private static int c7(int c8, float k) { return Math.max(0, Math.min(127, Math.round(c8 / 2f * k))); }

    // ---- what to light
    static final int GREEN = 0x34d058, CYAN = 0x2fd4ff, AMBER = 0xffa21f, RED = 0xff3b30, WHITE = 0xffffff;
    static final float DIMK = .12f;

    /** The 16 pad colours (rgb7) for a pad mode, from the decks' status / track info / cues. */
    static void padColours(int mode, double[][] st, double[][] info, double[][] cueT, int[][] cueRgb, int[] out) {
        for (int p = 0; p < 16; ++p) {
            int d = Ctl.padDeck(p), s = Ctl.padSlot(p);
            boolean has = info[d][5] > .5;
            int deckRgb = Pal.DECK[d] & 0xffffff;
            int c;
            switch (mode) {
                case Ctl.MODE_CUES:
                    c = has && cueT[d][s] >= 0 ? rgb7(cueRgb[d][s], 1f) : rgb7(deckRgb, has ? .08f : .04f);
                    break;
                case Ctl.MODE_LOOPS: {
                    boolean on = st[d][Native.ST_LOOPING] > .5 && loopBeatsMatch(st[d], info[d], Ctl.LOOP_BEATS[s]);
                    c = rgb7(GREEN, on ? 1f : DIMK);
                    break;
                }
                case Ctl.MODE_ROLLS:
                    c = rgb7(CYAN, st[d][Native.ST_ROLLING] > .5 ? .6f : DIMK);
                    break;
                default: {
                    boolean on;
                    int col;
                    switch (s) {
                        case 0: col = GREEN; on = st[d][Native.ST_PLAYING] > .5; break;
                        case 1: col = AMBER; on = false; break;
                        case 2: col = CYAN; on = st[d][Native.ST_SYNCED] > .5; break;
                        case 3: col = RED; on = st[d][Native.ST_CENSOR] > .5; break;
                        case 4: case 5: col = WHITE; on = false; break;
                        case 6: col = GREEN; on = false; break;
                        default: col = GREEN; on = st[d][Native.ST_LOOPING] > .5; break;
                    }
                    c = rgb7(col, on ? 1f : DIMK);
                }
            }
            out[p] = c;
        }
    }

    /** True when the deck's loop is `beats` long (within 3 %) at the track's tempo. */
    static boolean loopBeatsMatch(double[] st, double[] info, double beats) {
        double bpm = info[0], len = st[Native.ST_LOOP_B] - st[Native.ST_LOOP_A];
        if (bpm <= 0 || len <= 0) return false;
        double b = len * bpm / 60.0;
        return Math.abs(b - beats) <= beats * .03;
    }

    /** Button LED values in {@link #BUTTONS} order for the focused deck. */
    static void buttonValues(int focus, boolean playing, boolean synced, boolean looping, boolean fxOn, boolean slip, boolean pfl,
            boolean keylock, int[] out) {
        out[0] = playing ? FULL : OFF;           // PLAY
        out[1] = focus == 0 ? FULL : DIM;        // B1: deck A
        out[2] = focus == 1 ? FULL : DIM;        // B2: deck B
        out[3] = synced ? FULL : DIM;            // B3: sync
        out[4] = looping ? FULL : DIM;           // LOOP
        out[5] = fxOn ? FULL : DIM;              // KNOB FX: FX on
        out[6] = slip ? FULL : DIM;              // PAD FX: slip
        out[7] = pfl ? FULL : DIM;               // MUTE: headphone cue
        out[8] = keylock ? FULL : DIM;           // TAP TEMPO: key lock
    }
}
