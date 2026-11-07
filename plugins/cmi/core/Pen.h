// The light pen (Page 5): two waveform cycles, the start and the end of a waveform voice that morphs from one to the
// other (as the CMI's Mode 1 voices do). The drawing, its tools, the voice it makes, and saving it.
#pragma once
#include "Library.h"
#include <random>
#include <sstream>

namespace cmi {

struct PenDrawing {
    float c[2][PAGE] {};   // start, end; -1..1

    // a stroke from (x0,v0) to (x1,v1), x in 0..127
    void line(int box, int x0, float v0, int x1, float v1) {
        box = clampi(box, 0, 1); x0 = clampi(x0, 0, PAGE - 1); x1 = clampi(x1, 0, PAGE - 1);
        if (x0 == x1) { c[box][x0] = std::clamp(v1, -1.f, 1.f); return; }
        const int a = std::min(x0, x1), b = std::max(x0, x1);
        for (int i = a; i <= b; ++i) c[box][i] = std::clamp(v0 + (v1 - v0) * (float) (i - x0) / (float) (x1 - x0), -1.f, 1.f);
    }
    // a shape: 0 sine, 1 saw, 2 square, 3 triangle, 4 noise, 5 clear, 6 pulse 25%, 7 organ (1+2+3 harmonics)
    void shape(int box, int kind, uint32_t seed = 1) {
        std::mt19937 rng(seed); std::uniform_real_distribution<float> u(-1, 1);
        for (int i = 0; i < PAGE; ++i) {
            const float ph = i / (float) PAGE, w = 2 * (float) PI * ph;
            float v = 0;
            switch (kind) {
                case 0: v = std::sin(w); break; case 1: v = 1 - 2 * ph; break; case 2: v = ph < 0.5f ? 1.f : -1.f; break;
                case 3: v = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph; break; case 4: v = u(rng); break; case 6: v = ph < 0.25f ? 1.f : -0.33f; break;
                case 7: v = (std::sin(w) + 0.6f * std::sin(2 * w) + 0.4f * std::sin(3 * w)) / 1.6f; break; default: v = 0; break;
            }
            c[clampi(box, 0, 1)][i] = v;
        }
    }
    // smooth (a little low-pass, round the cycle), normalise to full height, invert, reverse, copy
    void smooth(int box) { float o[PAGE]; for (int i = 0; i < PAGE; ++i) o[i] = 0.25f * c[box][(i + PAGE - 1) % PAGE] + 0.5f * c[box][i] + 0.25f * c[box][(i + 1) % PAGE]; std::copy(o, o + PAGE, c[box]); }
    void normalise(int box) { float pk = 1e-6f; for (float v : c[box]) pk = std::max(pk, std::fabs(v)); for (float& v : c[box]) v /= pk; }
    void invert(int box) { for (float& v : c[box]) v = -v; }
    void reverse(int box) { std::reverse(c[box], c[box] + PAGE); }
    void copy(int from, int to) { std::copy(c[from], c[from] + PAGE, c[to]); }
    bool silent() const { for (int b = 0; b < 2; ++b) for (float v : c[b]) if (std::fabs(v) > 1e-4f) return false; return true; }

    Sound voice(const std::string& name) const { return drawnVoice(name, c[0], c[1]); }

    // a drawing from any voice: its first page as the start, its last as the end (a waveform voice comes back exactly)
    bool fromSound(const Sound& s) {
        if (s.ram.size() < (size_t) PAGE) return false;
        const int last = std::max(0, (int) s.ram.size() / PAGE - 1);
        const int pages[2] = { 0, s.mode == Mode::Waveform ? std::min(last, 127) : last };
        for (int b = 0; b < 2; ++b) for (int i = 0; i < PAGE; ++i) c[b][i] = ((int) s.ram[(size_t) (pages[b] * PAGE + i)] - 128) / 127.f;
        return true;
    }
    // as text (kept with the project)
    std::string save() const {
        std::ostringstream o; o.precision(4);
        for (int b = 0; b < 2; ++b) for (int i = 0; i < PAGE; ++i) o << c[b][i] << ((b == 1 && i == PAGE - 1) ? "" : " ");
        return o.str();
    }
    void load(const std::string& s) { std::istringstream in(s); for (int b = 0; b < 2; ++b) for (int i = 0; i < PAGE; ++i) { float v = 0; if (in >> v) c[b][i] = std::clamp(v, -1.f, 1.f); } }
};

// the drawing as the live PEN voice in a user slot (the same one each time, a free one the first time); the slot or -1
inline int penToSlot(Kit& kit, const PenDrawing& pen, int slot) {
    if (pen.silent()) return slot;
    if (slot < 0 || !kit.userLoaded(slot) || kit.user(slot)->name != "PEN") slot = kit.firstFree();
    if (slot < 0) return -1;
    kit.setUser(slot, pen.voice("PEN"));
    return slot;
}
// the drawing as a new voice "DRAWn"; the slot or -1
inline int penSaveAs(Kit& kit, const PenDrawing& pen, std::string& name) {
    const int slot = kit.firstFree(); if (slot < 0) return -1;
    int n = 1; for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->name.rfind("DRAW", 0) == 0) ++n;
    name = "DRAW" + std::to_string(n);
    kit.setUser(slot, pen.voice(name));
    return slot;
}

// any voice as a WAV file: its 16 KB at the rate its root key plays at, 16-bit, with a smpl chunk (root key and
// loop) so samplers (and this app's Page 8) loop it as the CMI does
inline std::vector<uint8_t> voiceWav(const Sound& s) {
    const int n = (int) s.ram.size();
    const double rate = s.mode == Mode::Waveform ? 128.0 * 440.0 * std::pow(2.0, (60 - 69) / 12.0) : s.rate;   // a waveform voice: middle C
    const int root = s.mode == Mode::Waveform ? 60 : s.root;
    const uint32_t fs = (uint32_t) std::lround(rate), data = (uint32_t) n * 2;
    const bool loop = s.loopStart >= 0 && s.loopEnd >= s.loopStart;
    std::vector<uint8_t> o;
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((uint8_t) (v >> (8 * k))); };
    auto u16 = [&](uint16_t v) { o.push_back((uint8_t) v); o.push_back((uint8_t) (v >> 8)); };
    auto tag = [&](const char* t) { o.insert(o.end(), t, t + 4); };
    const uint32_t smplSize = 36 + (loop ? 24 : 0);
    tag("RIFF"); u32(4 + 24 + 8 + smplSize + 8 + data); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1); u32(fs); u32(fs * 2); u16(2); u16(16);
    tag("smpl"); u32(smplSize); u32(0); u32(0); u32((uint32_t) std::lround(1e9 / rate)); u32((uint32_t) root); u32(0); u32(0); u32(0); u32(loop ? 1 : 0); u32(0);
    if (loop) { u32(0); u32(0); u32((uint32_t) (s.loopStart * PAGE)); u32((uint32_t) std::min(n - 1, (s.loopEnd + 1) * PAGE - 1)); u32(0); u32(0); }
    tag("data"); u32(data);
    for (int i = 0; i < n; ++i) u16((uint16_t) (int16_t) (((int) s.ram[(size_t) i] - 128) * 256));
    return o;
}

}  // namespace cmi
