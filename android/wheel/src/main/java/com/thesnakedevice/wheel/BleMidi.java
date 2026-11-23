package com.thesnakedevice.wheel;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

/**
 * Finds Bluetooth LE MIDI devices (the BLE MIDI service UUID) so the app can connect them itself: Numark says the
 * MixTrack Go connects over Bluetooth only when the DJ app selects it (pairing in the system settings doesn't work).
 * The UI ({@link BleView}) lists what is found; {@link MidiIn#openBluetooth} connects one. Callbacks come on the UI thread.
 *
 * <p>Permissions (asked by MainActivity only when BLUETOOTH MIDI is pressed): BLUETOOTH_SCAN (neverForLocation) and
 * BLUETOOTH_CONNECT on Android 12+, ACCESS_FINE_LOCATION on Android 8-11 (where Location must also be switched on
 * for scan results to arrive).
 */
final class BleMidi {
    static final UUID MIDI_SERVICE = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    static final long SCAN_MS = 15000;

    /** A device found by the scan. */
    static final class Found {
        final String name, address;
        int rssi;
        final BluetoothDevice device;
        Found(String name, String address, int rssi, BluetoothDevice device) { this.name = name; this.address = address; this.rssi = rssi; this.device = device; }
        String label() { return name == null || name.isEmpty() ? address : name; }
    }

    interface Listener {
        void onFound(List<Found> all);
        void onScanState(boolean scanning, String message);
    }

    private final Context ctx;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<Found> found = new ArrayList<>();
    private BluetoothLeScanner scanner;
    private boolean scanning;
    Listener listener;

    BleMidi(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    /** The runtime permissions BLE MIDI needs on this Android version. */
    static String[] permissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            return new String[] { android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_CONNECT };
        return new String[] { android.Manifest.permission.ACCESS_FINE_LOCATION };
    }

    static boolean granted(Context c) {
        for (String p : permissions()) if (c.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) return false;
        return true;
    }

    static boolean supported(Context c) {
        return c.getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)
                && c.getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI);
    }

    BluetoothAdapter adapter() {
        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        return bm == null ? null : bm.getAdapter();
    }

    boolean scanning() { return scanning; }
    List<Found> found() { return Collections.unmodifiableList(found); }

    @SuppressLint("MissingPermission")
    void start() {
        stop();
        found.clear();
        if (listener != null) listener.onFound(found());
        BluetoothAdapter a = adapter();
        if (a == null) { state(false, "This device has no Bluetooth"); return; }
        if (!a.isEnabled()) { state(false, "Bluetooth is off: switch it on and scan again"); return; }
        scanner = a.getBluetoothLeScanner();
        if (scanner == null) { state(false, "Bluetooth LE is not available"); return; }
        List<ScanFilter> filters = new ArrayList<>();
        filters.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(MIDI_SERVICE)).build());
        ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        try {
            scanner.startScan(filters, settings, callback);
        } catch (SecurityException e) {
            state(false, "Bluetooth permission was not given");
            return;
        } catch (RuntimeException e) {
            state(false, "Bluetooth scan failed: " + e.getMessage());
            return;
        }
        scanning = true;
        state(true, Build.VERSION.SDK_INT < Build.VERSION_CODES.S
                ? "Scanning… turn the controller on (Location must be on to find Bluetooth devices)"
                : "Scanning… turn the controller on");
        ui.postDelayed(timeout, SCAN_MS);
    }

    @SuppressLint("MissingPermission")
    void stop() {
        ui.removeCallbacks(timeout);
        if (!scanning) return;
        scanning = false;
        try { if (scanner != null) scanner.stopScan(callback); } catch (RuntimeException ignored) {}
    }

    private final Runnable timeout = () -> {
        stop();
        state(false, found.isEmpty() ? "Nothing found. Is the controller on and not connected to another app?" : "Tap a device to connect");
    };

    private void state(boolean on, String msg) { if (listener != null) listener.onScanState(on, msg); }

    private final ScanCallback callback = new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult r) { ui.post(() -> add(r)); }
        @Override public void onBatchScanResults(List<ScanResult> rs) { ui.post(() -> { for (ScanResult r : rs) add(r); }); }
        @Override public void onScanFailed(int code) { ui.post(() -> { scanning = false; state(false, "Bluetooth scan failed (" + code + ")"); }); }
    };

    @SuppressLint("MissingPermission")
    private void add(ScanResult r) {
        BluetoothDevice d = r.getDevice();
        if (d == null) return;
        String name = null;
        ScanRecord rec = r.getScanRecord();
        if (rec != null) name = rec.getDeviceName();
        if (name == null) try { name = d.getName(); } catch (SecurityException ignored) {}
        add(new Found(name, d.getAddress(), r.getRssi(), d));
    }

    /** Adds or refreshes one device in the list (also used by the screenshot test). */
    void add(Found f) {
        for (Found o : found) if (o.address.equals(f.address)) { o.rssi = f.rssi; if (listener != null) listener.onFound(found()); return; }
        found.add(f);
        if (listener != null) listener.onFound(found());
    }
}
