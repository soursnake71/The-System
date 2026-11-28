// Native check of the XR10 engine: renders presets, sounds and a recorded user pattern to WAV and checks the basics.
// Build: g++ -O2 -std=c++17 xr10/tools/test.cpp -o xr10_test && ./xr10_test outdir
#define _USE_MATH_DEFINES   // M_PI on Windows
#include "../core/xr10.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
using namespace xr;
static void wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
  FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
  int n = (int)L.size(), bytes = n * 4;
  auto w32 = [&](int v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](short v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
  fwrite("data", 1, 4, f); w32(bytes);
  for (int i = 0; i < n; i++) { for (float x : {L[i], R[i]}) { int v = (int)lrintf(x * 32767); if (v > 32767) v = 32767; if (v < -32768) v = -32768; w16((short)v); } }
  fclose(f);
}
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  const int sr = 48000;
  Machine* m = new Machine();
  m->init(sr);
  printf("Machine: %.1f MB\n", sizeof(Machine) / 1048576.0);
  u32 romUsed = m->romRef[63].start + m->romRef[63].len;
  printf("ROM: %u samples (%.1f s)\n", romUsed, romUsed / 44100.0);
  for (int i = 0; i < NSOUNDS; i++) CHECK(m->romRef[i].len > 100, "sound %d empty", i);
  // every sound, one after another
  {
    std::vector<float> L, R;
    for (int i = 0; i < NSOUNDS; i++) {
      m->trigger(i, 110);
      std::vector<float> l(sr * 6 / 10), r(sr * 6 / 10);
      m->process(l.data(), r.data(), nullptr, nullptr, (int)l.size());
      float pk = 0; for (float x : l) pk = std::max(pk, std::fabs(x));
      CHECK(pk > 0.02f, "sound %s silent (peak %f)", SOUND_NAMES[i], pk);
      CHECK(std::isfinite(pk), "sound %s not finite", SOUND_NAMES[i]);
      L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
    }
    wav(out + "/sounds.wav", L, R, sr);
  }
  // presets: 4 bars of VAR A, then fill, for a few styles
  for (int p : {0, 5, 10, 15, 27, 30, 33, 40, 44, 49}) {
    m->stop(); m->selectPattern(p); m->put(K_MODE, M_PATTERN);
    m->button(B_INTRO, 1); m->button(B_INTRO, 0);
    std::vector<float> L, R;
    double secPerBar = 60.0 / (m->set[K_TEMPO] / 10.0) * m->pat[p].beats;
    int total = (int)(secPerBar * 10 * sr), done = 0, block = 256;
    int fillAt = (int)(secPerBar * 4.5 * sr), endAt = (int)(secPerBar * 6.2 * sr);
    bool f1 = false, f2 = false;
    float pk = 0;
    while (done < total) {
      if (!f1 && done >= fillAt) { m->pad(14, 1); m->pad(14, 0); f1 = true; }
      if (!f2 && done >= endAt) { m->button(B_INTRO, 1); m->button(B_INTRO, 0); f2 = true; }
      std::vector<float> l(block), r(block);
      m->process(l.data(), r.data(), nullptr, nullptr, block);
      for (float x : l) pk = std::max(pk, std::fabs(x));
      L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
      done += block;
    }
    CHECK(pk > 0.1f, "pattern %d quiet", p);
    CHECK(!m->playing, "pattern %d still playing after ending", p + 1);
    wav(out + "/pattern" + std::to_string(p + 1) + ".wav", L, R, sr);
    printf("pattern %2d %-10s tempo %d peak %.2f events A=%d FA=%d IN=%d EN=%d\n", p + 1, m->pat[p].name, m->set[K_TEMPO] / 10, pk,
           m->pat[p].parts[0].count, m->pat[p].parts[3].count, m->pat[p].parts[6].count, m->pat[p].parts[7].count);
  }
  // record on a preset: copies to pattern 51; quantized hits land on 16ths
  m->stop(); m->selectPattern(0); m->put(K_REC, 1);
  CHECK(m->curPat == 50, "record on preset should copy to 51 (got %d)", m->curPat + 1);
  m->put(K_LEN, 1);
  m->button(B_START, 1); m->button(B_START, 0);
  int before = m->part().count;
  { std::vector<float> l(sr / 4 + 37), r(l.size()); m->process(l.data(), r.data(), nullptr, nullptr, (int)l.size()); }
  m->pad(1, 1, 100); m->pad(1, 0);
  CHECK(m->part().count == before + 1, "recorded hit missing");
  bool quant = false; for (int i = 0; i < m->part().count; i++) if (m->part().ev[i].sound == SNARE1 && m->part().ev[i].tick % 24 == 0) quant = true;
  CHECK(quant, "hit not quantized");
  m->button(B_START, 1);
  // state round trip
  Machine::Writer w{nullptr, 0, 0}; m->save(w);
  std::vector<u8> buf(w.n); Machine::Writer w2{buf.data(), 0, (u32)buf.size()}; m->save(w2);
  int cnt = m->pat[50].parts[0].count;
  Machine* m2 = new Machine(); m2->init(sr);
  CHECK(m2->load(buf.data(), (u32)buf.size()), "load failed");
  CHECK(m2->pat[50].parts[0].count == cnt, "state lost events");
  printf("state %u bytes\n", w.n);
  // LCD
  m->setMode(M_PATTERN); m->status();
  printf("[%s]\n[%s]\n", m->st.lcd[0], m->st.lcd[1]);
  m->setMode(M_SOUND); m->button(B_DOWN, 1); m->status(); printf("[%s]\n[%s]\n", m->st.lcd[0], m->st.lcd[1]);
  m->setMode(M_SONG); m->button(B_PLUS, 1); m->status(); printf("[%s]\n[%s]\n", m->st.lcd[0], m->st.lcd[1]);
  m->setMode(M_UTILITY); m->status(); printf("[%s]\n[%s]\n", m->st.lcd[0], m->st.lcd[1]);
  // song: 2 steps play through then stop
  m2->songSet(0, 0, 0, 0, 1); m2->songSet(0, 1, 1, P_END, 1); m2->put(K_MODE, M_SONG); m2->put(K_SONG, 0);
  m2->button(B_START, 1);
  CHECK(m2->playing && m2->playingSong, "song didn't start");
  { std::vector<float> l(sr * 12), r(l.size()); m2->process(l.data(), r.data(), nullptr, nullptr, (int)l.size()); }
  CHECK(!m2->playing, "song didn't end");
  // MIDI files: export a part, import it into another, the hits match
  {
    Machine* m3 = new Machine(); m3->init(sr);
    m3->selectPattern(10);
    std::vector<u8> mid(1 << 20);
    u32 n = m3->exportMidi(mid.data(), (u32)mid.size(), 0);
    CHECK(n > 30 && mid[0] == 'M' && mid[1] == 'T', "midi export");
    FILE* f = fopen((out + "/funk1.mid").c_str(), "wb"); fwrite(mid.data(), 1, n, f); fclose(f);
    int want = m3->pat[10].parts[0].count;
    m3->selectPattern(60);
    int bars = m3->importMidi(mid.data(), n);
    CHECK(bars == 2, "midi import bars %d", bars);
    CHECK(m3->pat[60].parts[0].count == want, "midi import events %d want %d", m3->pat[60].parts[0].count, want);
    bool same = true;
    for (int i = 0; i < want && i < m3->pat[60].parts[0].count; i++) { auto& a = m3->pat[10].parts[0].ev[i]; auto& b = m3->pat[60].parts[0].ev[i]; int ta = a.tick; if (ta % 48 == 24) ta = ta - 24 + m3->pat[10].swing * 48 / 100; same &= a.sound == b.sound && ta == b.tick && a.vel == b.vel; }
    CHECK(same, "midi round trip differs");
    // every sound survives the GM note map
    bool all = true; for (int s = 0; s < NSOUNDS; s++) { int nt = m3->soundToNote(s); if (m3->midiToSound(nt) != s) { all = false; printf("  sound %s -> note %d -> %d\n", SOUND_NAMES[s], nt, m3->midiToSound(nt)); } }
    CHECK(all, "GM note map loses sounds");
    // song export
    m3->songSet(0, 0, 0, P_INTRO, 1); m3->songSet(0, 1, 0, P_VARA, 2); m3->songSet(0, 2, 0, P_END, 1); m3->set[K_SONG] = 0;
    n = m3->exportMidi(mid.data(), (u32)mid.size(), 1);
    CHECK(n > 100, "song midi export");
    f = fopen((out + "/song.mid").c_str(), "wb"); fwrite(mid.data(), 1, n, f); fclose(f);
    CHECK(m3->importMidi((const u8*)"nope", 4) == -1, "non-midi refused");
  }
  // sample names
  {
    struct { const char* f; int s; } names[] = {{"KICK1.wav", 0}, {"kick10.WAV", 9}, {"07 KICK7.wav", 6}, {"Snare 10.wav", 19}, {"SNARE1.wav", 10}, {"Hat CL.wav", HATCL},
      {"HAT OP.wav", HATOP}, {"tom2 m.aif", TOM2M}, {"TOM4 H.wav", TOM4H}, {"Triangle H.wav", TRIH}, {"tambourine.wav", TAMB}, {"Timbales L.wav", TIMBL},
      {"VIBRASLAP.wav", VIBRA}, {"23.wav", 22}, {"Agogo H.wav", AGOGOH}, {"RIMSHOT2.wav", RIM2}, {"CRASH1.wav", CRASH1}, {"readme.txt", -1}};
    for (auto& t : names) CHECK(Machine::matchSoundName(t.f) == t.s, "name %s -> %d, want %d", t.f, Machine::matchSoundName(t.f), t.s);
  }
  // a sample ROM pack replaces sounds
  {
    std::vector<u8> pk; auto put32 = [&](u32 v) { for (int i = 0; i < 4; i++) pk.push_back((u8)(v >> (8 * i))); };
    put32(0x3250525a); put32(1); put32(ROM_HZ); put32(5); put32(1000); for (int i = 0; i < 1000; i++) { short v = (short)(i * 30); pk.push_back((u8)v); pk.push_back((u8)(v >> 8)); }
    Machine* m4 = new Machine(); m4->init(sr, pk.data(), (u32)pk.size());
    CHECK(m4->fromPack[5] && m4->romRef[5].len == 1000 && m4->rom[m4->romRef[5].start + 10] == 300, "rom pack");
    CHECK(!m4->fromPack[4] && m4->romRef[4].len > 1000, "unpacked sounds stay synthesized");
  }
  // the committed rom.bin: loads, and its synthesized sounds match the synthesizer
  {
    FILE* f = fopen("xr10/rom.bin", "rb");
    if (f) {
      std::vector<u8> rb; int c; while ((c = fgetc(f)) != EOF) rb.push_back((u8)c); fclose(f);
      Machine* m5 = new Machine(); m5->init(sr, rb.data(), (u32)rb.size());
      bool same = true;
      for (int s = 0; s < NSOUNDS; s++) if (!m5->fromPack[s]) { same &= m5->romRef[s].len == m->romRef[s].len; for (u32 k = 0; same && k < m5->romRef[s].len; k += 97) same &= m5->rom[m5->romRef[s].start + k] == m->rom[m->romRef[s].start + k]; }
      int mine = 0; for (int s = 0; s < NSOUNDS; s++) mine += m5->fromPack[s];
      CHECK(same, "rom.bin is stale: rebuild it with tools/make-rom");
      printf("rom.bin: %d sounds from samples, %d synthesized\n", mine, 64 - mine);
    }
  }
  // the output stage against the schematic
  {
    double L = LCLowpass::solveL();
    double dc = LCLowpass::response(0, L).mag();
    printf("BL-30VF inductors (solved for fc 12 kHz): %.1f mH each\n", L * 1000);
    CHECK(std::fabs(20 * std::log10(LCLowpass::response(12000, L).mag() / dc) + 3.0103) < 0.05, "LC low-pass not -3 dB at 12 kHz");
    // drive one channel with sines through the time-domain model and measure
    auto measure = [&](double f, double vol) {
      OutputChannel ch; const double fa = 192000; ch.design(fa, L, false); ch.setKnob(vol);
      double pk = 0; int n = (int)(fa * 0.3);
      for (int i = 0; i < n; i++) { double y = ch.run(std::sin(2 * M_PI * f * i / fa) * 0.1); if (i > n / 2) pk = std::max(pk, std::fabs(y)); }
      return pk / 0.1;
    };
    double g1k = measure(1000, 1.0);
    printf("output stage gain at 1 kHz, VOLUME max: %.2f (%.1f dB)\n", g1k, 20 * std::log10(g1k));
    for (double f : {100.0, 5000.0, 10000.0, 12000.0, 15000.0, 20000.0, 30000.0}) printf("  %6.0f Hz %+6.1f dB\n", f, 20 * std::log10(measure(f, 1.0) / g1k));
    // the whole chain: a full-scale 1 kHz sine from the DAC reads the manual's 8.2 dBm (2.81 V peak = 0.89 here)
    Machine* m6 = new Machine(); m6->init(48000);
    double vpk = 0;
    m6->outL.setKnob(1.0);
    const double fa = 48000.0 * m6->os;
    for (int j = 0; j < (int)fa; j++) { double y = m6->outL.run(std::sin(2 * M_PI * 1000 * j / fa) * m6->dacVolts); if (j > fa / 2) vpk = std::max(vpk, std::fabs(y)); }
    printf("full-scale sine at the jack: %.2f V peak (manual: 8.2 dBm = 2.81 V), DAC full scale %.3f V\n", vpk, m6->dacVolts);
    CHECK(std::fabs(vpk - 2.81) < 0.1, "output level calibration");
    // tuning steps: 100 cents and 6.25 cents
    m6->vp[0][V_TUNE] = 12; m6->trigger(0, 127);
    Voice* vv = nullptr; for (auto& o : m6->v) if (o.active && o.sound == 0) vv = &o;
    CHECK(vv && std::fabs(vv->baseInc - 2.0) < 1e-9, "tune +12 = an octave");
    m6->vp[0][V_TUNE] = 0; m6->vp[0][V_FINE] = 7; m6->trigger(0, 127);
    vv = nullptr; for (auto& o : m6->v) if (o.active && !o.fading && o.sound == 0) vv = &o;
    CHECK(vv && std::fabs(1200 * std::log2(vv->baseInc) - 43.75) < 1e-6, "fine +7 = 43.75 cents");
  }
  // hardware behaviour from the 1990 review: 15 velocity levels, 8 notes a step when recording
  {
    Machine* m7 = new Machine(); m7->init(48000);
    m7->trigger(0, 127); float g127 = 0; for (auto& o : m7->v) if (o.active) g127 = o.gL;
    m7->trigger(0, 60); float g60 = 0; for (auto& o : m7->v) if (o.active && !o.fading) g60 = o.gL;
    CHECK(std::fabs(g60 / g127 - 8.0 / 15.0) < 1e-6, "velocity 60 should be level 8 of 15 (%f)", g60 / g127);
    m7->selectPattern(55); m7->put(K_REC, 1); m7->put(K_TC, 0); m7->put(K_SOUND, 0);
    m7->button(B_START, 1);
    for (int s = 0; s < 9; s++) { m7->hitSound(s, 100); }
    int at0 = 0; for (int i = 0; i < m7->part().count; i++) at0 += m7->part().ev[i].tick == 0;
    CHECK(at0 == 8, "nine notes on one step should leave 8 (got %d)", at0);
    bool firstGone = true; for (int i = 0; i < m7->part().count; i++) if (m7->part().ev[i].tick == 0 && m7->part().ev[i].sound == 0) firstGone = false;
    CHECK(firstGone, "the earliest note on the step should be the one erased");
  }
  // dice
  {
    Machine* m8 = new Machine(); m8->init(48000);
    CHECK(m8->randomBeat(1234) && m8->curPat == 50 && m8->pat[50].parts[0].count > 8 && m8->pat[50].parts[3].count > 4, "random beat");
    CHECK(m8->randomBeat(99) && m8->curPat == 51, "second random beat goes to the next free pattern");
    m8->put(K_SOUND, 10); int before = m8->vp[10][V_FINE] * 100 + m8->vp[10][V_PAN];
    m8->randomSound(5); m8->randomSound(6);
    CHECK(m8->vp[10][V_DECAY] >= 0 && m8->vp[10][V_DECAY] <= 31 && m8->vp[10][V_TUNE] >= -26 && m8->vp[10][V_TUNE] <= 25, "random sound in range");
    (void)before;
    m8->start(); std::vector<float> l(48000 * 2), r(l.size()); m8->process(l.data(), r.data(), nullptr, nullptr, (int)l.size());
    float pk = 0; for (float x : l) pk = std::max(pk, std::fabs(x));
    CHECK(pk > 0.05f, "random beat plays");
  }
  // math
  CHECK(std::fabs(xm::sin(1.234) - std::sin(1.234)) < 1e-9, "sin"); CHECK(std::fabs(xm::exp(-3.3) - std::exp(-3.3)) < 1e-12, "exp");
  CHECK(std::fabs(xm::log(123.4) - std::log(123.4)) < 1e-9, "log"); CHECK(std::fabs(xm::pow(2.5, 1.7) - std::pow(2.5, 1.7)) < 1e-9, "pow");
  printf(fails ? "%d FAILED\n" : "all ok\n", fails);
  return fails ? 1 : 0;
}
