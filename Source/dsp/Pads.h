// The 16-pad sampler: the page's Sampler.trigger / Sampler.hit (trims, tune, decay, fades, filter, pan, normalise,
// reverse, hat choke), its SP-1200 (26.04 kHz 12-bit, channel filters), MPC60 and W-30 modes and 16 Levels.
// The page's sounds play through WebAudio nodes there; here each hit is a voice that does what those nodes do.
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include "Val.h"

namespace farp {

struct PadBuf {
    double sr = 48000; std::vector<std::vector<float>> ch; size_t len = 0; double peak = 1e-4; uint64_t id = 0;
    double duration() const { return len / sr; }
};

class PadSampler {
public:
    struct Pad { double level = 80, tune = 0, fine = 0, decay = 100, pan = 0, cut = 100, start = 0, end = 1; int rev = 0, norm = 0; double fi = 0, fo = 0; int ch = -1; };
    struct X { bool has = false; bool hTune = false, hDecay = false, hCut = false, hSlice = false, hVel = false; double tune = 0, decay = 0, cut = 0, vel = 0; int slice = 0; };
    double fs;
    std::shared_ptr<PadBuf> buf[16];
    Pad pads[16];
    struct { bool on = false; std::string type = "tune"; int src = 0; bool full = false; } lvx;
    struct { bool on = false; std::string mpc; double fc[2] = { 62, 70 }, fr[2] = { 30, 20 }; } spx;
    static constexpr double SP_FS = 26040, SP_MAX = 2.5;

    explicit PadSampler (double fs_) : fs (fs_) { voices.resize (48); }

    // ---- settings from the page ----
    void setPads (const Val& m) {
        const Val& P = m["pads"];
        for (size_t i = 0; i < 16 && i < P.size(); i++) {
            const Val& p = P[i]; Pad& d = pads[i];
            d.level = p["level"].num (80); d.tune = p["tune"].num(); d.fine = p["fine"].num(); d.decay = p["decay"].num (100); d.pan = p["pan"].num(); d.cut = p["cut"].num (100);
            d.start = p["start"].num(); d.end = p["end"].num (1); d.rev = p["rev"].truthy(); d.norm = p["norm"].truthy(); d.fi = p["fi"].num(); d.fo = p["fo"].num(); d.ch = p.has ("ch") ? (int) p["ch"].num (-1) : -1;
        }
        const Val& S = m["spx"];
        const bool spOn = S["on"].truthy(); const std::string mpc = S["mpc"].isStr() ? S["mpc"].str() : (S["mpc"].truthy() ? std::string ("mpc") : std::string());
        if (spOn != spx.on || mpc != spx.mpc) { spCache.clear(); }
        spx.on = spOn; spx.mpc = mpc;
        for (int c = 0; c < 2; c++) { const Val& f = S["f"][(size_t) c]; if (f.isObj()) { if (f["c"].num() != spx.fc[c] || f["r"].num() != spx.fr[c]) spCache.clear(); spx.fc[c] = f["c"].num(); spx.fr[c] = f["r"].num(); } }
        const Val& L = m["lvx"];
        lvx.on = L["on"].truthy(); lvx.type = L["type"].isStr() ? L["type"].str() : std::string ("tune"); lvx.src = (int) L["src"].num(); lvx.full = L["full"].truthy();
    }
    void setBuf (int i, const Val& m) {
        if (i < 0 || i > 15) return;
        auto b = std::make_shared<PadBuf>(); b->sr = m["fs"].num (48000); b->id = ++nextId;
        const Val& C = m["ch"];
        for (size_t c = 0; c < C.size() && c < 2; c++) if (C[c].type == Val::F32 && C[c].f) b->ch.push_back (*C[c].f);
        if (b->ch.empty()) { buf[i].reset(); return; }
        b->len = b->ch[0].size(); for (auto& c : b->ch) b->len = std::min (b->len, c.size());
        double p = 0; for (auto& c : b->ch) for (size_t k = 0; k < b->len; k++) p = std::max (p, (double) std::abs (c[k])); b->peak = std::max (p, 1e-4);
        buf[i] = b; spCache.clear();
    }

