package com.thesnakedevice.mkv2mp4;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.database.Cursor;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import com.antonkarpenko.ffmpegkit.FFmpegKit;
import com.antonkarpenko.ffmpegkit.FFmpegKitConfig;
import com.antonkarpenko.ffmpegkit.FFmpegSession;
import com.antonkarpenko.ffmpegkit.FFprobeKit;
import com.antonkarpenko.ffmpegkit.MediaInformation;
import com.antonkarpenko.ffmpegkit.MediaInformationSession;
import com.antonkarpenko.ffmpegkit.ReturnCode;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.LinkedBlockingQueue;

/** Converts the queued files one after another, with a progress notification. */
public class ConvertService extends Service {
  static final String EXTRA_MAX_COMPAT = "max_compat";
  /** most video kbit/s to stream, 0 = no limit */
  static final String EXTRA_MAX_KBPS = "max_kbps";
  /** a folder (document tree) to save into; absent = Movies/MKV to MP4 */
  static final String EXTRA_OUT_TREE = "out_tree";
  /** true when the ClipData holds folders (document trees) to search for MKVs, not files */
  static final String EXTRA_FOLDERS = "folders";
  static final String ACTION_CANCEL = "com.thesnakedevice.mkv2mp4.CANCEL";
  static final String DEFAULT_OUT = "Movies/MKV to MP4";
  private static final String CHANNEL = "convert";
  private static final int NOTE_PROGRESS = 1, NOTE_DONE = 2;

  /** What the screen shows. Only touched on the main thread. */
  static final class State {
    boolean running;
    String current = "";
    int done, total;
    /** 0..1, or -1 while the length is unknown */
    float progress = -1;
    final List<String> log = new ArrayList<>();
  }

  interface Listener { void onChanged(); }

  static final State state = new State();
  static Listener listener;

  private static final class Options {
    boolean maxCompat;
    int maxKbps;
    Uri outTree;
  }

  private static final class Job {
    final Uri uri;
    final boolean folder;
    final Options options;
    Job(Uri uri, boolean folder, Options options) { this.uri = uri; this.folder = folder; this.options = options; }
  }

  /** Where one MP4 goes: written straight to {@code file}, or to {@code file} as a temp copy and then into {@code tree}. */
  private static final class Target {
    File file;
    Uri tree;
    String name;
  }

  private final LinkedBlockingQueue<Job> queue = new LinkedBlockingQueue<>();
  private final Handler main = new Handler(Looper.getMainLooper());
  private Thread worker;
  private volatile boolean cancelled;
  private PowerManager.WakeLock wakeLock;
  private long lastNotify;
  private int ok, failed, skipped;

  @Override public IBinder onBind(Intent intent) { return null; }

  @Override public int onStartCommand(Intent intent, int flags, int startId) {
    if (intent != null && ACTION_CANCEL.equals(intent.getAction())) {
      cancelled = true;
      queue.clear();
      FFmpegKit.cancel();
      if (worker == null) stopSelf();
      return START_NOT_STICKY;
    }
    startForeground(progressNote("Starting…", -1));
    ClipData clip = intent == null ? null : intent.getClipData();
    if (clip != null) {
      Options o = new Options();
      o.maxCompat = intent.getBooleanExtra(EXTRA_MAX_COMPAT, false);
      o.maxKbps = intent.getIntExtra(EXTRA_MAX_KBPS, 0);
      String tree = intent.getStringExtra(EXTRA_OUT_TREE);
      o.outTree = tree == null ? null : Uri.parse(tree);
      boolean folders = intent.getBooleanExtra(EXTRA_FOLDERS, false);
      for (int i = 0; i < clip.getItemCount(); i++) {
        Uri u = clip.getItemAt(i).getUri();
        if (u != null) queue.add(new Job(u, folders, o));
      }
      // folders count once their MKVs are found
      final int added = folders ? 0 : clip.getItemCount();
      ui(() -> {
        if (!state.running) { state.done = 0; state.total = 0; }
        state.total += added;
        state.running = true;
      });
    }
    if (worker == null) {
      cancelled = false;
      ok = failed = skipped = 0;
      wakeLock = ((PowerManager) getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "mkv2mp4:convert");
      wakeLock.acquire(24 * 60 * 60 * 1000L);
      worker = new Thread(this::work, "convert");
      worker.start();
    }
    return START_NOT_STICKY;
  }

