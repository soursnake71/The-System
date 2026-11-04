package com.thesnakedevice.sid;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.util.SparseIntArray;
import android.view.Choreographer;
import android.view.MotionEvent;
import android.view.View;

/** The whole app in one view. Big screen (the default): the C64 picture as large as the display allows, at its real
 *  4:3 proportions inside a thin 1701 frame, with a slim keyboard strip below (sideways it gets more keys, not fatter
 *  ones). Desk: a 1701 monitor on a desk with the breadbin strip in front. The frame's power button switches.
 *  The monitor's screen is the C64 screen (25 rows, drawn by the native side) in a 4:3 tube, as on a 1701. */
public class SidView extends View implements Choreographer.FrameCallback {
    private static final int SW = 320, MAX_ROWS = 80;
    private float CASE_H = 250;                                // the keyboard strip, in design units (slimmer sideways)
    private float PANEL_H = 150;                               // the monitor's badge strip + jack panel (badge only sideways)
    private int whiteKeys = 15;                                // whole octaves and a top C, as many as fit
    private float caseW = 1000;                                // the keyboard strip's width, in design units
    private boolean big = true;                                // big screen, or the desk
    private final RectF powerRect = new RectF();               // the monitor's power button: switches the view
    // big screen, sideways: page buttons on the left, the selected setting's big controls on the right
    private boolean panels = false;
    private final RectF leftPanel = new RectF(), rightPanel = new RectF();
    private final RectF[] pageBtn = new RectF[11];
    private final RectF sliderRect = new RectF(), minusRect = new RectF(), plusRect = new RectF(), resetRect = new RectF();
    private final RectF presetPrev = new RectF(), presetNext = new RectF(), toggleRect = new RectF();
    private String[] edit = new String[0];
    private static final int[] STRIPES = { 0xffd8342b, 0xffef8a2c, 0xfff2d23a, 0xff4aa64a, 0xff2f74c0 };
    private static final int BEIGE_LINE = 0xff9c8f7b;
    private static final int KEY_BROWN = 0xff3f322b, KEY_BROWN_TOP = 0xff574639, KEY_TAUPE = 0xff7d7067, KEY_TAUPE_TOP = 0xff978a80;
    private static final String[] FKEY = { "PLAY", "OCT-", "OCT+", "PANIC" };

