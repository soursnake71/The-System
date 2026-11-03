package com.thesnakedevice.cmi;

import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The CMI's screen on a phone or tablet: green phosphor, pages along the top (2 library, 7 control, D waveform,
 * 8 sampling, R composer), a multi-touch keyboard along the bottom. The finger is the light pen.
 */
public class CmiView extends View {
    static final int INK = 0xff4dffa8, DIM = 0xff1f8a5a, FAINT = 0xff0f3d28, BG = 0xff020a06, CREAM = 0xffd9d3bc;

    private final MainActivity act;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float d;                       // density
    private char page = 'R';
    private String status = "READY";
    int rootKey = 60; boolean loopSample = false;
    private int selTrack = 0, libScroll = 0, wavePage = -1, waveSound = -1, octave = 4;
    private boolean songEditor = false;
    private final byte[] wave = new byte[16384];
    private final int[] env = new int[Native.NCH];

    // hit regions, rebuilt on every draw
    interface Act { void run(float x, float y); }
    static final class Hit { RectF r; Act down, drag; boolean longPress; Act hold; }
    private final List<Hit> hits = new ArrayList<>();
    private final Map<Integer, Hit> active = new LinkedHashMap<>();
    private final Map<Integer, Integer> keyOf = new LinkedHashMap<>();
    private final Map<Integer, float[]> downAt = new LinkedHashMap<>();
    private RectF keysRect = new RectF();

    public CmiView(MainActivity a) {
        super(a);
        act = a;
        d = getResources().getDisplayMetrics().density;
        p.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        setKeepScreenOn(true);
        postInvalidateOnAnimation();
    }

    void status(String s) { status = s.toUpperCase(); invalidate(); }

    // ---------------------------------------------------------------------------------------------------------
    private Hit hit(RectF r, Act down) { Hit h = new Hit(); h.r = r; h.down = down; hits.add(h); return h; }
    private float dp(float v) { return v * d; }

    private void text(Canvas c, String s, float x, float y, float size, int color) {
        p.setStyle(Paint.Style.FILL); p.setColor(color); p.setTextSize(dp(size)); p.setTextAlign(Paint.Align.LEFT);
        c.drawText(s, x, y, p);
    }
    private void textC(Canvas c, String s, RectF r, float size, int color) {
        p.setStyle(Paint.Style.FILL); p.setColor(color); p.setTextSize(dp(size)); p.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics fm = p.getFontMetrics();
        c.drawText(s, r.centerX(), r.centerY() - (fm.ascent + fm.descent) / 2, p);
        p.setTextAlign(Paint.Align.LEFT);
    }
    private void fill(Canvas c, RectF r, int color) { p.setStyle(Paint.Style.FILL); p.setColor(color); c.drawRect(r, p); }
    private void frame(Canvas c, RectF r, int color) { p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(dp(1)); p.setColor(color); c.drawRect(r, p); p.setStyle(Paint.Style.FILL); }
    private void button(Canvas c, RectF r, String s, boolean on, Runnable fn) {
        if (on) fill(c, r, INK); else frame(c, r, DIM);
        textC(c, s, r, 12, on ? BG : INK);
        hit(r, (x, y) -> fn.run());
    }

    private static String noteName(int n) { String[] nm = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return nm[((n % 12) + 12) % 12] + (n / 12 - 1); }
    private static String[] info(int i) { return Native.soundInfo(i).split("\t", -1); }

