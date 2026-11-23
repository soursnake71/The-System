package com.thesnakedevice.wheel;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.app.Notification;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.time.Duration;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.android.controller.ServiceController;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowChoreographer;
import org.junit.Before;

/**
 * Opens the app like a phone would (create, start, resume, a few frames) and saves what the screen shows, then the
 * SETUP sheet; checks the playback service is started and puts up its notification.
 */
@RunWith(RobolectricTestRunner.class)
public class LaunchTest {
    // a 60 Hz display: the screen asks for every frame, so frames must come with the clock, not back to back
    @Before public void display() { ShadowChoreographer.setPaused(true); ShadowChoreographer.setFrameDelay(Duration.ofMillis(16)); }

    static void shot(View v, String name) throws Exception {
        File dir = new File(System.getProperty("wheel.shots", "build/shots"));
        dir.mkdirs();
        Bitmap b = Bitmap.createBitmap(v.getWidth(), v.getHeight(), Bitmap.Config.ARGB_8888);
        v.draw(new Canvas(b));
        try (FileOutputStream o = new FileOutputStream(new File(dir, name))) { b.compress(Bitmap.CompressFormat.PNG, 100, o); }
    }
    static TextView findText(View v) {
        if (v instanceof TextView) return (TextView) v;
        if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) { TextView t = findText(((ViewGroup) v).getChildAt(i)); if (t != null) return t; }
        return null;
    }
    static <T> T find(View v, Class<T> k) {
        if (k.isInstance(v)) return k.cast(v);
        if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) { T t = find(((ViewGroup) v).getChildAt(i), k); if (t != null) return t; }
        return null;
    }
    static void frames(int n) { for (int i = 0; i < n; i++) shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(17)); }

    static MotionEvent ev(long t0, int action, float[][] pts) {
        MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[pts.length];
        MotionEvent.PointerCoords[] pc = new MotionEvent.PointerCoords[pts.length];
        for (int i = 0; i < pts.length; i++) {
            pp[i] = new MotionEvent.PointerProperties(); pp[i].id = i; pp[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            pc[i] = new MotionEvent.PointerCoords(); pc[i].x = pts[i][0]; pc[i].y = pts[i][1]; pc[i].pressure = 1; pc[i].size = 1;
        }
        return MotionEvent.obtain(t0, SystemClock.uptimeMillis(), action, pts.length, pp, pc, 0, 0, 1, 1, 0, 0, 0, 0);
    }

    /** Two fingers on the waveforms move apart: the zoom shows fewer seconds and is remembered. */
    static void pinch(WheelView w) {
        RectF a = w.waveRect(0), b = w.waveRect(1);
        float cx = a.centerX(), y0 = a.centerY(), y1 = b.centerY(), d = a.width() * .1f;
        float before = w.zoomSec();
        long t0 = SystemClock.uptimeMillis();
        int pd1 = MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
        int pu1 = MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
        w.dispatchTouchEvent(ev(t0, MotionEvent.ACTION_DOWN, new float[][] { { cx - d, y0 } }));
        w.dispatchTouchEvent(ev(t0, pd1, new float[][] { { cx - d, y0 }, { cx + d, y1 } }));
        w.dispatchTouchEvent(ev(t0, MotionEvent.ACTION_MOVE, new float[][] { { cx - 2 * d, y0 }, { cx + 2 * d, y1 } }));
        w.dispatchTouchEvent(ev(t0, pu1, new float[][] { { cx - 2 * d, y0 }, { cx + 2 * d, y1 } }));
        w.dispatchTouchEvent(ev(t0, MotionEvent.ACTION_UP, new float[][] { { cx - 2 * d, y0 } }));
        float after = w.zoomSec();
        assertEquals("pinching out halves the seconds shown", before / 2, after, before * .02f);
    }

    void open(String name) throws Exception {
        ActivityController<MainActivity> c = Robolectric.buildActivity(MainActivity.class).setup();
        frames(30);
        View root = c.get().getWindow().getDecorView();
        assertNotNull(root);
        shot(root, name + ".png");
        TextView fatal = findText(root);
        assertFalse("the engine didn't load: " + (fatal == null ? "" : fatal.getText()), fatal != null && fatal.getText().toString().contains("could not be loaded"));

        Intent svc = shadowOf(RuntimeEnvironment.getApplication()).getNextStartedService();
        assertNotNull("the playback service was not started", svc);
        assertEquals(PlaybackService.class.getName(), svc.getComponent().getClassName());

        WheelView w = find(root, WheelView.class);
        assertNotNull(w);
        pinch(w);
        frames(2);
        shot(root, name + "-zoom.png");
        w.openSheet();
        frames(4);
        shot(root, name + "-setup.png");
        w.closeSheet();
        assertFalse(w.sheetOpen());
        c.pause().stop().destroy();
        assertNotNull("the playback service was not stopped", shadowOf(RuntimeEnvironment.getApplication()).getNextStoppedService());
    }
    @Test @Config(sdk = 34, qualifiers = "w800dp-h360dp-land-xxhdpi") public void phone() throws Exception { open("phone"); }
    @Test @Config(sdk = 34, qualifiers = "w1280dp-h800dp-land-mdpi") public void tablet() throws Exception { open("tablet"); }

    @Test @Config(sdk = 34) public void serviceGoesForeground() {
        ServiceController<PlaybackService> sc = Robolectric.buildService(PlaybackService.class).create().startCommand(0, 1);
        Notification n = shadowOf(sc.get()).getLastForegroundNotification();
        assertNotNull("no foreground notification", n);
        assertEquals("Omega Wheel of Steel — playing", n.extras.getString(Notification.EXTRA_TITLE));
        assertTrue((n.flags & Notification.FLAG_ONGOING_EVENT) != 0);
        assertNotNull(n.contentIntent);
        sc.destroy();
    }
}
