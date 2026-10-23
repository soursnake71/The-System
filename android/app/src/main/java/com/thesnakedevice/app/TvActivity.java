package com.thesnakedevice.app;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

/** Google TV / Android TV: TVs usually have no Chrome, so the app runs in the system WebView.
 *  The remote's arrows and OK, game controllers (Gamepad API), file import and offline storage all work here. */
public class TvActivity extends Activity {
  static final String HOME = BuildConfig.START_URL;
  static final int PICK = 7;
  WebView web;
  ValueCallback<Uri[]> pending;
  long lastBack;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    web = new WebView(this);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setDatabaseEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setAllowFileAccess(false);
    s.setUserAgentString(s.getUserAgentString() + " Android TV OmegaSystemTV");
    web.setWebViewClient(new WebViewClient() {
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        if ("soursnake71.github.io".equals(u.getHost())) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { Toast.makeText(TvActivity.this, "No browser on this TV for " + u.getHost(), Toast.LENGTH_SHORT).show(); }
        return true;
      }
    });
    web.setWebChromeClient(new WebChromeClient() {
      @Override public void onPermissionRequest(PermissionRequest r) { r.deny(); }
      @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
        if (pending != null) pending.onReceiveValue(null);
        pending = cb;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
          .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE);
        try { startActivityForResult(i, PICK); }
        catch (ActivityNotFoundException e) {
          try { startActivityForResult(p.createIntent(), PICK); }
          catch (ActivityNotFoundException e2) { pending = null; cb.onReceiveValue(null); Toast.makeText(TvActivity.this, "Install a file manager app to import files on this TV", Toast.LENGTH_LONG).show(); }
        }
        return true;
      }
    });
    setContentView(web);
    if (b != null) web.restoreState(b); else web.loadUrl(HOME);
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    if (req != PICK || pending == null) { super.onActivityResult(req, res, data); return; }
    Uri[] out = null;
    if (res == RESULT_OK && data != null) {
      if (data.getClipData() != null) { int n = data.getClipData().getItemCount(); out = new Uri[n]; for (int k = 0; k < n; k++) out[k] = data.getClipData().getItemAt(k).getUri(); }
      else if (data.getData() != null) out = new Uri[] { data.getData() };
    }
    pending.onReceiveValue(out); pending = null;
  }

  // Back: let the page close what's open (pages, menus, dialogs); on the main screen press twice to leave
  @Override public void onBackPressed() {
    web.evaluateJavascript("(function(){var e=new KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true});(document.activeElement||document).dispatchEvent(e);return e.defaultPrevented;})()", v -> {
      if ("true".equals(v)) return;
      long now = System.currentTimeMillis();
      if (now - lastBack < 2000) finish();
      else { lastBack = now; Toast.makeText(this, "Press back again to leave", Toast.LENGTH_SHORT).show(); }
    });
  }

  @Override public void onWindowFocusChanged(boolean f) {
    super.onWindowFocusChanged(f);
    if (f) getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onPause() { super.onPause(); web.onPause(); }
  @Override protected void onResume() { super.onResume(); web.onResume(); }
}
