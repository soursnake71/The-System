package com.thesnakedevice.wheel;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.net.Uri;
import android.os.Looper;
import android.view.View;
import java.lang.reflect.Field;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowChoreographer;

/**
 * Opens the library screen with a small made-up library (the same crates and tracks the plugin's
 * wheel_library_snapshot writes) and saves what it shows: the reference for the plugin's PhoneLibraryView.
 */
@RunWith(RobolectricTestRunner.class)
public class LibraryShotTest {
    @Before public void display() { ShadowChoreographer.setPaused(true); ShadowChoreographer.setFrameDelay(Duration.ofMillis(16)); }

    static Object field(Object o, String name) throws Exception {
        Field f = o.getClass().getDeclaredField(name);
        f.setAccessible(true);
        return f.get(o);
    }

    static Library.Track track(String rel, String title, String artist, String bpm, String key, int seconds) {
        Library.Track t = new Library.Track();
        t.rel = rel; t.name = rel.substring(rel.lastIndexOf('/') + 1); t.docId = rel;
        t.title = title; t.artist = artist; t.bpm = bpm; t.key = key; t.seconds = seconds; t.tagged = true;
        return t;
    }

    void open(String name) throws Exception {
        ActivityController<MainActivity> c = Robolectric.buildActivity(MainActivity.class).setup();
        LaunchTest.frames(10);
        MainActivity a = c.get();
        Library lib = (Library) field(a, "library");
        LibraryView lv = (LibraryView) field(a, "libView");
        assertNotNull(lv);

        Library.Track house = track("Music/House/DJ Test - Warehouse Jack.wav", "Warehouse Jack", "DJ Test", "126", "8A", 372);
        Library.Track night = track("Music/House/Late Shift - Deep Into The Night.wav", "Deep Into The Night", "Late Shift", "122", "5A", 401);
        Library.Track boom = track("Music/Hip Hop/DJ Test - Boom Bap 90.wav", "Boom Bap 90", "DJ Test", "90", "4A", 214);
        Library.Track gold = track("Music/Hip Hop/Crate Digger - Golden Era Loop.wav", "Golden Era Loop", "Crate Digger", "93.5", "11B", 188);
        Library.Track disco = track("Music/Disco/The Edits - Disco Strut.wav", "Disco Strut", "The Edits", "118", "2B", 330);
        Library.Track chords = track("Music/Keys/chords_am.wav", "", "", "", "", 40);
        List<Library.Crate> crates = new ArrayList<>();
        Library.Crate all = new Library.Crate("all", "All tracks", Library.K_ALL, 0);
        all.tracks.add(disco); all.tracks.add(boom); all.tracks.add(gold); all.tracks.add(night); all.tracks.add(house); all.tracks.add(chords);
        crates.add(all);
        Library.Crate hip = new Library.Crate("crate:Hip Hop", "Hip Hop", Library.K_CRATE, 0);
        hip.tracks.add(boom); hip.tracks.add(gold);
        crates.add(hip);
        Library.Crate bap = new Library.Crate("crate:Hip Hop%%Boom Bap", "Boom Bap", Library.K_CRATE, 1);
        bap.tracks.add(boom);
        crates.add(bap);
        Library.Crate houseC = new Library.Crate("crate:House", "House", Library.K_CRATE, 0);
        houseC.tracks.add(house); houseC.tracks.add(night); houseC.tracks.add(chords); houseC.tracks.add(disco);
        crates.add(houseC);
        Library.Crate set = new Library.Crate("m3u:Playlists/set.m3u8", "set", Library.K_PLAYLIST, 0);
        set.tracks.add(house); set.tracks.add(chords);
        crates.add(set);
        String[] dirs = { "Music/Disco", "Music/Hip Hop", "Music/House", "Music/Keys" };
        for (String d : dirs) {
            Library.Crate f = new Library.Crate("dir:" + d, d, Library.K_FOLDER, 0);
            for (Library.Track t : all.tracks) if (t.rel.startsWith(d + "/")) f.tracks.add(t);
            crates.add(f);
        }
        lib.tree = Uri.parse("content://com.android.externalstorage.documents/tree/1234-ABCD%3A");
        lib.treeName = "MPC CARD";
        lib.crates = crates;
        lib.status = all.tracks.size() + " tracks · 3 crates";

        a.openLibrary();
        LaunchTest.frames(4);
        lv.deckPaths[0] = house.rel;
        lv.deckPaths[1] = boom.rel;
        lv.midiMove(3);            // the House crate
        lv.midiSelect(0);          // into the tracks: the first one is selected
        lv.midiMove(1);            // the second
        LaunchTest.frames(6);
        assertTrue(lv.getVisibility() == View.VISIBLE);
        View root = a.getWindow().getDecorView();
        LaunchTest.shot(root, name + "-library.png");
        lv.midiBack();             // focus back on the crates
        LaunchTest.frames(4);
        LaunchTest.shot(root, name + "-library-crates.png");
        shadowOf(Looper.getMainLooper()).idle();
        c.pause().stop().destroy();
    }

    @Test @Config(sdk = 34, qualifiers = "w800dp-h360dp-land-xxhdpi") public void phone() throws Exception { open("phone"); }
    @Test @Config(sdk = 34, qualifiers = "w1280dp-h800dp-land-mdpi") public void tablet() throws Exception { open("tablet"); }
}
