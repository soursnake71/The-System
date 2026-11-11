package com.thesnakedevice.drumscribe;

import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONArray;
import org.json.JSONObject;

/** The separation models for "Analyse on this phone" (Demucs htdemucs + DrumSep), downloaded once into the app's
 *  files folder from the drumscribe-models release. models.json there lists each file's size and SHA-256.
 *  Downloads resume (HTTP Range on a .part file); a file only gets its real name after its SHA-256 matched. */
final class ModelPack {
  static final String BASE = "https://github.com/soursnake71/The-System/releases/download/drumscribe-models/";
  static final String[] NEEDED = {"ggml-model-htdemucs-4s-f16.bin", "drumsep_mdx23c.onnx"};

  static final class Entry { String name, sha256; long size; }

  interface Progress { void at(long done, long total, String message); }

  private ModelPack() { }

  static File dir(Context c) { return new File(c.getFilesDir(), "models"); }

  static boolean installed(Context c) {
    for (String n : NEEDED) if (!new File(dir(c), n).isFile()) return false;
    return true;
  }

  static long installedBytes(Context c) {
    long s = 0;
    for (String n : NEEDED) s += new File(dir(c), n).length();
    return s;
  }

  static void delete(Context c) {
    File[] fs = dir(c).listFiles();
    if (fs != null) for (File f : fs) f.delete();
  }

  /** The files this app needs, from models.json. */
  static List<Entry> manifest() throws Exception {
    HttpURLConnection c = open(BASE + "models.json");
    if (c.getResponseCode() != 200) throw new Exception("model list: HTTP " + c.getResponseCode());
    ByteArrayOutputStream o = new ByteArrayOutputStream();
    try (InputStream in = c.getInputStream()) {
      byte[] b = new byte[8192];
      for (int n; (n = in.read(b)) > 0; ) o.write(b, 0, n);
    }
    JSONArray files = new JSONObject(new String(o.toByteArray(), StandardCharsets.UTF_8)).getJSONArray("files");
    List<Entry> out = new ArrayList<>();
    for (String need : NEEDED) {
      Entry e = null;
      for (int i = 0; i < files.length(); i++) {
        JSONObject f = files.getJSONObject(i);
        if (need.equals(f.optString("name"))) {
          e = new Entry();
          e.name = need;
          e.size = f.getLong("size");
          e.sha256 = f.getString("sha256").toLowerCase(java.util.Locale.ROOT);
        }
      }
      if (e == null) throw new Exception("the model list has no " + need);
      out.add(e);
    }
    return out;
  }

  static long total(List<Entry> m) {
    long s = 0;
    for (Entry e : m) s += e.size;
    return s;
  }

  static HttpURLConnection open(String url) throws Exception {
    HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
    c.setInstanceFollowRedirects(true);
    c.setConnectTimeout(15000);
    c.setReadTimeout(60000);
    c.setRequestProperty("User-Agent", "DrumScribe-Android");
    return c;
  }

  /** Downloads what's missing (resuming .part files), checks every SHA-256. */
  static void download(Context ctx, Progress p, AtomicBoolean cancel) throws Exception {
    File d = dir(ctx);
    d.mkdirs();
    p.at(0, 0, "Reading the model list");
    List<Entry> m = manifest();
    long total = total(m), base = 0;
    for (Entry e : m) {
      File fin = new File(d, e.name), part = new File(d, e.name + ".part");
      if (fin.isFile() && fin.length() == e.size) { base += e.size; continue; }
      fin.delete();
      long have = part.isFile() ? part.length() : 0;
      if (have > e.size) { part.delete(); have = 0; }
      if (have < e.size) {
        HttpURLConnection c = open(BASE + e.name);
        if (have > 0) c.setRequestProperty("Range", "bytes=" + have + "-");
        int code = c.getResponseCode();
        boolean append = code == 206;
        if (code != 200 && code != 206) throw new Exception(e.name + ": HTTP " + code);
        if (!append) have = 0;
        try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(part, append)) {
          byte[] b = new byte[1 << 16];
          long last = 0;
          for (int n; (n = in.read(b)) > 0; ) {
            if (cancel.get()) throw new Exception("Download paused: it continues where it stopped next time.");
            out.write(b, 0, n);
            have += n;
            long now = System.currentTimeMillis();
            if (now - last > 300) { last = now; p.at(base + have, total, "Downloading " + e.name); }
          }
        }
        if (have != e.size) throw new Exception(e.name + ": the download stopped early; try again to continue it.");
      }
      p.at(base + have, total, "Checking " + e.name);
      String sha = sha256(part);
      if (!sha.equals(e.sha256)) {
        part.delete();
        throw new Exception(e.name + " is damaged (SHA-256 mismatch): download it again.");
      }
      if (!part.renameTo(fin)) throw new Exception("can't store " + e.name);
      base += e.size;
    }
    p.at(total, total, "Models installed");
  }

  static String sha256(File f) throws Exception {
    MessageDigest md = MessageDigest.getInstance("SHA-256");
    try (InputStream in = new FileInputStream(f)) {
      byte[] b = new byte[1 << 16];
      for (int n; (n = in.read(b)) > 0; ) md.update(b, 0, n);
    }
    StringBuilder s = new StringBuilder();
    for (byte x : md.digest()) s.append(String.format(java.util.Locale.ROOT, "%02x", x));
    return s.toString();
  }
}
