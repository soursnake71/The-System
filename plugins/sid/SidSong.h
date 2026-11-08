// Omega SID 64: songs. A standard MIDI file (format 0 or 1) becomes a song of up to eight parts, one for each
// instrument in it (a track and channel, or a channel); each part plays through its own SID synth with a preset
// picked from the file's General MIDI program (channel 10 gets a kit of SID drums), or any preset or patch you give it.
// The parts render as buses (Synth::busMode) and mix into the main synth's echo and REV7: each part has a fader,
// pan, mute, solo and echo and reverb sends, and its meter. Included at the end of SidSynth.h.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace sid {

inline int presetByName(const char* name) {
    for (int i = 0; i < presetCount(); ++i) if (std::string(PRESETS[i].name) == name) return i;
    return 0;
}

// General MIDI program (0-127) -> a SID preset
inline int gmPreset(int prog) {
    prog = std::clamp(prog, 0, 127);
    switch (prog) {
        case 32: case 33: case 34: case 35: return presetByName("FINGER BASS");
        case 36: case 37: return presetByName("SLAP BASS");
        case 38: case 39: return presetByName("FAT SAW BASS");
        case 80: return presetByName("SQUARE LEAD");
        case 81: return presetByName("SAW LEAD");
        case 87: return presetByName("STACK LEAD");
        case 89: return presetByName("WARM PAD");
        case 105: return presetByName("BANJO");
        case 114: return presetByName("STEEL DRUM");
        case 116: case 117: case 118: return presetByName("TOM");
        case 122: return presetByName("OCEAN");
        case 127: return presetByName("EXPLOSION");
        default: break;
    }
    static const char* fam[16] = { "E-PIANO 64", "VIBES", "ORGAN", "GUITAR", "FINGER BASS", "STRING PAD", "CHOIR 64", "SID BRASS",
                                   "CLARINET", "FLUTE 64", "SID LEAD", "WARM PAD", "SPACE PAD", "KOTO", "STEEL DRUM", "EXPLOSION" };
    return presetByName(fam[prog / 8]);
}
inline const char* gmFamily(int prog) {
    static const char* f[16] = { "PIANO", "CHROM", "ORGAN", "GUITAR", "BASS", "STRINGS", "ENSEMBL", "BRASS",
                                 "REED", "PIPE", "LEAD", "PAD", "SYNTH FX", "ETHNIC", "PERC", "SFX" };
    return f[std::clamp(prog, 0, 127) / 8];
}

// the SID drum kit for channel 10: a synth per sound, and the note each GM drum plays
enum { DK_KICK, DK_SNARE, DK_HATC, DK_HATO, DK_CLAP, DK_TOM, DK_CYM, DK_PERC, DK_COUNT };
inline const char* drumPresetName(int slot) {
    static const char* n[DK_COUNT] = { "KICK", "SNARE", "HIHAT CLOSED", "HIHAT OPEN", "CLAP", "TOM", "CYMBAL", "COWBELL" };
    return n[std::clamp(slot, 0, DK_COUNT - 1)];
}
inline void drumMap(int gm, int& slot, int& note) {
    switch (gm) {
        case 35: case 36: slot = DK_KICK; note = 36; return;
        case 37: case 38: case 40: slot = DK_SNARE; note = 48; return;
        case 39: slot = DK_CLAP; note = 60; return;
        case 42: case 44: slot = DK_HATC; note = 60; return;
        case 46: slot = DK_HATO; note = 60; return;
        case 41: case 43: case 45: case 47: case 48: case 50: slot = DK_TOM; note = gm + 2; return;
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: slot = DK_CYM; note = 60; return;
        default: slot = DK_PERC; note = 60 + (gm - 54) % 12; return;
    }
}

struct SongEvent {
    double t = 0;                   // seconds
    uint8_t part = 0, sub = 0;      // part, and the drum synth for a kit
    uint8_t type = 0;               // 0 off, 1 on, 2 bend, 3 sustain
    uint8_t note = 0, vel = 0;      // bend: 14-bit value in note (high 7) / vel (low 7)
};

struct SongPart {
    std::string name;
    int channel = 0, program = 0, notes = 0, lo = 127, hi = 0;
    bool drums = false;
    std::atomic<int> preset { 0 };                       // the preset it plays (-1: a patch from the keyboard)
    std::string patch;                                   // that patch, as key=value lines (UI thread)
    std::atomic<float> level { 0.75f }, pan { 0 }, echo { 0 }, rev { 0.15f };
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> meter { 0 };
    std::vector<std::unique_ptr<Synth>> syn;              // one, or DK_COUNT for a kit
    // audio thread
    float gl = 0, gr = 0, ge = 0, grv = 0;                // smoothed gains
};

