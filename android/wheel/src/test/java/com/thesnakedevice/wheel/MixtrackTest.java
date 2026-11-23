package com.thesnakedevice.wheel;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.os.Handler;
import android.os.Looper;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

/**
 * The Numark MixTrack Go map (plugins/wheel/controllers/MixtrackGo.md) fed through Ctl as MidiIn would, with a recording
 * engine in place of Native, and its LEDs. Channels are 1-based as in Ctl.
 */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = 34)
public class MixtrackTest {
    /** One engine call. */
    static final class Call {
        final String what; final int type, deck, i; final double v; final boolean on;
        Call(String what, int type, int deck, int i, double v, boolean on) { this.what = what; this.type = type; this.deck = deck; this.i = i; this.v = v; this.on = on; }
        @Override public String toString() { return what + "(" + type + ", d" + deck + ", " + i + ", " + v + ", " + on + ")"; }
    }

    static final class Rec implements Ctl.Engine {
        final List<Call> calls = new ArrayList<>();
        final double[][] st = new double[2][Native.ST_COUNT], info = new double[2][7], cueT = { new double[8], new double[8] };
        final int[][] rgb = new int[2][8];
        Rec() { for (double[] t : cueT) java.util.Arrays.fill(t, -1); }
        @Override public void cmd(int type, int deck, int i, double v, boolean on) { calls.add(new Call("cmd", type, deck, i, v, on)); }
        @Override public void set(int id, float value) { calls.add(new Call("set", id, id / 32, 0, value, true)); }
        @Override public void status(int deck, double[] out) { System.arraycopy(st[deck], 0, out, 0, out.length); }
        @Override public void trackInfo(int deck, double[] out7) { System.arraycopy(info[deck], 0, out7, 0, 7); }
        @Override public void cues(int deck, double[] t, int[] c) { System.arraycopy(cueT[deck], 0, t, 0, 8); System.arraycopy(rgb[deck], 0, c, 0, 8); }
        List<Call> cmds(int type) { List<Call> o = new ArrayList<>(); for (Call c : calls) if (c.what.equals("cmd") && c.type == type) o.add(c); return o; }
        Call last() { return calls.get(calls.size() - 1); }
    }

    /** Records what the library is asked to do. */
    static final class FakeBrowser implements Ctl.Browser {
        boolean open; int moved, entered, backs, toggles; final List<Integer> loads = new ArrayList<>();
        @Override public void toggle() { open = !open; toggles++; }
        @Override public void move(int d) { moved += d; }
        @Override public void select(int deck) {}
        @Override public void back() { backs++; }
        @Override public boolean isOpen() { return open; }
        @Override public void enter() { entered++; }
        @Override public void load(int deck) { loads.add(deck); }
    }

    Rec rec;
    Ctl ctl;
    FakeBrowser browser;

    @Before public void setUp() {
        rec = new Rec();
        Ctl.engine = rec;
        ctl = new Ctl();
        browser = new FakeBrowser();
        ctl.browser = browser;
    }
    @After public void tearDown() { Ctl.engine = Ctl.NATIVE; }

