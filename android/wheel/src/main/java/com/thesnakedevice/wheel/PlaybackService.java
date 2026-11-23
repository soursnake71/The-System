package com.thesnakedevice.wheel;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the process in the foreground while the decks run, so Android doesn't kill the mix when the app is in the
 * background (screen off, another app on top). It owns nothing: the engine still lives exactly as long as
 * MainActivity, which starts this service once it is up and stops it in onDestroy. The ongoing notification brings the
 * decks back. Without the notification permission (Android 13+, denied) the service still runs; Android only hides
 * its notification.
 */
public final class PlaybackService extends Service {
    static final String CHANNEL = "playback";
    static final int NOTIFICATION_ID = 1;

    static void start(Context c) {
        try {
            c.startForegroundService(new Intent(c, PlaybackService.class));
        } catch (RuntimeException ignored) {
            // not allowed right now (e.g. started from the background on Android 12+): the mix plays on, unprotected
        }
    }

    static void stop(Context c) {
        try { c.stopService(new Intent(c, PlaybackService.class)); } catch (RuntimeException ignored) {}
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        try {
            Notification n = notification();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            else startForeground(NOTIFICATION_ID, n);
        } catch (RuntimeException e) {
            stopSelf();
        }
        return START_NOT_STICKY;   // never restarted on its own: without the activity there is no engine
    }

    @Override public void onTaskRemoved(Intent rootIntent) {
        // swiped away from the recent apps: the user is done; the activity's onDestroy stops the engine
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
        super.onTaskRemoved(rootIntent);
    }

    private Notification notification() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm != null && nm.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, getString(R.string.playback_channel), NotificationManager.IMPORTANCE_LOW);
            ch.setShowBadge(false);
            ch.setSound(null, null);
            nm.createNotificationChannel(ch);
        }
        Intent open = new Intent(this, MainActivity.class)
                .setAction(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(R.drawable.ic_stat_wheel)
                .setContentTitle(getString(R.string.playing_title))
                .setContentText(getString(R.string.playing_text))
                .setContentIntent(pi)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setShowWhen(false)
                .setCategory(Notification.CATEGORY_TRANSPORT)
                .setColor(Pal.DECK[0])
                .setVisibility(Notification.VISIBILITY_PUBLIC);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return b.build();
    }
}
