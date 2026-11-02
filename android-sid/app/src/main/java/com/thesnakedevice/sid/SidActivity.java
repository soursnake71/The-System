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
    private MidiManager midi;
    private final List<MidiDevice> openDevices = new ArrayList<>();
    private final List<MidiOutputPort> openPorts = new ArrayList<>();

    private final MidiReceiver receiver = new MidiReceiver() {
        @Override public void onSend(byte[] data, int offset, int count, long timestamp) { Native.midi(data, offset, count); }
    };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Native.init(prefs().getString("state", ""));
        view = new SidView(this);
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
        if (info.getOutputPortCount() == 0 || midi == null) return;
        midi.openDevice(info, device -> {
            if (device == null) return;
            openDevices.add(device);
            for (int p = 0; p < info.getOutputPortCount(); ++p) {
                final MidiOutputPort port = device.openOutputPort(p);
                if (port != null) { port.connect(receiver); openPorts.add(port); }
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
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (focus) hideBars(); }
    @Override protected void onDestroy() {
        for (MidiOutputPort p : openPorts) { try { p.disconnect(receiver); p.close(); } catch (IOException ignored) { } }
        for (MidiDevice d : openDevices) { try { d.close(); } catch (IOException ignored) { } }
        super.onDestroy();
    }
}
