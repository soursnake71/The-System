package com.thesnakedevice.mkv2mp4;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class MainActivity extends Activity {
  private static final int PICK = 1, PICK_FOLDER = 2, PICK_OUTPUT = 3;
  private static final int[] BITRATE_IDS = {R.id.kbps_6000, R.id.kbps_10000, R.id.kbps_20000, R.id.kbps_0};
  private static final int[] BITRATE_KBPS = {6000, 10000, 20000, 0};

  private TextView status, log, output;
  private ProgressBar progress;
  private Button cancel;
  private CheckBox maxCompat, keepSubs;
  private RadioGroup bitrate;
  private SharedPreferences prefs;

  @Override protected void onCreate(Bundle saved) {
    super.onCreate(saved);
    setContentView(R.layout.main);
    prefs = getSharedPreferences("settings", MODE_PRIVATE);
    status = findViewById(R.id.status);
    log = findViewById(R.id.log);
    output = findViewById(R.id.output);
    progress = findViewById(R.id.progress);
    cancel = findViewById(R.id.cancel);
    maxCompat = findViewById(R.id.max_compat);
    keepSubs = findViewById(R.id.keep_subs);
    bitrate = findViewById(R.id.bitrate);
    Button choose = findViewById(R.id.choose);
    choose.setOnClickListener(v -> pick());
    findViewById(R.id.choose_folder).setOnClickListener(v -> pickFolder(PICK_FOLDER));
    findViewById(R.id.change_output).setOnClickListener(v -> pickFolder(PICK_OUTPUT));
    findViewById(R.id.default_output).setOnClickListener(v -> setOutput(null));
    cancel.setOnClickListener(v -> startService(new Intent(this, ConvertService.class).setAction(ConvertService.ACTION_CANCEL)));

    maxCompat.setChecked(prefs.getBoolean("max_compat", false));
    maxCompat.setOnCheckedChangeListener((v, on) -> prefs.edit().putBoolean("max_compat", on).apply());
    keepSubs.setChecked(prefs.getBoolean("keep_subs", false));
    keepSubs.setOnCheckedChangeListener((v, on) -> prefs.edit().putBoolean("keep_subs", on).apply());
    int kbps = prefs.getInt("max_kbps", 10000);
    for (int i = 0; i < BITRATE_KBPS.length; i++) if (BITRATE_KBPS[i] == kbps) bitrate.check(BITRATE_IDS[i]);
    bitrate.setOnCheckedChangeListener((g, id) -> prefs.edit().putInt("max_kbps", maxKbps()).apply());
    showOutput();

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

  private int maxKbps() {
    for (int i = 0; i < BITRATE_IDS.length; i++) if (bitrate.getCheckedRadioButtonId() == BITRATE_IDS[i]) return BITRATE_KBPS[i];
    return 10000;
  }

  private void pick() {
    Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        // some file providers call MKVs octet-stream, so don't filter those out
        .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"video/*", "application/x-matroska", "application/octet-stream"});
    launch(i, PICK);
  }

  private void pickFolder(int request) {
    launch(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), request);
  }

  private void launch(Intent i, int request) {
    try {
      startActivityForResult(i, request);
    } catch (RuntimeException e) {
      // some TVs ship without a file picker
      Toast.makeText(this, "No file picker on this device. Share the MKV to this app from a file manager instead.", Toast.LENGTH_LONG).show();
    }
  }

  @Override protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (result != RESULT_OK || data == null) return;
    if (request == PICK_OUTPUT && data.getData() != null) {
      setOutput(data.getData());
    } else if (request == PICK_FOLDER && data.getData() != null) {
      // keeps access while the batch runs, even if the app is closed
      keep(data.getData(), Intent.FLAG_GRANT_READ_URI_PERMISSION);
      convert(Collections.singletonList(data.getData()), true);
    } else if (request == PICK) {
      List<Uri> uris = new ArrayList<>();
      if (data.getClipData() != null) {
        for (int i = 0; i < data.getClipData().getItemCount(); i++) uris.add(data.getClipData().getItemAt(i).getUri());
      } else if (data.getData() != null) {
        uris.add(data.getData());
      }
      for (Uri u : uris) keep(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
      convert(uris, false);
    }
  }

  private void keep(Uri uri, int flags) {
    try {
      getContentResolver().takePersistableUriPermission(uri, flags);
    } catch (RuntimeException ignored) {
      // not every provider offers persistable grants; the grant passed to the service still covers it
    }
  }

  private void setOutput(Uri tree) {
    String old = prefs.getString("out_tree", null);
    if (old != null && !old.equals(String.valueOf(tree))) {
      try {
        getContentResolver().releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
      } catch (RuntimeException ignored) {
        // already gone
      }
    }
    if (tree != null) keep(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
    prefs.edit().putString("out_tree", tree == null ? null : tree.toString()).apply();
    showOutput();
  }

  /** The chosen output folder, or null for the default (also when access to it has been lost). */
  private Uri outputTree() {
    String s = prefs.getString("out_tree", null);
    if (s == null) return null;
    Uri tree = Uri.parse(s);
    for (UriPermission p : getContentResolver().getPersistedUriPermissions()) {
      if (p.getUri().equals(tree) && p.isWritePermission()) return tree;
    }
    return null;
  }

  private void showOutput() {
    Uri tree = outputTree();
    if (tree == null) {
      output.setText(ConvertService.DEFAULT_OUT);
      return;
    }
    // "primary:Movies/TV" reads as "Movies/TV"; SD cards and other providers show their own id
    String id = DocumentsContract.getTreeDocumentId(tree);
    output.setText(id.startsWith("primary:") ? (id.length() > 8 ? id.substring(8) : "Internal storage") : id);
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
    convert(uris, false);
  }

  private void convert(List<Uri> uris, boolean folders) {
    if (uris.isEmpty()) return;
    // the URIs ride in ClipData with a read grant, so the service can still read them after this screen closes
    ClipData clip = null;
    for (Uri u : uris) {
      if (clip == null) clip = ClipData.newRawUri("video", u);
      else clip.addItem(new ClipData.Item(u));
    }
    Uri out = outputTree();
    Intent i = new Intent(this, ConvertService.class)
        .putExtra(ConvertService.EXTRA_MAX_COMPAT, maxCompat.isChecked())
        .putExtra(ConvertService.EXTRA_MAX_KBPS, maxKbps())
        .putExtra(ConvertService.EXTRA_KEEP_SUBS, keepSubs.isChecked())
        .putExtra(ConvertService.EXTRA_FOLDERS, folders)
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    if (out != null) i.putExtra(ConvertService.EXTRA_OUT_TREE, out.toString());
    i.setClipData(clip);
    if (Build.VERSION.SDK_INT >= 26) startForegroundService(i); else startService(i);
  }

  private void render() {
    ConvertService.State s = ConvertService.state;
    if (s.running) {
      String count = s.total > 1 ? " (" + Math.min(s.done + 1, s.total) + " of " + s.total + ")" : "";
      status.setText(s.current.isEmpty() ? "Starting…" : s.current + count
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
    if (!need.isEmpty()) requestPermissions(need.toArray(new String[0]), 4);
  }
}
