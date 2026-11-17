// Omega Wheel of Steel DSP: decks (platter physics, scratch, key lock, cues, loops, slip, sync), beat FX and the mixer.
// Plain C++17, no JUCE. Everything that runs per sample is allocation- and lock-free; prepare() allocates.
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define WHEEL_SSE 1
#endif

namespace wheel {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSecPerRev = 1.8;   // one platter turn at 33 1/3 rpm = 1.8 s of the record (also at 45: it plays faster)
constexpr double kNudge = 0.04;      // nudge: +-4 %

template <typename T> inline T clampv(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double coef(double tauSec, double sr) { return tauSec <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (tauSec * sr)); }
inline float dbGain(float db) { return std::pow(10.0f, db * 0.05f); }

// flush denormals for the duration of a render call (restored on exit)
struct NoDenormals {
#if WHEEL_SSE
    unsigned int old;
    NoDenormals() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040); }
    ~NoDenormals() { _mm_setcsr(old); }
#elif defined(__aarch64__)
    uint64_t old;
    NoDenormals() { asm volatile("mrs %0, fpcr" : "=r"(old)); uint64_t v = old | (1ull << 24); asm volatile("msr fpcr, %0" : : "r"(v)); }
    ~NoDenormals() { asm volatile("msr fpcr, %0" : : "r"(old)); }
#elif defined(__arm__) && defined(__ARM_FP) && ! defined(__SOFTFP__)
    uint32_t old;   // 32-bit ARM (armeabi-v7a, VFP): FPSCR.FZ
    NoDenormals() { asm volatile("vmrs %0, fpscr" : "=r"(old)); asm volatile("vmsr fpscr, %0" : : "r"(old | (1u << 24))); }
    ~NoDenormals() { asm volatile("vmsr fpscr, %0" : : "r"(old)); }
#endif
};

// 4-point, 3rd-order Hermite (Catmull-Rom) between x0 and x1
inline float hermite(float xm1, float x0, float x1, float x2, float t) {
    const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

// reads a Track at a fractional sample position; silence outside, 2 ms fades at the very ends so running off the
// record never clicks
struct Reader {
    const float* L = nullptr; const float* R = nullptr; int64_t n = 0; double edge = 88;
    void set(const Track* t) {
        if (!t || t->L.size() < 8) { L = R = nullptr; n = 0; return; }
        L = t->L.data(); R = t->R.size() == t->L.size() ? t->R.data() : L; n = (int64_t) t->L.size();
        edge = std::max(2.0, t->sr * 0.002);
    }
    inline void read(double pos, float& oL, float& oR) const {
        if (n == 0 || pos <= -1.0 || pos >= (double) n) { oL = oR = 0; return; }
        const double fl = std::floor(pos); const int64_t i = (int64_t) fl; const float t = (float) (pos - fl);
        if (pos >= edge && pos < (double) n - 1 - edge) {
            const float* l = L + i - 1; const float* r = R + i - 1;
            oL = hermite(l[0], l[1], l[2], l[3], t); oR = hermite(r[0], r[1], r[2], r[3], t);
            return;
        }
        auto at = [this](const float* b, int64_t k) { return (k >= 0 && k < n) ? b[k] : 0.0f; };
        oL = hermite(at(L, i - 1), at(L, i), at(L, i + 1), at(L, i + 2), t);
        oR = hermite(at(R, i - 1), at(R, i), at(R, i + 1), at(R, i + 2), t);
        double g = 1;
        if (pos < edge) g = std::max(0.0, pos / edge);
        if (pos > (double) n - 1 - edge) g = std::min(g, std::max(0.0, ((double) n - 1 - pos) / edge));
        oL *= (float) g; oR *= (float) g;
    }
    inline float mono(int64_t k) const { return (k >= 0 && k < n) ? 0.5f * (L[k] + R[k]) : 0.0f; }
};
}  // namespace

// ================================================================================================================
// Deck
// ================================================================================================================
struct Deck::Impl {
    // host
    double H = 48000; int Xf = 192, W = 2208, Hs = 1104;
    std::vector<float> win, xfw, bufT, bufC;
    double kHand = 0, kSpeed = 0, kNudgeC = 0, kBend = 0, kSync = 0, kSg = 0, kKl = 0;
    float dcR = 0.9993f;
    // track
    Reader rd; double T = 44100, r0 = 1, N = 0; std::shared_ptr<const Track> prev;
    double bpm = 0, first = 0;   // effective grid (track or overridden)
    // transport (positions in track samples)
    double pos = 0, recVel = 0, motorNorm = 0, speedSm = 1, nudgeSm = 0, nudgeT = 0, lastDp = 0;
    bool motorOn = false;
    double syncSpeed = 1; bool syncLatched = false; float latchPitch = 0;
    double bendA = 0, bendB = 0, syncBend = 0;
    // hand on the record
    bool touched = false, moved = false;
    double handT = 0, handR = 0, rampVel = 0, y1 = 0, y2 = 0, moveIv = 480, sinceMove = 1e9;
    bool censor = false;
    // slip
    double shadow = 0; bool slipEv = false;
    // cue
    double cuePt = 0; int held = -1, slipCue = -1; bool preview = false;
    // loops
    double loopA = -1, loopB = -1, svA = -1, svB = -1; bool looping = false, rolling = false, svLooping = false;
    double rollStack[8] {}; int rollN = 0;
    // click-free jumps (vinyl path): the old read position fades out while the new one fades in
    struct Fade { Reader rd; double pos = 0, vel = 0; int k = 0; bool on = false; } fade;
    // key lock: WSOLA grains
    struct Grain { double rp = 0; int age = 0; float f = 1, fs = 0; bool on = false; } gr[8];
    int latest = -1, nextGrain = 0; bool klRun = false, klWant = false; double klMix = 0;
    // tone / output
    float lpL = 0, lpR = 0, sg = 0, dcxL = 0, dcxR = 0, dcyL = 0, dcyR = 0, pkL = 0, pkR = 0;
    double angle = 0;

    Impl() { setup(48000); }

