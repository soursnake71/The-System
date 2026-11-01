// The Synsonics front panel, top-down like the photos: four pads in their square housings with the five red LEDs,
// the ACCENT bar, the TOM TOM 1 / SNARE / CYMBAL-MEMORY / TOM TOM 2 rate buttons in red outlined groups, and STOP /
// RECORD / PLAYBACK / SLOWER / FASTER / BASS. The header carries the STOCK / BENT switch, the tom 1 tune and volume
// thumbwheels (on the side of the real unit) and a status line. In BENT mode the Circuitbenders knob plate appears
// under the unit.
//
// Shared by the plugin and the Android app: it draws into a Canvas and takes pointer events (multitouch).
#pragma once
#include "Canvas.h"
#include "Engine.h"
#include <cstdio>
#include <functional>
#include <vector>

namespace syn {

// parameters go through the host (the plugin routes them through its automatable parameters)
struct PanelHost {
  virtual ~PanelHost() = default;
  virtual float getParam(int id) = 0;
  virtual void setParam(int id, float v) = 0;
  virtual void beginEdit(int) {}
  virtual void endEdit(int) {}
};

namespace pal {
constexpr Col bg = Col::hex(0x0c0c0d);
constexpr Col header = Col::hex(0x17171a);
constexpr Col body = Col::hex(0x1d1d1f);
constexpr Col bodyEdge = Col::hex(0x2c2c30);
constexpr Col tile = Col::hex(0x222224);
constexpr Col tileHi = Col::hex(0x2e2e31);
constexpr Col channel = Col::hex(0x111112);
constexpr Col well = Col::hex(0x080808);
constexpr Col pad = Col::hex(0x85868a);
constexpr Col padDark = Col::hex(0x6c6d71);
constexpr Col print = Col::hex(0xd23a2c);   // the red-orange printing
constexpr Col panel = Col::hex(0x151516);
constexpr Col key = Col::hex(0x8c8d91);
constexpr Col keyHi = Col::hex(0xa9aaae);
constexpr Col keyLo = Col::hex(0x5d5e62);
constexpr Col ledOff = Col::hex(0x3a0a08);
constexpr Col ledOn = Col::hex(0xff2a14);
constexpr Col text = Col::hex(0xd8d8dc);
constexpr Col dim = Col::hex(0x7a7a80);
constexpr Col knob = Col::hex(0x141415);
constexpr Col plate = Col::hex(0x19191b);
}  // namespace pal

class Panel {
 public:
  static constexpr float W = 1000, kStockH = 1370, kBentH = 1730;

  Panel(Engine& e, PanelHost& h) : eng(e), host(h) {}

  float logicalHeight() const { return bent() ? kBentH : kStockH; }
  float aspect() const { return W / logicalHeight(); }

  // safe-area insets in pixels (Android status/navigation bars)
  void setInsets(int top, int bottom) { insetTop = top; insetBottom = bottom; layoutValid = false; }

  // True if render() would draw something new for a w x h canvas (lets the app skip locking the window).
  bool changed(int w, int h) const { return !layoutValid || w != lastW || h != lastH || bent() != lastBent || stateHash() != lastHash; }

  // Draws into the canvas if anything changed. Returns true if pixels were written.
  bool render(Canvas& c, bool force = false) {
    bool b = bent();
    if (!layoutValid || c.w != lastW || c.h != lastH || b != lastBent) {
      layout(c.w, c.h);
      bgCache.assign(size_t(c.w) * c.h, 0);
      Canvas bgc;
      bgc.attach(bgCache.data(), c.w, c.h, c.w, c.rgba);
      bgc.setTransform(scale, offX, offY);
      drawStatic(bgc);
      lastW = c.w; lastH = c.h; lastBent = b; layoutValid = true;
      force = true;
    }
    uint64_t hsh = stateHash();
    if (!force && hsh == lastHash) return false;
    lastHash = hsh;
    for (int y = 0; y < c.h; ++y) std::memcpy(c.px + size_t(y) * c.stride, bgCache.data() + size_t(y) * c.w, size_t(c.w) * 4);
    c.setTransform(scale, offX, offY);
    c.resetClip();
    drawDynamic(c);
    return true;
  }

