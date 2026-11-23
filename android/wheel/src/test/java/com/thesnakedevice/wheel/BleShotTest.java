package com.thesnakedevice.wheel;

import static org.junit.Assert.assertTrue;

import android.view.View;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowChoreographer;

/** The BLUETOOTH MIDI screen with a few devices "found" (no real scan): saved as a screenshot. */
@RunWith(RobolectricTestRunner.class)
public class BleShotTest {
    @Before public void display() { ShadowChoreographer.setPaused(true); ShadowChoreographer.setFrameDelay(java.time.Duration.ofMillis(16)); }

    void open(String name) throws Exception {
        ActivityController<MainActivity> c = Robolectric.buildActivity(MainActivity.class).setup();
        LaunchTest.frames(6);
        MainActivity a = c.get();
        a.getSharedPreferences("wheel", 0).edit().putString("bleAddr", "C4:4F:33:12:9A:07").putString("bleName", "Numark MixTrack Go Bluetooth").apply();
        a.showBle();
        BleMidi ble = (BleMidi) LibraryShotTest.field(a, "ble");
        BleView v = (BleView) LibraryShotTest.field(a, "bleView");
        ble.add(new BleMidi.Found("Numark MixTrack Go Bluetooth", "C4:4F:33:12:9A:07", -48, null));
        ble.add(new BleMidi.Found("WIDI Master", "F0:2A:61:0B:33:C1", -71, null));
        ble.add(new BleMidi.Found(null, "5E:91:0D:7C:22:A4", -88, null));
        v.connected.add("F0:2A:61:0B:33:C1");
        v.setDevices(ble.found());
        v.setStatus(true, "Scanning… turn the controller on");
        LaunchTest.frames(4);
        assertTrue(v.getVisibility() == View.VISIBLE);
        LaunchTest.shot(a.getWindow().getDecorView(), name + "-bluetooth.png");
        a.bleClose();
        c.pause().stop().destroy();
    }

    @Test @Config(sdk = 34, qualifiers = "w800dp-h360dp-land-xxhdpi") public void phone() throws Exception { open("phone"); }
    @Test @Config(sdk = 34, qualifiers = "w1280dp-h800dp-land-mdpi") public void tablet() throws Exception { open("tablet"); }
}
