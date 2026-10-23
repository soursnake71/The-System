package com.thesnakedevice.sky;

import android.Manifest;
import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Base64;
import android.view.WindowManager;
import android.webkit.GeolocationPermissions;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;
import androidx.webkit.WebViewAssetLoader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONArray;
import org.json.JSONObject;

/** Sky Sequencer: the web app from the APK's assets in a WebView, plus a small bridge (window.SkyNative)
 *  for the things a page can't do itself here: flight-feed requests, USB MIDI out and saving MIDI files. */
public class SkyActivity extends Activity {
  static final String HOME = "https://appassets.androidplatform.net/assets/index.html";
  static final int LOCATION = 3;
  WebView web;
  WebViewAssetLoader assets;
  final Handler main = new Handler(Looper.getMainLooper());
  final ExecutorService net = Executors.newFixedThreadPool(3);
  GeolocationPermissions.Callback geoCb; String geoOrigin;
  long lastBack;

  // MIDI out: one open device port at a time
  MidiManager midi;
  MidiDevice midiDev; MidiInputPort midiPort; String midiId = "";

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    getWindow().setStatusBarColor(Color.parseColor("#04100c"));
    getWindow().setNavigationBarColor(Color.parseColor("#030b08"));
    if (getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI)) {
      midi = (MidiManager) getSystemService(Context.MIDI_SERVICE);
      midi.registerDeviceCallback(new MidiManager.DeviceCallback() {
        @Override public void onDeviceAdded(MidiDeviceInfo d) { js("window.__skyMidi&&__skyMidi()"); }
        @Override public void onDeviceRemoved(MidiDeviceInfo d) { if (midiId.startsWith(d.getId() + ":")) closeMidi(); js("window.__skyMidi&&__skyMidi()"); }
      }, main);
    }
    assets = new WebViewAssetLoader.Builder().addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
    web = new WebView(this);
    web.setBackgroundColor(Color.parseColor("#030b08"));
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setGeolocationEnabled(true);
    s.setAllowFileAccess(false);
    web.addJavascriptInterface(new Bridge(), "SkyNative");
    web.setWebViewClient(new WebViewClient() {
      @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) { return assets.shouldInterceptRequest(r.getUrl()); }
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        if ("appassets.androidplatform.net".equals(u.getHost())) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (Exception e) {}
        return true;
      }
    });
    web.setWebChromeClient(new WebChromeClient() {
      @Override public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback cb) {
        if (checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED) { cb.invoke(origin, true, false); return; }
        geoCb = cb; geoOrigin = origin;
        requestPermissions(new String[] { Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission.ACCESS_FINE_LOCATION }, LOCATION);
      }
    });
    setContentView(web);
    if (b != null) web.restoreState(b); else web.loadUrl(HOME);
  }

  @Override public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
    if (req != LOCATION || geoCb == null) return;
    boolean ok = false; for (int r : res) ok |= r == PackageManager.PERMISSION_GRANTED;
    geoCb.invoke(geoOrigin, ok, false); geoCb = null;
  }

  void js(String code) { main.post(() -> { if (web != null) web.evaluateJavascript(code, null); }); }

  void closeMidi() {
    try { if (midiPort != null) midiPort.close(); } catch (Exception e) {}
    try { if (midiDev != null) midiDev.close(); } catch (Exception e) {}
    midiPort = null; midiDev = null; midiId = "";
  }

  class Bridge {
    /** GET a URL off the main thread; answers with window.__skyReply(id, ok, text). */
    @JavascriptInterface public void fetch(String id, String url) {
      net.execute(() -> {
        boolean ok = false; String body;
        try {
          Uri u = Uri.parse(url);
          if (!"https".equals(u.getScheme()) && !"http".equals(u.getScheme())) throw new Exception("Only web addresses can be read");
          HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
          c.setConnectTimeout(10000); c.setReadTimeout(12000);
          c.setRequestProperty("User-Agent", "SkySequencer/1.0 (Android)");
          c.setRequestProperty("Accept", "application/json");
          int code = c.getResponseCode();
          InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
          ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] buf = new byte[16384]; int n;
          if (in != null) { while ((n = in.read(buf)) > 0) o.write(buf, 0, n); in.close(); }
          c.disconnect();
          if (code >= 400) body = "HTTP " + code + (code == 429 ? " (too many requests, slow down or try another feed)" : "");
          else { body = o.toString("UTF-8"); ok = true; }
        } catch (Exception e) { body = e.getMessage() == null ? e.toString() : e.getMessage(); }
        js("__skyReply(" + JSONObject.quote(id) + "," + ok + "," + JSONObject.quote(body) + ")");
      });
    }

    /** MIDI devices that accept notes, as [{id:"device:port", name}] */
    @JavascriptInterface public String midiOutputs() {
      JSONArray a = new JSONArray();
      if (midi == null) return a.toString();
      for (MidiDeviceInfo d : midi.getDevices()) {
        Bundle p = d.getProperties();
        String name = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        if (name == null) name = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT, "MIDI device");
        int ports = d.getInputPortCount();
        for (int k = 0; k < ports; k++) {
          try { a.put(new JSONObject().put("id", d.getId() + ":" + k).put("name", name + (ports > 1 ? " (port " + (k + 1) + ")" : ""))); } catch (Exception e) {}
        }
      }
      return a.toString();
    }

    @JavascriptInterface public void midiOpen(String id) {
      main.post(() -> {
        closeMidi();
        if (midi == null || id == null || id.isEmpty()) return;
        String[] parts = id.split(":"); if (parts.length != 2) return;
        int dev = Integer.parseInt(parts[0]), port = Integer.parseInt(parts[1]);
        for (MidiDeviceInfo d : midi.getDevices()) if (d.getId() == dev) {
          midi.openDevice(d, device -> {
            if (device == null) { Toast.makeText(SkyActivity.this, "Couldn't open that MIDI device", Toast.LENGTH_SHORT).show(); return; }
            midiDev = device; midiPort = device.openInputPort(port); midiId = id;
            if (midiPort == null) { Toast.makeText(SkyActivity.this, "That MIDI port is busy", Toast.LENGTH_SHORT).show(); closeMidi(); }
          }, main);
          return;
        }
      });
    }

    /** bytes as hex, sent delayMs from now (Android schedules it, so timing stays tight) */
    @JavascriptInterface public void midiSend(String hex, double delayMs) {
      MidiInputPort p = midiPort; if (p == null) return;
      byte[] b = new byte[hex.length() / 2];
      for (int k = 0; k < b.length; k++) b[k] = (byte) Integer.parseInt(hex.substring(k * 2, k * 2 + 2), 16);
      try { p.send(b, 0, b.length, System.nanoTime() + (long) (delayMs * 1e6)); } catch (Exception e) {}
    }

    /** saves into Downloads; returns a message for the page to show */
    @JavascriptInterface public String saveFile(String name, String base64, String mime) {
      byte[] data = Base64.decode(base64, Base64.DEFAULT);
      try {
        if (Build.VERSION.SDK_INT >= 29) {
          ContentValues v = new ContentValues();
          v.put(MediaStore.Downloads.DISPLAY_NAME, name);
          v.put(MediaStore.Downloads.MIME_TYPE, mime);
          Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
          try (OutputStream o = getContentResolver().openOutputStream(u)) { o.write(data); }
          return "Saved " + name + " to Downloads";
        }
        File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), f = new File(dir, name);
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
        return "Saved " + f.getAbsolutePath();
      } catch (Exception e) { return "Couldn't save: " + e.getMessage(); }
    }
  }

  // Back: press twice to leave (the music keeps playing in the background until then)
  @Override public void onBackPressed() {
    long now = System.currentTimeMillis();
    if (now - lastBack < 2000) finish();
    else { lastBack = now; Toast.makeText(this, "Press back again to leave", Toast.LENGTH_SHORT).show(); }
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onDestroy() { closeMidi(); net.shutdownNow(); if (web != null) web.destroy(); web = null; super.onDestroy(); }
}
