// Drum transcription against exact ground truth: hit F-measure per kit piece, attack-time error, note-length error,
// velocity correlation. Perfect stems with -26 dB bleed between them (the separator is tested on its own).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include "drum_synth.h"
#include "ds/modules.h"

struct Row { int n = 0, tp = 0, fp = 0, fn = 0; std::vector<double> err, len_err; std::vector<std::pair<double, double>> vel; };

static double corr(const std::vector<std::pair<double, double>>& v)
{
    if (v.size() < 4) return NAN;
    double ma = 0, mb = 0;
    for (auto& p : v) { ma += p.first; mb += p.second; }
    ma /= (double) v.size(); mb /= (double) v.size();
    double c = 0, a = 0, b = 0;
    for (auto& p : v) { c += (p.first - ma) * (p.second - mb); a += (p.first - ma) * (p.first - ma); b += (p.second - mb) * (p.second - mb); }
    return c / std::sqrt(a * b + 1e-20);
}

int main()
{
    int fails = 0;
    for (unsigned seed : {1u, 2u, 3u}) {
        auto R = synth::render(seed);
        auto t0 = std::chrono::steady_clock::now();
        auto hits = ds::transcribe_drums(R.stems, synth::SR);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::map<std::string, Row> rows;
        std::vector<bool> used(hits.size(), false);
        for (auto& e : R.truth) {
            auto& row = rows[e.piece];
            row.n++;
            long best = -1;
            double bd = 0.025;
            for (size_t i = 0; i < hits.size(); ++i)
                if (!used[i] && hits[i].piece == e.piece && std::fabs(hits[i].time - e.t) <= bd) { bd = std::fabs(hits[i].time - e.t); best = (long) i; }
            if (best < 0) { row.fn++; continue; }
            used[(size_t) best] = true;
            row.tp++;
            row.err.push_back((hits[(size_t) best].time - e.t) * 1000);
            row.len_err.push_back((hits[(size_t) best].end - hits[(size_t) best].time) - (e.end - e.t));
            row.vel.push_back({e.v, (double) hits[(size_t) best].velocity});
        }
        for (size_t i = 0; i < hits.size(); ++i) if (!used[i]) rows[hits[i].piece].fp++;
        int tp = 0, fp = 0, fn = 0;
        std::vector<double> all, lens;
        std::printf("seed %u: %zu hits in %.2f s of CPU for %.0f s of drums\n", seed, hits.size(), secs, R.duration);
        std::printf("%-12s %4s %4s %4s %4s %6s %7s %7s %8s %6s\n", "piece", "n", "tp", "fp", "fn", "F", "mae ms", "max ms", "len ms", "vel r");
        for (auto& [p, r] : rows) {
            tp += r.tp; fp += r.fp; fn += r.fn;
            double mae = 0, mx = 0, lm = 0;
            for (double e : r.err) { mae += std::fabs(e); mx = std::max(mx, std::fabs(e)); all.push_back(std::fabs(e)); }
            for (double e : r.len_err) { lm += std::fabs(e); lens.push_back(std::fabs(e)); }
            if (!r.err.empty()) { mae /= (double) r.err.size(); lm /= (double) r.len_err.size(); }
            const double f = 2.0 * r.tp / std::max(1, 2 * r.tp + r.fp + r.fn);
            std::printf("%-12s %4d %4d %4d %4d %6.3f %7.2f %7.2f %8.1f %6.2f\n", p.c_str(), r.n, r.tp, r.fp, r.fn, f, mae, mx, lm * 1000, corr(r.vel));
        }
        std::sort(all.begin(), all.end());
        std::sort(lens.begin(), lens.end());
        double mae = 0;
        for (double e : all) mae += e;
        mae /= std::max<size_t>(1, all.size());
        const double p95 = all.empty() ? 0 : all[(size_t) (0.95 * (double) (all.size() - 1))];
        const double lmed = lens.empty() ? 0 : lens[lens.size() / 2] * 1000;
        const double F = 2.0 * tp / std::max(1, 2 * tp + fp + fn);
        std::printf("TOTAL F=%.4f  attack mae=%.2f ms p95=%.2f ms  length median err=%.1f ms  fp=%d fn=%d\n\n", F, mae, p95, lmed, fp, fn);
        if (F < 0.95 || mae > 0.6 || p95 > 2.5) { std::printf("FAIL seed %u\n", seed); ++fails; }
    }
    return fails ? 1 : 0;
}
