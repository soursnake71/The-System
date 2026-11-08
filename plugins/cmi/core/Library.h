// Omega CMI sound library.
//
// The CMI's fame rests on its disk library (the orchestra stab, the breathy choir, breaking glass, the pizzicato
// strings, the drum sounds of Page R...). Those recordings belong to their owners, so none is included here: every
// sound below is built from scratch at start-up by physical and spectral models of the same kind of source (strings,
// voices, bars, membranes, glass), recorded the way the CMI recorded: one channel, 8 bits, at the sample rates the
// CMI used, at most 16 KB, loops on whole pages. Then they go through the channel card model like any CMI sound.
// The "waveform" sounds are Page 4 style: 32 harmonics per segment, 128 segments of one cycle each.
#pragma once
#include "Engine.h"
#include <cctype>
#include <functional>
#include <memory>
#include <mutex>

namespace cmi {
namespace gen {

using Vec = std::vector<float>;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uni() { return next() * (1.0 / 4294967296.0); }
    double bip() { return uni() * 2 - 1; }
};

struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static BQ make(double b0, double b1, double b2, double a0, double a1, double a2) { BQ q; q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0; return q; }
    static double w(double fs, double f) { return 2 * PI * std::min(f, fs * 0.47) / fs; }
    static BQ lp(double fs, double f, double q = 0.707) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    static BQ hp(double fs, double f, double q = 0.707) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    static BQ bp(double fs, double f, double q) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make(al, 0, -al, 1 + al, -2 * c, 1 - al); }
    static BQ peak(double fs, double f, double q, double db) { const double A = std::pow(10, db / 40), w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
};

inline double blep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
struct Saw { double ph = 0; double run(double f, double fs) { const double dt = std::min(f / fs, 0.45); ph += dt; if (ph >= 1) ph -= 1; return 2 * ph - 1 - blep(ph, dt); } };
struct Pulse { double ph = 0; double run(double f, double fs, double pw) { const double dt = std::min(f / fs, 0.45); ph += dt; if (ph >= 1) ph -= 1;
    double v = ph < pw ? 1 : -1; v += blep(ph, dt); double t2 = ph - pw; if (t2 < 0) t2 += 1; v -= blep(t2, dt); return v; } };

inline Vec zeros(double fs, double sec) { return Vec((size_t) std::max(1.0, std::round(fs * sec)), 0.f); }
inline double cents(double c) { return std::pow(2.0, c / 1200.0); }
inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }
inline void add(Vec& a, const Vec& b, double g = 1, size_t off = 0) { for (size_t i = 0; i + off < a.size() && i < b.size(); ++i) a[i + off] += (float) (b[i] * g); }
inline void filt(Vec& x, BQ q) { for (auto& v : x) v = (float) q.run(v); }

// a sum of decaying sinusoids (bars, bells, membranes, glass)
struct Mode_ { double ratio, amp, decay, phase = 0; };
inline Vec modal(double fs, double sec, double f0, const std::vector<Mode_>& m, double pitchDrop = 0, double dropTime = 0.05) {
    Vec y = zeros(fs, sec);
    for (const auto& md : m) {
        double ph = md.phase; const double fmax = fs * 0.45;
        for (size_t i = 0; i < y.size(); ++i) {
            const double t = i / fs;
            const double f = f0 * md.ratio * (1 + pitchDrop * std::exp(-t / dropTime));
            if (f >= fmax) break;
            ph += 2 * PI * f / fs;
            y[i] += (float) (md.amp * std::exp(-t / md.decay) * std::sin(ph));
        }
    }
    return y;
}

