package com.thesnakedevice.reset;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class ReminderReceiver extends BroadcastReceiver {
  @Override public void onReceive(Context c, Intent in) {
    int id = in.getIntExtra("id", 0);
    String[] r = Reminders.get(c, id);
    if (r == null) return;
    Reminders.channel(c);
    PendingIntent open = PendingIntent.getActivity(c, 0, new Intent(c, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE);
    Notification n = new Notification.Builder(c, Reminders.CHANNEL)
        .setSmallIcon(R.drawable.ic_fg)
        .setContentTitle(r[2])
        .setContentText(r[3])
        .setStyle(new Notification.BigTextStyle().bigText(r[3]))
        .setContentIntent(open)
        .setAutoCancel(true)
        .build();
    try { c.getSystemService(NotificationManager.class).notify(id, n); } catch (SecurityException ignored) {}
    Reminders.arm(c, id);   // same time tomorrow
  }
}
