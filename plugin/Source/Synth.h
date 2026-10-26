#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/* The built-in sound engines, the same as the web app (sky/sound.js): seven simple voices, an OB-Xa style synth,
   an eight-piece synthesized drum kit, a shared reverb and a tempo-locked delay unit (simple or Edge style). */
namespace sky
{
enum Engine { ePluck, ePad, eBell, eBass, eGlass, eLead, ePerc, eObxa, eMidiOnly, numEngines };
inline juce::StringArray engineNames() { return { "Pluck", "Pad", "FM Bell", "Bass", "Glass", "Lead", "Perc", "OB-Xa", "MIDI only" }; }
inline juce::StringArray engineKeys() { return { "pluck", "pad", "bell", "bass", "glass", "lead", "perc", "obxa", "none" }; }   // names used in presets.json

// OB-Xa patch, same fields as SkyPresets.PATCH in sky/presets.js
struct Patch { float w1 = 0, w2 = 1, pw = 0.3f, mix2 = 0.7f, det = 7, int2 = 0, cut = 1800, res = 0.3f, poles = 1, env = 2.5f, fa = 0.005f, fd = 0.4f, fs = 0.3f, fr = 0.3f,
               aa = 0.005f, ad = 0.5f, as = 0.7f, ar = 0.3f, lfo = 5, vib = 0, pwm = 0; };
// drum kit, same fields as SkyPresets.KIT
struct Kit { float kp = 180, kf = 48, kd = 0.45f, kc = 0.4f, sf = 190, sd = 0.18f, ss = 0.7f, hd = 0.05f, ho = 0.4f, ht = 7000, cd = 0.2f, tf = 120, td = 0.3f, rf = 900,
             pf = 560, pr = 1.48f, pd = 0.15f, cy = 1.2f, ct = 5500, shd = 0.06f, cgf = 250, cgd = 0.18f, tbd = 0.12f, lvl = 0.9f; };
enum Drum { dKick, dSnare, dClosedHat, dOpenHat, dClap, dTom, dRim, dPerc, dCymbal, dShaker, dConga, dTambourine, numDrums };
// gated reverb and random echo settings
struct SpaceSettings { float gtime = 0.3f, gmix = 0.7f, emix = 0.6f; };
struct DelaySettings { int mode = 0; float mix = 0.5f, fb = 0.38f, tone = 2800; };

class Synth
{
public:
    static constexpr int numLanes = 7;   // six lanes + the emergency alarm
    std::array<float, numLanes> laneVol {}, laneRev {}, laneDly {}, laneGtd {};
    float drumVol = 0.9f, drumRev = 0.12f, drumDly = 0.05f, drumGtd = 0.4f;
    SpaceSettings space;
    // peak level per channel since the editor last read it: six lanes, alarm, drums
    std::array<std::atomic<float>, 8> peaks {};

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices) v.on = false;
        juce::Reverb::Parameters p; p.roomSize = 0.82f; p.damping = 0.45f; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.width = 1.0f;
        reverb.setParameters (p); reverb.setSampleRate (sr); reverb.reset();
        dlL.assign ((size_t) (sr * 2.0) + 1, 0.0f); dlR.assign (dlL.size(), 0.0f); dlPos = 0; dlLp = { 0, 0 };
        sendRev.setSize (2, maxBlock); sendDly.setSize (2, maxBlock); sendGtd.setSize (2, maxBlock); echoIn.setSize (3, maxBlock);
        juce::Reverb::Parameters g; g.roomSize = 0.92f; g.damping = 0.25f; g.wetLevel = 1.0f; g.dryLevel = 0.0f; g.width = 1.0f;
        gatedRev.setParameters (g); gatedRev.setSampleRate (sr); gatedRev.reset(); gateHold = 0; gateGain = 0; gateEnv.assign ((size_t) maxBlock, 0.0f);
        for (auto& e : echoLines) { e.buf.assign ((size_t) (sr * 3.0) + 1, 0.0f); e.lp = e.hp = 0; }
        echoPos = 0;
    }
    // delay taps follow the tempo: a dotted eighth, and a quarter for the Edge's second tap
    void setBpm (double bpm)
    {
        delaySamples = juce::jlimit (1.0, sr * 1.9, sr * 45.0 / bpm); quarterSamples = juce::jlimit (1.0, sr * 1.9, sr * 60.0 / bpm);
        static const double beats[3] = { 1.0 / 3.0, 1.0, 1.5 };   // random echoes: 8th triplet, quarter, dotted quarter
        for (int k = 0; k < 3; ++k) echoLines[(size_t) k].time = juce::jlimit (1.0, sr * 2.9, sr * 60.0 / bpm * beats[k]);
    }
    void allOff() { for (auto& v : voices) if (v.on && v.t < v.dur) v.dur = v.t; }
    DelaySettings fx;
    Kit kit;

    // start a note `offset` samples into the next render call; `patch` is used by the OB-Xa engine
    // echoAmt > 0 also throws the note into echo line echoLine (random echoes)
    void noteOn (int engine, int lane, int note, float vel, double durSec, float pan, int offset, const Patch* patch = nullptr, float echoAmt = 0, int echoLine = 0)
    {
        if (engine < 0 || engine >= eMidiOnly) return;
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.on) { v = &x; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }   // steal the oldest
        *v = Voice();
        v->on = true; v->engine = engine; v->lane = lane; v->vel = vel; v->delay = offset;
        v->f = 440.0 * std::pow (2.0, (juce::jlimit (12, 115, note) - 69) / 12.0);
        v->dur = durSec; v->seed = (juce::uint32) (note * 7919 + offset + 1);
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
        v->gl = std::cos (a); v->gr = std::sin (a);
        if (patch != nullptr) v->p = *patch;
        v->echo = echoAmt; v->echoLine = juce::jlimit (0, 2, echoLine);
        setShape (*v);
    }

    // a drum hit `offset` samples into the next render call, using the current kit
    void drumOn (int drum, float vel, float pan, int offset, float echoAmt = 0, int echoLine = 0)
    {
        DrumVoice* v = nullptr;
        for (auto& x : drumVoices) if (! x.on) { v = &x; break; }
        if (v == nullptr) { v = &drumVoices[0]; for (auto& x : drumVoices) if (x.t > v->t) v = &x; }
        *v = DrumVoice();
        v->on = true; v->drum = drum; v->vel = vel; v->delay = offset; v->k = kit; v->seed = (juce::uint32) (drum * 977 + offset * 13 + 7);
        v->echo = echoAmt; v->echoLine = juce::jlimit (0, 2, echoLine);
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
        v->gl = std::cos (a); v->gr = std::sin (a);
    }

    void render (juce::AudioBuffer<float>& out, int n)
    {
        out.clear (0, n); sendRev.clear (0, n); sendDly.clear (0, n); sendGtd.clear (0, n); echoIn.clear (0, n);
        auto *L = out.getWritePointer (0), *R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        auto *rl = sendRev.getWritePointer (0), *rr = sendRev.getWritePointer (1), *dl = sendDly.getWritePointer (0), *dr = sendDly.getWritePointer (1);
        auto *gl = sendGtd.getWritePointer (0), *gr = sendGtd.getWritePointer (1);
        std::array<float, 8> pk {};
        const double dt = 1.0 / sr;
        for (auto& v : voices)
        {
            if (! v.on) continue;
            ++v.age;
            const float vol = laneVol[(size_t) v.lane], rv = laneRev[(size_t) v.lane], dv = laneDly[(size_t) v.lane], gv = laneGtd[(size_t) v.lane];
            auto* ei = echoIn.getWritePointer (v.echoLine);
            float& peak = pk[(size_t) v.lane];
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = tick (v, dt) * vol;
                L[i] += s * v.gl; if (R) R[i] += s * v.gr;
                rl[i] += s * v.gl * rv; rr[i] += s * v.gr * rv; dl[i] += s * v.gl * dv; dr[i] += s * v.gr * dv; gl[i] += s * v.gl * gv; gr[i] += s * v.gr * gv;
                ei[i] += s * v.echo;
                peak = juce::jmax (peak, std::abs (s));
                if (! v.on) break;
            }
        }
        // drums: their own channel with sends; the gated reverb is mostly for these
        for (auto& v : drumVoices)
        {
            if (! v.on) continue;
            auto* ei = echoIn.getWritePointer (v.echoLine);
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = drumTick (v) * kit.lvl * drumVol;
                L[i] += s * v.gl; if (R) R[i] += s * v.gr;
                rl[i] += s * drumRev; rr[i] += s * drumRev; dl[i] += s * drumDly; dr[i] += s * drumDly; gl[i] += s * v.gl * drumGtd; gr[i] += s * v.gr * drumGtd;
                ei[i] += s * v.echo;
                pk[7] = juce::jmax (pk[7], std::abs (s));
                if (! v.on) break;
            }
        }
        for (size_t k = 0; k < 8; ++k) peaks[k] = juce::jmax (peaks[k].load(), pk[k]);
        // gated reverb: a big bright room whose output opens when something hits it and slams shut after the gate time
        {
            const int hold = (int) (space.gtime * sr);
            const float att = 1.0f - std::exp (-1.0f / (0.0008f * (float) sr)), rel = 1.0f - std::exp (-1.0f / (0.012f * (float) sr));
            for (int i = 0; i < n; ++i) { if (std::abs (gl[i]) + std::abs (gr[i]) > 0.01f) gateHold = hold; gateEnv[(size_t) i] = gateHold-- > 0 ? 1.0f : 0.0f; }
            gatedRev.processStereo (gl, gr, n);
            for (int i = 0; i < n; ++i)
            {
                gateGain += (gateEnv[(size_t) i] - gateGain) * (gateEnv[(size_t) i] > gateGain ? att : rel);
                const float g = gateGain * space.gmix * 0.9f;
                L[i] += gl[i] * g; if (R) R[i] += gr[i] * g;
            }
        }
        // random echoes: three tempo-locked lines (8th triplet left, quarter right, dotted quarter centre), darker each repeat
        {
            static const float pans[3][2] = { { 0.92f, 0.38f }, { 0.38f, 0.92f }, { 0.71f, 0.71f } };
            const float cl = coef (3800), ch = coef (220);
            const int N = (int) echoLines[0].buf.size();
            for (int i = 0; i < n; ++i)
            {
                for (size_t k = 0; k < 3; ++k)
                {
                    auto& e = echoLines[k];
                    double rp = echoPos - e.time; if (rp < 0) rp += N;
                    const int i0 = (int) rp, i1 = (i0 + 1) % N; const float f = (float) (rp - i0);
                    const float y = e.buf[(size_t) i0] + (e.buf[(size_t) i1] - e.buf[(size_t) i0]) * f;
                    e.lp += (y - e.lp) * cl; e.hp += (e.lp - e.hp) * ch;
                    const float w = e.lp - e.hp;
                    e.buf[(size_t) echoPos] = echoIn.getSample ((int) k, i) + w * 0.48f;
                    const float o = w * space.emix;
                    L[i] += o * pans[k][0]; if (R) R[i] += o * pans[k][1];
                    rl[i] += o * 0.25f; rr[i] += o * 0.25f;
                }
                echoPos = (echoPos + 1) % N;
            }
        }
        // the delay unit, locked to the tempo. Simple: a dotted eighth, cross-fed, darker each repeat.
        // Edge (U2 style): driven input, a modulated dotted eighth with feedback on the right, a quarter-note tap on the left.
        const int N = (int) dlL.size();
        const float twoPi = juce::MathConstants<float>::twoPi, wet = fx.mix * 1.6f;
        const float c1 = 1.0f - std::exp (-twoPi * juce::jmin (fx.tone, 18000.0f) / (float) sr);
        const float cA = 1.0f - std::exp (-twoPi * juce::jmin (fx.tone * 1.4f, 18000.0f) / (float) sr), cB = 1.0f - std::exp (-twoPi * juce::jmin (fx.tone * 1.2f, 18000.0f) / (float) sr);
        const float cHp = 1.0f - std::exp (-twoPi * 140.0f / (float) sr);
        auto readAt = [N] (const std::vector<float>& b, double rp) { rp = std::fmod (rp + 4.0 * N, (double) N); const int i0 = (int) rp, i1 = (i0 + 1) % N; const float f = (float) (rp - i0); return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f; };
        for (int i = 0; i < n; ++i)
        {
            float yl = 0, yr = 0;
            if (fx.mode == 0)
            {
                const float al = readAt (dlL, dlPos - delaySamples), ar = readAt (dlR, dlPos - delaySamples);
                dlLp[0] += (al - dlLp[0]) * c1; dlLp[1] += (ar - dlLp[1]) * c1;
                dlL[(size_t) dlPos] = dl[i] * 0.5f + dlLp[1] * fx.fb; dlR[(size_t) dlPos] = dr[i] * 0.5f + dlLp[0] * fx.fb;
                yl = dlLp[0]; yr = dlLp[1];
            }
            else
            {
                lfoPh += 0.7 / sr; if (lfoPh >= 1) lfoPh -= 1;
                const double mod = std::sin (lfoPh * juce::MathConstants<double>::twoPi) * 0.0018 * sr;
                const float a = readAt (dlL, dlPos - delaySamples - mod), b = readAt (dlR, dlPos - quarterSamples - mod);
                edgeHp += (a - edgeHp) * cHp; dlLp[0] += ((a - edgeHp) - dlLp[0]) * cA; dlLp[1] += (b - dlLp[1]) * cB;
                const float in = std::tanh (1.6f * 0.5f * (dl[i] + dr[i])) / std::tanh (1.6f);
                dlL[(size_t) dlPos] = in * 0.5f + dlLp[0] * juce::jmin (0.92f, fx.fb * 1.1f);
                dlR[(size_t) dlPos] = in * 0.5f + dlLp[1] * fx.fb * 0.55f;
                const float tapA = dlLp[0], tapB = dlLp[1] * 0.6f;   // A sits right, B left
                yl = tapA * 0.35f + tapB * 0.65f; yr = tapA * 0.65f + tapB * 0.35f;
            }
            dlPos = (dlPos + 1) % N;
            yl *= wet; yr *= wet;
            L[i] += yl; if (R) R[i] += yr;
            rl[i] = rl[i] * 0.5f + yl * 0.3f; rr[i] = rr[i] * 0.5f + yr * 0.3f;
        }
        reverb.processStereo (rl, rr, n);
        for (int i = 0; i < n; ++i)
        {
            L[i] = std::tanh ((L[i] + rl[i]) * 0.8f);
            if (R) R[i] = std::tanh ((R[i] + rr[i]) * 0.8f);
        }
    }

