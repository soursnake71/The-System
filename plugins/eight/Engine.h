// Omega 808 engine: a line-for-line C++ port of eight/engine.js (TR-808 voices modelled on their circuits, the accent
// bus, the Egyptian Lover trigger-out click, sample slots, mixer and the Rev7 / TC2290 sends). Same arithmetic in double
// precision and float buffers where the JS uses Float32Arrays; eight/tools/compare.mjs checks they render the same.
#pragma once
#include "Fx.h"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eight {

struct ParamInfo { const char* key; const char* label; const char* group; double min, max, def; int step; std::vector<const char*> opts; };

static const char* const VOICE_NAMES[17] = {"BD","SD","LT","MT","HT","LC","MC","HC","RS","CL","CP","MA","CB","CY","OH","CH","TRIG"};
static const char* const SEND_VOICES[12] = {"BD","SD","LT","MT","HT","RS","CP","CB","CY","OH","CH","TRIG"};
static const char* const OWNER[17] = {"BD","SD","LT","MT","HT","LT","MT","HT","RS","RS","CP","CP","CB","CY","OH","CH","TRIG"};
static const double TOM_F[6][2] = {{75,110},{110,160},{160,230},{160,230},{230,320},{320,460}};
static const double TOM_TAU[6] = {0.35, 0.28, 0.22, 0.18, 0.15, 0.12};
static const double METAL_F[6] = {205.3, 304.4, 369.6, 522.7, 540, 800};

inline const std::vector<const char*>& voices() { static const std::vector<const char*> v(VOICE_NAMES, VOICE_NAMES + 17); return v; }
inline const std::vector<const char*>& revPrograms() {
  static const std::vector<const char*> v = [] { std::vector<const char*> r; for (int i = 0; i < NUM_REV_PROGRAMS; i++) r.push_back(REV_PROGRAMS[i].name); return r; }();
  return v;
}
inline const std::vector<const char*>& divisionNames() {
  static const std::vector<const char*> v = [] { std::vector<const char*> r; for (int i = 0; i < NUM_DIVISIONS; i++) r.push_back(DIVISIONS[i].name); return r; }();
  return v;
}

