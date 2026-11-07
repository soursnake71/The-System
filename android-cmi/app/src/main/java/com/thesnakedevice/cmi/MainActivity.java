package com.thesnakedevice.cmi;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
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

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;
import java.util.ArrayList;
import java.util.List;

/** Omega CMI: the screen, MIDI in (USB and Bluetooth), the audio engine's life cycle, sampling from files. */
public class MainActivity extends Activity {
    private CmiView view;
    private final List<MidiDevice> devices = new ArrayList<>();
    private final List<MidiOutputPort> ports = new ArrayList<>();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private int pickSlot = 0;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        loadState();
        Native.setRemembered(getSharedPreferences("cmi", MODE_PRIVATE).getString("remembered", ""));
        Native.setPresetChoices(getSharedPreferences("cmi", MODE_PRIVATE).getString("presets", ""));
        { String fx = getSharedPreferences("cmi", MODE_PRIVATE).getString("fx", ""); if (!fx.isEmpty()) Native.setFxState(fx); }
        view = new CmiView(this);
        rescanLibrary();
        rescanSongs();
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
        try { getSharedPreferences("cmi", MODE_PRIVATE).edit().putString("remembered", Native.getRemembered()).putString("presets", Native.getPresetChoices()).putString("fx", Native.fxState()).apply(); } catch (Exception ignored) {}
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

    // ---- Page 8: your own sounds, one file, many files or a whole folder ----
    static final int PICK_ONE = 41, PICK_MANY = 42, PICK_FOLDER = 43, SAVE_WAV = 44, SAVE_STEMS = 45, SAVE_MIDI = 46;

