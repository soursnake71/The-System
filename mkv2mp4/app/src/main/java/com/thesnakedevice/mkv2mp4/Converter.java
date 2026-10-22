package com.thesnakedevice.mkv2mp4;

import com.antonkarpenko.ffmpegkit.MediaInformation;
import com.antonkarpenko.ffmpegkit.StreamInformation;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Works out the FFmpeg command that turns one MKV into a streaming-friendly MP4. */
final class Converter {
  /** Text subtitles MP4 can carry (as mov_text). Picture subtitles (PGS, VobSub) can't go in an MP4 and are left out. */
  private static final Set<String> TEXT_SUBS = new HashSet<>(Arrays.asList("subrip", "srt", "ass", "ssa", "webvtt", "mov_text", "text"));
  /** Cover art and other still images some MKVs carry as video streams. */
  private static final Set<String> STILL_IMAGES = new HashSet<>(Arrays.asList("mjpeg", "png", "bmp", "gif", "webp"));

  static final class Plan {
    final List<String> args = new ArrayList<>();
    long durationMs;
    /** One line for the log, such as "video copied (HEVC), 2 audio to AAC, 1 subtitle". */
    String summary;
  }

  private Converter() {}

  /**
   * @param maxKbps the most video bitrate a Google TV dongle should be asked to stream (0 = no limit). Video above it is
   *     re-encoded under that limit even when it could be copied, since a big Blu-ray rip stutters over Wi-Fi.
   */
  static Plan plan(MediaInformation info, String input, String output, boolean maxCompat, int maxKbps) {
    StreamInformation video = null;
    List<StreamInformation> audio = new ArrayList<>();
    List<StreamInformation> subs = new ArrayList<>();
    int skippedSubs = 0;
    for (StreamInformation s : info.getStreams()) {
      String type = s.getType(), codec = codec(s);
      if ("video".equals(type)) {
        if (video == null && !STILL_IMAGES.contains(codec) && disposition(s, "attached_pic") == 0) video = s;
      } else if ("audio".equals(type)) {
        // the track the MKV marks as default goes first, since that is the one TVs play
        if (disposition(s, "default") == 1) audio.add(0, s); else audio.add(s);
      } else if ("subtitle".equals(type)) {
        if (TEXT_SUBS.contains(codec)) subs.add(s); else skippedSubs++;
      }
    }
    if (video == null) throw new IllegalArgumentException("no video track found");

    Plan p = new Plan();
    p.durationMs = durationMs(info);
    List<String> a = p.args;
    add(a, "-y", "-hide_banner", "-fflags", "+genpts", "-i", input);
    add(a, "-map", "0:" + video.getIndex());
    for (StreamInformation s : audio) add(a, "-map", "0:" + s.getIndex());
    for (StreamInformation s : subs) add(a, "-map", "0:" + s.getIndex());
    add(a, "-map_metadata", "0", "-map_chapters", "0");

    // Video: copy H.264 (8-bit) and HEVC straight across; anything else is re-encoded to H.264.
    // 10-bit H.264 ("Hi10P", common in anime) is re-encoded too, since TV hardware can't decode it.
    String vc = codec(video);
    boolean h264ok = "h264".equals(vc) && is8bit420(video);
    long kbps = videoKbps(info, video, audio);
    boolean tooBig = maxKbps > 0 && kbps > maxKbps;
    String videoNote;
    if ((h264ok || ("hevc".equals(vc) && !maxCompat)) && !tooBig) {
      add(a, "-c:v", "copy");
      // hvc1 is the HEVC tag Apple and Google TV players expect in MP4
      if ("hevc".equals(vc)) add(a, "-tag:v", "hvc1");
      videoNote = "video copied (" + label(vc) + (kbps > 0 ? ", " + mbps(kbps) : "") + ")";
    } else {
      add(a, "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", "-profile:v", "high");
      // aim 10% under the limit with a one-second buffer: x264 overshoots maxrate a little, and this keeps the
      // average under the limit and peaks close to it, so the stream never outruns the Wi-Fi
      if (maxKbps > 0) add(a, "-maxrate", (maxKbps * 9 / 10) + "k", "-bufsize", maxKbps + "k");
      // a keyframe every 2 seconds, so skipping around while streaming is quick
      add(a, "-force_key_frames", "expr:gte(t,n_forced*2)");
      Long h = video.getHeight();
      // a phone re-encoding 4K to H.264 would take hours; 1080p plays everywhere
      if (h != null && h > 1080) add(a, "-vf", "scale=-2:1080");
      videoNote = "video re-encoded " + label(vc) + ("h264".equals(vc) && !h264ok ? " 10-bit" : "") + " → H.264";
      if (tooBig) videoNote += " (was " + mbps(kbps) + ", now at most " + mbps(maxKbps) + ")";
      else if (maxKbps > 0) videoNote += " (at most " + mbps(maxKbps) + ")";
    }

    // Audio: AAC is what every Google TV / Chromecast app plays; AC-3, DTS, TrueHD, FLAC, Opus... become AAC.
    int copied = 0, encoded = 0;
    for (int i = 0; i < audio.size(); i++) {
      StreamInformation s = audio.get(i);
      int ch = channels(s);
      String n = String.valueOf(i);
      if ("aac".equals(codec(s)) && (!maxCompat || ch <= 2)) {
        add(a, "-c:a:" + n, "copy");
        copied++;
      } else {
        int out = maxCompat ? 2 : Math.min(Math.max(ch, 1), 6);
        add(a, "-c:a:" + n, "aac", "-ac:a:" + n, String.valueOf(out), "-b:a:" + n, out <= 2 ? "192k" : "384k");
        encoded++;
      }
      add(a, "-disposition:a:" + n, i == 0 ? "default" : "0");
    }
    for (int i = 0; i < subs.size(); i++) add(a, "-c:s:" + i, "mov_text", "-disposition:s:" + i, "0");

    // faststart puts the index at the front of the file, so a TV can start playing before it has the whole file
    add(a, "-movflags", "+faststart", "-max_muxing_queue_size", "9999", "-f", "mp4", output);

    StringBuilder sum = new StringBuilder(videoNote);
    if (copied > 0) sum.append(", ").append(copied).append(" audio copied");
    if (encoded > 0) sum.append(", ").append(encoded).append(" audio → AAC");
    if (audio.isEmpty()) sum.append(", no audio");
    if (!subs.isEmpty()) sum.append(", ").append(subs.size()).append(subs.size() == 1 ? " subtitle" : " subtitles");
    if (skippedSubs > 0) sum.append(" (").append(skippedSubs).append(" picture subtitle").append(skippedSubs == 1 ? "" : "s").append(" left out)");
    p.summary = sum.toString();
    return p;
  }

