package com.thesnakedevice.wheel;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The music on an SD card / folder chosen with the Storage Access Framework: one breadth-first DocumentsContract scan
 * (not DocumentFile per file), then crates: All tracks, Serato crates (_Serato_/Subcrates/*.crate, parsed by the
 * engine), .m3u / .m3u8 playlists and every folder with music in it. Tags are read by the engine in the background
 * (the length by MediaMetadataRetriever) and cached by document id + last-modified in a small append-only file, which
 * is rewritten with only the current tracks when it has gathered many stale or repeated lines.
 */
final class Library {
    static final int MAX_DEPTH = 12, MAX_ENTRIES = 80000, MAX_TRACKS = 30000;
    static final String[] AUDIO_EXT = { "mp3", "m4a", "aac", "mp4", "flac", "wav", "wave", "aif", "aiff", "aifc", "ogg", "oga", "opus" };

    static final class Track {
        String docId, name, rel, folder;   // rel: path from the tree root (no leading slash)
        long size, modified;
        String title = "", artist = "", album = "", bpm = "", key = "";
        boolean tagged, serato;
        int seconds = -1;                  // length, -1 = not known (yet)
        String lowerSearch;
        String label() { return title.isEmpty() ? name : title; }
        String search() {
            if (lowerSearch == null) lowerSearch = (title + " " + artist + " " + album + " " + name + " " + key + " " + bpm).toLowerCase(Locale.ROOT);
            return lowerSearch;
        }
    }

    static final int K_ALL = 0, K_CRATE = 1, K_PLAYLIST = 2, K_FOLDER = 3;
    static final class Crate {
        String name, id; int depth, kind;
        final List<Track> tracks = new ArrayList<>();
        Crate(String id, String name, int kind, int depth) { this.id = id; this.name = name; this.kind = kind; this.depth = depth; }
    }

    interface Listener { void onLibraryChanged(boolean tagsOnly); }

    private final Context ctx;
    private final ContentResolver cr;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService scanExec = Executors.newSingleThreadExecutor(r -> new Thread(r, "wheel-scan"));
    private final ExecutorService tagExec = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "wheel-tags");
        t.setPriority(Thread.MIN_PRIORITY);
        return t;
    });
    /** key -> {title, artist, album, bpm, key, serato, seconds} ("" seconds = never measured, "-1" = couldn't). */
    private final Map<String, String[]> tagCache = new HashMap<>();
    private boolean cacheLoaded;
    private int cacheLines;   // lines in the cache file (repeats and stale entries included)
    static final int CACHE_FIELDS = 7;
    private int scanGen;

    volatile Uri tree;
    volatile String treeName = "";
    volatile List<Crate> crates = Collections.emptyList();
    volatile boolean scanning;
    volatile String status = "Choose your SD card or music folder";
    Listener listener;

    Library(Context ctx) { this.ctx = ctx.getApplicationContext(); cr = this.ctx.getContentResolver(); }

    void shutdown() { scanExec.shutdownNow(); tagExec.shutdownNow(); }

    Uri uriOf(Track t) { return DocumentsContract.buildDocumentUriUsingTree(tree, t.docId); }

    static boolean isAudio(String lowerName) {
        int dot = lowerName.lastIndexOf('.');
        if (dot < 0) return false;
        String ext = lowerName.substring(dot + 1);
        for (String e : AUDIO_EXT) if (e.equals(ext)) return true;
        return false;
    }

    // ---- scanning
    private static final class Dir { final String id, rel; final int depth; Dir(String id, String rel, int depth) { this.id = id; this.rel = rel; this.depth = depth; } }
    private static final class Doc { String id, name, rel; }

    void scan(Uri treeUri) {
        tree = treeUri;
        final int g = ++scanGen;
        scanning = true;
        status = "Scanning…";
        notifyChanged(false);
        scanExec.execute(() -> {
            try { doScan(treeUri, g); }
            catch (Throwable e) {
                if (g == scanGen) { status = "Can't read this folder: choose it again"; crates = Collections.emptyList(); }
            } finally {
                if (g == scanGen) { scanning = false; notifyChanged(false); }
            }
        });
    }

    private void doScan(Uri treeUri, int g) throws IOException {
        String rootId = DocumentsContract.getTreeDocumentId(treeUri);
        treeName = queryName(treeUri, rootId);
        List<Track> all = new ArrayList<>();
        LinkedHashMap<String, List<Track>> folders = new LinkedHashMap<>();
        List<Doc> crateFiles = new ArrayList<>(), playlists = new ArrayList<>();
        ArrayDeque<Dir> queue = new ArrayDeque<>();
        queue.add(new Dir(rootId, "", 0));
        int entries = 0;
        String[] proj = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED };
        long lastNote = 0;
        while (!queue.isEmpty() && entries < MAX_ENTRIES && all.size() < MAX_TRACKS) {
            if (g != scanGen) return;
            Dir dir = queue.poll();
            Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, dir.id);
            List<Track> here = new ArrayList<>();
            boolean inSubcrates = dir.rel.toLowerCase(Locale.ROOT).endsWith("_serato_/subcrates");
            try (Cursor c = cr.query(kids, proj, null, null, null)) {
                if (c == null) continue;
                while (c.moveToNext()) {
                    if (++entries > MAX_ENTRIES) break;
                    String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                    if (id == null || name == null || name.startsWith(".") || name.startsWith("._")) continue;
                    String rel = dir.rel.isEmpty() ? name : dir.rel + "/" + name;
                    String lower = name.toLowerCase(Locale.ROOT);
                    if (Document.MIME_TYPE_DIR.equals(mime)) {
                        if (dir.depth + 1 > MAX_DEPTH) continue;
                        if (lower.equals("lost.dir") || lower.equals("android") || lower.equals("$recycle.bin")
                                || lower.equals("system volume information") || lower.startsWith(".trash")) continue;
                        // inside _Serato_ only the crates matter
                        if (dir.rel.toLowerCase(Locale.ROOT).endsWith("_serato_") && !lower.equals("subcrates")) continue;
                        queue.add(new Dir(id, rel, dir.depth + 1));
                        continue;
                    }
                    if (inSubcrates) {
                        if (lower.endsWith(".crate")) { Doc d = new Doc(); d.id = id; d.name = name; d.rel = rel; crateFiles.add(d); }
                        continue;
                    }
                    if (lower.endsWith(".m3u") || lower.endsWith(".m3u8")) { Doc d = new Doc(); d.id = id; d.name = name; d.rel = rel; playlists.add(d); continue; }
                    if (!isAudio(lower)) continue;
                    Track t = new Track();
                    t.docId = id; t.name = name; t.rel = rel; t.folder = dir.rel;
                    t.size = c.isNull(3) ? 0 : c.getLong(3);
                    t.modified = c.isNull(4) ? 0 : c.getLong(4);
                    String[] fb = TrackLoader.fallbackTitle(name);
                    t.title = fb[0]; t.artist = fb[1];
                    here.add(t);
                }
            } catch (SecurityException | IllegalArgumentException e) {
                if (dir.depth == 0) throw new IOException(e);
            }
            if (!here.isEmpty()) {
                here.sort((a, b) -> a.name.compareToIgnoreCase(b.name));
                all.addAll(here);
                folders.put(dir.rel, here);
            }
            long now = System.currentTimeMillis();
            if (now - lastNote > 400) { lastNote = now; status = "Scanning… " + all.size() + " tracks"; notifyChanged(false); }
        }
        if (g != scanGen) return;

        // lookup tables for crate / playlist entries
        Map<String, Track> byRel = new HashMap<>();
        Map<String, Track> byName = new HashMap<>();
        Map<String, Integer> nameCount = new HashMap<>();
        for (Track t : all) {
            byRel.put(t.rel.toLowerCase(Locale.ROOT), t);
            String n = t.name.toLowerCase(Locale.ROOT);
            byName.put(n, t);
            nameCount.merge(n, 1, Integer::sum);
        }

        List<Crate> out = new ArrayList<>();
        Crate allC = new Crate("all", "All tracks", K_ALL, 0);
        allC.tracks.addAll(all);
        out.add(allC);

        // Serato crates: "Parent%%Child.crate", shown as a tree
        crateFiles.sort((a, b) -> a.name.compareToIgnoreCase(b.name));
        java.util.Set<String> seenCrate = new java.util.HashSet<>();
        for (Doc d : crateFiles) {
            String base = d.name.substring(0, d.name.length() - ".crate".length());
            if (!seenCrate.add(base.toLowerCase(Locale.ROOT))) continue;   // same crate in two _Serato_ folders
            String[] parts = base.split("%%");
            Crate cr8 = new Crate("crate:" + base, parts[parts.length - 1], K_CRATE, parts.length - 1);
            try {
                byte[] bytes = readAll(DocumentsContract.buildDocumentUriUsingTree(treeUri, d.id), 16 << 20);
                String[] paths = Native.parseCrate(bytes);
                if (paths != null) for (String p : paths) {
                    Track t = resolve(p, byRel, byName, nameCount);
                    if (t != null) cr8.tracks.add(t);
                }
            } catch (Throwable ignored) {}
            out.add(cr8);
        }

        // playlists
        playlists.sort((a, b) -> a.name.compareToIgnoreCase(b.name));
        for (Doc d : playlists) {
            Crate pl = new Crate("m3u:" + d.rel, d.name.replaceFirst("(?i)\\.m3u8?$", ""), K_PLAYLIST, 0);
            String dirRel = d.rel.contains("/") ? d.rel.substring(0, d.rel.lastIndexOf('/')) : "";
            try {
                byte[] bytes = readAll(DocumentsContract.buildDocumentUriUsingTree(treeUri, d.id), 4 << 20);
                String text = new String(bytes, StandardCharsets.UTF_8);
                if (text.startsWith("\uFEFF")) text = text.substring(1);
                for (String line : text.split("\r?\n|\r")) {
                    line = line.trim();
                    if (line.isEmpty() || line.startsWith("#")) continue;
                    Track t = null;
                    if (!line.startsWith("/") && !line.matches("^[A-Za-z]:.*") && !line.contains("://"))
                        t = byRel.get(normRel(dirRel.isEmpty() ? line : dirRel + "/" + line));
                    if (t == null) t = resolve(line, byRel, byName, nameCount);
                    if (t != null) pl.tracks.add(t);
                }
            } catch (Throwable ignored) {}
            out.add(pl);
        }

        // every folder with music in it
        List<String> keys = new ArrayList<>(folders.keySet());
        Collections.sort(keys, String::compareToIgnoreCase);
        for (String k : keys) {
            String nm = k.isEmpty() ? (treeName.isEmpty() ? "(top folder)" : treeName) : k;
            Crate f = new Crate("dir:" + k, nm, K_FOLDER, 0);
            f.tracks.addAll(folders.get(k));
            out.add(f);
        }

        crates = out;
        status = all.size() + " tracks · " + crateFiles.size() + " crates" + (all.size() >= MAX_TRACKS ? " (limit reached)" : "");
        notifyChanged(false);
        readTagsInBackground(all, g);
    }

    /** "a/./b/../c" style cleanup + lower case, for map lookups. */
    private static String normRel(String p) {
        String[] parts = p.replace('\\', '/').split("/");
        ArrayDeque<String> st = new ArrayDeque<>();
        for (String s : parts) {
            if (s.isEmpty() || s.equals(".")) continue;
            if (s.equals("..")) { if (!st.isEmpty()) st.removeLast(); continue; }
            st.addLast(s);
        }
        return String.join("/", st).toLowerCase(Locale.ROOT);
    }

    /**
     * A path from a crate / playlist made on another computer ("Users/me/Music/x.mp3", "/Volumes/SD/Music/x.mp3",
     * "D:\Music\x.mp3", "file:///..."): the longest suffix of it that is a path in the tree; else the file name when
     * only one file has it.
     */
    static Track resolve(String path, Map<String, Track> byRel, Map<String, Track> byName, Map<String, Integer> nameCount) {
        if (path == null || path.isEmpty()) return null;
        String p = path.trim();
        if (p.regionMatches(true, 0, "file://", 0, 7)) {
            p = p.substring(7);
            try { p = java.net.URLDecoder.decode(p.replace("+", "%2B"), "UTF-8"); } catch (Exception ignored) {}
        }
        p = p.replace('\\', '/');
        if (p.matches("^[A-Za-z]:/.*")) p = p.substring(3);
        String lower = normRel(p);
        if (lower.startsWith("volumes/")) {
            int s = lower.indexOf('/', 8);
            lower = s > 0 ? lower.substring(s + 1) : lower;
        }
        String[] parts = lower.split("/");
        StringBuilder sb = new StringBuilder();
        for (int k = 0; k < parts.length; ++k) {
            sb.setLength(0);
            for (int j = k; j < parts.length; ++j) { if (j > k) sb.append('/'); sb.append(parts[j]); }
            Track t = byRel.get(sb.toString());
            if (t != null) return t;
        }
        String name = parts[parts.length - 1];
        Integer n = nameCount.get(name);
        return n != null && n == 1 ? byName.get(name) : null;
    }

    private byte[] readAll(Uri uri, int max) throws IOException {
        try (InputStream in = cr.openInputStream(uri)) {
            if (in == null) throw new IOException("can't open");
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) { bo.write(buf, 0, n); if (bo.size() > max) break; }
            return bo.toByteArray();
        }
    }

    private String queryName(Uri treeUri, String docId) {
        try (Cursor c = cr.query(DocumentsContract.buildDocumentUriUsingTree(treeUri, docId), new String[] { Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) { String s = c.getString(0); return s == null ? "" : s; }
        } catch (Throwable ignored) {}
        return "";
    }

    // ---- tags
    private static String key(Track t) { return t.docId + "|" + t.modified; }

    private void apply(Track t, String[] tags) {
        if (tags == null || tags.length < 5) return;
        if (tags[0] != null && !tags[0].isEmpty()) t.title = tags[0];
        if (tags[1] != null && !tags[1].isEmpty()) t.artist = tags[1];
        t.album = tags[2] == null ? "" : tags[2];
        t.bpm = tags[3] == null ? "" : tags[3];
        t.key = tags[4] == null ? "" : tags[4];
        t.serato = tags.length > 5 && "1".equals(tags[5]);
        t.seconds = tags.length > 6 ? parseSeconds(tags[6]) : -1;
        t.tagged = true;
        t.lowerSearch = null;
    }

    private File cacheFile() { return new File(ctx.getFilesDir(), "tags-v1.tsv"); }

    private void loadCache() {
        if (cacheLoaded) return;
        cacheLoaded = true;
        File f = cacheFile();
        if (!f.exists()) return;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                ++cacheLines;
                String[] p = line.split("\t", -1);
                // v1 lines have the six tags; lines since 1.2 add the length
                if (p.length >= 7) tagCache.put(p[0], new String[] { p[1], p[2], p[3], p[4], p[5], p[6], p.length > 7 ? p[7] : "" });
            }
        } catch (IOException ignored) {}
    }

    static int parseSeconds(String s) {
        if (s == null || s.isEmpty()) return -1;
        try { return Integer.parseInt(s.trim()); } catch (NumberFormatException e) { return -1; }
    }

    /** The track's length in whole seconds from the container's metadata, -1 if it has none. */
    static int durationSeconds(java.io.FileDescriptor fd) {
        MediaMetadataRetriever r = new MediaMetadataRetriever();
        try {
            r.setDataSource(fd);
            String ms = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            if (ms == null) return -1;
            long v = Long.parseLong(ms.trim());
            return v > 0 ? (int) Math.round(v / 1000.0) : -1;
        } catch (Exception e) {
            return -1;
        } finally {
            try { r.release(); } catch (Exception ignored) {}
        }
    }

    /** "m:ss" (or "h:mm:ss"), "" when unknown. */
    static String lengthText(int s) {
        if (s < 0) return "";
        int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
        return h > 0 ? String.format(Locale.US, "%d:%02d:%02d", h, m, sec) : String.format(Locale.US, "%d:%02d", m, sec);
    }

    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' '); }

    private void readTagsInBackground(List<Track> all, int g) {
        tagExec.execute(() -> {
            synchronized (tagCache) { loadCache(); }
            List<Track> todo = new ArrayList<>();
            Map<Track, String[]> cached = new HashMap<>();   // tags known, length not yet
            synchronized (tagCache) {
                for (Track t : all) {
                    String[] c = tagCache.get(key(t));
                    if (c != null) apply(t, c);
                    if (c == null || c[6].isEmpty()) { todo.add(t); if (c != null) cached.put(t, c); }
                }
            }
            notifyChanged(true);
            if (todo.isEmpty()) { compactCache(all, g); return; }
            Writer w = null;
            boolean finished = false;
            try {
                w = new OutputStreamWriter(new FileOutputStream(cacheFile(), true), StandardCharsets.UTF_8);
                long last = System.currentTimeMillis();
                int done = 0;
                for (Track t : todo) {
                    if (g != scanGen || Thread.currentThread().isInterrupted()) break;
                    String[] tags = cached.get(t);
                    int secs = -1;
                    try (ParcelFileDescriptor pfd = cr.openFileDescriptor(DocumentsContract.buildDocumentUriUsingTree(tree, t.docId), "r")) {
                        if (pfd != null) {
                            if (tags == null) tags = Native.readTags(pfd.getFd(), t.name);
                            secs = durationSeconds(pfd.getFileDescriptor());
                        }
                    } catch (Throwable ignored) {}
                    if (tags == null) tags = new String[] { "", "", "", "", "", "0" };
                    String[] rec = new String[CACHE_FIELDS];
                    for (int i = 0; i < 6; ++i) rec[i] = clean(i < tags.length ? tags[i] : "");
                    rec[6] = String.valueOf(secs);
                    apply(t, rec);
                    synchronized (tagCache) { tagCache.put(key(t), rec); }
                    w.write(key(t) + "\t" + String.join("\t", rec) + "\n");
                    ++cacheLines;
                    ++done;
                    long now = System.currentTimeMillis();
                    if (now - last > 700) { last = now; w.flush(); status = "Reading tags " + done + " / " + todo.size(); notifyChanged(true); }
                }
                finished = true;
            } catch (IOException ignored) {
            } finally {
                if (w != null) try { w.close(); } catch (IOException ignored) {}
            }
            if (finished) compactCache(all, g);
            if (g == scanGen) {
                int tracks = all.size();
                status = tracks + " tracks" + (tree != null ? " · " + treeName : "");
                notifyChanged(true);
            }
        });
    }

    /**
     * Rewrites the cache file with one line per current track when more than half of it (and at least 1000 lines) is
     * repeats (a track re-read after it changed) or tracks no longer in the library. Written to a temporary file and
     * renamed, so a crash never leaves half a cache.
     */
    private void compactCache(List<Track> all, int g) {
        if (g != scanGen) return;
        synchronized (tagCache) {
            Map<String, String[]> live = new LinkedHashMap<>();
            for (Track t : all) { String k = key(t); String[] v = tagCache.get(k); if (v != null) live.put(k, v); }
            int waste = cacheLines - live.size();
            if (!needsCompaction(cacheLines, live.size())) return;
            File f = cacheFile(), tmp = new File(f.getPath() + ".tmp");
            try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
                for (Map.Entry<String, String[]> e : live.entrySet()) w.write(e.getKey() + "\t" + String.join("\t", e.getValue()) + "\n");
            } catch (IOException e) {
                if (!tmp.delete()) tmp.deleteOnExit();
                return;
            }
            if (!tmp.renameTo(f)) { if (!tmp.delete()) tmp.deleteOnExit(); return; }
            tagCache.clear();
            tagCache.putAll(live);
            cacheLines = live.size();
            android.util.Log.i("wheel", "tag cache compacted: " + waste + " stale / repeated lines dropped");
        }
    }

    static boolean needsCompaction(int lines, int live) {
        int waste = lines - live;
        return waste >= 1000 && waste > live;
    }

    private void notifyChanged(boolean tagsOnly) {
        main.post(() -> { if (listener != null) listener.onLibraryChanged(tagsOnly); });
    }
}