private:
    struct Voice
    {
        bool on = false; int engine = 0, lane = 0, delay = 0; juce::int64 age = 0;
        double f = 440, t = 0, dur = 0.2, ph[4] {}; float vel = 1, gl = 0.7f, gr = 0.7f;
        // envelope: attack, decay time constant, sustain level, release time constant, peak
        double a = 0.01, d = 0.1, sus = 0.5, rel = 0.1, peak = 0.3, endAt = 1, relFrom = -1;
        float ic1 = 0, ic2 = 0, hp1 = 0, hp2 = 0, g = 0.1f, k = 1; int coefCount = 0; juce::uint32 seed = 1;
        Patch p; float jc1 = 0, jc2 = 0; double lfoPh = 0;   // OB-Xa: second filter stage, LFO
        float echo = 0; int echoLine = 0;
    };
    struct DrumVoice { bool on = false; int drum = 0, delay = 0; float vel = 1, gl = 0.7f, gr = 0.7f; double t = 0, ph = 0, ph2 = 0; std::array<double, 6> mp {}; Kit k; juce::uint32 seed = 1; float z1 = 0, z2 = 0;
                       float echo = 0; int echoLine = 0; };
    struct EchoLine { std::vector<float> buf; double time = 12000; float lp = 0, hp = 0; };
    std::array<EchoLine, 3> echoLines; int echoPos = 0;
    juce::Reverb gatedRev; int gateHold = 0; float gateGain = 0; std::vector<float> gateEnv;
    juce::AudioBuffer<float> sendGtd, echoIn;
    std::array<Voice, 64> voices;
    std::array<DrumVoice, 24> drumVoices;
    double sr = 48000, delaySamples = 18000, quarterSamples = 24000, lfoPh = 0; float edgeHp = 0;
    juce::Reverb reverb;
    std::vector<float> dlL, dlR; int dlPos = 0; std::array<float, 2> dlLp {};
    juce::AudioBuffer<float> sendRev, sendDly;

    void setShape (Voice& v)
    {
        const float vl = v.vel;
        switch (v.engine)
        {
            case ePluck: v.a = 0.003; v.d = 0.25 / 3; v.sus = 0.25; v.rel = 0.18 / 4; v.peak = 0.35 * vl; v.k = 1.0f / 6.0f; break;
            case ePad:   v.a = juce::jmin (1.2, v.dur * 0.4); v.d = 0.5 / 3; v.sus = 0.8; v.rel = 1.6 / 4; v.peak = 0.16 * vl; v.k = 1.4f; break;
            case eBell:  v.dur = juce::jmax (v.dur, 0.6); v.a = 0.002; v.d = 1.4 / 3; v.sus = 0.05; v.rel = 0.6 / 4; v.peak = 0.3 * vl; break;
            case eBass:  v.a = 0.004; v.d = 0.2 / 3; v.sus = 0.6; v.rel = 0.08 / 4; v.peak = 0.45 * vl; v.k = 1.0f / 8.0f; break;
            case eGlass: v.a = 0.02; v.d = 0.6 / 3; v.sus = 0.3; v.rel = 0.9 / 4; v.peak = 0.22 * vl; break;
            case eLead:  v.a = 0.01; v.d = 0.1 / 3; v.sus = 0.7; v.rel = 0.15 / 4; v.peak = 0.2 * vl; v.k = 0.5f; break;
            case ePerc:  v.dur = 0.0; v.a = 0.0005; v.d = 0.35 / 7; v.sus = 0; v.rel = 0.05; v.peak = 0.6 * vl; break;
            case eObxa:  v.a = juce::jmax (0.001f, v.p.aa); v.d = v.p.ad / 3; v.sus = juce::jmax (0.0001f, v.p.as); v.rel = v.p.ar / 4; v.peak = 0.2 * vl;
                         v.endAt = v.dur + juce::jmax (v.p.ar, v.p.fr) * 1.75 + 0.05; return;
            default: break;
        }
        v.endAt = v.dur + v.rel * 7 + 0.02;
        if (v.engine == ePerc) v.endAt = 0.5;
    }
    float env (Voice& v) const
    {
        auto held = [&v] (double t) { return t < v.a ? v.peak * t / v.a : v.peak * (v.sus + (1 - v.sus) * std::exp (-(t - v.a) / v.d)); };
        if (v.t < v.dur || v.engine == ePerc) return (float) held (v.t);
        return (float) (held (v.dur) * std::exp (-(v.t - v.dur) / v.rel));
    }
    static float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1); }
        if (t > 1 - dt) { t = (t - 1) / dt; return (float) (t * t + t + t + 1); }
        return 0;
    }
    float saw (Voice& v, int i, double f, double dt) { const double inc = f * dt; v.ph[i] += inc; v.ph[i] -= std::floor (v.ph[i]); return (float) (2 * v.ph[i] - 1) - blep (v.ph[i], inc); }
    float sqr (Voice& v, int i, double f, double dt)
    {
        const double inc = f * dt; v.ph[i] += inc; v.ph[i] -= std::floor (v.ph[i]);
        double p2 = v.ph[i] + 0.5; p2 -= std::floor (p2);
        return (v.ph[i] < 0.5 ? 1.0f : -1.0f) + blep (v.ph[i], inc) - blep (p2, inc);
    }
    float sine (Voice& v, int i, double f, double dt) { v.ph[i] += f * dt; v.ph[i] -= std::floor (v.ph[i]); return (float) std::sin (v.ph[i] * juce::MathConstants<double>::twoPi); }
    float tri (Voice& v, int i, double f, double dt) { v.ph[i] += f * dt; v.ph[i] -= std::floor (v.ph[i]); return (float) (4 * std::abs (v.ph[i] - 0.5) - 1); }
    float pulse (Voice& v, int i, double f, double dt, double pw)
    {
        const double inc = f * dt; v.ph[i] += inc; v.ph[i] -= std::floor (v.ph[i]);
        double p2 = v.ph[i] + (1 - pw); p2 -= std::floor (p2);
        return (v.ph[i] < pw ? 1.0f : -1.0f) + blep (v.ph[i], inc) - blep (p2, inc);
    }
    static float rnd (juce::uint32& seed) { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }
    // one-pole helpers for the drums
    static float lp1 (float x, float& z, float c) { z += (x - z) * c; return z; }
    float coef (double hz) const { return 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (hz, sr * 0.45) / sr); }
    float drumTick (DrumVoice& v)
    {
        const double t = v.t, dt = 1.0 / sr, tau = juce::MathConstants<double>::twoPi; const auto& K = v.k; float s = 0, len = 1;
        auto dec = [t] (double d) { return (float) std::exp (-t / juce::jmax (0.001, d / 6.9)); };   // reaches -60 dB at d
        auto sq = [] (double p) { return p - std::floor (p) < 0.5 ? 1.0f : -1.0f; };
        auto tri = [] (double p) { p -= std::floor (p); return (float) (4 * std::abs (p - 0.5) - 1); };
        switch (v.drum)
        {
            case dKick:
            {
                const double f = K.kf + (K.kp - K.kf) * std::exp (-t / 0.0115);
                v.ph += f * dt; s = (float) std::sin (v.ph * tau) * 0.9f * dec (K.kd);
                if (t < 0.02) { const float nz = rnd (v.seed); s += (nz - lp1 (nz, v.z1, coef (2500))) * 0.35f * K.kc * dec (0.012); }
                len = K.kd + 0.1f; break;
            }
            case dSnare:
            {
                v.ph += K.sf * dt; v.ph2 += K.sf * 1.47 * dt;
                s = (tri (v.ph) * 0.32f + tri (v.ph2) * 0.22f) * dec (0.11);
                const float nz = rnd (v.seed); s += (nz - lp1 (nz, v.z1, coef (1500))) * 0.45f * K.ss * dec (K.sd);
                len = juce::jmax (0.25f, K.sd + 0.05f); break;
            }
            case dClosedHat: case dOpenHat:
            {
                const float d = v.drum == dClosedHat ? K.hd : K.ho, nz = rnd (v.seed), h = nz - lp1 (nz, v.z1, coef (K.ht));
                s = (h - lp1 (h, v.z2, coef (K.ht * 2.5)) * 0.4f) * 0.32f * dec (d);
                len = d + 0.05f; break;
            }
            case dClap:
            {
                const float nz = rnd (v.seed), bp = lp1 (nz - lp1 (nz, v.z1, coef (800)), v.z2, coef (1800));
                float e = 0;
                for (double k : { 0.0, 0.011, 0.022 }) if (t >= k && t < k + 0.011) e = juce::jmax (e, (float) std::exp (-(t - k) / 0.004));
                if (t >= 0.033) e = 0.8f * (float) std::exp (-(t - 0.033) / juce::jmax (0.001, K.cd / 6.9));
                s = bp * 1.1f * e; len = K.cd + 0.08f; break;
            }
            case dTom:
            {
                const double f = K.tf + (K.tf * 0.3) * std::exp (-t / 0.015);
                v.ph += f * dt; s = (float) std::sin (v.ph * tau) * 0.6f * dec (K.td);
                len = K.td + 0.1f; break;
            }
            case dRim: { v.ph += K.rf * dt; const float x = sq (v.ph); s = (x - lp1 (x, v.z1, coef (K.rf * 0.8))) * 0.22f * dec (0.03); len = 0.06f; break; }
            case dPerc:
            {
                v.ph += K.pf * dt; v.ph2 += K.pf * K.pr * dt;
                const float m = sq (v.ph) + sq (v.ph2), b = lp1 (m - lp1 (m, v.z1, coef (K.pf * 0.7)), v.z2, coef (K.pf * K.pr * 1.4));
                s = b * 0.16f * dec (K.pd); len = K.pd + 0.05f; break;
            }
            case dCymbal:
            {
                // six detuned squares (the 808 trick) and bright noise, high-passed
                static const double ratios[6] = { 1, 1.342, 1.2312, 1.6532, 1.9523, 2.1523 };
                float m = 0; for (size_t k = 0; k < 6; ++k) { v.mp[k] += K.ct / 13.0 * ratios[k] * dt; m += sq (v.mp[k]); }
                const float x = m * 0.12f + rnd (v.seed) * 0.6f, h = x - lp1 (x, v.z1, coef (K.ct));
                s = (h - lp1 (h, v.z2, coef (K.ct * 2.2)) * 0.3f) * 0.2f * dec (K.cy); len = K.cy + 0.05f; break;
            }
            case dShaker:
            {
                const float nz = rnd (v.seed), h = nz - lp1 (nz, v.z1, coef (6000));
                const float env = t < 0.012 ? (float) (t / 0.012) : dec (K.shd);
                s = h * 0.24f * env; len = K.shd + 0.03f; break;
            }
            case dConga:
            {
                const double f = K.cgf + K.cgf * 0.12 * std::exp (-t / 0.008);
                v.ph += f * dt; v.ph2 += K.cgf * 2.7 * dt;
                s = (float) std::sin (v.ph * tau) * 0.55f * dec (K.cgd) + (float) std::sin (v.ph2 * tau) * 0.1f * dec (0.03);
                len = K.cgd + 0.1f; break;
            }
            case dTambourine:
            {
                const float nz = rnd (v.seed), h = nz - lp1 (nz, v.z1, coef (7000));
                float e = 0;
                for (double k : { 0.0, 0.006, 0.013 }) if (t >= k && t < k + 0.006) e = juce::jmax (e, (float) std::exp (-(t - k) / 0.002));
                if (t >= 0.018) e = 0.8f * (float) std::exp (-(t - 0.018) / juce::jmax (0.001, K.tbd / 6.9));
                s = (h - lp1 (h, v.z2, coef (12000)) * 0.5f) * 0.3f * e; len = K.tbd + 0.04f; break;
            }
            default: break;
        }
        v.t += dt;
        if (v.t > len) v.on = false;
        return s * v.vel;
    }
    float noise (Voice& v) { v.seed = v.seed * 1664525u + 1013904223u; return (float) (v.seed >> 8) / 8388608.0f - 1.0f; }
    // Cytomic state-variable lowpass
    float lowpass (Voice& v, float x, double cutoff)
    {
        if (v.coefCount-- <= 0) { v.coefCount = 8; v.g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, cutoff) / sr); }
        const float a1 = 1.0f / (1.0f + v.g * (v.g + v.k)), a2 = v.g * a1, a3 = v.g * a2;
        const float v3 = x - v.ic2, v1 = a1 * v.ic1 + a2 * v3, v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
        v.ic1 = 2 * v1 - v.ic1; v.ic2 = 2 * v2 - v.ic2;
        return v2;
    }
    float tick (Voice& v, double dt)
    {
        const double t = v.t, f = v.f; float s = 0;
        switch (v.engine)
        {
            case ePluck: s = lowpass (v, 0.5f * (saw (v, 0, f, dt) + sqr (v, 1, f * 1.003, dt)), f * 1.2 + f * 10 * (0.5 + v.vel) * std::exp (-t / 0.08)); break;
            case ePad:   s = lowpass (v, 0.4f * (saw (v, 0, f * 0.99596, dt) + saw (v, 1, f, dt) + saw (v, 2, f * 1.00405, dt)), f * (3 + 3 * juce::jmin (1.0, t / juce::jmax (0.1, v.dur)))); break;
            case eBell:  { const double idx = f * 0.2 + (f * 4 * v.vel - f * 0.2) * std::exp (-t / 0.35); s = (float) std::sin ((v.ph[0] += f * dt) * juce::MathConstants<double>::twoPi + idx / (f * 3.5) * sine (v, 1, f * 3.5, dt)); break; }
            case eBass:  s = lowpass (v, saw (v, 0, f, dt), f * 1.5 + f * 8 * std::exp (-t / 0.06)) + 0.6f * sine (v, 1, f * 0.5, dt); break;
            case eGlass: s = tri (v, 0, f, dt) + 0.3f * sine (v, 1, f * 2.01, dt); break;
            case eLead:  { const double vib = 1 + 0.008 * juce::jmin (1.0, t / 0.3) * std::sin (t * 5.5 * juce::MathConstants<double>::twoPi); s = lowpass (v, saw (v, 0, f * vib, dt), f * 5); break; }
            case eObxa:
            {
                const auto& P = v.p;
                v.lfoPh += P.lfo * dt; if (v.lfoPh >= 1) v.lfoPh -= 1;
                const double lfo = std::sin (v.lfoPh * juce::MathConstants<double>::twoPi);
                static const int iv[5] = { 0, 7, 12, -12, 5 };
                const double vib = std::pow (2.0, P.vib * lfo / 1200.0), f1 = f * vib * std::pow (2.0, -P.det / 2400.0);
                const double f2 = f * vib * std::pow (2.0, (iv[juce::jlimit (0, 4, (int) P.int2)] * 100.0 + P.det / 2) / 1200.0);
                const double pw = juce::jlimit (0.05, 0.95, (double) P.pw + P.pwm * lfo);
                auto osc = [&] (int i, float wave, double fr) { return wave > 0.5f ? pulse (v, i, fr, dt, pw) : saw (v, i, fr, dt); };
                const float x = 0.5f * (osc (0, P.w1, f1) + P.mix2 * osc (1, P.w2, f2));
                // filter: key-tracked cutoff with its own ADSR, in octaves
                auto adsr = [&] (double A, double D, double S, double Rl) {
                    auto h = [&] (double tt) { return tt < A ? tt / A : S + (1 - S) * std::exp (-(tt - A) / juce::jmax (0.001, D / 3)); };
                    return t < v.dur ? h (t) : h (v.dur) * std::exp (-(t - v.dur) / juce::jmax (0.001, Rl / 4)); };
                const double fe = adsr (juce::jmax (0.001f, P.fa), P.fd, P.fs, P.fr) * (0.6 + 0.4 * v.vel);
                const double cut = juce::jmin (sr * 0.45, P.cut * std::pow (2.0, std::log2 (f / 261.63) * 0.5 + P.env * fe));
                if (v.coefCount-- <= 0) { v.coefCount = 8; v.g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, cut) / sr); }
                const float q = P.poles > 0.5f ? 0.6f + P.res * 14.0f : 0.6f + P.res * 12.0f;
                auto stage = [&] (float in, float& c1, float& c2, float kk) {
                    const float a1 = 1.0f / (1.0f + v.g * (v.g + kk)), a2 = v.g * a1, a3 = v.g * a2;
                    const float v3 = in - c2, v1 = a1 * c1 + a2 * v3, v2 = c2 + a2 * c1 + a3 * v3; c1 = 2 * v1 - c1; c2 = 2 * v2 - c2; return v2; };
                s = P.poles > 0.5f ? stage (stage (x, v.ic1, v.ic2, 1.0f / 0.7f), v.jc1, v.jc2, 1.0f / q) : stage (x, v.ic1, v.ic2, 1.0f / q);
                s = std::tanh (s * 1.2f);
                break;
            }
            case ePerc:
            {
                const double pf = f / 2 + (f * 2 - f / 2) * std::exp (-t / 0.04);
                s = sine (v, 0, pf, dt);
                // a click of high noise on top
                const float nz = noise (v), hp = nz - v.hp1; v.hp1 = nz;
                s += (float) (0.4 * std::exp (-t / 0.015)) * hp;
                break;
            }
            default: break;
        }
        const float e = env (v);
        v.t += dt;
        if (v.t > v.endAt) v.on = false;
        return s * e;
    }
};
} // namespace sky
