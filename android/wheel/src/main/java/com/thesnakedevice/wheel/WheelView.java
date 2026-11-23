package com.thesnakedevice.wheel;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.Choreographer;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * The performance screen: two stacked scrolling waveforms with headers and overviews, two platters with transport and
 * pitch, the scratch mixer in the middle, pads / loops / FX under each deck. One View handles every finger itself, so
 * any number of controls work at once (a hand on the platter and the other on the crossfader is the scratch move).
 * Redrawn every display frame (Choreographer); every frame polls the engine's live state.
 */
@SuppressLint("ViewConstructor")
final class WheelView extends View implements Choreographer.FrameCallback {

    interface Host {
        void openLibrary();
        /** Loads the library's selected track on the deck; false when nothing is selected. */
        boolean loadSelected(int deck);
        String selectedTitle();
        String midiStatus();
        TrackLoader loader();
        /** SETUP's BLUETOOTH MIDI: find and connect a Bluetooth LE MIDI controller (asks for permission first). */
        void bluetoothMidi();
    }

    private final Ctl ctl;
    private final Host host;
    private final float dp;
    private float ts = 1;   // text scale (px per "point")

    // ---- live state, refreshed every frame
    final double[][] st = new double[2][Native.ST_COUNT];
    final double[][] info = new double[2][7];
    final double[][] cueT = new double[2][8];
    final int[][] cueRgb = new int[2][8];
    final float[] meters = new float[4];
    final float[] meterHold = new float[4];
    final String[][] text = { { "", "", "" }, { "", "", "" } };
    final long[] serial = { Long.MIN_VALUE, Long.MIN_VALUE };
    final WaveData[] wave = { new WaveData(), new WaveData() };
    boolean engineOk = true;

    // ---- paints
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint txt = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mono = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bmp = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF tmp = new RectF();
    private final Path path = new Path();

    private final List<Ctrl> ctrls = new ArrayList<>();
    private final SparseArray<Ctrl> owner = new SparseArray<>();
    private boolean running;

    // regions for drawing panels
    private final RectF mixRect = new RectF();
    private final RectF[] deckMid = { new RectF(), new RectF() }, deckBot = { new RectF(), new RectF() };
    private final RectF[] meterR = { new RectF(), new RectF(), new RectF(), new RectF() };
    private final RectF statusR = new RectF();
    private float autoZoom = 8;
    /** Seconds across a waveform: the pinched zoom, or the default for this screen width. */
    float zoomSec() { float z = ctl.waveZoom; return z > 0 ? z : autoZoom; }

    WheelView(Context c, Ctl ctl, Host host) {
        super(c);
        this.ctl = ctl;
        this.host = host;
        dp = c.getResources().getDisplayMetrics().density;
        txt.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        mono.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        stroke.setStyle(Paint.Style.STROKE);
        setHapticFeedbackEnabled(true);
        build();
    }

