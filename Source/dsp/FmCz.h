// FM-4 (4-operator FM playing DX100 voices) and PD-8 (phase distortion playing CZ tones).
// C++ port of the page's DXEngine and CZEngine; voices/tones arrive decoded from the page.
#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include "Val.h"

namespace farp {

class DXEngine {
public:
    struct Op { int ar = 31, d1r = 0, d2r = 0, rr = 7, d1l = 15; double ls = 0; int kvs = 0; double out = 0; int crs = 4, rs = 0, dbt = 3, ame = 0; };
    struct Voice { Op ops[4]; int alg = 7, fb = 0; double lfs = 35, lfd = 0, pmd = 0, amd = 0; int pms = 0, ams = 0, lfw = 2; double trps = 24, pbr = 2; int mono = 0; };
    struct Rates { double a = 0, d1 = 0, d2 = 0, rr = 0, sl = 0; };
    struct V { int note = -1; bool on = false; int age = 0; double ph[4] = {}, env[4] = {96, 96, 96, 96}; int st[4] = {4, 4, 4, 4}; double fb1 = 0, fb2 = 0, inc[4] = {}, keyAtt[4] = {}; Rates r[4]; bool hasR = false; double vel = 1; };
    double sr; static const int N = 8; V v[N]; int counter = 0; double lfoPh = 0, lfoSH = 0, bend = 0, wheel = 0, level = 1.6, lfoHz = 0;
    float sinT[4096]; Voice voice; uint32_t rng = 0x12345678;

