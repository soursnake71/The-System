// The app's screens, drawn by a small software renderer and played with multitouch. Shared by the plugin and the
// Android app.
//
//  DRUMS  the Synsonics front panel, top-down like the photos: four pads in their housings with the five red LEDs,
//         ACCENT, the TOM TOM 1 / SNARE / CYMBAL-MEMORY / TOM TOM 2 rate buttons, STOP / RECORD / PLAYBACK /
//         SLOWER / FASTER / BASS, the STOCK / BENT switch, and the tune and volume thumbwheels. In BENT mode the
//         Circuitbenders knob plate appears under the unit.
//  GRID   the step sequencer: 16 patterns (1-3 are the front panel's memories), a 16-step page of the grid per voice
//         plus accent and the sample pad, length, swing, and the song chain.
//  MIXER  a channel strip per voice (level, pan, REV7 and delay sends, mute, solo, meter), the two returns, master.
//  FX     the REV7 (programs, display, its parameters, 3-band EQ), the delay, and the send matrix.
//  FILE   export WAV / MP3 / MIDI, import MIDI into the patterns, import WAV / MP3 onto the sample pad.
#pragma once
#include "Canvas.h"
#include "Engine.h"
#include "Files.h"
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace syn {

// parameters go through the host (the plugin routes them through its automatable parameters); files too, since
// only the host can show a file dialog
struct PanelHost {
  virtual ~PanelHost() = default;
  virtual float getParam(int id) = 0;
  virtual void setParam(int id, float v) = 0;
  virtual void beginEdit(int) {}
  virtual void endEdit(int) {}
  virtual void requestFile(int /*FileOp*/) {}
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
constexpr Col btn = Col::hex(0x2a2a2e);
constexpr Col btnHi = Col::hex(0x36363b);
constexpr Col cellOff = Col::hex(0x1e1e21);
constexpr Col cellOn = Col::hex(0xe23b26);
constexpr Col rack = Col::hex(0x232326);
constexpr Col lcd = Col::hex(0x8fa05a);
constexpr Col lcdInk = Col::hex(0x1d2410);
constexpr Col green = Col::hex(0x4fd16a);
constexpr Col amber = Col::hex(0xf0a020);
}  // namespace pal

class Panel {
 public:
  static constexpr float W = 1000, kTabH = 64, kStockH = 1370, kBentH = 1730;
  enum Tab { TabDrums, TabGrid, TabMixer, TabFx, TabFile, NumTabs };

  Panel(Engine& e, PanelHost& h) : eng(e), host(h) {}

  float logicalHeight() const { return kTabH + (bent() ? kBentH : kStockH); }
  float aspect() const { return W / logicalHeight(); }

  // safe-area insets in pixels (Android status/navigation bars)
  void setInsets(int top, int bottom) { insetTop = top; insetBottom = bottom; layoutValid = false; }

  // what the FILE page has chosen; the host reads this when it does the export or import
  ExportOptions exportOptions() const {
    ExportOptions o;
    o.song = exportSong; o.pattern = editPat; o.loops = exportLoops; o.wavBits = wavBits;
    o.sampleRate = eng.getSampleRate() >= 44100 ? int(std::min(96000.0, eng.getSampleRate())) : 44100;
    return o;
  }
  int editPattern() const { return editPat; }
  void setStatus(const std::string& s) { std::lock_guard<std::mutex> lk(statusMutex); status = s; ++statusVersion; }
  void setTab(int t) { tab = std::clamp(t, 0, NumTabs - 1); layoutValid = false; }

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
    Ctl c = ctls[size_t(ci)];
    Touch t{id, ci, lx, ly, 0, -1};
    switch (c.kind) {
      case KPad: eng.hit(c.id); if (c.id < NumDrums) padDown[c.id] = true; break;
      case KButton: eng.buttonDown(c.id); btnDown[c.id] = true; break;
      case KKnob: case KWheel: case KFader: t.startVal = host.getParam(c.id); host.beginEdit(c.id); break;
      case KToggle: case KLatch: host.beginEdit(c.id); host.setParam(c.id, host.getParam(c.id) > 0.5f ? 0.f : 1.f); host.endEdit(c.id); break;
      case KMode: host.beginEdit(pMode); host.setParam(pMode, c.id ? 1.f : 0.f); host.endEdit(pMode); break;
      case KTab: if (tab != c.id) { tab = c.id; layoutValid = false; } break;
      case KAction: action(c.id, c.a); break;
      case KCell: {
        int s = page * 16 + c.a;
        if (eng.patternLength(editPat) == 0) eng.setPatternLength(editPat, 16);  // a new pattern starts at one bar
        if (s >= eng.patternLength(editPat)) break;
        bool on = !((eng.step(editPat, s) >> c.id) & 1);
        eng.setStepBit(editPat, s, c.id, on);
        t.paint = on ? 1 : 0;
        break;
      }
      case KSong: {
        int n = eng.songLength();
        if (c.a < n) eng.setSongEntry(c.a, editPat);
        else if (c.a == n) eng.setSongEntry(c.a, editPat);
        break;
      }
    }
    touches.push_back(t);
    lastHash = ~0ull;
  }
  void pointerMove(int id, float x, float y) {
    float lx = (x - offX) / scale, ly = (y - offY) / scale;
    for (auto& t : touches) {
      if (t.id != id || t.ctl < 0 || t.ctl >= int(ctls.size())) continue;
      const Ctl& c = ctls[size_t(t.ctl)];
      if (c.kind == KKnob) host.setParam(c.id, std::clamp(t.startVal + ((t.y0 - ly) + (lx - t.x0) * 0.5f) / 260.f, 0.f, 1.f));
      if (c.kind == KWheel) host.setParam(c.id, std::clamp(t.startVal + (lx - t.x0) / 320.f, 0.f, 1.f));
      if (c.kind == KFader) host.setParam(c.id, std::clamp(t.startVal + (t.y0 - ly) / (c.h - 40), 0.f, 1.f));
      if (c.kind == KCell && t.paint >= 0) {  // drag to paint across the grid
        int ci = hit(lx, ly);
        if (ci >= 0 && ctls[size_t(ci)].kind == KCell) {
          const Ctl& o = ctls[size_t(ci)];
          int s = page * 16 + o.a;
          if (s < eng.patternLength(editPat)) eng.setStepBit(editPat, s, o.id, t.paint == 1);
        }
      }
    }
  }
  void pointerUp(int id) {
    for (size_t i = 0; i < touches.size(); ++i) {
      if (touches[i].id != id) continue;
      Touch t = touches[i];
      touches.erase(touches.begin() + long(i));
      if (t.ctl < 0 || t.ctl >= int(ctls.size())) return;
      const Ctl& c = ctls[size_t(t.ctl)];
      if (c.kind == KPad && c.id < NumDrums) padDown[c.id] = stillHeld(t.ctl);
      if (c.kind == KButton) { if (!stillHeld(t.ctl)) { eng.buttonUp(c.id); btnDown[c.id] = false; } }
      if (c.kind == KKnob || c.kind == KWheel || c.kind == KFader) host.endEdit(c.id);
      lastHash = ~0ull;
      return;
    }
  }
  void pointerCancelAll() { while (!touches.empty()) pointerUp(touches.back().id); }
  // double tap on a knob, wheel or fader: back to its default
  void pointerDoubleTap(float x, float y) {
    int ci = hit((x - offX) / scale, (y - offY) / scale);
    if (ci < 0) return;
    const Ctl& c = ctls[size_t(ci)];
    if (c.kind == KKnob || c.kind == KWheel || c.kind == KFader) { host.beginEdit(c.id); host.setParam(c.id, paramInfo()[c.id].def); host.endEdit(c.id); }
  }

 private:
  enum Kind { KPad, KButton, KKnob, KWheel, KToggle, KLatch, KMode, KTab, KAction, KCell, KSong, KFader };
  struct Ctl { Kind kind; int id; float x, y, w, h; const char* label; int a = 0; };
  struct Touch { int id, ctl; float x0, y0, startVal; int paint; };
  enum Action {
    APlay, AStop, ARec, ASongMode, ABpmDown, ABpmUp, APattern, ALenBarDown, ALenDown, ALenUp, ALenBarUp, APagePrev, APageNext,
    ACopy, APaste, AClear, ASongDel, ASongClear, ARevPrev, ARevNext, AScope, ALoops, ABits, AFile, ASmpPlay, ASmpClear
  };

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
  int tab = TabDrums;
  // grid
  int editPat = 0, page = 0;
  std::vector<uint8_t> clip;
  int clipLen = -1;
  // file page
  bool exportSong = false;
  int exportLoops = 1, wavBits = 24;
  mutable std::mutex statusMutex;
  std::string status = "Pick what to export or import.";
  int statusVersion = 0;
  int lastRevParam = -1;

  bool bent() const { return host.getParam(pMode) > 0.5f; }
  bool stillHeld(int ctl) const { for (auto& t : touches) if (t.ctl == ctl) return true; return false; }
  float contentH() const { return logicalHeight() - kTabH; }

  // ------------------------------------------------------------------------------------------------ actions
  void setP(int id, float v) { host.beginEdit(id); host.setParam(id, std::clamp(v, 0.f, 1.f)); host.endEdit(id); }
  int pages() const { return std::max(1, (eng.patternLength(editPat) + 15) / 16); }

  void action(int a, int arg) {
    switch (a) {
      case APlay: if (eng.songMode() && eng.songLength() > 0) eng.playSong(); else { if (eng.patternLength(editPat) == 0) eng.setPatternLength(editPat, 16); eng.playPattern(editPat); } break;
      case AStop: eng.stopSequencer(); break;
      case ARec: if (eng.display.seq == SeqPlay) { eng.buttonDown(bRecord); eng.buttonUp(bRecord); } break;
      case ASongMode: eng.setSongMode(!eng.songMode()); break;
      case ABpmDown: eng.setTempo(std::round(eng.tempo()) - 1); break;
      case ABpmUp: eng.setTempo(std::round(eng.tempo()) + 1); break;
      case APattern:
        editPat = arg; page = 0;
        if (eng.display.seq == SeqPlay && !eng.display.song) eng.cuePattern(arg);
        break;
      case ALenBarDown: eng.setPatternLength(editPat, std::max(16, (eng.patternLength(editPat) - 1) / 16 * 16)); break;
      case ALenDown: eng.setPatternLength(editPat, std::max(1, eng.patternLength(editPat) - 1)); break;
      case ALenUp: eng.setPatternLength(editPat, eng.patternLength(editPat) + 1); break;
      case ALenBarUp: eng.setPatternLength(editPat, (eng.patternLength(editPat) / 16 + 1) * 16); break;
      case APagePrev: page = std::max(0, page - 1); break;
      case APageNext: page = std::min(pages() - 1, page + 1); break;
      case ACopy:
        clip.resize(Engine::kMemSteps);
        for (int s = 0; s < Engine::kMemSteps; ++s) clip[size_t(s)] = eng.step(editPat, s);
        clipLen = eng.patternLength(editPat);
        break;
      case APaste:
        if (clipLen < 0) break;
        for (int s = 0; s < Engine::kMemSteps; ++s) eng.setStepBits(editPat, s, clip[size_t(s)]);
        eng.setPatternLength(editPat, clipLen);
        break;
      case AClear: eng.clearPattern(editPat); page = 0; break;
      case ASongDel: eng.setSongLength(eng.songLength() - 1); break;
      case ASongClear: eng.setSongLength(0); break;
      case ARevPrev: case ARevNext: {
        int p = revProgramIndex(host.getParam(pRevProg)) + (a == ARevNext ? 1 : -1);
        p = (p + kRevPrograms) % kRevPrograms;
        loadRevProgram(p);
        break;
      }
      case AScope: exportSong = arg != 0; break;
      case ALoops: exportLoops = arg; break;
      case ABits: wavBits = arg; break;
      case AFile: host.requestFile(arg); break;
      case ASmpPlay: eng.hit(Sample); break;
      case ASmpClear: eng.setSample(nullptr); setStatus("Sample pad cleared."); break;
    }
    if (page >= pages()) page = pages() - 1;
  }

  void loadRevProgram(int p) {
    const RevProgram& r = revPrograms()[p];
    setP(pRevProg, float(p) / float(kRevPrograms - 1));
    setP(pRevTime, r.time); setP(pRevHigh, r.high); setP(pRevLow, r.low); setP(pRevInit, r.init);
    setP(pRevFirst, r.first); setP(pRevDiff, r.diff); setP(pRevLive, r.live); setP(pRevSize, r.size);
  }

  // ------------------------------------------------------------------------------------------------ layout
  // DRUMS: pad housings: top-left TOM TOM 1, top-right CYMBAL, bottom-left SNARE, bottom-right TOM TOM 2 (same
  // places as their button groups)
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
  // GRID rows: voice bit, label
  struct Row { int bit; const char* name; int voice; };
  static const Row* rows() {
    static const Row r[7] = {{Tom1, "TOM 1", Tom1}, {Snare, "SNARE", Snare}, {Cymbal, "CYMBAL", Cymbal}, {kAccentBit, "ACCENT", -1},
                             {Tom2, "TOM 2", Tom2}, {Bass, "BASS", Bass}, {kSampleBit, "SAMPLE", Sample}};
    return r;
  }
  static constexpr float kGridY = 300, kRowH = 94, kCellX = 152, kCellW = 53;

  void layout(int pw, int ph) {
    float lh = logicalHeight();
    float availH = float(ph - insetTop - insetBottom);
    scale = std::min(pw / W, availH / lh);
    offX = (pw - W * scale) / 2;
    offY = insetTop + (availH - lh * scale) / 2 + kTabH * scale;  // content origin: under the tab bar
    ctls.clear();
    static const char* tabNames[NumTabs] = {"DRUMS", "GRID", "MIXER", "FX", "FILE"};
    for (int i = 0; i < NumTabs; ++i) ctls.push_back({KTab, i, i * W / NumTabs, -kTabH, W / NumTabs, kTabH, tabNames[i]});
    switch (tab) {
      case TabDrums: layoutDrums(); break;
      case TabGrid: layoutGrid(); break;
      case TabMixer: layoutMixer(); break;
      case TabFx: layoutFx(); break;
      case TabFile: layoutFile(); break;
    }
  }
  void act(float x, float y, float w, float h, const char* l, int a, int arg = 0) { ctls.push_back({KAction, a, x, y, w, h, l, arg}); }

  void layoutDrums() {
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

  void layoutGrid() {
    act(16, 16, 110, 70, "PLAY", APlay);
    act(134, 16, 110, 70, "STOP", AStop);
    act(252, 16, 110, 70, "REC", ARec);
    act(370, 16, 110, 70, "SONG", ASongMode);
    act(498, 16, 64, 70, "-", ABpmDown);
    act(706, 16, 64, 70, "+", ABpmUp);
    ctls.push_back({KWheel, pSwing, 794, 42, 186, 44, "SWING"});
    for (int i = 0; i < Engine::kNumPat; ++i) act(16 + (i % 8) * 122.f, 104 + (i / 8) * 54.f, 116, 48, "", APattern, i);
    act(118, 222, 64, 58, "-BAR", ALenBarDown);
    act(186, 222, 52, 58, "-1", ALenDown);
    act(372, 222, 52, 58, "+1", ALenUp);
    act(428, 222, 64, 58, "+BAR", ALenBarUp);
    act(560, 222, 52, 58, "<", APagePrev);
    act(706, 222, 52, 58, ">", APageNext);
    act(780, 222, 64, 58, "COPY", ACopy);
    act(848, 222, 64, 58, "PASTE", APaste);
    act(916, 222, 64, 58, "CLEAR", AClear);
    for (int r = 0; r < 7; ++r) {
      const Row& row = rows()[r];
      float y = kGridY + r * kRowH;
      if (row.voice >= 0) ctls.push_back({KPad, row.voice, 16, y + 4, 128, kRowH - 8, row.name});  // tap the name to hear it
      for (int col = 0; col < 16; ++col) ctls.push_back({KCell, row.bit, kCellX + col * kCellW, y + 4, kCellW - 4, kRowH - 8, "", col});
    }
    for (int i = 0; i < 48; ++i) ctls.push_back({KSong, 0, 16 + (i % 16) * 61.f, 1012 + (i / 16) * 52.f, 56, 46, "", i});
    act(16, 1176, 180, 56, "DEL LAST", ASongDel);
    act(206, 1176, 180, 56, "CLEAR SONG", ASongClear);
  }

  static float stripX(int i) { return 12 + i * 109.f; }
  void layoutMixer() {
    for (int v = 0; v < NumVoices; ++v) {
      float x = stripX(v), cx = x + 52;
      ctls.push_back({KKnob, chParam(v, ChSendA), cx - 40, 64, 80, 80, "REV7"});
      ctls.push_back({KKnob, chParam(v, ChSendB), cx - 40, 172, 80, 80, "DELAY"});
      ctls.push_back({KKnob, chParam(v, ChPan), cx - 40, 280, 80, 80, "PAN"});
      ctls.push_back({KToggle, chParam(v, ChMute), x + 4, 384, 46, 44, "M"});
      ctls.push_back({KToggle, chParam(v, ChSolo), x + 54, 384, 46, 44, "S"});
      ctls.push_back({KFader, chParam(v, ChLevel), x + 10, 450, 60, 780, ""});
    }
    ctls.push_back({KFader, pRetA, stripX(6) + 10, 450, 60, 780, ""});
    ctls.push_back({KFader, pRetB, stripX(7) + 10, 450, 60, 780, ""});
    ctls.push_back({KFader, pMaster, stripX(8) + 10, 450, 60, 780, ""});
    ctls.push_back({KPad, Sample, stripX(5) + 4, 20, 96, 36, ""});  // tap the SAMPLE name to hear it
  }

  void layoutFx() {
    act(680, 82, 120, 80, "PROG -", ARevPrev);
    act(812, 82, 120, 80, "PROG +", ARevNext);
    const int r1[6] = {pRevTime, pRevHigh, pRevLow, pRevInit, pRevFirst, pRevDiff};
    const int r2[6] = {pRevLive, pRevSize, pRevEqLo, pRevEqMid, pRevEqHi, pRetA};
    for (int i = 0; i < 6; ++i) {
      ctls.push_back({KKnob, r1[i], 40 + i * 155.f, 200, 140, 150, ""});
      ctls.push_back({KKnob, r2[i], 40 + i * 155.f, 380, 140, 150, ""});
    }
    const int d[4] = {pDlyDiv, pDlyFb, pDlyDamp, pRetB};
    for (int i = 0; i < 4; ++i) ctls.push_back({KKnob, d[i], 40 + i * 155.f, 712, 140, 150, ""});
    ctls.push_back({KToggle, pDlyPing, 680, 740, 200, 70, "PING-PONG"});
    for (int v = 0; v < NumVoices; ++v) {
      ctls.push_back({KKnob, chParam(v, ChSendA), 40 + v * 155.f, 1000, 70, 90, ""});
      ctls.push_back({KKnob, chParam(v, ChSendB), 40 + v * 155.f + 70, 1000, 70, 90, ""});
    }
  }

  void layoutFile() {
    act(40, 96, 220, 64, "PATTERN", AScope, 0);
    act(270, 96, 220, 64, "SONG", AScope, 1);
    act(560, 96, 120, 64, "1X", ALoops, 1);
    act(690, 96, 120, 64, "2X", ALoops, 2);
    act(820, 96, 120, 64, "4X", ALoops, 4);
    act(560, 196, 185, 64, "16-BIT", ABits, 16);
    act(755, 196, 185, 64, "24-BIT", ABits, 24);
    act(40, 300, 290, 100, "EXPORT WAV", AFile, OpExportWav);
    act(355, 300, 290, 100, "EXPORT MP3", AFile, OpExportMp3);
    act(670, 300, 290, 100, "EXPORT MIDI", AFile, OpExportMidi);
    act(40, 540, 440, 100, "IMPORT MIDI", AFile, OpImportMidi);
    act(520, 540, 440, 100, "IMPORT WAV / MP3", AFile, OpImportAudio);
    act(40, 820, 200, 70, "PLAY", ASmpPlay);
    act(260, 820, 200, 70, "CLEAR", ASmpClear);
    ctls.push_back({KWheel, pSmpTune, 520, 836, 300, 50, "TUNE"});
  }

  int hit(float lx, float ly) const {
    // pads are round: hit the housing square (generous, like the real rubber pads)
    for (int i = int(ctls.size()) - 1; i >= 0; --i) {
      const Ctl& c = ctls[size_t(i)];
      float m = c.kind == KButton ? 10.f : 0.f;
      if (lx >= c.x - m && lx < c.x + c.w + m && ly >= c.y - m && ly < c.y + c.h + m) return i;
    }
    return -1;
  }

  uint64_t stateHash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(uint64_t(tab));
    for (int i = 0; i < NumVoices; ++i) mix(uint64_t(std::min(1.f, eng.display.led[i].load()) * 12));
    for (int i = 0; i < NumParams; ++i) mix(uint64_t(host.getParam(i) * 1000));
    mix(uint64_t(eng.display.seq)); mix(uint64_t(eng.display.memory)); mix(eng.display.bassOn); mix(eng.display.overdub);
    mix(uint64_t(eng.display.bpm.load() * 2));
    for (int i = 0; i < 4; ++i) mix(uint64_t(eng.display.rate[i] + 1));
    for (bool b : padDown) mix(b);
    for (bool b : btnDown) mix(b);
    int sq = eng.display.seq;
    if (sq == SeqRecArm || sq == SeqPlayArm || sq == SeqRecord) mix(uint64_t(blinkPhase()));
    if (tab == TabGrid) {
      mix(uint64_t(editPat)); mix(uint64_t(page)); mix(uint64_t(eng.display.step)); mix(eng.display.song); mix(uint64_t(eng.display.songPos));
      mix(uint64_t(eng.display.cue + 1)); mix(eng.songMode());
      for (int p = 0; p < Engine::kNumPat; ++p) mix(uint64_t(eng.patternLength(p)));
      for (int s = 0; s < 16; ++s) mix(eng.step(editPat, page * 16 + s));
      mix(uint64_t(eng.songLength()));
      for (int i = 0; i < eng.songLength(); ++i) mix(uint64_t(eng.songEntry(i)));
    }
    if (tab == TabMixer) for (int i = 0; i < NumVoices + 4; ++i) mix(uint64_t(std::sqrt(std::min(2.f, eng.display.meter[i].load())) * 60));
    if (tab == TabFile) {
      mix(exportSong); mix(uint64_t(exportLoops)); mix(uint64_t(wavBits)); mix(uint64_t(editPat));
      std::lock_guard<std::mutex> lk(statusMutex);
      mix(uint64_t(statusVersion));
      mix(uint64_t(reinterpret_cast<uintptr_t>(eng.sample().get())));
    }
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

  // a flat button for the editor pages; lit = red, text size from the height
  void drawButton(Canvas& c, float x, float y, float w, float h, const char* t, bool lit, float ts = 0, const Col* litCol = nullptr) {
    Col lc = litCol ? *litCol : pal::ledOn;
    c.vGradient(x, y, w, h, 7, lit ? pal::btnHi : pal::btn, lit ? pal::btn : Col::hex(0x1f1f22));
    c.strokeRoundRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 7, lit ? 2.5f : 1.2f, lit ? lc : Col::hex(0x3a3a40));
    if (ts <= 0) ts = std::min(18.f, h * 0.3f);
    size_t n = std::strlen(t);
    if (n > 0) ts = std::min(ts, (w - 12) * 7.f / (float(n) * 6.f - 1.f));  // shrink to fit
    c.text(x + w / 2, y + h / 2 - ts / 2, ts, t, lit ? lc : pal::text, Canvas::Center, 1.f, true);
  }

  void drawStatic(Canvas& c) {
    c.fillAllPixels(pal::bg);
    // tab bar
    c.vGradient(0, -kTabH, W, kTabH, 0, Col::hex(0x202024), Col::hex(0x141416));
    c.rect(0, -2, W, 2, Col::hex(0x2a2a2e));
    switch (tab) {
      case TabDrums: drawDrumsStatic(c); break;
      case TabGrid: drawGridStatic(c); break;
      case TabMixer: drawMixerStatic(c); break;
      case TabFx: drawFxStatic(c); break;
      case TabFile: drawFileStatic(c); break;
    }
  }

  void drawDynamic(Canvas& c) {
    for (auto& k : ctls) {
      if (k.kind != KTab) continue;
      bool on = k.id == tab;
      if (on) {
        c.rect(k.x + 4, k.y + 6, k.w - 8, k.h - 10, Col::hex(0x2c2c31));
        c.rect(k.x + 14, k.y + k.h - 8, k.w - 28, 4, pal::ledOn);
      }
      c.text(k.x + k.w / 2, k.y + k.h / 2 - 9, 18, k.label, on ? pal::text : pal::dim, Canvas::Center, 1.2f, true);
    }
    // a dot on GRID while the sequencer runs
    if (eng.display.seq == SeqPlay || eng.display.seq == SeqRecord) c.circle(W / NumTabs * 1.5f + 60, -kTabH / 2, 6, eng.display.seq == SeqRecord || eng.display.overdub ? pal::ledOn : pal::green);
    switch (tab) {
      case TabDrums: drawDrumsDynamic(c); break;
      case TabGrid: drawGridDynamic(c); break;
      case TabMixer: drawMixerDynamic(c); break;
      case TabFx: drawFxDynamic(c); break;
      case TabFile: drawFileDynamic(c); break;
    }
  }

  // ------------------------------------------------------------------------------------------------ DRUMS
  void drawDrumsStatic(Canvas& c) {
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

  void drawLed(Canvas& c, float x, float y, float level, Col on = pal::ledOn, Col off = pal::ledOff) {
    c.circle(x, y, 9.5f, off.mix(on, level));
    if (level > 0.05f) {
      c.circle(x, y, 22, on.withA(0.22f * level));
      c.circle(x - 2.5f, y - 3, 3.2f, Col(1, 0.85f, 0.75f, 0.6f * level));
    } else {
      c.circle(x - 2.5f, y - 3, 2.5f, Col(1, 0.5f, 0.4f, 0.18f));
    }
  }

  void drawWheel(Canvas& c, float x, float y, float w, float h, float v) {
    c.roundRect(x - 4, y - 4, w + 8, h + 8, 8, Col::hex(0x050505));
    c.vGradient(x, y, w, h, 6, Col::hex(0x2a2a2d), Col::hex(0x0f0f10));
    // ribs scroll with the value; shading makes it read as a cylinder
    float off = v * 260.f;
    for (int i = -2; i < 60; ++i) {
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

  void drawKnob(Canvas& c, float cx, float cy, float r, float v, bool ticks = true) {
    if (ticks) {  // printed scale
      for (int i = 0; i <= 10; ++i) {
        float a = float(M_PI) * (0.75f + 1.5f * i / 10.f);
        float r0 = r + 7, r1 = r + (i % 5 == 0 ? 17 : 12);
        c.line(cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a), cy + r1 * std::sin(a), 2.2f, pal::print);
      }
    } else {  // a value arc
      for (int i = 0; i <= 20; ++i) {
        float t = i / 20.f, a = float(M_PI) * (0.75f + 1.5f * t);
        c.circle(cx + (r + 9) * std::cos(a), cy + (r + 9) * std::sin(a), 2.4f, t <= v + 0.001f ? pal::ledOn : Col::hex(0x3a3a3e));
      }
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
    c.line(cx + r * 0.2f * std::cos(a), cy + r * 0.2f * std::sin(a), cx + r * 0.9f * std::cos(a), cy + r * 0.9f * std::sin(a), std::max(2.5f, r * 0.11f), Col::hex(0xf2f2f2));
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

  void statusLine(char* st, size_t n) const {
    int sq = eng.display.seq, m = eng.display.memory + 1;
    switch (sq) {
      case SeqRecArm: std::snprintf(st, n, "RECORD: PICK MEMORY"); break;
      case SeqPlayArm: std::snprintf(st, n, "PLAYBACK: PICK MEMORY"); break;
      case SeqRecord: std::snprintf(st, n, "REC MEMORY %d", m); break;
      case SeqPlay:
        if (eng.display.song) std::snprintf(st, n, "SONG %d/%d: PATTERN %d", eng.display.songPos + 1, eng.songLength(), m);
        else std::snprintf(st, n, eng.display.overdub ? "OVERDUB PATTERN %d" : "PLAY PATTERN %d", m);
        break;
      default: std::snprintf(st, n, "READY"); break;
    }
  }

  void drawDrumsDynamic(Canvas& c) {
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
    int sq = eng.display.seq;
    char st[64];
    statusLine(st, sizeof st);
    c.text(442, 34, 15, st, sq == SeqRecord || eng.display.overdub ? pal::ledOn : pal::text, Canvas::Center, 1.f, true);
    char bp[32];
    std::snprintf(bp, sizeof bp, "%d BPM", int(eng.display.bpm.load() + 0.5f));
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

  // ------------------------------------------------------------------------------------------------ GRID
  void drawGridStatic(Canvas& c) {
    c.text(642, 14, 12, "TEMPO", pal::dim, Canvas::Center);
    c.text(16, 244, 15, "LENGTH", pal::dim, Canvas::Left);
    // grid frame: beat shading every 4 steps
    for (int col = 0; col < 16; ++col)
      if ((col / 4) % 2 == 0) c.rect(kCellX + col * kCellW - 2, kGridY, kCellW, 7 * kRowH, Col::hex(0x141417));
    c.text(16, 986, 16, "SONG", pal::print, Canvas::Left, 1.2f, true);
    c.text(110, 988, 12, "TAP A SLOT TO PUT THE SELECTED PATTERN THERE; SONG ON PLAYS THE CHAIN", pal::dim, Canvas::Left);
    c.text(400, 1196, 12, "PATTERNS 1-3 ARE THE FRONT PANEL'S MEMORIES", pal::dim, Canvas::Left);
  }

  void drawGridDynamic(Canvas& c) {
    int sq = eng.display.seq;
    bool playing = sq == SeqPlay;
    int playPat = eng.display.memory;
    for (auto& k : ctls) {
      if (k.kind != KAction) continue;
      switch (k.id) {
        case APlay: drawButton(c, k.x, k.y, k.w, k.h, "PLAY", playing, 18, &pal::green); break;
        case AStop: drawButton(c, k.x, k.y, k.w, k.h, "STOP", false, 18); break;
        case ARec: drawButton(c, k.x, k.y, k.w, k.h, "REC", eng.display.overdub, 18); break;
        case ASongMode: drawButton(c, k.x, k.y, k.w, k.h, "SONG", eng.songMode(), 18, &pal::amber); break;
        case APattern: {
          int p = k.a;
          bool sel = p == editPat, isPlay = playing && p == playPat, cued = eng.display.cue == p;
          char t[16];
          std::snprintf(t, sizeof t, "%d", p + 1);
          drawButton(c, k.x, k.y, k.w, k.h, t, sel, 18);
          if (isPlay) c.roundRect(k.x + 6, k.y + 6, 10, k.h - 12, 3, pal::green);
          if (cued) c.roundRect(k.x + 6, k.y + 6, 10, k.h - 12, 3, pal::amber);
          if (eng.patternLength(p) > 0) c.circle(k.x + k.w - 14, k.y + k.h / 2, 5, pal::dim);
          if (p < 3) c.text(k.x + k.w - 26, k.y + 6, 10, p == 0 ? "M1" : p == 1 ? "M2" : "M3", pal::dim, Canvas::Right);
          break;
        }
        default: drawButton(c, k.x, k.y, k.w, k.h, k.label, false, std::min(15.f, k.h * 0.3f)); break;
      }
    }
    char b[48];
    std::snprintf(b, sizeof b, "%d BPM", int(std::round(eng.tempo())));
    c.text(634, 42, 20, b, pal::text, Canvas::Center, 1.f, true);
    int sw = int(std::round(host.getParam(pSwing) * 40));
    std::snprintf(b, sizeof b, "SWING %d%%", sw);
    c.text(794, 14, 12, b, pal::dim, Canvas::Left);
    drawWheel(c, 794, 38, 186, 40, host.getParam(pSwing));
    int len = eng.patternLength(editPat);
    std::snprintf(b, sizeof b, "%d STEPS", len);
    c.text(305, 236, 15, b, pal::text, Canvas::Center, 1.f, true);
    std::snprintf(b, sizeof b, "%d.%d BARS", len / 16, (len % 16) / 4);
    c.text(305, 260, 11, b, pal::dim, Canvas::Center);
    std::snprintf(b, sizeof b, "PAGE %d/%d", page + 1, pages());
    c.text(659, 245, 12, b, pal::text, Canvas::Center, 1.f, true);
    // rows
    int curStep = eng.display.step;
    bool showHead = playing && playPat == editPat;
    for (int r = 0; r < 7; ++r) {
      const Row& row = rows()[r];
      float y = kGridY + r * kRowH;
      bool flash = row.voice >= 0 && eng.display.led[row.voice] > 0.3f;
      c.vGradient(16, y + 4, 128, kRowH - 8, 7, flash ? Col::hex(0x4a1d18) : pal::btn, Col::hex(0x1f1f22));
      c.text(80, y + kRowH / 2 - 8, 15, row.name, row.voice < 0 ? pal::amber : pal::text, Canvas::Center, 1.f, true);
      for (int col = 0; col < 16; ++col) {
        int s = page * 16 + col;
        float x = kCellX + col * kCellW;
        bool inside = s < len;
        bool on = inside && ((eng.step(editPat, s) >> row.bit) & 1);
        Col base = !inside ? Col::hex(0x111113) : (on ? (row.voice < 0 ? pal::amber : pal::cellOn) : pal::cellOff);
        c.roundRect(x, y + 4, kCellW - 4, kRowH - 8, 6, base);
        if (on) c.roundRect(x + 4, y + 8, kCellW - 12, 10, 4, Col(1, 1, 1, 0.18f));
        if (showHead && s == curStep) c.strokeRoundRect(x - 1, y + 3, kCellW - 2, kRowH - 6, 6, 3, pal::green);
      }
    }
    // step numbers
    for (int col = 0; col < 16; col += 4) {
      std::snprintf(b, sizeof b, "%d", page * 16 + col + 1);
      c.text(kCellX + col * kCellW + 4, kGridY - 18, 12, b, pal::dim, Canvas::Left);
    }
    // song slots
    int n = eng.songLength();
    for (auto& k : ctls) {
      if (k.kind != KSong) continue;
      bool used = k.a < n, next = k.a == n, now = eng.display.song && playing && eng.display.songPos == k.a;
      c.roundRect(k.x, k.y, k.w, k.h, 6, used ? pal::btnHi : (next ? Col::hex(0x232327) : Col::hex(0x151517)));
      if (now) c.strokeRoundRect(k.x, k.y, k.w, k.h, 6, 3, pal::green);
      else if (next) c.strokeRoundRect(k.x + 1, k.y + 1, k.w - 2, k.h - 2, 6, 1.5f, pal::dim);
      if (used) { std::snprintf(b, sizeof b, "%d", eng.songEntry(k.a) + 1); c.text(k.x + k.w / 2, k.y + k.h / 2 - 8, 16, b, pal::text, Canvas::Center, 1.f, true); }
      else if (next) c.text(k.x + k.w / 2, k.y + k.h / 2 - 8, 16, "+", pal::dim, Canvas::Center);
    }
  }

  // ------------------------------------------------------------------------------------------------ MIXER
  void drawMixerStatic(Canvas& c) {
    for (int i = 0; i < 9; ++i) {
      float x = stripX(i);
      c.vGradient(x, 8, 104, 1350, 10, Col::hex(0x1c1c1f), Col::hex(0x141416));
      c.strokeRoundRect(x, 8, 104, 1350, 10, 1.2f, Col::hex(0x2c2c31));
      const char* nm = i < NumVoices ? voiceName(i) : i == 6 ? "REV7" : i == 7 ? "DELAY" : "MASTER";
      c.text(x + 52, 28, 14, nm, i == 8 ? pal::text : pal::print, Canvas::Center, 1.f, true);
      if (i < NumVoices) {
        c.text(x + 52, 146, 11, "REV7", pal::dim, Canvas::Center);
        c.text(x + 52, 254, 11, "DELAY", pal::dim, Canvas::Center);
      } else if (i < 8) {
        c.text(x + 52, 200, 12, "RETURN", pal::dim, Canvas::Center);
      }
      // fader slot and scale
      c.roundRect(x + 36, 470, 8, 740, 4, Col::hex(0x060607));
      for (int db : {6, 0, -6, -12, -24, -48}) {
        float v = 0.8f * std::pow(10.f, float(db) / 60.f);
        float y = 450 + 20 + (1 - v) * 740;
        char t[16]; std::snprintf(t, sizeof t, "%d", db);
        c.line(x + 50, y, x + 58, y, 1.5f, pal::dim);
        if (i == 0) c.text(x + 30, y - 5, 10, t, pal::dim, Canvas::Right);
      }
    }
  }
  void drawFader(Canvas& c, float x, float y, float h, float v) {
    float cy = y + 20 + (1 - v) * (h - 40);
    c.roundRect(x + 2, cy - 17, 56, 34, 5, Col(0, 0, 0, 0.5f));
    c.vGradient(x, cy - 18, 56, 34, 5, Col::hex(0xb8b9bd), Col::hex(0x6a6b70));
    c.rect(x + 4, cy - 1.5f, 48, 3, Col::hex(0x111111));
  }
  void drawMeter(Canvas& c, float x, float y, float h, float level) {
    c.roundRect(x, y, 12, h, 3, Col::hex(0x0a0a0b));
    float db = level <= 1e-5f ? -100.f : 20.f * std::log10(level);
    int segs = 30;
    for (int i = 0; i < segs; ++i) {
      float segDb = -48.f + 54.f * (i + 1) / segs;  // -48 .. +6 dBFS
      bool on = db >= segDb - 54.f / segs;
      Col col = segDb > 0 ? pal::ledOn : (segDb > -9 ? pal::amber : pal::green);
      float sy = y + h - (i + 1) * (h / segs);
      c.rect(x + 2, sy + 1, 8, h / segs - 2, on ? col : col.scaled(0.18f));
    }
  }
  void drawMixerDynamic(Canvas& c) {
    char b[24];
    for (auto& k : ctls) {
      if (k.kind == KKnob) {
        bool pan = (k.id - pChan) % NumChParams == ChPan;
        drawKnob(c, k.x + k.w / 2, k.y + k.h / 2, 26, host.getParam(k.id), false);
        if (pan) {
          float p = host.getParam(k.id) * 2 - 1;
          if (std::fabs(p) < 0.02f) std::snprintf(b, sizeof b, "PAN C");
          else std::snprintf(b, sizeof b, "PAN %s%d", p < 0 ? "L" : "R", int(std::round(std::fabs(p) * 100)));
          c.text(k.x + k.w / 2, k.y + k.h + 2, 11, b, pal::text, Canvas::Center);
        }
      }
      if (k.kind == KToggle) {
        bool on = host.getParam(k.id) > 0.5f;
        bool solo = (k.id - pChan) % NumChParams == ChSolo;
        drawButton(c, k.x, k.y, k.w, k.h, k.label, on, 16, solo ? &pal::amber : &pal::ledOn);
      }
      if (k.kind == KFader) {
        drawFader(c, k.x, k.y, k.h, host.getParam(k.id));
        float db = faderDb(host.getParam(k.id));
        if (db < -90) std::snprintf(b, sizeof b, "-INF");
        else std::snprintf(b, sizeof b, "%+.1f", double(db));
        c.text(k.x + 30, 1250, 13, b, pal::text, Canvas::Center, 1.f, true);
      }
      if (k.kind == KPad && k.id == Sample) {
        auto smp = eng.sample();
        c.text(k.x + 6, 52, 10, smp ? "TAP NAME" : "EMPTY", pal::dim, Canvas::Left);
      }
    }
    for (int i = 0; i < 9; ++i) {
      float x = stripX(i);
      if (i < 8) drawMeter(c, x + 80, 470, 740, eng.display.meter[i]);
      else { drawMeter(c, x + 76, 470, 740, eng.display.meter[NumVoices + 2]); drawMeter(c, x + 90, 470, 740, eng.display.meter[NumVoices + 3]); }
      if (i < NumVoices) drawLed(c, x + 92, 56, std::min(1.f, eng.display.led[i].load()));
    }
  }

  // ------------------------------------------------------------------------------------------------ FX
  void rackUnit(Canvas& c, float y, float h, const char* name, const char* sub) {
    c.vGradient(16, y, 968, h, 10, Col::hex(0x2c2c30), pal::rack);
    c.strokeRoundRect(16, y, 968, h, 10, 1.5f, Col::hex(0x3c3c42));
    for (float sx : {34.f, 966.f})
      for (float sy : {y + 18, y + h - 18}) { c.roundRect(sx - 9, sy - 5, 18, 10, 5, Col::hex(0x0d0d0e)); }
    c.text(56, y + 18, 16, name, pal::text, Canvas::Left, 1.3f, true);
    c.text(56 + Canvas::textWidth(name, 16, 1.3f) + 20, y + 22, 11, sub, pal::dim, Canvas::Left);
  }
  void drawFxStatic(Canvas& c) {
    rackUnit(c, 12, 540, "REV7", "DIGITAL REVERBERATOR  (MODEL)");
    // display windows
    c.roundRect(52, 70, 140, 100, 8, Col::hex(0x0a0606));
    c.roundRect(210, 70, 448, 100, 8, Col::hex(0x0d0f08));
    const char* l1[6] = {"REV TIME", "HIGH", "LOW", "INIT DLY", "REV DLY", "DIFFUSION"};
    const char* l2[6] = {"LIVENESS", "ROOM SIZE", "EQ LOW", "EQ MID", "EQ HIGH", "RETURN"};
    for (int i = 0; i < 6; ++i) {
      c.text(110 + i * 155.f, 318, 12, l1[i], pal::text, Canvas::Center, 1.f, true);
      c.text(110 + i * 155.f, 498, 12, l2[i], i >= 2 && i <= 4 ? pal::amber : (i == 5 ? pal::print : pal::text), Canvas::Center, 1.f, true);
    }
    rackUnit(c, 572, 330, "DELAY", "TEMPO-SYNCED DIGITAL DELAY");
    const char* ld[4] = {"TIME", "FEEDBACK", "DAMP", "RETURN"};
    for (int i = 0; i < 4; ++i) c.text(110 + i * 155.f, 830, 12, ld[i], i == 3 ? pal::print : pal::text, Canvas::Center, 1.f, true);
    c.roundRect(680, 640, 260, 70, 8, Col::hex(0x0a0606));
    // send matrix
    c.vGradient(16, 922, 968, 230, 10, Col::hex(0x1c1c1f), Col::hex(0x141416));
    c.text(40, 938, 15, "EFFECT SENDS", pal::print, Canvas::Left, 1.2f, true);
    c.text(330, 941, 11, "LEFT KNOB REV7, RIGHT KNOB DELAY (POST FADER)", pal::dim, Canvas::Left);
    for (int v = 0; v < NumVoices; ++v) c.text(110 + v * 155.f, 1100, 12, voiceName(v), pal::text, Canvas::Center, 1.f, true);
  }
  // seven-segment style digits for the program number
  void segDigits(Canvas& c, float x, float y, float h, const char* s, Col col) {
    float w = h * 0.55f, t = h * 0.12f;
    static const uint8_t seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    for (const char* p = s; *p; ++p) {
      if (*p >= '0' && *p <= '9') {
        uint8_t m = seg[*p - '0'];
        float hh = h / 2;
        auto H = [&](float yy, bool on) { c.roundRect(x + t, yy - t / 2, w - 2 * t, t, t / 2, on ? col : col.withA(0.08f)); };
        auto V = [&](float xx, float yy, bool on) { c.roundRect(xx - t / 2, yy + t / 2, t, hh - t, t / 2, on ? col : col.withA(0.08f)); };
        H(y, m & 1); V(x + w, y, m & 2); V(x + w, y + hh, m & 4); H(y + h, m & 8); V(x, y + hh, m & 16); V(x, y, m & 32); H(y + hh, m & 64);
      }
      x += w + t * 2;
    }
  }
  std::string revValueText(int id) const {
    float v = host.getParam(id);
    char b[32];
    switch (id) {
      case pRevTime: std::snprintf(b, sizeof b, "%.1f S", double(Rev7::rtSeconds(v))); break;
      case pRevHigh: std::snprintf(b, sizeof b, "X%.2f", double(0.1f + 0.9f * v)); break;
      case pRevLow: std::snprintf(b, sizeof b, "X%.2f", double(0.4f + 2.0f * v)); break;
      case pRevInit: case pRevFirst: std::snprintf(b, sizeof b, "%.1f MS", double(Rev7::msInit(v))); break;
      case pRevDiff: std::snprintf(b, sizeof b, "%d", Rev7::diffSteps(v)); break;
      case pRevLive: case pRevSize: std::snprintf(b, sizeof b, "%d", int(std::round(v * 10))); break;
      case pRevEqLo: case pRevEqMid: case pRevEqHi: std::snprintf(b, sizeof b, "%+.1f DB", double((v - 0.5f) * 24)); break;
      case pRetA: case pRetB: { float db = faderDb(v); if (db < -90) std::snprintf(b, sizeof b, "-INF"); else std::snprintf(b, sizeof b, "%+.1f DB", double(db)); break; }
      case pDlyDiv: std::snprintf(b, sizeof b, "%s", delayDivName(delayDiv(v))); break;
      case pDlyFb: std::snprintf(b, sizeof b, "%d%%", int(std::round(std::min(0.95f, v) * 100))); break;
      case pDlyDamp: std::snprintf(b, sizeof b, "%d", int(std::round(v * 10))); break;
      default: std::snprintf(b, sizeof b, "%d", int(std::round(v * 100)));
    }
    return b;
  }
  void drawFxDynamic(Canvas& c) {
    int prog = revProgramIndex(host.getParam(pRevProg));
    const RevProgram& rp = revPrograms()[prog];
    char b[48];
    std::snprintf(b, sizeof b, "%02d", prog + 1);
    segDigits(c, 70, 88, 64, b, pal::ledOn);
    // LCD: program name, and the algorithm with the reverb time
    c.roundRect(218, 78, 432, 84, 5, pal::lcd);
    c.text(232, 92, 20, rp.name, pal::lcdInk, Canvas::Left, 1.f, true);
    static const char* algoNames[NumAlgos] = {"HALL", "ROOM", "VOCAL", "PLATE", "EARLY REF", "GATE", "REVERSE", "DELAY", "ECHO", "FLANGE", "PHASE"};
    std::snprintf(b, sizeof b, "%s  RT %s", algoNames[rp.algo], revValueText(pRevTime).c_str());
    c.text(232, 128, 14, b, pal::lcdInk, Canvas::Left);
    for (auto& k : ctls) {
      if (k.kind == KAction) drawButton(c, k.x, k.y, k.w, k.h, k.label, false, 16);
      if (k.kind == KKnob) {
        bool small = k.y >= 1000;
        float cx = k.x + k.w / 2, cy = k.y + (small ? 42 : 60);
        drawKnob(c, cx, cy, small ? 22 : 36, host.getParam(k.id), small ? false : true);
        if (!small) c.text(cx, k.y + 140, 12, revValueText(k.id).c_str(), pal::dim, Canvas::Center);
      }
      if (k.kind == KToggle) drawButton(c, k.x, k.y, k.w, k.h, "PING-PONG", host.getParam(k.id) > 0.5f, 16);
    }
    // delay display: the note value and the time it makes at this tempo
    int div = delayDiv(host.getParam(pDlyDiv));
    double ms = delayDivBeats(div) * 60000.0 / std::max(30.f, eng.tempo());
    std::snprintf(b, sizeof b, "%s", delayDivName(div));
    c.text(700, 652, 24, b, pal::ledOn, Canvas::Left, 1.f, true);
    std::snprintf(b, sizeof b, "%.0f MS", ms);
    c.text(925, 660, 15, b, pal::ledOn.withA(0.8f), Canvas::Right, 1.f, true);
  }

  // ------------------------------------------------------------------------------------------------ FILE
  void drawFileStatic(Canvas& c) {
    c.text(40, 30, 20, "EXPORT", pal::print, Canvas::Left, 1.3f, true);
    c.text(40, 72, 12, "WHAT", pal::dim, Canvas::Left);
    c.text(560, 72, 12, "HOW MANY TIMES", pal::dim, Canvas::Left);
    c.text(560, 176, 12, "WAV BIT DEPTH", pal::dim, Canvas::Left);
    c.text(40, 420, 12, "WAV AND MP3 ARE RENDERED THROUGH THE WHOLE CIRCUIT, MIXER AND EFFECTS, WITH THE TAIL.", pal::dim, Canvas::Left);
    c.text(40, 442, 12, "MIDI USES GENERAL MIDI DRUM NOTES ON CHANNEL 10 (SAMPLE PAD = C4).", pal::dim, Canvas::Left);
    c.rect(40, 486, 920, 2, Col::hex(0x2a2a2e));
    c.text(40, 506, 20, "IMPORT", pal::print, Canvas::Left, 1.3f, true);
    c.text(40, 660, 12, "MIDI: DRUM NOTES GO INTO THE SELECTED PATTERN, QUANTIZED TO 16THS. LONGER THAN 21 BARS:", pal::dim, Canvas::Left);
    c.text(40, 680, 12, "4-BAR PATTERNS FROM THE SELECTED ONE ON, CHAINED AS THE SONG. THE FILE'S TEMPO IS USED.", pal::dim, Canvas::Left);
    c.text(40, 702, 12, "WAV / MP3: LOADED ONTO THE SAMPLE PAD (GRID ROW SAMPLE, MIXER CHANNEL SAMPLE, MIDI C4).", pal::dim, Canvas::Left);
    c.rect(40, 740, 920, 2, Col::hex(0x2a2a2e));
    c.text(40, 760, 20, "SAMPLE PAD", pal::print, Canvas::Left, 1.3f, true);
    c.text(520, 812, 12, "TUNE", pal::dim, Canvas::Left);
    c.rect(40, 1000, 920, 2, Col::hex(0x2a2a2e));
    c.text(40, 1020, 20, "STATUS", pal::print, Canvas::Left, 1.3f, true);
  }
  void drawFileDynamic(Canvas& c) {
    char b[64];
    for (auto& k : ctls) {
      if (k.kind != KAction) continue;
      bool lit = false;
      const char* t = k.label;
      if (k.id == AScope) {
        lit = exportSong == (k.a != 0);
        if (k.a == 0) { std::snprintf(b, sizeof b, "PATTERN %d", editPat + 1); t = b; }
      }
      if (k.id == ALoops) lit = exportLoops == k.a;
      if (k.id == ABits) lit = wavBits == k.a;
      bool big = k.id == AFile;
      drawButton(c, k.x, k.y, k.w, k.h, t, lit, big ? 20 : 16, big ? nullptr : &pal::ledOn);
      if (k.id == AFile && k.a == OpImportMidi) {
        std::snprintf(b, sizeof b, "INTO PATTERN %d", editPat + 1);
        c.text(k.x + k.w / 2, k.y + k.h - 22, 11, b, pal::dim, Canvas::Center);
      }
    }
    auto smp = eng.sample();
    if (smp) {
      std::snprintf(b, sizeof b, "%.2f S  %.1f KHZ  %s", double(smp->frames()) / smp->rate, smp->rate / 1000.0, smp->r.empty() ? "MONO" : "STEREO");
      c.text(260, 766, 14, smp->name.substr(0, 40).c_str(), pal::text, Canvas::Left, 1.f, true);
      c.text(40, 924, 13, b, pal::dim, Canvas::Left);
    } else {
      c.text(260, 766, 14, "EMPTY: IMPORT A WAV OR MP3", pal::dim, Canvas::Left, 1.f, true);
    }
    float semis = (host.getParam(pSmpTune) - 0.5f) * 24;
    std::snprintf(b, sizeof b, "%+.1f SEMITONES", double(semis));
    c.text(830, 852, 13, b, pal::text, Canvas::Left);
    drawWheel(c, 520, 836, 300, 50, host.getParam(pSmpTune));
    // status, wrapped
    std::string s;
    { std::lock_guard<std::mutex> lk(statusMutex); s = status; }
    float y = 1066;
    while (!s.empty() && y < 1340) {
      size_t n = s.size() <= 62 ? s.size() : s.rfind(' ', 62);
      if (n == std::string::npos || n == 0) n = std::min<size_t>(62, s.size());
      std::string line = s.substr(0, n);
      for (auto& ch : line) if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
      c.text(40, y, 16, line.c_str(), pal::text, Canvas::Left, 1.f, true);
      s = n < s.size() ? s.substr(n + (s[n] == ' ' ? 1 : 0)) : "";
      y += 34;
    }
  }
};

}  // namespace syn
