// Omega Solina engine: a line-for-line port of solina/engine.js (see that file for the circuit notes and component
// values from the Solina service schematics). solina/tools/compare.mjs renders both and checks they match.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace solina {

// MSVC has no kPi / kSqrt2; the same doubles as Math.PI and Math.SQRT2
static constexpr double kPi = 3.141592653589793, kSqrt2 = 1.4142135623730951;

struct ParamInfo { const char* id; const char* name; double min, max, def, step; const char* unit; const char* group; int cc; std::vector<const char*> choices; };
#include "Params.inc"

static constexpr int CH_TAB[6] = {pid::violin, pid::viola, pid::trumpet, pid::horn, pid::cello, pid::contra};
static constexpr int CH_BASE[6] = {pid::vnLvl, pid::vaLvl, pid::tpLvl, pid::hnLvl, pid::ceLvl, pid::cbLvl};
// offsets from a strip's Lvl
enum { oLvl, oPan, oPit, oAtk, oDec, oTon, oEns, oEch, oRev, oMut, oSol };
static const char* const CH_NAMES[6] = {"Violin", "Viola", "Trumpet", "Horn", "Cello", "Contra Bass"};

using Params = std::array<double, pid::count>;
inline Params defaults() { Params p{}; for (int i = 0; i < pid::count; i++) p[(size_t) i] = PARAMS[i].def; return p; }

class Engine {
public:
    using Bq = std::array<double, 5>;
    explicit Engine(double sampleRate) : fs(sampleRate) {
        p = defaults();
        static const int TOS[12] = {478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253};
        for (int pc = 0; pc < 12; pc++) {
            const double det = ((double) TOS[9] / TOS[pc]) / std::pow(2.0, (pc - 9) / 12.0);
            fTop[pc] = 440 * std::pow(2.0, (pc - 9) / 12.0 + 4) * det;
            shR[pc] = .35 * std::pow(2.0, (pc - 6) / 12.0); shA[pc] = std::exp(-.5 / shR[pc]);
        }
        ws.fill(-1);
        static const double G8_R[5] = {18000, 12000, 10000, 8200, 5600}, G8_C[5] = {2.2e-9, 4.7e-9, 10e-9, 12e-9, 18e-9};
        static const double G4_R[5] = {10000, 8200, 5600, 8200, 5600}, G4_C[5] = {5.6e-9, 10e-9, 22e-9, 47e-9, 68e-9};
        auto lpk = [&](double R, double C) { const double Rt = R * 10000 / (R + 10000); return 1 - std::exp(-1 / (Rt * C * fs)); };
        for (int i = 0; i < 5; i++) { g8k[i] = lpk(G8_R[i], G8_C[i]); g8a[i] = 10000 / (G8_R[i] + 10000); g4k[i] = lpk(G4_R[i], G4_C[i]); g4a[i] = 10000 / (G4_R[i] + 10000); }
        auto hp1 = [&](double R, double C) { return std::exp(-1 / (R * C * fs)); };
        hpVaLo = hp1(22000, 180e-9); hpVaHi = hp1(22000, 270e-9); hpVn = hp1(22000, 47e-9);
        shVnLo = shelf(10000, 4.7e-9, 1580); shVnHi = shelf(18000, 1e-9, 1580);
        fTr = bp(1350, 1.3); fHo = bp(680, 1.1);
        lpPre = lp2(1 / (2 * kPi * std::sqrt(22000.0 * 23000 * 1.5e-9 * 220e-12)), 1.305);
        bassF = lp2(1 / (2 * kPi * std::sqrt(47000.0 * 47000 * 47e-9 * 5.6e-9)), 1.45);
        bufL.assign(16384, 0); bufR.assign(16384, 0);
        post1 = lp2(11400, .707); post2 = lp2(1 / (2 * kPi * std::sqrt(22000.0 * 23000 * 2.7e-9 * 560e-12)), 1.1);
        static const double LF[7] = {1.33, .234, 2.84, 4.18, 1.30, 41.8, .482};
        for (int i = 0; i < 7; i++) lfk[i] = 1 - std::exp(-2 * kPi * LF[i] / fs);
        corr1 = lp2(1 / (2 * kPi * std::sqrt(22000.0 * 23000 * 27e-9 * 1.5e-9)), 2.12);
        corr2 = lp2(1 / (2 * kPi * std::sqrt(22000.0 * 23000 * 12e-9 * 6.8e-9)), .664);
        smk = 1 - std::exp(-1 / (.01 * fs));
        dlN = 1 << (int) std::ceil(std::log2(1.6 * fs)); dlL.assign((size_t) dlN, 0); dlR.assign((size_t) dlN, 0);
        const double sc = fs / 44100;
        for (int j = 0; j < 8; j++) { rvMax[j] = (int) std::ceil(RV_LEN[j] * 2 * sc) + 2; rvBuf[j].assign((size_t) rvMax[j], 0); }
        preN = 1 << (int) std::ceil(std::log2(.21 * fs)); preBuf.assign((size_t) preN, 0);
        update();
    }