// the parameter table, in the same order as Eight808.PARAMS
inline const std::vector<ParamInfo>& params() {
  static std::deque<std::string> pool;   // keeps generated keys and labels alive
  static const std::vector<ParamInfo> P = [] {
    std::vector<ParamInfo> P;
    auto S = [](const std::string& s) { pool.push_back(s); return pool.back().c_str(); };
    auto add = [&](const char* k, const char* label, const char* group, double def, double mn = 0, double mx = 1, int step = 0, std::vector<const char*> opts = {}) {
      P.push_back({k, label, group, mn, mx, def, step, opts}); };
    add("master","Master Volume","Faceplate",0.75); add("AC.level","Accent","Faceplate",0.7);
    add("BD.level","BD Level","Faceplate",0.75); add("BD.tone","BD Tone","Faceplate",0.45); add("BD.decay","BD Decay","Faceplate",0.55);
    add("SD.level","SD Level","Faceplate",0.7); add("SD.tone","SD Tone","Faceplate",0.5); add("SD.snappy","SD Snappy","Faceplate",0.55);
    for (std::string t : {"LT","MT","HT"}) {
      add(S(t + ".level"), S(t + " Level"), "Faceplate", 0.7); add(S(t + ".tuning"), S(t + " Tuning"), "Faceplate", 0.5);
      add(S(t + ".sel"), S(t + " Tom/Conga"), "Faceplate", 0, 0, 1, 1, {"Tom","Conga"});
    }
    add("RS.level","RS Level","Faceplate",0.65); add("RS.sel","Rim Shot/Claves","Faceplate",0,0,1,1,{"Rim Shot","Claves"});
    add("CP.level","CP Level","Faceplate",0.7); add("CP.sel","Hand Clap/Maracas","Faceplate",0,0,1,1,{"Hand Clap","Maracas"});
    add("CB.level","CB Level","Faceplate",0.6); add("CY.level","CY Level","Faceplate",0.65); add("CY.tone","CY Tone","Faceplate",0.55); add("CY.decay","CY Decay","Faceplate",0.6);
    add("OH.level","OH Level","Faceplate",0.65); add("OH.decay","OH Decay","Faceplate",0.45); add("CH.level","CH Level","Faceplate",0.7);
    add("mod.BD.tune","Tune","Bass Drum",0.5); add("mod.BD.sweep","Pitch Sweep","Bass Drum",0.5); add("mod.BD.longDecay","Long Decay Mod","Bass Drum",0);
    add("mod.BD.click","Click","Bass Drum",0.3); add("mod.BD.drive","Drive","Bass Drum",0);
    add("mod.SD.tune","Tune","Snare",0.5); add("mod.SD.decay","Decay","Snare",0.5); add("mod.SD.noiseColor","Noise Color","Snare",0.5); add("mod.SD.drive","Drive","Snare",0);
    add("mod.TOM.decay","Tom Decay","Toms/Congas",0.5); add("mod.TOM.sweep","Tom Sweep","Toms/Congas",0.5); add("mod.CONGA.decay","Conga Decay","Toms/Congas",0.5); add("mod.TOM.drive","Drive","Toms/Congas",0);
    add("mod.RS.tune","Rim Tune","Rim/Clap",0.5); add("mod.CL.tune","Claves Tune","Rim/Clap",0.5); add("mod.CP.tail","Clap Tail","Rim/Clap",0.5);
    add("mod.CP.spread","Clap Spread","Rim/Clap",0.5); add("mod.MA.decay","Maracas Decay","Rim/Clap",0.5); add("mod.CP.drive","Clap Drive","Rim/Clap",0);
    add("mod.CB.tune","Cowbell Tune","Cowbell/Metal",0.5); add("mod.CB.decay","Cowbell Decay","Cowbell/Metal",0.5); add("mod.HH.tune","Metal Osc Tune","Cowbell/Metal",0.5);
    add("mod.CH.decay","CH Decay","Cowbell/Metal",0.5); add("mod.CY.tune","Cymbal Tune","Cowbell/Metal",0.5);
    add("mod.AC.depth","Accent Depth","Accent",0.5); add("mod.AC.punch","Accent Punch","Accent",0);
    add("mod.TRIG.src","Trigger Source","Trigger Out",0,0,3,1,{"CB","CP","AC","TRIG"}); add("mod.TRIG.width","Pulse Width (ms)","Trigger Out",20,1,40);
    add("mod.TRIG.level","Click Into Mix","Trigger Out",0); add("mod.TRIG.tone","Coupling Cap","Trigger Out",0.5);
    add("mod.age","Component Age","Machine",0.25); add("mod.drive","Machine Overdrive","Machine",0); add("mod.hiss","Hiss","Machine",0.15); add("swing","Swing","Machine",0);
    for (std::string v : SEND_VOICES) add(S("rev.send." + v), S(v + " Reverb"), "Reverb Sends", v == "SD" ? 0.12 : v == "CP" ? 0.18 : 0);
    for (std::string v : SEND_VOICES) add(S("dly.send." + v), S(v + " Delay"), "Delay Sends", 0);
    const int rp = std::min(6, NUM_REV_PROGRAMS - 1); const RevProgram& RP = REV_PROGRAMS[rp];
    add("rev.program","Program","REV7",rp,0,NUM_REV_PROGRAMS - 1,1,revPrograms());
    add("rev.time","Rev Time (s)","REV7",RP.time,0.1,12); add("rev.predelay","Predelay (ms)","REV7",RP.predelay,0,250); add("rev.hf","HF Ratio","REV7",RP.hf,0.1,1);
    add("rev.diffusion","Diffusion","REV7",RP.diffusion); add("rev.density","Density","REV7",RP.density); add("rev.lpf","LPF (Hz)","REV7",RP.lpf,1000,16000);
    add("rev.hpf","HPF (Hz)","REV7",RP.hpf,20,1000); add("rev.er","ER/Rev Balance","REV7",RP.er); add("rev.return","Reverb Return","REV7",0.6);
    add("dly.time","Delay Time (ms)","TC 2290",375,1,3000); add("dly.sync","Tempo Sync","TC 2290",1,0,1,1,{"Off","On"});
    add("dly.division","Division","TC 2290",6,0,NUM_DIVISIONS - 1,1,divisionNames());
    add("dly.feedback","Feedback","TC 2290",0.35); add("dly.hicut","Hi Cut (Hz)","TC 2290",6000,1000,20000); add("dly.lowcut","Low Cut (Hz)","TC 2290",120,20,2000);
    add("dly.modDepth","Mod Depth","TC 2290",0.15); add("dly.modRate","Mod Rate (Hz)","TC 2290",0.6,0.05,10); add("dly.pingpong","Ping Pong","TC 2290",1,0,1,1,{"Off","On"});
    add("dly.duck","Dynamic (Duck)","TC 2290",0.3); add("dly.return","Delay Return","TC 2290",0.5);
    return P;
  }();
  return P;
}
inline int paramIndex(const std::string& k) {
  static const std::unordered_map<std::string, int> idx = [] { std::unordered_map<std::string, int> m; const auto& P = params(); for (int i = 0; i < (int)P.size(); i++) m[P[i].key] = i; return m; }();
  auto it = idx.find(k); return it == idx.end() ? -1 : it->second;
}
inline int voiceIndex(const std::string& v) { for (int i = 0; i < 17; i++) if (v == VOICE_NAMES[i]) return i; return -1; }

