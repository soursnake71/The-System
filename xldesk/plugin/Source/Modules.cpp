// The SSL 500 modules, ported from xldesk/dsp/console.js. Each class quotes its sources there; numbers match.
#include "Modules.h"

namespace xl {
thread_local Rng rng;

const float (&TruePeak::table())[PH][TAPS]
{
    static float t[PH][TAPS];
    static bool done = false;
    if (! done)
    {
        for (int p = 0; p < PH; ++p)
        {
            double s = 0, row[TAPS];
            for (int k = 0; k < TAPS; ++k)
            {
                const double x = k - (TAPS / 2 - 1) - (double) p / PH, w = .5 + .5 * std::cos (PI * x / (TAPS / 2));
                row[k] = (std::abs (x) < 1e-9 ? 1.0 : std::sin (PI * x) / (PI * x)) * w; s += row[k];
            }
            for (int k = 0; k < TAPS; ++k) t[p][k] = (float) (row[k] / s);
        }
        done = true;
    }
    return t;
}
float TruePeak::run (const float* b, int N)
{
    const auto& H = table();
    float pk = 0; const int c0 = TAPS / 2 - 1;
    for (int n = 0; n < N; ++n)
    {
        std::copy (z + 1, z + TAPS, z); z[TAPS - 1] = b[n];
        const float a = std::abs (b[n]); if (a > pk) pk = a;
        if (std::max (std::abs (z[c0]), std::abs (z[c0 + 1])) < pk * .7f) continue;
        for (int p = 1; p < PH; ++p) { float y = 0; for (int k = 0; k < TAPS; ++k) y += H[p][k] * z[k]; y = std::abs (y); if (y > pk) pk = y; }
    }
    return pk;
}

/* ---------- SSL VHD Pre ---------- */
struct VHDPre : Module
{
    double fs = 48000; VHD amp; BQ h1, h2; double hk = 0; int prevP48 = -1;
    int iGain, iDrive, iVhd, iTrim, iPhase, iPad, iHiz, iHpfIn, iHpf, iP48;
    float gain = 1, trim = 1, padHiz = 1, drive = .5f; bool vhd = false, hpfIn = false;
    VHDPre() { hasPeak = true; pre = true; }
    void prepare (double f) override { fs = f; amp.prepare (f); }
    void bind (const Controls& c) override { iGain = c.index ("gain"); iDrive = c.index ("drive"); iVhd = c.index ("vhd"); iTrim = c.index ("trim"); iPhase = c.index ("phase"); iPad = c.index ("pad"); iHiz = c.index ("hiz"); iHpfIn = c.index ("hpfIn"); iHpf = c.index ("hpf"); iP48 = c.index ("p48"); }
    void update (const Controls& g) override
    {
        gain = (float) dB (20 + 55 * g (iGain)); vhd = g (iVhd) > .5f; drive = g (iDrive);
        trim = (float) dB ((g (iTrim) - .5) * 40) * (g (iPhase) > .5f ? -1.f : 1.f);
        padHiz = (g (iPad) > .5f ? .1f : 1.f) * (g (iHiz) > .5f ? 10000.f / 10150.f : 1200.f / 1350.f); hpfIn = g (iHpfIn) > .5f;
        const double f = 15 * std::pow (500.0 / 15, g (iHpf));
        if (std::abs (f - hk) > 1e-9) { hk = f; h1.hp1 (f, fs); h2.hp (f, 1, fs); }   // 18 dB/oct: 1st order + 2nd order Q 1
        const int p48 = g (iP48) > .5f; if (prevP48 >= 0 && p48 != prevP48) amp.thump(); prevP48 = p48;
    }
    void run (float* b, int N) override
    {
        for (int i = 0; i < N; ++i) b[i] *= padHiz;
        amp.run (b, N, gain, vhd, drive, noise);
        float p = pk;
        for (int i = 0; i < N; ++i) { float x = b[i] * trim; if (hpfIn) x = h2.run (h1.run (x)); x = rail (x); b[i] = x; p = std::max (p, std::abs (x)); }
        pk = p;
    }
};

/* ---------- SSL E Series EQ ---------- */
struct EEQ : Module
{
    double fs = 48000; BQ f[4]; double k[12] {}; bool on = true, first = true;
    int iHfG, iHfF, iHfBell, iHmfG, iHmfF, iHmfQ, iBlk, iIn, iLmfG, iLmfF, iLmfQ, iLfF, iLfG, iLfBell;
    void prepare (double f_) override { fs = f_; }
    void bind (const Controls& c) override
    { iHfG = c.index ("hfG"); iHfF = c.index ("hfF"); iHfBell = c.index ("hfBell"); iHmfG = c.index ("hmfG"); iHmfF = c.index ("hmfF"); iHmfQ = c.index ("hmfQ"); iBlk = c.index ("blk"); iIn = c.index ("in");
      iLmfG = c.index ("lmfG"); iLmfF = c.index ("lmfF"); iLmfQ = c.index ("lmfQ"); iLfF = c.index ("lfF"); iLfG = c.index ("lfG"); iLfBell = c.index ("lfBell"); }
    void update (const Controls& g) override
    {
        const bool blk = g (iBlk) > .5f; const double rng_ = blk ? 18 : 15;
        auto gn = [&] (int j) { return (g (j) - .5) * 2 * rng_; };
        const double hfG = gn (iHfG), hmfG = gn (iHmfG), lmfG = gn (iLmfG), lfG = gn (iLfG);
        const double hfF = 1500 * std::pow (16000.0 / 1500, g (iHfF)), hmfF = 600 * std::pow (7000.0 / 600, g (iHmfF)), lmfF = 200 * std::pow (2500.0 / 200, g (iLmfF));
        const double lfF = 30 * std::pow (450.0 / 30, blk ? std::pow ((double) g (iLfF), 1.4) : g (iLfF));
        double hq = 2.5 * std::pow (.2, g (iHmfQ)), lq = 2.5 * std::pow (.2, g (iLmfQ));
        if (blk) { hq *= .6 + .8 * std::abs (hmfG) / 18; lq *= .6 + .8 * std::abs (lmfG) / 18; }
        const double v[12] = { hfG, hmfG, lmfG, lfG, hfF, hmfF, lmfF, lfF, hq, lq, g (iHfBell), g (iLfBell) };
        on = g (iIn) > .5f;
        if (! first && std::equal (v, v + 12, k)) return;
        first = false; std::copy (v, v + 12, k);
        const double BELLQ = .7;
        if (g (iHfBell) > .5f) f[0].peak (std::min (hfF, fs * .45), BELLQ, hfG, fs); else f[0].hshelf1 (std::min (hfF, fs * .45), hfG, fs);
        f[1].peak (std::min (hmfF, fs * .45), hq, hmfG, fs); f[2].peak (lmfF, lq, lmfG, fs);
        if (g (iLfBell) > .5f) f[3].peak (lfF, BELLQ, lfG, fs); else f[3].lshelf1 (lfF, lfG, fs);
    }
    void run (float* b, int N) override { if (! on) return; for (int i = 0; i < N; ++i) b[i] = rail (f[3].run (f[2].run (f[1].run (f[0].run (b[i]))))); }
};

/* ---------- SSL E Series Dynamics ---------- */
struct EDyn : Module
{
    double fs = 48000, ms = 0, gr = 0, att = 0, pkE = 0, msA = 0, pkR = 0;
    double R = 1, T = 4, crel = .1, catt = 0, range = 0, GT = 0, grel = .1, gatt = 0, hyst = 2, slope = 0, mk = 0; bool hard = false, lin = false, on = true, expd = false;
    int iRatio, iHard, iCthr, iLin, iCrel, iCfast, iIn, iRange, iGthr, iExp, iGrel, iGfast;
    EDyn() { hasGr = true; }
    void prepare (double f) override { fs = f; msA = 1 - std::exp (-1 / (.008 * fs)); pkR = std::exp (-1 / (.01 * fs)); }
    void bind (const Controls& c) override { iRatio = c.index ("ratio"); iHard = c.index ("hard"); iCthr = c.index ("cthr"); iLin = c.index ("linrel"); iCrel = c.index ("crel"); iCfast = c.index ("cfast"); iIn = c.index ("in"); iRange = c.index ("range"); iGthr = c.index ("gthr"); iExp = c.index ("exp"); iGrel = c.index ("grel"); iGfast = c.index ("gfast"); }
    void update (const Controls& g) override
    {
        const double p = g (iRatio); R = p >= .999 ? xlspec::INF : 1 / (1 - p); T = 4 - 30 * g (iCthr); hard = g (iHard) > .5f; lin = g (iLin) > .5f;
        crel = .1 * std::pow (40.0, g (iCrel)); catt = (g (iCfast) > .5f ? 20 / .003 : 20 / .03) / fs; on = g (iIn) > .5f;
        range = 40 * g (iRange); GT = -30 + 40 * g (iGthr); expd = g (iExp) > .5f; grel = .1 * std::pow (40.0, g (iGrel)); gatt = (g (iGfast) > .5f ? 40 / .0001 : 40 / .0015) / fs;
        hyst = 2 + (10 - GT) * .15; slope = std::isinf (R) ? 1 : 1 - 1 / R; mk = std::max (0.0, 4 - T) * slope;
    }
    void run (float* b, int N) override
    {
        if (! on) { grMax = gMax = 0; return; }
        const double Tt = T + (lin ? 6 : 0), W = hard ? 0 : 10; const bool compOn = R > 1.001, gateOn = range > .05;
        const double relC = 1 - std::exp (-1 / (crel * fs)), relLin = 20 / crel / fs, gRel = 1 - std::exp (-1 / (grel * fs));
        float grM = 0, gM = 0;
        for (int i = 0; i < N; ++i)
        {
            const double x = b[i]; double gdb = 0;
            if (compOn)
            {
                ms += (x * x - ms) * msA;
                const double L = 4.342944819 * std::log (2 * ms + 1e-24) + xlspec::REF, o = L - Tt;
                const double tgt = W > 0 && o > -W / 2 && o < W / 2 ? slope * (o + W / 2) * (o + W / 2) / (2 * W) : o > 0 ? slope * o : 0;
                if (tgt > gr) gr = std::min (tgt, gr + catt); else gr = lin ? std::max (tgt, gr - relLin) : gr + (tgt - gr) * relC;
                gdb = mk - gr; grM = std::max (grM, (float) gr);
            }
            if (gateOn)
            {
                const double a = std::abs (x); pkE = a > pkE ? a : pkE * pkR;
                const double L = toDbu (pkE), th = att < 1 ? GT - hyst : GT;
                const double tgt = L >= th ? 0 : std::min (range, (th - L) * (expd ? 1 : 19));
                if (tgt < att) att = std::max (tgt, att - gatt); else att += (tgt - att) * gRel;
                gdb -= att; gM = std::max (gM, (float) att);
            }
            b[i] = rail ((float) (x * std::exp (gdb * .1151292546)));
        }
        grMax = grM; gMax = gM;
    }
};

/* ---------- SSL G Series Bus Compressor ---------- */
struct GComp : Module
{
    double fs = 48000, gr = 0, grS = 0, attMs = 10, rel = -1, R = 4, hpf = 0, T = 0, mk = 0; bool on = true; double hk = -2;
    BQ hL[2], hR[2];
    int iThr, iMk, iAtt, iRel, iRatio, iHpf, iIn; const xlspec::ModuleDef* def = nullptr;
    GComp() { stereo = true; }
    void prepare (double f) override { fs = f; }
    void bind (const Controls& c) override { def = c.def; iThr = c.index ("thr"); iMk = c.index ("mk"); iAtt = c.index ("att"); iRel = c.index ("rel"); iRatio = c.index ("ratio"); iHpf = c.index ("hpf"); iIn = c.index ("in"); }
    void update (const Controls& g) override
    {
        attMs = def->params[iAtt].stepVals[(int) g (iAtt)]; rel = def->params[iRel].stepVals[(int) g (iRel)];   // -1 = AUTO
        R = def->params[iRatio].stepVals[(int) g (iRatio)]; hpf = def->params[iHpf].stepVals[(int) g (iHpf)];      // -1 = OFF
        T = -20 + 40 * g (iThr) - 8 * (1 - std::log10 (R)) + 4; mk = -5 + 20 * g (iMk); on = g (iIn) > .5f;
        if (std::abs (hpf - hk) > 1e-9) { hk = hpf; if (hpf > 0) for (auto* f : { &hL[0], &hL[1], &hR[0], &hR[1] }) f->hp (hpf, .7071, fs); }
    }
    void runStereo (float* L, float* Rr, int N) override
    {
        const double sl = 1 - 1 / R, aC = 1 - std::exp (-1 / (attMs / 1000 * fs)); const bool autoR = rel < 0;
        const double rC = 1 - std::exp (-1 / ((autoR ? .1 : rel) * fs)), sC = 1 - std::exp (-1 / (1.2 * fs)), sA = 1 - std::exp (-1 / (.3 * fs));
        const double mkG = dB (mk), W = 4, mC = 1 - std::exp (-1 / (.12 * fs)); const bool hp = hpf > 0;
        for (int i = 0; i < N; ++i)
        {
            float l = L[i], r = Rr[i];
            if (on)
            {
                if (hp) { l = hL[1].run (hL[0].run (l)); r = hR[1].run (hR[0].run (r)); }
                const double d = std::max (std::abs (l), std::abs (r)), o = toDbu (d) - T;
                const double tgt = o <= -W / 2 ? 0 : o < W / 2 ? sl * (o + W / 2) * (o + W / 2) / (2 * W) : sl * o;
                gr += (tgt - gr) * (tgt > gr ? aC : rC);
                if (autoR) grS += (tgt - grS) * (tgt > grS ? sA : sC);
            }
            else { gr *= 1 - rC; grS *= 1 - sC; }
            const double grd = autoR ? std::max (gr, grS) : gr, gn = std::exp (-grd * .1151292546) * mkG;
            L[i] = rail ((float) (L[i] * gn)); Rr[i] = rail ((float) (Rr[i] * gn));
            needle += (float) ((grd - needle) * mC);
        }
    }
};

static std::map<std::string, Factory> buildRegistry()
{
    std::map<std::string, Factory> r;
    // one line per module; a new revision is a new line with a new type id
    r["vhdpre"] = [] { return std::make_unique<VHDPre>(); };
    r["eeq"]    = [] { return std::make_unique<EEQ>(); };
    r["edyn"]   = [] { return std::make_unique<EDyn>(); };
    r["gcomp"]  = [] { return std::make_unique<GComp>(); };
    return r;
}
const std::map<std::string, Factory>& registry() { static const auto r = buildRegistry(); return r; }
const xlspec::ModuleDef* moduleDef (const std::string& type) { for (const auto& m : xlspec::MODULES) if (type == m.type) return &m; return nullptr; }
std::unique_ptr<Module> makeModule (const std::string& type, double fs)
{
    const auto& r = registry(); auto it = r.find (type);
    if (it == r.end()) return nullptr;
    auto m = it->second(); m->prepare (fs); return m;
}
} // namespace xl
