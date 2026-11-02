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

/** The whole app in one view: a 1701-style monitor with the C64 screen (drawn by the native side) and the breadbin
 *  keyboard: brown keys, raised sharps, and the four function keys (f1 play / stop, f3 / f5 octave, f7 panic).
 *  Laid out at 1000 units wide (the same drawing as the plugin); on a tall screen the extra height goes to the keys. */
public class SidView extends View implements Choreographer.FrameCallback {
    private static final int SW = 320, SH = 200;
    private static final float SX = 244, SY = 99, SS = 1.6f;   // where the screen sits in design units
    private static final int WHITE_KEYS = 15;                  // two octaves and a top C: big enough for fingers
    private static final int[] STRIPES = { 0xffd8342b, 0xffef8a2c, 0xfff2d23a, 0xff4aa64a, 0xff2f74c0 };
    private static final int BEIGE = 0xffcbbfad, BEIGE_DARK = 0xffb3a691, BEIGE_LINE = 0xff9c8f7b;
    private static final int KEY_BROWN = 0xff3f322b, KEY_BROWN_TOP = 0xff574639, KEY_TAUPE = 0xff7d7067, KEY_TAUPE_TOP = 0xff978a80;
    private static final String[] FKEY = { "PLAY", "OCT-", "OCT+", "PANIC" };