  private static void add(List<String> list, String... items) { list.addAll(Arrays.asList(items)); }

  private static String codec(StreamInformation s) {
    String c = s.getCodec();
    return c == null ? "" : c.toLowerCase();
  }

  private static int disposition(StreamInformation s, String key) {
    JSONObject d = s.getProperty("disposition");
    return d == null ? 0 : d.optInt(key, 0);
  }

  private static int channels(StreamInformation s) {
    Long ch = s.getNumberProperty("channels");
    return ch == null ? 2 : ch.intValue();
  }

  private static boolean is8bit420(StreamInformation s) {
    String pf = s.getStringProperty("pix_fmt");
    return pf == null || pf.equals("yuv420p") || pf.equals("yuvj420p");
  }

  /**
   * Video bitrate in kbit/s, or 0 if unknown. MKVs rarely store it on the stream, but mkvmerge writes a BPS tag;
   * failing both, it is the whole file's bitrate minus the audio.
   */
  private static long videoKbps(MediaInformation info, StreamInformation video, List<StreamInformation> audio) {
    long bps = number(video.getBitrate());
    JSONObject tags = video.getTags();
    if (bps <= 0 && tags != null) {
      for (java.util.Iterator<String> it = tags.keys(); it.hasNext() && bps <= 0; ) {
        String k = it.next();
        if (k.equalsIgnoreCase("BPS") || k.toUpperCase().startsWith("BPS-")) bps = number(tags.optString(k));
      }
    }
    if (bps <= 0) {
      bps = number(info.getBitrate());
      if (bps <= 0) return 0;
      for (StreamInformation s : audio) {
        long ab = number(s.getBitrate());
        bps -= ab > 0 ? ab : 640_000;
      }
    }
    return Math.max(bps, 0) / 1000;
  }

  private static long number(String s) {
    try {
      return s == null ? 0 : Long.parseLong(s.trim());
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  private static String mbps(long kbps) {
    return kbps >= 10_000 ? Math.round(kbps / 1000.0) + " Mbps" : String.format(java.util.Locale.US, "%.1f Mbps", kbps / 1000.0);
  }

  private static long durationMs(MediaInformation info) {
    try {
      return (long) (Double.parseDouble(info.getDuration()) * 1000);
    } catch (RuntimeException e) {
      return 0;
    }
  }

  private static String label(String codec) {
    switch (codec) {
      case "h264": return "H.264";
      case "hevc": return "HEVC";
      case "": return "unknown";
      default: return codec.toUpperCase();
    }
  }
}