class Song {
public:
    static constexpr int kParts = 8;
    std::string name = "SONG", file;            // file: the MIDI file's bytes, kept to save with the project
    double length = 0, bpm = 120;
    int dropped = 0;                            // instruments past kParts that were left out
    std::vector<SongEvent> ev;
    std::vector<std::unique_ptr<SongPart>> parts;
    std::atomic<bool> playing { false }, loop { true };
    std::atomic<double> position { 0 };        // seconds, for the screen
    std::atomic<double> seekTo { -1 };          // the UI asks the audio thread to move
    std::atomic<int> stopReq { 0 };

    // ---- reading a MIDI file (UI thread). "" when it worked, else what was wrong ----
    std::string load(const std::string& f, const std::string& nm) {
        auto u16 = [&](size_t i) { return (int) ((uint8_t) f[i] << 8 | (uint8_t) f[i + 1]); };
        auto u32 = [&](size_t i) { return (uint32_t) ((uint8_t) f[i] << 24 | (uint8_t) f[i + 1] << 16 | (uint8_t) f[i + 2] << 8 | (uint8_t) f[i + 3]); };
        if (f.size() < 14 || f.compare(0, 4, "MThd") != 0) return "NOT A MIDI FILE";
        const int division = u16(12);
        if (division <= 0 || (division & 0x8000)) return "SMPTE TIME NOT SUPPORTED";
        struct Raw { uint32_t tick; int track, ch, st, d1, d2; uint32_t ord; };
        std::vector<Raw> raw; std::vector<std::pair<uint32_t, uint32_t>> tempo;   // tick, us per quarter
        std::vector<std::string> trackName;
        size_t p = 8 + u32(4); int track = 0; uint32_t ord = 0;
        while (p + 8 <= f.size()) {
            const uint32_t len = u32(p + 4);
            const bool isTrack = f.compare(p, 4, "MTrk") == 0;
            size_t q = p + 8; const size_t end = std::min(f.size(), (size_t) (q + len));
            p = q + len;
            if (!isTrack) continue;
            trackName.emplace_back();
            uint32_t tick = 0; int run = 0;
            auto vlq = [&]() { uint32_t v = 0; for (int k = 0; k < 4 && q < end; ++k) { const uint8_t b = (uint8_t) f[q++]; v = v << 7 | (b & 127); if (!(b & 128)) break; } return v; };
            while (q < end) {
                tick += vlq(); if (q >= end) break;
                int st = (uint8_t) f[q];
                if (st == 0xFF) {
                    if (q + 2 > end) break;
                    const int type = (uint8_t) f[q + 1]; q += 2; const uint32_t l = vlq();
                    if (q + l > end) break;
                    if (type == 0x51 && l == 3) tempo.push_back({ tick, (uint32_t) ((uint8_t) f[q] << 16 | (uint8_t) f[q + 1] << 8 | (uint8_t) f[q + 2]) });
                    if ((type == 0x03 || (type == 0x04 && trackName.back().empty())) && l > 0) trackName.back() = f.substr(q, std::min<size_t>(l, 24));
                    q += l; if (type == 0x2F) break; continue;
                }
                if (st == 0xF0 || st == 0xF7) { ++q; const uint32_t l = vlq(); q += l; continue; }
                if (st & 0x80) { run = st; ++q; } else st = run;
                if (!(st & 0x80)) break;
                const int hi = st & 0xF0, nd = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                if (q + (size_t) nd > end) break;
                const int d1 = (uint8_t) f[q] & 127, d2 = nd > 1 ? (uint8_t) f[q + 1] & 127 : 0; q += (size_t) nd;
                raw.push_back({ tick, track, st & 15, hi, d1, d2, ord++ });
            }
            ++track;
        }
        if (raw.empty()) return "NO NOTES IN FILE";
        std::stable_sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
        std::sort(tempo.begin(), tempo.end());
        // ticks -> seconds through the tempo map
        auto sec = [&](uint32_t t) {
            double s = 0; uint32_t at = 0; double us = 500000;
            for (auto& m : tempo) { if (m.first >= t) break; s += (m.first - at) * us / division / 1e6; at = m.first; us = m.second; }
            return s + (t - at) * us / division / 1e6;
        };
        bpm = tempo.empty() ? 120.0 : 60e6 / std::max<uint32_t>(1, tempo.front().second);
        // instruments: by track and channel, or by channel when that makes too many
        auto groups = [&](bool byTrack) {
            std::vector<std::pair<int, int>> g;   // key, notes
            for (auto& r : raw) if (r.st == 0x90 && r.d2 > 0) {
                const int key = r.ch == 9 ? 9 : (byTrack ? r.track * 16 + r.ch : r.ch);
                auto it = std::find_if(g.begin(), g.end(), [&](auto& x) { return x.first == key; });
                if (it == g.end()) g.push_back({ key, 1 }); else ++it->second;
            }
            return g;
        };
        bool byTrack = true; auto g = groups(true);
        if ((int) g.size() > kParts) { byTrack = false; g = groups(false); }
        if (g.empty()) return "NO NOTES IN FILE";
        dropped = std::max(0, (int) g.size() - kParts);
        if (dropped) {
            std::stable_sort(g.begin(), g.end(), [](auto& a, auto& b) { return a.second > b.second; });
            g.resize(kParts);
        }
        std::sort(g.begin(), g.end(), [](auto& a, auto& b) { return (a.first == 9 ? 999 : a.first) < (b.first == 9 ? 999 : b.first); });
        auto partOf = [&](const Raw& r) {
            const int key = r.ch == 9 ? 9 : (byTrack ? r.track * 16 + r.ch : r.ch);
            for (size_t i = 0; i < g.size(); ++i) if (g[i].first == key) return (int) i;
            return -1;
        };
        parts.clear();
        for (auto& x : g) {
            auto sp = std::make_unique<SongPart>();
            sp->drums = x.first == 9;
            sp->channel = sp->drums ? 9 : (byTrack ? x.first % 16 : x.first);
            sp->notes = x.second;
            const int trk = byTrack && !sp->drums ? x.first / 16 : -1;
            // the program: the last change on its channel before its first note (or the first after)
            int prog = -1; bool seenNote = false; float vol = -1, pan = -2;
            for (auto& r : raw) {
                if (r.ch != sp->channel || (trk >= 0 && r.track != trk && r.st != 0xC0)) continue;
                if (r.st == 0x90 && r.d2 > 0) { seenNote = true; sp->lo = std::min(sp->lo, r.d1); sp->hi = std::max(sp->hi, r.d1); }
                if (r.st == 0xC0 && (!seenNote || prog < 0)) prog = r.d1;
                if (r.st == 0xB0 && !seenNote && r.d1 == 7) vol = r.d2 / 127.f;
                if (r.st == 0xB0 && !seenNote && r.d1 == 10) pan = (r.d2 - 64) / 63.f;
            }
            sp->program = std::max(0, prog);
            if (vol >= 0) sp->level = 0.75f * std::sqrt(vol) / std::sqrt(100.f / 127.f);
            sp->level = std::clamp(sp->level.load(), 0.f, 1.f);
            if (pan >= -1) sp->pan = std::clamp(pan, -1.f, 1.f);
            std::string tn = trk >= 0 && trk < (int) trackName.size() ? trackName[(size_t) trk] : std::string();
            std::string clean; for (char c : tn) { if (c >= 32 && c < 127) clean += (char) std::toupper((unsigned char) c); }
            while (!clean.empty() && clean.back() == ' ') clean.pop_back();
            while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
            sp->name = sp->drums ? "DRUMS" : (clean.empty() ? gmFamily(sp->program) : clean);
            sp->preset = sp->drums ? presetByName("KICK") : gmPreset(sp->program);
            if (sp->drums) sp->rev = 0.1f;
            parts.push_back(std::move(sp));
        }
        // the events
        ev.clear(); length = 0;
        struct Open { int part, sub, note; double t; };
        std::vector<Open> open;
        for (auto& r : raw) {
            const int pi = partOf(r); if (pi < 0) continue;
            const double t = sec(r.tick);
            const bool on = r.st == 0x90 && r.d2 > 0, off = r.st == 0x80 || (r.st == 0x90 && r.d2 == 0);
            if (on || off) {
                int sub = 0, note = r.d1;
                if (parts[(size_t) pi]->drums) drumMap(r.d1, sub, note);
                if (on) { ev.push_back({ t, (uint8_t) pi, (uint8_t) sub, 1, (uint8_t) note, (uint8_t) r.d2 }); open.push_back({ pi, sub, r.d1, t }); }
                else {
                    auto it = std::find_if(open.begin(), open.end(), [&](const Open& o) { return o.part == pi && o.note == r.d1; });
                    if (it == open.end()) continue;
                    open.erase(it);
                    ev.push_back({ t, (uint8_t) pi, (uint8_t) sub, 0, (uint8_t) note, 0 });
                }
                length = std::max(length, t);
            } else if (r.st == 0xE0 && !parts[(size_t) pi]->drums) ev.push_back({ t, (uint8_t) pi, 0, 2, (uint8_t) r.d2, (uint8_t) r.d1 });
            else if (r.st == 0xB0 && r.d1 == 64 && !parts[(size_t) pi]->drums) ev.push_back({ t, (uint8_t) pi, 0, 3, (uint8_t) (r.d2 >= 64), 0 });
        }
        for (auto& o : open) { int sub = 0, note = o.note; if (parts[(size_t) o.part]->drums) drumMap(o.note, sub, note); ev.push_back({ length + 0.25, (uint8_t) o.part, (uint8_t) sub, 0, (uint8_t) note, 0 }); }
        // offs before ons at the same time, so a repeated note retriggers
        std::stable_sort(ev.begin(), ev.end(), [](const SongEvent& a, const SongEvent& b) { return a.t < b.t || (a.t == b.t && a.type < b.type); });
        // the length: on to the end of the bar the last note ends in (4/4 at the first tempo)
        const double bar = 4 * 60.0 / std::max(20.0, bpm);
        length = std::max(bar, std::ceil((length - 0.01) / bar) * bar);
        name = nm; file = f;
        for (size_t i = 0; i < parts.size(); ++i) build((int) i);
        return "";
    }

