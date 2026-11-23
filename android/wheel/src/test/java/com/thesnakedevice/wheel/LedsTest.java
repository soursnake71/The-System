package com.thesnakedevice.wheel;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** The MPC Sample LED messages and the library's small helpers (plain JVM, no engine). */
public class LedsTest {
    static byte[] b(int... v) { byte[] o = new byte[v.length]; for (int i = 0; i < v.length; i++) o[i] = (byte) v[i]; return o; }

    @Test public void padSysex() {
        assertArrayEquals(b(0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, 3, 127, 64, 0, 0xF7), SampleLeds.padSysex(3, (127 << 16) | (64 << 8)));
        assertEquals(0x7f7f7f, SampleLeds.rgb7(0xffffff, 1f));
        assertEquals(0, SampleLeds.rgb7(0xffffff, 0f));
    }

    @Test public void buttonCc() {
        assertArrayEquals(b(0xB0, 82, 127), SampleLeds.buttonCc(Ctl.SB_PLAY, SampleLeds.FULL));
        int[] v = new int[SampleLeds.BUTTONS.length];
        SampleLeds.buttonValues(1, true, false, false, true, false, true, false, v);
        assertEquals(SampleLeds.FULL, v[0]);   // PLAY
        assertEquals(SampleLeds.DIM, v[1]);    // B1 (deck A not focused)
        assertEquals(SampleLeds.FULL, v[2]);   // B2
        assertEquals(SampleLeds.FULL, v[5]);   // KNOB FX
        assertEquals(SampleLeds.FULL, v[7]);   // MUTE = PFL
    }

    @Test public void cuePadsFollowThePluginPadOrder() {
        double[][] st = new double[2][Native.ST_COUNT], info = new double[2][7], cueT = new double[2][8];
        int[][] rgb = new int[2][8];
        for (double[] t : cueT) java.util.Arrays.fill(t, -1);
        info[1][5] = 1;
        cueT[1][2] = 10; rgb[1][2] = 0xff0000;   // deck B, cue 3: pad 7 (index 6, notes 46)
        int[] out = new int[16];
        SampleLeds.padColours(Ctl.MODE_CUES, st, info, cueT, rgb, out);
        assertEquals(6, Ctl.padIndexOf(1, 2));
        assertEquals(46, Ctl.SAMPLE_PAD_NOTES[6]);
        assertEquals(127 << 16, out[6]);
        for (int p = 0; p < 16; p++) if (p != 6) assertTrue((out[p] & 0x7f7f7f) != (127 << 16));
    }

    @Test public void library() {
        assertEquals("4:05", Library.lengthText(245));
        assertEquals("1:02:03", Library.lengthText(3723));
        assertEquals("", Library.lengthText(-1));
        assertFalse(Library.needsCompaction(1500, 1000));
        assertTrue(Library.needsCompaction(5000, 1000));
        assertFalse(Library.needsCompaction(900, 0));
    }
}
