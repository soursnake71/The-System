/* The XL-Desk console engine for the VST3: the same signal flow as xldesk/dsp/console.js (Appendix A of the Owner's
   Manual), with the console's rear-panel inputs and outputs mapped to the plugin's buses. No JUCE in here, so it can
   be unit-tested on its own. */
#pragma once
#include "Modules.h"
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace xl {

// console inputs (rear panel connectors)
enum Src : int {
    IN_MIC1 = 0, IN_LINE9 = 8, IN_DAW1 = 16, IN_ST1L = 32, IN_RET1L = 40, IN_EXTL = 44, IN_IJL = 46, IN_TALK = 48, IN_LISTEN = 49, IN_SLOTIN1 = 50, IN_NSRC = 66
};
// console output signals, one entry per channel
enum Sig : int {
    OUT_MIXA = 0, OUT_MIXB = 2, OUT_MIXC = 4, OUT_MIXD = 6, OUT_MAIN = 8, OUT_MINI1 = 10, OUT_MINI2 = 12, OUT_SUB = 14, OUT_HP = 15, OUT_FBA = 17, OUT_FBB = 19, OUT_CUE = 21,
    OUT_AUX1 = 23, OUT_AUX2 = 24, OUT_CHOUT1 = 25, OUT_STOUT1 = 41, OUT_RETOUT1 = 49, OUT_SLOTOUT1 = 53, OUT_METER = 69, OUT_LSN = 71, OUT_NSIG = 72
};

struct Meters
{
    std::array<std::atomic<float>, 16> mono {};     // channel signal LEDs, peak dBu
    std::array<std::atomic<float>, 8> stereo {};    // stereo channel LEDs L/R, peak dBu
    std::array<std::atomic<float>, 8> bus {};       // Mix A-D L/R, true peak dBu
    std::atomic<float> gcomp { 0 };                 // bus compressor needle, dB GR
    std::array<std::array<std::atomic<float>, 2>, xlspec::NWHERE> mod {};   // per location: [GR or peak, gate]
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    // normalised parameter values, indexed like xlspec::PARAMS; owned by the caller, read every block
    void setParams (const float* p) { P = p; }
    // message thread: fit a module (the swap happens at the next audio block)
    void setType (int where, const std::string& type);
    const std::string& type (int where) const { return types[(size_t) where]; }
    void setMicRef (double dbu) { micScale = (float) dBu (dbu); }
    void setConvRef (double dbu) { convScale = (float) dBu (dbu); }
    /* audio thread. src[i] may be null (nothing plugged in). out[i] receives Sig i (may be null). */
    void process (int n, const float* const* src, float* const* out);
    Meters meters;
    static int whereIndex (const std::string& w);
    static int paramIndex (const std::string& id);

private:
    void block (int N, const float* const* src, int off);
    float v (int i) const { return P[i]; }
    bool on (int i) const { return P[i] > .5f; }
    float law (int i) const { return (float) xlspec::law (xlspec::PARAMS[i].law, P[i]); }
    struct Ramp { float last = -1; void get (float tgt, int N, float& s, float& d) { if (last < 0) last = tgt; s = last; d = (tgt - last) / N; last = tgt; } };
    void runModule (int w, float* b, int N);
    void modMeter (int w, Module& m);
    const float* srcBlock (int s, const float* const* src, int off, int N, float scale, float* tmp);

    double fs = 48000; int maxN = 0; const float* P = nullptr;
    float micScale = (float) dBu (-20), convScale = 1;
    std::array<std::string, xlspec::NWHERE> types;
    std::array<std::array<std::unique_ptr<Module>, 2>, xlspec::NWHERE> mods, pending;
    std::array<bool, xlspec::NWHERE> hasPending {};
    std::mutex swapLock; std::vector<std::unique_ptr<Module>> graveyard;
    std::array<int, xlspec::NWHERE> modBase {};
    std::array<Controls, xlspec::NWHERE> ctl;

    std::vector<std::vector<float>> sig, tmp, bus;   // per-block buffers (128)
    std::array<std::vector<float>, 16> ch, alt;
    std::vector<float> slotOut[17];
    bool slotExt[17] {};
    TruePeak tpk[8];
    VHD pre[8]; int prevP48[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    BQ monSub, monSub1, monHpL, monHpR;
    double lsnEnv = 0;
    // ramps
    Ramp rTrim[16], rFd[16], rCue[16], rA1[16], rA2[16], sFd[4], rLv[2], bLv[3], aFd, monV;
    // parameter indices
    struct MonoIdx { int gain = -1, drive = -1, vhd = -1, hiz = -1, p48 = -1, pad = -1, trim, daw, phase, s500, ins, chop, cue, cuepan, cuepost, alt, aux1, aux2, pan, A, B, C, D, cut, solo, safe, fader; } mi[16];
    struct StIdx { int trim, phL, phR, mono, chop, cue, cuepan, cuepost, aux1, aux2, pan, A, B, C, D, cut, solo, safe, fader; } si[4];
    struct RetIdx { int level, bal, mono, cue, A, B, C, D, afl; } ri[2];
    struct BusIdx { int s500, ins, sum, toA, level, afl; } bi[4];
    int aComp, aFader, cueLevel, cueAfl, cueToA, auxLevel[2], auxPre[2], auxAfl[2], soloAfl;
    int monLevel, monDim, monCut, monDimAmt, monAfl, monMono, monMini1, monMini2, monSub_[3], monSrcA, monSrcExt, monSrcIj, monOff1, monOff2;
    int hpLevel, hpFbA, hpFbB, fbLevel[2], fbMon[2], fbTalk[2], talkLevel, listenLevel, listenOn, dip[13], noise, unlink1, unlink2, slotExtP[17];
    int wSlot[18], wIns[17], wBins[4];
};
} // namespace xl
