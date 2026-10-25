// The F.A.R.P. sound engine: the string machine (Omni / Omni-2), its synth and bass sections, the 3-voice BBD
// chorus, the DFX-84 effects unit, the instrument engines, the vocoder and the mixer.
// C++ port of the page's RomPcmEngine and FarpDSP; it takes the same messages the page sends its AudioWorklet.
#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include "Val.h"
#include "Wms.h"
#include "Emu.h"
#include "FmCz.h"
#include "Vox.h"

namespace farp {

class RomPcmEngine {
public:
    struct P { double a = 0.002, d = 1.5, s = 1, r = 0.4, cut = 1, res = 0.1, tune = 0, level = 0.9, vel = 0.6; } p;
    struct V { int note = -1; bool on = false; int age = 0; const Zone* z = nullptr; double pos = 0, dir = 1, env = 0; int st = 0; double vel = 1, ic1 = 0, ic2 = 0, base = 0; };
    double sr; std::vector<Zone> zones; V v[16]; int counter = 0; double bend = 0, wheel = 0, level = 0.9;
    explicit RomPcmEngine(double sr_) : sr(sr_) {}
    void setParams(const Val& o) {
#define F(k) setNum(p.k, o, #k);
        F(a)F(d)F(s)F(r)F(cut)F(res)F(tune)F(level)F(vel)
#undef F
    }
    void setZones(std::vector<Zone> z) { allOff(); for (auto& x : v) { x.st = 0; x.env = 0; x.z = nullptr; } zones = std::move(z); }
    const Zone* zoneFor(int n, double vel) const {
        for (auto& z : zones) if (n >= z.lo && n <= z.hi && (z.vlo < 0 || (vel * 127 >= z.vlo && vel * 127 <= z.vhi))) return &z;
        return zones.empty() ? nullptr : &zones.back();
    }
    void noteOn(int n, double vel) {
        const Zone* z = zoneFor(n, vel); if (!z) return; V* t = nullptr; for (auto& x : v) if (x.st == 0) { t = &x; break; }
        if (!t) { t = &v[0]; for (auto& x : v) if (x.age < t->age) t = &x; }
        t->note = n; t->on = true; t->age = ++counter; t->z = z; t->pos = 0; t->dir = 1; t->vel = vel; t->st = 1; t->env = 0; t->ic1 = t->ic2 = 0; t->base = z->rate / sr * std::pow(2.0, (n - z->root) / 12);
    }
    void noteOff(int n) { for (auto& x : v) if (x.note == n && x.on) { x.on = false; if (x.st) x.st = 4; } }
    void allOff() { for (auto& x : v) { x.on = false; if (x.st) x.st = 4; } }
    bool active() const { for (auto& x : v) if (x.st) return true; return false; }
    double tick() {
        const double bnd = std::pow(2.0, (bend + p.tune / 100) / 12), kA = 1 - std::exp(-1 / (std::max(0.0005, p.a) * sr)), kD = 1 - std::exp(-1 / (std::max(0.001, p.d) * sr * 0.3)), kR = 1 - std::exp(-1 / (std::max(0.001, p.r) * sr * 0.3));
        const double fc = std::min(0.45 * sr, 40 * std::pow(500.0, p.cut)), g = std::tan(PI * fc / sr), k = 2 - 1.9 * std::min(1.0, p.res), a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2; double sum = 0;
        for (auto& x : v) {
            if (!x.st || !x.z) continue; const Zone* z = x.z; const std::vector<float>& d = *z->data; const double L = z->end; const size_t dn = d.size();
            if (x.st == 1) { x.env += (1.02 - x.env) * kA; if (x.env >= 1) { x.env = 1; x.st = 2; } } else if (x.st == 2) { x.env += (p.s - x.env) * kD; } else if (x.st == 4) { x.env -= x.env * kR; if (x.env < 1e-4) { x.st = 0; continue; } }
            const size_t i = (size_t)x.pos; const double f = x.pos - (double)i; if (i >= dn) { x.st = 0; continue; }
            const double di = d[i], dj = i + 1 < dn ? d[i + 1] : di; const double xs = di + (dj - di) * f;
            const double step = x.base * bnd;
            if (z->loop == 2) { x.pos += step * x.dir; if (x.dir > 0 && x.pos >= L - 1) { x.dir = -1; x.pos = L - 1; } else if (x.dir < 0 && x.pos <= z->ls) { x.dir = 1; x.pos = z->ls; } }
            else { x.pos += step; if (x.pos >= L - 1) { if (z->loop == 1 && L - z->ls > 2) x.pos = z->ls + std::fmod(x.pos - z->ls, L - z->ls); else { x.st = 0; continue; } } }
            const double v3 = xs - x.ic2, b1 = a1 * x.ic1 + a2 * v3, lp = x.ic2 + a2 * x.ic1 + a3 * v3; x.ic1 = 2 * b1 - x.ic1; x.ic2 = 2 * lp - x.ic2;
            sum += (p.cut >= 0.999 ? xs : lp) * x.env * (1 - p.vel + p.vel * x.vel);
        }
        return sum * 0.5 * p.level * level;
    }
};

inline std::vector<Zone> zonesFrom(const Val& arr) {
    std::vector<Zone> out;
    for (size_t i = 0; i < arr.size(); i++) {
        const Val& o = arr[i]; Zone z;
        if (o["data"].type == Val::F32) z.data = o["data"].f; else z.data = std::make_shared<std::vector<float>>();
        z.lo = (int)o["lo"].num(0); z.hi = (int)o["hi"].num(127); z.root = o.has("root") ? o["root"].num() : -1;
        z.end = o.has("end") ? o["end"].num() : (double)z.data->size(); z.ls = o["ls"].num(); z.rate = o["rate"].num(); z.loop = (int)o["loop"].num();
        if (o.has("vlo")) { z.vlo = o["vlo"].num(); z.vhi = o["vhi"].num(); }
        out.push_back(std::move(z));
    }
    return out;
}

class FarpDSP {
public:
    // ---- outgoing messages (JSON) ----
    std::function<void(std::string&&)> post;