// TPT state-variable filter (Simper)
struct SVF {
  double ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1, g = 0, bp = 0, lp = 0, hp = 0;
  void set(double fc, double Q, double sr) { const double gg = std::tan(PI * clampd(fc, 5, sr * 0.45) / sr); g = gg; k = 1 / Q; a1 = 1 / (1 + gg * (gg + k)); a2 = gg * a1; a3 = gg * a2; }
  double tick(double x) { const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; bp = v1; lp = v2; hp = x - k * v1 - v2; return v1; }
  void reset() { ic1 = 0; ic2 = 0; }
};
struct OP { double s = 0, G = 0;
  void set(double fc, double sr) { const double w = std::tan(PI * clampd(fc, 1, sr * 0.45) / sr); G = w / (1 + w); }
  double lp(double x) { const double v = (x - s) * G, y = v + s; s = y + v; return y; } double hp(double x) { return x - lp(x); } };
inline double sq(double ph, double dt) {   // polyBLEP square
  double y = ph < 0.5 ? 1 : -1;
  double t = ph; if (t < dt) { t /= dt; y += t + t - t * t - 1; } else if (t > 1 - dt) { t = (t - 1) / dt; y -= t * t + t + t + 1; }
  t = ph + 0.5; if (t >= 1) t -= 1;
  if (t < dt) { t /= dt; y -= t + t - t * t - 1; } else if (t > 1 - dt) { t = (t - 1) / dt; y += t * t + t + t + 1; }
  return y;
}

class Eight808 {
 public:
  static const int NV = 17;
  explicit Eight808(double sr_) : sr(sr_) {
    const auto& P = params();
    p.resize(P.size());
    for (size_t i = 0; i < P.size(); i++) p[i] = P[i].def;
    int32_t r = 0x13579bdf;
    auto xs = [&r] { r ^= (int32_t)((uint32_t)r << 13); r ^= (int32_t)((uint32_t)r >> 17); r ^= (int32_t)((uint32_t)r << 5); return (double)(uint32_t)r / 4294967296.0 * 2 - 1; };
    for (int i = 0; i < NV; i++) { tolF[i] = xs(); tolD[i] = xs(); }
    for (int i = 0; i < NV; i++) { pulseN[i] = 0; pulseH[i] = pulseH2[i] = env[i] = env2[i] = envC[i] = env2C[i] = sw[i] = swC[i] = amp[i] = 0; t[i] = 0; active[i] = 0;
      spos[i] = srate[i] = senv[i] = senvC[i] = 0; gain[i] = drk[i] = drn[i] = rs[i] = ds[i] = 0; }
    for (int k = 0; k < 6; k++) { mph[k] = mInc[k] = 0; tomF[k] = tomTau[k] = 0; }
    rev.reset(new Rev7(sr)); dly.reset(new TC2290(sr));
    rIn.assign(bufN, 0.f); dIn.assign(bufN, 0.f); rL.assign(bufN, 0.f); rR.assign(bufN, 0.f); dL.assign(bufN, 0.f); dR.assign(bufN, 0.f);
    for (size_t i = 0; i < P.size(); i++) apply(P[i].key, p[i]);
    dirty = 1;
  }
  double get(const std::string& k) const { const int i = paramIndex(k); return i < 0 ? 0 : p[i]; }
  void set(const std::string& k, double val) {
    if (k == "tempo") { tempo = val; dly->setTempo(val); return; }
    const int i = paramIndex(k); if (i < 0) return;
    const ParamInfo& d = params()[i]; val = clampd(val, d.min, d.max); if (d.step) val = jsround(val);
    p[i] = val; apply(k, val); dirty = 1;
    if (k == "rev.program") {
      const RevProgram& pr = REV_PROGRAMS[(int)val];
      p[paramIndex("rev.time")] = pr.time; p[paramIndex("rev.predelay")] = pr.predelay; p[paramIndex("rev.hf")] = pr.hf; p[paramIndex("rev.diffusion")] = pr.diffusion;
      p[paramIndex("rev.density")] = pr.density; p[paramIndex("rev.lpf")] = pr.lpf; p[paramIndex("rev.hpf")] = pr.hpf; p[paramIndex("rev.er")] = pr.er;
    }
  }
  std::string trackVoice(const std::string& tr) const {
    if (tr == "LT" || tr == "MT" || tr == "HT") return v(tr + ".sel") >= 0.5 ? std::string(1, tr[0]) + "C" : tr;
    if (tr == "RS") return v("RS.sel") >= 0.5 ? "CL" : "RS";
    if (tr == "CP") return v("CP.sel") >= 0.5 ? "MA" : "CP";
    return tr;
  }
  void loadSample(const std::string& vn, const float* data, int len, double ssr) {
    const int i = voiceIndex(vn); if (i < 0) return;
    const int n = std::max(1, (int)std::floor(len * sr / ssr)); const double r = ssr / sr;
    std::vector<float> out(n);
    auto at = [&](int a) { return a >= 0 && a < len ? (double)data[a] : 0.0; };
    for (int j = 0; j < n; j++) { const double x = j * r; const int a = (int)std::floor(x); const double f = x - a; out[j] = (float)(at(a) + (at(a + 1) - at(a)) * f); }
    smp[i] = std::move(out); spos[i] = 1e12;
  }
  void clearSample(const std::string& vn) { const int i = voiceIndex(vn); if (i >= 0) smp[i].clear(); }
  bool hasSample(const std::string& vn) const { const int i = voiceIndex(vn); return i >= 0 && !smp[i].empty(); }
  // fresh effects with the current settings (only after a NaN; allocates)
  void resetFx() {
    rev.reset(new Rev7(sr)); dly.reset(new TC2290(sr));
    const auto& P = params(); for (size_t i = 0; i < P.size(); i++) apply(P[i].key, p[i]);
    if (tempo != 0) dly->setTempo(tempo);
    for (int i = 0; i < NV; i++) { active[i] = 0; res1[i].reset(); res2[i].reset(); f1[i].reset(); f2[i].reset(); op1[i].s = 0; op2[i].s = 0; }
  }
  int trigFired = 0;   // set when the trigger out fires (the worklet clears it before each step)