    // ---------------------------------------------------------------------------------------------------------
    @Override protected void onDraw(Canvas c) {
        hits.clear();
        final float W = getWidth(), H = getHeight();
        final float keyH = Math.min(H * 0.26f, dp(110));
        c.drawColor(0xff151515);
        fill(c, new RectF(0, H - keyH, W, H), CREAM);
        RectF scr = new RectF(dp(6), dp(4), W - dp(6), H - keyH - dp(4));
        fill(c, scr, BG);
        // tabs
        final char[] pages = { '2', '7', 'D', '8', 'R' };
        final String[] names = { "LIBRARY", "CONTROL", "WAVE", "SAMPLE", "COMPOSER" };
        float tw = (scr.width() - dp(110)) / 5f;
        text(c, "PAGE " + page, scr.left + dp(8), scr.top + dp(18), 13, INK);
        for (int i = 0; i < 5; ++i) {
            final char pg = pages[i];
            RectF r = new RectF(scr.left + dp(100) + i * tw, scr.top + dp(3), scr.left + dp(100) + (i + 1) * tw - dp(4), scr.top + dp(23));
            button(c, r, pg + " " + names[i], page == pg, () -> { page = pg; status = "READY"; });
        }
        fill(c, new RectF(scr.left + dp(4), scr.top + dp(27), scr.right - dp(4), scr.top + dp(43)), INK);
        textC(c, "***  " + status + "  ***", new RectF(scr.left, scr.top + dp(27), scr.right, scr.top + dp(43)), 11, BG);
        RectF body = new RectF(scr.left + dp(6), scr.top + dp(48), scr.right - dp(6), scr.bottom - dp(4));
        switch (page) {
            case '2': library(c, body); break;
            case '7': controls(c, body); break;
            case 'D': waveform(c, body); break;
            case '8': sampling(c, body); break;
            default: composer(c, body); break;
        }
        // scanlines
        p.setColor(0x2a000000);
        for (float y = scr.top; y < scr.bottom; y += dp(2)) c.drawRect(scr.left, y, scr.right, y + dp(0.7f), p);
        keyboard(c, new RectF(0, H - keyH, W, H));
        postInvalidateDelayed(40);
    }

    // ---- Page 2 ----
    private void library(Canvas c, RectF b) {
        final int n = Native.soundCount(), cur = Native.getSound();
        final float rowH = dp(24);
        final int rows = Math.max(1, (int) ((b.height() - dp(20)) / rowH));
        final float colW = dp(170);
        final int cols = Math.max(1, (int) (b.width() / colW));
        final int perPage = rows * cols;
        libScroll = Math.max(0, Math.min(libScroll, Math.max(0, n - perPage)));
        for (int k = 0; k < perPage && libScroll + k < n; ++k) {
            final int i = libScroll + k;
            String[] f = info(i);
            RectF r = new RectF(b.left + (k / rows) * colW, b.top + (k % rows) * rowH, b.left + (k / rows + 1) * colW - dp(6), b.top + (k % rows + 1) * rowH - dp(2));
            if (i == cur) fill(c, r, INK);
            text(c, String.format("%02d %-8s %s", i + 1, f[0], f[2].length() > 6 ? f[2].substring(0, 6) : f[2]), r.left + dp(3), r.bottom - dp(6), 12, i == cur ? BG : INK);
            hit(r, (x, y) -> { Native.loadSound(i); status = "LOADED " + info(i)[0]; });
        }
        if (n > perPage) {
            button(c, new RectF(b.right - dp(90), b.bottom - dp(20), b.right - dp(50), b.bottom), "<", false, () -> libScroll = Math.max(0, libScroll - rows));
            button(c, new RectF(b.right - dp(44), b.bottom - dp(20), b.right, b.bottom), ">", false, () -> libScroll += rows);
        }
        String[] f = info(cur);
        if (f.length > 3) text(c, f[0] + ": " + f[1], b.left, b.bottom - dp(4), 11, DIM);
    }

