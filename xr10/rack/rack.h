// Omega XR10: the fold-out's effects rack. Four slots of outboard units fed from the XR10's EFFECT jack, the way the
// machine is meant to be used (it has no internal effect). The units are the Omega System's circuit-modelled rack
// (plugins/dsp: Lexicon 224 / AMS RMX16, PrimeTime, Effectron, DP4, Microverb, ADA Flanger, 1176, gated-drum sim,
// H910, MXR 113, SSL-style bus compressor), the same code as their own plugins.
// A slot either takes the chain so far (series) or the send itself (parallel, summed). The rack's output comes back
// into the mix through RETURN.
#pragma once
#include "../../plugins/dsp/Rack.h"
#include "../../plugins/dsp/Inserts.h"
#include "../../plugins/src/Specs.h"
#include <cstdio>
#include <cstring>

namespace xrack {
using namespace omega;

struct UnitType { const char* code; const char* name; Spec (*spec)(); };

inline Spec withMix(Spec s, double def) { s.knob("omix", "Dry/Wet", 0, 1, .01, def, Fmt::pct); return s; }
inline const UnitType* unitTypes(int& n) {
    static const UnitType T[] = {
        {"lx", "Lexicon 224 / AMS RMX16 (reverb)", [] { Spec s; gen::rack_lx(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::Verb224(p)); }; return s; }},
        {"mv", "Alesis Microverb (reverb)", [] { Spec s; gen::rack_mv(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::Microverb(p)); }; return s; }},
        {"dp", "Ensoniq DP4 (multi-effect)", [] { Spec s; gen::rack_dp(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::DP4(p)); }; return s; }},
        {"pc", "Gated drum room (In the Air Tonight style)", [] { Spec s; gen::rack_pc(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::PCSim(p)); }; return s; }},
        {"pt", "Lexicon PrimeTime (delay)", [] { Spec s; gen::rack_pt(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::PrimeTime(p)); }; return s; }},
        {"ef", "Deltalab Effectron (delay, flange)", [] { Spec s; gen::rack_ef(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::Effectron(p)); }; return s; }},
        {"h9", "Eventide H910 (harmonizer)", [] { Spec s; gen::rack_h9(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::H910(p)); }; return s; }},
        {"ad", "A/DA Flanger", [] { Spec s; gen::rack_ad(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::ADAFlanger(p)); }; return s; }},
        {"mx", "MXR 113 (delay)", [] { Spec s; gen::rack_mx(s); s = withMix(s, 1); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::MXR113(p)); }; return s; }},
        {"u7", "UREI 1176 (compressor)", [] { Spec s; gen::rack_u7(s); s.remove("ret"); s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::UREI1176(p)); }; return s; }},
        {"bc", "SSL-style bus compressor", [] {
            Spec s; s.name = "Bus compressor";
            s.knob("thr", "Threshold", -30, 0, .5, -12, Fmt::db1);
            s.choice("ratio", "Ratio", {{"2:1", 2}, {"4:1", 4}, {"10:1", 10}}, 1);
            s.choice("atk", "Attack", {{"0.1 ms", 0}, {"0.3 ms", 1}, {"1 ms", 2}, {"3 ms", 3}, {"10 ms", 4}, {"30 ms", 5}}, 3);
            s.choice("rel", "Release", {{"0.1 s", 0}, {"0.3 s", 1}, {"0.6 s", 2}, {"1.2 s", 3}, {"Auto", 4}}, 4);
            s.knob("makeup", "Make-up", 0, 15, .5, 0, Fmt::db1);
            s.choice("hp", "S/C HPF", {{"Off", 0}, {"30 Hz", 30}, {"60 Hz", 60}, {"90 Hz", 90}, {"120 Hz", 120}, {"150 Hz", 150}}, 0);
            s.knob("mix", "Mix", 0, 1, .05, 1, Fmt::pct);
            s.make = [](const Spec& p) { return std::unique_ptr<Engine>(new dsp::SSLBus(p)); };
            return s; }},
    };
    n = (int) (sizeof T / sizeof T[0]);
    return T;
}

struct Slot {
    int type = -1;
    bool bypass = false, parallel = false;
    std::unique_ptr<Spec> spec;
    std::unique_ptr<Engine> eng;
    std::vector<double> vals;
    float peak = 0;
};