    // ---- parts (UI thread) ----
    void build(int i) {
        SongPart& sp = *parts[(size_t) i];
        sp.syn.clear();
        const int n = sp.drums ? DK_COUNT : 1;
        for (int k = 0; k < n; ++k) { auto s = std::make_unique<Synth>(); s->busMode = true; sp.syn.push_back(std::move(s)); }
        applyPreset(i, sp.preset.load());
        if (fs > 0) for (auto& s : sp.syn) s->prepare(fs);
    }
    // the part's sound: a preset, or (-1) the patch in sp.patch
    void applyPreset(int i, int preset) {
        SongPart& sp = *parts[(size_t) i];
        if (sp.drums) {
            for (int k = 0; k < (int) sp.syn.size(); ++k) { sp.syn[(size_t) k]->applyPreset(presetByName(drumPresetName(k))); fixUp(*sp.syn[(size_t) k], 2); }
            return;
        }
        Synth& s = *sp.syn[0];
        if (preset >= 0) { s.applyPreset(preset); sp.patch.clear(); }
        else s.loadState(sp.patch);
        sp.preset = preset;
        fixUp(s, 4);
    }
    // a part takes the keyboard's sound (everything a preset sets, and its sample)
    void takePatch(int i, const Synth& from) {
        SongPart& sp = *parts[(size_t) i];
        if (sp.drums) return;
        std::string st;
        for (int k = 0; k < NP; ++k) if (params()[k].preset) { char b[64]; std::snprintf(b, sizeof b, "%s=%.6g\n", params()[k].key, from.get(k)); st += b; }
        if (!from.source.empty()) st += "smpname=" + from.source.name + "\nsmp=" + from.source.save() + "\n";
        st += "name=" + from.presetName + "\n";
        sp.patch = st;
        applyPreset(i, -1);
    }
    std::string soundName(int i) const {
        const SongPart& sp = *parts[(size_t) i];
        if (sp.drums) return "SID KIT";
        if (sp.preset.load() >= 0) return PRESETS[sp.preset.load()].name;
        return sp.syn.empty() ? "PATCH" : sp.syn[0]->presetName;
    }
    void prepare(double rate) {
        fs = rate;
        for (auto& p : parts) for (auto& s : p->syn) { s->prepare(fs); s->pollSample(); }
        bl.assign(5 * 32, 0.f);
    }
    void poll() { for (auto& p : parts) for (auto& s : p->syn) s->pollSample(); }
    bool anySolo() const { for (auto& p : parts) if (p->solo.load(std::memory_order_relaxed)) return true; return false; }

