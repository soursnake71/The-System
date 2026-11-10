// Synthetic bass lines with exact ground truth for tests/test_bass.cpp.
//  make_acid():     303-style line: PolyBLEP saw/square -> resonant 4-pole ladder low-pass with envelope, VCA gate,
//                   16ths at 126 BPM with human timing jitter, slides (exponential glide, no re-attack), accents,
//                   rests, octave jumps.
//  make_fingered(): electric-bass-like line: inharmonic decaying partials with a pluck transient, E1..G3, staccato /
//                   normal / legato notes, a bend, a slide into a note, vibrato, mutes, accents.
//  add_bleed():     kick-drum thumps (pitch-swept sine bursts) at a given level, noise and a quiet pad (other-instrument
//                   bleed), as in a Demucs bass stem.
// Everything is deterministic (own RNG), sample-exact truth: note.time is the first sample of the attack (or of the
// glide for a slid-into note), note.end the gate-off / mute start (or the next note's start for legato and slides).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace bsynth {

constexpr double kPi = 3.14159265358979323846;

struct TruthNote {
    double time = 0, end = 0;
    int note = 0;
    bool slide = false;   // glides into the next note without a re-attack
    bool accent = false;
    bool bend = false;    // has an in-note pitch movement (bend / vibrato / slide-in)
};

struct Line {
    std::string name;
    std::vector<float> sig;
    std::vector<TruthNote> notes;
    double bpm = 120, first_beat = 0;
};

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint64_t next() {  // splitmix64
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
    double uni(double a, double b) { return a + (b - a) * uni(); }
    int pick(int n) { return int(uni() * n) % n; }
    double gauss() {
        double u1 = std::max(uni(), 1e-12), u2 = uni();
        return std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
    }
};

