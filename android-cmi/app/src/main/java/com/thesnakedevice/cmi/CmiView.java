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
        if (Native.recording()) Native.recPoll();   // what's being recorded, out of the engine's queue
        hits.clear();
        final float W = getWidth(), H = getHeight();
        portrait = H > W;
        final float keyH = portrait ? Math.min(H * 0.2f, dp(150)) : Math.min(H * 0.26f, dp(110));
        c.drawColor(0xff151515);
        fill(c, new RectF(0, H - keyH, W, H), CREAM);
        RectF scr = new RectF(dp(6), dp(4), W - dp(6), H - keyH - dp(4));
        fill(c, scr, BG);
        // tabs
        final char[] pages = { 'R', '2', '7', 'D', '5', '8', 'F', 'M', 'E', 'S' };
        final String[] names = { "PRESETS", "VOICES", "CONTROL", "WAVE", "PEN", "SAMPLE", "FILES", "MIXER", "EFFECTS", "COMPOSER" };
        final String[] shortNames = { "PRE", "VOX", "CTL", "WAV", "PEN", "SMP", "FIL", "MIX", "FX", "SEQ" };
        final float tabX = portrait ? dp(4) : dp(100);
        float tw = (scr.width() - tabX - dp(10)) / 10f;
        if (!portrait) text(c, "PAGE " + page, scr.left + dp(8), scr.top + dp(18), 13, INK);
        for (int i = 0; i < 10; ++i) {
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
            case 'R': presets(c, body); break;
            case 'E': effects(c, body); break;
            case 'K': kitPage(c, body); break;
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

    // ---- Page 2: the voices in your library folder, or the ones loaded ----
    private boolean loadedOnly = false;
    private String[] catLines = new String[0]; private int catFor = -1;
    String[] catalog() {
        final int n = Native.catalogCount();
        if (n != catFor) { catFor = n; catLines = n == 0 ? new String[0] : Native.catalogList().split("\n"); }
        return catLines;
    }
    private void library(Canvas c, RectF b) {
        final int n = Native.soundCount(), cur = Native.getSound();
        final String[] curInfo = Native.isBlankSound(cur) ? new String[0] : info(cur);
        final List<Integer> list = new ArrayList<>();   // sound numbers (LOADED) or catalogue numbers (LIBRARY)
        final String[] cat = catalog();
        if (loadedOnly) { for (int i = 0; i < n; ++i) if (!Native.isBlankSound(i)) list.add(i); }
        else for (int i = 0; i < cat.length; ++i) if (cat[i].split("\t", -1).length >= 3) list.add(i);
        button(c, new RectF(b.left, b.top, b.left + dp(80), b.top + dp(22)), "LIBRARY", !loadedOnly, () -> { loadedOnly = false; libScroll = 0; });
        button(c, new RectF(b.left + dp(86), b.top, b.left + dp(176), b.top + dp(22)), "LOADED", loadedOnly, () -> { loadedOnly = true; libScroll = 0; });
        text(c, list.size() + " VOICES", b.left + dp(186), b.top + dp(16), 11, DIM);
        final RectF lb = new RectF(b.left, b.top + dp(28), b.right, b.bottom - dp(22));
        final float rowH = dp(24);
        final int rows = Math.max(1, (int) (lb.height() / rowH));
        final float colW = dp(170);
        final int cols = Math.max(1, (int) (lb.width() / colW));
        final int perPage = rows * cols;
        libScroll = Math.max(0, Math.min(libScroll, Math.max(0, list.size() - 1) / perPage * perPage));
        if (list.isEmpty()) {
            if (loadedOnly) text(c, "No voices loaded yet: pick one in LIBRARY, or import on Page 8.", lb.left, lb.top + dp(16), 12, DIM);
            else { text(c, "No CMI library yet: set your library folder on FIL", lb.left, lb.top + dp(16), 12, DIM); text(c, "(your disk images and .VC files).", lb.left, lb.top + dp(34), 12, DIM); }
        }
        for (int k = 0; k < perPage && libScroll + k < list.size(); ++k) {
            final int i = list.get(libScroll + k);
            final String name, where;
            if (loadedOnly) { String[] f = info(i); name = f[0]; where = f[2]; }
            else { String[] f = cat[i].split("\t", -1); name = f[0]; where = f[1]; }
            final boolean on = loadedOnly ? i == cur : curInfo.length > 2 && curInfo[0].equals(name) && where.startsWith(curInfo[2]) && !curInfo[2].isEmpty();
            RectF r = new RectF(lb.left + (k / rows) * colW, lb.top + (k % rows) * rowH, lb.left + (k / rows + 1) * colW - dp(6), lb.top + (k % rows + 1) * rowH - dp(2));
            if (on) fill(c, r, INK);
            text(c, String.format("%03d %-8s %s", libScroll + k + 1, name, where.length() > 6 ? where.substring(0, 6) : where), r.left + dp(3), r.bottom - dp(6), 12, on ? BG : INK);
            hit(r, (x, y) -> {
                final int v = loadedOnly ? i : Native.loadCatalogVoice(i);
                if (v >= 0) { Native.loadSound(v); status = "LOADED " + name; act.saveState(); } else status = "USER VOICES FULL";
            });
        }
        if (list.size() > perPage) {
            button(c, new RectF(b.right - dp(90), b.bottom - dp(20), b.right - dp(50), b.bottom), "<", false, () -> libScroll = Math.max(0, libScroll - perPage));
            button(c, new RectF(b.right - dp(44), b.bottom - dp(20), b.right, b.bottom), ">", false, () -> libScroll += perPage);
        }
        if (curInfo.length > 3) text(c, curInfo[0] + ": " + (curInfo[1].length() > 40 ? curInfo[1].substring(0, 40) : curInfo[1]), b.left, b.bottom - dp(4), 11, DIM);
    }

    // ---- Page R: the factory presets (the IIx factory library), each found in your library folder ----
    private int presetScroll = 0;
    private String[] presetNames = null, presetFiles = null;
    private void presets(Canvas c, RectF b) {
        final int n = Native.presetCount();
        if (presetNames == null) { presetNames = new String[n]; presetFiles = new String[n]; for (int i = 0; i < n; ++i) { String[] f = Native.presetInfo(i).split("\t", -1); presetNames[i] = f[0]; presetFiles[i] = f.length > 1 ? f[1] : ""; } }
        final String found = Native.presetFound();
        int have = 0; for (int i = 0; i < found.length(); ++i) if (found.charAt(i) != '0') ++have;
        final int cur = Native.currentPreset();
        text(c, "FACTORY PRESETS " + n + "   FOUND " + have + "   MISSING " + (n - have), b.left, b.top + dp(16), 11, INK);
        button(c, new RectF(b.right - dp(70), b.top, b.right, b.top + dp(22)), "DISK", false, this::diskDialog);
        final RectF lb = new RectF(b.left, b.top + dp(28), b.right, b.bottom - dp(40));
        final float rowH = dp(24);
        final int rows = Math.max(1, (int) (lb.height() / rowH));
        final float colW = dp(170);
        final int cols = Math.max(1, (int) (lb.width() / colW));
        final int perPage = rows * cols;
        presetScroll = Math.max(0, Math.min(presetScroll, Math.max(0, n - 1) / perPage * perPage));
        for (int k = 0; k < perPage && presetScroll + k < n; ++k) {
            final int i = presetScroll + k;
            final char st = i < found.length() ? found.charAt(i) : '0';
            RectF r = new RectF(lb.left + (k / rows) * colW, lb.top + (k % rows) * rowH, lb.left + (k / rows + 1) * colW - dp(6), lb.top + (k % rows + 1) * rowH - dp(2));
            if (i == cur) fill(c, r, INK);
            final String f = presetFiles[i].length() > 8 ? presetFiles[i].substring(0, 8) : presetFiles[i];
            text(c, String.format("%03d%s%-8s %s%s", i + 1, st == '*' ? "*" : " ", presetNames[i], f, st == '0' ? " --" : ""), r.left + dp(3), r.bottom - dp(6), 12, i == cur ? BG : st == '0' ? DIM : INK);
            Hit h = hit(r, (x, y) -> {
                if (Native.loadPreset(i)) status = "PRESET " + (i + 1) + " " + presetNames[i];
                else status = presetNames[i] + " NOT IN YOUR LIBRARY";
            });
            h.longPress = true; h.hold = (x, y) -> presetDialog(i);
        }
        if (n > perPage) {
            button(c, new RectF(b.right - dp(90), b.bottom - dp(36), b.right - dp(50), b.bottom - dp(16)), "<", false, () -> presetScroll = Math.max(0, presetScroll - perPage));
            button(c, new RectF(b.right - dp(44), b.bottom - dp(36), b.right, b.bottom - dp(16)), ">", false, () -> presetScroll += perPage);
            text(c, (presetScroll / perPage + 1) + "/" + ((n + perPage - 1) / perPage), b.right - dp(150), b.bottom - dp(21), 11, INK);
        }
        if (Native.catalogCount() == 0) text(c, "Set your library folder on FIL: presets are found there by file name and disk.", b.left, b.bottom - dp(4), 10, INK);
        else if (cur >= 0) {
            String[] f = Native.presetInfo(cur).split("\t", -1);
            text(c, (cur + 1) + " " + f[0] + " (" + f[2] + ", " + f[1] + ")" + (f[3].isEmpty() ? "" : ": " + f[3]) + "  from " + f[6], b.left, b.bottom - dp(4), 10, DIM);
        } else text(c, "Tap: load onto the keyboard (MIDI program = number). Hold: match to another voice.", b.left, b.bottom - dp(4), 10, DIM);
    }
    private void diskDialog() {
        final List<String> disks = new ArrayList<>(); final List<Integer> first = new ArrayList<>();
        for (int i = 0; i < presetFiles.length; ++i) if (disks.isEmpty() || !disks.get(disks.size() - 1).equals(presetFiles[i])) { disks.add(presetFiles[i]); first.add(i); }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Factory disks")
                .setItems(disks.toArray(new String[0]), (d, w) -> { presetScroll = first.get(w); invalidate(); }).show();
    }
    private void presetDialog(final int n) {
        final String[] items = { "Automatic (its name on its disk)", "Match to a library voice..." };
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle((n + 1) + " " + presetNames[n] + " (" + presetFiles[n] + ")")
                .setItems(items, (d, w) -> {
                    if (w == 0) { Native.choosePresetVoice(n, -1); Native.loadPreset(n); act.saveState(); invalidate(); return; }
                    if (Native.catalogCount() == 0) { status("SET YOUR LIBRARY FOLDER ON FIL"); return; }
                    libraryDialog(presetNames[n], ci -> { Native.choosePresetVoice(n, ci); Native.loadPreset(n); act.saveState(); invalidate(); });
                }).show();
    }

    // ---- Page E: the effects section. A Yamaha REV7 (the Omega REV7 model) on the mixer's aux send ----
    private String[][] fxDefs = null;
    private int fxScroll = 0;
    private void effects(Canvas c, RectF b) {
        final boolean on = Native.fxOn();
        button(c, new RectF(b.left, b.top, b.left + dp(56), b.top + dp(22)), "REV7", fxTab == 0, () -> fxTab = 0);
        button(c, new RectF(b.left + dp(60), b.top, b.left + dp(140), b.top + dp(22)), "FX MIXER", fxTab == 1, () -> fxTab = 1);
        if (fxTab == 1) {
            button(c, new RectF(b.right - dp(60), b.top, b.right, b.top + dp(22)), on ? "ON" : "OFF", on, () -> { Native.setFxOn(!on); act.saveState(); });
            fxMixer(c, new RectF(b.left, b.top + dp(30), b.right, b.bottom)); return;
        }
        effectsUnit(c, new RectF(b.left, b.top + dp(28), b.right, b.bottom));
    }
    private void effectsUnit(Canvas c, RectF b) {
        if (fxDefs == null) { final int n = Native.fxCount(); fxDefs = new String[n][]; for (int i = 0; i < n; ++i) fxDefs[i] = Native.fxParam(i).split("\t", -1); }
        final boolean on = Native.fxOn();
        button(c, new RectF(b.right - dp(232), b.top, b.right - dp(172), b.top + dp(22)), on ? "ON" : "OFF", on, () -> { Native.setFxOn(!on); act.saveState(); });
        button(c, new RectF(b.right - dp(166), b.top, b.right - dp(94), b.top + dp(22)), "PRESET", false, this::fxPresetDialog);
        // the dice: tap a random preset, hold for random settings
        final RectF dr = new RectF(b.right - dp(88), b.top, b.right, b.top + dp(22));
        Hit dh = hit(dr, (xx, yy) -> fxRoll(false)); dh.longPress = true; dh.hold = (xx, yy) -> fxRoll(true);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(dp(1.5f)); p.setColor(INK); c.drawRoundRect(new RectF(dr.left + dp(2), dr.top + dp(1), dr.left + dp(22), dr.bottom - dp(1)), dp(4), dp(4), p); p.setStyle(Paint.Style.FILL);
        final float[][] pp = { {}, {.5f, .5f}, {.25f, .25f, .75f, .75f}, {.25f, .25f, .5f, .5f, .75f, .75f}, {.25f, .25f, .75f, .25f, .25f, .75f, .75f, .75f},
                { .25f, .25f, .75f, .25f, .5f, .5f, .25f, .75f, .75f, .75f }, { .25f, .25f, .25f, .5f, .25f, .75f, .75f, .25f, .75f, .5f, .75f, .75f } };
        for (int k = 0; k + 1 < pp[diceFace].length; k += 2) c.drawCircle(dr.left + dp(2) + dp(20) * pp[diceFace][k], dr.top + dp(1) + dp(20) * pp[diceFace][k + 1], dp(1.8f), p);
        text(c, "DICE", dr.left + dp(28), dr.bottom - dp(7), 10, INK);
        int progIdx = 0; for (int i = 0; i < fxDefs.length; ++i) if (fxDefs[i][0].equals("prog")) progIdx = i;
        final int pi = progIdx, prog = Math.round(Native.fxGet(progIdx));
        final RectF lcd = new RectF(b.left, b.top + dp(28), b.right - dp(96), b.top + dp(64));
        fill(c, lcd, FAINT);
        text(c, String.format("%02d  %s", prog, Native.fxProgName(prog)), lcd.left + dp(6), lcd.top + dp(18), 14, INK);
        final int cp = Native.fxCurrentPreset();
        if (cp >= 0) text(c, Native.fxPresetName(cp), lcd.left + dp(6), lcd.bottom - dp(5), 9, DIM);
        button(c, new RectF(b.right - dp(90), lcd.top + dp(4), b.right - dp(48), lcd.bottom - dp(4)), "<", false, () -> { Native.fxSet(pi, prog > 1 ? prog - 1 : 30); Native.fxNoPreset(); act.saveState(); });
        button(c, new RectF(b.right - dp(42), lcd.top + dp(4), b.right, lcd.bottom - dp(4)), ">", false, () -> { Native.fxSet(pi, prog < 30 ? prog + 1 : 1); Native.fxNoPreset(); act.saveState(); });
        final List<Integer> ids = new ArrayList<>();
        for (int i = 0; i < fxDefs.length; ++i) if (fxDefs[i].length > 7 && fxDefs[i][7].equals("1")) ids.add(i);
        final float rh = dp(26);
        final RectF lb = new RectF(b.left, lcd.bottom + dp(8), b.right, b.bottom - dp(26));
        final int cols = portrait ? 1 : 2, rows = Math.max(1, (int) (lb.height() / rh)), per = rows * cols;
        fxScroll = Math.max(0, Math.min(fxScroll, Math.max(0, ids.size() - per)));
        final float cw = (lb.width() - dp(12) * (cols - 1)) / cols;
        for (int k = 0; k < per && fxScroll + k < ids.size(); ++k) {
            final int i = ids.get(fxScroll + k);
            final String[] d = fxDefs[i];
            final float x = lb.left + (k / rows) * (cw + dp(12)), y = lb.top + (k % rows) * rh;
            final boolean lit = Native.fxRelevant(i);
            String label = d[1].startsWith("Panel: ") ? d[1].substring(7) : d[1];
            text(c, label.toUpperCase(), x, y + dp(16), 10, lit ? INK : DIM);
            final float mn = Float.parseFloat(d[2]), mx = Float.parseFloat(d[3]), v = Native.fxGet(i);
            final RectF tr = new RectF(x + dp(100), y + dp(5), x + cw - dp(64), y + rh - dp(5));
            p.setTextAlign(Paint.Align.RIGHT); p.setTextSize(dp(10)); p.setColor(lit ? INK : DIM);
            c.drawText(Native.fxFormat(i), x + cw, y + dp(16), p); p.setTextAlign(Paint.Align.LEFT);
            if (d[5].equals("1") || !d[6].isEmpty()) {
                final boolean tog = d[5].equals("1");
                final String[] ch = d[6].isEmpty() ? new String[0] : d[6].split("\\|");
                button(c, new RectF(tr.left, tr.top - dp(2), tr.left + dp(56), tr.bottom + dp(2)), tog ? (v > 0.5f ? "ON" : "OFF") : "NEXT", tog && v > 0.5f, () -> {
                    if (tog) Native.fxSet(i, v > 0.5f ? 0 : 1);
                    else { float nv = v + 1; if (nv > mx) nv = mn; Native.fxSet(i, nv); }
                    act.saveState();
                });
                continue;
            }
            fill(c, tr, FAINT);
            fill(c, new RectF(tr.left, tr.top, tr.left + tr.width() * Math.max(0, Math.min(1, (v - mn) / (mx - mn))), tr.bottom), lit ? INK : DIM);
            Act set = (xx, yy) -> Native.fxSet(i, mn + Math.max(0, Math.min(1, (xx - tr.left) / tr.width())) * (mx - mn));
            Hit h = hit(new RectF(tr.left - dp(6), y, tr.right + dp(6), y + rh), set); h.drag = set;
        }
        if (ids.size() > per) {
            button(c, new RectF(b.right - dp(90), b.bottom - dp(22), b.right - dp(50), b.bottom), "<", false, () -> fxScroll = Math.max(0, fxScroll - per));
            button(c, new RectF(b.right - dp(44), b.bottom - dp(22), b.right, b.bottom), ">", false, () -> fxScroll += per);
        }
        text(c, "Sends: FX MIXER tab. Dim: not used. Dice: tap random preset, hold random settings.", b.left, b.bottom - dp(6), 9, DIM);
    }
    private int diceFace = 5;
    private void fxRoll(boolean settings) {
        diceFace = 1 + (int) (Math.random() * 6);
        String w = Native.fxDice(settings); int sl = w.indexOf(" / "); if (sl >= 0) w = w.substring(sl + 3);
        status("DICE: " + w); act.saveState(); invalidate();
    }
    private void fxPresetDialog() {
        final int n = Native.fxPresetCount();
        final java.util.LinkedHashMap<String, List<Integer>> cats = new java.util.LinkedHashMap<>();
        for (int k = 0; k < n; ++k) { String nm = Native.fxPresetName(k); int sl = nm.indexOf(" / "); cats.computeIfAbsent(sl < 0 ? "Factory (ROM 1-30)" : nm.substring(0, sl), q -> new ArrayList<>()).add(k); }
        final String[] cs = cats.keySet().toArray(new String[0]);
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("REV7 presets")
                .setItems(cs, (d1, w) -> {
                    final List<Integer> ks = cats.get(cs[w]); final String[] nm = new String[ks.size()];
                    for (int j = 0; j < ks.size(); ++j) { String s = Native.fxPresetName(ks.get(j)); int sl = s.indexOf(" / "); nm[j] = sl < 0 ? s : s.substring(sl + 3); }
                    new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(cs[w])
                            .setItems(nm, (d2, j) -> { Native.fxLoadPreset(ks.get(j)); status("REV7: " + nm[j]); act.saveState(); invalidate(); }).show();
                }).show();
    }

    // ---- recording: REC (the selected track, after a count-in), the click, the count-in, the quantise, overdub/replace ----
    private boolean recCount = true, recReplace = false; private double recQuant = 0;
    private void recRow(Canvas c, float x, float y, float w, final double from) {
        final boolean rec = Native.recording(), clk = Native.clickOn();
        final float bw = (w - dp(4) * 4) / 5;
        final String q = recQuant <= 0 ? "Q OFF" : recQuant >= 0.5 ? "Q 1/8" : recQuant >= 0.25 ? "Q 1/16" : "Q 1/32";
        final boolean blink = rec && (System.currentTimeMillis() / 400) % 2 == 0;
        final RectF rr = new RectF(x, y, x + bw, y + dp(22));
        if (rec) { fill(c, rr, blink ? Color.WHITE : INK); textC(c, "REC", rr, 11, BG); hit(rr, (xx, yy) -> { status(Native.recStop()); gStale = true; act.saveState(); }); }
        else button(c, rr, "REC", false, () -> { status(Native.recStart(selTrack, from, recCount, recQuant, recReplace)); });
        button(c, new RectF(x + (bw + dp(4)), y, x + 2 * bw + dp(4), y + dp(22)), "CLICK", clk, () -> { Native.setClick(!clk); act.saveState(); });
        button(c, new RectF(x + 2 * (bw + dp(4)), y, x + 3 * bw + 2 * dp(4), y + dp(22)), "COUNT", recCount, () -> recCount = !recCount);
        button(c, new RectF(x + 3 * (bw + dp(4)), y, x + 4 * bw + 3 * dp(4), y + dp(22)), q, recQuant > 0, () -> recQuant = recQuant <= 0 ? 0.5 : recQuant >= 0.5 ? 0.25 : recQuant >= 0.25 ? 0.125 : 0);
        button(c, new RectF(x + 4 * (bw + dp(4)), y, x + 5 * bw + 4 * dp(4), y + dp(22)), recReplace ? "REPLACE" : "OVERDUB", recReplace, () -> recReplace = !recReplace);
    }

    // ---- the DRUM KIT: each drum of the song's kit, its sound, tuning, level, pan, mute, solo, meter ----
    private char kitBack = 'S'; private boolean kitAll = false; private int kitPageN = 0;
    private void kitVoiceDialog(final int note, String name) {
        final int n = Native.soundCount();
        final List<Integer> ids = new ArrayList<>(); final List<String> names = new ArrayList<>();
        ids.add(-1); names.add("Automatic (the CMI drum map)");
        if (Native.catalogCount() > 0) { ids.add(-100); names.add(">> CMI LIBRARY (drum disks...)"); }
        for (int i = 0; i < n; ++i) { if (Native.isBlankSound(i)) continue; String[] f = info(i); ids.add(i); names.add(f[0] + "  (" + f[2] + ")"); }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(note + " " + name)
                .setItems(names.toArray(new String[0]), (d, w) -> {
                    final int id = ids.get(w);
                    if (id == -100) { libraryDialog(name, ci -> { Native.setKitCatalogVoice(note, ci); act.saveState(); invalidate(); }); return; }
                    Native.setKitVoice(note, id); act.saveState(); invalidate();
                }).show();
    }
    private void kitPage(Canvas c, RectF b) {
        if (Native.currentSong() < 0) { text(c, "DRUM KIT: load a song (FIL) or REC on SEQ first", b.left, b.top + dp(14), 11, INK); return; }
        text(c, "DRUM KIT", b.left, b.top + dp(14), 12, INK);
        final float bw = dp(64);
        button(c, new RectF(b.right - 4 * bw - dp(12), b.top, b.right - 3 * bw - dp(12), b.top + dp(22)), kitAll ? "ALL" : "IN SONG", kitAll, () -> { kitAll = !kitAll; kitPageN = 0; });
        button(c, new RectF(b.right - 3 * bw - dp(8), b.top, b.right - 2 * bw - dp(8), b.top + dp(22)), "RESET", false, () -> { Native.resetKit(); act.saveState(); });
        final boolean run = Native.songRunning();
        button(c, new RectF(b.right - 2 * bw - dp(4), b.top, b.right - bw - dp(4), b.top + dp(22)), "PLAY", run, () -> Native.songPlay(!run));
        button(c, new RectF(b.right - bw, b.top, b.right, b.top + dp(22)), "BACK", false, () -> page = kitBack);
        final int[] notes = Native.kitNotes(kitAll);
        if (notes.length == 0) { text(c, "No drum-kit track in this song: set a track's voice to DRUM KIT.", b.left, b.top + dp(44), 10, DIM); return; }
        final int perRow = portrait ? 6 : 12, rowsN = portrait ? 2 : 1, per = perRow * rowsN, pages = (notes.length + per - 1) / per;
        kitPageN = Math.min(kitPageN, pages - 1);
        final float sw = b.width() / perRow, rowH = (b.height() - dp(56)) / rowsN, fh = rowH - dp(150);
        for (int k = 0; k < per && kitPageN * per + k < notes.length; ++k) {
            final int n = notes[kitPageN * per + k];
            final String[] f = Native.kitPiece(n).split("\\t", -1);
            if (f.length < 8) continue;
            final float level = Float.parseFloat(f[1]), pan = Float.parseFloat(f[2]), tune = Float.parseFloat(f[3]);
            final boolean mute = f[4].equals("1"), solo = f[5].equals("1"), chosen = !f[0].isEmpty();
            final float x = b.left + (k % perRow) * sw, top = b.top + dp(30) + (k / perRow) * rowH;
            textC(c, n + " " + f[7], new RectF(x, top, x + sw, top + dp(12)), 7, INK);
            final RectF vb = new RectF(x + dp(2), top + dp(14), x + sw - dp(2), top + dp(32));
            if (chosen) fill(c, vb, INK); else frame(c, vb, FAINT);
            textC(c, f[6].length() > 8 ? f[6].substring(0, 8) : f[6], vb, 8, chosen ? BG : DIM);
            final String gm = f[7]; hit(vb, (xx, yy) -> kitVoiceDialog(n, gm));
            final float km = Math.min(1, Native.kitMeter(n) * 2);
            final RectF pad = new RectF(x + dp(3), top + dp(36), x + sw - dp(3), top + dp(62));
            fill(c, pad, (((int) (64 + 191 * km)) << 24) | (INK & 0xffffff)); textC(c, "PLAY", pad, 9, BG);
            hit(pad, (xx, yy) -> Native.auditionKit(n));
            final float ty = top + dp(66);
            button(c, new RectF(x + dp(1), ty, x + dp(18), ty + dp(18)), "-", false, () -> { Native.setKitPiece(n, level, pan, Math.max(-24, tune - 1), mute, solo); Native.auditionKit(n); act.saveState(); });
            textC(c, (tune > 0 ? "+" : "") + Math.round(tune), new RectF(x + dp(18), ty, x + sw - dp(18), ty + dp(18)), 9, INK);
            button(c, new RectF(x + sw - dp(18), ty, x + sw - dp(1), ty + dp(18)), "+", false, () -> { Native.setKitPiece(n, level, pan, Math.min(24, tune + 1), mute, solo); Native.auditionKit(n); act.saveState(); });
            vfader(c, x, sw, ty + dp(20), Math.max(dp(40), fh), "", 2f, () -> level, v -> { Native.setKitPiece(n, v, pan, tune, mute, solo); act.saveState(); }, km, true);
            final float py = ty + dp(20) + dp(16) + Math.max(dp(40), fh) + dp(18);
            fill(c, new RectF(x + dp(3), py, x + sw - dp(3), py + dp(8)), FAINT);
            fill(c, new RectF(x + dp(3) + (sw - dp(10)) * (pan < 0 ? 0.5f : pan), py - dp(2), x + dp(7) + (sw - dp(10)) * (pan < 0 ? 0.5f : pan), py + dp(10)), pan < 0 ? DIM : INK);
            Act pa = (xx, yy) -> Native.setKitPiece(n, level, Math.max(0, Math.min(1, (xx - x - dp(3)) / (sw - dp(6)))), tune, mute, solo);
            Hit ph = hit(new RectF(x, py - dp(6), x + sw, py + dp(14)), pa); ph.drag = pa; ph.longPress = true; ph.hold = (xx, yy) -> Native.setKitPiece(n, level, -1, tune, mute, solo);
            button(c, new RectF(x + dp(1), py + dp(14), x + sw / 2 - dp(1), py + dp(34)), "M", mute, () -> { Native.setKitPiece(n, level, pan, tune, !mute, solo); act.saveState(); });
            button(c, new RectF(x + sw / 2 + dp(1), py + dp(14), x + sw - dp(1), py + dp(34)), "S", solo, () -> { Native.setKitPiece(n, level, pan, tune, mute, !solo); act.saveState(); });
        }
        if (pages > 1) {
            final float y = b.bottom - dp(22);
            button(c, new RectF(b.left, y, b.left + dp(40), y + dp(22)), "<", false, () -> kitPageN = Math.max(0, kitPageN - 1));
            textC(c, (kitPageN + 1) + "/" + pages, new RectF(b.left + dp(40), y, b.left + dp(100), y + dp(22)), 11, INK);
            button(c, new RectF(b.left + dp(100), y, b.left + dp(140), y + dp(22)), ">", false, () -> kitPageN = Math.min(pages - 1, kitPageN + 1));
        }
    }

    // ---- Page M: the mixer, every strip on one screen (two rows on a phone held upright) ----
    interface FGet { float get(); }
    interface FSet { void set(float v); }
    private void mixList(List<Integer> strips, List<String> names) {
        strips.add(0); names.add("KEYS");
        if (Native.currentSong() >= 0) {
            for (int t = 0; t < Native.SONG_TRK; ++t) {
                String[] f = Native.songTrack(t).split("\\t", -1);
                if (f.length > 3 && f[2].equals("1")) { final int v = Integer.parseInt(f[1]); strips.add(9 + t); names.add((t + 1) + " " + (v == Native.GM_KIT ? "KIT" : Native.isBlankSound(v) ? f[0] : info(v)[0])); }
            }
        } else for (int t = 0; t < Native.TRACKS; ++t) { strips.add(1 + t); final int sd = Native.trackSound(t); names.add(Native.isBlankSound(sd) ? "" + (t + 1) : info(sd)[0]); }
    }
    private void vfader(Canvas c, float x, float sw, float top, float fh, String nm, final float max, FGet get, FSet set, float meter, boolean db) {
        textC(c, nm.length() > Math.max(3, (int) (sw / dp(5.2f))) ? nm.substring(0, Math.max(3, (int) (sw / dp(5.2f)))) : nm, new RectF(x, top, x + sw, top + dp(12)), 8, INK);
        final RectF slot = new RectF(x + sw / 2 - dp(3), top + dp(16), x + sw / 2 + dp(3), top + dp(16) + fh);
        fill(c, slot, FAINT);
        final float mv = Math.min(1, meter);
        fill(c, new RectF(x + dp(3), slot.bottom - fh * mv, x + dp(6), slot.bottom), mv > 0.95f ? Color.WHITE : DIM);
        final float v = get.get(), y = slot.bottom - fh * v / max;
        fill(c, new RectF(x + dp(7), y - dp(6), x + sw - dp(7), y + dp(6)), INK);
        textC(c, db ? (v <= 0.001f ? "-INF" : String.format("%.1f", 20 * Math.log10(v))) : Math.round(v / max * 100) + "%", new RectF(x, slot.bottom + dp(2), x + sw, slot.bottom + dp(13)), 7, DIM);
        Act a = (xx, yy) -> set.set(Math.max(0, Math.min(max, (slot.bottom - yy) / fh * max)));
        Hit h = hit(new RectF(x, top + dp(12), x + sw, slot.bottom + dp(6)), a); h.drag = a;
    }
    private void mixer(Canvas c, RectF b) {
        final List<Integer> strips = new ArrayList<>(); final List<String> names = new ArrayList<>();
        mixList(strips, names);
        text(c, "MIXER " + (Native.currentSong() >= 0 ? "SONG" : "PAGE S"), b.left, b.top + dp(12), 12, INK);
        button(c, new RectF(b.right - dp(110), b.top, b.right, b.top + dp(22)), "FX MIXER", false, () -> { page = 'E'; fxTab = 1; });
        if (Native.currentSong() >= 0) button(c, new RectF(b.right - dp(200), b.top, b.right - dp(116), b.top + dp(22)), "DRUM KIT", false, () -> { kitBack = 'M'; page = 'K'; });
        final int count = strips.size() + 1, rowsN = portrait && count > 9 ? 2 : 1, per = (count + rowsN - 1) / rowsN;
        final float sw = b.width() / per, rowH = (b.height() - dp(28)) / rowsN, fh = rowH - dp(80);
        for (int k = 0; k < count; ++k) {
            final boolean master = k == count - 1;
            final int idx = master ? -1 : strips.get(k), row = k / per, col = k % per;
            final float x = b.left + col * sw, top = b.top + dp(28) + row * rowH;
            if (master) { vfader(c, x, sw, top, fh, "MASTER", 1.5f, () -> Native.getParam(Native.MASTER), v -> Native.setParam(Native.MASTER, v), Native.masterMeter(), true); continue; }
            vfader(c, x, sw, top, fh, names.get(k), 1.5f, () -> Native.mixGet(idx, 0), v -> Native.mixSet(idx, 0, v), Native.mixGet(idx, 4), true);
            final float py = top + dp(16) + fh + dp(18);
            final float po = Native.mixGet(idx, 1);
            fill(c, new RectF(x + dp(3), py, x + sw - dp(3), py + dp(8)), FAINT);
            fill(c, new RectF(x + dp(3) + (sw - dp(10)) * (po < 0 ? 0.5f : po), py - dp(2), x + dp(7) + (sw - dp(10)) * (po < 0 ? 0.5f : po), py + dp(10)), po < 0 ? DIM : INK);
            Act pan = (xx, yy) -> Native.mixSet(idx, 1, Math.max(0, Math.min(1, (xx - x - dp(3)) / (sw - dp(6)))));
            Hit ph = hit(new RectF(x, py - dp(6), x + sw, py + dp(14)), pan); ph.drag = pan; ph.longPress = true; ph.hold = (xx, yy) -> Native.mixSet(idx, 1, -1);
            final boolean mu = Native.mixGet(idx, 2) > 0.5f, so = Native.mixGet(idx, 3) > 0.5f;
            button(c, new RectF(x + dp(1), py + dp(16), x + sw / 2 - dp(1), py + dp(36)), "M", mu, () -> Native.mixSet(idx, 2, mu ? 0 : 1));
            button(c, new RectF(x + sw / 2 + dp(1), py + dp(16), x + sw - dp(1), py + dp(36)), "S", so, () -> Native.mixSet(idx, 3, so ? 0 : 1));
        }
    }
    // ---- Page E, FX MIXER: each strip's send to the REV7, and its return ----
    private int fxTab = 0;
    private void fxMixer(Canvas c, RectF b) {
        final List<Integer> strips = new ArrayList<>(); final List<String> names = new ArrayList<>();
        mixList(strips, names);
        final int count = strips.size() + 1, rowsN = portrait && count > 9 ? 2 : 1, per = (count + rowsN - 1) / rowsN;
        final float sw = b.width() / per, rowH = (b.height() - dp(30)) / rowsN, fh = rowH - dp(40);
        for (int k = 0; k < count; ++k) {
            final boolean ret = k == count - 1;
            final int idx = ret ? Native.FX_STRIP : strips.get(k), row = k / per, col = k % per;
            final float x = b.left + col * sw, top = b.top + row * rowH;
            if (ret) vfader(c, x, sw, top, fh, Native.fxOn() ? "REV7" : "OFF", 1.5f, () -> Native.mixGet(idx, 0), v -> Native.mixSet(idx, 0, v), Native.mixGet(idx, 4), true);
            else vfader(c, x, sw, top, fh, names.get(k), 1f, () -> Native.mixGet(idx, 5), v -> Native.mixSet(idx, 5, v), Native.mixGet(idx, 4) * Native.mixGet(idx, 5), false);
        }
        final float by = b.bottom - dp(24), bw = (b.width() - dp(12)) / 3;
        final String[] l = { "SENDS 0", "SENDS 15%", "SENDS 30%" }; final float[] vs = { 0, 0.15f, 0.3f };
        for (int i = 0; i < 3; ++i) { final float v = vs[i]; button(c, new RectF(b.left + i * (bw + dp(6)), by, b.left + i * (bw + dp(6)) + bw, by + dp(22)), l[i], false, () -> { for (int st = 0; st < Native.STRIPS; ++st) Native.mixSet(st, 5, v); act.saveState(); }); }
    }

    // ---- Page F: the library folder ----
    private int filePage = 0, songFilePage = 0; private boolean songTab = true;
    // the song folder: tap a song to load it (or go to it), hold it to load and play
    // the song browser: search, order by title or artist, A-Z, two-line rows, the song loaded at the bottom
    private String songSearch = ""; private boolean songByArtist = false;
    private static String[] artistTitle(String nm) {
        String n = nm.replace('_', ' ').trim();
        if (n.length() > 7 && n.substring(0, 4).matches("\\d{4}") && n.startsWith(" - ", 4)) n = n.substring(7);
        final int d = n.indexOf(" - ");
        return d > 0 ? new String[] { n.substring(0, d).trim(), n.substring(d + 3).trim() } : new String[] { "", n };
    }
    private void songSearchDialog() {
        final android.widget.EditText ed = new android.widget.EditText(getContext()); ed.setText(songSearch); ed.setSingleLine(true);
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Search songs").setView(ed)
                .setPositiveButton("Search", (d, w) -> { songSearch = ed.getText().toString().trim(); songFilePage = 0; invalidate(); })
                .setNeutralButton("All", (d, w) -> { songSearch = ""; songFilePage = 0; invalidate(); }).show();
    }
    private void songFolder(Canvas c, RectF b) {
        final String sf = act.songFolderName();
        text(c, "SONGS: " + (sf.isEmpty() ? "(none set)" : sf), b.left, b.top + dp(12), 10, DIM);
        final float y0 = b.top + dp(18), bw4 = (b.width() - dp(18)) / 4;
        final String[] tl = { "FOLDER", "RESCAN", songSearch.isEmpty() ? "SEARCH" : "\"" + (songSearch.length() > 6 ? songSearch.substring(0, 6) : songSearch) + "\"", songByArtist ? "BY ARTIST" : "BY TITLE" };
        final Runnable[] tf = { act::chooseSongFolder, act::rescanSongs, this::songSearchDialog, () -> { songByArtist = !songByArtist; songFilePage = 0; } };
        for (int i = 0; i < 4; ++i) button(c, new RectF(b.left + i * (bw4 + dp(6)), y0, b.left + i * (bw4 + dp(6)) + bw4, y0 + dp(24)), tl[i], i == 2 && !songSearch.isEmpty(), tf[i]);
        // the list: filtered, ordered
        final List<Integer> ix = new ArrayList<>(); final List<String[]> at = new ArrayList<>();
        for (int i = 0; i < act.songNames.size(); ++i) at.add(artistTitle(MainActivity.baseName(act.songNames.get(i))));
        final String q = songSearch.toLowerCase();
        for (int i = 0; i < act.songNames.size(); ++i) if (q.isEmpty() || act.songNames.get(i).toLowerCase().contains(q)) ix.add(i);
        java.util.Collections.sort(ix, (x, y) -> sortKey(at.get(x)).compareToIgnoreCase(sortKey(at.get(y))));
        // A-Z
        final float ly = y0 + dp(30); final String letters = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ"; final float lw = b.width() / letters.length();
        final float top = ly + dp(24), rh = dp(34);
        final int rows = Math.max(1, (int) ((b.bottom - dp(56) - top) / rh)), nf = ix.size(), pages = Math.max(1, (nf + rows - 1) / rows);
        for (int k = 0; k < letters.length(); ++k) {
            final char ch = letters.charAt(k); int first = -1;
            for (int j = 0; j < nf && first < 0; ++j) { final String key = sortKey(at.get(ix.get(j))); final char c0 = key.isEmpty() ? '#' : Character.toUpperCase(key.charAt(0)); if (ch == '#' ? !Character.isLetter(c0) : c0 == ch) first = j; }
            final RectF r = new RectF(b.left + k * lw, ly, b.left + (k + 1) * lw, ly + dp(20));
            textC(c, "" + ch, r, 9, first >= 0 ? INK : 0xff0e3a1e);
            final int f = first; if (f >= 0) hit(r, (xx, yy) -> songFilePage = f / rows);
        }
        songFilePage = Math.min(songFilePage, pages - 1);
        if (nf == 0) text(c, sf.isEmpty() ? "Pick the folder with your MIDI songs (searched inside too)." : !songSearch.isEmpty() ? "Nothing matches." : "No MIDI files there.", b.left, top + dp(14), 11, DIM);
        final int cur = Native.currentSong(); final boolean running = Native.songRunning();
        for (int r = 0; r < rows && songFilePage * rows + r < nf; ++r) {
            final int i = ix.get(songFilePage * rows + r);
            final String nm = MainActivity.baseName(act.songNames.get(i)); final String[] a2 = at.get(i);
            final int li = Native.songLoadedAs(nm);
            final RectF row = new RectF(b.left, top + r * rh, b.right, top + (r + 1) * rh - dp(3));
            final boolean on = li >= 0 && li == cur;
            if (on) fill(c, row, INK);
            text(c, (on && running ? "> " : li >= 0 ? "* " : "  ") + (a2[1].length() > 34 ? a2[1].substring(0, 34) : a2[1]), row.left + dp(2), row.top + dp(14), 12, on ? BG : INK);
            text(c, "   " + a2[0], row.left + dp(2), row.bottom - dp(4), 9, on ? BG : DIM);
            Hit h = hit(row, (xx, yy) -> act.openSong(i, false));
            h.longPress = true; h.hold = (xx, yy) -> act.openSong(i, true);
        }
        final float y = b.bottom - dp(50);
        if (pages > 1) {
            button(c, new RectF(b.left, y, b.left + dp(40), y + dp(22)), "<", false, () -> songFilePage = Math.max(0, songFilePage - 1));
            textC(c, (songFilePage + 1) + "/" + pages, new RectF(b.left + dp(40), y, b.left + dp(100), y + dp(22)), 11, INK);
            button(c, new RectF(b.left + dp(100), y, b.left + dp(140), y + dp(22)), ">", false, () -> songFilePage = Math.min(pages - 1, songFilePage + 1));
        }
        text(c, nf + " SONGS   tap: load   hold: play", b.left + dp(148), y + dp(15), 9, DIM);
        // the song loaded
        final float ny = b.bottom - dp(24);
        if (cur >= 0) {
            final String[] si = Native.songInfo().split("\\t", -1);
            final String nmS = si.length > 0 ? artistTitle(si[0])[1] : "";
            text(c, (nmS.length() > 18 ? nmS.substring(0, 18) : nmS) + (si.length > 1 ? "  " + si[1] + " BPM" : ""), b.left, ny + dp(15), 10, INK);
            button(c, new RectF(b.right - dp(166), ny, b.right - dp(112), ny + dp(22)), running ? "STOP" : "PLAY", running, () -> Native.songPlay(!running));
            button(c, new RectF(b.right - dp(106), ny, b.right, ny + dp(22)), "TO SEQUENCER", false, () -> { page = 'S'; grid = false; });
        }
    }
    private String sortKey(String[] at) { return songByArtist ? at[0] + " " + at[1] : at[1] + " " + at[0]; }

    private void files(Canvas c, RectF b) {
        final float tw = (b.width() - dp(6)) / 2;
        button(c, new RectF(b.left, b.top, b.left + tw, b.top + dp(22)), "SONGS", songTab, () -> songTab = true);
        button(c, new RectF(b.left + tw + dp(6), b.top, b.right, b.top + dp(22)), "CMI LIBRARY", !songTab, () -> songTab = false);
        b = new RectF(b.left, b.top + dp(28), b.right, b.bottom);
        if (songTab) { songFolder(c, b); return; }
        final String lib = act.libraryName();
        text(c, "LIBRARY: " + (lib.isEmpty() ? "(none set)" : lib) + "   " + Native.catalogCount() + " CMI VOICES", b.left, b.top + dp(12), 11, INK);
        final float y0 = b.top + dp(20), bw = (b.width() - dp(12)) / 3;
        final boolean only = act.hfeOnly();
        final String[] lbl = { "SET FOLDER", "RESCAN", "DEDUPE", "ALL SONGS", "ALL SOUNDS", only ? "HFE FIRST" : "ANY FORMAT" };
        final Runnable[] fn = { act::chooseLibrary, act::rescanLibrary, () -> { status("" + Native.removeDuplicateSongs() + " DUPLICATES REMOVED"); act.saveState(); },
                () -> act.loadFromLibrary(true, false, -1), () -> act.loadFromLibrary(false, true, -1), () -> act.setHfeOnly(!only) };
        for (int i = 0; i < 6; ++i) { float rx = b.left + (i % 3) * (bw + dp(6)), ry = y0 + (i / 3) * dp(26); button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], i == 5 && only, fn[i]); }
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
    // ---- Page 5: the light pen (core/Pen.h). Draw the start and the end; LIVE: each stroke is the PEN voice, which the
    // keyboard and MIDI in play. The S Pen draws, its button (or eraser end) flattens.
    private final RectF[] penBoxes = { new RectF(), new RectF() };
    private int penBox = 0, penLastX = -1; private float penLastV = 0; private boolean penStylus = false; private long penLiveAt = 0;
    private void pen(Canvas c, RectF b) {
        final float[] cyc = Native.penGet();
        final boolean live = Native.penIsLive();
        text(c, "LIGHT PEN: start and end; the voice morphs", b.left, b.top + dp(14), 11, INK);
        button(c, new RectF(b.right - dp(84), b.top, b.right, b.top + dp(22)), live ? "LIVE ON" : "LIVE OFF", live, () -> { Native.setPenLive(!live); if (!live) Native.penLive(); });
        final float bh = (b.height() - dp(176)) / 2;
        for (int k = 0; k < 2; ++k) {
            final RectF r = new RectF(b.left, b.top + dp(28) + k * (bh + dp(8)), b.right, b.top + dp(28) + k * (bh + dp(8)) + bh);
            penBoxes[k] = r;
            frame(c, r, k == penBox ? INK : DIM);
            p.setColor(FAINT); c.drawLine(r.left, r.centerY(), r.right, r.centerY(), p);
            text(c, k == 0 ? "START" : "END", r.left + dp(4), r.top + dp(12), 9, DIM);
            Path path = new Path();
            for (int i = 0; i < 128; ++i) { float x = r.left + r.width() * i / 127f, y = r.centerY() - cyc[k * 128 + i] * r.height() * 0.48f; if (i == 0) path.moveTo(x, y); else path.lineTo(x, y); }
            Paint lp = new Paint(Paint.ANTI_ALIAS_FLAG); lp.setStyle(Paint.Style.STROKE); lp.setStrokeWidth(dp(2)); lp.setColor(INK); c.drawPath(path, lp);
        }
        final float y = b.bottom - dp(140), bw = (b.width() - dp(18)) / 4;
        final String[] lbl = { "SINE", "SAW", "SQUARE", "TRIANGLE", "PULSE", "ORGAN", "NOISE", "CLEAR", "SMOOTH", "FULL", "INVERT", "REVERSE" };
        final int[] ops = { 0, 1, 2, 3, 6, 7, 4, 5, 10, 11, 12, 13 };
        for (int i = 0; i < lbl.length; ++i) {
            final int op = ops[i];
            final float rx = b.left + (i % 4) * (bw + dp(6)), ry = y + (i / 4) * dp(26);
            button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], false, () -> { Native.penOp(penBox, op); Native.penLive(); act.saveState(); });
        }
        final float y2 = y + dp(78), bw2 = (b.width() - dp(12)) / 3;
        final String[] l2 = { penBox == 0 ? "START>END" : "END>START", "FROM KEYS", "SAVE VOICE", "EXPORT WAV" };
        final Runnable[] f2 = { () -> { Native.penOp(penBox, 14); Native.penLive(); }, () -> { if (Native.penFromKeyboard()) { Native.penLive(); status("THE KEYBOARD'S VOICE IS ON THE PEN"); } else status("NO VOICE ON THE KEYBOARD"); },
                () -> { status(Native.penSaveAs()); act.saveState(); }, () -> act.export(3) };
        final float bw3 = (b.width() - dp(18)) / 4;
        for (int i = 0; i < 4; ++i) { final float rx = b.left + i * (bw3 + dp(6)); button(c, new RectF(rx, y2, rx + bw3, y2 + dp(22)), l2[i], false, f2[i]); }
        final int ps = Native.penSlotNow();
        text(c, ps >= 0 ? "PEN = voice " + (ps + 1) + ": the keys and MIDI play it" : "draw: the keys and MIDI play it", b.left, y2 + dp(40), 9, DIM);
    }
    private boolean penTouch(MotionEvent e) {
        final int a = e.getActionMasked();
        final boolean stylus = e.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS || e.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER;
        if (stylus) penStylus = true;
        if (penStylus && !stylus) return false;   // the S Pen is in use: a resting palm doesn't draw
        int k = -1; for (int i = 0; i < 2; ++i) if (penBoxes[i].contains(e.getX(), e.getY())) k = i;
        if (a == MotionEvent.ACTION_DOWN) { if (k < 0) return false; penBox = k; penLastX = -1; }
        if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) { penLastX = -1; Native.penLive(); act.saveState(); invalidate(); return true; }
        final RectF r = penBoxes[penBox];
        final boolean erase = e.getToolType(0) == MotionEvent.TOOL_TYPE_ERASER || (e.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY) != 0;
        for (int hI = 0; hI <= e.getHistorySize(); ++hI) {
            final float x = hI < e.getHistorySize() ? e.getHistoricalX(hI) : e.getX(), y = hI < e.getHistorySize() ? e.getHistoricalY(hI) : e.getY();
            final int px = Math.max(0, Math.min(127, Math.round((x - r.left) / r.width() * 127)));
            final float pv = erase ? 0 : Math.max(-1, Math.min(1, (r.centerY() - y) / (r.height() * 0.48f)));
            Native.penLine(penBox, penLastX < 0 ? px : penLastX, penLastX < 0 ? pv : penLastV, px, pv);
            penLastX = px; penLastV = pv;
        }
        final long now = System.currentTimeMillis();
        if (now - penLiveAt > 120) { penLiveAt = now; Native.penLive(); }   // hear it while drawing
        invalidate();
        return true;
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
        final int rows = portrait ? 25 : 14;
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
                { Native.BLENDON, "LOOP BLEND", 0f, 1f, true, true, null },
                { Native.BLEND, "BLEND AMOUNT", 0f, 1f, false, false, (Fmt) v -> Math.round(v * 100) + "%" },
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
        items.add((cur < 0 ? "> " : "   ") + "Page S patterns");
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
        final int n = Native.soundCount();
        final List<Integer> ids = new ArrayList<>(); final List<String> names = new ArrayList<>();
        final boolean lib = Native.catalogCount() > 0;
        if (lib) { ids.add(-100); names.add(">> CMI LIBRARY (your disks)"); }
        ids.add(Native.GM_KIT); names.add("DRUM KIT  (a voice per GM drum note)");
        for (int i = 0; i < n; ++i) { if (Native.isBlankSound(i)) continue; String[] f = info(i); ids.add(i); names.add(f[0] + "  (" + f[2] + ")  " + f[1]); }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Track " + (track + 1) + " voice")
                .setItems(names.toArray(new String[0]), (dlg, w) -> {
                    if (ids.get(w) == -100) { libraryDialog("Track " + (track + 1), ci -> { Native.useCatalogVoice(track, ci); act.saveState(); invalidate(); }); return; }
                    Native.setSongVoice(track, ids.get(w)); if (ids.get(w) == Native.GM_KIT) Native.rememberKit(track); act.saveState(); invalidate(); })
                .show();
    }
    // the CMI library by disk, then the disk's voices
    interface Pick { void on(int catIndex); }
    private void libraryDialog(final String title, final Pick pick) {
        final String[] lines = catalog();
        final java.util.TreeMap<String, List<Integer>> byDisk = new java.util.TreeMap<>();
        for (int i = 0; i < lines.length; ++i) { String[] f = lines[i].split("\t", -1); if (f.length < 3) continue; byDisk.computeIfAbsent(f[1], k -> new ArrayList<>()).add(i); }
        final String[] disks = byDisk.keySet().toArray(new String[0]);
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(title + ": CMI library")
                .setItems(disks, (d1, w) -> {
                    final List<Integer> vs = byDisk.get(disks[w]);
                    final String[] nm = new String[vs.size()];
                    for (int k = 0; k < vs.size(); ++k) { String[] f = lines[vs.get(k)].split("\t", -1); nm[k] = f[0] + (f[2].equals("1") ? "  (loop)" : ""); }
                    new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(disks[w])
                            .setItems(nm, (d2, k) -> pick.on(vs.get(k))).show();
                }).show();
    }

    private int[] notesCache = new int[0]; private String notesFor = "";
    // the editing grid (one track as a piano roll): keys down the side (tap to hear), a velocity lane underneath.
    // Tools: DRAW (tap adds, drag a note moves the selection, its end lengthens, drag empty space scrolls, hold deletes),
    // SEL (drag a box, tap toggles), ERASE (tap or drag over notes). Two fingers: scroll, pinch to zoom.
    private boolean grid = false, gridActive = false, gFollow = true;
    private double vStart = 0, vBeats = 16, snapB = 0.25, newLenB = 0.5; private int pLow = 48, gridTrack = -1, gRows = 20, gTool = 0;
    private int[] gNotes = new int[0]; private boolean[] gSel = new boolean[0]; private boolean gStale = true; private String gridFor = "";
    private RectF gridRect = new RectF(), velRect = new RectF(), gKeysRect = new RectF();
    private int gIdx = -1, gMode = -1; private float gDownX, gDownY, gCurX, gCurY; private boolean gMoved, gLong; private double gStartV; private int gStartP; private int[] gFrom = null;
    private final Runnable gLongPress = () -> { if (!gMoved && gIdx >= 0 && gTool == 0) { selectOnly(gIdx); gridOp(1, 0); gIdx = -1; gLong = true; invalidate(); } };
    // two fingers
    private boolean gTwo = false; private float g2Span, g2MidX, g2MidY; private double g2Start, g2Beats; private int g2Low;

    private int nNotes() { return gNotes.length / 5; }
    private void fitSel() { if (gSel.length != nNotes()) { boolean[] s = new boolean[nNotes()]; System.arraycopy(gSel, 0, s, 0, Math.min(gSel.length, s.length)); gSel = s; } }
    private void selectOnly(int i) { fitSel(); java.util.Arrays.fill(gSel, false); if (i >= 0 && i < gSel.length) gSel[i] = true; }
    private int selCount() { int n = 0; for (boolean b : gSel) if (b) ++n; return n; }
    private void commitNotes() { Native.setSongNoteList(gNotes); notesFor = ""; act.saveState(); }
    // an edit through GridEdit (native): ops as in native.cpp gridOp
    private void gridOp(int op, float a) { gridOp(op, a, 0, 0, 0, false); }
    private void gridOp(int op, float a, float b, float c, float d, boolean add) {
        fitSel();
        final int[] r = Native.gridOp(gNotes, gSel, selTrack, op, a, b, c, d, add);
        final int n = r.length / 6; int[] nn = new int[n * 5]; boolean[] ss = new boolean[n];
        for (int i = 0; i < n; ++i) { System.arraycopy(r, i * 6, nn, i * 5, 5); ss[i] = r[i * 6 + 5] != 0; }
        gNotes = nn; gSel = ss;
        if (op != 8 && op != 10 && op != 11) commitNotes();
    }
    private void removeNote(int i) { selectOnly(i); gridOp(1, 0); }

    private float gx() { return gridRect.left; }
    private float rowH() { return gridRect.height() / gRows; }
    private double beatAt(float x) { return vStart + (x - gx()) / (gridRect.right - gx()) * vBeats; }
    private int pitchAt(float y) { return pLow + (int) Math.floor((gridRect.bottom - y) / rowH()); }
    private float xOfBeat(double b) { return gx() + (float) ((b - vStart) / vBeats) * (gridRect.right - gx()); }
    private double snapIt(double b) { return snapB > 0 ? Math.round(b / snapB) * snapB : b; }
    private int noteAt(float x, float y) {
        final double b = beatAt(x); final int p = pitchAt(y); int best = -1;
        final double minLen = Math.max(snapB, 0.125) * 0.5 + vBeats * dp(6) / Math.max(1, gridRect.width());   // a finger's width
        for (int i = 0; i + 4 < gNotes.length; i += 5) {
            if (gNotes[i] != selTrack || gNotes[i + 3] != p) continue;
            final double st = gNotes[i + 1] / 480.0, ln = Math.max(gNotes[i + 2] / 480.0, minLen);
            if (b >= st - minLen * 0.3 && b <= st + ln) best = i / 5;
        }
        return best;
    }
    private void setVelAt(float x, float y) {
        final int v = Math.max(1, Math.min(127, Math.round((velRect.bottom - y) / velRect.height() * 127)));
        if (gIdx < 0) { float best = dp(12); for (int i = 0; i + 4 < gNotes.length; i += 5) { if (gNotes[i] != selTrack) continue; float d = Math.abs(xOfBeat(gNotes[i + 1] / 480.0) - x); if (d < best) { best = d; gIdx = i / 5; } } }
        if (gIdx < 0) return;
        fitSel();
        if (gSel[gIdx]) { for (int i = 0; i < gSel.length; ++i) if (gSel[i]) gNotes[i * 5 + 4] = v; } else gNotes[gIdx * 5 + 4] = v;
    }
    private boolean gridTouch(MotionEvent e, int a, float x, float y) {
        fitSel();
        if (a == MotionEvent.ACTION_DOWN) {
            gDownX = gCurX = x; gDownY = gCurY = y; gMoved = false; gLong = false; gStartV = vStart; gStartP = pLow; gIdx = -1; gFrom = gNotes.clone();
            if (gKeysRect.contains(x, y)) { gMode = -1; Native.auditionSong(selTrack, pitchAt(y)); return true; }
            if (velRect.contains(x, y)) { gMode = 6; setVelAt(x, y); invalidate(); return true; }
            gIdx = noteAt(x, y);
            if (gTool == 2) { gMode = 4; if (gIdx >= 0) { removeNote(gIdx); gIdx = -1; } invalidate(); return true; }
            if (gIdx >= 0) {
                if (gTool == 1) gSel[gIdx] = !gSel[gIdx]; else if (!gSel[gIdx]) selectOnly(gIdx);
                gFrom = gNotes.clone();
                final double st = gNotes[gIdx * 5 + 1] / 480.0, end = (gNotes[gIdx * 5 + 1] + gNotes[gIdx * 5 + 2]) / 480.0;
                gMode = x > xOfBeat(end) - Math.max(dp(10), (xOfBeat(end) - xOfBeat(st)) * 0.3f) ? 1 : 0;
                newLenB = gNotes[gIdx * 5 + 2] / 480.0;
                Native.auditionSong(selTrack, gNotes[gIdx * 5 + 3]);
                postDelayed(gLongPress, 550);
            } else gMode = gTool == 1 ? 3 : 2;
            invalidate(); return true;
        }
        if (a == MotionEvent.ACTION_MOVE) {
            gCurX = x; gCurY = y;
            if (!gMoved && Math.hypot(x - gDownX, y - gDownY) > dp(8)) { gMoved = true; removeCallbacks(gLongPress); }
            if (gMode == 6) { setVelAt(x, y); invalidate(); return true; }
            if (!gMoved || gLong) return true;
            if (gMode == 2) {
                vStart = Math.max(0, gStartV - (x - gDownX) / gridRect.width() * vBeats); gFollow = false;
                pLow = Math.max(0, Math.min(127 - gRows, gStartP + Math.round((y - gDownY) / rowH())));
            } else if (gMode == 3) {
                gridOp(11, (float) beatAt(gDownX), (float) beatAt(x), pitchAt(gDownY), pitchAt(y), false);
            } else if (gMode == 4) {
                final int i = noteAt(x, y); if (i >= 0) removeNote(i);
            } else if ((gMode == 0 || gMode == 1) && gFrom != null && gFrom.length == gNotes.length) {
                final double db = snapIt(beatAt(x) - beatAt(gDownX)); final int dpch = pitchAt(y) - pitchAt(gDownY);
                for (int i = 0; i < gSel.length; ++i) {
                    if (!gSel[i]) continue;
                    if (gMode == 0) { gNotes[i * 5 + 1] = (int) Math.max(0, Math.round((gFrom[i * 5 + 1] / 480.0 + db) * 480)); gNotes[i * 5 + 3] = Math.max(0, Math.min(127, gFrom[i * 5 + 3] + dpch)); }
                    else gNotes[i * 5 + 2] = (int) Math.round(Math.max(snapB > 0 ? snapB : 1.0 / 32, gFrom[i * 5 + 2] / 480.0 + db) * 480);
                }
                if (gIdx >= 0) newLenB = gNotes[gIdx * 5 + 2] / 480.0;
            }
            invalidate(); return true;
        }
        if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
            removeCallbacks(gLongPress);
            final int mode = gMode; gMode = -1;
            if (gLong || a == MotionEvent.ACTION_CANCEL) { gIdx = -1; invalidate(); return true; }
            if (mode == 6 || ((mode == 0 || mode == 1) && gMoved)) commitNotes();
            else if (mode == 2 && !gMoved) {   // a tap on an empty place: a new note
                final double st = Math.max(0, snapB > 0 ? Math.floor(beatAt(x) / snapB) * snapB : beatAt(x));
                final int pt = Math.max(0, Math.min(127, pitchAt(y)));
                int[] n = new int[gNotes.length + 5]; System.arraycopy(gNotes, 0, n, 0, gNotes.length);
                n[gNotes.length] = selTrack; n[gNotes.length + 1] = (int) Math.round(st * 480); n[gNotes.length + 2] = (int) Math.round(Math.max(1.0 / 32, newLenB) * 480);
                n[gNotes.length + 3] = pt; n[gNotes.length + 4] = 100;
                gNotes = n; fitSel(); selectOnly(nNotes() - 1); commitNotes(); Native.auditionSong(selTrack, pt);
            } else if (mode == 3 && !gMoved) { selectOnly(-1); }
            gIdx = -1; invalidate(); return true;
        }
        return true;
    }
    // two fingers on the grid: scroll both ways, pinch to zoom (time)
    private boolean gridTwo(MotionEvent e, int a) {
        if (e.getPointerCount() < 2) { gTwo = false; return true; }
        final float x0 = e.getX(0), x1 = e.getX(1), mx = (x0 + x1) / 2, my = (e.getY(0) + e.getY(1)) / 2, span = Math.max(dp(20), Math.abs(x1 - x0));
        if (!gTwo || a == MotionEvent.ACTION_POINTER_DOWN) {
            removeCallbacks(gLongPress); if (gFrom != null && gFrom.length == gNotes.length && gMoved) gNotes = gFrom.clone();
            gMode = -1; gIdx = -1; gTwo = true; g2Span = span; g2MidX = mx; g2MidY = my; g2Start = vStart; g2Beats = vBeats; g2Low = pLow; return true;
        }
        final double beats = Math.max(2, Math.min(256, g2Beats * g2Span / span));
        final double anchor = g2Start + (g2MidX - gx()) / gridRect.width() * g2Beats;   // the beat under the fingers stays there
        vBeats = beats; vStart = Math.max(0, anchor - (mx - gx()) / gridRect.width() * beats); gFollow = false;
        pLow = Math.max(0, Math.min(127 - gRows, g2Low + Math.round((my - g2MidY) / rowH())));
        invalidate(); return true;
    }

    private void drawGrid(Canvas c, RectF r, double beat, double barBeats, String key) {
        final float keyW = dp(30), velH = Math.min(dp(40), r.height() * 0.15f);
        gKeysRect = new RectF(r.left, r.top, r.left + keyW, r.bottom - velH - dp(4));
        gridRect = new RectF(r.left + keyW, r.top, r.right, r.bottom - velH - dp(4));
        velRect = new RectF(r.left + keyW, r.bottom - velH, r.right, r.bottom);
        if (gStale || !key.equals(gridFor)) { gNotes = Native.songNoteList(); gSel = new boolean[nNotes()]; gStale = false; gridFor = key; }
        fitSel();
        if (selTrack != gridTrack) {
            gridTrack = selTrack; selectOnly(-1);
            int sum = 0, cnt = 0; for (int i = 0; i + 4 < gNotes.length; i += 5) if (gNotes[i] == selTrack) { sum += gNotes[i + 3]; ++cnt; }
            if (cnt > 0) pLow = Math.max(0, Math.min(127 - gRows, sum / cnt - gRows / 2));
        }
        pLow = Math.max(0, Math.min(127 - gRows, pLow));
        if (gFollow && Native.songRunning() && gMode < 0 && !gTwo && (beat < vStart || beat > vStart + vBeats)) vStart = Math.floor(beat / 4) * 4;
        final RectF gr = gridRect; final float rh = rowH(), gx = gx();
        for (int row = 0; row < gRows; ++row) {
            final int pt = pLow + row, k = pt % 12; final float y = gr.bottom - (row + 1) * rh;
            final boolean black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
            fill(c, new RectF(gx, y, gr.right, y + rh), black ? 0xff031208 : BG);
            fill(c, new RectF(r.left, y, r.left + (black ? keyW * 0.62f : keyW - dp(2)), y + rh - 1), black ? 0xff0d2a1c : 0xd9cfe9d6);
            if (k == 0) text(c, noteName(pt), r.left + dp(2), y + rh - dp(2), Math.min(9, rh / d * 0.8f), BG);
        }
        final double stp = snapB > 0 ? snapB : 0.25;
        for (double b = Math.ceil(vStart / stp) * stp; b <= vStart + vBeats; b += stp) {
            final boolean bar = Math.abs(b / barBeats - Math.rint(b / barBeats)) < 1e-6, bt = Math.abs(b - Math.rint(b)) < 1e-6;
            if (!bt && vBeats > 32) continue;
            p.setColor(bar ? DIM : bt ? FAINT : 0xff08200f); c.drawRect(xOfBeat(b), gr.top, xOfBeat(b) + (bar ? dp(1.2f) : dp(0.6f)), gr.bottom, p);
            if (bar) text(c, "" + (1 + Math.round(b / barBeats)), xOfBeat(b) + dp(2), gr.top + dp(9), 8, DIM);
        }
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i + 4 < gNotes.length; i += 5) {
                if ((gNotes[i] == selTrack) != (pass == 1)) continue;
                final double st = gNotes[i + 1] / 480.0, ln = gNotes[i + 2] / 480.0; final int pt = gNotes[i + 3];
                if (st + ln < vStart || st > vStart + vBeats || pt < pLow || pt >= pLow + gRows) continue;
                final float y = gr.bottom - (pt - pLow + 1) * rh, x0 = Math.max(gx, xOfBeat(st)), x1 = Math.min(gr.right, xOfBeat(st + ln));
                if (pass == 0) { fill(c, new RectF(x0, y + rh * 0.35f, Math.max(x0 + 1, x1), y + rh * 0.65f), FAINT); continue; }
                final boolean on = i / 5 < gSel.length && gSel[i / 5];
                final int alpha = Math.min(255, 128 + gNotes[i + 4]);
                fill(c, new RectF(x0, y + 1, Math.max(x0 + dp(3), x1 - 1), y + rh - 1), (alpha << 24) | ((on ? Color.WHITE : INK) & 0xffffff));
                frame(c, new RectF(x0, y + 1, Math.max(x0 + dp(3), x1 - 1), y + rh - 1), on ? Color.WHITE : BG);
            }
        if (gMode == 3 && gMoved) { p.setColor(0x30ffffff); RectF br = new RectF(Math.min(gDownX, gCurX), Math.min(gDownY, gCurY), Math.max(gDownX, gCurX), Math.max(gDownY, gCurY)); c.drawRect(br, p); frame(c, br, Color.WHITE); }
        if (beat >= vStart && beat <= vStart + vBeats) { p.setColor(Color.WHITE); c.drawRect(xOfBeat(beat), gr.top, xOfBeat(beat) + dp(1.5f), gr.bottom, p); }
        frame(c, gr, INK);
        // the velocity lane
        fill(c, velRect, 0xff041a0c); frame(c, velRect, FAINT);
        text(c, "VEL", r.left, velRect.centerY() + dp(3), 8, DIM);
        for (int i = 0; i + 4 < gNotes.length; i += 5) {
            if (gNotes[i] != selTrack) continue;
            final double st = gNotes[i + 1] / 480.0; if (st < vStart || st > vStart + vBeats) continue;
            final float x = xOfBeat(st), hgt = (velRect.height() - dp(3)) * gNotes[i + 4] / 127f;
            fill(c, new RectF(x, velRect.bottom - dp(1) - hgt, x + dp(2.5f), velRect.bottom - dp(1)), i / 5 < gSel.length && gSel[i / 5] ? Color.WHITE : INK);
        }
    }

    // the grid: track chips across the top, the roll, tools and edits underneath
    private void gridFooter(Canvas c, RectF b, double beat) {
        final String[] si = Native.songInfo().split("\\t", -1);
        final double barBeats = si.length > 3 ? Integer.parseInt(si[2]) * 4.0 / Integer.parseInt(si[3]) : 4;
        final float t0 = b.top + dp(52);
        // track chips, two rows of eight
        final float cw = b.width() / 8f;
        for (int t = 0; t < Native.SONG_TRK; ++t) {
            final int tk = t;
            final String[] f = Native.songTrack(t).split("\\t", -1);
            if (f.length < 4) continue;
            final boolean used = f[2].equals("1"); final int v = Integer.parseInt(f[1]);
            final String nm = !used ? "--" : v == Native.GM_KIT ? "KIT" : info(v)[0];
            RectF r = new RectF(b.left + (t % 8) * cw, t0 + (t / 8) * dp(22), b.left + (t % 8 + 1) * cw - dp(2), t0 + (t / 8) * dp(22) + dp(20));
            if (t == selTrack) fill(c, r, INK); else frame(c, r, used ? FAINT : 0xff0a2a14);
            textC(c, (t + 1) + " " + (nm.length() > 5 ? nm.substring(0, 5) : nm), r, 8, t == selTrack ? BG : used ? INK : DIM);
            Hit h = hit(r, (xx, yy) -> selTrack = tk); h.longPress = true; h.hold = (xx, yy) -> { selTrack = tk; songVoiceDialog(tk); };
        }
        final float footH = dp(5 * 26), by = b.bottom - footH;
        recRow(c, b.left, by + 4 * dp(26), b.width(), Math.max(0, Math.floor(vStart / barBeats) * barBeats));
        drawGrid(c, new RectF(b.left, t0 + dp(48), b.right, by - dp(6)), beat, barBeats, (si.length > 0 ? si[0] : "") + "g");
        final String sn = snapB <= 0 ? "OFF" : snapB >= 4 ? "BAR" : snapB >= 1 ? "1/4" : snapB >= 0.5 ? "1/8" : snapB >= 0.25 ? "1/16" : "1/32";
        final int ns = selCount();
        final String[] lbl = { "DRAW", "SEL", "ERASE", "SNAP " + sn, "ZOOM-", "ZOOM+", "ROWS " + gRows,
                "UNDO", "ALL", "DEL", "COPY", "PASTE", "DUP", "QUANT",
                "-12", "-1", "+1", "+12", "LEGATO", "VEL-", "VEL+",
                "^", "v", "FOLLOW", "FROM HERE", "TRACKS" };
        final Runnable[] fns = {
                () -> gTool = 0, () -> gTool = 1, () -> gTool = 2,
                () -> snapB = snapB <= 0 ? 4 : snapB >= 4 ? 1 : snapB >= 1 ? 0.5 : snapB >= 0.5 ? 0.25 : snapB >= 0.25 ? 0.125 : 0,
                () -> vBeats = Math.min(256, vBeats * 1.5), () -> vBeats = Math.max(2, vBeats / 1.5),
                () -> gRows = gRows == 14 ? 20 : gRows == 20 ? 28 : 14,
                () -> { if (Native.undoSongEdit()) { gStale = true; status("UNDONE"); act.saveState(); } },
                () -> gridOp(10, 0), () -> { if (selCount() == 0) gridOp(10, 0); gridOp(1, 0); status("DELETED"); },
                () -> { gridOp(8, 0); status(Native.clipboardSize() + " NOTES COPIED"); },
                () -> { final double at = beat >= vStart && beat <= vStart + vBeats ? beat : vStart; gridOp(9, (float) Math.max(0, snapIt(at))); status("PASTED"); },
                () -> { gridOp(7, (float) snapB); status("DUPLICATED"); }, () -> { gridOp(4, (float) (snapB > 0 ? snapB : 0.25)); status("QUANTIZED"); },
                () -> gridOp(2, -12), () -> gridOp(2, -1), () -> gridOp(2, 1), () -> gridOp(2, 12), () -> gridOp(6, 0), () -> gridOp(5, -10), () -> gridOp(5, 10),
                () -> pLow = Math.min(127 - gRows, pLow + 12), () -> pLow = Math.max(0, pLow - 12), () -> gFollow = !gFollow,
                () -> Native.songPlayFrom(vStart), () -> grid = false };
        final int per = 7; final float bw = (b.width() - dp(4) * (per - 1)) / per;
        for (int i = 0; i < lbl.length; ++i) {
            final float rx = b.left + (i % per) * (bw + dp(4)), ry = by + (i / per) * dp(26);
            final boolean on = (i < 3 && gTool == i) || (i == 23 && gFollow);
            button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], on, fns[i]);
        }
        text(c, ns > 0 ? ns + " SELECTED" : "NONE SELECTED: EDITS ON THE TRACK", b.left + 5 * (bw + dp(4)), by + 3 * dp(26) + dp(15), 8, DIM);
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
        final float footH = grid ? dp(5 * 26) : dp(3 * 26), by = b.bottom - footH;
        final String[] tr = Native.songTrack(selTrack).split("\\t", -1);
        final boolean muted = tr.length > 3 && tr[3].equals("1");
        final String[] lbl; final Runnable[] fns;
        if (!grid) {
            final boolean lib = Native.catalogCount() > 0;
            final boolean mono = Native.songMono(selTrack);
            lbl = new String[] { muted ? "MUTED" : "AUDIBLE", "VOICE", lib ? "CMI LIBRARY" : "AUTO MATCH", mono ? "MONO" : "POLY", "EDIT GRID", "DRUM KIT", "PATTERNS" };
            fns = new Runnable[] { () -> { Native.setSongMute(selTrack, !muted); act.saveState(); }, () -> songVoiceDialog(selTrack),
                    () -> { if (lib) status(Native.connectToLibrary() + " TRACKS ON CMI LIBRARY VOICES"); else { Native.gmVoices(); status("SET A LIBRARY FOLDER (FIL) FOR THE ORIGINAL VOICES"); } act.saveState(); },
                    () -> { Native.setSongMono(selTrack, !mono); status(mono ? "POLY: CHORDS" : "MONO: ONE NOTE AT A TIME"); act.saveState(); },
                    () -> grid = true, () -> { kitBack = 'S'; page = 'K'; }, () -> Native.selectSongAt(-1) };
            recRow(c, b.left, by + 2 * dp(26), b.width(), 0);
        } else { gridFooter(c, b, beat); return; }
        final float bw = (b.width() - dp(18)) / 4;
        for (int i = 0; i < lbl.length; ++i) { float rx = b.left + (i % 4) * (bw + dp(6)), ry = by + (i / 4) * dp(26); button(c, new RectF(rx, ry, rx + bw, ry + dp(22)), lbl[i], (i == 0 && !grid && !muted) || (i == 3 && lbl[3].equals("MONO")), fns[i]); }

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
        final String[] items = song ? new String[] { "Song as a WAV file", "Stems: a WAV file per track (pick a folder)", "Song as a MIDI file" } : new String[] { "Page S song as a WAV file" };
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
        final List<Integer> ids = new ArrayList<>(); final List<String> names = new ArrayList<>();
        if (Native.catalogCount() > 0) { ids.add(-100); names.add(">> CMI LIBRARY (your disks)"); }
        else { ids.add(-101); names.add("(set your library folder on FIL for the CMI voices)"); }
        for (int i = 0; i < n; ++i) { if (Native.isBlankSound(i)) continue; String[] f = info(i); ids.add(i); names.add(f[0] + "  (" + f[2] + ")  " + f[1]); }
        new AlertDialog.Builder(getContext(), android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Track " + (track + 1) + " voice")
                .setItems(names.toArray(new String[0]), (dlg, w) -> {
                    final int id = ids.get(w);
                    if (id == -101) return;
                    if (id == -100) { libraryDialog("Track " + (track + 1), ci -> { final int v = Native.loadCatalogVoice(ci); if (v >= 0) { Native.setTrackSound(track, v); Native.preview(track); act.saveState(); } else status("USER VOICES FULL"); invalidate(); }); return; }
                    Native.setTrackSound(track, id); Native.preview(track); act.saveState(); })
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
        if (grid && page == 'S' && !gridRect.isEmpty() && a == MotionEvent.ACTION_DOWN && (e.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY) != 0) {
            final int ni = noteAt(e.getX(), e.getY()); if (ni >= 0) { removeNote(ni); invalidate(); } return true;   // the pen's button: delete
        }
        if (grid && page == 'S' && !gridRect.isEmpty() && (gTwo || (e.getPointerCount() == 2 && gridActive))) {
            if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) { gTwo = false; gridActive = false; return true; }
            if (a == MotionEvent.ACTION_POINTER_UP) { gTwo = false; return true; }
            return gridTwo(e, a);
        }
        if (grid && page == 'S' && !gridRect.isEmpty() && e.getPointerCount() == 1 && (gridActive || (a == MotionEvent.ACTION_DOWN && (gridRect.contains(e.getX(), e.getY()) || velRect.contains(e.getX(), e.getY()) || gKeysRect.contains(e.getX(), e.getY()))))) {
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
