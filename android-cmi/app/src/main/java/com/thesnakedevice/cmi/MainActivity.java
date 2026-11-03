package com.thesnakedevice.cmi;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;
import java.util.ArrayList;
import java.util.List;

/** Omega CMI: the screen, MIDI in (USB and Bluetooth), the audio engine's life cycle, sampling from files. */
public class MainActivity extends Activity {
    static final int PICK = 41;
    private CmiView view;
    private final List<MidiDevice> devices = new ArrayList<>();
    private final List<MidiOutputPort> ports = new ArrayList<>();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private int pickSlot = 0;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        loadState();
        view = new CmiView(this);
        setContentView(view);
        hideBars();
        openMidi();
    }

    private void hideBars() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override protected void onResume() { super.onResume(); Native.start(); hideBars(); }
    @Override protected void onPause() { saveState(); Native.allOff(); Native.stop(); super.onPause(); }
    @Override protected void onDestroy() {
        for (MidiOutputPort p : ports) try { p.close(); } catch (Exception ignored) {}
        for (MidiDevice d : devices) try { d.close(); } catch (Exception ignored) {}
        super.onDestroy();
    }

    // ---- state ----
    private File stateFile() { return new File(getFilesDir(), "cmi-state.bin"); }
    void saveState() {
        try (FileOutputStream o = new FileOutputStream(stateFile())) { o.write(Native.saveState()); } catch (Exception ignored) {}
    }
    private void loadState() {
        File f = stateFile();
        if (!f.exists()) return;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int n = 0; while (n < b.length) { int r = in.read(b, n, b.length - n); if (r < 0) break; n += r; }
            Native.loadState(b);
        } catch (Exception ignored) {}
    }

    // ---- MIDI: every device with an output port feeds the engine ----
    private void openMidi() {
        if (!getPackageManager().hasSystemFeature("android.software.midi")) return;
        final MidiManager mm = (MidiManager) getSystemService(Context.MIDI_SERVICE);
        if (mm == null) return;
        for (MidiDeviceInfo info : mm.getDevices()) connect(mm, info);
        mm.registerDeviceCallback(new MidiManager.DeviceCallback() {
            @Override public void onDeviceAdded(MidiDeviceInfo info) { connect(mm, info); }
        }, ui);
    }
    private void connect(MidiManager mm, final MidiDeviceInfo info) {
        if (info.getOutputPortCount() == 0) return;
        mm.openDevice(info, device -> {
            if (device == null) return;
            devices.add(device);
            for (int i = 0; i < info.getOutputPortCount(); ++i) {
                MidiOutputPort port = device.openOutputPort(i);
                if (port == null) continue;
                ports.add(port);
                port.connect(new MidiReceiver() {
                    @Override public void onSend(byte[] msg, int offset, int count, long timestamp) { Native.midi(msg, offset, count); }
                });
            }
            if (view != null) view.status("MIDI: " + info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME, "device"));
        }, ui);
    }

    // ---- Page 8: decode any audio file the system can read, into a user voice ----
    void pickSample(int slot) {
        pickSlot = slot;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("audio/*");
        startActivityForResult(i, PICK);
    }
    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != PICK || res != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        final int slot = pickSlot, root = view.rootKey; final boolean loop = view.loopSample;
        view.status("SAMPLING...");
        new Thread(() -> {
            String msg;
            try {
                int[] rate = new int[1];
                float[] mono = decode(uri, rate, 10.0);
                String name = uri.getLastPathSegment();
                if (name == null) name = "SAMPLE";
                int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
                int dot = name.lastIndexOf('.'); if (dot > 0) name = name.substring(0, dot);
                String err = Native.loadSample(mono, rate[0], name, slot, root, loop);
                msg = err.isEmpty() ? "SAMPLED INTO USER" + (slot + 1) : err;
            } catch (Exception e) { msg = "CAN'T READ FILE"; }
            final String m = msg;
            ui.post(() -> view.status(m));
        }).start();
    }

    private float[] decode(Uri uri, int[] rateOut, double maxSec) throws Exception {
        MediaExtractor ex = new MediaExtractor();
        ex.setDataSource(this, uri, null);
        int track = -1; MediaFormat fmt = null;
        for (int i = 0; i < ex.getTrackCount(); ++i) {
            MediaFormat f = ex.getTrackFormat(i);
            String mime = f.getString(MediaFormat.KEY_MIME);
            if (mime != null && mime.startsWith("audio/")) { track = i; fmt = f; break; }
        }
        if (track < 0) throw new Exception("no audio");
        ex.selectTrack(track);
        int rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE), chans = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
        MediaCodec codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
        codec.configure(fmt, null, null, 0);
        codec.start();
        int max = (int) (rate * maxSec);
        float[] out = new float[max]; int n = 0;
        MediaCodec.BufferInfo bi = new MediaCodec.BufferInfo();
        boolean inDone = false, outDone = false;
        while (!outDone && n < max) {
            if (!inDone) {
                int ii = codec.dequeueInputBuffer(10000);
                if (ii >= 0) {
                    ByteBuffer ib = codec.getInputBuffer(ii);
                    int sz = ex.readSampleData(ib, 0);
                    if (sz < 0) { codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inDone = true; }
                    else { codec.queueInputBuffer(ii, 0, sz, ex.getSampleTime(), 0); ex.advance(); }
                }
            }
            int oi = codec.dequeueOutputBuffer(bi, 10000);
            if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                MediaFormat of = codec.getOutputFormat();
                rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE); chans = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            } else if (oi >= 0) {
                ByteBuffer ob = codec.getOutputBuffer(oi);
                if (ob != null && bi.size > 0) {
                    ob.position(bi.offset); ob.limit(bi.offset + bi.size);
                    ShortBuffer sb = ob.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer();
                    int frames = sb.remaining() / Math.max(1, chans);
                    for (int f = 0; f < frames && n < max; ++f) {
                        float s = 0; for (int c = 0; c < chans; ++c) s += sb.get(f * chans + c);
                        out[n++] = s / (32768f * chans);
                    }
                }
                codec.releaseOutputBuffer(oi, false);
                if ((bi.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
            }
        }
        codec.stop(); codec.release(); ex.release();
        rateOut[0] = rate;
        float[] r = new float[n]; System.arraycopy(out, 0, r, 0, n);
        return r;
    }
}
