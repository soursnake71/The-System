package com.thesnakedevice.app;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.PackageManager;
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
import android.webkit.PermissionRequest;
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

/** MXR Flanger with the web app bundled inside (assets/flanger), so it works offline and needs no website.
 *  Supplies what Android's WebView doesn't do on its own:
 *  - the microphone / USB audio interface for live input (RECORD_AUDIO, then the page's getUserMedia request),
 *  - an audio file picker for the file player,
 *  - AndroidFlanger.save: WAV exports go to Downloads/MXR Flanger. */
public class FlangerActivity extends Activity {
  static final String HOME = "https://appassets.androidplatform.net/assets/flanger/index.html";
  static final int PICK = 7, MIC = 8;
  WebView web;
  ValueCallback<Uri[]> pending;
  PermissionRequest pendingMic;
  long lastBack;
  final Handler ui = new Handler(Looper.getMainLooper());
  WebViewAssetLoader assets;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    assets = new WebViewAssetLoader.Builder().addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
    web = new WebView(this);
    web.setHapticFeedbackEnabled(false);
    web.setLongClickable(false);
    web.setOnLongClickListener(v -> true);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setAllowFileAccess(false);
    s.setAllowContentAccess(false);
    web.setWebViewClient(new WebViewClient() {
      @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) { return assets.shouldInterceptRequest(r.getUrl()); }
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        if ("appassets.androidplatform.net".equals(u.getHost())) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { }
        return true;
      }
    });
    web.addJavascriptInterface(new Files(), "AndroidFlanger");
    web.setWebChromeClient(new WebChromeClient() {
      @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
        if (pending != null) pending.onReceiveValue(null);
        pending = cb;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
          .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"audio/*", "video/*", "application/octet-stream"});
        try { startActivityForResult(i, PICK); }
        catch (ActivityNotFoundException e) { pending = null; cb.onReceiveValue(null); toast("No file picker on this device"); }
        return true;
      }
      // the page asks for the mic: grant it once Android has granted RECORD_AUDIO to the app
      @Override public void onPermissionRequest(final PermissionRequest r) {
        ui.post(() -> {
          boolean audio = false;
          for (String res : r.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(res)) audio = true;
          if (!audio) { r.deny(); return; }
          if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            r.grant(new String[] {PermissionRequest.RESOURCE_AUDIO_CAPTURE});
          } else {
            if (pendingMic != null) pendingMic.deny();
            pendingMic = r;
            requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, MIC);
          }
        });
      }
      @Override public void onPermissionRequestCanceled(PermissionRequest r) { if (pendingMic == r) pendingMic = null; }
    });
    // Android 15 draws apps under the status and navigation bars: keep the panel between them
    final android.widget.FrameLayout root = new android.widget.FrameLayout(this);
    root.setBackgroundColor(0xff141414);
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

  @Override public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
    if (req != MIC) { super.onRequestPermissionsResult(req, perms, res); return; }
    if (pendingMic == null) return;
    if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) pendingMic.grant(new String[] {PermissionRequest.RESOURCE_AUDIO_CAPTURE});
    else { pendingMic.deny(); toast("Live input needs the microphone permission"); }
    pendingMic = null;
  }

  void toast(String s) { ui.post(() -> Toast.makeText(this, s, Toast.LENGTH_LONG).show()); }

  /* ---------------- saving exports ---------------- */
  class Files {
    @JavascriptInterface public String save(String base64, String name, String mime) {
      try {
        byte[] data = Base64.decode(base64, Base64.DEFAULT);
        String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        if (Build.VERSION.SDK_INT >= 29) {
          ContentValues v = new ContentValues();
          v.put(MediaStore.Downloads.DISPLAY_NAME, safe);
          v.put(MediaStore.Downloads.MIME_TYPE, mime);
          v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/MXR Flanger");
          Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
          try (OutputStream o = getContentResolver().openOutputStream(u)) { o.write(data); }
          return "Download/MXR Flanger/" + safe;
        }
        File dir = new File(getExternalFilesDir(Environment.DIRECTORY_MUSIC), "MXR Flanger");
        dir.mkdirs();
        File f = new File(dir, safe);
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
        return f.getAbsolutePath();
      } catch (Exception e) { return "error: " + e.getMessage(); }
    }
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    if (req != PICK || pending == null) { super.onActivityResult(req, res, data); return; }
    Uri[] out = null;
    if (res == RESULT_OK && data != null && data.getData() != null) out = new Uri[] { data.getData() };
    pending.onReceiveValue(out); pending = null;
  }

  // Back: press twice to leave
  @Override public void onBackPressed() {
    long now = System.currentTimeMillis();
    if (now - lastBack < 2000) finish();
    else { lastBack = now; Toast.makeText(this, "Press back again to leave", Toast.LENGTH_SHORT).show(); }
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onPause() { super.onPause(); web.onPause(); }
  @Override protected void onResume() { super.onResume(); web.onResume(); }
  @Override protected void onDestroy() { web.destroy(); super.onDestroy(); }
}