// plucked string (Karplus-Strong with tuned loss), pitch can move: f(t) in Hz
inline Vec pluck(double fs, double sec, const std::function<double(double)>& f, double t60, double bright, double pickPos, uint32_t seed, double excite = 1.0,
                 double jawari = 0) {
    Vec y = zeros(fs, sec);
    const double fmin = 25;
    const int cap = (int) (fs / fmin) + 8;
    std::vector<double> d((size_t) cap, 0.0);
    Rng r(seed);
    // excitation: filtered noise, combed at the pluck position
    const double f0 = f(0);
    const int N0 = std::max(2, (int) (fs / f0));
    std::vector<double> ex((size_t) N0);
    double lp = 0;
    for (int i = 0; i < N0; ++i) { lp += (r.bip() - lp) * (0.15 + 0.85 * bright); ex[(size_t) i] = lp * excite; }
    const int pp = std::max(1, (int) (pickPos * N0));
    for (int i = N0 - 1; i >= pp; --i) ex[(size_t) i] -= ex[(size_t) (i - pp)];
    for (int i = 0; i < N0; ++i) d[(size_t) i] = ex[(size_t) i];
    int w = N0 % cap; double last = 0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs, fr = std::max(fmin, f(t));
        double delay = fs / fr - 0.5;
        // the loss filter: one-pole average, per-period gain for the t60
        const double g = std::pow(10.0, -3.0 / (fr * t60));
        if (jawari > 0) delay *= 1.0 - jawari * std::min(1.0, std::fabs(last) * 4);
        const double rp = w - delay;
        double ri = std::floor(rp); const double fr2 = rp - ri;
        const int i0 = ((int) ri % cap + cap) % cap, i1 = (i0 + 1) % cap;
        const double v = d[(size_t) i0] * (1 - fr2) + d[(size_t) i1] * fr2;
        const double s = 0.5 * (1 - bright) + 0.02;
        const double nv = g * ((1 - s) * v + s * last);
        last = nv;
        d[(size_t) w] = nv;
        w = (w + 1) % cap;
        y[i] = (float) nv;
    }
    return y;
}

// a few singers or players: band-limited sawtooth sources with their own detune, vibrato and jitter
inline Vec ensemble(double fs, double sec, double f0, int n, double detuneCents, double vibDepthSemis, double vibRate, uint32_t seed,
                    double attack = 0.02, double scoopCents = 0, double scoopTime = 0.05, double tilt = 0) {
    Vec y = zeros(fs, sec); Rng r(seed);
    for (int k = 0; k < n; ++k) {
        Saw o; o.ph = r.uni();
        const double det = cents(detuneCents * (n == 1 ? 0 : r.bip()));
        const double vr = vibRate * (0.85 + 0.3 * r.uni()), vp = r.uni();
        const double start = r.uni() * 0.03 * (n > 1);
        double jit = 0, tl = 0;
        for (size_t i = 0; i < y.size(); ++i) {
            const double t = i / fs;
            jit += (r.bip() * 0.002 - jit) * 0.01;
            const double vib = vibDepthSemis * std::sin(2 * PI * (vr * t + vp)) * std::min(1.0, t / 0.35);
            const double sc = scoopCents * std::exp(-t / scoopTime);
            const double f = f0 * det * std::pow(2.0, (vib + sc / 100.0) / 12.0) * (1 + jit);
            double v = o.run(f, fs);
            if (tilt > 0) { tl += (v - tl) * tilt; v = tl; }
            const double a = t < start ? 0 : std::min(1.0, (t - start) / std::max(attack, 1e-3));
            y[i] += (float) (v * a / std::sqrt((double) n));
        }
    }
    return y;
}

struct Formant { double f, q, gainDb; };
inline Vec formants(const Vec& x, double fs, const std::vector<Formant>& fm) {
    Vec y(x.size(), 0.f);
    for (const auto& f : fm) {
        BQ q = BQ::bp(fs, f.f, f.q); const double g = std::pow(10.0, f.gainDb / 20.0);
        for (size_t i = 0; i < x.size(); ++i) y[i] += (float) (q.run(x[i]) * g);
    }
    return y;
}

inline Vec noise(double fs, double sec, uint32_t seed) { Vec y = zeros(fs, sec); Rng r(seed); for (auto& v : y) v = (float) r.bip(); return y; }

