package com.thesnakedevice.drumscribe;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.security.MessageDigest;
import java.util.concurrent.atomic.AtomicBoolean;

/** Any audio or video file the phone can play -> raw PCM (the decoder's own rate, channels and sample format) in a
 *  file, for the engine (which converts it to 44.1 kHz stereo with its own resampler). */
final class AudioDecoder {
  static final class Pcm { int sampleRate, channels; boolean isFloat; long bytes; }

  interface Progress { void at(double fraction); }

  private AudioDecoder() { }

  static Pcm decode(Context ctx, Uri uri, File out, Progress progress, AtomicBoolean cancel) throws Exception {
    MediaExtractor ex = new MediaExtractor();
    MediaCodec codec = null;
    try {
      ex.setDataSource(ctx, uri, null);
      int track = -1;
      MediaFormat fmt = null;
      for (int i = 0; i < ex.getTrackCount(); i++) {
        MediaFormat f = ex.getTrackFormat(i);
        String mime = f.getString(MediaFormat.KEY_MIME);
        if (mime != null && mime.startsWith("audio/")) { track = i; fmt = f; break; }
      }
      if (track < 0) throw new Exception("That file has no audio track.");
      ex.selectTrack(track);
      long durUs = fmt.containsKey(MediaFormat.KEY_DURATION) ? fmt.getLong(MediaFormat.KEY_DURATION) : 0;
      Pcm pcm = new Pcm();
      pcm.sampleRate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE);
      pcm.channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
      codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
      codec.configure(fmt, null, null, 0);
      codec.start();
      MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
      boolean inputDone = false;
      try (FileOutputStream fo = new FileOutputStream(out); FileChannel ch = fo.getChannel()) {
        while (true) {
          if (cancel != null && cancel.get()) throw new Exception("Cancelled");
          if (!inputDone) {
            int in = codec.dequeueInputBuffer(10000);
            if (in >= 0) {
              ByteBuffer buf = codec.getInputBuffer(in);
              int n = ex.readSampleData(buf, 0);
              if (n < 0) {
                codec.queueInputBuffer(in, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                inputDone = true;
              } else {
                long t = ex.getSampleTime();
                codec.queueInputBuffer(in, 0, n, t, 0);
                ex.advance();
                if (durUs > 0 && progress != null) progress.at(Math.min(1.0, (double) t / durUs));
              }
            }
          }
          int o = codec.dequeueOutputBuffer(info, 10000);
          if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
            MediaFormat of = codec.getOutputFormat();
            if (pcm.bytes == 0) {  // the real output format (decoders may change rate / channels / encoding)
              pcm.sampleRate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE);
              pcm.channels = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
              pcm.isFloat = of.containsKey(MediaFormat.KEY_PCM_ENCODING)
                  && of.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT;
            }
          } else if (o >= 0) {
            if (info.size > 0) {
              ByteBuffer ob = codec.getOutputBuffer(o);
              ob.position(info.offset);
              ob.limit(info.offset + info.size);
              while (ob.hasRemaining()) pcm.bytes += ch.write(ob);
            }
            codec.releaseOutputBuffer(o, false);
            if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break;
          }
        }
      }
      if (pcm.bytes == 0) throw new Exception("Couldn't decode any audio from that file.");
      return pcm;
    } finally {
      if (codec != null) { try { codec.stop(); } catch (Exception ignored) { } codec.release(); }
      ex.release();
    }
  }

  /** The song's identity: SHA-256 of the file (its first 16 hex digits name the song's folder). */
  static String hash(Context ctx, Uri uri) throws Exception {
    MessageDigest md = MessageDigest.getInstance("SHA-256");
    try (InputStream in = ctx.getContentResolver().openInputStream(uri)) {
      if (in == null) throw new Exception("Can't open the file.");
      byte[] b = new byte[1 << 16];
      for (int n; (n = in.read(b)) > 0; ) md.update(b, 0, n);
    }
    StringBuilder s = new StringBuilder();
    for (byte x : md.digest()) s.append(String.format(java.util.Locale.ROOT, "%02x", x));
    return s.substring(0, 16);
  }
}
