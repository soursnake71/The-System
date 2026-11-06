package app.callbuddy;

import android.Manifest;
import android.app.Activity;
import android.app.role.RoleManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.ViewGroup;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.JavascriptInterface;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The Call Buddy app: the same screens as the iPhone version (served by your Call Buddy server),
 * plus an Android-only bridge that lets the page turn on automatic call screening.
 */
public class MainActivity extends Activity {
  private static final int REQ_ROLE = 1, REQ_MIC = 2, REQ_CONTACTS = 3;
  private WebView web;
  private PermissionRequest pendingMic;

  @Override
  protected void onCreate(Bundle saved) {
    super.onCreate(saved);
    String url = Prefs.get(this).getString(Prefs.URL, "");
    if (url.isEmpty()) showServerPrompt(); else showApp(url);
  }

  // ---------- first run: where is your Call Buddy server? ----------
  private void showServerPrompt() {
    LinearLayout box = new LinearLayout(this);
    box.setOrientation(LinearLayout.VERTICAL);
    box.setGravity(Gravity.CENTER_VERTICAL);
    int pad = (int) (24 * getResources().getDisplayMetrics().density);
    box.setPadding(pad, pad, pad, pad);

    TextView title = new TextView(this);
    title.setText("Call Buddy");
    title.setTextSize(32);
    TextView help = new TextView(this);
    help.setText("Enter your Call Buddy server address (the link Render gave you, like https://callbuddy-xxxx.onrender.com).");
    help.setTextSize(16);
    help.setPadding(0, pad / 2, 0, pad / 2);
    EditText input = new EditText(this);
    input.setHint("https://…onrender.com");
    input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
    input.setSingleLine(true);
    Button go = new Button(this);
    go.setText("Continue");
    go.setOnClickListener(v -> {
      String u = input.getText().toString().trim().replaceAll("/+$", "");
      if (!u.startsWith("http")) u = "https://" + u;
      if (!u.startsWith("https://")) { Toast.makeText(this, "The address must start with https://", Toast.LENGTH_LONG).show(); return; }
      Prefs.get(this).edit().putString(Prefs.URL, u).apply();
      showApp(u);
    });
    box.addView(title); box.addView(help); box.addView(input); box.addView(go);
    setContentView(box);
  }

