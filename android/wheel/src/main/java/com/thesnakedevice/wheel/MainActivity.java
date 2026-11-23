package com.thesnakedevice.wheel;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * Omega Wheel of Steel: two decks and a scratch mixer. The engine (Native) plays while this activity exists, also in
 * the background (BACK only sends the app behind; it does not stop the mix); it stops in onDestroy. While it runs,
 * {@link PlaybackService} keeps the process in the foreground so Android doesn't kill the mix in the background.
 */
public final class MainActivity extends Activity implements WheelView.Host, LibraryView.Callbacks, Library.Listener, BleView.Callbacks {
    private static final int REQ_TREE = 7, REQ_NOTIFY = 8, REQ_BLE = 9;

    private SharedPreferences prefs;
    private final Ctl ctl = new Ctl();
    private Library library;
    private TrackLoader loader;
    private MidiIn midi;
    private WheelView wheel;
    private LibraryView libView;
    private BleView bleView;
    private BleMidi ble;
    private boolean engineStarted;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean libOpen;
    private boolean serviceStarted;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        immersive();
        prefs = getSharedPreferences("wheel", MODE_PRIVATE);

        try {
            engineStarted = Native.start();
        } catch (Throwable t) {
            showFatal("The audio engine could not be loaded on this device.\n\n" + t);
            return;
        }
        ctl.init(prefs);
        loader = new TrackLoader(this);
        library = new Library(this);
        library.listener = this;
        midi = new MidiIn(ctl);
        ctl.browser = new Ctl.Browser() {
            @Override public void toggle() { ui.post(() -> { if (libOpen) closeLibrary(); else openLibrary(); }); }
            @Override public void move(int delta) { ui.post(() -> { if (libView != null) libView.midiMove(delta); }); }
            @Override public void select(int deck) { ui.post(() -> { if (libView != null) libView.midiSelect(deck); }); }
            @Override public void back() { ui.post(() -> { if (libView != null) libView.midiBack(); }); }
            @Override public boolean isOpen() { return libOpen; }
            @Override public void enter() { ui.post(() -> { if (libView != null) libView.midiEnter(); }); }
            @Override public void load(int deck) { ui.post(() -> loadFromBrowser(deck)); }
        };

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Pal.BG);
        wheel = new WheelView(this, ctl, this);
        root.addView(wheel, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        libView = new LibraryView(this, library, this);
        libView.setVisibility(View.GONE);
        root.addView(libView, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        bleView = new BleView(this, this);
        bleView.setVisibility(View.GONE);
        root.addView(bleView, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        ble = new BleMidi(this);
        ble.listener = new BleMidi.Listener() {
            @Override public void onFound(java.util.List<BleMidi.Found> all) { bleView.setDevices(all); }
            @Override public void onScanState(boolean scanning, String message) { bleView.setStatus(scanning, message); }
        };
        setContentView(root);

        // the folder chosen last time, if Android still lets us read it
        String last = prefs.getString("tree", null);
        if (last != null) {
            Uri u = Uri.parse(last);
            boolean ok = false;
            for (UriPermission p : getContentResolver().getPersistedUriPermissions())
                if (p.getUri().equals(u) && p.isReadPermission()) ok = true;
            if (ok) library.scan(u);
            else library.status = "Choose your SD card or music folder again";
        }
        libView.refresh(false, prefs.getString("crate", null));
        midi.start(this);
        if (!engineStarted) library.status = "Audio output could not be opened";
        askNotificationPermission();
    }

    /** Android 13+: the playing notification needs permission. Asked once; if denied the service runs without it. */
    private void askNotificationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return;
        if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) == android.content.pm.PackageManager.PERMISSION_GRANTED) return;
        if (prefs.getBoolean("notifyAsked", false)) return;
        prefs.edit().putBoolean("notifyAsked", true).apply();
        try { requestPermissions(new String[] { android.Manifest.permission.POST_NOTIFICATIONS }, REQ_NOTIFY); } catch (RuntimeException ignored) {}
    }

    @Override public void onRequestPermissionsResult(int req, String[] perms, int[] results) {
        super.onRequestPermissionsResult(req, perms, results);
        // granted or not, the service is already running; with permission its notification now shows
        if (req == REQ_NOTIFY && serviceStarted) PlaybackService.start(this);
        if (req == REQ_BLE) {
            if (BleMidi.granted(this)) { showBle(); bleScan(); }
            else { showBle(); bleView.setStatus(false, "Bluetooth MIDI needs the Nearby devices (Bluetooth) permission"); }
        }
    }

    private void showFatal(String msg) {
        TextView t = new TextView(this);
        t.setText(msg);
        t.setTextColor(0xffffffff);
        t.setBackgroundColor(0xff000000);
        t.setTextSize(18);
        t.setGravity(Gravity.CENTER);
        t.setPadding(48, 48, 48, 48);
        setContentView(t);
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        // the window's decor must exist before its insets controller does (asking first crashes on Android 11+)
        getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) immersive();
    }

    @Override public void onConfigurationChanged(Configuration c) {
        super.onConfigurationChanged(c);
        immersive();
    }

    @Override protected void onResume() {
        super.onResume();
        immersive();
        if (wheel != null) wheel.resume();
        if (loader != null && !engineStarted) {
            try { engineStarted = Native.start(); } catch (Throwable ignored) {}
        }
        // in the foreground now, so Android allows the foreground service to start
        if (loader != null && engineStarted && !serviceStarted) { PlaybackService.start(this); serviceStarted = true; }
    }

    @Override protected void onPause() {
        super.onPause();
        if (wheel != null) { wheel.pause(); wheel.releaseAll(); }
        if (loader != null) ctl.save();
    }

    @Override protected void onDestroy() {
        if (serviceStarted) { PlaybackService.stop(this); serviceStarted = false; }
        if (loader != null) {
            ctl.save();
            ble.stop();
            midi.stop();
            loader.shutdown();
            library.shutdown();
            try { Native.stop(); } catch (Throwable ignored) {}
        }
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() {
        if (bleView != null && bleView.getVisibility() == View.VISIBLE) { bleClose(); return; }
        if (libOpen) { closeLibrary(); return; }
        if (wheel != null && wheel.sheetOpen()) { wheel.closeSheet(); return; }
        moveTaskToBack(true);   // never stop the mix by accident
    }

    // ---- WheelView.Host
    @Override public void openLibrary() {
        if (libView == null) return;
        libOpen = true;
        libView.deckPaths[0] = wheel.text[0].length > 2 ? wheel.text[0][2] : null;
        libView.deckPaths[1] = wheel.text[1].length > 2 ? wheel.text[1][2] : null;
        libView.refresh(false, prefs.getString("crate", null));
        libView.setVisibility(View.VISIBLE);
        wheel.releaseAll();
        if (library.tree == null && !library.scanning) chooseFolder();
    }

    @Override public boolean loadSelected(int deck) {
        Library.Track t = libView != null ? libView.selected : null;
        if (t == null || library.tree == null) return false;
        loadTrack(deck, t);
        libView.selected = null;
        return true;
    }

    @Override public String selectedTitle() {
        Library.Track t = libView != null ? libView.selected : null;
        return t == null ? null : t.label() + (t.artist.isEmpty() ? "" : "  ·  " + t.artist);
    }

    @Override public String midiStatus() {
        String s = midi != null ? midi.status : "";
        if (ctl.sampleSeen) s += "  ·  MPC Sample: deck " + (ctl.focus == 0 ? "A" : "B");
        if (ctl.mixtrackSeen) s += "  ·  jog " + (ctl.vinyl[0] ? "VINYL" : "NUDGE") + " / " + (ctl.vinyl[1] ? "VINYL" : "NUDGE")
                + (ctl.filterLow ? "  ·  knob LOW" : "");
        return s;
    }

    @Override public TrackLoader loader() { return loader; }

    // ---- BLUETOOTH MIDI (SETUP sheet): permissions are asked only now, when the button is pressed
    @Override public void bluetoothMidi() {
        if (ble == null) return;
        if (!BleMidi.supported(this)) { showBle(); bleView.setStatus(false, "This device has no Bluetooth LE MIDI"); return; }
        if (!BleMidi.granted(this)) {
            try { requestPermissions(BleMidi.permissions(), REQ_BLE); } catch (RuntimeException e) { showBle(); }
            return;
        }
        showBle();
        bleScan();
    }

    /** Shows the Bluetooth MIDI screen (no scan). */
    void showBle() {
        if (bleView == null) return;
        if (wheel != null) { wheel.closeSheet(); wheel.releaseAll(); }
        bleView.setReconnect(prefs.getString("bleAddr", null) == null ? null : prefs.getString("bleName", prefs.getString("bleAddr", null)));
        bleView.setDevices(ble.found());
        bleView.setVisibility(View.VISIBLE);
    }

    @Override public void bleClose() {
        if (ble != null) ble.stop();
        if (bleView != null) bleView.setVisibility(View.GONE);
        immersive();
    }

    @Override public void bleScan() {
        if (ble == null) return;
        if (!BleMidi.granted(this)) { bluetoothMidi(); return; }
        ble.start();
    }

    @Override public void bleConnect(BleMidi.Found f) {
        if (f == null || f.device == null || midi == null) return;
        ble.stop();
        bleView.connecting = f.address;
        bleView.setDevices(ble.found());
        bleView.setStatus(false, "Connecting to " + f.label() + "…");
        connectBle(f.device, f.address, f.label());
    }

    @Override public void bleReconnect() {
        String addr = prefs.getString("bleAddr", null);
        if (addr == null || ble == null || midi == null) return;
        if (!BleMidi.granted(this)) { bluetoothMidi(); return; }
        android.bluetooth.BluetoothAdapter a = ble.adapter();
        if (a == null || !a.isEnabled()) { bleView.setStatus(false, "Bluetooth is off: switch it on first"); return; }
        android.bluetooth.BluetoothDevice d;
        try { d = a.getRemoteDevice(addr); } catch (IllegalArgumentException e) { return; }
        ble.stop();
        String name = prefs.getString("bleName", addr);
        bleView.connecting = addr;
        bleView.setStatus(false, "Connecting to " + name + "…");
        connectBle(d, addr, name);
    }

    private void connectBle(android.bluetooth.BluetoothDevice d, String addr, String label) {
        midi.openBluetooth(d, (ok, name) -> ui.post(() -> {
            bleView.connecting = null;
            if (ok) {
                String n = name != null && !name.isEmpty() ? name : label;
                prefs.edit().putString("bleAddr", addr).putString("bleName", n).apply();
                bleView.connected.add(addr);
                bleView.setReconnect(n);
                bleView.setStatus(false, "Connected: " + n);
            } else bleView.setStatus(false, "Could not connect to " + label + ". Is it on and not used by another app?");
            bleView.setDevices(ble.found());
        }));
    }

    /** LOAD A / B on the MixTrack Go: the library's selected track goes on the deck; the library stays open. */
    private void loadFromBrowser(int deck) {
        Library.Track t = libView != null ? libView.selected : null;
        if (t == null || library.tree == null) return;
        loadTrack(deck, t);
        libView.markLoaded(deck, t.rel);
    }

    // ---- LibraryView.Callbacks
    @Override public void chooseFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (library.tree != null) i.putExtra(DocumentsContract.EXTRA_INITIAL_URI, library.tree);
        try { startActivityForResult(i, REQ_TREE); }
        catch (ActivityNotFoundException e) { library.status = "This device has no folder picker"; libView.refresh(true, null); }
    }

    @SuppressWarnings("deprecation")
    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_TREE || res != RESULT_OK || data == null || data.getData() == null || library == null) return;
        Uri u = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {}
        // forget folders chosen before (Android limits how many an app may keep)
        for (UriPermission p : getContentResolver().getPersistedUriPermissions())
            if (!p.getUri().equals(u)) try { getContentResolver().releasePersistableUriPermission(p.getUri(), Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (Exception ignored) {}
        prefs.edit().putString("tree", u.toString()).apply();
        library.scan(u);
    }

    @Override public void closeLibrary() {
        if (libView == null) return;
        libView.hideKeyboard();
        libView.setVisibility(View.GONE);
        libOpen = false;
        immersive();
    }

    @Override public void load(int deck, Library.Track t) {
        loadTrack(deck, t);
        closeLibrary();
    }

    @Override public void crateChosen(String crateId) { prefs.edit().putString("crate", crateId).apply(); }

    private void loadTrack(int deck, Library.Track t) {
        if (library.tree == null) return;
        ctl.focus = deck;
        loader.load(deck, library.uriOf(t), t.rel, t.name, t.title, t.artist);
    }

    // ---- Library.Listener
    @Override public void onLibraryChanged(boolean tagsOnly) {
        if (libView != null) libView.refresh(tagsOnly, prefs.getString("crate", null));
    }
}