  // ---- input, in pixels. id: pointer / finger id
  void pointerDown(int id, float x, float y) {
    float lx = (x - offX) / scale, ly = (y - offY) / scale;
    int ci = hit(lx, ly);
    if (ci < 0) return;
    Ctl& c = ctls[ci];
    Touch t{id, ci, lx, ly, 0};
    switch (c.kind) {
      case KPad: eng.hit(c.id); padDown[c.id] = true; break;
      case KButton: eng.buttonDown(c.id); btnDown[c.id] = true; break;
      case KKnob: case KWheel: t.startVal = host.getParam(c.id); host.beginEdit(c.id); break;
      case KToggle: case KLatch: host.beginEdit(c.id); host.setParam(c.id, host.getParam(c.id) > 0.5f ? 0.f : 1.f); host.endEdit(c.id); break;
      case KMode: host.beginEdit(pMode); host.setParam(pMode, c.id ? 1.f : 0.f); host.endEdit(pMode); break;
    }
    touches.push_back(t);
    lastHash = ~0ull;
  }
  void pointerMove(int id, float x, float y) {
    float lx = (x - offX) / scale, ly = (y - offY) / scale;
    for (auto& t : touches) {
      if (t.id != id || t.ctl < 0 || t.ctl >= int(ctls.size())) continue;
      Ctl& c = ctls[t.ctl];
      if (c.kind == KKnob) host.setParam(c.id, std::clamp(t.startVal + ((t.y0 - ly) + (lx - t.x0) * 0.5f) / 260.f, 0.f, 1.f));
      if (c.kind == KWheel) host.setParam(c.id, std::clamp(t.startVal + (lx - t.x0) / 320.f, 0.f, 1.f));
    }
  }
  void pointerUp(int id) {
    for (size_t i = 0; i < touches.size(); ++i) {
      if (touches[i].id != id) continue;
      Touch t = touches[i];
      touches.erase(touches.begin() + long(i));
      if (t.ctl < 0 || t.ctl >= int(ctls.size())) return;
      Ctl& c = ctls[t.ctl];
      if (c.kind == KPad) padDown[c.id] = stillHeld(t.ctl);
      if (c.kind == KButton) { if (!stillHeld(t.ctl)) { eng.buttonUp(c.id); btnDown[c.id] = false; } }
      if (c.kind == KKnob || c.kind == KWheel) host.endEdit(c.id);
      lastHash = ~0ull;
      return;
    }
  }
  void pointerCancelAll() { while (!touches.empty()) pointerUp(touches.back().id); }
  // double tap on a knob: back to its default
  void pointerDoubleTap(float x, float y) {
    int ci = hit((x - offX) / scale, (y - offY) / scale);
    if (ci < 0) return;
    Ctl& c = ctls[ci];
    if (c.kind == KKnob || c.kind == KWheel) { host.beginEdit(c.id); host.setParam(c.id, paramInfo()[c.id].def); host.endEdit(c.id); }
  }

 private:
  enum Kind { KPad, KButton, KKnob, KWheel, KToggle, KLatch, KMode };
  struct Ctl { Kind kind; int id; float x, y, w, h; const char* label; };
  struct Touch { int id, ctl; float x0, y0, startVal; };

  Engine& eng;
  PanelHost& host;
  std::vector<Ctl> ctls;
  std::vector<Touch> touches;
  std::vector<uint32_t> bgCache;
  bool padDown[NumDrums] = {};
  bool btnDown[NumButtons] = {};
  float scale = 1, offX = 0, offY = 0;
  int lastW = -1, lastH = -1, insetTop = 0, insetBottom = 0;
  bool lastBent = false, layoutValid = false;
  uint64_t lastHash = 0;

  bool bent() const { return host.getParam(pMode) > 0.5f; }
  bool stillHeld(int ctl) const { for (auto& t : touches) if (t.ctl == ctl) return true; return false; }

