package com.thesnakedevice.wheel;

import android.content.ContentResolver;
import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Decodes a track (MediaExtractor + MediaCodec, or a small AIFF reader when the device has no AIFF extractor) to
 * stereo float PCM at the file's own rate, then hands it to the engine with Native.loadTrack. One load at a time
 * (two decoded tracks in flight would need too much memory); a newer load on the same deck cancels the older one.
 */
final class TrackLoader {
    static final int MAX_SECONDS = 20 * 60;

    private final Context ctx;
    private final ExecutorService exec = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "wheel-loader");
        t.setPriority(Thread.NORM_PRIORITY - 1);
        return t;
    });
    private final AtomicInteger[] gen = { new AtomicInteger(), new AtomicInteger() };
    /** Per deck: -1 idle, else 0..1 while loading. */
    final float[] progress = { -1f, -1f };
    final String[] loadingName = { null, null };
    /** Per deck: the last error (shown on the deck for a few seconds), or null. */
    final String[] error = { null, null };
    final long[] errorAt = { 0, 0 };

    TrackLoader(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    void shutdown() { exec.shutdownNow(); }

    boolean busy(int deck) { return progress[deck] >= 0; }

    void load(int deck, Uri uri, String path, String fileName, String title, String artist) {
        final int g = gen[deck].incrementAndGet();
        progress[deck] = 0f;
        loadingName[deck] = title != null && !title.isEmpty() ? title : fileName;
        error[deck] = null;
        exec.execute(() -> {
            if (gen[deck].get() != g) return;
            try {
                Pcm pcm = decode(deck, g, uri, fileName);
                if (pcm == null) return;   // cancelled
                if (gen[deck].get() != g) return;
                progress[deck] = .99f;
                String[] fb = fallbackTitle(fileName);
                String t = title != null && !title.isEmpty() ? title : fb[0];
                String a = artist != null && !artist.isEmpty() ? artist : fb[1];
                boolean ok;
                ParcelFileDescriptor pfd = null;
                try {
                    try { pfd = ctx.getContentResolver().openFileDescriptor(uri, "r"); } catch (Exception ignored) {}
                    ok = Native.loadTrack(deck, pcm.left, pcm.right, pcm.rate, pfd != null ? pfd.getFd() : -1, path, t, a);
                } finally {
                    if (pfd != null) try { pfd.close(); } catch (IOException ignored) {}
                }
                if (!ok) fail(deck, g, "CAN'T LOAD");
            } catch (OutOfMemoryError e) {
                fail(deck, g, "TOO BIG");
            } catch (Throwable e) {
                fail(deck, g, "CAN'T PLAY THIS FILE");
            } finally {
                if (gen[deck].get() == g) { progress[deck] = -1f; loadingName[deck] = null; }
            }
        });
    }

    private void fail(int deck, int g, String msg) {
        if (gen[deck].get() != g) return;
        error[deck] = msg;
        errorAt[deck] = android.os.SystemClock.uptimeMillis();
    }

    /** "Artist - Title.mp3" -> {title, artist}. */
    static String[] fallbackTitle(String fileName) {
        String n = fileName == null ? "" : fileName;
        int dot = n.lastIndexOf('.');
        if (dot > 0) n = n.substring(0, dot);
        int dash = n.indexOf(" - ");
        if (dash > 0) return new String[] { n.substring(dash + 3).trim(), n.substring(0, dash).trim() };
        return new String[] { n.trim(), "" };
    }

    static final class Pcm {
        float[] left, right;
        int rate;
    }

    /** Growable stereo float buffer, trimmed to exact length at the end (the engine takes whole arrays). */
    private static final class Acc {
        float[] l, r;
        int n;
        final int cap;
        Acc(int initial, int cap) { int c = Math.max(4096, Math.min(initial, cap)); l = new float[c]; r = new float[c]; this.cap = cap; }
        boolean full() { return n >= cap; }
        void ensure(int more) {
            if (n + more <= l.length) return;
            int c = (int) Math.min((long) cap, Math.max((long) n + more, (long) l.length * 3 / 2));
            if (c < n + more) c = n + more;
            l = Arrays.copyOf(l, c);
            r = Arrays.copyOf(r, c);
        }
        Pcm finish(int rate) {
            Pcm p = new Pcm();
            p.rate = rate;
            if (n == l.length) { p.left = l; p.right = r; }
            else {
                float[] a = Arrays.copyOf(l, n); l = null;   // one channel at a time keeps the peak at 1.5x
                float[] b = Arrays.copyOf(r, n); r = null;
                p.left = a; p.right = b;
            }
            return p;
        }
    }

    private Pcm decode(int deck, int g, Uri uri, String name) throws Exception {
        String lower = name == null ? "" : name.toLowerCase(java.util.Locale.ROOT);
        boolean aiff = lower.endsWith(".aif") || lower.endsWith(".aiff") || lower.endsWith(".aifc");
        MediaExtractor ex = new MediaExtractor();
        int track = -1;
        try {
            ex.setDataSource(ctx, uri, null);
            for (int i = 0; i < ex.getTrackCount(); ++i) {
                String mime = ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) { track = i; break; }
            }
        } catch (Exception e) {
            track = -1;
        }
        if (track < 0) {
            ex.release();
            if (aiff) return decodeAiff(deck, g, uri);
            throw new IOException("no audio track");
        }
        try {
            return decodeCodec(deck, g, ex, track);
        } catch (Exception e) {
            if (aiff) return decodeAiff(deck, g, uri);
            throw e;
        } finally {
            ex.release();
        }
    }

    private Pcm decodeCodec(int deck, int g, MediaExtractor ex, int track) throws Exception {
        ex.selectTrack(track);
        MediaFormat fmt = ex.getTrackFormat(track);
        String mime = fmt.getString(MediaFormat.KEY_MIME);
        int rate = fmt.containsKey(MediaFormat.KEY_SAMPLE_RATE) ? fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE) : 44100;
        int chans = fmt.containsKey(MediaFormat.KEY_CHANNEL_COUNT) ? fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT) : 2;
        long durUs = fmt.containsKey(MediaFormat.KEY_DURATION) ? fmt.getLong(MediaFormat.KEY_DURATION) : 0;
        long maxUs = (long) MAX_SECONDS * 1000000L;
        int enc = AudioFormat.ENCODING_PCM_16BIT;
        MediaCodec codec = MediaCodec.createDecoderByType(mime);
        Acc acc = null;
        try {
            codec.configure(fmt, null, null, 0);
            codec.start();
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inDone = false, outDone = false;
            int idle = 0;
            while (!outDone) {
                if (gen[deck].get() != g) return null;
                if (!inDone) {
                    int ii = codec.dequeueInputBuffer(5000);
                    if (ii >= 0) {
                        ByteBuffer ib = codec.getInputBuffer(ii);
                        int sz = ib == null ? -1 : ex.readSampleData(ib, 0);
                        long t = ex.getSampleTime();
                        if (sz < 0 || t > maxUs) {
                            codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inDone = true;
                        } else {
                            codec.queueInputBuffer(ii, 0, sz, Math.max(0, t), 0);
                            ex.advance();
                            if (durUs > 0) progress[deck] = Math.min(.98f, (float) t / durUs);
                        }
                    }
                }
                int oi = codec.dequeueOutputBuffer(info, 5000);
                if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat of = codec.getOutputFormat();
                    if (of.containsKey(MediaFormat.KEY_SAMPLE_RATE)) rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                    if (of.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) chans = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                    if (of.containsKey(MediaFormat.KEY_PCM_ENCODING)) enc = of.getInteger(MediaFormat.KEY_PCM_ENCODING);
                    continue;
                }
                if (oi < 0) {
                    if (inDone && ++idle > 400) break;   // a decoder that never signals the end (2 s of nothing)
                    continue;
                }
                idle = 0;
                if (acc == null) {
                    long est = durUs > 0 ? durUs * rate / 1000000L + rate : (long) rate * 300;
                    acc = new Acc((int) Math.min(est, (long) MAX_SECONDS * rate), MAX_SECONDS * rate);
                }
                ByteBuffer ob = codec.getOutputBuffer(oi);
                if (ob != null && info.size > 0 && chans > 0 && !acc.full()) {
                    ob.position(info.offset);
                    ob.limit(info.offset + info.size);
                    ob = ob.slice().order(ByteOrder.nativeOrder());
                    append(acc, ob, enc, chans);
                }
                codec.releaseOutputBuffer(oi, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0 || acc.full()) outDone = true;
            }
        } finally {
            try { codec.stop(); } catch (Exception ignored) {}
            codec.release();
        }
        if (acc == null || acc.n == 0) throw new IOException("nothing decoded");
        return acc.finish(rate);
    }

    private static void append(Acc acc, ByteBuffer b, int enc, int chans) {
        int bps = enc == AudioFormat.ENCODING_PCM_FLOAT ? 4 : enc == AudioFormat.ENCODING_PCM_8BIT ? 1
                : enc == AudioFormat.ENCODING_PCM_32BIT ? 4 : enc == AudioFormat.ENCODING_PCM_24BIT_PACKED ? 3 : 2;
        int frames = b.remaining() / (bps * chans);
        frames = Math.min(frames, acc.cap - acc.n);
        if (frames <= 0) return;
        acc.ensure(frames);
        float[] l = acc.l, r = acc.r;
        int n = acc.n;
        int rc = chans > 1 ? 1 : 0;
        for (int f = 0; f < frames; ++f) {
            int base = f * chans * bps;
            float a = sample(b, base, enc), c = rc == 0 ? a : sample(b, base + bps, enc);
            l[n + f] = a; r[n + f] = c;
        }
        acc.n = n + frames;
    }

    private static float sample(ByteBuffer b, int at, int enc) {
        switch (enc) {
            case AudioFormat.ENCODING_PCM_FLOAT: return b.getFloat(at);
            case AudioFormat.ENCODING_PCM_8BIT: return ((b.get(at) & 0xff) - 128) / 128f;
            case AudioFormat.ENCODING_PCM_32BIT: return b.getInt(at) / 2147483648f;
            case AudioFormat.ENCODING_PCM_24BIT_PACKED: {
                int v = b.order() == ByteOrder.LITTLE_ENDIAN
                        ? (b.get(at) & 0xff) | ((b.get(at + 1) & 0xff) << 8) | (b.get(at + 2) << 16)
                        : (b.get(at + 2) & 0xff) | ((b.get(at + 1) & 0xff) << 8) | (b.get(at) << 16);
                return v / 8388608f;
            }
            default: return b.getShort(at) / 32768f;
        }
    }

    // ---- AIFF / AIFC (uncompressed: NONE, twos, sowt, fl32, in32), for devices whose extractor doesn't read AIFF
    private Pcm decodeAiff(int deck, int g, Uri uri) throws IOException {
        ContentResolver cr = ctx.getContentResolver();
        InputStream raw = cr.openInputStream(uri);
        if (raw == null) throw new IOException("can't open");
        try (DataInputStream in = new DataInputStream(new BufferedInputStream(raw, 1 << 16))) {
            if (!"FORM".equals(id4(in))) throw new IOException("not IFF");
            in.readInt();
            String form = id4(in);
            boolean aifc = "AIFC".equals(form);
            if (!aifc && !"AIFF".equals(form)) throw new IOException("not AIFF");
            int chans = 0, bits = 16; long frames = 0; double rate = 0; String comp = "NONE";
            boolean haveComm = false;
            while (true) {
                String id;
                try { id = id4(in); } catch (EOFException e) { break; }
                long size = in.readInt() & 0xffffffffL;
                if ("COMM".equals(id)) {
                    chans = in.readShort();
                    frames = in.readInt() & 0xffffffffL;
                    bits = in.readShort();
                    byte[] ext = new byte[10];
                    in.readFully(ext);
                    rate = extended(ext);
                    long used = 18;
                    if (aifc && size >= 22) { comp = id4(in); used += 4; }
                    skip(in, size - used + (size & 1));
                    haveComm = true;
                } else if ("SSND".equals(id)) {
                    if (!haveComm || chans <= 0 || rate <= 0) throw new IOException("SSND before COMM");
                    long off = in.readInt() & 0xffffffffL;
                    in.readInt();
                    skip(in, off);
                    int sr = (int) Math.round(rate);
                    boolean le = "sowt".equals(comp);
                    boolean flt = "fl32".equalsIgnoreCase(comp);
                    if (!le && !flt && !"NONE".equals(comp) && !"twos".equals(comp) && !"in32".equals(comp)) throw new IOException("compressed AIFC");
                    int bps = flt ? 4 : (bits + 7) / 8;
                    long avail = (size - 8 - off) / ((long) bps * chans);
                    long n = Math.min(Math.min(frames > 0 ? frames : avail, avail), (long) MAX_SECONDS * sr);
                    Acc acc = new Acc((int) n, (int) n);
                    byte[] buf = new byte[bps * chans * 4096];
                    long done = 0;
                    while (done < n) {
                        if (gen[deck].get() != g) return null;
                        int want = (int) Math.min(4096, n - done);
                        in.readFully(buf, 0, want * bps * chans);
                        for (int f = 0; f < want; ++f) {
                            int base = f * bps * chans;
                            float a = aiffSample(buf, base, bps, le, flt);
                            float c = chans > 1 ? aiffSample(buf, base + bps, bps, le, flt) : a;
                            acc.l[acc.n] = a; acc.r[acc.n] = c; acc.n++;
                        }
                        done += want;
                        progress[deck] = Math.min(.98f, (float) done / n);
                    }
                    if (acc.n == 0) throw new IOException("empty");
                    return acc.finish(sr);
                } else {
                    skip(in, size + (size & 1));
                }
            }
            throw new IOException("no sound data");
        }
    }

    private static float aiffSample(byte[] b, int at, int bps, boolean le, boolean flt) {
        if (flt) {
            int v = ((b[at] & 0xff) << 24) | ((b[at + 1] & 0xff) << 16) | ((b[at + 2] & 0xff) << 8) | (b[at + 3] & 0xff);
            return Float.intBitsToFloat(v);
        }
        switch (bps) {
            case 1: return b[at] / 128f;
            case 2: return (short) (le ? ((b[at] & 0xff) | (b[at + 1] << 8)) : ((b[at + 1] & 0xff) | (b[at] << 8))) / 32768f;
            case 3: return ((b[at] << 16) | ((b[at + 1] & 0xff) << 8) | (b[at + 2] & 0xff)) / 8388608f;
            default: return (((b[at] & 0xff) << 24) | ((b[at + 1] & 0xff) << 16) | ((b[at + 2] & 0xff) << 8) | (b[at + 3] & 0xff)) / 2147483648f;
        }
    }

    private static String id4(DataInputStream in) throws IOException {
        byte[] b = new byte[4];
        in.readFully(b);
        return new String(b, java.nio.charset.StandardCharsets.ISO_8859_1);
    }

    private static void skip(DataInputStream in, long n) throws IOException {
        while (n > 0) {
            long s = in.skip(n);
            if (s <= 0) { if (in.read() < 0) throw new EOFException(); s = 1; }
            n -= s;
        }
    }

    /** IEEE 754 80-bit extended (the AIFF sample rate) to double. */
    static double extended(byte[] b) {
        int exp = ((b[0] & 0x7f) << 8) | (b[1] & 0xff);
        long hi = ((long) (b[2] & 0xff) << 24) | ((b[3] & 0xff) << 16) | ((b[4] & 0xff) << 8) | (b[5] & 0xff);
        long lo = ((long) (b[6] & 0xff) << 24) | ((b[7] & 0xff) << 16) | ((b[8] & 0xff) << 8) | (b[9] & 0xff);
        if (exp == 0 && hi == 0 && lo == 0) return 0;
        double v = Math.scalb((double) hi, exp - 16383 - 31) + Math.scalb((double) lo, exp - 16383 - 63);
        return (b[0] & 0x80) != 0 ? -v : v;
    }
}
