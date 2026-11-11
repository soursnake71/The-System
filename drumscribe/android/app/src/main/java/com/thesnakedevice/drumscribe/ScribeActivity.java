package com.thesnakedevice.drumscribe;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.ConnectivityManager;
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
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.json.JSONArray;
import org.json.JSONObject;

/** Drum Scribe on Android, two ways:
 *  - "Analyse on this phone": the whole engine (Demucs + DrumSep separation, every hit and bass note, the groove maths,
 *    MIDI) runs in the app (libdrumscribe.so), on an audio or video file picked, shared or opened with the app. The
 *    models are downloaded once. Links can't be fetched on the phone (no yt-dlp).
 *  - "Use my computer": the app is a remote for drumscribe-server on a computer on the same network (links too).
 *  MIDI is saved to Downloads/Drum Scribe and can be shared to a DAW app. */
public class ScribeActivity extends Activity {
  static final int PICK_AUDIO = 11;
  static final int BG = 0xFF111317, PANEL = 0xFF1B1E24, LINE = 0xFF2C313A, TEXT = 0xFFE8EAEE, DIM = 0xFF9AA1AD,
      ACCENT = 0xFFF2A33A, BAD = 0xFFE5615B, GOOD = 0xFF7BC67E;

  final Handler ui = new Handler(Looper.getMainLooper());
  SharedPreferences prefs;
  boolean phone;  // the mode: analyse on this phone (true) or use my computer
  EditText server, link, tempo, token;
  Spinner quality;
  Button go, pick, cancel, modePhone, modeComputer, save, saveSplit, share, saveConst, saveDrums, saveBass, modelGet, modelDel;
  ProgressBar bar, modelBar;
  TextView status, title, info, grooveView, inputHint, modelInfo;
  LinearLayout results, pieceList, computerCard, modelCard, recentCard, recentList, qualityBox;
  HitsView hitsView;
  String jobId;        // computer mode: the engine's job
  String localWork;    // phone mode: the shown song's folder
  JSONObject result;
  boolean polling;
  int seenSerial;
  long modelBytes = -1;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().setStatusBarColor(BG);
    getWindow().setNavigationBarColor(BG);
    prefs = getSharedPreferences("scribe", MODE_PRIVATE);
    // default: on this phone once the models are there (or when the engine can't run here, the computer)
    phone = prefs.contains("mode") ? "phone".equals(prefs.getString("mode", "")) : (Engine.available && ModelPack.installed(this));
    if (!Engine.available) phone = false;
    seenSerial = ScribeService.finishedSerial;
    buildUi();
    setMode(phone);
    if (b != null && b.getString("job") != null) { jobId = b.getString("job"); poll(); }
    if (b != null && b.getString("work") != null) showLocal(b.getString("work"));
    else if (ScribeService.busy || ("done".equals(ScribeService.phase) && "analyse".equals(ScribeService.task) && ScribeService.workDir != null))
      showLocal(ScribeService.workDir);
    handleIntent(getIntent());
    ui.post(watch);
  }

  @Override protected void onDestroy() { ui.removeCallbacks(watch); super.onDestroy(); }

  @Override protected void onSaveInstanceState(Bundle out) {
    super.onSaveInstanceState(out);
    if (jobId != null) out.putString("job", jobId);
    if (localWork != null) out.putString("work", localWork);
  }

  @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); handleIntent(i); }

  void handleIntent(Intent i) {
    if (i == null) return;
    Uri stream = null;
    String text = null;
    if (Intent.ACTION_SEND.equals(i.getAction())) {
      text = i.getStringExtra(Intent.EXTRA_TEXT);
      stream = i.getParcelableExtra(Intent.EXTRA_STREAM);
    } else if (Intent.ACTION_VIEW.equals(i.getAction())) {
      stream = i.getData();
    }
    setIntent(new Intent(this, ScribeActivity.class));  // handled: not again on a configuration change
    if (stream != null) {
      if (phone) analyseHere(stream); else upload(stream);
    } else if (text != null) {
      String url = firstUrl(text);
      if (url != null) { link.setText(url); start(); }
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
    style(bt, primary);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(46), 1);
    lp.setMargins(0, dp(10), dp(8), 0);
    bt.setLayoutParams(lp);
    return bt;
  }

  void style(Button bt, boolean primary) {
    bt.setTextColor(primary ? 0xFF1A1205 : TEXT);
    bt.setBackground(box(primary ? ACCENT : 0x00000000, primary ? 0 : LINE));
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

  TextView text(int color, float size) {
    TextView t = new TextView(this);
    t.setTextColor(color);
    t.setTextSize(size);
    return t;
  }

  ProgressBar progressBar() {
    ProgressBar p = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    p.setMax(1000);
    p.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
    LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(14));
    bl.setMargins(0, dp(12), 0, dp(4));
    p.setLayoutParams(bl);
    return p;
  }

  void buildUi() {
    ScrollView scroll = new ScrollView(this);
    scroll.setBackgroundColor(BG);
    scroll.setFitsSystemWindows(true);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setPadding(dp(16), dp(16), dp(16), dp(32));
    scroll.addView(root);

    TextView h = text(TEXT, 22);
    h.setText("DRUM SCRIBE");
    h.setTypeface(Typeface.DEFAULT_BOLD);
    root.addView(h);
    TextView sub = text(DIM, 14);
    sub.setText("A song in, every drum hit and bass note out as MIDI: unquantized, with velocities, every piece of the kit.");
    sub.setPadding(0, dp(2), 0, dp(10));
    root.addView(sub);

    modePhone = button("Analyse on this phone", true);
    modeComputer = button("Use my computer", false);
    root.addView(row(modePhone, modeComputer));
    View gap = new View(this);
    gap.setLayoutParams(new LinearLayout.LayoutParams(1, dp(14)));
    root.addView(gap);

    // the models (phone mode)
    modelCard = card();
    modelCard.addView(label("Separation models (on this phone)"));
    modelInfo = text(TEXT, 14);
    modelCard.addView(modelInfo);
    modelBar = progressBar();
    modelBar.setVisibility(View.GONE);
    modelCard.addView(modelBar);
    modelGet = button("Download the models", true);
    modelDel = button("Delete them", false);
    modelCard.addView(row(modelGet, modelDel));
    root.addView(modelCard);

    LinearLayout c1 = card();
    inputHint = label("");
    c1.addView(inputHint);
    link = field("https://www.youtube.com/watch?v=...", "", InputType.TYPE_TEXT_VARIATION_URI);
    c1.addView(link);
    qualityBox = new LinearLayout(this);
    qualityBox.setOrientation(LinearLayout.VERTICAL);
    quality = new Spinner(this);
    ArrayAdapter<String> qa = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, new String[] {"Best separation (slower)", "Fast separation"}) {
      @Override public View getView(int p, View v, ViewGroup g) { TextView t = (TextView) super.getView(p, v, g); t.setTextColor(TEXT); return t; }
    };
    qa.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
    quality.setAdapter(qa);
    quality.setBackground(box(0xFF0D0F12, LINE));
    quality.setSelection(prefs.getInt("quality", 0));
    qualityBox.addView(label("Separation"));
    qualityBox.addView(quality);
    c1.addView(qualityBox);
    c1.addView(label("Your project tempo (optional, else detected)"));
    tempo = field("detect", prefs.getString("tempo", ""), InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
    c1.addView(tempo);
    go = button("Make drum MIDI", true);
    pick = button("Pick an audio file", false);
    c1.addView(row(go, pick));
    bar = progressBar();
    c1.addView(bar);
    status = text(DIM, 14);
    c1.addView(status);
    cancel = button("Cancel", false);
    cancel.setVisibility(View.GONE);
    c1.addView(row(cancel));
    root.addView(c1);

    results = card();
    results.setVisibility(View.GONE);
    title = text(TEXT, 18);
    title.setTypeface(Typeface.DEFAULT_BOLD);
    results.addView(title);
    info = text(DIM, 14);
    results.addView(info);
    save = button("Save MIDI", true);
    saveSplit = button("Track per piece", false);
    results.addView(row(save, saveSplit));
    saveConst = button("Constant tempo", false);
    saveDrums = button("Drums only", false);
    saveBass = button("Bass only", false);
    results.addView(row(saveConst, saveDrums, saveBass));
    share = button("Send MIDI to an app (DAW, Drive...)", false);
    results.addView(row(share));
    TextView hint = text(DIM, 13);
    hint.setText("Notes sit exactly where they were played. The main file has a tempo map (the grid follows the band); for DAWs that ignore file tempos use Constant tempo and set the project to the tempo above. Line the MIDI up with the start of the song.");
    hint.setPadding(0, dp(10), 0, dp(10));
    results.addView(hint);
    hitsView = new HitsView(this);
    hitsView.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(220)));
    results.addView(hitsView);
    pieceList = new LinearLayout(this);
    pieceList.setOrientation(LinearLayout.VERTICAL);
    results.addView(pieceList);
    results.addView(label("The groove, in numbers"));
    grooveView = text(TEXT, 11);
    grooveView.setTypeface(Typeface.MONOSPACE);
    grooveView.setTextIsSelectable(true);
    results.addView(grooveView);
    root.addView(results);

    // songs analysed on this phone (results are kept: opening one is instant)
    recentCard = card();
    recentCard.addView(label("Songs analysed on this phone"));
    recentList = new LinearLayout(this);
    recentList.setOrientation(LinearLayout.VERTICAL);
    recentCard.addView(recentList);
    root.addView(recentCard);

    computerCard = card();
    computerCard.addView(label("Engine address (the computer running drumscribe-server)"));
    server = field("http://192.168.1.20:8765", prefs.getString("server", ""), InputType.TYPE_TEXT_VARIATION_URI);
    computerCard.addView(server);
    computerCard.addView(label("Token (only if the engine has DRUMSCRIBE_TOKEN set)"));
    token = field("", prefs.getString("token", ""), InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
    computerCard.addView(token);
    Button test = button("Test connection", false);
    computerCard.addView(row(test));
    root.addView(computerCard);

    setContentView(scroll);

    modePhone.setOnClickListener(v -> {
      if (!Engine.available) { toast("This phone can't run the engine (it needs a 64-bit ARM phone): use your computer."); return; }
      setMode(true);
    });
    modeComputer.setOnClickListener(v -> setMode(false));
    go.setOnClickListener(v -> start());
    pick.setOnClickListener(v -> {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
      i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("*/*");
      i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"audio/*", "video/*"});
      startActivityForResult(i, PICK_AUDIO);
    });
    cancel.setOnClickListener(v -> { ScribeService.requestCancel(); setStatus("Cancelling...", false); });
    modelGet.setOnClickListener(v -> downloadModels());
    modelDel.setOnClickListener(v -> new AlertDialog.Builder(this).setMessage("Delete the models? On-phone analysis needs them; songs already analysed stay.")
        .setPositiveButton("Delete", (d, w) -> { ModelPack.delete(this); refreshModels(); }).setNegativeButton("Keep", null).show());
    save.setOnClickListener(v -> download("all", false, true, false));
    saveSplit.setOnClickListener(v -> download("all", true, true, false));
    saveConst.setOnClickListener(v -> download("all", false, false, false));
    saveDrums.setOnClickListener(v -> download("drums", false, true, false));
    saveBass.setOnClickListener(v -> download("bass", false, true, false));
    share.setOnClickListener(v -> download("all", false, true, true));
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

  void setMode(boolean onPhone) {
    phone = onPhone;
    prefs.edit().putString("mode", onPhone ? "phone" : "computer").apply();
    style(modePhone, onPhone);
    style(modeComputer, !onPhone);
    modelCard.setVisibility(onPhone ? View.VISIBLE : View.GONE);
    computerCard.setVisibility(onPhone ? View.GONE : View.VISIBLE);
    qualityBox.setVisibility(onPhone ? View.GONE : View.VISIBLE);
    inputHint.setText(onPhone
        ? "Pick, share or open an audio or video file with Drum Scribe. (YouTube / Spotify links need your computer: the phone can't download them.)"
        : "YouTube, Spotify or SoundCloud link (or Share a song to Drum Scribe), or an audio file");
    go.setText(onPhone ? "Analyse a link" : "Make drum MIDI");
    if (onPhone) { refreshModels(); refreshRecent(); } else recentCard.setVisibility(View.GONE);
  }

  void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

  void setStatus(String s, boolean bad) { status.setText(s); status.setTextColor(bad ? BAD : DIM); }

  void remember() {
    prefs.edit().putString("server", server.getText().toString().trim()).putString("token", token.getText().toString().trim())
        .putString("tempo", tempo.getText().toString().trim()).putInt("quality", quality.getSelectedItemPosition()).apply();
  }

  String tempoValue() {
    String t = tempo.getText().toString().trim();
    try { return t.isEmpty() ? null : String.valueOf(Double.parseDouble(t)); } catch (NumberFormatException e) { return null; }
  }

  /* ---------------- on this phone ---------------- */
  void refreshModels() {
    boolean have = ModelPack.installed(this);
    boolean dl = ScribeService.busy && "download".equals(ScribeService.task);
    modelDel.setVisibility(have && !dl ? View.VISIBLE : View.GONE);
    modelGet.setVisibility(have ? View.GONE : View.VISIBLE);
    modelGet.setText(dl ? "Pause the download" : "Download the models");
    modelBar.setVisibility(dl ? View.VISIBLE : View.GONE);
    if (have) {
      modelInfo.setText(String.format(Locale.US, "Installed (%d MB). Analysis on the phone uses the fast separation (htdemucs + DrumSep) and takes a while (usually longer than the song itself); it keeps going with the screen off.", ModelPack.installedBytes(this) >> 20));
      modelInfo.setTextColor(GOOD);
      return;
    }
    modelInfo.setTextColor(TEXT);
    if (dl) { modelInfo.setText(ScribeService.message); return; }
    modelInfo.setText("Needed once to analyse on this phone" + (modelBytes > 0 ? String.format(Locale.US, ": %d MB", modelBytes >> 20) : "")
        + ". Wi-Fi recommended; an interrupted download continues where it stopped.");
    if (modelBytes < 0) {
      modelBytes = 0;
      new Thread(() -> {
        try {
          long s = ModelPack.total(ModelPack.manifest());
          ui.post(() -> { modelBytes = s; refreshModels(); });
        } catch (Exception e) { ui.post(() -> modelInfo.setText("Needed once to analyse on this phone (the model list can't be read right now: " + e.getMessage() + "). Wi-Fi recommended.")); }
      }).start();
    }
  }

  void downloadModels() {
    if (ScribeService.busy) {
      if ("download".equals(ScribeService.task)) ScribeService.requestCancel();
      else toast("Wait for the analysis to finish.");
      return;
    }
    ConnectivityManager cm = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
    if (cm != null && cm.isActiveNetworkMetered()) {
      new AlertDialog.Builder(this).setMessage(String.format(Locale.US, "You're on mobile data. Download the models (%s) anyway?",
          modelBytes > 0 ? (modelBytes >> 20) + " MB" : "a few hundred MB"))
          .setPositiveButton("Download", (d, w) -> { askNotify(); ScribeService.downloadModels(this); refreshModels(); })
          .setNegativeButton("Wait for Wi-Fi", null).show();
      return;
    }
    askNotify();
    ScribeService.downloadModels(this);
    refreshModels();
  }

  void askNotify() {
    if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
      requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
  }

  void analyseHere(Uri uri) {
    remember();
    if (!ModelPack.installed(this)) {
      setStatus("Download the models first (above): they're needed to analyse on this phone.", true);
      return;
    }
    if (ScribeService.busy) { setStatus("Wait for the current " + ("download".equals(ScribeService.task) ? "download" : "song") + " to finish.", true); return; }
    String name = displayName(uri).replaceAll("\\.[A-Za-z0-9]{1,5}$", "");
    String t = tempoValue();
    results.setVisibility(View.GONE);
    localWork = null;
    jobId = null;
    askNotify();
    ScribeService.analyse(this, uri, name, t == null ? 0 : Double.parseDouble(t));
    setStatus("Starting: " + name, false);
    bar.setProgress(0);
  }

  /** Follows the service (analysis or download) while the app is open. */
  final Runnable watch = new Runnable() {
    @Override public void run() {
      boolean running = ScribeService.busy;
      boolean analysing = running && "analyse".equals(ScribeService.task);
      cancel.setVisibility(analysing ? View.VISIBLE : View.GONE);
      if (analysing) {
        bar.setProgress((int) (ScribeService.fraction * 1000));
        long eta = ScribeService.etaSeconds;
        setStatus(ScribeService.message + String.format(Locale.US, " (%d%%)", (int) (ScribeService.fraction * 100))
            + (eta >= 0 ? "\nAbout " + ScribeService.eta(eta) + " left. You can leave the app: it carries on." : ""), false);
      }
      if (running && "download".equals(ScribeService.task) && phone) {
        modelBar.setProgress((int) (ScribeService.fraction * 1000));
        modelInfo.setText(ScribeService.message);
        modelGet.setText("Pause the download");
      }
      if (ScribeService.finishedSerial != seenSerial) {
        seenSerial = ScribeService.finishedSerial;
        String ph = ScribeService.phase;
        if ("download".equals(ScribeService.task)) {
          modelBar.setVisibility(View.GONE);
          if (!"done".equals(ph)) toast(ScribeService.message);
          refreshModels();
        } else if ("done".equals(ph)) {
          bar.setProgress(1000);
          setStatus("Done: " + ScribeService.message, false);
          showLocal(ScribeService.workDir);
          refreshRecent();
        } else {
          setStatus(ScribeService.message, !"cancelled".equals(ph));
        }
      }
      ui.postDelayed(this, 500);
    }
  };

  void showLocal(String work) {
    if (work == null) return;
    File f = new File(work, "result.json");
    if (!f.isFile()) return;
    try {
      JSONObject r = new JSONObject(ScribeService.read(f));
      localWork = work;
      jobId = null;
      show(r);
    } catch (Exception e) { toast("Can't read that result: " + e.getMessage()); }
  }

  void refreshRecent() {
    recentList.removeAllViews();
    File[] songs = ScribeService.songsDir(this).listFiles();
    List<File> done = new ArrayList<>();
    if (songs != null) for (File s : songs) if (new File(s, "result.json").isFile()) done.add(s);
    done.sort((a, b) -> Long.compare(new File(b, "result.json").lastModified(), new File(a, "result.json").lastModified()));
    recentCard.setVisibility(phone && !done.isEmpty() ? View.VISIBLE : View.GONE);
    for (File s : done.subList(0, Math.min(10, done.size()))) {
      String name = s.getName();
      double dur = 0;
      try {
        JSONObject m = new JSONObject(ScribeService.read(new File(s, "source.json")));
        name = m.optString("title", name);
        dur = m.optDouble("duration", 0);
      } catch (Exception ignored) { }
      TextView t = text(TEXT, 15);
      t.setText(String.format(Locale.US, "%s   %d:%02d", name, (int) dur / 60, (int) dur % 60));
      t.setPadding(0, dp(8), 0, dp(8));
      final String w = s.getAbsolutePath();
      t.setOnClickListener(v -> { showLocal(w); setStatus("", false); });
      t.setOnLongClickListener(v -> {
        new AlertDialog.Builder(this).setMessage("Forget this song (its result and stems)?").setPositiveButton("Forget", (d, x) -> {
          deleteTree(new File(w));
          if (w.equals(localWork)) { results.setVisibility(View.GONE); localWork = null; }
          refreshRecent();
        }).setNegativeButton("Keep", null).show();
        return true;
      });
      recentList.addView(t);
    }
  }

  static void deleteTree(File f) {
    File[] k = f.listFiles();
    if (k != null) for (File c : k) deleteTree(c);
    f.delete();
  }

  String displayName(Uri uri) {
    String name = "song";
    try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
      if (c != null && c.moveToFirst()) {
        int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
        if (i >= 0 && c.getString(i) != null) name = c.getString(i);
      }
    } catch (Exception ignored) {
      String p = uri.getLastPathSegment();
      if (p != null) name = p;
    }
    return name;
  }

  /* ---------------- on my computer ---------------- */
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

  void start() {
    remember();
    String l = link.getText().toString().trim();
    String u = firstUrl(l);
    if (phone) {
      setStatus(u == null ? "On this phone: pick, share or open an audio or video file (button on the right)."
          : "The phone can't download songs from links (YouTube, Spotify...). Switch to \"Use my computer\" for links, or share / pick the audio file itself.", true);
      return;
    }
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
        localWork = null;
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
    final String fname = displayName(uri);
    setStatus("Sending " + fname + "...", false);
    new Thread(() -> {
      try {
        byte[] data = readAll(getContentResolver().openInputStream(uri));
        String q = "/api/upload?name=" + URLEncoder.encode(fname, "UTF-8") + "&quality=" + (quality.getSelectedItemPosition() == 1 ? "fast" : "best");
        String t = tempoValue();
        if (t != null) q += "&tempo=" + t;
        JSONObject j = new JSONObject(new String(http("POST", q, data, "application/octet-stream"), StandardCharsets.UTF_8));
        jobId = j.getString("id");
        localWork = null;
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

  /* ---------------- results (both modes) ---------------- */
  void show(JSONObject r) {
    result = r;
    results.setVisibility(View.VISIBLE);
    title.setText(r.optString("title"));
    JSONArray hits = r.optJSONArray("hits");
    JSONArray bass = r.optJSONArray("bass");
    info.setText(String.format(Locale.US, "%.2f BPM  ·  %d drum hits  ·  %d bass notes  ·  %d s", r.optDouble("tempo"),
        hits == null ? 0 : hits.length(), bass == null ? 0 : bass.length(), (int) r.optDouble("duration")));
    StringBuilder g = new StringBuilder();
    JSONObject groove = r.optJSONObject("groove");
    JSONArray findings = groove == null ? null : groove.optJSONArray("findings");
    for (int i = 0; findings != null && i < findings.length(); i++) g.append("• ").append(findings.optString(i)).append('\n');
    if (g.length() > 0) g.append('\n');
    g.append(r.optString("groove_text"));
    grooveView.setText(g.toString());
    pieceList.removeAllViews();
    JSONArray ps = r.optJSONArray("pieces");
    List<Integer> notes = new ArrayList<>();
    List<String> names = new ArrayList<>();
    for (int i = 0; ps != null && i < ps.length(); i++) {
      JSONObject p = ps.optJSONObject(i);
      notes.add(p.optInt("note"));
      names.add(p.optString("label"));
      TextView t = text(TEXT, 13);
      t.setText(String.format(Locale.US, "%-18s note %3d   %5d hits", p.optString("label"), p.optInt("note"), p.optInt("count")));
      t.setTypeface(Typeface.MONOSPACE);
      t.setPadding(0, dp(4), 0, dp(4));
      pieceList.addView(t);
    }
    hitsView.set(hits, notes, names, r.optDouble("duration", 1));
  }

  /** part all|drums|bass; split: a track per kit piece; tempoMap false: constant tempo. */
  void download(String part, boolean split, boolean tempoMap, boolean thenShare) {
    if (result == null || (jobId == null && localWork == null)) return;
    String title = result.optString("title", "drums").replaceAll("[\\\\/:*?\"<>|]", "_");
    String name = title + ("drums".equals(part) ? " - drums" : "bass".equals(part) ? " - bass" : " - drums + bass")
        + (split ? " (track per piece)" : "") + (tempoMap ? "" : " (constant tempo)") + ".mid";
    final String work = localWork, job = jobId;
    new Thread(() -> {
      try {
        byte[] data;
        if (work != null) {
          File f = new File(Engine.makeMidi(work, part, split, tempoMap));
          data = Files.readAllBytes(f.toPath());
          f.delete();
        } else {
          data = http("GET", "/api/jobs/" + job + "/midi?part=" + part + (split ? "&split=1" : "") + (tempoMap ? "" : "&tempo=constant"), null, null);
        }
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
      } catch (Throwable e) { ui.post(() -> toast("Couldn't make the MIDI: " + e.getMessage())); }
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
      return u;
    }
    File dir = new File(getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "Drum Scribe");
    dir.mkdirs();
    File f = new File(dir, safe);
    try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
    return null;  // Android 8-9: no shareable uri without a FileProvider; the file is in the app's Download folder
  }

  @Override protected void onActivityResult(int req, int res, Intent data) {
    super.onActivityResult(req, res, data);
    if (req == PICK_AUDIO && res == RESULT_OK && data != null && data.getData() != null) {
      if (phone) analyseHere(data.getData()); else upload(data.getData());
    }
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
        float len = (float) (e.optDouble(3, 0) / dur * w);
        c.drawRect(x, i * lane + lane * 0.15f, x + Math.max(px, len), (i + 0.85f) * lane, p);
      }
      p.setColor(DIM);
      p.setTextSize(Math.min(lane * 0.6f, 11 * px));
      for (int i = 0; i < names.size(); i++) c.drawText(names.get(i), 4 * px, i * lane + lane * 0.5f + p.getTextSize() * 0.35f, p);
    }
  }
}