    explicit DXEngine(double sr_) : sr(sr_) {
        for (int i = 0; i < 4096; i++) sinT[i] = (float)std::sin(2 * 3.14159265358979323846 * i / 4096);
        Voice p; p.ops[0].out = 99; setVoice(p);
    }
    static Voice fromVal(const Val& o) {
        Voice p;
        for (int i = 0; i < 4; i++) {
            const Val& q = o["ops"][i]; Op& d = p.ops[i];
            d.ar = (int)q["ar"].num(); d.d1r = (int)q["d1r"].num(); d.d2r = (int)q["d2r"].num(); d.rr = (int)q["rr"].num(); d.d1l = (int)q["d1l"].num();
            d.ls = q["ls"].num(); d.kvs = (int)q["kvs"].num(); d.out = q["out"].num(); d.crs = (int)q["crs"].num(); d.rs = (int)q["rs"].num(); d.dbt = (int)q["dbt"].num(); d.ame = (int)q["ame"].num();
        }
        p.alg = (int)o["alg"].num(); p.fb = (int)o["fb"].num(); p.lfs = o["lfs"].num(); p.lfd = o["lfd"].num(); p.pmd = o["pmd"].num(); p.amd = o["amd"].num();
        p.pms = (int)o["pms"].num(); p.ams = (int)o["ams"].num(); p.lfw = (int)o["lfw"].num(); p.trps = o["trps"].num(24); p.pbr = o["pbr"].num(); p.mono = (int)o["mono"].num();
        return p;
    }
    void setVoice(const Voice& p) { voice = p; lfoHz = 0.05 * std::pow(2.0, p.lfs / 11); }
    static double decayTime(double r) { return 30 * std::pow(2.0, -r / 4.2); }
    static double ratio(int i) {
        static const double R[64] = {0.50, 0.71, 0.78, 0.87, 1.00, 1.41, 1.57, 1.73, 2.00, 2.82, 3.00, 3.14, 3.46, 4.00, 4.24, 4.71, 5.00, 5.19, 5.65, 6.00, 6.28, 6.92, 7.00, 7.07, 7.85, 8.00, 8.48, 8.65, 9.00, 9.42, 9.89, 10.00,
            10.38, 10.99, 11.00, 11.30, 12.00, 12.11, 12.56, 12.72, 13.00, 13.84, 14.00, 14.10, 14.13, 15.00, 15.55, 15.57, 15.70, 16.96, 17.27, 17.30, 18.37, 18.84, 19.03, 19.78, 20.41, 20.76, 21.20, 21.98, 22.49, 23.55, 24.22, 25.95};
        return (i >= 0 && i < 64) ? R[i] : std::nan("");
    }
    void noteOn(int n, double vel) {
        const Voice& p = voice; V* t = nullptr;
        if (p.mono) t = &v[0];
        else {
            for (auto& x : v) if (x.note == n) { t = &x; break; }
            if (!t) for (auto& x : v) if (!x.on && x.st[0] == 4 && x.st[1] == 4 && x.st[2] == 4 && x.st[3] == 4) { t = &x; break; }
            if (!t) { t = &v[0]; for (auto& x : v) if (x.age < t->age) t = &x; }
        }
        t->note = n; t->on = true; t->age = ++counter; t->vel = vel; const double key = n + (p.trps - 24); const int kc = (int)std::max(0.0, std::min(31.0, std::floor((key - 12) / 3)));
        t->hasR = true;
        for (int i = 0; i < 4; i++) {
            const Op& o = p.ops[i]; const int ks = kc >> (3 - o.rs); t->st[i] = 0;
            t->r[i] = {o.ar ? (double)std::min(63, 2 * o.ar + ks) : 0.0, o.d1r ? (double)std::min(63, 2 * o.d1r + ks) : 0.0, o.d2r ? (double)std::min(63, 2 * o.d2r + ks) : 0.0, (double)std::min(63, 4 * o.rr + 2 + ks), o.d1l ? (15.0 - o.d1l) * 3 : 96.0};
            const double hz = 440 * std::pow(2.0, (key - 69) / 12) * ratio(o.crs) * std::pow(2.0, (o.dbt - 3) * 1.5 / 1200); t->inc[i] = hz / sr;
            const double att = o.out != 0 ? (99 - o.out) * 0.75 : 96, ls = o.ls / 99 * std::max(0.0, key - 48) / 12 * 6, kv = o.kvs / 7.0 * (1 - vel) * 24; t->keyAtt[i] = att + ls + kv;
        }
    }
    void noteOff(int n) { for (auto& x : v) if (x.note == n && x.on) { x.on = false; for (int i = 0; i < 4; i++) x.st[i] = 3; } }
    void allOff() { for (auto& x : v) { x.on = false; if (x.hasR) for (int i = 0; i < 4; i++) if (x.st[i] != 4) x.st[i] = 3; } }
    bool active() const { for (auto& x : v) for (int i = 0; i < 4; i++) if (x.st[i] != 4) return true; return false; }
    double env(V& x, int i, double dt) {
        const Rates& r = x.r[i]; double e = x.env[i];
        switch (x.st[i]) {
            case 0: if (r.a >= 62) { e = 0; x.st[i] = 1; break; } if (r.a == 0) break; { double k = 1 - std::exp(-dt * 6 / decayTime(r.a)); e -= (e + 8) * k; if (e <= 0) { e = 0; x.st[i] = 1; } } break;
            case 1: if (r.d1 != 0) e += 96 * dt / decayTime(r.d1); if (e >= r.sl) { e = r.sl; x.st[i] = 2; } break;
            case 2: if (r.d2 != 0) e += 96 * dt / decayTime(r.d2); if (e >= 96) { e = 96; x.st[i] = 4; } break;
            case 3: e += 96 * dt / decayTime(r.rr); if (e >= 96) { e = 96; x.st[i] = 4; } break;
        }
        x.env[i] = e; return e;
    }
    double tick() {
        const Voice& p = voice; const double dt = 1 / sr;
        lfoPh += lfoHz * dt; if (lfoPh >= 1) { lfoPh -= 1; rng = rng * 1664525u + 1013904223u; lfoSH = (double)(rng >> 8) / 16777216.0 * 2 - 1; }
        const double ph = lfoPh, lf = p.lfw == 0 ? 2 * ph - 1 : p.lfw == 1 ? (ph < 0.5 ? 1 : -1) : p.lfw == 2 ? 1 - 4 * std::abs(ph - 0.5) : lfoSH;
        static const double PMS[8] = {0, 0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 3.2}; static const double AMS[4] = {0, 1.5, 6, 12};
        const double pmDepth = (p.pmd / 99 + wheel * 0.5) * PMS[p.pms & 7] * lf, pitchMul = std::pow(2.0, (pmDepth + bend) / 12);
        const double amAtt = (p.amd / 99) * AMS[p.ams & 3] * (0.5 - 0.5 * lf);
        const double fbK = p.fb ? 8191 / std::pow(2.0, 10 - p.fb) / 1024 * 2 * 3.14159265358979323846 : 0, MOD = 8 * 3.14159265358979323846, TWO = 1 / (2 * 3.14159265358979323846);
        double sum = 0;
        for (auto& x : v) {
            if (x.st[0] == 4 && x.st[1] == 4 && x.st[2] == 4 && x.st[3] == 4) continue;
            double a[4];
            for (int i = 0; i < 4; i++) { double e = env(x, i, dt) + x.keyAtt[i] + (p.ops[i].ame ? amAtt : 0); a[i] = e >= 96 ? 0 : std::pow(10.0, -e / 20); x.ph[i] += x.inc[i] * pitchMul; if (x.ph[i] >= 1) x.ph[i] -= std::floor(x.ph[i]); }
            auto op = [&](int i, double m) { double y = x.ph[i] + m * TWO; y -= std::floor(y); int k = (int)(y * 4096); if (k > 4095) k = 4095; if (k < 0) k = 0; return sinT[k] * a[i]; };
            const double o4 = op(3, (x.fb1 + x.fb2) * fbK); x.fb2 = x.fb1; x.fb1 = o4; double out;
            switch (p.alg) {
                case 0: { double o3 = op(2, o4 * MOD), o2 = op(1, o3 * MOD); out = op(0, o2 * MOD); break; }
                case 1: { double o3 = op(2, 0), o2 = op(1, (o3 + o4) * MOD); out = op(0, o2 * MOD); break; }
                case 2: { double o3 = op(2, 0), o2 = op(1, o3 * MOD); out = op(0, (o2 + o4) * MOD); break; }
                case 3: { double o3 = op(2, o4 * MOD), o2 = op(1, 0); out = op(0, (o2 + o3) * MOD); break; }
                case 4: { double o3 = op(2, o4 * MOD), o2 = op(1, 0); out = o3 + op(0, o2 * MOD); break; }
                case 5: { out = op(2, o4 * MOD) + op(1, o4 * MOD) + op(0, o4 * MOD); break; }
                case 6: { out = op(2, o4 * MOD) + op(1, 0) + op(0, 0); break; }
                default: out = o4 + op(2, 0) + op(1, 0) + op(0, 0);
            }
            sum += out;
        }
        return sum * 0.18 * level;
    }
};

class CZEngine {
public:
    struct Step { double r = 99, l = 0; };
    struct EnvDef { std::vector<Step> steps; int end = 0, sus = -1; };
    struct Line { int w1 = 1, w2 = 0; bool two = false; int mod = 0; double akf = 0, wkf = 0; EnvDef dca, dcw, dco; };
    struct Tone { int ls = 0; double oct = 0, det = 0; int vibWave = 0; double vibDelay = 0, vibRate = 30, vibDepth = 0; int mod = 0; Line l1, l2; };
    struct E { int step = 0; double val = 0, target = 0, rate = 0, from = 0; bool done = true; };
    struct LS { double ph = 0; int ph2 = 0; E e[3]; };
    struct V { int note = -1; bool on = false; int age = 0; double t = 0, vel = 1; LS l[2]; };
    double sr; V v[8]; int counter = 0; double bend = 0, wheel = 0, level = 0.8, vibPh = 0; uint32_t noise = 7; Tone tone;

