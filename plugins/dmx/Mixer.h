// Omega DMX sound engine: a line-for-line C++ port of dmx/engine.js's Engine (see there): the voices with the Omega 110 /
// Omega Traks chain (start, decay, SVF, ADSR, drive, EQ, pan, sends), the open-hat choke, the DMX's group faders and
// stereo mix, the REV7-style reverb, the TC 2290-style delay, the master EQ, the overdrive, VOLUME and the metronome.
// dmx/tools/compare.mjs checks it renders the same samples as the app.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "Fx.h"
#include "Params.inc"

namespace dmx {

class Mixer {
public:
    static constexpr int NGRP = 7, MAXV = 32, NSND = 20;
    struct Sound { std::shared_ptr<const std::vector<float>> d; double rate = 1; int card = 0, group = 0; bool open = false; double panL = 1, panR = 1, ratio = 1; };

    explicit Mixer(double sampleRate) : sr(sampleRate), rv(sampleRate), dl(sampleRate) {
        for (int i = 0; i < kParams; ++i) p[(size_t) i] = kParamDefs[i].def;
        fA = fx::onePole(30, sr); chokeK = fx::decayCoef(0.004, sr);
        update(); setFaders(fader); for (int g = 0; g < NGRP; ++g) gG[g] = gT[g]; vG = vT;
    }
    static double potLaw(double x) { return (std::pow(81.0, x) - 1) / 80; }
    double gainOf(double v) const { return circuit ? potLaw(v) * (0.8 / potLaw(0.8)) : v * v * 1.25; }
    void setFaders(const std::array<double, 9>& f) { fader = f; for (int g = 0; g < NGRP; ++g) gT[g] = gainOf(f[(size_t) g]); metT = gainOf(f[7]); vT = gainOf(f[8]); }
    void setCircuit(bool on) { circuit = on; setFaders(fader); }
    void setTempo(double bpm) { tempo = bpm; update(); }
    void setAll(const double* arr) { for (int i = 0; i < kParams; ++i) p[(size_t) i] = arr[i]; update(); }
    void setSound(int vi, Sound s) { if (vi >= 0 && vi < NSND) snd[(size_t) vi] = (s.d && s.d->size() > 1) ? std::make_unique<Sound>(std::move(s)) : nullptr; }
    const double* params() const { return p.data(); }