  // ------------------------------------------------------------------------------------------------ layout
  // pad housings: top-left TOM TOM 1, top-right CYMBAL, bottom-left SNARE, bottom-right TOM TOM 2 (same places as
  // their button groups)
  struct PadPos { int drum; float x, y; int ledCorner; };  // ledCorner: 0 bottom-right, 1 bottom-left, 2 top-right, 3 top-left
  static constexpr float kTile = 448, kPadR = 176;
  static const PadPos* pads() {
    static const PadPos p[4] = {{Tom1, 42, 152, 0}, {Cymbal, 510, 152, 1}, {Snare, 42, 620, 2}, {Tom2, 510, 620, 3}};
    return p;
  }
  static void ledPos(const PadPos& p, float& x, float& y) {
    x = (p.ledCorner == 0 || p.ledCorner == 2) ? p.x + kTile - 92 : p.x + 92;
    y = p.ledCorner < 2 ? p.y + kTile - 22 : p.y + 22;
  }
  static constexpr float kKey = 50, kGroupW = 270, kGroupPad = 24, kGroupStep = 86, kCtlX = 734, kCtlStep = 80;
  // rate group origin: tom1 top-left, snare bottom-left, cymbal top-middle, tom2 bottom-middle
  static void groupRect(int drum, float& x, float& y) {
    x = (drum == Tom1 || drum == Snare) ? 140.f : 428.f;
    y = (drum == Tom1 || drum == Cymbal) ? 1104.f : 1196.f;
  }

  void layout(int pw, int ph) {
    float lh = logicalHeight();
    float availH = float(ph - insetTop - insetBottom);
    scale = std::min(pw / W, availH / lh);
    offX = (pw - W * scale) / 2;
    offY = insetTop + (availH - lh * scale) / 2;
    ctls.clear();
    // header
    ctls.push_back({KMode, 0, 24, 30, 128, 64, "STOCK"});
    ctls.push_back({KMode, 1, 152, 30, 128, 64, "BENT"});
    ctls.push_back({KWheel, pTune, 600, 50, 170, 50, "TOM TOM 1 TUNE"});
    ctls.push_back({KWheel, pVolume, 806, 50, 170, 50, "VOLUME"});
    for (int i = 0; i < 4; ++i) { const PadPos& p = pads()[i]; ctls.push_back({KPad, p.drum, p.x, p.y, kTile, kTile, ""}); }
    ctls.push_back({KButton, bAccent, 62, 1110, 54, 150, "ACCENT"});
    for (int d = 0; d < 4; ++d) {
      float gx, gy; groupRect(d, gx, gy);
      for (int s = 0; s < 3; ++s) ctls.push_back({KButton, rateButton(d, s), gx + kGroupPad + s * kGroupStep, gy + 12, kKey, kKey, ""});
    }
    const int ctlBtns[2][3] = {{bStop, bRecord, bPlayback}, {bSlower, bFaster, bBass}};
    for (int r = 0; r < 2; ++r)
      for (int k = 0; k < 3; ++k) ctls.push_back({KButton, ctlBtns[r][k], kCtlX + k * kCtlStep, 1100 + r * 94.f, kKey, kKey, ""});
    if (bent()) {
      // the Circuitbenders plate: two rows of five
      struct B { Kind k; int id; const char* l; };
      const B row[2][5] = {{{KKnob, pHatDecay, "HI HAT DECAY"}, {KLatch, pHatClosed, "HAT OPEN/CLOSED"}, {KKnob, pSnareDecay, "SNARE DECAY"},
                            {KKnob, pTom1Decay, "TOM 1 DECAY"}, {KKnob, pTom1Click, "TOM 1 CLICK"}},
                           {{KKnob, pTom1Drive, "TOM 1 LEVEL/OD"}, {KToggle, pTom1Boing, "TOM 1 BOING"}, {KKnob, pTom2Pitch, "TOM 2 PITCH"},
                            {KKnob, pTom2Decay, "TOM 2 DECAY"}, {KKnob, pTom2Drive, "TOM 2 LEVEL/OD"}}};
      for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 5; ++k) ctls.push_back({row[r][k].k, row[r][k].id, 40 + k * 184.f, 1428 + r * 146.f, 184, 140, row[r][k].l});
    }
  }

  int hit(float lx, float ly) const {
    // pads are round: hit the housing square (generous, like the real rubber pads)
    for (int i = int(ctls.size()) - 1; i >= 0; --i) {
      const Ctl& c = ctls[i];
      float m = c.kind == KButton ? 10.f : 0.f;
      if (lx >= c.x - m && lx < c.x + c.w + m && ly >= c.y - m && ly < c.y + c.h + m) return i;
    }
    return -1;
  }

