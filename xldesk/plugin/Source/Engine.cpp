// Port of the process() loop in xldesk/dsp/console.js. Comments there explain each stage and its source.
#include "Engine.h"
#include <cstring>
#include <string>

namespace xl {
static constexpr int BLK = 128;

int Engine::paramIndex (const std::string& id) { for (int i = 0; i < xlspec::NPARAMS; ++i) if (id == xlspec::PARAMS[i].id) return i; return -1; }
int Engine::whereIndex (const std::string& w) { for (int i = 0; i < xlspec::NWHERE; ++i) if (w == xlspec::WHERE[i].id) return i; return -1; }

Engine::Engine()
{
    auto I = [] (const std::string& id) { const int i = paramIndex (id); return i; };
    for (int i = 0; i < 16; ++i)
    {
        const std::string m = "m" + std::to_string (i + 1) + ".";
        auto& x = mi[i];
        if (i < 8) { x.gain = I (m + "gain"); x.drive = I (m + "drive"); x.vhd = I (m + "vhd"); x.hiz = I (m + "hiz"); x.p48 = I (m + "p48"); x.pad = I (m + "pad"); }
        x.trim = I (m + "trim"); x.daw = I (m + "daw"); x.phase = I (m + "phase"); x.s500 = I (m + "s500"); x.ins = I (m + "ins"); x.chop = I (m + "chop");
        x.cue = I (m + "cue"); x.cuepan = I (m + "cuepan"); x.cuepost = I (m + "cuepost"); x.alt = I (m + "alt"); x.aux1 = I (m + "aux1"); x.aux2 = I (m + "aux2"); x.pan = I (m + "pan");
        x.A = I (m + "A"); x.B = I (m + "B"); x.C = I (m + "C"); x.D = I (m + "D"); x.cut = I (m + "cut"); x.solo = I (m + "solo"); x.safe = I (m + "safe"); x.fader = I (m + "fader");
    }
    for (int k = 0; k < 4; ++k)
    {
        const std::string s = "s" + std::to_string (k + 1) + ".";
        auto& x = si[k];
        x.trim = I (s + "trim"); x.phL = I (s + "phL"); x.phR = I (s + "phR"); x.mono = I (s + "mono"); x.chop = I (s + "chop"); x.cue = I (s + "cue"); x.cuepan = I (s + "cuepan"); x.cuepost = I (s + "cuepost");
        x.aux1 = I (s + "aux1"); x.aux2 = I (s + "aux2"); x.pan = I (s + "pan"); x.A = I (s + "A"); x.B = I (s + "B"); x.C = I (s + "C"); x.D = I (s + "D"); x.cut = I (s + "cut"); x.solo = I (s + "solo"); x.safe = I (s + "safe"); x.fader = I (s + "fader");
    }
    for (int k = 0; k < 2; ++k)
    {
        const std::string r = "r" + std::to_string (k + 1) + ".";
        ri[k] = { I (r + "level"), I (r + "bal"), I (r + "mono"), I (r + "cue"), I (r + "A"), I (r + "B"), I (r + "C"), I (r + "D"), I (r + "afl") };
    }
    const char* X[4] = { "A", "B", "C", "D" };
    for (int b = 0; b < 4; ++b) { const std::string p = std::string (X[b]) + "."; bi[b] = { I (p + "s500"), I (p + "ins"), I (p + "sum"), b ? I (p + "toA") : -1, b ? I (p + "level") : -1, b ? I (p + "afl") : -1 }; }
    aComp = I ("A.comp"); aFader = I ("A.fader"); cueLevel = I ("cue.level"); cueAfl = I ("cue.afl"); cueToA = I ("cue.toA");
    for (int a = 0; a < 2; ++a) { const std::string p = "aux" + std::to_string (a + 1) + "."; auxLevel[a] = I (p + "level"); auxPre[a] = I (p + "pre"); auxAfl[a] = I (p + "afl"); }
    soloAfl = I ("solo.afl");
    monLevel = I ("mon.level"); monDim = I ("mon.dim"); monCut = I ("mon.cut"); monDimAmt = I ("mon.dimamt"); monAfl = I ("mon.afl"); monMono = I ("mon.mono"); monMini1 = I ("mon.mini1"); monMini2 = I ("mon.mini2");
    monSub_[0] = I ("mon.subMain"); monSub_[1] = I ("mon.subMini1"); monSub_[2] = I ("mon.subMini2"); monSrcA = I ("mon.srcA"); monSrcExt = I ("mon.srcExt"); monSrcIj = I ("mon.srcIj"); monOff1 = I ("mon.off1"); monOff2 = I ("mon.off2");
    hpLevel = I ("hp.level"); hpFbA = I ("hp.fbA"); hpFbB = I ("hp.fbB");
    for (int f = 0; f < 2; ++f) { const std::string p = f ? "fbB." : "fbA."; fbLevel[f] = I (p + "level"); fbMon[f] = I (p + "mon"); fbTalk[f] = I (p + "talk"); }
    talkLevel = I ("talk.level"); listenLevel = I ("listen.level"); listenOn = I ("listen.on");
    for (int d = 1; d <= 12; ++d) dip[d] = I ("dip." + std::to_string (d));
    noise = I ("sys.noise"); unlink1 = I ("rear.unlink1"); unlink2 = I ("rear.unlink2");
    for (int i = 1; i <= 16; ++i) slotExtP[i] = I ("slot" + std::to_string (i) + ".ext");
    for (int i = 1; i <= 17; ++i) wSlot[i] = whereIndex ("slot" + std::to_string (i));
    for (int i = 1; i <= 16; ++i) wIns[i] = whereIndex ("ins" + std::to_string (i));
    for (int b = 0; b < 4; ++b) wBins[b] = whereIndex (std::string ("bins") + X[b]);
    for (int w = 0; w < xlspec::NWHERE; ++w) { modBase[(size_t) w] = I (std::string (xlspec::WHERE[w].id) + ".0"); types[(size_t) w] = xlspec::WHERE[w].defType; }
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate; maxN = maxBlock;
    sig.assign (OUT_NSIG, std::vector<float> (BLK)); tmp.assign (6, std::vector<float> (BLK)); bus.assign (16, std::vector<float> (BLK));
    for (int i = 0; i < 16; ++i) { ch[(size_t) i].assign (BLK, 0); alt[(size_t) i].assign (BLK, 0); }
    for (auto& s : slotOut) s.assign (BLK, 0);
    for (auto& p : pre) p.prepare (fs);
    monSub.lp (80, 1, fs); monSub1.lp1 (80, fs); monHpL.hp (80, .7071, fs); monHpR.hp (80, .7071, fs);
    std::lock_guard<std::mutex> g (swapLock);
    for (int w = 0; w < xlspec::NWHERE; ++w)
    {
        const auto& t = types[(size_t) w]; const auto* def = moduleDef (t);
        mods[(size_t) w][0] = makeModule (t, fs);
        mods[(size_t) w][1] = (def && ! def->stereo && std::string (xlspec::WHERE[w].id).rfind ("bins", 0) == 0) ? makeModule (t, fs) : nullptr;
        hasPending[(size_t) w] = false;
        ctl[(size_t) w].def = def;
        for (auto& m : mods[(size_t) w]) if (m) m->bind (ctl[(size_t) w]);
    }
}

void Engine::setType (int w, const std::string& t)
{
    if (w < 0) return;
    const auto* def = moduleDef (t);
    std::array<std::unique_ptr<Module>, 2> m;
    m[0] = makeModule (t, fs);
    if (def && ! def->stereo && std::string (xlspec::WHERE[w].id).rfind ("bins", 0) == 0) m[1] = makeModule (t, fs);
    Controls c; c.def = def; for (auto& x : m) if (x) x->bind (c);
    std::lock_guard<std::mutex> g (swapLock);
    graveyard.clear();
    for (auto& x : pending[(size_t) w]) if (x) graveyard.push_back (std::move (x));
    pending[(size_t) w] = std::move (m); hasPending[(size_t) w] = true; types[(size_t) w] = t;
}

void Engine::runModule (int w, float* b, int N)
{
    auto& m = mods[(size_t) w][0];
    if (! m) { std::fill (b, b + N, 0.f); return; }
    auto& c = ctl[(size_t) w]; c.base = P + modBase[(size_t) w];
    m->update (c); m->run (b, N); modMeter (w, *m);
}
void Engine::modMeter (int w, Module& m)
{
    auto& o = meters.mod[(size_t) w];
    if (m.hasGr) { o[0] = std::max (o[0].load(), m.grMax); o[1] = std::max (o[1].load(), m.gMax); }
    else if (m.hasPeak) { o[0] = std::max (o[0].load(), m.pk); m.pk = 0; }
}

const float* Engine::srcBlock (int s, const float* const* src, int off, int N, float scale, float* t)
{
    const float* a = src[s]; if (! a) return nullptr;
    for (int i = 0; i < N; ++i) t[i] = a[off + i] * scale;
    return t;
}

void Engine::process (int n, const float* const* src, float* const* out)
{
    if (! P) return;
    if (swapLock.try_lock())
    {
        for (int w = 0; w < xlspec::NWHERE; ++w) if (hasPending[(size_t) w])
        {
            for (int j = 0; j < 2; ++j) std::swap (mods[(size_t) w][(size_t) j], pending[(size_t) w][(size_t) j]);   // the old one goes to pending, freed on the message thread
            ctl[(size_t) w].def = moduleDef (types[(size_t) w]); hasPending[(size_t) w] = false;
        }
        swapLock.unlock();
    }
    for (int off = 0; off < n; off += BLK)
    {
        const int N = std::min (BLK, n - off);
        block (N, src, off);
        for (int s = 0; s < OUT_NSIG; ++s) if (out[s]) std::memcpy (out[s] + off, sig[(size_t) s].data(), sizeof (float) * (size_t) N);
    }
}

void Engine::block (int N, const float* const* src, int off)
{
    const bool noiseOn = on (noise);
    // uniform noise with the published RMS level: amplitude = rms * sqrt(3), rms of a sine-calibrated dBu = dBu() / sqrt(2)
    constexpr float U = 1.2247449f;
    const float nCh = noiseOn ? (float) dBu (-86) * U : 1e-9f, nPre = noiseOn ? (float) dBu (-128) * U : 0.f, nBus = noiseOn ? (float) dBu (-86) * U : 0.f;
    for (auto& b : bus) std::fill (b.begin(), b.begin() + N, 0.f);
    for (auto& s : sig) std::fill (s.begin(), s.begin() + N, 0.f);
    float* BA[2] = { bus[0].data(), bus[1].data() }; float* BB[2] = { bus[2].data(), bus[3].data() }; float* BC[2] = { bus[4].data(), bus[5].data() }; float* BD[2] = { bus[6].data(), bus[7].data() };
    float* BCUE[2] = { bus[8].data(), bus[9].data() }; float* BAUX1 = bus[10].data(); float* BAUX2 = bus[11].data(); float* BAFL[2] = { bus[12].data(), bus[13].data() };
    float** BUS[4] = { BA, BB, BC, BD };
    const bool aflMode = on (soloAfl);
    bool soloAny = false;
    for (int i = 0; i < 16 && ! soloAny; ++i) soloAny = on (mi[i].solo);
    for (int k = 0; k < 4 && ! soloAny; ++k) soloAny = on (si[k].solo);
    const bool sip = soloAny && ! aflMode;
    int stolen[17] {}; // 0 none, else bus 1..4
    for (int b = 0; b < 4; ++b) if (on (bi[b].s500)) { stolen[9 + 2 * b] = b + 1; stolen[10 + 2 * b] = b + 1; }
    const bool links[2] = { ! on (unlink1), ! on (unlink2) };
    const bool auxPreOn[2] = { on (auxPre[0]), on (auxPre[1]) };
    double idle[4] {};

    /* 500 slots fed from the rear 500 SLOT IN connector */
    for (int i = 1; i <= 16; ++i)
    {
        slotExt[i] = false; const int w = wSlot[i];
        if (stolen[i] || ! on (slotExtP[i]) || ! mods[(size_t) w][0]) continue;
        float* so = sig[(size_t) (OUT_SLOTOUT1 + i - 1)].data();
        if (! srcBlock (IN_SLOTIN1 + i - 1, src, off, N, micScale, so)) std::fill (so, so + N, 0.f);
        mods[(size_t) w][0]->noise = nPre;
        runModule (w, so, N); slotExt[i] = true;
    }

    /* mono channels */
    for (int i = 0; i < 16; ++i)
    {
        const auto& m = mi[i]; float* x = ch[(size_t) i].data(); float* al = alt[(size_t) i].data();
        const float* main = nullptr;
        if (i < 8)
        {
            if (srcBlock (IN_MIC1 + i, src, off, N, micScale, tmp[0].data()))
            {
                float* mb = tmp[0].data(); const float k = (on (m.pad) ? .1f : 1.f) * (on (m.hiz) ? 10000.f / 10150.f : 1200.f / 1350.f);
                for (int n = 0; n < N; ++n) mb[n] *= k;
                const int p48 = on (m.p48); if (prevP48[i] >= 0 && p48 != prevP48[i]) pre[i].thump(); prevP48[i] = p48;
                pre[i].run (mb, N, (float) dB (law (m.gain)), on (m.vhd), v (m.drive), nPre); main = mb;
            }
        }
        else main = srcBlock (IN_LINE9 + i - 8, src, off, N, convScale, tmp[0].data());
        const float* dw = srcBlock (IN_DAW1 + i, src, off, N, convScale, tmp[1].data());
        const bool daw = on (m.daw); const float* sel = daw ? dw : main; const float* oth = daw ? main : dw;
        const bool cutNow = on (m.cut) || (sip && ! on (m.solo) && ! on (m.safe));
        if (! sel && ! oth && ! (on (m.s500) && slotExt[i + 1]))
        {
            const float f = cutNow ? 0.f : (float) dB (law (m.fader)), e = f * f * .5f;
            const int bx[4] = { m.A, m.B, m.C, m.D }; for (int q = 0; q < 4; ++q) if (on (bx[q])) idle[q] += e;
            rFd[i].last = -1; continue;
        }
        float t0, tS; rTrim[i].get ((float) dB (law (m.trim)) * (on (m.phase) ? -1.f : 1.f), N, t0, tS);
        if (sel) for (int n = 0; n < N; ++n) x[n] = sel[n] * (t0 + tS * n) + nCh * rng.next();
        else for (int n = 0; n < N; ++n) x[n] = nCh * rng.next();
        if (oth) std::memcpy (al, oth, sizeof (float) * (size_t) N); else std::fill (al, al + N, 0.f);
        // 500 insert
        if (on (m.s500))
        {
            const int w = wSlot[i + 1];
            if (stolen[i + 1] || ! mods[(size_t) w][0] || (! slotExt[i + 1] && ! links[i < 8 ? 0 : 1])) std::fill (x, x + N, 0.f);
            else if (slotExt[i + 1]) std::memcpy (x, sig[(size_t) (OUT_SLOTOUT1 + i)].data(), sizeof (float) * (size_t) N);
            else { runModule (w, x, N); std::memcpy (sig[(size_t) (OUT_SLOTOUT1 + i)].data(), x, sizeof (float) * (size_t) N); }
        }
        if (on (m.ins))
        {
            const int w = wIns[i + 1]; const auto& t = types[(size_t) w];
            if (t == "none") std::fill (x, x + N, 0.f); else if (t != "loop") runModule (w, x, N);
        }
        float pk = 0; for (int n = 0; n < N; ++n) { x[n] = rail (x[n]); pk = std::max (pk, std::abs (x[n])); }
        meters.mono[(size_t) i] = std::max (meters.mono[(size_t) i].load(), pk);
        float f0, fS; rFd[i].get ((float) dB (law (m.fader)) * (cutNow ? 0.f : 1.f), N, f0, fS);
        const float pa = (law (m.pan) + 1) * (float) PI / 4, pL = std::cos (pa), pR = std::sin (pa);
        const float ca = (law (m.cuepan) + 1) * (float) PI / 4, cL = std::cos (ca), cR = std::sin (ca);
        float c0, cS, a10, a1S, a20, a2S; rCue[i].get ((float) dB (law (m.cue)), N, c0, cS); rA1[i].get ((float) dB (law (m.aux1)), N, a10, a1S); rA2[i].get ((float) dB (law (m.aux2)), N, a20, a2S);
        const bool cuePost = on (m.cuepost), cueAlt = on (m.alt), bA = on (m.A), bB = on (m.B), bC = on (m.C), bD = on (m.D), chop = on (m.chop), afl = aflMode && on (m.solo);
        float* D = sig[(size_t) (OUT_CHOUT1 + i)].data();
        for (int n = 0; n < N; ++n)
        {
            const float vv = x[n], post = vv * (f0 + fS * n), l = post * pL, r = post * pR;
            if (bA) { BA[0][n] += l; BA[1][n] += r; } if (bB) { BB[0][n] += l; BB[1][n] += r; }
            if (bC) { BC[0][n] += l; BC[1][n] += r; } if (bD) { BD[0][n] += l; BD[1][n] += r; }
            const float cs = (cueAlt ? al[n] : cuePost ? post : vv) * (c0 + cS * n); BCUE[0][n] += cs * cL; BCUE[1][n] += cs * cR;
            BAUX1[n] += (auxPreOn[0] ? vv : post) * (a10 + a1S * n); BAUX2[n] += (auxPreOn[1] ? vv : post) * (a20 + a2S * n);
            D[n] = chop ? post : vv;
            if (afl) { BAFL[0][n] += l; BAFL[1][n] += r; }
        }
    }

    /* stereo channels */
    for (int k = 0; k < 4; ++k)
    {
        const auto& s = si[k];
        const float* a = srcBlock (IN_ST1L + 2 * k, src, off, N, convScale, tmp[2].data());
        const float* b = srcBlock (IN_ST1L + 2 * k + 1, src, off, N, convScale, tmp[3].data());
        const bool cutNow = on (s.cut) || (sip && ! on (s.solo) && ! on (s.safe));
        if (! a && ! b)
        {
            const float f = cutNow ? 0.f : (float) dB (law (s.fader)), e = f * f;
            const int bx[4] = { s.A, s.B, s.C, s.D }; for (int q = 0; q < 4; ++q) if (on (bx[q])) idle[q] += e;
            sFd[k].last = -1; continue;
        }
        float* L = tmp[4].data(); float* R = tmp[5].data();
        const float tr = (float) dB (law (s.trim)); const bool mono = on (s.mono); const float sL = on (s.phL) ? -tr : tr, sR = on (s.phR) ? -tr : tr;
        for (int n = 0; n < N; ++n) { L[n] = rail ((a ? a[n] : 0.f) * sL + nCh * rng.next()); R[n] = rail (mono ? L[n] : (b ? b[n] : 0.f) * sR + nCh * rng.next()); }
        float pl = 0, pr = 0; for (int n = 0; n < N; ++n) { pl = std::max (pl, std::abs (L[n])); pr = std::max (pr, std::abs (R[n])); }
        meters.stereo[(size_t) (2 * k)] = std::max (meters.stereo[(size_t) (2 * k)].load(), pl); meters.stereo[(size_t) (2 * k + 1)] = std::max (meters.stereo[(size_t) (2 * k + 1)].load(), pr);
        float f0, fS; sFd[k].get ((float) dB (law (s.fader)) * (cutNow ? 0.f : 1.f), N, f0, fS);
        const float pan = law (s.pan); float gL, gR;
        if (mono) { const float aa = (pan + 1) * (float) PI / 4; gL = std::cos (aa); gR = std::sin (aa); std::memcpy (L, R, sizeof (float) * (size_t) N); }
        else { gL = pan > 0 ? std::cos (pan * (float) PI / 2) : 1.f; gR = pan < 0 ? std::cos (-pan * (float) PI / 2) : 1.f; }
        const float cb = law (s.cuepan), cgL = cb > 0 ? std::cos (cb * (float) PI / 2) : 1.f, cgR = cb < 0 ? std::cos (-cb * (float) PI / 2) : 1.f, cl = (float) dB (law (s.cue));
        const bool cuePost = on (s.cuepost), chop = on (s.chop), afl = aflMode && on (s.solo);
        const float a1g = (float) dB (law (s.aux1)), a2g = (float) dB (law (s.aux2));
        const bool bs[4] = { on (s.A), on (s.B), on (s.C), on (s.D) };
        float* DL = sig[(size_t) (OUT_STOUT1 + 2 * k)].data(); float* DR = sig[(size_t) (OUT_STOUT1 + 2 * k + 1)].data();
        for (int n = 0; n < N; ++n)
        {
            const float f = f0 + fS * n, l = L[n] * f * gL, r = R[n] * f * gR;
            for (int q = 0; q < 4; ++q) if (bs[q]) { BUS[q][0][n] += l; BUS[q][1][n] += r; }
            const float cs = cuePost ? f : 1.f; BCUE[0][n] += L[n] * cs * cl * cgL; BCUE[1][n] += R[n] * cs * cl * cgR;
            const float mn = (L[n] + R[n]) * .5f; BAUX1[n] += mn * (auxPreOn[0] ? 1.f : f) * a1g; BAUX2[n] += mn * (auxPreOn[1] ? 1.f : f) * a2g;
            DL[n] = chop ? l : L[n]; DR[n] = chop ? r : R[n];
            if (afl) { BAFL[0][n] += l; BAFL[1][n] += r; }
        }
    }

    /* stereo returns */
    for (int k = 0; k < 2; ++k)
    {
        const auto& r = ri[k];
        const float* a = srcBlock (IN_RET1L + 2 * k, src, off, N, convScale, tmp[2].data());
        const float* b = srcBlock (IN_RET1L + 2 * k + 1, src, off, N, convScale, tmp[3].data());
        const bool afl = on (r.afl); if (! a && ! b && ! afl) continue;
        const float lv = sip ? 0.f : (float) dB (law (r.level)), bal = law (r.bal); const bool mono = on (r.mono);
        float gL, gR; if (mono) { const float aa = (bal + 1) * (float) PI / 4; gL = std::cos (aa); gR = std::sin (aa); } else { gL = bal > 0 ? std::cos (bal * (float) PI / 2) : 1.f; gR = bal < 0 ? std::cos (-bal * (float) PI / 2) : 1.f; }
        float l0, lS; rLv[k].get (lv, N, l0, lS);
        const bool bs[4] = { on (r.A), on (r.B), on (r.C), on (r.D) }, cue = on (r.cue);
        float* DL = sig[(size_t) (OUT_RETOUT1 + 2 * k)].data(); float* DR = sig[(size_t) (OUT_RETOUT1 + 2 * k + 1)].data();
        for (int n = 0; n < N; ++n)
        {
            float x = a ? a[n] : 0.f, y = b ? b[n] : x; if (! b && a) y = x; if (mono) x = y = (x + y) * .5f;
            const float vv = l0 + lS * n, l = x * vv * gL, rr = y * vv * gR;
            for (int q = 0; q < 4; ++q) if (bs[q]) { BUS[q][0][n] += l; BUS[q][1][n] += rr; }
            if (cue) { BCUE[0][n] += l; BCUE[1][n] += rr; }
            if (afl) { BAFL[0][n] += l; BAFL[1][n] += rr; }
            DL[n] = l; DR[n] = rr;
        }
    }

    /* mix buses B, C, D, then A */
    auto bamp = [&] (float** b, float nz) { for (int s = 0; s < 2; ++s) for (int n = 0; n < N; ++n) b[s][n] = rail (b[s][n] + nz * rng.next()); };
    auto busChain = [&] (int bix, int s1)
    {
        float** b = BUS[bix];
        if (on (bi[bix].s500)) for (int side = 0; side < 2; ++side) runModule (wSlot[s1 + side], b[side], N);
        const bool ins = on (bi[bix].ins), sum = on (bi[bix].sum);
        if (ins || sum)
        {
            const int w = wBins[bix]; const auto& t = types[(size_t) w];
            float* rl = tmp[4].data(); float* rr = tmp[5].data();
            std::memcpy (rl, b[0], sizeof (float) * (size_t) N); std::memcpy (rr, b[1], sizeof (float) * (size_t) N);
            auto& ms = mods[(size_t) w];
            if (t == "none") { std::fill (rl, rl + N, 0.f); std::fill (rr, rr + N, 0.f); }
            else if (ms[0] && ms[0]->stereo)
            {
                auto& c = ctl[(size_t) w]; c.base = P + modBase[(size_t) w]; ms[0]->update (c); ms[0]->runStereo (rl, rr, N);
                meters.mod[(size_t) w][0] = ms[0]->needle;
            }
            else if (t != "loop") { auto& c = ctl[(size_t) w]; c.base = P + modBase[(size_t) w]; for (int j = 0; j < 2; ++j) if (ms[(size_t) j]) { ms[(size_t) j]->update (c); ms[(size_t) j]->run (j ? rr : rl, N); modMeter (w, *ms[(size_t) j]); } }
            for (int n = 0; n < N; ++n) { b[0][n] = sum ? b[0][n] + rl[n] : rl[n]; b[1][n] = sum ? b[1][n] + rr[n] : rr[n]; }
        }
    };
    for (int bix = 1; bix < 4; ++bix)
    {
        float** b = BUS[bix]; bamp (b, (float) std::sqrt (nBus * nBus + nCh * nCh * idle[bix])); busChain (bix, 9 + 2 * bix);
        float v0, vS; bLv[bix - 1].get ((float) dB (law (bi[bix].level)), N, v0, vS);
        float* oL = sig[(size_t) (OUT_MIXA + 2 * bix)].data(); float* oR = sig[(size_t) (OUT_MIXA + 2 * bix + 1)].data();
        const bool toA = on (bi[bix].toA), afl = on (bi[bix].afl);
        for (int n = 0; n < N; ++n) { const float vv = v0 + vS * n, l = rail (b[0][n] * vv), r = rail (b[1][n] * vv); oL[n] = l; oR[n] = r; if (toA) { BA[0][n] += l; BA[1][n] += r; } if (afl) { BAFL[0][n] += l; BAFL[1][n] += r; } }
    }
    {
        const float cv = (float) dB (law (cueLevel)); const bool toA = on (cueToA), afl = on (cueAfl);
        float* oL = sig[OUT_CUE].data(); float* oR = sig[OUT_CUE + 1].data();
        for (int n = 0; n < N; ++n) { const float l = rail (BCUE[0][n] * cv), r = rail (BCUE[1][n] * cv); oL[n] = l; oR[n] = r; if (toA) { BA[0][n] += l; BA[1][n] += r; } if (afl) { BAFL[0][n] += l; BAFL[1][n] += r; } }
        float* AB[2] = { BAUX1, BAUX2 };
        for (int a = 0; a < 2; ++a) { const float vv = (float) dB (law (auxLevel[a])); float* o = sig[(size_t) (OUT_AUX1 + a)].data(); const bool af = on (auxAfl[a]); for (int n = 0; n < N; ++n) { o[n] = rail (AB[a][n] * vv); if (af) { BAFL[0][n] += o[n]; BAFL[1][n] += o[n]; } } }
    }
    {
        bamp (BA, (float) std::sqrt (nBus * nBus + nCh * nCh * idle[0])); busChain (0, 9);
        // the bus compressor's send is always live, so its needle moves even with COMP out
        float* cl = tmp[2].data(); float* cr = tmp[3].data();
        std::memcpy (cl, BA[0], sizeof (float) * (size_t) N); std::memcpy (cr, BA[1], sizeof (float) * (size_t) N);
        auto& gc = mods[(size_t) wSlot[17]][0];
        if (gc) { auto& c = ctl[(size_t) wSlot[17]]; c.base = P + modBase[(size_t) wSlot[17]]; gc->update (c); gc->runStereo (cl, cr, N); meters.gcomp = gc->needle; }
        else { std::fill (cl, cl + N, 0.f); std::fill (cr, cr + N, 0.f); meters.gcomp = 0; }
        if (on (aComp)) { std::memcpy (BA[0], cl, sizeof (float) * (size_t) N); std::memcpy (BA[1], cr, sizeof (float) * (size_t) N); }
        float f0, fS; aFd.get ((float) dB (law (aFader)), N, f0, fS);
        float* oL = sig[OUT_MIXA].data(); float* oR = sig[OUT_MIXA + 1].data();
        for (int n = 0; n < N; ++n) { const float f = f0 + fS * n; oL[n] = rail (BA[0][n] * f); oR[n] = rail (BA[1][n] * f); }
    }
    for (int s = 0; s < 8; ++s) { const float p = tpk[s].run (sig[(size_t) (OUT_MIXA + s)].data(), N); meters.bus[(size_t) s] = std::max (meters.bus[(size_t) s].load(), p); }

    /* listen mic and its compressor */
    float* lsn = nullptr;
    if (srcBlock (IN_LISTEN, src, off, N, micScale, sig[OUT_LSN].data()))
    {
        lsn = sig[OUT_LSN].data(); const double gn = dB (law (listenLevel)), aC = 1 - std::exp (-1 / (.0002 * fs)), rC = 1 - std::exp (-1 / (.12 * fs)), T = -16, mk = dB (14);
        for (int n = 0; n < N; ++n) { const double x = lsn[n] * gn, a = std::abs (x); lsnEnv += (a - lsnEnv) * (a > lsnEnv ? aC : rC); const double o = toDbu (lsnEnv) - T, gr = o > 0 ? o * (1 - 1.0 / 20) : 0; lsn[n] = rail ((float) (x * std::pow (10.0, -gr / 20) * mk)); }
    }

    /* monitoring */
    float* mon = tmp[0].data(); float* mon2 = tmp[1].data();
    const bool aflAny = (aflMode && soloAny) || on (ri[0].afl) || on (ri[1].afl) || on (bi[1].afl) || on (bi[2].afl) || on (bi[3].afl) || on (cueAfl) || on (auxAfl[0]) || on (auxAfl[1]);
    if (aflAny) { const float vv = (float) dB (law (monAfl)); for (int n = 0; n < N; ++n) { mon[n] = BAFL[0][n] * vv; mon2[n] = BAFL[1][n] * vv; } }
    else
    {
        std::fill (mon, mon + N, 0.f); std::fill (mon2, mon2 + N, 0.f);
        if (on (monSrcA)) for (int n = 0; n < N; ++n) { mon[n] += sig[OUT_MIXA][(size_t) n]; mon2[n] += sig[OUT_MIXA + 1][(size_t) n]; }
        for (int s : { on (monSrcExt) ? IN_EXTL : -1, on (monSrcIj) ? IN_IJL : -1 }) if (s >= 0)
        {
            const float* a = src[s]; const float* b = src[s + 1] ? src[s + 1] : a;
            if (a) for (int n = 0; n < N; ++n) { mon[n] += a[off + n] * convScale; mon2[n] += b[off + n] * convScale; }
        }
    }
    if (on (listenOn) && lsn) for (int n = 0; n < N; ++n) { mon[n] += lsn[n]; mon2[n] += lsn[n]; }
    std::memcpy (sig[OUT_METER].data(), mon, sizeof (float) * (size_t) N); std::memcpy (sig[OUT_METER + 1].data(), mon2, sizeof (float) * (size_t) N);
    {
        // headphones: F/B A or B, else the monitor source (talkback is not heard here)
        const float* aux1 = sig[OUT_AUX1].data();
        const float* fbL[2] = { on (fbMon[0]) ? mon : sig[OUT_CUE].data(), on (fbMon[1]) ? mon : aux1 };
        const float* fbR[2] = { on (fbMon[0]) ? mon2 : sig[OUT_CUE + 1].data(), on (fbMon[1]) ? mon : aux1 };
        const int hs = on (hpFbA) ? 0 : on (hpFbB) ? 1 : -1;
        const float hv = (float) dB (law (hpLevel)), fv = hs >= 0 ? (float) dB (law (fbLevel[hs])) : 1.f;
        for (int n = 0; n < N; ++n) { sig[OUT_HP][(size_t) n] = (hs >= 0 ? fbL[hs][n] : mon[n]) * fv * hv; sig[OUT_HP + 1][(size_t) n] = (hs >= 0 ? fbR[hs][n] : mon2[n]) * fv * hv; }
        const float* tk = src[IN_TALK]; const float tg = (float) dB (law (talkLevel)) * micScale;
        for (int f = 0; f < 2; ++f)
        {
            const float lv = (float) dB (law (fbLevel[f])); const bool talk = on (fbTalk[f]) && tk, rep = talk && on (dip[f ? 4 : 2]);
            float* oL = sig[(size_t) (OUT_FBA + 2 * f)].data(); float* oR = sig[(size_t) (OUT_FBA + 2 * f + 1)].data();
            for (int n = 0; n < N; ++n) { const float t = talk ? tk[off + n] * tg : 0.f; oL[n] = rail (((rep ? 0.f : fbL[f][n]) + t) * lv); oR[n] = rail (((rep ? 0.f : fbR[f][n]) + t) * lv); }
        }
    }
    {
        const int set = on (monMini1) ? 1 : on (monMini2) ? 2 : 0;
        float vv = on (monCut) ? 0.f : (float) dB (law (monLevel)) * (on (monDim) ? (float) dB (law (monDimAmt)) : 1.f);
        if (set == 1) vv *= (float) dB (law (monOff1));
        else if (set == 2) vv *= (float) dB (law (monOff2));
        float v0, vS; monV.get (vv, N, v0, vS);
        const bool subOn = on (monSub_[set]), mono = on (monMono) || (set == 2 && on (dip[7]));
        float* oL = sig[(size_t) (OUT_MAIN + 2 * set)].data(); float* oR = sig[(size_t) (OUT_MAIN + 2 * set + 1)].data(); float* sb = sig[OUT_SUB].data();
        for (int n = 0; n < N; ++n)
        {
            float l = mon[n] * (v0 + vS * n), r = mon2[n] * (v0 + vS * n);
            if (mono) l = r = (l + r) * .5f;
            float s = 0;
            if (subOn) { s = monSub.run (monSub1.run (l + r)); l = monHpL.run (l); r = monHpR.run (r); }   // 3rd-order LP at 80 Hz to the sub, 2nd-order OUT_HP on the speakers
            oL[n] = rail (l); oR[n] = rail (r); sb[n] = rail (s);
        }
    }
}
} // namespace xl
