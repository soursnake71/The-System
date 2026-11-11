package com.thesnakedevice.drumscribe;

/** The Drum Scribe engine (drumscribe/core, C++) running on the phone: libdrumscribe.so + libonnxruntime.so.
 *  One analysis at a time, on a native thread; see app/src/main/cpp/drumscribe_jni.cpp. */
final class Engine {
  static final boolean available;
  static final String loadError;

  static {
    boolean ok = false;
    String err = null;
    try {
      System.loadLibrary("drumscribe");
      ok = true;
    } catch (Throwable t) {
      err = t.getMessage();
    }
    available = ok;
    loadError = err;
  }

  private Engine() { }

  /** Starts the analysis of a raw PCM file (interleaved, 16-bit or float, any rate; deleted once read). pcmPath ""
   *  uses the stems cached in workDir. quality: "fast" (htdemucs). tempo > 0: constant grid. false if busy. */
  static native boolean start(String pcmPath, int sampleRate, int channels, boolean isFloat, String title,
      String workDir, String modelDir, String quality, double tempo, int demucsThreads);

  /** {"state": idle|running|done|error|cancelled, "stage", "progress" 0..1, "message", "work"} */
  static native String progress();

  static native void cancel();

  /** The finished job's result JSON, "" if none. */
  static native String result();

  /** Writes a MIDI file from workDir/result.json; part all|drums|bass. Returns its path. */
  static native String makeMidi(String workDir, String part, boolean split, boolean tempoMap);

  /** The grid at a fixed tempo (bpm > 0) or detected again (0): returns the new result JSON (also stored). */
  static native String retempo(String workDir, double bpm);

  static native String grooveText(String workDir);
}
