// Omega Drum Trigger: the hit detector. Plain C++ (no JUCE) so the same code runs in the plugin, in the offline file
// analysis and in tools/trigger_test.cpp.
//
// How a hit is found, per sample:
//   1. The input goes through the detection band (4th-order Butterworth high-pass and low-pass): everything below
//      works on that, and the low-pass's small delay (2.6 / (2 pi f): 0.07 ms at 6 kHz) is taken back off at the end.
//   2. On the band two envelopes run. "fast" jumps instantly to every peak, holds for half a period of the lowest
//      frequency in the band (so a low kick doesn't ripple), then falls. "slow" is "fast" smoothed over a few ms, so it
//      sits at whatever the drum was doing just before now: silence, or the tail of the last hit.
//   3. A hit starts when fast is over the threshold AND fast has jumped above slow by the sensitivity (a transient, not
//      a loud tail or a ringing shell; slow is taken from 1.5 ms back if it was lower then, so bleed landing just
//      before a hit can't hide it) AND the retrigger time since the last hit has passed AND the detector is armed
//      (fast came back down toward slow after the last hit, so one hit can't fire twice).
//   4. The detector then looks ahead a few ms (this is the plugin's latency, compensated by the DAW): it takes the
//      level (velocity) and places the start on a peak-hold envelope of the band. The threshold crossing is the
//      hit, unless it was a quiet precursor (bleed over a low threshold) followed by a much bigger jump, which is
//      then the hit. From there it walks back down the foot of the attack to 10% of the attack's peak, and on to an
//      earlier attack if one is joined to it (a stick hit before the shell rings up). That sample is where the note
//      goes, so the replacement lines up with the start of the real hit, not the threshold crossing.
//   5. Optional sidechain: the same band is run on a second mic (the snare mic on the kick, the hat mic on the
//      snare...). If the hit jumped here only as much as bleed would (the bleed level below what the sidechain
//      jumped), it was bleed and is dropped. Hits played together with the other drum stay.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace trig {

struct Settings {
    float thresholdDb = -30.f;    // fast envelope must pass this (dBFS, detection band)
    float sensitivityDb = 8.f;    // and jump this far above the slow envelope
    float retriggerMs = 20.f;     // minimum time between two hits
    float hpHz = 400.f;           // detection band
    float lpHz = 10000.f;
    bool sideOn = false;          // sidechain bleed rejection
    float sideRatioDb = -12.f;    // bleed level: drop the hit if it jumped here this many dB (or more) less than on the sidechain
};

