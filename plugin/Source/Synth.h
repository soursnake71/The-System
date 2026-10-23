#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/* The built-in sound engines, the same seven as the web app (sky/sound.js), plus a shared reverb and tempo delay. */
namespace sky
{
enum Engine { ePluck, ePad, eBell, eBass, eGlass, eLead, ePerc, eMidiOnly, numEngines };
inline juce::StringArray engineNames() { return { "Pluck", "Pad", "FM Bell", "Bass", "Glass", "Lead", "Perc", "MIDI only" }; }

class Synth
{
public:
    static constexpr int numLanes = 7;   // six lanes + the emergency alarm
    std::array<float, numLanes> laneVol {}, laneRev {}, laneDly {};

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices) v.on = false;
        juce::Reverb::Parameters p; p.roomSize = 0.82f; p.damping = 0.45f; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.width = 1.0f;
        reverb.setParameters (p); reverb.setSampleRate (sr); reverb.reset();
        dlL.assign ((size_t) (sr * 2.0) + 1, 0.0f); dlR.assign (dlL.size(), 0.0f); dlPos = 0; dlLp = { 0, 0 };
        sendRev.setSize (2, maxBlock); sendDly.setSize (2, maxBlock);
    }
    void setBpm (double bpm) { delaySamples = juce::jlimit (1.0, sr * 1.9, sr * 45.0 / bpm); }   // a dotted eighth
    void allOff() { for (auto& v : voices) if (v.on && v.t < v.dur) v.dur = v.t; }

    // start a note `offset` samples into the next render call
    void noteOn (int engine, int lane, int note, float vel, double durSec, float pan, int offset)
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
        setShape (*v);
    }

    void render (juce::AudioBuffer<float>& out, int n)
    {
        out.clear (0, n); sendRev.clear (0, n); sendDly.clear (0, n);
        auto *L = out.getWritePointer (0), *R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        auto *rl = sendRev.getWritePointer (0), *rr = sendRev.getWritePointer (1), *dl = sendDly.getWritePointer (0), *dr = sendDly.getWritePointer (1);
        const double dt = 1.0 / sr;
        for (auto& v : voices)
        {
            if (! v.on) continue;
            ++v.age;
            const float vol = laneVol[(size_t) v.lane], rv = laneRev[(size_t) v.lane], dv = laneDly[(size_t) v.lane];
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                const float s = tick (v, dt) * vol;
                L[i] += s * v.gl; if (R) R[i] += s * v.gr;
                rl[i] += s * v.gl * rv; rr[i] += s * v.gr * rv; dl[i] += s * v.gl * dv; dr[i] += s * v.gr * dv;
                if (! v.on) break;
            }
        }
        // tempo delay, cross-fed for width, darker each repeat; it also feeds the reverb
        const int N = (int) dlL.size();
        for (int i = 0; i < n; ++i)
        {
            const double rp = dlPos - delaySamples + N;
            const int i0 = (int) rp % N, i1 = (i0 + 1) % N; const float fr = (float) (rp - std::floor (rp));
            const float yl = dlL[(size_t) i0] + (dlL[(size_t) i1] - dlL[(size_t) i0]) * fr, yr = dlR[(size_t) i0] + (dlR[(size_t) i1] - dlR[(size_t) i0]) * fr;
            const float c = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2800.0f / (float) sr);
            dlLp[0] += (yl - dlLp[0]) * c; dlLp[1] += (yr - dlLp[1]) * c;
            dlL[(size_t) dlPos] = dl[i] * 0.5f + dlLp[1] * 0.38f; dlR[(size_t) dlPos] = dr[i] * 0.5f + dlLp[0] * 0.38f;
            dlPos = (dlPos + 1) % N;
            L[i] += dlLp[0]; if (R) R[i] += dlLp[1];
            rl[i] = rl[i] * 0.5f + dlLp[0] * 0.3f; rr[i] = rr[i] * 0.5f + dlLp[1] * 0.3f;
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
    };
    std::array<Voice, 64> voices;
    double sr = 48000, delaySamples = 18000;
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