    void setup(double rate) {
        H = rate > 1000 ? rate : 48000;
        Xf = std::max(16, (int) std::lround(0.004 * H));
        Hs = std::max(64, (int) std::lround(0.023 * H)); W = 2 * Hs;         // 46 ms grains, 50 % overlap
        win.assign((size_t) W + 1, 0.0f);
        for (int i = 0; i <= W; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2 * kPi * i / W));   // periodic Hann: sums to 1
        xfw.assign((size_t) Xf + 1, 0.0f);
        for (int i = 0; i <= Xf; ++i) xfw[(size_t) i] = (float) (0.5 + 0.5 * std::cos(kPi * i / Xf));
        bufT.assign(16384, 0.0f); bufC.assign(16384, 0.0f);
        kHand = coef(0.0022, H); kSpeed = coef(0.04, H); kNudgeC = coef(0.015, H); kBend = coef(0.012, H);
        kSync = coef(0.25, H); kSg = coef(0.0015, H); kKl = 1.0 / (0.015 * H);
        dcR = (float) (1.0 - 2 * kPi * 5.0 / H);
        r0 = T / H; moveIv = 0.01 * H;
        for (auto& g : gr) g.on = false;
        klRun = false; klMix = 0; fade.on = false;
    }

    double beatLen() const { return bpm > 0 ? 60.0 / bpm : 0.5; }
    // snap a time (s) to the grid of 'grid' beats
    double snap(double t, double grid, bool floorMode) const {
        if (bpm <= 0) return t;
        const double bl = beatLen() * grid, k = (t - first) / bl;
        return first + (floorMode ? std::floor(k + 1e-6) : std::round(k)) * bl;
    }
    double platter() const { return motorNorm * speedSm * (1.0 + kNudge * nudgeSm); }
    void startInstant() { motorOn = true; motorNorm = 1; recVel = speedSm * (1.0 + kNudge * nudgeSm); }
    void stopInstant() { motorOn = false; motorNorm = 0; recVel = 0; }

    // ---- key lock (WSOLA) ----
    int freeGrain() {
        int best = 0; float bw = 1e9f;
        for (int i = 0; i < 8; ++i) {
            if (!gr[i].on) return i;
            const float w = gr[i].f * win[(size_t) std::min(gr[i].age, W)];
            if (w < bw) { bw = w; best = i; }
        }
        return best;
    }
    // best start near 'nominal' whose first half continues the template (what the previous grain plays next)
    double search(double tplPos, double nominal) {
        const int64_t tp = (int64_t) std::floor(tplPos); const double fr = tplPos - (double) tp;
        const int64_t c0 = (int64_t) std::llround(nominal - fr);
        const int D = std::max(1, (int) std::lround(T / 11025.0));
        const int Lt = std::max(16, (int) (Hs * r0));
        int Sd = std::max(2, (int) (0.012 * T) / D), Ld = std::max(4, Lt / D);
        Ld = std::min(Ld, 6000); Sd = std::min(Sd, 4000);
        const int nc = Ld + 2 * Sd;
        double et = 0;
        for (int k = 0; k < Ld; ++k) {
            float s = 0; const int64_t b = tp + (int64_t) k * D;
            for (int j = 0; j < D; ++j) s += rd.mono(b + j);
            bufT[(size_t) k] = s; et += (double) s * s;
        }
        if (et < 1e-9) return nominal;
        const int64_t cb = c0 - (int64_t) Sd * D;
        for (int k = 0; k < nc; ++k) {
            float s = 0; const int64_t b = cb + (int64_t) k * D;
            for (int j = 0; j < D; ++j) s += rd.mono(b + j);
            bufC[(size_t) k] = s;
        }
        double e = 0;
        for (int k = 0; k < Ld; ++k) e += (double) bufC[(size_t) k] * bufC[(size_t) k];
        double best = -1e30; int bo = Sd;
        const float* tb = bufT.data();
        for (int o = 0; o <= 2 * Sd; ++o) {
            const float* cbp = bufC.data() + o;
            float num = 0;
            for (int k = 0; k < Ld; ++k) num += tb[k] * cbp[k];
            const double sc = num / std::sqrt(e + 1e-9);
            if (sc > best) { best = sc; bo = o; }
            if (o + Ld < nc) { e += (double) cbp[Ld] * cbp[Ld] - (double) cbp[0] * cbp[0]; if (e < 0) e = 0; }
        }
        int64_t bp = c0 + (int64_t) (bo - Sd) * D;
        if (D > 1) {   // refine at full resolution
            const int64_t pc = bp; best = -1e30;
            for (int64_t p = pc - D + 1; p <= pc + D - 1; ++p) {
                double num = 0, en = 0;
                for (int k = 0; k < Lt; k += 2) { const float a = rd.mono(tp + k), b = rd.mono(p + k); num += a * b; en += b * b; }
                const double sc = num / std::sqrt(en + 1e-9);
                if (sc > best) { best = sc; bp = p; }
            }
        }
        return (double) bp + fr;
    }
    void spawn() {
        Grain* t = (latest >= 0 && gr[latest].on && gr[latest].fs >= 0) ? &gr[latest] : nullptr;
        const double nominal = pos + (recVel - 1.0) * Hs * r0;   // grain centre lands where the timeline will be
        double start = nominal;
        if (t) start = std::abs(nominal - t->rp) < 0.5 ? t->rp : search(t->rp, nominal);
        const float f = t ? t->f : 1.0f, fs = t ? t->fs : 0.0f;
        const int i = freeGrain();
        gr[i] = Grain { start, 0, f, fs, true };
        latest = i; nextGrain = Hs;
    }
    // start the grain stream at 'pos' as if it had been running (a grain at its peak), optionally fading in while
    // the previous grains fade out (jumps)
    void klInit(bool crossfade) {
        for (auto& g : gr) {
            if (!g.on) continue;
            if (crossfade) { g.fs = -1.0f / (float) Xf; } else g.on = false;
        }
        const int i = freeGrain();
        gr[i] = Grain { pos, Hs, crossfade ? 0.0f : 1.0f, crossfade ? 1.0f / (float) Xf : 0.0f, true };
        latest = i;
        spawn();
    }

    void jumpTo(double np) {
        if (rd.n == 0) { pos = np; return; }
        if (!(fade.on && fade.k < Xf / 2)) { fade.pos = pos; fade.rd = rd; }
        fade.vel = lastDp; fade.k = 0; fade.on = true;
        const double dl = np - pos;
        pos = np;
        if (touched) { handT += dl; handR += dl; y1 += dl; y2 += dl; }
        if (klRun) klInit(true);
    }
};

#define WHEEL_IMPL if (!d) d = std::make_shared<Impl>(); Impl& s = *d

void Deck::prepare(double sr, int maxBlock) {
    (void) maxBlock;
    hostSr = sr;
    WHEEL_IMPL;
    s.setup(sr);
    s.r0 = s.T / s.H;
}

void Deck::load(std::shared_ptr<const Track> t) {
    WHEEL_IMPL;
    if (s.rd.n && (std::abs(s.lastDp) > 1e-6 || s.klRun)) {   // fade the old record out
        s.fade.rd = s.rd; s.fade.pos = s.pos; s.fade.vel = s.lastDp; s.fade.k = 0; s.fade.on = true;
    }
    s.prev = trk;
    trk = std::move(t);
    s.rd.set(trk.get());
    s.T = trk && trk->sr > 0 ? trk->sr : 44100; s.r0 = s.T / s.H; s.N = (double) s.rd.n;
    bpmOverride = 0; gridOff = 0; syncOn = false;
    s.bpm = trk ? trk->bpm : 0; s.first = trk ? trk->firstBeat : 0;
    if (trk) cues = trk->cues; else cues = {};
    s.stopInstant(); s.touched = false; s.censor = false; s.moved = false;
    s.looping = s.rolling = false; s.loopA = s.loopB = -1; s.rollN = 0;
    s.held = -1; s.slipCue = -1; s.preview = false; s.slipEv = false; s.syncLatched = false;
    s.bendA = s.bendB = s.syncBend = 0; s.nudgeT = 0;
    for (auto& g : s.gr) g.on = false;
    s.klRun = false; s.klMix = 0; s.lastDp = 0;
    s.cuePt = trk ? clampv(s.first, 0.0, trk->length()) : 0;
    s.pos = s.shadow = s.cuePt * s.T;
}

void Deck::playPause() { WHEEL_IMPL; play(!(s.motorOn && !s.preview)); }

void Deck::play(bool on) {
    WHEEL_IMPL;
    if (!trk || !s.rd.n) return;
    if (s.preview) {   // play pressed while holding a cue: keep playing after release
        if (on) { s.preview = false; return; }
        s.preview = false;
    }
    if (on && s.pos >= s.N - 2) return;
    s.motorOn = on;
}