  uint64_t stateHash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (int i = 0; i < NumDrums; ++i) mix(uint64_t(std::min(1.f, eng.display.led[i].load()) * 12));
    for (int i = 0; i < NumParams; ++i) mix(uint64_t(host.getParam(i) * 1000));
    mix(eng.display.seq); mix(eng.display.memory); mix(eng.display.bassOn); mix(eng.display.overdub);
    mix(uint64_t(eng.display.bpm.load() * 2));
    for (int i = 0; i < 4; ++i) mix(uint64_t(eng.display.rate[i] + 1));
    for (bool b : padDown) mix(b);
    for (bool b : btnDown) mix(b);
    // armed states blink
    int sq = eng.display.seq;
    if (sq == SeqRecArm || sq == SeqPlayArm || sq == SeqRecord) mix(uint64_t(blinkPhase()));
    return h;
  }
  bool blinkPhase() const { return eng.display.led[Bass] > 0.3f; }

  // ------------------------------------------------------------------------------------------------ drawing
  void label(Canvas& c, float x, float y, float size, const char* t, Canvas::Align a = Canvas::Center) { c.text(x, y, size, t, pal::print, a, 1.f, true); }

  void drawKey(Canvas& c, float x, float y, float w, float h, bool down, bool lit) {
    c.roundRect(x - 5, y - 5, w + 10, h + 10, 6, Col::hex(0x060606));
    float dy = down ? 2.5f : 0;
    Col top = down ? pal::key.scaled(0.8f) : pal::keyHi, bot = down ? pal::keyLo.scaled(0.8f) : pal::key;
    c.vGradient(x, y + dy, w, h, 4, top, bot);
    c.strokeRoundRect(x + 1, y + 1 + dy, w - 2, h - 2, 3.5f, 1.2f, Col(1, 1, 1, down ? 0.05f : 0.18f));
    if (lit) {
      c.roundRect(x, y + dy, w, h, 4, Col(1, 0.15f, 0.08f, 0.38f));
      c.strokeRoundRect(x - 2, y - 2 + dy, w + 4, h + 4, 5, 2.5f, pal::ledOn.withA(0.8f));
    }
  }

  void drawStatic(Canvas& c) {
    c.fillAllPixels(pal::bg);
    // header strip
    c.vGradient(0, 0, W, 122, 0, Col::hex(0x1e1e22), pal::header);
    c.rect(0, 121, W, 2, Col::hex(0x2a2a2e));
    c.text(600, 24, 13, "TOM TOM 1 TUNE", pal::dim, Canvas::Left);
    c.text(806, 24, 13, "VOLUME", pal::dim, Canvas::Left);
    // the unit
    c.vGradient(16, 134, 968, 1228, 22, Col::hex(0x232326), Col::hex(0x19191b));
    c.strokeRoundRect(16, 134, 968, 1228, 22, 2, pal::bodyEdge);
    c.rect(26, 588, 948, 14, pal::channel);            // the cross channel between the housings
    c.rect(490 + 4, 142, 12, 940, pal::channel);
    for (int i = 0; i < 4; ++i) drawPadHousing(c, pads()[i], false);
    // centre LED (bass / tempo)
    c.circle(500, 595, 13, Col::hex(0x0a0a0a));
    // control panel
    c.roundRect(34, 1088, 932, 220, 10, pal::panel);
    c.strokeRoundRect(34, 1088, 932, 220, 10, 2, Col::hex(0x0a0a0a));
    // accent bar
    drawKey(c, 62, 1110, 54, 150, false, false);
    label(c, 89, 1276, 15, "ACCENT");
    // rate groups
    const char* gname[4] = {"TOM TOM 1", "SNARE", "CYMBAL/ MEMORY", "TOM TOM 2"};
    for (int d = 0; d < 4; ++d) {
      float gx, gy; groupRect(d, gx, gy);
      float gw = kGroupW, gh = 76;
      c.strokeRoundRect(gx, gy, gw, gh, 6, 2.6f, pal::print);
      bool top = (d == Tom1 || d == Cymbal);
      float ly = top ? gy - 8 : gy + gh - 8;
      float tw = Canvas::textWidth(gname[d], 15) + 18;
      c.rect(gx + gw / 2 - tw / 2, ly - 2, tw, 20, pal::panel);
      label(c, gx + gw / 2, ly, 15, gname[d]);
      for (int s = 0; s < 3; ++s) drawKey(c, gx + kGroupPad + s * kGroupStep, gy + 12, kKey, kKey, false, false);
    }
    // control group
    c.strokeRoundRect(kCtlX - 16, 1090, 3 * kCtlStep + 12, 210, 6, 2.6f, pal::print);
    const char* top[3] = {"STOP", "RECORD", "PLAY-"};
    const char* bot[3] = {"SLOWER", "FASTER", "BASS"};
    for (int k = 0; k < 3; ++k) {
      float x = kCtlX + k * kCtlStep;
      drawKey(c, x, 1100, kKey, kKey, false, false);
      drawKey(c, x, 1194, kKey, kKey, false, false);
      label(c, x + kKey / 2, 1158, 12, top[k]);
      label(c, x + kKey / 2, 1252, 12, bot[k]);
    }
    float bx = kCtlX + 2 * kCtlStep + kKey / 2, tx = kCtlX + kCtlStep / 2 + kKey / 2;
    label(c, bx, 1172, 12, "BACK");
    label(c, bx, 1270, 12, "ON/OFF");
    // TEMPO with its arrow lines
    label(c, tx, 1272, 12, "TEMPO");
    c.line(kCtlX - 2, 1278, tx - 34, 1278, 2.2f, pal::print);
    c.line(tx + 34, 1278, kCtlX + kCtlStep + kKey + 4, 1278, 2.2f, pal::print);
    c.triangle(kCtlX - 8, 1278, kCtlX + 2, 1273, kCtlX + 2, 1283, pal::print);
    c.triangle(kCtlX + kCtlStep + kKey + 10, 1278, kCtlX + kCtlStep + kKey, 1273, kCtlX + kCtlStep + kKey, 1283, pal::print);
    // logo row
    Col fill = Col::hex(0x1b1b1d);
    c.text(56, 1316, 32, "SYNSONICS", pal::print, Canvas::Left, 1.3f, false, &fill);
    c.text(56 + Canvas::textWidth("SYNSONICS", 32, 1.3f) + 28, 1316, 32, "DRUMS", pal::print, Canvas::Left, 1.3f, false, &fill);
    c.text(600, 1326, 15, "OMEGA ELECTRONICS", pal::print, Canvas::Left, 1.f, true);
    c.text(948, 1326, 12, "5281", pal::dim, Canvas::Right);
    if (bent()) drawPlateStatic(c);
  }

  void drawPadHousing(Canvas& c, const PadPos& p, bool down) {
    // square housing with a soft bevel
    c.vGradient(p.x, p.y, kTile, kTile, 10, pal::tileHi, pal::tile);
    c.strokeRoundRect(p.x + 1, p.y + 1, kTile - 2, kTile - 2, 9, 1.5f, Col(1, 1, 1, 0.07f));
    float cx = p.x + kTile / 2, cy = p.y + kTile / 2;
    // the pad sits a little away from the centre channel
    cx += (p.x < 400 ? -6.f : 6.f);
    cy += (p.y < 500 ? -6.f : 6.f);
    c.circle(cx, cy, kPadR + 12, pal::well);
    float dy = down ? 3.f : 0.f;
    float k = down ? 0.82f : 1.f;
    c.circleShaded(cx, cy + dy, kPadR, [&](float x, float y) {
      float dx = (x - (cx - 60)) / (kPadR * 2.1f), dyy = (y - (cy - 70)) / (kPadR * 2.1f);
      float t = std::clamp(std::sqrt(dx * dx + dyy * dyy), 0.f, 1.f);
      return pal::pad.mix(pal::padDark, t).scaled(k);
    });
    c.ring(cx, cy + dy, kPadR - 1.5f, 1.5f, Col(0, 0, 0, 0.35f));
    float lx, ly; ledPos(p, lx, ly);
    c.circle(lx, ly, 12, Col::hex(0x0a0a0a));
  }

  void drawLed(Canvas& c, float x, float y, float level) {
    c.circle(x, y, 9.5f, pal::ledOff.mix(pal::ledOn, level));
    if (level > 0.05f) {
      c.circle(x, y, 22, pal::ledOn.withA(0.22f * level));
      c.circle(x - 2.5f, y - 3, 3.2f, Col(1, 0.8f, 0.7f, 0.6f * level));
    } else {
      c.circle(x - 2.5f, y - 3, 2.5f, Col(1, 0.5f, 0.4f, 0.18f));
    }
  }

  void drawWheel(Canvas& c, float x, float y, float w, float h, float v) {
    c.roundRect(x - 4, y - 4, w + 8, h + 8, 8, Col::hex(0x050505));
    c.vGradient(x, y, w, h, 6, Col::hex(0x2a2a2d), Col::hex(0x0f0f10));
    // ribs scroll with the value; shading makes it read as a cylinder
    float off = v * 260.f;
    for (int i = -2; i < 40; ++i) {
      float rx = x + std::fmod(off, 14.f) + i * 14.f - 14.f;
      if (rx < x + 3 || rx > x + w - 3) continue;
      float t = (rx - x) / w;
      float shade = std::sin(t * float(M_PI));
      c.rect(rx, y + 3, 3.5f, h - 6, Col(0.55f, 0.55f, 0.58f, 0.15f + 0.55f * shade));
    }
    c.vGradient(x, y, w, h * 0.4f, 6, Col(1, 1, 1, 0.10f), Col(1, 1, 1, 0));
    // position bar under the wheel
    c.rect(x, y + h + 8, w, 4, Col::hex(0x2b2b2f));
    c.rect(x, y + h + 8, w * v, 4, pal::print);
  }

  void drawKnob(Canvas& c, float cx, float cy, float r, float v) {
    // printed scale
    for (int i = 0; i <= 10; ++i) {
      float a = float(M_PI) * (0.75f + 1.5f * i / 10.f);
      float r0 = r + 7, r1 = r + (i % 5 == 0 ? 17 : 12);
      c.line(cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a), cy + r1 * std::sin(a), 2.2f, pal::print);
    }
    // black ribbed knob
    c.circle(cx, cy + 3, r + 2, Col(0, 0, 0, 0.6f));
    c.circle(cx, cy, r, Col::hex(0x0b0b0c));
    for (int i = 0; i < 24; ++i) {
      float a = i * float(M_PI) * 2 / 24;
      c.line(cx + (r - 5) * std::cos(a), cy + (r - 5) * std::sin(a), cx + (r - 1) * std::cos(a), cy + (r - 1) * std::sin(a), 2.f, Col::hex(0x2a2a2d));
    }
    c.circleShaded(cx, cy, r * 0.72f, [&](float x, float y) {
      float t = std::clamp(std::hypot(x - (cx - r * 0.3f), y - (cy - r * 0.3f)) / (r * 1.3f), 0.f, 1.f);
      return Col::hex(0x3a3a3e).mix(pal::knob, t);
    });
    float a = float(M_PI) * (0.75f + 1.5f * v);
    c.line(cx + r * 0.2f * std::cos(a), cy + r * 0.2f * std::sin(a), cx + r * 0.9f * std::cos(a), cy + r * 0.9f * std::sin(a), 4.f, Col::hex(0xf2f2f2));
  }

  void drawPlateStatic(Canvas& c) {
    c.vGradient(16, 1378, 968, 340, 18, Col::hex(0x1f1f22), pal::plate);
    c.strokeRoundRect(16, 1378, 968, 340, 18, 2, pal::bodyEdge);
    // four screws
    for (float sx : {36.f, 964.f})
      for (float sy : {1398.f, 1698.f}) {
        c.circle(sx, sy, 8, Col::hex(0x55565a));
        c.line(sx - 5, sy - 5, sx + 5, sy + 5, 2, Col::hex(0x2a2a2c));
      }
    c.text(500, 1396, 16, "CIRCUITBENDERS MODS", pal::print, Canvas::Center, 1.4f, true);
    for (auto& k : ctls) {
      if (k.y < 1400) continue;
      c.text(k.x + k.w / 2, k.y + 112, 12, k.label, pal::text, Canvas::Center);
      if (k.kind == KToggle) { c.text(k.x + k.w / 2 + 50, k.y + 22, 11, "ON", pal::dim, Canvas::Center); c.text(k.x + k.w / 2 + 50, k.y + 88, 11, "OFF", pal::dim, Canvas::Center); }
    }
  }

  void drawDynamic(Canvas& c) {
    // mode switch
    bool b = bent();
    c.roundRect(20, 26, 264, 72, 12, Col::hex(0x050506));
    for (int i = 0; i < 2; ++i) {
      bool on = (i == 1) == b;
      float x = 24 + i * 128.f;
      if (on) c.vGradient(x, 30, 128, 64, 9, Col::hex(0x3a3a40), Col::hex(0x26262a));
      c.text(x + 64, 55, 16, i ? "BENT" : "STOCK", on ? (i ? pal::ledOn : pal::text) : pal::dim, Canvas::Center, 1.2f, true);
    }
    // status line
    int sq = eng.display.seq, m = eng.display.memory + 1;
    char st[64];
    switch (sq) {
      case SeqRecArm: std::snprintf(st, sizeof st, "RECORD: PICK MEMORY"); break;
      case SeqPlayArm: std::snprintf(st, sizeof st, "PLAYBACK: PICK MEMORY"); break;
      case SeqRecord: std::snprintf(st, sizeof st, "REC MEMORY %d", m); break;
      case SeqPlay: std::snprintf(st, sizeof st, eng.display.overdub ? "OVERDUB MEMORY %d" : "PLAY MEMORY %d", m); break;
      default: std::snprintf(st, sizeof st, "READY"); break;
    }
    c.text(442, 34, 15, st, sq == SeqRecord || eng.display.overdub ? pal::ledOn : pal::text, Canvas::Center, 1.f, true);
    char bp[32];
    std::snprintf(bp, sizeof bp, "%d BPM%s", int(eng.display.bpm.load() + 0.5f), host.getParam(pSync) > 0.5f ? "" : "");
    c.text(442, 70, 22, bp, pal::text, Canvas::Center, 1.f, true);
    drawWheel(c, 600, 50, 170, 50, host.getParam(pTune));
    drawWheel(c, 806, 50, 170, 50, host.getParam(pVolume));

    // pads pressed
    for (int i = 0; i < 4; ++i) if (padDown[pads()[i].drum]) drawPadHousing(c, pads()[i], true);
    // LEDs
    for (int i = 0; i < 4; ++i) { float lx, ly; ledPos(pads()[i], lx, ly); drawLed(c, lx, ly, std::min(1.f, eng.display.led[pads()[i].drum].load())); }
    drawLed(c, 500, 595, std::min(1.f, eng.display.led[Bass].load()));

    // keys: pressed and lit states
    bool blink = blinkPhase();
    for (auto& k : ctls) {
      if (k.kind != KButton) continue;
      bool down = btnDown[k.id], lit = false;
      if (k.id >= bRate0) {
        int d = (k.id - bRate0) / 3, s = (k.id - bRate0) % 3;
        if (d == Cymbal && (sq == SeqRecArm || sq == SeqPlayArm)) lit = blink;
        else if (d == Cymbal && (sq == SeqRecord || sq == SeqPlay)) lit = (s == eng.display.memory) || eng.display.rate[d] == s;
        else lit = eng.display.rate[d] == s;
      } else if (k.id == bRecord) lit = sq == SeqRecArm ? blink : (sq == SeqRecord || eng.display.overdub.load());
      else if (k.id == bPlayback) lit = sq == SeqPlayArm ? blink : sq == SeqPlay;
      else if (k.id == bBass) lit = eng.display.bassOn;
      else if (k.id == bAccent) lit = false;
      if (down || lit) drawKey(c, k.x, k.y, k.w, k.h, down, lit);
    }

    if (b) {
      for (auto& k : ctls) {
        float cx = k.x + k.w / 2;
        if (k.kind == KKnob) drawKnob(c, cx, k.y + 56, 34, host.getParam(k.id));
        if (k.kind == KLatch) {
          bool closed = host.getParam(k.id) > 0.5f;
          drawKey(c, cx - 28, k.y + 30, 56, 44, false, false);
          drawLed(c, cx, k.y + 14, closed ? 1.f : 0.f);
          c.text(cx, k.y + 88, 11, closed ? "CLOSED" : "OPEN", closed ? pal::ledOn : pal::dim, Canvas::Center);
        }
        if (k.kind == KToggle) {
          bool on = host.getParam(k.id) > 0.5f;
          c.circle(cx, k.y + 60, 22, Col::hex(0x9a9ba0));
          c.circle(cx, k.y + 60, 15, Col::hex(0x6a6b70));
          float ty = on ? k.y + 22 : k.y + 96;
          c.line(cx, k.y + 60, cx, ty, 11, Col::hex(0xc9cace));
          c.circle(cx, ty, 8, Col::hex(0xe4e5e8));
        }
      }
    }
  }
};

}  // namespace syn