class Rack {
public:
    static const int NSLOT = 4;
    Slot slot[NSLOT];
    double fs = 48000, bpm = 120;
    float ret = 0.8f;          // RETURN level into the mix
    float peakIn = 0, peakOut = 0;
    int maxBlock = 2048;

    void prepare(double rate, int block) {
        fs = rate; maxBlock = block > 0 ? block : 2048;
        a.assign((size_t) maxBlock, 0.f); b.assign((size_t) maxBlock, 0.f); cL.assign((size_t) maxBlock, 0.f); cR.assign((size_t) maxBlock, 0.f);
        oL.assign((size_t) maxBlock, 0.f); oR.assign((size_t) maxBlock, 0.f); sL.assign((size_t) maxBlock, 0.f); sR.assign((size_t) maxBlock, 0.f);
        for (auto& s : slot) if (s.eng) { s.eng->prepare(fs, maxBlock); s.eng->setValues(s.vals); }
    }
    void defaults() {   // a room reverb and a delay to start with
        for (int i = 0; i < NSLOT; i++) setType(i, -1);
        setType(0, 0); setType(1, 4); slot[1].parallel = true;
        setParamById(1, "omix", 0.6);
        ret = 0.8f;
    }
    void setType(int i, int t) {
        if (i < 0 || i >= NSLOT) return;
        int n; const UnitType* T = unitTypes(n);
        Slot& s = slot[i];
        s.type = t >= 0 && t < n ? t : -1;
        s.eng.reset(); s.spec.reset(); s.vals.clear();
        if (s.type < 0) return;
        s.spec.reset(new Spec(T[s.type].spec()));
        s.eng = s.spec->make(*s.spec);
        for (auto& p : s.spec->params) s.vals.push_back(p.choices.empty() ? p.def : p.choices[(size_t) p.defChoice].value);
        s.eng->setValues(s.vals);
        s.eng->prepare(fs, maxBlock);
        s.eng->setValues(s.vals);
    }
    void setParam(int i, int p, double v) {
        if (i < 0 || i >= NSLOT || !slot[i].eng || p < 0 || p >= (int) slot[i].vals.size()) return;
        const ParamDef& d = slot[i].spec->params[(size_t) p];
        if (d.choices.empty()) v = v < d.min ? d.min : (v > d.max ? d.max : v);
        slot[i].vals[(size_t) p] = v;
        slot[i].eng->setValues(slot[i].vals);
    }
    void setParamById(int i, const char* id, double v) { if (slot[i].spec) setParam(i, slot[i].spec->index(id), v); }
    void applyPreset(int i, int k) {
        if (i < 0 || i >= NSLOT || !slot[i].spec || k < 0 || k >= (int) slot[i].spec->presets.size()) return;
        for (auto& pv : slot[i].spec->presets[(size_t) k].values) setParamById(i, pv.first.c_str(), pv.second);
    }

    // send in (the EFFECT jack, mono duplicated), returns added into L / R
    void process(const float* send, float* L, float* R, int n) {
        while (n > 0) {
            const int c = n > maxBlock ? maxBlock : n;
            block(send, L, R, c);
            send += c; L += c; R += c; n -= c;
        }
    }