struct Hit {
    int64_t onset = 0;      // sample index (input stream) where the hit starts
    float peak = 0.f;       // linear peak in the detection band, over the velocity window
    float level = 0.f;      // RMS over the same window x sqrt(2) (a sine's RMS reads as its peak): steadier than the peak, used for velocity
    float sidePeak = 0.f;
    bool rejected = false;  // dropped as sidechain bleed (reported so the display can show it)
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float run(float x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
    void set(bool high, double f, double q, double sr) {
        f = std::min(std::max(f, 5.0), sr * 0.45);
        const double w = 2 * 3.14159265358979323846 * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (high) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
        else      { b0 = (1 - c) / 2; b1 = 1 - c;    b2 = (1 - c) / 2; }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

// 4th-order Butterworth high-pass then low-pass
struct Band {
    Biquad h[2], l[2];
    bool lpOn = true;
    void set(double hp, double lp, double sr) {
        h[0].set(true, hp, 0.5411961, sr); h[1].set(true, hp, 1.3065630, sr);
        lpOn = lp < sr * 0.45;
        l[0].set(false, lp, 0.5411961, sr); l[1].set(false, lp, 1.3065630, sr);
    }
    void reset() { for (auto& b : h) b.reset(); for (auto& b : l) b.reset(); }
    inline float run(float x) {
        x = h[1].run(h[0].run(x));
        return lpOn ? l[1].run(l[0].run(x)) : x;
    }
};

class Detector {
public:
    static constexpr double kBackMs = 3.0;     // how far before the threshold crossing the start can be
    static constexpr double kWindowMs = 4.0;   // velocity window after the start
    static constexpr float kAnchorFrac = 0.5f; // a crossing whose own peak is under half the hit's is a precursor...
    static constexpr float kJumpFrac = 0.4f;   // ...if a jump this big (fraction of the peak, in 0.25 ms) follows
    static constexpr float kOnsetFrac = 0.1f;  // the start: down the attack's foot to 10% of its peak

    void prepare(double sampleRate) {
        sr = sampleRate;
        back = std::max(1, (int) std::lround(kBackMs * sr / 1000));
        win = std::max(1, (int) std::lround(kWindowMs * sr / 1000));
        rise = std::max(1, (int) std::lround(0.25 * sr / 1000));
        slack = (int) std::lround(1.5 * sr / 1000);   // room to undo the low-pass delay (fully, for low-pass >= 280 Hz)
        slowLag = std::max(1, (int) std::lround(1.5 * sr / 1000));
        int need = 4 * (back + win + slack + 2 * rise) + 8, size = 1;
        while (size < need) size <<= 1;
        mask = size - 1;
        rT.assign((size_t) size, 0.f); rSl.assign((size_t) size, 0.f); rD.assign((size_t) size, 0.f); rS.assign((size_t) size, 0.f);
        tRel = (float) std::exp(-1.0 / (1e-3 * sr));
        tHold = std::max(1, (int) std::lround(0.5e-3 * sr));
        applied = Settings(); applied.hpHz = -1;   // force coefficients
        update(cur);
        reset();
    }
    // latency the host must compensate: a note for a hit starting at sample s is sent at s + latency()
    int latency() const { return back + win + slack; }

    void reset() {
        main.reset(); side.reset();
        std::fill(rT.begin(), rT.end(), 0.f); std::fill(rSl.begin(), rSl.end(), 0.f); std::fill(rD.begin(), rD.end(), 0.f); std::fill(rS.begin(), rS.end(), 0.f);
        fast = slow = tEnv = 0; holdLeft = tHoldLeft = 0; n = 0; armed = true; measuring = false; lastOnset = INT64_MIN / 2;
        meterFast = meterSlow = 0;
    }

    void update(const Settings& s) {
        cur = s;
        if (s.hpHz != applied.hpHz || s.lpHz != applied.lpHz) {
            main.set(s.hpHz, s.lpHz, sr); side.set(s.hpHz, s.lpHz, sr);
            // hold = half a period of the lowest frequency that gets through: a rectified wave peaks every half period
            hold = std::max(1, (int) std::lround(0.5 * sr / std::max(20.f, s.hpHz)));
            // a 4th-order Butterworth low-pass delays by 2.613 / (2 pi fc) at low frequencies
            lpDelay = s.lpHz < sr * 0.45 ? (int) std::lround(2.613 / (2 * 3.14159265358979 * s.lpHz) * sr) : 0;
            const double holdMs = 1000.0 * hold / sr;
            fastRel = (float) std::exp(-1.0 / (holdMs * 1e-3 * sr));
            slowCo = (float) (1 - std::exp(-1.0 / ((3.0 + holdMs) * 1e-3 * sr)));
        }
        thr = (float) std::pow(10.0, s.thresholdDb / 20.0);
        sens = (float) std::pow(10.0, s.sensitivityDb / 20.0);
        rearm = (float) std::pow(10.0, (s.sensitivityDb * 0.5) / 20.0);
        retrig = std::max<int64_t>(1, (int64_t) std::lround(s.retriggerMs * sr / 1000));
        sideK = (float) std::pow(10.0, s.sideRatioDb / 20.0);
        applied = s;
    }

    // run len samples; sideIn may be null. onHit is called (inside this call) once per accepted or rejected hit.
    template <class F>
    void process(const float* in, const float* sideIn, int len, F&& onHit) {
        for (int i = 0; i < len; ++i, ++n) {
            const float d = std::fabs(main.run(in[i]));
            const float sd = sideIn ? std::fabs(side.run(sideIn[i])) : 0.f;
            // the timing envelope: instant attack, 0.5 ms hold, so it only ever climbs through an attack
            if (d >= tEnv) { tEnv = d; tHoldLeft = tHold; } else if (tHoldLeft > 0) --tHoldLeft; else tEnv *= tRel;
            const size_t k = (size_t) (n & mask);
            rT[k] = tEnv; rD[k] = d; rS[k] = sd;

            if (d >= fast) { fast = d; holdLeft = hold; }
            else if (holdLeft > 0) --holdLeft;
            else fast *= fastRel;
            // the reference a hit must jump above: the slow level now, or 1.5 ms ago if that was lower (bleed that
            // landed just before the hit has already lifted it)
            const float slowBefore = std::min(slow, rSl[(size_t) ((n - slowLag) & mask)]);
            slow += (fast - slow) * slowCo;
            rSl[k] = slow;
            if (fast > meterFast) meterFast = fast;
            meterSlow = slow;

            if (measuring) {
                if (n >= decideAt) decide(onHit);
                continue;
            }
            if (!armed && (fast < slowBefore * rearm || fast < thr * 0.5f)) armed = true;
            if (armed && fast > thr && fast > slowBefore * sens && n - lastOnset >= retrig) {
                measuring = true; cand = n; decideAt = n + win;
            }
        }
    }

    // velocity 1..127 from a peak: ceilDb and above -> 127, ceilDb - rangeDb -> 1; dynamics 0 = always 127
    static int velocity(float peak, float ceilDb, float rangeDb, float dynamics) {
        const double db = 20 * std::log10(std::max(peak, 1e-9f));
        double x = (db - (ceilDb - rangeDb)) / std::max(1.f, rangeDb);
        x = std::min(1.0, std::max(0.0, x));
        const double v = 127.0 * (1 - dynamics) + (1 + 126 * x) * dynamics;
        return std::min(127, std::max(1, (int) std::lround(v)));
    }

    // levels for a meter (since the last call)
    float takeMeter() { const float m = meterFast; meterFast = 0; return m; }
    int64_t position() const { return n; }
    struct Debug { int64_t cand, anchor, onset; float tpk; } dbg{};   // the last decision, for tools

private:
    template <class F>
    void decide(F&& onHit) {
        measuring = false; armed = false;
        // place the start
        const int64_t lo = std::max<int64_t>(cand - back, n - (int64_t) mask + rise);
        auto T = [&](int64_t j) { return rT[(size_t) (j & mask)]; };
        auto localPeak = [&](int64_t j) { float m = 0; for (int64_t i = j; i <= std::min(n, j + 2 * rise); ++i) m = std::max(m, T(i)); return m; };
        float tpk = 0; int64_t p = cand;
        for (int64_t j = cand; j <= n; ++j) if (T(j) > tpk) { tpk = T(j); p = j; }
        // (a) the threshold crossing is the hit, unless it was a quiet precursor (bleed over a low threshold, under
        //     half the hit) followed by a big jump (40% of the peak in 0.25 ms): then the jump is the hit
        int64_t anchor = cand;
        if (localPeak(cand) < kAnchorFrac * tpk)
            for (int64_t j = cand + rise; j <= p; ++j)
                if (T(j) - T(j - rise) >= kJumpFrac * tpk) { anchor = j - rise; break; }
        // (b) down the foot of the attack: back while the envelope keeps falling (it only climbs through an attack;
        //     flats up to 0.1 ms are noise between new peaks) and is above 10% of the attack's own peak
        const float lp = localPeak(anchor);
        const int flatMax = std::max(1, rise / 2);
        auto foot = [&](int64_t a, float pk) {
            while (a > lo && T(a - 1) == T(a)) --a;   // off the plateau the hold makes after a peak, to its rise
            int64_t o = a; int flat = 0;
            while (o > lo && T(o - 1) >= kOnsetFrac * pk && T(o - 1) <= T(o)) {
                flat = T(o - 1) == T(o) ? flat + 1 : 0;
                if (flat > flatMax) break;
                --o;
            }
            while (o < a && T(o) == T(o + 1)) ++o;   // don't end on a flat: it belongs before the rise
            return o;
        };
        int64_t onset = foot(anchor, lp);
        // (c) an earlier attack joined to this one (a stick hit before the shell rings up, the crossing having come
        //     late): a 6 dB jump to at least half this attack's peak, with the envelope never falling back under
        //     a quarter of it in between. Bleed and tails don't join up like that.
        for (int pass = 0; pass < 2; ++pass) {
            float segMin = T(onset); int64_t found = -1;
            for (int64_t j = onset - 1; j > lo + rise; --j) {
                segMin = std::min(segMin, T(j));
                if (segMin < 0.25f * lp) break;
                if (T(j) >= kAnchorFrac * lp && T(j - rise) <= 0.5f * T(j)) { found = j; break; }
            }
            if (found < 0) break;
            onset = foot(found, lp);
        }
        // velocity window from the start
        Hit h; h.onset = onset - std::min(lpDelay, slack);   // the low-pass delayed the band a little: undo it
        dbg = {cand, anchor, onset, tpk};
        const int64_t end = std::min<int64_t>(onset + win, n);
        double e = 0;
        for (int64_t j = onset; j <= end; ++j) {
            const float v = rD[(size_t) (j & mask)];
            h.peak = std::max(h.peak, v); e += (double) v * v;
            h.sidePeak = std::max(h.sidePeak, rS[(size_t) (j & mask)]);
        }
        h.level = (float) std::sqrt(2 * e / (double) (end - onset + 1));
        // bleed test: how much each mic jumped at this moment (the energy the hit added over what was already
        // there in the 2 ms before). It's bleed if it jumped here no more than the bleed level times what the
        // sidechain jumped: a snare alone leaks into the kick mic ~15-25 dB down, but a kick played with it (a hammer blast) is far
        // louder than that here, so it stays. And a snare still ringing in the sidechain can't veto anything.
        if (cur.sideOn) {
            // new energy at the hit = energy over the velocity window minus the energy already there just before
            auto ms = [&](const std::vector<float>& r, int64_t from, int64_t to) {
                double acc = 0; for (int64_t j = from; j <= to; ++j) { const double v = r[(size_t) (j & mask)]; acc += v * v; }
                return acc / (double) std::max<int64_t>(1, to - from + 1);
            };
            const int64_t pre0 = onset - 2 * back / 3, pre1 = onset - rise;
            const double jm = std::max(0.0, ms(rD, onset, end) - ms(rD, pre0, pre1));
            const double js = std::max(0.0, ms(rS, onset, end) - ms(rS, pre0, pre1));
            h.rejected = std::sqrt(jm) < std::sqrt(js) * sideK;
        }
        if (!h.rejected) lastOnset = h.onset;
        onHit(h);
    }

    double sr = 44100;
    Settings cur, applied;
    Band main, side;
    std::vector<float> rT, rSl, rD, rS;
    int64_t mask = 0, n = 0, cand = 0, decideAt = 0, lastOnset = 0, retrig = 1;
    int back = 1, win = 1, rise = 1, slowLag = 1, lpDelay = 0, slack = 0, hold = 1, holdLeft = 0, tHold = 1, tHoldLeft = 0;
    float fast = 0, slow = 0, tEnv = 0, fastRel = 0.99f, slowCo = 0.01f, tRel = 0.9f;
    float thr = 0.03f, sens = 2.5f, rearm = 1.5f, sideK = 1.f;
    float meterFast = 0, meterSlow = 0;
    bool armed = true, measuring = false;
};

// a threshold from a set of candidate peaks (dB) found with the threshold all the way down. Real hits and bleed
// form two groups: split them where the two groups are each tightest (Otsu's method on the dB values), then put the
// threshold in the middle of the gap between the loudest bleed and the softest hit. Returns false when the groups
// aren't clearly apart (less than a 3 dB gap): then the user sets it by ear.
inline bool autoThreshold(std::vector<float> db, float& out) {
    if (db.size() < 8) return false;
    std::sort(db.begin(), db.end());
    // only look within 36 dB of the loudest hit: below that is the noise floor and room, not the bleed/hit border
    const float top = db.back();
    db.erase(db.begin(), std::lower_bound(db.begin(), db.end(), top - 36.f));
    const size_t nn = db.size();
    if (nn < 8) return false;
    double total = 0; for (float v : db) total += v;
    double best = -1, sumLo = 0; size_t split = 0;
    for (size_t i = 0; i + 1 < nn; ++i) {      // lower group = [0..i], upper = [i+1..]
        sumLo += db[i];
        const double nLo = (double) (i + 1), nHi = (double) (nn - i - 1);
        const double mLo = sumLo / nLo, mHi = (total - sumLo) / nHi;
        const double between = nLo * nHi * (mLo - mHi) * (mLo - mHi);
        if (between > best) { best = between; split = i; }
    }
    const float gap = db[split + 1] - db[split];
    if (gap < 3.f || nn - split - 1 < 4) return false;
    out = db[split] + gap * 0.5f;
    return true;
}

// the whole of a recording at once (the "Analyze file" button): same detector, nothing to compensate
inline std::vector<Hit> analyze(const float* x, const float* side, int64_t len, double sr, const Settings& s) {
    Detector d; d.update(s); d.prepare(sr);
    std::vector<Hit> out;
    const int chunk = 4096;
    for (int64_t i = 0; i < len; i += chunk) {
        const int m = (int) std::min<int64_t>(chunk, len - i);
        d.process(x + i, side ? side + i : nullptr, m, [&](const Hit& h) { if (!h.rejected) out.push_back(h); });
    }
    // flush the look-ahead with silence
    std::vector<float> z((size_t) d.latency() + 16, 0.f);
    d.process(z.data(), side ? z.data() : nullptr, (int) z.size(), [&](const Hit& h) { if (!h.rejected) out.push_back(h); });
    return out;
}

} // namespace trig

namespace trig {
// candidate peaks for autoThreshold: the detector with the threshold all the way down
inline std::vector<float> candidatePeaksDb(const float* x, const float* side, int64_t len, double sr, Settings s) {
    s.thresholdDb = -70; s.sideOn = false;
    std::vector<float> out;
    for (auto& h : analyze(x, side, len, sr, s)) out.push_back(20 * std::log10(std::max(h.peak, 1e-9f)));
    return out;
}
} // namespace trig