    private final int[] px = new int[SW * SH];
    private final Bitmap screen = Bitmap.createBitmap(SW, SH, Bitmap.Config.ARGB_8888);
    private Bitmap backdrop;          // everything that doesn't move
    private float scale = 1, offX = 0, offY = 0, dh = 900;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), crisp = new Paint();
    private final Path tube = new Path();
    private final RectF r = new RectF();
    private final RectF screenRect = new RectF(212, 70, 212 + 576, 70 + 366);
    private boolean running;
    private int frame;
    private int base = 48;                                     // lowest key: C3
    private final boolean[] down = new boolean[128];
    private final SparseIntArray pointerNote = new SparseIntArray();   // pointer id -> note (or -1)
    private final SparseIntArray pointerKind = new SparseIntArray();   // 1 screen, 2 keys, 3 function key
    private int fkeyDown = -1;

    public SidView(Context c) {
        super(c);
        crisp.setFilterBitmap(false);
        crisp.setAntiAlias(false);
        p.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        tube.addRoundRect(new RectF(212, 70, 788, 436), 34, 34, Path.Direction.CW);
        setKeepScreenOn(true);
    }

    void resume() { if (!running) { running = true; Choreographer.getInstance().postFrameCallback(this); } }
    void pause() { running = false; }

    @Override public void doFrame(long t) {
        if (!running) return;
        if ((frame++ & 1) == 0) {   // 30 frames a second is plenty for a C64
            Native.render(px);
            screen.setPixels(px, 0, SW, 0, 0, SW, SH);
            invalidate();
        }
        Choreographer.getInstance().postFrameCallback(this);
    }

    // ---------------- layout ----------------
    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        final float sw = w / 1000f, sh = h / 900f;
        if (sw <= sh) { scale = sw; dh = Math.min(h / scale, 1500); offX = 0; offY = (h - dh * scale) / 2; }
        else { scale = sh; dh = 900; offX = (w - 1000 * scale) / 2; offY = 0; }
        backdrop = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(backdrop);
        c.drawColor(0xff1b1714);
        c.translate(offX, offY);
        c.scale(scale, scale);
        drawRoom(c);
        drawMonitor(c);
        drawCase(c);
    }
    private RectF kWell() { return new RectF(40, 668, 40 + 772, dh - 30); }
    private RectF fWell() { return new RectF(842, 668, 842 + 118, dh - 30); }
    private RectF whiteRect(int i) {
        final RectF k = kWell(); final float w = (k.width() - 16) / WHITE_KEYS;
        return new RectF(k.left + 8 + i * w, k.top + 8, k.left + 8 + i * w + w - 4, k.bottom - 8);
    }
    private RectF blackRect(int whiteIndexLeft) {
        final RectF a = whiteRect(whiteIndexLeft); final float w = a.width();
        return new RectF(a.right - w * 0.3f, a.top - 2, a.right - w * 0.3f + w * 0.62f, a.top - 2 + a.height() * 0.58f);
    }
    private RectF fkeyRect(int i) {
        final RectF f = fWell(); final float h = (f.height() - 16) / 4;
        return new RectF(f.left + 10, f.top + 8 + i * h, f.right - 10, f.top + 8 + i * h + h - 8);
    }
    private static boolean isBlack(int n) { final int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    // ---------------- drawing ----------------
    private void drawRoom(Canvas c) {
        p.setShader(new LinearGradient(0, 0, 0, 560, 0xff2e2925, 0xff1b1714, Shader.TileMode.CLAMP));
        c.drawRect(-2000, -2000, 3000, 580, p);
        p.setShader(new LinearGradient(0, 580, 0, dh, 0xff5a3d29, 0xff3a281b, Shader.TileMode.CLAMP));
        c.drawRect(-2000, 580, 3000, dh + 2000, p);
        p.setShader(null);
    }
    private void rainbow(Canvas c, float x, float y, float w, float h) {
        final float sh = h / 5;
        for (int i = 0; i < 5; ++i) { p.setColor(STRIPES[i]); c.drawRect(x, y + i * sh, x + w, y + i * sh + sh * 0.72f, p); }
    }
    private void text(Canvas c, String s, float x, float y, float size, int color, Paint.Align a) {
        p.setTextSize(size); p.setColor(color); p.setTextAlign(a); c.drawText(s, x, y, p);
    }
    private void drawMonitor(Canvas c) {
        p.setColor(0x66000000); r.set(156, 28, 856, 560); c.drawRoundRect(r, 16, 16, p);
        p.setColor(0xff222222); c.drawRect(250, 548, 750, 562, p); c.drawRect(230, 560, 770, 570, p);
        p.setShader(new LinearGradient(0, 18, 0, 550, 0xffd8cdbc, BEIGE_DARK, Shader.TileMode.CLAMP));
        r.set(150, 18, 850, 550); c.drawRoundRect(r, 16, 16, p); p.setShader(null);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(1.5f); p.setColor(BEIGE_LINE);
        r.set(156, 24, 844, 544); c.drawRoundRect(r, 12, 12, p); c.drawLine(158, 470, 842, 470, p);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xff42484a); r.set(184, 44, 816, 462); c.drawRoundRect(r, 10, 10, p);
        p.setColor(0xff262624); r.set(198, 58, 802, 448); c.drawRoundRect(r, 8, 8, p);
        p.setColor(0xff6c5eb5); c.drawPath(tube, p);   // the light-blue border
        // badge strip
        p.setColor(0xff3b3d3a); r.set(382, 482, 618, 516); c.drawRoundRect(r, 3, 3, p);
        text(c, "omega", 392, 506, 18, 0xebffffff, Paint.Align.LEFT);
        rainbow(c, 500, 488, 76, 22);
        p.setColor(0xff2a2a28); c.drawRect(582, 486, 610, 512, p);
        p.setColor(0xff8ee05a); c.drawRect(589, 493, 603, 497, p);
        p.setStyle(Paint.Style.STROKE); p.setColor(0xff6e6458); c.drawRect(660, 486, 790, 512, p); p.setStyle(Paint.Style.FILL);
        text(c, "VIDEO MONITOR", 666, 497, 10, 0xff6e6458, Paint.Align.LEFT);
        text(c, "MODEL 6581", 666, 508, 10, 0xff6e6458, Paint.Align.LEFT);
        p.setColor(0xff4c4640); c.drawRect(800, 486, 826, 512, p);
        text(c, "POWER", 813, 526, 10, 0xff6e6458, Paint.Align.CENTER);
    }
    private void drawCase(Canvas c) {
        final float b = dh - 8;
        final Path bin = new Path();
        bin.moveTo(30, 596); bin.lineTo(970, 596); bin.quadTo(992, 600, 994, 640);
        bin.lineTo(996, b - 24); bin.quadTo(996, b, 972, b); bin.lineTo(28, b); bin.quadTo(4, b, 4, b - 24);
        bin.lineTo(6, 640); bin.quadTo(8, 600, 30, 596); bin.close();
        p.setColor(0x73000000); c.save(); c.translate(4, 8); c.drawPath(bin, p); c.restore();
        p.setShader(new LinearGradient(0, 596, 0, b, 0xffd2c7b6, 0xffaa9d88, Shader.TileMode.CLAMP)); c.drawPath(bin, p); p.setShader(null);
        p.setStrokeWidth(1.6f);
        for (int i = 0; i < 5; ++i) { p.setColor(BEIGE_LINE); c.drawLine(440, 606 + i * 9, 960, 606 + i * 9, p); p.setColor(0xffe0d6c8); c.drawLine(440, 608 + i * 9, 960, 608 + i * 9, p); }
        p.setColor(0xff8e8576); r.set(40, 610, 340, 644); c.drawRoundRect(r, 17, 17, p);
        p.setColor(0xff595249); r.set(44, 613, 336, 641); c.drawRoundRect(r, 14, 14, p);
        text(c, "omega", 62, 633, 17, 0xffded4c4, Paint.Align.LEFT);
        rainbow(c, 160, 617, 90, 20);
        text(c, "SID 64", 256, 633, 17, 0xffded4c4, Paint.Align.LEFT);
        p.setColor(0xff595249); r.set(842, 640, 960, 658); c.drawRoundRect(r, 9, 9, p);
        text(c, "POWER", 856, 653, 10, 0xffded4c4, Paint.Align.LEFT);
        p.setColor(0xffe0241a); c.drawCircle(940, 649, 6, p);
        p.setColor(0xff7d7262); r.set(kWell()); r.inset(-3, -3); c.drawRoundRect(r, 6, 6, p); r.set(fWell()); r.inset(-3, -3); c.drawRoundRect(r, 6, 6, p);
        p.setColor(0xff2c2622); c.drawRoundRect(kWell(), 5, 5, p); c.drawRoundRect(fWell(), 5, 5, p);
    }
    private void keycap(Canvas c, RectF k, int base, int top, boolean pressed) {
        final RectF a = new RectF(k); if (pressed) a.offset(0, 2);
        p.setColor(0x73000000); r.set(a); r.offset(0, pressed ? 1 : 4); c.drawRoundRect(r, 5, 5, p);
        p.setColor(base); c.drawRoundRect(a, 5, 5, p);
        final float ix = a.width() * 0.12f;
        r.set(a.left + ix, a.top + 3, a.right - ix, a.bottom - 3 - a.height() * 0.10f);
        p.setShader(new LinearGradient(0, r.top, 0, r.bottom, pressed ? top : brighten(top), darken(top), Shader.TileMode.CLAMP));
        c.drawRoundRect(r, 4, 4, p); p.setShader(null);
    }
    private static int brighten(int c) { return mix(c, 0xffffffff, 0.12f); }
    private static int darken(int c) { return mix(c, 0xff000000, 0.15f); }
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
        // the screen: crisp pixels, scanlines, the tube's darker edges and a reflection
        c.save();
        c.clipPath(tube);
        r.set(SX, SY, SX + SW * SS, SY + SH * SS);
        c.drawBitmap(screen, null, r, crisp);
        p.setColor(0x21000000);
        for (float y = 70; y < 436; y += SS) c.drawRect(212, y + SS * 0.55f, 788, y + SS, p);
        p.setShader(new RadialGradient(500, 253, 330, 0x00000000, 0x6b000000, Shader.TileMode.CLAMP)); c.drawPath(tube, p);
        p.setShader(new LinearGradient(250, 80, 420, 230, 0x1affffff, 0x00ffffff, Shader.TileMode.CLAMP));
        r.set(200, 60, 560, 260); c.drawOval(r, p); p.setShader(null);
        c.restore();
        drawKeys(c);
        c.restore();
    }
    private void drawKeys(Canvas c) {
        int wi = 0;
        for (int n = base; wi < WHITE_KEYS; ++n) {
            if (isBlack(n)) continue;
            final RectF k = whiteRect(wi++);
            keycap(c, k, KEY_BROWN, KEY_BROWN_TOP, down[n]);
            if (n % 12 == 0) text(c, "C" + (n / 12 - 1), k.centerX(), k.bottom - 12, 13, 0xccffffff, Paint.Align.CENTER);
        }
        wi = 0;
        for (int n = base; wi < WHITE_KEYS; ++n) {
            if (!isBlack(n)) { ++wi; continue; }
            keycap(c, blackRect(wi - 1), darken(darken(KEY_TAUPE)), KEY_TAUPE_TOP, down[n]);
        }
        final boolean playing = Native.seqRunning();
        for (int i = 0; i < 4; ++i) {
            final RectF k = fkeyRect(i);
            keycap(c, k, KEY_TAUPE, KEY_TAUPE_TOP, fkeyDown == i);
            text(c, "f" + (i * 2 + 1), k.centerX(), k.centerY() - 2, 15, 0xddffffff, Paint.Align.CENTER);
            text(c, i == 0 && playing ? "STOP" : FKEY[i], k.centerX(), k.centerY() + 16, 11, 0xddffffff, Paint.Align.CENTER);
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

    @Override public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        for (int i = 0; i < e.getPointerCount(); ++i) {
            final int id = e.getPointerId(i);
            final float x = (e.getX(i) - offX) / scale, y = (e.getY(i) - offY) / scale;
            final boolean isThis = e.getActionIndex() == i;
            if ((action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) && isThis) {
                if (screenRect.contains(x, y)) { pointerKind.put(id, 1); Native.touch(0, (x - SX) / SS, (y - SY) / SS); }
                else if (fkeyAt(x, y) >= 0) { pointerKind.put(id, 3); fkeyDown = fkeyAt(x, y); fkey(fkeyDown); }
                else { pointerKind.put(id, 2); setNote(id, noteAt(x, y), y); }
            } else if (action == MotionEvent.ACTION_MOVE) {
                final int kind = pointerKind.get(id, 0);
                if (kind == 1) Native.touch(1, (x - SX) / SS, (y - SY) / SS);
                else if (kind == 2) setNote(id, noteAt(x, y), y);   // slide across the keys
            } else if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) && (isThis || action == MotionEvent.ACTION_CANCEL)) {
                final int kind = pointerKind.get(id, 0);
                if (kind == 1) Native.touch(2, (x - SX) / SS, (y - SY) / SS);
                else if (kind == 2) { setNote(id, -1, y); pointerNote.delete(id); }
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
            if (screenRect.contains(x, y)) { Native.wheel((x - SX) / SS, (y - SY) / SS, e.getAxisValue(MotionEvent.AXIS_VSCROLL)); return true; }
        }
        return super.onGenericMotionEvent(e);
    }
}