    double fs;
    std::map<std::string, double> p;
    int div[12] = {478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253};
    double ph[12] = {}; int32_t cnt[12] = {};
    float sq[12 * 9] = {}, saw[12 * 9] = {};
    uint8_t down[128] = {}, susd[128] = {}; float vel[128]; float se[128] = {}, sg[128] = {};
    double sus = 0, bend = 0, mod = 0; bool trig = false, prevHeld = false;
    double env = 0; int stage = 0; double ar = 0;
    double y1 = 0, y2 = 0, y3 = 0, y4 = 0;
    double vl1 = 0, vh1 = 0, vh2 = 0, va1 = 0, va2 = 0, ce1 = 0, ce2 = 0, sb1 = 0;
    double sbsEnv = 0; int sbsNote = -1;
    int bNote = -1; double bEnv = 0; bool bHeld = false; double bLow = 0, bBand = 0;
    struct Mx { double g[9], bl[9], br[9], s[9]; double master = 1, bus = 1, busL = 1, busR = 1; float pk[12]; } mx;
    P5Engine p5; E2Engine e2; P5R3Engine p5r3; SixtrakEngine six; DXEngine dx; CZEngine cz; RomPcmEngine sd1, cm;
    enum Ins { I_P5, I_E2, I_P5R3, I_SIX, I_DX, I_CZ, I_SD1, I_CM, I_N };
    int insCh[I_N] = {4, 5, 4, 6, 7, 8, 5, 5}; double insG[I_N] = {3, 1, 3.2, 3.4, 2.2, 1.3, 2.6, 2};
    double iout[9] = {}; int stN = 0;
    uint8_t mask[128]; struct QMsg { double at; bool on; int n; double v; int m; int inst; };
    std::vector<QMsg> q; double frames = 0; bool sHeld = false;
    double lfo = 0; int bAtt = 0, bStage = 0; float outs[3] = {};
    std::vector<float> cb; int cl, cw = 0;
    double cpre1 = 0, cpre2 = 0; float cpost[3] = {}; double cph[3] = {0, 0.33, 0.71}, cdrift[3] = {};
    uint32_t seed = 12345;
    int D; double lfs; int dcount = 0; double acc = 0;
    double fxL = 0, fxR = 0, fpre = 0, fpostL = 0, fpostL2 = 0, fpostR = 0, fpostR2 = 0;
    std::vector<float> flb; int flw = 0; double flp = 0;
    std::vector<float> dlb; int dlw = 0; double dld = 0;
    struct Comb { std::vector<float> b; int i = 0; double f = 0; }; struct Ap { std::vector<float> b; int i = 0; };
    Comb cmbL[4], cmbR[4]; Ap apL[2], apR[2];
    float scope[1024] = {}; int si = 0;
    double time() const { return frames / fs; }
    struct SusOff { int n; int inst; }; std::vector<SusOff> isus; bool isusSet = false;
    // vocoder
    struct Voc { bool on = false; std::string eng = "clear"; int ch = -1; std::unique_ptr<VxClear> clear; std::unique_ptr<VxCircuit> svc; std::unique_ptr<TalkerPedal> talker; } voc; bool vocMade = false;
    // derived coefficients
    double cresC = 0, sustC = 0, aC = 0, dC = 0, rC = 0, gC = 0, fcBase = 0, lfoF = 0, cVl = 0, cVh = 0, cVa = 0, cCe = 0, cCh = 0, cSb = 0, cPre = 0, cPost = 0, cBass = 0, cFx = 0;
    int bits = 12; double qs = 2048, flF = 0, dlMs = 0, cDamp = 0, rvFb = 0;

    explicit FarpDSP(double fs_) : fs(fs_), p5(fs_), e2(fs_), p5r3(fs_), six(fs_), dx(fs_), cz(fs_), sd1(fs_), cm(fs_) {
        std::fill(vel, vel + 128, 1.f); std::fill(mask, mask + 128, (uint8_t)7);
        for (int j = 0; j < 9; j++) { mx.g[j] = mx.bl[j] = mx.br[j] = mx.s[j] = 1; } std::fill(mx.pk, mx.pk + 12, 0.f);
        cm.level = 3.2;
        cl = (int)std::ceil(fs * 0.03); cb.assign(cl, 0.f);
        D = fs >= 88000 ? 4 : (fs >= 40000 ? 2 : 1); lfs = fs / D;
        const double L = lfs; flb.assign((size_t)std::ceil(L * 0.02), 0.f); dlb.assign((size_t)std::ceil(L * 1.1), 0.f);
        const double sc = L / 44100; const int cmbLen[4] = {1116, 1188, 1277, 1356}; const int apLen[2] = {556, 441};
        for (int j = 0; j < 4; j++) { int n = std::max(8, (int)std::lround(cmbLen[j] * sc)); cmbL[j].b.assign(n, 0.f); cmbR[j].b.assign(n + (int)std::lround(23 * sc), 0.f); }
        for (int j = 0; j < 2; j++) { int n = std::max(4, (int)std::lround(apLen[j] * sc)); apL[j].b.assign(n, 0.f); apR[j].b.assign(n + (int)std::lround(17 * sc), 0.f); }
        upd();
    }

    bool has(const char* k) const { return p.count(k) > 0; }
    double g(const char* k) const { auto it = p.find(k); return it == p.end() ? 0 : it->second; }

