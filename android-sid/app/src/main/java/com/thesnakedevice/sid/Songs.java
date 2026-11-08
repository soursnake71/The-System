package com.thesnakedevice.sid;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** The song folder: a folder of MIDI files the user picks once (the system's folder picker, with permission kept
 *  across restarts). Its .mid files (and those in folders inside it) are listed on the SNG page; tapping one reads it
 *  and hands it to the screen, which turns it into a song of SID instruments. */
final class Songs {
    static final int PICK_DIR = 68, PICK_FILE = 69;
    private static final String KEY = "songTree";
    private final Activity act;
    private final SharedPreferences prefs;
    private Uri tree;
    private final List<Uri> uris = new ArrayList<>();
    private final List<String> names = new ArrayList<>();

    Songs(Activity a) {
        act = a; prefs = a.getSharedPreferences("sid", Activity.MODE_PRIVATE);
        final String t = prefs.getString(KEY, null);
        if (t != null) { tree = Uri.parse(t); scan(); }
    }

    void chooseFolder() {
        final Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        act.startActivityForResult(i, PICK_DIR);
    }
    void openAny() {
        final Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*");
        act.startActivityForResult(i, PICK_FILE);
    }
    void onResult(int req, Uri uri) {
        if (uri == null) return;
        if (req == PICK_DIR) {
            try { act.getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (Exception ignored) { }
            tree = uri; prefs.edit().putString(KEY, uri.toString()).apply();
            scan();
        } else read(uri, displayName(uri));
    }
    void open(int index) {
        if (index < 0 || index >= uris.size()) return;
        read(uris.get(index), names.get(index));
    }

    /** List the folder's MIDI files on a worker thread, then hand the names to the screen. */
    void scan() {
        if (tree == null) return;
        final Uri t = tree;
        new Thread(() -> {
            final List<String[]> found = new ArrayList<>();   // relative name, document id
            String folder = "MIDI";
            try {
                final String root = DocumentsContract.getTreeDocumentId(t);
                try (Cursor c = act.getContentResolver().query(DocumentsContract.buildDocumentUriUsingTree(t, root), new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
                    if (c != null && c.moveToFirst()) folder = c.getString(0);
                }
                walk(t, root, "", 0, found);
            } catch (Exception ignored) { }
            Collections.sort(found, (a, b) -> a[0].compareToIgnoreCase(b[0]));
            final String name = folder;
            act.runOnUiThread(() -> {
                uris.clear(); names.clear();
                final String[] list = new String[found.size()];
                for (int i = 0; i < found.size(); ++i) { list[i] = found.get(i)[0]; names.add(list[i]); uris.add(DocumentsContract.buildDocumentUriUsingTree(t, found.get(i)[1])); }
                Native.setSongFiles(name, list);
            });
        }, "sid-songs").start();
    }
    private void walk(Uri t, String docId, String prefix, int depth, List<String[]> out) {
        if (depth > 3 || out.size() >= 2000) return;
        final Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(t, docId);
        try (Cursor c = act.getContentResolver().query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext() && out.size() < 2000) {
                final String id = c.getString(0), nm = c.getString(1), mime = c.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) walk(t, id, prefix + nm + "/", depth + 1, out);
                else {
                    final String low = nm == null ? "" : nm.toLowerCase();
                    if (low.endsWith(".mid") || low.endsWith(".midi") || low.endsWith(".smf") || low.endsWith(".kar")) out.add(new String[] { prefix + nm, id });
                }
            }
        } catch (Exception ignored) { }
    }
    private void read(Uri uri, String name) {
        new Thread(() -> {
            byte[] data = null;
            try (java.io.InputStream in = act.getContentResolver().openInputStream(uri)) {
                if (in != null) {
                    final java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream(); final byte[] buf = new byte[8192]; int n;
                    while ((n = in.read(buf)) > 0 && b.size() < 4 << 20) b.write(buf, 0, n);
                    data = b.toByteArray();
                }
            } catch (Exception ignored) { }
            final byte[] d = data;
            if (d != null) act.runOnUiThread(() -> Native.songFileLoaded(d, name));
        }, "sid-song-read").start();
    }
    private String displayName(Uri uri) {
        String n = "SONG.MID";
        try (Cursor c = act.getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) n = c.getString(0);
        } catch (Exception ignored) { }
        return n;
    }
}
