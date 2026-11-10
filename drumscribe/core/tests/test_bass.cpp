// Bass transcription against exact ground truth (tests/bass_synth.h).
// Prints a table per line; returns non-zero when a threshold fails. BASS_DEBUG=1 lists every mismatch.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bass_synth.h"
#include "ds/modules.h"

using bsynth::TruthNote;

struct Result {
    std::string name;
    int ntruth = 0, nest = 0, tp = 0;
    double P = 0, R = 0, F = 0;
    double on_mae = 0, on_p95 = 0, on_mae_attacked = 0, rel_mae = 0, rel_p95 = 0;
    double pitch_acc = 0, cents_mae = 0;
    double slide_p = 1, slide_r = 1, slide_f = 1;
    double acc_p = 1, acc_r = 1;
    double bend_p = 1, bend_r = 1;
    double secs = 0, audio = 0;
};

static double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * (v.size() - 1) + 0.5))];
}
static double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0 : s / v.size();
}
static double safe_div(double a, double b) { return b > 0 ? a / b : 1.0; }

static Result evaluate(const bsynth::Line& L, int debug) {
    Result r;
    r.name = L.name;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<ds::BassNote> est = ds::transcribe_bass(L.sig, 44100);
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.audio = L.sig.size() / 44100.0;
    const auto& tru = L.notes;
    r.ntruth = int(tru.size());
    r.nest = int(est.size());
    // match: same pitch, onset within 30 ms, closest first
    struct Pair { double d; int t, e; };
    std::vector<Pair> cand;
    for (int i = 0; i < int(tru.size()); ++i)
        for (int j = 0; j < int(est.size()); ++j) {
            double d = std::fabs(est[j].time - tru[i].time);
            if (d <= 0.030 && est[j].note == tru[i].note) cand.push_back({d, i, j});
        }
    std::sort(cand.begin(), cand.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    std::vector<int> mt(tru.size(), -1), me(est.size(), -1);
    for (auto& c : cand)
        if (mt[c.t] < 0 && me[c.e] < 0) { mt[c.t] = c.e; me[c.e] = c.t; }
    std::vector<double> on, on_att, rel, cents;
    int sl_tp = 0, sl_est = 0, sl_tru = 0, ac_tp = 0, ac_est = 0, ac_tru = 0, b_tp = 0, b_est = 0, b_tru = 0;
    for (int i = 0; i < int(tru.size()); ++i) {
        int j = mt[i];
        if (j < 0) continue;
        ++r.tp;
        on.push_back(1e3 * std::fabs(est[j].time - tru[i].time));
        bool slid_into = i > 0 && tru[i - 1].slide && mt[i - 1] >= 0;
        if (!(i > 0 && tru[i - 1].slide)) on_att.push_back(on.back());
        (void)slid_into;
        rel.push_back(1e3 * std::fabs(est[j].end - tru[i].end));
        cents.push_back(std::fabs(est[j].cents));
        sl_tru += tru[i].slide;
        sl_est += est[j].slide_to_next;
        sl_tp += tru[i].slide && est[j].slide_to_next;
        ac_tru += tru[i].accent;
        ac_est += est[j].accent;
        ac_tp += tru[i].accent && est[j].accent;
        b_tru += tru[i].bend;
        b_est += !est[j].bends.empty();
        b_tp += tru[i].bend && !est[j].bends.empty();
    }
    // pitch accuracy: among onset matches regardless of pitch
    {
        std::vector<int> used(est.size(), 0);
        int n = 0, ok = 0;
        for (auto& t : tru) {
            int bj = -1;
            double bd = 0.030;
            for (int j = 0; j < int(est.size()); ++j) {
                double d = std::fabs(est[j].time - t.time);
                if (!used[j] && d <= bd) { bd = d; bj = j; }
            }
            if (bj < 0) continue;
            used[bj] = 1;
            ++n;
            ok += est[bj].note == t.note;
        }
        r.pitch_acc = safe_div(ok, n);
    }
    r.P = safe_div(r.tp, r.nest);
    r.R = safe_div(r.tp, r.ntruth);
    r.F = r.P + r.R > 0 ? 2 * r.P * r.R / (r.P + r.R) : 0;
    r.on_mae = mean(on);
    r.on_p95 = pct(on, 0.95);
    r.on_mae_attacked = mean(on_att);
    r.rel_mae = mean(rel);
    r.rel_p95 = pct(rel, 0.95);
    r.cents_mae = mean(cents);
    r.slide_p = safe_div(sl_tp, sl_est);
    r.slide_r = safe_div(sl_tp, sl_tru);
    r.slide_f = r.slide_p + r.slide_r > 0 ? 2 * r.slide_p * r.slide_r / (r.slide_p + r.slide_r) : 0;
    r.acc_p = safe_div(ac_tp, ac_est);
    r.acc_r = safe_div(ac_tp, ac_tru);
    r.bend_p = safe_div(b_tp, b_est);
    r.bend_r = safe_div(b_tp, b_tru);
    if (debug) {
        std::printf("--- %s: %zu truth, %zu est\n", L.name.c_str(), tru.size(), est.size());
        for (int i = 0; i < int(tru.size()); ++i) {
            int j = mt[i];
            const auto& t = tru[i];
            if (j < 0) {
                std::printf("  MISS  t=%8.4f-%8.4f n=%d%s%s\n", t.time, t.end, t.note, t.slide ? " slide" : "",
                            t.accent ? " acc" : "");
                continue;
            }
            const auto& e = est[j];
            double don = 1e3 * (e.time - t.time), drel = 1e3 * (e.end - t.end);
            bool bad = std::fabs(don) > 2 || std::fabs(drel) > 10 || e.slide_to_next != t.slide || e.accent != t.accent ||
                       t.bend != !e.bends.empty();
            if (bad || debug > 1)
                std::printf("  %s t=%8.4f n=%d don=%+6.2f drel=%+7.2f slide %d/%d acc %d/%d bend %d/%zu v=%d c=%+.0f\n",
                            bad ? "DIFF" : "ok  ", t.time, t.note, don, drel, t.slide, e.slide_to_next, t.accent,
                            e.accent, t.bend, e.bends.size(), e.velocity, e.cents);
        }
        for (int j = 0; j < int(est.size()); ++j)
            if (me[j] < 0)
                std::printf("  EXTRA t=%8.4f-%8.4f n=%d v=%d\n", est[j].time, est[j].end, est[j].note, est[j].velocity);
    }
    return r;
}

int main() {
    const char* dbg = std::getenv("BASS_DEBUG");
    int debug = dbg ? std::atoi(dbg) : 0;
    struct Spec { bsynth::Line L; double minF, maxOn, maxRel, minSlideF, minAccF; };
    std::vector<Spec> specs;
    bsynth::Line a = bsynth::make_acid(7), b = bsynth::make_fingered(11);
    bsynth::Line ac = a, bc = b;
    bsynth::add_bleed(ac, -12, -50, -40, 21);
    bsynth::add_bleed(bc, -12, -50, -40, 23);
    // thresholds: what the algorithm achieves, with margin
    specs.push_back({a, 0.97, 2.0, 10.0, 0.90, 0.80});
    specs.push_back({b, 0.97, 2.0, 10.0, 0.90, 0.70});
    specs.push_back({ac, 0.93, 3.0, 12.0, 0.85, 0.75});
    specs.push_back({bc, 0.93, 3.0, 12.0, 0.85, 0.60});
    bool ok = true;
    std::printf("%-30s %5s %5s %5s %5s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "line", "ntru", "nest", "P", "R",
                "F", "onMAE", "onP95", "relMAE", "relP95", "pitch", "c_err", "slP", "slR", "accP", "accR");
    std::vector<Result> res;
    const char* only = std::getenv("BASS_ONLY");
    int idx = -1;
    for (auto& sp : specs) {
        ++idx;
        if (only && std::atoi(only) != idx) continue;
        Result r = evaluate(sp.L, debug);
        res.push_back(r);
        std::printf("%-30s %5d %5d %5.3f %5.3f %6.3f %6.2f %6.2f %6.2f %6.2f %6.3f %6.1f %6.2f %6.2f %6.2f %6.2f\n",
                    r.name.c_str(), r.ntruth, r.nest, r.P, r.R, r.F, r.on_mae, r.on_p95, r.rel_mae, r.rel_p95,
                    r.pitch_acc, r.cents_mae, r.slide_p, r.slide_r, r.acc_p, r.acc_r);
        double accF = r.acc_p + r.acc_r > 0 ? 2 * r.acc_p * r.acc_r / (r.acc_p + r.acc_r) : 0;
        bool pass = r.F >= sp.minF && r.on_mae < sp.maxOn && r.rel_mae < sp.maxRel && r.slide_f >= sp.minSlideF &&
                    accF >= sp.minAccF;
        if (!pass) {
            std::printf("  FAIL (need F>=%.2f onMAE<%.1f relMAE<%.1f slideF>=%.2f accentF>=%.2f)\n", sp.minF, sp.maxOn,
                        sp.maxRel, sp.minSlideF, sp.minAccF);
            ok = false;
        }
    }
    std::printf("\n%-30s %8s %8s %8s %8s\n", "line", "onMAE*", "bendP", "bendR", "x-rt");
    for (auto& r : res)
        std::printf("%-30s %8.2f %8.2f %8.2f %8.0f\n", r.name.c_str(), r.on_mae_attacked, r.bend_p, r.bend_r,
                    r.audio / std::max(r.secs, 1e-9));
    std::printf("(onMAE* = attacked notes only, slid-into notes excluded; x-rt = audio seconds per CPU second)\n");
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