    void update() {
        const auto P = [&](const char* id) { return p[(size_t) index(id)]; };
        rv.setParams((int) jsRound(P("rvProg")), P("rvTime"), P("rvHigh"), P("rvDelay"), P("rvDiff"), P("rvEr"), P("rvCut")); rvLevel = P("rvLevel");
        static const double DIVS[14] = {0, 0.125, 1.0 / 6, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1, 4.0 / 3, 1.5, 2, 4};
        const double div = DIVS[std::max(0, std::min(13, (int) jsRound(P("dlSync"))))];
        dl.setParams(div > 0 ? 60 / tempo * div * 1000 : P("dlTime"), P("dlFb"), P("dlHiCut"), P("dlLoCut"), P("dlModRate"), P("dlModDepth"), P("dlDuck"), P("dlPing") >= 0.5);
        dlLevel = P("dlLevel");
        const char* mf[4][4] = {{"0", "mLsF", "mLsG", ""}, {"1", "mP1F", "mP1G", "mP1Q"}, {"1", "mP2F", "mP2G", "mP2Q"}, {"2", "mHsF", "mHsG", ""}};
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) meq[c][k].set(mf[k][0][0] - '0', P(mf[k][1]), P(mf[k][2]), mf[k][3][0] ? P(mf[k][3]) : 0.707, sr);
        od = P("odDrive"); odMode = (int) jsRound(P("odMode")); odMix = P("odMix"); odOut = std::pow(10.0, P("odOut") / 20);
        for (int c = 0; c < kCards; ++c) {
            auto q = [&](const char* f) { return p[(size_t) (kNG + c * kNF + field(f))]; };
            const double a = q("atk"), su = q("sus"), r = q("rel"); const bool mute = q("mute") >= 0.5;
            Card& C = card[(size_t) c];
            C.lv = mute ? 0 : q("lv"); C.pan = q("pan"); C.dk = q("dec") >= 2.999 ? 1 : fx::decayCoef(q("dec") / 3, sr); C.cut = q("cut"); C.cutOn = q("cut") < 19999; C.q = 0.707 + q("res") * 9.3;
            C.start = q("start"); C.snd = mute ? 0 : q("snd"); C.dls = mute ? 0 : q("dls");
            C.eq[0] = {0, q("lsF"), q("lsG"), 0.707}; C.eq[1] = {1, q("pkF"), q("pkG"), q("pkQ")}; C.eq[2] = {2, q("hsF"), q("hsG"), 0.707};
            C.envOn = !(a <= 0 && su >= 0.999 && r >= 3.999); C.envA = a > 0 ? 1 / (a * sr) : 1; C.envD = fx::decayCoef(q("dcy") / 3, sr); C.envS = su; C.envR = r >= 3.999 ? 1 : fx::decayCoef(r / 3, sr); C.drv = q("drv");
        }
        for (auto& v : vs) if (v.on) setupVoice(v);
    }
    // a hit at sample `offset` of the next process(); gate: samples until the pad's ADSR releases (-1: until release())
    void trigger(int vi, double vel, int offset, long gate) {
        if (vi < 0 || vi >= NSND || !snd[(size_t) vi]) return;
        const Sound& s = *snd[(size_t) vi];
        if (s.group == 2 && !s.open) for (auto& v : vs) if (v.on && v.open && v.choke < 0) v.choke = offset;   // a closed hat cuts the open one
        Voice* v = nullptr; for (auto& x : vs) if (!x.on) { v = &x; break; }
        if (!v) { v = &vs[0]; for (auto& x : vs) if (x.pos > v->pos) v = &x; }
        v->on = true; v->vi = vi; v->s = s.d; v->card = s.card; v->group = s.group; v->open = s.open; v->panL = s.panL; v->panR = s.panR; v->vel = vel;
        v->inc = s.rate / sr * s.ratio; v->startAt = offset; v->choke = -1; v->fade = 1; v->dec = 1; v->gate = gate;
        const Card& C = card[(size_t) s.card]; v->pos = std::floor(C.start * (double) (s.d->size() - 1));
        v->svf.ic1 = 0; v->svf.ic2 = 0; for (auto& b : v->eq) { b.z1 = 0; b.z2 = 0; }
        setupVoice(*v);
        if (v->envA < 1) { v->env = 0; v->stage = 0; } else { v->env = 1; v->stage = 1; }
    }
    void release(int vi) { for (auto& v : vs) if (v.on && v.vi == vi && v.stage < 3) v.stage = 3; }
    void click(bool loud, int offset) { clk = std::max(1, (int) jsRound(sr * 0.001)); clkAt = offset; clkLoud = loud; }
    void stopAll() { for (auto& v : vs) v.on = false; }
    std::array<double, NSND> meter {};

    void process(double* L, double* R, int n) {
        const double MET[2] = {2.5 / (5.0 / 8300 * 3.8 * 2400), 1.25 / (5.0 / 8300 * 3.8 * 2400)};
        for (int s = 0; s < n; ++s) {
            double bl[NGRP] {}, br[NGRP] {}, bs[NGRP] {}, bd[NGRP] {};
            for (auto& v : vs) {
                if (!v.on || s < v.startAt) continue;
                const auto& d = *v.s; const long a = (long) std::floor(v.pos);
                if (a >= (long) d.size() - 1) { v.on = false; continue; }
                double y = d[(size_t) a] + ((double) d[(size_t) a + 1] - d[(size_t) a]) * (v.pos - (double) a); v.pos += v.inc;
                y *= v.amp * v.dec; v.dec *= v.dk;
                if (v.choke >= 0 && s >= v.choke) { y *= v.fade; v.fade *= chokeK; if (v.fade < 1e-4) { v.on = false; continue; } }
                if (v.cutOn) { v.svf.tick(y); y = v.svf.lp; }
                if (v.envOn) {
                    if (v.gate >= 0) { if (v.gate == 0 && v.stage < 3) v.stage = 3; else v.gate--; }
                    double e = v.env;
                    if (v.stage == 0) { e += v.envA; if (e >= 1) { e = 1; v.stage = 1; } }
                    else if (v.stage == 1) e = v.envS + (e - v.envS) * v.envD;
                    else e *= v.envR;
                    v.env = e; y *= e;
                    if (v.stage == 3 && e < 1e-5) { v.on = false; continue; }
                }
                if (v.drv > 0) y = std::tanh(y * (1 + v.drv * 15)) / (1 + v.drv);
                y = v.eq[2].tick(v.eq[1].tick(v.eq[0].tick(y)));
                const double ay = std::abs(y); if (ay > meter[(size_t) v.vi]) meter[(size_t) v.vi] = ay;
                const int g = v.group; bl[g] += y * v.gl; br[g] += y * v.gr; bs[g] += y * v.snd; bd[g] += y * v.dls;
            }
            double l = 0, r = 0, sR = 0, sD = 0;
            for (int g = 0; g < NGRP; ++g) { gG[g] += (gT[g] - gG[g]) * fA; const double k = gG[g]; l += bl[g] * k; r += br[g] * k; sR += bs[g] * k; sD += bd[g] * k; }
            double oL, oR; rv.tick(sR, oL, oR); l += oL * rvLevel; r += oR * rvLevel;
            dl.tick(sD, oL, oR); l += oL * dlLevel; r += oR * dlLevel;
            for (int k = 0; k < 4; ++k) { l = meq[0][k].tick(l); r = meq[1][k].tick(r); }
            if (od > 0) { l = overdrive(l); r = overdrive(r); }
            vG += (vT - vG) * fA; l *= vG; r *= vG;
            if (clk > 0 && s >= clkAt) { const double c = MET[clkLoud ? 0 : 1] * metT; l += c; r += c; clk--; }
            L[s] = l; R[s] = r;
        }
        for (auto& v : vs) if (v.on) { v.startAt = 0; if (v.choke > 0) v.choke = 0; }
        clkAt = 0;
    }