    Params p;
    double bpm = 120;

    void set(int id, double v) { if (id >= 0 && id < pid::count) { p[(size_t) id] = v; update(); } }
    void setAll(const Params& o) { p = o; update(); }

    void update() {
        for (int c = 0; c < 6; c++) {
            const int b = CH_BASE[c];
            atkK[c] = 1 - std::exp(-1 / (820 * 15e-6 * std::pow(2.0, p[b + oAtk] * 8) * fs));
            const double tr = (P(pid::sus) != 0 ? 15e-6 * (5600 + P(pid::susLen) * 10000) : .012) * std::pow(2.0, (p[b + oDec] - .5) * 4);
            relK[c] = std::exp(-1 / (tr * fs));
        }
        const double tauC = (5600 + 25000 * (std::pow(10.0, 2 * P(pid::cresc)) - 1) / 99) * 22e-6; crK = 1 - std::exp(-1 / (tauC * fs));
        bool solo = false; for (int c = 0; c < 6; c++) if (p[CH_BASE[c] + oSol] != 0) solo = true;
        static const double CAL[6] = {4.26, 1.93, 1.59, .89, 2.24, 1.93};
        for (int c = 0; c < 6; c++) {
            const int b = CH_BASE[c];
            const bool o = P(CH_TAB[c]) != 0 && p[b + oMut] == 0 && (!solo || p[b + oSol] != 0);
            on[c] = o ? 1 : 0;
            const double l = p[b + oLvl] / .8; gain[c] = o ? l * l * CAL[c] : 0;
            const double th = (p[b + oPan] + 1) * kPi / 4; panL[c] = std::cos(th) * kSqrt2; panR[c] = std::sin(th) * kSqrt2;
            const double fc = 150 * std::pow(2.0, p[b + oTon] * 7.1); tk[c] = p[b + oTon] >= 1 ? 1 : 1 - std::exp(-2 * kPi * fc / fs);
        }
        bassVolG = P(pid::bassVol) * 1.6;
        fMul = std::pow(2.0, (P(pid::tune) / 100 + bendSt) / 12);
        if (P(pid::rvSize) != rvCfg[0] || P(pid::rvDecay) != rvCfg[1] || P(pid::rvDamp) != rvCfg[2]) {
            rvCfg = {P(pid::rvSize), P(pid::rvDecay), P(pid::rvDamp)};
            const double sc = fs / 44100 * (.5 + 1.5 * P(pid::rvSize));
            for (int i = 0; i < 8; i++) {
                rvLen[i] = std::min(rvMax[i] - 1, std::max(8, (int) std::floor(RV_LEN[i] * sc + .5)));
                rvG[i] = std::pow(10.0, -3 * rvLen[i] / (P(pid::rvDecay) * fs));
            }
            rvDk = 1 - std::exp(-2 * kPi * (18000 * std::pow(.06, P(pid::rvDamp))) / fs);
        }
        if (P(pid::eqLow) != eqCfg[0] || P(pid::eqMid) != eqCfg[1] || P(pid::eqHigh) != eqCfg[2]) {
            eqCfg = {P(pid::eqLow), P(pid::eqMid), P(pid::eqHigh)};
            eq[0] = peq(0, 150, P(pid::eqLow), .7); eq[1] = peq(1, 1000, P(pid::eqMid), .7); eq[2] = peq(2, 6000, P(pid::eqHigh), .7);
            eqFlat = P(pid::eqLow) == 0 && P(pid::eqMid) == 0 && P(pid::eqHigh) == 0;
        }
        dlK = 1 - std::exp(-2 * kPi * (600 * std::pow(25.0, P(pid::dlTone))) / fs);
        driveK = 1 + 9 * P(pid::drive);
        const double ml = P(pid::master) / .8, vl = P(pid::volume) / .8; outG = ml * ml * vl * vl;
    }