    int padCh (int i) const { static const int CHMAP[16] = { 0, 1, 2, 6, 6, 3, 3, 4, 5, 7, 7, 7, 5, 4, 7, 5 }; return pads[i].ch >= 0 ? pads[i].ch : CHMAP[i]; }
    X levelX (int k) const {
        X x; x.has = true;
        if (lvx.type == "tune") { x.hTune = true; x.tune = k - 8; }
        else if (lvx.type == "vel") { x.hVel = true; x.vel = (k + 1) / 16.0; }
        else if (lvx.type == "decay") { x.hDecay = true; x.decay = std::floor ((k + 1) / 16.0 * 100 + 0.5); }
        else if (lvx.type == "cut") { x.hCut = true; x.cut = std::floor (15 + 85 * k / 15.0 + 0.5); }
        else if (lvx.type == "slice") { x.hSlice = true; x.slice = k; }
        return x;
    }
    static X xFrom (const Val& v) {
        X x; if (! v.isObj()) return x; x.has = true;
        if (v.has ("tune")) { x.hTune = true; x.tune = v["tune"].num(); }
        if (v.has ("decay")) { x.hDecay = true; x.decay = v["decay"].num(); }
        if (v.has ("cut")) { x.hCut = true; x.cut = v["cut"].num(); }
        if (v.has ("slice")) { x.hSlice = true; x.slice = (int) v["slice"].num(); }
        if (v.has ("vel")) { x.hVel = true; x.vel = v["vel"].num(); }
        return x;
    }
    // a pad from MIDI: 16 Levels spreads the source pad across all 16
    int hit (int k, double vel, double at, double now) {
        int i = k; X x; if (lvx.on) { i = lvx.src; x = levelX (k); }
        trigger (i, vel, at, x, now);
        return lvx.on ? k : i;
    }

    void trigger (int i, double vel, double at, const X& x, double now) {
        if (i < 0 || i > 15) return;
        const auto raw = buf[i]; if (! raw || raw->len < 2) return;
        const Pad& pd = pads[i]; const double t = std::max (at, now);
        if (lvx.full) vel = 1; if (x.hVel) vel = x.vel;
        const double tune = pd.tune + pd.fine / 100 + (x.hTune ? x.tune : 0), decay = x.hDecay ? x.decay : pd.decay, cut = x.hCut ? x.cut : pd.cut;
        double st = std::min (pd.start, pd.end - 0.001), en = pd.end; if (x.hSlice) { const double w = (en - st) / 16; st = st + w * x.slice; en = st + w; }
        const double rate = std::pow (2.0, tune / 12);
        Voice v; double dur, off = 0, len;
        if (spx.on) {
            v.src = spRender (i, raw, pd, st, en, rate); v.step = 1; v.rev = false; v.pos = 0; dur = v.src->duration(); len = dur; v.endPos = (double) v.src->len;
        } else {
            std::shared_ptr<PadBuf> rb = spx.mpc.empty() ? raw : lofi (raw, spx.mpc == "w30");
            const double L = rb->duration(); off = pd.rev ? (1 - en) * L : st * L; len = (en - st) * L; dur = len / rate;
            v.src = rb; v.rev = pd.rev; v.step = rate * rb->sr / fs; v.pos = off * rb->sr; v.endPos = (off + len) * rb->sr;
        }
        const double lvl = (pd.level / 100) * (0.25 + 0.75 * vel) * (pd.norm ? std::min (8.0, 0.95 / raw->peak) : 1);
        v.t0 = t; v.lvl = lvl; v.fi = pd.fi / 100 * dur; v.fo = pd.fo / 100 * dur; v.dur = dur;
        v.decayOn = decay < 100; if (v.decayOn) { const double hold = dur * decay / 100; v.d0 = std::max (v.fi, hold * 0.5); v.tau = std::max (0.005, hold * 0.18); }
        v.filter = ! spx.on && cut < 100;
        if (v.filter) { // WebAudio low-pass biquad, Q 0.8 dB
            const double f0 = 40 * std::pow (2.0, cut / 100 * 9), w = 2 * 3.14159265358979323846 * std::min (f0, fs * 0.5) / fs, Qlin = std::pow (10.0, 0.8 / 20), al = std::sin (w) / (2 * Qlin), cw = std::cos (w), a0 = 1 + al;
            v.b0 = (1 - cw) / 2 / a0; v.b1 = (1 - cw) / a0; v.b2 = v.b0; v.a1 = -2 * cw / a0; v.a2 = (1 - al) / a0;
        }
        const double pan = std::max (-1.0, std::min (1.0, pd.pan / 100));
        v.pan = pan; v.stereo = v.src->ch.size() > 1;
        v.choke = spx.on ? padCh (i) : ((i == 3 || i == 4) ? 100 : -1);
        if (v.choke >= 0) for (auto& o : voices) if (o.on && o.choke == v.choke && o.chokeAt < 0) { o.chokeAt = t; }
        v.on = true; v.age = ++counter;
        Voice* slot = nullptr; for (auto& o : voices) if (! o.on) { slot = &o; break; }
        if (! slot) { slot = &voices[0]; for (auto& o : voices) if (o.age < slot->age) slot = &o; }
        *slot = v;
    }

