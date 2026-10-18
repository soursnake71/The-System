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
                .build();

        web.addJavascriptInterface(new Bridge(), "FarpAndroid");
        web.setWebChromeClient(new WebChromeClient() {
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
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
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

    /** Lets the page save exported MIDI files into the phone's Downloads folder. */
    private class Bridge {
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
