package com.thesnakedevice.wheel;

import android.os.Handler;

import java.io.IOException;

/**
 * Lights the Numark MixTrack Go from the decks (sent to the controller's own MIDI input port, USB or Bluetooth).
 * Every LED is a note-on on the button's channel with the button's note: velocity {@link #ON} 127 = lit, {@link #OFF}
 * 1 = off (dim), see plugins/wheel/controllers/MixtrackGo.md. Per deck (channel 1 / 2): play (note 0), cue (1), sync (2),
 * PFL (27), vinyl (80). Per pad bank (channel 5 / 6): pads 1-4 (notes 20-23: cue set / loop of that length active /
 * pad held) and the pad mode LEDs (notes 1-4: hot cue / loop / sampler = rolls / stems = transport).
 *
 * <p>Runs on the MIDI handler thread every {@link #PERIOD_MS}; only what changed is sent, at most
 * {@link #MAX_MESSAGES} messages a tick. {@link #lightsOff} sends velocity 0 to every LED (the app closes).
 */
final class MixtrackLeds {
    /** Where the bytes go: the controller's input port (a fake one in the tests). */
    interface Sink { void send(byte[] msg, int offset, int count) throws IOException; }

    static final long PERIOD_MS = 40;
    static final int MAX_MESSAGES = 16;
    static final int ON = 127, OFF = 1, DARK = 0;
    static final int[] DECK_NOTES = { Ctl.MT_PLAY, Ctl.MT_CUE, Ctl.MT_SYNC, Ctl.MT_PFL, Ctl.MT_VINYL };
    static final int PER_DECK = DECK_NOTES.length + 4 + 4;
    static final int COUNT = 2 * PER_DECK;

    /** Channel (0-based) of LED i. */
    static int ledChannel(int i) {
        int d = i / PER_DECK, k = i % PER_DECK;
        return k < DECK_NOTES.length ? Ctl.MT_CH_A - 1 + d : Ctl.MT_CH_PADS_A - 1 + d;
    }
    /** Note of LED i. */
    static int ledNote(int i) {
        int k = i % PER_DECK;
        if (k < DECK_NOTES.length) return DECK_NOTES[k];
        k -= DECK_NOTES.length;
        return k < 4 ? Ctl.MT_PAD_FIRST + k : Ctl.MT_PAD_MODE_LED_FIRST + (k - 4);
    }

    static byte[] noteOn(int ch0, int note, int vel) { return new byte[] { (byte) (0x90 | (ch0 & 0x0f)), (byte) (note & 0x7f), (byte) (vel & 0x7f) }; }

    private final Ctl ctl;
    private final Handler handler;
    private volatile Sink sink;
    private boolean scheduled;
    private final int[] sent = new int[COUNT], want = new int[COUNT];
    private final double[][] st = new double[2][Native.ST_COUNT], info = new double[2][7], cueT = new double[2][8];
    private final int[][] cueRgb = new int[2][8];

    MixtrackLeds(Ctl ctl, Handler handler) { this.ctl = ctl; this.handler = handler; }

    /** Starts lighting through this sink (the handler thread only). Everything is sent once. */
    void attach(Sink s) {
        sink = s;
        java.util.Arrays.fill(sent, -1);
        if (!scheduled) { scheduled = true; handler.post(tick); }
    }

    void detach() { sink = null; }

    boolean attached() { return sink != null; }

    /** All LEDs dark (the app is closing / the device goes away). */
    void lightsOff() {
        Sink s = sink;
        if (s == null) return;
        try { for (int i = 0; i < COUNT; ++i) s.send(noteOn(ledChannel(i), ledNote(i), DARK), 0, 3); } catch (IOException ignored) {}
        java.util.Arrays.fill(sent, DARK);
    }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (sink == null) { scheduled = false; return; }
            try { update(); } catch (Throwable ignored) {}
            handler.postDelayed(this, PERIOD_MS);
        }
    };

    /** One tick: reads the decks, sends what changed (at most MAX_MESSAGES). Returns how many messages went out. */
    int update() {
        Ctl.Engine e = Ctl.engine;
        for (int d = 0; d < 2; ++d) {
            e.status(d, st[d]);
            e.trackInfo(d, info[d]);
            e.cues(d, cueT[d], cueRgb[d]);
        }
        values(ctl, st, info, cueT, want);
        Sink s = sink;
        if (s == null) return 0;
        int n = 0;
        try {
            for (int i = 0; i < COUNT && n < MAX_MESSAGES; ++i) {
                if (want[i] == sent[i]) continue;
                s.send(noteOn(ledChannel(i), ledNote(i), want[i]), 0, 3);
                sent[i] = want[i];
                ++n;
            }
        } catch (IOException ex) {
            sink = null;   // gone: MidiIn closes it
        }
        return n;
    }

    /** The LED values (ON / OFF) in LED order. */
    static void values(Ctl ctl, double[][] st, double[][] info, double[][] cueT, int[] out) {
        int mode = ctl.padMode;
        for (int d = 0; d < 2; ++d) {
            int b = d * PER_DECK;
            boolean has = info[d][5] > .5, playing = st[d][Native.ST_PLAYING] > .5;
            out[b] = playing ? ON : OFF;
            out[b + 1] = has && !playing && Math.abs(st[d][Native.ST_POS] - st[d][Native.ST_CUE]) < .03 ? ON : OFF;
            out[b + 2] = st[d][Native.ST_SYNCED] > .5 ? ON : OFF;
            out[b + 3] = ctl.on(d, Native.D_PFL) ? ON : OFF;
            out[b + 4] = ctl.vinyl[d] ? ON : OFF;
            for (int s = 0; s < 4; ++s) {
                boolean lit;
                switch (mode) {
                    case Ctl.MODE_CUES: lit = has && cueT[d][s] >= 0; break;
                    case Ctl.MODE_LOOPS: lit = st[d][Native.ST_LOOPING] > .5 && SampleLeds.loopBeatsMatch(st[d], info[d], Ctl.LOOP_BEATS[s]); break;
                    default: lit = ctl.padHeld(1, d, s); break;
                }
                out[b + DECK_NOTES.length + s] = lit ? ON : OFF;
            }
            for (int m = 0; m < 4; ++m) out[b + DECK_NOTES.length + 4 + m] = mode == m ? ON : OFF;
        }
    }
}
