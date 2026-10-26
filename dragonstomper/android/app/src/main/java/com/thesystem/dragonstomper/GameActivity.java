package com.thesystem.dragonstomper;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;
import androidx.webkit.WebViewAssetLoader;

/** The game, served from the APK's assets over https://appassets.androidplatform.net/
 *  (a real origin, so ES modules, fetch and saves work as in a browser). Controllers reach
 *  the page through the Gamepad API. */
public class GameActivity extends Activity {
  static final String START = "https://appassets.androidplatform.net/game/index.html";
  WebView web;
  long lastBack;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    final WebViewAssetLoader assets = new WebViewAssetLoader.Builder()
        .addPathHandler("/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
    web = new WebView(this);
    web.setBackgroundColor(0xff07080c);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    s.setAllowFileAccess(false);
    s.setAllowContentAccess(false);
    s.setUserAgentString(s.getUserAgentString() + " DragonstomperAndroid");
    web.setWebViewClient(new WebViewClient() {
      @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) { return assets.shouldInterceptRequest(r.getUrl()); }
      // the game never navigates away; anything else (font CSS links etc.) stays put
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) { return !"appassets.androidplatform.net".equals(r.getUrl().getHost()); }
    });
    web.setFocusable(true); web.setFocusableInTouchMode(true);
    setContentView(web);
    web.requestFocus();
    if (b != null) web.restoreState(b); else web.loadUrl(START);
  }

  static boolean fromController(KeyEvent e) {
    int src = e.getSource();
    return (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
  }

  // Controller buttons go to the game (Android would otherwise treat B / Circle as Back)
  @Override public boolean dispatchKeyEvent(KeyEvent e) {
    if (fromController(e)) return web.dispatchKeyEvent(e) || true;
    return super.dispatchKeyEvent(e);
  }
  @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
    if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) return web.dispatchGenericMotionEvent(e) || true;
    return super.dispatchGenericMotionEvent(e);
  }

  // The phone's or remote's Back: pause / close menus; on the title screen press twice to leave
  @Override public void onBackPressed() {
    web.evaluateJavascript("(function(){var t=document.getElementById('title');if(t&&t.classList.contains('on'))return false;window.dispatchEvent(new Event('ds-back'));return true;})()", v -> {
      if ("true".equals(v)) return;
      long now = System.currentTimeMillis();
      if (now - lastBack < 2000) finish();
      else { lastBack = now; Toast.makeText(this, "Press back again to quit", Toast.LENGTH_SHORT).show(); }
    });
  }

  @Override public void onWindowFocusChanged(boolean f) {
    super.onWindowFocusChanged(f);
    if (f) getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
  }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }
  @Override protected void onPause() { web.evaluateJavascript("window.dispatchEvent(new Event('ds-pause'))", null); web.onPause(); super.onPause(); }
  @Override protected void onResume() { super.onResume(); web.onResume(); }
}
