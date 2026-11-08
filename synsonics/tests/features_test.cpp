// The sequencer grid, song, swing, mixer, REV7, delay, sample pad, and file import/export.
// Usage: syn_features_test [outdir]
#include "../core/Files.h"
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static void run(syn::Engine& e, double secs, std::vector<float>* L = nullptr, std::vector<float>* R = nullptr) {
  std::vector<float> l(256), r(256);
  float* p[2] = {l.data(), r.data()};
  int blocks = int(secs * e.getSampleRate() / 256);
  for (int i = 0; i < blocks; ++i) {
    e.process(p, 256);
    if (L) { L->insert(L->end(), l.begin(), l.end()); R->insert(R->end(), r.begin(), r.end()); }
  }
}
static float peakOf(const std::vector<float>& v, size_t a = 0, size_t b = SIZE_MAX) {
  float pk = 0;
  for (size_t i = a; i < std::min(b, v.size()); ++i) pk = std::max(pk, std::fabs(v[i]));
  return pk;
}
static void save(const std::string& path, const std::vector<uint8_t>& d) { FILE* f = std::fopen(path.c_str(), "wb"); if (f) { std::fwrite(d.data(), 1, d.size(), f); std::fclose(f); } }

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  using namespace syn;

  // a four-on-the-floor pattern in pattern 5, played from the grid
  Engine e;
  e.setSampleRate(48000);
  int P = 4;
  e.setPatternLength(P, 16);
  for (int s = 0; s < 16; s += 4) e.setStepBit(P, s, Bass, true);
  for (int s = 4; s < 16; s += 8) e.setStepBit(P, s, Snare, true);
  for (int s = 0; s < 16; s += 2) { e.setStepBit(P, s, Cymbal, true); e.setStepBit(P, s, kAccentBit, true); }
  e.setStepBit(P, 14, Tom1, true);
  e.setStepBit(P, 15, Tom2, true);
  e.playPattern(P);
  std::vector<float> L, R;
  run(e, 3.0, &L, &R);
  CHECK(e.display.seq == SeqPlay && e.display.memory == P, "grid play (seq %d mem %d)", e.display.seq.load(), e.display.memory.load());
  CHECK(peakOf(L) > 0.1f && peakOf(R) > 0.1f, "pattern plays (%.3f %.3f)", peakOf(L), peakOf(R));

  // song: patterns 5 then 6, then back
  e.setPatternLength(5, 8);
  e.setStepBit(5, 0, Tom2, true);
  e.setSongEntry(0, P); e.setSongEntry(1, 5); e.setSongLength(2);
  e.playSong();
  run(e, 0.6);
  CHECK(e.display.song && e.display.memory == P, "song starts on its first entry");
  run(e, 2.2);  // 16 steps at 120 = 2 s
  CHECK(e.display.memory == 5, "song moves to the second entry (mem %d)", e.display.memory.load());
  e.stopSequencer();
  run(e, 0.1);
  CHECK(e.display.seq == SeqIdle, "stop");

  // mixer: mute silences, solo isolates, pan moves, defaults equal the stock blend
  {
    Engine a, b;
    b.params[chParam(Snare, ChMute)] = 1;
    a.strikeNow(Snare); b.strikeNow(Snare);
    std::vector<float> al, ar, bl, br;
    run(a, 0.3, &al, &ar); run(b, 0.3, &bl, &br);
    CHECK(peakOf(al) > 0.1f && peakOf(bl) < 0.01f, "mute (%.3f vs %.3f)", peakOf(al), peakOf(bl));
    Engine c; c.params[chParam(Tom1, ChSolo)] = 1;
    c.strikeNow(Snare);
    std::vector<float> cl, cr; run(c, 0.3, &cl, &cr);
    CHECK(peakOf(cl) < 0.01f, "solo on tom 1 silences the snare");
    Engine d; d.params[chParam(Snare, ChPan)] = 1.f;  // hard right
    d.strikeNow(Snare);
    std::vector<float> dl, dr; run(d, 0.3, &dl, &dr);
    CHECK(peakOf(dr) > peakOf(dl) * 5, "pan right (%.3f %.3f)", peakOf(dl), peakOf(dr));
  }

  // REV7: a snare into send A leaves a tail; each program makes sound and stays finite
  for (int prog = 0; prog < kRevPrograms; ++prog) {
    Engine r;
    const RevProgram& pr = revPrograms()[prog];
    r.params[pRevProg] = float(prog) / (kRevPrograms - 1);
    r.params[pRevTime] = pr.time; r.params[pRevHigh] = pr.high; r.params[pRevLow] = pr.low; r.params[pRevInit] = pr.init;
    r.params[pRevFirst] = pr.first; r.params[pRevDiff] = pr.diff; r.params[pRevLive] = pr.live; r.params[pRevSize] = pr.size;
    r.params[chParam(Snare, ChSendA)] = 0.8f;
    Engine dryOnly;
    r.strikeNow(Snare); dryOnly.strikeNow(Snare);
    std::vector<float> rl, rr, ql, qr; run(r, 2.0, &rl, &rr); run(dryOnly, 2.0, &ql, &qr);
    float diff = 0;
    for (size_t i = 0; i < rl.size(); ++i) diff = std::max(diff, std::fabs(rl[i] - ql[i]) + std::fabs(rr[i] - qr[i]));
    // the dry snare is gone after ~0.1 s; what is left is the effect
    float early = peakOf(rl, 4800, 48000 / 2) + peakOf(rr, 4800, 48000 / 2), late = peakOf(rl, 48000 / 2, 48000), all = peakOf(rl);
    bool finite = true; for (float v : rl) finite &= std::isfinite(v);
    CHECK(finite && all < 2.f, "REV7 %s blows up", pr.name);
    CHECK(diff > 0.01f, "REV7 %s adds nothing (%.4f)", pr.name, diff);
    if (prog == 1) std::printf("REV7 HALL: peak %.3f, at 0.5-1 s %.4f\n", early, late);
    if (pr.algo == AHall) CHECK(late > 0.0005f, "REV7 %s has no tail", pr.name);
    if (prog == 1) { auto w = encodeWav(rl, rr, 48000, 16); save(dir + "/rev7_hall_snare.wav", w); }
  }
  // delay: echoes at the synced time (1/4 at 120 = 0.5 s)
  {
    Engine d;
    d.params[pDlyDiv] = 6.f / (kDelayDivs - 1);
    d.params[pDlyFb] = 0.3f;
    d.params[chParam(Tom2, ChSendB)] = 0.8f;
    d.strikeNow(Tom2);
    std::vector<float> dl, dr; run(d, 1.2, &dl, &dr);
    float before = peakOf(dl, 48000 * 25 / 100, 48000 * 45 / 100) + peakOf(dr, 48000 * 25 / 100, 48000 * 45 / 100), echo = peakOf(dr, 48000 * 45 / 100, 48000 * 6 / 10);
    std::printf("delay: before %.4f, echo at 0.5 s %.4f\n", before, echo);
    CHECK(before < 0.002f && echo > 0.02f, "delay echo timing");
  }
  // swing: odd 16ths start late
  {
    Engine s;
    s.params[pSwing] = 1.f;  // 40%
    s.setPatternLength(0, 4);
    s.setStepBit(0, 0, Bass, true); s.setStepBit(0, 1, Bass, true);
    s.playPattern(0);
    std::vector<float> sl, sr; run(s, 2.0, &sl, &sr);
    // find the first two onsets after the pattern starts
    std::vector<size_t> on;
    for (size_t i = 1; i < sl.size() && on.size() < 2; ++i) if (std::fabs(sl[i]) > 0.05f && std::fabs(sl[i - 1]) <= 0.05f) { on.push_back(i); i += 2000; }
    if (on.size() == 2) {
      double gap = double(on[1] - on[0]) / 48000.0, straight = 60.0 / 120 / 4;
      std::printf("swing gap %.1f ms (straight %.1f)\n", gap * 1000, straight * 1000);
      CHECK(gap > straight * 1.3, "swing delays the off 16th");
    } else CHECK(false, "swing: onsets not found");
  }

  // files: WAV and MP3 export decode back; MIDI round trip keeps the pattern
  {
    ExportOptions o; o.pattern = P; o.loops = 2;
    std::vector<uint8_t> wav, mp3, mid;
    CHECK(exportFile(e, o, OpExportWav, wav).empty(), "export wav");
    CHECK(exportFile(e, o, OpExportMp3, mp3).empty(), "export mp3");
    CHECK(exportFile(e, o, OpExportMidi, mid).empty(), "export midi");
    save(dir + "/" + exportName(e, o, OpExportWav), wav);
    save(dir + "/" + exportName(e, o, OpExportMp3), mp3);
    save(dir + "/" + exportName(e, o, OpExportMidi), mid);
    std::printf("export: wav %zu bytes, mp3 %zu bytes, midi %zu bytes\n", wav.size(), mp3.size(), mid.size());
    std::string err;
    auto w = decodeAudio(wav, err), m = decodeAudio(mp3, err);
    CHECK(w && w->frames() > 44100 * 4, "wav decodes (%s)", err.c_str());
    CHECK(m && m->frames() > 44100 * 4, "mp3 decodes (%s)", err.c_str());
    if (w && m) {
      float pw = peakOf(w->l), pm = peakOf(m->l);
      std::printf("wav peak %.3f, mp3 peak %.3f, %.2f s / %.2f s\n", pw, pm, w->frames() / w->rate, m->frames() / m->rate);
      CHECK(pw > 0.1f && std::fabs(pw - pm) < 0.25f * pw, "mp3 level close to wav");
    }
    Engine f;
    std::string info;
    CHECK(importFile(f, OpImportMidi, mid, "x.mid", 7, info).empty(), "import midi");
    std::printf("midi import: %s\n", info.c_str());
    // two loops exported: 32 steps, the first 16 equal to the pattern
    bool same = f.patternLength(7) == 32;
    for (int s = 0; s < 32; ++s) same &= f.step(7, s) == e.step(P, s % 16);
    CHECK(same, "midi round trip (length %d)", f.patternLength(7));
    // audio onto the sample pad, played from the grid
    CHECK(importFile(f, OpImportAudio, mp3, "/sdcard/loop.mp3", 0, info).empty(), "import mp3");
    std::printf("audio import: %s\n", info.c_str());
    CHECK(f.sample() && f.sample()->name == "loop.mp3", "sample name");
    f.setPatternLength(0, 16);
    f.setStepBit(0, 0, kSampleBit, true);
    f.playPattern(0);
    std::vector<float> fl, fr; run(f, 1.5, &fl, &fr);
    CHECK(peakOf(fl) > 0.05f, "sample pad plays from the grid (%.3f)", peakOf(fl));
    std::vector<uint8_t> junk = {1, 2, 3, 4, 5};
    CHECK(!importFile(f, OpImportAudio, junk, "x", 0, info).empty(), "junk rejected");
    CHECK(!importFile(f, OpImportMidi, junk, "x", 0, info).empty(), "junk midi rejected");
  }
  // state round trip with patterns, song and mixer
  {
    e.params[chParam(Bass, ChLevel)] = 0.5f;
    auto st = e.saveState();
    Engine g;
    CHECK(g.loadState(st.data(), st.size()), "load v2 state");
    CHECK(g.saveState() == st, "state round trip");
    CHECK(g.songLength() == 2 && g.patternLength(P) == 16 && g.step(P, 14) == e.step(P, 14), "patterns and song restored");
  }
  std::printf(failures ? "%d FAILED\n" : "all feature tests passed\n", failures);
  return failures ? 1 : 0;
}