  private void startForeground(Notification n) {
    if (Build.VERSION.SDK_INT >= 35) startForeground(NOTE_PROGRESS, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING);
    else if (Build.VERSION.SDK_INT >= 29) startForeground(NOTE_PROGRESS, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    else startForeground(NOTE_PROGRESS, n);
  }

  private void work() {
    Job job;
    while (!cancelled && (job = queue.poll()) != null) {
      if (job.folder) {
        expandFolder(job);
        continue;
      }
      convert(job);
      ui(() -> state.done++);
      try {
        // a picked file's access was only kept for the queue; Android caps how many grants an app may hold
        getContentResolver().releasePersistableUriPermission(job.uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
      } catch (RuntimeException ignored) {
        // shared files and folder contents have no grant of their own
      }
    }
    final String result = cancelled ? "Cancelled." : ok + " converted"
        + (skipped > 0 ? ", " + skipped + " already done" : "") + (failed > 0 ? ", " + failed + " failed" : "") + ".";
    final boolean wasCancelled = cancelled;
    ui(() -> {
      state.running = false;
      state.current = "";
      state.progress = -1;
      state.log.add(result);
    });
    main.post(() -> {
      worker = null;
      if (wakeLock.isHeld()) wakeLock.release();
      stopForeground(true);
      if (!wasCancelled) notes().notify(NOTE_DONE, builder().setSmallIcon(android.R.drawable.stat_sys_download_done)
          .setContentTitle("MKV to MP4: " + result).setContentIntent(openApp()).setAutoCancel(true).build());
      // more files may have been queued while this was finishing
      if (!queue.isEmpty()) onStartCommand(null, 0, 0); else stopSelf();
    });
  }

  /** Queues every MKV in a picked folder and its subfolders, in name order. */
  private void expandFolder(Job folder) {
    ui(() -> { state.current = "Looking for MKV files…"; state.progress = -1; });
    List<String[]> found = new ArrayList<>();
    try {
      listMkv(folder.uri, DocumentsContract.getTreeDocumentId(folder.uri), "", found, 0);
    } catch (RuntimeException e) {
      log("✗ couldn't read the folder: " + e.getMessage());
      return;
    }
    found.sort((a, b) -> a[1].compareToIgnoreCase(b[1]));
    // run the folder's files before anything queued after it
    List<Job> rest = new ArrayList<>();
    queue.drainTo(rest);
    for (String[] f : found) queue.add(new Job(DocumentsContract.buildDocumentUriUsingTree(folder.uri, f[0]), false, folder.options));
    queue.addAll(rest);
    final int n = found.size();
    if (n == 0) log("No MKV files in that folder.");
    else log("Found " + n + " MKV file" + (n == 1 ? "" : "s") + " in the folder.");
    ui(() -> state.total += n);
  }

  /** Adds {document id, path for sorting} of each .mkv under a folder. */
  private void listMkv(Uri tree, String docId, String path, List<String[]> out, int depth) {
    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
    String[] cols = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};
    try (Cursor c = getContentResolver().query(children, cols, null, null, null)) {
      while (c != null && c.moveToNext()) {
        String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
        if (name == null) continue;
        if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
          if (depth < 10 && !name.startsWith(".")) listMkv(tree, id, path + name + "/", out, depth + 1);
        } else if (name.toLowerCase().endsWith(".mkv")) {
          out.add(new String[] {id, path + name});
        }
      }
    }
  }