    static int index(const char* id) { for (int i = 0; i < kParams; ++i) if (!std::strcmp(kParamDefs[i].id, id)) return i; return 0; }
    static int field(const char* f) { for (int i = 0; i < kNF; ++i) if (!std::strcmp(kParamDefs[kNG + i].id + 2, f)) return i; return 0; }

private:
    struct Eq { int type; double f, g, q; };
    struct Card { double lv = 1, pan = 0, dk = 1, cut = 20000, q = 0.707, start = 0, snd = 0, dls = 0; bool cutOn = false, envOn = false; Eq eq[3] {}; double envA = 1, envD = 0, envS = 1, envR = 1, drv = 0; };
    struct Voice {
        bool on = false, open = false, cutOn = false, envOn = false; int vi = 0, card = 0, group = 0, startAt = 0, choke = -1, stage = 0; long gate = -1;
        std::shared_ptr<const std::vector<float>> s; double pos = 0, inc = 1, vel = 1, amp = 1, dec = 1, dk = 1, fade = 1, env = 1, envA = 1, envD = 0, envS = 1, envR = 1, drv = 0;
        double panL = 1, panR = 1, gl = 1, gr = 1, snd = 0, dls = 0; fx::SVF svf; fx::Biquad eq[3];
    };
    void setupVoice(Voice& v) {
        const Card& C = card[(size_t) v.card];
        v.amp = v.vel * C.lv; v.dk = C.dk; v.cutOn = C.cutOn; if (C.cutOn) v.svf.set(C.cut, C.q, sr);
        for (int k = 0; k < 3; ++k) { const Eq& e = C.eq[k]; v.eq[k].set(e.type, e.f, e.g, e.q, sr); }
        v.envOn = C.envOn; v.envA = C.envA; v.envD = C.envD; v.envS = C.envS; v.envR = C.envR; v.drv = C.drv;
        v.gl = v.panL * std::min(1.0, 1 - C.pan); v.gr = v.panR * std::min(1.0, 1 + C.pan); v.snd = C.snd; v.dls = C.dls;
    }
    double overdrive(double x) const {
        const double g = 1 + od * 20; double y;
        if (odMode == 1) y = std::max(-1.0, std::min(1.0, x * g)) * 0.8;
        else if (odMode == 2) y = (std::tanh(x * g + 0.3) - std::tanh(0.3)) * 0.85;
        else y = std::tanh(x * g) * 0.85;
        return (x * (1 - odMix) + y * odMix) * odOut;
    }
    static double jsRound(double x) { return std::floor(x + 0.5); }

    double sr, tempo = 120; std::array<double, kParams> p {}; std::array<double, 9> fader {.8, .8, .8, .8, .8, .8, .8, 0, .85}; bool circuit = false;
    std::array<std::unique_ptr<Sound>, NSND> snd; std::array<Voice, MAXV> vs; std::array<Card, kCards> card;
    double gG[NGRP] {}, gT[NGRP] {}, vG = 0, vT = 0, metT = 0, fA = 0, chokeK = 0;
    fx::Biquad meq[2][4]; fx::Rev7 rv; fx::TCDelay dl;
    double rvLevel = 0, dlLevel = 0, od = 0, odMix = 1, odOut = 1; int odMode = 0;
    int clk = 0, clkAt = 0; bool clkLoud = false;
};

}  // namespace dmx