    void noteOn(int n, double v) {
        if (n < 0 || n > 127) return;
        if (v <= 0) { noteOff(n); return; }
        Key& k = keys[n];
        if (!k.active) { k = Key{}; k.active = true; order.push_back(n); }
        k.down = true; k.held = true; k.v = 1 - P(pid::velSens) * (1 - v); idle = 0;
    }
    void noteOff(int n) { if (n < 0 || n > 127 || !keys[n].active) return; keys[n].held = false; if (!sustainPedal) keys[n].down = false; }
    void pedal(bool o) { sustainPedal = o; if (!o) for (int n : order) if (!keys[n].held) keys[n].down = false; }
    void bend(double v) { bendSt = v * P(pid::bendRange); update(); }
    void modWheel(double v) { mod = v; }
    void expression(double v) { expr = v; }
    void allOff() { for (int n : order) { keys[n].down = false; keys[n].held = false; } sustainPedal = false; }
    bool anyKeyDown() const { for (int n : order) if (keys[n].down) return true; return false; }

    double wave(int m) {
        if (m < 0 || m > 150) return 0;
        if (ws[m] == (double) nS) return wc[m];
        const int pc = m % 12, oct = (int) std::floor(m / 12.0) - 1, kk = 8 - oct;
        const double div = kk >= 0 ? (double) (1 << kk) : std::pow(2.0, kk);
        const double ph = std::fmod(cyc[pc] / div, 1.0), dt = fTop[pc] * fMul / div / fs;
        double y = 0;
        if (dt < .5) {
            const double r = shR[pc], a = shA[pc];
            y = ph < .5 ? std::exp(-ph / r) : 0;
            y += .5 * blep(ph, dt); y -= .5 * a * blep(std::fmod(ph + .5, 1.0), dt);
            y -= .3;
        }
        ws[m] = (double) nS; wc[m] = y; return y;
    }
    double spike(int m, double tau) {
        if (m < 0 || m > 150) return 0;
        const int pc = m % 12, oct = (int) std::floor(m / 12.0) - 1, kk = 8 - oct;
        const double div = kk >= 0 ? (double) (1 << kk) : std::pow(2.0, kk);
        const double ph = std::fmod(cyc[pc] / div, 1.0), f = fTop[pc] * fMul / div, dt = f / fs;
        if (dt >= .5) return 0;
        const double x = ph / (f * tau); return std::exp(-x) + blep(ph, dt) * .5 - f * tau;
    }
    static double blep(double t, double dt) {
        if (t < dt) { const double x = t / dt; return x + x - x * x - 1; }
        if (t > 1 - dt) { const double x = (t - 1) / dt; return x * x + x + x + 1; }
        return 0;
    }

