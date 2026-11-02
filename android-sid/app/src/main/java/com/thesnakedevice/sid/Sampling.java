package com.thesnakedevice.sid;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaRecorder;
import android.net.Uri;
import android.provider.OpenableColumns;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;

/** Getting sound into the C64: the microphone (AudioRecord) and audio files (the system picker, decoded with
 *  MediaCodec, so WAV, MP3, AAC, FLAC, Ogg... whatever the phone plays). Up to 20 s recorded, 60 s from a file;
 *  the native side converts it to the sample rate and bit depth chosen on the SMP page. */
final class Sampling {
    static final int PICK = 64, PERMISSION = 65;
    private static final int RATE = 44100;
    private final Activity act;
    private volatile boolean recording;
    private Thread recThread;

    Sampling(Activity a) { act = a; }

    /** Called every frame by the view: act on what the screen's LOAD / REC buttons asked for. */
    void poll() {
        switch (Native.pollRequest()) {
            case 1: openFile(); break;
            case 2: startRecording(); break;
            case 3: stopRecording(); break;
            default: break;
        }
    }

    private void openFile() {
        final Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("audio/*");
        act.startActivityForResult(i, PICK);
    }

    void onPicked(Uri uri) {
        if (uri == null) return;
        final String name = displayName(uri);
        new Thread(() -> {
            final float[] mono = decode(uri);
            act.runOnUiThread(() -> { if (mono != null && mono.length > 0) Native.setSample(mono, decodedRate, name); });
        }, "sid-decode").start();
    }

    private String displayName(Uri uri) {
        String n = "SAMPLE";
        try (Cursor c = act.getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) n = c.getString(0);
        } catch (Exception ignored) { }
        final int dot = n.lastIndexOf('.');
        if (dot > 0) n = n.substring(0, dot);
        n = n.toUpperCase();
        return n.length() > 13 ? n.substring(0, 13) : n;
    }

    private int decodedRate = RATE;
    /** Decode a file to mono floats (null if it can't be read). */
    private float[] decode(Uri uri) {
        final MediaExtractor ex = new MediaExtractor();
        MediaCodec codec = null;
        try {
            ex.setDataSource(act, uri, null);
            int track = -1; MediaFormat fmt = null;
            for (int t = 0; t < ex.getTrackCount(); ++t) {
                final MediaFormat f = ex.getTrackFormat(t);
                final String mime = f.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) { track = t; fmt = f; break; }
            }
            if (track < 0) return null;
            ex.selectTrack(track);
            int rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE), ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
            codec.configure(fmt, null, null, 0);
            codec.start();
            final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            float[] out = new float[rate * 4]; int n = 0; final int max = rate * 60;
            boolean inDone = false, outDone = false;
            while (!outDone && n < max) {
                if (!inDone) {
                    final int ib = codec.dequeueInputBuffer(10000);
                    if (ib >= 0) {
                        final ByteBuffer buf = codec.getInputBuffer(ib);
                        final int sz = ex.readSampleData(buf, 0);
                        if (sz < 0) { codec.queueInputBuffer(ib, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                        else { codec.queueInputBuffer(ib, 0, sz, ex.getSampleTime(), 0); ex.advance(); }
                    }
                }
                final int ob = codec.dequeueOutputBuffer(info, 10000);
                if (ob == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    final MediaFormat of = codec.getOutputFormat();
                    rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE); ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                } else if (ob >= 0) {
                    final ByteBuffer bb = codec.getOutputBuffer(ob);
                    if (bb != null && info.size > 0) {
                        bb.position(info.offset); bb.limit(info.offset + info.size);
                        final ShortBuffer sb = bb.order(ByteOrder.nativeOrder()).asShortBuffer();
                        final int frames = sb.remaining() / Math.max(1, ch);
                        if (n + frames > out.length) out = java.util.Arrays.copyOf(out, Math.max(out.length * 2, n + frames));
                        for (int f = 0; f < frames && n < max; ++f) {
                            float s = 0; for (int c = 0; c < ch; ++c) s += sb.get() / 32768f;
                            out[n++] = s / ch;
                        }
                    }
                    codec.releaseOutputBuffer(ob, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                }
            }
            decodedRate = rate;
            return java.util.Arrays.copyOf(out, n);
        } catch (Exception e) {
            return null;
        } finally {
            if (codec != null) { try { codec.stop(); } catch (Exception ignored) { } codec.release(); }
            ex.release();
        }
    }

    private void startRecording() {
        if (act.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            act.requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, PERMISSION);
            return;
        }
        if (recording) return;
        final int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT);
        // the raw microphone if the phone offers it (no noise suppression or gain riding), else the usual one
        AudioRecord r = null;
        for (int src : new int[] { MediaRecorder.AudioSource.UNPROCESSED, MediaRecorder.AudioSource.MIC }) {
            try {
                r = new AudioRecord(src, RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT, Math.max(min, 8192));
                if (r.getState() == AudioRecord.STATE_INITIALIZED) break;
                r.release(); r = null;
            } catch (SecurityException | IllegalArgumentException e) { r = null; }
        }
        if (r == null) return;
        final AudioRecord rec = r;
        recording = true;
        Native.setRecordingState(true, 0);
        recThread = new Thread(() -> {
            final float[] all = new float[RATE * 20]; final float[] chunk = new float[1024]; int n = 0;
            rec.startRecording();
            while (recording && n < all.length) {
                final int got = rec.read(chunk, 0, chunk.length, AudioRecord.READ_BLOCKING);
                if (got <= 0) break;
                final int k = Math.min(got, all.length - n);
                System.arraycopy(chunk, 0, all, n, k); n += k;
                Native.setRecordingState(true, n / (float) RATE);
            }
            rec.stop(); rec.release(); recording = false;
            final float[] take = java.util.Arrays.copyOf(all, n);
            act.runOnUiThread(() -> { Native.setRecordingState(false, 0); if (take.length > RATE / 20) Native.setSample(take, RATE, "RECORDING"); });
        }, "sid-record");
        recThread.start();
    }
    private void stopRecording() { recording = false; }

    void onPermission(boolean granted) { if (granted) startRecording(); }
}