    // ---- transport (UI thread) ----
    void play() { playing = true; }
    void stop() { playing = false; stopReq.fetch_add(1); }
    void rewind() { seekTo = 0.0; }
    void seek(double t) { seekTo = std::clamp(t, 0.0, length); }

    // ---- audio: n (<= 32) samples of the song onto the five buses ----
    void render(float (*bus)[32], int n) {
        for (int c = 0; c < 5; ++c) std::fill(bus[c], bus[c] + n, 0.f);
        if (parts.empty() || fs <= 0) return;
        const int sr = stopReq.load(std::memory_order_relaxed);
        if (sr != stopSeen) { stopSeen = sr; allOff(); }
        const double sk = seekTo.exchange(-1);
        if (sk >= 0) { allOff(); pos = sk; cursor = (size_t) (std::lower_bound(ev.begin(), ev.end(), sk, [](const SongEvent& e, double t) { return e.t < t; }) - ev.begin()); }
        if (playing.load(std::memory_order_relaxed)) {
            const double end = pos + n / fs;
            while (cursor < ev.size() && ev[cursor].t < end) fire(ev[cursor++]);
            pos = end;
            if (pos >= length) {
                allOff();
                if (loop.load(std::memory_order_relaxed)) { pos = 0; cursor = 0; }
                else { playing = false; pos = 0; cursor = 0; }
            }
            position.store(pos, std::memory_order_relaxed);
        }
        const bool solo = anySolo();
        const float a = 1 - std::exp(-(float) n / (float) (fs * 0.01));
        for (auto& pp : parts) {
            SongPart& sp = *pp;
            const bool audible = !sp.mute.load(std::memory_order_relaxed) && (!solo || sp.solo.load(std::memory_order_relaxed));
            const float lv = sp.level.load(std::memory_order_relaxed), g = audible ? 1.0667f * lv * lv : 0.f;
            const float pn = std::clamp(sp.pan.load(std::memory_order_relaxed), -1.f, 1.f), ang = (pn + 1) * 0.78539816f;
            const float tl = g * std::cos(ang) * 1.41421356f, tr = g * std::sin(ang) * 1.41421356f;
            const float te = g * sp.echo.load(std::memory_order_relaxed), trv = g * sp.rev.load(std::memory_order_relaxed);
            float pk = 0;
            for (auto& s : sp.syn) {
                float* L = bl.data(); float* R = L + 32; float* E1 = R + 32; float* E2 = E1 + 32; float* RV = E2 + 32;
                s->renderBus(L, R, E1, E2, RV, n);
                if (s->idle()) continue;
                float gl0 = sp.gl, gr0 = sp.gr, ge0 = sp.ge, grv0 = sp.grv;
                for (int k = 0; k < n; ++k) {
                    gl0 += a * (tl - gl0); gr0 += a * (tr - gr0); ge0 += a * (te - ge0); grv0 += a * (trv - grv0);
                    const float l = L[k] * gl0, r = R[k] * gr0;
                    bus[0][k] += l; bus[1][k] += r;
                    bus[2][k] += L[k] * ge0; bus[3][k] += R[k] * ge0; bus[4][k] += 0.5f * (L[k] + R[k]) * grv0;
                    pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
                }
            }
            // the smoothing runs once a block for the part (all its synths start from the same gains)
            for (int k = 0; k < n; ++k) { sp.gl += a * (tl - sp.gl); sp.gr += a * (tr - sp.gr); sp.ge += a * (te - sp.ge); sp.grv += a * (trv - sp.grv); }
            const float fall = std::exp(-(float) n / (float) (fs * 0.3));
            sp.meter.store(std::max(pk, sp.meter.load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
        }
    }

    // ---- saved with the project ----
    std::string state() const {
        std::string s = "songname=" + name + "\nsong=" + SampleSource::b64enc(file) + "\n";
        for (size_t i = 0; i < parts.size(); ++i) {
            const SongPart& sp = *parts[i]; char b[160];
            std::snprintf(b, sizeof b, "songpart%d=%d %.4g %.4g %.4g %.4g %d %d\n", (int) i, sp.preset.load(), sp.level.load(), sp.pan.load(),
                          sp.echo.load(), sp.rev.load(), sp.mute.load() ? 1 : 0, sp.solo.load() ? 1 : 0);
            s += b;
            if (sp.preset.load() < 0 && !sp.patch.empty()) s += "songpatch" + std::to_string(i) + "=" + SampleSource::b64enc(sp.patch) + "\n";
        }
        s += std::string("songloop=") + (loop.load() ? "1" : "0") + "\n";
        return s;
    }
    // the part settings saved by state(), onto a song just loaded from the same file
    void applyState(const std::string& st) {
        size_t i = 0;
        while (i < st.size()) {
            size_t j = st.find('\n', i); if (j == std::string::npos) j = st.size();
            const std::string line = st.substr(i, j - i); i = j + 1;
            const size_t eq = line.find('='); if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "songloop") loop = v == "1";
            else if (k.rfind("songpatch", 0) == 0) { const int p = std::atoi(k.c_str() + 9); if (p >= 0 && p < (int) parts.size()) parts[(size_t) p]->patch = SampleSource::b64dec(v); }
            else if (k.rfind("songpart", 0) == 0) {
                const int p = std::atoi(k.c_str() + 8); if (p < 0 || p >= (int) parts.size()) continue;
                SongPart& sp = *parts[(size_t) p];
                int pr = 0, mu = 0, so = 0; float lv = .75f, pn = 0, e = 0, r = 0;
                if (std::sscanf(v.c_str(), "%d %f %f %f %f %d %d", &pr, &lv, &pn, &e, &r, &mu, &so) == 7) {
                    sp.preset = std::clamp(pr, -1, presetCount() - 1); sp.level = std::clamp(lv, 0.f, 1.f); sp.pan = std::clamp(pn, -1.f, 1.f);
                    sp.echo = std::clamp(e, 0.f, 1.f); sp.rev = std::clamp(r, 0.f, 1.f); sp.mute = mu != 0; sp.solo = so != 0;
                }
            }
        }
        for (size_t p = 0; p < parts.size(); ++p) {
            if (parts[p]->preset.load() < 0 && parts[p]->patch.empty()) parts[p]->preset = gmPreset(parts[p]->program);
            if (!parts[p]->drums) applyPreset((int) p, parts[p]->preset.load());
        }
    }

private:
    double fs = 0, pos = 0;
    size_t cursor = 0; int stopSeen = 0;
    std::vector<float> bl;
    void fire(const SongEvent& e) {
        if (e.part >= parts.size()) return;
        SongPart& sp = *parts[e.part];
        if (e.sub >= sp.syn.size()) return;
        Synth& s = *sp.syn[e.sub];
        switch (e.type) {
            case 0: s.noteOff(e.note); break;
            case 1: s.noteOn(e.note, e.vel / 127.f); break;
            case 2: s.setBend(((e.note << 7 | e.vel) - 8192) / 8192.0); break;
            case 3: s.setSustain(e.note != 0); break;
            default: break;
        }
    }
    void allOff() { for (auto& p : parts) for (auto& s : p->syn) { s->setSustain(false); s->allNotesOff(); s->setBend(0); } }
    static void fixUp(Synth& s, int maxPoly) {
        // a part: lighter on the CPU than the keyboard synth (2x oversampling, a few chips), no sequencer, echo or reverb of its own
        s.set(P::quality, 0);
        s.set(P::poly, (float) std::min(maxPoly, (int) std::lround(s.get(P::poly))));
        s.set(P::shost, 0); s.set(P::clksrc, 0); s.set(P::strans, 0);
    }
};

// ---- the Synth's song hooks ----
inline void Synth::songBlock(int n) { song->render(songBus, n); }
inline void Synth::setSong(Song* s) {
    if (s) s->prepare(fs);
    Song* old;
    { std::lock_guard<std::mutex> lk(songMx); old = songShared; songShared = s; }
    delete old;
}
inline void Synth::destroySong() { delete songShared; songShared = nullptr; }
inline void Synth::prepareSong() { if (songShared) songShared->prepare(fs); }
inline void Synth::pollSong() { if (songShared) songShared->poll(); }
// (a host may ask for the state on any thread: hold the song still while it's written; the audio skips the song for a block)
inline std::string Synth::songState() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(songMx));
    return songShared ? songShared->state() : std::string();
}
inline void Synth::loadSongState(const std::string& st) {
    if (busMode) return;
    auto val = [&](const std::string& key) {
        const std::string k = "\n" + key + "="; size_t a = ("\n" + st).find(k);
        if (a == std::string::npos) return std::string();
        a += k.size() - 1; size_t b = st.find('\n', a); return st.substr(a, b == std::string::npos ? std::string::npos : b - a);
    };
    const std::string f = val("song");
    if (f.empty()) { setSong(nullptr); return; }
    auto s = std::make_unique<Song>();
    if (!s->load(SampleSource::b64dec(f), val("songname")).empty()) { setSong(nullptr); return; }
    s->applyState(st);
    setSong(s.release());
}

}  // namespace sid