    // ---- Page 7 ----
    private interface Fmt { String of(float v); }
    private void barRow(Canvas c, RectF r, final int id, String label, final float min, final float max, final boolean toggle, final boolean integer, Fmt fmt) {
        final float v = Native.getParam(id);
        text(c, label, r.left, r.centerY() + dp(4), 11, INK);
        final RectF tr = new RectF(r.left + dp(86), r.top + dp(4), r.right - dp(58), r.bottom - dp(4));
        if (toggle) {
            RectF bx = new RectF(tr.left, tr.top, tr.left + dp(50), tr.bottom);
            button(c, bx, v > 0.5f ? "ON" : "OFF", v > 0.5f, () -> Native.setParam(id, v > 0.5f ? 0 : 1));
            return;
        }
        fill(c, tr, FAINT);
        fill(c, new RectF(tr.left, tr.top, tr.left + tr.width() * Math.max(0, Math.min(1, (v - min) / (max - min))), tr.bottom), INK);
        p.setTextAlign(Paint.Align.RIGHT); p.setTextSize(dp(11)); p.setColor(INK);
        c.drawText(fmt != null ? fmt.of(v) : String.format("%.2f", v), r.right, r.centerY() + dp(4), p);
        p.setTextAlign(Paint.Align.LEFT);
        Act set = (x, y) -> { float u = Math.max(0, Math.min(1, (x - tr.left) / tr.width())); float nv = min + u * (max - min); if (integer) nv = Math.round(nv); Native.setParam(id, nv); };
        Hit h = hit(new RectF(tr.left - dp(6), r.top, tr.right + dp(6), r.bottom), set); h.drag = set;
    }
    private String time(float v) { float t = Native.envTimeOf(v); return t < 1 ? Math.round(t * 1000) + "ms" : String.format("%.1fs", t); }
    private void controls(Canvas c, RectF b) {
        final int rows = 13;
        final float rh = Math.min(dp(26), b.height() / rows), cw = (b.width() - dp(16)) / 2;
        Object[][] left = {
                { Native.LEVEL, "LEVEL", 0f, 1f, false, false, (Fmt) v -> "" + Math.round(v * 255) },
                { Native.FILTER, "FILTER", 0f, 255f, false, true, (Fmt) v -> "" + Math.round(v) },
                { Native.ATTACK, "ATTACK", 0f, 31f, false, false, (Fmt) this::time },
                { Native.DAMP, "DAMPING", 0f, 31f, false, false, (Fmt) this::time },
                { Native.SUSTAIN, "SUSTAIN", 0f, 1f, true, true, null },
                { Native.LOOP, "LOOP", 0f, 1f, true, true, null },
                { Native.VIB, "VIB DEPTH", 0f, 1f, false, false, (Fmt) v -> Math.round(v * 100) + "c" },
                { Native.VIBRATE, "VIB SPEED", 0.5f, 12f, false, false, (Fmt) v -> String.format("%.1fHz", v) },
                { Native.VIBDELAY, "VIB DELAY", 0f, 2f, false, false, (Fmt) v -> String.format("%.2fs", v) },
                { Native.PORTA, "PORTAMENTO", 0f, 1f, false, false, (Fmt) v -> String.format("%.2fs", v) },
                { Native.VELVOL, "VEL>LEVEL", 0f, 1f, false, false, null },
                { Native.VELFILT, "VEL>FILTER", 0f, 1f, false, false, null },
                { Native.SEG, "SEGMENT", 0.005f, 0.2f, false, false, (Fmt) v -> Math.round(v * 1000) + "ms" },
        };
        Object[][] right = {
                { Native.TRANSPOSE, "TRANSPOSE", -24f, 24f, false, true, (Fmt) v -> "" + Math.round(v) },
                { Native.FINE, "FINE TUNE", -100f, 100f, false, false, (Fmt) v -> Math.round(v) + "c" },
                { Native.POLY, "VOICES", 1f, 8f, false, true, (Fmt) v -> "" + Math.round(v) },
                { Native.BEND, "BEND RANGE", 0f, 12f, false, true, (Fmt) v -> "" + Math.round(v) },
                { Native.MASTER, "MASTER", 0f, 1.5f, false, false, null },
                { Native.HISS, "NOISE FLOOR", 0f, 4f, false, false, null },
                { Native.SPREAD, "SPREAD", 0f, 1f, false, false, null },
                { Native.TEMPO, "SPEED", 40f, 240f, false, false, (Fmt) v -> String.format("%.1f", v) },
                { Native.SWING, "SWING", 0f, 0.5f, false, false, (Fmt) v -> Math.round(v * 200) + "%" },
                { Native.SONGMODE, "SONG MODE", 0f, 1f, true, true, null },
        };
        for (int i = 0; i < left.length; ++i) { Object[] o = left[i]; barRow(c, new RectF(b.left, b.top + i * rh, b.left + cw, b.top + (i + 1) * rh), (Integer) o[0], (String) o[1], (Float) o[2], (Float) o[3], (Boolean) o[4], (Boolean) o[5], (Fmt) o[6]); }
        for (int i = 0; i < right.length; ++i) { Object[] o = right[i]; barRow(c, new RectF(b.left + cw + dp(16), b.top + i * rh, b.right, b.top + (i + 1) * rh), (Integer) o[0], (String) o[1], (Float) o[2], (Float) o[3], (Boolean) o[4], (Boolean) o[5], (Fmt) o[6]); }
        button(c, new RectF(b.left + cw + dp(16), b.top + right.length * rh + dp(6), b.left + cw + dp(170), b.top + right.length * rh + dp(28)), "RESET TO VOICE", false,
                () -> Native.loadSound(Native.getSound()));
    }

