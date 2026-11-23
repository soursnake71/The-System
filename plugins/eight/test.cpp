// Renders the Omega 808 C++ engine to raw float files for eight/tools/compare.mjs, which renders the same scenarios with
// the app's JS engine and compares them sample by sample.   eight_test <outdir>
// Scenarios: every voice plain and accented (1 s), and two bars of the sequencer with REV7 and the 2290 (stock and modded).
#include "Seq.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace eight;
static const double SR = 48000;

static void write(const std::string& dir, const std::string& name, const std::vector<float>& L, const std::vector<float>& R) {
  FILE* f = std::fopen((dir + "/" + name + ".f32").c_str(), "wb");
  if (!f) { std::perror(name.c_str()); return; }
  std::fwrite(L.data(), sizeof(float), L.size(), f); std::fwrite(R.data(), sizeof(float), R.size(), f);
  std::fclose(f);
}

static void seqScenario(const std::string& dir, const std::string& name, bool modded) {
  Machine m(SR);
  m.set("rev.send.BD", 0.3); m.set("dly.send.SD", 0.4); m.set("dly.send.CH", 0.3); m.set("rev.program", 0);
  if (modded) {
    m.set("mod.drive", 0.5); m.set("mod.BD.longDecay", 0.5); m.set("mod.age", 1); m.set("swing", 0.5); m.set("CP.sel", 1); m.set("LT.sel", 1);
    m.set("mod.TRIG.src", 2); m.set("mod.TRIG.level", 0.6); m.set("rev.program", 11); m.set("dly.pingpong", 0); m.set("dly.sync", 0); m.set("dly.time", 230);
    m.set("SD.drive", 0); m.set("mod.SD.drive", 0.7); m.set("mod.CP.drive", 0.5);
    m.seq.select(1, 0, "AB", "B"); m.seq.setFillEvery(2);
  }
  m.seq.play(true);
  const int n = (int)(SR * 4.2);
  std::vector<float> L(n), R(n);
  for (int o = 0; o < n; o += 128) m.process(L.data() + o, R.data() + o, std::min(128, n - o));
  write(dir, name, L, R);
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  for (const char* v : voices()) for (int acc = 0; acc < 2; acc++) {
    Eight808 e(SR);
    if (std::string(v) == "TRIG") e.set("mod.TRIG.level", 0.8);
    e.trigger(v, acc, 1);
    const int n = (int)SR;
    std::vector<float> L(n), R(n);
    for (int o = 0; o < n; o += 128) e.processAt(L.data(), R.data(), std::min(128, n - o), o);
    write(dir, std::string(v) + (acc ? "-acc" : ""), L, R);
  }
  seqScenario(dir, "seq", false);
  seqScenario(dir, "seq-mod", true);
  std::printf("rendered to %s\n", dir.c_str());
  return 0;
}