  private void convert(Job job) {
    String name = displayName(job.uri);
    ui(() -> { state.current = "Converting " + name; state.progress = -1; });
    Target t = null;
    try {
      t = target(name, job.options.outTree);
      if (t == null) {
        log("• " + mp4Name(name) + " is already in the output folder, skipped");
        skipped++;
        return;
      }
      MediaInformationSession probe = FFprobeKit.getMediaInformation(FFmpegKitConfig.getSafParameterForRead(this, job.uri));
      MediaInformation info = probe.getMediaInformation();
      if (info == null) throw new IllegalArgumentException("couldn't read the file");
      Converter.Plan plan = Converter.plan(info, FFmpegKitConfig.getSafParameterForRead(this, job.uri), t.file.getAbsolutePath(),
          job.options.maxCompat, job.options.maxKbps);
      final long duration = plan.durationMs;
      FFmpegSession session = FFmpegSession.create(plan.args.toArray(new String[0]), null, null, stats -> {
        if (duration <= 0) return;
        float f = (float) Math.min(1.0, stats.getTime() / duration);
        ui(() -> state.progress = f);
        long now = System.currentTimeMillis();
        if (now - lastNotify > 1000) {
          lastNotify = now;
          notes().notify(NOTE_PROGRESS, progressNote(name, f));
        }
      });
      notes().notify(NOTE_PROGRESS, progressNote(name, -1));
      FFmpegKitConfig.ffmpegExecute(session);
      if (ReturnCode.isCancel(session.getReturnCode()) || cancelled) {
        t.file.delete();
        log(name + ": cancelled");
        return;
      }
      if (!ReturnCode.isSuccess(session.getReturnCode())) throw new IllegalStateException(lastLines(session.getAllLogsAsString(), 3));
      if (t.tree != null) {
        ui(() -> state.progress = -1);
        saveToTree(t);
        if (cancelled) return;
      } else {
        MediaScannerConnection.scanFile(this, new String[] {t.file.getAbsolutePath()}, new String[] {"video/mp4"}, null);
      }
      log("✓ " + t.name + " — " + plan.summary);
      ok++;
    } catch (Exception e) {
      if (t != null) t.file.delete();
      log("✗ " + name + ": " + e.getMessage());
      failed++;
    }
  }

  private static String mp4Name(String inputName) {
    return inputName.replaceFirst("\\.[A-Za-z0-9]{2,4}$", "").replaceAll("[\\\\/:*?\"<>|]", "_") + ".mp4";
  }

