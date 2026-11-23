// Omega 808 sequencer: a line-for-line C++ port of eight/seq.js (12 basic rhythms and 4 intro/fill rhythms, each with
// variation A and B; basic variation A / AB / B; I/F variation; TAP fill-in; auto fill-in; 1st/2nd part; pre-scale;
// swing; sample-accurate), plus Machine, which renders like eight/worklet.js does (splits blocks at the steps).
#pragma once
#include "Engine.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eight {

struct Pattern { int scale = 0, len = 16, parts = 1; std::map<std::string, std::string> steps; };
struct Rhythm { Pattern A, B; };
struct Bank { Rhythm basic[12]; Rhythm fill[4]; double tempo = 120, swing = 0; int fillEvery = 0; };
struct StepInfo { int step, part; bool trig, acc; std::vector<std::string> hits; };

static const char* const SEQ_TRACKS[13] = {"AC","BD","SD","LT","MT","HT","RS","CP","CB","CY","OH","CH","TRIG"};
static const int SEQ_NT = 13, SEQ_NSLOT = 32, SEQ_MAXS = 32;
static const int SEQ_SPB[4] = {4, 8, 6, 3};

inline Bank defaultBank() {
  auto P = [](std::map<std::string, std::string> s) { Pattern p; p.steps = std::move(s); return p; };
  Bank b; b.tempo = 118; b.swing = 0; b.fillEvery = 0;
  b.basic[0] = {P({{"AC","x...x...x...x..."},{"BD","x......x..x....."},{"SD","....x.......x..."},{"CH","x.x.x.x.x.x.x.x."}}),
                P({{"AC","x...x...x...x..."},{"BD","x......x..x...x."},{"SD","....x.......x..."},{"CH","x.x.x.x.x.x.x..."},{"OH","..............x."},{"CP","....x.......x..."}})};
  b.basic[1] = {P({{"BD","x..x..x...x..x.."},{"SD","....x.......x..."},{"CH","xxxxxxxxxxxxxxxx"},{"CB","..x.....x.....x."},{"AC","x.......x......."}}),
                P({{"BD","x..x..x...x..xx."},{"SD","....x.......x.x."},{"CH","xxxxxxxxxxxxxx.."},{"OH","..............x."},{"CB","..x.....x...x..."}})};
  b.basic[2] = {P({{"BD","x.......x......."},{"CP","....x.......x..."},{"CH","..x...x...x...x."},{"RS","...x..x....x..x."}}),
                P({{"BD","x.......x..x...."},{"CP","....x.......x..x"},{"CH","..x...x...x...x."},{"RS","...x..x....x...."},{"CY","x..............."}})};
  b.fill[0] = {P({{"AC","....x...x...xxxx"},{"BD","x.......x......."},{"SD","....x.x.x.xxxxxx"},{"HT","........x.x....."},{"MT","..........x.x..."},{"LT","............x.xx"}}),
               P({{"BD","x...x...x...x..."},{"CP","x.x.x.x.x.x.xxxx"},{"CY","x..............."}})};
  return b;
}

