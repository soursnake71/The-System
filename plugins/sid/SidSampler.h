// Omega SID 64: sampling into the C64. A source sound (a file, a recording, or a built-in) is brought down to what a
// C64 would hold: mono, band-limited and resampled to the NMI rate, quantised to 8 or 4 bits, and optionally cut
// to what fits in 64K of RAM (4-bit samples packed two to a byte, as C64 players store them). The chip model
// (SidChip.h) then plays it the way C64 code does: register writes at the timer rate, in cycles.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sid {

// the converted sample, handed to the audio thread
struct SampleData {
    std::vector<uint8_t> d;   // one byte per sample (4-bit samples use the top nibble), 0..255, 128 = silence
    double rate = 8000;       // the rate it was converted at (the NMI rate at the root key)
    int bits = 8;
    std::string name;
};

// digi playback methods, the parameter "dmode"
enum DigiMethod { DIGI_OFF = 0, DIGI_VOL4, DIGI_MAHONEY, DIGI_PWM, DIGI_DIGIMAX };
inline const double* digiRates() { static const double r[10] = { 3906, 4000, 5512, 6250, 7812, 8000, 10000, 11025, 16000, 22050 }; return r; }

namespace builtin {
constexpr double FS = 22050;
inline float noise(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 8388608.f - 1.f; }
struct Reso {   // a two-pole resonator
    double a1 = 0, a2 = 0, b0 = 0, y1 = 0, y2 = 0;
    Reso(double f, double bw) { const double r = std::exp(-3.14159265 * bw / FS); a1 = 2 * r * std::cos(2 * 3.14159265 * f / FS); a2 = r * r; b0 = 1 - r; }
    double operator()(double x) { const double y = b0 * x + a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
};
inline const char* const* names() {
    static const char* n[12] = { "KICK", "SNARE", "CLAP", "HIHAT", "OPEN HAT", "TOM", "COWBELL", "ZAP", "BASS", "ORCH HIT", "VOX AH", "VOX OH" };
    return n;
}
constexpr int count = 12;
inline std::vector<float> make(int which) {
    std::vector<float> o;
    uint32_t s = 0x1234567u + (uint32_t) which;
    const double tau = 2 * 3.14159265358979;
    auto len = [&](double sec) { o.assign((size_t) (sec * FS), 0.f); };
    double ph = 0;
    switch (which) {
        case 0: len(0.45); for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS, f = 45 + 110 * std::exp(-t * 28); ph += tau * f / FS; o[i] = (float) (std::sin(ph) * std::exp(-t * 7) + (t < 0.004 ? 0.5 * noise(s) : 0)); } break;
        case 1: { len(0.3); double hp = 0, lx = 0; for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; ph += tau * 190 / FS; const double n = noise(s); hp = 0.8 * (hp + n - lx); lx = n; o[i] = (float) (0.7 * hp * std::exp(-t * 14) + 0.6 * std::sin(ph) * std::exp(-t * 25)); } break; }
        case 2: { len(0.3); Reso r(1200, 600); for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; double e = t < 0.03 ? (std::fmod(t, 0.01) < 0.004 ? 1 : 0.2) : std::exp(-(t - 0.03) * 16); o[i] = (float) (r(noise(s)) * 6 * e); } break; }
        case 3: case 4: { len(which == 3 ? 0.08 : 0.4); double hp = 0, lx = 0; const double dk = which == 3 ? 60 : 9; for (size_t i = 0; i < o.size(); ++i) { const double n = noise(s); hp = 0.5 * (hp + n - lx); lx = n; o[i] = (float) (hp * std::exp(-i / FS * dk)); } break; }
        case 5: len(0.5); for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; ph += tau * (90 + 50 * std::exp(-t * 12)) / FS; o[i] = (float) (std::sin(ph) * std::exp(-t * 6)); } break;
        case 6: { len(0.35); double p2 = 0; Reso r(800, 300); for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; ph += 540 / FS; p2 += 800 / FS; const double x = (std::fmod(ph, 1.0) < .5 ? 1 : -1) + (std::fmod(p2, 1.0) < .5 ? 1 : -1); o[i] = (float) (r(x) * 3 * std::exp(-t * 9)); } break; }
        case 7: len(0.3); for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; ph += tau * (100 + 2400 * std::exp(-t * 14)) / FS; o[i] = (float) (std::sin(ph) * std::exp(-t * 5)); } break;
        case 8: { len(0.7); double lp = 0; for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; ph += 55 / FS; const double x = 2 * std::fmod(ph, 1.0) - 1; const double k = 0.02 + 0.3 * std::exp(-t * 8); lp += k * (x - lp); o[i] = (float) (lp * std::exp(-t * 3)); } break; }
        case 9: { len(0.7); const double f[4] = { 261.6, 329.6, 392.0, 523.3 }; double p[4] {}; double lp = 0; for (size_t i = 0; i < o.size(); ++i) { const double t = i / FS; double x = 0; for (int k = 0; k < 4; ++k) { p[k] += f[k] / FS; x += 2 * std::fmod(p[k], 1.0) - 1; } lp += 0.35 * (x - lp); o[i] = (float) ((lp * 0.4 + (t < 0.02 ? noise(s) * 0.6 : 0)) * std::exp(-t * 5)); } break; }
        default: {   // a glottal pulse through three formants
            len(0.8); const bool ah = which == 10;
            Reso f1(ah ? 700 : 450, 80), f2(ah ? 1220 : 800, 90), f3(ah ? 2600 : 2830, 120);
            for (size_t i = 0; i < o.size(); ++i) {
                const double t = i / FS; ph += (110 + 3 * std::sin(tau * 5 * t)) / FS;
                const double fr = std::fmod(ph, 1.0); const double g = fr < 0.4 ? std::sin(3.14159 * fr / 0.4) : 0;
                const double y = f1(g) * 1.0 + f2(g) * 0.6 + f3(g) * 0.25;
                const double env = std::min(1.0, t * 20) * (t > 0.6 ? std::exp(-(t - 0.6) * 15) : 1);
                o[i] = (float) (y * env * 4);
            }
            break;
        }
    }
    return o;
}
}  // namespace builtin

