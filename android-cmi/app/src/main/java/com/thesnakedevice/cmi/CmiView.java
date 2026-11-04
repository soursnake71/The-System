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
    private boolean songEditor = false, portrait = false;
    private int half = 0;   // portrait Page R: steps 1-8 or 9-16
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
    void showPage(char p) { page = p; invalidate(); }

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
        portrait = H > W;
        final float keyH = portrait ? Math.min(H * 0.2f, dp(150)) : Math.min(H * 0.26f, dp(110));
        c.drawColor(0xff151515);
        fill(c, new RectF(0, H - keyH, W, H), CREAM);
        RectF scr = new RectF(dp(6), dp(4), W - dp(6), H - keyH - dp(4));
        fill(c, scr, BG);
        // tabs
        final char[] pages = { '2', '7', 'D', '5', '8', 'F', 'M', 'R' };
        final String[] names = { "VOICES", "CONTROL", "WAVE", "PEN", "SAMPLE", "FILES", "MIXER", "COMPOSER" };
        final String[] shortNames = { "VOX", "CTL", "WAV", "PEN", "SMP", "FIL", "MIX", "SEQ" };
        final float tabX = portrait ? dp(4) : dp(100);
        float tw = (scr.width() - tabX - dp(10)) / 8f;
        if (!portrait) text(c, "PAGE " + page, scr.left + dp(8), scr.top + dp(18), 13, INK);
        for (int i = 0; i < 8; ++i) {
            final char pg = pages[i];
            RectF r = new RectF(scr.left + tabX + i * tw, scr.top + dp(3), scr.left + tabX + (i + 1) * tw - dp(4), scr.top + dp(23));
            if (tw < dp(96)) { button(c, r, tw < dp(60) ? shortNames[i] : pg + " " + shortNames[i], page == pg, () -> { page = pg; status = "READY"; }); continue; }
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
            case 'M': mixer(c, body); break;
            case 'F': files(c, body); break;
            case '5': pen(c, body); break;
            default: composer(c, body); break;
        }
        // scanlines
        p.setColor(0x2a000000);
        for (float y = scr.top; y < scr.bottom; y += dp(2)) c.drawRect(scr.left, y, scr.right, y + dp(0.7f), p);
        keyboard(c, new RectF(0, H - keyH, W, H));
        if (hoverOn) {   // the light pen: the S Pen hovering over the screen
            p.setColor(0x904dffa8); p.setStrokeWidth(dp(1));
            c.drawLine(hoverX - dp(14), hoverY, hoverX + dp(14), hoverY, p); c.drawLine(hoverX, hoverY - dp(14), hoverX, hoverY + dp(14), p);
            p.setStyle(Paint.Style.STROKE); c.drawCircle(hoverX, hoverY, dp(6), p); p.setStyle(Paint.Style.FILL);
        }
        postInvalidateDelayed(40);
    }

    // ---- Page 2 ----
    private boolean mineOnly = false;
    private void library(Canvas c, RectF b) {
        final int n = Native.soundCount(), cur = Native.getSound(), built = Native.builtInCount();
        final List<Integer> list = new ArrayList<>();
        for (int i = 0; i < n; ++i) { boolean user = i >= built; if (user ? Native.userLoaded(i - built) : !mineOnly) list.add(i); }
        button(c, new RectF(b.left, b.top, b.left + dp(70), b.top + dp(22)), "ALL", !mineOnly, () -> { mineOnly = false; libScroll = 0; });
        button(c, new RectF(b.left + dp(76), b.top, b.left + dp(176), b.top + dp(22)), "MINE ONLY", mineOnly, () -> { mineOnly = true; libScroll = 0; });
        text(c, list.size() + " VOICES", b.left + dp(186), b.top + dp(16), 11, DIM);
        final RectF lb = new RectF(b.left, b.top + dp(28), b.right, b.bottom - dp(22));
        final float rowH = dp(24);
        final int rows = Math.max(1, (int) (lb.height() / rowH));
        final float colW = dp(170);
        final int cols = Math.max(1, (int) (lb.width() / colW));
        final int perPage = rows * cols;
        libScroll = Math.max(0, Math.min(libScroll, Math.max(0, list.size() - 1) / perPage * perPage));
        if (list.isEmpty()) text(c, "No voices of your own yet: import them on Page 8.", lb.left, lb.top + dp(16), 12, DIM);
        for (int k = 0; k < perPage && libScroll + k < list.size(); ++k) {
            final int i = list.get(libScroll + k);
            String[] f = info(i);
            RectF r = new RectF(lb.left + (k / rows) * colW, lb.top + (k % rows) * rowH, lb.left + (k / rows + 1) * colW - dp(6), lb.top + (k % rows + 1) * rowH - dp(2));
            if (i == cur) fill(c, r, INK);
            text(c, String.format("%03d %-8s %s", i + 1, f[0], f[2].length() > 6 ? f[2].substring(0, 6) : f[2]), r.left + dp(3), r.bottom - dp(6), 12, i == cur ? BG : INK);
            hit(r, (x, y) -> { Native.loadSound(i); status = "LOADED " + info(i)[0]; });
        }
        if (list.size() > perPage) {
            button(c, new RectF(b.right - dp(90), b.bottom - dp(20), b.right - dp(50), b.bottom), "<", false, () -> libScroll = Math.max(0, libScroll - perPage));
            button(c, new RectF(b.right - dp(44), b.bottom - dp(20), b.right, b.bottom), ">", false, () -> libScroll += perPage);
        }
        String[] f = info(cur);
        if (f.length > 3) text(c, f[0] + ": " + (f[1].length() > 40 ? f[1].substring(0, 40) : f[1]), b.left, b.bottom - dp(4), 11, DIM);
    }

    // ---- Page M: the mixer ----
    private int mixPage = 0;
    private void mixer(Canvas c, RectF b) {
        final List<Integer> strips = new ArrayList<>(); final List<String> names = new ArrayList<>();
        strips.add(0); names.add("KEYS");
        if (Native.currentSong() >= 0) {
            for (int t = 0; t < Native.SONG_TRK; ++t) { String[] f = Native.songTrack(t).split("\\t", -1); if (f.length > 3 && f[2].equals("1")) { strips.add(9 + t); names.add(f[0].length() > 7 ? f[0].substring(0, 7) : f[0]); } }
        } else for (int t = 0; t < Native.TRACKS; ++t) { strips.add(1 + t); names.add(info(Native.trackSound(t))[0]); }
        final int per = portrait ? 6 : 12, pages = Math.max(1, (strips.size() + per - 1) / per);
        mixPage = Math.min(mixPage, pages - 1);
        text(c, "MIXER " + (Native.currentSong() >= 0 ? "SONG" : "PAGE R"), b.left, b.top + dp(12), 12, INK);
        if (pages > 1) button(c, new RectF(b.right - dp(110), b.top, b.right, b.top + dp(22)), "STRIPS " + (mixPage + 1) + "/" + pages, false, () -> mixPage = (mixPage + 1) % pages);
        final int n = Math.min(per, strips.size() - mixPage * per) + 1;
        final float sw = b.width() / n, top = b.top + dp(28), fh = b.height() - dp(110);
        for (int k = 0; k < n; ++k) {
            final boolean master = k == n - 1;
            final int idx = master ? -1 : strips.get(mixPage * per + k);
            final float x = b.left + k * sw;
            final String nm = master ? "MASTER" : names.get(mixPage * per + k);
            textC(c, nm, new RectF(x, top, x + sw, top + dp(14)), 9, INK);
            final RectF slot = new RectF(x + sw / 2 - dp(3), top + dp(20), x + sw / 2 + dp(3), top + dp(20) + fh);
            fill(c, slot, FAINT);
            final float mv = Math.min(1, master ? Native.masterMeter() : Native.mixGet(idx, 4));
            fill(c, new RectF(x + dp(4), slot.bottom - fh * mv, x + dp(8), slot.bottom), mv > 0.95f ? Color.WHITE : DIM);
            final float v = master ? Native.getParam(Native.MASTER) : Native.mixGet(idx, 0), y = slot.bottom - fh * v / 1.5f;
            fill(c, new RectF(x + dp(10), y - dp(6), x + sw - dp(10), y + dp(6)), INK);
            textC(c, v <= 0.001f ? "-INF" : String.format("%.1f", 20 * Math.log10(v)), new RectF(x, slot.bottom + dp(2), x + sw, slot.bottom + dp(14)), 8, DIM);
            Act set = (xx, yy) -> { float nv = Math.max(0, Math.min(1.5f, (slot.bottom - yy) / fh * 1.5f)); if (master) Native.setParam(Native.MASTER, nv); else Native.mixSet(idx, 0, nv); };
            Hit h = hit(new RectF(x, top + dp(16), x + sw, slot.bottom + dp(6)), set); h.drag = set;
            if (master) continue;
            final float py = slot.bottom + dp(18);
            final float po = Native.mixGet(idx, 1);
            fill(c, new RectF(x + dp(4), py, x + sw - dp(4), py + dp(8)), FAINT);
            fill(c, new RectF(x + dp(4) + (sw - dp(12)) * (po < 0 ? 0.5f : po), py - dp(2), x + dp(8) + (sw - dp(12)) * (po < 0 ? 0.5f : po), py + dp(10)), po < 0 ? DIM : INK);
            Act pan = (xx, yy) -> Native.mixSet(idx, 1, Math.max(0, Math.min(1, (xx - x - dp(4)) / (sw - dp(8)))));
            Hit ph = hit(new RectF(x, py - dp(6), x + sw, py + dp(14)), pan); ph.drag = pan; ph.longPress = true; ph.hold = (xx, yy) -> Native.mixSet(idx, 1, -1);
            final boolean mu = Native.mixGet(idx, 2) > 0.5f, so = Native.mixGet(idx, 3) > 0.5f;
            button(c, new RectF(x + dp(2), py + dp(18), x + sw / 2 - dp(1), py + dp(38)), "M", mu, () -> Native.mixSet(idx, 2, mu ? 0 : 1));
            button(c, new RectF(x + sw / 2 + dp(1), py + dp(18), x + sw - dp(2), py + dp(38)), "S", so, () -> Native.mixSet(idx, 3, so ? 0 : 1));
        }
    }

    // ---- Page F: the library folder ----
    private int filePage = 0;
    private void files(Canvas c, RectF b) {
        final String lib = act.libraryName();
        text(c, "LIBRARY: " + (lib.isEmpty() ? "(none set)" : lib) + "   " + Native.catalogCount() + " CMI VOICES", b.left, b.top + dp(12), 11, INK);
        final float y0 = b.top + dp(20), bw = (b.width() - dp(12)) / 3;
        final String[] lbl = { "SET FOLDER", "RESCAN", "DEDUPE", "ALL SONGS", "ALL SOUNDS", "" };
        final Runnable[] fn = { act::chooseLibrary, act::rescanLibrary, () -> { status("" + Native.removeDuplicateSongs() + " DUPLICATES REMOVED"); act.saveState(); },
                () -> act.loadFromLibrary(true, false, -1), () -> act.loadFromLibrary(false, true, -1), () -> {} };
        for (int i = 0; i < 5; ++i) { float rx = b.left + (i % 3) * (bw + dp(6)), ry = y0 + (i / 3) * dp(26); button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], false, fn[i]); }
        final float top = y0 + dp(58), rh = dp(26);
        final int rows = Math.max(1, (int) ((b.bottom - dp(26) - top) / rh));
        final int nf = act.libNames.size(), pages = Math.max(1, (nf + rows - 1) / rows);
        filePage = Math.min(filePage, pages - 1);
        if (nf == 0) text(c, lib.isEmpty() ? "Pick the folder with your songs, CMI disks and samples." : "No songs, disks or samples found there.", b.left, top + dp(14), 11, DIM);
        for (int r = 0; r < rows && filePage * rows + r < nf; ++r) {
            final int i = filePage * rows + r;
            final String nm = act.libNames.get(i), lo = nm.toLowerCase();
            final String tag = lo.endsWith(".mid") || lo.endsWith(".midi") ? "SONG" : lo.endsWith(".zip") ? "ZIP" : lo.endsWith(".vc") ? "VC" : lo.endsWith(".wav") ? "WAV" : "DISK";
            final RectF row = new RectF(b.left, top + r * rh, b.right, top + (r + 1) * rh - dp(2));
            text(c, tag, row.left, row.bottom - dp(7), 10, DIM);
            text(c, nm.length() > 40 ? nm.substring(0, 40) : nm, row.left + dp(40), row.bottom - dp(7), 11, tag.equals("SONG") ? INK : DIM);
            hit(row, (xx, yy) -> act.loadFromLibrary(false, false, i));
        }
        if (pages > 1) {
            final float y = b.bottom - dp(22);
            button(c, new RectF(b.left, y, b.left + dp(40), y + dp(22)), "<", false, () -> filePage = Math.max(0, filePage - 1));
            textC(c, (filePage + 1) + "/" + pages, new RectF(b.left + dp(40), y, b.left + dp(100), y + dp(22)), 11, INK);
            button(c, new RectF(b.left + dp(100), y, b.left + dp(140), y + dp(22)), ">", false, () -> filePage = Math.min(pages - 1, filePage + 1));
        }
    }

    // ---- Page 5: the light pen (the S Pen, or a finger): draw the start and end waveforms ----
    final float[][] cyc = new float[2][128];
    private final RectF[] penBoxes = { new RectF(), new RectF() };
    private int penBox = 0, penLastX = -1; private float penLastV = 0; private boolean penStylus = false;
    private void pen(Canvas c, RectF b) {
        text(c, "LIGHT PEN: draw the start and the end waveform", b.left, b.top + dp(12), 11, INK);
        final float bh = (b.height() - dp(120)) / 2;
        for (int k = 0; k < 2; ++k) {
            final RectF r = new RectF(b.left, b.top + dp(20) + k * (bh + dp(8)), b.right, b.top + dp(20) + k * (bh + dp(8)) + bh);
            penBoxes[k] = r;
            frame(c, r, k == penBox ? INK : DIM);
            p.setColor(FAINT); c.drawLine(r.left, r.centerY(), r.right, r.centerY(), p);
            text(c, k == 0 ? "START" : "END", r.left + dp(4), r.top + dp(12), 9, DIM);
            Path path = new Path();
            for (int i = 0; i < 128; ++i) { float x = r.left + r.width() * i / 127f, y = r.centerY() - cyc[k][i] * r.height() * 0.48f; if (i == 0) path.moveTo(x, y); else path.lineTo(x, y); }
            Paint lp = new Paint(Paint.ANTI_ALIAS_FLAG); lp.setStyle(Paint.Style.STROKE); lp.setStrokeWidth(dp(2)); lp.setColor(INK); c.drawPath(path, lp);
        }
        final float y = b.bottom - dp(92), bw = (b.width() - dp(12)) / 3;
        final String[] lbl = { "SINE", "SAW", "SQUARE", "TRIANGLE", "CLEAR", "START>END" };
        for (int i = 0; i < 6; ++i) {
            final int kind = i;
            float rx = b.left + (i % 3) * (bw + dp(6)), ry = y + (i / 3) * dp(26);
            button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], false, () -> {
                if (kind == 5) { System.arraycopy(cyc[0], 0, cyc[1], 0, 128); return; }
                for (int j = 0; j < 128; ++j) { float ph = j / 128f; cyc[penBox][j] = kind == 0 ? (float) Math.sin(2 * Math.PI * ph) : kind == 1 ? 1 - 2 * ph : kind == 2 ? (ph < 0.5f ? 1 : -1) : kind == 3 ? (ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph) : 0; }
                if (Native.hasDrawn()) Native.saveDrawn(cyc[0], cyc[1], false);
            });
        }
        button(c, new RectF(b.left, y + dp(56), b.right, y + dp(80)), "SAVE AS A NEW VOICE (then every stroke updates it)", false, () -> { String e = Native.saveDrawn(cyc[0], cyc[1], true); status(e.isEmpty() ? "DRAWN VOICE ON THE KEYBOARD" : e); act.saveState(); });
    }
    private boolean penTouch(MotionEvent e) {
        final int a = e.getActionMasked();
        final boolean stylus = e.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS || e.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER;
        if (stylus) penStylus = true;
        if (penStylus && !stylus) return false;   // the S Pen is in use: a resting palm doesn't draw
        int k = -1; for (int i = 0; i < 2; ++i) if (penBoxes[i].contains(e.getX(), e.getY())) k = i;
        if (a == MotionEvent.ACTION_DOWN) { if (k < 0) return false; penBox = k; penLastX = -1; }
        if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) { penLastX = -1; if (Native.hasDrawn()) { Native.saveDrawn(cyc[0], cyc[1], false); act.saveState(); } return true; }
        final RectF r = penBoxes[penBox];
        final boolean erase = e.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER || (e.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY) != 0;
        for (int hI = 0; hI <= e.getHistorySize(); ++hI) {
            final float x = hI < e.getHistorySize() ? e.getHistoricalX(hI) : e.getX(), y = hI < e.getHistorySize() ? e.getHistoricalY(hI) : e.getY();
            final int px = Math.max(0, Math.min(127, Math.round((x - r.left) / r.width() * 127)));
            final float pv = erase ? 0 : Math.max(-1, Math.min(1, (r.centerY() - y) / (r.height() * 0.48f)));
            if (penLastX < 0) cyc[penBox][px] = pv;
            else { int lo = Math.min(penLastX, px), hi = Math.max(penLastX, px); for (int i = lo; i <= hi; ++i) cyc[penBox][i] = hi == lo ? pv : penLastV + (pv - penLastV) * (i - penLastX) / (float) (px - penLastX); }
            penLastX = px; penLastV = pv;
        }
        invalidate(); return true;
    }
    private float hoverX, hoverY; private boolean hoverOn = false;
    @Override public boolean onHoverEvent(MotionEvent e) {
        final int a = e.getActionMasked();
        hoverOn = a != MotionEvent.ACTION_HOVER_EXIT && e.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS;
        hoverX = e.getX(); hoverY = e.getY(); invalidate();
        return true;
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
        final int rows = portrait ? 24 : 13;
        final float rh = Math.min(dp(26), b.height() / rows), cw = portrait ? b.width() : (b.width() - dp(16)) / 2;
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
        final float rx = portrait ? b.left : b.left + cw + dp(16), ry = portrait ? b.top + left.length * rh : b.top;
        for (int i = 0; i < right.length; ++i) { Object[] o = right[i]; barRow(c, new RectF(rx, ry + i * rh, b.right, ry + (i + 1) * rh), (Integer) o[0], (String) o[1], (Float) o[2], (Float) o[3], (Boolean) o[4], (Boolean) o[5], (Fmt) o[6]); }
        button(c, new RectF(rx, ry + right.length * rh + dp(2), rx + dp(154), ry + right.length * rh + Math.min(rh, dp(24))), "RESET TO VOICE", false,
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
        final float infoW = portrait ? 0 : dp(150), infoH = portrait ? Math.min(dp(200), b.height() * 0.4f) : 0;
        final float wh = b.height() - infoH;
        final float w = (b.width() - infoW) * 0.62f, dx = (b.width() - infoW) * 0.3f, h = wh * 0.16f, dy = wh * 0.68f;
        final float x0 = b.left, y0 = b.top + wh - h * 0.6f;
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
        Hit hh = hit(new RectF(b.left, b.top, b.right - infoW, b.top + wh), pick); hh.drag = pick;
        float ix = portrait ? b.left : b.right - infoW + dp(6), iy = portrait ? b.top + wh + dp(14) : b.top + dp(12);
        String[] lines = { f[0], "PAGE " + (wavePage + 1) + "/" + np, "RATE " + f[3] + " Hz", "ROOT " + noteName(Integer.parseInt(f[4])),
                "LOOP " + (ls >= 0 ? (ls + 1) + "-" + (le + 1) : "NONE"), f[8].equals("1") ? "WAVEFORM" : "SAMPLE", "8 BIT " + len + " B" };
        for (int i = 0; i < lines.length; ++i) text(c, lines[i], ix, iy + i * dp(16), 11, i == 0 ? INK : DIM);
        // channel cards
        Native.channelEnv(env);
        float cy = portrait ? iy - dp(10) : iy + lines.length * dp(16) + dp(6);
        if (portrait) ix = b.left + b.width() * 0.45f;
        float bw = ((portrait ? b.right - ix : infoW) - dp(12)) / 8f, bh = Math.min(dp(44), (b.bottom - cy) / 2 - dp(6));
        for (int k = 0; k < Native.NCH; ++k) {
            RectF r = new RectF(ix + (k % 8) * bw, cy + (k / 8) * (bh + dp(6)), ix + (k % 8 + 1) * bw - dp(3), cy + (k / 8) * (bh + dp(6)) + bh);
            fill(c, r, FAINT);
            fill(c, new RectF(r.left, r.bottom - r.height() * env[k] / 255f, r.right, r.bottom), k < 8 ? INK : DIM);
        }
    }

    // ---- Page 8 ----
    private int userPage = 0;
    private void sampling(Canvas c, RectF b) {
        text(c, "DISKS & SAMPLING  " + Native.userCount() + " OF " + Native.USER_SLOTS + " USER VOICES", b.left, b.top + dp(12), 12, INK);
        text(c, "CMI disks (.IMD .IMG) and .VC voices load as the CMI holds them. Audio files too.", b.left, b.top + dp(28), 9, DIM);
        float y0 = b.top + dp(36);
        float bw = (b.width() - dp(12)) / 3;
        button(c, new RectF(b.left, y0, b.left + bw, y0 + dp(26)), "DISKS/FILES", false, act::importFiles);
        button(c, new RectF(b.left + bw + dp(6), y0, b.left + 2 * bw + dp(6), y0 + dp(26)), "IMPORT FOLDER", false, act::importFolder);
        button(c, new RectF(b.left + 2 * bw + dp(12), y0, b.right, y0 + dp(26)), "CLEAR ALL", false, () -> {
            new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Clear all user voices?")
                    .setPositiveButton("Clear", (d2, w) -> { for (int k = 0; k < Native.USER_SLOTS; ++k) Native.clearUser(k); status("USER VOICES CLEARED"); act.saveState(); })
                    .setNegativeButton("Cancel", null).show();
        });
        final float top = y0 + dp(32), bottom = b.bottom - dp(30);
        final float rh = portrait ? dp(46) : dp(28);
        final int per = Math.max(1, (int) ((bottom - top) / rh));
        final int pages = (Native.USER_SLOTS + per - 1) / per;
        userPage = Math.max(0, Math.min(pages - 1, userPage));
        for (int r = 0; r < per; ++r) {
            final int slot = userPage * per + r;
            if (slot >= Native.USER_SLOTS) break;
            float y = top + r * rh;
            boolean has = Native.userLoaded(slot);
            String[] f = info(Native.userSlot(slot));
            boolean cmiVoice = has && !f[2].equals("User");
            String line = String.format("USER%03d ", slot + 1) + (!has ? "--" : cmiVoice ? f[0] + "  " + f[2] + "  MODE " + (f[8].equals("1") ? "1" : "4") + (Integer.parseInt(f[6]) >= 0 ? "  LOOP" : "")
                    : f[0] + "  " + f[3] + "Hz  " + noteName(Integer.parseInt(f[4])) + (Integer.parseInt(f[6]) >= 0 ? "  LOOP" : ""));
            final float ty = portrait ? y + dp(14) : y + rh * 0.65f;
            text(c, line, b.left, ty, 12, has ? INK : DIM);
            final float btop = portrait ? y + dp(19) : y + dp(2), bh = portrait ? rh - dp(3) : rh - dp(3);
            final float w3 = portrait ? (b.width() - dp(12)) / 3 : dp(64);
            final float bx = portrait ? b.left : b.right - 3 * w3 - dp(12);
            button(c, new RectF(bx, btop, bx + w3, y + bh), "LOAD", false, () -> act.pickSample(slot));
            button(c, new RectF(bx + w3 + dp(6), btop, bx + 2 * w3 + dp(6), y + bh), "PLAY", false, () -> { Native.loadSound(Native.userSlot(slot)); status = "LOADED"; });
            button(c, new RectF(bx + 2 * w3 + dp(12), btop, bx + 3 * w3 + dp(12), y + bh), "CLEAR", false, () -> { Native.clearUser(slot); act.saveState(); });
        }
        float y = b.bottom - dp(24);
        button(c, new RectF(b.left, y, b.left + dp(34), y + dp(22)), "<", false, () -> userPage = Math.max(0, userPage - 1));
        textC(c, (userPage * per + 1) + "-" + Math.min(Native.USER_SLOTS, (userPage + 1) * per), new RectF(b.left + dp(34), y, b.left + dp(96), y + dp(22)), 11, INK);
        button(c, new RectF(b.left + dp(96), y, b.left + dp(130), y + dp(22)), ">", false, () -> userPage = Math.min(pages - 1, userPage + 1));
        text(c, "ROOT " + noteName(rootKey), b.left + dp(140), y + dp(16), 11, INK);
        button(c, new RectF(b.left + dp(200), y, b.left + dp(230), y + dp(22)), "-", false, () -> rootKey = Math.max(24, rootKey - 1));
        button(c, new RectF(b.left + dp(234), y, b.left + dp(264), y + dp(22)), "+", false, () -> rootKey = Math.min(96, rootKey + 1));
        button(c, new RectF(b.left + dp(272), y, Math.min(b.right, b.left + dp(372)), y + dp(22)), loopSample ? "LOOP ON" : "LOOP OFF", loopSample, () -> loopSample = !loopSample);
    }

    // ---- Page R ----
    // the song list: Page R patterns, the imported MIDI songs, import, remove
    private void songsDialog() {
        final int n = Native.songCount(), cur = Native.currentSong();
        final List<String> items = new ArrayList<>();
        items.add((cur < 0 ? "> " : "   ") + "Page R patterns");
        for (int k = 0; k < n; ++k) items.add((cur == k ? "> " : "   ") + Native.songName(k));
        items.add("   Import MIDI file...");
        items.add("   New empty song (for the grid)");
        if (cur >= 0) items.add("   Remove this song");
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Songs")
                .setItems(items.toArray(new String[0]), (dlg, w) -> {
                    if (w == 0) Native.selectSongAt(-1);
                    else if (w <= n) Native.selectSongAt(w - 1);
                    else if (w == n + 1) act.importMidi();
                    else if (w == n + 2) { Native.newSong(); grid = true; selTrack = 0; vStart = 0; act.saveState(); }
                    else { Native.removeSong(); act.saveState(); }
                    invalidate();
                }).show();
    }
    private void songVoiceDialog(final int track) {
        final int n = Native.soundCount(), built = Native.builtInCount();
        final List<Integer> ids = new ArrayList<>(); final List<String> names = new ArrayList<>();
        final boolean lib = Native.catalogCount() > 0;
        if (lib) { ids.add(-100); names.add(">> CMI LIBRARY (your disks)"); }
        ids.add(Native.GM_KIT); names.add("DRUM KIT  (a voice per GM drum note)");
        for (int i = 0; i < n; ++i) { if (i >= built && !Native.userLoaded(i - built)) continue; String[] f = info(i); ids.add(i); names.add(f[0] + "  (" + f[2] + ")  " + f[1]); }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Track " + (track + 1) + " voice")
                .setItems(names.toArray(new String[0]), (dlg, w) -> {
                    if (ids.get(w) == -100) { libraryDialog(track); return; }
                    Native.setSongVoice(track, ids.get(w)); if (ids.get(w) == Native.GM_KIT) Native.rememberKit(track); act.saveState(); invalidate(); })
                .show();
    }
    // the CMI library by disk, then the disk's voices
    private void libraryDialog(final int track) {
        final String[] lines = Native.catalogList().split("\n");
        final java.util.TreeMap<String, List<Integer>> byDisk = new java.util.TreeMap<>();
        for (int i = 0; i < lines.length; ++i) { String[] f = lines[i].split("\t", -1); if (f.length < 3) continue; byDisk.computeIfAbsent(f[1], k -> new ArrayList<>()).add(i); }
        final String[] disks = byDisk.keySet().toArray(new String[0]);
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Track " + (track + 1) + ": CMI library")
                .setItems(disks, (d1, w) -> {
                    final List<Integer> vs = byDisk.get(disks[w]);
                    final String[] nm = new String[vs.size()];
                    for (int k = 0; k < vs.size(); ++k) { String[] f = lines[vs.get(k)].split("\t", -1); nm[k] = f[0] + (f[2].equals("1") ? "  (loop)" : ""); }
                    new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(disks[w])
                            .setItems(nm, (d2, k) -> { Native.useCatalogVoice(track, vs.get(k)); act.saveState(); invalidate(); }).show();
                }).show();
    }

    private int[] notesCache = new int[0]; private String notesFor = "";
    // the editing grid (one track as a piano roll)
    private boolean grid = false, gridActive = false;
    private double vStart = 0, vBeats = 16, snapB = 0.25; private int pLow = 48, gridTrack = -1, selNote = -1;
    private int[] gNotes = new int[0]; private boolean gStale = true; private String gridFor = "";
    private RectF gridRect = new RectF();
    private int gIdx = -1, gMode = 0; private float gDownX, gDownY; private boolean gMoved, gLong; private double gStartV; private int gStartP; private int[] gOrig = new int[5];
    private final Runnable gLongPress = () -> { if (!gMoved && gIdx >= 0) { removeNote(gIdx); gIdx = -1; gLong = true; invalidate(); } };

    private void commitNotes() { Native.setSongNoteList(gNotes); gStale = true; notesFor = ""; act.saveState(); }
    private void removeNote(int i) { int[] n = new int[gNotes.length - 5]; System.arraycopy(gNotes, 0, n, 0, i * 5); System.arraycopy(gNotes, i * 5 + 5, n, i * 5, gNotes.length - i * 5 - 5); gNotes = n; commitNotes(); }

    private float gx() { return gridRect.left + dp(28); }
    private float rowH() { return gridRect.height() / 24f; }
    private double beatAt(float x) { return vStart + (x - gx()) / (gridRect.right - gx()) * vBeats; }
    private int pitchAt(float y) { return pLow + (int) ((gridRect.bottom - y) / rowH()); }
    private float xOfBeat(double b) { return gx() + (float) ((b - vStart) / vBeats) * (gridRect.right - gx()); }
    private int noteAt(float x, float y) {
        final double b = beatAt(x); final int p = pitchAt(y);
        for (int i = 0; i + 4 < gNotes.length; i += 5) {
            if (gNotes[i] != selTrack || gNotes[i + 3] != p) continue;
            final double st = gNotes[i + 1] / 480.0, ln = Math.max(gNotes[i + 2] / 480.0, snapB);
            if (b >= st && b <= st + ln) return i / 5;
        }
        return -1;
    }
    private boolean gridTouch(MotionEvent e, int a, float x, float y) {
        if (a == MotionEvent.ACTION_DOWN) {
            gDownX = x; gDownY = y; gMoved = false; gLong = false; gStartV = vStart; gStartP = pLow;
            gIdx = noteAt(x, y);
            if (gIdx >= 0) { System.arraycopy(gNotes, gIdx * 5, gOrig, 0, 5); final double end = (gOrig[1] + gOrig[2]) / 480.0; gMode = x > xOfBeat(end) - (xOfBeat(end) - xOfBeat(gOrig[1] / 480.0)) * 0.3f ? 1 : 0; selNote = gIdx; }
            else gMode = 2;
            postDelayed(gLongPress, 500);
            return true;
        }
        if (a == MotionEvent.ACTION_MOVE) {
            if (!gMoved && Math.hypot(x - gDownX, y - gDownY) > dp(8)) { gMoved = true; removeCallbacks(gLongPress); }
            if (!gMoved || gLong) return true;
            if (gMode == 2) {
                vStart = Math.max(0, gStartV - (x - gDownX) / (gridRect.right - gx()) * vBeats);
                pLow = Math.max(0, Math.min(103, gStartP + Math.round((y - gDownY) / rowH())));
            } else if (gIdx >= 0 && gIdx * 5 + 4 < gNotes.length) {
                final double db = Math.round((beatAt(x) - beatAt(gDownX)) / snapB) * snapB;
                if (gMode == 0) {
                    gNotes[gIdx * 5 + 1] = (int) Math.max(0, Math.round((gOrig[1] / 480.0 + db) * 480));
                    gNotes[gIdx * 5 + 3] = Math.max(0, Math.min(127, gOrig[3] + pitchAt(y) - pitchAt(gDownY)));
                } else gNotes[gIdx * 5 + 2] = (int) Math.round(Math.max(snapB, Math.round((gOrig[2] / 480.0 + db) / snapB) * snapB) * 480);
            }
            invalidate(); return true;
        }
        if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
            removeCallbacks(gLongPress);
            if (gLong || a == MotionEvent.ACTION_CANCEL) { gIdx = -1; return true; }
            if (gMoved) { if (gMode != 2 && gIdx >= 0) commitNotes(); }
            else if (gIdx < 0) {   // a tap on an empty place: a new note
                final double st = Math.max(0, Math.floor(beatAt(x) / snapB) * snapB);
                int[] n = new int[gNotes.length + 5]; System.arraycopy(gNotes, 0, n, 0, gNotes.length);
                n[gNotes.length] = selTrack; n[gNotes.length + 1] = (int) Math.round(st * 480); n[gNotes.length + 2] = (int) Math.round(Math.max(snapB, 0.5) * 480);
                n[gNotes.length + 3] = Math.max(0, Math.min(127, pitchAt(y))); n[gNotes.length + 4] = 100;
                gNotes = n; selNote = gNotes.length / 5 - 1; commitNotes();
            }
            gIdx = -1; invalidate(); return true;
        }
        return true;
    }

    private void drawGrid(Canvas c, RectF r, double beat, double barBeats, String key) {
        gridRect = r;
        if (gStale || !key.equals(gridFor)) { gNotes = Native.songNoteList(); gStale = false; gridFor = key; }
        if (selTrack != gridTrack) {
            gridTrack = selTrack; selNote = -1;
            int sum = 0, cnt = 0; for (int i = 0; i + 4 < gNotes.length; i += 5) if (gNotes[i] == selTrack) { sum += gNotes[i + 3]; ++cnt; }
            if (cnt > 0) pLow = Math.max(0, Math.min(103, sum / cnt - 12));
        }
        if (Native.songRunning() && gIdx < 0 && (beat < vStart || beat > vStart + vBeats)) vStart = Math.floor(beat / 4) * 4;
        final float rh = rowH(), gx = gx();
        for (int row = 0; row < 24; ++row) {
            final int pt = pLow + row, k = pt % 12; final float y = r.bottom - (row + 1) * rh;
            final boolean black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
            fill(c, new RectF(gx, y, r.right, y + rh), black ? 0xff031208 : BG);
            fill(c, new RectF(r.left, y, gx - dp(2), y + rh - 1), black ? 0xff0d2a1c : FAINT);
            if (k == 0) text(c, noteName(pt), r.left + dp(1), y + rh - dp(2), 8, INK);
        }
        for (double b = Math.ceil(vStart / snapB) * snapB; b <= vStart + vBeats; b += snapB) {
            final boolean bar = Math.abs(b / barBeats - Math.rint(b / barBeats)) < 1e-6, bt = Math.abs(b - Math.rint(b)) < 1e-6;
            if (!bt && vBeats > 32) continue;
            p.setColor(bar ? DIM : bt ? FAINT : 0xff08200f); c.drawRect(xOfBeat(b), r.top, xOfBeat(b) + (bar ? dp(1.2f) : dp(0.6f)), r.bottom, p);
            if (bar) text(c, "" + (1 + Math.round(b / barBeats)), xOfBeat(b) + dp(2), r.top + dp(9), 8, DIM);
        }
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i + 4 < gNotes.length; i += 5) {
                if ((gNotes[i] == selTrack) != (pass == 1)) continue;
                final double st = gNotes[i + 1] / 480.0, ln = gNotes[i + 2] / 480.0; final int pt = gNotes[i + 3];
                if (st + ln < vStart || st > vStart + vBeats || pt < pLow || pt >= pLow + 24) continue;
                final float y = r.bottom - (pt - pLow + 1) * rh, x0 = Math.max(gx, xOfBeat(st)), x1 = Math.min(r.right, xOfBeat(st + ln));
                if (pass == 0) { fill(c, new RectF(x0, y + rh * 0.35f, Math.max(x0 + 1, x1), y + rh * 0.65f), FAINT); continue; }
                final int alpha = 110 + gNotes[i + 4];
                fill(c, new RectF(x0, y + 1, Math.max(x0 + dp(2), x1 - 1), y + rh - 1), (Math.min(255, alpha) << 24) | (INK & 0xffffff));
                if (i / 5 == selNote) frame(c, new RectF(x0, y + 1, Math.max(x0 + dp(2), x1 - 1), y + rh - 1), Color.WHITE);
            }
        if (beat >= vStart && beat <= vStart + vBeats) { p.setColor(Color.WHITE); c.drawRect(xOfBeat(beat), r.top, xOfBeat(beat) + dp(1.5f), r.bottom, p); }
        frame(c, r, INK);
    }

    // a MIDI song on Page R: sixteen tracks, their voices, the whole song at a glance, or one track on the grid
    private void songView(Canvas c, RectF b) {
        final String[] si = Native.songInfo().split("\\t", -1);
        if (si.length < 6) return;
        final double len = Math.max(1, Double.parseDouble(si[4])), beat = Double.parseDouble(si[5]);
        final int num = Integer.parseInt(si[2]), den = Integer.parseInt(si[3]);
        final double barBeats = num * 4.0 / den;
        final boolean running = Native.songRunning(), loop = Native.songLoop();
        float x = b.left;
        text(c, si[0].length() > 16 ? si[0].substring(0, 16) : si[0], x, b.top + dp(17), 13, INK);
        button(c, new RectF(b.right - dp(76), b.top, b.right, b.top + dp(22)), "SONGS", false, this::songsDialog);
        button(c, new RectF(b.right - dp(158), b.top, b.right - dp(82), b.top + dp(22)), "EXPORT", false, this::exportDialog);
        final float t0 = b.top + dp(26);
        button(c, new RectF(x, t0, x + dp(56), t0 + dp(22)), "PLAY", running, () -> Native.songPlay(true)); x += dp(60);
        button(c, new RectF(x, t0, x + dp(56), t0 + dp(22)), "STOP", !running, () -> Native.songPlay(false)); x += dp(60);
        button(c, new RectF(x, t0, x + dp(60), t0 + dp(22)), loop ? "LOOP" : "ONCE", loop, () -> Native.setSongLoop(!loop)); x += dp(66);
        text(c, String.format("%s BPM %d/%d  BAR %d/%d", si[1], num, den, 1 + (int) (beat / barBeats), (int) Math.ceil(len / barBeats)), x, t0 + dp(16), 10, DIM);

        // the footer
        final float footH = dp(50), by = b.bottom - footH;
        final String[] tr = Native.songTrack(selTrack).split("\\t", -1);
        final boolean muted = tr.length > 3 && tr[3].equals("1");
        final String[] lbl; final Runnable[] fns;
        if (!grid) {
            final boolean lib = Native.catalogCount() > 0;
            lbl = new String[] { muted ? "MUTED" : "AUDIBLE", "VOICE", lib ? "CMI LIBRARY" : "AUTO MATCH", "EDIT GRID", "NEW SONG", "PATTERNS" };
            fns = new Runnable[] { () -> { Native.setSongMute(selTrack, !muted); act.saveState(); }, () -> songVoiceDialog(selTrack),
                    () -> { if (lib) status(Native.connectToLibrary() + " TRACKS ON CMI LIBRARY VOICES"); else { Native.gmVoices(); status("SET A LIBRARY FOLDER (FIL) FOR THE ORIGINAL VOICES"); } act.saveState(); },
                    () -> grid = true, () -> { Native.newSong(); grid = true; selTrack = 0; vStart = 0; act.saveState(); }, () -> Native.selectSongAt(-1) };
        } else {
            final String sn = snapB >= 1 ? "1/4" : snapB >= 0.5 ? "1/8" : snapB >= 0.25 ? "1/16" : "1/32";
            lbl = new String[] { "UNDO", "SNAP " + sn, "ZOOM-", "ZOOM+", "VEL", "TRACKS" };
            fns = new Runnable[] { () -> { if (Native.undoSongEdit()) { gStale = true; status("UNDONE"); act.saveState(); } }, () -> snapB = snapB >= 1 ? 0.125 : snapB * 2,
                    () -> vBeats = Math.min(256, vBeats * 1.5), () -> vBeats = Math.max(2, vBeats / 1.5),
                    () -> { if (selNote >= 0 && selNote * 5 + 4 < gNotes.length) { int v = gNotes[selNote * 5 + 4]; gNotes[selNote * 5 + 4] = v >= 120 ? 64 : v >= 90 ? 127 : 100; commitNotes(); } },
                    () -> grid = false };
        }
        final float bw = (b.width() - dp(12)) / 3;
        for (int i = 0; i < lbl.length; ++i) { float rx = b.left + (i % 3) * (bw + dp(6)), ry = by + (i / 3) * dp(26); button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], i == 0 && !grid && !muted, fns[i]); }

        RectF g = new RectF(b.left, t0 + dp(28), b.right, by - dp(6));
        final int nT = Native.SONG_TRK;
        final float nameW = grid ? Math.min(dp(84), g.width() * 0.22f) : Math.min(dp(120), g.width() * 0.3f), rh = g.height() / nT;
        for (int t = 0; t < nT; ++t) {
            final int tk = t;
            final String[] f = Native.songTrack(t).split("\\t", -1);
            if (f.length < 4) continue;
            final boolean used = f[2].equals("1"), mute = f[3].equals("1");
            final int v = Integer.parseInt(f[1]);
            RectF nr = new RectF(g.left, g.top + t * rh, g.left + nameW, g.top + (t + 1) * rh);
            if (t == selTrack) fill(c, nr, FAINT);
            text(c, (t + 1) + " " + (used ? (v == Native.GM_KIT ? "DRUMKIT" : info(v)[0]) : "--"), nr.left + dp(2), nr.top + rh * 0.72f, Math.min(11, rh / d * 0.62f), used && !mute ? INK : DIM);
            if (!grid && rh > dp(20)) text(c, f[0].length() > 12 ? f[0].substring(0, 12) : f[0], nr.left + dp(70), nr.top + rh * 0.72f, 8, DIM);
            Hit h = hit(nr, (xx, yy) -> selTrack = tk);
            h.longPress = true; h.hold = (xx, yy) -> { selTrack = tk; songVoiceDialog(tk); };
        }
        final RectF area = new RectF(g.left + nameW + dp(4), g.top, g.right, g.bottom);
        if (grid) { drawGrid(c, area, beat, barBeats, si[0] + si[4] + "g"); return; }
        gridRect = new RectF();
        if (!notesFor.equals(si[0] + si[4])) { notesCache = Native.songNotes(); notesFor = si[0] + si[4]; gStale = true; }
        for (int t = 0; t < nT; ++t) { if (t == selTrack) fill(c, new RectF(area.left, area.top + t * rh, area.right, area.top + (t + 1) * rh), FAINT); }
        final float gw = area.width();
        for (int i = 0; i + 3 < notesCache.length; i += 4) {
            final int t = notesCache[i];
            final float y = area.top + t * rh + dp(1) + (rh - dp(2)) * (1 - notesCache[i + 3] / 1000f);
            final float x0 = area.left + gw * notesCache[i + 1] / 10000f, x1 = area.left + gw * notesCache[i + 2] / 10000f;
            p.setColor(DIM); c.drawRect(x0, y - dp(0.7f), Math.max(x0 + dp(1), x1), y + dp(0.7f), p);
        }
        frame(c, area, DIM);
        p.setColor(Color.WHITE); final float px = area.left + gw * (float) (beat / len); c.drawRect(px, area.top, px + dp(1.5f), area.bottom, p);
        hit(area, (xx, yy) -> selTrack = Math.max(0, Math.min(nT - 1, (int) ((yy - area.top) / rh))));
    }

    private void exportDialog() {
        final boolean song = Native.currentSong() >= 0;
        final String[] items = song ? new String[] { "Song as a WAV file", "Stems: a WAV file per track (pick a folder)", "Song as a MIDI file" } : new String[] { "Page R song as a WAV file" };
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Export")
                .setItems(items, (dlg, w) -> act.export(w)).show();
    }

    private void composer(Canvas c, RectF b) {
        if (Native.currentSong() >= 0) { songView(c, b); return; }
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
        float row2 = b.top;
        if (portrait) { x = b.left; row2 = b.top + dp(26); }
        final float t0 = row2;
        button(c, new RectF(b.right - dp(76), b.top, b.right, b.top + dp(22)), "SONGS", false, this::songsDialog);
        final float tempo = Native.getParam(Native.TEMPO);
        text(c, String.format("SPEED %.0f", tempo), x, t0 + dp(17), 12, INK); x += dp(78);
        button(c, new RectF(x, t0, x + dp(26), t0 + dp(22)), "-", false, () -> Native.setParam(Native.TEMPO, Math.max(40, Math.round(tempo) - 2))); x += dp(28);
        button(c, new RectF(x, t0, x + dp(26), t0 + dp(22)), "+", false, () -> Native.setParam(Native.TEMPO, Math.min(240, Math.round(tempo) + 2))); x += dp(36);
        button(c, new RectF(x, t0, x + dp(56), t0 + dp(22)), "PLAY", running, () -> Native.seqRun(true)); x += dp(60);
        button(c, new RectF(x, t0, x + dp(56), t0 + dp(22)), "STOP", !running, () -> Native.seqRun(false)); x += dp(64);
        final boolean songOn = Native.getParam(Native.SONGMODE) > 0.5f;
        button(c, new RectF(x, t0, x + dp(64), t0 + dp(22)), "SONG", songOn, () -> Native.setParam(Native.SONGMODE, songOn ? 0 : 1)); x += dp(68);
        if (running) text(c, "BAR " + (Native.seqBar() + 1), x, t0 + dp(17), 12, DIM);

        // bottom line: editing
        final float footH = portrait ? dp(50) : dp(22);
        float by = b.bottom - footH;
        final boolean aud = Native.trackAudible(selTrack);
        float bx = b.left;
        String[] lbl = { aud ? "AUDIBLE" : "MUTED", "INSERT", "DELETE", "ZERO", "FILL", "VOICE", songEditor ? "PATTERN" : "SONG" };
        final int perRow = portrait ? 4 : lbl.length;
        float bw = (b.width() - dp(6) * (perRow - 1)) / perRow;
        Runnable[] fns = {
                () -> Native.setTrackAudible(selTrack, !aud),
                () -> { if (pat + 1 < Native.NPAT) { Native.copyPattern(pat, pat + 1); Native.setPattern(pat + 1); status = "COPIED"; } },
                () -> { for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, selTrack, s, 0, 0); },
                () -> { for (int t = 0; t < Native.TRACKS; ++t) for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, t, s, 0, 0); },
                () -> { for (int s = 0; s < Native.STEPS; ++s) Native.setStep(pat, selTrack, s, s % 2 == 1 ? 80 : 110, 0); },
                () -> chooseSound(selTrack),
                () -> songEditor = !songEditor,
        };
        for (int i = 0; i < lbl.length; ++i) {
            final float ry = by + (i / perRow) * dp(28), rx = b.left + (i % perRow) * (bw + dp(6));
            button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], i == 0 ? aud : (i == 6 && songEditor), fns[i]);
        }
        if (portrait && !songEditor) {   // the eighth slot of the footer: which half of the bar is on screen
            final float ry = by + dp(28), rx = b.left + 3 * (bw + dp(6));
            button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), half == 0 ? "STEPS 1-8" : "STEPS 9-16", false, () -> half ^= 1);
        }

        RectF g = new RectF(b.left, t0 + dp(28), b.right, by - dp(6));
        if (songEditor) {
            final int n = Native.songLen(), bar = running && songOn ? Native.seqBar() : -1;
            final int perLine = portrait ? 8 : 16, lines = Native.SONG_MAX / perLine;
            float cw = g.width() / perLine, ch = Math.min(dp(40), (g.height() - dp(16)) / lines - dp(4));
            for (int i = 0; i < Native.SONG_MAX; ++i) {
                final int k = i;
                RectF r = new RectF(g.left + (i % perLine) * cw, g.top + (i / perLine) * (ch + dp(4)), g.left + (i % perLine + 1) * cw - dp(3), g.top + (i / perLine) * (ch + dp(4)) + ch);
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
        final int shown = portrait ? 8 : Native.STEPS, first = portrait ? half * 8 : 0;
        final float gx = g.left + nameW + dp(20), gw = g.right - gx, cw = gw / shown;
        final int visNow = stepNow - first;
        if (stepNow >= 0 && Native.seqPat() == pat && visNow >= 0 && visNow < shown) fill(c, new RectF(gx + visNow * cw, g.top, gx + (visNow + 1) * cw, g.bottom), FAINT);
        for (int t = 0; t < Native.TRACKS; ++t) {
            final int tr = t;
            RectF nr = new RectF(g.left, g.top + t * rh, g.left + nameW, g.top + (t + 1) * rh);
            if (t == selTrack) fill(c, nr, FAINT);
            text(c, info(Native.trackSound(t))[0], nr.left + dp(4), nr.centerY() + dp(5), 13, Native.trackAudible(t) ? INK : DIM);
            Hit nh = hit(nr, (xx, yy) -> { selTrack = tr; Native.preview(tr); });
            nh.longPress = true; nh.hold = (xx, yy) -> { selTrack = tr; chooseSound(tr); };
            textC(c, "" + (t + 1), new RectF(nr.right, nr.top, gx, nr.bottom), 16, INK);
            p.setColor(DIM); c.drawLine(gx, nr.bottom, g.right, nr.bottom, p);
            for (int s = first; s < first + shown; ++s) {
                final int st = s;
                RectF cell = new RectF(gx + (s - first) * cw, nr.top, gx + (s - first + 1) * cw, nr.bottom);
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
        for (int s = 0; s < shown; s += 4) { p.setColor(INK); c.drawRect(gx + s * cw, g.bottom - dp(2), gx + s * cw + dp(8), g.bottom, p); }
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
    private int whiteCount() { return Math.max(8, Math.min(22, (int) (keysRect.width() / dp(portrait ? 38 : 34)))); }
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
        if (page == '5' && e.getPointerCount() == 1 && (penLastX >= 0 || a == MotionEvent.ACTION_DOWN) && penTouch(e)) return true;
        if (grid && page == 'R' && !gridRect.isEmpty() && a == MotionEvent.ACTION_DOWN && (e.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY) != 0) {
            final int ni = noteAt(e.getX(), e.getY()); if (ni >= 0) { removeNote(ni); invalidate(); } return true;   // the pen's button: delete
        }
        if (grid && page == 'R' && !gridRect.isEmpty() && e.getPointerCount() == 1 && (gridActive || (a == MotionEvent.ACTION_DOWN && gridRect.contains(e.getX(), e.getY())))) {
            gridActive = a != MotionEvent.ACTION_UP && a != MotionEvent.ACTION_CANCEL;
            return gridTouch(e, a, e.getX(), e.getY());
        }
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