inline void envelope(Vec& x, double fs, const std::function<double(double)>& e) { for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (x[i] * e(i / fs)); }

// small Schroeder room, mixed in
inline void room(Vec& x, double fs, double mix, double rt) {
    const double cd[4] = { 0.0297, 0.0371, 0.0411, 0.0437 }, ad[2] = { 0.005, 0.0017 };
    std::vector<std::vector<double>> cb(4), ab(2); std::vector<size_t> cp(4, 0), ap(2, 0); std::vector<double> g(4), lpS(4, 0);
    for (int i = 0; i < 4; ++i) { cb[(size_t) i].assign((size_t) (cd[i] * fs) + 1, 0); g[(size_t) i] = std::pow(10.0, -3 * cd[i] / rt); }
    for (int i = 0; i < 2; ++i) ab[(size_t) i].assign((size_t) (ad[i] * fs) + 1, 0);
    for (auto& v : x) {
        double s = 0;
        for (int i = 0; i < 4; ++i) {
            auto& b = cb[(size_t) i]; size_t& p = cp[(size_t) i];
            const double o = b[p]; lpS[(size_t) i] += (o - lpS[(size_t) i]) * 0.6;
            b[p] = v + lpS[(size_t) i] * g[(size_t) i]; p = (p + 1) % b.size(); s += o;
        }
        s *= 0.25;
        for (int i = 0; i < 2; ++i) {
            auto& b = ab[(size_t) i]; size_t& p = ap[(size_t) i];
            const double o = b[p]; const double in = s + o * 0.5; b[p] = in; p = (p + 1) % b.size(); s = o - in * 0.5;
        }
        v = (float) (v + s * mix);
    }
}

inline void drive(Vec& x, double g) { for (auto& v : x) v = (float) std::tanh(v * g); }
inline double peakOf(const Vec& x) { double p = 0; for (float v : x) p = std::max(p, (double) std::fabs(v)); return p; }

// ---- into CMI memory ----
// crossfade: smooth the loop seam (the synthesised voices); off for loops a file brings with it, which are kept exactly
inline Sound sampled(const char* name, const char* title, const char* cat, double fs, int root, Vec x, double loopFrom, double loopTo, Controls c,
                     bool crossfade = true, bool normalize = true) {
    Sound s; s.name = name; s.title = title; s.category = cat; s.mode = Mode::Sample; s.rate = fs; s.root = root; s.ctl = c;
    if (x.size() > (size_t) RAM_SIZE) x.resize((size_t) RAM_SIZE);
    // remove DC
    double m = 0; for (float v : x) m += v; m /= std::max<size_t>(1, x.size());
    for (auto& v : x) v -= (float) m;
    const size_t pagesN = (x.size() + PAGE - 1) / PAGE;
    x.resize(pagesN * PAGE, 0.f);
    if (loopFrom >= 0) {
        int ls = (int) std::lround(loopFrom * fs / PAGE), le = (int) std::floor(loopTo * fs / PAGE) - 1;
        le = std::min(le, (int) pagesN - 1); ls = clampi(ls, crossfade ? 1 : 0, std::max(crossfade ? 1 : 0, le));
        if (le > ls || (!crossfade && le >= ls)) {
            s.loopStart = ls; s.loopEnd = le;
            const int span = (le - ls + 1) * PAGE;
            const int C = crossfade ? std::min({ 2048, ls * PAGE, span / 2 }) : 0;
            const int endI = (le + 1) * PAGE, preI = ls * PAGE;
            for (int i = 0; i < C; ++i) {
                const double w = (double) (i + 1) / C;
                const size_t a = (size_t) (endI - C + i), b = (size_t) (preI - C + i);
                x[a] = (float) (x[a] * std::cos(w * PI / 2) + x[b] * std::sin(w * PI / 2));
            }
        }
    } else {
        // one-shot: fade the last page so the sound ends at silence
        const size_t f = std::min<size_t>(x.size(), 256);
        for (size_t i = 0; i < f; ++i) x[x.size() - 1 - i] *= (float) (i / (double) f);
    }
    const double p = normalize ? std::max(1e-9, peakOf(x)) : 1.0;
    s.ram.resize(x.size());
    for (size_t i = 0; i < x.size(); ++i) s.ram[i] = (uint8_t) clampi((int) std::lround(x[i] / p * 126.0) + 128, 1, 255);
    return s;
}