    // ---- description for the UI and the saved state (plain text, one line per item) ----
    std::string describe(int i) const {   // name|n then per param: id|name|min|max|step|value|choice;choice...
        std::string o;
        if (i < 0 || i >= NSLOT || !slot[i].spec) return "none\n";
        const Spec& s = *slot[i].spec;
        o += s.name + "\n";
        for (size_t k = 0; k < s.params.size(); k++) {
            const ParamDef& p = s.params[k];
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s|%s|%g|%g|%g|%g|", p.id.c_str(), p.name.c_str(), p.min, p.max, p.step, slot[i].vals[k]);
            o += buf;
            for (size_t c = 0; c < p.choices.size(); c++) { std::snprintf(buf, sizeof buf, "%s%s=%g", c ? ";" : "", p.choices[c].label.c_str(), p.choices[c].value); o += buf; }
            o += "\n";
        }
        o += "presets";
        for (auto& pr : s.presets) { o += "|"; o += pr.name; }
        o += "\n";
        return o;
    }
    std::string save() const {
        std::string o = "XRRK1\n";
        char buf[96];
        std::snprintf(buf, sizeof buf, "ret %g\n", (double) ret); o += buf;
        int n; const UnitType* T = unitTypes(n);
        for (int i = 0; i < NSLOT; i++) {
            const Slot& s = slot[i];
            std::snprintf(buf, sizeof buf, "slot %d %s %d %d", i, s.type >= 0 ? T[s.type].code : "-", (int) s.bypass, (int) s.parallel); o += buf;
            if (s.spec) for (size_t k = 0; k < s.vals.size(); k++) { o += " "; o += s.spec->params[k].id; std::snprintf(buf, sizeof buf, "=%g", s.vals[k]); o += buf; }
            o += "\n";
        }
        return o;
    }
    bool load(const char* text, size_t len) {
        std::string t(text, len);
        if (t.compare(0, 6, "XRRK1\n") != 0) return false;
        int n; const UnitType* T = unitTypes(n);
        size_t pos = 6;
        while (pos < t.size()) {
            size_t e = t.find('\n', pos); if (e == std::string::npos) e = t.size();
            std::string line = t.substr(pos, e - pos); pos = e + 1;
            if (line.compare(0, 4, "ret ") == 0) { ret = (float) std::atof(line.c_str() + 4); continue; }
            if (line.compare(0, 5, "slot ") != 0) continue;
            char code[8] = {0}; int i = -1, by = 0, par = 0, used = 0;
            if (std::sscanf(line.c_str(), "slot %d %7s %d %d%n", &i, code, &by, &par, &used) < 4 || i < 0 || i >= NSLOT) continue;
            int ty = -1; for (int k = 0; k < n; k++) if (!std::strcmp(code, T[k].code)) ty = k;
            setType(i, ty); slot[i].bypass = by != 0; slot[i].parallel = par != 0;
            std::string rest = line.substr((size_t) used);
            size_t q = 0;
            while (q < rest.size()) {
                while (q < rest.size() && rest[q] == ' ') q++;
                size_t sp = rest.find(' ', q); if (sp == std::string::npos) sp = rest.size();
                std::string kv = rest.substr(q, sp - q); q = sp;
                size_t eq = kv.find('='); if (eq == std::string::npos) continue;
                setParamById(i, kv.substr(0, eq).c_str(), std::atof(kv.c_str() + eq + 1));
            }
        }
        return true;
    }

private:
    std::vector<float> a, b, cL, cR, oL, oR, sL, sR;
    void block(const float* send, float* L, float* R, int n) {
        Transport tr; tr.bpm = bpm;
        float pin = 0;
        for (int k = 0; k < n; k++) { sL[(size_t) k] = sR[(size_t) k] = send[k]; cL[(size_t) k] = cR[(size_t) k] = send[k]; oL[(size_t) k] = oR[(size_t) k] = 0; pin = std::max(pin, std::fabs(send[k])); }
        peakIn = std::max(pin, peakIn * 0.9f);
        bool chainUsed = false;
        for (auto& s : slot) {
            if (!s.eng) continue;
            if (s.bypass) { s.peak *= 0.9f; continue; }
            const float* inL = s.parallel ? sL.data() : cL.data();
            const float* inR = s.parallel ? sR.data() : cR.data();
            s.eng->process(inL, inR, nullptr, nullptr, a.data(), b.data(), n, tr);
            float pk = 0;
            for (int k = 0; k < n; k++) pk = std::max(pk, std::max(std::fabs(a[(size_t) k]), std::fabs(b[(size_t) k])));
            s.peak = std::max(pk, s.peak * 0.9f);
            if (s.parallel) for (int k = 0; k < n; k++) { oL[(size_t) k] += a[(size_t) k]; oR[(size_t) k] += b[(size_t) k]; }
            else { for (int k = 0; k < n; k++) { cL[(size_t) k] = a[(size_t) k]; cR[(size_t) k] = b[(size_t) k]; } chainUsed = true; }
        }
        float pout = 0;
        for (int k = 0; k < n; k++) {
            float l = oL[(size_t) k] + (chainUsed ? cL[(size_t) k] : 0.f), r = oR[(size_t) k] + (chainUsed ? cR[(size_t) k] : 0.f);
            l *= ret; r *= ret;
            L[k] += l; R[k] += r;
            pout = std::max(pout, std::max(std::fabs(l), std::fabs(r)));
        }
        peakOut = std::max(pout, peakOut * 0.9f);
    }
};
}  // namespace xrack