void Deck::cue(bool down) {
    WHEEL_IMPL;
    if (!trk || !s.rd.n) return;
    if (down) {
        if (s.motorOn && !s.preview) { s.stopInstant(); s.jumpTo(s.cuePt * s.T); return; }
        const double now = s.pos / s.T;
        if (std::abs(now - s.cuePt) > 0.002) {
            s.cuePt = clampv(set.quantize ? s.snap(now, 1, false) : now, 0.0, trk->length());
            if (std::abs(s.cuePt * s.T - s.pos) > 0.5) s.jumpTo(s.cuePt * s.T);
        }
        s.startInstant(); s.preview = true; s.held = kCues;
    } else if (s.held == kCues) {
        if (s.preview) { s.preview = false; s.stopInstant(); s.jumpTo(s.cuePt * s.T); }
        s.held = -1;
    }
}

void Deck::seek(double sec) {
    WHEEL_IMPL;
    if (!trk || !s.rd.n) return;
    s.jumpTo(clampv(sec, 0.0, trk->length()) * s.T);
}

void Deck::nudge(float amount) { WHEEL_IMPL; s.nudgeT = clampv((double) amount, -1.0, 1.0); }

void Deck::bend(double secDelta) {
    WHEEL_IMPL;
    if (s.touched) { s.handT = clampv(s.handT + secDelta * s.T, 0.0, s.N); s.moved = true; }
    else s.bendA += secDelta * s.T;
}

void Deck::touch(bool on) {
    WHEEL_IMPL;
    if (on == s.touched) return;
    if (on) {
        // the hand catches the record: it keeps a few ms of momentum (slip mat give), then follows the hand
        const double carry = s.lastDp / s.kHand;
        s.y2 = s.pos; s.y1 = s.handR = s.handT = s.pos + carry;
        s.rampVel = 0; s.moved = false; s.sinceMove = 1e9;
        s.touched = true;
    } else {
        s.touched = false;
        s.recVel = s.lastDp / s.r0;   // let go with the hand's speed; the slip mat drags it back up to the platter
    }
}

void Deck::move(double secDelta) {
    WHEEL_IMPL;
    if (!s.touched) { bend(secDelta); return; }
    s.handT = clampv(s.handT + secDelta * s.T, 0.0, s.N);
    s.moved = true;
}

void Deck::censor(bool on) { WHEEL_IMPL; if (trk) s.censor = on; }

void Deck::hotCue(int i, bool down) {
    WHEEL_IMPL;
    if (i < 0 || i >= kCues || !trk || !s.rd.n) return;
    const double now = s.pos / s.T;
    if (down) {
        if (!cues[(size_t) i].set()) {
            cues[(size_t) i].t = clampv(set.quantize ? s.snap(now, 1, false) : now, 0.0, trk->length());
            return;
        }
        double tgt = cues[(size_t) i].t;
        if (s.motorOn && !s.preview) {
            if (set.quantize && s.bpm > 0) tgt += now - s.snap(now, 1, false);   // keep the beat phase
            if (set.slip) s.slipCue = i;
            s.jumpTo(clampv(tgt, 0.0, trk->length()) * s.T);
        } else {
            s.jumpTo(tgt * s.T); s.startInstant(); s.preview = true; s.held = i;
        }
    } else {
        if (s.held == i) {
            if (s.preview) { s.preview = false; s.stopInstant(); s.jumpTo(cues[(size_t) i].t * s.T); }
            s.held = -1;
        }
        if (s.slipCue == i) s.slipCue = -1;
    }
}

void Deck::clearCue(int i) { if (i >= 0 && i < kCues) cues[(size_t) i].t = -1; }

void Deck::autoLoop(double beats) {
    WHEEL_IMPL;
    if (!trk || !s.rd.n) return;
    beats = clampv(beats, 1.0 / 32, 32.0);
    const double len = beats * s.beatLen() * s.T;
    if (s.looping && !s.rolling) {
        if (std::abs((s.loopB - s.loopA) - len) < 1.0) { s.looping = false; return; }
        s.loopB = s.loopA + len;   // resize from the loop start
        if (s.pos >= s.loopB) s.jumpTo(s.loopA + std::fmod(s.pos - s.loopA, len));
        return;
    }
    if (s.rolling) return;
    const double now = s.pos / s.T;
    const double a = set.quantize ? s.snap(now, std::min(beats, 1.0), false) : now;
    s.loopA = std::max(0.0, a * s.T); s.loopB = s.loopA + len; s.looping = true;
    if (s.pos >= s.loopB) s.jumpTo(s.loopA + std::fmod(s.pos - s.loopA, len));
}

void Deck::loopIn() {
    WHEEL_IMPL;
    if (!trk || s.rolling) return;
    const double now = s.pos / s.T;
    s.loopA = std::max(0.0, (set.quantize ? s.snap(now, 1.0 / 4, false) : now) * s.T);
    s.loopB = -1; s.looping = false;
}

void Deck::loopOut() {
    WHEEL_IMPL;
    if (!trk || s.rolling || s.loopA < 0) return;
    const double now = s.pos / s.T;
    double b = (set.quantize ? s.snap(now, 1.0 / 4, false) : now) * s.T;
    if (b <= s.loopA + 0.005 * s.T) b = std::max(s.pos, s.loopA + 0.01 * s.T);
    s.loopB = b; s.looping = true;
    if (s.pos >= s.loopB) s.jumpTo(s.loopA + std::fmod(s.pos - s.loopB, s.loopB - s.loopA));
}

void Deck::reloop() {
    WHEEL_IMPL;
    if (!trk || s.rolling) return;
    if (s.looping) { s.looping = false; return; }
    if (s.loopA >= 0 && s.loopB > s.loopA) { s.looping = true; s.jumpTo(s.loopA); }
}

void Deck::loopExit() { WHEEL_IMPL; if (!s.rolling) s.looping = false; }

void Deck::loopHalve() {
    WHEEL_IMPL;
    if (s.loopA < 0 || s.loopB <= s.loopA) return;
    const double len = std::max((s.loopB - s.loopA) * 0.5, s.beatLen() / 32 * s.T);
    s.loopB = s.loopA + len;
    if (s.looping && s.pos >= s.loopB) s.jumpTo(s.loopA + std::fmod(s.pos - s.loopA, len));
}

void Deck::loopDouble() {
    WHEEL_IMPL;
    if (s.loopA < 0 || s.loopB <= s.loopA) return;
    const double len = std::min((s.loopB - s.loopA) * 2, s.beatLen() * 64 * s.T);
    s.loopB = std::min(s.loopA + len, s.N);
}

void Deck::loopMove(double beats) {
    WHEEL_IMPL;
    if (s.loopA < 0 || s.loopB <= s.loopA) return;
    double dl = beats * s.beatLen() * s.T;
    if (s.loopA + dl < 0) dl = -s.loopA;
    if (s.loopB + dl > s.N) dl = s.N - s.loopB;
    s.loopA += dl; s.loopB += dl;
    if (s.looping) s.jumpTo(clampv(s.pos + dl, 0.0, s.N));
}