    static int insOf(const std::string& s) {
        if (s == "p5") return I_P5; if (s == "e2") return I_E2; if (s == "p5r3") return I_P5R3; if (s == "six") return I_SIX;
        if (s == "dx") return I_DX; if (s == "cz") return I_CZ; if (s == "sd1") return I_SD1; if (s == "cm") return I_CM; return -1;
    }
    bool insActive(int i) const {
        switch (i) { case I_P5: return p5.active(); case I_E2: return e2.active(); case I_P5R3: return p5r3.active(); case I_SIX: return six.active();
            case I_DX: return dx.active(); case I_CZ: return cz.active(); case I_SD1: return sd1.active(); default: return cm.active(); }
    }
    double insTick(int i) {
        switch (i) { case I_P5: return p5.tick(); case I_E2: return e2.tick(); case I_P5R3: return p5r3.tick(); case I_SIX: return six.tick();
            case I_DX: return dx.tick(); case I_CZ: return cz.tick(); case I_SD1: return sd1.tick(); default: return cm.tick(); }
    }
    void insOn(int i, int n, double v) {
        switch (i) { case I_P5: p5.noteOn(n, v); break; case I_E2: e2.noteOn(n, v); break; case I_P5R3: p5r3.noteOn(n); break; case I_SIX: six.noteOn(n); break;
            case I_DX: dx.noteOn(n, v); break; case I_CZ: cz.noteOn(n, v); break; case I_SD1: sd1.noteOn(n, v); break; default: cm.noteOn(n, v); }
    }
    void insOff(int i, int n) {
        switch (i) { case I_P5: p5.noteOff(n); break; case I_E2: e2.noteOff(n); break; case I_P5R3: p5r3.noteOff(n); break; case I_SIX: six.noteOff(n); break;
            case I_DX: dx.noteOff(n); break; case I_CZ: cz.noteOff(n); break; case I_SD1: sd1.noteOff(n); break; default: cm.noteOff(n); }
    }
    void insAllOff(int i) {
        switch (i) { case I_P5: p5.allOff(); break; case I_E2: e2.allOff(); break; case I_P5R3: p5r3.allOff(); break; case I_SIX: six.allOff(); break;
            case I_DX: dx.allOff(); break; case I_CZ: cz.allOff(); break; case I_SD1: sd1.allOff(); break; default: cm.allOff(); }
    }