  // ---------- the app ----------
  private void showApp(String url) {
    web = new WebView(this);
    web.setBackgroundColor(Color.TRANSPARENT);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    web.addJavascriptInterface(new Bridge(), "CallBuddyAndroid");
    web.setWebViewClient(new WebViewClient() {
      @Override public boolean shouldOverrideUrlLoading(WebView v, android.webkit.WebResourceRequest r) {
        Uri u = r.getUrl();
        if (u.toString().startsWith(url)) return false;
        startActivity(new Intent(Intent.ACTION_VIEW, u)); // other links (Twilio, ElevenLabs) open in the browser
        return true;
      }
    });
    web.setWebChromeClient(new WebChromeClient() {
      @Override public void onPermissionRequest(PermissionRequest req) {
        runOnUiThread(() -> {
          if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            req.grant(new String[] {PermissionRequest.RESOURCE_AUDIO_CAPTURE});
          } else {
            pendingMic = req;
            requestPermissions(new String[] {Manifest.permission.RECORD_AUDIO}, REQ_MIC);
          }
        });
      }
    });
    web.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    setContentView(web);
    web.loadUrl(url + "/");
  }

  private boolean holdsScreeningRole() {
    RoleManager rm = getSystemService(RoleManager.class);
    return rm != null && rm.isRoleAvailable(RoleManager.ROLE_CALL_SCREENING) && rm.isRoleHeld(RoleManager.ROLE_CALL_SCREENING);
  }

  private void notifyPage() {
    if (web != null) web.evaluateJavascript("window.cbAndroidChanged && window.cbAndroidChanged()", null);
  }

  @Override
  public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
    boolean ok = results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED;
    if (code == REQ_MIC && pendingMic != null) {
      if (ok) pendingMic.grant(new String[] {PermissionRequest.RESOURCE_AUDIO_CAPTURE}); else pendingMic.deny();
      pendingMic = null;
    }
    if (code == REQ_CONTACTS) notifyPage();
  }

  @Override
  protected void onActivityResult(int code, int result, Intent data) {
    super.onActivityResult(code, result, data);
    if (code == REQ_ROLE) {
      if (holdsScreeningRole() && checkSelfPermission(Manifest.permission.READ_CONTACTS) != PackageManager.PERMISSION_GRANTED) {
        requestPermissions(new String[] {Manifest.permission.READ_CONTACTS}, REQ_CONTACTS);
      }
      notifyPage();
    }
  }

  @Override protected void onResume() { super.onResume(); notifyPage(); }

  @Override
  public void onBackPressed() {
    if (web != null && web.canGoBack()) web.goBack(); else super.onBackPressed();
  }

  /** Called from the page's JavaScript as window.CallBuddyAndroid.*  */
  class Bridge {
    /** The page sends the caller's numbers, the mode and the Twilio number every time it loads or changes. */
    @JavascriptInterface public void sync(String json) {
      try {
        JSONObject j = new JSONObject(json);
        JSONArray a = j.optJSONArray("numbers");
        StringBuilder nums = new StringBuilder();
        for (int i = 0; a != null && i < a.length(); i++) nums.append(i > 0 ? "," : "").append(a.getString(i));
        Prefs.get(MainActivity.this).edit()
            .putString(Prefs.NUMBERS, nums.toString())
            .putString(Prefs.MODE, j.optString("mode", "ai"))
            .putString(Prefs.TWILIO, j.optString("twilioNumber", ""))
            .apply();
      } catch (Exception ignored) {}
    }

    @JavascriptInterface public String status() {
      SharedPreferences p = Prefs.get(MainActivity.this);
      try {
        return new JSONObject()
            .put("role", holdsScreeningRole())
            .put("contacts", checkSelfPermission(Manifest.permission.READ_CONTACTS) == PackageManager.PERMISSION_GRANTED)
            .put("auto", p.getBoolean(Prefs.AUTO, false))
            .put("server", p.getString(Prefs.URL, ""))
            .toString();
      } catch (Exception e) { return "{}"; }
    }

    @JavascriptInterface public void setAuto(boolean on) {
      Prefs.get(MainActivity.this).edit().putBoolean(Prefs.AUTO, on).apply();
      if (on) runOnUiThread(MainActivity.this::askForScreening);
    }

    @JavascriptInterface public void enableScreening() { runOnUiThread(MainActivity.this::askForScreening); }

    /** Opens the phone dialer with the carrier's busy-forwarding code typed in; the user taps Call. */
    @JavascriptInterface public void dialCode(String code) {
      runOnUiThread(() -> startActivity(new Intent(Intent.ACTION_DIAL, Uri.parse("tel:" + Uri.encode(code)))));
    }

    @JavascriptInterface public void changeServer() {
      runOnUiThread(() -> { Prefs.get(MainActivity.this).edit().remove(Prefs.URL).apply(); web = null; showServerPrompt(); });
    }
  }

  private void askForScreening() {
    if (holdsScreeningRole()) {
      if (checkSelfPermission(Manifest.permission.READ_CONTACTS) != PackageManager.PERMISSION_GRANTED) {
        requestPermissions(new String[] {Manifest.permission.READ_CONTACTS}, REQ_CONTACTS);
      }
      notifyPage();
      return;
    }
    RoleManager rm = getSystemService(RoleManager.class);
    if (rm == null || !rm.isRoleAvailable(RoleManager.ROLE_CALL_SCREENING)) {
      Toast.makeText(this, "This phone doesn't allow call screening apps.", Toast.LENGTH_LONG).show();
      return;
    }
    startActivityForResult(rm.createRequestRoleIntent(RoleManager.ROLE_CALL_SCREENING), REQ_ROLE);
  }
}
