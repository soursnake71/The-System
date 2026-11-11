package com.thesnakedevice.drumscribe;

import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONObject;

/** Long work that must survive the screen turning off: the on-phone analysis (up to an hour for a long song) and the
 *  model download. A foreground service with a progress notification and a partial wake lock. The activity reads the
 *  static state below (same process) and shows it. */
public class ScribeService extends Service {
  static final String ACTION_ANALYSE = "analyse", ACTION_DOWNLOAD = "download";
  static final String CHANNEL = "work";
  static final int NOTE_ID = 7;

  /** What the service is doing: idle | hashing | decoding | analysing | downloading | done | error | cancelled */
  static volatile String phase = "idle";
  static volatile String message = "", task = "", workDir, title;
  static volatile double fraction;
  static volatile long etaSeconds = -1;
  static volatile int finishedSerial;  // bumps when a task ends, so the activity reacts once
  static final AtomicBoolean cancel = new AtomicBoolean();
  static volatile boolean busy;

  PowerManager.WakeLock wake;
  NotificationManager nm;

  @Override public IBinder onBind(Intent i) { return null; }

  static File songsDir(Context c) { return new File(c.getFilesDir(), "songs"); }

  static void analyse(Context c, Uri uri, String name, double tempo) {
    Intent i = new Intent(c, ScribeService.class).setAction(ACTION_ANALYSE).setData(uri)
        .putExtra("name", name).putExtra("tempo", tempo).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    begin(c, i, "analyse");
  }

  static void downloadModels(Context c) { begin(c, new Intent(c, ScribeService.class).setAction(ACTION_DOWNLOAD), "download"); }

  private static void begin(Context c, Intent i, String what) {
    if (busy) return;
    busy = true;
    task = what;
    cancel.set(false);
    phase = "analyse".equals(what) ? "hashing" : "downloading";
    fraction = 0;
    etaSeconds = -1;
    message = "Starting";
    try {
      c.startForegroundService(i);
    } catch (RuntimeException e) {  // e.g. not allowed from the background
      busy = false;
      phase = "error";
      message = "Can't start: " + e.getMessage();
      finishedSerial++;
    }
  }

  static void requestCancel() {
    cancel.set(true);
    if (Engine.available) Engine.cancel();
  }