    void note(int ch, int n, boolean down) { ctl.midiNote(ch, n, down ? 127 : 0, down); }
    void press(int ch, int n) { note(ch, n, true); note(ch, n, false); }
    static void advance(long ms) { shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(ms)); }

    @Test public void detectedByNameAndBySignature() {
        assertTrue(MidiIn.isMixtrackName("Numark Mixtrack GO"));
        assertTrue(MidiIn.isMixtrackName("Numark MixTrack Go Bluetooth"));
        assertTrue(MidiIn.isMixtrackName("Numark MixTrack Go"));
        assertFalse(MidiIn.isMixtrackName("Numark Mixtrack Pro 3"));
        assertFalse(MidiIn.isMixtrackName("MPC Sample"));
        assertEquals("Numark MixTrack Go (USB)", MidiIn.displayName("Numark Mixtrack GO", true, false));
        assertEquals("Numark MixTrack Go (Bluetooth)", MidiIn.displayName("Numark MixTrack Go Bluetooth", true, true));
        assertEquals("MPC One", MidiIn.displayName("MPC One", false, false));

        assertFalse(ctl.mixtrackSeen);
        ctl.midiCc(16, 8, 0);   // the crossfader all the way left
        assertTrue(ctl.mixtrackSeen);
        assertEquals(-1f, ctl.get(Ctl.gid(Native.G_XFADER)), 1e-6);

        Ctl c2 = new Ctl();
        c2.midiNote(6, 22, 100, true);   // deck B pad 3: also a MixTrack signature
        assertTrue(c2.mixtrackSeen);
        assertFalse(Ctl.mixtrackSignature(false, 2, 20));
        assertFalse(Ctl.mixtrackSignature(true, 1, 8));
    }

    @Test public void transport() {
        ctl.mixtrackSeen = true;
        press(1, 0);                                               // play A
        assertEquals(1, rec.cmds(Native.C_PLAY_PAUSE).size());
        assertEquals(0, rec.cmds(Native.C_PLAY_PAUSE).get(0).deck);
        note(2, 1, true); note(2, 1, false);                       // cue B down / up
        List<Call> cue = rec.cmds(Native.C_CUE);
        assertEquals(2, cue.size());
        assertTrue(cue.get(0).on); assertFalse(cue.get(1).on); assertEquals(1, cue.get(0).deck);
        press(1, 2);                                               // sync
        assertEquals(1, rec.cmds(Native.C_SYNC).size());
        press(1, 3);                                               // shift + sync: sync off
        assertEquals(1, rec.cmds(Native.C_SYNC_OFF).size());
        note(2, 4, true);                                          // shift + play: censor while held
        note(2, 4, false);
        List<Call> cen = rec.cmds(Native.C_CENSOR);
        assertEquals(2, cen.size()); assertTrue(cen.get(0).on); assertFalse(cen.get(1).on);
        press(1, 5);                                               // shift + cue: to the start
        List<Call> seek = rec.cmds(Native.C_SEEK);
        assertEquals(1, seek.size()); assertEquals(0, seek.get(0).v, 0);
    }

    @Test public void jogScratchesInVinylMode() {
        ctl.mixtrackSeen = true;
        assertTrue(ctl.vinyl[0]);
        ctl.midiCc(1, 6, 10);                                      // 10 ticks forward: the hand lands, the record moves
        List<Call> touch = rec.cmds(Native.C_TOUCH);
        assertEquals(1, touch.size()); assertTrue(touch.get(0).on);
        List<Call> mv = rec.cmds(Native.C_MOVE);
        assertEquals(1, mv.size());
        assertEquals(10 / Ctl.MT_JOG_TICKS_PER_TURN * Ctl.REV_SECONDS, mv.get(0).v, 1e-9);
        ctl.midiCc(1, 6, 128 - 4);                                 // 4 ticks back (two's complement)
        assertEquals(-4 / Ctl.MT_JOG_TICKS_PER_TURN * Ctl.REV_SECONDS, rec.cmds(Native.C_MOVE).get(1).v, 1e-9);
        assertEquals(1, rec.cmds(Native.C_TOUCH).size());          // still the same hand
        advance(20);
        ctl.tick();
        assertEquals(1, rec.cmds(Native.C_TOUCH).size());
        advance(100);
        ctl.tick();                                                // 60 ms after the last tick: the hand lifts
        touch = rec.cmds(Native.C_TOUCH);
        assertEquals(2, touch.size()); assertFalse(touch.get(1).on);
        assertFalse(ctl.handOn(0));
    }

    @Test public void jogTouchHoldsTheRecord() {
        ctl.mixtrackSeen = true;
        note(2, 6, true);                                          // hand on deck B's jog
        assertEquals(1, rec.cmds(Native.C_TOUCH).size());
        ctl.midiCc(2, 6, 3);
        advance(500);
        ctl.tick();
        assertTrue("touch held: the record stays held", ctl.handOn(1));
        note(2, 6, false);
        List<Call> t = rec.cmds(Native.C_TOUCH);
        assertEquals(2, t.size()); assertFalse(t.get(1).on);
    }

    @Test public void vinylOffNudgesAndShiftSeeks() {
        ctl.mixtrackSeen = true;
        press(1, 80);                                              // vinyl off on deck A
        assertFalse(ctl.vinyl[0]);
        assertTrue(ctl.vinyl[1]);
        ctl.midiCc(1, 6, 5);
        assertEquals(0, rec.cmds(Native.C_TOUCH).size());
        List<Call> b = rec.cmds(Native.C_BEND);
        assertEquals(1, b.size());
        double nudge = b.get(0).v;
        assertEquals(5 / Ctl.MT_JOG_TICKS_PER_TURN * Ctl.MT_NUDGE_PER_TURN, nudge, 1e-9);
        ctl.midiCc(1, 43, 5);                                      // shift + jog: fast seek, the same ticks go further
        b = rec.cmds(Native.C_BEND);
        assertEquals(2, b.size());
        assertTrue(b.get(1).v > nudge * 10);
        note(1, 32, true);                                         // shift held: the plain jog seeks too
        ctl.midiCc(1, 6, 5);
        assertEquals(b.get(1).v, rec.cmds(Native.C_BEND).get(2).v, 1e-12);
        note(1, 32, false);
        press(16, 73);                                             // global: vinyl on both decks
        assertTrue(ctl.vinyl[0] && ctl.vinyl[1]);
        press(16, 73);
        assertFalse(ctl.vinyl[0] || ctl.vinyl[1]);
        // jog sensitivity: twice as sensitive = half the ticks per turn
        ctl.jogSens = 2f;
        ctl.midiCc(2, 6, 5);
        assertEquals(5 / (Ctl.MT_JOG_TICKS_PER_TURN / 2) * Ctl.MT_NUDGE_PER_TURN, rec.last().v, 1e-9);
    }

    @Test public void pitchFaderAndMixer() {
        ctl.mixtrackSeen = true;
        ctl.midiCc(1, 9, 0);                                       // top: slowest
        assertEquals(-1f, ctl.get(0, Native.D_PITCH), 1e-6);
        ctl.midiCc(1, 9, 127);                                     // MSB only: bottom
        assertEquals(1f, ctl.get(0, Native.D_PITCH), 1e-6);
        ctl.midiCc(1, 9, 64); ctl.midiCc(1, 41, 0);                // 14-bit centre (8192)
        assertEquals(0f, ctl.get(0, Native.D_PITCH), 1e-6);
        ctl.midiCc(1, 9, 96); ctl.midiCc(1, 41, 64);               // 96 * 128 + 64 = 12352
        assertEquals(12352 / 16383f * 2 - 1, ctl.get(0, Native.D_PITCH), 1e-5);
        press(1, 84);                                              // pitch reset
        assertEquals(0f, ctl.get(0, Native.D_PITCH), 0);

        ctl.midiCc(2, 22, 127);                                    // channel level B
        assertEquals(1f, ctl.get(1, Native.D_FADER), 1e-6);
        ctl.midiCc(2, 26, 127);                                    // filter: high pass
        assertEquals(1f, ctl.get(1, Native.D_FILTER), 1e-6);
        ctl.midiCc(2, 26, 64);                                     // centre: off
        assertEquals(0f, ctl.get(1, Native.D_FILTER), 0);
        press(16, 72);                                             // FILTER / LOW: low EQ
        assertTrue(ctl.filterLow);
        ctl.midiCc(2, 26, 0);
        assertEquals(-26f, ctl.get(1, Native.D_LOW), 1e-4);       // kill
        ctl.midiCc(2, 26, 127);
        assertEquals(6f, ctl.get(1, Native.D_LOW), 1e-4);
        assertEquals(0f, ctl.get(1, Native.D_FILTER), 0);          // the filter stays where it was

        press(1, 27);                                              // PFL
        assertTrue(ctl.on(0, Native.D_PFL));
        press(1, 70);                                              // FX on
        assertTrue(ctl.on(0, Native.D_FX_ON));
        press(1, 72);                                              // slip
        assertTrue(ctl.on(0, Native.D_SLIP));
        ctl.midiCc(16, 8, 127);                                    // crossfader right
        assertEquals(1f, ctl.get(Ctl.gid(Native.G_XFADER)), 1e-6);
        int before = rec.calls.size();
        ctl.midiCc(16, 10, 100); ctl.midiCc(16, 12, 100);          // main / cue level: hardware
        assertEquals(before, rec.calls.size());
    }

    @Test public void globalFx() {
        ctl.mixtrackSeen = true;
        ctl.set(0, Native.D_FX_TYPE, 1);
        ctl.set(1, Native.D_FX_TYPE, 1);
        press(16, 70);
        assertTrue(ctl.on(0, Native.D_FX_ON) && ctl.on(1, Native.D_FX_ON));
        press(16, 70);
        assertFalse(ctl.on(0, Native.D_FX_ON) || ctl.on(1, Native.D_FX_ON));
        press(16, 71);
        assertEquals(2f, ctl.get(0, Native.D_FX_TYPE), 0);
        assertEquals(2f, ctl.get(1, Native.D_FX_TYPE), 0);
        for (int i = 0; i < 6; ++i) press(16, 71);
        assertEquals(1f, ctl.get(0, Native.D_FX_TYPE), 0);        // 7 wraps to 1, never "off"
    }

    @Test public void pads() {
        ctl.mixtrackSeen = true;
        assertEquals(Ctl.MODE_CUES, ctl.padMode);
        note(5, 21, true);                                         // deck A hot cue 2
        note(5, 21, false);
        List<Call> hc = rec.cmds(Native.C_HOT_CUE);
        assertEquals(2, hc.size()); assertEquals(0, hc.get(0).deck); assertEquals(1, hc.get(0).i); assertTrue(hc.get(0).on); assertFalse(hc.get(1).on);
        press(6, 30);                                              // shift + pad 3 on deck B: clear cue 3
        List<Call> clr = rec.cmds(Native.C_CLEAR_CUE);
        assertEquals(1, clr.size()); assertEquals(1, clr.get(0).deck); assertEquals(2, clr.get(0).i);
        press(5, 0);                                               // next pad mode: loops
        assertEquals(Ctl.MODE_LOOPS, ctl.padMode);
        press(6, 23);                                              // slot 3: a 2-beat loop
        assertEquals(2.0, rec.cmds(Native.C_AUTO_LOOP).get(0).v, 0);
        press(6, 31);                                              // shift + pad 4 = slot 7: 32 beats
        assertEquals(32.0, rec.cmds(Native.C_AUTO_LOOP).get(1).v, 0);
        press(5, 0);                                               // rolls
        note(5, 20, true);
        assertTrue(ctl.padHeld(1, 0, 0));
        note(5, 20, false);
        assertFalse(ctl.padHeld(1, 0, 0));
        assertEquals(2, rec.cmds(Native.C_ROLL).size());
    }

    @Test public void browser() {
        ctl.mixtrackSeen = true;
        ctl.midiCc(16, 0, 1);                                      // browse turns: the library opens, the cursor moves
        assertTrue(browser.open);
        assertEquals(1, browser.moved);
        ctl.midiCc(16, 0, 127);                                    // -1
        assertEquals(0, browser.moved);
        ctl.midiCc(16, 1, 2);                                      // shift + browse: pages of 10
        assertEquals(20, browser.moved);
        press(16, 7);                                              // push: into the crate
        assertEquals(1, browser.entered);
        press(16, 3);                                              // LOAD B
        press(16, 2);                                              // LOAD A
        assertEquals(java.util.Arrays.asList(1, 0), browser.loads);
        note(16, 50, true);                                        // shift + push: back to the crates
        press(16, 7);
        note(16, 50, false);
        assertEquals(1, browser.backs);
        browser.open = false;
        press(16, 7);                                              // push with the library closed: it opens
        assertTrue(browser.open);
    }

    // ---- LEDs
    @Test public void ledMessages() {
        assertEquals(MixtrackLeds.COUNT, 26);
        assertArrayEquals(LedsTest.b(0x90, 0, 127), MixtrackLeds.noteOn(MixtrackLeds.ledChannel(0), MixtrackLeds.ledNote(0), MixtrackLeds.ON));
        // deck B's vinyl LED: channel 2 (0x91), note 80; deck B's pad 4: channel 6 (0x95), note 23; mode "stems": note 4
        int vinylB = MixtrackLeds.PER_DECK + 4, padB4 = MixtrackLeds.PER_DECK + 5 + 3, modeB4 = MixtrackLeds.PER_DECK + 5 + 4 + 3;
        assertArrayEquals(LedsTest.b(0x91, 80, 1), MixtrackLeds.noteOn(MixtrackLeds.ledChannel(vinylB), MixtrackLeds.ledNote(vinylB), MixtrackLeds.OFF));
        assertArrayEquals(LedsTest.b(0x95, 23, 127), MixtrackLeds.noteOn(MixtrackLeds.ledChannel(padB4), MixtrackLeds.ledNote(padB4), MixtrackLeds.ON));
        assertArrayEquals(LedsTest.b(0x95, 4, 1), MixtrackLeds.noteOn(MixtrackLeds.ledChannel(modeB4), MixtrackLeds.ledNote(modeB4), MixtrackLeds.OFF));
    }

    @Test public void ledsFollowTheDecks() throws Exception {
        List<byte[]> out = new ArrayList<>();
        MixtrackLeds leds = new MixtrackLeds(ctl, new Handler(Looper.getMainLooper()));
        leds.attach((m, o, n) -> out.add(java.util.Arrays.copyOfRange(m, o, o + n)));
        rec.st[0][Native.ST_PLAYING] = 1;
        rec.info[1][5] = 1; rec.cueT[1][2] = 12.5;               // deck B has hot cue 3
        rec.st[1][Native.ST_SYNCED] = 1;
        ctl.set(1, Native.D_PFL, 1);
        assertEquals(MixtrackLeds.MAX_MESSAGES, leds.update());    // the first sends are rate limited
        assertEquals(MixtrackLeds.COUNT - MixtrackLeds.MAX_MESSAGES, leds.update());
        assertEquals(0, leds.update());                            // nothing changed: nothing sent
        List<String> all = new ArrayList<>();
        for (byte[] m : out) all.add((m[0] & 0xff) + "/" + m[1] + "/" + m[2]);
        assertTrue(all.contains((0x90) + "/0/127"));                // play A lit
        assertTrue(all.contains((0x91) + "/0/1"));                  // play B off
        assertTrue(all.contains((0x91) + "/2/127"));                // sync B
        assertTrue(all.contains((0x91) + "/27/127"));               // PFL B
        assertTrue(all.contains((0x90) + "/80/127"));               // vinyl A (default on)
        assertTrue(all.contains((0x95) + "/22/127"));               // deck B pad 3: cue set
        assertTrue(all.contains((0x95) + "/20/1"));
        assertTrue(all.contains((0x94) + "/1/127"));                // pad mode: hot cue
        assertTrue(all.contains((0x94) + "/2/1"));
        // a change sends just that LED
        out.clear();
        ctl.mixtrackSeen = true;
        press(1, 80);                                              // vinyl off on A
        assertEquals(1, leds.update());
        assertArrayEquals(LedsTest.b(0x90, 80, 1), out.get(0));
        // the pad mode moves: the old mode LED goes off, the new one on, on both pad banks
        out.clear();
        press(5, 0);
        assertEquals(5, leds.update());                            // 2 mode LEDs a bank + deck B pad 3 (its cue LED, off in loop mode)
        // closing: every LED dark
        out.clear();
        leds.lightsOff();
        assertEquals(MixtrackLeds.COUNT, out.size());
        for (byte[] m : out) assertEquals(0, m[2]);
        leds.detach();
    }
}