class Seq808 {
 public:
  explicit Seq808(double sr_) : sr(sr_) {
    si.hits.reserve(SEQ_NT);
    std::memset(grid, 0, sizeof grid);
    for (int s = 0; s < SEQ_NSLOT; s++) { scale[s] = 0; len[s] = 16; parts[s] = 1; }
    setBank(defaultBank());
  }
  void setBank(const Bank& b) {
    std::memset(grid, 0, sizeof grid);
    for (int i = 0; i < 12; i++) { setPattern(slotOf(false, i, 'A'), b.basic[i].A); setPattern(slotOf(false, i, 'B'), b.basic[i].B); }
    for (int i = 0; i < 4; i++) { setPattern(slotOf(true, i, 'A'), b.fill[i].A); setPattern(slotOf(true, i, 'B'), b.fill[i].B); }
    if (b.tempo) setTempo(b.tempo);
    swing = b.swing;
    fillEvery = b.fillEvery;
  }
  // the bank as it is now (edits included)
  const Bank& bank() const {
    auto pat = [this](int s) { Pattern p; p.scale = scale[s]; p.len = len[s]; p.parts = parts[s]; const int L = len[s] * parts[s];
      for (int t = 0; t < SEQ_NT; t++) { std::string str; for (int k = 0; k < L; k++) str += grid[(s * SEQ_NT + t) * SEQ_MAXS + k] ? 'x' : '.'; p.steps[SEQ_TRACKS[t]] = str; }
      return p; };
    for (int i = 0; i < 12; i++) { bankOut.basic[i].A = pat(slotOf(false, i, 'A')); bankOut.basic[i].B = pat(slotOf(false, i, 'B')); }
    for (int i = 0; i < 4; i++) { bankOut.fill[i].A = pat(slotOf(true, i, 'A')); bankOut.fill[i].B = pat(slotOf(true, i, 'B')); }
    bankOut.tempo = bpm; bankOut.swing = swing; bankOut.fillEvery = fillEvery;
    return bankOut;
  }
  // basic or fill < 0, or an empty variation, keeps the current setting (like the JS select({...}) with fields left out)
  void select(int b, int f, const std::string& variation_, const std::string& ifVar_) {
    if (b >= 0) basic = b; if (f >= 0) fill = f; if (!variation_.empty()) variation = variation_; if (!ifVar_.empty()) ifVar = ifVar_;
  }
  void setFillEvery(int n) { fillEvery = n; }
  void setSwing(double s) { swing = s; }
  void setTempo(double b) { bpm = std::max(20.0, std::min(400.0, b)); }
  double tempo() const { return bpm; }
  void edit(bool isFill, int n, char var, const std::string& track, int st, bool on) {
    int t = -1; for (int i = 0; i < SEQ_NT; i++) if (track == SEQ_TRACKS[i]) t = i;
    if (t < 0 || st < 0 || st >= SEQ_MAXS) return;
    grid[(slotOf(isFill, n, var) * SEQ_NT + t) * SEQ_MAXS + st] = on ? 1 : 0;
  }
  void tap() { if (playing_) pendingFill = true; else intro = true; }
  void play(bool on) {
    if (on && !playing_) {
      step = 0; phase = 0; fired = false; measure = 0; ab = 0;
      if (intro) { cur = slotOf(true, fill, ifVar[0]); inFill = true; intro = false; } else { cur = basicSlot(); inFill = false; }
    }
    playing_ = on;
  }
  bool playing() const { return playing_; }
  // host sync: jump to a musical position (quarter notes from the song start), in the rhythm now playing
  void setPositionBeats(double ppqPos) {
    if (ppqPos < 0) ppqPos = 0;
    const double steps = ppqPos * SEQ_SPB[scale[cur]];
    const int L = len[cur] * parts[cur];
    const double whole = std::floor(steps), frac = steps - whole;
    step = (int)std::fmod(whole, (double)L);
    phase = frac * stepLen();
    const double sw = (step & 1) ? swing * 0.5 * stepLen() : 0;
    fired = phase > sw;
  }
  int currentStep() const { return step; }

  // advance n samples; cb(offset, StepInfo) at each step. StepInfo.hits holds the TRACK names here (AC excluded,
  // flagged in .acc); Machine turns them into voices with Eight808::trackVoice. info() has the rhythm and variation
  template <class F> void tick(int n, F&& cb) {
    if (!playing_) return;
    double off = 0;
    for (int guard = 0; guard < 1000; guard++) {
      const double L = stepLen();
      const double sw = (step & 1) ? swing * 0.5 * L : 0;
      const double target = fired ? L : sw;
      const double need = target - phase;
      if (need > n - off) { phase += n - off; return; }
      if (need > 0) { off += need; phase = target; }
      if (!fired) { fired = true; fire(std::min(n - 1, (int)std::floor(off)), cb); }
      else {
        phase -= L; if (phase < 0) phase = 0; fired = false; step++;
        if (step >= len[cur] * parts[cur]) { step = 0; nextMeasure(); }
      }
    }
  }

  // what fire() reports: the tracks (indices into SEQ_TRACKS, accent excluded) on this step
  struct Info { int step = 0, part = 0, rhythm = 0; bool isFill = false; char var = 'A'; int acc = 0, n = 0; int tracks[SEQ_NT]; };
  const Info& info() const { return inf; }

 private:
  double sr;
  double bpm = 120, swing = 0; bool playing_ = false;
  uint8_t grid[SEQ_NSLOT * SEQ_NT * SEQ_MAXS];
  uint8_t scale[SEQ_NSLOT], len[SEQ_NSLOT], parts[SEQ_NSLOT];
  int basic = 0, fill = 0; std::string variation = "A", ifVar = "A"; int fillEvery = 0;
  int cur = 0, step = 0; double phase = 0; bool fired = false; int measure = 0, ab = 0;
  bool pendingFill = false, intro = false, inFill = false;
  Info inf;
  StepInfo si{};
  mutable Bank bankOut;

