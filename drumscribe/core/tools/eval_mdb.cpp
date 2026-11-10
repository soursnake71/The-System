// Drum transcription on REAL recordings: MDB Drums (Southall et al. 2017, MedleyDB drum tracks with hand annotations).
//   eval_mdb <folder with kick/snare/toms/hh/ride/crash.wav> <MusicDelta_X_subclass.txt>
// The stems come from DrumSep on the dataset's drum-only track. Scores per class at +-50 ms (the usual tolerance for
// drum transcription) and +-20 ms; timing vs. the annotations (hand annotations are only good to a few ms).
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include "ds/modules.h"
#include "ds/kit.h"

static std::string cls(const std::string& piece)  // our pieces -> MDB classes
{
    if (piece == "kick") return "KD";
    if (piece == "snare" || piece == "side_stick") return "SD";
    if (piece == "hh_closed") return "CHH";
    if (piece == "hh_open") return "OHH";
    if (piece == "hh_pedal") return "PHH";
    if (piece.rfind("crash", 0) == 0) return "CRC";
    if (piece.rfind("ride", 0) == 0) return "RDC";
    return "TT";
}
static std::string mdb(const std::string& c)
{
    if (c == "SDG" || c == "SDB" || c == "SDD" || c == "SST" || c == "SDF") return "SD";
    if (c == "HIT" || c == "MHT" || c == "HFT" || c == "LFT" || c == "LMT") return "TT";
    if (c == "RDC" || c == "RDB" || c == "SPC" || c == "CHC") return c == "RDB" ? "RDC" : (c == "RDC" ? "RDC" : "CRC");
    if (c == "TMB") return "";
    return c;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::cerr << "eval_mdb <stems dir> <subclass.txt>\n"; return 1; }
    std::map<std::string, ds::Signal> kit;
    for (auto& s : ds::stem_names()) kit[s] = ds::to_mono(ds::load_audio(std::string(argv[1]) + "/" + s + ".wav"));
    auto hits = ds::transcribe_drums(kit, ds::kSR);
    if (std::getenv("DUMP")) for (auto& h : hits) std::printf("HIT %.4f %s %d %.1f\n", h.time, h.piece.c_str(), h.velocity, h.level_db);
    std::vector<std::pair<double, std::string>> truth;
    std::ifstream f(argv[2]);
    for (std::string line; std::getline(f, line);) {
        std::istringstream is(line);
        double t; std::string c;
        if (is >> t >> c && !mdb(c).empty()) truth.push_back({t, mdb(c)});
    }
    // hi-hat: also score "any hi-hat" (open vs closed is often a judgement call in the annotations)
    for (double tol : {0.050, 0.020}) {
        std::map<std::string, std::array<int, 3>> s;  // tp fp fn
        std::vector<double> off;
        auto score = [&](auto key_t, auto key_h, bool collect) {
            std::vector<bool> used(hits.size(), false);
            std::map<std::string, std::array<int, 3>> r;
            for (auto& [t, c] : truth) {
                const std::string k = key_t(c);
                long best = -1; double bd = tol;
                for (size_t i = 0; i < hits.size(); ++i)
                    if (!used[i] && key_h(cls(hits[i].piece)) == k && std::fabs(hits[i].time - t) <= bd) { bd = std::fabs(hits[i].time - t); best = (long) i; }
                if (best < 0) r[k][2]++;
                else { used[(size_t) best] = true; r[k][0]++; if (collect) off.push_back((hits[(size_t) best].time - t) * 1000); }
            }
            for (size_t i = 0; i < hits.size(); ++i) if (!used[i]) r[key_h(cls(hits[i].piece))][1]++;
            return r;
        };
        auto id = [](const std::string& c) { return c; };
        auto hhmerge = [](const std::string& c) { return (c == "CHH" || c == "OHH" || c == "PHH") ? std::string("HH(any)") : c; };
        s = score(id, id, tol > 0.03);
        auto s2 = score(hhmerge, hhmerge, false);
        s["HH(any)"] = s2["HH(any)"];
        std::printf("tolerance %2.0f ms\n%-8s %5s %5s %5s %6s\n", tol * 1000, "class", "tp", "fp", "fn", "F");
        int tp = 0, fp = 0, fn = 0;
        for (auto& [k, v] : s) {
            if (k != "HH(any)") { tp += v[0]; fp += v[1]; fn += v[2]; }
            std::printf("%-8s %5d %5d %5d %6.3f\n", k.c_str(), v[0], v[1], v[2], 2.0 * v[0] / std::max(1, 2 * v[0] + v[1] + v[2]));
        }
        std::printf("ALL      %5d %5d %5d %6.3f\n", tp, fp, fn, 2.0 * tp / std::max(1, 2 * tp + fp + fn));
        if (!off.empty()) {
            std::sort(off.begin(), off.end());
            double mae = 0;
            for (double o : off) mae += std::fabs(o);
            std::printf("offset vs annotation: median %+.2f ms, mean abs %.2f ms\n", off[off.size() / 2], mae / (double) off.size());
        }
        std::printf("\n");
    }
}