  void trigger(const std::string& voice, int acc, double vel) {
    if (dirty) prepare();
    const int i = voiceIndex(voice);
    const double a = acc ? std::min(1.5, v("AC.level") * 2 * v("mod.AC.depth")) : 0;
    const double V = vel * (0.62 + 0.55 * a), punch = acc ? v("mod.AC.punch") * v("AC.level") : 0;
    const int src = (int)jsround(v("mod.TRIG.src"));
    if (voice == "AC") { if (src == 2) fireTrig(V); return; }
    if (i < 0) return;
    if (i == 16) { fireTrig(V); return; }
    if ((src == 0 && i == 12) || (src == 1 && (i == 10 || i == 11))) fireTrig(V);
    if (!smp[i].empty()) { spos[i] = 0; senv[i] = V; active[i] = 1; if (i == 15) choke = 1; return; }
    amp[i] = V; t[i] = 0; active[i] = 1;
    if (i == 0) { pulseN[i] = std::max(4, (int)jsround(0.001 * sr * (1 - 0.4 * punch))); sw[i] = V * (1 + punch); swC[i] = ec(0.004);
      res1[0].set(bdF * (1 + v("mod.BD.sweep") * 1.2 * sw[i]), PI * bdF * bdTau, sr);
      pulseH[i] = exc(res1[0], V, pulseN[i]); }
    else if (i == 1) { pulseN[i] = (int)jsround(0.0005 * sr); pulseH[i] = exc(res1[1], V, pulseN[i]); pulseH2[i] = exc(res2[1], V, pulseN[i]); env[i] = V; }
    else if (i <= 7) { const int j = i - 2; pulseN[i] = (int)jsround(0.0007 * sr); sw[i] = V; swC[i] = ec(0.02);
      res1[i].set(tomF[j], PI * tomF[j] * tomTau[j], sr); pulseH[i] = exc(res1[i], V, pulseN[i]); env[i] = V; envC[i] = ec(0.04); }
    else if (i == 8) { pulseN[i] = std::max(2, (int)jsround(0.0002 * sr)); pulseH[i] = exc(res1[8], V, pulseN[i]); pulseH2[i] = exc(res2[8], V, pulseN[i]); }
    else if (i == 9) { pulseN[i] = std::max(2, (int)jsround(0.0001 * sr)); pulseH[i] = exc(res1[9], V, pulseN[i]); }
    else if (i == 10) { env[i] = V; env2[i] = 0; }
    else if (i == 11) { env[i] = V; env2[i] = 0; }
    else if (i == 12) { env[i] = V * 0.75; env2[i] = V * 0.25; }
    else if (i == 13) { env[i] = V; env2[i] = V; }
    else if (i == 14) { choke = 0; dirty = 1; env[i] = V; }
    else if (i == 15) { env[i] = V; choke = 1; dirty = 1; }
  }