    // ---- exports: 0 the song (or Page R) as WAV, 1 stems into a folder, 2 the song as MIDI ----
    void export(int what) {
        String name = Native.currentSong() >= 0 ? Native.songInfo().split("\\t", -1)[0] : "PAGE S";
        if (what == 1) { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), SAVE_STEMS); return; }
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType(what == 2 ? "audio/midi" : "audio/wav");
        i.putExtra(Intent.EXTRA_TITLE, name + (what == 2 ? ".mid" : ".wav"));
        startActivityForResult(i, what == 2 ? SAVE_MIDI : SAVE_WAV);
    }
    private int fdFor(Uri u) throws Exception {
        android.os.ParcelFileDescriptor pfd;
        try { pfd = getContentResolver().openFileDescriptor(u, "rw"); } catch (Exception e) { pfd = getContentResolver().openFileDescriptor(u, "w"); }
        if (pfd == null) throw new Exception("no file");
        return pfd.detachFd();
    }
    private void exportResult(int req, Intent data) {
        final Uri uri = data.getData();
        if (uri == null) return;
        view.status("RENDERING...");
        new Thread(() -> {
            String msg;
            try {
                if (req == SAVE_WAV) msg = Native.exportSongWav(fdFor(uri), -1);
                else if (req == SAVE_MIDI) msg = Native.exportSongMidi(fdFor(uri));
                else {
                    String base = Native.songInfo().split("\\t", -1)[0];
                    Uri dir = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));
                    int n = 0;
                    for (int t = 0; t < Native.SONG_TRK; ++t) {
                        String[] f = Native.songTrack(t).split("\\t", -1);
                        if (f.length < 4 || !f[2].equals("1") || f[3].equals("1")) continue;
                        final int tt = t; ui.post(() -> view.status("RENDERING TRACK " + (tt + 1)));
                        Uri doc = DocumentsContract.createDocument(getContentResolver(), dir, "audio/wav", String.format("%s %02d %s.wav", base, t + 1, f[0]).trim());
                        if (doc == null) continue;
                        Native.exportSongWav(fdFor(doc), t); ++n;
                    }
                    msg = "EXPORTED " + n + " STEMS";
                }
            } catch (Exception e) { msg = "CAN'T WRITE THE FILE"; }
            final String m = msg; ui.post(() -> view.status(m));
        }).start();
    }
    void pickSample(int slot) { pickSlot = slot; startActivityForResult(audioIntent(false), PICK_ONE); }
    void importFiles() { startActivityForResult(audioIntent(true), PICK_MANY); }
    void importFolder() { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_FOLDER); }
    private Intent audioIntent(boolean many) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "audio/*", "application/zip", "application/x-zip-compressed", "application/octet-stream" });
        if (many) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        return i;
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null) return;
        if (req == SAVE_WAV || req == SAVE_MIDI || req == SAVE_STEMS) { exportResult(req, data); return; }
        if (req == PICK_SONGS && data.getData() != null) {
            final Uri tree = data.getData();
            try { getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (Exception ignored) {}
            getSharedPreferences("cmi", MODE_PRIVATE).edit().putString("songs", tree.toString()).apply();
            rescanSongs();
            return;
        }
        if (req == PICK_LIBRARY && data.getData() != null) {
            final Uri tree = data.getData();
            try { getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (Exception ignored) {}
            getSharedPreferences("cmi", MODE_PRIVATE).edit().putString("library", tree.toString()).apply();
            rescanLibrary();
            return;
        }
        final List<Uri> uris = new ArrayList<>();
        final List<String> names = new ArrayList<>();
        if (req == PICK_FOLDER && data.getData() != null) {
            listFolder(data.getData(), DocumentsContract.getTreeDocumentId(data.getData()), uris, names, 0);
        } else if (data.getClipData() != null) {
            for (int i = 0; i < data.getClipData().getItemCount(); ++i) { Uri u = data.getClipData().getItemAt(i).getUri(); uris.add(u); names.add(displayName(u)); }
        } else if (data.getData() != null) { uris.add(data.getData()); names.add(displayName(data.getData())); }
        importList(uris, names, req == PICK_ONE ? pickSlot : -1);
    }

    // ---- the song folder: your MIDI songs, browsed and loaded one at a time ----
    static final int PICK_SONGS = 48;
    final List<Uri> songUris = new ArrayList<>(); final List<String> songNames = new ArrayList<>();
    void chooseSongFolder() { Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE); i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION); startActivityForResult(i, PICK_SONGS); }
    String songFolderName() { String u = getSharedPreferences("cmi", MODE_PRIVATE).getString("songs", ""); if (u.isEmpty()) return ""; String d = Uri.parse(u).getLastPathSegment(); return d == null ? u : d; }
    void rescanSongs() {
        final String u = getSharedPreferences("cmi", MODE_PRIVATE).getString("songs", "");
        if (u.isEmpty()) return;
        new Thread(() -> {
            final List<Uri> us = new ArrayList<>(); final List<String> ns = new ArrayList<>();
            try { final Uri tree = Uri.parse(u); listFolder(tree, DocumentsContract.getTreeDocumentId(tree), us, ns, 0); } catch (Exception ignored) {}
            final List<Integer> ord = new ArrayList<>();
            for (int i = 0; i < us.size(); ++i) { String n = ns.get(i).toLowerCase(); if (n.endsWith(".mid") || n.endsWith(".midi") || n.endsWith(".kar") || n.endsWith(".smf")) ord.add(i); }
            java.util.Collections.sort(ord, (x, y) -> ns.get(x).compareToIgnoreCase(ns.get(y)));
            ui.post(() -> { songUris.clear(); songNames.clear(); for (int i : ord) { songUris.add(us.get(i)); songNames.add(ns.get(i)); } view.status(songUris.size() + " SONGS IN THE SONG FOLDER"); });
        }).start();
    }
    static String baseName(String n) { int d = n.lastIndexOf('.'); return d > 0 ? n.substring(0, d) : n; }
    /** a song from the song folder: loaded if it isn't, selected, played if asked */
    void openSong(final int i, final boolean play) {
        if (i < 0 || i >= songUris.size()) return;
        final String base = baseName(songNames.get(i));
        int k = Native.songLoadedAs(base);
        if (k >= 0) { Native.selectSongAt(k); if (play) Native.songPlay(true); view.showPage('S'); return; }
        view.status("LOADING " + base.toUpperCase());
        new Thread(() -> {
            String err;
            try { err = Native.importMidi(readAll(songUris.get(i), 4 << 20), base); } catch (Exception e) { err = "CAN'T READ"; }
            final String e2 = err;
            ui.post(() -> {
                int k2 = Native.songLoadedAs(base);
                if (k2 >= 0) { Native.selectSongAt(k2); if (play) Native.songPlay(true); view.showPage('S'); view.status("LOADED " + base.toUpperCase()); saveState(); }
                else view.status(e2.equals("DUPLICATE") ? "ALREADY LOADED (ANOTHER VERSION)" : e2);
            });
        }).start();
    }

    // ---- the library folder: chosen once (the permission is kept), scanned, its files loaded on demand ----
    static final int PICK_LIBRARY = 47;
    final List<Uri> libUris = new ArrayList<>(); final List<String> libNames = new ArrayList<>();
    void chooseLibrary() { Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE); i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION); startActivityForResult(i, PICK_LIBRARY); }
    String libraryName() { String u = getSharedPreferences("cmi", MODE_PRIVATE).getString("library", ""); if (u.isEmpty()) return ""; String d = Uri.parse(u).getLastPathSegment(); return d == null ? u : d; }
    void rescanLibrary() {
        final String u = getSharedPreferences("cmi", MODE_PRIVATE).getString("library", "");
        if (u.isEmpty()) return;
        view.status("SCANNING THE LIBRARY...");
        new Thread(() -> {
            final List<Uri> us = new ArrayList<>(); final List<String> ns = new ArrayList<>();
            try { final Uri tree = Uri.parse(u); listFolder(tree, DocumentsContract.getTreeDocumentId(tree), us, ns, 0); } catch (Exception ignored) {}
            final List<Integer> ord = new ArrayList<>(); for (int i = 0; i < us.size(); ++i) ord.add(i);
            java.util.Collections.sort(ord, (x, y) -> ns.get(x).compareToIgnoreCase(ns.get(y)));
            // the CMI library: every voice on the disks and .VC files there
            Native.catalogClear();
            int done = 0;
            for (int pass = 0; pass < 2; ++pass) for (int i : ord) {   // disk images first: their labels name the voices' disks
                String lo = ns.get(i).toLowerCase();
                boolean disk = lo.endsWith(".imd") || lo.endsWith(".img") || lo.endsWith(".bin") || lo.endsWith(".raw") || lo.endsWith(".dsk");
                if (pass == 0 ? !disk : !lo.endsWith(".vc")) continue;
                try { Native.catalogAdd(readAll(us.get(i), 4 << 20), ns.get(i), ""); } catch (Exception ignored) {}
                if (++done % 10 == 0) { final int d2 = done; ui.post(() -> view.status("READING THE CMI LIBRARY: " + d2 + " FILES")); }
            }
            Native.connectDefaults();   // no built-in voices: Page R and the keyboard take theirs from the library
            ui.post(() -> { libUris.clear(); libNames.clear(); for (int i : ord) { libUris.add(us.get(i)); libNames.add(ns.get(i)); } saveState();
                view.status(Native.catalogCount() + " CMI VOICES ON " + Native.catalogDisks() + " DISKS, " + libUris.size() + " FILES"); });
        }).start();
    }
    void loadFromLibrary(boolean songs, boolean sounds, int only) {
        final List<Uri> us = new ArrayList<>(); final List<String> ns = new ArrayList<>();
        for (int i = 0; i < libUris.size(); ++i) {
            if (only >= 0 && i != only) continue;
            String n = libNames.get(i).toLowerCase(); boolean song = n.endsWith(".mid") || n.endsWith(".midi") || n.endsWith(".zip");
            if (only >= 0 || (song ? songs : sounds)) { us.add(libUris.get(i)); ns.add(libNames.get(i)); }
        }
        importList(us, ns, -1);
    }

    private int dupes = 0;
    void importList(final List<Uri> uris, final List<String> names, final int single) {
        if (uris.isEmpty()) { view.status("NO AUDIO FILES FOUND"); return; }
        // name order, like a disk directory
        final List<Integer> order = new ArrayList<>();
        for (int i = 0; i < uris.size(); ++i) order.add(i);
        java.util.Collections.sort(order, (x, y) -> names.get(x).compareToIgnoreCase(names.get(y)));
        final int root = view.rootKey; final boolean loop = view.loopSample;
        view.status("IMPORTING...");
        new Thread(() -> {
            int ok = 0, bad = 0, first = -1, disks = 0; String last = ""; importedSongs = 0; dupes = 0;
            for (int idx : order) {
                int slot = single >= 0 ? single : Native.firstFreeUser();
                String nlow = names.get(idx).toLowerCase();
                boolean midi = nlow.endsWith(".mid") || nlow.endsWith(".midi") || nlow.endsWith(".smf") || nlow.endsWith(".zip");
                if (slot < 0 && !midi) { last = "USER VOICES FULL"; break; }
                lastDiskCount = 0;
                String err = importOne(uris.get(idx), names.get(idx), slot, root, loop);
                if (err.isEmpty()) { if (midi && lastDiskCount == 0) continue; if (lastDiskCount > 0) { ok += lastDiskCount; ++disks; } else ++ok; if (first < 0) first = slot; } else { ++bad; last = err; }
                final int done = ok + bad;
                ui.post(() -> view.status("IMPORTING " + done + " / " + order.size()));
                if (single >= 0) break;
            }
            if (first >= 0) Native.loadSound(Native.userSlot(first));
            final int songsIn = importedSongs;
            final String dupTxt = dupes > 0 ? ", " + dupes + " DUPLICATES SKIPPED" : "";
            final String m0 = (songsIn > 0 && ok == 0 && bad == 0) ? songsIn + (songsIn == 1 ? " SONG" : " SONGS") + " LOADED (PAGE S)" : ok + " IMPORTED" + (disks > 0 ? " FROM " + disks + (disks == 1 ? " DISK" : " DISKS") : "") + (bad > 0 ? ", " + bad + " UNREADABLE" : "") + (last.equals("USER VOICES FULL") ? ", USER VOICES FULL" : "");
            final String mm = m0 + dupTxt;
            ui.post(() -> { view.status(mm); if (songsIn > 0) view.showPage('S'); saveState(); });
        }).start();
    }

    private int lastDiskCount = 0, importedSongs = 0;
    void importMidi() { Intent i = audioIntent(true); i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "audio/midi", "audio/x-midi", "application/x-midi", "audio/sp-midi", "application/zip", "application/x-zip-compressed", "application/octet-stream" }); startActivityForResult(i, PICK_MANY); }
    private String importOne(Uri uri, String name, int slot, int root, boolean loop) {
        String base = name; int dot = base.lastIndexOf('.'); if (dot > 0) base = base.substring(0, dot);
        String lower = name.toLowerCase();
        try {
            if (lower.endsWith(".zip")) return importZip(uri);
            if (lower.endsWith(".imd") || lower.endsWith(".img") || lower.endsWith(".bin") || lower.endsWith(".raw") || lower.endsWith(".dsk") || lower.endsWith(".hfe")) {
                String r = Native.loadDisk(readAll(uri, 4 << 20), base);
                try { int n = Integer.parseInt(r); lastDiskCount += n; return n > 0 ? "" : "NO VOICES ON " + name; } catch (NumberFormatException e) { return r; }
            }
            if (lower.endsWith(".vc")) return Native.loadVc(readAll(uri, 1 << 20), base, "", slot);
            if (lower.endsWith(".mid") || lower.endsWith(".midi") || lower.endsWith(".smf")) { String r = Native.importMidi(readAll(uri, 4 << 20), base); if (r.equals("DUPLICATE")) { dupes++; return ""; } if (r.isEmpty()) importedSongs++; return r; }
            if (name.toLowerCase().endsWith(".wav")) {
                byte[] b = readAll(uri, 64 << 20);
                String err = Native.loadWav(b, base, slot, root, loop);
                if (err.isEmpty()) return err;   // otherwise let the system decoder try (compressed WAVs)
            }
            int[] rate = new int[1];
            float[] mono = decode(uri, rate, 12.0);
            if (mono.length == 0) return "EMPTY FILE";
            return Native.loadSample(mono, rate[0], base, slot, root, loop, -1, -1);
        } catch (Exception e) { return "CAN'T READ " + name; }
    }

    // everything in a zip: songs, disks, voice files, WAVs (other audio needs unpacking first)
    private String importZip(Uri uri) throws Exception {
        int songs = 0, voices = 0, bad = 0;
        try (InputStream in = getContentResolver().openInputStream(uri); java.util.zip.ZipInputStream z = new java.util.zip.ZipInputStream(in)) {
            java.util.zip.ZipEntry en;
            while ((en = z.getNextEntry()) != null) {
                if (en.isDirectory()) continue;
                String nm = en.getName(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
                if (nm.startsWith(".") || nm.startsWith("._")) continue;
                String lo = nm.toLowerCase(), base = nm; int dot = base.lastIndexOf('.'); if (dot > 0) base = base.substring(0, dot);
                boolean midi = lo.endsWith(".mid") || lo.endsWith(".midi"), disk = lo.endsWith(".imd") || lo.endsWith(".img") || lo.endsWith(".bin") || lo.endsWith(".raw") || lo.endsWith(".dsk");
                boolean vc = lo.endsWith(".vc"), wav = lo.endsWith(".wav");
                if (!midi && !disk && !vc && !wav) continue;
                ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] buf = new byte[65536]; int r;
                while ((r = z.read(buf)) > 0 && o.size() < (64 << 20)) o.write(buf, 0, r);
                byte[] b = o.toByteArray();
                String err;
                if (midi) { err = Native.importMidi(b, base); if (err.equals("DUPLICATE")) { dupes++; err = ""; } else if (err.isEmpty()) songs++; }
                else if (disk) { err = Native.loadDisk(b, base); try { voices += Integer.parseInt(err); err = ""; } catch (NumberFormatException e) { } }
                else { int slot = Native.firstFreeUser(); if (slot < 0) { err = "FULL"; } else { err = vc ? Native.loadVc(b, base, "", slot) : Native.loadWav(b, base, slot, 60, false); if (err.isEmpty()) voices++; } }
                if (!err.isEmpty()) bad++;
                final int s2 = songs, v2 = voices; ui.post(() -> view.status("UNZIPPING: " + s2 + " SONGS, " + v2 + " VOICES"));
            }
        }
        importedSongs += songs; lastDiskCount += voices;
        return songs + voices > 0 ? "" : "NOTHING TO IMPORT IN THE ZIP";
    }

    private byte[] readAll(Uri uri, int max) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) throw new Exception("no stream");
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[65536]; int r;
            while ((r = in.read(buf)) > 0 && o.size() < max) o.write(buf, 0, r);
            return o.toByteArray();
        }
    }

    private String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) return c.getString(0);
        } catch (Exception ignored) {}
        String s = u.getLastPathSegment(); if (s == null) return "SAMPLE";
        int slash = s.lastIndexOf('/'); return slash >= 0 ? s.substring(slash + 1) : s;
    }

    // a folder picked with the system's folder picker, and its sub-folders
    private void listFolder(Uri tree, String docId, List<Uri> uris, List<String> names, int depth) {
        if (depth > 6) return;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) { listFolder(tree, id, uris, names, depth + 1); continue; }
                String n = name.toLowerCase();
                boolean audio = (mime != null && mime.startsWith("audio/")) || n.endsWith(".wav") || n.endsWith(".aif") || n.endsWith(".aiff") || n.endsWith(".flac")
                        || n.endsWith(".mp3") || n.endsWith(".ogg") || n.endsWith(".vc") || n.endsWith(".imd") || n.endsWith(".img") || n.endsWith(".bin")
                        || n.endsWith(".raw") || n.endsWith(".dsk") || n.endsWith(".mid") || n.endsWith(".midi") || n.endsWith(".zip");
                if (!audio) continue;
                uris.add(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                names.add(name);
            }
        } catch (Exception ignored) {}
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