    // adds the pads into L/R (the pads' mixer channel input); t0 = time of the first sample
    void render (float* L, float* R, int n, double t0) {
        const double dt = 1 / fs;
        for (auto& v : voices) {
            if (! v.on) continue;
            const PadBuf& b = *v.src; const float* c0 = b.ch[0].data(); const float* c1 = v.stereo ? b.ch[1].data() : c0;
            for (int k = 0; k < n; k++) {
                const double t = t0 + k * dt; if (t < v.t0) continue;
                const double tau = t - v.t0;
                if (v.chokeAt >= 0 && t >= v.chokeAt + 0.04) { v.on = false; break; }
                if (v.pos >= v.endPos || v.pos < 0) { v.on = false; break; }
                // the sample, linear interpolation (reverse reads the reversed buffer)
                const double p = v.pos; const size_t i0 = (size_t) p; const double fr = p - (double) i0; double sL, sR;
                auto at = [&] (const float* c, size_t j) -> double { if (j >= b.len) return 0; return v.rev ? c[b.len - 1 - j] : c[j]; };
                sL = at (c0, i0) + (at (c0, i0 + 1) - at (c0, i0)) * fr; sR = v.stereo ? at (c1, i0) + (at (c1, i0 + 1) - at (c1, i0)) * fr : sL;
                v.pos += v.step;
                if (v.filter) { double y = v.b0 * sL + v.z1; v.z1 = v.b1 * sL - v.a1 * y + v.z2; v.z2 = v.b2 * sL - v.a2 * y; sL = y;
                    if (v.stereo) { y = v.b0 * sR + v.y1; v.y1 = v.b1 * sR - v.a1 * y + v.y2; v.y2 = v.b2 * sR - v.a2 * y; sR = y; } else sR = sL; }
                // the gain automation the page schedules
                double g;
                if (v.fi > 0.0005 && tau < v.fi) g = v.lvl * tau / v.fi; else g = v.lvl;
                if (v.decayOn) { if (tau >= v.d0) g = v.lvl * std::exp (-(tau - v.d0) / v.tau); }
                else if (v.fo > 0.0005) { const double f0 = std::max (v.fi, v.dur - v.fo); if (tau >= f0) g = v.lvl * std::max (0.0, 1 - (tau - f0) / std::max (1e-9, v.dur - f0)); }
                if (v.chokeAt >= 0 && t >= v.chokeAt) { if (v.chokeG < 0) v.chokeG = g; v.chokeG *= std::exp (-dt / 0.004); g = v.chokeG; }
                // stereo panner (equal power), into the pad bus (gain 1.2)
                double l, r;
                if (! v.stereo) { const double xp = (v.pan + 1) / 2; l = sL * std::cos (xp * 3.14159265358979323846 / 2); r = sL * std::sin (xp * 3.14159265358979323846 / 2); }
                else if (v.pan <= 0) { const double xp = v.pan + 1; l = sL + sR * std::cos (xp * 3.14159265358979323846 / 2); r = sR * std::sin (xp * 3.14159265358979323846 / 2); }
                else { const double xp = v.pan; l = sL * std::cos (xp * 3.14159265358979323846 / 2); r = sR + sL * std::sin (xp * 3.14159265358979323846 / 2); }
                L[k] += (float) (l * g * 1.2); R[k] += (float) (r * g * 1.2);
            }
        }
    }
    void allOff() { for (auto& v : voices) v.on = false; }

private:
    struct Voice {
        bool on = false; int age = 0; std::shared_ptr<PadBuf> src; double pos = 0, step = 1, endPos = 0; bool rev = false, stereo = false;
        double t0 = 0, lvl = 0, fi = 0, fo = 0, dur = 0, d0 = 0, tau = 1; bool decayOn = false; double pan = 0;
        bool filter = false; double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0, y1 = 0, y2 = 0;
        int choke = -1; double chokeAt = -1, chokeG = -1;
    };
    std::vector<Voice> voices; int counter = 0; uint64_t nextId = 0;
    std::map<std::string, std::shared_ptr<PadBuf>> spCache, lofiCache; std::map<uint64_t, std::shared_ptr<std::vector<float>>> spSrc;