  void process(float* outL, float* outR, int n) { processAt(outL, outR, n, 0); }
  // renders into outL/outR[off .. off+n), as engine.js process(outL, outR, n, off)
  void processAt(float* outL, float* outR, int n, int off) {
    if (dirty) prepare();
    for (int s0 = 0; s0 < n; s0 += bufN) {
      const int m = std::min(bufN, n - s0);
      for (int s = 0; s < m; s++) {
        double dry = hissG > 0 ? noise() * hissG : 0, rv = 0, dl = 0;
        if (active[13] || active[14] || active[15]) {
          double x = 0;
          for (int k = 0; k < 6; k++) {
            const double f = mInc[k]; double ph = mph[k] + f; if (ph >= 1) ph -= 1; mph[k] = ph;
            double y = ph < 0.5 ? 1 : -1, tt = ph;
            if (tt < f) { tt /= f; y += tt + tt - tt * tt - 1; } else if (tt > 1 - f) { tt = (tt - 1) / f; y -= tt * tt + tt + tt + 1; }
            tt = ph + 0.5; if (tt >= 1) tt -= 1;
            if (tt < f) { tt /= f; y -= tt + tt - tt * tt - 1; } else if (tt > 1 - f) { tt = (tt - 1) / f; y += tt * tt + tt + tt + 1; }
            x += y;
          }
          x /= 6;
          if (active[13] && smp[13].empty()) {
            const double lo = f1[13].tick(x) * env[13], hi = op1[13].hp(x) * env2[13]; env[13] *= envC[13]; env2[13] *= env2C[13];
            const double tn = cyTone; const double y = (lo * (1.2 * (1 - tn) + 0.2) + hi * (0.4 + tn)) * 0.9;
            if (env[13] < 1e-4) active[13] = 0;
            dry += y * gain[13]; rv += y * gain[13] * rs[13]; dl += y * gain[13] * ds[13];
          }
          for (int h = 14; h <= 15; h++) if (active[h] && smp[h].empty()) {
            const double y = op1[h].hp(f1[h].tick(x)) * env[h] * 1.8; env[h] *= envC[h];
            if (env[h] < 1e-4) active[h] = 0;
            dry += y * gain[h]; rv += y * gain[h] * rs[h]; dl += y * gain[h] * ds[h];
          }
        }
        for (int i = 0; i < 17; i++) {
          if (!active[i]) continue;
          if ((i >= 13 && i <= 15) && smp[i].empty()) continue;
          double y = voice(i);
          if (drn[i] > 0) y = std::tanh(y * drk[i]) * drn[i];
          y *= gain[i];
          dry += y; rv += y * rs[i]; dl += y * ds[i];
        }
        if (od > 0.001) {
          const double k = 1 + 15 * od, b = 0.2 * od, x = dry * k;
          const double yd = (std::tanh(x + b) - std::tanh(b)) / (1 + 2.5 * od);
          const double gr = std::fabs(dry) > 1e-9 ? yd / dry : k / (1 + 2.5 * od);
          dry = yd; rv *= gr; dl *= gr;
        }
        rIn[s] = (float)rv; dIn[s] = (float)dl;
        outL[off + s0 + s] = (float)dry; outR[off + s0 + s] = (float)dry;
      }
      rev->process(rIn.data(), rIn.data(), rL.data(), rR.data(), m, 0);
      dly->process(dIn.data(), dIn.data(), dL.data(), dR.data(), m, 0);
      const double rr = v("rev.return"), dr = v("dly.return"), ms = v("master");
      for (int s = 0; s < m; s++) {
        const int j = off + s0 + s;
        double l = outL[j], r = outR[j];
        l += rL[s] * rr; r += rR[s] * rr;
        l += dL[s] * dr; r += dR[s] * dr;
        outL[j] = (float)std::tanh(l * ms * 1.2); outR[j] = (float)std::tanh(r * ms * 1.2);
      }
    }
  }