// Page 4 style: harmonics(page, amps[32], phases[32]) for each of 128 segments
inline Sound waveform(const char* name, const char* title, const char* cat, const std::function<void(int, double*, double*)>& h,
                      int loopS, int loopE, Controls c) {
    Sound s; s.name = name; s.title = title; s.category = cat; s.mode = Mode::Waveform; s.rate = 128.0 * 440.0; s.root = 69; s.ctl = c;
    s.loopStart = loopS; s.loopEnd = loopE;
    std::vector<double> buf((size_t) RAM_SIZE, 0.0);
    for (int p = 0; p < PAGES; ++p) {
        double a[32] {}, ph[32] {};
        h(p, a, ph);
        for (int i = 0; i < PAGE; ++i) {
            double v = 0;
            for (int k = 0; k < 32; ++k) if (a[k] != 0) v += a[k] * std::sin(2 * PI * (k + 1) * i / PAGE + ph[k]);
            buf[(size_t) (p * PAGE + i)] = v;
        }
    }
    double pk = 1e-9; for (double v : buf) pk = std::max(pk, std::fabs(v));
    s.ram.resize((size_t) RAM_SIZE);
    for (size_t i = 0; i < buf.size(); ++i) s.ram[i] = (uint8_t) clampi((int) std::lround(buf[i] / pk * 126.0) + 128, 1, 255);
    return s;
}

inline Controls ctl(std::initializer_list<std::pair<const char*, float>> kv) {
    Controls c;
    for (const auto& p : kv) {
        const std::string k = p.first; const float v = p.second;
        if (k == "level") c.level = v; else if (k == "filter") c.filter = v; else if (k == "attack") c.attack = v;
        else if (k == "damp") c.damp = v; else if (k == "sustain") c.sustain = v; else if (k == "vib") c.vibDepth = v;
        else if (k == "vibRate") c.vibRate = v; else if (k == "vibDelay") c.vibDelay = v; else if (k == "porta") c.porta = v;
        else if (k == "velVol") c.velVol = v; else if (k == "velFilt") c.velFilt = v; else if (k == "loop") c.loop = v;
        else if (k == "seg") c.segTime = v; else if (k == "transpose") c.transpose = v;
    }
    return c;
}

}  // namespace gen

// ---------------------------------------------------------------------------------------------------------------
// the library and its presets
// ---------------------------------------------------------------------------------------------------------------
class Library {
public:
    static Library& get() { static Library l; return l; }

    Bank bank;
    int size() const { return bank.count.load(); }
    const Sound* sound(int i) const { return bank.at(i); }
    int find(const std::string& name) const { for (int i = 0; i < size(); ++i) if (sound(i) && sound(i)->name == name) return i; return -1; }

    // user slots (loaded samples): replace the sound in a slot, keeping the old one alive
    // user voice k's sound index (NO_VOICE, always silent, sits among them at 255), and back (-1: not a user voice)
    static constexpr int slotOf(int k) { return k < NO_VOICE ? k : k + 1; }
    static constexpr int userOf(int i) { return i < 0 || i >= MAX_SOUNDS || i == NO_VOICE ? -1 : i < NO_VOICE ? i : i - 1; }
    int userSlot(int k) const { return slotOf(k); }
    static constexpr int USER_SLOTS = MAX_SOUNDS - 1;   // and NO_VOICE, always silent
    void setUser(int k, Sound s) {
        if (k < 0 || k >= USER_SLOTS) return;
        auto p = std::make_unique<Sound>(std::move(s));
        bank.slot[(size_t) slotOf(k)].store(p.get());
        std::lock_guard<std::mutex> g(lock); owned.push_back(std::move(p));
    }

