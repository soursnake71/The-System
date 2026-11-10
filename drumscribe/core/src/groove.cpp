// The mathematics of the groove, measured against the beat grid (track_beats or a given one).
//
// Every drum hit and bass onset gets a beat position p (beat number + fraction, linear between the grid's beats). The
// swing of the 8ths (s8 = position of the off-beat 8th, 0.5 straight .. 0.667 triplet) and of the 16ths (s16 = where
// the 'e' / 'a' 16th splits its 8th) is fitted to all events (coordinate ascent on a Gaussian match score), then each
// event is assigned to its nearest swung 16th step. From that:
//   microtiming   offset vs the SWUNG grid (ms; + = behind / laid back), per instrument and per bar step
//   template      offset vs the STRAIGHT 16th grid + mean velocity per bar step (the feel fingerprint for quantized MIDI)
//   dynamics      velocity stats, accent pattern per step, ghost share (velocity < 45 % of the 90th percentile)
//   density       hits per active bar;  syncopation: Longuet-Higgins & Lee per bar
//   push/pull     on-beat offsets kick / snare / hats / bass and their differences; bass-kick lock
//   humanization  residual timing sd after removing each (instrument, step) mean, its entropy, a 0-100 score
//   tempo         mean / median / sd / min / max, drift (linear fit, BPM per minute), abrupt sections (binary
//                 segmentation of the bar tempi with an abruptness test, so gradual drift is not split)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <sstream>
#include "ds/modules.h"