inline double midi_hz(double m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

inline double poly_blep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

// ------------------------------------------------------------------------------------------------ 303 acid line
inline Line make_acid(uint64_t seed, int sr = 44100) {
    Line L;
    L.name = "a: 303 acid";
    L.bpm = 126;
    Rng rng(seed);
    const double step = 60.0 / L.bpm / 4;
    const int nsteps = 128;
    const double t0 = 0.25;
    L.first_beat = t0;
    const int root = 33;  // A1
    const int offs[] = {-5, 0, 0, 0, 3, 5, 7, 10, 12, 12, 15, 19, 24};
    struct Step { bool on = false; int note = 0; bool accent = false, slide = false; long n = 0; };
    std::vector<Step> st(nsteps);
    for (int i = 0; i < nsteps; ++i) {
        st[i].on = rng.uni() > 0.2;
        st[i].note = root + offs[rng.pick(13)];
        st[i].accent = rng.uni() < 0.25;
        double jit = rng.uni(-0.005, 0.005);
        st[i].n = std::lround((t0 + i * step + jit) * sr);
    }
    for (int i = 0; i + 1 < nsteps; ++i) {
        if (st[i].on && st[i + 1].on && rng.uni() < 0.25) {
            st[i].slide = true;
            if (st[i + 1].note == st[i].note) st[i + 1].note += (rng.uni() < 0.5 ? 5 : -2);
            // 303 slides stay within about an octave
            while (st[i + 1].note - st[i].note > 12) st[i + 1].note -= 12;
            while (st[i].note - st[i + 1].note > 12) st[i + 1].note += 12;
            st[i + 1].accent = false;  // a slid-into note has no attack, so no accent
        }
    }
    st[nsteps - 1].slide = false;
    // truth
    for (int i = 0; i < nsteps; ++i) {
        if (!st[i].on) continue;
        TruthNote tn;
        tn.time = double(st[i].n) / sr;
        tn.note = st[i].note;
        tn.accent = st[i].accent;
        tn.slide = st[i].slide;
        long gate_off = st[i].n + std::lround(0.55 * step * sr);
        tn.end = st[i].slide ? double(st[i + 1].n) / sr : double(gate_off) / sr;
        tn.bend = i > 0 && st[i - 1].on && st[i - 1].slide;  // the glide is in the slid-into note's bends
        L.notes.push_back(tn);
    }
    // events per sample
    const long N = std::lround((t0 + nsteps * step + 0.6) * sr);
    std::vector<float> out(N, 0.f);
    double phase = 0, pitch = root, target = root;
    double amp = 0, amp_peak = 0.5, fenv = 0, fmod = 2.5, res = 2.8;
    bool gate = false, gliding = false;
    long gate_off_n = -1;
    long attack_start = -1, attack_len = std::lround(0.0015 * sr);
    double attack_from = 0;
    double y1 = 0, y2 = 0, y3 = 0, y4 = 0;
    const double tau_glide = 0.015, tau_rel = 0.005, tau_dec = 0.4, tau_f = 0.16;
    const double kg = 1 - std::exp(-1.0 / (tau_glide * sr));
    const double kr = std::exp(-1.0 / (tau_rel * sr)), kd = std::exp(-1.0 / (tau_dec * sr));
    const double kf = std::exp(-1.0 / (tau_f * sr));
    int si = 0;
    for (long n = 0; n < N; ++n) {
        while (si < nsteps && st[si].n == n) {
            const Step& s = st[si];
            bool slid_into = si > 0 && st[si - 1].on && st[si - 1].slide;
            if (s.on) {
                target = s.note;
                if (slid_into) {
                    gliding = true;
                } else {
                    gliding = false;
                    pitch = target;
                    gate = true;
                    attack_start = n;
                    attack_from = amp;
                    amp_peak = s.accent ? 1.0 : 0.5;
                    fenv = 1.0;
                    fmod = s.accent ? 3.3 : 2.4;
                    res = s.accent ? 3.2 : 2.8;
                }
                gate_off_n = s.slide ? -1 : s.n + std::lround(0.55 * step * sr);
            }
            ++si;
        }
        if (n == gate_off_n) gate = false;
        if (gliding) pitch += (target - pitch) * kg;
        // VCA
        if (gate) {
            if (attack_start >= 0 && n - attack_start < attack_len)
                amp = attack_from + (amp_peak - attack_from) * double(n - attack_start + 1) / attack_len;
            else
                amp *= kd;
        } else {
            amp *= kr;
        }
        fenv *= kf;
        // oscillator (saw for the first half, square for the second)
        double f = midi_hz(pitch), dt = f / sr;
        double t = double(n) / sr;
        double osc;
        if (t < t0 + nsteps * step / 2) {
            osc = 2 * phase - 1 - poly_blep(phase, dt);
        } else {
            double p2 = phase + 0.5;
            if (p2 >= 1) p2 -= 1;
            osc = (2 * phase - 1 - poly_blep(phase, dt)) - (2 * p2 - 1 - poly_blep(p2, dt));
            osc *= 0.5;
        }
        phase += dt;
        if (phase >= 1) phase -= 1;
        // filter: a slowly sweeping cutoff plus the envelope
        double base = 260 * std::pow(2.0, 1.4 * (0.5 + 0.5 * std::sin(2 * kPi * t / 7.3)));
        double fc = std::min(base * std::pow(2.0, fmod * fenv), 0.2 * sr);
        double g = 1 - std::exp(-2 * kPi * fc / sr);
        double u = std::tanh(osc - res * y4);
        y1 += g * (u - y1);
        y2 += g * (y1 - y2);
        y3 += g * (y2 - y3);
        y4 += g * (y3 - y4);
        out[n] = float(0.6 * amp * y4 * (1 + 0.5 * res));
    }
    L.sig = std::move(out);
    return L;
}

// ------------------------------------------------------------------------------------------------ fingered bass
struct PluckSpec {
    long n0 = 0, n_end = 0;  // start, end (mute start or next note's start)
    bool legato = false;     // ended by the next pluck on the same string (fast damp)
    double midi = 40, gain = 1;
    // pitch modulation (semitones) as a function of time since the onset
    int kind = 0;  // 0 none, 1 bend up and back, 2 slide into the note from below, 3 vibrato
};

inline double pluck_mod(const PluckSpec& p, double t) {
    switch (p.kind) {
    case 1: {  // bend: start at 0.12 s, up 2 st over 0.11 s, hold 0.08 s, back over 0.11 s
        double a = 0.12, r = 0.11, h = 0.08;
        if (t < a) return 0;
        if (t < a + r) return 2.0 * 0.5 * (1 - std::cos(kPi * (t - a) / r));
        if (t < a + r + h) return 2.0;
        if (t < a + 2 * r + h) return 2.0 * 0.5 * (1 + std::cos(kPi * (t - a - r - h) / r));
        return 0;
    }
    case 2:  // slide into the note: from -2 st, linear over 70 ms
        return t < 0.07 ? -2.0 * (1 - t / 0.07) : 0.0;
    case 3:  // vibrato +-0.3 st at 5.5 Hz after 0.15 s
        return t < 0.15 ? 0.0 : 0.3 * std::sin(2 * kPi * 5.5 * (t - 0.15));
    }
    return 0;
}

inline void render_pluck(std::vector<float>& out, const PluckSpec& p, Rng& rng, int sr) {
    const double f0 = midi_hz(p.midi);
    const double B = 1.5e-4, beta = 0.22, tau0 = 1.6 - 0.02 * (p.midi - 28);
    int K = 0;
    while ((K + 1) * f0 * std::sqrt(1 + B * (K + 1) * (K + 1)) < 4500 && K < 120) ++K;
    std::vector<double> amp(K), ph(K), dec(K), ratio(K);
    double norm = 0;
    for (int k = 1; k <= K; ++k) {
        double a = std::fabs(std::sin(kPi * k * beta)) / std::pow(k, 1.15);
        if (k == 1 && f0 < 50) a *= 0.45;  // weak fundamental on the low strings (pickup / speaker)
        amp[k - 1] = a;
        norm += a * a;
        ph[k - 1] = rng.uni(0, 2 * kPi);
        dec[k - 1] = std::exp(-1.0 / (tau0 / (1 + 0.12 * (k - 1)) * sr));
        ratio[k - 1] = k * std::sqrt(1 + B * k * k);
    }
    norm = std::sqrt(norm);  // equal loudness (RMS) across the range: the note's gain is its level
    for (auto& a : amp) a *= p.gain * 0.25 / norm;
    const double tau_mute = 0.007, tau_damp = 0.003;
    const long tail = std::lround((p.legato ? 8 * tau_damp : 9 * tau_mute) * sr);
    const long nlast = std::min<long>(long(out.size()), p.n_end + tail);
    const long ramp = std::lround(0.0004 * sr);
    const double kmute = std::exp(-1.0 / ((p.legato ? tau_damp : tau_mute) * sr));
    double gmute = 1, lp = 0, nenv = 0.1 * p.gain;
    const double knoise = std::exp(-1.0 / (0.0025 * sr)), alp = 1 - std::exp(-2 * kPi * 2500.0 / sr);
    std::vector<double> env(K, 1.0);
    for (long n = p.n0; n < nlast; ++n) {
        double t = double(n - p.n0) / sr;
        double f = f0 * std::pow(2.0, pluck_mod(p, t) / 12.0);
        double s = 0;
        for (int k = 0; k < K; ++k) {
            double fk = f * ratio[k];
            if (fk < 0.45 * sr) s += amp[k] * env[k] * std::sin(ph[k]);
            ph[k] += 2 * kPi * fk / sr;
            if (ph[k] > 2 * kPi) ph[k] -= 2 * kPi;
            env[k] *= dec[k];
        }
        // pluck transient: low-passed noise burst
        lp += alp * (rng.uni(-1, 1) - lp);
        s += nenv * lp;
        nenv *= knoise;
        double r = n - p.n0 < ramp ? 0.5 * (1 - std::cos(kPi * double(n - p.n0 + 1) / ramp)) : 1.0;
        if (n >= p.n_end) gmute *= kmute;
        out[n] += float(s * r * gmute);
    }
}

inline Line make_fingered(uint64_t seed, int sr = 44100) {
    Line L;
    L.name = "b: fingered bass";
    L.bpm = 100;
    L.first_beat = 0.3;
    Rng rng(seed);
    const double e8 = 60.0 / L.bpm / 2;  // an eighth note
    const int scale[] = {0, 2, 3, 5, 7, 8, 10};
    std::vector<PluckSpec> ps;
    double slot = 0;  // in eighths
    int deg = 0, oct = 0;
    const int nnotes = 72;
    for (int i = 0; i < nnotes; ++i) {
        // pitch: random walk on E minor from E1 (28) to G3 (55)
        int stepd = rng.pick(5) - 2;
        if (rng.uni() < 0.15) stepd = (rng.uni() < 0.5 ? 7 : -7);
        deg += stepd;
        int m;
        for (;;) {
            int d = ((deg % 7) + 7) % 7, o = (deg - d) / 7;
            m = 28 + 12 * (o + oct) + scale[d];
            if (m < 28) { deg += 7; continue; }
            if (m > 55) { deg -= 7; continue; }
            break;
        }
        double dur_slots = (rng.uni() < 0.35) ? 0.5 : (rng.uni() < 0.7 ? 1.0 : (rng.uni() < 0.6 ? 2.0 : 3.0));
        PluckSpec p;
        p.midi = m;
        p.gain = std::pow(10.0, rng.uni(-2.5, 2.5) / 20);
        double jit = rng.uni(-0.006, 0.006);
        p.n0 = std::lround((L.first_beat + slot * e8 + jit) * sr);
        ps.push_back(p);
        slot += dur_slots;
        if (rng.uni() < 0.12) slot += 0.5;  // a rest
    }
    // accents, special articulations
    for (size_t i = 0; i < ps.size(); ++i)
        if (rng.uni() < 0.08) ps[i].gain *= std::pow(10.0, 8.0 / 20);
    std::vector<bool> acc(ps.size());
    for (size_t i = 0; i < ps.size(); ++i) acc[i] = ps[i].gain > std::pow(10.0, 5.0 / 20);
    // lengths
    for (size_t i = 0; i < ps.size(); ++i) {
        long next = i + 1 < ps.size() ? ps[i + 1].n0 : ps[i].n0 + std::lround(2 * e8 * sr);
        long slotlen = next - ps[i].n0;
        double r = rng.uni();
        if (i + 1 < ps.size() && r < 0.3) {
            ps[i].legato = true;
            ps[i].n_end = next;
        } else if (r < 0.55) {
            ps[i].n_end = ps[i].n0 + std::lround(slotlen * rng.uni(0.3, 0.5));  // staccato
        } else {
            ps[i].n_end = ps[i].n0 + std::lround(slotlen * rng.uni(0.6, 0.9));
        }
        // at least 45 ms
        ps[i].n_end = std::max(ps[i].n_end, ps[i].n0 + std::lround(0.045 * sr));
        if (!ps[i].legato) ps[i].n_end = std::min(ps[i].n_end, next - std::lround(0.012 * sr));
    }
    // a bend on a long note, a slide-in, a vibrato
    auto longest_from = [&](size_t from, double min_len) -> long {
        for (size_t i = from; i < ps.size(); ++i)
            if (double(ps[i].n_end - ps[i].n0) / sr >= min_len && ps[i].kind == 0) return long(i);
        return -1;
    };
    long ib = longest_from(8, 0.45), is = longest_from(30, 0.12), iv = longest_from(45, 0.5);
    if (ib >= 0) ps[ib].kind = 1;
    if (is >= 0) ps[is].kind = 2;
    if (iv >= 0) ps[iv].kind = 3;
    long N = ps.back().n_end + std::lround(0.6 * sr);
    L.sig.assign(N, 0.f);
    for (size_t i = 0; i < ps.size(); ++i) {
        render_pluck(L.sig, ps[i], rng, sr);
        TruthNote tn;
        tn.time = double(ps[i].n0) / sr;
        tn.end = double(ps[i].n_end) / sr;
        tn.note = int(ps[i].midi);
        tn.accent = acc[i];
        tn.bend = ps[i].kind != 0;
        L.notes.push_back(tn);
    }
    return L;
}

// ------------------------------------------------------------------------------------------------ bleed
// kick thumps on every beat (pitch-swept sine bursts) at kick_db relative to the line's peak, white noise at
// noise_db and a quiet pad (other-instrument bleed) at pad_db.
inline void add_bleed(Line& L, double kick_db, double noise_db, double pad_db, uint64_t seed, int sr = 44100) {
    Rng rng(seed);
    float peak = 0;
    for (float v : L.sig) peak = std::max(peak, std::fabs(v));
    const double ka = peak * std::pow(10.0, kick_db / 20), na = peak * std::pow(10.0, noise_db / 20) * std::sqrt(3.0);
    const double pa = peak * std::pow(10.0, pad_db / 20);
    const long N = long(L.sig.size());
    const double beat = 60.0 / L.bpm;
    for (double tb = L.first_beat; tb < double(N) / sr - 0.3; tb += beat) {
        long n0 = std::lround((tb + rng.uni(-0.002, 0.002)) * sr);
        double ph = 0;
        for (long n = n0; n < std::min(N, n0 + long(0.35 * sr)); ++n) {
            double t = double(n - n0) / sr;
            double f = 45 + 110 * std::exp(-t / 0.012);
            double a = std::exp(-t / 0.045) * std::min(1.0, t / 0.0005);
            L.sig[n] += float(ka * a * std::sin(ph));
            ph += 2 * kPi * f / sr;
        }
    }
    // pad: a soft triad around 300-600 Hz changing every 2 s
    const int chords[][3] = {{64, 67, 71}, {62, 65, 69}, {60, 64, 67}, {65, 69, 72}};
    double ph[3] = {0, 0, 0};
    for (long n = 0; n < N; ++n) {
        int c = int(double(n) / sr / 2.0) % 4;
        double s = 0;
        for (int j = 0; j < 3; ++j) {
            ph[j] += 2 * kPi * midi_hz(chords[c][j]) / sr;
            s += std::sin(ph[j]) + 0.3 * std::sin(2 * ph[j]);
        }
        L.sig[n] += float(pa * s / 3 + na * rng.uni(-1, 1));
    }
    L.name += " + kick/noise";
}

}  // namespace bsynth