 private:
  double sr;
  std::vector<double> p;
  uint32_t rng = 808;
  double tolF[NV], tolD[NV];
  SVF res1[NV], res2[NV], f1[NV], f2[NV];
  OP op1[NV], op2[NV];
  int pulseN[NV]; double pulseH[NV], pulseH2[NV];
  double env[NV], env2[NV], envC[NV], env2C[NV], sw[NV], swC[NV], amp[NV]; int t[NV];
  uint8_t active[NV]; int choke = 0;
  double mph[6], mInc[6], cph[2] = {0, 0};
  std::vector<float> smp[NV];
  double spos[NV], srate[NV], senv[NV], senvC[NV];
  int trigHigh = 0; double trigAmp = 0; OP trigHP;
  std::unique_ptr<Rev7> rev; std::unique_ptr<TC2290> dly;
  int bufN = 4096;
  std::vector<float> rIn, dIn, rL, rR, dL, dR;
  double gain[NV], drk[NV], drn[NV], rs[NV], ds[NV];
  int dirty = 1;
  double tempo = 0;
  double od = 0, bdF = 0, bdTau = 0, tomF[6], tomTau[6], cpSpread = 0, cbT = 0, mT = 0, hissG = 0;
  double bdSweep = 0, bdClick = 0, bdDk = 1, bdDn = 0, sdTone = 0, sdSnap = 0, cyTone = 0, tomSweep = 0;

  double v(const std::string& k) const { return p[paramIndex(k)]; }
  void apply(const std::string& k, double val) {
    if (k.compare(0, 4, "rev.") == 0 && k.compare(0, 8, "rev.send") != 0 && k != "rev.return") rev->set(k, val);
    if (k.compare(0, 4, "dly.") == 0 && k.compare(0, 8, "dly.send") != 0 && k != "dly.return") dly->set(k, val);
  }
  double noise() { uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return ((double)rng / 2147483648.0) - 1; }
  double tf(int i) const { return 1 + v("mod.age") * tolF[i] * 0.05; }
  double td(int i) const { return 1 + v("mod.age") * tolD[i] * 0.2; }
  double ec(double tau) const { return std::exp(-1 / (std::max(1e-4, tau) * sr)); }
  static double exc(const SVF& s, double a, int n) { return a / (2 * s.g * n); }
  void fireTrig(double V) { trigHigh = std::max(1, (int)jsround(v("mod.TRIG.width") * 0.001 * sr)); trigAmp = V; trigFired = 1; active[16] = 1; }