    // ---- messages from the page ----
    void msg(const Val& m) {
        const std::string& t = m["t"].str();
        if ((t == "on" || t == "off") && m["at"].num() > 0) {
            QMsg e{m["at"].num(), t == "on", (int)m["n"].num(), m["v"].num(1), m.has("m") ? (int)m["m"].num() : -1, insOf(m["inst"].str())};
            if (!m["inst"].truthy()) e.inst = -1; q.push_back(e); return;
        }
        if (t == "clearq") { q.clear(); return; }
        if (t == "p5") { p5.setParams(m["p"]); return; }
        if (t == "e2") { e2.setParams(m["p"]); return; }
        if (t == "e2z") { e2.setZones(zonesFrom(m["z"])); return; }
        if (t == "voc") {
            voc.on = m["on"].truthy(); voc.eng = m["eng"].isStr() ? m["eng"].str() : std::string("undefined"); voc.ch = (int)m["ch"].num(-1);
            if (voc.on) {
                if (voc.eng == "svc") { if (!voc.svc) voc.svc = std::make_unique<VxCircuit>(fs); if (m.has("p")) voc.svc->p.assign(m["p"]); }
                else if (voc.eng == "talker") { if (!voc.talker) voc.talker = std::make_unique<TalkerPedal>(fs); const Val& tk = m["tk"];
                    if (tk.isObj()) { voc.talker->setMode(tk["mode"].num()); voc.talker->knob.mic = tk["mic"].num(); voc.talker->knob.inst = tk["inst"].num(); voc.talker->knob.out = tk["out"].num(); } }
                else { if (!voc.clear) voc.clear = std::make_unique<VxClear>(fs); if (m.has("p")) voc.clear->p.assign(m["p"]); }
            }
            return;
        }
        if (t == "cap") return; // stem capture is the page's FILES screen, not part of the plugin
        if (t == "eng") { engMsg(m); return; }
        if (t == "mix") {
            const Val& G = m["g"];
            for (size_t j = 0; j < G.size() && j < 9; j++) { mx.g[j] = G[j].num(); const double pn = m["pan"][j].num(); mx.bl[j] = std::min(1.0, 1 - pn); mx.br[j] = std::min(1.0, 1 + pn); mx.s[j] = m["send"][j].num(); }
            mx.master = m["master"].num(1);
            if (m.has("bus")) { mx.bus = m["bus"].num(); const double bp = m["busPan"].num(); mx.busL = std::min(1.0, 1 - bp); mx.busR = std::min(1.0, 1 + bp); }
            return;
        }
        apply(m);
    }
    void engMsg(const Val& m) {
        const int i = insOf(m["i"].str()); if (i < 0) return; const std::string& op = m["op"].str();
        auto bytes = [](const Val& v) -> const std::vector<uint8_t>* { return v.type == Val::U8 ? v.u.get() : nullptr; };
        if (op == "load") {
            bool loaded = false;
            if (i == I_P5R3 && bytes(m["rom"])) { p5r3.load(*bytes(m["rom"]), bytes(m["nv"])); loaded = p5r3.loaded; }
            else if (i == I_SIX && bytes(m["rom"])) { six.load(*bytes(m["rom"]), bytes(m["a"]), bytes(m["b"])); loaded = six.loaded; }
            if (post) { JsonOut j; j.raw("{\"t\":\"eng\""); j.key("i"); j.str(m["i"].str()); j.key("loaded"); j.raw(loaded ? "true" : "false"); j.raw("}"); post(std::move(j.s)); }
        }
        else if (op == "unload") { insAllOff(i); if (i == I_P5R3) p5r3.loaded = false; if (i == I_SIX) six.loaded = false; }
        else if (op == "pot") {
            if (i == I_P5R3) { int k = (int)m["k"].num(); if (k >= 0 && k < 24) p5r3.m.pots[k] = std::min(1.0, std::max(0.0, m["v"].num())); }
            else if (i == I_SIX) { const std::string& k = m["k"].str(); const double v = m["v"].num(); auto& M = six.m;
                if (k == "valueKnob") M.valueKnob = v; else if (k == "tuneKnob") M.tuneKnob = v; else if (k == "trackVolKnob") M.trackVolKnob = v; else if (k == "speedKnob") M.speedKnob = v;
                else if (k == "modWheel") M.modWheel = v; else if (k == "pitchWheel") M.pitchWheel = v; else if (k == "footswitch") M.footswitch = m["v"].truthy(); }
        }
        else if (op == "tap") {
            if (i == I_P5R3) p5r3.tap((int)m["row"].num(), (int)m["bit"].num(), m["ms"].num());
            else if (i == I_SIX) six.tap((int)m["row"].num(), (int)m["bit"].num(), m["ms"].num(), m.has("hr") && !m["hr"].isNull() ? (int)m["hr"].num() : -1, (int)m["hb"].num());
        }
        else if (op == "key") { if (i == I_SIX) six.setKey((int)m["row"].num(), (int)m["bit"].num(), m["down"].truthy()); }
        else if (op == "voice") { if (i == I_DX) dx.setVoice(DXEngine::fromVal(m["v"])); }
        else if (op == "tone") { if (i == I_CZ) cz.setTone(CZEngine::fromVal(m["v"])); }
        else if (op == "zones") { if (i == I_SD1) sd1.setZones(zonesFrom(m["z"])); else if (i == I_CM) cm.setZones(zonesFrom(m["z"])); else if (i == I_E2) e2.setZones(zonesFrom(m["z"])); }
        else if (op == "params") { if (i == I_SD1) sd1.setParams(m["p"]); else if (i == I_CM) cm.setParams(m["p"]); else if (i == I_P5) p5.setParams(m["p"]); else if (i == I_E2) e2.setParams(m["p"]); }
        else if (op == "level") {
            const double v = m["v"].num();
            switch (i) { case I_P5: p5.p.level = v; break; case I_E2: e2.p.level = v; break; case I_P5R3: p5r3.level = v; break; case I_SIX: six.level = v; break;
                case I_DX: dx.level = v; break; case I_CZ: cz.level = v; break; case I_SD1: sd1.level = v; break; default: cm.level = v; }
        }
        else if (op == "run") {
            const bool run = m["v"].truthy();
            if (i == I_P5R3) p5r3.paused = !run; else if (i == I_SIX) six.paused = !run;
            if (!run) insAllOff(i);
        }
        else if (op == "nv" && post) {
            JsonOut j; j.raw("{\"t\":\"nv\""); j.key("i"); j.str(m["i"].str());
            if (i == I_P5R3) { p5r3.m.nvDirty = false; j.key("nv"); j.raw("{\"$u\":"); j.str(Val::enc64(p5r3.m.nvram, 0x400)); j.raw("}"); j.key("a"); j.raw("null"); j.key("b"); j.raw("null"); }
            else if (i == I_SIX) { six.m.nvDirty = false; j.key("nv"); j.raw("null"); j.key("a"); j.raw("{\"$u\":"); j.str(Val::enc64(six.m.nvA, 0x1000)); j.raw("}"); j.key("b"); j.raw("{\"$u\":"); j.str(Val::enc64(six.m.nvB, 0x800)); j.raw("}"); }
            j.raw("}"); post(std::move(j.s));
        }
    }
    void noteOnMsg(int n, double v, int m, int inst) {
        if (n < 0 || n > 127) return;
        if (inst >= 0) {
            if (isusSet) isus.erase(std::remove_if(isus.begin(), isus.end(), [&](const SusOff& x) { return x.n == n && x.inst == inst; }), isus.end());
            insOn(inst, n, v); return;
        }
        mask[n] = (uint8_t)(m < 0 ? 7 : m); down[n] = 1; susd[n] = 0; vel[n] = (float)v; trig = true;
    }
    void noteOffMsg(int n, int inst) {
        if (n < 0 || n > 127) return;
        if (inst >= 0) { if (sus != 0) { isus.push_back({n, inst}); isusSet = true; } else insOff(inst, n); return; }
        down[n] = 0; if (sus != 0) susd[n] = 1;
    }
    void apply(const Val& m) {
        const std::string& t = m["t"].str();
        if (t == "all") { const Val& P = m["p"]; for (auto& kv : P.o) setP(kv.first, kv.second); upd(); }
        else if (t == "p") { setP(m["k"].str(), m["v"]); upd(); }
        else if (t == "on" || t == "off") {
            int inst = m["inst"].truthy() ? insOf(m["inst"].str()) : -1;
            if (m["inst"].truthy() && inst < 0) inst = -2; // unknown instrument: JS falls through to the strings
            if (inst == -2) inst = -1;
            if (t == "on") noteOnMsg((int)m["n"].num(), m["v"].num(1), m.has("m") && !m["m"].isNull() ? (int)m["m"].num() : -1, inst);
            else noteOffMsg((int)m["n"].num(), inst);
        }
        else if (t == "sus") setSus(m["v"].num());
        else if (t == "bend") setBend(m["v"].num());
        else if (t == "mod") setMod(m["v"].num());
        else if (t == "panic") panic();
    }
    void setSus(double v) { sus = v; if (v == 0) { std::fill(susd, susd + 128, 0); if (isusSet) { for (auto& x : isus) insOff(x.inst, x.n); isus.clear(); isusSet = false; } } }
    void setBend(double v) { bend = v; p5.p.bend = v; e2.p.bend = v; dx.bend = v; cz.bend = v; sd1.bend = v; cm.bend = v; p5r3.v.bendMul = std::pow(2.0, v / 12); six.m.pitchWheel = std::max(0.0, std::min(100.0, 50 + v * 25)); }
    void setMod(double v) { mod = v; p5.p.wheel = v; dx.wheel = v; cz.wheel = v; six.m.modWheel = v * 100; }
    void panic() {
        for (int i = 0; i < I_N; i++) insAllOff(i); isus.clear(); isusSet = false; q.clear(); std::fill(mask, mask + 128, (uint8_t)7);
        std::fill(down, down + 128, 0); std::fill(susd, susd + 128, 0); std::fill(se, se + 128, 0.f); std::fill(sg, sg + 128, 0.f); env = 0; stage = 0; ar = 0; bEnv = 0; sbsEnv = 0;
    }
    void setP(const std::string& k, const Val& v) {
        if (v.type == Val::Num || v.type == Val::Bool) p[k] = v.num();
        else if (v.isNull()) p.erase(k);
        else if (v.type == Val::Str) p[k] = v.num();
    }
    void upd() {
        const double TP = 2 * PI;
        cresC = std::exp(-1 / (fs * 0.003 * std::pow(1000.0, g("cres"))));
        sustC = std::exp(-1 / (fs * 0.03 * std::pow(266.0, g("sust"))));
        aC = std::exp(-1 / (fs * 0.002 * std::pow(2000.0, g("a"))));
        dC = std::exp(-1 / (fs * 0.005 * std::pow(1600.0, g("d"))));
        rC = std::exp(-1 / (fs * 0.005 * std::pow(1600.0, g("r"))));
        gC = std::exp(-1 / (fs * 0.002));
        fcBase = 30 * std::pow(2.0, g("cut") * 9.5);
        lfoF = 0.3 * std::pow(40.0, g("lrate"));
        auto op = [&](double f) { return 1 - std::exp(-TP * std::min(f, fs * 0.45) / fs); };
        const double br = g("bright");
        cVl = op(1400 + br * 6500); cVh = op(260); cVa = op(800 + br * 2600); cCe = op(500 + br * 1600); cCh = op(55); cSb = op(320);
        cPre = op(9000); cPost = op(g("model") == 1 ? 6200 : 8000);
        cBass = op(380);
        cFx = op(std::min(10500.0, lfs * 0.45));
        { double b = g("bits"); bits = (int)std::lround(b != 0 && !std::isnan(b) ? b : 12); } qs = std::pow(2.0, bits - 1);
        flF = 0.05 * std::pow(100.0, g("flRate"));
        dlMs = (g("dlSync") != 0 && g("dlms") > 0) ? std::min(1000.0, g("dlms")) : 10 * std::pow(100.0, g("dlTime"));
        cDamp = op(std::min(4500.0, lfs * 0.4));
        rvFb = 0.70 + 0.27 * g("rvSize");
    }
    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0 - 0.5; }

