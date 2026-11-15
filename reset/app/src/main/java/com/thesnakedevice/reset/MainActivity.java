package com.thesnakedevice.reset;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.util.ArrayList;
import java.util.List;

/** Hosts the bundled web app (assets/www) and gives it a few phone features:
 *  the step counter, daily reminders, vibration and keep-screen-on for workouts. */
public class MainActivity extends Activity implements SensorEventListener {
  WebView web;
  SensorManager sm;
  Sensor stepSensor;
  volatile long steps = -1;   // steps since the phone booted, -1 if unknown

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    web = new WebView(this);
    WebSettings s = web.getSettings();
    s.setJavaScriptEnabled(true);
    s.setDomStorageEnabled(true);
    s.setMediaPlaybackRequiresUserGesture(false);
    web.addJavascriptInterface(new Bridge(), "Android");
    web.setWebViewClient(new WebViewClient() {
      @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
        Uri u = r.getUrl();
        if ("file".equals(u.getScheme())) return false;
        try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (Exception ignored) {}
        return true;
      }
    });
    setContentView(web);
    sm = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
    stepSensor = sm == null ? null : sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER);
    if (b != null) web.restoreState(b); else web.loadUrl("file:///android_asset/www/index.html");
    askPermissions();
  }

  void askPermissions() {
    List<String> want = new ArrayList<>();
    if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
      want.add(Manifest.permission.POST_NOTIFICATIONS);
    if (Build.VERSION.SDK_INT >= 29 && stepSensor != null && checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) != PackageManager.PERMISSION_GRANTED)
      want.add(Manifest.permission.ACTIVITY_RECOGNITION);
    if (!want.isEmpty()) requestPermissions(want.toArray(new String[0]), 1);
  }

  @Override public void onRequestPermissionsResult(int req, String[] p, int[] r) { startSteps(); }

  void startSteps() {
    if (stepSensor == null) return;
    if (Build.VERSION.SDK_INT >= 29 && checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) != PackageManager.PERMISSION_GRANTED) return;
    sm.registerListener(this, stepSensor, SensorManager.SENSOR_DELAY_NORMAL);
  }

  @Override protected void onResume() {
    super.onResume();
    startSteps();
    web.evaluateJavascript("window.onAppResume&&onAppResume()", null);
  }
  @Override protected void onPause() { super.onPause(); if (sm != null) sm.unregisterListener(this); }
  @Override protected void onSaveInstanceState(Bundle o) { super.onSaveInstanceState(o); web.saveState(o); }

  @Override public void onSensorChanged(SensorEvent e) { steps = (long) e.values[0]; }
  @Override public void onAccuracyChanged(Sensor s, int a) {}

  @Override public void onBackPressed() {
    web.evaluateJavascript("window.onBack?onBack():false", v -> { if (!"true".equals(v)) MainActivity.super.onBackPressed(); });
  }

  class Bridge {
    @JavascriptInterface public boolean hasStepSensor() { return stepSensor != null; }
    @JavascriptInterface public String getSteps() { return Long.toString(steps); }
    @JavascriptInterface public void schedule(int id, int hour, int minute, String title, String text) {
      Reminders.save(MainActivity.this, id, hour, minute, title, text);
      Reminders.arm(MainActivity.this, id);
    }
    @JavascriptInterface public void cancel(int id) { Reminders.remove(MainActivity.this, id); }
    @JavascriptInterface public void vibrate(int ms) {
      Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
      if (v != null) v.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
    }
    @JavascriptInterface public void keepScreenOn(boolean on) {
      runOnUiThread(() -> {
        if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
      });
    }
  }
}
