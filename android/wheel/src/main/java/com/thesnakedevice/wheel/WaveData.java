package com.thesnakedevice.wheel;

import android.graphics.Bitmap;
import android.util.LruCache;

/**
 * A deck's waveform (150 columns a second from the engine: low / mid / high / peak bands) turned into Serato-style
 * colours (red lows, green mids, blue highs, brightness from the level), rendered lazily into 512-column bitmap tiles
 * that the scrolling view stretches to the zoom, plus a whole-track overview bitmap.
 */
final class WaveData {
    static final double COLS_PER_SEC = 150;
    static final int TILE = 512;

    int n;
    private int[] outer, core;
    private byte[] amp;
    private int tileH;
    private final LruCache<Integer, Bitmap> tiles = new LruCache<>(28);
    private Bitmap overview;
    private int ovW, ovH;

    void clear() { n = 0; outer = core = null; amp = null; tiles.evictAll(); overview = null; }

    void set(byte[] lo, byte[] mi, byte[] hi, byte[] pk) {
        tiles.evictAll();
        overview = null;
        if (lo == null || mi == null || hi == null) { n = 0; return; }
        int len = Math.min(lo.length, Math.min(mi.length, hi.length));
        outer = new int[len]; core = new int[len]; amp = new byte[len];
        for (int i = 0; i < len; ++i) {
            float l = (lo[i] & 0xff) / 255f, m = (mi[i] & 0xff) / 255f, h = (hi[i] & 0xff) / 255f;
            float p = pk != null && i < pk.length ? (pk[i] & 0xff) / 255f : Math.max(l, Math.max(m, h));
            float r = l, g = m * .92f, b = h * 1.1f;
            float mx = Math.max(Math.max(r, g), Math.max(b, 1e-3f));
            float bright = .42f + .58f * (float) Math.sqrt(p);
            float cr = r / mx * bright, cg = g / mx * bright, cb = Math.min(1f, b / mx) * bright;
            outer[i] = rgb(cr, cg, cb);
            float w = .35f + .3f * h;
            core[i] = rgb(cr + (1 - cr) * w, cg + (1 - cg) * w, cb + (1 - cb) * w);
            amp[i] = (byte) Math.max(0, Math.min(255, (int) (Math.pow(p, .8) * 255 + .5)));
        }
        n = len;
    }

    private static int rgb(float r, float g, float b) {
        return 0xff000000 | (clamp255(r) << 16) | (clamp255(g) << 8) | clamp255(b);
    }
    private static int clamp255(float v) { return Math.max(0, Math.min(255, (int) (v * 255 + .5f))); }

    /** Tile k (columns k*TILE ..), height h px, transparent background. */
    Bitmap tile(int k, int h) {
        if (n == 0 || k < 0 || k * TILE >= n || h < 4) return null;
        if (h != tileH) { tiles.evictAll(); tileH = h; }
        Bitmap b = tiles.get(k);
        if (b != null) return b;
        int[] px = new int[TILE * h];
        float half = h / 2f;
        for (int x = 0; x < TILE; ++x) {
            int i = k * TILE + x;
            if (i >= n) break;
            float a = (amp[i] & 0xff) / 255f * half;
            if (a < .5f) continue;
            int top = (int) (half - a), bot = (int) Math.ceil(half + a);
            float ca = a * .45f;
            int ctop = (int) (half - ca), cbot = (int) Math.ceil(half + ca);
            int oc = outer[i], cc = core[i];
            for (int y = Math.max(0, top); y < Math.min(h, bot); ++y) px[y * TILE + x] = (y >= ctop && y < cbot) ? cc : oc;
        }
        b = Bitmap.createBitmap(px, TILE, h, Bitmap.Config.ARGB_8888);
        tiles.put(k, b);
        return b;
    }

    /** The whole track squeezed into w x h. */
    Bitmap overview(int w, int h) {
        if (n == 0 || w < 4 || h < 4) return null;
        if (overview != null && ovW == w && ovH == h) return overview;
        int[] px = new int[w * h];
        float half = h / 2f;
        for (int x = 0; x < w; ++x) {
            int i0 = (int) ((long) x * n / w), i1 = Math.max(i0 + 1, (int) ((long) (x + 1) * n / w));
            int best = i0, bestA = -1;
            for (int i = i0; i < i1 && i < n; ++i) { int a = amp[i] & 0xff; if (a > bestA) { bestA = a; best = i; } }
            float a = Math.max(0, bestA) / 255f * half;
            int top = (int) (half - a), bot = (int) Math.ceil(half + a);
            int c = outer[best];
            for (int y = Math.max(0, top); y < Math.min(h, bot); ++y) px[y * w + x] = c;
        }
        overview = Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
        ovW = w; ovH = h;
        return overview;
    }
}
