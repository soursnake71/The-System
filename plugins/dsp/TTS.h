// The talking machine, ported from the web app (ttsWord / ttsParse / ttsRender in index.html):
// letter-to-sound rules and a small dictionary -> phonemes -> a formant synthesizer (Rosenberg glottal pulse,
// five cascade resonators, nasal and frication branches) at 22.05 kHz, with a speech-like pitch contour.
#pragma once
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include "Common.h"

namespace omega::dsp::tts {

#include "../gen/tts_rules.inc"

struct Rule { std::string l, m, r; std::vector<std::string> p; };

inline std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (std::isspace((unsigned char) c)) { if (!cur.empty()) out.push_back(cur), cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Tables {
    std::map<char, std::vector<Rule>> rules; std::map<std::string, std::string> dict;
    Tables() {
        for (int i = 0; kRules[i]; ++i) {
            const std::string r = kRules[i]; const auto a = r.find('['), b = r.find(']', a), eq = r.find('=', b);
            Rule x; x.l = r.substr(0, a); x.m = r.substr(a + 1, b - a - 1); x.r = r.substr(b + 1, eq - b - 1); x.p = split(r.substr(eq + 1));
            rules[x.m[0]].push_back(x);
        }
        for (int i = 0; kDict[i]; i += 2) dict[kDict[i]] = kDict[i + 1];
    }
};
inline const Tables& tables() { static const Tables t; return t; }

inline std::string number(long n) {
    static const char* NUM[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* TENS[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 20) return NUM[n];
    if (n < 100) return std::string(TENS[n / 10]) + (n % 10 ? std::string(" ") + NUM[n % 10] : "");
    if (n < 1000) return std::string(NUM[n / 100]) + " hundred" + (n % 100 ? " " + number(n % 100) : "");
    if (n < 1000000) return number(n / 1000) + " thousand" + (n % 1000 ? " " + number(n % 1000) : "");
    std::string s = std::to_string(n), o; for (char c : s) { if (!o.empty()) o += ' '; o += NUM[c - '0']; } return o;
}

inline std::vector<std::string> word(const std::string& wd) {
    const auto& T = tables();
    if (auto it = T.dict.find(wd); it != T.dict.end()) return split(it->second);
    const std::string w = " " + wd + " "; std::vector<std::string> out;
    const std::string VOW = "AEIOUY", VOICED = "BDVGJLMNRWZ";
    auto ch = [&](int i) { return i >= 0 && i < (int) w.size() ? w[(size_t) i] : ' '; };
    auto isV = [&](char c) { return VOW.find(c) != std::string::npos; };
    auto isC = [&](char c) { return c >= 'A' && c <= 'Z' && !isV(c); };
    auto in = [](char c, const char* set) { return c != 0 && std::string(set).find(c) != std::string::npos; };
    auto sub = [&](int i, size_t n) { return i >= 0 && i < (int) w.size() ? w.substr((size_t) i, n) : std::string(); };
    // context matcher: returns the new index, or -9999 when it fails
    auto ctx = [&](const std::string& pat, int i, int dir) {
        std::string seq = pat; if (dir < 0) seq = std::string(pat.rbegin(), pat.rend());
        for (char p : seq) {
            const char c = ch(i);
            if (p == '#') { if (!isV(c)) return -9999; while (isV(ch(i))) i += dir; }
            else if (p == ':') { while (isC(ch(i))) i += dir; }
            else if (p == '^') { if (!isC(c)) return -9999; i += dir; }
            else if (p == '.') { if (!in(c, VOICED.c_str())) return -9999; i += dir; }
            else if (p == '+') { if (!in(c, "EIY")) return -9999; i += dir; }
            else if (p == '@') { const std::string two = sub(i, 2); if (!in(c, "TSRDLZNJ") && !(dir > 0 && (two == "TH" || two == "CH" || two == "SH"))) return -9999; i += dir; }
            else if (p == '&') { if (!in(c, "SCGZXJ")) return -9999; i += dir; }
            else if (p == '%') { if (dir < 0) return -9999; const char* sfx[] = {"ING", "ELY", "ER", "ES", "ED", "E"}; bool ok = false;
                for (auto* s : sfx) { const std::string S = s; if (sub(i, S.size()) == S) { i += (int) S.size(); ok = true; break; } } if (!ok) return -9999; }
            else if (p == ' ') { if (c >= 'A' && c <= 'Z') return -9999; i += dir; }
            else { if (c != p) return -9999; i += dir; }
        }
        return i;
    };
    int i = 1;
    while (i < (int) w.size() - 1) {
        auto it = T.rules.find(w[(size_t) i]);
        if (it == T.rules.end()) { i++; continue; }
        bool done = false;
        for (const auto& r : it->second) {
            if (sub(i, r.m.size()) != r.m) continue;
            if (!r.l.empty() && ctx(r.l, i - 1, -1) == -9999) continue;
            if (!r.r.empty() && ctx(r.r, i + (int) r.m.size(), 1) == -9999) continue;
            out.insert(out.end(), r.p.begin(), r.p.end()); i += (int) r.m.size(); done = true; break;
        }
        if (!done) i++;
    }
    return out;
}

struct Ph {
    double v = 0; bool son = false, nas = false, asp = false; int stop = 0;   // stop: 1 = unvoiced (p), 2 = voiced (b)
    double f[3] {}, d = 0; bool hasF2 = false; double f2[3] {};
    bool fr = false; double frC = 0, frB = 0, frA = 0; double burst[2] {}; std::string aff;
};
inline const std::map<std::string, Ph>& phones() {
    static const std::map<std::string, Ph> P = [] {
        std::map<std::string, Ph> m;
        auto V = [&](const char* k, double f1, double f2, double f3, double d, double a = 0, double b = 0, double c = 0) { Ph p; p.v = 1; p.f[0] = f1; p.f[1] = f2; p.f[2] = f3; p.d = d; if (a) { p.hasF2 = true; p.f2[0] = a; p.f2[1] = b; p.f2[2] = c; } m[k] = p; };
        V("IY", 270, 2290, 3010, 130); V("IH", 390, 1990, 2550, 90); V("EH", 530, 1840, 2480, 110); V("AE", 660, 1720, 2410, 150); V("AA", 730, 1090, 2440, 150); V("AO", 570, 840, 2410, 150);
        V("UH", 440, 1020, 2240, 90); V("UW", 300, 870, 2240, 130); V("AH", 640, 1190, 2390, 100); V("ER", 490, 1350, 1690, 140); V("AX", 500, 1500, 2500, 60);
        V("EY", 480, 1900, 2500, 170, 300, 2200, 2900); V("AY", 700, 1200, 2500, 200, 320, 2100, 2800); V("OW", 560, 900, 2400, 170, 350, 750, 2300); V("AW", 720, 1200, 2450, 200, 380, 900, 2300); V("OY", 560, 850, 2400, 200, 330, 2100, 2800);
        auto S = [&](const char* k, bool nas, double f1, double f2, double f3, double d) { Ph p; p.v = 1; p.son = !nas; p.nas = nas; p.f[0] = f1; p.f[1] = f2; p.f[2] = f3; p.d = d; m[k] = p; };
        S("L", false, 360, 1100, 2700, 70); S("R", false, 420, 1250, 1600, 70); S("W", false, 300, 650, 2200, 60); S("Y", false, 260, 2070, 3020, 60);
        S("M", true, 270, 1000, 2200, 75); S("N", true, 270, 1600, 2600, 70); S("NG", true, 270, 2000, 2600, 80);
        auto F = [&](const char* k, double v, double c, double b, double a, double f1, double f2, double f3, double d) { Ph p; p.v = v; p.fr = true; p.frC = c; p.frB = b; p.frA = a; p.f[0] = f1; p.f[1] = f2; p.f[2] = f3; p.d = d; m[k] = p; };
        F("S", 0, 6200, 2000, 1, 320, 1700, 2600, 110); F("Z", .5, 6000, 2000, .6, 240, 1700, 2600, 90); F("SH", 0, 3200, 1800, 1, 300, 1840, 2750, 120); F("ZH", .5, 3000, 1800, .6, 300, 1840, 2750, 90);
        F("F", 0, 5000, 5000, .35, 340, 1100, 2400, 100); F("V", .6, 5000, 5000, .25, 220, 1100, 2400, 70); F("TH", 0, 5500, 5000, .3, 320, 1400, 2700, 100); F("DH", .6, 5500, 5000, .2, 270, 1400, 2700, 60);
        { Ph p; p.asp = true; p.d = 70; m["HH"] = p; }
        auto P2 = [&](const char* k, int stop, double f1, double f2, double f3, double b0, double b1, double d, const char* aff = "") { Ph p; p.stop = stop; p.f[0] = f1; p.f[1] = f2; p.f[2] = f3; p.burst[0] = b0; p.burst[1] = b1; p.d = d; p.aff = aff; m[k] = p; };
        P2("P", 1, 300, 900, 2400, 1000, 3000, 90); P2("B", 2, 200, 900, 2400, 1000, 3000, 70); P2("T", 1, 300, 1700, 2600, 4500, 3000, 90); P2("D", 2, 200, 1700, 2600, 4000, 3000, 70);
        P2("K", 1, 300, 2000, 2700, 2200, 1500, 90); P2("G", 2, 200, 2000, 2700, 2000, 1500, 70); P2("CH", 1, 300, 1700, 2600, 3500, 2500, 70, "SH"); P2("JH", 2, 200, 1700, 2600, 3200, 2500, 60, "ZH");
        return m;
    }();
    return P;
}

struct Seg { std::string ph; int w = 0; int x = 0; };

inline std::vector<Seg> parse(const std::string& text0) {
    // numbers to words
    std::string text; for (size_t i = 0; i < text0.size();) {
        if (std::isdigit((unsigned char) text0[i])) { size_t j = i; while (j < text0.size() && std::isdigit((unsigned char) text0[j])) j++;
            long n = 0; try { n = std::stol(text0.substr(i, std::min<size_t>(j - i, 9))); } catch (...) {} text += " " + number(n) + " "; i = j; }
        else text += text0[i++];
    }
    std::vector<Seg> res; int wi = 0; const auto& PH = phones();
    size_t i = 0;
    auto isL = [](char c) { return std::isalpha((unsigned char) c) || c == '\''; };
    while (i < text.size()) {
        const char c = text[i];
        if (c == '/') { const auto e = text.find('/', i + 1); if (e != std::string::npos) {
                std::string in = text.substr(i + 1, e - i - 1); for (auto& ch : in) ch = (char) std::toupper((unsigned char) ch);
                for (auto& p : split(in)) res.push_back({p, wi, 0}); wi++; i = e + 1; continue; } }
        if (isL(c)) { size_t j = i; while (j < text.size() && isL(text[j])) j++;
            std::string W; for (size_t k = i; k < j; ++k) { const char u = (char) std::toupper((unsigned char) text[k]); if ((u >= 'A' && u <= 'Z') || u == '\'') W += u; }
            int stretch = 0; for (size_t k = 1; k < W.size(); ++k) if (W[k] == W[k - 1] && std::string("AEIOUH").find(W[k]) != std::string::npos) stretch++;
            for (auto& p : word(W)) res.push_back({p, wi, stretch});
            wi++; i = j; continue; }
        if (c == '.' || c == '!' || c == '?') { size_t j = i; while (j < text.size() && (text[j] == '.' || text[j] == '!' || text[j] == '?')) j++; res.push_back({c == '?' ? "?" : ".", wi, 0}); i = j; continue; }
        if (c == ',' || c == ';' || c == ':' || c == '-') { res.push_back({"_", wi, 0}); i++; continue; }
        i++;
    }
    // collapse doubled consonants ("stuff" -> S T AH F)
    std::vector<Seg> out;
    for (size_t k = 0; k < res.size(); ++k) {
        if (k && res[k].ph == res[k - 1].ph) { auto it = PH.find(res[k].ph); if (it == PH.end() || it->second.v == 0) continue; }
        out.push_back(res[k]);
    }
    return out;
}

struct Opts { double pitch = 110, speed = 1; bool robot = false; double sr = 22050; };

inline std::vector<float> render(const std::string& text, const Opts& o, Rand& rnd) {
    const double sr = o.sr, f0 = o.pitch, sp = 1 / (o.speed > 0 ? o.speed : 1); const bool robot = o.robot;
    const auto seq = parse(text); const auto& PH = phones();
    struct Fr { double av = 0, af = 0, ah = 0, fc = 0, fb = 0, f[3] {}; bool acc = false, nas = false; double f0 = -1, f0b = 1; };
    std::vector<Fr> fr;
    auto push = [&](int n, Fr p) { for (int k = 0; k < n; ++k) { fr.push_back(p); if (k == 0) p.acc = false; } };
    auto ms = [&](double d) { return std::max(1, (int) jround(d * sp / 5)); };
    auto mk = [](double av, const double* f) { Fr r; r.av = av; for (int j = 0; j < 3; ++j) r.f[j] = f[j]; return r; };
    double lastF[3] = {500, 1500, 2500}; int wordStart = -1;
    const Ph& AX = PH.at("AX");
    auto nextVowel = [&](size_t k) -> const Ph& { for (size_t j = k + 1; j < seq.size(); ++j) { auto it = PH.find(seq[j].ph); if (it == PH.end()) break; const Ph& q = it->second; if (q.v == 1 && !q.son && !q.nas) return q; } return AX; };
    struct End { size_t at; bool q; }; std::vector<End> ends;
    for (size_t k = 0; k < seq.size(); ++k) {
        const auto& s = seq[k];
        if (s.ph == "_") { push(ms(140), mk(0, lastF)); continue; }
        if (s.ph == "." || s.ph == "?") { ends.push_back({fr.size(), s.ph == "?"}); push(ms(260), mk(0, lastF)); continue; }
        auto it = PH.find(s.ph); if (it == PH.end()) continue; const Ph& P = it->second;
        const bool acc = s.w != wordStart; wordStart = s.w;
        auto setLast = [&](const double* f) { for (int j = 0; j < 3; ++j) lastF[j] = f[j]; };
        if (P.v == 1 && !P.son && !P.nas) {
            const int n = ms(P.d * (1 + s.x * .6));
            if (P.hasF2) { const int h = (int) jround(n * .45); Fr a = mk(1, P.f); a.acc = acc; push(h, a); push(n - h, mk(1, P.f2)); setLast(P.f2); }
            else { Fr a = mk(1, P.f); a.acc = acc; push(n, a); setLast(P.f); }
        } else if (P.son || P.nas) { Fr a = mk(P.nas ? .55 : .8, P.f); a.nas = P.nas; push(ms(P.d), a); setLast(P.f); }
        else if (P.fr) { Fr a = mk(P.v * .6, P.f); a.af = P.frA; a.fc = P.frC; a.fb = P.frB; push(ms(P.d), a); setLast(P.f); }
        else if (P.asp) { const Ph& nv = nextVowel(k); Fr a = mk(0, nv.f); a.ah = .5; push(ms(P.d), a); setLast(nv.f); }
        else if (P.stop) {
            const bool vo = P.stop == 2; const Ph& nv = nextVowel(k);
            const double fc[3] = {200, P.f[1], P.f[2]};
            push(ms(P.d * .6), mk(vo ? .12 : 0, fc));
            Fr b = mk(vo ? .3 : 0, P.f); b.af = .9; b.fc = P.burst[0]; b.fb = P.burst[1]; push(ms(12), b);
            if (!P.aff.empty()) { const Ph& A = PH.at(P.aff); Fr c = mk(A.v * .5, A.f); c.af = A.frA; c.fc = A.frC; c.fb = A.frB; push(ms(60), c); }
            else if (!vo) { Fr c = mk(0, nv.f); c.ah = .4; push(ms(35), c); }
            setLast(P.f);
        }
    }
    if (fr.empty()) return std::vector<float>(1, 0.f);
    push(ms(80), mk(0, lastF));
    size_t s0 = 0; if (ends.empty()) ends.push_back({fr.size(), false});
    for (const auto& e : ends) {
        const double len = (double) std::max<size_t>(1, e.at - s0);
        for (size_t k = s0; k < e.at && k < fr.size(); ++k) {
            const double x = (k - s0) / len; double p = robot ? 1 : 1.08 - .2 * x;
            if (!robot && x > .82) p *= e.q ? 1 + (x - .82) * 1.6 : 1 - (x - .82) * .6;
            fr[k].f0 = f0 * p;
        }
        s0 = e.at;
    }
    for (size_t k = 0; k < fr.size(); ++k) { if (fr[k].f0 < 0) fr[k].f0 = f0;
        if (!robot && fr[k].acc) for (size_t j = k; j < std::min(fr.size(), k + 14); ++j) fr[j].f0b *= 1 + .1 * std::sin(PI * (double) (j - k) / 14); }
    // synthesis
    const int hop = (int) jround(sr * .005); std::vector<float> out(fr.size() * (size_t) hop);
    struct Res { double y1 = 0, y2 = 0; }; Res R[5], RF[2], RN;
    const double BW[5] = {70, 90, 150, 250, 300};
    struct Co { double A, B, C; };
    auto coef = [&](double f, double bw) { const double r = std::exp(-PI * bw / sr), C = -r * r, B = 2 * r * std::cos(2 * PI * f / sr), A = 1 - B - C; return Co {A, B, C}; };
    auto run = [](Res& st, const Co& c, double x) { const double y = c.A * x + c.B * st.y1 + c.C * st.y2; st.y2 = st.y1; st.y1 = y; return y; };
    double cur[3] = {fr[0].f[0], fr[0].f[1], fr[0].f[2]}, ph = 0, prevU = 0;
    const double glide = 1 - std::exp(-1 / (sr * .018)), amp = 1 - std::exp(-1 / (sr * .006));
    double av = 0, af = 0, ah = 0, fc = 4000, fb = 2000, lastFc = 0, fcNorm = 1; Co fcCoef {1, 0, 0}; size_t n = 0;
    const Co nasC = coef(270, 120);
    for (size_t k = 0; k < fr.size(); ++k) {
        const Fr& T = fr[k];
        if (T.fc != 0) { fc = T.fc; fb = T.fb; }
        const double pitch = T.f0 * T.f0b;
        for (int s = 0; s < hop; ++s, ++n) {
            for (int j = 0; j < 3; ++j) cur[j] += (T.f[j] - cur[j]) * glide;
            av += (T.av - av) * amp; af += (T.af - af) * amp * 2; ah += (T.ah - ah) * amp;
            ph += pitch * (1 + (robot ? 0 : (rnd() - .5) * .01)) / sr; if (ph >= 1) ph -= 1;
            const double u = ph < .4 ? .5 * (1 - std::cos(PI * ph / .4)) : ph < .56 ? std::cos(PI * (ph - .4) / .32) : 0;
            const double g = (u - prevU) * 4; prevU = u;
            const double white = rnd() * 2 - 1;
            double x = g * av + white * ah * .25;
            if (T.nas) x = run(RN, nasC, x);
            for (int j = 0; j < 5; ++j) { const double F = j < 3 ? cur[j] : j == 3 ? 3500 : 4500; x = run(R[j], coef(F, BW[j] * (T.nas && j == 0 ? 2 : 1)), x); }
            if (fc != lastFc) { lastFc = fc; fcCoef = coef(fc, fb); const double w = 2 * PI * fc / sr, re = 1 - fcCoef.B * std::cos(w) - fcCoef.C * std::cos(2 * w), im = fcCoef.B * std::sin(w) + fcCoef.C * std::sin(2 * w); fcNorm = std::hypot(re, im) / fcCoef.A; }
            const double fz = run(RF[1], fcCoef, run(RF[0], fcCoef, white)) * fcNorm * fcNorm * af * .12;
            out[n] = (float) (x * .5 + fz);
        }
    }
    double m = 0, dc = 0, xp = 0;
    for (auto& s : out) { const double y = s - xp + .995 * dc; xp = s; dc = y; s = (float) y; m = std::max(m, std::fabs(y)); }
    if (m > 0) for (auto& s : out) s = (float) (s * .9 / m);
    return out;
}

struct Voice { const char* id; const char* label; double pitch; bool robot; };
inline const Voice* voices() {
    static const Voice V[] = {{"robot", "Robot (monotone)", 98, true}, {"deep", "Deep", 76, false}, {"normal", "Normal", 112, false}, {"high", "High", 190, false},
                              {"female", "Female", 215, false}, {"chipmunk", "Chipmunk", 320, false}};
    return V;
}

}  // namespace omega::dsp::tts