void Deck::roll(double beats, bool on) {
    WHEEL_IMPL;
    if (!trk || !s.rd.n) return;
    beats = clampv(beats, 1.0 / 32, 32.0);
    if (on) {
        if (!s.rolling) {
            s.svA = s.loopA; s.svB = s.loopB; s.svLooping = s.looping;
            const double now = s.pos / s.T;
            s.loopA = std::max(0.0, (set.quantize ? s.snap(now, std::min(beats, 1.0), true) : now) * s.T);
            s.rolling = true; s.rollN = 0;
        }
        if (s.rollN < 8) s.rollStack[s.rollN++] = beats;
    } else {
        int k = -1;
        for (int i = 0; i < s.rollN; ++i) if (std::abs(s.rollStack[i] - beats) < 1e-9) k = i;
        if (k < 0) return;
        for (int i = k; i + 1 < s.rollN; ++i) s.rollStack[i] = s.rollStack[i + 1];
        --s.rollN;
        if (s.rollN == 0) {   // let go: back to the old loop state; the slip return happens on the next render
            s.rolling = false; s.loopA = s.svA; s.loopB = s.svB; s.looping = s.svLooping;
            return;
        }
        beats = s.rollStack[s.rollN - 1];
    }
    const double len = beats * s.beatLen() * s.T;
    s.loopB = s.loopA + len; s.looping = true;
    if (s.pos >= s.loopB) s.jumpTo(s.loopA + std::fmod(s.pos - s.loopA, len));
}

void Deck::savedLoop(int i) {
    WHEEL_IMPL;
    if (!trk || i < 0 || i >= kCues || s.rolling) return;
    const SavedLoop& l = trk->loops[(size_t) i];
    if (!l.set()) return;
    s.loopA = l.a * s.T; s.loopB = l.b * s.T; s.looping = true;
    s.jumpTo(s.loopA);
}

void Deck::syncTo(double masterBpm, double masterBeatPhase, bool phase) {
    WHEEL_IMPL;
    if (!trk || s.bpm <= 0 || masterBpm <= 0) return;
    double r = masterBpm / s.bpm, mult = 1;
    if (std::abs(std::log(r * 0.5)) < std::abs(std::log(r))) { r *= 0.5; mult = 2; }       // 70 vs 140: half time
    else if (std::abs(std::log(r * 2)) < std::abs(std::log(r))) { r *= 2; mult = 0.5; }
    s.syncSpeed = clampv(r, 0.25, 4.0); s.syncLatched = true; s.latchPitch = set.pitch;
    if (phase && mult == 1 && s.motorOn && !s.touched && !s.looping) {
        double e = masterBeatPhase - beatPhase();
        e -= std::round(e);
        const double shift = e * s.beatLen() * s.T;
        if (std::abs(e) > 0.03) { s.syncBend = 0; s.jumpTo(clampv(s.pos + shift, 0.0, s.N)); }
        else s.syncBend = shift;
    }
}

double Deck::currentBpm() const {
    if (!d) return trk ? trk->bpm : 0;
    return d->bpm * d->speedSm * (1.0 + kNudge * d->nudgeSm);
}

double Deck::beatPhase() const {
    if (!d || d->bpm <= 0) return 0;
    const double ph = (d->pos / d->T - d->first) / d->beatLen();
    return ph - std::floor(ph);
}

double Deck::beatLength() const {
    const double b = d ? d->bpm : (trk ? trk->bpm : 0);
    return b > 0 ? 60.0 / b : 0;
}

void Deck::gridShift(double sec) { WHEEL_IMPL; gridOff += sec; s.first = (trk ? trk->firstBeat : 0) + gridOff; }
void Deck::gridBpm(double bpm) { WHEEL_IMPL; bpmOverride = bpm > 0 ? bpm : 0; s.bpm = bpmOverride > 0 ? bpmOverride : (trk ? trk->bpm : 0); }

