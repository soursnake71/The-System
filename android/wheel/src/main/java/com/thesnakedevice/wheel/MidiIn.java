package com.thesnakedevice.wheel;

import android.bluetooth.BluetoothDevice;
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
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Opens every MIDI device that sends (USB, and Bluetooth LE MIDI devices the system knows), follows plugging and
 * unplugging, parses the byte stream (running status, realtime and sysex skipped) and hands notes / CCs / pitch bend
 * to {@link Ctl}. When one of them is an MPC Sample (its name says so, or it sends the MPC Sample's encoder / encoder
 * press), its input port is opened too and {@link SampleLeds} lights its pads and buttons. A Numark MixTrack Go (its
 * name, or its crossfader / pad messages) gets the MixTrack map ({@link Ctl#mixtrackNote}) and {@link MixtrackLeds}.
 * Bluetooth LE MIDI devices the system doesn't know (the MixTrack Go only connects when the app asks for it) are
 * opened with {@link #openBluetooth} and then handled like any other device.
 */
final class MidiIn {
    private final Ctl ctl;
    private MidiManager mm;
    private HandlerThread thread;
    private Handler handler;
    private final Map<Integer, MidiDevice> devices = new HashMap<>();
    private final Map<Integer, List<MidiOutputPort>> ports = new HashMap<>();
    private final Map<Integer, String> names = new HashMap<>();
    private final Set<Integer> mixtracks = new HashSet<>();
    private final Set<Integer> bluetooth = new HashSet<>();
    private SampleLeds leds;
    private int ledDevice = -1;
    private MidiInputPort ledPort;
    private MixtrackLeds mtLeds;
    private int mtLedDevice = -1;
    private MidiInputPort mtLedPort;
    /** Bluetooth devices opened with openBluetooth: kept open (closing one drops the Bluetooth connection). */
    private final List<MidiDevice> bleOpened = new ArrayList<>();

    /** The result of {@link #openBluetooth} (called on the MIDI thread). */
    interface BleResult { void done(boolean ok, String name); }
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
        mtLeds = new MixtrackLeds(ctl, handler);
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
            mtLeds.lightsOff();
            for (Integer id : new ArrayList<>(devices.keySet())) close(id);
            for (MidiDevice d : bleOpened) try { d.close(); } catch (IOException ignored) {}
            bleOpened.clear();
            t.quitSafely();
        });
        handler = null;
        thread = null;
    }

    private void open(MidiDeviceInfo info) {
        if (!running || info.getOutputPortCount() == 0 || devices.containsKey(info.getId())) return;
        try {
            mm.openDevice(info, dev -> adopt(dev), handler);
        } catch (Throwable ignored) {}
    }

    /** An opened device (the MIDI thread): its output ports go to the parser, its kind decides the map and LEDs. */
    private void adopt(MidiDevice dev) {
        if (dev == null) return;
        MidiDeviceInfo info = dev.getInfo();
        if (!running || devices.containsKey(info.getId()) || info.getOutputPortCount() == 0) {
            try { dev.close(); } catch (IOException ignored) {}
            return;
        }
        Bundle p = info.getProperties();
        String product = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT), pname = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        String name = product;
        if (name == null || name.isEmpty()) name = pname;
        if (name == null || name.isEmpty()) name = "MIDI " + info.getId();
        boolean mt = isMixtrackName(product) || isMixtrackName(pname);
        boolean bt = info.getType() == MidiDeviceInfo.TYPE_BLUETOOTH || name.toLowerCase(Locale.ROOT).contains("bluetooth");
        int id = info.getId();
        if (bt) bluetooth.add(id);
        List<MidiOutputPort> list = new ArrayList<>();
        for (int i = 0; i < info.getOutputPortCount(); ++i) {
            MidiOutputPort port = dev.openOutputPort(i);
            if (port != null) { port.connect(new Parser(id, mt)); list.add(port); }
        }
        devices.put(id, dev);
        ports.put(id, list);
        names.put(id, name);
        if (mt) mixtrackFound(id);
        updateStatus();
        if (isSampleName(name)) startLeds(id);
    }

    /**
     * Connects a Bluetooth LE MIDI device (found by {@link BleMidi}) and handles it like any other device. The opened
     * device is kept open until stop(): closing it would drop the connection.
     */
    void openBluetooth(BluetoothDevice bd, BleResult result) {
        Handler h = handler;
        if (!running || mm == null || h == null) { if (result != null) result.done(false, null); return; }
        try {
            mm.openBluetoothDevice(bd, dev -> {
                if (dev == null) { if (result != null) result.done(false, null); return; }
                if (!running) { try { dev.close(); } catch (IOException ignored) {} return; }
                bleOpened.add(dev);
                MidiDeviceInfo info = dev.getInfo();
                bluetooth.add(info.getId());
                if (!devices.containsKey(info.getId())) open(info);   // its own client, like a USB device
                String n = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
                if (result != null) result.done(true, n);
            }, h);
        } catch (Throwable t) {
            if (result != null) result.done(false, null);
        }
    }

    static boolean isMixtrackName(String name) {
        String n = name == null ? "" : name.toLowerCase(Locale.ROOT);
        return n.contains("mixtrack") && n.contains("go");
    }

    /** What the status line shows for a device. */
    static String displayName(String name, boolean mixtrack, boolean bluetooth) {
        return mixtrack ? "Numark MixTrack Go (" + (bluetooth ? "Bluetooth" : "USB") + ")" : name;
    }

    /** Device id is a MixTrack Go (the MIDI thread): its map, and its LEDs on its own input port. */
    private void mixtrackFound(int id) {
        mixtracks.add(id);
        ctl.mixtrackSeen = true;
        updateStatus();
        if (!running || mtLedDevice == id) return;
        MidiDevice dev = devices.get(id);
        if (dev == null || dev.getInfo().getInputPortCount() == 0) return;
        if (mtLedDevice >= 0) stopMtLeds();
        MidiInputPort in = null;
        for (int i = 0; i < dev.getInfo().getInputPortCount() && in == null; ++i) in = dev.openInputPort(i);
        if (in == null) return;
        final MidiInputPort port = in;
        mtLedDevice = id;
        mtLedPort = in;
        mtLeds.attach(port::send);
    }

    private void stopMtLeds() {
        mtLeds.detach();
        if (mtLedPort != null) try { mtLedPort.close(); } catch (IOException ignored) {}
        mtLedPort = null;
        mtLedDevice = -1;
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
        if (id == mtLedDevice) stopMtLeds();
        mixtracks.remove(id);
        bluetooth.remove(id);
        if (mixtracks.isEmpty()) ctl.mixtrackSeen = false;
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
        for (Map.Entry<Integer, String> e : names.entrySet()) {
            if (!first) sb.append(", ");
            sb.append(displayName(e.getValue(), mixtracks.contains(e.getKey()), bluetooth.contains(e.getKey())));
            first = false;
        }
        status = sb.toString();
    }

    /** A MIDI 1.0 byte-stream parser per port. */
    private final class Parser extends MidiReceiver {
        private final int device;
        private int running = 0, need = 0, have = 0, d1 = 0;
        private boolean sysex, sample, mixtrack;
        Parser(int device, boolean mixtrack) { this.device = device; this.mixtrack = mixtrack; }

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
            int kind = status & 0xf0;
            // only the MixTrack Go sends these: its crossfader (CC 8 on channel 16) and its pads (notes 20-31 on channels 5 / 6)
            if (!mixtrack && ((kind == 0xb0 && Ctl.mixtrackSignature(true, ch, a)) || (kind == 0x90 && b > 0 && Ctl.mixtrackSignature(false, ch, a)))) {
                mixtrack = true;
                Handler h = handler;
                if (h != null) h.post(() -> { if (devices.containsKey(device)) mixtrackFound(device); });
            }
            try {
                if (mixtrack) {
                    switch (kind) {
                        case 0x90: ctl.mixtrackNote(ch, a, b, b > 0); break;
                        case 0x80: ctl.mixtrackNote(ch, a, 0, false); break;
                        case 0xb0: ctl.mixtrackCc(ch, a, b); break;
                        default: break;
                    }
                    return;
                }
                switch (kind) {
                    case 0x90: ctl.otherNote(ch, a, b, b > 0); break;
                    case 0x80: ctl.otherNote(ch, a, 0, false); break;
                    case 0xb0: ctl.otherCc(ch, a, b); break;
                    case 0xe0: ctl.midiPitchBend(ch, a | (b << 7)); break;
                    default: break;
                }
            } catch (Throwable ignored) {}
        }
    }
}