    // =================================================================================================== frame loop
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); resume(); }
    @Override protected void onDetachedFromWindow() { pause(); super.onDetachedFromWindow(); }

    void resume() { if (!running) { running = true; Choreographer.getInstance().postFrameCallback(this); } }
    void pause() { running = false; Choreographer.getInstance().removeFrameCallback(this); }

    @Override public void doFrame(long frameTimeNanos) {
        if (!running) return;
        invalidate();
        Choreographer.getInstance().postFrameCallback(this);
    }

    private void poll() {
        try {
            for (int d = 0; d < 2; ++d) {
                Native.status(d, st[d]);
                Native.trackInfo(d, info[d]);
                Native.cues(d, cueT[d], cueRgb[d]);
                long s = (long) info[d][6];
                if (s != serial[d]) {
                    serial[d] = s;
                    if (info[d][5] > .5) {
                        wave[d].set(Native.wave(d, 0), Native.wave(d, 1), Native.wave(d, 2), Native.wave(d, 3));
                        String[] t = Native.trackText(d);
                        text[d] = t != null && t.length >= 2 ? t : new String[] { "", "", "" };
                    } else {
                        wave[d].clear();
                        text[d] = new String[] { "", "", "" };
                    }
                }
            }
            Native.meters(meters);
            engineOk = true;
        } catch (Throwable t) {
            engineOk = false;
        }
        for (int i = 0; i < 4; ++i) meterHold[i] = Math.max(meters[i], meterHold[i] * .93f);
        ctl.tick();
        for (Ctrl c : ctrls) c.tick();
    }

    boolean hasTrack(int d) { return info[d][5] > .5; }
    boolean playing(int d) { return st[d][Native.ST_PLAYING] > .5; }

    // =================================================================================================== controls
    abstract class Ctrl {
        final RectF r = new RectF();
        int pointer = -1;
        /** Takes a finger; false = let it fall through. */
        boolean down(int id, float x, float y) { if (pointer >= 0) return false; pointer = id; return true; }
        void move(MotionEvent e, int idx) {}
        void up(boolean cancel) { pointer = -1; }
        boolean hit(float x, float y) { return r.contains(x, y); }
        void tick() {}
        abstract void draw(Canvas c);
        boolean held() { return pointer >= 0; }
    }

    interface Press { void on(boolean down); }
    interface Lit { boolean get(); }
    interface Txt { String get(); }

    class Btn extends Ctrl {
        Txt label; Lit lit; int color; Press press; boolean haptic = true; float textMax = 13;
        Btn(String l, int color, Lit lit, Press p) { this(() -> l, color, lit, p); }
        Btn(Txt l, int color, Lit lit, Press p) { label = l; this.color = color; this.lit = lit; press = p; }
        @Override boolean down(int id, float x, float y) {
            if (!super.down(id, x, y)) return false;
            if (haptic) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            if (press != null) press.on(true);
            return true;
        }
        @Override void up(boolean cancel) { super.up(cancel); if (press != null) press.on(false); }
        @Override void draw(Canvas c) {
            boolean on = lit != null && lit.get();
            drawButton(c, r, label.get(), color, on, held(), textMax);
        }
    }

    class Knob extends Ctrl {
        final int id; final Txt label; final int color; final boolean bipolar; final float def;
        final Conv conv; float lastY; long lastDown;
        Knob(int id, String label, int color, boolean bipolar, float def, Conv conv) { this(id, () -> label, color, bipolar, def, conv); }
        Knob(int id, Txt label, int color, boolean bipolar, float def, Conv conv) {
            this.id = id; this.label = label; this.color = color; this.bipolar = bipolar; this.def = def; this.conv = conv;
        }
        float norm() { return conv.toNorm(ctl.get(id)); }
        @Override boolean down(int pid, float x, float y) {
            if (!super.down(pid, x, y)) return false;
            lastY = y;
            long now = SystemClock.uptimeMillis();
            if (now - lastDown < 300) { ctl.set(id, conv.fromNorm(def)); performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); }
            lastDown = now;
            return true;
        }
        @Override void move(MotionEvent e, int idx) {
            float y = e.getY(idx);
            float dn = (lastY - y) / (160 * dp);
            lastY = y;
            float n = Math.max(0, Math.min(1, norm() + dn));
            if (bipolar && Math.abs(n - .5f) < .012f) n = .5f;
            ctl.set(id, conv.fromNorm(n));
        }
        @Override void draw(Canvas c) {
            float lh = Math.min(11 * ts, r.height() * .26f);
            float size = Math.min(r.width(), r.height() - lh) * .86f;
            float cx = r.centerX(), cy = r.top + (r.height() - lh) / 2f, rad = size / 2f;
            float n = norm();
            fill.setColor(held() ? Pal.EDGE_HI : Pal.RAISED);
            c.drawCircle(cx, cy, rad, fill);
            stroke.setStrokeWidth(Math.max(2 * dp, rad * .16f));
            stroke.setStrokeCap(Paint.Cap.ROUND);
            tmp.set(cx - rad * .78f, cy - rad * .78f, cx + rad * .78f, cy + rad * .78f);
            stroke.setColor(Pal.WELL);
            c.drawArc(tmp, 135, 270, false, stroke);
            float killed = (id % 32 >= Native.D_HI && id % 32 <= Native.D_LOW && id < 64 && n <= .001f) ? 1 : 0;
            stroke.setColor(killed > 0 ? Pal.RED : color);
            if (bipolar) { float a0 = 270, sweep = (n - .5f) * 270; c.drawArc(tmp, sweep < 0 ? a0 + sweep : a0, Math.abs(sweep), false, stroke); }
            else c.drawArc(tmp, 135, n * 270, false, stroke);
            double ang = Math.toRadians(135 + n * 270);
            stroke.setColor(Pal.TEXT);
            stroke.setStrokeWidth(Math.max(2 * dp, rad * .12f));
            c.drawLine(cx + (float) Math.cos(ang) * rad * .2f, cy + (float) Math.sin(ang) * rad * .2f,
                    cx + (float) Math.cos(ang) * rad * .62f, cy + (float) Math.sin(ang) * rad * .62f, stroke);
            stroke.setStrokeCap(Paint.Cap.BUTT);
            label(c, killed > 0 ? "KILL" : label.get(), cx, r.bottom - lh * .2f, lh * .92f, killed > 0 ? Pal.RED : Pal.DIM, Paint.Align.CENTER, r.width());
        }
    }

    interface Conv { float toNorm(float v); float fromNorm(float n); }
    static final Conv LIN01 = new Conv() { public float toNorm(float v) { return v; } public float fromNorm(float n) { return n; } };
    static final Conv BI = new Conv() { public float toNorm(float v) { return (v + 1) / 2; } public float fromNorm(float n) { return n * 2 - 1; } };
    static final Conv EQ = new Conv() { public float toNorm(float v) { return Ctl.eqNorm(v); } public float fromNorm(float n) { return Ctl.eqDb(n); } };
    static final Conv TRIM = new Conv() { public float toNorm(float v) { return Ctl.trimNorm(v); } public float fromNorm(float n) { return Ctl.trimDb(n); } };
    static final Conv MASTER = new Conv() { public float toNorm(float v) { return Ctl.masterNorm(v); } public float fromNorm(float n) { return Ctl.masterDb(n); } };

    /** A vertical fader: channel fader (0 bottom .. 1 top) or pitch (-1 top .. +1 bottom, like a Technics: down = faster). */
    class VFader extends Ctrl {
        final int id; final boolean pitch; final int color; float grabOff; long lastDown;
        VFader(int id, boolean pitch, int color) { this.id = id; this.pitch = pitch; this.color = color; }
        float pos() { float v = ctl.get(id); return pitch ? (v + 1) / 2 : 1 - v; }   // 0 top .. 1 bottom
        float trackTop() { return r.top + capH() / 2; }
        float trackLen() { return Math.max(1, r.height() - capH()); }
        float capH() { return Math.min(r.height() * .2f, Math.max(18 * dp, r.width() * .55f)); }
        @Override boolean down(int pid, float x, float y) {
            if (!super.down(pid, x, y)) return false;
            float capY = trackTop() + pos() * trackLen();
            grabOff = Math.abs(y - capY) < capH() ? y - capY : 0;
            long now = SystemClock.uptimeMillis();
            if (now - lastDown < 300) { ctl.set(id, pitch ? 0f : 1f); grabOff = 0; lastDown = 0; pointer = -1; return true; }
            lastDown = now;
            if (grabOff == 0) setFromY(y);
            return true;
        }
        void setFromY(float y) {
            float p = Math.max(0, Math.min(1, (y - grabOff - trackTop()) / trackLen()));
            if (pitch) { float v = p * 2 - 1; if (Math.abs(v) < .025f) v = 0; ctl.set(id, v); }   // centre detent
            else ctl.set(id, 1 - p);
        }
        @Override void move(MotionEvent e, int idx) { setFromY(e.getY(idx)); }
        @Override void draw(Canvas c) {
            float cx = r.centerX();
            fill.setColor(Pal.WELL);
            tmp.set(cx - 3 * dp, trackTop(), cx + 3 * dp, trackTop() + trackLen());
            c.drawRoundRect(tmp, 3 * dp, 3 * dp, fill);
            // scale ticks
            stroke.setStrokeWidth(dp);
            for (int i = 0; i <= 10; ++i) {
                float y = trackTop() + trackLen() * i / 10f;
                stroke.setColor(pitch && i == 5 ? Pal.TEXT : Pal.FAINT);
                float w = (pitch && i == 5) ? r.width() * .45f : r.width() * .28f;
                c.drawLine(cx - w, y, cx - 6 * dp, y, stroke);
                c.drawLine(cx + 6 * dp, y, cx + w, y, stroke);
            }
            float capY = trackTop() + pos() * trackLen(), ch = capH(), cw = Math.min(r.width(), 64 * dp);
            tmp.set(cx - cw / 2, capY - ch / 2, cx + cw / 2, capY + ch / 2);
            fill.setColor(held() ? Pal.mix(Pal.RAISED, color, .35f) : 0xff3b4047);
            c.drawRoundRect(tmp, 4 * dp, 4 * dp, fill);
            fill.setColor(pitch && ctl.get(id) == 0 ? Pal.GREEN : color);
            c.drawRect(tmp.left + 4 * dp, capY - 1.5f * dp, tmp.right - 4 * dp, capY + 1.5f * dp, fill);
        }
    }

    /** The crossfader: the cap jumps to the finger (cut-in scratching). */
    class XFader extends Ctrl {
        float capW() { return Math.min(r.width() * .14f, Math.max(28 * dp, r.height() * .7f)); }
        float left() { return r.left + capW() / 2; }
        float len() { return Math.max(1, r.width() - capW()); }
        @Override boolean down(int id, float x, float y) { if (!super.down(id, x, y)) return false; set(x); return true; }
        @Override void move(MotionEvent e, int idx) {
            set(e.getX(idx));
        }
        void set(float x) {
            float p = Math.max(0, Math.min(1, (x - left()) / len()));
            ctl.set(Ctl.gid(Native.G_XFADER), p * 2 - 1);
        }
        @Override void draw(Canvas c) {
            float cy = r.centerY();
            fill.setColor(Pal.WELL);
            tmp.set(r.left, cy - 4 * dp, r.right, cy + 4 * dp);
            c.drawRoundRect(tmp, 4 * dp, 4 * dp, fill);
            stroke.setStrokeWidth(dp);
            for (int i = 0; i <= 8; ++i) {
                float x = left() + len() * i / 8f;
                stroke.setColor(i == 4 ? Pal.TEXT : Pal.FAINT);
                c.drawLine(x, r.top + 2 * dp, x, r.top + r.height() * .22f, stroke);
                c.drawLine(x, r.bottom - r.height() * .22f, x, r.bottom - 2 * dp, stroke);
            }
            float p = (ctl.get(Ctl.gid(Native.G_XFADER)) + 1) / 2;
            float x = left() + p * len(), cw = capW();
            tmp.set(x - cw / 2, r.top + r.height() * .12f, x + cw / 2, r.bottom - r.height() * .12f);
            fill.setColor(held() ? 0xff5a616b : 0xff40464f);
            c.drawRoundRect(tmp, 5 * dp, 5 * dp, fill);
            fill.setColor(Pal.WHITE);
            c.drawRect(x - 1.5f * dp, tmp.top + 4 * dp, x + 1.5f * dp, tmp.bottom - 4 * dp, fill);
            // hamster: the ends swap
            boolean ham = ctl.get(Ctl.gid(Native.G_HAMSTER)) > .5f;
            label(c, ham ? "B" : "A", r.left + 8 * dp, cy + 5 * ts, 13 * ts, Pal.DECK[ham ? 1 : 0], Paint.Align.CENTER, 0);
            label(c, ham ? "A" : "B", r.right - 8 * dp, cy + 5 * ts, 13 * ts, Pal.DECK[ham ? 0 : 1], Paint.Align.CENTER, 0);
            if (ham) label(c, "HAMSTER", r.centerX(), r.top + r.height() * .2f, 9 * ts, Pal.AMBER, Paint.Align.CENTER, 0);
        }
    }

    /** A platter: each finger on it scratches that deck (angle around the centre; event history for smoothness). */
    class Platter extends Ctrl {
        final int deck; float cx, cy, rad; double lastAng;
        Platter(int d) { deck = d; }
        @Override boolean hit(float x, float y) { float dx = x - cx, dy = y - cy; return dx * dx + dy * dy <= rad * rad * 1.04f; }
        @Override boolean down(int id, float x, float y) {
            if (!super.down(id, x, y)) return false;
            lastAng = Math.atan2(y - cy, x - cx);
            ctl.touch(deck, Ctl.HAND_PLATTER, true);
            return true;
        }
        @Override void move(MotionEvent e, int idx) {
            double sum = 0;
            int hs = e.getHistorySize();
            for (int h = 0; h <= hs; ++h) {
                float x = h < hs ? e.getHistoricalX(idx, h) : e.getX(idx);
                float y = h < hs ? e.getHistoricalY(idx, h) : e.getY(idx);
                float dx = x - cx, dy = y - cy;
                double a = Math.atan2(dy, dx);
                if (dx * dx + dy * dy > rad * rad * .02f) {   // too near the spindle the angle means nothing
                    double d = a - lastAng;
                    while (d > Math.PI) d -= 2 * Math.PI;
                    while (d < -Math.PI) d += 2 * Math.PI;
                    sum += d;
                }
                lastAng = a;
            }
            if (sum != 0) ctl.move(deck, sum / (2 * Math.PI) * Ctl.REV_SECONDS);   // clockwise = forward
        }
        @Override void up(boolean cancel) { super.up(cancel); ctl.touch(deck, Ctl.HAND_PLATTER, false); }
        @Override void draw(Canvas c) { drawPlatter(c, this); }
    }

    /**
     * The scrolling waveform: drag sideways to scratch. The hand goes on the record once the finger moves or after
     * {@link #HOLD_MS}, so a second finger landing first makes it a pinch (zoom) without stopping the record.
     */
    class Wave extends Ctrl {
        static final long HOLD_MS = 90;
        final int deck; float lastX, startX; long downAt; boolean engaged;
        Wave(int d) { deck = d; }
        float pxPerSec() { return r.width() / zoomSec(); }
        @Override boolean down(int id, float x, float y) {
            if (!super.down(id, x, y)) return false;   // taken even with no track, so a pinch can start here
            lastX = startX = x;
            downAt = SystemClock.uptimeMillis();
            engaged = false;
            return true;
        }
        void engage() { if (!engaged && held() && hasTrack(deck)) { engaged = true; ctl.touch(deck, Ctl.HAND_WAVE, true); } }
        @Override void move(MotionEvent e, int idx) {
            float x = e.getX(idx);
            if (!engaged) { if (Math.abs(x - startX) < 4 * dp) return; engage(); if (!engaged) return; }
            float dx = x - lastX;
            lastX = x;
            if (dx != 0) ctl.move(deck, -dx / pxPerSec());
        }
        @Override void tick() { if (held() && !engaged && SystemClock.uptimeMillis() - downAt > HOLD_MS) engage(); }
        @Override void up(boolean cancel) {
            super.up(cancel);
            if (engaged) ctl.touch(deck, Ctl.HAND_WAVE, false);
            engaged = false;
        }
        @Override void draw(Canvas c) { drawWave(c, this); }
    }

    /** The whole-track strip: touch to seek. */
    class Overview extends Ctrl {
        final int deck;
        Overview(int d) { deck = d; }
        @Override boolean down(int id, float x, float y) { if (!hasTrack(deck) || !super.down(id, x, y)) return false; seek(x); return true; }
        @Override void move(MotionEvent e, int idx) { seek(e.getX(idx)); }
        void seek(float x) {
            double len = info[deck][4];
            if (len <= 0) return;
            double t = Math.max(0, Math.min(1, (x - r.left) / r.width())) * len;
            Ctl.cmd(Native.C_SEEK, deck, 0, t, true);
        }
        @Override void draw(Canvas c) { drawOverview(c, this); }
    }

    /** The deck header: tap = MIDI focus on this deck; with a track picked in the library and the deck stopped, loads it. */
    class Header extends Ctrl {
        final int deck;
        Header(int d) { deck = d; }
        @Override boolean down(int id, float x, float y) {
            if (!super.down(id, x, y)) return false;
            ctl.focus = deck;
            if (!playing(deck) && host.loadSelected(deck)) performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            return true;
        }
        @Override void draw(Canvas c) { drawHeader(c, this); }
    }

    class Pad extends Ctrl {
        final int deck, slot; long downAt; boolean cleared;
        Pad(int d, int s) { deck = d; slot = s; }
        @Override boolean down(int id, float x, float y) {
            if (!super.down(id, x, y)) return false;
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            downAt = SystemClock.uptimeMillis();
            cleared = false;
            ctl.pad(0, deck, slot, true, 1f);
            return true;
        }
        @Override void up(boolean cancel) { super.up(cancel); if (!cleared) ctl.pad(0, deck, slot, false, 0f); }
        @Override void tick() {
            // long press on a hot cue pad clears that cue
            if (held() && !cleared && ctl.padMode == Ctl.MODE_CUES && cueT[deck][slot] >= 0
                    && SystemClock.uptimeMillis() - downAt > 750) {
                ctl.pad(0, deck, slot, false, 0f);
                ctl.padForget(0, deck, slot);
                Ctl.cmd(Native.C_CLEAR_CUE, deck, slot, 0, true);
                cleared = true;
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            }
        }
        @Override void draw(Canvas c) { drawPad(c, this); }
    }

    // ---- the controls
    private final Header[] headers = new Header[2];
    private final Wave[] waves = new Wave[2];
    private final Overview[] overviews = new Overview[2];
    private final Platter[] platters = new Platter[2];
    private final VFader[] pitch = new VFader[2], chFader = new VFader[2];
    private final Btn[][] transport = new Btn[2][8];
    private final Btn[][] modeTabs = new Btn[2][4];
    private final Btn[][] fxBtns = new Btn[2][3];
    private final Knob[] fxDepth = new Knob[2];
    private final Btn[][] loopBtns = new Btn[2][5];
    private final Pad[][] pads = new Pad[2][8];
    private final Knob[][] eq = new Knob[2][4];
    private Knob master, curve;
    private XFader xfader;
    private Btn libBtn, setupBtn;
    private final Btn[] pfl = new Btn[2];

    // ---- the setup sheet (opened with SETUP): trim, FX parameter, crossfader assign, 33 / 45, quantize, cue, hamster
    private final List<Ctrl> sheetCtrls = new ArrayList<>();
    private boolean sheetOpen;
    private final RectF sheetR = new RectF(), sheetTitleR = new RectF();
    private final RectF[] sheetCol = { new RectF(), new RectF() };
    private final RectF sheetMid = new RectF();
    private final float[] sheetLabelY = new float[3];   // section captions: knobs, crossfader assign, switches
    private final Knob[] trimK = new Knob[2], fxParamK = new Knob[2];
    private final Btn[][] xfAssign = new Btn[2][3];
    private final Btn[][] deckSw = new Btn[2][3];        // 33/45, quantize, cue
    private Btn hamsterBtn, doneBtn, jogSensBtn, bleBtn;
    private Knob chCurve;

    private void add(Ctrl c) { ctrls.add(c); }
    private void addSheet(Ctrl c) { sheetCtrls.add(c); }

    boolean sheetOpen() { return sheetOpen; }
    void openSheet() { releaseAll(); sheetOpen = true; }
    void closeSheet() { releaseAll(); sheetOpen = false; }

    private void build() {
        for (int d = 0; d < 2; ++d) {
            final int dk = d;
            final int col = Pal.DECK[d];
            add(headers[d] = new Header(d));
            add(overviews[d] = new Overview(d));
            add(waves[d] = new Wave(d));
            add(platters[d] = new Platter(d));
            add(pitch[d] = new VFader(Ctl.did(d, Native.D_PITCH), true, col));
            add(chFader[d] = new VFader(Ctl.did(d, Native.D_FADER), false, col));
            Btn[] t = transport[d];
            t[0] = new Btn("CUE", Pal.AMBER, () -> hasTrack(dk) && !playing(dk) && Math.abs(st[dk][Native.ST_POS] - st[dk][Native.ST_CUE]) < .03, down -> Ctl.cmd(Native.C_CUE, dk, 0, 0, down));
            t[1] = new Btn(() -> playing(dk) ? "❚❚" : "▶", Pal.GREEN, () -> playing(dk), down -> { if (down) Ctl.cmd(Native.C_PLAY_PAUSE, dk, 0, 0, true); });
            t[2] = new Btn("SYNC", Pal.CYAN, () -> st[dk][Native.ST_SYNCED] > .5, down -> { if (down) Ctl.cmd(Native.C_SYNC, dk, 0, 0, true); });
            t[3] = new Btn("KEY", Pal.AMBER, () -> ctl.on(dk, Native.D_KEYLOCK), down -> { if (down) ctl.toggle(dk, Native.D_KEYLOCK); });
            t[4] = new Btn("SLIP", Pal.VIOLET, () -> ctl.on(dk, Native.D_SLIP), down -> { if (down) ctl.toggle(dk, Native.D_SLIP); });
            t[5] = new Btn(() -> "±" + Math.round(ctl.get(dk, Native.D_RANGE) * 100) + "%", Pal.DIM, () -> false, down -> {
                if (!down) return;
                float r = ctl.get(dk, Native.D_RANGE);
                int i = 0;
                for (int k = 0; k < Ctl.RANGES.length; ++k) if (Math.abs(Ctl.RANGES[k] - r) < .01f) i = k;
                ctl.set(dk, Native.D_RANGE, Ctl.RANGES[(i + 1) % Ctl.RANGES.length]);
            });
            t[6] = new Btn("◀ NDG", Pal.TEXT, null, down -> Ctl.cmd(Native.C_NUDGE, dk, 0, down ? -1 : 0, down));
            t[7] = new Btn("NDG ▶", Pal.TEXT, null, down -> Ctl.cmd(Native.C_NUDGE, dk, 0, down ? 1 : 0, down));
            t[1].textMax = 22; t[0].textMax = 16;
            for (Btn b : t) add(b);
            for (int m = 0; m < 4; ++m) {
                final int mm = m;
                add(modeTabs[d][m] = new Btn(Ctl.MODE_SHORT[m], col, () -> ctl.padMode == mm, down -> { if (down) ctl.setPadMode(mm); }));
                modeTabs[d][m].textMax = 11;
            }
            fxBtns[d][0] = new Btn(() -> Ctl.FX_NAMES[Math.max(0, Math.min(7, Math.round(ctl.get(dk, Native.D_FX_TYPE))))], Pal.TEXT, null, down -> {
                if (down) ctl.set(dk, Native.D_FX_TYPE, (Math.round(ctl.get(dk, Native.D_FX_TYPE)) + 1) % 8);
            });
            fxBtns[d][1] = new Btn(() -> Ctl.FX_BEAT_NAMES[fxBeatIndex(dk)], Pal.TEXT, null, down -> {
                if (down) ctl.set(dk, Native.D_FX_BEATS, (float) Ctl.FX_BEATS[(fxBeatIndex(dk) + 1) % Ctl.FX_BEATS.length]);
            });
            fxBtns[d][2] = new Btn("FX ON", Pal.RED, () -> ctl.on(dk, Native.D_FX_ON), down -> { if (down) ctl.toggle(dk, Native.D_FX_ON); });
            for (Btn b : fxBtns[d]) { b.textMax = 11; add(b); }
            add(fxDepth[d] = new Knob(Ctl.did(d, Native.D_FX_DEPTH), "DEPTH", col, false, .5f, LIN01));
            Btn[] l = loopBtns[d];
            l[0] = new Btn("IN", Pal.GREEN, () -> st[dk][Native.ST_LOOPING] > .5, down -> { if (down) Ctl.cmd(Native.C_LOOP_IN, dk, 0, 0, true); });
            l[1] = new Btn("OUT", Pal.GREEN, () -> st[dk][Native.ST_LOOPING] > .5, down -> { if (down) Ctl.cmd(Native.C_LOOP_OUT, dk, 0, 0, true); });
            l[2] = new Btn("½", Pal.TEXT, null, down -> { if (down) Ctl.cmd(Native.C_LOOP_HALVE, dk, 0, 0, true); });
            l[3] = new Btn("×2", Pal.TEXT, null, down -> { if (down) Ctl.cmd(Native.C_LOOP_DOUBLE, dk, 0, 0, true); });
            l[4] = new Btn(() -> st[dk][Native.ST_LOOPING] > .5 ? "EXIT" : "RELOOP", Pal.GREEN, () -> st[dk][Native.ST_LOOPING] > .5, down -> {
                if (down) Ctl.cmd(st[dk][Native.ST_LOOPING] > .5 ? Native.C_LOOP_EXIT : Native.C_RELOOP, dk, 0, 0, true);
            });
            for (Btn b : l) add(b);
            for (int s = 0; s < 8; ++s) add(pads[d][s] = new Pad(d, s));
            add(eq[d][0] = new Knob(Ctl.did(d, Native.D_HI), "HI", col, true, .5f, EQ));
            add(eq[d][1] = new Knob(Ctl.did(d, Native.D_MID), "MID", col, true, .5f, EQ));
            add(eq[d][2] = new Knob(Ctl.did(d, Native.D_LOW), "LOW", col, true, .5f, EQ));
            add(eq[d][3] = new Knob(Ctl.did(d, Native.D_FILTER), "FILTER", Pal.VIOLET, true, .5f, BI));
        }
        add(master = new Knob(Ctl.gid(Native.G_MASTER), "MASTER", Pal.TEXT, false, Ctl.masterNorm(0), MASTER));
        add(curve = new Knob(Ctl.gid(Native.G_XF_CURVE), "XF CURVE", Pal.AMBER, false, .5f, LIN01));
        add(xfader = new XFader());
        add(libBtn = new Btn("LIBRARY", Pal.TEXT, null, down -> { if (down) host.openLibrary(); }));
        libBtn.textMax = 14;
        for (int d = 0; d < 2; ++d) {
            final int dk = d;
            // headphone cue (PFL): on the mixer, under each channel
            add(pfl[d] = new Btn(d == 0 ? "PFL A" : "PFL B", Pal.DECK[d], () -> ctl.on(dk, Native.D_PFL), down -> { if (down) ctl.toggle(dk, Native.D_PFL); }));
            pfl[d].textMax = 12;
        }
        add(setupBtn = new Btn("SETUP", Pal.DIM, () -> sheetOpen, down -> { if (down) openSheet(); }));
        setupBtn.textMax = 12;

        for (int d = 0; d < 2; ++d) {
            final int dk = d;
            final int col = Pal.DECK[d];
            addSheet(trimK[d] = new Knob(Ctl.did(d, Native.D_TRIM), "TRIM", col, true, .5f, TRIM));
            addSheet(fxParamK[d] = new Knob(Ctl.did(d, Native.D_FX_PARAM),
                    () -> Ctl.FX_PARAM_NAMES[Math.max(0, Math.min(7, Math.round(ctl.get(dk, Native.D_FX_TYPE))))], Pal.RED, false, .5f, LIN01));
            String[] names = { "A", "THRU", "B" };
            for (int k = 0; k < 3; ++k) {
                final int kk = k;
                addSheet(xfAssign[d][k] = new Btn(names[k], k == 1 ? Pal.TEXT : Pal.DECK[k / 2], () -> Math.round(ctl.get(dk, Native.D_XF_ASSIGN)) == kk,
                        down -> { if (down) ctl.set(dk, Native.D_XF_ASSIGN, kk); }));
            }
            addSheet(deckSw[d][0] = new Btn(() -> ctl.on(dk, Native.D_RPM45) ? "45 RPM" : "33 RPM", Pal.AMBER, () -> ctl.on(dk, Native.D_RPM45),
                    down -> { if (down) ctl.toggle(dk, Native.D_RPM45); }));
            addSheet(deckSw[d][1] = new Btn("QUANTIZE", Pal.CYAN, () -> ctl.on(dk, Native.D_QUANTIZE), down -> { if (down) ctl.toggle(dk, Native.D_QUANTIZE); }));
            addSheet(deckSw[d][2] = new Btn("PFL CUE", col, () -> ctl.on(dk, Native.D_PFL), down -> { if (down) ctl.toggle(dk, Native.D_PFL); }));
        }
        addSheet(hamsterBtn = new Btn("HAMSTER", Pal.AMBER, () -> ctl.get(Ctl.gid(Native.G_HAMSTER)) > .5f,
                down -> { if (down) ctl.set(Ctl.gid(Native.G_HAMSTER), ctl.get(Ctl.gid(Native.G_HAMSTER)) > .5f ? 0f : 1f); }));
        addSheet(chCurve = new Knob(Ctl.gid(Native.G_FADER_CURVE), "CURVE", Pal.AMBER, false, .3f, LIN01));
        addSheet(jogSensBtn = new Btn(() -> "JOG SENSITIVITY ×" + jogSensText(ctl.jogSens), Pal.TEXT, null, down -> { if (down) ctl.nextJogSens(); }));
        jogSensBtn.textMax = 11;
        addSheet(bleBtn = new Btn("BLUETOOTH MIDI", Pal.CYAN, null, down -> { if (down) host.bluetoothMidi(); }));
        bleBtn.textMax = 12;
        addSheet(doneBtn = new Btn("DONE", Pal.GREEN, null, down -> { if (down) closeSheet(); }));
        doneBtn.textMax = 14;
    }

    static String jogSensText(float k) {
        return Math.abs(k - Math.round(k)) < .01f ? String.valueOf(Math.round(k)) : String.format(java.util.Locale.US, "%.2f", k).replaceAll("0+$", "");
    }

    private int fxBeatIndex(int d) {
        float b = ctl.get(d, Native.D_FX_BEATS);
        int best = 0;
        for (int i = 0; i < Ctl.FX_BEATS.length; ++i) if (Math.abs(Ctl.FX_BEATS[i] - b) < Math.abs(Ctl.FX_BEATS[best] - b)) best = i;
        return best;
    }

    // =================================================================================================== layout
    private static float clamp(float v, float lo, float hi) { return Math.max(lo, Math.min(hi, v)); }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        layout(w, h);
        // the platters sit at the screen edges: keep the system's edge-swipe (back) gesture off them
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            List<android.graphics.Rect> ex = new ArrayList<>();
            for (Platter p : platters) {
                android.graphics.Rect rr = new android.graphics.Rect();
                p.r.roundOut(rr);
                ex.add(rr);
            }
            setSystemGestureExclusionRects(ex);
        }
    }

    private void layout(float W, float H) {
        float g = 3 * dp;
        ts = dp * clamp(Math.min(W / (800 * dp), H / (360 * dp)), .85f, 2.2f);
        autoZoom = clamp(W / dp / 110f, 6, 12);

        float topH = clamp(H * .34f, 110 * dp, 330 * dp);
        float hdrH = clamp(topH * .18f, 20 * dp, 42 * dp);
        float ovH = clamp(topH * .085f, 8 * dp, 22 * dp);
        float waveH = (topH - 2 * hdrH - 2 * ovH) / 2;
        float y = 0;
        headers[0].r.set(0, y, W, y += hdrH);
        overviews[0].r.set(0, y, W, y += ovH);
        waves[0].r.set(0, y, W, y += waveH);
        waves[1].r.set(0, y, W, y += waveH);
        overviews[1].r.set(0, y, W, y += ovH);
        headers[1].r.set(0, y, W, y += hdrH);

        float botH = clamp(H * .29f, 96 * dp, 290 * dp);
        float midTop = topH + g, midH = H - topH - botH - 2 * g;
        float mixW = clamp(W * .27f, 196 * dp, 440 * dp);
        float deckW = (W - mixW) / 2 - g;
        mixRect.set(deckW + g, midTop, deckW + g + mixW, H);

        for (int d = 0; d < 2; ++d) {
            float x0 = d == 0 ? 0 : W - deckW, x1 = x0 + deckW;
            deckMid[d].set(x0, midTop, x1, midTop + midH);
            deckBot[d].set(x0, H - botH, x1, H);

            // platter at the outer edge, pitch fader at the mixer side, buttons between
            float diam = Math.min(midH - 2 * g, deckW * .5f);
            Platter p = platters[d];
            p.rad = diam / 2;
            p.cx = d == 0 ? x0 + g + p.rad + 2 * dp : x1 - g - p.rad - 2 * dp;
            p.cy = midTop + midH / 2;
            p.r.set(p.cx - p.rad, p.cy - p.rad, p.cx + p.rad, p.cy + p.rad);
            float pitchW = clamp(deckW * .13f, 32 * dp, 64 * dp);
            float px0 = d == 0 ? x1 - pitchW - g : x0 + g;
            pitch[d].r.set(px0, midTop + 14 * ts, px0 + pitchW, midTop + midH - g);
            float bx0 = d == 0 ? p.r.right + 2 * g : px0 + pitchW + 2 * g;
            float bx1 = d == 0 ? px0 - 2 * g : p.r.left - 2 * g;
            float bw = (bx1 - bx0 - g) / 2, bh = (midH - 3 * g - 2 * g) / 4;
            for (int i = 0; i < 8; ++i) {
                int row = i / 2, colI = i % 2;
                float bx = bx0 + colI * (bw + g), by = midTop + g + row * (bh + g);
                transport[d][i].r.set(bx, by, bx + bw, by + bh);
            }

            // under the deck: mode tabs + FX on a toolbar, pads and the loop block
            float tbH = clamp(botH * .22f, 24 * dp, 48 * dp);
            float by0 = H - botH + g;
            float tabsW = deckW * .5f;
            float tx = x0 + g;
            float tw = (tabsW - 3 * g) / 4;
            for (int m = 0; m < 4; ++m) modeTabs[d][m].r.set(tx + m * (tw + g), by0, tx + m * (tw + g) + tw, by0 + tbH);
            float fx0 = tx + tabsW + 2 * g, fx1 = x1 - g;
            float knobW = tbH * 1.3f;
            float fxw = fx1 - fx0 - knobW - 3 * g;
            fxBtns[d][0].r.set(fx0, by0, fx0 + fxw * .45f, by0 + tbH);
            fxBtns[d][1].r.set(fxBtns[d][0].r.right + g, by0, fxBtns[d][0].r.right + g + fxw * .22f, by0 + tbH);
            fxBtns[d][2].r.set(fxBtns[d][1].r.right + g, by0, fx0 + fxw + g * 2, by0 + tbH);
            fxDepth[d].r.set(fx1 - knobW, by0 - g, fx1, by0 + tbH + g);

            float py0 = by0 + tbH + 2 * g, py1 = H - g;
            float loopW = clamp(deckW * .28f, 76 * dp, 220 * dp);
            float padX0 = d == 0 ? x0 + g : x0 + g + loopW + g;
            float padX1 = d == 0 ? x1 - g - loopW - g : x1 - g;
            float pw = (padX1 - padX0 - 3 * g) / 4, ph = (py1 - py0 - g) / 2;
            for (int s = 0; s < 8; ++s) {
                int colI = s % 4, row = s / 4;   // bottom row = slots 1-4, like the pads under your fingers
                float sx = padX0 + colI * (pw + g), sy = py1 - (row + 1) * ph - row * g;
                pads[d][s].r.set(sx, sy, sx + pw, sy + ph);
            }
            float lx0 = d == 0 ? x1 - g - loopW : x0 + g;
            float lw = (loopW - g) / 2, lh = (py1 - py0 - 2 * g) / 3;
            loopBtns[d][0].r.set(lx0, py0, lx0 + lw, py0 + lh);
            loopBtns[d][1].r.set(lx0 + lw + g, py0, lx0 + loopW, py0 + lh);
            loopBtns[d][2].r.set(lx0, py0 + lh + g, lx0 + lw, py0 + 2 * lh + g);
            loopBtns[d][3].r.set(lx0 + lw + g, py0 + lh + g, lx0 + loopW, py0 + 2 * lh + g);
            loopBtns[d][4].r.set(lx0, py0 + 2 * (lh + g), lx0 + loopW, py1);
        }

        // the mixer
        float mx0 = mixRect.left + g, mx1 = mixRect.right - g, mw = mx1 - mx0;
        float my = mixRect.top + g;
        float row0 = clamp(mixRect.height() * .14f, 30 * dp, 56 * dp);
        libBtn.r.set(mx0, my, mx0 + mw * .42f, my + row0);
        float kw = (mw * .58f - g) / 2;
        master.r.set(libBtn.r.right + g, my - g, libBtn.r.right + g + kw, my + row0 + g);
        curve.r.set(master.r.right, my - g, mx1, my + row0 + g);
        my += row0 + 2 * g;
        float xfH = clamp(mixRect.height() * .2f, 44 * dp, 96 * dp);
        float statusH = 12 * ts;
        statusR.set(mx0, mixRect.bottom - statusH - g, mx1, mixRect.bottom - g);
        xfader.r.set(mx0, statusR.top - xfH, mx1, statusR.top - g);
        // PFL A | SETUP | PFL B, just above the crossfader
        float rowH = clamp(mixRect.height() * .1f, 22 * dp, 40 * dp);
        float rowY = xfader.r.top - g - rowH;
        float pw = mw * .34f;
        pfl[0].r.set(mx0, rowY, mx0 + pw, rowY + rowH);
        pfl[1].r.set(mx1 - pw, rowY, mx1, rowY + rowH);
        setupBtn.r.set(pfl[0].r.right + g, rowY, pfl[1].r.left - g, rowY + rowH);
        float meterW = clamp(mw * .12f, 20 * dp, 44 * dp);
        float hw = (mw - meterW - 2 * g) / 2;
        float cellH = clamp(mixRect.height() * .17f, 36 * dp, 92 * dp);
        float faderTop = my + 2 * cellH + g, faderBot = rowY - g;
        // short screens (phones): the channel fader goes beside the knobs, so it keeps a useful throw
        boolean side = faderBot - faderTop < 80 * dp;
        for (int d = 0; d < 2; ++d) {
            float hx = d == 0 ? mx0 : mx1 - hw;
            float kw2 = side ? hw * .62f : hw;                      // knob block width
            float kx0 = side ? (d == 0 ? hx : hx + hw - kw2) : hx;  // knobs on the outer side
            float ch = side ? (faderBot - my) / 2 : cellH;
            for (int k = 0; k < 4; ++k) {
                int colI = k % 2, row = k / 2;
                float kx = kx0 + colI * kw2 / 2, ky = my + row * ch;
                eq[d][k].r.set(kx, ky, kx + kw2 / 2, ky + ch);
            }
            if (side) {
                float fw = hw - kw2 - g;
                float fx = d == 0 ? hx + kw2 + g : hx;
                chFader[d].r.set(fx, my, fx + fw, faderBot);
            } else {
                float fw = clamp(hw * .42f, 28 * dp, 60 * dp);
                float fcx = hx + hw / 2;
                chFader[d].r.set(fcx - fw / 2, faderTop, fcx + fw / 2, faderBot);
            }
        }
        float mcx = mx0 + hw + g, mtop = my, mbot = faderBot;
        float bw4 = (meterW - 3 * dp) / 4;
        for (int i = 0; i < 4; ++i) meterR[i].set(mcx + i * (bw4 + dp), mtop, mcx + i * (bw4 + dp) + bw4, mbot);

        layoutSheet(W, H, g);
    }

    private void layoutSheet(float W, float H, float g) {
        float mxs = clamp(W * .04f, 8 * dp, 80 * dp), mys = clamp(H * .05f, 8 * dp, 80 * dp);
        float sw = Math.min(W - 2 * mxs, 980 * dp), sh = Math.min(H - 2 * mys, 520 * dp);
        sheetR.set((W - sw) / 2, (H - sh) / 2, (W + sw) / 2, (H + sh) / 2);
        float pad = clamp(sw * .015f, 6 * dp, 16 * dp);
        float titleH = clamp(sh * .12f, 30 * dp, 52 * dp);
        sheetTitleR.set(sheetR.left + pad, sheetR.top + pad, sheetR.right - pad, sheetR.top + pad + titleH);
        doneBtn.r.set(sheetTitleR.right - clamp(sw * .14f, 84 * dp, 160 * dp), sheetTitleR.top, sheetTitleR.right, sheetTitleR.bottom);
        float top = sheetTitleR.bottom + pad, bot = sheetR.bottom - pad;
        float inner = sheetR.width() - 2 * pad, colW = inner * .37f, midW = inner - 2 * colW - 2 * pad;
        sheetCol[0].set(sheetR.left + pad, top, sheetR.left + pad + colW, bot);
        sheetCol[1].set(sheetR.right - pad - colW, top, sheetR.right - pad, bot);
        sheetMid.set(sheetCol[0].right + pad, top, sheetCol[1].left - pad, bot);
        float capH = clamp(sh * .055f, 13 * dp, 22 * dp);
        float h = bot - top;
        float knobsH = (h - 3 * capH - 2 * g) * .44f, rowH = (h - 3 * capH - 2 * g - knobsH) / 2;
        for (int d = 0; d < 2; ++d) {
            RectF c = sheetCol[d];
            float y = c.top;
            sheetLabelY[0] = y + capH * .8f;
            y += capH;
            float kw = Math.min(c.width() / 2, knobsH * 1.25f);
            float kcx0 = c.left + c.width() * .25f, kcx1 = c.left + c.width() * .75f;
            trimK[d].r.set(kcx0 - kw / 2, y, kcx0 + kw / 2, y + knobsH);
            fxParamK[d].r.set(kcx1 - kw / 2, y, kcx1 + kw / 2, y + knobsH);
            y += knobsH + g;
            sheetLabelY[1] = y + capH * .8f;
            y += capH;
            float bw = (c.width() - 2 * g) / 3;
            for (int k = 0; k < 3; ++k) xfAssign[d][k].r.set(c.left + k * (bw + g), y, c.left + k * (bw + g) + bw, y + rowH);
            y += rowH + g;
            sheetLabelY[2] = y + capH * .8f;
            y += capH;
            for (int k = 0; k < 3; ++k) deckSw[d][k].r.set(c.left + k * (bw + g), y, c.left + k * (bw + g) + bw, y + rowH);
        }
        float y = sheetMid.top + capH;
        hamsterBtn.r.set(sheetMid.left, y, sheetMid.right, y + rowH);
        y += rowH + g + capH;
        // controller block at the bottom: caption, jog sensitivity, Bluetooth MIDI
        float cRow = Math.min(rowH, (sheetMid.height() - 3 * capH) * .2f);
        float cTop = sheetMid.bottom - 2 * cRow - g - capH;
        float kh = Math.max(capH, Math.min(knobsH, cTop - y - g));
        float kw = Math.min(sheetMid.width(), kh * 1.25f);
        chCurve.r.set(sheetMid.centerX() - kw / 2, y, sheetMid.centerX() + kw / 2, y + kh);
        ctrlCapY = cTop + capH * .8f;
        jogSensBtn.r.set(sheetMid.left, cTop + capH, sheetMid.right, cTop + capH + cRow);
        bleBtn.r.set(sheetMid.left, jogSensBtn.r.bottom + g, sheetMid.right, jogSensBtn.r.bottom + g + cRow);
    }
    private float ctrlCapY;

    // =================================================================================================== touch
    @SuppressLint("ClickableViewAccessibility")
    @Override public boolean onTouchEvent(MotionEvent e) {
        int act = e.getActionMasked();
        switch (act) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                float x = e.getX(idx), y = e.getY(idx);
                if (sheetOpen) {
                    if (!sheetR.contains(x, y)) { if (owner.size() == 0) closeSheet(); return true; }
                    for (int i = sheetCtrls.size() - 1; i >= 0; --i) {
                        Ctrl c = sheetCtrls.get(i);
                        if (c.hit(x, y)) { if (c.down(id, x, y)) owner.put(id, c); break; }
                    }
                    return true;
                }
                if (pinchA < 0 && startPinch(id, x, y)) return true;
                if (pinchA >= 0 && inWaves(x, y)) return true;   // a third finger on the waves while zooming: ignored
                for (int i = ctrls.size() - 1; i >= 0; --i) {
                    Ctrl c = ctrls.get(i);
                    if (c.hit(x, y)) {
                        if (c.down(id, x, y)) { owner.put(id, c); }
                        break;
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE:
                if (pinchA >= 0) movePinch(e);
                for (int idx = 0; idx < e.getPointerCount(); ++idx) {
                    Ctrl c = owner.get(e.getPointerId(idx));
                    if (c != null && c.pointer == e.getPointerId(idx)) c.move(e, idx);
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                if (id == pinchA || id == pinchB) { endPinch(); return true; }
                Ctrl c = owner.get(id);
                owner.remove(id);
                if (c != null && c.pointer == id) { c.move(e, idx); c.up(false); }
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                return true;
            default:
                return true;
        }
    }

    void releaseAll() {
        for (int i = 0; i < owner.size(); ++i) { Ctrl c = owner.valueAt(i); if (c.pointer >= 0) c.up(true); }
        owner.clear();
        endPinch();
    }

    // ---- pinch on the waveforms: both decks zoom together (2..32 s across), saved with the other settings
    private int pinchA = -1, pinchB = -1;
    private float pinchD0, pinchZ0;
    private long zoomShownAt;

    RectF waveRect(int d) { return waves[d].r; }

    private boolean inWaves(float x, float y) { return waves[0].r.contains(x, y) || waves[1].r.contains(x, y); }

    /** A finger lands on the waveforms while another one already rests there: that is a pinch, not a scratch. */
    private boolean startPinch(int id, float x, float y) {
        if (!inWaves(x, y)) return false;
        int other = -1;
        float ox = 0;
        for (int i = 0; i < owner.size(); ++i) {
            if (owner.valueAt(i) instanceof Wave) { other = owner.keyAt(i); ox = ((Wave) owner.valueAt(i)).lastX; break; }
        }
        if (other < 0) return false;
        Ctrl w = owner.get(other);
        owner.remove(other);
        if (w.pointer >= 0) w.up(true);
        pinchA = other;
        pinchB = id;
        pinchD0 = Math.max(24 * dp, Math.abs(x - ox));
        pinchZ0 = zoomSec();
        zoomShownAt = SystemClock.uptimeMillis();
        return true;
    }

    private void movePinch(MotionEvent e) {
        int a = e.findPointerIndex(pinchA), b = e.findPointerIndex(pinchB);
        if (a < 0 || b < 0) return;
        float d = Math.max(24 * dp, Math.abs(e.getX(a) - e.getX(b)));
        ctl.setWaveZoom(pinchZ0 * pinchD0 / d);   // fingers apart = fewer seconds = zoom in
        zoomShownAt = SystemClock.uptimeMillis();
    }

    private void endPinch() {
        if (pinchA < 0) return;
        pinchA = pinchB = -1;
        zoomShownAt = SystemClock.uptimeMillis();
    }

    // =================================================================================================== drawing
    @Override protected void onDraw(Canvas c) {
        poll();
        c.drawColor(Pal.BG);
        // panels
        for (int d = 0; d < 2; ++d) {
            panel(c, deckMid[d], Pal.PANEL, 6 * dp, 2 * dp);
            panel(c, deckBot[d], Pal.PANEL, 6 * dp, 2 * dp);
            if (ctl.focus == d) {
                fill.setColor(Pal.WHITE);
                RectF m = deckMid[d];
                c.drawRect(m.left + 10 * dp, m.top + 1 * dp, m.left + 60 * dp, m.top + 4 * dp, fill);
            }
        }
        panel(c, mixRect, Pal.PANEL2, 6 * dp, 2 * dp);
        for (Ctrl k : ctrls) k.draw(c);
        drawMeters(c);
        drawPitchReadout(c);
        String ms = host.midiStatus();
        if (!engineOk) ms = "AUDIO ENGINE NOT RUNNING";
        label(c, ms, statusR.centerX(), statusR.bottom - 2 * dp, Math.min(10 * ts, statusR.height()), engineOk ? Pal.DIM : Pal.RED, Paint.Align.CENTER, statusR.width());
        drawZoom(c);
        if (sheetOpen) drawSheet(c);
    }

    /** The zoom while pinching (and a moment after). */
    private void drawZoom(Canvas c) {
        long age = SystemClock.uptimeMillis() - zoomShownAt;
        if (zoomShownAt == 0 || (pinchA < 0 && age > 1200)) return;
        float z = zoomSec();
        String s = (z < 10 ? String.format(java.util.Locale.US, "%.1f", z) : String.valueOf(Math.round(z))) + " s";
        float size = Math.min(20 * ts, waves[0].r.height() * .5f);
        txt.setTextSize(size);
        float w = txt.measureText("00.0 s") + 16 * dp;
        float cx = waves[0].r.centerX(), cy = waves[0].r.bottom;
        tmp.set(cx - w / 2, cy - size * .9f, cx + w / 2, cy + size * .9f);
        fill.setColor(0xe6000000);
        c.drawRoundRect(tmp, 6 * dp, 6 * dp, fill);
        stroke.setStrokeWidth(2 * dp);
        stroke.setColor(Pal.WHITE);
        c.drawRoundRect(tmp, 6 * dp, 6 * dp, stroke);
        label(c, s, cx, cy + size * .36f, size, Pal.WHITE, Paint.Align.CENTER, 0);
    }

    private void drawSheet(Canvas c) {
        c.drawColor(0xd8000000);
        fill.setColor(Pal.PANEL2);
        c.drawRoundRect(sheetR, 10 * dp, 10 * dp, fill);
        stroke.setStrokeWidth(2 * dp);
        stroke.setColor(Pal.EDGE_HI);
        c.drawRoundRect(sheetR, 10 * dp, 10 * dp, stroke);
        float th = Math.min(sheetTitleR.height() * .55f, 20 * ts);
        label(c, "SETUP", sheetTitleR.left, sheetTitleR.centerY() + th * .36f, th, Pal.WHITE, Paint.Align.LEFT, 0);
        txt.setTextSize(th);
        float hx = sheetTitleR.left + txt.measureText("SETUP") + 12 * dp;
        label(c, "pinch the waveforms to zoom", hx, sheetTitleR.centerY() + th * .3f, th * .6f, Pal.DIM, Paint.Align.LEFT, doneBtn.r.left - hx - 8 * dp);
        float cap = Math.min(11 * ts, (sheetCol[0].height()) * .05f);
        String[] caps = { "", "CROSSFADER ASSIGN", "DECK" };
        for (int d = 0; d < 2; ++d) {
            RectF col = sheetCol[d];
            fill.setColor(Pal.DECK[d]);
            c.drawRect(col.left, col.top + 2 * dp, col.left + 3 * dp, col.bottom, fill);
            for (int k = 0; k < 3; ++k) {
                String s = k == 0 ? "DECK " + (d == 0 ? "A" : "B") + "  ·  TRIM  ·  " + fxBtns[d][0].label.get() : caps[k];
                label(c, s, col.left + 8 * dp, sheetLabelY[k], cap, k == 0 ? Pal.DECK[d] : Pal.DIM, Paint.Align.LEFT, col.width() - 8 * dp);
            }
        }
        label(c, "MIXER", sheetMid.centerX(), sheetMid.top + cap * 1.1f, cap, Pal.DIM, Paint.Align.CENTER, sheetMid.width());
        label(c, "CHANNEL FADERS", sheetMid.centerX(), chCurve.r.top - cap * .4f, cap, Pal.DIM, Paint.Align.CENTER, sheetMid.width());
        label(c, "CONTROLLER", sheetMid.centerX(), ctrlCapY, cap, Pal.DIM, Paint.Align.CENTER, sheetMid.width());
        for (Ctrl k : sheetCtrls) k.draw(c);
    }

    private void panel(Canvas c, RectF r, int col, float pad, float rad) {
        fill.setColor(col);
        tmp.set(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
        c.drawRoundRect(tmp, pad, pad, fill);
    }

    /** Text with a baseline at y, shrunk then ellipsised to fit maxW (0 = no limit). */
    private void label(Canvas c, String s, float x, float y, float size, int color, Paint.Align align, float maxW) {
        if (s == null || s.isEmpty()) return;
        txt.setTextSize(size);
        txt.setColor(color);
        txt.setTextAlign(align);
        if (maxW > 0) {
            float w = txt.measureText(s);
            if (w > maxW) {
                float ns = Math.max(size * .75f, size * maxW / w);
                txt.setTextSize(ns);
                if (txt.measureText(s) > maxW) s = ellipsize(s, maxW, txt);
            }
        }
        c.drawText(s, x, y, txt);
    }

    private static String ellipsize(String s, float maxW, Paint p) {
        int n = p.breakText(s, true, Math.max(0, maxW - p.measureText("…")), null);
        return n <= 0 ? "" : s.substring(0, n) + "…";
    }

    private void drawButton(Canvas c, RectF r, String s, int color, boolean on, boolean pressed, float textMax) {
        float rad = Math.min(6 * dp, r.height() * .2f);
        fill.setColor(on ? Pal.mix(Pal.PANEL, color, .30f) : (pressed ? Pal.EDGE_HI : Pal.RAISED));
        c.drawRoundRect(r, rad, rad, fill);
        stroke.setStrokeWidth(on ? 2 * dp : dp);
        stroke.setColor(on ? color : (pressed ? Pal.TEXT : Pal.EDGE));
        tmp.set(r.left + stroke.getStrokeWidth() / 2, r.top + stroke.getStrokeWidth() / 2, r.right - stroke.getStrokeWidth() / 2, r.bottom - stroke.getStrokeWidth() / 2);
        c.drawRoundRect(tmp, rad, rad, stroke);
        float size = Math.min(textMax * ts, r.height() * .42f);
        label(c, s, r.centerX(), r.centerY() + size * .36f, size, on ? Pal.WHITE : (color == Pal.DIM ? Pal.TEXT : Pal.mix(Pal.TEXT, color, .25f)), Paint.Align.CENTER, r.width() - 6 * dp);
    }

    private void drawHeader(Canvas c, Header h) {
        int d = h.deck;
        RectF r = h.r;
        fill.setColor(Pal.PANEL);
        c.drawRect(r, fill);
        float H = r.height(), pad = 4 * dp;
        // deck tag: white when it has the MIDI focus
        tmp.set(r.left + pad, r.top + 2 * dp, r.left + pad + H * 1.1f, r.bottom - 2 * dp);
        fill.setColor(ctl.focus == d ? Pal.WHITE : Pal.DECK[d]);
        c.drawRoundRect(tmp, 4 * dp, 4 * dp, fill);
        label(c, d == 0 ? "A" : "B", tmp.centerX(), tmp.centerY() + H * .24f, H * .66f, ctl.focus == d ? Pal.DECK[d] : 0xff000000, Paint.Align.CENTER, 0);
        float x = tmp.right + 2 * pad;

        // right side, right to left: time left, BPM + pitch, key
        float big = Math.min(H * .72f, 22 * ts);
        double len = info[d][4], pos = st[d][Native.ST_POS];
        boolean has = hasTrack(d);
        double left = has ? Math.max(0, len - pos) : 0;
        mono.setTextSize(big);
        mono.setTextAlign(Paint.Align.RIGHT);
        mono.setColor(has && left < 30 && len > 0 ? (((SystemClock.uptimeMillis() / 400) % 2 == 0 || !playing(d)) ? Pal.RED : 0xff7a1d18) : Pal.TEXT);
        float rx = r.right - pad;
        String tl = has ? Pal.time(-left) : "--:--.-";
        c.drawText(tl, rx, r.centerY() + big * .36f, mono);
        rx -= mono.measureText("-00:00.0") + 3 * pad;
        double bpm = st[d][Native.ST_BPM];
        mono.setColor(Pal.DECK[d]);
        String bs = has && bpm > 0 ? String.format(java.util.Locale.US, "%.1f", bpm) : "---.-";
        c.drawText(bs, rx, r.centerY() + big * .36f, mono);
        rx -= mono.measureText("000.0") + 2 * pad;
        // pitch %
        float small = big * .62f;
        mono.setTextSize(small);
        mono.setColor(Pal.DIM);
        double pct = pitchPercent(d);
        String ps = String.format(java.util.Locale.US, "%+.1f%%", pct);
        c.drawText(ps, rx, r.centerY() + small * .36f, mono);
        rx -= mono.measureText("+00.0%") + 2 * pad;
        int key = (int) Math.round(info[d][2]);
        if (has && key >= 0 && key < 24) {
            String ks = Pal.camelot(key);
            txt.setTextSize(small);
            float kw = txt.measureText("12A") + 2 * pad;
            tmp.set(rx - kw, r.top + H * .2f, rx, r.bottom - H * .2f);
            fill.setColor(Pal.keyColour(key));
            c.drawRoundRect(tmp, 3 * dp, 3 * dp, fill);
            label(c, ks, tmp.centerX(), tmp.centerY() + small * .36f, small, 0xff000000, Paint.Align.CENTER, 0);
            rx -= kw + 2 * pad;
        }
        // state flags
        float fx = rx;
        if (st[d][Native.ST_SYNCED] > .5) fx = flag(c, "SYNC", Pal.CYAN, fx, r, small);
        if (st[d][Native.ST_LOOPING] > .5) fx = flag(c, "LOOP", Pal.GREEN, fx, r, small);
        if (ctl.on(d, Native.D_KEYLOCK)) fx = flag(c, "KEY", Pal.AMBER, fx, r, small);
        if (ctl.on(d, Native.D_SLIP)) fx = flag(c, "SLIP", Pal.VIOLET, fx, r, small);
        if (ctl.on(d, Native.D_RPM45)) fx = flag(c, "45", Pal.AMBER, fx, r, small);
        if (ctl.on(d, Native.D_PFL)) fx = flag(c, "PFL", Pal.DECK[d], fx, r, small);
        if (st[d][Native.ST_CENSOR] > .5) fx = flag(c, "CENSOR", Pal.RED, fx, r, small);

        // title + artist (or loading / error)
        float tw = fx - x - pad;
        TrackLoader ld = host.loader();
        float prog = ld != null ? ld.progress[d] : -1;
        String err = ld != null ? ld.error[d] : null;
        if (err != null && SystemClock.uptimeMillis() - ld.errorAt[d] > 6000) err = null;
        float ty = r.centerY() + H * .2f;
        if (prog >= 0) {
            String nm = ld.loadingName[d];
            label(c, "LOADING " + Math.round(prog * 100) + "%  " + (nm == null ? "" : nm), x, ty, Math.min(H * .5f, 15 * ts), Pal.AMBER, Paint.Align.LEFT, tw);
        } else if (err != null) {
            label(c, err, x, ty, Math.min(H * .5f, 15 * ts), Pal.RED, Paint.Align.LEFT, tw);
        } else if (has) {
            String title = text[d][0] == null ? "" : text[d][0];
            String artist = text[d].length > 1 && text[d][1] != null ? text[d][1] : "";
            float ts1 = Math.min(H * .52f, 16 * ts);
            txt.setTextSize(ts1);
            float titleW = Math.min(tw, txt.measureText(title));
            label(c, title, x, ty, ts1, Pal.TEXT, Paint.Align.LEFT, tw);
            if (!artist.isEmpty() && tw - titleW > 40 * dp)
                label(c, artist, x + titleW + 3 * pad, ty, ts1 * .8f, Pal.DIM, Paint.Align.LEFT, tw - titleW - 3 * pad);
        } else {
            String sel = host.selectedTitle();
            label(c, sel != null ? "TAP HERE TO LOAD  " + sel : "NO TRACK  ·  open LIBRARY", x, ty, Math.min(H * .48f, 14 * ts), sel != null ? Pal.DECK[d] : Pal.FAINT, Paint.Align.LEFT, tw);
        }
    }

    private float flag(Canvas c, String s, int col, float rx, RectF r, float size) {
        txt.setTextSize(size * .85f);
        float w = txt.measureText(s) + 8 * dp;
        tmp.set(rx - w, r.top + r.height() * .22f, rx, r.bottom - r.height() * .22f);
        stroke.setStrokeWidth(1.5f * dp);
        stroke.setColor(col);
        c.drawRoundRect(tmp, 3 * dp, 3 * dp, stroke);
        label(c, s, tmp.centerX(), tmp.centerY() + size * .3f, size * .85f, col, Paint.Align.CENTER, 0);
        return rx - w - 4 * dp;
    }

    double pitchPercent(int d) {
        double bpm0 = info[d][0], bpm = st[d][Native.ST_BPM];
        if (bpm0 > 0 && bpm > 0 && st[d][Native.ST_TOUCHED] < .5) return (bpm / bpm0 - 1) * 100;
        return ctl.get(d, Native.D_PITCH) * ctl.get(d, Native.D_RANGE) * 100;
    }

    private void drawPitchReadout(Canvas c) {
        for (int d = 0; d < 2; ++d) {
            RectF p = pitch[d].r;
            double pct = ctl.get(d, Native.D_PITCH) * ctl.get(d, Native.D_RANGE) * 100;
            String s = String.format(java.util.Locale.US, "%+.1f", pct);
            label(c, s, p.centerX(), p.top - 3 * dp, 11 * ts, pct == 0 ? Pal.GREEN : Pal.DECK[d], Paint.Align.CENTER, p.width() + 8 * dp);
        }
    }

    private void drawWave(Canvas c, Wave w) {
        int d = w.deck;
        RectF r = w.r;
        fill.setColor(Pal.WELL);
        c.drawRect(r, fill);
        float cx = r.centerX(), pps = w.pxPerSec();
        double pos = st[d][Native.ST_POS];
        if (!hasTrack(d)) {
            stroke.setColor(Pal.EDGE);
            stroke.setStrokeWidth(dp);
            c.drawLine(r.left, r.centerY(), r.right, r.centerY(), stroke);
            return;
        }
        c.save();
        c.clipRect(r);
        double t0 = pos - (cx - r.left) / pps, t1 = pos + (r.right - cx) / pps;
        // loop shading
        if (st[d][Native.ST_LOOPING] > .5) {
            double a = st[d][Native.ST_LOOP_A], b = st[d][Native.ST_LOOP_B];
            if (b > a) {
                fill.setColor(0x3334d058);
                c.drawRect((float) (cx + (a - pos) * pps), r.top, (float) (cx + (b - pos) * pps), r.bottom, fill);
            }
        }
        // waveform tiles
        WaveData wd = wave[d];
        int th = Math.max(8, Math.round(r.height()));
        double cps = WaveData.COLS_PER_SEC;
        int k0 = (int) Math.floor(Math.max(0, t0) * cps / WaveData.TILE), k1 = (int) Math.floor(Math.max(0, t1) * cps / WaveData.TILE);
        float tileW = (float) (WaveData.TILE / cps * pps);
        for (int k = k0; k <= k1; ++k) {
            Bitmap b = wd.tile(k, th);
            if (b == null) continue;
            float x = (float) (cx + (k * WaveData.TILE / cps - pos) * pps);
            tmp.set(x, r.top, x + tileW, r.bottom);
            c.drawBitmap(b, null, tmp, bmp);
        }
        // beatgrid: beat ticks, bar lines every 4 beats
        double bpm = info[d][0], first = info[d][1];
        if (bpm > 20) {
            double beat = 60.0 / bpm;
            long b0 = (long) Math.floor((t0 - first) / beat), b1 = (long) Math.ceil((t1 - first) / beat);
            if (b1 - b0 < 400) {
                for (long b = b0; b <= b1; ++b) {
                    double t = first + b * beat;
                    if (t < 0) continue;
                    float x = (float) (cx + (t - pos) * pps);
                    boolean bar = Math.floorMod(b, 4) == 0;
                    stroke.setStrokeWidth(bar ? 2 * dp : dp);
                    stroke.setColor(bar ? 0xaaffffff : 0x44ffffff);
                    float tick = r.height() * (bar ? .2f : .1f);
                    c.drawLine(x, r.top, x, r.top + tick, stroke);
                    c.drawLine(x, r.bottom - tick, x, r.bottom, stroke);
                    if (bar) { stroke.setColor(0x22ffffff); stroke.setStrokeWidth(dp); c.drawLine(x, r.top + tick, x, r.bottom - tick, stroke); }
                }
            }
        }
        // hot cues
        for (int i = 0; i < 8; ++i) {
            double t = cueT[d][i];
            if (t < 0 || t < t0 - 1 || t > t1 + 1) continue;
            float x = (float) (cx + (t - pos) * pps);
            int col = 0xff000000 | cueRgb[d][i];
            stroke.setColor(col);
            stroke.setStrokeWidth(2 * dp);
            c.drawLine(x, r.top, x, r.bottom, stroke);
            float s = Math.min(14 * dp, r.height() * .3f);
            tmp.set(x, r.top, x + s * 1.1f, r.top + s);
            fill.setColor(col);
            c.drawRect(tmp, fill);
            label(c, String.valueOf(i + 1), tmp.centerX(), tmp.bottom - s * .2f, s * .8f, 0xff000000, Paint.Align.CENTER, 0);
        }
        // the cue point: an orange triangle top and bottom
        double cue = st[d][Native.ST_CUE];
        if (cue >= t0 && cue <= t1) {
            float x = (float) (cx + (cue - pos) * pps), s = Math.min(9 * dp, r.height() * .18f);
            fill.setColor(Pal.AMBER);
            path.reset(); path.moveTo(x - s, r.top); path.lineTo(x + s, r.top); path.lineTo(x, r.top + s); path.close();
            path.moveTo(x - s, r.bottom); path.lineTo(x + s, r.bottom); path.lineTo(x, r.bottom - s); path.close();
            c.drawPath(path, fill);
        }
        // playhead
        fill.setColor(Pal.WHITE);
        c.drawRect(cx - 1.2f * dp, r.top, cx + 1.2f * dp, r.bottom, fill);
        if (ctl.handOn(d)) {
            fill.setColor(Pal.alpha(Pal.DECK[d], .25f));
            c.drawRect(cx - 6 * dp, r.top, cx + 6 * dp, r.bottom, fill);
        }
        c.restore();
        // the deck's colour on the left edge
        fill.setColor(Pal.DECK[d]);
        c.drawRect(r.left, r.top, r.left + 3 * dp, r.bottom, fill);
    }

    private void drawOverview(Canvas c, Overview o) {
        int d = o.deck;
        RectF r = o.r;
        fill.setColor(0xff050607);
        c.drawRect(r, fill);
        if (!hasTrack(d)) return;
        Bitmap b = wave[d].overview(Math.max(4, Math.round(r.width())), Math.max(4, Math.round(r.height())));
        if (b != null) c.drawBitmap(b, null, r, bmp);
        double len = info[d][4];
        if (len <= 0) return;
        float px = (float) (r.left + st[d][Native.ST_POS] / len * r.width());
        fill.setColor(0x99000000);
        c.drawRect(r.left, r.top, px, r.bottom, fill);
        if (st[d][Native.ST_LOOPING] > .5) {
            fill.setColor(0x6634d058);
            c.drawRect((float) (r.left + st[d][Native.ST_LOOP_A] / len * r.width()), r.top, (float) (r.left + st[d][Native.ST_LOOP_B] / len * r.width()) + dp, r.bottom, fill);
        }
        for (int i = 0; i < 8; ++i) {
            if (cueT[d][i] < 0) continue;
            fill.setColor(0xff000000 | cueRgb[d][i]);
            float x = (float) (r.left + cueT[d][i] / len * r.width());
            c.drawRect(x - dp, r.top, x + dp, r.bottom, fill);
        }
        fill.setColor(Pal.WHITE);
        c.drawRect(px - dp, r.top, px + dp, r.bottom, fill);
    }

    private void drawPlatter(Canvas c, Platter p) {
        int d = p.deck;
        float cx = p.cx, cy = p.cy, R = p.rad;
        boolean touched = ctl.handOn(d);
        // rim with strobe dots
        fill.setColor(touched ? Pal.DECK[d] : 0xff5c626a);
        c.drawCircle(cx, cy, R, fill);
        fill.setColor(0xff2a2e33);
        c.drawCircle(cx, cy, R * .955f, fill);
        double ang = st[d][Native.ST_ANGLE];
        float deg = (float) ((ang - Math.floor(ang)) * 360.0);
        c.save();
        c.rotate(deg, cx, cy);
        fill.setColor(0xffc8ccd2);
        float dotR = Math.max(1.2f * dp, R * .018f);
        for (int i = 0; i < 48; ++i) {
            double a = i * Math.PI * 2 / 48;
            c.drawCircle(cx + (float) Math.cos(a) * R * .91f, cy + (float) Math.sin(a) * R * .91f, dotR, fill);
        }
        // vinyl and grooves
        fill.setColor(0xff0c0d0f);
        c.drawCircle(cx, cy, R * .86f, fill);
        stroke.setStrokeWidth(Math.max(1, R * .006f));
        stroke.setColor(0xff1d2024);
        for (float k = .44f; k < .84f; k += .045f) c.drawCircle(cx, cy, R * k, stroke);
        // sheen
        stroke.setColor(0x14ffffff);
        stroke.setStrokeWidth(R * .12f);
        tmp.set(cx - R * .65f, cy - R * .65f, cx + R * .65f, cy + R * .65f);
        c.drawArc(tmp, 200, 40, false, stroke);
        c.drawArc(tmp, 20, 40, false, stroke);
        // the marker line (a sticker on the record)
        stroke.setColor(Pal.WHITE);
        stroke.setStrokeWidth(Math.max(2 * dp, R * .03f));
        stroke.setStrokeCap(Paint.Cap.ROUND);
        c.drawLine(cx, cy - R * .40f, cx, cy - R * .84f, stroke);
        stroke.setStrokeCap(Paint.Cap.BUTT);
        // label
        boolean has = hasTrack(d);
        fill.setColor(has ? Pal.DECK[d] : Pal.DECK_DARK[d]);
        c.drawCircle(cx, cy, R * .36f, fill);
        if (has) {
            String t = text[d][0] == null ? "" : text[d][0];
            label(c, t.toUpperCase(java.util.Locale.ROOT), cx, cy - R * .12f, R * .085f, 0xff101010, Paint.Align.CENTER, R * .56f);
            String a = text[d].length > 1 && text[d][1] != null ? text[d][1] : "";
            label(c, a, cx, cy + R * .22f, R * .07f, 0xff202020, Paint.Align.CENTER, R * .5f);
        }
        c.restore();
        fill.setColor(0xffd8dbe0);
        c.drawCircle(cx, cy, Math.max(2.5f * dp, R * .03f), fill);
        // loading progress around the rim
        TrackLoader ld = host.loader();
        float prog = ld != null ? ld.progress[d] : -1;
        if (prog >= 0) {
            stroke.setColor(Pal.AMBER);
            stroke.setStrokeWidth(R * .05f);
            tmp.set(cx - R * .97f, cy - R * .97f, cx + R * .97f, cy + R * .97f);
            c.drawArc(tmp, -90, prog * 360, false, stroke);
            label(c, Math.round(prog * 100) + "%", cx, cy + R * .1f, R * .22f, Pal.WHITE, Paint.Align.CENTER, 0);
        }
    }

    private void drawPad(Canvas c, Pad p) {
        int d = p.deck, s = p.slot, mode = ctl.padMode;
        RectF r = p.r;
        String l;
        int col;
        boolean on;
        switch (mode) {
            case Ctl.MODE_CUES: {
                boolean set = cueT[d][s] >= 0;
                col = set ? 0xff000000 | cueRgb[d][s] : Pal.FAINT;
                l = "CUE " + (s + 1);
                on = set;
                break;
            }
            case Ctl.MODE_LOOPS: l = Ctl.LOOP_NAMES[s]; col = Pal.GREEN; on = false; break;
            case Ctl.MODE_ROLLS: l = Ctl.ROLL_NAMES[s]; col = Pal.CYAN; on = st[d][Native.ST_ROLLING] > .5 && p.held(); break;
            default:
                l = Ctl.TRANSPORT_NAMES[s];
                col = Pal.AMBER;
                on = (s == 0 && playing(d)) || (s == 2 && st[d][Native.ST_SYNCED] > .5) || (s == 3 && st[d][Native.ST_CENSOR] > .5)
                        || (s == 7 && st[d][Native.ST_LOOPING] > .5);
                if (s == 7 && st[d][Native.ST_LOOPING] > .5) l = "EXIT";
        }
        float rad = Math.min(6 * dp, r.height() * .16f);
        boolean pressed = p.held();
        if (mode == Ctl.MODE_CUES && on) fill.setColor(pressed ? Pal.mix(col, Pal.WHITE, .4f) : Pal.mix(Pal.PANEL, col, .85f));
        else fill.setColor(pressed ? Pal.mix(Pal.RAISED, col, .6f) : on ? Pal.mix(Pal.PANEL, col, .35f) : Pal.RAISED);
        c.drawRoundRect(r, rad, rad, fill);
        stroke.setStrokeWidth(pressed ? 2.5f * dp : dp);
        stroke.setColor(pressed ? Pal.WHITE : (mode == Ctl.MODE_CUES ? (on ? Pal.mix(col, Pal.WHITE, .3f) : Pal.EDGE) : Pal.mix(Pal.EDGE, col, .4f)));
        c.drawRoundRect(r, rad, rad, stroke);
        boolean dark = mode == Ctl.MODE_CUES && on && luminance(col) > .55f;
        float size = Math.min(14 * ts, r.height() * .34f);
        label(c, l, r.centerX(), r.centerY() + size * .36f, size, dark ? 0xff101010 : Pal.WHITE, Paint.Align.CENTER, r.width() - 6 * dp);
        if (p.held() && mode == Ctl.MODE_CUES && on && !p.cleared) {
            float k = Math.min(1f, (SystemClock.uptimeMillis() - p.downAt) / 750f);
            if (k > .25f) { fill.setColor(Pal.RED); c.drawRect(r.left + rad, r.bottom - 4 * dp, r.left + rad + (r.width() - 2 * rad) * k, r.bottom - 2 * dp, fill); }
        }
    }

    private static float luminance(int c) {
        return (.299f * ((c >> 16) & 0xff) + .587f * ((c >> 8) & 0xff) + .114f * (c & 0xff)) / 255f;
    }

    private void drawMeters(Canvas c) {
        int segs = 18;
        for (int i = 0; i < 4; ++i) {
            RectF r = meterR[i];
            if (r.height() < 10) continue;
            float lvl = meterHold[i];
            double db = lvl > 1e-6 ? 20 * Math.log10(lvl) : -100;
            float frac = (float) Math.max(0, Math.min(1, (db + 36) / 39));   // -36 .. +3 dB
            int lit = Math.round(frac * segs);
            float sh = r.height() / segs;
            for (int k = 0; k < segs; ++k) {
                float y1 = r.bottom - k * sh, y0 = y1 - sh + dp;
                int col = k >= segs - 2 ? Pal.RED : k >= segs - 5 ? Pal.AMBER : Pal.GREEN;
                fill.setColor(k < lit ? col : Pal.alpha(col, .12f));
                c.drawRect(r.left, y0, r.right, y1, fill);
            }
        }
    }
}