// the source on the UI side, and the conversion "into the C64"
class SampleSource {
public:
    std::vector<float> src; double srcRate = 22050; std::string name;
    std::vector<uint8_t> preview;   // the last conversion (for the screen)
    double previewRate = 8000; int previewBits = 8;
    int builtinIndex = -1;

    bool empty() const { return src.empty(); }
    void set(std::vector<float> mono, double rate, const std::string& nm) {
        src = std::move(mono); srcRate = rate; name = nm; builtinIndex = -1;
        // trim near-silence at the ends, then normalise
        size_t a = 0, b = src.size(); float pk = 0;
        for (float x : src) pk = std::max(pk, std::fabs(x));
        const float th = pk * 0.01f;
        while (a < b && std::fabs(src[a]) < th) ++a;
        while (b > a && std::fabs(src[b - 1]) < th) --b;
        src = std::vector<float>(src.begin() + (long) a, src.begin() + (long) b);
        if (pk > 0) for (auto& x : src) x /= pk;
    }
    void loadBuiltin(int i) {
        i = ((i % builtin::count) + builtin::count) % builtin::count;
        set(builtin::make(i), builtin::FS, builtin::names()[i]);
        builtinIndex = i;
    }

    // convert: band-limit and resample (windowed sinc), quantise, apply the RAM limit
    SampleData* convert(double rate, int bits, bool ramLimit) {
        auto* out = new SampleData; out->rate = rate; out->bits = bits; out->name = name;
        if (src.empty()) return out;
        const double ratio = srcRate / rate;
        size_t n = (size_t) std::floor(src.size() / ratio);
        const size_t maxN = ramLimit ? (bits == 4 ? 2 : 1) * 60 * 1024 : (size_t) (rate * 30);   // 60K free of 64K
        n = std::min(n, maxN);
        out->d.resize(n);
        const double fc = 0.45 / std::max(1.0, ratio);   // cutoff relative to the source rate
        const int half = (int) std::ceil(8 * std::max(1.0, ratio));
        for (size_t i = 0; i < n; ++i) {
            const double c = i * ratio; const long ci = (long) c;
            double acc = 0, wsum = 0;
            for (long k = ci - half + 1; k <= ci + half; ++k) {
                if (k < 0 || k >= (long) src.size()) continue;
                const double x = k - c, w = 0.5 + 0.5 * std::cos(3.14159265 * x / (half + 1));
                const double sn = std::fabs(x) < 1e-9 ? 2 * fc : std::sin(2 * 3.14159265 * fc * x) / (3.14159265 * x);
                acc += src[(size_t) k] * sn * w; wsum += sn * w;
            }
            double y = std::fabs(wsum) > 1e-12 ? acc / wsum : 0.0;
            y = std::clamp(y, -1.0, 1.0);
            if (bits == 4) { const int q = (int) std::lround((y * 0.5 + 0.5) * 15); out->d[i] = (uint8_t) (q * 17); }
            else out->d[i] = (uint8_t) std::lround((y * 0.5 + 0.5) * 255);
        }
        preview = out->d; previewRate = rate; previewBits = bits;
        return out;
    }

