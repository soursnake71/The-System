// Omega Linn kits in C++: the same rules as linn/kit.js (which file goes on which sound by its name, dynamics, how a
// recording becomes a ROM layer: its sample clock from its length, the open hat without its VCA decay). Used by the
// plugin's LOAD FOLDER. linn/tools/compare.mjs checks the name rules against kit.js.
#pragma once
#include "Linn.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace linn { namespace kit {
inline std::string words(const std::string& name) {
    std::string s; for (char c : name) s += (char) std::tolower((unsigned char) c);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < s.size()) { bool ext = true; for (size_t i = dot + 1; i < s.size(); ++i) if (!std::isalnum((unsigned char) s[i])) ext = false; if (ext) s = s.substr(0, dot); }
    std::string a; for (char c : s) a += (c == '_' || c == '-' || c == '.' || c == '(' || c == ')' || c == '[' || c == ']') ? ' ' : c;
    std::string b; for (size_t i = 0; i < a.size(); ++i) { b += a[i]; if (i + 1 < a.size() && a[i] >= 'a' && a[i] <= 'z' && a[i + 1] >= '0' && a[i + 1] <= '9') b += ' '; }
    std::string c; for (char ch : b) { if (ch == ' ' && (c.empty() || c.back() == ' ')) continue; c += ch; }
    while (!c.empty() && c.back() == ' ') c.pop_back();
    return " " + c + " ";
}
inline std::vector<std::string> tokens(const std::string& w) { std::vector<std::string> t; std::string cur; for (char c : w) { if (c == ' ') { if (!cur.empty()) t.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) t.push_back(cur); return t; }
// a short key: a word that starts with it and has up to 3 more letters
inline bool shortMatch(const std::string& w, const std::string& k) {
    for (const auto& t : tokens(w)) { if (t.compare(0, k.size(), k) != 0 || t.size() > k.size() + 3) continue; bool ok = true; for (size_t i = k.size(); i < t.size(); ++i) if (t[i] < 'a' || t[i] > 'z') ok = false; if (ok) return true; }
    return false;
}
inline bool hasWord(const std::string& w, std::initializer_list<const char*> ks) { for (const auto& t : tokens(w)) for (const char* k : ks) if (t == k) return true; return false; }
struct Rule { int g; std::vector<std::string> keys; };
inline const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {G::SIDESTICK, {"sidestick", "side stick", "xstick", "cross stick", "sst", "stick", "rim", "rimshot", "rs"}},
        {G::SNARE, {"snare", "sd", "snr", "sn"}},
        {G::TOM, {"tom", "toms"}},
        {G::CONGA, {"conga", "cga", "cng"}},
        {G::RIDE, {"ride", "rd"}},
        {G::CRASH, {"crash", "cr", "cymbal", "cym"}},
        {G::CABASA, {"cabasa", "cab", "cbs", "cbsa", "shaker"}},
        {G::TAMB, {"tambourine", "tamb", "tmb", "tam"}},
        {G::COWBELL, {"cowbell", "cowb", "cow", "cb", "bell"}},
        {G::CLAPS, {"claps", "clap", "clp", "cp", "handclap"}},
        {G::HAT, {"hihat", "hi hat", "hi-hat", "hat", "hh", "ohh", "chh", "oh", "ch"}},
        {G::BASS, {"kick", "bassdrum", "bass drum", "bd", "bass", "kik", "kck"}}};
    return r;
}
inline int groupOf(const std::string& name) {
    const std::string w = words(name);
    for (const auto& r : rules()) for (const auto& k : r.keys) if (k.size() >= 4 ? w.find(k) != std::string::npos : shortMatch(w, k)) return r.g;
    return -1;
}
inline bool isOpen(const std::string& name) { const std::string w = words(name); return hasWord(w, {"open", "oh", "ohh", "op"}) || w.find("open") != std::string::npos || w.find("ohh") != std::string::npos; }
inline bool isAlt(const std::string& name) {
    std::string w; for (char c : words(name)) if (c != ' ') w += c;
    for (const char* k : {"kickme", "alt", "kick2", "kick 2", "bd2", "bass2"}) if (w.find(k) != std::string::npos) return true;
    return false;
}
inline int dynOf(const std::string& name, int g) {
    if (g == G::TOM || g == G::CONGA) return -1;
    const std::string w = words(name);
    if (hasWord(w, {"soft", "quiet", "light", "pp", "p"}) || w.find(" lo vel ") != std::string::npos || w.find(" low vel ") != std::string::npos) return 0;
    if (hasWord(w, {"mid", "med", "medium", "mf"})) return 1;
    if (hasWord(w, {"hard", "loud", "accent", "acc", "ff", "f"})) return 2;
    return -1;
}
struct Classified { std::vector<std::vector<int>> groups; std::vector<int> dyn, left; };
inline Classified classify(const std::vector<std::string>& names) {
    Classified c; c.groups.resize(NG_ROM); c.dyn.assign(names.size(), -1);
    std::vector<int> hats, kicks;
    for (int i = 0; i < (int) names.size(); ++i) {
        const int g = groupOf(names[(size_t) i]);
        if (g >= 0) c.dyn[(size_t) i] = dynOf(names[(size_t) i], g);
        auto& L = c.groups[(size_t) std::max(0, g)];
        if (g < 0) c.left.push_back(i);
        else if (g == G::HAT) hats.push_back(i);
        else if (g == G::BASS && c.dyn[(size_t) i] < 0) kicks.push_back(i);
        else if (g == G::SNARE || g == G::TOM || g == G::CONGA) L.push_back(i);
        else if (c.dyn[(size_t) i] >= 0 ? !std::any_of(L.begin(), L.end(), [&](int j) { return c.dyn[(size_t) j] == c.dyn[(size_t) i] || c.dyn[(size_t) j] < 0; }) : L.empty()) L.push_back(i);
        else c.left.push_back(i);
    }
    int hat = -1; for (int i : hats) if (isOpen(names[(size_t) i])) { hat = i; break; }
    if (hat < 0 && !hats.empty()) hat = hats[0];
    if (hat >= 0) c.groups[G::HAT].push_back(hat);
    for (int i : hats) if (i != hat) c.left.push_back(i);
    std::vector<int> plain, alts; for (int i : kicks) (isAlt(names[(size_t) i]) ? alts : plain).push_back(i);
    std::stable_sort(plain.begin(), plain.end(), [&](int a, int b) { return names[(size_t) a].size() < names[(size_t) b].size(); });
    const int main = !plain.empty() ? plain[0] : !alts.empty() ? alts[0] : -1;
    int alt = -1; for (int i : alts) if (i != main) { alt = i; break; } if (alt < 0) for (int i : plain) if (i != main) { alt = i; break; }
    if (main >= 0 && c.groups[G::BASS].empty()) c.groups[G::BASS].push_back(main); else if (main >= 0) c.left.push_back(main);
    if (alt >= 0) c.groups[G::BASS2].push_back(alt);
    for (int i : kicks) if (i != main && i != alt) c.left.push_back(i);
    for (auto& L : c.groups) if (std::any_of(L.begin(), L.end(), [&](int i) { return c.dyn[(size_t) i] >= 0; }))
        for (int k = (int) L.size() - 1; k >= 0; --k) if (c.dyn[(size_t) L[(size_t) k]] < 0 && L.size() > 1) { c.left.push_back(L[(size_t) k]); L.erase(L.begin() + k); }
    return c;
}
inline double defaultClock(int g) { return g == G::SNARE || g == G::SIDESTICK ? snareClock(25) : g == G::TOM ? tomClock(50) : g == G::CONGA ? tomClock(46) : muxClock(25); }
inline bool tuned(int g) { return g == G::SNARE || g == G::SIDESTICK || g == G::TOM || g == G::CONGA; }
inline double peakOf(const std::vector<double>& m) { double pk = 1e-9; for (double x : m) pk = std::max(pk, std::abs(x)); return pk; }
inline Layer makeLayer(int g, const std::vector<double>& mono, double rate, bool open, int dyn = -1, double norm = 0) {
    const double pk = peakOf(mono), scale = norm > 0 ? norm : pk;
    size_t st = 0; while (st + 1 < mono.size() && std::abs(mono[st]) / pk < 0.002) st++;
    size_t end = mono.size(); while (end > st + 16 && std::abs(mono[end - 1]) / pk < 0.002) end--;
    Layer L; L.rate = rate; L.dyn = dyn; L.kit = 0; L.data.resize(end - st);
    for (size_t i = 0; i < L.data.size(); ++i) L.data[i] = mono[st + i] / scale;
    double clk = defaultClock(g);
    if (tuned(g)) { const double est = ROM_SIZE[g] * rate / (double) L.data.size(); if (est >= 8000 && est <= 80000) clk = est; }
    if (g == G::HAT && open) {
        const double hold = HAT_HOLD * rate, k = std::exp(-1 / (hatTau(true, 0) * rate)); double eg = 5;
        for (size_t i = 0; i < L.data.size(); ++i) { if ((double) i >= hold) eg *= k; L.data[i] /= std::max(0.05, hatVca(eg)); }
        double p2 = 1e-9; for (double x : L.data) p2 = std::max(p2, std::abs(x)); for (auto& x : L.data) x /= p2;
    }
    L.clk = jsRound(clk);
    return L;
}
struct Rec { std::vector<double> mono; double rate = 44100; };
// every file of a kit -> layers per group (recs in the order of names)
inline std::vector<std::vector<Layer>> buildKit(const std::vector<std::string>& names, const std::vector<Rec>& recs, Classified* out = nullptr) {
    const auto c = classify(names); if (out) *out = c;
    std::vector<std::vector<Layer>> groups((size_t) NG_ROM);
    for (int g = 0; g < NG_ROM; ++g) {
        const auto& list = c.groups[(size_t) g]; if (list.empty()) continue;
        const bool withDyn = std::any_of(list.begin(), list.end(), [&](int i) { return c.dyn[(size_t) i] >= 0; });
        double norm = 0; if (withDyn) for (int i : list) norm = std::max(norm, peakOf(recs[(size_t) i].mono));
        std::vector<Layer> ls;
        for (int i : list) if (recs[(size_t) i].mono.size() > 16) ls.push_back(makeLayer(g, recs[(size_t) i].mono, recs[(size_t) i].rate, g == G::HAT && isOpen(names[(size_t) i]), c.dyn[(size_t) i], norm));
        if (ls.empty()) continue;
        if (withDyn && tuned(g)) { const Layer* top = &ls[0]; for (const auto& L : ls) if (L.dyn > top->dyn) top = &L; const double ck = top->clk; for (auto& L : ls) L.clk = ck; }
        std::stable_sort(ls.begin(), ls.end(), [](const Layer& a, const Layer& b) { return a.dyn != b.dyn ? a.dyn < b.dyn : a.clk < b.clk; });
        groups[(size_t) g] = std::move(ls);
    }
    return groups;
}
}} // namespace linn::kit
