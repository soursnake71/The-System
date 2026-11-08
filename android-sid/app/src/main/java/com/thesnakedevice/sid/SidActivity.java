package com.thesnakedevice.sid;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Omega SID 64: the C64 monitor and breadbin keyboard, full screen. MIDI keyboards plugged in by USB (or paired over
 *  Bluetooth MIDI) play it too. The patch, patterns and sequencer settings are kept between launches. */
public class SidActivity extends Activity {
    private SidView view;
    private Sampling sampling;
    private MidiManager midi;
    private final List<MidiDevice> openDevices = new ArrayList<>();
    private final List<MidiOutputPort> openPorts = new ArrayList<>();
    private final List<android.media.midi.MidiInputPort> sendPorts = new ArrayList<>();
    private final long[] outTimes = new long[256]; private final byte[] outBytes = new byte[768]; private final int[] outLens = new int[256];
    /** Called every frame: the sequencer's notes and clock, out to every device that takes MIDI. */
    void sendMidiOut() {
        final int n = Native.midiOut(outTimes, outBytes, outLens);
        if (n == 0 || sendPorts.isEmpty()) return;
        for (int i = 0; i < n; ++i)
            for (android.media.midi.MidiInputPort p : sendPorts) { try { p.send(outBytes, i * 3, outLens[i], outTimes[i]); } catch (IOException ignored) { } }
    }

    private final MidiReceiver receiver = new MidiReceiver() {
        @Override public void onSend(byte[] data, int offset, int count, long timestamp) { Native.midi(data, offset, count); }
    };

    // ---- rotation: free (both ways up), or held the way the phone is turned; remembered ----
    boolean orientationLocked() { return prefs().getInt("lockedOrientation", -99) != -99; }
    void toggleOrientationLock() {
        if (orientationLocked()) {
            prefs().edit().remove("lockedOrientation").apply();
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_USER);
        } else {
            final int o = currentOrientation();
            prefs().edit().putInt("lockedOrientation", o).apply();
            setRequestedOrientation(o);
        }
    }
    /** the orientation the screen is in now, as an ActivityInfo constant (so it can be held after a restart) */
    private int currentOrientation() {
        final int rot = getWindowManager().getDefaultDisplay().getRotation();
        final boolean land = getResources().getConfiguration().orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE;
        if (land) return rot == android.view.Surface.ROTATION_270 ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        return rot == android.view.Surface.ROTATION_180 ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
    }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        setRequestedOrientation(prefs().getInt("lockedOrientation", android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_USER));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Native.init(prefs().getString("state", ""));
        sampling = new Sampling(this);
        view = new SidView(this, sampling);
        setContentView(view);
        hideBars();
        if (getPackageManager().hasSystemFeature("android.software.midi")) {
            midi = (MidiManager) getSystemService(Context.MIDI_SERVICE);
            if (midi != null) {
                for (MidiDeviceInfo info : midi.getDevices()) openMidi(info);
                midi.registerDeviceCallback(new MidiManager.DeviceCallback() {
                    @Override public void onDeviceAdded(MidiDeviceInfo info) { openMidi(info); }
                }, new Handler(Looper.getMainLooper()));
            }
        }
    }

    private SharedPreferences prefs() { return getSharedPreferences("sid", MODE_PRIVATE); }

    private void hideBars() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            final WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /** Listen to every output port of a MIDI device (its notes come to us). */
    private void openMidi(MidiDeviceInfo info) {
        if ((info.getOutputPortCount() == 0 && info.getInputPortCount() == 0) || midi == null) return;
        midi.openDevice(info, device -> {
            if (device == null) return;
            openDevices.add(device);
            for (int p = 0; p < info.getOutputPortCount(); ++p) {
                final MidiOutputPort port = device.openOutputPort(p);
                if (port != null) { port.connect(receiver); openPorts.add(port); }
            }
            for (int p = 0; p < info.getInputPortCount(); ++p) {
                final android.media.midi.MidiInputPort port = device.openInputPort(p);
                if (port != null) sendPorts.add(port);
            }
        }, new Handler(Looper.getMainLooper()));
    }

    @Override protected void onResume() { super.onResume(); hideBars(); Native.start(); view.resume(); }
    @Override protected void onPause() {
        view.pause();
        prefs().edit().putString("state", Native.saveState()).apply();
        Native.stop();
        super.onPause();
    }
    @Override protected void onActivityResult(int req, int res, android.content.Intent data) {
        super.onActivityResult(req, res, data);
        if (req == Sampling.PICK && res == RESULT_OK && data != null) sampling.onPicked(data.getData());
        if ((req == Sampling.MIDI_SAVE || req == Sampling.MIDI_OPEN) && res == RESULT_OK && data != null) sampling.onMidiFile(req, data.getData());
    }
    @Override public void onRequestPermissionsResult(int req, String[] perms, int[] results) {
        super.onRequestPermissionsResult(req, perms, results);
        if (req == Sampling.PERMISSION) sampling.onPermission(results.length > 0 && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED);
    }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (focus) hideBars(); }
    @Override protected void onDestroy() {
        for (MidiOutputPort p : openPorts) { try { p.disconnect(receiver); p.close(); } catch (IOException ignored) { } }
        for (android.media.midi.MidiInputPort p : sendPorts) { try { p.close(); } catch (IOException ignored) { } }
        for (MidiDevice d : openDevices) { try { d.close(); } catch (IOException ignored) { } }
        super.onDestroy();
    }
}