    // ---- Page D ----
    private void waveform(Canvas c, RectF b) {
        final int s = Native.getSound();
        String[] f = info(s);
        final int len = Native.wave(s, wave), np = Math.max(1, len / 128);
        final int ls = Integer.parseInt(f[6]), le = Integer.parseInt(f[7]);
        if (s != waveSound) { waveSound = s; wavePage = ls >= 0 ? ls : np / 4; }
        wavePage = Math.max(0, Math.min(np - 1, wavePage));
        final float infoW = dp(150);
        final float w = (b.width() - infoW) * 0.62f, dx = (b.width() - infoW) * 0.3f, h = b.height() * 0.16f, dy = b.height() * 0.68f;
        final float x0 = b.left, y0 = b.bottom - h * 0.6f;
        final int stepP = np > 64 ? 2 : 1;
        Paint lp = new Paint(Paint.ANTI_ALIAS_FLAG); lp.setStyle(Paint.Style.STROKE); lp.setStrokeWidth(dp(1));
        for (int pg = np - 1; pg >= 0; pg -= stepP) {
            float u = np > 1 ? (float) pg / (np - 1) : 0, bx = x0 + dx * u, by = y0 - dy * u;
            Path line = new Path(), area = new Path();
            for (int i = 0; i < 128; i += 2) {
                float v = ((wave[pg * 128 + i] & 0xff) - 128) / 128f;
                float x = bx + w * i / 127f, y = by - v * h * 0.5f;
                if (i == 0) { line.moveTo(x, y); area.moveTo(x, by + h * 0.5f); }
                line.lineTo(x, y); area.lineTo(x, y);
            }
            area.lineTo(bx + w, by + h * 0.5f); area.close();
            p.setColor(BG); c.drawPath(area, p);
            boolean sel = Math.abs(pg - wavePage) < stepP, inLoop = ls >= 0 && pg >= ls && pg <= le;
            lp.setColor(sel ? Color.WHITE : inLoop ? INK : DIM);
            c.drawPath(line, lp);
        }
        Act pick = (x, y) -> wavePage = Math.max(0, Math.min(np - 1, Math.round((y0 - y) / dy * (np - 1))));
        Hit hh = hit(new RectF(b.left, b.top, b.right - infoW, b.bottom), pick); hh.drag = pick;
        float ix = b.right - infoW + dp(6), iy = b.top + dp(12);
        String[] lines = { f[0], "PAGE " + (wavePage + 1) + "/" + np, "RATE " + f[3] + " Hz", "ROOT " + noteName(Integer.parseInt(f[4])),
                "LOOP " + (ls >= 0 ? (ls + 1) + "-" + (le + 1) : "NONE"), f[8].equals("1") ? "WAVEFORM" : "SAMPLE", "8 BIT " + len + " B" };
        for (int i = 0; i < lines.length; ++i) text(c, lines[i], ix, iy + i * dp(16), 11, i == 0 ? INK : DIM);
        // channel cards
        Native.channelEnv(env);
        float cy = iy + lines.length * dp(16) + dp(6), bw = (infoW - dp(12)) / 8f, bh = Math.min(dp(44), (b.bottom - cy) / 2 - dp(6));
        for (int k = 0; k < Native.NCH; ++k) {
            RectF r = new RectF(ix + (k % 8) * bw, cy + (k / 8) * (bh + dp(6)), ix + (k % 8 + 1) * bw - dp(3), cy + (k / 8) * (bh + dp(6)) + bh);
            fill(c, r, FAINT);
            fill(c, new RectF(r.left, r.bottom - r.height() * env[k] / 255f, r.right, r.bottom), k < 8 ? INK : DIM);
        }
    }

