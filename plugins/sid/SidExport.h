// Omega SID 64: audio export. Renders the current pattern offline (a copy of the synth with the same settings,
// patterns and sample, so the one you're playing isn't disturbed), or takes what was recorded from the output, and
// writes it as WAV (16-bit with TPDF dither, or 24-bit) or MP3 (shine, third_party/shine, LGPL).
#pragma once
#include "SidSynth.h"
#include <functional>
#include <memory>

extern "C" {
#include "../third_party/shine/layer3.h"
}

namespace sid {

struct Audio { std::vector<float> lr; int fs = 44100; };   // interleaved stereo

// the pattern played `loops` times through a copy of the synth made from `state`, then up to `tail` seconds of what
// rings on (stopping early once it's silent). progress (0..1) is updated as it goes.
inline Audio renderPattern(const std::string& state, int loops, int fs, double tail, const std::function<void(float)>& progress = {}) {
    auto s = std::make_unique<Synth>();
    s->loadState(state);
    s->set(P::shost, 0); s->set(P::clksrc, 0); s->set(P::clkout, 0);
    s->prepare(fs);
    s->pollSample();
    const double beats = loops * std::clamp((int) std::lround(s->get(P::slen)), 1, kSteps) * [&] { static const double d[4] = { 0.5, 0.25, 1.0 / 6, 0.125 }; return d[std::clamp((int) std::lround(s->get(P::srate)), 0, 3)]; }();
    const long body = (long) std::lround(beats * 60.0 / std::max(20.0, (double) s->get(P::bpm)) * fs);
    const long maxTail = (long) (tail * fs);
    Audio a; a.fs = fs; a.lr.reserve((size_t) (body + maxTail) * 2);
    std::vector<float> L(512), R(512);
    s->seqPlay = true;
    long done = 0;
    while (done < body) {
        const int n = (int) std::min<long>(512, body - done);
        s->render(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) { a.lr.push_back(L[(size_t) i]); a.lr.push_back(R[(size_t) i]); }
        done += n;
        if (progress && (done & 0xFFFF) < 512) progress(0.9f * (float) done / (float) (body + maxTail));
    }
    s->seqPlay = false;
    long t = 0, quiet = 0;
    while (t < maxTail && quiet < fs / 2) {   // the tail: until half a second of silence
        s->render(L.data(), R.data(), 512);
        float pk = 0;
        for (int i = 0; i < 512; ++i) { a.lr.push_back(L[(size_t) i]); a.lr.push_back(R[(size_t) i]); pk = std::max({ pk, std::fabs(L[(size_t) i]), std::fabs(R[(size_t) i]) }); }
        quiet = pk < 1e-4f ? quiet + 512 : 0; t += 512;
    }
    if (progress) progress(0.9f);
    return a;
}

inline float peakOf(const Audio& a) { float p = 0; for (float x : a.lr) p = std::max(p, std::fabs(x)); return p; }
inline void normalize(Audio& a, float to = 0.966f) { const float p = peakOf(a); if (p > 1e-6f) for (float& x : a.lr) x *= to / p; }   // -0.3 dBFS

inline std::string encodeWav(const Audio& a, int bits) {
    bits = bits == 24 ? 24 : 16;
    const uint32_t frames = (uint32_t) (a.lr.size() / 2), bps = (uint32_t) bits / 8, data = frames * 2 * bps;
    std::string f;
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) f += (char) (v >> (8 * k) & 255); };
    auto u16 = [&](uint32_t v) { f += (char) (v & 255); f += (char) (v >> 8 & 255); };
    f += "RIFF"; u32(36 + data); f += "WAVEfmt "; u32(16); u16(1); u16(2); u32((uint32_t) a.fs); u32((uint32_t) a.fs * 2 * bps); u16(2 * bps); u16((uint32_t) bits);
    f += "data"; u32(data);
    f.reserve(f.size() + data);
    uint32_t seed = 22222;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float) seed / 4294967296.f; };
    const double q = bits == 24 ? 8388607.0 : 32767.0;
    for (float x : a.lr) {
        const double d = bits == 16 ? (double) (rnd() - rnd()) : 0.0;   // TPDF dither at 16 bits
        const long v = std::clamp(std::lround(std::clamp((double) x, -1.0, 1.0) * q + d), (long) -q - 1, (long) q);
        f += (char) (v & 255); f += (char) (v >> 8 & 255);
        if (bits == 24) f += (char) (v >> 16 & 255);
    }
    return f;
}

// MP3: MPEG-1 layer III at 32 / 44.1 / 48 kHz, stereo. Returns "" if the rate / bitrate pair isn't allowed.
inline std::string encodeMp3(const Audio& a, int kbps, const std::function<void(float)>& progress = {}) {
    if (shine_check_config(a.fs, kbps) < 0) return {};
    shine_config_t cfg {};
    shine_set_config_mpeg_defaults(&cfg.mpeg);
    cfg.wave.channels = PCM_STEREO; cfg.wave.samplerate = a.fs;
    cfg.mpeg.mode = STEREO; cfg.mpeg.bitr = kbps;
    shine_t sh = shine_initialise(&cfg);
    if (!sh) return {};
    const int spp = shine_samples_per_pass(sh);
    std::vector<int16_t> buf((size_t) spp * 2);
    std::string out;
    const size_t frames = a.lr.size() / 2;
    for (size_t f0 = 0; f0 < frames; f0 += (size_t) spp) {
        for (int i = 0; i < spp; ++i) {
            const size_t k = f0 + (size_t) i;
            for (int c = 0; c < 2; ++c) buf[(size_t) i * 2 + (size_t) c] = k < frames ? (int16_t) std::lround(std::clamp(a.lr[k * 2 + (size_t) c], -1.f, 1.f) * 32767.f) : 0;
        }
        int written = 0;
        const unsigned char* d = shine_encode_buffer_interleaved(sh, buf.data(), &written);
        if (d && written > 0) out.append((const char*) d, (size_t) written);
        if (progress && ((f0 / (size_t) spp) & 63) == 0) progress(0.9f + 0.1f * (float) f0 / (float) std::max<size_t>(1, frames));
    }
    int written = 0;
    const unsigned char* d = shine_flush(sh, &written);
    if (d && written > 0) out.append((const char*) d, (size_t) written);
    shine_close(sh);
    return out;
}

}  // namespace sid