namespace ds {
namespace {

using json = nlohmann::json;
enum Inst { KICK, SNARE, HIHAT, TOMS, RIDE, CRASH, BASS, NINST };
const char* const kKey[NINST] = {"kick", "snare", "hihat", "toms", "ride", "crash", "bass"};
const char* const kName[NINST] = {"Kick", "Snare", "Hi-hat", "Toms", "Ride", "Crash", "Bass"};

int inst_of(const std::string& p, const std::string& stem)
{
    auto starts = [&](const char* s) { return p.rfind(s, 0) == 0; };
    if (p == "kick") return KICK;
    if (p == "snare" || p == "side_stick") return SNARE;
    if (starts("hh")) return HIHAT;
    if (starts("tom") || starts("floor")) return TOMS;
    if (starts("ride")) return RIDE;
    if (starts("crash")) return CRASH;
    if (stem == "kick") return KICK;
    if (stem == "snare") return SNARE;
    if (stem == "hh") return HIHAT;
    if (stem == "ride") return RIDE;
    if (stem == "crash") return CRASH;
    return TOMS;
}

double r3(double x) { return std::round(x * 1000) / 1000; }
double r2(double x) { return std::round(x * 100) / 100; }
double r1(double x) { return std::round(x * 10) / 10; }

struct Stat {
    double n = 0, s = 0, ss = 0;
    void add(double x) { n += 1; s += x; ss += x * x; }
    double mean() const { return n ? s / n : 0; }
    double sd() const { return n > 1 ? std::sqrt(std::max(0.0, (ss - s * s / n) / (n - 1))) : 0; }
};

double median(std::vector<double> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
double quantile(std::vector<double> v, double q)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double x = q * (v.size() - 1);
    const size_t i = (size_t) x;
    return i + 1 < v.size() ? v[i] + (x - i) * (v[i + 1] - v[i]) : v.back();
}

// ---------------------------------------------------------------- the grid as a function
struct Grid {
    const BeatGrid& g;
    int n, bpb;
    explicit Grid(const BeatGrid& gg) : g(gg), n((int) gg.beats.size()), bpb(std::max(1, gg.beats_per_bar)) {}
    double pos(double t) const
    {
        const auto& b = g.beats;
        if (n < 2) return 0;
        if (t < b[0]) return (t - b[0]) / (b[1] - b[0]);
        if (t >= b[n - 1]) return (n - 1) + (t - b[n - 1]) / (b[n - 1] - b[n - 2]);
        const int i = (int) (std::upper_bound(b.begin(), b.end(), t) - b.begin()) - 1;
        return i + (t - b[i]) / (b[i + 1] - b[i]);
    }
    double time(double p) const
    {
        const auto& b = g.beats;
        const int i = (int) std::floor(p);
        if (i < 0) return b[0] + p * (b[1] - b[0]);
        if (i >= n - 1) return b[n - 1] + (p - (n - 1)) * (b[n - 1] - b[n - 2]);
        return b[i] + (p - i) * (b[i + 1] - b[i]);
    }
    // bar of beat i: (bar number, 0-based from the first downbeat; negative = pickup), start beat, length in beats
    void bar_of(int i, int& bar, int& start, int& len) const
    {
        const auto& d = g.downbeats;
        if (d.empty()) { bar = (int) std::floor((double) i / bpb); start = bar * bpb; len = bpb; return; }
        if (i < d[0]) {
            const int k = (d[0] - i + bpb - 1) / bpb;
            bar = -k; start = d[0] - k * bpb; len = bpb; return;
        }
        const int j = (int) (std::upper_bound(d.begin(), d.end(), i) - d.begin()) - 1;
        bar = j; start = d[j];
        len = j + 1 < (int) d.size() ? d[j + 1] - d[j] : bpb;
        if (i >= start + len) {  // past the last downbeat's bar
            const int k = (i - start) / bpb;
            bar = j + k; start = d[j] + k * bpb; len = bpb;
        }
    }
};

struct Ev {
    double t;
    int inst, vel;
    double p = 0;                 // beat position
    int beat = 0, k = 0;          // assigned beat and 16th step in it (0..3)
    int bar = 0, step = -1;       // bar number and step in the bar (-1 when the bar is irregular)
    double off_sw = 0, off_st = 0;  // ms vs swung / straight grid
};

double step_pos(int k, double s8, double s16)
{
    switch (k) {
    case 0: return 0;
    case 1: return s16 * s8;
    case 2: return s8;
    default: return s8 + s16 * (1 - s8);
    }
}

double swing_score(const std::vector<Ev>& ev, double s8, double s16)
{
    const double pos[5] = {0, s16 * s8, s8, s8 + s16 * (1 - s8), 1};
    double s = 0;
    for (auto& e : ev) {
        if (e.inst == CRASH) continue;
        const double f = e.p - std::floor(e.p);
        double d = 1;
        for (double q : pos) d = std::min(d, std::abs(f - q));
        s += std::exp(-0.5 * (d / 0.025) * (d / 0.025));
    }
    return s;
}

// ---------------------------------------------------------------- tempo sections
struct Section { int b0, b1; };
void segment(const std::vector<double>& x, int lo, int hi, double sigma, std::vector<int>& cuts)
{
    const int minlen = 4;
    if (hi - lo < 2 * minlen) return;
    double best = 0;
    int bk = -1;
    std::vector<double> cs(hi - lo + 1, 0);
    for (int i = lo; i < hi; ++i) cs[i - lo + 1] = cs[i - lo] + x[i];
    for (int k = lo + minlen; k <= hi - minlen; ++k) {
        const double nl = k - lo, nr = hi - k;
        const double ml = cs[k - lo] / nl, mr = (cs[hi - lo] - cs[k - lo]) / nr;
        const double t = std::abs(ml - mr) * std::sqrt(nl * nr / (nl + nr));
        if (t > best) { best = t; bk = k; }
    }
    if (bk < 0) return;
    const double nl = bk - lo, nr = hi - bk;
    const double ml = (cs[bk - lo]) / nl, mr = (cs[hi - lo] - cs[bk - lo]) / nr, diff = std::abs(ml - mr);
    // abruptness: the two bars either side of the cut must already differ by most of the step
    const double near = std::abs((x[bk - 1] + x[bk - 2]) / 2 - (x[bk] + x[bk + 1]) / 2);
    if (diff >= std::max(2.0, 0.015 * (ml + mr) / 2) && best / std::max(sigma, 0.05) > 6 && near >= 0.6 * diff) {
        cuts.push_back(bk);
        segment(x, lo, bk, sigma, cuts);
        segment(x, bk, hi, sigma, cuts);
    }
}

std::string fmt(const char* f, double a) { char b[160]; std::snprintf(b, sizeof b, f, a); return b; }
std::string fmt(const char* f, double a, double b2) { char b[160]; std::snprintf(b, sizeof b, f, a, b2); return b; }
std::string fmt(const char* f, const char* s, double a) { char b[160]; std::snprintf(b, sizeof b, f, s, a); return b; }

json arr_or_null(const std::vector<Stat>& v, bool sd = false, int digits = 2)
{
    json a = json::array();
    for (auto& s : v) {
        if (!s.n) a.push_back(nullptr);
        else {
            const double x = sd ? s.sd() : s.mean();
            a.push_back(digits == 1 ? r1(x) : r2(x));
        }
    }
    return a;
}

}  // namespace

json analyze_groove(const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass, const BeatGrid& bg,
                    double duration)
{
    json out;
    out["version"] = 1;
    out["duration_s"] = r3(duration);
    out["hits"] = {{"drums", drums.size()}, {"bass", bass.size()}};
    const int bpb = std::max(1, bg.beats_per_bar);
    const bool compound = bpb == 2;
    out["meter"] = {{"beats_per_bar", bpb}, {"signature", compound ? "6/8" : std::to_string(bpb) + "/4"}};
    out["findings"] = json::array();
    if (bg.beats.size() < 2) { out["error"] = "no beat grid"; return out; }
    const Grid G(bg);
    const int spb = compound ? 3 : 4;  // steps per beat: 16ths, or the three 8ths of a dotted beat in 6/8
    const int steps = bpb * spb;

    // ---- events
    std::vector<Ev> ev;
    for (auto& h : drums) ev.push_back({h.time, inst_of(h.piece, h.stem), std::max(1, std::min(127, h.velocity))});
    for (auto& b : bass) ev.push_back({b.time, BASS, std::max(1, std::min(127, b.velocity))});
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    for (auto& e : ev) e.p = G.pos(e.t);

    // ---- tempo (over the played stretch, not the extrapolated beats)
    {
        json tj;
        int i0 = 0, i1 = G.n - 1;
        if (!ev.empty()) {
            i0 = std::max(0, (int) std::floor(ev.front().p + 0.5) - 0);
            i1 = std::min(G.n - 1, (int) std::ceil(ev.back().p - 0.5));
            if (i1 - i0 < 2) { i0 = 0; i1 = G.n - 1; }
        }
        std::vector<double> bpm, tt;
        for (int i = i0; i < i1; ++i) { bpm.push_back(60 / (bg.beats[i + 1] - bg.beats[i])); tt.push_back(bg.beats[i]); }
        Stat st;
        for (double b : bpm) st.add(b);
        const double mean = 60.0 * (i1 - i0) / (bg.beats[i1] - bg.beats[i0]);
        // linear trend of the per-beat tempo against time
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t k = 0; k < bpm.size(); ++k) { sx += tt[k]; sy += bpm[k]; sxx += tt[k] * tt[k]; sxy += tt[k] * bpm[k]; }
        const double nn = (double) bpm.size(), den = nn * sxx - sx * sx;
        const double slope = den > 1e-12 ? (nn * sxy - sx * sy) / den : 0;  // BPM per second
        const double span = bg.beats[i1] - bg.beats[i0];
        // bar tempi
        std::vector<double> barbpm;
        std::vector<int> barstart;
        for (size_t j = 0; j + 1 < bg.downbeats.size(); ++j) {
            const int a = bg.downbeats[j], b = bg.downbeats[j + 1];
            if (a < i0 || b > i1) continue;
            barbpm.push_back(60.0 * (b - a) / (bg.beats[b] - bg.beats[a]));
            barstart.push_back((int) j);
        }
        double mn = st.n ? *std::min_element(bpm.begin(), bpm.end()) : mean, mx = st.n ? *std::max_element(bpm.begin(), bpm.end()) : mean;
        if (barbpm.size() >= 2) { mn = *std::min_element(barbpm.begin(), barbpm.end()); mx = *std::max_element(barbpm.begin(), barbpm.end()); }
        tj["mean_bpm"] = r2(mean);
        tj["median_bpm"] = r2(median(bpm));
        tj["sd_bpm"] = r3(st.sd());
        tj["min_bpm"] = r2(mn);
        tj["max_bpm"] = r2(mx);
        tj["drift_bpm_per_min"] = r3(slope * 60);
        tj["change_bpm"] = r2(slope * span);
        // sections
        json secs = json::array();
        std::vector<int> cuts;
        if (barbpm.size() >= 8) {
            std::vector<double> dif;
            for (size_t k = 1; k < barbpm.size(); ++k) dif.push_back(std::abs(barbpm[k] - barbpm[k - 1]));
            const double sigma = 1.4826 * median(dif) / std::sqrt(2.0);
            segment(barbpm, 0, (int) barbpm.size(), sigma, cuts);
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.insert(cuts.begin(), 0);
        cuts.push_back((int) barbpm.size());
        if (barbpm.size() >= 1)
            for (size_t c = 0; c + 1 < cuts.size(); ++c) {
                const int j0 = barstart[cuts[c]], j1 = barstart[cuts[c + 1] - 1] + 1;  // bars [j0, j1)
                const int a = bg.downbeats[j0], b = bg.downbeats[j1];
                secs.push_back({{"start_bar", j0 + 1}, {"end_bar", j1}, {"start_s", r3(bg.beats[a])}, {"end_s", r3(bg.beats[b])},
                                {"bpm", r2(60.0 * (b - a) / (bg.beats[b] - bg.beats[a]))}});
            }
        tj["sections"] = secs;
        out["tempo"] = tj;
        auto& F = out["findings"];
        if (st.sd() < 1.0 && std::abs(slope * span) < 1 && secs.size() <= 1)
            F.push_back(fmt("Tempo %.1f BPM, steady (sd %.2f BPM)", mean, st.sd()));
        else
            F.push_back(fmt("Tempo %.1f BPM (range %.1f", mean, mn) + fmt("-%.1f, sd %.2f BPM)", mx, st.sd()));
        if (secs.size() > 1) {
            std::string s = "Tempo changes: " + fmt("%.1f", secs[0]["bpm"].get<double>());
            for (size_t k = 1; k < secs.size(); ++k)
                s += fmt(" -> %.1f BPM at bar %.0f", secs[k]["bpm"].get<double>(), secs[k]["start_bar"].get<int>());
            F.push_back(s);
        } else if (std::abs(slope * span) >= 1.0) {
            F.push_back(fmt(slope > 0 ? "Tempo rises %.1f BPM over the song (%+.2f BPM/min)" : "Tempo falls %.1f BPM over the song (%+.2f BPM/min)",
                            std::abs(slope * span), slope * 60));
        }
    }
    if (ev.empty()) return out;

    // ---- swing: coordinate ascent of (s8, s16), then per-instrument precise means
    double s8 = 0.5, s16 = 0.5;
    if (!compound) {
        double best = swing_score(ev, s8, s16);
        for (int round = 0; round < 3; ++round) {
            for (int pass = 0; pass < 2; ++pass)
                for (double v = 0.40; v <= 0.7501; v += 0.0025) {
                    const double sc = pass == 0 ? swing_score(ev, v, s16) : swing_score(ev, s8, v);
                    const double cur = pass == 0 ? s8 : s16;
                    // only move on a clear gain, and towards straight on ties
                    if (sc > best * 1.01 || (sc > best - 1e-9 && std::abs(v - 0.5) < std::abs(cur - 0.5) && sc >= best)) {
                        best = sc;
                        (pass == 0 ? s8 : s16) = v;
                    }
                }
        }
    }
    // assignment to the nearest swung 16th, bar and step
    for (auto& e : ev) {
        const double fl = std::floor(e.p), f = e.p - fl;
        double bd = 2;
        int bk = 0;
        auto posk = [&](int k) { return compound ? k / 3.0 : step_pos(k, s8, s16); };
        for (int k = 0; k <= spb; ++k) {
            const double q = k == spb ? 1.0 : posk(k);
            if (std::abs(f - q) < bd) { bd = std::abs(f - q); bk = k; }
        }
        e.beat = (int) fl + (bk == spb ? 1 : 0);
        e.k = bk == spb ? 0 : bk;
        e.off_sw = (e.t - G.time(e.beat + posk(e.k))) * 1000;
        e.off_st = (e.t - G.time(e.beat + (double) e.k / spb)) * 1000;
        int start, len;
        G.bar_of(e.beat, e.bar, start, len);
        e.step = len == bpb ? (e.beat - start) * spb + e.k : -1;
    }
    {
        json sw;
        Stat a8, a16;
        json per = json::object();
        for (int in = 0; in < NINST && !compound; ++in) {
            if (in == CRASH) continue;
            // ratios against the instrument's OWN neighbouring hits where they exist (immune to grid error and to
            // the instrument's lag), else against the grid minus the instrument's mean on-beat offset
            std::map<int, double> on, off8;
            Stat lag;
            for (auto& e : ev) {
                if (e.inst != in) continue;
                if (e.k == 0) { on[e.beat] = e.t; lag.add(e.p - e.beat); }
                if (e.k == 2) off8[e.beat] = e.t;
            }
            const double d = lag.n ? lag.mean() : 0;
            auto get = [](const std::map<int, double>& m, int k, double& t) {
                auto it = m.find(k);
                if (it == m.end()) return false;
                t = it->second;
                return true;
            };
            Stat i8, i16;
            for (auto& e : ev) {
                if (e.inst != in) continue;
                const double f = e.p - e.beat - d;
                double a, b, c;
                if (e.k == 2) {
                    const double x = get(on, e.beat, a) && get(on, e.beat + 1, b) ? (e.t - a) / (b - a) : f;
                    if (x > 0.3 && x < 0.85) { i8.add(x); a8.add(x); }
                } else if (e.k == 1) {
                    const double x = get(on, e.beat, a) && get(off8, e.beat, c) ? (e.t - a) / (c - a) : f / s8;
                    if (x > 0.1 && x < 0.9) { i16.add(x); a16.add(x); }
                } else if (e.k == 3) {
                    const double x = get(off8, e.beat, c) && get(on, e.beat + 1, b) ? (e.t - c) / (b - c) : (f - s8) / (1 - s8);
                    if (x > 0.1 && x < 0.9) { i16.add(x); a16.add(x); }
                }
            }
            if (!i8.n && !i16.n) continue;
            json j;
            j["eighths_pct"] = i8.n ? json(r2(100 * i8.mean())) : json(nullptr);
            j["sixteenths_pct"] = i16.n ? json(r2(100 * i16.mean())) : json(nullptr);
            j["n_eighths"] = (int) i8.n;
            j["n_sixteenths"] = (int) i16.n;
            per[kKey[in]] = j;
        }
        sw["eighths_pct"] = a8.n ? json(r2(100 * a8.mean())) : json(nullptr);
        sw["sixteenths_pct"] = a16.n ? json(r2(100 * a16.mean())) : json(nullptr);
        sw["grid_eighths_pct"] = r2(100 * s8);
        sw["grid_sixteenths_pct"] = r2(100 * s16);
        sw["per_instrument"] = per;
        out["swing"] = sw;
        const double e8 = a8.n ? 100 * a8.mean() : 50, e16 = a16.n ? 100 * a16.mean() : 50;
        out["meter"]["subdivision"] = compound ? "ternary" : s8 >= 0.62 ? "swung (triplet feel)" : s8 >= 0.53 || s16 >= 0.53 ? "swung" : "straight";
        out["meter"]["compound"] = compound;
        auto& F = out["findings"];
        if (a8.n >= 4 && e8 >= 53) F.push_back(fmt("8ths swung %.1f%% (straight 50, triplet 66.7)", e8));
        if (a16.n >= 4 && e16 >= 53) F.push_back(fmt("16ths swung %.1f%%", e16));
        if ((a8.n < 4 || e8 < 53) && (a16.n < 4 || e16 < 53) && !compound)
            F.push_back(fmt("Straight feel (8ths at %.1f%%)", a8.n ? e8 : 50.0));
        if (compound) F.push_back("Compound meter: 6/8, the beat is a dotted quarter (three 8ths)");
    }

    // ---- per instrument: microtiming, dynamics, density, syncopation
    std::vector<int> present(NINST, 0);
    for (auto& e : ev) present[e.inst]++;
    std::vector<int> active_bars;
    for (auto& e : ev) active_bars.push_back(e.bar);
    std::sort(active_bars.begin(), active_bars.end());
    active_bars.erase(std::unique(active_bars.begin(), active_bars.end()), active_bars.end());
    const double nbars = std::max<size_t>(1, active_bars.size());
    out["bars"] = (int) active_bars.size();

    json micro = json::object(), dyn = json::object(), dens = json::object(), sync = json::object();
    std::vector<double> on_mean(NINST, NAN), on_sd(NINST, NAN);
    // metrical weights (LHL): bar 0, half bar (4/4) -1, beat -2, 8th -3, 16th -4
    std::vector<int> mw(steps);
    for (int s = 0; s < steps; ++s) {
        const int beat = s / spb, k = s % spb;
        if (compound) mw[s] = s == 0 ? 0 : k == 0 ? -1 : -3;  // 6/8: bar, dotted beat, 8th
        else mw[s] = s == 0 ? 0 : k == 0 ? (bpb == 4 && beat == 2 ? -1 : -2) : k == 2 ? -3 : -4;
    }
    auto lhl = [&](const std::vector<int>& insts) {
        // onset grid over all bars (regular bars only), syncopation summed per bar, averaged over active bars
        std::map<int, std::vector<char>> bars;
        for (auto& e : ev)
            if (e.step >= 0 && std::find(insts.begin(), insts.end(), e.inst) != insts.end()) {
                auto& v = bars[e.bar];
                if (v.empty()) v.assign(steps, 0);
                v[e.step] = 1;
            }
        if (bars.empty()) return std::make_pair(0.0, 0);
        const int b0 = bars.begin()->first, b1 = bars.rbegin()->first;
        std::vector<char> seq;
        for (int b = b0; b <= b1; ++b) {
            auto it = bars.find(b);
            for (int s = 0; s < steps; ++s) seq.push_back(it == bars.end() ? 0 : it->second[s]);
        }
        double total = 0;
        const int N = (int) seq.size();
        for (int i = 0; i < N; ++i) {
            if (!seq[i]) continue;
            int j = i + 1;
            int wr = -100;
            while (j < N && !seq[j]) { wr = std::max(wr, mw[j % steps]); ++j; }
            if (j >= N) break;  // no following note: the rest is open-ended
            if (wr > mw[i % steps]) total += wr - mw[i % steps];
        }
        return std::make_pair(total / (double) bars.size(), (int) bars.size());
    };
    for (int in = 0; in < NINST; ++in) {
        if (!present[in]) continue;
        Stat on, all;
        std::vector<Stat> per(steps);
        std::vector<double> vels;
        std::vector<Stat> vstep(steps);
        std::vector<int> stepcnt(steps, 0);
        for (auto& e : ev) {
            if (e.inst != in) continue;
            all.add(e.off_sw);
            if (e.k == 0) on.add(e.off_sw);
            if (e.step >= 0) { per[e.step].add(e.off_sw); vstep[e.step].add(e.vel); }
            vels.push_back(e.vel);
        }
        const Stat& use = on.n >= 4 ? on : all;
        on_mean[in] = use.mean();
        on_sd[in] = use.sd();
        const double m = use.mean();
        json mj;
        mj["mean_ms"] = r2(m);
        mj["sd_ms"] = r2(use.sd());
        mj["n"] = (int) use.n;
        mj["reference"] = on.n >= 4 ? "on-beat hits" : "all hits";
        mj["label"] = std::abs(m) < 2.5 ? "on the beat" : m > 0 ? "behind (laid back)" : "ahead (pushing)";
        mj["per_step_ms"] = arr_or_null(per);
        micro[kKey[in]] = mj;

        Stat v;
        for (double x : vels) v.add(x);
        const double ghost_thr = 0.45 * quantile(vels, 0.9);
        int ghosts = 0;
        for (double x : vels) ghosts += x < ghost_thr;
        std::string pat;
        for (int s = 0; s < steps; ++s) {
            if (vstep[s].n < 0.25 * nbars) pat += '.';
            else if (vstep[s].mean() < ghost_thr) pat += 'g';
            else if (v.sd() > 3 && vstep[s].mean() >= v.mean() + 0.5 * v.sd()) pat += 'X';
            else pat += 'x';
        }
        json dj;
        dj["mean"] = r2(v.mean());
        dj["sd"] = r2(v.sd());
        dj["min"] = (int) *std::min_element(vels.begin(), vels.end());
        dj["max"] = (int) *std::max_element(vels.begin(), vels.end());
        dj["range"] = dj["max"].get<int>() - dj["min"].get<int>();
        dj["ghost_share"] = r3((double) ghosts / vels.size());
        dj["per_step"] = arr_or_null(vstep, false, 1);
        dj["pattern"] = pat;
        dyn[kKey[in]] = dj;
        dens[kKey[in]] = r2(present[in] / nbars);
        if (in != CRASH) sync[kKey[in]] = r2(lhl({in}).first);
    }
    out["microtiming"] = micro;
    out["dynamics"] = dyn;
    out["density"] = {{"per_bar", dens}, {"total_per_bar", r2((double) ev.size() / nbars)}};
    {
        const auto all = lhl({KICK, SNARE, TOMS, BASS});
        const double idx = all.first;
        out["syncopation"] = {{"index", r2(idx)}, {"bars", all.second}, {"per_instrument", sync},
                              {"method", "Longuet-Higgins & Lee per bar (kick+snare+toms+bass)"},
                              {"label", idx < 0.5 ? "on the grid" : idx < 2 ? "lightly syncopated" : idx < 5 ? "syncopated" : "highly syncopated"}};
    }

    // ---- 16-step template vs the straight grid
    {
        std::vector<Stat> off(steps), vel(steps);
        json per = json::object();
        for (int in = 0; in < NINST; ++in) {
            if (!present[in] || in == CRASH) continue;
            std::vector<Stat> o2(steps), v2(steps);
            for (auto& e : ev)
                if (e.inst == in && e.step >= 0) {
                    o2[e.step].add(e.off_st); v2[e.step].add(e.vel);
                    if (in != BASS) { off[e.step].add(e.off_st); vel[e.step].add(e.vel); }
                }
            per[kKey[in]] = {{"offset_ms", arr_or_null(o2)}, {"velocity", arr_or_null(v2, false, 1)}};
        }
        json cnt = json::array();
        for (auto& s : off) cnt.push_back((int) s.n);
        out["template"] = {{"steps", steps}, {"grid", compound ? "8ths (3 per dotted beat)" : "straight 16ths"}, {"offset_ms", arr_or_null(off)},
                           {"velocity", arr_or_null(vel, false, 1)}, {"count", cnt}, {"per_instrument", per}};
    }

    // ---- push / pull
    {
        auto val = [&](int in) { return std::isnan(on_mean[in]) ? json(nullptr) : json(r2(on_mean[in])); };
        auto dif = [&](int a, int b) {
            return std::isnan(on_mean[a]) || std::isnan(on_mean[b]) ? json(nullptr) : json(r2(on_mean[a] - on_mean[b]));
        };
        json pp = {{"kick_ms", val(KICK)}, {"snare_ms", val(SNARE)}, {"hihat_ms", val(HIHAT)}, {"bass_ms", val(BASS)},
                   {"snare_vs_kick_ms", dif(SNARE, KICK)}, {"hihat_vs_kick_ms", dif(HIHAT, KICK)},
                   {"bass_vs_kick_ms", dif(BASS, KICK)}, {"snare_vs_hihat_ms", dif(SNARE, HIHAT)}};
        std::vector<int> ord;
        for (int in : {KICK, SNARE, HIHAT, RIDE, BASS}) if (!std::isnan(on_mean[in])) ord.push_back(in);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return on_mean[a] < on_mean[b]; });
        json o = json::array();
        for (int in : ord) o.push_back(kKey[in]);
        pp["order_early_to_late"] = o;
        out["push_pull"] = pp;
        auto& F = out["findings"];
        for (int in : {SNARE, KICK, HIHAT, RIDE, BASS}) {
            if (std::isnan(on_mean[in]) || std::abs(on_mean[in]) < 2.5) continue;
            F.push_back(on_mean[in] > 0 ? fmt("%s sits %.1f ms behind the beat (laid back)", kName[in], on_mean[in])
                                        : fmt("%s pushes %.1f ms ahead of the beat", kName[in], -on_mean[in]));
        }
        if (!std::isnan(on_mean[SNARE]) && !std::isnan(on_mean[KICK]) && std::abs(on_mean[SNARE] - on_mean[KICK]) >= 3)
            F.push_back(fmt("Snare lands %.1f ms ", std::abs(on_mean[SNARE] - on_mean[KICK])) +
                        (on_mean[SNARE] > on_mean[KICK] ? "later" : "earlier") + " than the kick");
        int tight = -1, loose = -1;
        for (int in : {KICK, SNARE, HIHAT, RIDE, BASS}) {
            if (std::isnan(on_sd[in]) || present[in] < 8) continue;
            if (tight < 0 || on_sd[in] < on_sd[tight]) tight = in;
            if (loose < 0 || on_sd[in] > on_sd[loose]) loose = in;
        }
        if (tight >= 0 && loose >= 0 && tight != loose)
            F.push_back(std::string(kName[tight]) + fmt(" is tightest (sd %.1f ms), ", on_sd[tight]) + kName[loose] +
                        fmt(" loosest (sd %.1f ms)", on_sd[loose]));
    }

    // ---- bass / kick lock
    {
        std::vector<double> kicks;
        for (auto& e : ev) if (e.inst == KICK) kicks.push_back(e.t);
        int nb = 0, w20 = 0;
        Stat m;
        for (auto& e : ev) {
            if (e.inst != BASS) continue;
            ++nb;
            if (kicks.empty()) continue;
            auto it = std::lower_bound(kicks.begin(), kicks.end(), e.t);
            double d = 1e9;
            if (it != kicks.end()) d = e.t - *it;
            if (it != kicks.begin() && std::abs(e.t - *(it - 1)) < std::abs(d)) d = e.t - *(it - 1);
            if (std::abs(d) <= 0.020) ++w20;
            if (std::abs(d) <= 0.050) m.add(d * 1000);
        }
        if (nb) {
            const double share = (double) w20 / nb;
            json bk = {{"bass_notes", nb}, {"share_within_20ms", r3(share)}, {"matched_within_50ms", (int) m.n},
                       {"mean_offset_ms", m.n ? json(r2(m.mean())) : json(nullptr)}, {"sd_ms", m.n ? json(r2(m.sd())) : json(nullptr)}};
            bk["label"] = !m.n ? "independent of the kick" : share >= 0.5 ? (std::abs(m.mean()) < 2.5 ? "locked to the kick" : m.mean() > 0 ? "locked, bass behind the kick" : "locked, bass ahead of the kick") : "loosely related to the kick";
            out["bass_kick_lock"] = bk;
            if (m.n && kicks.size())
                out["findings"].push_back(fmt("Bass-kick lock: %.0f%% of bass notes within 20 ms of a kick", 100 * share) +
                                          (std::abs(m.mean()) >= 1 ? fmt(", bass %.1f ms ", std::abs(m.mean())) + (m.mean() > 0 ? "after" : "before") + " the kick" : std::string(", dead on")));
        }
    }

    // ---- dynamics findings
    {
        auto& F = out["findings"];
        for (int in : {HIHAT, RIDE}) {
            if (!dyn.contains(kKey[in])) continue;
            const auto& d = dyn[kKey[in]];
            const auto& ps = d["per_step"];
            Stat onb, offb;
            for (int s = 0; s < steps; ++s) if (!ps[s].is_null()) (s % spb == 0 ? onb : offb).add(ps[s].get<double>());
            if (onb.n && offb.n && std::abs(onb.mean() - offb.mean()) >= 8)
                F.push_back(std::string(kName[in]) + (onb.mean() > offb.mean() ? " accents the beats" : " accents the off-beats") +
                            fmt(" (%.0f vs %.0f)", std::max(onb.mean(), offb.mean()), std::min(onb.mean(), offb.mean())));
        }
        for (int in : {SNARE, HIHAT}) {
            if (!dyn.contains(kKey[in])) continue;
            const double g = dyn[kKey[in]]["ghost_share"].get<double>();
            if (g >= 0.05) F.push_back(std::string(kName[in]) + fmt(" ghost notes: %.0f%% of its hits", 100 * g));
        }
        const double idx = out["syncopation"]["index"].get<double>();
        F.push_back(fmt("Syncopation %.2f per bar (Longuet-Higgins & Lee): ", idx) + out["syncopation"]["label"].get<std::string>());
    }

    // ---- humanization: residuals after each (instrument, step) cell mean
    {
        std::map<std::pair<int, int>, Stat> cell, vcell;
        for (auto& e : ev) if (e.step >= 0 && e.inst != CRASH) { cell[{e.inst, e.step}].add(e.off_sw); vcell[{e.inst, e.step}].add(e.vel); }
        double ss = 0, vss = 0, sys = 0;
        int n = 0, cells = 0, nsys = 0;
        std::vector<int> hist(101, 0);
        for (auto& [k, s] : cell) if (s.n >= 3) { ++cells; sys += s.n * s.mean() * s.mean(); nsys += (int) s.n; }
        for (auto& e : ev) {
            if (e.step < 0 || e.inst == CRASH) continue;
            const auto& c = cell[{e.inst, e.step}];
            if (c.n < 3) continue;
            const double r = e.off_sw - c.mean(), vr = e.vel - vcell[{e.inst, e.step}].mean();
            ss += r * r; vss += vr * vr; ++n;
            hist[std::max(0, std::min(100, (int) std::floor(r / 2.0) + 50))]++;
        }
        const double dof = std::max(1, n - cells);
        const double tsd = std::sqrt(ss / dof), vsd = std::sqrt(vss / dof);
        double H = 0;
        for (int c : hist) if (c) { const double p = (double) c / n; H -= p * std::log2(p); }
        const double score = 100 * (0.75 * (1 - std::exp(-tsd / 8)) + 0.25 * (1 - std::exp(-vsd / 8)));
        out["humanization"] = {{"timing_sd_ms", r2(tsd)}, {"systematic_ms", r2(nsys ? std::sqrt(sys / nsys) : 0)},
                               {"velocity_sd", r2(vsd)}, {"entropy_bits", r2(H)}, {"score", (int) std::lround(score)},
                               {"label", score < 15 ? "machine-tight" : score < 40 ? "tight human" : score < 70 ? "relaxed human" : "loose"}};
        out["findings"].push_back(fmt("Human feel %.0f/100", score) + fmt(" (timing sd %.1f ms, velocity sd %.1f)", tsd, vsd));
    }
    return out;
}

