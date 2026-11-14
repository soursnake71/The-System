package com.thesnakedevice.app;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Base64;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;
import androidx.webkit.WebViewAssetLoader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/** Omega Linn with the web app bundled inside (assets/linn), so it works offline and needs no website.
 *  Android's WebView has no Web MIDI or file downloads, so this activity supplies them natively:
 *  - AndroidMidi: USB / Bluetooth MIDI devices through android.media.midi, shaped like Web MIDI by io.js,
 *  - AndroidLinn.save: exports (backups, MIDI files, WAVs) go to Downloads/Omega Linn. */
public class LinnActivity extends Activity {
  // the bundled page
  static final String HOME = "https://appassets.androidplatform.net/assets/linn/index.html" + BuildConfig.BRAND_QUERY;
  static final int PICK = 7;
  WebView web;
  ValueCallback<Uri[]> pending;
  long lastBack;
  final Handler ui = new Handler(Looper.getMainLooper());
  WebViewAssetLoader assets;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    assets = new WebViewAssetLoader.Builder().addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
    web = makeWeb(this);
    web.addJavascriptInterface(new Files(), "AndroidLinn");
    web.addJavascriptInterface(new Midi(), "AndroidMidi");
    web.setWebChromeClient(new WebChromeClient() {
      @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
        if (pending != null) pending.onReceiveValue(null);
        pending = cb;
        Intent i;
        pickTree = wantFolder; wantFolder = false;
        if (pickTree) i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);   // LOAD KIT FOLDER: pick a folder, every sound in it comes back
        else {
          i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"audio/*", "application/json", "audio/midi", "audio/x-midi", "application/octet-stream"});
          if (p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        }
        try { startActivityForResult(i, PICK); }
        catch (ActivityNotFoundException e) { pending = null; cb.onReceiveValue(null); toast("No file picker on this device"); }
        return true;
      }
    });
    // Android 15 draws apps under the status and navigation bars: keep the panel between them so nothing is cut off
    final android.widget.FrameLayout root = new android.widget.FrameLayout(this);
    root.setBackgroundColor(0xff111111);
    root.addView(web, new android.widget.FrameLayout.LayoutParams(-1, -1));
    root.setOnApplyWindowInsetsListener((v, ins) -> {
      if (Build.VERSION.SDK_INT >= 30) {
        android.graphics.Insets bars = ins.getInsets(android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout());
        v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
      } else v.setPadding(ins.getSystemWindowInsetLeft(), ins.getSystemWindowInsetTop(), ins.getSystemWindowInsetRight(), ins.getSystemWindowInsetBottom());
      return ins;
    });
    setContentView(root);
    if (b != null) web.restoreState(b); else web.loadUrl(HOME);
  }

  WebView makeWeb(Context c) {
    WebView w = new WebView(c);
    w.setHapticFeedbackEnabled(false);   // no buzz on long presses (the page also cancels them on pads and knobs)
    w.setLongClickable(false);
    w.setOnLongClickListener(v -> true);
    WebSettings s = w.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setAllowFileAccess(false);
    s.setAllowContentAccess(false);
    w.setWebViewClient(new WebViewClient() {
      @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) { return assets.shouldInterceptRequest(r.getUrl()); }
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        if ("appassets.androidplatform.net".equals(u.getHost())) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { }
        return true;
      }
    });
    return w;
  }

  void toast(String s) { ui.post(() -> Toast.makeText(this, s, Toast.LENGTH_LONG).show()); }
  void js(WebView w, String code) { ui.post(() -> { if (w != null) w.evaluateJavascript(code, null); }); }

  /* ---------------- saving exports ---------------- */
  class Files {
    // The page calls this just before it opens its folder picker (WebView has no folder input of its own)
    @JavascriptInterface public void folderNext() { wantFolder = true; }
    @JavascriptInterface public String save(String base64, String name, String mime) {
      try {
        byte[] data = Base64.decode(base64, Base64.DEFAULT);
        String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        if (Build.VERSION.SDK_INT >= 29) {
          ContentValues v = new ContentValues();
          v.put(MediaStore.Downloads.DISPLAY_NAME, safe);
          v.put(MediaStore.Downloads.MIME_TYPE, mime);
          v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/" + getString(R.string.app_name));
          Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
          try (OutputStream o = getContentResolver().openOutputStream(u)) { o.write(data); }
          return "Download/" + getString(R.string.app_name) + "/" + safe;
        }
        File dir = new File(getExternalFilesDir(Environment.DIRECTORY_MUSIC), getString(R.string.app_name));
        dir.mkdirs();
        File f = new File(dir, safe);
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
        return f.getAbsolutePath();
      } catch (Exception e) { return "error: " + e.getMessage(); }
    }
  }

  /* ---------------- MIDI ---------------- */
  class Midi {
    final MidiManager mm = Build.VERSION.SDK_INT >= 23 ? (MidiManager) getSystemService(Context.MIDI_SERVICE) : null;
    final Map<String, MidiDevice> open = new HashMap<>();
    final Map<String, MidiInputPort> outs = new HashMap<>();
    final Map<String, MidiOutputPort> ins = new HashMap<>();
    boolean watching;

    @JavascriptInterface public boolean available() { return mm != null; }
    /** every device's ports as JSON: [{id, name, kind:"input"|"output"}] (input = notes come to us) */
    @JavascriptInterface public String list() {
      JSONArray a = new JSONArray();
      if (mm == null) return a.toString();
      if (!watching) {
        watching = true;
        mm.registerDeviceCallback(new MidiManager.DeviceCallback() {
          @Override public void onDeviceAdded(MidiDeviceInfo i) { js(web, "window.__drMidiChanged && __drMidiChanged()"); }
          @Override public void onDeviceRemoved(MidiDeviceInfo i) { js(web, "window.__drMidiChanged && __drMidiChanged()"); }
        }, ui);
      }
      try {
        for (MidiDeviceInfo d : mm.getDevices()) {
          String name = d.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME, "MIDI " + d.getId());
          for (MidiDeviceInfo.PortInfo p : d.getPorts()) {
            JSONObject o = new JSONObject();
            boolean toUs = p.getType() == MidiDeviceInfo.PortInfo.TYPE_OUTPUT;
            o.put("id", d.getId() + ":" + p.getPortNumber() + ":" + (toUs ? "i" : "o"));
            o.put("name", name + (d.getOutputPortCount() + d.getInputPortCount() > 2 ? " " + (p.getPortNumber() + 1) : ""));
            o.put("kind", toUs ? "input" : "output");
            a.put(o);
          }
        }
      } catch (Exception e) { }
      return a.toString();
    }
    MidiDeviceInfo find(int id) { for (MidiDeviceInfo d : mm.getDevices()) if (d.getId() == id) return d; return null; }
    void withDevice(int id, java.util.function.Consumer<MidiDevice> then) {
      String k = "" + id;
      if (open.containsKey(k)) { then.accept(open.get(k)); return; }
      MidiDeviceInfo info = find(id);
      if (info == null) return;
      mm.openDevice(info, dev -> { if (dev == null) return; open.put(k, dev); then.accept(dev); }, ui);
    }
    /** start listening to an input port; messages arrive in JS as __twMidi(id, [bytes...]) */
    @JavascriptInterface public void listen(String id) {
      if (mm == null || ins.containsKey(id)) return;
      String[] p = id.split(":");
      ui.post(() -> withDevice(Integer.parseInt(p[0]), dev -> {
        MidiOutputPort port = dev.openOutputPort(Integer.parseInt(p[1]));
        if (port == null) return;
        ins.put(id, port);
        port.connect(new MidiReceiver() {
          @Override public void onSend(byte[] msg, int off, int count, long ts) {
            StringBuilder sb = new StringBuilder("__drMidi('").append(id).append("',[");
            for (int i = 0; i < count; i++) { if (i > 0) sb.append(','); sb.append(msg[off + i] & 0xff); }
            js(web, sb.append("])").toString());
          }
        });
      }));
    }
    @JavascriptInterface public void send(String id, String bytes) {
      if (mm == null) return;
      String[] p = id.split(":");
      ui.post(() -> {
        MidiInputPort port = outs.get(id);
        if (port == null) {
          withDevice(Integer.parseInt(p[0]), dev -> { MidiInputPort q = dev.openInputPort(Integer.parseInt(p[1])); if (q != null) { outs.put(id, q); write(q, bytes); } });
        } else write(port, bytes);
      });
    }
    void write(MidiInputPort port, String bytes) {
      try {
        String[] parts = bytes.split(",");
        byte[] b = new byte[parts.length];
        for (int i = 0; i < parts.length; i++) b[i] = (byte) Integer.parseInt(parts[i].trim());
        port.send(b, 0, b.length);
      } catch (Exception e) { }
    }
  }

  boolean pickTree = false;
  volatile boolean wantFolder = false;

  // Every audio file in the picked folder and its subfolders, as document Uris the page can read
  Uri[] folderFiles(Uri tree) {
    java.util.ArrayList<Uri> got = new java.util.ArrayList<>();
    java.util.ArrayDeque<String> dirs = new java.util.ArrayDeque<>();
    dirs.add(android.provider.DocumentsContract.getTreeDocumentId(tree));
    String[] cols = { android.provider.DocumentsContract.Document.COLUMN_DOCUMENT_ID,
      android.provider.DocumentsContract.Document.COLUMN_DISPLAY_NAME, android.provider.DocumentsContract.Document.COLUMN_MIME_TYPE };
    while (!dirs.isEmpty() && got.size() < 512) {
      Uri kids = android.provider.DocumentsContract.buildChildDocumentsUriUsingTree(tree, dirs.poll());
      try (android.database.Cursor c = getContentResolver().query(kids, cols, null, null, null)) {
        while (c != null && c.moveToNext()) {
          String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
          if (android.provider.DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) { dirs.add(id); continue; }
          String n = name == null ? "" : name.toLowerCase();
          if ((mime != null && mime.startsWith("audio/")) || n.matches(".*\\.(wav|aif|aiff|flac|mp3|ogg|m4a)$"))
            got.add(android.provider.DocumentsContract.buildDocumentUriUsingTree(tree, id));
        }
      } catch (Exception e) { }
    }
    if (got.isEmpty()) toast("No sounds in that folder");
    return got.toArray(new Uri[0]);
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    if (req != PICK || pending == null) { super.onActivityResult(req, res, data); return; }
    Uri[] out = null;
    if (res == RESULT_OK && data != null) {
      if (pickTree && data.getData() != null) out = folderFiles(data.getData());
      else if (data.getClipData() != null) {
        android.content.ClipData c = data.getClipData();
        out = new Uri[c.getItemCount()];
        for (int k = 0; k < out.length; k++) out[k] = c.getItemAt(k).getUri();
      } else if (data.getData() != null) out = new Uri[] { data.getData() };
    }
    pickTree = false;
    pending.onReceiveValue(out); pending = null;
  }

  // Back: let the page close what's open; on the main screen press twice to leave
  @Override public void onBackPressed() {
    web.evaluateJavascript("(function(){var d=document.querySelector('dialog[open]');if(d){d.close();return true;}return false;})()", v -> {
      if ("true".equals(v)) return;
      long now = System.currentTimeMillis();
      if (now - lastBack < 2000) finish();
      else { lastBack = now; Toast.makeText(this, "Press back again to leave", Toast.LENGTH_SHORT).show(); }
    });
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onPause() { super.onPause(); web.onPause(); }
  @Override protected void onResume() { super.onResume(); web.onResume(); }
}
