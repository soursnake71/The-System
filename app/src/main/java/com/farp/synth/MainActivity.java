package com.farp.synth;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Base64;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import android.content.pm.PackageManager;
import android.content.SharedPreferences;
import android.Manifest;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.webkit.PermissionRequest;
import java.io.FileInputStream;
import java.io.ByteArrayOutputStream;
import android.database.Cursor;
import android.provider.DocumentsContract;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.webkit.WebViewAssetLoader;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The F.A.R.P. for Android: runs the synth page from the app's own files on a secure
 * local origin, and feeds USB / Bluetooth MIDI keyboards into it through Android's MIDI API.
 */
public class MainActivity extends Activity {

    private static final String START_URL = "https://appassets.androidplatform.net/assets/index.html";

    private WebView web;
    private MidiManager midiManager;
    private boolean pageReady = false;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<MidiDevice> devices = new ArrayList<>();
    private final List<MidiOutputPort> ports = new ArrayList<>();
    private final Set<Integer> openedIds = new HashSet<>();
    private static final int PICK_FILE = 42;
    private static final int PICK_ROMS = 43;
    private static final int ASK_MIC = 44;
    private PermissionRequest pendingMic;
    private TextToSpeech tts;
    private boolean ttsReady = false;
    private String pendingSpeech = null; private float pendingRate = 1f, pendingPitch = 1f;
    private File saveTmp; private FileOutputStream saveOut; private String saveName;
    private final Map<String, Uri> romDocs = new HashMap<>();
    private ValueCallback<Uri[]> pendingChooser;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        web = new WebView(this);
        web.setBackgroundColor(0xFF121417);
        web.setHapticFeedbackEnabled(false);
        web.setLongClickable(false);
        web.setOnLongClickListener(v -> true);
        web.setVerticalScrollBarEnabled(false);
        web.setHorizontalScrollBarEnabled(false);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        setContentView(web);
        hideSystemBars();

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setTextZoom(100);