    static double jr (double x) { return std::floor (x + 0.5); }
    // MPC60 / W-30: their anti-alias filter, resampled to 40 / 30 kHz, 12-bit
    std::shared_ptr<PadBuf> lofi (const std::shared_ptr<PadBuf>& b, bool w30) {
        const std::string key = std::to_string (b->id) + (w30 ? "w" : "m");
        auto it = lofiCache.find (key); if (it != lofiCache.end()) return it->second;
        if (lofiCache.size() > 40) lofiCache.clear();
        const double FS = w30 ? 30000 : 40000, FC = w30 ? 13000 : 16000, sr = b->sr; const size_t n = std::max<size_t> (1, (size_t) std::floor (b->len * FS / sr));
        auto o = std::make_shared<PadBuf>(); o->sr = FS; o->len = n; o->id = ++nextId; o->peak = b->peak;
        const double w = 2 * 3.14159265358979323846 * std::min (FC, 0.45 * sr) / sr, al = std::sin (w) / (2 * 0.707), a0 = 1 + al, c = std::cos (w);
        const double B0 = (1 - c) / 2 / a0, B1 = (1 - c) / a0, B2 = (1 - c) / 2 / a0, A1 = -2 * c / a0, A2 = (1 - al) / a0;
        for (auto& d : b->ch) {
            std::vector<float> tmp (b->len); double x1 = 0, x2 = 0, y1 = 0, y2 = 0, X1 = 0, X2 = 0, Y1 = 0, Y2 = 0;
            for (size_t i = 0; i < b->len; i++) {
                const double x = d[i]; const double y = B0 * x + B1 * x1 + B2 * x2 - A1 * y1 - A2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y;
                const double z = B0 * y + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2; X2 = X1; X1 = y; Y2 = Y1; Y1 = z; tmp[i] = (float) z;
            }
            std::vector<float> out (n);
            for (size_t i = 0; i < n; i++) { const double p = i * sr / FS; const size_t i0 = (size_t) p; const double fr = p - (double) i0; const double a = i0 < tmp.size() ? tmp[i0] : 0, bb = i0 + 1 < tmp.size() ? tmp[i0 + 1] : 0;
                out[i] = (float) (jr (std::max (-1.0, std::min (1.0, a + (bb - a) * fr)) * 2047) / 2048); }
            o->ch.push_back (std::move (out));
        }
        lofiCache[key] = o; return o;
    }
    // SP-1200: mono, nearest-sample pick at 26.04 kHz, 12-bit, 2.5 s max
    std::shared_ptr<std::vector<float>> spSource (const std::shared_ptr<PadBuf>& b) {
        auto it = spSrc.find (b->id); if (it != spSrc.end()) return it->second;
        if (spSrc.size() > 32) spSrc.clear();
        const size_t n = (size_t) std::min (std::floor (b->duration() * SP_FS), std::floor (SP_MAX * SP_FS)); const double ratio = b->sr / SP_FS; const size_t chn = b->ch.size();
        auto s = std::make_shared<std::vector<float>> (std::max<size_t> (1, n));
        for (size_t k = 0; k < n; k++) { const size_t j = (size_t) std::floor (k * ratio); double v = 0; for (auto& c : b->ch) v += j < c.size() ? c[j] : 0; v /= (double) chn;
            v = std::max (-1.0, std::min (2047.0 / 2048, v)); (*s)[k] = (float) (jr (v * 2048) / 2048); }
        spSrc[b->id] = s; return s;
    }
    static double SAT (double x) { return x < -3 ? -1 : (x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x)); }
    static void ladder (std::vector<float>& a, double fs, double fc, double res) {
        double y1 = 0, y2 = 0, y3 = 0, y4 = 0; const double G = 1 - std::exp (-2 * 3.14159265358979323846 * std::min (fc, fs * 0.45) / fs), r = res * 3.95;
        for (auto& s : a) { const double x = SAT (s * 1.15 - r * y4); y1 += G * (x - SAT (y1)); y2 += G * (SAT (y1) - SAT (y2)); y3 += G * (SAT (y2) - SAT (y3)); y4 += G * (SAT (y3) - SAT (y4)); s = (float) (y4 * (1 + res * 0.6)); }
    }
    static void lp2 (std::vector<float>& a, double fs, double fc) {
        const double w = 2 * 3.14159265358979323846 * std::min (fc, fs * 0.45) / fs, al = std::sin (w) / (2 * std::sqrt (0.5)), cw = std::cos (w), a0 = 1 + al;
        const double b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0; double z1 = 0, z2 = 0;
        for (auto& s : a) { const double x = s, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; s = (float) y; }
    }
    std::shared_ptr<PadBuf> spRender (int i, const std::shared_ptr<PadBuf>& raw, const Pad& pd, double st, double en, double rate) {
        const int ch = padCh (i);
        char kb[160]; std::snprintf (kb, sizeof kb, "%d|%.4f|%.4f|%.5f|%d|%d|%g/%g|%llu", i, st, en, rate, pd.rev, ch, ch < 2 ? spx.fc[ch] : 0.0, ch < 2 ? spx.fr[ch] : 0.0, (unsigned long long) raw->id);
        const std::string key (kb);
        auto it = spCache.find (key); if (it != spCache.end()) return it->second;
        const auto sp = spSource (raw); const size_t N = sp->size(); const size_t a = (size_t) std::floor (st * N); const size_t z = std::max (a + 1, (size_t) std::floor (en * N));
        const size_t frames = std::max<size_t> (1, (size_t) std::floor ((double) (z - a) / rate * fs / SP_FS));
        std::vector<float> out (frames);
        for (size_t nn = 0; nn < frames; nn++) {
            const double tick = std::floor (nn * SP_FS / fs); size_t idx = (size_t) std::floor (tick * rate); if (idx >= z - a) idx = z - a - 1;
            const size_t j = pd.rev ? z - 1 - idx : a + idx; out[nn] = j < N ? (*sp)[j] : 0.f;
        }
        if (ch < 2) ladder (out, fs, 80 * std::pow (2.0, spx.fc[ch] / 100 * 8), spx.fr[ch] / 100);
        else if (ch < 4) lp2 (out, fs, 6800);
        else if (ch < 6) lp2 (out, fs, 10500);
        for (auto& s : out) s = (float) (std::tanh (s * 1.05) / 1.05);
        auto b = std::make_shared<PadBuf>(); b->sr = fs; b->len = frames; b->ch.push_back (std::move (out)); b->id = ++nextId; b->peak = raw->peak;
        if (spCache.size() > 160) spCache.clear();
        spCache[key] = b; return b;
    }
};

} // namespace farp