// ---------------------------------------------------------------- text report
std::string groove_text(const json& g)
{
    std::ostringstream o;
    char b[256];
    auto num = [](const json& j, const char* k, double d = NAN) {
        return j.contains(k) && j[k].is_number() ? j[k].get<double>() : d;
    };
    o << "GROOVE ANALYSIS\n";
    if (g.contains("tempo")) {
        const auto& t = g["tempo"];
        std::snprintf(b, sizeof b, "Tempo   %.2f BPM  median %.2f  sd %.2f  range %.1f-%.1f  drift %+.2f BPM/min\n",
                      num(t, "mean_bpm"), num(t, "median_bpm"), num(t, "sd_bpm"), num(t, "min_bpm"), num(t, "max_bpm"),
                      num(t, "drift_bpm_per_min"));
        o << b;
        if (t.contains("sections") && t["sections"].size() > 1) {
            o << "Sections";
            for (auto& s : t["sections"]) { std::snprintf(b, sizeof b, "  bar %d: %.1f", s["start_bar"].get<int>(), s["bpm"].get<double>()); o << b; }
            o << "\n";
        }
    }
    if (g.contains("meter")) {
        const auto& m = g["meter"];
        o << "Meter   " << m.value("signature", "?") << "  " << m.value("subdivision", "") << "\n";
    }
    if (g.contains("swing")) {
        const auto& s = g["swing"];
        auto pct = [&](const char* k) {
            const double v = num(s, k);
            return std::isnan(v) ? std::string("-") : fmt("%.1f%%", v);
        };
        o << "Swing   8ths " << pct("eighths_pct") << "  16ths " << pct("sixteenths_pct") << "  (50 straight, 66.7 triplet)\n";
    }
    if (g.contains("microtiming")) {
        o << "            offset ms   sd ms   vel  vel sd  ghost  /bar  accents\n";
        for (auto& [k, m] : g["microtiming"].items()) {
            const json d = g.contains("dynamics") && g["dynamics"].contains(k) ? g["dynamics"][k] : json::object();
            const double dens = g.contains("density") ? num(g["density"]["per_bar"], k.c_str(), 0) : 0;
            std::snprintf(b, sizeof b, "  %-8s %+9.2f %7.2f %5.0f %7.1f %5.0f%% %5.1f  %s\n", k.c_str(), num(m, "mean_ms"),
                          num(m, "sd_ms"), num(d, "mean", 0), num(d, "sd", 0), 100 * num(d, "ghost_share", 0), dens,
                          d.value("pattern", "").c_str());
            o << b;
        }
    }
    if (g.contains("push_pull")) {
        const auto& p = g["push_pull"];
        o << "Push/pull (ms):";
        for (const char* k : {"snare_vs_kick_ms", "hihat_vs_kick_ms", "bass_vs_kick_ms"})
            if (p.contains(k) && p[k].is_number()) { std::snprintf(b, sizeof b, "  %s %+.2f", std::string(k).substr(0, std::string(k).size() - 3).c_str(), p[k].get<double>()); o << b; }
        o << "\n";
    }
    if (g.contains("bass_kick_lock")) {
        const auto& l = g["bass_kick_lock"];
        std::snprintf(b, sizeof b, "Bass-kick lock  %.0f%% within 20 ms, mean offset %+.2f ms (%s)\n", 100 * num(l, "share_within_20ms", 0),
                      num(l, "mean_offset_ms", 0), l.value("label", "").c_str());
        o << b;
    }
    if (g.contains("syncopation")) {
        std::snprintf(b, sizeof b, "Syncopation  %.2f per bar (LHL), %s\n", num(g["syncopation"], "index", 0),
                      g["syncopation"].value("label", "").c_str());
        o << b;
    }
    if (g.contains("template")) {
        const auto& t = g["template"];
        o << "Template (straight-grid offset ms / velocity per step):\n";
        const int n = t.value("steps", 16);
        for (int half = 0; half < (n + 7) / 8; ++half) {
            std::string a = "  ", v = "  ";
            for (int s = half * 8; s < std::min(n, half * 8 + 8); ++s) {
                const auto& x = t["offset_ms"][s];
                const auto& y = t["velocity"][s];
                if (x.is_null()) std::snprintf(b, sizeof b, "%7s", "-"); else std::snprintf(b, sizeof b, "%+7.1f", x.get<double>());
                a += b;
                if (y.is_null()) std::snprintf(b, sizeof b, "%7s", "-"); else std::snprintf(b, sizeof b, "%7.0f", y.get<double>());
                v += b;
            }
            o << a << "\n" << v << "\n";
        }
    }
    if (g.contains("humanization")) {
        const auto& h = g["humanization"];
        std::snprintf(b, sizeof b, "Human feel  %d/100 (%s): timing sd %.2f ms, entropy %.2f bits, velocity sd %.1f\n",
                      h.value("score", 0), h.value("label", "").c_str(), num(h, "timing_sd_ms", 0), num(h, "entropy_bits", 0),
                      num(h, "velocity_sd", 0));
        o << b;
    }
    if (g.contains("findings")) {
        o << "Findings:\n";
        int k = 0;
        for (auto& f : g["findings"]) { if (++k > 10) break; o << "  - " << f.get<std::string>() << "\n"; }
    }
    return o.str();
}

}  // namespace ds