    // Page R: the factory song (pattern 1 is the one on the famous screen). Its tracks play the CMI voices these
    // names find among the voices loaded (connectPageR in Catalog.h loads them from your library); none: silent
    static const char* trackVoices(int t) {
        static const char* v[TRACKS] = { "KICK1|BDRUM|KICK2", "SNARE1|LMSNARE|SNARE", "CROSSTIX|RRIM", "ORCH5|ORCH2|ORCHHIT", "HIHAT2|HIHAT1|HHCLOSD",
                                         "HHOPEN1|HHOPNCL1", "CLAPGATE|CLAP|SNAREA10", "BASSGT|BASS1|PICKBASS" };
        return v[t];
    }
    static void factorySeq(SeqData& d, const Bank& bank) {
        for (int t = 0; t < TRACKS; ++t) {
            d.trk[t].sound = NO_VOICE; d.trk[t].level = 0.7f; d.trk[t].audible = true; d.trk[t].pan = 0.5f;
            std::string want = trackVoices(t); size_t a = 0; bool found = false;
            while (!found && a <= want.size()) { size_t b = want.find('|', a); if (b == std::string::npos) b = want.size(); const std::string w = want.substr(a, b - a); a = b + 1;
                for (int i = 0; i < bank.count.load(); ++i) if (const Sound* x = bank.at(i)) if (x->name == w) { d.trk[t].sound = i; found = true; break; } }
        }
        d.trk[4].pan = 0.62f; d.trk[5].pan = 0.62f; d.trk[2].pan = 0.4f;
        for (auto& p : d.pat) for (auto& tr : p.st) for (auto& s : tr) s = {};
        auto on = [&](int p, int t, std::initializer_list<int> steps, int vel = 110, int note = 0) { for (int s : steps) d.pat[p].st[t][s] = { (uint8_t) vel, (int8_t) note }; };
        // pattern 1
        on(0, 0, { 0, 4, 8, 12 }); on(0, 1, { 4, 12 }); on(0, 2, { 15 }, 90);
        for (int s = 0; s < 16; ++s) on(0, 4, { s }, s % 2 ? 70 : 100);
        on(0, 5, { 2, 10 }, 90); on(0, 6, { 12, 13, 14, 15 }, 100);
        // pattern 2: + bass line
        d.pat[1] = d.pat[0];
        const int bl[16] = { 0, -99, 0, 12, -99, 0, -99, 10, 0, -99, 0, 12, -99, 7, -99, 10 };
        for (int s = 0; s < 16; ++s) if (bl[s] > -99) d.pat[1].st[7][s] = { 110, (int8_t) (bl[s] - 3) };
        // pattern 3: orchestra stabs over the bass, no claps
        d.pat[2] = d.pat[1];
        for (int s = 12; s < 16; ++s) d.pat[2].st[6][s] = {};
        on(2, 3, { 0 }, 120, 0); on(2, 3, { 3 }, 110, 0); on(2, 3, { 6 }, 120, 3); on(2, 3, { 10 }, 110, -2);
        // pattern 4: breakdown fill
        on(3, 0, { 0, 3, 6, 10 }); on(3, 1, { 4, 12, 13, 14, 15 }, 100); on(3, 3, { 0 }, 127, 5); on(3, 3, { 8 }, 120, 3);
        for (int s = 0; s < 16; s += 2) on(3, 4, { s }, 90);
        on(3, 6, { 12, 14 });
        const int song[] = { 0, 0, 1, 1, 2, 2, 1, 3 };
        d.songLen = 8; for (int i = 0; i < 8; ++i) d.song[i] = song[i];
        d.swing = 0;
    }

    int builtIn = 0;
    // projects saved before the built-in voices went: their voice numbers counted 51 of them first
    static constexpr int OLD_BUILT_IN = 51;
    static int fromOldIndex(int i) { return i < 0 ? i : i >= OLD_BUILT_IN && i - OLD_BUILT_IN < USER_SLOTS ? i - OLD_BUILT_IN : NO_VOICE; }
    std::vector<const Sound*> blank;   // the empty user voices

