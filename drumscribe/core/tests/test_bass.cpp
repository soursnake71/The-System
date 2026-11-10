// Bass transcription against exact ground truth (tests/bass_synth.h).
// Four renderings (seeds) of each line kind are pooled; prints a table and returns non-zero when a threshold fails.
// BASS_DEBUG=1 lists every mismatch (2: every note, 3: with bends), BASS_SEED=n / BASS_ONLY=k narrow the run.
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

// counts and errors pooled over several renderings (seeds) of one kind of line
struct Acc {
    int ntruth = 0, nest = 0, tp = 0, pn = 0, pok = 0;
    int sl_tp = 0, sl_est = 0, sl_tru = 0, ac_tp = 0, ac_est = 0, ac_tru = 0, b_tp = 0, b_est = 0, b_tru = 0;
    std::vector<double> on, on_att, rel, cents;
    double secs = 0, audio = 0;
};

static void evaluate(const bsynth::Line& L, int debug, Acc& A) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<ds::BassNote> est = ds::transcribe_bass(L.sig, 44100);
    A.secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    A.audio += L.sig.size() / 44100.0;
    const auto& tru = L.notes;
    A.ntruth += int(tru.size());
    A.nest += int(est.size());
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
    for (int i = 0; i < int(tru.size()); ++i) {
        int j = mt[i];
        if (j < 0) continue;
        ++A.tp;
        A.on.push_back(1e3 * std::fabs(est[j].time - tru[i].time));
        if (!(i > 0 && tru[i - 1].slide)) A.on_att.push_back(A.on.back());
        A.rel.push_back(1e3 * std::fabs(est[j].end - tru[i].end));
        A.cents.push_back(std::fabs(est[j].cents));
        A.sl_tru += tru[i].slide;
        A.sl_est += est[j].slide_to_next;
        A.sl_tp += tru[i].slide && est[j].slide_to_next;
        A.ac_tru += tru[i].accent;
        A.ac_est += est[j].accent;
        A.ac_tp += tru[i].accent && est[j].accent;
        A.b_tru += tru[i].bend;
        A.b_est += !est[j].bends.empty();
        A.b_tp += tru[i].bend && !est[j].bends.empty();
    }
    // pitch accuracy: among onset matches regardless of pitch
    {
        std::vector<int> used(est.size(), 0);

        for (auto& t : tru) {
            int bj = -1;
            double bd = 0.030;
            for (int j = 0; j < int(est.size()); ++j) {
                double d = std::fabs(est[j].time - t.time);
                if (!used[j] && d <= bd) { bd = d; bj = j; }
            }
            if (bj < 0) continue;
            used[bj] = 1;
            ++A.pn;
            A.pok += est[bj].note == t.note;
        }
    }
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
            if (debug > 2 && (bad || debug > 3))
                for (auto& b : e.bends) std::printf("        bend %.4f %+.3f\n", b.time, b.semitones);
        }
        for (int j = 0; j < int(est.size()); ++j)
            if (me[j] < 0)
                std::printf("  EXTRA t=%8.4f-%8.4f n=%d v=%d\n", est[j].time, est[j].end, est[j].note, est[j].velocity);
    }
}

static Result finish(const std::string& name, const Acc& A) {
    Result r;
    r.name = name;
    r.ntruth = A.ntruth;
    r.nest = A.nest;
    r.tp = A.tp;
    r.pitch_acc = safe_div(A.pok, A.pn);
    r.P = safe_div(r.tp, r.nest);
    r.R = safe_div(r.tp, r.ntruth);
    r.F = r.P + r.R > 0 ? 2 * r.P * r.R / (r.P + r.R) : 0;
    r.on_mae = mean(A.on);
    r.on_p95 = pct(A.on, 0.95);
    r.on_mae_attacked = mean(A.on_att);
    r.rel_mae = mean(A.rel);
    r.rel_p95 = pct(A.rel, 0.95);
    r.cents_mae = mean(A.cents);
    r.slide_p = safe_div(A.sl_tp, A.sl_est);
    r.slide_r = safe_div(A.sl_tp, A.sl_tru);
    r.slide_f = r.slide_p + r.slide_r > 0 ? 2 * r.slide_p * r.slide_r / (r.slide_p + r.slide_r) : 0;
    r.acc_p = safe_div(A.ac_tp, A.ac_est);
    r.acc_r = safe_div(A.ac_tp, A.ac_tru);
    r.bend_p = safe_div(A.b_tp, A.b_est);
    r.bend_r = safe_div(A.b_tp, A.b_tru);
    r.secs = A.secs;
    r.audio = A.audio;
    return r;
}