  static int slotOf(bool isFill, int n, char v) { return ((isFill ? 12 : 0) + n) * 2 + (v == 'B' ? 1 : 0); }
  void setPattern(int s, const Pattern& p) {
    scale[s] = (uint8_t)p.scale; len[s] = (uint8_t)std::max(1, std::min(16, p.len ? p.len : 16)); parts[s] = p.parts == 2 ? 2 : 1;
    for (int t = 0; t < SEQ_NT; t++) {
      auto it = p.steps.find(SEQ_TRACKS[t]); const std::string empty; const std::string& str = it == p.steps.end() ? empty : it->second;
      for (int k = 0; k < SEQ_MAXS; k++) grid[(s * SEQ_NT + t) * SEQ_MAXS + k] = (k < (int)str.size() && str[k] == 'x') ? 1 : 0;
    }
  }
  int basicSlot() { std::string v = variation; if (v == "AB") { v = ab ? "B" : "A"; ab ^= 1; } return slotOf(false, basic, v[0]); }
  double stepLen() const { return sr * 60 / (bpm * SEQ_SPB[scale[cur]]); }
  void nextMeasure() {
    measure++;
    const bool wasFill = inFill; inFill = false;
    if (pendingFill) { pendingFill = false; cur = slotOf(true, fill, ifVar[0]); inFill = true; }
    else if (!wasFill && fillEvery > 0 && (measure + 1) % fillEvery == 0) { cur = slotOf(true, fill, ifVar[0]); inFill = true; }
    else cur = basicSlot();
  }
  template <class F> void fire(int off, F&& cb) {
    const int s = cur; inf.step = step; inf.part = step >= len[s] ? 1 : 0;
    inf.isFill = s >= 24; inf.rhythm = (s >> 1) - (s >= 24 ? 12 : 0); inf.var = (s & 1) ? 'B' : 'A';
    int c = 0; inf.acc = 0;
    for (int t = 0; t < SEQ_NT; t++) if (grid[(s * SEQ_NT + t) * SEQ_MAXS + step]) { if (t == 0) inf.acc = 1; else inf.tracks[c++] = t; }
    inf.n = c;
    si.step = inf.step; si.part = inf.part; si.acc = inf.acc != 0; si.trig = false; si.hits.clear();
    for (int i = 0; i < c; i++) si.hits.push_back(SEQ_TRACKS[inf.tracks[i]]);
    cb(off, si);
  }
};

// one engine and one sequencer, rendered the way eight/worklet.js does it
struct Machine {
  Eight808 eng; Seq808 seq;
  explicit Machine(double sr) : eng(sr), seq(sr) { eng.set("tempo", seq.tempo()); }
  void setTempo(double bpm) { seq.setTempo(bpm); eng.set("tempo", seq.tempo()); }
  void setBank(const Bank& b) { seq.setBank(b); eng.set("tempo", seq.tempo()); }
  void set(const std::string& k, double v) {
    if (k == "tempo") { setTempo(v); return; }
    eng.set(k, v); if (k == "swing") seq.setSwing(v);
  }
  void process(float* L, float* R, int n, std::function<void(int, const StepInfo&)> onStep = {}) {
    int done = 0;
    seq.tick(n, [&](int off, const StepInfo& in) {
      if (off > done) { eng.processAt(L, R, off - done, done); done = off; }
      eng.trigFired = 0;
      StepInfo si; si.step = in.step; si.part = in.part; si.acc = in.acc;
      if (in.acc) eng.trigger("AC", 1, 1);
      for (const std::string& tr : in.hits) {
        const std::string v = tr == "TRIG" ? std::string("TRIG") : eng.trackVoice(tr);
        eng.trigger(v, in.acc ? 1 : 0, 1); si.hits.push_back(v);
      }
      si.trig = eng.trigFired != 0;
      if (onStep) onStep(off, si);
    });
    if (n > done) eng.processAt(L, R, n - done, done);
    // never let one bad sample silence the machine for good: a NaN in the effects' feedback loops would stay there
    if (n > 0 && (!std::isfinite(L[n - 1]) || !std::isfinite(R[n - 1]))) { eng.resetFx(); for (int i = 0; i < n; i++) { L[i] = 0; R[i] = 0; } }
  }
};

}  // namespace eight