  void prepare() {
    static const double TRIM[17] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.7, 0.85, 0.55, 0.45, 0.45, 1};
    od = v("mod.drive");
    for (int i = 0; i < NV; i++) {
      const std::string o = OWNER[i];
      const double lv = o == "TRIG" ? v("mod.TRIG.level") : v(o + ".level");
      gain[i] = lv * lv * 1.4 * TRIM[i];
      const double dk = i == 1 ? v("mod.SD.drive") : (i >= 2 && i <= 7) ? v("mod.TOM.drive") : i == 10 ? v("mod.CP.drive") : 0;
      drk[i] = 1 + 6 * dk; drn[i] = dk > 0.001 ? 1 / std::tanh(drk[i]) : 0;
      rs[i] = v("rev.send." + o); ds[i] = v("dly.send." + o);
    }
    // BD
    bdF = 52 * std::pow(2.0, (v("mod.BD.tune") - 0.5) * 2) * tf(0);
    bdTau = 0.05 * std::pow(16.0, v("BD.decay")) * (1 + v("mod.BD.longDecay") * 15) * td(0);
    op1[0].set(120 * std::pow(2.0, v("BD.tone") * 6.5), sr);
    // SD
    const double st = std::pow(2.0, (v("mod.SD.tune") - 0.5)) * tf(1), sdm = 0.4 + 1.2 * v("mod.SD.decay");
    res1[1].set(180 * st, PI * 180 * st * 0.06 * sdm, sr); res2[1].set(330 * st, PI * 330 * st * 0.045 * sdm, sr);
    op1[1].set(600 + 2500 * v("SD.tone"), sr); op2[1].set(3000 * std::pow(2.0, v("mod.SD.noiseColor") * 2.3), sr);
    envC[1] = ec(0.09 * (0.5 + v("mod.SD.decay")) * td(1));
    // toms / congas
    for (int j = 0; j < 6; j++) {
      const int i = 2 + j; const std::string o = OWNER[i];
      const double f = TOM_F[j][0] * std::pow(TOM_F[j][1] / TOM_F[j][0], v(o + ".tuning")) * tf(i);
      const double dm = j < 3 ? 0.3 + 1.4 * v("mod.TOM.decay") : 0.3 + 1.4 * v("mod.CONGA.decay");
      f1[i].set(f, 1, sr);
      tomF[j] = f;
      tomTau[j] = TOM_TAU[j] * dm * td(i);
      if (sw[i] < 1e-4) res1[i].set(f, PI * f * tomTau[j], sr);
    }
    op1[2].set(1500, sr);
    // RS, CL
    const double rt = std::pow(2.0, v("mod.RS.tune") - 0.5) * tf(8);
    res1[8].set(455 * rt, PI * 455 * rt * 0.012, sr); res2[8].set(1667 * rt, PI * 1667 * rt * 0.007, sr); op1[8].set(250, sr);
    const double ct = std::pow(2.0, v("mod.CL.tune") - 0.5) * tf(9);
    res1[9].set(2500 * ct, PI * 2500 * ct * 0.02, sr);
    // CP, MA
    f1[10].set(1100, 2.5, sr);
    cpSpread = jsround((0.005 + 0.012 * v("mod.CP.spread")) * sr);
    envC[10] = ec(0.0025); env2C[10] = ec(0.03 + 0.4 * std::pow(v("mod.CP.tail"), 1.5));
    f1[11].set(6000, 0.8, sr); envC[11] = ec(0.025 * (0.4 + 1.2 * v("mod.MA.decay")) * td(11));
    // CB
    cbT = std::pow(2.0, v("mod.CB.tune") - 0.5) * tf(12);
    f1[12].set(900, 1.2, sr); op1[12].set(300, sr);
    envC[12] = ec(0.012); env2C[12] = ec((0.05 + 0.3 * v("mod.CB.decay")) * td(12));
    // metal
    mT = std::pow(2.0, v("mod.HH.tune") - 0.5);
    for (int k = 0; k < 6; k++) mInc[k] = METAL_F[k] * mT / sr;
    const double cyt = std::pow(2.0, v("mod.CY.tune") - 0.5);
    f1[13].set(3440 * cyt, 1.5, sr); op1[13].set(7000 * cyt, sr);
    envC[13] = ec((0.3 + 1.7 * std::pow(v("CY.decay"), 1.5)) * td(13)); env2C[13] = ec((0.15 + 0.4 * v("CY.decay")) * td(13));
    f1[14].set(7100, 1.5, sr); op1[14].set(6500, sr);
    envC[14] = choke ? ec(0.004) : ec((0.06 + 0.6 * v("OH.decay") * v("OH.decay")) * td(14));
    f1[15].set(7100, 1.5, sr); op1[15].set(6500, sr);
    envC[15] = ec((0.008 + 0.04 * v("mod.CH.decay")) * td(15));
    // TRIG
    trigHP.set(20 * std::pow(2.0, v("mod.TRIG.tone") * 7), sr);
    // samples: decay knob where the voice has one
    for (int i = 0; i < NV; i++) {
      const std::string o = OWNER[i];
      const double dk = o == "BD" ? v("BD.decay") : o == "CY" ? v("CY.decay") : o == "OH" ? v("OH.decay") : 1;
      senvC[i] = dk >= 0.999 ? 1 : ec(0.03 * std::pow(200.0, dk));
      const double tu = (o == "LT" || o == "MT" || o == "HT") ? v(o + ".tuning") : 0.5;
      srate[i] = std::pow(2.0, (tu - 0.5) * 2);
    }
    hissG = v("mod.hiss") * 0.003;
    bdSweep = v("mod.BD.sweep") * 1.2; bdClick = v("mod.BD.click") * 0.6;
    const double bdr = v("mod.BD.drive"); bdDk = 1 + 6 * bdr; bdDn = bdr > 0.001 ? 1 / std::tanh(bdDk) : 0;
    sdTone = v("SD.tone"); sdSnap = v("SD.snappy") * 1.6; cyTone = v("CY.tone"); tomSweep = 0.6 * v("mod.TOM.sweep");
    dirty = 0;
  }

  double voice(int i) {
    double y = 0;
    if (!smp[i].empty()) {
      const std::vector<float>& d = smp[i]; const double ps = spos[i];
      if (ps >= (double)d.size() - 1) { active[i] = 0; return 0; }
      const int a = (int)std::floor(ps); const double f = ps - a; y = ((double)d[a] + ((double)d[a + 1] - (double)d[a]) * f) * senv[i];
      spos[i] = ps + srate[i]; senv[i] *= senvC[i];
      if (i == 14 && choke) senv[i] *= 0.995;
      return y;
    }
    const int tt = t[i]++; const bool pl = tt < pulseN[i];
    switch (i) {
      case 0: {
        SVF& r = res1[0];
        if (sw[0] > 1e-4) { sw[0] *= swC[0]; const double f = bdF * (1 + bdSweep * sw[0]); r.set(f, PI * f * bdTau, sr); }
        const double x = pl ? pulseH[0] : 0;
        y = r.tick(x) + (pl ? amp[0] * bdClick : 0);
        y = op1[0].lp(y);
        if (bdDn > 0) y = std::tanh(y * bdDk) * bdDn;
        if (tt > sr * 0.05 && std::fabs(r.ic1) + std::fabs(r.ic2) < 3e-5) active[0] = 0;
        return y * 0.9;
      }
      case 1: {
        SVF& r1 = res1[1]; SVF& r2 = res2[1]; const double tn = sdTone;
        const double b = r1.tick(pl ? pulseH[1] : 0) * (1.1 - 0.6 * tn) + r2.tick(pl ? pulseH2[1] : 0) * (0.4 + 0.6 * tn);
        const double nz = op2[1].lp(op1[1].hp(noise())) * env[1] * sdSnap;
        env[1] *= envC[1];
        if (tt > sr * 0.05 && env[1] < 1e-4 && std::fabs(r1.ic1) + std::fabs(r1.ic2) + std::fabs(r2.ic1) + std::fabs(r2.ic2) < 3e-5) active[1] = 0;
        return (b * 0.7 + nz) * 0.8;
      }
      case 8: { double x = res1[8].tick(pl ? pulseH[8] : 0) * 0.8 - res2[8].tick(pl ? pulseH2[8] : 0) * 0.5;
        x = op1[8].hp(x); if (tt > sr * 0.3) active[8] = 0; return std::tanh(2 * x) * 0.55; }
      case 9: { if (tt > sr * 0.3) active[9] = 0; return res1[9].tick(pl ? pulseH[9] : 0) * 0.7; }
      case 10: {
        const int sp = (int)cpSpread;
        if (tt < 3 * sp) { if (tt % sp == 0) env[10] = amp[10]; } else if (tt == 3 * sp) env2[10] = amp[10] * 0.7;
        const double e = env[10] + env2[10]; env[10] *= envC[10]; env2[10] *= env2C[10];
        if (tt > 3 * sp && e < 1e-4) active[10] = 0;
        return f1[10].tick(noise()) * e * 1.6;
      }
      case 11: { env2[11] += (1 - env2[11]) * 0.02; const double e = env[11] * env2[11]; env[11] *= envC[11];
        f1[11].tick(noise()); if (env[11] < 1e-4) active[11] = 0; return f1[11].hp * e * 0.9; }
      case 12: {
        const double fa = 540 * cbT / sr, fb = 800 * cbT / sr;
        cph[0] += fa; if (cph[0] >= 1) cph[0] -= 1; cph[1] += fb; if (cph[1] >= 1) cph[1] -= 1;
        const double x = (sq(cph[0], fa) + sq(cph[1], fb)) * 0.5;
        const double e = env[12] + env2[12]; env[12] *= envC[12]; env2[12] *= env2C[12];
        if (e < 1e-4) active[12] = 0;
        return op1[12].hp(f1[12].tick(x)) * e * 1.2;
      }
      case 16: {
        const double x = trigHigh > 0 ? trigAmp : 0; if (trigHigh > 0) trigHigh--;
        const double yy = std::tanh(trigHP.hp(x) * 3) * 0.8;
        if (trigHigh == 0 && std::fabs(trigHP.s - x) < 1e-5) active[16] = 0;
        return yy;
      }
      default: {
        const int j = i - 2; SVF& r = res1[i];
        if (sw[i] > 1e-4) { sw[i] *= swC[i]; const double dep = j < 3 ? tomSweep : 0.05;
          const double f = tomF[j] * (1 + dep * sw[i]); r.set(f, PI * f * tomTau[j], sr); }
        y = r.tick(pl ? pulseH[i] : 0);
        if (j < 3) { y += op1[2].lp(noise()) * env[i] * 0.25; env[i] *= envC[i]; }
        if (tt > sr * 0.1 && std::fabs(r.ic1) + std::fabs(r.ic2) < 3e-5) active[i] = 0;
        return y * 0.85;
      }
    }
  }
};

}  // namespace eight