    // ---- Page 8 ----
    private void sampling(Canvas c, RectF b) {
        text(c, "SAMPLING  8 BITS, UP TO 32 kHz, 16 KB PER VOICE", b.left, b.top + dp(12), 12, INK);
        final float rh = Math.min(dp(30), (b.height() - dp(56)) / 8);
        for (int k = 0; k < Native.USER_SLOTS; ++k) {
            final int slot = k;
            float y = b.top + dp(20) + k * rh;
            boolean has = Native.userLoaded(k);
            String[] f = info(Native.userSlot(k));
            text(c, "USER" + (k + 1) + "  " + (has ? f[0] + "  " + f[3] + " Hz  root " + noteName(Integer.parseInt(f[4])) : "-- EMPTY --"), b.left, y + rh * 0.65f, 12, has ? INK : DIM);
            button(c, new RectF(b.right - dp(200), y + dp(2), b.right - dp(110), y + rh - dp(3)), "LOAD", false, () -> act.pickSample(slot));
            button(c, new RectF(b.right - dp(104), y + dp(2), b.right, y + rh - dp(3)), "PLAY", false, () -> { Native.loadSound(Native.userSlot(slot)); status = "LOADED"; });
        }
        float y = b.bottom - dp(26);
        text(c, "ROOT " + noteName(rootKey), b.left, y + dp(16), 12, INK);
        button(c, new RectF(b.left + dp(90), y, b.left + dp(124), y + dp(22)), "-", false, () -> rootKey = Math.max(24, rootKey - 1));
        button(c, new RectF(b.left + dp(128), y, b.left + dp(162), y + dp(22)), "+", false, () -> rootKey = Math.min(96, rootKey + 1));
        button(c, new RectF(b.left + dp(180), y, b.left + dp(280), y + dp(22)), loopSample ? "LOOP ON" : "LOOP OFF", loopSample, () -> loopSample = !loopSample);
    }

