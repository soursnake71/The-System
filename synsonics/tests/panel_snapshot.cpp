// Renders every page of the panel (stock and bent drums, grid, mixer, FX, file) to PPM images after putting some
// content in, and times a full redraw and a frame. Usage: syn_panel_snapshot [outdir]
#include "../core/Panel.h"
#include <chrono>
#include <string>

struct TestHost : syn::PanelHost {
  syn::Engine& e;
  int requested = -1;
  explicit TestHost(syn::Engine& en) : e(en) {}
  float getParam(int id) override { return e.params[id]; }
  void setParam(int id, float v) override { e.params[id] = v; }
  void requestFile(int op) override { requested = op; }
};

static void writePPM(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
  FILE* f = std::fopen(path.c_str(), "wb");
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (uint32_t v : px) { unsigned char rgb[3] = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; std::fwrite(rgb, 1, 3, f); }
  std::fclose(f);
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  syn::Engine e;
  TestHost host(e);
  syn::Panel panel(e, host);
  // content: a pattern, a song, a sample
  for (int s = 0; s < 16; s += 4) e.setStepBit(0, s, syn::Bass, true);
  for (int s = 4; s < 16; s += 8) e.setStepBit(0, s, syn::Snare, true);
  for (int s = 0; s < 16; s += 2) e.setStepBit(0, s, syn::Cymbal, true);
  e.setStepBit(0, 6, syn::kAccentBit, true);
  e.setStepBit(0, 14, syn::Tom1, true);
  e.setPatternLength(0, 32);
  e.setPatternLength(3, 16);
  for (int i = 0; i < 6; ++i) e.setSongEntry(i, i % 2 ? 3 : 0);
  auto smp = std::make_shared<syn::SampleData>();
  smp->l.assign(44100, 0.1f); smp->name = "break.wav";
  e.setSample(smp);
  e.params[syn::chParam(syn::Snare, syn::ChSendA)] = 0.5f;
  e.params[syn::chParam(syn::Tom2, syn::ChMute)] = 1.f;
  e.params[syn::chParam(syn::Bass, syn::ChSolo)] = 0.f;
  e.playPattern(0);
  std::vector<float> l(512), r(512); float* p[2] = {l.data(), r.data()};
  for (int i = 0; i < 40; ++i) e.process(p, 512);

  const char* names[] = {"drums", "grid", "mixer", "fx", "file"};
  for (int mode = 0; mode < 2; ++mode) {
    e.params[syn::pMode] = float(mode);
    for (int t = 0; t < syn::Panel::NumTabs; ++t) {
      if (mode == 1 && t > 0) break;
      panel.setTab(t);
      int w = 1080, h = mode ? 2400 : 1560;
      std::vector<uint32_t> px(size_t(w) * h);
      syn::Canvas c;
      c.attach(px.data(), w, h, w, false);
      auto t0 = std::chrono::steady_clock::now();
      panel.render(c, true);
      auto t1 = std::chrono::steady_clock::now();
      e.process(p, 512);
      e.params[syn::pTune] = e.params[syn::pTune] + 0.01f;
      panel.render(c);
      auto t2 = std::chrono::steady_clock::now();
      std::printf("%s%s: full %.1f ms, frame %.1f ms\n", names[t], mode ? " (bent)" : "", std::chrono::duration<double, std::milli>(t1 - t0).count(),
                  std::chrono::duration<double, std::milli>(t2 - t1).count());
      writePPM(dir + "/panel_" + names[t] + (mode ? "_bent" : "") + ".ppm", px, w, h);
    }
  }
  // tapping: the FILE page's EXPORT WAV button asks the host for a file
  e.params[syn::pMode] = 0;
  panel.setTab(syn::Panel::TabFile);
  int w = 1080, h = 1560;
  std::vector<uint32_t> px(size_t(w) * h);
  syn::Canvas c; c.attach(px.data(), w, h, w, false);
  panel.render(c, true);
  float lh = panel.logicalHeight(), sc = std::min(w / 1000.f, h / lh);
  float ox = (w - 1000 * sc) / 2, oy = (h - lh * sc) / 2 + syn::Panel::kTabH * sc;
  panel.pointerDown(1, ox + 180 * sc, oy + 350 * sc);
  panel.pointerUp(1);
  std::printf("export wav tap -> host op %d\n", host.requested);
  // grid: tap a cell on pattern 1, row SNARE, step 3
  panel.setTab(syn::Panel::TabGrid);
  panel.render(c, true);
  uint8_t before = e.step(0, 2);
  panel.pointerDown(2, ox + (152 + 2 * 53 + 20) * sc, oy + (300 + 94 + 40) * sc);
  panel.pointerUp(2);
  std::printf("grid tap: step 3 bits %02x -> %02x\n", before, e.step(0, 2));
  return (host.requested == syn::OpExportWav && e.step(0, 2) != before) ? 0 : 1;
}