    explicit CZEngine(double sr_) : sr(sr_) { setTone(initTone()); }
    static Tone initTone() {
        auto env = [](double lv) { EnvDef e; e.steps = {{99, lv}, {50, 0}}; e.end = 1; e.sus = 0; return e; };
        Line line; line.w1 = 1; line.w2 = 0; line.two = false; line.dca = env(99); line.dcw = env(60); line.dco.steps = {{99, 0}}; line.dco.end = 0; line.dco.sus = -1; line.akf = 0; line.wkf = 0;
        Tone t; t.ls = 0; t.oct = 0; t.det = 0; t.vibWave = 0; t.vibDelay = 0; t.vibRate = 30; t.vibDepth = 0; t.mod = 0; t.l1 = line; t.l2 = line; return t;
    }
    static EnvDef envFrom(const Val& o) {
        EnvDef e; e.end = (int)o["end"].num(); e.sus = (int)o["sus"].num(-1);
        for (size_t i = 0; i < o["steps"].size(); i++) { const Val& s = o["steps"][i]; e.steps.push_back({s["r"].num(), s["l"].num()}); }
        return e;
    }
    static Line lineFrom(const Val& o) {
        Line l; l.w1 = (int)o["w1"].num(1); l.w2 = (int)o["w2"].num(); l.two = o["two"].truthy(); l.mod = (int)o["mod"].num(); l.akf = o["akf"].num(); l.wkf = o["wkf"].num();
        l.dca = envFrom(o["dca"]); l.dcw = envFrom(o["dcw"]); l.dco = envFrom(o["dco"]); return l;
    }
    static Tone fromVal(const Val& o) {
        Tone t; t.ls = (int)o["ls"].num(); t.oct = o["oct"].num(); t.det = o["det"].num(); t.vibWave = (int)o["vibWave"].num(); t.vibDelay = o["vibDelay"].num(); t.vibRate = o["vibRate"].num();
        t.vibDepth = o["vibDepth"].num(); t.mod = (int)o["mod"].num(); t.l1 = lineFrom(o["l1"]); t.l2 = lineFrom(o["l2"]); return t;
    }
    void setTone(const Tone& t) { tone = t; }
    static double rateTime(double r) { return 20 * std::pow(2.0, -r / 8.5); }
    void startEnv(E& e, const EnvDef& def, double from) { e.step = 0; e.done = false; seg(e, def, from); }
    void seg(E& e, const EnvDef& def, double from) {
        const Step* s = (e.step >= 0 && e.step < (int)def.steps.size()) ? &def.steps[e.step] : nullptr;
        e.from = from; e.target = s ? s->l : 0; e.rate = 99 / rateTime(s ? s->r : 99);
    }
    double envTick(E& e, const EnvDef& def, bool on, double dt) {
        if (e.done) return e.val; const double d = e.target - e.val, stp = e.rate * dt;
        if (std::abs(d) <= stp) {
            e.val = e.target;
            if (on && e.step == def.sus) return e.val;
            if (e.step >= def.end) { e.done = true; return e.val; }
            e.step++; seg(e, def, e.val);
        } else e.val += (d > 0 ? 1 : d < 0 ? -1 : 0) * stp;
        return e.val;
    }
    void release(E& e, const EnvDef& def) {
        if (e.done) return;
        if (def.sus >= 0 && e.step <= def.sus) {
            e.step = def.sus + 1 > def.end ? def.end : def.sus + 1; seg(e, def, e.val);
            if (def.sus >= def.end) { e.target = 0; e.rate = 99 / rateTime((def.end >= 0 && def.end < (int)def.steps.size()) ? def.steps[def.end].r : 99); }
        }
    }
    const EnvDef& envOf(const Line& L, int j) const { return j == 0 ? L.dca : j == 1 ? L.dcw : L.dco; }
    void noteOn(int n, double vel) {
        V* t = nullptr; for (auto& x : v) if (x.note == n) { t = &x; break; }
        if (!t) for (auto& x : v) if (!x.on && x.l[0].e[0].done) { t = &x; break; }
        if (!t) { t = &v[0]; for (auto& x : v) if (x.age < t->age) t = &x; }
        t->note = n; t->on = true; t->age = ++counter; t->t = 0; t->vel = vel;
        for (int k = 0; k < 2; k++) { const Line& L = k ? tone.l2 : tone.l1; LS& ln = t->l[k]; ln.ph = 0; ln.ph2 = 0; for (int j = 0; j < 3; j++) { ln.e[j].val = 0; startEnv(ln.e[j], envOf(L, j), 0); } }
    }
    void noteOff(int n) {
        for (auto& x : v) if (x.note == n && x.on) { x.on = false; for (int k = 0; k < 2; k++) { const Line& L = k ? tone.l2 : tone.l1; for (int j = 0; j < 3; j++) release(x.l[k].e[j], envOf(L, j)); } }
    }
    void allOff() { for (auto& x : v) if (x.on) noteOff(x.note); }
    bool active() const { for (auto& x : v) if (!x.l[0].e[0].done || !x.l[1].e[0].done) return true; return false; }
    static double wave(int w, double ph, double d) {
        const double TP = 2 * 3.14159265358979323846;
        switch (w) {
            case 1: { double k = 0.5 - 0.49 * d, x = ph < k ? ph * 0.5 / k : 0.5 + (ph - k) * 0.5 / (1 - k); return -std::cos(TP * x); }
            case 2: { double kk = 0.5 * (1 - 0.98 * d), x = ph < 0.5 ? std::min(ph / kk, 1.0) * 0.5 : 0.5 + std::min((ph - 0.5) / kk, 1.0) * 0.5; return -std::cos(TP * x); }
            case 3: { double k = 1 - 0.98 * d, x = ph < k ? ph / k : 1; return -std::cos(TP * x); }
            case 4: { double k = 0.5 - 0.49 * d, h = ph < 0.5 ? ph * 2 : (ph - 0.5) * 2, x = h < k ? h * 0.5 / k : 0.5 + (h - k) * 0.5 / (1 - k); return ph < 0.5 ? -std::cos(TP * x) : std::cos(TP * x) * -1; }
            case 5: { double k = 0.5 - 0.49 * d; if (ph < 0.5) { double x = ph < k ? ph * 0.5 / k : 0.5; return -std::cos(TP * x); } return -std::cos(TP * (0.5 + (ph - 0.5))); }
            case 6: { double f = 1 + d * 15; return (1 - std::cos(TP * ph * f)) * (1 - ph) - 1; }
            case 7: { double f = 1 + d * 15, win = ph < 0.5 ? 2 * ph : 2 - 2 * ph; return (1 - std::cos(TP * ph * f)) * win - 1; }
            default: { double f = 1 + d * 15, win = ph < 0.5 ? 1 : 2 - 2 * ph; return (1 - std::cos(TP * ph * f)) * win - 1; }
        }
    }
    double tick() {
        const Tone& T = tone; const double dt = 1 / sr; double sum = 0;
        vibPh += (0.1 + T.vibRate * 0.12) * dt; if (vibPh >= 1) vibPh -= 1;
        const double vp = vibPh, vw = T.vibWave == 1 ? (vp < 0.5 ? 1 : -1) : T.vibWave == 2 ? 2 * vp - 1 : T.vibWave == 3 ? 1 - 2 * vp : 1 - 4 * std::abs(vp - 0.5);
        for (auto& x : v) {
            LS& l0 = x.l[0]; if (l0.e[0].done && x.l[1].e[0].done) continue; x.t += dt;
            const double vib = (x.t > T.vibDelay * 0.03 ? vw * T.vibDepth * 0.01 : 0) + wheel * vw * 0.3, base = x.note + 12 * T.oct + bend * 2;
            double out = 0; double ring1 = 0;
            const int first = T.ls == 1 ? 1 : 0, last = T.ls == 0 ? 0 : 1;
            for (int k = first; k <= last; k++) {
                const Line& L = k ? T.l2 : T.l1; LS& ln = x.l[k];
                const double a = envTick(ln.e[0], L.dca, x.on, dt) / 99, wv = envTick(ln.e[1], L.dcw, x.on, dt) / 99, pe = envTick(ln.e[2], L.dco, x.on, dt);
                const double kfA = 1 - L.akf * std::max(0, x.note - 60) / 600, kfW = 1 - L.wkf * std::max(0, x.note - 60) / 400;
                const double det = k == 1 && T.ls >= 2 ? T.det + (T.ls == 2 ? 0.08 : 0) : 0;
                const double hz = 440 * std::pow(2.0, (base + det + vib + pe * 0.25 - 69) / 12), inc = hz * dt;
                ln.ph += inc; if (ln.ph >= 1) { ln.ph -= 1; ln.ph2 ^= 1; }
                const double d = std::max(0.0, std::min(1.0, wv * kfW)); double s;
                if (L.two) s = wave(ln.ph2 ? L.w2 : L.w1, ln.ph, d); else s = wave(L.w1, ln.ph, d);
                if (k == 1 && T.mod == 4) s *= ring1;
                else if (k == 1 && T.mod == 3) { noise = noise * 1103515245u + 12345u; s *= ((noise >> 16) & 1) ? 1 : -1; }
                if (k == 0) ring1 = s;
                out += s * a * a * std::max(0.0, kfA);
            }
            sum += out;
        }
        return sum * 0.16 * level;
    }
};

} // namespace farp
