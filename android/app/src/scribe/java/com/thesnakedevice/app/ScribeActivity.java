package com.thesnakedevice.app;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.json.JSONArray;
import org.json.JSONObject;

/** Drum Scribe on Android. The analysis (separating the drums, splitting the kit, finding every hit) runs in the
 *  Drum Scribe engine on a computer on the same network: a phone can't run those models. This app sends it the link
 *  (or an audio file), follows the progress, then saves the MIDI to Downloads/Drum Scribe and can share it to a DAW. */
public class ScribeActivity extends Activity {
  static final int PICK_AUDIO = 11;
  static final int BG = 0xFF111317, PANEL = 0xFF1B1E24, LINE = 0xFF2C313A, TEXT = 0xFFE8EAEE, DIM = 0xFF9AA1AD,
      ACCENT = 0xFFF2A33A, BAD = 0xFFE5615B;

  final Handler ui = new Handler(Looper.getMainLooper());
  SharedPreferences prefs;
  EditText server, link, tempo, token;
  Spinner quality;
  Button go, pick, save, saveSplit, share;
  ProgressBar bar;
  TextView status, title, info;
  LinearLayout results, pieceList;
  HitsView hitsView;
  String jobId;
  JSONObject result;
  Uri lastSaved;
  boolean polling;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().setStatusBarColor(BG);
    getWindow().setNavigationBarColor(BG);
    prefs = getSharedPreferences("scribe", MODE_PRIVATE);
    buildUi();
    handleIntent(getIntent());
    if (b != null && b.getString("job") != null) { jobId = b.getString("job"); poll(); }
  }

  @Override protected void onSaveInstanceState(Bundle out) {
    super.onSaveInstanceState(out);
    if (jobId != null) out.putString("job", jobId);
  }

  @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); handleIntent(i); }

  void handleIntent(Intent i) {
    if (i == null || !Intent.ACTION_SEND.equals(i.getAction())) return;
    String text = i.getStringExtra(Intent.EXTRA_TEXT);
    Uri stream = i.getParcelableExtra(Intent.EXTRA_STREAM);
    if (text != null) {
      String url = firstUrl(text);
      if (url != null) { link.setText(url); start(); }
    } else if (stream != null) {
      upload(stream);
    }
  }

  /** Apps share "Song name - Artist https://..." : keep just the link. */
  static String firstUrl(String s) {
    Matcher m = Pattern.compile("(https?://\\S+|spotify:track:\\S+)").matcher(s);
    return m.find() ? m.group(1) : null;
  }

  /* ---------------- UI ---------------- */
  int dp(float v) { return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()); }

  TextView label(String s) {
    TextView t = new TextView(this);
    t.setText(s);
    t.setTextColor(DIM);
    t.setTextSize(13);
    t.setPadding(0, dp(10), 0, dp(4));
    return t;
  }

  EditText field(String hint, String value, int type) {
    EditText e = new EditText(this);
    e.setHint(hint);
    e.setText(value);
    e.setInputType(type);
    e.setSingleLine(true);
    e.setTextColor(TEXT);
    e.setHintTextColor(0xFF5D6470);
    e.setTextSize(15);
    e.setPadding(dp(10), dp(10), dp(10), dp(10));
    e.setBackground(box(0xFF0D0F12, LINE));
    return e;
  }

  GradientDrawable box(int fill, int stroke) {
    GradientDrawable g = new GradientDrawable();
    g.setColor(fill);
    g.setCornerRadius(dp(8));
    if (stroke != 0) g.setStroke(dp(1), stroke);
    return g;
  }

  Button button(String s, boolean primary) {
    Button bt = new Button(this);
    bt.setText(s);
    bt.setAllCaps(false);
    bt.setTextSize(15);
    bt.setTypeface(Typeface.DEFAULT_BOLD);
    bt.setTextColor(primary ? 0xFF1A1205 : TEXT);
    bt.setBackground(box(primary ? ACCENT : 0x00000000, primary ? 0 : LINE));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(46), 1);
    lp.setMargins(0, dp(10), dp(8), 0);
    bt.setLayoutParams(lp);
    return bt;
  }

  LinearLayout card() {
    LinearLayout c = new LinearLayout(this);
    c.setOrientation(LinearLayout.VERTICAL);
    c.setPadding(dp(14), dp(8), dp(14), dp(14));
    c.setBackground(box(PANEL, LINE));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    lp.setMargins(0, 0, 0, dp(14));
    c.setLayoutParams(lp);
    return c;
  }

  LinearLayout row(View... vs) {
    LinearLayout r = new LinearLayout(this);
    r.setOrientation(LinearLayout.HORIZONTAL);
    for (View v : vs) r.addView(v);
    return r;
  }

  void buildUi() {
    ScrollView scroll = new ScrollView(this);
    scroll.setBackgroundColor(BG);
    scroll.setFitsSystemWindows(true);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setPadding(dp(16), dp(16), dp(16), dp(32));
    scroll.addView(root);

    TextView h = new TextView(this);
    h.setText("DRUM SCRIBE");
    h.setTextColor(TEXT);
    h.setTextSize(22);
    h.setTypeface(Typeface.DEFAULT_BOLD);
    root.addView(h);
    TextView sub = new TextView(this);
    sub.setText("A song link in, every drum hit out as MIDI: unquantized, with velocities, every piece of the kit.");
    sub.setTextColor(DIM);
    sub.setPadding(0, dp(2), 0, dp(14));
    root.addView(sub);

    LinearLayout c1 = card();
    c1.addView(label("YouTube, Spotify or SoundCloud link (or Share a song to Drum Scribe)"));
    link = field("https://www.youtube.com/watch?v=...", "", InputType.TYPE_TEXT_VARIATION_URI);
    c1.addView(link);
    quality = new Spinner(this);
    ArrayAdapter<String> qa = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, new String[] {"Best separation (slower)", "Fast separation"}) {
      @Override public View getView(int p, View v, ViewGroup g) { TextView t = (TextView) super.getView(p, v, g); t.setTextColor(TEXT); return t; }
    };
    qa.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
    quality.setAdapter(qa);
    quality.setBackground(box(0xFF0D0F12, LINE));
    quality.setSelection(prefs.getInt("quality", 0));
    c1.addView(label("Separation"));
    c1.addView(quality);
    c1.addView(label("Your project tempo (optional, else detected)"));
    tempo = field("detect", "", InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
    c1.addView(tempo);
    go = button("Make drum MIDI", true);
    pick = button("Pick an audio file", false);
    c1.addView(row(go, pick));
    bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    bar.setMax(1000);
    bar.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
    LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(14));
    bl.setMargins(0, dp(12), 0, dp(4));
    bar.setLayoutParams(bl);
    c1.addView(bar);
    status = new TextView(this);
    status.setTextColor(DIM);
    status.setTextSize(14);
    c1.addView(status);
    root.addView(c1);

    results = card();
    results.setVisibility(View.GONE);
    title = new TextView(this);
    title.setTextColor(TEXT);
    title.setTextSize(18);
    title.setTypeface(Typeface.DEFAULT_BOLD);
    results.addView(title);
    info = new TextView(this);
    info.setTextColor(DIM);
    results.addView(info);
    save = button("Save MIDI", true);
    saveSplit = button("Track per piece", false);
    results.addView(row(save, saveSplit));
    share = button("Send MIDI to an app (DAW, Drive...)", false);
    results.addView(row(share));
    TextView hint = new TextView(this);
    hint.setText("Notes sit exactly where the drummer played them. Set your project to the tempo above before importing, and line the MIDI up with the start of the song.");
    hint.setTextColor(DIM);
    hint.setTextSize(13);
    hint.setPadding(0, dp(10), 0, dp(10));
    results.addView(hint);
    hitsView = new HitsView(this);
    hitsView.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(220)));
    results.addView(hitsView);
    pieceList = new LinearLayout(this);
    pieceList.setOrientation(LinearLayout.VERTICAL);
    results.addView(pieceList);
    root.addView(results);

    LinearLayout c3 = card();
    c3.addView(label("Engine address (the computer running drumscribe serve)"));
    server = field("http://192.168.1.20:8765", prefs.getString("server", ""), InputType.TYPE_TEXT_VARIATION_URI);
    c3.addView(server);
    c3.addView(label("Token (only if the engine has DRUMSCRIBE_TOKEN set)"));
    token = field("", prefs.getString("token", ""), InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
    c3.addView(token);
    Button test = button("Test connection", false);
    c3.addView(row(test));
    root.addView(c3);

    setContentView(scroll);

    go.setOnClickListener(v -> start());
    pick.setOnClickListener(v -> {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("*/*");
      i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"audio/*", "video/*"});
      startActivityForResult(i, PICK_AUDIO);
    });
    save.setOnClickListener(v -> download(false, false));
    saveSplit.setOnClickListener(v -> download(true, false));
    share.setOnClickListener(v -> download(false, true));
    test.setOnClickListener(v -> {
      remember();
      new Thread(() -> {
        try {
          JSONObject j = new JSONObject(new String(http("GET", "/api/health", null, null), StandardCharsets.UTF_8));
          ui.post(() -> toast("Connected: engine " + j.optString("version")));
        } catch (Exception e) { ui.post(() -> toast("Can't reach the engine: " + e.getMessage())); }
      }).start();
    });
  }

  void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

  void setStatus(String s, boolean bad) { status.setText(s); status.setTextColor(bad ? BAD : DIM); }

  void remember() {
    prefs.edit().putString("server", server.getText().toString().trim()).putString("token", token.getText().toString().trim())
        .putInt("quality", quality.getSelectedItemPosition()).apply();
  }

  String base() {
    String s = server.getText().toString().trim();
    if (s.isEmpty()) return null;
    if (!s.startsWith("http")) s = "http://" + s;
    if (!s.matches(".*:\\d+/?$")) s = s.replaceAll("/$", "") + ":8765";
    return s.replaceAll("/$", "");
  }

  byte[] http(String method, String path, byte[] body, String ctype) throws Exception {
    String b = base();
    if (b == null) throw new Exception("set the engine address below first");
    String tok = token.getText().toString().trim();
    HttpURLConnection c = (HttpURLConnection) new URL(b + path).openConnection();
    c.setRequestMethod(method);
    c.setConnectTimeout(8000);
    c.setReadTimeout(120000);
    if (!tok.isEmpty()) c.setRequestProperty("X-Token", tok);
    if (body != null) {
      c.setDoOutput(true);
      c.setRequestProperty("Content-Type", ctype);
      c.setFixedLengthStreamingMode(body.length);
      try (OutputStream o = c.getOutputStream()) { o.write(body); }
    }
    int code = c.getResponseCode();
    InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
    byte[] data = readAll(in);
    if (code >= 400) {
      String msg = new String(data, StandardCharsets.UTF_8);
      try { msg = new JSONObject(msg).optString("error", msg); } catch (Exception ignored) { }
      throw new Exception(msg);
    }
    return data;
  }

  static byte[] readAll(InputStream in) throws Exception {
    if (in == null) return new byte[0];
    ByteArrayOutputStream o = new ByteArrayOutputStream();
    byte[] buf = new byte[65536];
    int n;
    while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
    in.close();
    return o.toByteArray();
  }

  String tempoValue() {
    String t = tempo.getText().toString().trim();
    try { return t.isEmpty() ? null : String.valueOf(Double.parseDouble(t)); } catch (NumberFormatException e) { return null; }
  }

  void start() {
    remember();
    String l = link.getText().toString().trim();
    String u = firstUrl(l);
    if (u == null) { setStatus("Paste a YouTube, Spotify or SoundCloud link first.", true); return; }
    results.setVisibility(View.GONE);
    setStatus("Sending...", false);
    bar.setProgress(0);
    go.setEnabled(false);
    new Thread(() -> {
      try {
        JSONObject req = new JSONObject();
        req.put("link", u);
        req.put("quality", quality.getSelectedItemPosition() == 1 ? "fast" : "best");
        String t = tempoValue();
        if (t != null) req.put("tempo", Double.parseDouble(t));
        JSONObject j = new JSONObject(new String(http("POST", "/api/jobs", req.toString().getBytes(StandardCharsets.UTF_8), "application/json"), StandardCharsets.UTF_8));
        jobId = j.getString("id");
        ui.post(this::poll);
      } catch (Exception e) {
        ui.post(() -> { setStatus("Can't start: " + e.getMessage(), true); go.setEnabled(true); });
      }
    }).start();
  }

  void upload(Uri uri) {
    remember();
    results.setVisibility(View.GONE);
    go.setEnabled(false);
    String name = "song";
    try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
      if (c != null && c.moveToFirst()) {
        int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
        if (i >= 0) name = c.getString(i);
      }
    } catch (Exception ignored) { }
    final String fname = name;
    setStatus("Sending " + fname + "...", false);
    new Thread(() -> {
      try {
        byte[] data = readAll(getContentResolver().openInputStream(uri));
        String q = "/api/upload?name=" + URLEncoder.encode(fname, "UTF-8") + "&quality=" + (quality.getSelectedItemPosition() == 1 ? "fast" : "best");
        String t = tempoValue();
        if (t != null) q += "&tempo=" + t;
        JSONObject j = new JSONObject(new String(http("POST", q, data, "application/octet-stream"), StandardCharsets.UTF_8));
        jobId = j.getString("id");
        ui.post(this::poll);
      } catch (Exception e) {
        ui.post(() -> { setStatus("Can't send the file: " + e.getMessage(), true); go.setEnabled(true); });
      }
    }).start();
  }

  void poll() {
    if (polling || jobId == null) return;
    polling = true;
    go.setEnabled(false);
    new Thread(() -> {
      while (true) {
        try {
          JSONObject j = new JSONObject(new String(http("GET", "/api/jobs/" + jobId + "?hits=1", null, null), StandardCharsets.UTF_8));
          String state = j.optString("state");
          double p = j.optDouble("progress", 0);
          String msg = j.optString("message");
          if ("done".equals(state)) {
            JSONObject r = j.getJSONObject("result");
            ui.post(() -> { polling = false; go.setEnabled(true); bar.setProgress(1000); setStatus("Done: " + msg, false); show(r); });
            return;
          }
          if ("error".equals(state)) {
            ui.post(() -> { polling = false; go.setEnabled(true); setStatus(msg, true); });
            return;
          }
          ui.post(() -> { bar.setProgress((int) (p * 1000)); setStatus(msg + String.format(Locale.US, " (%d%%)", (int) (p * 100)), false); });
        } catch (Exception e) {
          ui.post(() -> setStatus("Waiting for the engine: " + e.getMessage(), true));
        }
        try { Thread.sleep(1500); } catch (InterruptedException e) { return; }
      }
    }).start();
  }

  void show(JSONObject r) {
    result = r;
    results.setVisibility(View.VISIBLE);
    title.setText(r.optString("title"));
    JSONArray hits = r.optJSONArray("hits");
    info.setText(String.format(Locale.US, "%.2f BPM  ·  %d hits  ·  %d s", r.optDouble("tempo"), hits == null ? 0 : hits.length(), (int) r.optDouble("duration")));
    pieceList.removeAllViews();
    JSONArray ps = r.optJSONArray("pieces");
    List<Integer> notes = new ArrayList<>();
    List<String> names = new ArrayList<>();
    for (int i = 0; ps != null && i < ps.length(); i++) {
      JSONObject p = ps.optJSONObject(i);
      notes.add(p.optInt("note"));
      names.add(p.optString("label"));
      TextView t = new TextView(this);
      t.setText(String.format(Locale.US, "%-18s note %3d   %5d hits", p.optString("label"), p.optInt("note"), p.optInt("count")));
      t.setTypeface(Typeface.MONOSPACE);
      t.setTextColor(TEXT);
      t.setTextSize(13);
      t.setPadding(0, dp(4), 0, dp(4));
      pieceList.addView(t);
    }
    hitsView.set(hits, notes, names, r.optDouble("duration", 1));
  }

  void download(boolean split, boolean thenShare) {
    if (jobId == null || result == null) return;
    String name = result.optString(split ? "midi_split" : "midi", "drums.mid");
    new Thread(() -> {
      try {
        byte[] data = http("GET", "/api/jobs/" + jobId + "/midi" + (split ? "?split=1" : ""), null, null);
        Uri u = saveToDownloads(name, data);
        ui.post(() -> {
          toast("Saved to Download/Drum Scribe/" + name);
          if (thenShare && u != null) {
            Intent s = new Intent(Intent.ACTION_SEND);
            s.setType("audio/midi");
            s.putExtra(Intent.EXTRA_STREAM, u);
            s.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(s, "Send the drum MIDI to"));
          }
        });
      } catch (Exception e) { ui.post(() -> toast("Couldn't get the MIDI: " + e.getMessage())); }
    }).start();
  }

  Uri saveToDownloads(String name, byte[] data) throws Exception {
    String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
    if (Build.VERSION.SDK_INT >= 29) {
      ContentValues v = new ContentValues();
      v.put(MediaStore.Downloads.DISPLAY_NAME, safe);
      v.put(MediaStore.Downloads.MIME_TYPE, "audio/midi");
      v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/Drum Scribe");
      Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
      try (OutputStream o = getContentResolver().openOutputStream(u)) { o.write(data); }
      return lastSaved = u;
    }
    File dir = new File(getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "Drum Scribe");
    dir.mkdirs();
    File f = new File(dir, safe);
    try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
    return null;  // Android 6-9: no shareable uri without a FileProvider; the file is in the app's Download folder
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    super.onActivityResult(req, res, data);
    if (req == PICK_AUDIO && res == RESULT_OK && data != null && data.getData() != null) upload(data.getData());
  }

  /** Every hit on one lane per kit piece, brightness = velocity. */
  static class HitsView extends View {
    JSONArray hits;
    List<Integer> notes = new ArrayList<>();
    List<String> names = new ArrayList<>();
    double dur = 1;
    final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

    HitsView(Context c) { super(c); }

    void set(JSONArray h, List<Integer> n, List<String> nm, double d) { hits = h; notes = n; names = nm; dur = Math.max(1, d); invalidate(); }

    @Override protected void onDraw(Canvas c) {
      int w = getWidth(), h = getHeight();
      p.setColor(0xFF0D0F12);
      c.drawRect(0, 0, w, h, p);
      if (hits == null || notes.isEmpty()) return;
      float lane = (float) h / notes.size();
      for (int i = 0; i < notes.size(); i++) {
        p.setColor(i % 2 == 1 ? 0xFF14171C : 0xFF101317);
        c.drawRect(0, i * lane, w, (i + 1) * lane, p);
      }
      float px = Math.max(1f, getResources().getDisplayMetrics().density);
      for (int k = 0; k < hits.length(); k++) {
        JSONArray e = hits.optJSONArray(k);
        int i = notes.indexOf(e.optInt(1));
        if (i < 0) continue;
        float v = e.optInt(2) / 127f;
        p.setColor(Color.HSVToColor(new float[] {30 + i * 23, 0.8f, 0.35f + 0.65f * v}));
        float x = (float) (e.optDouble(0) / dur * w);
        c.drawRect(x, i * lane + lane * 0.15f, x + px, (i + 0.85f) * lane, p);
      }
      p.setColor(DIM);
      p.setTextSize(Math.min(lane * 0.6f, 11 * px));
      for (int i = 0; i < names.size(); i++) c.drawText(names.get(i), 4 * px, i * lane + lane * 0.5f + p.getTextSize() * 0.35f, p);
    }
  }
}
