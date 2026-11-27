// Native check of the XR10 engine: renders presets, sounds and a recorded user pattern to WAV and checks the basics.
// Build: g++ -O2 -std=c++17 xr10/tools/test.cpp -o xr10_test && ./xr10_test outdir
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
  // math
  CHECK(std::fabs(xm::sin(1.234) - std::sin(1.234)) < 1e-9, "sin"); CHECK(std::fabs(xm::exp(-3.3) - std::exp(-3.3)) < 1e-12, "exp");
  CHECK(std::fabs(xm::log(123.4) - std::log(123.4)) < 1e-9, "log"); CHECK(std::fabs(xm::pow(2.5, 1.7) - std::pow(2.5, 1.7)) < 1e-9, "pow");
  printf(fails ? "%d FAILED\n" : "all ok\n", fails);
  return fails ? 1 : 0;
}
