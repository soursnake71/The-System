package com.thesnakedevice.reset;

import android.app.AlarmManager;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import java.util.Calendar;

/** Daily reminders, kept in SharedPreferences so they survive reboots. Each fires once a day and re-arms itself. */
final class Reminders {
  static final String CHANNEL = "reset";
  static SharedPreferences prefs(Context c) { return c.getSharedPreferences("reminders", Context.MODE_PRIVATE); }

  static void save(Context c, int id, int h, int m, String title, String text) {
    prefs(c).edit().putString("r" + id, h + "|" + m + "|" + title.replace("|", "/") + "|" + text.replace("|", "/")).apply();
  }

  static void remove(Context c, int id) {
    prefs(c).edit().remove("r" + id).apply();
    AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
    am.cancel(pending(c, id));
  }

  static String[] get(Context c, int id) {
    String v = prefs(c).getString("r" + id, null);
    return v == null ? null : v.split("\\|", 4);
  }

  static PendingIntent pending(Context c, int id) {
    Intent i = new Intent(c, ReminderReceiver.class).putExtra("id", id);
    return PendingIntent.getBroadcast(c, id, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
  }

  static void arm(Context c, int id) {
    String[] r = get(c, id);
    if (r == null) return;
    Calendar t = Calendar.getInstance();
    t.set(Calendar.HOUR_OF_DAY, Integer.parseInt(r[0]));
    t.set(Calendar.MINUTE, Integer.parseInt(r[1]));
    t.set(Calendar.SECOND, 0);
    t.set(Calendar.MILLISECOND, 0);
    if (t.getTimeInMillis() <= System.currentTimeMillis() + 1000) t.add(Calendar.DAY_OF_YEAR, 1);
    AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, t.getTimeInMillis(), pending(c, id));
  }

  static void armAll(Context c) {
    for (String k : prefs(c).getAll().keySet()) if (k.startsWith("r")) arm(c, Integer.parseInt(k.substring(1)));
  }

  static void channel(Context c) {
    NotificationManager nm = c.getSystemService(NotificationManager.class);
    nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Habit reminders", NotificationManager.IMPORTANCE_DEFAULT));
  }
}
