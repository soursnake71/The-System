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

/** The whole app in one view: a 1701 monitor on a desk with a slim breadbin keyboard strip in front of it.
 *  The monitor's screen is the C64 screen (25 rows, drawn by the native side) in a 4:3 tube, as on a 1701. */
public class SidView extends View implements Choreographer.FrameCallback {
    private static final int SW = 320, MAX_ROWS = 80;
    private float CASE_H = 250;                                // the keyboard strip, in design units (slimmer sideways)
    private float PANEL_H = 150;                               // the monitor's badge strip + jack panel (badge only sideways)
    private static final int WHITE_KEYS = 15;                  // two octaves and a top C
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
    // The set as on a desk: a 1701 (nearly square, a 4:3 tube showing the C64's 25 rows and its border) with the
    // breadbin strip in front, scaled to fit and centred. Design units: 1000 wide.
    private static final float TUBE_W = 832, TUBE_H = 624, MON_BOTTOM = 890;
    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        final boolean sideways = w > h;
        CASE_H = sideways ? 205 : 250; PANEL_H = 150;
        final float contentH = MON_BOTTOM + 6 + CASE_H;
        scale = Math.min(w / 1000f, h / contentH);
        T = contentH;
        offX = (w - 1000 * scale) / 2; offY = (h - contentH * scale) / 2;
        final float monBottom = MON_BOTTOM, panelTop = monBottom - PANEL_H, tubeBottom = panelTop - 40;
        tubeRect.set(84, tubeBottom - TUBE_H, 84 + TUBE_W, tubeBottom);
        tube.reset(); tube.addRoundRect(tubeRect, 54, 54, Path.Direction.CW);
        // the C64's border round the 320 x 200 picture, which the tube shows a little taller than square pixels
        rows = 25;
        final float side = 28;
        SX = tubeRect.left + side; SS = (TUBE_W - 2 * side) / SW;
        SSY = SS * 1.06f;
        final float picH = rows * 8 * SSY;
        SY = tubeRect.top + (TUBE_H - picH) / 2;
        screenArea.set(SX, SY, SX + SW * SS, SY + picH);
        glass.reset(); glass.addRoundRect(screenArea, 22, 22, Path.Direction.CW);
        screen = Bitmap.createBitmap(SW, rows * 8, Bitmap.Config.ARGB_8888);
        KDY = monBottom + 6 - 596;
        backdrop = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(backdrop);
        c.drawColor(0xff1b1714);
        c.translate(offX, offY);
        c.scale(scale, scale);
        drawRoom(c);
        drawMonitor(c, monBottom, panelTop);
        c.save(); c.translate(0, KDY); drawCase(c); c.restore();
    }
    // keyboard parts, in the case's own coordinates (its top edge at 596)
    private RectF kWell() { return new RectF(40, 664, 40 + 772, 596 + CASE_H - 26); }
    private RectF fWell() { return new RectF(842, 664, 842 + 118, 596 + CASE_H - 26); }
    private RectF whiteRect(int i) {
        final RectF k = kWell(); final float w = (k.width() - 16) / WHITE_KEYS;
        return new RectF(k.left + 8 + i * w, k.top + 6, k.left + 8 + i * w + w - 4, k.bottom - 6);
    }
    private RectF blackRect(int whiteIndexLeft) {
        final RectF a = whiteRect(whiteIndexLeft); final float w = a.width();
        return new RectF(a.right - w * 0.3f, a.top - 2, a.right - w * 0.3f + w * 0.62f, a.top - 2 + a.height() * 0.6f);
    }
    private RectF fkeyRect(int i) {   // two by two
        final RectF f = fWell(); final float hw = (f.width() - 18) / 2, hh = (f.height() - 18) / 2;
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
        rrect(c, 892, sy + 12, 930, sy + 42, 1, 0xff4c4640);
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
        bin.moveTo(30, 596); bin.lineTo(970, 596); bin.quadTo(992, 600, 994, 630);
        bin.lineTo(996, b - 20); bin.quadTo(996, b, 976, b); bin.lineTo(24, b); bin.quadTo(4, b, 4, b - 20);
        bin.lineTo(6, 630); bin.quadTo(8, 600, 30, 596); bin.close();
        p.setColor(0x73000000); c.save(); c.translate(4, 6); c.drawPath(bin, p); c.restore();
        p.setShader(new LinearGradient(0, 596, 0, b, 0xffd2c7b6, 0xffaa9d88, Shader.TileMode.CLAMP)); c.drawPath(bin, p); p.setShader(null);
        p.setStrokeWidth(1.6f);
        for (int i = 0; i < 4; ++i) { p.setColor(BEIGE_LINE); c.drawLine(440, 606 + i * 9, 820, 606 + i * 9, p); p.setColor(0xffe0d6c8); c.drawLine(440, 608 + i * 9, 820, 608 + i * 9, p); }
        rrect(c, 40, 606, 340, 640, 17, 0xff8e8576);
        rrect(c, 44, 609, 336, 637, 14, 0xff595249);
        text(c, "omega", 62, 629, 17, 0xffded4c4, Paint.Align.LEFT);
        rainbow(c, 160, 613, 90, 20);
        text(c, "SID 64", 256, 629, 17, 0xffded4c4, Paint.Align.LEFT);
        rrect(c, 842, 612, 960, 632, 10, 0xff595249);
        text(c, "POWER", 856, 626, 10, 0xffded4c4, Paint.Align.LEFT);
        p.setColor(0xffe0241a); c.drawCircle(940, 622, 6, p);
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
        c.save(); c.translate(0, KDY); drawKeys(c); c.restore();
        c.restore();
    }
    private void drawKeys(Canvas c) {
        int wi = 0;
        for (int n = base; wi < WHITE_KEYS; ++n) {
            if (isBlack(n)) continue;
            final RectF k = whiteRect(wi++);
            keycap(c, k, KEY_BROWN, KEY_BROWN_TOP, down[n]);
            if (n % 12 == 0) text(c, "C" + (n / 12 - 1), k.centerX(), k.bottom - 10, 12, 0xccffffff, Paint.Align.CENTER);
        }
        wi = 0;
        for (int n = base; wi < WHITE_KEYS; ++n) {
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
        for (int n = base; wi < WHITE_KEYS; ++n) { if (!isBlack(n)) { ++wi; continue; } if (blackRect(wi - 1).contains(x, y)) return n; }
        wi = 0;
        for (int n = base; wi < WHITE_KEYS; ++n) {
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
                if (tubeRect.contains(x, y)) { pointerKind.put(id, 1); Native.touch(0, scrX(x), scrY(y)); }
                else if (fkeyAt(x, ky) >= 0) { pointerKind.put(id, 3); fkeyDown = fkeyAt(x, ky); fkey(fkeyDown); }
                else { pointerKind.put(id, 2); setNote(id, noteAt(x, ky), ky); }
            } else if (action == MotionEvent.ACTION_MOVE) {
                final int kind = pointerKind.get(id, 0);
                if (kind == 1) Native.touch(1, scrX(x), scrY(y));
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