    // renders n samples into L, R starting at offset o
    template <typename T> void process(T* L, T* R, int o, int n) {
        if (order.empty() && bassG[0] < 1e-5 && bassG[1] < 1e-5 && idle > fs * 12) {
            for (int i = 0; i < n; i++) { L[o + i] = 0; R[o + i] = 0; } cresc = 0; return;
        }
        const double step = fMul / fs;
        if (!smInit) { smInit = true; for (int c = 0; c < 6; c++) { sm[c * 3] = gain[c]; sm[c * 3 + 1] = panL[c]; sm[c * 3 + 2] = panR[c]; } }
        int pit[6]; double ensOn[6], ech[6], rev[6];
        for (int c = 0; c < 6; c++) { const int b = CH_BASE[c]; pit[c] = (int) p[b + oPit]; ensOn[c] = p[b + oEns]; ech[c] = p[b + oEch]; rev[c] = p[b + oRev]; }
        const double depth = std::min(1.0, P(pid::depth) + (P(pid::modDest) == 0 ? mod * .65 : 0)), tremA = std::min(1.0, P(pid::trem) + (P(pid::modDest) == 1 ? mod : 0));
        const double phRate = P(pid::phRate) * (P(pid::modDest) == 2 ? 1 + mod * 6 : 1);
        static const double SYNC_BEATS[7] = {0, 1, .5, .75, .25, 2.0 / 3, 2};
        double dlMs = P(pid::dlTime); const int sy = (int) P(pid::dlSync); if (sy > 0) dlMs = std::min(1500.0, SYNC_BEATS[sy] * 60000 / bpm);
        const double dlTarget = dlMs * .001 * fs; if (dlT < 0) dlT = dlTarget;
        const int preD = std::max(1, (int) std::floor(P(pid::rvPre) * .001 * fs + .5));
        bool alive = false;
        for (int i = 0; i < n; i++) {
            nS++;
            for (int pc = 0; pc < 12; pc++) { cyc[pc] += fTop[pc] * step; if (cyc[pc] >= 1048576) cyc[pc] -= 1048576; }
            bus.fill(0);
            bool any = false; int low = 999;
            for (size_t oi = 0; oi < order.size(); oi++) {
                const int nt = order[oi]; Key& k = keys[nt];
                bool live = false;
                for (int c = 0; c < 4; c++) { k.g[c] = k.down ? k.g[c] + (1 - k.g[c]) * atkK[c] : k.g[c] * relK[c]; if (k.g[c] > 1e-4) live = true; }
                if (!live && !k.down) { k.active = false; order.erase(order.begin() + (long) oi); oi--; continue; }
                if (k.down && nt < 48 && nt < low) low = nt;
                const int gr = groupOf(nt); const double v = k.v;
                if (on[0] && k.g[0] > 1e-5) bus[0 * 10 + 5 + gr] += k.g[0] * v * wave(nt + pit[0] + 12);
                if (on[1] && k.g[1] > 1e-5) bus[1 * 10 + gr] += k.g[1] * v * wave(nt + pit[1]);
                if (on[2] && k.g[2] > 1e-5) { const int m = nt + pit[2]; bus[2 * 10 + gr] += k.g[2] * v * wave(m); bus[2 * 10 + 5 + gr] += k.g[2] * v * wave(m + 12); }
                if (on[3] && k.g[3] > 1e-5) { const int m = nt + pit[3]; bus[3 * 10 + gr] += k.g[3] * v * wave(m); bus[3 * 10 + 5 + gr] += k.g[3] * v * wave(m + 12); }
                if (k.g[0] > .01 || k.g[1] > .01 || k.g[2] > .01 || k.g[3] > .01) any = true;
            }
            for (int c = 0; c < 4; c++) {
                if (!on[c]) continue;
                for (int g = 0; g < 5; g++) {
                    const int i8 = c * 10 + g, i4 = c * 10 + 5 + g;
                    bz[i8] += (bus[i8] * g8a[g] - bz[i8]) * g8k[g];
                    bz[i4] += (bus[i4] * g4a[g] - bz[i4]) * g4k[g];
                }
            }
            double va = 0, vn = 0;
            if (on[1]) { const int b = 10; const double a = bz[b + 3] + bz[b + 4], c3 = bz[b] + bz[b + 1] + bz[b + 2];
                const double ya = hpVaLo * (hz[0] + a - hz[1]); hz[0] = ya; hz[1] = a;
                const double yb = hpVaHi * (hz[2] + c3 - hz[3]); hz[2] = yb; hz[3] = c3;
                va = (ya + yb) * .5 * .19; }
            if (on[0]) { const int b = 5; const double a = bz[b + 3] + bz[b + 4], c3 = bz[b] + bz[b + 1] + bz[b + 2];
                const double ya = hpVn * (hz[8] + a - hz[9]); hz[8] = ya; hz[9] = a;
                const double yb = hpVn * (hz[10] + c3 - hz[11]); hz[10] = yb; hz[11] = c3;
                const double za = shVnLo[0] * ya + hz[12]; hz[12] = shVnLo[1] * ya - shVnLo[2] * za;
                const double zb = shVnHi[0] * yb + hz[13]; hz[13] = shVnHi[1] * yb - shVnHi[2] * zb;
                vn = (za + zb) * .5; }
            double tp = 0, hn = 0;
            if (on[2]) { double s = 0; for (int g = 0; g < 10; g++) s += bz[20 + g]; tp = bq(fTr, fz, 0, s * .5) * 2.2 * .19; }
            if (on[3]) { double s = 0; for (int g = 0; g < 10; g++) s += bz[30 + g]; hn = bq(fHo, fz, 2, s * .5) * 2.4 * .19; }
            if (any) cresc += (1 - cresc) * crK; else cresc *= .999;
            const double cr = cresc * .45 * 2.2;
            chs[0] = vn * cr; chs[1] = va * cr; chs[2] = tp * cr; chs[3] = hn * cr;
            if (low < 999) bassNote = low;
            double bc = 0, bb = 0;
            for (int c = 0; c < 2; c++) bassG[c] = low < 999 ? bassG[c] + (1 - bassG[c]) * atkK[4 + c] : bassG[c] * relK[4 + c];
            if (bassNote >= 0 && (bassG[0] > 1e-5 || bassG[1] > 1e-5)) {
                if (on[4]) bc = spike(bassNote + pit[4], 5.8e-9 * 50000) * bassG[0];
                if (on[5]) bb = spike(bassNote + pit[5] - 12, 10e-9 * 50000) * bassG[1];
                alive = true;
            }
            bc = bq(bassF, bassZ, 0, bc) * bassVolG * .45; bb = bq(bassF, bassZ, 2, bb) * bassVolG * .45;
            chs[4] = bc; chs[5] = bb;
            double eL = 0, eR = 0, dL = 0, dR = 0, sE = 0, sR = 0;
            for (int c = 0; c < 6; c++) {
                const int j = c * 3; sm[j] += (gain[c] - sm[j]) * smk; sm[j + 1] += (panL[c] - sm[j + 1]) * smk; sm[j + 2] += (panR[c] - sm[j + 2]) * smk;
                double x = chs[c];
                if (tk[c] < 1) { tz[c] += (x - tz[c]) * tk[c]; x = tz[c]; }
                x *= sm[j];
                if (ensOn[c] != 0) { eL += x * sm[j + 1]; eR += x * sm[j + 2]; } else { dL += x * sm[j + 1]; dR += x * sm[j + 2]; }
                sE += x * ech[c]; sR += x * rev[c];
            }
            if (any || !order.empty()) alive = true;
            eL = bq(lpPre, preZ, 0, eL); eR = bq(lpPre, preZ, 2, eR); dL = bq(lpPre, preZ, 4, dL); dR = bq(lpPre, preZ, 6, dR);
            phC += P(pid::chorusHz) / fs; if (phC >= .5) { phC -= .5; sC = -sC; }
            phT += P(pid::tremHz) / fs; if (phT >= .5) { phT -= .5; sT = -sT; }
            lf[0] += (sC * .453 - lf[0]) * lfk[0]; lf[1] += (lf[0] - lf[1]) * lfk[1]; lf[2] += (lf[1] - lf[2]) * lfk[2];
            lf[3] += (sT * .548 - lf[3]) * lfk[3]; lf[4] += (lf[3] - lf[4]) * lfk[4]; lf[5] += (lf[4] - lf[5]) * lfk[5];
            const double chv = lf[2] * 3.4, trv = lf[5] * 8.8 * tremA;
            const double c1 = chv + trv; lf[6] += (c1 - lf[6]) * lfk[6]; const double c2 = -1.83 * lf[6], c3 = -(c1 + c2);
            double oL, oR;
            const int wi = (int) (w & 16383); bufL[(size_t) wi] = eL; bufR[(size_t) wi] = eR;
            if (P(pid::ens) != 0) {
                const double cv[3] = {c1, c2, c3}; double yl[3], yr[3];
                for (int j = 0; j < 3; j++) {
                    const double fclk = 33000 * (1 + depth * std::max(-.9, std::min(.9, cv[j]))), d = 185 / (2 * fclk) * fs;
                    const double r = (double) w - d, fi = std::floor(r), f = r - fi; const int64_t i0 = (int64_t) fi;
                    const double a1 = bufL[(size_t) (i0 & 16383)], b1 = bufL[(size_t) ((i0 + 1) & 16383)], a2 = bufR[(size_t) (i0 & 16383)], b2 = bufR[(size_t) ((i0 + 1) & 16383)];
                    yl[j] = bq(post2, postZ, j * 8 + 2, bq(post1, postZ, j * 8, a1 + (b1 - a1) * f));
                    yr[j] = bq(post2, postZ, j * 8 + 6, bq(post1, postZ, j * 8 + 4, a2 + (b2 - a2) * f));
                }
                if (P(pid::wide) != 0) { oL = (yl[0] + yl[1] * .5) * .8; oR = (yr[2] + yr[1] * .5) * .8; }
                else { oL = (yl[0] + yl[1] + yl[2]) * .5; oR = (yr[0] + yr[1] + yr[2]) * .5; }
            } else { oL = eL; oR = eR; }
            w++;
            oL += dL; oR += dR;
            oL *= expr; oR *= expr;
            if (P(pid::outSel) != 0) { oL = bq(corr2, corrZ, 2, bq(corr1, corrZ, 0, oL)) * 1.4; oR = bq(corr2, corrZ, 6, bq(corr1, corrZ, 4, oR)) * 1.4; }
            if (P(pid::phOn) != 0) {
                phPh += phRate / fs; if (phPh >= 1) phPh -= 1;
                const double pd = P(pid::phDepth), pfb = P(pid::phFb), pmix = P(pid::phMix);
                for (int s = 0; s < 2; s++) {
                    double t = std::fmod(phPh + s * .25, 1.0); t = t < .5 ? t * 2 : 2 - t * 2;
                    const double f = 180 * std::pow(4500.0 / 180, t * pd + (1 - pd) * .3), tn = std::tan(kPi * std::min(f, fs * .45) / fs), a = (tn - 1) / (tn + 1);
                    const double xin = s ? oR : oL; double x = xin + phLast[s] * pfb;
                    for (int st = 0; st < 4; st++) { const int zi = s * 4 + st; const double y = a * x + phz[zi]; phz[zi] = x - a * y; x = y; }
                    phLast[s] = x;
                    const double y = xin * (1 - pmix) + x * pmix;
                    if (s) oR = y; else oL = y;
                }
            }
            double eRL = 0, eRR = 0;
            if (P(pid::dlOn) != 0) {
                dlT += (dlTarget - dlT) * .0003;
                const int64_t M = dlN - 1; const double r = (double) dlW - dlT, fi = std::floor(r), f = r - fi; const int64_t i0 = (int64_t) fi;
                const double xl = dlL[(size_t) (i0 & M)], xl2 = dlL[(size_t) ((i0 + 1) & M)], xr = dlR[(size_t) (i0 & M)], xr2 = dlR[(size_t) ((i0 + 1) & M)];
                double yl = xl + (xl2 - xl) * f, yr = xr + (xr2 - xr) * f;
                dlZ[0] += (yl - dlZ[0]) * dlK; dlZ[1] += (yr - dlZ[1]) * dlK; yl = dlZ[0]; yr = dlZ[1];
                const size_t wi2 = (size_t) (dlW & M); const double fb = P(pid::dlFb);
                if (P(pid::dlPing) != 0) { dlL[wi2] = std::tanh(sE + yr * fb); dlR[wi2] = std::tanh(yl * fb); }
                else { dlL[wi2] = std::tanh(sE + yl * fb); dlR[wi2] = std::tanh(sE + yr * fb); }
                dlW++;
                eRL = yl * P(pid::dlRet); eRR = yr * P(pid::dlRet);
                if (std::abs(yl) + std::abs(yr) > 1e-6) alive = true;
            }
            double rRL = 0, rRR = 0;
            if (P(pid::rvOn) != 0) {
                const int64_t PM = preN - 1; preBuf[(size_t) (preW & PM)] = sR + (eRL + eRR) * .25;
                const double rin = preBuf[(size_t) ((preW - preD) & PM)]; preW++;
                double o8[8], h[8];
                for (int j = 0; j < 8; j++) { auto& b = rvBuf[j]; int ri = rvW[j] - rvLen[j]; o8[j] = b[(size_t) (ri < 0 ? ri + (int) b.size() : ri)]; rvLp[j] += (o8[j] - rvLp[j]) * rvDk; }
                for (int j = 0; j < 8; j++) h[j] = rvLp[j];
                for (int len = 1; len < 8; len <<= 1) for (int a = 0; a < 8; a += len << 1) for (int b = a; b < a + len; b++) { const double u = h[b], v = h[b + len]; h[b] = u + v; h[b + len] = u - v; }
                for (int j = 0; j < 8; j++) { auto& b = rvBuf[j]; b[(size_t) rvW[j]] = h[j] * .35355339059327373 * rvG[j] + rin * (j & 1 ? -.5 : .5); rvW[j] = rvW[j] + 1 >= (int) b.size() ? 0 : rvW[j] + 1; }
                rRL = (o8[0] + o8[2] + o8[4] + o8[6]) * .5 * P(pid::rvRet); rRR = (o8[1] + o8[3] + o8[5] + o8[7]) * .5 * P(pid::rvRet);
                if (std::abs(rRL) + std::abs(rRR) > 1e-6) alive = true;
            }
            oL += eRL + rRL; oR += eRR + rRR;
            if (!eqFlat) for (int b = 0; b < 3; b++) { oL = bq(eq[b], eqZ, b * 4, oL); oR = bq(eq[b], eqZ, b * 4 + 2, oR); }
            if (P(pid::drive) > 0) { const double k = driveK, m = (1 + P(pid::drive) * 2) / k; oL = std::tanh(oL * k) * m; oR = std::tanh(oR * k) * m; }
            L[o + i] = (T) (oL * outG); R[o + i] = (T) (oR * outG);
        }
        if (alive) idle = 0; else idle += n;
    }

private:
    static constexpr double RV_LEN[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static int groupOf(int n) { return n >= 75 ? 0 : n >= 65 ? 1 : n >= 54 ? 2 : n >= 42 ? 3 : 4; }
    double P(int id) const { return p[(size_t) id]; }
    Bq lp2(double f, double Q) const { const double w0 = 2 * kPi * std::min(f, fs * .45), K = w0 / std::tan(w0 / (2 * fs)), d = K * K + w0 / Q * K + w0 * w0;
        return {w0 * w0 / d, 2 * w0 * w0 / d, w0 * w0 / d, (2 * w0 * w0 - 2 * K * K) / d, (K * K - w0 / Q * K + w0 * w0) / d}; }
    Bq bp(double f, double Q) const { const double w0 = 2 * kPi * f / fs, al = std::sin(w0) / (2 * Q), a0 = 1 + al; return {al / a0, 0, -al / a0, -2 * std::cos(w0) / a0, (1 - al) / a0}; }
    std::array<double, 3> shelf(double R, double C, double Rl) const { const double K = 2 * fs, t = R * C, b0 = Rl + Rl * t * K, b1 = Rl - Rl * t * K, a0 = R + Rl + t * Rl * K, a1 = R + Rl - t * Rl * K;
        return {b0 / a0, b1 / a0, a1 / a0}; }
    Bq peq(int type, double f, double gdb, double Q) const {
        const double A = std::pow(10.0, gdb / 40), w0 = 2 * kPi * f / fs, c = std::cos(w0), s = std::sin(w0);
        double b0, b1, b2, a0, a1, a2;
        if (type == 0) { const double al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
            b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
            a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa; }
        else if (type == 2) { const double al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
            b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
            a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa; }
        else { const double al = s / (2 * Q); b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }
    template <size_t N> static double bq(const Bq& q, std::array<double, N>& z, int o, double x) {
        const double y = q[0] * x + z[(size_t) o]; z[(size_t) o] = q[1] * x - q[3] * y + z[(size_t) o + 1]; z[(size_t) o + 1] = q[2] * x - q[4] * y; return y;
    }

    struct Key { bool active = false, down = false, held = false; double v = 1; double g[4] = {0, 0, 0, 0}; };
    double fs;
    std::array<Key, 128> keys{}; std::vector<int> order;
    bool sustainPedal = false; double bendSt = 0, mod = 0, expr = 1;
    double cyc[12] = {}, fTop[12], shR[12], shA[12];
    std::array<double, 160> wc{}, ws{}; int64_t nS = 0;
    double cresc = 0;
    double g8k[5], g8a[5], g4k[5], g4a[5];
    std::array<double, 40> bus{}, bz{};
    double hpVaLo, hpVaHi, hpVn; std::array<double, 3> shVnLo, shVnHi; double hz[32] = {};
    Bq fTr, fHo; std::array<double, 4> fz{};
    Bq lpPre; std::array<double, 8> preZ{};
    Bq bassF; std::array<double, 4> bassZ{}; double bassG[2] = {0, 0}; int bassNote = -1;
    std::vector<double> bufL, bufR; int64_t w = 0;
    Bq post1, post2; std::array<double, 24> postZ{};
    double phC = 0, phT = 0, sC = 1, sT = 1, lf[7] = {}, lfk[7];
    Bq corr1, corr2; std::array<double, 8> corrZ{};
    double tz[6] = {}, sm[18] = {}; bool smInit = false; double smk;
    double phz[8] = {}, phLast[2] = {0, 0}, phPh = 0;
    int64_t dlN; std::vector<double> dlL, dlR; int64_t dlW = 0; double dlT = -1, dlZ[2] = {0, 0};
    int rvMax[8]; std::vector<double> rvBuf[8]; int rvW[8] = {}; double rvLp[8] = {};
    int64_t preN; std::vector<double> preBuf; int64_t preW = 0;
    std::array<double, 3> rvCfg{{-1, -1, -1}}; int rvLen[8]; double rvG[8], rvDk = 0;
    std::array<double, 3> eqCfg{{-99, -99, -99}}; Bq eq[3]; bool eqFlat = true; std::array<double, 12> eqZ{};
    double chs[6] = {};
    double atkK[6], relK[6], crK, gain[6], panL[6], panR[6], tk[6], bassVolG, fMul = 1, dlK, driveK, outG;
    int on[6] = {};
    int64_t idle = 0;
};

} // namespace solina