    static double Tsat(double x) { return x < -3 ? -1 : (x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x)); }
    static int kf(int n) { const int pc = n % 12; int k = (96 + pc - n) / 12; return k < 1 ? 1 : (k > 7 ? 7 : k); }

    // oL/oR: output; vIn: the vocoder's voice (may be null); N samples. t0 = engine time of the block start (s).
    void process(float* oL, float* oR, int N, double t0, const float* vIn) {
        const double now = t0 > 0 ? t0 : frames / fs;
        if (!q.empty()) {
            const double end = now + N / fs; std::vector<QMsg> due, keep;
            for (auto& e : q) (e.at < end ? due : keep).push_back(e);
            if (!due.empty()) {
                std::stable_sort(due.begin(), due.end(), [](const QMsg& a, const QMsg& b) { if (a.at != b.at) return a.at < b.at; return (!a.on) && b.on; });
                q.swap(keep); for (auto& e : due) { if (e.on) noteOnMsg(e.n, e.v, e.m, e.inst); else noteOffMsg(e.n, e.inst); }
            }
        }
        frames += N;
        p5r3.block(N); six.block(N);
        const double vF = voc.on ? voc.ch : -9; const double modelV = g("model"); const int model = modelV != 0 ? (int)modelV : 2;
        double vln = g("vln"), vla = g("vla"), cel = g("cel"), s16 = (g("s16") != 0 && model == 2) ? 1 : 0, s8 = g("s8"), s4 = g("s4");
        const double sbs = g("sbs"), hol = g("hol"), chs = g("chs");
        if (vF == 0 && vln == 0 && vla == 0 && cel == 0) { vln = 1; vla = 1; } if (vF == 1 && s16 == 0 && s8 == 0 && s4 == 0) s8 = 1;
        const bool vs = g("velSens") != 0;
        int act[128]; int nAct = 0; bool anyHeld = false; int low = -1, lowS = -1; uint8_t pcUse[12] = {};
        for (int n = 0; n < 128; n++) {
            const bool h = down[n] || susd[n];
            if (h) { const int mk = mask[n]; if (mk & 2) anyHeld = true; if ((mk & 4) && ((mk & 8) || n < 60) && low < 0) low = n; if ((mk & 1) && ((mk & 8) || n < 60) && lowS < 0) lowS = n; }
            if (h || se[n] > 1e-5 || sg[n] > 1e-5) { act[nAct++] = n; pcUse[n % 12] = 1; }
        }
        if (low >= 0) { if (!bHeld || low != bNote) { if (!bHeld || trig) bAtt = 1; } bNote = low; bHeld = true; } else bHeld = false;
        if (lowS >= 0) { sbsNote = lowS; sHeld = true; } else sHeld = false;
        if (bNote >= 0) pcUse[bNote % 12] = 1;
        if (sbsNote >= 0) pcUse[sbsNote % 12] = 1;
        if ((anyHeld && !prevHeld) || (trig && g("multi") != 0 && model == 2 && anyHeld)) stage = 1;
        if (!anyHeld && prevHeld) stage = 3;
        prevHeld = anyHeld; trig = false;
        const double sL = g("s"), twoPi = 2 * PI;
        const double tuneV = g("tune"); const double tuneF = std::pow(2.0, ((tuneV != 0 ? tuneV : 0.5) - 0.5) / 12 + bend / 12);
        const double vibD = std::min(1.0, g("vib") + mod) * 0.35 / 12;
        const double stac = g("stac"); const bool bassOn = g("bass") != 0 || vF == 2;
        const double bassLv = vF == 2 ? std::max(0.8, g("bassLv")) : g("bassLv"), strLv = vF == 0 ? std::max(0.6, g("strLv")) : g("strLv"), synLv = vF == 1 ? std::max(0.6, g("synLv")) : g("synLv");
        const double vlfo = g("vlfo"), envA = g("env"), res = g("res");
        const double vol = has("vol") ? g("vol") : 0.8;
        const double chorusSlow = chs != 0 ? 0.55 : 1;
        const double cr[3] = {model == 1 ? 0.92 : 0.78, model == 1 ? 1.84 : 1.57, model == 1 ? 3.6 : 3.1};
        const double cdep[3] = {model == 1 ? 0.0015 : 0.0019, model == 1 ? 0.0009 : 0.001, model == 1 ? 0.00045 : 0.0005};
        const double cbase = model == 1 ? 0.0042 : 0.0048;
        const double L = lfs, q_ = qs;
        const bool fxOn = !has("fxOn") || g("fxOn") != 0; const double fxMix = fxOn ? (has("fxMix") ? g("fxMix") : 0.6) : 0;
        const bool flOn = g("flOn") != 0, dlOn = g("dlOn") != 0, rvOn = g("rvOn") != 0; const double flDep = g("flDep"), flFb = g("flFb"), dlFb = g("dlFb"), dlMix = g("dlMix"), rvMix = g("rvMix");
        auto quant = [&](double x) { return std::floor(x * q_ + 0.5) / q_; }; // JS Math.round
        Mx& MX = mx; double* MG = MX.g; double* MBL = MX.bl; double* MBR = MX.br; double* MSD = MX.s; float* MPK = MX.pk;

        for (int i = 0; i < N; i++) {
            lfo += lfoF / fs; if (lfo >= 1) lfo -= 1;
            const double lv = std::sin(twoPi * lfo);
            const double clk = 1e6 * tuneF * std::pow(2.0, lv * vibD);
            for (int pc = 0; pc < 12; pc++) {
                const double dt = clk / div[pc] / fs; double phv = ph[pc] + dt;
                if (phv >= 1) { phv -= 1; cnt[pc] = (cnt[pc] + 1) & 255; }
                ph[pc] = phv;
                if (!pcUse[pc]) continue;
                const int c = cnt[pc];
                for (int k = 0; k < 9; k++) {
                    const int Nk = 1 << k; const double dk = dt / Nk; const double tt = ((c & (Nk - 1)) + phv) / Nk;
                    double s = tt < 0.5 ? 1 : -1; double u = tt;
                    if (u < dk) { u /= dk; s += u + u - u * u - 1; } else if (u > 1 - dk) { u = (u - 1) / dk; s += u * u + u + u + 1; }
                    u = tt + 0.5; if (u >= 1) u -= 1;
                    if (u < dk) { u /= dk; s -= u + u - u * u - 1; } else if (u > 1 - dk) { u = (u - 1) / dk; s -= u * u + u + u + 1; }
                    sq[k * 12 + pc] = (float)s;
                }
                for (int k = 0; k < 9; k++) {
                    double a = sq[k * 12 + pc] * 0.5, w = 0.25;
                    for (int j = 1; j < 4 && k - j >= 0; j++) { a += sq[(k - j) * 12 + pc] * w; w *= 0.5; }
                    saw[k * 12 + pc] = (float)(a * 1.07);
                }
            }
            double bv = 0, ba = 0, bc = 0, bs = 0;
            for (int ai = 0; ai < nAct; ai++) {
                const int n = act[ai], pc = n % 12, k = kf(n); const bool h = down[n] || susd[n];
                const double vg = vs ? vel[n] : 1;
                double e = se[n]; e = h ? 1 - (1 - e) * cresC : e * sustC; se[n] = (float)e;
                const int mk = mask[n];
                if (e > 1e-5 && (mk & 1)) {
                    const double w8 = saw[k * 12 + pc], w4 = saw[(k - 1) * 12 + pc], w16 = saw[(k + 1) * 12 + pc];
                    if (vln != 0) bv += e * vg * (w8 * 0.7 + w4 * 0.55);
                    if (vla != 0) ba += e * vg * w8;
                    if (cel != 0 && (n < 60 || (mk & 8))) bc += e * vg * (w16 * 0.8 + w8 * 0.35);
                }
                double gg = sg[n]; gg = h ? 1 - (1 - gg) * gC : gg * rC; sg[n] = (float)gg;
                if (gg > 1e-5 && (s16 != 0 || s8 != 0 || s4 != 0) && (mk & 2)) {
                    const float* W = hol != 0 ? sq : saw;
                    bs += gg * vg * (s16 * W[(k + 1) * 12 + pc] + s8 * W[k * 12 + pc] + s4 * W[(k - 1) * 12 + pc]);
                }
            }
            double sbsOut = 0;
            if (sbs != 0 && sbsNote >= 0) {
                sbsEnv = sHeld ? 1 - (1 - sbsEnv) * cresC : sbsEnv * sustC; const double e = sbsEnv;
                const int n = sbsNote, k = kf(n); sbsOut = e * saw[(k + 1) * 12 + n % 12];
            } else sbsEnv *= sustC;
            vl1 += cVl * (bv - vl1); vh1 += cVh * (vl1 - vh1); const double vO = vl1 - vh1;
            va1 += cVa * (ba - va1); va2 += cVa * (va1 - va2);
            ce1 += cCe * (bc - ce1); ce2 += cCh * (ce1 - ce2); const double cO = ce1 - ce2;
            sb1 += cSb * (sbsOut - sb1);
            const double strings = (vO * 0.17 + va2 * 0.19 + cO * 0.2 + sb1 * 0.5) * strLv;
            if (stage == 1) { env += (1.3 - env) * (1 - aC); if (env >= 1) { env = 1; stage = 2; } }
            else if (stage == 2) { env = sL + (env - sL) * dC; }
            else if (stage == 3) { env *= rC; }
            double vca;
            if (model == 1) { ar = anyHeld ? 1 - (1 - ar) * aC : ar * rC; vca = ar; } else vca = env;
            double fc = fcBase * std::pow(2.0, env * envA * 7 + lv * vlfo * 2.5);
            if (fc > fs * 0.42) fc = fs * 0.42;
            const double G = 1 - std::exp(-twoPi * fc / fs), r = res * 4.1;
            const double xin = bs * 0.22;
            const double x = Tsat(xin * (1 + res * 0.6) - r * y4);
            y1 += G * (x - Tsat(y1)); y2 += G * (Tsat(y1) - Tsat(y2)); y3 += G * (Tsat(y2) - Tsat(y3)); y4 += G * (Tsat(y3) - Tsat(y4));
            const double synOut = y4 * vca * synLv * 1.45;
            double bassOut = 0;
            if (bassOn && bNote >= 0) {
                const int n = bNote, k = kf(n); const double src = saw[(k + 1) * 12 + n % 12];
                if (model == 2) {
                    if (bAtt) { bStage = 1; bAtt = 0; }
                    if (bHeld) {
                        if (bStage == 1) { bEnv += (1.2 - bEnv) * (1 - std::exp(-1 / (fs * 0.004))); if (bEnv >= 1) { bEnv = 1; bStage = 2; } }
                        else bEnv = stac != 0 ? bEnv * std::exp(-1 / (fs * 0.16)) : bEnv;
                    } else bEnv *= std::exp(-1 / (fs * 0.11));
                    const double f = std::min(fs * 0.4, 260 + 1300 * bEnv), F = 2 * std::sin(PI * f / fs), Q = 1.15;
                    const double hp = src - bLow - Q * bBand; bBand += F * hp; bLow += F * bBand;
                    bassOut = bLow * bEnv * bassLv * 0.55;
                } else {
                    if (bAtt) bAtt = 0;
                    bLow += cBass * (src - bLow);
                    bassOut = bLow * env * bassLv * 0.6;
                }
            } else bAtt = 0;
            const double cin = strings + (chs != 0 ? synOut * 0.8 : 0);
            cpre1 += cPre * (cin - cpre1); cpre2 += cPre * (cpre1 - cpre2);
            double cx = cpre2; cx = cx / (1 + std::abs(cx) * 0.25);
            cx += rnd() * (model == 1 ? 0.0011 : 0.0006);
            cb[cw] = (float)cx;
            for (int j = 0; j < 3; j++) {
                cph[j] += cr[j] * chorusSlow / fs; if (cph[j] >= 1) cph[j] -= 1;
                cdrift[j] += (rnd() * 0.002 - cdrift[j] * 0.0005);
                const double dly = (cbase + cdep[j] * std::sin(twoPi * cph[j]) + cdep[j] * 0.15 * cdrift[j]) * fs;
                double rp = cw - dly; while (rp < 0) rp += cl;
                const int i0 = (int)rp; const double fr = rp - i0; const int i1 = (i0 + 1) % cl;
                const double v = cb[i0] + (cb[i1] - cb[i0]) * fr;
                cpost[j] = (float)(cpost[j] + cPost * (v - cpost[j])); outs[j] = cpost[j];
            }
            cw = (cw + 1) % cl;
            double mSL = (outs[0] * 0.45 + outs[1] * 0.33 + outs[2] * 0.22) * 1.35 * vol, mSR = (outs[2] * 0.45 + outs[1] * 0.33 + outs[0] * 0.22) * 1.35 * vol;
            double mSy = (chs != 0 ? 0 : synOut) * vol, mBa = bassOut * vol; double* IO = iout; IO[4] = IO[5] = IO[6] = IO[7] = IO[8] = 0;
            for (int j = 0; j < I_N; j++) if (insActive(j)) IO[insCh[j]] += insTick(j) * vol * insG[j];
            const double mPL = 0, mPR = 0;
            double vcL = 0, vcR = 0; int vcCh = -2;
            if (voc.on) {
                double vv = vIn ? vIn[i] : 0; if (!(vv == vv && std::abs(vv) < 8)) vv = 0; const int ch = voc.ch; double car;
                bool have = (voc.eng == "svc" && voc.svc) || (voc.eng == "talker" && voc.talker) || (voc.eng != "svc" && voc.eng != "talker" && voc.clear);
                if (have) {
                    if (ch < 0) { car = (mSL + mSR) * 0.5 + mSy + mBa; mSL = mSR = mSy = mBa = 0; } else if (ch == 0) { car = (mSL + mSR) * 0.5; mSL = mSR = 0; } else if (ch == 1) { car = mSy; mSy = 0; } else if (ch == 2) { car = mBa; mBa = 0; } else if (ch < 9) { car = IO[ch]; IO[ch] = 0; } else car = 0;
                    if (voc.eng == "talker") { voc.talker->process(vv, car * 2); vcL = vcR = voc.talker->L * 0.35; }
                    else if (voc.eng == "svc") { voc.svc->process(vv, car); vcL = voc.svc->L; vcR = voc.svc->R; }
                    else { voc.clear->process(vv, car); vcL = voc.clear->L; vcR = voc.clear->R; }
                    vcCh = ch < 0 ? 0 : std::min(ch, 8);
                }
            }
            const double mAL = mSL * MG[0] * MBL[0], mAR = mSR * MG[0] * MBR[0], mBL = mSy * MG[1] * MBL[1], mBR = mSy * MG[1] * MBR[1], mCL = mBa * MG[2] * MBL[2], mCR = mBa * MG[2] * MBR[2], mEL = mPL * MG[3] * MBL[3], mER = mPR * MG[3] * MBR[3];
            double mFx = (mSL + mSR) * 0.5 * MG[0] * MSD[0] + mSy * MG[1] * MSD[1] + mBa * MG[2] * MSD[2] + (mPL + mPR) * 0.5 * MG[3] * MSD[3];
            double dL = mAL + mBL + mCL + mEL, dR = mAR + mBR + mCR + mER;
            for (int j = 4; j < 9; j++) { const double xx = IO[j]; if (xx == 0) continue; const double gg = xx * MG[j]; dL += gg * MBL[j]; dR += gg * MBR[j]; mFx += gg * MSD[j]; const double a = std::abs(gg); if (a > MPK[j]) MPK[j] = (float)a; }
            if (vcCh > -2) { const double gg = MG[vcCh], a = vcL * gg * MBL[vcCh], b = vcR * gg * MBR[vcCh]; dL += a; dR += b; mFx += (vcL + vcR) * 0.5 * gg * MSD[vcCh]; const double pk = std::max(std::abs(a), std::abs(b)); if (pk > MPK[vcCh]) MPK[vcCh] = (float)pk; }
            double mv;
            mv = std::max(std::abs(mAL), std::abs(mAR)); if (mv > MPK[0]) MPK[0] = (float)mv; mv = std::abs(mSy * MG[1]); if (mv > MPK[1]) MPK[1] = (float)mv; mv = std::abs(mBa * MG[2]); if (mv > MPK[2]) MPK[2] = (float)mv; mv = std::max(std::abs(mEL), std::abs(mER)); if (mv > MPK[3]) MPK[3] = (float)mv;
            double yL = dL, yR = dR;
            if (fxMix > 0) {
                fpre += cFx * (mFx - fpre);
                acc += fpre; dcount++;
                if (dcount >= D) {
                    double xx = acc / D; acc = 0; dcount = 0; double wl = 0, wr = 0;
                    xx = quant(std::max(-1.0, std::min(1.0, xx * 0.9)));
                    if (flOn) {
                        flp += flF / L; if (flp >= 1) flp -= 1;
                        const double tri = flp < 0.5 ? flp * 4 - 1 : 3 - flp * 4;
                        const double d = (0.00025 + flDep * 0.006 * (0.5 + 0.5 * tri)) * L;
                        const int n = (int)flb.size(); double rp = flw - d; while (rp < 0) rp += n;
                        const int i0 = (int)rp; const double fr = rp - i0; const double v = flb[i0] + (flb[(i0 + 1) % n] - flb[i0]) * fr;
                        const double fb = flFb * 0.92;
                        flb[flw] = (float)quant(std::max(-1.0, std::min(1.0, xx + v * fb))); flw = (flw + 1) % n;
                        wl += v * 0.8; wr += v * 0.8; xx = (xx + v) * 0.62;
                    }
                    double xl = xx, xr = xx;
                    if (dlOn) {
                        const int n = (int)dlb.size(); const double d = dlMs * 0.001 * L;
                        double rp = dlw - d; while (rp < 0) rp += n; const int i0 = (int)rp; const double v = dlb[i0];
                        double rp2 = rp - 0.007 * L; while (rp2 < 0) rp2 += n; const double v2 = dlb[(int)rp2];
                        dld += cDamp * (v - dld);
                        dlb[dlw] = (float)quant(std::max(-1.0, std::min(1.0, xx + dld * dlFb * 0.95))); dlw = (dlw + 1) % n;
                        const double m = dlMix; xl = xx + v * m; xr = xx + v2 * m; wl += v * m; wr += v2 * m;
                    }
                    if (rvOn) {
                        const double inp = (xl + xr) * 0.5 * 0.3; double rl = 0, rr = 0; const double fb = rvFb, dm = 0.3;
                        for (int j = 0; j < 4; j++) {
                            Comb* c = &cmbL[j]; double o = c->b[c->i]; c->f = o * (1 - dm) + c->f * dm; c->b[c->i] = (float)quant(inp + c->f * fb); c->i = (c->i + 1) % (int)c->b.size(); rl += o;
                            c = &cmbR[j]; o = c->b[c->i]; c->f = o * (1 - dm) + c->f * dm; c->b[c->i] = (float)quant(inp + c->f * fb); c->i = (c->i + 1) % (int)c->b.size(); rr += o;
                        }
                        for (int j = 0; j < 2; j++) {
                            Ap* a = &apL[j]; double bo = a->b[a->i]; double w = rl + bo * 0.5; a->b[a->i] = (float)w; a->i = (a->i + 1) % (int)a->b.size(); rl = bo - w * 0.5;
                            a = &apR[j]; bo = a->b[a->i]; w = rr + bo * 0.5; a->b[a->i] = (float)w; a->i = (a->i + 1) % (int)a->b.size(); rr = bo - w * 0.5;
                        }
                        const double m = rvMix * 1.6; xl += rl * m; xr += rr * m; wl += rl * m; wr += rr * m;
                    }
                    fxL = quant(std::max(-1.0, std::min(1.0, wl))) / 0.9;
                    fxR = quant(std::max(-1.0, std::min(1.0, wr))) / 0.9;
                }
                fpostL += cFx * (fxL - fpostL); fpostL2 += cFx * (fpostL - fpostL2);
                fpostR += cFx * (fxR - fpostR); fpostR2 += cFx * (fpostR - fpostR2);
                const double rg = fxMix * MX.bus, rL = fpostL2 * rg * MX.busL, rR = fpostR2 * rg * MX.busR;
                yL = dL + rL; yR = dR + rR; mv = std::max(std::abs(rL), std::abs(rR)); if (mv > MPK[11]) MPK[11] = (float)mv;
            }
            yL *= MX.master; yR *= MX.master;
            yL = yL / (1 + std::abs(yL) * 0.3); yR = yR / (1 + std::abs(yR) * 0.3);
            mv = std::abs(yL); if (mv > MPK[9]) MPK[9] = (float)mv; mv = std::abs(yR); if (mv > MPK[10]) MPK[10] = (float)mv;
            oL[i] = (float)yL; oR[i] = (float)yR;
            scope[si++] = (float)yL;
            if (si >= 1024) { si = 0; postScope(); std::fill(MPK, MPK + 12, 0.f); }
        }
    }
    // the page's LCD scope, meters and firmware displays (about every 21 ms at 48 kHz)
    std::function<void(const float* scope, const float* peaks)> onScope; std::string stateJson;
    void postScope() {
        if (onScope) {
            stateJson.clear();
            if (++stN >= 4) { stN = 0; JsonOut j; j.s.swap(stateJson); j.raw("{"); if (p5r3.loaded) { j.key("p5r3"); p5r3.state(j); } if (six.loaded) { j.key("six"); six.state(j); }
                if (p5r3.m.nvDirty || six.m.nvDirty) { j.key("nvDirty"); j.raw("1"); } j.raw("}"); stateJson.swap(j.s); }
            onScope(scope, mx.pk); return;
        }
        if (!post) return;
        JsonOut j; j.s.reserve(9000); j.raw("{\"t\":\"scope\",\"d\":[");
        for (int k = 0; k < 1024; k += 2) { if (k) j.raw(","); j.num(scope[k]); j.raw(","); j.num(scope[k + 1]); }
        j.raw("],\"m\":["); for (int k = 0; k < 12; k++) { if (k) j.raw(","); j.num(mx.pk[k]); } j.raw("]");
        j.key("st");
        if (++stN >= 4) {
            stN = 0; j.raw("{");
            bool any = false;
            if (p5r3.loaded) { j.key("p5r3"); p5r3.state(j); any = true; }
            if (six.loaded) { j.key("six"); six.state(j); any = true; }
            if (p5r3.m.nvDirty || six.m.nvDirty) { j.key("nvDirty"); j.raw("1"); }
            (void)any; j.raw("}");
        } else j.raw("null");
        j.raw("}"); post(std::move(j.s));
    }
};

} // namespace farp