    // ---- Page R ----
    private void composer(Canvas c, RectF b) {
        final int pat = Native.getPattern();
        final boolean running = Native.seqRunning();
        final int stepNow = running ? Native.seqStep() : -1;
        final float hh = dp(26);
        // top line: pattern, speed, transport, song
        float x = b.left;
        text(c, "PATTERN", x, b.top + dp(17), 12, INK); x += dp(62);
        button(c, new RectF(x, b.top, x + dp(26), b.top + dp(22)), "<", false, () -> Native.setPattern(pat - 1)); x += dp(28);
        textC(c, "" + (pat + 1), new RectF(x, b.top, x + dp(30), b.top + dp(22)), 18, INK); x += dp(32);
        button(c, new RectF(x, b.top, x + dp(26), b.top + dp(22)), ">", false, () -> Native.setPattern(pat + 1)); x += dp(36);
        final float tempo = Native.getParam(Native.TEMPO);
        text(c, String.format("SPEED %.0f", tempo), x, b.top + dp(17), 12, INK); x += dp(78);
        button(c, new RectF(x, b.top, x + dp(26), b.top + dp(22)), "-", false, () -> Native.setParam(Native.TEMPO, Math.max(40, Math.round(tempo) - 2))); x += dp(28);
        button(c, new RectF(x, b.top, x + dp(26), b.top + dp(22)), "+", false, () -> Native.setParam(Native.TEMPO, Math.min(240, Math.round(tempo) + 2))); x += dp(36);
        button(c, new RectF(x, b.top, x + dp(56), b.top + dp(22)), "PLAY", running, () -> Native.seqRun(true)); x += dp(60);
        button(c, new RectF(x, b.top, x + dp(56), b.top + dp(22)), "STOP", !running, () -> Native.seqRun(false)); x += dp(64);
        final boolean songOn = Native.getParam(Native.SONGMODE) > 0.5f;
        button(c, new RectF(x, b.top, x + dp(64), b.top + dp(22)), "SONG", songOn, () -> Native.setParam(Native.SONGMODE, songOn ? 0 : 1)); x += dp(68);
        if (running) text(c, "BAR " + (Native.seqBar() + 1), x, b.top + dp(17), 12, DIM);

        // bottom line: editing
        float by = b.bottom - dp(22);
        final boolean aud = Native.trackAudible(selTrack);
        float bx = b.left;
        String[] lbl = { aud ? "AUDIBLE" : "MUTED", "INSERT", "DELETE", "ZERO", "FILL", "VOICE", songEditor ? "PATTERN" : "SONG" };
        float bw = (b.width() - dp(6) * (lbl.length - 1)) / lbl.length;
        Runnable[] fns = {
                () -> Native.setTrackAudible(selTrack, !aud),
                () -> { if (pat + 1 < Native.NPAT) { Native.copyPattern(pat, pat + 1); Native.setPattern(pat + 1); status = "COPIED"; } },
                () -> { for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, selTrack, s, 0, 0); },
                () -> { for (int t = 0; t < Native.TRACKS; ++t) for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, t, s, 0, 0); },
                () -> { for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, selTrack, s, s % 2 == 1 ? 80 : 110, 0); },
                () -> chooseSound(selTrack),
                () -> songEditor = !songEditor,
        };
        for (int i = 0; i < lbl.length; ++i) { button(c, new RectF(bx, by, bx + bw, by + dp(22)), lbl[i], i == 0 ? aud : (i == 6 && songEditor), fns[i]); bx += bw + dp(6); }

        RectF g = new RectF(b.left, b.top + dp(28), b.right, by - dp(6));
        if (songEditor) {
            final int n = Native.songLen(), bar = running && songOn ? Native.seqBar() : -1;
            float cw = g.width() / 16f, ch = Math.min(dp(40), g.height() / 4f - dp(4));
            for (int i = 0; i < Native.SONG_MAX; ++i) {
                final int k = i;
                RectF r = new RectF(g.left + (i % 16) * cw, g.top + (i / 16) * (ch + dp(4)), g.left + (i % 16 + 1) * cw - dp(3), g.top + (i / 16) * (ch + dp(4)) + ch);
                frame(c, r, i < n ? INK : FAINT);
                if (i < n) textC(c, "" + (Native.songAt(i) + 1), r, 15, i == bar ? Color.WHITE : INK);
                Hit h = hit(r, (xx, yy) -> { if (k >= Native.songLen()) Native.setSongLen(k + 1); else Native.setSongAt(k, (Native.songAt(k) + 1) % Native.NPAT); });
                h.longPress = true; h.hold = (xx, yy) -> Native.setSongLen(Math.max(1, k));
            }
            text(c, "tap: next pattern   hold: song ends here", g.left, g.bottom - dp(4), 11, DIM);
            return;
        }
        final float nameW = Math.min(dp(120), g.width() * 0.22f);
        final float rh = g.height() / Native.TRACKS;
        final float gx = g.left + nameW + dp(20), gw = g.right - gx, cw = gw / Native.STEPS;
        if (stepNow >= 0 && Native.seqPat() == pat) fill(c, new RectF(gx + stepNow * cw, g.top, gx + (stepNow + 1) * cw, g.bottom), FAINT);
        for (int t = 0; t < Native.TRACKS; ++t) {
            final int tr = t;
            RectF nr = new RectF(g.left, g.top + t * rh, g.left + nameW, g.top + (t + 1) * rh);
            if (t == selTrack) fill(c, nr, FAINT);
            text(c, info(Native.trackSound(t))[0], nr.left + dp(4), nr.centerY() + dp(5), 13, Native.trackAudible(t) ? INK : DIM);
            Hit nh = hit(nr, (xx, yy) -> { selTrack = tr; Native.preview(tr); });
            nh.longPress = true; nh.hold = (xx, yy) -> { selTrack = tr; chooseSound(tr); };
            textC(c, "" + (t + 1), new RectF(nr.right, nr.top, gx, nr.bottom), 16, INK);
            p.setColor(DIM); c.drawLine(gx, nr.bottom, g.right, nr.bottom, p);
            for (int s = 0; s < Native.STEPS; ++s) {
                final int st = s;
                RectF cell = new RectF(gx + s * cw, nr.top, gx + (s + 1) * cw, nr.bottom);
                int v = Native.getStep(pat, t, s), vel = v & 0xff, note = (byte) (v >> 8);
                if (vel > 0) {
                    float sc = vel >= 120 ? 1f : vel >= 80 ? 0.75f : 0.5f;
                    float top = cell.top + cell.height() * (1 - sc) * 0.5f + dp(3), bot = cell.bottom - dp(4), lx = cell.left + cw * 0.3f;
                    boolean lit = s == stepNow && Native.seqPat() == pat;
                    p.setColor(lit ? Color.WHITE : INK);
                    c.drawRect(lx, top, lx + dp(2), bot, p);
                    c.drawRect(lx, top, lx + Math.min(dp(8), cw * 0.45f), top + dp(2), p);
                    c.drawRect(lx, bot - dp(2), lx + Math.min(dp(8), cw * 0.45f), bot, p);
                    if (note != 0) text(c, (note > 0 ? "+" : "") + note, cell.left + dp(1), cell.bottom - dp(2), 8, DIM);
                }
                Hit h = hit(cell, (xx, yy) -> {
                    selTrack = tr;
                    int cur = Native.getStep(pat, tr, st);
                    if ((cur & 0xff) == 0) Native.setStep(pat, tr, st, 100, 0); else Native.setStep(pat, tr, st, 0, 0);
                });
                h.longPress = true;
                h.hold = (xx, yy) -> {   // hold a note: cycle accent / ghost / normal, slide up or down for pitch
                    int cur = Native.getStep(pat, tr, st), cv = cur & 0xff;
                    if (cv == 0) cv = 100;
                    Native.setStep(pat, tr, st, cv >= 120 ? 60 : cv >= 80 ? 127 : 100, (byte) (cur >> 8));
                };
                h.drag = (xx, yy) -> {
                    float[] o = downAt.values().isEmpty() ? null : downAt.values().iterator().next();
                    if (o == null) return;
                    int cur = Native.getStep(pat, tr, st);
                    if ((cur & 0xff) == 0) return;
                    int semis = Math.max(-24, Math.min(24, Math.round((o[1] - yy) / dp(10))));
                    Native.setStep(pat, tr, st, cur & 0xff, semis);
                };
            }
        }
        for (int s = 0; s < Native.STEPS; s += 4) { p.setColor(INK); c.drawRect(gx + s * cw, g.bottom - dp(2), gx + s * cw + dp(8), g.bottom, p); }
    }

    private void chooseSound(final int track) {
        final int n = Native.soundCount();
        final String[] items = new String[n];
        for (int i = 0; i < n; ++i) { String[] f = info(i); items[i] = f[0] + "  (" + f[2] + ")  " + f[1]; }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Track " + (track + 1) + " voice")
                .setItems(items, (dlg, which) -> { Native.setTrackSound(track, which); Native.preview(track); })
                .show();
    }

    // ---- keyboard: two and a half octaves, slide between keys, height = velocity ----
    private void keyboard(Canvas c, RectF r) {
        keysRect = new RectF(r.left + dp(44), r.top + dp(6), r.right - dp(44), r.bottom - dp(4));
        RectF kr = keysRect;
        button(c, new RectF(r.left + dp(4), r.top + dp(10), r.left + dp(40), r.top + dp(40)), "<", false, () -> octave = Math.max(1, octave - 1));
        button(c, new RectF(r.right - dp(40), r.top + dp(10), r.right - dp(4), r.top + dp(40)), ">", false, () -> octave = Math.min(7, octave + 1));
        p.setColor(0xff202020); c.drawRect(r.left + dp(4), r.top + dp(46), r.left + dp(40), r.bottom - dp(8), p);
        textC(c, "C" + octave, new RectF(r.left + dp(4), r.top + dp(46), r.left + dp(40), r.bottom - dp(8)), 11, INK);
        final int whites = whiteCount();
        final float ww = kr.width() / whites;
        int wi = 0;
        for (int n = lowKey(); wi < whites; ++n) {
            if (isBlack(n)) continue;
            RectF k = new RectF(kr.left + wi * ww, kr.top, kr.left + (wi + 1) * ww - dp(1.5f), kr.bottom);
            p.setColor(keyOf.containsValue(n) ? INK : 0xfff4f0e2); c.drawRect(k, p);
            if (n % 12 == 0) text(c, "C" + (n / 12 - 1), k.left + dp(3), k.bottom - dp(5), 9, 0xff6a6656);
            ++wi;
        }
        wi = 0;
        for (int n = lowKey(); wi < whites; ++n) {
            if (isBlack(n)) {
                RectF k = new RectF(kr.left + wi * ww - ww * 0.32f, kr.top, kr.left + wi * ww + ww * 0.32f, kr.top + kr.height() * 0.6f);
                p.setColor(keyOf.containsValue(n) ? INK : 0xff1c1c1c); c.drawRect(k, p);
            } else ++wi;
        }
    }
    private int lowKey() { return 12 * (octave + 1); }
    private int whiteCount() { return Math.max(8, Math.min(22, (int) (keysRect.width() / dp(34)))); }
    private static boolean isBlack(int n) { int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    private int keyAt(float x, float y) {
        if (!keysRect.contains(x, y)) return -1;
        final float ww = keysRect.width() / whiteCount();
        int wi = (int) ((x - keysRect.left) / ww);
        int n = lowKey(), w = 0;
        while (true) { if (!isBlack(n)) { if (w == wi) break; ++w; } ++n; }
        if (y < keysRect.top + keysRect.height() * 0.6f) {
            float fx = (x - keysRect.left) / ww - wi;
            if (fx < 0.32f && isBlack(n - 1)) return n - 1;
            if (fx > 0.68f && isBlack(n + 1)) return n + 1;
        }
        return n;
    }
    private int velAt(float y) { return Math.max(30, Math.min(127, (int) (30 + 97 * (y - keysRect.top) / Math.max(1, keysRect.height())))); }

    // ---------------------------------------------------------------------------------------------------------
    private final Runnable[] holdTimers = new Runnable[16];
    @Override public boolean onTouchEvent(MotionEvent e) {
        final int a = e.getActionMasked();
        if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_POINTER_DOWN) {
            final int i = e.getActionIndex(), id = e.getPointerId(i);
            final float x = e.getX(i), y = e.getY(i);
            int k = keyAt(x, y);
            if (k >= 0) { keyOf.put(id, k); Native.noteOn(k, velAt(y)); invalidate(); return true; }
            for (int h = hits.size() - 1; h >= 0; --h) {
                final Hit hit = hits.get(h);
                if (!hit.r.contains(x, y)) continue;
                active.put(id, hit); downAt.put(id, new float[] { x, y });
                if (hit.longPress) {
                    final int slot = id & 15;
                    holdTimers[slot] = () -> { if (active.get(id) == hit) { hit.hold.run(x, y); active.put(id, null); invalidate(); } };
                    postDelayed(holdTimers[slot], 450);
                } else if (hit.down != null) hit.down.run(x, y);
                break;
            }
            invalidate();
            return true;
        }
        if (a == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < e.getPointerCount(); ++i) {
                final int id = e.getPointerId(i); final float x = e.getX(i), y = e.getY(i);
                if (keyOf.containsKey(id)) {
                    int k = keyAt(x, y), old = keyOf.get(id);
                    if (k >= 0 && k != old) { Native.noteOff(old); Native.noteOn(k, velAt(y)); keyOf.put(id, k); invalidate(); }
                    continue;
                }
                Hit h = active.get(id);
                float[] o = downAt.get(id);
                if (h != null && o != null && Math.hypot(x - o[0], y - o[1]) > dp(8)) {
                    if (h.longPress) { removeCallbacks(holdTimers[id & 15]); }
                    if (h.drag != null) { h.drag.run(x, y); invalidate(); }
                }
            }
            return true;
        }
        if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_POINTER_UP || a == MotionEvent.ACTION_CANCEL) {
            final int i = e.getActionIndex(), id = e.getPointerId(i);
            Integer k = keyOf.remove(id);
            if (k != null) Native.noteOff(k);
            Hit h = active.remove(id);
            float[] o = downAt.remove(id);
            if (h != null && h.longPress) {
                removeCallbacks(holdTimers[id & 15]);
                boolean moved = o != null && Math.hypot(e.getX(i) - o[0], e.getY(i) - o[1]) > dp(8);
                if (!moved && h.down != null && a != MotionEvent.ACTION_CANCEL) h.down.run(e.getX(i), e.getY(i));
            }
            if (a == MotionEvent.ACTION_CANCEL) { for (int n : keyOf.values()) Native.noteOff(n); keyOf.clear(); active.clear(); downAt.clear(); }
            invalidate();
            return true;
        }
        return true;
    }
}