void Deck::render(float* L, float* R, int n) {
    NoDenormals nd;
    WHEEL_IMPL;
    if (n <= 0) return;
    if (!s.rd.n && !s.fade.on) {
        std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f);
        const float dec = (float) std::exp(-n / (0.3 * s.H));
        s.pkL *= dec; s.pkR *= dec;
        st.peakL.store(s.pkL, std::memory_order_relaxed); st.peakR.store(s.pkR, std::memory_order_relaxed);
        st.playing.store(false, std::memory_order_relaxed); st.rate.store(0, std::memory_order_relaxed);
        return;
    }

    // ---- per block ----
    if (!syncOn && s.syncLatched && std::abs(set.pitch - s.latchPitch) > 0.015f) s.syncLatched = false;   // pitch takeover
    const double baseSpeed = (set.rpm45 ? 1.35 : 1.0) * (1.0 + (double) set.pitch * set.pitchRange);
    const double tgtSpeed = s.syncLatched ? s.syncSpeed : baseSpeed;

    const bool ev = s.rolling || s.censor || (set.slip && (s.touched || s.looping || s.slipCue >= 0));
    if (ev && !s.slipEv) s.slipEv = true;
    else if (!ev && s.slipEv) {   // the slip event is over: back to where the record would have been
        s.slipEv = false;
        s.jumpTo(clampv(s.shadow, 0.0, s.N));
        s.recVel = s.platter();
    }

    if (s.touched) {   // de-jitter the hand: spread each controller step over the controller's own update interval
        if (s.moved) {
            // the controller's update interval: follows the slow updates at once and relaxes slowly, so the ramp
            // does not run dry (and stall the record) between irregular updates; long gaps are pauses, not rate
            if (s.sinceMove < 0.06 * s.H) {
                const double iv = s.sinceMove;
                s.moveIv = clampv(iv > s.moveIv ? iv : s.moveIv + (iv - s.moveIv) * 0.08, (double) n, 0.04 * s.H);
            }
            s.sinceMove = 0; s.moved = false;
            s.rampVel = (s.handT - s.handR) / std::max(s.moveIv, (double) n);
        }
        s.sinceMove += n;
    }

    {
        const double pl = s.platter();
        const bool shortLoop = s.looping && (s.loopB - s.loopA) < 0.12 * s.T;
        s.klWant = set.keyLock && !s.touched && !s.censor && s.motorOn && s.motorNorm > 0.98 && !shortLoop && s.rd.n
                   && s.recVel > 0.2 && std::abs(s.recVel - pl) < 0.03 * pl;
        if (s.klWant && !s.klRun) { s.klRun = true; s.klInit(false); }
    }

    const double st0 = std::max(0.05, (double) set.startTime), bt0 = std::max(0.05, (double) set.brakeTime);
    const double aStart = 1.15 / st0, kStart = 10.0 / st0, aBrake = 1.1 / bt0, kBrake = 20.0 / bt0;
    const double dt = 1.0 / s.H;
    float pkL = 0, pkR = 0; double motion = 0;

    for (int i = 0; i < n; ++i) {
        // motor: torque-limited run-up, quartz-locked landing; electronic brake
        const double tgt = s.motorOn ? 1.0 : 0.0, e = tgt - s.motorNorm;
        if (e != 0) {
            const double a = e > 0 ? std::min(aStart, e * kStart) : std::max(-aBrake, e * kBrake);
            s.motorNorm += a * dt;
            const double e2 = tgt - s.motorNorm;
            if (std::abs(e2) < 1e-7 || (e > 0) != (e2 > 0)) s.motorNorm = tgt;
        }
        s.speedSm += (tgtSpeed - s.speedSm) * s.kSpeed;
        s.nudgeSm += (s.nudgeT - s.nudgeSm) * s.kNudgeC;
        const double platter = s.platter();
        const double prev = s.pos;

        if (s.touched) {
            const double rem = s.handT - s.handR;
            double stp = s.rampVel;
            if (stp * rem <= 0) stp = 0; else if (std::abs(stp) > std::abs(rem)) stp = rem;
            s.handR += stp;
            s.y1 += (s.handR - s.y1) * s.kHand;
            s.y2 += (s.y1 - s.y2) * s.kHand;
            s.pos = s.y2;
            s.recVel = (s.pos - prev) / s.r0;
        } else {
            // the slip mat couples the record to the platter (fast, but not instant: realistic pick-up)
            const double tv = s.censor ? -platter : platter, ve = tv - s.recVel, F = s.censor ? 60.0 : 30.0;
            s.recVel += clampv(ve * 150.0, -F, F) * dt;
            s.pos += s.recVel * s.r0;
            if (s.bendA != 0 || s.bendB != 0) {
                const double t1 = s.bendA * s.kBend; s.bendA -= t1; s.bendB += t1;
                const double t2 = s.bendB * s.kBend; s.bendB -= t2; s.pos += t2;
                if (std::abs(s.bendA) + std::abs(s.bendB) < 1e-3) s.bendA = s.bendB = 0;
            }
            if (s.syncBend != 0 && s.motorOn) {
                const double lim = 0.02 * s.r0, stp = clampv(s.syncBend * s.kSync, -lim, lim);
                s.syncBend -= stp; s.pos += stp;
                if (std::abs(s.syncBend) < 1e-3) s.syncBend = 0;
            }
        }
        const double dp = s.pos - prev;
        s.lastDp = dp; motion += dp;
        if (s.slipEv) s.shadow += platter * s.r0;

        if (s.looping && s.loopB > s.loopA) {
            const double len = s.loopB - s.loopA;
            if (dp > 0 && s.pos >= s.loopB && prev < s.loopB) s.jumpTo(s.pos - len);
            else if (dp < 0 && s.pos < s.loopA && prev >= s.loopA) s.jumpTo(s.pos + len);
        }
        if (!s.rd.n) { s.pos = 0; s.recVel = 0; }
        else if (s.pos >= s.N - 1) {
            if (!s.touched) { s.motorOn = false; s.motorNorm = 0; s.recVel = 0; s.preview = false; s.pos = s.N - 1; }
        } else if (s.pos < 0 && !s.touched && s.recVel < 0) { s.pos = 0; s.recVel = 0; }
        if (!s.slipEv) s.shadow = s.pos;

        float oL = 0, oR = 0;
        const double av = std::abs(s.recVel);
        float fw = 0, fL = 0, fR = 0;
        if (s.fade.on) {
            fw = s.xfw[(size_t) s.fade.k];
            if (s.klMix < 1.0) s.fade.rd.read(s.fade.pos, fL, fR);
            s.fade.pos += s.fade.vel;
            if (++s.fade.k > s.Xf) s.fade.on = false;
        }
        if (s.klMix < 1.0) {
            float a, b;
            s.rd.read(s.pos, a, b);
            a = a * (1 - fw) + fL * fw; b = b * (1 - fw) + fR * fw;
            // a stylus that is not moving is silent; slow drags lose their top end (like the real thing)
            const float sgT = (float) std::min(1.0, av / 0.04);
            s.sg += (sgT - s.sg) * (float) s.kSg;
            const float lpa = (float) std::min(1.0, 2 * kPi * (150.0 + 16000.0 * av) / s.H);
            s.lpL += (a - s.lpL) * lpa; s.lpR += (b - s.lpR) * lpa;
            oL = s.lpL * s.sg; oR = s.lpR * s.sg;
        }
        if (s.klRun) {
            if (--s.nextGrain <= 0) s.spawn();
            float kl = 0, kr = 0;
            for (auto& g : s.gr) {
                if (!g.on) continue;
                float a, b;
                s.rd.read(g.rp, a, b);
                const float w = s.win[(size_t) g.age] * g.f;
                kl += a * w; kr += b * w;
                g.rp += s.r0;
                if (g.fs != 0) { g.f += g.fs; if (g.fs > 0 && g.f >= 1) { g.f = 1; g.fs = 0; } }
                if (++g.age >= s.W || (g.fs < 0 && g.f <= 0)) g.on = false;
            }
            const float m = (float) s.klMix;
            oL = oL * (1 - m) + kl * m; oR = oR * (1 - m) + kr * m;
            s.klMix = s.klWant ? std::min(1.0, s.klMix + s.kKl) : std::max(0.0, s.klMix - s.kKl);
            if (s.klMix <= 0 && !s.klWant) { s.klRun = false; for (auto& g : s.gr) g.on = false; }
        }
        // DC blocker (5 Hz): a record held still on a non-zero sample must not leave an offset
        const float yL = oL - s.dcxL + s.dcR * s.dcyL, yR = oR - s.dcxR + s.dcR * s.dcyR;
        s.dcxL = oL; s.dcxR = oR; s.dcyL = yL; s.dcyR = yR;
        L[i] = yL; R[i] = yR;
        pkL = std::max(pkL, std::abs(yL)); pkR = std::max(pkR, std::abs(yR));
    }

    // ---- status ----
    s.angle += motion / s.T / kSecPerRev;
    s.angle -= std::floor(s.angle);
    const float dec = (float) std::exp(-n / (0.3 * s.H));
    s.pkL = std::max(pkL, s.pkL * dec); s.pkR = std::max(pkR, s.pkR * dec);
    const auto rl = std::memory_order_relaxed;
    st.pos.store(s.pos / s.T, rl); st.rate.store(s.recVel, rl); st.shadow.store(s.shadow / s.T, rl);
    st.bpmNow.store(currentBpm(), rl); st.angle.store(s.angle, rl);
    st.playing.store(s.motorOn, rl); st.touched.store(s.touched, rl); st.looping.store(s.looping, rl);
    st.rolling.store(s.rolling, rl); st.censoring.store(s.censor, rl); st.synced.store(syncOn || s.syncLatched, rl);
    const bool hasLoop = s.loopA >= 0 && s.loopB > s.loopA;
    st.loopA.store(s.loopA >= 0 ? s.loopA / s.T : -1, rl); st.loopB.store(hasLoop ? s.loopB / s.T : -1, rl);
    st.cuePoint.store(s.cuePt, rl);
    st.peakL.store(s.pkL, rl); st.peakR.store(s.pkR, rl);
}

// ================================================================================================================
// FX
// ================================================================================================================
namespace {
struct Comb {
    std::vector<float> b; int i = 0; float store = 0;
    inline float run(float x, float fb, float damp) {
        const float o = b[(size_t) i];
        store = o * (1 - damp) + store * damp;
        b[(size_t) i] = x + store * fb;
        if (++i >= (int) b.size()) i = 0;
        return o;
    }
};
struct Allpass {
    std::vector<float> b; int i = 0;
    inline float run(float x) {
        const float o = b[(size_t) i], y = o - x;
        b[(size_t) i] = x + o * 0.5f;
        if (++i >= (int) b.size()) i = 0;
        return y;
    }
};
}  // namespace