    // copies the library into another bank (each plugin instance or app has its own, for its own user samples)
    void fill(Bank& b) const { const int n = size(); for (int i = 0; i < n; ++i) b.slot[(size_t) i].store(bank.at(i)); b.count = n; }

private:
    Library() {
        // no built-in sounds: every voice comes from your library folder, your samples or your drawings.
        // Empty voices are silent until something is loaded into them.
        builtIn = 0;
        Sound empty; empty.name = "--"; empty.title = "Empty: set your library folder (Page F) or load a sound"; empty.category = "User";
        empty.mode = Mode::Sample; empty.ram.assign((size_t) PAGE, 0x80); empty.rate = 128 * 440.0; empty.root = 69;
        for (int k = 0; k < USER_SLOTS; ++k) {
            Sound s = empty;
            auto p = std::make_unique<Sound>(std::move(s)); blank.push_back(p.get()); bank.slot[(size_t) slotOf(k)].store(p.get()); owned.push_back(std::move(p));
        }
        empty.name = "--"; empty.title = "No voice";
        auto none = std::make_unique<Sound>(empty); bank.slot[(size_t) NO_VOICE].store(none.get()); owned.push_back(std::move(none));
        bank.count = MAX_SOUNDS;
    }
    std::vector<std::unique_ptr<Sound>> owned;
    std::mutex lock;
};

// One instance's sounds: the library plus its own user samples
class Kit {
public:
    Bank bank;
    Kit() { Library::get().fill(bank); }
    int userSlot(int k) const { return Library::slotOf(k); }
    void setUser(int k, Sound s) {
        if (k < 0 || k >= Library::USER_SLOTS) return;
        auto p = std::make_unique<Sound>(std::move(s));
        bank.slot[(size_t) userSlot(k)].store(p.get());
        std::lock_guard<std::mutex> g(lock); owned.push_back(std::move(p));
    }
    const Sound* user(int k) const { return bank.at(userSlot(k)); }
    void clearUser(int k) { if (k >= 0 && k < Library::USER_SLOTS) bank.slot[(size_t) userSlot(k)].store(Library::get().blank[(size_t) k]); }
    // a library voice's place kept for it (a project reopened before the library is read): silent until
    // fillLibraryPlaces (Catalog.h) loads it there
    void keepPlace(int k, const std::string& name, const std::string& disk) {
        Sound s; s.name = name; s.category = disk; s.title = name + " from " + disk + " (waiting for your library folder)";
        s.fromLibrary = true; s.waiting = true; s.ram.assign((size_t) PAGE, 0x80); s.rate = 128 * 440.0; s.root = 69;
        setUser(k, std::move(s));
    }
    // your own voices (sampled, imported, drawn) and the ones loaded from the library folder
    int ownCount() const { int n = 0; for (int k = 0; k < Library::USER_SLOTS; ++k) n += userLoaded(k) && !user(k)->fromLibrary; return n; }
    int libraryCount() const { int n = 0; for (int k = 0; k < Library::USER_SLOTS; ++k) n += userLoaded(k) && user(k)->fromLibrary; return n; }
    int firstFree(int from = 0) const { for (int k = std::max(0, from); k < Library::USER_SLOTS; ++k) if (!userLoaded(k)) return k; return -1; }
    int loadedCount() const { int n = 0; for (int k = 0; k < Library::USER_SLOTS; ++k) n += userLoaded(k); return n; }
    bool userLoaded(int k) const { return k >= 0 && k < Library::USER_SLOTS && user(k) != Library::get().blank[(size_t) k]; }
private:
    std::vector<std::unique_ptr<Sound>> owned; std::mutex lock;
};

