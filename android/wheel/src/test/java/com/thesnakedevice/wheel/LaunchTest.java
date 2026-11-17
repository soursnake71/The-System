package com.thesnakedevice.wheel;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.robolectric.Shadows.shadowOf;

import android.graphics.Bitmap;
import android.graphics.Canvas;
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
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowChoreographer;
import org.junit.Before;

/** Opens the app like a phone would (create, start, resume, a few frames) and saves what the screen shows. */
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
    void open(String name) throws Exception {
        ActivityController<MainActivity> c = Robolectric.buildActivity(MainActivity.class).setup();
        for (int i = 0; i < 30; i++) shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(17));
        View root = c.get().getWindow().getDecorView();
        assertNotNull(root);
        shot(root, name);
        TextView fatal = findText(root);
        assertFalse("the engine didn't load: " + (fatal == null ? "" : fatal.getText()), fatal != null && fatal.getText().toString().contains("could not be loaded"));
        c.pause().stop().destroy();
    }
    @Test @Config(sdk = 34, qualifiers = "w800dp-h360dp-land-xxhdpi") public void phone() throws Exception { open("phone.png"); }
    @Test @Config(sdk = 34, qualifiers = "w1280dp-h800dp-land-mdpi") public void tablet() throws Exception { open("tablet.png"); }
}