struct Fx::Impl {
    double sr = 48000;
    // echo / ping-pong
    std::vector<float> eL, eR; int eN = 0, ew = 0; double eDel = 0; float eSend = 0, eWet = 0, eWetT = 0.5f, eFb = 0.5f;
    bool ping = false; int eLive = 0; float ehpL = 0, ehpR = 0, elpL = 0, elpR = 0;
    // reverb (Freeverb)
    Comb cL[8], cR[8]; Allpass aL[4], aR[4]; float rSend = 0, rWet = 0, rWetT = 0.5f, rSize = 0.84f, rhp = 0, rhpx = 0; int rLive = 0;
    // inserts
    FxType ins = FxType::off; float insMix = 0;
    std::vector<float> mL, mR; int mN = 0, mw = 0; float flFbL = 0, flFbR = 0;   // flanger
    float apL[6] {}, apR[6] {}, phFbL = 0, phFbR = 0;                            // phaser
    float holdL = 0, holdR = 0; double holdAcc = 1;                               // crush
    float gateG = 1;                                                              // gate
    double beatPos = 0; bool beatPosSet = false; double extBeat = 0;
    double kTime = 0, kMix = 0, kGateA = 0, kGateR = 0;

    void prepare(double s) {
        sr = s > 1000 ? s : 48000;
        eN = (int) (6.2 * sr); eL.assign((size_t) eN, 0.0f); eR.assign((size_t) eN, 0.0f);
        const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, at[4] = { 556, 441, 341, 225 };
        const double k = sr / 44100.0;
        for (int i = 0; i < 8; ++i) {
            cL[i].b.assign((size_t) std::max(8, (int) (ct[i] * k)), 0.0f);
            cR[i].b.assign((size_t) std::max(8, (int) ((ct[i] + 23) * k)), 0.0f);
        }
        for (int i = 0; i < 4; ++i) {
            aL[i].b.assign((size_t) std::max(8, (int) (at[i] * k)), 0.0f);
            aR[i].b.assign((size_t) std::max(8, (int) ((at[i] + 23) * k)), 0.0f);
        }
        mN = (int) (0.02 * sr) + 8; mL.assign((size_t) mN, 0.0f); mR.assign((size_t) mN, 0.0f);
        kTime = coef(0.08, sr); kMix = coef(0.012, sr); kGateA = coef(0.0015, sr); kGateR = coef(0.004, sr);
        reset();
    }
    void reset() {
        std::fill(eL.begin(), eL.end(), 0.0f); std::fill(eR.begin(), eR.end(), 0.0f);
        for (auto* c : { cL, cR }) for (int i = 0; i < 8; ++i) { std::fill(c[i].b.begin(), c[i].b.end(), 0.0f); c[i].store = 0; c[i].i = 0; }
        for (auto* a : { aL, aR }) for (int i = 0; i < 4; ++i) { std::fill(a[i].b.begin(), a[i].b.end(), 0.0f); a[i].i = 0; }
        eSend = rSend = 0; eLive = rLive = 0; insMix = 0; ins = FxType::off; eDel = 0;
        ehpL = ehpR = elpL = elpR = rhp = rhpx = 0;
        resetIns();
    }
    void resetIns() {
        std::fill(mL.begin(), mL.end(), 0.0f); std::fill(mR.begin(), mR.end(), 0.0f);
        flFbL = flFbR = 0; std::fill(apL, apL + 6, 0.0f); std::fill(apR, apR + 6, 0.0f); phFbL = phFbR = 0;
        holdL = holdR = 0; holdAcc = 1; gateG = 1;
    }
    inline float tapE(const std::vector<float>& b, double del) const {   // read 'del' samples behind the write head
        double rp = ew - del; while (rp < 0) rp += eN;
        const int i0 = (int) rp; const float f = (float) (rp - i0); const int i1 = i0 + 1 >= eN ? 0 : i0 + 1;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }
    inline float tapM(const std::vector<float>& b, double del) const {
        double rp = mw - del; while (rp < 0) rp += mN;
        const int i0 = (int) rp; const float f = (float) (rp - i0); const int i1 = i0 + 1 >= mN ? 0 : i0 + 1;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }
};

void Fx::prepare(double sr, int) { if (!d) d = std::make_shared<Impl>(); d->prepare(sr); }
void Fx::reset() { if (d) d->reset(); }
void Fx::setBeatPos(double beats) { if (d) { d->extBeat = beats; d->beatPosSet = true; } }

