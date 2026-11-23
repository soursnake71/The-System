package com.thesnakedevice.wheel;

import android.content.Context;
import android.content.pm.PackageManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Opens every MIDI device that sends (USB, and Bluetooth LE MIDI devices the system knows), follows plugging and
 * unplugging, parses the byte stream (running status, realtime and sysex skipped) and hands notes / CCs / pitch bend
 * to {@link Ctl}. When one of them is an MPC Sample (its name says so, or it sends the MPC Sample's encoder / encoder
 * press), its input port is opened too and {@link SampleLeds} lights its pads and buttons.
 */
final class MidiIn {
    private final Ctl ctl;
    private MidiManager mm;
    private HandlerThread thread;
    private Handler handler;
    private final Map<Integer, MidiDevice> devices = new HashMap<>();
    private final Map<Integer, List<MidiOutputPort>> ports = new HashMap<>();
    private final Map<Integer, String> names = new HashMap<>();
    private SampleLeds leds;
    private int ledDevice = -1;
    private MidiInputPort ledPort;
    volatile String status = "MIDI: none";
    private boolean running;

    private final MidiManager.DeviceCallback callback = new MidiManager.DeviceCallback() {
        @Override public void onDeviceAdded(MidiDeviceInfo info) { open(info); }
        @Override public void onDeviceRemoved(MidiDeviceInfo info) { close(info.getId()); }
    };

    MidiIn(Ctl ctl) { this.ctl = ctl; }

    @SuppressWarnings("deprecation")
    void start(Context ctx) {
        if (running) return;
        if (!ctx.getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI)) { status = "MIDI: not supported"; return; }
        mm = (MidiManager) ctx.getSystemService(Context.MIDI_SERVICE);
        if (mm == null) { status = "MIDI: not supported"; return; }
        running = true;
        thread = new HandlerThread("wheel-midi");
        thread.start();
        handler = new Handler(thread.getLooper());
        leds = new SampleLeds(ctl, handler);
        try { mm.registerDeviceCallback(callback, handler); } catch (Throwable ignored) {}
        handler.post(() -> {
            try { for (MidiDeviceInfo info : mm.getDevices()) open(info); } catch (Throwable ignored) {}
            updateStatus();
        });
    }

    void stop() {
        if (!running) return;
        running = false;
        try { mm.unregisterDeviceCallback(callback); } catch (Throwable ignored) {}
        Handler h = handler;
        HandlerThread t = thread;
        h.post(() -> {
            leds.lightsOff();
            for (Integer id : new ArrayList<>(devices.keySet())) close(id);
            t.quitSafely();
        });
        handler = null;
        thread = null;
    }

    private void open(MidiDeviceInfo info) {
        if (!running || info.getOutputPortCount() == 0 || devices.containsKey(info.getId())) return;
        Bundle p = info.getProperties();
        String name = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        if (name == null || name.isEmpty()) name = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        if (name == null || name.isEmpty()) name = "MIDI " + info.getId();
        final String nm = name;
        try {
            mm.openDevice(info, dev -> {
                if (dev == null) return;
                if (!running) { try { dev.close(); } catch (IOException ignored) {} return; }
                List<MidiOutputPort> list = new ArrayList<>();
                for (int i = 0; i < info.getOutputPortCount(); ++i) {
                    MidiOutputPort port = dev.openOutputPort(i);
                    if (port != null) { port.connect(new Parser(info.getId())); list.add(port); }
                }
                devices.put(info.getId(), dev);
                ports.put(info.getId(), list);
                names.put(info.getId(), nm);
                updateStatus();
                if (isSampleName(nm)) startLeds(info.getId());
            }, handler);
        } catch (Throwable ignored) {}
    }

    static boolean isSampleName(String name) {
        String n = name == null ? "" : name.toLowerCase(java.util.Locale.ROOT);
        return n.contains("mpc") && n.contains("sample");
    }

    /** The device is an MPC Sample: open its input port (handler thread) and light it. */
    private void startLeds(int id) {
        if (!running || ledDevice == id) return;
        MidiDevice dev = devices.get(id);
        if (dev == null || dev.getInfo().getInputPortCount() == 0) return;
        if (ledDevice >= 0) stopLeds();
        MidiInputPort in = null;
        for (int i = 0; i < dev.getInfo().getInputPortCount() && in == null; ++i) in = dev.openInputPort(i);
        if (in == null) return;
        ledDevice = id;
        ledPort = in;
        leds.attach(in);
    }

    private void stopLeds() {
        leds.detach();
        if (ledPort != null) try { ledPort.close(); } catch (IOException ignored) {}
        ledPort = null;
        ledDevice = -1;
    }

    private void close(int id) {
        if (id == ledDevice) stopLeds();
        List<MidiOutputPort> list = ports.remove(id);
        if (list != null) for (MidiOutputPort port : list) try { port.close(); } catch (IOException ignored) {}
        MidiDevice d = devices.remove(id);
        if (d != null) try { d.close(); } catch (IOException ignored) {}
        names.remove(id);
        ctl.releaseAllPads(1);
        updateStatus();
    }

    private void updateStatus() {
        if (names.isEmpty()) { status = "MIDI: plug in an MPC or controller"; return; }
        StringBuilder sb = new StringBuilder("MIDI: ");
        boolean first = true;
        for (String n : names.values()) { if (!first) sb.append(", "); sb.append(n); first = false; }
        status = sb.toString();
    }

    /** A MIDI 1.0 byte-stream parser per port. */
    private final class Parser extends MidiReceiver {
        private final int device;
        private int running = 0, need = 0, have = 0, d1 = 0;
        private boolean sysex, sample;
        Parser(int device) { this.device = device; }

        @Override public void onSend(byte[] msg, int offset, int count, long timestamp) {
            for (int i = offset; i < offset + count; ++i) {
                int b = msg[i] & 0xff;
                if (b >= 0xf8) continue;                         // realtime: clock, start, stop...
                if (b == 0xf0) { sysex = true; continue; }
                if (b == 0xf7) { sysex = false; continue; }
                if (b >= 0x80) {
                    sysex = false;
                    if (b >= 0xf0) { running = 0; continue; }   // system common: ignore (and clears running status)
                    running = b; have = 0;
                    int hi = b & 0xf0;
                    need = (hi == 0xc0 || hi == 0xd0) ? 1 : 2;
                    continue;
                }
                if (sysex || running == 0) continue;
                if (have == 0) { d1 = b; have = 1; if (need == 1) { have = 0; } else continue; }
                else { have = 0; dispatch(running, d1, b); continue; }
            }
        }

        private void dispatch(int status, int a, int b) {
            int ch = (status & 0x0f) + 1;
            // only the MPC Sample sends these on channel 1: its big encoder (CC 100) and the encoder press (note 111)
            if (!sample && ch == 1 && (((status & 0xf0) == 0xb0 && a == Ctl.SCC_ENCODER) || ((status & 0xf0) == 0x90 && a == Ctl.SB_ENC_PRESS))) {
                sample = true;
                Handler h = handler;
                if (h != null) h.post(() -> startLeds(device));
            }
            try {
                switch (status & 0xf0) {
                    case 0x90: ctl.midiNote(ch, a, b, b > 0); break;
                    case 0x80: ctl.midiNote(ch, a, 0, false); break;
                    case 0xb0: ctl.midiCc(ch, a, b); break;
                    case 0xe0: ctl.midiPitchBend(ch, a | (b << 7)); break;
                    default: break;
                }
            } catch (Throwable ignored) {}
        }
    }
}