        final WebViewAssetLoader loader = new WebViewAssetLoader.Builder()
                .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this))
                .addPathHandler("/roms/", path -> {
                    // the user's ROM folder, read through the access they granted (nothing is copied)
                    try {
                        String name = Uri.decode(path);
                        if (romDocs.isEmpty()) listRomDocs();
                        Uri doc = romDocs.get(name);
                        if (doc == null) return null;
                        InputStream in = getContentResolver().openInputStream(doc);
                        return new WebResourceResponse("application/zip", null, in);
                    } catch (Exception e) {
                        return null;
                    }
                })
                .build();

        web.addJavascriptInterface(new Bridge(), "FarpAndroid");
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(final PermissionRequest request) {
                main.post(() -> {
                    boolean wantsMic = false;
                    for (String r : request.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(r)) wantsMic = true;
                    if (!wantsMic) { request.deny(); return; }
                    if (Build.VERSION.SDK_INT < 23 || checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                        request.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
                    } else {
                        pendingMic = request;
                        requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, ASK_MIC);
                    }
                });
            }

            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (pendingChooser != null) pendingChooser.onReceiveValue(null);
                pendingChooser = callback;
                Intent pick = new Intent(Intent.ACTION_GET_CONTENT);
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.setType("*/*");
                if (params != null && params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE)
                    pick.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(Intent.createChooser(pick, "Choose a file"), PICK_FILE);
                } catch (Exception e) {
                    pendingChooser = null;
                    callback.onReceiveValue(null);
                    return false;
                }
                return true;
            }
        });
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return loader.shouldInterceptRequest(request.getUrl());
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                pageReady = true;
                startMidi();
            }
        });

        web.loadUrl(START_URL);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        if (requestCode != ASK_MIC || pendingMic == null) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED)
            pendingMic.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
        else pendingMic.deny();
        pendingMic = null;
    }

    // ---------------------------------------------------------------- speech for the vocoder
    private void speakNow(final String text, float rate, float pitch) {
        try {
            tts.setSpeechRate(rate);
            tts.setPitch(pitch);
            final File f = new File(getCacheDir(), "farp-speech.wav");
            final String id = "farp" + System.currentTimeMillis();
            tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                @Override public void onStart(String u) { }
                @Override public void onDone(String u) {
                    try (FileInputStream in = new FileInputStream(f); ByteArrayOutputStream bo = new ByteArrayOutputStream()) {
                        byte[] buf = new byte[65536]; int n;
                        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
                        final String b64 = Base64.encodeToString(bo.toByteArray(), Base64.NO_WRAP);
                        main.post(() -> { if (web != null) web.evaluateJavascript("window.onSpeech&&onSpeech('" + b64 + "')", null); });
                    } catch (Exception e) { speechFailed(); }
                }
                @Override public void onError(String u) { speechFailed(); }
            });
            Bundle params = new Bundle();
            if (tts.synthesizeToFile(text, params, f, id) != TextToSpeech.SUCCESS) speechFailed();
        } catch (Exception e) { speechFailed(); }
    }

    private void speechFailed() {
        main.post(() -> { if (web != null) web.evaluateJavascript("window.onSpeech&&onSpeech(null,'failed')", null); });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_ROMS) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                Uri tree = data.getData();
                try {
                    getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (Exception ignored) { }
                prefs().edit().putString("romTree", tree.toString()).apply();
                romDocs.clear();
                final String label = folderLabel(tree);
                if (web != null) web.evaluateJavascript("window.onRomFolder&&onRomFolder('" + label.replace("'", "") + "')", null);
            }
            return;
        }
        if (requestCode != PICK_FILE || pendingChooser == null) return;
        Uri[] result = null;
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                int n = data.getClipData().getItemCount();
                result = new Uri[n];
                for (int i = 0; i < n; i++) result[i] = data.getClipData().getItemAt(i).getUri();
            } else if (data.getData() != null) {
                result = new Uri[] { data.getData() };
            }
        }
        pendingChooser.onReceiveValue(result);
        pendingChooser = null;
    }

    private SharedPreferences prefs() { return getSharedPreferences("farp", MODE_PRIVATE); }

    private Uri romTree() {
        String t = prefs().getString("romTree", null);
        return t == null ? null : Uri.parse(t);
    }

    /** "primary:ROMs" -> "/sdcard/ROMs", "1234-5678:ROMs" -> "SD card/ROMs" */
    private static String folderLabel(Uri tree) {
        try {
            String id = DocumentsContract.getTreeDocumentId(tree);
            int c = id.indexOf(':');
            String vol = c >= 0 ? id.substring(0, c) : id, rest = c >= 0 ? id.substring(c + 1) : "";
            return ("primary".equals(vol) ? "/sdcard/" : "SD card/") + rest;
        } catch (Exception e) {
            return "ROM folder";
        }
    }

    private synchronized JSONArray listRomDocs() {
        JSONArray out = new JSONArray();
        romDocs.clear();
        Uri tree = romTree();
        if (tree == null) return out;
        try {
            Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
            try (Cursor c = getContentResolver().query(kids, new String[] {
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_SIZE }, null, null, null)) {
                while (c != null && c.moveToNext()) {
                    String id = c.getString(0), name = c.getString(1);
                    if (name == null || !name.toLowerCase().endsWith(".zip")) continue;
                    romDocs.put(name, DocumentsContract.buildDocumentUriUsingTree(tree, id));
                    JSONObject o = new JSONObject();
                    o.put("n", name);
                    o.put("s", c.getLong(2));
                    out.put(o);
                }
            }
        } catch (Exception ignored) { }
        return out;
    }

    /** Lets the page save exported MIDI files into the phone's Downloads folder, and reach the ROM folder. */
    private class Bridge {
        @JavascriptInterface
        public void speak(final String text, final String rate, final String pitch) {
            main.post(() -> {
                float r = 1f, p = 1f;
                try { r = Float.parseFloat(rate); p = Float.parseFloat(pitch); } catch (Exception ignored) { }
                if (tts != null && ttsReady) { speakNow(text, r, p); return; }
                pendingSpeech = text; pendingRate = r; pendingPitch = p;
                if (tts == null) tts = new TextToSpeech(MainActivity.this, status -> {
                    ttsReady = status == TextToSpeech.SUCCESS;
                    if (!ttsReady) { speechFailed(); return; }
                    if (pendingSpeech != null) { speakNow(pendingSpeech, pendingRate, pendingPitch); pendingSpeech = null; }
                });
            });
        }

        // large files arrive in pieces (a whole stems zip would not fit through one call)
        @JavascriptInterface
        public void saveBegin(String name) {
            try {
                saveName = name.replaceAll("[^A-Za-z0-9._ -]", "_");
                saveTmp = new File(getCacheDir(), "farp-save.tmp");
                saveOut = new FileOutputStream(saveTmp);
            } catch (Exception e) { saveOut = null; }
        }

        @JavascriptInterface
        public void saveChunk(String b64) {
            try { if (saveOut != null) saveOut.write(Base64.decode(b64, Base64.DEFAULT)); } catch (Exception ignored) { }
        }

        @JavascriptInterface
        public void saveEnd() {
            String where;
            try {
                saveOut.close(); saveOut = null;
                String lower = saveName.toLowerCase(), mime = lower.endsWith(".zip") ? "application/zip" : lower.endsWith(".wav") ? "audio/wav" : lower.endsWith(".mid") ? "audio/midi" : "application/octet-stream";
                if (Build.VERSION.SDK_INT >= 29) {
                    ContentValues v = new ContentValues();
                    v.put(MediaStore.MediaColumns.DISPLAY_NAME, saveName);
                    v.put(MediaStore.MediaColumns.MIME_TYPE, mime);
                    v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                    Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                    if (uri == null) throw new Exception("no uri");
                    try (OutputStream os = getContentResolver().openOutputStream(uri); FileInputStream in = new FileInputStream(saveTmp)) {
                        byte[] buf = new byte[1 << 16]; int n; while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                    }
                    where = "Downloads";
                } else {
                    File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
                    File f = new File(dir, saveName);
                    try (OutputStream os = new FileOutputStream(f); FileInputStream in = new FileInputStream(saveTmp)) {
                        byte[] buf = new byte[1 << 16]; int n; while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                    }
                    where = f.getParent();
                }
                saveTmp.delete();
                final String msg = "Saved " + saveName + " to " + where;
                main.post(() -> Toast.makeText(MainActivity.this, msg, Toast.LENGTH_LONG).show());
            } catch (Exception e) {
                main.post(() -> Toast.makeText(MainActivity.this, "Couldn't save the file", Toast.LENGTH_LONG).show());
            }
        }

        @JavascriptInterface
        public void pickRomFolder() {
            main.post(() -> {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                try {
                    startActivityForResult(i, PICK_ROMS);
                } catch (Exception e) {
                    Toast.makeText(MainActivity.this, "No folder picker on this device", Toast.LENGTH_LONG).show();
                }
            });
        }

        @JavascriptInterface
        public String romFolder() {
            Uri t = romTree();
            return t == null ? "" : folderLabel(t);
        }

        @JavascriptInterface
        public String romList() {
            return listRomDocs().toString();
        }

        @JavascriptInterface
        public void saveFile(String name, String base64) {
            final String safeName = name.replaceAll("[^A-Za-z0-9._-]", "_");
            String where;
            try {
                byte[] bytes = Base64.decode(base64, Base64.DEFAULT);
                if (Build.VERSION.SDK_INT >= 29) {
                    ContentValues v = new ContentValues();
                    v.put(MediaStore.MediaColumns.DISPLAY_NAME, safeName);
                    v.put(MediaStore.MediaColumns.MIME_TYPE, "audio/midi");
                    v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                    Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                    if (uri == null) throw new Exception("no uri");
                    try (OutputStream os = getContentResolver().openOutputStream(uri)) { os.write(bytes); }
                    where = "Downloads";
                } else {
                    File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
                    File f = new File(dir, safeName);
                    try (FileOutputStream os = new FileOutputStream(f)) { os.write(bytes); }
                    where = f.getParent();
                }
                final String msg = "Saved " + safeName + " to " + where;
                main.post(() -> {
                    Toast.makeText(MainActivity.this, msg, Toast.LENGTH_LONG).show();
                    if (web != null) web.evaluateJavascript("typeof sqMsg===\"function\"&&sqMsg('" + msg.replace("'", "") + "')", null);
                });
            } catch (Exception e) {
                main.post(() -> Toast.makeText(MainActivity.this, "Couldn't save the file", Toast.LENGTH_LONG).show());
            }
        }
    }

    // ---------------------------------------------------------------- system UI

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (web != null && pageReady) web.evaluateJavascript("window.farpSuspend&&farpSuspend()", null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (web != null && pageReady) web.evaluateJavascript("window.farpResume&&farpResume()", null);
    }

    @Override
    protected void onDestroy() {
        for (MidiOutputPort p : ports) {
            try { p.close(); } catch (Exception ignored) { }
        }
        for (MidiDevice d : devices) {
            try { d.close(); } catch (Exception ignored) { }
        }
        if (tts != null) { try { tts.shutdown(); } catch (Exception ignored) { } }
        if (web != null) {
            web.destroy();
            web = null;
        }
        super.onDestroy();
    }

    // ---------------------------------------------------------------- MIDI

    private void status(String text) {
        if (web == null || !pageReady) return;
        final String safe = text.replace("\\", "\\\\").replace("'", "\\'");
        main.post(() -> {
            if (web != null) web.evaluateJavascript("window.nativeMidiStatus&&nativeMidiStatus('" + safe + "')", null);
        });
    }

    @SuppressWarnings("deprecation")
    private void startMidi() {
        if (midiManager != null) return;
        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI)) {
            status("This device has no MIDI support");
            return;
        }
        midiManager = (MidiManager) getSystemService(Context.MIDI_SERVICE);
        if (midiManager == null) return;

        MidiDeviceInfo[] infos = midiManager.getDevices();
        for (MidiDeviceInfo info : infos) openDevice(info);
        if (infos.length == 0) status("Plug in a USB or Bluetooth MIDI keyboard");

        midiManager.registerDeviceCallback(new MidiManager.DeviceCallback() {
            @Override
            public void onDeviceAdded(MidiDeviceInfo info) {
                openDevice(info);
            }

            @Override
            public void onDeviceRemoved(MidiDeviceInfo info) {
                openedIds.remove(info.getId());
                status("Disconnected: " + deviceName(info));
            }
        }, main);
    }

    private static String deviceName(MidiDeviceInfo info) {
        String name = info.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
        return name != null ? name : "MIDI device";
    }

    private void openDevice(final MidiDeviceInfo info) {
        if (info.getOutputPortCount() == 0 || openedIds.contains(info.getId())) return;
        openedIds.add(info.getId());
        midiManager.openDevice(info, device -> {
            if (device == null) {
                openedIds.remove(info.getId());
                status("Couldn't open " + deviceName(info));
                return;
            }
            devices.add(device);
            for (int i = 0; i < info.getOutputPortCount(); i++) {
                MidiOutputPort port = device.openOutputPort(i);
                if (port != null) {
                    port.connect(new ToWebReceiver());
                    ports.add(port);
                }
            }
            status("Connected: " + deviceName(info));
        }, main);
    }

    /** Splits the incoming byte stream into single MIDI messages (with running status) and hands them to the page. */
    private class ToWebReceiver extends MidiReceiver {
        private int running = 0, need = 0, have = 0;
        private final int[] buf = new int[2];
        private boolean inSysex = false;

        @Override
        public void onSend(byte[] data, int offset, int count, long timestamp) {
            for (int i = offset; i < offset + count; i++) {
                int b = data[i] & 0xFF;
                if (b >= 0xF8) { emit(b, -1, -1); continue; }          // real-time: clock, start, stop
                if (b == 0xF0) { inSysex = true; continue; }
                if (b == 0xF7) { inSysex = false; continue; }
                if (inSysex) continue;
                if (b >= 0x80) {
                    if (b >= 0xF0) { running = 0; continue; }          // system common: ignored
                    running = b;
                    have = 0;
                    int type = b & 0xF0;
                    need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
                    continue;
                }
                if (running == 0) continue;
                buf[have++] = b;
                if (have == need) {
                    emit(running, buf[0], need == 2 ? buf[1] : -1);
                    have = 0;
                }
            }
        }

        private void emit(int a, int b, int c) {
            final String arr = "[" + a + (b >= 0 ? "," + b : "") + (c >= 0 ? "," + c : "") + "]";
            main.post(() -> {
                if (web != null && pageReady) web.evaluateJavascript("window.nativeMidi&&nativeMidi(" + arr + ")", null);
            });
        }
    }
}
