package com.thesnakedevice.mkv2mp4;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
  private static final int PICK = 1;

  private TextView status, log;
  private ProgressBar progress;
  private Button cancel;
  private CheckBox maxCompat;

  @Override protected void onCreate(Bundle saved) {
    super.onCreate(saved);
    setContentView(R.layout.main);
    status = findViewById(R.id.status);
    log = findViewById(R.id.log);
    progress = findViewById(R.id.progress);
    cancel = findViewById(R.id.cancel);
    maxCompat = findViewById(R.id.max_compat);
    Button choose = findViewById(R.id.choose);
    choose.setOnClickListener(v -> pick());
    cancel.setOnClickListener(v -> startService(new Intent(this, ConvertService.class).setAction(ConvertService.ACTION_CANCEL)));
    choose.requestFocus();
    askPermissions();
    if (saved == null) handleShared(getIntent());
  }

  @Override protected void onNewIntent(Intent intent) {
    super.onNewIntent(intent);
    handleShared(intent);
  }

  @Override protected void onResume() {
    super.onResume();
    ConvertService.listener = this::render;
    render();
  }

  @Override protected void onPause() {
    super.onPause();
    ConvertService.listener = null;
  }

  private void pick() {
    Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        // some file providers call MKVs octet-stream, so don't filter those out
        .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"video/*", "application/x-matroska", "application/octet-stream"});
    try {
      startActivityForResult(i, PICK);
    } catch (RuntimeException e) {
      // some TVs ship without a file picker
      Toast.makeText(this, "No file picker on this device. Share the MKV to this app from a file manager instead.", Toast.LENGTH_LONG).show();
    }
  }

  @Override protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (request != PICK || result != RESULT_OK || data == null) return;
    List<Uri> uris = new ArrayList<>();
    if (data.getClipData() != null) {
      for (int i = 0; i < data.getClipData().getItemCount(); i++) uris.add(data.getClipData().getItemAt(i).getUri());
    } else if (data.getData() != null) {
      uris.add(data.getData());
    }
    for (Uri u : uris) {
      try {
        // keeps read access if the app is closed while the queue is still converting
        getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
      } catch (RuntimeException ignored) {
        // not every provider offers persistable grants; the grant passed to the service still covers it
      }
    }
    convert(uris);
  }

  /** Files opened with or shared to the app from a file manager. */
  private void handleShared(Intent intent) {
    if (intent == null) return;
    List<Uri> uris = new ArrayList<>();
    String action = intent.getAction();
    if (Intent.ACTION_VIEW.equals(action) && intent.getData() != null) {
      uris.add(intent.getData());
    } else if (Intent.ACTION_SEND.equals(action)) {
      Uri u = intent.getParcelableExtra(Intent.EXTRA_STREAM);
      if (u != null) uris.add(u);
    } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
      ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
      if (list != null) uris.addAll(list);
    }
    convert(uris);
  }

  private void convert(List<Uri> uris) {
    if (uris.isEmpty()) return;
    // the URIs ride in ClipData with a read grant, so the service can still read them after this screen closes
    ClipData clip = null;
    for (Uri u : uris) {
      if (clip == null) clip = ClipData.newRawUri("video", u);
      else clip.addItem(new ClipData.Item(u));
    }
    Intent i = new Intent(this, ConvertService.class).putExtra(ConvertService.EXTRA_MAX_COMPAT, maxCompat.isChecked())
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    i.setClipData(clip);
    if (Build.VERSION.SDK_INT >= 26) startForegroundService(i); else startService(i);
  }

  private void render() {
    ConvertService.State s = ConvertService.state;
    if (s.running) {
      String count = s.total > 1 ? " (" + Math.min(s.done + 1, s.total) + " of " + s.total + ")" : "";
      status.setText(s.current.isEmpty() ? "Starting…" : "Converting " + s.current + count
          + (s.progress >= 0 ? " — " + Math.round(s.progress * 100) + "%" : ""));
      progress.setVisibility(View.VISIBLE);
      progress.setIndeterminate(s.progress < 0);
      if (s.progress >= 0) progress.setProgress((int) (s.progress * 1000));
      cancel.setVisibility(View.VISIBLE);
    } else {
      status.setText(R.string.idle);
      progress.setVisibility(View.GONE);
      cancel.setVisibility(View.GONE);
    }
    StringBuilder b = new StringBuilder();
    for (int i = s.log.size() - 1; i >= 0; i--) b.append(s.log.get(i)).append("\n\n");
    log.setText(b.toString().trim());
  }

  private void askPermissions() {
    List<String> need = new ArrayList<>();
    if (Build.VERSION.SDK_INT >= 33) need.add(Manifest.permission.POST_NOTIFICATIONS);
    if (Build.VERSION.SDK_INT <= 29) need.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
    need.removeIf(p -> checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED);
    if (!need.isEmpty()) requestPermissions(need.toArray(new String[0]), 2);
  }
}