int main() {
    const char* dbg = std::getenv("BASS_DEBUG");
    const int debug = dbg ? std::atoi(dbg) : 0;
    // every line kind is rendered with 4 seeds (different notes, timing, phases, bleed) and the numbers pooled;
    // BASS_SEED=n runs only seed n, BASS_ONLY=k only line kind k (0 a, 1 b, 2 a+bleed, 3 b+bleed, 4 d)
    std::vector<uint64_t> seeds = {0, 1, 2, 3};
    if (const char* sd = std::getenv("BASS_SEED")) seeds = {std::strtoull(sd, nullptr, 10)};
    // maxFalse: on a line without slides, the share of matched notes that may be flagged slide_to_next
    struct Spec { std::string name; double minF, maxOn, maxRel, minSlideF, minAccF, maxFalse; };
    // thresholds: what the algorithm achieves, with margin
    const Spec specs[5] = {{"a: 303 acid", 0.97, 2.0, 5.0, 0.90, 0.90, 0},
                           {"b: fingered bass", 0.97, 2.0, 5.0, 0.80, 0.80, 0.04},
                           {"c: a + kick bleed -12 dB, noise", 0.93, 2.0, 8.0, 0.80, 0.85, 0},
                           {"c: b + kick bleed -12 dB, noise", 0.93, 2.0, 15.0, 0.00, 0.60, 0.04},
                           // modelled on real rock/punk stems: pitch reaches the next note before the pluck, compressed
                           {"d: picked legato, compressed", 0.93, 5.0, 25.0, 0.00, 0.90, 0.15}};
    Acc acc[5];
    const char* only = std::getenv("BASS_ONLY");
    for (uint64_t so : seeds) {
        bsynth::Line a = bsynth::make_acid(7 + so), b = bsynth::make_fingered(11 + so);
        bsynth::Line ac = a, bc = b, d = bsynth::make_picked(31 + so);
        bsynth::add_bleed(ac, -12, -50, -40, 21 + so);
        bsynth::add_bleed(bc, -12, -50, -40, 23 + so);
        const bsynth::Line* L[5] = {&a, &b, &ac, &bc, &d};
        for (int k = 0; k < 5; ++k)
            if (!only || std::atoi(only) == k) evaluate(*L[k], debug, acc[k]);
    }
    bool ok = true;
    std::printf("%-32s %5s %5s %5s %5s %6s %6s %6s %6s %6s %6s %6s %5s %5s %5s %5s\n", "line (4 seeds pooled)", "ntru",
                "nest", "P", "R", "F", "onMAE", "onP95", "relMAE", "relP95", "pitch", "c_err", "slP", "slR", "accP",
                "accR");
    std::vector<Result> res;
    for (int k = 0; k < 5; ++k) {
        if (only && std::atoi(only) != k) continue;
        const Spec& sp = specs[k];
        Result r = finish(sp.name, acc[k]);
        res.push_back(r);
        char slp[16], slr[16];
        if (acc[k].sl_tru > 0) {
            std::snprintf(slp, sizeof slp, "%5.2f", r.slide_p);
            std::snprintf(slr, sizeof slr, "%5.2f", r.slide_r);
        } else {  // no slides in the truth: show the false slides instead
            std::snprintf(slp, sizeof slp, "%3dfp", acc[k].sl_est);
            std::snprintf(slr, sizeof slr, "  n/a");
        }
        std::printf("%-32s %5d %5d %5.3f %5.3f %6.3f %6.2f %6.2f %6.2f %6.2f %6.3f %6.1f %5s %5s %5.2f %5.2f\n",
                    r.name.c_str(), r.ntruth, r.nest, r.P, r.R, r.F, r.on_mae, r.on_p95, r.rel_mae, r.rel_p95,
                    r.pitch_acc, r.cents_mae, slp, slr, r.acc_p, r.acc_r);
        double accF = r.acc_p + r.acc_r > 0 ? 2 * r.acc_p * r.acc_r / (r.acc_p + r.acc_r) : 0;
        bool slides_ok = acc[k].sl_tru > 0 ? r.slide_f >= sp.minSlideF : acc[k].sl_est <= sp.maxFalse * acc[k].tp;
        bool pass = r.F >= sp.minF && r.on_mae < sp.maxOn && r.rel_mae < sp.maxRel && slides_ok && accF >= sp.minAccF;
        if (!pass) {
            std::printf("  FAIL (need F>=%.2f onMAE<%.1f relMAE<%.1f slideF>=%.2f (or <%.0f%% false slides) accentF>=%.2f)\n",
                        sp.minF, sp.maxOn, sp.maxRel, sp.minSlideF, 100 * sp.maxFalse, sp.minAccF);
            ok = false;
        }
    }
    std::printf("\n%-32s %8s %8s %8s %8s\n", "line", "onMAE*", "bendP", "bendR", "x-rt");
    for (auto& r : res)
        std::printf("%-32s %8.2f %8.2f %8.2f %8.0f\n", r.name.c_str(), r.on_mae_attacked, r.bend_p, r.bend_r,
                    r.audio / std::max(r.secs, 1e-9));
    std::printf("(onMAE* = attacked notes only, slid-into notes excluded; x-rt = audio seconds per CPU second)\n");
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
