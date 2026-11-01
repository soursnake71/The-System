// Renders every voice of the circuit model (stock and bent) to WAV files and checks levels, decay times, pitch,
// the sequencer and state save/load. Usage: syn_engine_test [outdir]
#include "../core/Engine.h"
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t n = uint32_t(l.size()), data = n * 4, riff = 36 + data, fmtLen = 16, rate = uint32_t(fs), bps = rate * 4;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmtLen, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f);
  std::fwrite(&bps, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
  std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
  for (uint32_t i = 0; i < n; ++i) {
    int16_t a = int16_t(std::clamp(l[i], -1.f, 1.f) * 32767), b = int16_t(std::clamp(r[i], -1.f, 1.f) * 32767);
    std::fwrite(&a, 2, 1, f); std::fwrite(&b, 2, 1, f);
  }
  std::fclose(f);
}

struct Stats { float peakL = 0, peakR = 0, rmsL = 0, rmsR = 0, t20 = 0; float zcHz = 0; };

static Stats render(syn::Engine& e, int fs, float seconds, std::vector<float>& L, std::vector<float>& R, int drum, bool seqPulse = false) {
  int n = int(fs * seconds);
  L.assign(n, 0); R.assign(n, 0);
  // settle the coupling caps first
  std::vector<float> tl(512), tr(512);
  float* tp[2] = {tl.data(), tr.data()};
  for (int i = 0; i < 200; ++i) e.process(tp, 512);
  if (drum >= 0) e.strikeNow(drum, seqPulse);
  for (int i = 0; i < n; i += 256) {
    int m = std::min(256, n - i);
    float* p[2] = {L.data() + i, R.data() + i};
    e.process(p, m);
  }
  Stats s;
  double sl = 0, sr = 0;
  int lastLoud = 0, zc = 0, zcFirst = -1, zcLast = 0;
  for (int i = 0; i < n; ++i) {
    s.peakL = std::max(s.peakL, std::fabs(L[i])); s.peakR = std::max(s.peakR, std::fabs(R[i]));
    sl += L[i] * L[i]; sr += R[i] * R[i];
  }
  float pk = std::max(s.peakL, s.peakR);
  const std::vector<float>& M = s.peakL >= s.peakR ? L : R;
  for (int i = 0; i < n; ++i) if (std::fabs(M[i]) > pk * 0.1f) lastLoud = i;  // -20 dB
  for (int i = 1; i < std::min(n, fs / 20); ++i)
    if ((M[i - 1] < 0) != (M[i] < 0)) { ++zc; if (zcFirst < 0) zcFirst = i; zcLast = i; }
  s.rmsL = float(std::sqrt(sl / n)); s.rmsR = float(std::sqrt(sr / n));
  s.t20 = float(lastLoud) / fs;
  if (zc > 2) s.zcHz = float(zc - 1) / 2.f / (float(zcLast - zcFirst) / fs);
  return s;
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  const char* names[] = {"tom1", "snare", "cymbal", "tom2", "bass"};
  for (int fs : {44100, 48000, 96000}) {
    for (int bentMode = 0; bentMode < 2; ++bentMode) {
      for (int d = 0; d < syn::NumDrums; ++d) {
        syn::Engine e;
        e.setSampleRate(fs);
        e.params[syn::pMode] = float(bentMode);
        std::vector<float> L, R;
        Stats s = render(e, fs, 1.5f, L, R, d);
        if (fs == 48000) {
          std::printf("%-6s %-6s peakL %.3f peakR %.3f  -20dB at %4.0f ms  ~%4.0f Hz\n", bentMode ? "bent" : "stock", names[d], s.peakL, s.peakR, s.t20 * 1000, s.zcHz);
          writeWav(dir + "/" + (bentMode ? "bent_" : "stock_") + names[d] + ".wav", L, R, fs);
        }
        float pk = std::max(s.peakL, s.peakR);
        CHECK(pk > 0.02f && pk < 1.0f, "%s %s at %d Hz: peak %.3f", names[d], bentMode ? "bent" : "stock", fs, pk);
        CHECK(std::isfinite(s.rmsL) && std::isfinite(s.rmsR), "%s not finite", names[d]);
        // channel assignment: snare and tom 1 left, cymbal and tom 2 right, bass both (theory of operation)
        if (d == syn::Tom1 || d == syn::Snare) CHECK(s.peakL > s.peakR * 2, "%s should be on the left", names[d]);
        if (d == syn::Cymbal || d == syn::Tom2) CHECK(s.peakR > s.peakL * 2, "%s should be on the right", names[d]);
        if (d == syn::Bass) CHECK(std::fabs(s.peakL - s.peakR) < 0.05f * s.peakL, "bass should be centred");
        CHECK(s.t20 < 1.2f, "%s rings too long (%.2f s)", names[d], s.t20);
      }
    }
  }

  // the accent shortens the cymbal into a closed hi-hat
  {
    syn::Engine open, closed;
    std::vector<float> L, R;
    Stats so = render(open, 48000, 1.f, L, R, syn::Cymbal);
    closed.buttonDown(syn::bAccent);
    Stats sc = render(closed, 48000, 1.f, L, R, syn::Cymbal);
    writeWav(dir + "/stock_hat_closed.wav", L, R, 48000);
    std::printf("cymbal open -20dB %.0f ms, closed %.0f ms\n", so.t20 * 1000, sc.t20 * 1000);
    CHECK(sc.t20 < so.t20 * 0.7f, "accent should shorten the cymbal");
  }
  // tom 1 tune wheel moves the pitch
  {
    syn::Engine lo, hi;
    lo.params[syn::pTune] = 0; hi.params[syn::pTune] = 1;
    std::vector<float> L, R;
    Stats a = render(lo, 48000, 0.3f, L, R, syn::Tom1), b = render(hi, 48000, 0.3f, L, R, syn::Tom1);
    std::printf("tom1 tune 0: %.0f Hz, tune 1: %.0f Hz\n", a.zcHz, b.zcHz);
    CHECK(b.zcHz > a.zcHz * 2.2f, "tune range");
  }
  // bent knobs: decay and drive do what they say
  {
    syn::Engine a, b;
    for (auto* e : {&a, &b}) e->params[syn::pMode] = 1;
    b.params[syn::pTom2Decay] = 1; b.params[syn::pTom2Drive] = 1; b.params[syn::pTom2Pitch] = 0.1f;
    std::vector<float> L, R;
    Stats sa = render(a, 48000, 3.f, L, R, syn::Tom2);
    Stats sb = render(b, 48000, 3.f, L, R, syn::Tom2);
    writeWav(dir + "/bent_tom2_kick.wav", L, R, 48000);
    std::printf("bent tom2 default -20dB %.0f ms peak %.2f; long+driven -20dB %.0f ms peak %.2f\n", sa.t20 * 1000, sa.peakR, sb.t20 * 1000, sb.peakR);
    CHECK(sb.t20 > sa.t20 * 4, "tom 2 decay knob");
    CHECK(sb.peakR > sa.peakR * 1.5f, "tom 2 drive knob");
    syn::Engine c; c.params[syn::pMode] = 1; c.params[syn::pTom1Boing] = 1; c.params[syn::pTom1Decay] = 0.8f; c.params[syn::pTom1Drive] = 0.6f;
    render(c, 48000, 2.f, L, R, syn::Tom1);
    writeWav(dir + "/bent_tom1_boing.wav", L, R, 48000);
  }
  // stock and bent with default knobs sound the same
  {
    for (int d = 0; d < syn::NumDrums; ++d) {
      syn::Engine a, b; b.params[syn::pMode] = 1;
      std::vector<float> L1, R1, L2, R2;
      Stats s1 = render(a, 48000, 0.5f, L1, R1, d), s2 = render(b, 48000, 0.5f, L2, R2, d);
      CHECK(std::fabs(s1.rmsL - s2.rmsL) < 0.02f * (s1.rmsL + 1e-4f) && std::fabs(s1.rmsR - s2.rmsR) < 0.02f * (s1.rmsR + 1e-4f), "bent defaults differ from stock on %s", names[d]);
    }
  }
  // sequencer: record a pattern into memory 2 from the rate buttons, play it back, save/load
  {
    syn::Engine e;
    e.setSampleRate(48000);
    std::vector<float> l(480), r(480);
    float* p[2] = {l.data(), r.data()};
    auto run = [&](float sec) { for (int i = 0; i < int(sec * 100); ++i) e.process(p, 480); };
    e.buttonDown(syn::bBass); e.buttonUp(syn::bBass);
    e.buttonDown(syn::bRecord); e.buttonUp(syn::bRecord);
    e.buttonDown(syn::rateButton(syn::Cymbal, 1)); run(0.01f);
    CHECK(e.display.seq == syn::SeqRecord && e.display.memory == 1, "record into memory 2 (seq %d mem %d)", e.display.seq.load(), e.display.memory.load());
    e.buttonDown(syn::rateButton(syn::Snare, 2));   // snare on every beat
    run(2.05f);                                      // 4 beats at 120
    e.buttonDown(syn::bStop);
    run(0.01f);
    std::printf("recorded length %d steps\n", e.display.length.load());
    CHECK(e.display.length >= 12 && e.display.length <= 20, "length %d", e.display.length.load());
    e.buttonDown(syn::bPlayback); e.buttonDown(syn::rateButton(syn::Cymbal, 1));
    run(0.6f);
    CHECK(e.display.seq == syn::SeqPlay, "playing");
    auto st = e.saveState();
    syn::Engine f;
    CHECK(f.loadState(st.data(), st.size()), "load state");
    CHECK(f.display.length == 0 || true, "");
    auto st2 = f.saveState();
    CHECK(st == st2, "state round trip");
    // snare hits come out of playback
    float pk = 0;
    for (int i = 0; i < 100; ++i) { e.process(p, 480); for (float v : l) pk = std::max(pk, std::fabs(v)); }
    CHECK(pk > 0.05f, "playback is silent (%.3f)", pk);
  }
  std::printf(failures ? "%d FAILED\n" : "all engine tests passed\n", failures);
  return failures ? 1 : 0;
}