void Fx::process(float* L, float* R, int n, const FxSettings& s, double bpm) {
    NoDenormals nd;
    if (!d) prepare(48000, n);
    Impl& f = *d;
    const double sr = f.sr, bl = 60.0 / (bpm > 20 ? bpm : 120.0);
    const double beats = clampv((double) s.beats, 1.0 / 16, 8.0), period = beats * bl;   // seconds
    const bool isEcho = s.type == FxType::echo || s.type == FxType::pingpong, isRev = s.type == FxType::reverb;
    const bool isIns = s.type == FxType::flanger || s.type == FxType::phaser || s.type == FxType::crush || s.type == FxType::gate;
    const float param = clampv(s.param, 0.0f, 1.0f), depth = clampv(s.depth, 0.0f, 1.0f);

    if (isIns && s.type != f.ins && f.insMix <= 1e-4f) { f.ins = s.type; f.resetIns(); }
    const float insT = (s.on && isIns && s.type == f.ins) ? depth : 0.0f;
    const float eSendT = (s.on && isEcho) ? 1.0f : 0.0f, rSendT = (s.on && isRev) ? 1.0f : 0.0f;
    if (isEcho) { f.eWetT = depth; f.ping = s.type == FxType::pingpong; f.eFb = 0.95f * param; }
    if (isRev) { f.rWetT = depth; f.rSize = 0.70f + 0.27f * param; }
    const bool runE = eSendT > 0 || f.eSend > 1e-5f || f.eLive > 0;
    const bool runR = rSendT > 0 || f.rSend > 1e-5f || f.rLive > 0;
    const bool runI = insT > 0 || f.insMix > 1e-5f;
    if (f.beatPosSet) { f.beatPos = f.extBeat; f.beatPosSet = false; }
    const double dBeat = 1.0 / (bl * sr);
    if (!runE && !runR && !runI) { f.beatPos += dBeat * n; return; }

    const double eDelT = clampv(period * sr, 16.0, (double) f.eN - 4);
    if (f.eDel <= 0) f.eDel = eDelT;
    const float kM = (float) f.kMix;
    const float hpC = (float) (1 - std::exp(-2 * kPi * 150 / sr)), lpC = (float) (1 - std::exp(-2 * kPi * 5500 / sr));
    const float rhpC = (float) (1 - std::exp(-2 * kPi * 200 / sr));
    const float damp = 0.3f;
    bool eLoud = false, rLoud = false;

    for (int i = 0; i < n; ++i) {
        float x = L[i], y = R[i];
        f.beatPos += dBeat;
        // ---- insert effect ----
        if (runI) {
            f.insMix += (insT - f.insMix) * kM;
            const double ph = f.beatPos / beats; const double lfo = ph - std::floor(ph);
            float wl = x, wr = y;
            switch (f.ins) {
                case FxType::flanger: {
                    const double sw = 0.5 - 0.5 * std::cos(2 * kPi * lfo);
                    const double del = (0.0003 + 0.0045 * sw) * sr;
                    const float fb = 0.85f * param;
                    const float dl = f.tapM(f.mL, del), dr = f.tapM(f.mR, del);
                    f.mL[(size_t) f.mw] = x + dl * fb; f.mR[(size_t) f.mw] = y + dr * fb;
                    if (++f.mw >= f.mN) f.mw = 0;
                    wl = 0.5f * (x + dl); wr = 0.5f * (y + dr);
                    break;
                }
                case FxType::phaser: {
                    const double sw = 0.5 - 0.5 * std::cos(2 * kPi * lfo);
                    const double fc = 180.0 * std::pow(2.0, sw * 4.3);
                    const double t = std::tan(kPi * std::min(fc, 0.45 * sr) / sr);
                    const float a = (float) ((t - 1) / (t + 1)), fb = 0.7f * param;
                    float vl = x + f.phFbL * fb, vr = y + f.phFbR * fb;
                    for (int k = 0; k < 6; ++k) {
                        const float ol = a * vl + f.apL[k]; f.apL[k] = vl - a * ol; vl = ol;
                        const float orr = a * vr + f.apR[k]; f.apR[k] = vr - a * orr; vr = orr;
                    }
                    f.phFbL = vl; f.phFbR = vr;
                    wl = 0.5f * (x + vl); wr = 0.5f * (y + vr);
                    break;
                }
                case FxType::crush: {
                    const double hold = 1.0 + param * param * 30.0;
                    f.holdAcc += 1.0;
                    if (f.holdAcc >= hold) { f.holdAcc -= hold; f.holdL = x; f.holdR = y; }
                    const float q = (float) std::pow(2.0, 15.0 - 12.0 * param);
                    wl = std::round(f.holdL * q) / q; wr = std::round(f.holdR * q) / q;
                    break;
                }
                case FxType::gate: {
                    const double duty = 0.15 + 0.7 * (1.0 - param);
                    const float g = lfo < duty ? 1.0f : 0.0f;
                    f.gateG += (g - f.gateG) * (float) (g > f.gateG ? f.kGateA : f.kGateR);
                    wl = x * f.gateG; wr = y * f.gateG;
                    break;
                }
                default: break;
            }
            x += (wl - x) * f.insMix; y += (wr - y) * f.insMix;
        }
        float outL = x, outR = y;
        // ---- echo / ping-pong (tails ring out when switched off) ----
        if (runE) {
            f.eSend += (eSendT - f.eSend) * kM;
            f.eWet += (f.eWetT - f.eWet) * kM;
            f.eDel += (eDelT - f.eDel) * f.kTime;
            const float dl = f.tapE(f.eL, f.eDel), dr = f.tapE(f.eR, f.eDel);
            // tone: the repeats lose lows and highs (dub style)
            f.elpL += (dl - f.elpL) * lpC; f.elpR += (dr - f.elpR) * lpC;
            f.ehpL += (f.elpL - f.ehpL) * hpC; f.ehpR += (f.elpR - f.ehpR) * hpC;
            const float tl = f.elpL - f.ehpL, tr = f.elpR - f.ehpR;
            float wl, wr;
            if (f.ping) { wl = 0.5f * (x + y) * f.eSend + tr * f.eFb; wr = tl * f.eFb; }
            else { wl = x * f.eSend + tl * f.eFb; wr = y * f.eSend + tr * f.eFb; }
            f.eL[(size_t) f.ew] = wl; f.eR[(size_t) f.ew] = wr;
            if (++f.ew >= f.eN) f.ew = 0;
            if (std::abs(wl) + std::abs(wr) > 1e-5f) eLoud = true;
            outL += dl * f.eWet; outR += dr * f.eWet;
        }
        // ---- reverb ----
        if (runR) {
            f.rSend += (rSendT - f.rSend) * kM;
            f.rWet += (f.rWetT - f.rWet) * kM;
            const float in0 = (x + y) * f.rSend;
            f.rhp += (in0 - f.rhp) * rhpC;
            const float in = (in0 - f.rhp) * 0.015f;
            float sl = 0, srr = 0;
            for (int k = 0; k < 8; ++k) { sl += f.cL[k].run(in, f.rSize, damp); srr += f.cR[k].run(in, f.rSize, damp); }
            for (int k = 0; k < 4; ++k) { sl = f.aL[k].run(sl); srr = f.aR[k].run(srr); }
            if (std::abs(sl) + std::abs(srr) > 1e-5f || std::abs(in) > 1e-6f) rLoud = true;
            outL += sl * 3.0f * f.rWet; outR += srr * 3.0f * f.rWet;
        }
        L[i] = outL; R[i] = outR;
    }
    if (runE) f.eLive = eLoud ? (int) f.eDel + 2 * n : std::max(0, f.eLive - n);
    if (runR) f.rLive = rLoud ? (int) (0.3 * sr) : std::max(0, f.rLive - n);
    if (!runI) f.insMix = 0;
}

// ================================================================================================================
// Mixer
// ================================================================================================================
namespace {
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run(float xf) { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void set(int type, double fc, double q, double sr) {   // 0 LP, 1 HP, 2 AP (RBJ)
        const double w = 2 * kPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (type == 0) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }
        else if (type == 1) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
        else { b0 = 1 - al; b1 = -2 * c; b2 = 1 + al; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0; b0 /= a0; b1 /= a0; b2 /= a0;
    }
};
struct Svf {   // Cytomic TPT state variable filter (stable under fast modulation)
    float ic1 = 0, ic2 = 0;
    inline void run(float v0, float a1, float a2, float a3, float k, float& lp, float& hp) {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; hp = v0 - k * v1 - v2;
    }
};
// EQ band gain with a true kill at the bottom (-26 dB end): below -20 dB it fades linearly to silence
inline float eqGain(float db) {
    db = clampv(db, -26.0f, 6.0f);
    if (db >= -20) return dbGain(db);
    return dbGain(-20) * (db + 26.0f) / 6.0f;
}
// one side of the crossfader: u = distance from its closed end (0..1); w = width of the fade (1 blend .. 0.012 cut)
inline float xfSide(float u, float w) {
    const float x = clampv(u / w, 0.0f, 1.0f);
    return (float) std::sin(x * kPi * 0.5);
}
inline float faderLaw(float x, float c) {
    x = clampv(x, 0.0f, 1.0f); c = clampv(c, 0.0f, 1.0f);
    if (c < 0.5f) return std::pow(x, 1.0f + 4.0f * (0.5f - c));          // 0: x^3 (slow) .. 0.5: linear
    return 1.0f - std::pow(1.0f - x, 1.0f + (c - 0.5f) * 58.0f);         // 1: opens almost fully in the first 10 %
}
}  // namespace

struct Mixer::Impl {
    double sr = 48000;
    struct Chan {
        Biquad lpA[2], lpB[2], hpA[2], hpB[2], l4a[2], l4b[2], h4a[2], h4b[2], ap4[2];
        Svf svf[2];
        float trim = 1, gl = 1, gm = 1, gh = 1, filt = 0, g = 0, pfl = 0, meter = 0;
    } ch[2];
    float master = 1, cueMix = 0, cueGain = 1, lim = 1, mL = 0, mR = 0;
    float kSlow = 0, kFast = 0, kFilt = 0, kAtt = 0, kRel = 0;
    void prepare(double s) {
        sr = s > 1000 ? s : 48000;
        for (auto& c : ch) {
            for (int k = 0; k < 2; ++k) {
                c.lpA[k].set(0, 300, 0.70710678, sr); c.lpB[k] = c.lpA[k];
                c.hpA[k].set(1, 300, 0.70710678, sr); c.hpB[k] = c.hpA[k];
                c.l4a[k].set(0, 4000, 0.70710678, sr); c.l4b[k] = c.l4a[k];
                c.h4a[k].set(1, 4000, 0.70710678, sr); c.h4b[k] = c.h4a[k];
                c.ap4[k].set(2, 4000, 0.70710678, sr);
                c.svf[k] = Svf {};
            }
        }
        kSlow = (float) coef(0.015, sr); kFast = (float) coef(0.0008, sr); kFilt = (float) coef(0.02, sr);
        kAtt = (float) coef(0.0003, sr); kRel = (float) coef(0.15, sr);
    }
};

