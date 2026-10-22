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
import android.provider.OpenableColumns;

import com.antonkarpenko.ffmpegkit.FFmpegKit;
import com.antonkarpenko.ffmpegkit.FFmpegKitConfig;
import com.antonkarpenko.ffmpegkit.FFmpegSession;
import com.antonkarpenko.ffmpegkit.FFprobeKit;
import com.antonkarpenko.ffmpegkit.MediaInformation;
import com.antonkarpenko.ffmpegkit.MediaInformationSession;
import com.antonkarpenko.ffmpegkit.ReturnCode;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;

/** Converts the queued files one after another, with a progress notification. */
public class ConvertService extends Service {
  static final String EXTRA_MAX_COMPAT = "max_compat";
  static final String ACTION_CANCEL = "com.thesnakedevice.mkv2mp4.CANCEL";
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

  private static final class Job {
    final Uri uri;
    final boolean maxCompat;
    Job(Uri uri, boolean maxCompat) { this.uri = uri; this.maxCompat = maxCompat; }
  }

  private final LinkedBlockingQueue<Job> queue = new LinkedBlockingQueue<>();
  private final Handler main = new Handler(Looper.getMainLooper());
  private Thread worker;
  private volatile boolean cancelled;
  private PowerManager.WakeLock wakeLock;
  private long lastNotify;

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
      boolean maxCompat = intent.getBooleanExtra(EXTRA_MAX_COMPAT, false);
      for (int i = 0; i < clip.getItemCount(); i++) {
        Uri u = clip.getItemAt(i).getUri();
        if (u != null) queue.add(new Job(u, maxCompat));
      }
      final int added = clip.getItemCount();
      ui(() -> {
        if (!state.running) { state.done = 0; state.total = 0; }
        state.total += added;
        state.running = true;
      });
    }
    if (worker == null) {
      cancelled = false;
      wakeLock = ((PowerManager) getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "mkv2mp4:convert");
      wakeLock.acquire(12 * 60 * 60 * 1000L);
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
    int ok = 0, failed = 0;
    Job job;
    while (!cancelled && (job = queue.poll()) != null) {
      if (convert(job)) ok++; else failed++;
      ui(() -> state.done++);
    }
    final String result = cancelled ? "Cancelled." : ok + " converted" + (failed > 0 ? ", " + failed + " failed" : "") + ".";
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
          .setContentTitle("MKV to MP4: " + result).setContentText("Saved to Movies/MKV to MP4")
          .setContentIntent(openApp()).setAutoCancel(true).build());
      // more files may have been queued while this was finishing
      if (!queue.isEmpty()) onStartCommand(null, 0, 0); else stopSelf();
    });
  }

  private boolean convert(Job job) {
    String name = displayName(job.uri);
    ui(() -> { state.current = name; state.progress = -1; });
    File out = null;
    try {
      MediaInformationSession probe = FFprobeKit.getMediaInformation(FFmpegKitConfig.getSafParameterForRead(this, job.uri));
      MediaInformation info = probe.getMediaInformation();
      if (info == null) throw new IllegalArgumentException("couldn't read the file");
      out = outputFile(name);
      Converter.Plan plan = Converter.plan(info, FFmpegKitConfig.getSafParameterForRead(this, job.uri), out.getAbsolutePath(), job.maxCompat);
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
        out.delete();
        log(name + ": cancelled");
        return false;
      }
      if (!ReturnCode.isSuccess(session.getReturnCode())) {
        out.delete();
        throw new IllegalStateException(lastLines(session.getAllLogsAsString(), 3));
      }
      MediaScannerConnection.scanFile(this, new String[] {out.getAbsolutePath()}, new String[] {"video/mp4"}, null);
      log("✓ " + out.getName() + " — " + plan.summary);
      return true;
    } catch (Exception e) {
      if (out != null) out.delete();
      log("✗ " + name + ": " + e.getMessage());
      return false;
    }
  }

  /** Movies/MKV to MP4/<name>.mp4, numbered if that name is taken. */
  private File outputFile(String inputName) {
    File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES), "MKV to MP4");
    if (!dir.isDirectory() && !dir.mkdirs()) dir = getExternalFilesDir(Environment.DIRECTORY_MOVIES);
    String base = inputName.replaceFirst("\\.[A-Za-z0-9]{2,4}$", "").replaceAll("[\\\\/:*?\"<>|]", "_");
    File f = new File(dir, base + ".mp4");
    for (int i = 1; f.exists(); i++) f = new File(dir, base + " (" + i + ").mp4");
    return f;
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