    private final int[] px = new int[SW * MAX_ROWS * 8];
    private Bitmap screen = Bitmap.createBitmap(SW, 200, Bitmap.Config.ARGB_8888);
    private int rows = 25;
    private Bitmap backdrop;          // everything that doesn't move
    private float scale = 1, offX = 0, offY = 0, T = 1030;
    // the screen: where the C64 pixels go (design units) and their size; KDY moves the keyboard to the bottom
    private float SX = 112, SY = 104, SS = 2.425f, SSY = 2.425f, KDY = 0;
    private final RectF tubeRect = new RectF(), screenArea = new RectF();
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), crisp = new Paint();
    private final Path tube = new Path(), glass = new Path();
    private final RectF r = new RectF();
    private boolean running;
    private int frame;
    private int base = 48;                                     // lowest key: C3
    private final boolean[] down = new boolean[128];
    private final SparseIntArray pointerNote = new SparseIntArray();   // pointer id -> note (or -1)
    private final SparseIntArray pointerKind = new SparseIntArray();   // 1 screen, 2 keys, 3 function key
    private int fkeyDown = -1;
    private final Sampling sampling;

    public SidView(Context c, Sampling s) {
        super(c);
        sampling = s;
        crisp.setFilterBitmap(false);
        crisp.setAntiAlias(false);
        p.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        setKeepScreenOn(true);
    }

    void resume() { if (!running) { running = true; Choreographer.getInstance().postFrameCallback(this); } }
    void pause() { running = false; }

    @Override public void doFrame(long t) {
        if (!running) return;
        if ((frame++ & 1) == 0) {   // 30 frames a second is plenty for a C64
            Native.setRows(rows);
            Native.render(px);
            sampling.poll();
            screen.setPixels(px, 0, SW, 0, 0, SW, rows * 8);
            invalidate();
        }
        Choreographer.getInstance().postFrameCallback(this);
    }

    // ---------------- layout ----------------
    private static final float TUBE_W = 832, TUBE_H = 624, MON_BOTTOM = 890;
    private float rimL, rimT, rimR, rimB;   // big screen: the frame round the tube
    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        if (w <= 0 || h <= 0) return;
        final boolean sideways = w > h;
        big = getContext().getSharedPreferences("sid", 0).getBoolean("bigScreen", true);
        CASE_H = sideways ? 205 : 250; PANEL_H = 150;
        rows = 25;
        if (big) layoutBig(w, h, sideways); else layoutDesk(w, h);
        // the keys: whole octaves and a top C, each white key at least ~75 px wide
        final float kwPx = (kWell().width() - 16) * scale;
        final int oct = Math.max(2, Math.min(7, ((int) (kwPx / 75) - 1) / 7));
        whiteKeys = 7 * oct + 1;
        base = oct >= 4 ? 36 : 48;
        screen = Bitmap.createBitmap(SW, rows * 8, Bitmap.Config.ARGB_8888);
        glass.reset(); glass.addRoundRect(screenArea, 22, 22, Path.Direction.CW);
        tube.reset(); tube.addRoundRect(tubeRect, Math.min(54, tubeRect.width() * 0.065f), Math.min(54, tubeRect.width() * 0.065f), Path.Direction.CW);
        backdrop = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(backdrop);
        c.drawColor(0xff1b1714);
        c.translate(offX, offY);
        c.scale(scale, scale);
        if (big) { drawRoomBig(c); drawBigMonitor(c); }
        else { drawRoom(c); drawMonitor(c, MON_BOTTOM, MON_BOTTOM - PANEL_H); }
        c.save(); c.translate(0, KDY); drawCase(c); c.restore();
    }
    // the picture inside a tube: the C64's border round 320 x 200, shown a little taller than square pixels
    private void placePicture() {
        final float side = tubeRect.width() * 0.034f;
        SX = tubeRect.left + side; SS = (tubeRect.width() - 2 * side) / SW;
        SSY = SS * 1.06f;
        final float picH = rows * 8 * SSY;
        SY = tubeRect.top + (tubeRect.height() - picH) / 2;
        screenArea.set(SX, SY, SX + SW * SS, SY + picH);
    }
    // big screen: the keyboard strip sets the scale; the tube takes the largest 4:3 that fits above it
    private void layoutBig(int w, int h, boolean sideways) {
        final float kh = sideways ? Math.max(190, Math.min(330, h * 0.23f)) : Math.max(260, Math.min(420, h * 0.16f));
        scale = kh / CASE_H;
        final float DW = w / scale, DH = h / scale;
        offX = 0; offY = 0; T = DH; caseW = DW;
        final float topH = DH - CASE_H - 6, sideM = 36, topM = 36, botM = 70;
        final float availW = DW - 2 * sideM, availH = topH - topM - botM;
        final float tw = Math.min(availW, availH * 4 / 3f), th = tw * 0.75f;
        final float tl = (DW - tw) / 2, tt = topM + (availH - th) / 2;
        tubeRect.set(tl, tt, tl + tw, tt + th);
        rimL = tl - 30; rimT = tt - 30; rimR = tl + tw + 30; rimB = tt + th + 30 + 34;
        powerRect.set(rimR - 66, rimB - 34, rimR - 20, rimB - 10);
        placePicture();
        KDY = DH - CASE_H - 596;
        layoutPanels(DW);
    }
    private void layoutPanels(float DW) {
        final float lw = rimL - 36, rw = DW - rimR - 36;
        panels = Math.min(lw, rw) * scale > 220;   // only when there's real room beside the monitor (sideways)
        if (!panels) return;
        leftPanel.set(18, rimT, rimL - 18, rimB);
        rightPanel.set(rimR + 18, rimT, DW - 18, rimB);
        // 11 page buttons, two columns
        final float gap = 12, bw = (leftPanel.width() - gap) / 2, bh = (leftPanel.height() - 5 * gap) / 6;
        for (int i = 0; i < 11; ++i) {
            final float x = leftPanel.left + (i % 2) * (bw + gap), y = leftPanel.top + (i / 2) * (bh + gap);
            pageBtn[i] = new RectF(x, y, x + bw, y + bh);
        }
        final RectF R = rightPanel; final float u = R.height() / 12;
        sliderRect.set(R.left, R.top + u * 5.2f, R.right, R.top + u * 6.9f);
        toggleRect.set(R.left, R.top + u * 4.9f, R.right, R.top + u * 7.2f);
        final float third = (R.width() - 2 * gap) / 3;
        minusRect.set(R.left, R.top + u * 7.6f, R.left + third, R.top + u * 9.4f);
        resetRect.set(R.left + third + gap, minusRect.top, R.left + 2 * third + gap, minusRect.bottom);
        plusRect.set(R.left + 2 * (third + gap), minusRect.top, R.right, minusRect.bottom);
        presetPrev.set(R.left, R.top + u * 10.2f, R.left + third, R.bottom);
        presetNext.set(R.right - third, presetPrev.top, R.right, R.bottom);
    }
    // the desk: a 1701 (nearly square, a 4:3 tube) with the breadbin strip in front, scaled to fit and centred
    private void layoutDesk(int w, int h) {
        panels = false;
        final float contentH = MON_BOTTOM + 6 + CASE_H;
        scale = Math.min(w / 1000f, h / contentH);
        T = contentH; caseW = 1000;
        offX = (w - 1000 * scale) / 2; offY = (h - contentH * scale) / 2;
        final float tubeBottom = MON_BOTTOM - PANEL_H - 40;
        tubeRect.set(84, tubeBottom - TUBE_H, 84 + TUBE_W, tubeBottom);
        placePicture();
        final float sy = MON_BOTTOM - PANEL_H + 4;
        powerRect.set(880, sy + 4, 940, sy + 50);
        KDY = MON_BOTTOM + 6 - 596;
    }
    private void toggleView() {
        releaseAll();
        big = !big;
        getContext().getSharedPreferences("sid", 0).edit().putBoolean("bigScreen", big).apply();
        onSizeChanged(getWidth(), getHeight(), 0, 0);
        invalidate();
    }
    // keyboard parts, in the case's own coordinates (its top edge at 596)
    // a wide strip (sideways) puts the four function keys in a row in a wider well, so they stay finger-sized
    private boolean fRow() { return caseW > 1500; }
    private float fWellW() { return fRow() ? 460 : 118; }
    private RectF kWell() { return new RectF(40, 664, caseW - fWellW() - 70, 596 + CASE_H - 26); }
    private RectF fWell() { return new RectF(caseW - fWellW() - 40, 664, caseW - 40, 596 + CASE_H - 26); }
    private RectF whiteRect(int i) {
        final RectF k = kWell(); final float w = (k.width() - 16) / whiteKeys;
        return new RectF(k.left + 8 + i * w, k.top + 6, k.left + 8 + i * w + w - 4, k.bottom - 6);
    }
    private RectF blackRect(int whiteIndexLeft) {
        final RectF a = whiteRect(whiteIndexLeft); final float w = a.width();
        return new RectF(a.right - w * 0.3f, a.top - 2, a.right - w * 0.3f + w * 0.62f, a.top - 2 + a.height() * 0.6f);
    }
    private RectF fkeyRect(int i) {   // two by two, or in a row
        final RectF f = fWell();
        if (fRow()) { final float kw = (f.width() - 30) / 4; return new RectF(f.left + 6 + i * (kw + 6), f.top + 6, f.left + 6 + i * (kw + 6) + kw, f.bottom - 6); }
        final float hw = (f.width() - 18) / 2, hh = (f.height() - 18) / 2;
        final int cx = i % 2, cy = i / 2;
        return new RectF(f.left + 6 + cx * (hw + 6), f.top + 6 + cy * (hh + 6), f.left + 6 + cx * (hw + 6) + hw, f.top + 6 + cy * (hh + 6) + hh);
    }
    private static boolean isBlack(int n) { final int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    // ---------------- drawing ----------------
    private void drawRoom(Canvas c) {
        final float desk = MON_BOTTOM - 40;   // the monitor stands on the desk; the keyboard sits in front
        p.setShader(new LinearGradient(0, -400, 0, desk, 0xff2e2925, 0xff1b1714, Shader.TileMode.CLAMP));
        c.drawRect(-3000, -3000, 4000, desk, p);
        p.setShader(new LinearGradient(0, desk, 0, desk + 900, 0xff5a3d29, 0xff3a281b, Shader.TileMode.CLAMP));
        c.drawRect(-3000, desk, 4000, desk + 4000, p);
        p.setShader(null);
    }
    private void rainbow(Canvas c, float x, float y, float w, float h) {
        final float sh = h / 5;
        for (int i = 0; i < 5; ++i) { p.setColor(STRIPES[i]); c.drawRect(x, y + i * sh, x + w, y + i * sh + sh * 0.72f, p); }
    }
    private void text(Canvas c, String s, float x, float y, float size, int color, Paint.Align a) {
        p.setTextSize(size); p.setColor(color); p.setTextAlign(a); c.drawText(s, x, y, p);
    }
    private void rrect(Canvas c, float l, float t, float rr, float b, float rad, int col) { p.setColor(col); r.set(l, t, rr, b); c.drawRoundRect(r, rad, rad, p); }
    private void stroke(Canvas c, float l, float t, float rr, float b, float rad, int col, float w) {
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(w); p.setColor(col); r.set(l, t, rr, b); c.drawRoundRect(r, rad, rad, p); p.setStyle(Paint.Style.FILL);
    }
    private void drawRoomBig(Canvas c) {
        p.setShader(new LinearGradient(0, 0, 0, T, 0xff2e2925, 0xff171310, Shader.TileMode.CLAMP));
        c.drawRect(-100, -100, caseW + 100, T + 100, p); p.setShader(null);
    }
    // big screen: a thin 1701 frame (beige rim, the stepped bezels) round the tube, the badge and the power button
    private void drawBigMonitor(Canvas c) {
        rrect(c, rimL + 6, rimT + 8, rimR + 6, rimB + 10, 16, 0x66000000);
        p.setShader(new LinearGradient(0, rimT, 0, rimB, 0xffd3c8b8, 0xffbcb1a1, Shader.TileMode.CLAMP));
        r.set(rimL, rimT, rimR, rimB); c.drawRoundRect(r, 16, 16, p); p.setShader(null);
        final RectF t = tubeRect;
        rrect(c, t.left - 24, t.top - 24, t.right + 24, t.bottom + 24, 14, 0xff454a49);
        rrect(c, t.left - 15, t.top - 15, t.right + 15, t.bottom + 15, 10, 0xff282826);
        rrect(c, t.left - 8, t.top - 8, t.right + 8, t.bottom + 8, 30, 0xff2a2726);
        p.setColor(0xff6c5eb5); c.drawPath(tube, p);
        p.setColor(0xff352879); c.drawPath(glass, p);
        // the badge, centred under the tube
        final float by = t.bottom + 30, cx = (rimL + rimR) / 2;
        rrect(c, cx - 100, by, cx + 100, by + 28, 2, 0xff3f413e);
        text(c, "omega", cx - 90, by + 20, 17, 0xf0ffffff, Paint.Align.LEFT);
        rainbow(c, cx - 4, by + 4, 62, 20);
        rrect(c, cx + 64, by + 4, cx + 92, by + 24, 1, 0xff2a2a28);
        p.setColor(0xff8ee05a); c.drawRect(cx + 71, by + 10, cx + 85, by + 14, p);
        text(c, "VIDEO MONITOR MODEL 6581", rimL + 30, by + 19, 11, 0xff6e6458, Paint.Align.LEFT);
        // the power button: tap it for the desk view
        rrect(c, powerRect.left, powerRect.top, powerRect.right, powerRect.bottom, 2, 0xff4c4640);
        text(c, "DESK", powerRect.left - 8, powerRect.centerY() + 4, 10, 0xff6e6458, Paint.Align.RIGHT);
    }
    // the 1701: beige case, stepped grey bezels, the tube with its light-blue border, the badge strip and the jacks
    private void drawMonitor(Canvas c, float mb, float pt) {
        rrect(c, 24, 18, 988, mb + 8, 12, 0x66000000);
        p.setShader(new LinearGradient(0, 10, 0, mb, 0xffd3c8b8, 0xffbcb1a1, Shader.TileMode.CLAMP));
        r.set(18, 10, 982, mb); c.drawRoundRect(r, 10, 10, p); p.setShader(null);
        stroke(c, 28, 20, 972, pt - 6, 12, 0xff8f8274, 2);
        final float bb = pt - 14;                                  // bottom of the bezel stack
        rrect(c, 38, 30, 962, bb, 12, 0xff454a49);                 // outer bezel
        rrect(c, 54, 46, 946, bb - 16, 8, 0xff282826);             // dark step
        rrect(c, 64, 56, 936, bb - 26, 6, 0xff454a49);             // inner grey ring
        rrect(c, 74, 66, 926, tubeRect.bottom + 10, 26, 0xff2a2726);   // the recess round the tube
        p.setColor(0xff6c5eb5); c.drawPath(tube, p);               // the light-blue border
        p.setColor(0xff352879); c.drawPath(glass, p);
        // the badge strip
        final float sy = pt + 4;
        p.setColor(0xff8f8274); c.drawRect(18, sy - 2, 982, sy, p); c.drawRect(18, sy + 54, 982, sy + 56, p);
        c.drawRect(380, sy, 382, sy + 54, p); c.drawRect(632, sy, 634, sy + 54, p);
        rrect(c, 392, sy + 8, 622, sy + 46, 2, 0xff3f413e);
        text(c, "omega", 404, sy + 34, 20, 0xf0ffffff, Paint.Align.LEFT);
        rainbow(c, 506, sy + 13, 74, 26);
        rrect(c, 586, sy + 10, 616, sy + 44, 1, 0xff2a2a28);
        p.setColor(0xff8ee05a); c.drawRect(594, sy + 18, 608, sy + 22, p);
        text(c, "POWER", 601, sy + 38, 6, 0xffbbbbbb, Paint.Align.CENTER);
        stroke(c, 700, sy + 12, 860, sy + 42, 1, 0xff6e6458, 1.5f);
        text(c, "VIDEO MONITOR", 708, sy + 25, 11, 0xff5d544a, Paint.Align.LEFT);
        text(c, "MODEL 6581", 708, sy + 37, 11, 0xff5d544a, Paint.Align.LEFT);
        rrect(c, 892, sy + 12, 930, sy + 42, 1, 0xff4c4640);   // power: tap it for the big screen
        // the lower panel with the jacks (when there's room for it)
        if (PANEL_H < 120) return;
        final float jy = sy + 56;
        p.setColor(0xff8f8274); c.drawRect(800, jy, 802, mb - 6, p);
        for (int k = 0; k < 2; ++k) {
            final float cx = 846 + k * 66, cy = jy + 36;
            p.setColor(0xffa79c8c); c.drawCircle(cx, cy, 20, p);
            p.setColor(0xffd9d1c4); c.drawCircle(cx, cy, 15, p);
            p.setColor(0xff2a2a28); c.drawCircle(cx, cy, 9, p);
            p.setColor(k == 0 ? 0xfff2d23a : 0xfff4f4f4); c.drawCircle(cx, cy, 4.5f, p);
            text(c, k == 0 ? "VIDEO" : "AUDIO", cx, cy + 34, 10, 0xff5d544a, Paint.Align.CENTER);
        }
        text(c, "POWER", 911, sy + 52, 9, 0xff5d544a, Paint.Align.CENTER);
    }
    private void drawCase(Canvas c) {
        final float b = 596 + CASE_H - 8;
        final Path bin = new Path();
        final float R = caseW;
        bin.moveTo(30, 596); bin.lineTo(R - 30, 596); bin.quadTo(R - 8, 600, R - 6, 630);
        bin.lineTo(R - 4, b - 20); bin.quadTo(R - 4, b, R - 24, b); bin.lineTo(24, b); bin.quadTo(4, b, 4, b - 20);
        bin.lineTo(6, 630); bin.quadTo(8, 600, 30, 596); bin.close();
        p.setColor(0x73000000); c.save(); c.translate(4, 6); c.drawPath(bin, p); c.restore();
        p.setShader(new LinearGradient(0, 596, 0, b, 0xffd2c7b6, 0xffaa9d88, Shader.TileMode.CLAMP)); c.drawPath(bin, p); p.setShader(null);
        p.setStrokeWidth(1.6f);
        for (int i = 0; i < 4; ++i) { p.setColor(BEIGE_LINE); c.drawLine(440, 606 + i * 9, R - 180, 606 + i * 9, p); p.setColor(0xffe0d6c8); c.drawLine(440, 608 + i * 9, R - 180, 608 + i * 9, p); }
        rrect(c, 40, 606, 340, 640, 17, 0xff8e8576);
        rrect(c, 44, 609, 336, 637, 14, 0xff595249);
        text(c, "omega", 62, 629, 17, 0xffded4c4, Paint.Align.LEFT);
        rainbow(c, 160, 613, 90, 20);
        text(c, "SID 64", 256, 629, 17, 0xffded4c4, Paint.Align.LEFT);
        rrect(c, R - 158, 612, R - 40, 632, 10, 0xff595249);
        text(c, "POWER", R - 144, 626, 10, 0xffded4c4, Paint.Align.LEFT);
        p.setColor(0xffe0241a); c.drawCircle(R - 60, 622, 6, p);
        r.set(kWell()); r.inset(-3, -3); p.setColor(0xff7d7262); c.drawRoundRect(r, 6, 6, p);
        r.set(fWell()); r.inset(-3, -3); c.drawRoundRect(r, 6, 6, p);
        p.setColor(0xff2c2622); c.drawRoundRect(kWell(), 5, 5, p); c.drawRoundRect(fWell(), 5, 5, p);
    }
    private void keycap(Canvas c, RectF k, int base, int top, boolean pressed) {
        final RectF a = new RectF(k); if (pressed) a.offset(0, 2);
        p.setColor(0x73000000); r.set(a); r.offset(0, pressed ? 1 : 4); c.drawRoundRect(r, 5, 5, p);
        p.setColor(base); c.drawRoundRect(a, 5, 5, p);
        final float ix = a.width() * 0.12f;
        r.set(a.left + ix, a.top + 3, a.right - ix, a.bottom - 3 - a.height() * 0.10f);
        p.setShader(new LinearGradient(0, r.top, 0, r.bottom, pressed ? top : mix(top, 0xffffffff, 0.12f), mix(top, 0xff000000, 0.15f), Shader.TileMode.CLAMP));
        c.drawRoundRect(r, 4, 4, p); p.setShader(null);
    }
    private static int mix(int a, int b, float t) {
        final int ra = a >> 16 & 255, ga = a >> 8 & 255, ba = a & 255, rb = b >> 16 & 255, gb = b >> 8 & 255, bb = b & 255;
        return 0xff000000 | (int) (ra + (rb - ra) * t) << 16 | (int) (ga + (gb - ga) * t) << 8 | (int) (ba + (bb - ba) * t);
    }

    @Override protected void onDraw(Canvas c) {
        if (backdrop == null) return;
        c.drawBitmap(backdrop, 0, 0, null);
        c.save();
        c.translate(offX, offY);
        c.scale(scale, scale);
        // the screen: crisp pixels, scanlines, a little darkening at the tube's edges, the glass
        c.save();
        c.clipPath(glass);
        c.drawBitmap(screen, null, screenArea, crisp);
        p.setColor(0x1c000000);
        for (float y = screenArea.top; y < screenArea.bottom; y += SSY) c.drawRect(screenArea.left, y + SSY * 0.62f, screenArea.right, y + SSY, p);
        c.restore();
        c.save();
        c.clipPath(tube);
        final float cx = tubeRect.centerX(), cy = tubeRect.centerY(), rad = Math.max(tubeRect.width(), tubeRect.height()) * 0.62f;
        p.setShader(new RadialGradient(cx, cy, rad, 0x00000000, 0x55000000, Shader.TileMode.CLAMP)); c.drawPath(tube, p);
        p.setShader(new LinearGradient(tubeRect.left, tubeRect.top, tubeRect.left + 400, tubeRect.top + 500, 0x16ffffff, 0x00ffffff, Shader.TileMode.CLAMP));
        r.set(tubeRect.left - 40, tubeRect.top - 40, tubeRect.left + 560, tubeRect.top + 420); c.drawOval(r, p); p.setShader(null);
        c.restore();
        if (panels) drawPanels(c);
        c.save(); c.translate(0, KDY); drawKeys(c); c.restore();
        c.restore();
    }
    // C64 colours for the big controls
    private static final int C_BLUE = 0xff352879, C_LBLUE = 0xff6c5eb5, C_WHITE = 0xffffffff, C_YELLOW = 0xffb8c76f, C_GREY = 0xff959595, C_GREEN = 0xff9ad284;
    private void bigButton(Canvas c, RectF b, String label, boolean lit, float size) {
        rrect(c, b.left, b.top, b.right, b.bottom, 10, lit ? C_LBLUE : C_BLUE);
        stroke(c, b.left, b.top, b.right, b.bottom, 10, C_LBLUE, 3);
        p.setTypeface(Typeface.MONOSPACE); p.setFakeBoldText(true);
        text(c, label, b.centerX(), b.centerY() + size * 0.36f, size, lit ? C_BLUE : C_WHITE, Paint.Align.CENTER);
        p.setFakeBoldText(false); p.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    }
    private void drawPanels(Canvas c) {
        edit = Native.editState().split("\\|", -1);
        if (edit.length < 8) return;
        final int page = parseInt(edit[0], 0), sel = parseInt(edit[1], -1);
        for (int i = 0; i < 11; ++i) bigButton(c, pageBtn[i], ScreenPages.NAME[i], ScreenPages.ORDER[i] == page, Math.min(44, pageBtn[i].height() * 0.42f));
        final RectF R = rightPanel; final float u = R.height() / 12;
        rrect(c, R.left - 8, R.top - 8, R.right + 8, R.top + u * 9.8f, 14, 0x40000000);
        p.setTypeface(Typeface.MONOSPACE); p.setFakeBoldText(true);
        if (sel < 0) {
            text(c, "TAP A SETTING", R.centerX(), R.top + u * 3, Math.min(40, R.width() / 11), C_LBLUE, Paint.Align.CENTER);
            text(c, "ON THE SCREEN", R.centerX(), R.top + u * 4.2f, Math.min(40, R.width() / 11), C_LBLUE, Paint.Align.CENTER);
            text(c, "TO EDIT IT HERE", R.centerX(), R.top + u * 5.4f, Math.min(40, R.width() / 11), C_GREY, Paint.Align.CENTER);
        } else {
            final int kind = parseInt(edit[4], 0); final float norm = parseFloat(edit[5]);
            text(c, edit[2], R.centerX(), R.top + u * 1.4f, Math.min(34, R.width() / (edit[2].length() * 0.62f + 1)), C_GREY, Paint.Align.CENTER);
            text(c, edit[3], R.centerX(), R.top + u * 3.9f, Math.min(u * 2.0f, R.width() / (edit[3].length() * 0.62f + 1)), C_WHITE, Paint.Align.CENTER);
            if (kind == 2) bigButton(c, toggleRect, edit[3].equals("ON") ? "ON" : "OFF", edit[3].equals("ON"), u * 1.1f);
            else {
                // the slider: drag along it
                rrect(c, sliderRect.left, sliderRect.top, sliderRect.right, sliderRect.bottom, 12, 0xff1a1450);
                final float fx = sliderRect.left + norm * sliderRect.width();
                rrect(c, sliderRect.left, sliderRect.top, Math.max(sliderRect.left + 24, fx), sliderRect.bottom, 12, kind == 3 ? C_YELLOW : C_GREEN);
                rrect(c, fx - 8, sliderRect.top - 10, fx + 8, sliderRect.bottom + 10, 4, C_WHITE);
            }
            bigButton(c, minusRect, kind == 3 ? "<" : "-", false, u * 1.0f);
            bigButton(c, resetRect, "RESET", false, u * 0.55f);
            bigButton(c, plusRect, kind == 3 ? ">" : "+", false, u * 1.0f);
        }
        p.setFakeBoldText(false); p.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        // presets
        bigButton(c, presetPrev, "<", false, u * 1.0f);
        bigButton(c, presetNext, ">", false, u * 1.0f);
        p.setTypeface(Typeface.MONOSPACE); p.setFakeBoldText(true);
        final String pn = edit[7] + " " + edit[6];
        text(c, pn, R.centerX(), presetPrev.centerY() + u * 0.2f, Math.min(u * 0.6f, (presetNext.left - presetPrev.right - 16) / (pn.length() * 0.62f + 1)), C_YELLOW, Paint.Align.CENTER);
        text(c, "PRESET", R.centerX(), presetPrev.top - u * 0.15f, u * 0.4f, C_GREY, Paint.Align.CENTER);
        p.setFakeBoldText(false); p.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    }
    private static int parseInt(String s, int d) { try { return Integer.parseInt(s.trim()); } catch (Exception e) { return d; } }
    private static float parseFloat(String s) { try { return Float.parseFloat(s.trim()); } catch (Exception e) { return 0; } }
    /** the panels' touches: true if one was hit (kind 5 = dragging the slider) */
    private boolean panelDown(int id, float x, float y) {
        if (!panels) return false;
        for (int i = 0; i < 11; ++i) if (pageBtn[i].contains(x, y)) { Native.editAction(4, ScreenPages.ORDER[i]); pointerKind.put(id, 6); return true; }
        final int kind = edit.length >= 8 ? parseInt(edit[4], 0) : 0; final boolean sel = edit.length >= 8 && parseInt(edit[1], -1) >= 0;
        if (sel && kind == 2 && toggleRect.contains(x, y)) { Native.editAction(1, 1); pointerKind.put(id, 6); return true; }
        if (sel && kind != 2 && sliderRect.contains(x, y)) { pointerKind.put(id, 5); sliderTo(x); return true; }
        if (sel && minusRect.contains(x, y)) { Native.editAction(1, -1); pointerKind.put(id, 6); return true; }
        if (sel && plusRect.contains(x, y)) { Native.editAction(1, 1); pointerKind.put(id, 6); return true; }
        if (sel && resetRect.contains(x, y)) { Native.editAction(2, 0); pointerKind.put(id, 6); return true; }
        if (presetPrev.contains(x, y)) { Native.editAction(3, -1); pointerKind.put(id, 6); return true; }
        if (presetNext.contains(x, y)) { Native.editAction(3, 1); pointerKind.put(id, 6); return true; }
        if (leftPanel.contains(x, y) || rightPanel.contains(x, y)) { pointerKind.put(id, 6); return true; }
        return false;
    }
    private void sliderTo(float x) { Native.editAction(0, Math.max(0, Math.min(1, (x - sliderRect.left) / sliderRect.width()))); }
    private void drawKeys(Canvas c) {
        int wi = 0;
        for (int n = base; wi < whiteKeys; ++n) {
            if (isBlack(n)) continue;
            final RectF k = whiteRect(wi++);
            keycap(c, k, KEY_BROWN, KEY_BROWN_TOP, down[n]);
            if (n % 12 == 0) text(c, "C" + (n / 12 - 1), k.centerX(), k.bottom - 10, 12, 0xccffffff, Paint.Align.CENTER);
        }
        wi = 0;
        for (int n = base; wi < whiteKeys; ++n) {
            if (!isBlack(n)) { ++wi; continue; }
            keycap(c, blackRect(wi - 1), mix(KEY_TAUPE, 0xff000000, 0.3f), KEY_TAUPE_TOP, down[n]);
        }
        final boolean playing = Native.seqRunning();
        for (int i = 0; i < 4; ++i) {
            final RectF k = fkeyRect(i);
            keycap(c, k, KEY_TAUPE, KEY_TAUPE_TOP, fkeyDown == i);
            text(c, "f" + (i * 2 + 1), k.centerX(), k.centerY() - 3, 13, 0xddffffff, Paint.Align.CENTER);
            text(c, i == 0 && playing ? "STOP" : FKEY[i], k.centerX(), k.centerY() + 12, 9, 0xddffffff, Paint.Align.CENTER);
        }
    }

    // ---------------- touch ----------------
    private int noteAt(float x, float y) {
        if (!kWell().contains(x, y)) return -1;
        int wi = 0;
        for (int n = base; wi < whiteKeys; ++n) { if (!isBlack(n)) { ++wi; continue; } if (blackRect(wi - 1).contains(x, y)) return n; }
        wi = 0;
        for (int n = base; wi < whiteKeys; ++n) {
            if (isBlack(n)) continue;
            final RectF k = whiteRect(wi++); k.inset(-2, 0);
            if (k.contains(x, y)) return n;
        }
        return -1;
    }
    private int fkeyAt(float x, float y) { for (int i = 0; i < 4; ++i) if (fkeyRect(i).contains(x, y)) return i; return -1; }
    private float velocityAt(float y, int n) {
        // further down the key = louder, like striking it harder
        final RectF k = isBlack(n) ? blackRect(0) : whiteRect(0);
        return Math.max(0.35f, Math.min(1f, 0.35f + 0.75f * (y - k.top) / k.height()));
    }
    private void setNote(int id, int n, float y) {
        final int old = pointerNote.get(id, -1);
        if (old == n) return;
        if (old >= 0) { down[old] = false; Native.noteOff(old); }
        if (n >= 0) { down[n] = true; Native.noteOn(n, velocityAt(y, n)); }
        pointerNote.put(id, n);
    }
    private void fkey(int k) {
        switch (k) {
            case 0: Native.seqToggle(); break;
            case 1: releaseAll(); base = Math.max(12, base - 12); break;
            case 2: releaseAll(); base = Math.min(96, base + 12); break;
            default: releaseAll(); Native.panic(); break;
        }
    }
    private void releaseAll() { for (int n = 0; n < 128; ++n) if (down[n]) { down[n] = false; Native.noteOff(n); } pointerNote.clear(); }
    private float scrX(float x) { return (x - SX) / SS; }
    private float scrY(float y) { return (y - SY) / SSY; }

    @Override public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        for (int i = 0; i < e.getPointerCount(); ++i) {
            final int id = e.getPointerId(i);
            final float x = (e.getX(i) - offX) / scale, y = (e.getY(i) - offY) / scale, ky = y - KDY;
            final boolean isThis = e.getActionIndex() == i;
            if ((action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) && isThis) {
                if (powerRect.contains(x, y)) { pointerKind.put(id, 4); toggleView(); return true; }
                if (panelDown(id, x, y)) continue;
                if (tubeRect.contains(x, y)) { pointerKind.put(id, 1); Native.touch(0, scrX(x), scrY(y)); }
                else if (fkeyAt(x, ky) >= 0) { pointerKind.put(id, 3); fkeyDown = fkeyAt(x, ky); fkey(fkeyDown); }
                else { pointerKind.put(id, 2); setNote(id, noteAt(x, ky), ky); }
            } else if (action == MotionEvent.ACTION_MOVE) {
                final int kind = pointerKind.get(id, 0);
                if (kind == 1) Native.touch(1, scrX(x), scrY(y));
                else if (kind == 5) sliderTo(x);
                else if (kind == 2) setNote(id, noteAt(x, ky), ky);   // slide across the keys
            } else if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) && (isThis || action == MotionEvent.ACTION_CANCEL)) {
                final int kind = pointerKind.get(id, 0);
                if (kind == 1) Native.touch(2, scrX(x), scrY(y));
                else if (kind == 2) { setNote(id, -1, ky); pointerNote.delete(id); }
                else if (kind == 3) fkeyDown = -1;
                pointerKind.delete(id);
            }
        }
        invalidate();
        return true;
    }
    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        if (e.getAction() == MotionEvent.ACTION_SCROLL) {
            final float x = (e.getX() - offX) / scale, y = (e.getY() - offY) / scale;
            if (tubeRect.contains(x, y)) { Native.wheel(scrX(x), scrY(y), e.getAxisValue(MotionEvent.AXIS_VSCROLL)); return true; }
        }
        return super.onGenericMotionEvent(e);
    }
}