  @Override public int onStartCommand(Intent intent, int flags, int startId) {
    nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
    nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Analysis and downloads", NotificationManager.IMPORTANCE_LOW));
    Notification n = note("Drum Scribe", "Starting", 0, true);
    if (Build.VERSION.SDK_INT >= 29)
      startForeground(NOTE_ID, n, Build.VERSION.SDK_INT >= 35 ? ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
          : ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    else startForeground(NOTE_ID, n);
    if (intent == null) { finish("idle", ""); return START_NOT_STICKY; }
    PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
    wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DrumScribe:work");
    wake.acquire(4 * 3600 * 1000L);
    final Intent in = intent;
    new Thread(() -> {
      try {
        if (ACTION_DOWNLOAD.equals(in.getAction())) download();
        else analyse(in.getData(), in.getStringExtra("name"), in.getDoubleExtra("tempo", 0));
      } catch (Throwable t) {
        String m = t.getMessage() == null ? t.toString() : t.getMessage();
        finish(cancel.get() ? "cancelled" : "error", cancel.get() ? "Cancelled" : m);
      }
    }, "drumscribe-work").start();
    return START_NOT_STICKY;
  }

  /** Android 15 limits these services to 6 hours a day: stop cleanly instead of being stopped. */
  @Override public void onTimeout(int startId, int fgsType) { requestCancel(); }

  void download() throws Exception {
    ModelPack.download(this, (done, total, msg) -> {
      fraction = total > 0 ? (double) done / total : 0;
      message = total > 0 ? String.format(Locale.US, "%s: %d of %d MB", msg, done >> 20, total >> 20) : msg;
      update("Downloading the models", message, fraction);
    }, cancel);
    finish("done", "Models installed");
  }

  void analyse(Uri uri, String name, double tempo) throws Exception {
    if (!Engine.available) throw new Exception("This phone can't run the engine (" + Engine.loadError + ").");
    if (!ModelPack.installed(this)) throw new Exception("Download the models first.");
    title = name;
    message = "Reading " + name;
    update("Drum Scribe", message, 0);
    String id = AudioDecoder.hash(this, uri);
    File work = new File(songsDir(this), id);
    work.mkdirs();
    workDir = work.getAbsolutePath();
    File result = new File(work, "result.json");
    if (result.isFile()) {  // analysed before: instant (a new tempo only redoes the grid)
      JSONObject r = new JSONObject(read(result));
      if (Math.abs(r.optDouble("fixed_tempo", 0) - tempo) > 1e-6) Engine.retempo(workDir, tempo);
      finish("done", "From last time");
      return;
    }
    if (cancel.get()) throw new Exception("Cancelled");
    String pcmPath = "";
    AudioDecoder.Pcm pcm = new AudioDecoder.Pcm();
    boolean stems = new File(work, "stems/bass.wav").isFile() && new File(work, "source.json").isFile();
    if (!stems) {
      phase = "decoding";
      File raw = new File(getCacheDir(), "decoded.pcm");
      pcm = AudioDecoder.decode(this, uri, raw, f -> {
        fraction = 0.05 * f;
        message = String.format(Locale.US, "Decoding the audio (%d%%)", (int) (f * 100));
      }, cancel);
      pcmPath = raw.getAbsolutePath();
    }
    phase = "analysing";
    int threads = 1;  // one Demucs worker (~1.2 GB each): fits phones with 4 GB+
    ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
    ((ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(mi);
    if (mi.totalMem > 11L << 30) threads = 2;
    if (!Engine.start(pcmPath, pcm.sampleRate, pcm.channels, pcm.isFloat, name, workDir,
        ModelPack.dir(this).getAbsolutePath(), "fast", tempo, threads))
      throw new Exception("The engine is busy with another song.");
    long t0 = System.currentTimeMillis();
    double p0 = -1;
    while (true) {
      Thread.sleep(1000);
      JSONObject j = new JSONObject(Engine.progress());
      String st = j.optString("state");
      double p = Math.max(0.05, j.optDouble("progress", 0));
      if (p0 < 0 && p > 0.051) { p0 = p; t0 = System.currentTimeMillis(); }
      fraction = p;
      message = j.optString("message");
      if (p0 >= 0 && p - p0 > 0.01) etaSeconds = (long) ((System.currentTimeMillis() - t0) / 1000.0 * (1 - p) / (p - p0));
      update(title, message + (etaSeconds >= 0 ? "  ·  " + eta(etaSeconds) + " left" : ""), p);
      if ("done".equals(st)) { finish("done", message); return; }
      if ("error".equals(st)) throw new Exception(message);
      if ("cancelled".equals(st)) { finish("cancelled", "Cancelled"); return; }
    }
  }

  static String eta(long s) {
    if (s >= 3600) return String.format(Locale.US, "%d h %02d min", s / 3600, (s % 3600) / 60);
    if (s >= 60) return String.format(Locale.US, "%d min", (s + 30) / 60);
    return s + " s";
  }

  static String read(File f) throws Exception {
    return new String(java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
  }

  Notification note(String t, String text, double f, boolean ongoing) {
    Intent open = new Intent(this, ScribeActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
    PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    Notification.Builder b = new Notification.Builder(this, CHANNEL);
    b.setSmallIcon(android.R.drawable.stat_sys_download).setContentTitle(t).setContentText(text).setContentIntent(pi)
        .setOnlyAlertOnce(true).setOngoing(ongoing);
    if (ongoing) b.setProgress(1000, (int) (f * 1000), false);
    return b.build();
  }

  void update(String t, String text, double f) { nm.notify(NOTE_ID, note(t, text, f, true)); }

  void finish(String ph, String msg) {
    phase = ph;
    message = msg;
    if ("done".equals(ph)) fraction = 1;
    etaSeconds = -1;
    busy = false;
    finishedSerial++;
    if (wake != null && wake.isHeld()) wake.release();
    stopForeground(true);
    if (!"idle".equals(ph) && nm != null) {
      String t = "analyse".equals(task) ? (title == null ? "Drum Scribe" : title) : "Drum Scribe models";
      nm.notify(NOTE_ID + 1, note(t, "done".equals(ph) ? "Done: " + msg : msg, 1, false));
    }
    stopSelf();
  }
}
