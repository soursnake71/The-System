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
 * the background (BACK only sends the app behind; it does not stop the mix); it stops in onDestroy.
 */
public final class MainActivity extends Activity implements WheelView.Host, LibraryView.Callbacks, Library.Listener {
    private static final int REQ_TREE = 7;

    private SharedPreferences prefs;
    private final Ctl ctl = new Ctl();
    private Library library;
    private TrackLoader loader;
    private MidiIn midi;
    private WheelView wheel;
    private LibraryView libView;
    private boolean engineStarted;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean libOpen;

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
        };

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Pal.BG);
        wheel = new WheelView(this, ctl, this);
        root.addView(wheel, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        libView = new LibraryView(this, library, this);
        libView.setVisibility(View.GONE);
        root.addView(libView, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
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
    }

    @Override protected void onPause() {
        super.onPause();
        if (wheel != null) { wheel.pause(); wheel.releaseAll(); }
        if (loader != null) ctl.save();
    }

    @Override protected void onDestroy() {
        if (loader != null) {
            ctl.save();
            midi.stop();
            loader.shutdown();
            library.shutdown();
            try { Native.stop(); } catch (Throwable ignored) {}
        }
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() {
        if (libOpen) { closeLibrary(); return; }
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
        return s;
    }

    @Override public TrackLoader loader() { return loader; }

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