  /** Where this file's MP4 goes, or null if the output folder already has it (so re-running a folder only does new files). */
  private Target target(String inputName, Uri outTree) throws IOException {
    Target t = new Target();
    t.name = mp4Name(inputName);
    File dir;
    if (outTree == null) {
      dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES), "MKV to MP4");
      if (!dir.isDirectory() && !dir.mkdirs()) dir = getExternalFilesDir(Environment.DIRECTORY_MOVIES);
    } else {
      dir = writableDir(outTree);
      if (dir == null) {
        // not a plain folder this app may write to, so convert to a temp file and copy it in afterwards
        if (childNames(outTree).contains(t.name)) return null;
        File cache = getExternalCacheDir() != null ? getExternalCacheDir() : getCacheDir();
        t.file = new File(cache, "converting.mp4");
        t.tree = outTree;
        return t;
      }
    }
    t.file = new File(dir, t.name);
    return t.file.exists() ? null : t;
  }

  /**
   * The picked folder as a plain path, if FFmpeg can write there directly (internal storage on most phones).
   * Writing in place avoids needing room for a temp copy of a whole movie.
   */
  private File writableDir(Uri tree) {
    if (!"com.android.externalstorage.documents".equals(tree.getAuthority())) return null;
    String id = DocumentsContract.getTreeDocumentId(tree);
    int colon = id.indexOf(':');
    if (colon < 0) return null;
    String volume = id.substring(0, colon), rel = id.substring(colon + 1);
    File root = "primary".equalsIgnoreCase(volume) ? Environment.getExternalStorageDirectory() : new File("/storage/" + volume);
    File dir = rel.isEmpty() ? root : new File(root, rel);
    File probe = new File(dir, "mkv2mp4-write-test-" + System.nanoTime() + ".mp4");
    try {
      if (probe.createNewFile()) {
        probe.delete();
        return dir;
      }
    } catch (IOException | SecurityException ignored) {
      // fall back to copying through the document provider
    }
    return null;
  }

  private Set<String> childNames(Uri tree) {
    Set<String> names = new HashSet<>();
    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
    try (Cursor c = getContentResolver().query(children, new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
      while (c != null && c.moveToNext()) names.add(c.getString(0));
    }
    return names;
  }

  private void saveToTree(Target t) throws IOException {
    ui(() -> state.current = "Saving " + t.name + " to the output folder…");
    Uri parent = DocumentsContract.buildDocumentUriUsingTree(t.tree, DocumentsContract.getTreeDocumentId(t.tree));
    Uri doc = DocumentsContract.createDocument(getContentResolver(), parent, "video/mp4", t.name);
    if (doc == null) throw new IOException("couldn't create " + t.name + " in the output folder");
    try (InputStream in = new FileInputStream(t.file); OutputStream out = getContentResolver().openOutputStream(doc)) {
      if (out == null) throw new IOException("couldn't write to the output folder");
      byte[] buf = new byte[1 << 20];
      long total = t.file.length(), copied = 0;
      for (int n; (n = in.read(buf)) > 0 && !cancelled; ) {
        out.write(buf, 0, n);
        copied += n;
        final float f = total > 0 ? (float) copied / total : -1;
        ui(() -> state.progress = f);
      }
    } catch (IOException e) {
      DocumentsContract.deleteDocument(getContentResolver(), doc);
      throw e;
    } finally {
      t.file.delete();
    }
    if (cancelled) {
      DocumentsContract.deleteDocument(getContentResolver(), doc);
      log(t.name + ": cancelled");
    }
  }

  private String displayName(Uri uri) {
    try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
      if (c != null && c.moveToFirst() && c.getString(0) != null) return c.getString(0);
    } catch (RuntimeException ignored) {
      // fall through to the last path segment
    }
    String last = uri.getLastPathSegment();
    return last == null ? "video" : last.substring(last.lastIndexOf('/') + 1);
  }

  private static String lastLines(String text, int n) {
    if (text == null) return "conversion failed";
    String[] lines = text.trim().split("\n");
    StringBuilder b = new StringBuilder();
    for (int i = Math.max(0, lines.length - n); i < lines.length; i++) b.append(lines[i].trim()).append(' ');
    return b.toString().trim();
  }

  private void log(String line) { ui(() -> state.log.add(line)); }

  /** Runs a change to the shared state on the main thread and tells the screen. */
  private void ui(Runnable r) {
    main.post(() -> {
      r.run();
      if (listener != null) listener.onChanged();
    });
  }

  private Notification progressNote(String name, float progress) {
    Notification.Builder b = builder().setSmallIcon(android.R.drawable.stat_sys_download)
        .setContentTitle("Converting " + name).setOngoing(true).setOnlyAlertOnce(true).setContentIntent(openApp())
        .addAction(new Notification.Action.Builder(null, "Cancel",
            PendingIntent.getService(this, 1, new Intent(this, ConvertService.class).setAction(ACTION_CANCEL), PendingIntent.FLAG_IMMUTABLE)).build());
    if (progress < 0) b.setProgress(0, 0, true);
    else b.setProgress(1000, (int) (progress * 1000), false).setContentText(Math.round(progress * 100) + "%");
    return b.build();
  }

  private Notification.Builder builder() {
    if (Build.VERSION.SDK_INT < 26) return new Notification.Builder(this);
    notes().createNotificationChannel(new NotificationChannel(CHANNEL, "Conversions", NotificationManager.IMPORTANCE_LOW));
    return new Notification.Builder(this, CHANNEL);
  }

  private PendingIntent openApp() {
    return PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE);
  }

  private NotificationManager notes() { return (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE); }
}
