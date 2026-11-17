package com.thesnakedevice.wheel;

import android.graphics.Color;

/** Colours: black, white and two loud deck colours (the MPC screen's palette), plus the Serato-style panel greys. */
final class Pal {
    private Pal() {}
    static final int BG = 0xff0a0b0d, PANEL = 0xff141619, PANEL2 = 0xff1b1e22, RAISED = 0xff24282d, WELL = 0xff07080a,
            EDGE = 0xff2b3036, EDGE_HI = 0xff3a4048, TEXT = 0xffe8eaed, DIM = 0xff9098a2, FAINT = 0xff545b64,
            AMBER = 0xffffa21f, RED = 0xffff3b30, GREEN = 0xff34d058, CYAN = 0xff2fd4ff, VIOLET = 0xffb06cff, WHITE = 0xffffffff;
    static final int[] DECK = { 0xff26a6ff, 0xffff8a1a };
    static final int[] DECK_DARK = { 0xff0b2a44, 0xff442408 };

    static final String[] CAM_MAJ = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };
    static final String[] CAM_MIN = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };
    static final String[] KEY_NAMES = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    static String camelot(int key) {
        if (key < 0 || key > 23) return "";
        return key >= 12 ? CAM_MIN[key - 12] : CAM_MAJ[key];
    }
    static String keyName(int key) {
        if (key < 0 || key > 23) return "";
        return KEY_NAMES[key % 12] + (key >= 12 ? "m" : "");
    }
    /** Camelot wheel colours: hue walks the wheel, minor deeper, major lighter. */
    static int keyColour(int key) {
        if (key < 0) return FAINT;
        String c = camelot(key);
        int n = 1;
        try { n = Integer.parseInt(c.substring(0, c.length() - 1)); } catch (Exception ignored) {}
        float hue = (float) (((0.40 + (n - 1) / 12.0) % 1.0) * 360.0);
        return key >= 12 ? Color.HSVToColor(new float[] { hue, .78f, .95f }) : Color.HSVToColor(new float[] { hue, .55f, 1f });
    }
    static int keyColour(String camelotText) {
        if (camelotText == null || camelotText.length() < 2) return FAINT;
        String t = camelotText.trim().toUpperCase(java.util.Locale.ROOT);
        char ab = t.charAt(t.length() - 1);
        int n;
        try { n = Integer.parseInt(t.substring(0, t.length() - 1)); } catch (Exception e) { return FAINT; }
        if (n < 1 || n > 12 || (ab != 'A' && ab != 'B')) return FAINT;
        float hue = (float) (((0.40 + (n - 1) / 12.0) % 1.0) * 360.0);
        return ab == 'A' ? Color.HSVToColor(new float[] { hue, .78f, .95f }) : Color.HSVToColor(new float[] { hue, .55f, 1f });
    }

    static int alpha(int c, float a) { return (c & 0x00ffffff) | ((int) (Math.max(0, Math.min(1, a)) * 255) << 24); }
    static int mix(int a, int b, float t) {
        int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
        int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
        return 0xff000000 | ((int) (ar + (br - ar) * t) << 16) | ((int) (ag + (bg - ag) * t) << 8) | (int) (ab + (bb - ab) * t);
    }

    static String time(double s) {
        boolean neg = s < 0;
        s = Math.abs(s);
        int m = (int) (s / 60);
        int sec = (int) (s - m * 60);
        int tenth = (int) ((s - Math.floor(s)) * 10);
        return (neg ? "-" : "") + (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec + "." + tenth;
    }
}
