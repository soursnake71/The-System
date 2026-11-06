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
import android.widget.FrameLayout;
import android.widget.Toast;
import androidx.webkit.WebViewAssetLoader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

/** NAM A2 Finder with the web app bundled inside (assets/nam), so it needs no website.
 *  The WebView has no downloads or microphone of its own, so this activity supplies them:
 *  - AndroidNam.save: models, rigs and clips go to Downloads/NAM Finder,
 *  - the microphone (live guitar through a USB-C interface) after Android's permission prompt,
 *  - the file picker for .nam files, DIs and cab IRs.
 *  TONE3000 sign-in pages open inside the app so their redirect comes back here. */
public class NamActivity extends Activity {
  static final String HOME = "https://appassets.androidplatform.net/assets/nam/index.html";
  static final int PICK = 7, MIC = 8;
  WebView web;
  ValueCallback<Uri[]> pending;
  PermissionRequest micRequest;
  long lastBack;
  final Handler ui = new Handler(Looper.getMainLooper());
  WebViewAssetLoader assets;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    assets = new WebViewAssetLoader.Builder().addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
    web = new WebView(this);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setAllowFileAccess(false);
    s.setAllowContentAccess(false);
    web.addJavascriptInterface(new Files(), "AndroidNam");
    web.setWebViewClient(new WebViewClient() {
      @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) { return assets.shouldInterceptRequest(r.getUrl()); }
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        String h = u.getHost() == null ? "" : u.getHost();
        // the app itself and TONE3000's sign-in stay in the app; everything else opens in the browser
        if (h.equals("appassets.androidplatform.net") || h.equals("www.tone3000.com") || h.equals("tone3000.com")) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { }
        return true;
      }
    });
    web.setWebChromeClient(new WebChromeClient() {
      @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
        if (pending != null) pending.onReceiveValue(null);
        pending = cb;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
          .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE);
        try { startActivityForResult(i, PICK); }
        catch (ActivityNotFoundException e) { pending = null; cb.onReceiveValue(null); toast("No file picker on this device"); }
        return true;
      }
      @Override public void onPermissionRequest(PermissionRequest r) {
        boolean audio = false;
        for (String res : r.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(res)) audio = true;
        if (!audio) { r.deny(); return; }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
          r.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
        } else {
          micRequest = r;
          requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, MIC);
        }
      }
    });
    // Android 15 draws apps under the status and navigation bars: keep the page between them
    final FrameLayout root = new FrameLayout(this);
    root.setBackgroundColor(0xff16181b);
    root.addView(web, new FrameLayout.LayoutParams(-1, -1));
    root.setOnApplyWindowInsetsListener((v, ins) -> {
      if (Build.VERSION.SDK_INT >= 30) {
        android.graphics.Insets bars = ins.getInsets(android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout() | android.view.WindowInsets.Type.ime());
        v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
      } else v.setPadding(ins.getSystemWindowInsetLeft(), ins.getSystemWindowInsetTop(), ins.getSystemWindowInsetRight(), ins.getSystemWindowInsetBottom());
      return ins;
    });
    setContentView(root);
    if (b != null) web.restoreState(b); else web.loadUrl(HOME);
  }

  void toast(String s) { ui.post(() -> Toast.makeText(this, s, Toast.LENGTH_LONG).show()); }

  class Files {
    @JavascriptInterface public String save(String base64, String name, String mime) {
      try {
        byte[] data = Base64.decode(base64, Base64.DEFAULT);
        String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        String where;
        if (Build.VERSION.SDK_INT >= 29) {
          ContentValues v = new ContentValues();
          v.put(MediaStore.Downloads.DISPLAY_NAME, safe);
          v.put(MediaStore.Downloads.MIME_TYPE, mime);
          v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/NAM Finder");
          Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
          try (OutputStream o = getContentResolver().openOutputStream(u)) { o.write(data); }
          where = "Download/NAM Finder/" + safe;
        } else {
          File dir = new File(getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "NAM Finder");
          dir.mkdirs();
          File f = new File(dir, safe);
          try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
          where = f.getAbsolutePath();
        }
        toast("Saved to " + where);
        return where;
      } catch (Exception e) { toast("Couldn't save: " + e.getMessage()); return "error: " + e.getMessage(); }
    }
  }

  @Override public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
    if (req != MIC || micRequest == null) { super.onRequestPermissionsResult(req, perms, res); return; }
    if (res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED) micRequest.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
    else { micRequest.deny(); toast("Live guitar needs the microphone permission"); }
    micRequest = null;
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    if (req != PICK || pending == null) { super.onActivityResult(req, res, data); return; }
    Uri[] out = null;
    if (res == RESULT_OK && data != null) {
      if (data.getClipData() != null) {
        out = new Uri[data.getClipData().getItemCount()];
        for (int i = 0; i < out.length; i++) out[i] = data.getClipData().getItemAt(i).getUri();
      } else if (data.getData() != null) out = new Uri[] { data.getData() };
    }
    pending.onReceiveValue(out); pending = null;
  }

  // Back: go back inside the app (e.g. from the TONE3000 sign-in); on the main page press twice to leave
  @Override public void onBackPressed() {
    if (web.canGoBack()) { web.goBack(); return; }
    long now = System.currentTimeMillis();
    if (now - lastBack < 2000) finish();
    else { lastBack = now; Toast.makeText(this, "Press back again to leave", Toast.LENGTH_SHORT).show(); }
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onPause() { super.onPause(); web.onPause(); }
  @Override protected void onResume() { super.onResume(); web.onResume(); }
}