    // saved with the project: the source at 22.05 kHz, 16-bit, up to 20 s, as base64
    std::string save() const {
        if (src.empty()) return {};
        const double ratio = srcRate / 22050.0;
        const size_t n = std::min((size_t) (src.size() / ratio), (size_t) (22050 * 20));
        std::string raw; raw.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) {
            const double p = i * ratio; const size_t k = (size_t) p; const double f = p - k;
            const double y = src[k] * (1 - f) + (k + 1 < src.size() ? src[k + 1] : 0) * f;
            const int16_t v = (int16_t) std::lround(std::clamp(y, -1.0, 1.0) * 32767);
            raw += (char) (v & 255); raw += (char) ((v >> 8) & 255);
        }
        return b64enc(raw);
    }
    void load(const std::string& b64, const std::string& nm) {
        const std::string raw = b64dec(b64);
        std::vector<float> v(raw.size() / 2);
        for (size_t i = 0; i < v.size(); ++i) v[i] = (int16_t) ((uint8_t) raw[2 * i] | ((uint8_t) raw[2 * i + 1] << 8)) / 32767.f;
        src = std::move(v); srcRate = 22050; name = nm; builtinIndex = -1;
    }

    static std::string b64enc(const std::string& in) {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o; o.reserve((in.size() + 2) / 3 * 4);
        for (size_t i = 0; i < in.size(); i += 3) {
            const uint32_t v = (uint32_t) (uint8_t) in[i] << 16 | (i + 1 < in.size() ? (uint32_t) (uint8_t) in[i + 1] << 8 : 0) | (i + 2 < in.size() ? (uint8_t) in[i + 2] : 0);
            o += T[v >> 18 & 63]; o += T[v >> 12 & 63];
            o += i + 1 < in.size() ? T[v >> 6 & 63] : '='; o += i + 2 < in.size() ? T[v & 63] : '=';
        }
        return o;
    }
    static std::string b64dec(const std::string& in) {
        auto val = [](char c) -> int { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+') return 62; if (c == '/') return 63; return -1; };
        std::string o; uint32_t buf = 0; int bitsIn = 0;
        for (char c : in) { const int v = val(c); if (v < 0) continue; buf = buf << 6 | (uint32_t) v; bitsIn += 6; if (bitsIn >= 8) { bitsIn -= 8; o += (char) (buf >> bitsIn & 255); } }
        return o;
    }
};

}  // namespace sid
