// Renders the panel (stock and bent) to PPM images, presses a few controls, and times a full redraw.
// Usage: syn_panel_snapshot [outdir]
#include "../core/Panel.h"
#include <chrono>
#include <string>

struct TestHost : syn::PanelHost {
  syn::Engine& e;
  explicit TestHost(syn::Engine& en) : e(en) {}
  float getParam(int id) override { return e.params[id]; }
  void setParam(int id, float v) override { e.params[id] = v; }
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
  for (int mode = 0; mode < 2; ++mode) {
    e.params[syn::pMode] = float(mode);
    int w = 1080, h = mode ? 2400 : 1480;
    std::vector<uint32_t> px(size_t(w) * h);
    syn::Canvas c;
    c.attach(px.data(), w, h, w, false);
    auto t0 = std::chrono::steady_clock::now();
    panel.render(c, true);
    auto t1 = std::chrono::steady_clock::now();
    // dynamic-only redraw (what happens per frame)
    e.params[syn::pTune] = e.params[syn::pTune] + 0.01f;
    panel.render(c);
    auto t2 = std::chrono::steady_clock::now();
    std::printf("mode %d: full %.1f ms, frame %.1f ms\n", mode, std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count());
    writePPM(dir + (mode ? "/panel_bent.ppm" : "/panel_stock.ppm"), px, w, h);
  }
  // lit state: press a pad, bass on, a rate button, and record arm
  e.params[syn::pMode] = 0;
  int w = 1080, h = 1480;
  std::vector<uint32_t> px(size_t(w) * h);
  syn::Canvas c; c.attach(px.data(), w, h, w, false);
  panel.render(c, true);
  float sc = std::min(w / 1000.f, h / syn::Panel::kStockH);
  float ox = (w - 1000 * sc) / 2, oy = (h - syn::Panel::kStockH * sc) / 2;
  panel.pointerDown(1, ox + 260 * sc, oy + 370 * sc);  // tom 1 pad
  e.buttonDown(syn::bBass);
  e.buttonDown(syn::rateButton(syn::Snare, 1));
  std::vector<float> l(256), r(256); float* p[2] = {l.data(), r.data()};
  e.process(p, 256);
  panel.render(c);
  writePPM(dir + "/panel_pressed.ppm", px, w, h);
  return 0;
}