void Mixer::prepare(double sr, int) { if (!d) d = std::make_shared<Impl>(); d->prepare(sr); }

void Mixer::process(const float* aL, const float* aR, const float* bL, const float* bR, float* outL, float* outR,
                    float* cueL, float* cueR, int n, const MixerSettings& s) {
    NoDenormals nd;
    if (!d) prepare(48000, n);
    Impl& m = *d;
    const float sr = (float) m.sr;
    // crossfader
    float t = clampv((s.xfader + 1.0f) * 0.5f, 0.0f, 1.0f);
    if (s.hamster) t = 1.0f - t;
    const float w = (float) std::exp(std::log(1.0) + (std::log(0.012) - std::log(1.0)) * clampv(s.xfCurve, 0.0f, 1.0f));
    const float xA = xfSide(1.0f - t, w), xB = xfSide(t, w);
    float tgtG[2], tgtTrim[2], tgtL[2], tgtM[2], tgtH[2], tgtF[2], tgtP[2];
    for (int c = 0; c < 2; ++c) {
        const ChannelSettings& cs = s.ch[c];
        const float xg = cs.xfAssign == 0 ? xA : (cs.xfAssign == 2 ? xB : 1.0f);
        tgtG[c] = faderLaw(cs.fader, s.faderCurve) * xg;
        tgtTrim[c] = dbGain(clampv(cs.trim, -12.0f, 12.0f));
        tgtL[c] = eqGain(cs.low); tgtM[c] = eqGain(cs.mid); tgtH[c] = eqGain(cs.hi);
        tgtF[c] = clampv(cs.filter, -1.0f, 1.0f); tgtP[c] = cs.pfl ? 1.0f : 0.0f;
    }
    const float tgtMaster = dbGain(clampv(s.master, -60.0f, 12.0f)), tgtCueMix = clampv(s.cueMix, 0.0f, 1.0f);
    const float tgtCueGain = dbGain(clampv(s.cueGain, -60.0f, 12.0f));
    const float thr = 0.891f;   // -1 dBFS
    const float* in[2][2] = { { aL, aR }, { bL, bR } };
    float pk[2] = { 0, 0 }, pM[2] = { 0, 0 };
    const float kS = m.kSlow, kF = m.kFast, kFl = m.kFilt;

    for (int i = 0; i < n; ++i) {
        float sumL = 0, sumR = 0, cl = 0, cr = 0;
        for (int c = 0; c < 2; ++c) {
            auto& C = m.ch[c];
            C.trim += (tgtTrim[c] - C.trim) * kS; C.gl += (tgtL[c] - C.gl) * kS; C.gm += (tgtM[c] - C.gm) * kS;
            C.gh += (tgtH[c] - C.gh) * kS; C.g += (tgtG[c] - C.g) * kF; C.pfl += (tgtP[c] - C.pfl) * kS;
            C.filt += (tgtF[c] - C.filt) * kFl;
            // filter coefficients (shared by both sides)
            const float af = std::abs(C.filt);
            const float wet = clampv((af - 0.05f) / 0.08f, 0.0f, 1.0f);
            float a1 = 0, a2 = 0, a3 = 0, k = 1;
            const bool hpMode = C.filt > 0;
            if (wet > 0) {
                const float u = clampv((af - 0.05f) / 0.95f, 0.0f, 1.0f);
                float fc = hpMode ? 15.0f * std::pow(8000.0f / 15.0f, u) : 20000.0f * std::pow(55.0f / 20000.0f, u);
                fc = std::min(fc, 0.45f * sr);
                const float g = std::tan((float) kPi * fc / sr);
                const float q = 0.8f + 0.7f * u;   // more resonance as it sweeps
                k = 1.0f / q; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
            }
            float o[2];
            for (int sd = 0; sd < 2; ++sd) {
                const float x = in[c][sd] ? in[c][sd][i] * C.trim : 0.0f;
                const float low = C.ap4[sd].run(C.lpB[sd].run(C.lpA[sd].run(x)));
                const float rest = C.hpB[sd].run(C.hpA[sd].run(x));
                const float mid = C.l4b[sd].run(C.l4a[sd].run(rest)), hi = C.h4b[sd].run(C.h4a[sd].run(rest));
                float y = low * C.gl + mid * C.gm + hi * C.gh;
                if (wet > 0) {
                    float lp, hp;
                    C.svf[sd].run(y, a1, a2, a3, k, lp, hp);
                    y += ((hpMode ? hp : lp) - y) * wet;
                } else { C.svf[sd].ic1 = C.svf[sd].ic2 = 0; }
                o[sd] = y;
            }
            pk[c] = std::max(pk[c], std::max(std::abs(o[0]), std::abs(o[1])) * C.g);
            sumL += o[0] * C.g; sumR += o[1] * C.g;
            cl += o[0] * C.pfl; cr += o[1] * C.pfl;
        }
        m.master += (tgtMaster - m.master) * kS;
        float l = sumL * m.master, r = sumR * m.master;
        // soft limiter: fast peak gain riding, then a soft clip for what slips through
        const float p = std::max(std::abs(l), std::abs(r));
        const float gt = p > thr ? thr / p : 1.0f;
        m.lim += (gt - m.lim) * (gt < m.lim ? m.kAtt : m.kRel);
        l *= m.lim; r *= m.lim;
        auto soft = [](float x) {
            const float a = std::abs(x);
            if (a <= 0.9f) return x;
            const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f);
            return x < 0 ? -y : y;
        };
        l = soft(l); r = soft(r);
        outL[i] = l; outR[i] = r;
        pM[0] = std::max(pM[0], std::abs(l)); pM[1] = std::max(pM[1], std::abs(r));
        m.cueMix += (tgtCueMix - m.cueMix) * kS; m.cueGain += (tgtCueGain - m.cueGain) * kS;
        if (cueL && cueR) {
            cueL[i] = soft((cl * (1 - m.cueMix) + l * m.cueMix) * m.cueGain);
            cueR[i] = soft((cr * (1 - m.cueMix) + r * m.cueMix) * m.cueGain);
        }
    }
    const float dec = std::exp(-(float) n / (0.3f * sr));
    for (int c = 0; c < 2; ++c) {
        m.ch[c].meter = std::max(pk[c], m.ch[c].meter * dec);
        meter[c].store(m.ch[c].meter, std::memory_order_relaxed);
    }
    m.mL = std::max(pM[0], m.mL * dec); m.mR = std::max(pM[1], m.mR * dec);
    masterL.store(m.mL, std::memory_order_relaxed); masterR.store(m.mR, std::memory_order_relaxed);
}

}  // namespace wheel