// A voice drawn with the light pen: a start and an end cycle (128 points each, -1..1), morphing across the 128
// segments as the CMI's waveform voices do (start == end: one waveform all the way).
inline Sound drawnVoice(const std::string& name, const float* start, const float* end) {
    Sound s; s.name = name.substr(0, 8); s.title = "Drawn with the light pen"; s.category = "User"; s.mode = Mode::Waveform;
    s.rate = 128.0 * 440.0; s.root = 69; s.loopStart = 96; s.loopEnd = 127;
    Controls c; c.attack = 2; c.damp = 10; c.segTime = 0.02f; s.ctl = c;
    s.ram.resize((size_t) RAM_SIZE);
    float pk = 1e-6f; for (int i = 0; i < PAGE; ++i) pk = std::max({ pk, std::fabs(start[i]), std::fabs(end[i]) });
    for (int p = 0; p < PAGES; ++p) {
        const float u = std::min(1.f, p / 96.f);
        // remove any offset so the cycle sits on the zero line
        double m = 0; for (int i = 0; i < PAGE; ++i) m += start[i] * (1 - u) + end[i] * u; m /= PAGE;
        for (int i = 0; i < PAGE; ++i) s.ram[(size_t) (p * PAGE + i)] = (uint8_t) clampi((int) std::lround(((start[i] * (1 - u) + end[i] * u) - m) / pk * 126.0) + 128, 1, 255);
    }
    return s;
}

// Turns any audio (mono floats at srcRate) into a CMI sound, like Page 8 sampling.
//  - A sound that already fits a channel card (at most 32 kHz and 16 KB, as the CMI library's own sounds are) is
//    kept sample for sample at its own rate.
//  - Anything else is resampled to the highest CMI rate that fits 16 KB (never above 32 kHz), anti-aliased first.
//  - fileLoopStart / fileLoopEnd (samples at srcRate, end inclusive): a loop the file brings (WAV "smpl" chunk, AIFF
//    markers); it's kept exactly, snapped to whole pages as the card needs. Without one, loop = loop the last 45%.
inline Sound sampleToCmi(const std::vector<float>& in, double srcRate, const std::string& name, int root, bool loop,
                         long fileLoopStart = -1, long fileLoopEnd = -1) {
    using namespace gen;
    const bool fileLoop = fileLoopStart >= 0 && fileLoopEnd > fileLoopStart && fileLoopStart < (long) in.size();
    // what has to fit: with a loop, everything up to its end
    const double need = (fileLoop ? std::min<double>((double) in.size(), fileLoopEnd + 1.0) : (double) in.size()) / srcRate;
    double rate;
    const bool native = srcRate <= 32000.5 && srcRate >= 4000 && need * srcRate <= RAM_SIZE;
    if (native) rate = srcRate;
    else rate = clampd(std::min({ 32000.0, srcRate, (double) RAM_SIZE / std::max(need, 1e-3) }), 4000.0, 32000.0);
    const double dur = in.size() / srcRate;
    const size_t n = (size_t) std::min<double>(RAM_SIZE, std::floor(dur * rate + 1e-6));
    Vec y(n);
    if (native) { for (size_t i = 0; i < n; ++i) y[i] = in[i]; }
    else {
        std::vector<float> f = in;
        if (rate < srcRate) { BQ a = BQ::lp(srcRate, rate * 0.45, 0.54), b = BQ::lp(srcRate, rate * 0.45, 1.3); for (auto& v : f) v = (float) b.run(a.run(v)); }
        for (size_t i = 0; i < n; ++i) {
            const double p = i * srcRate / rate; const size_t k = (size_t) p; const double fr = p - k;
            const float a = k < f.size() ? f[k] : 0.f, b = k + 1 < f.size() ? f[k + 1] : a;
            y[i] = (float) (a + (b - a) * fr);
        }
    }
    std::string nm = name; for (auto& c : nm) c = (char) std::toupper((unsigned char) c);
    nm.erase(std::remove_if(nm.begin(), nm.end(), [](char c) { return !std::isalnum((unsigned char) c); }), nm.end());
    if (nm.size() > 8) nm.resize(8);
    if (nm.empty()) nm = "SAMPLE";
    const double secs = n / rate;
    const std::string title = "Sampled: " + name;
    if (fileLoop) {
        const double ls = fileLoopStart / srcRate, le = (fileLoopEnd + 1.0) / srcRate;
        // page-aligned already (sounds from a CMI): kept exactly; otherwise the seam is smoothed
        const double lsP = ls * rate / PAGE, leP = le * rate / PAGE;
        const bool aligned = std::fabs(lsP - std::round(lsP)) < 0.02 && std::fabs(leP - std::round(leP)) < 0.02;
        return gen::sampled(nm.c_str(), title.c_str(), "User", rate, root, y, ls, std::min(le, secs), Controls(), !aligned, true);
    }
    return gen::sampled(nm.c_str(), title.c_str(), "User", rate, root, y, loop ? secs * 0.55 : -1, secs, Controls());
}

// A WAV file in memory: PCM 8/16/24/32-bit or float, any channels (mixed to mono), its "smpl" chunk (root key and
// first loop). Used where there's no audio library (the Android app).
struct WavData { std::vector<float> mono; double rate = 0; int root = -1; long loopStart = -1, loopEnd = -1; std::string err; };
inline WavData parseWav(const uint8_t* d, size_t n, size_t maxFrames = 32000 * 12) {
    WavData w;
    auto u16 = [&](size_t o) { return o + 2 <= n ? (uint32_t) (d[o] | d[o + 1] << 8) : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t) (d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t) d[o + 3] << 24) : 0u; };
    if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) { w.err = "NOT A WAV FILE"; return w; }
    int fmt = 0, ch = 0, bits = 0; size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= n;) {
        const uint32_t len = u32(o + 4); const size_t body = o + 8;
        if (!std::memcmp(d + o, "fmt ", 4)) {
            fmt = (int) u16(body); ch = (int) u16(body + 2); w.rate = u32(body + 4); bits = (int) u16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = (int) u16(body + 24);   // extensible: the sub-format's first word
        } else if (!std::memcmp(d + o, "data", 4)) { dataOff = body; dataLen = std::min<size_t>(len, n - body); }
        else if (!std::memcmp(d + o, "smpl", 4) && len >= 36) {
            const uint32_t unity = u32(body + 12), loops = u32(body + 28);
            if (unity > 0 && unity < 128) w.root = (int) unity;
            if (loops > 0 && len >= 36 + 24) { w.loopStart = (long) u32(body + 36 + 8); w.loopEnd = (long) u32(body + 36 + 12); }
        }
        o = body + len + (len & 1);
    }
    if (!dataOff || ch <= 0 || w.rate <= 0) { w.err = "NO AUDIO IN WAV"; return w; }
    if (!(fmt == 1 || fmt == 3) || !(bits == 8 || bits == 16 || bits == 24 || bits == 32) || (fmt == 3 && bits != 32)) { w.err = "UNSUPPORTED WAV FORMAT"; return w; }
    const size_t bps = (size_t) bits / 8, frames = std::min(maxFrames, dataLen / (bps * (size_t) ch));
    w.mono.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        double s = 0;
        for (int c = 0; c < ch; ++c) {
            const uint8_t* p = d + dataOff + (f * (size_t) ch + (size_t) c) * bps;
            double v;
            if (fmt == 3) { float x; std::memcpy(&x, p, 4); v = x; }
            else if (bits == 8) v = (p[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t) (p[0] | p[1] << 8) / 32768.0;
            else if (bits == 24) { int32_t x = p[0] | p[1] << 8 | p[2] << 16; if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0; }
            else { int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.0; }
            s += v;
        }
        w.mono[f] = (float) (s / ch);
    }
    if (w.loopEnd >= (long) frames) w.loopEnd = (long) frames - 1;
    if (w.loopStart >= w.loopEnd) w.loopStart = w.loopEnd = -1;
    return w;
}

}  // namespace cmi
