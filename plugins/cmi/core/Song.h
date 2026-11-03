// MIDI songs for the composer: Standard MIDI Files (format 0 or 1) read into MidiSong, each MIDI channel a track,
// each track given the CMI voice closest to its General MIDI program (changeable), and the factory songs.
#pragma once
#include "Library.h"

namespace cmi {

// the CMI voice for a General MIDI program
inline const char* gmVoice(int program) {
    static const char* map[128] = {
        "PIANO", "PIANO", "PIANO", "PIANO", "TINEPNO", "TINEPNO", "HARPSI", "CLAV",
        "GLOCK", "GLOCK", "KALIMBA", "MARIMBA", "MARIMBA", "MARIMBA", "TUBEBELL", "KOTO",
        "ORGAN", "ORGAN", "ORGAN", "ORGAN", "ORGAN", "ORGAN", "SAX", "ORGAN",
        "NYLONGTR", "NYLONGTR", "NYLONGTR", "NYLONGTR", "NYLONGTR", "GTRLEAD", "GTRLEAD", "GTRLEAD",
        "BASSGT", "BASSGT", "BASSGT", "FRETLESS", "SLAPBASS", "SLAPBASS", "SYNBASS", "SYNBASS",
        "STRINGS", "STRINGS", "CELLO", "CELLO", "STRINGS", "PIZZ", "KOTO", "TIMPANI",
        "STRINGS", "STRINGS", "SYNSTR", "SYNSTR", "CHOIRAAH", "MALECHR", "VOXMORPH", "ORCHHIT",
        "TRUMPET", "BRASS", "BRASS", "TRUMPET", "BRASS", "BRASS", "BRASS", "BRASS",
        "SAX", "SAX", "SAX", "SAX", "SAX", "SAX", "SAX", "FLUTE",
        "FLUTE", "FLUTE", "FLUTE", "PANPIPE", "PANPIPE", "SHAKUHCH", "FLUTE", "FLUTE",
        "SQUARE", "SAWTOOTH", "SAWTOOTH", "SAWTOOTH", "SAWTOOTH", "VOXMORPH", "SAWTOOTH", "SAWTOOTH",
        "SYNSTR", "SYNSTR", "SYNSTR", "CHOIRAAH", "SYNBELL", "SYNSTR", "VOXMORPH", "SYNSTR",
        "SYNBELL", "SYNSTR", "SYNBELL", "SYNBELL", "SYNSTR", "SYNSTR", "VOXMORPH", "SYNBELL",
        "SITAR", "NYLONGTR", "KOTO", "KOTO", "KALIMBA", "SAX", "STRINGS", "SAX",
        "GLOCK", "COWBELL", "STEELDRM", "CROSSTIX", "TOMLO", "TOMHI", "TOMHI", "HHOPEN1",
        "NYLONGTR", "BREATH", "BREATH", "FLUTE", "SYNBELL", "BREATH", "HHOPEN1", "SNARE1",
    };
    return program >= 0 && program < 128 ? map[program] : "PIANO";
}

// a GM drum note on the CMI's drum voices
inline const char* gmDrum(int note) {
    switch (note) {
        case 35: case 36: return "KICK1";
        case 37: return "CROSSTIX";
        case 38: case 40: return "SNARE1";
        case 39: return "CLAPGATE";
        case 42: return "HIHAT2";
        case 44: return "HHPEDAL";
        case 46: case 49: case 51: case 52: case 55: case 57: case 59: return "HHOPEN1";
        case 41: case 43: case 45: return "TOMLO";
        case 47: case 48: case 50: return "TOMHI";
        case 56: return "COWBELL";
        case 60: case 61: case 62: case 63: case 64: return "CONGA";
        case 54: case 69: case 70: return "HHPEDAL";
        case 75: case 76: case 77: return "CROSSTIX";
        default: return note < 41 ? "KICK1" : note < 60 ? "TOMHI" : "CONGA";
    }
}

inline bool parseSmf(const uint8_t* d, size_t n, const std::string& name, MidiSong& out, std::string& err) {
    auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t) d[o] << 24 | (uint32_t) d[o + 1] << 16 | (uint32_t) d[o + 2] << 8 | d[o + 3] : 0u; };
    auto u16 = [&](size_t o) { return o + 2 <= n ? (uint32_t) (d[o] << 8 | d[o + 1]) : 0u; };
    if (n < 14 || std::memcmp(d, "MThd", 4)) { err = "NOT A MIDI FILE"; return false; }
    const uint32_t hlen = u32(4), ntrk = u16(10), div = u16(12);
    if (div & 0x8000) { err = "SMPTE-TIMED MIDI FILES AREN'T SUPPORTED"; return false; }
    const double tpq = div ? div : 96;
    struct Raw { uint64_t tick; int ch; uint8_t st, d1, d2; int trk; };
    std::vector<Raw> ev; std::vector<std::pair<uint64_t, double>> tempo; std::vector<std::string> trkNames(ntrk);
    int num = 4, den = 4;
    size_t p = 8 + hlen;
    for (uint32_t t = 0; t < ntrk && p + 8 <= n; ++t) {
        if (std::memcmp(d + p, "MTrk", 4)) break;
        const size_t len = u32(p + 4), end = std::min(n, p + 8 + len);
        size_t q = p + 8; uint64_t tick = 0; uint8_t run = 0;
        auto vlq = [&]() { uint32_t v = 0; for (int k = 0; k < 4 && q < end; ++k) { const uint8_t b = d[q++]; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; } return v; };
        while (q < end) {
            tick += vlq();
            if (q >= end) break;
            uint8_t st = d[q];
            if (st & 0x80) { ++q; if (st < 0xF0) run = st; } else st = run;
            if (st == 0xFF) {
                if (q >= end) break;
                const uint8_t type = d[q++]; const uint32_t ml = vlq();
                if (q + ml > end) break;
                if (type == 0x51 && ml >= 3) tempo.push_back({ tick, 60000000.0 / std::max(1u, (uint32_t) (d[q] << 16 | d[q + 1] << 8 | d[q + 2])) });
                else if (type == 0x58 && ml >= 2) { num = d[q]; den = 1 << d[q + 1]; }
                else if ((type == 0x03 || type == 0x01) && trkNames[t].empty()) trkNames[t].assign((const char*) d + q, ml);
                else if (type == 0x2F) { q += ml; break; }
                q += ml; continue;
            }
            if (st == 0xF0 || st == 0xF7) { const uint32_t sl = vlq(); q += sl; continue; }
            if (!(st & 0x80)) break;   // no running status to use: give up on this track
            const int hi = st & 0xF0, need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (q + (size_t) need > end) break;
            const uint8_t a = d[q], b = need > 1 ? d[q + 1] : 0; q += (size_t) need;
            ev.push_back({ tick, st & 0x0F, (uint8_t) st, a, b, (int) t });
        }
        p = p + 8 + len;
    }
    if (ev.empty()) { err = "NO NOTES IN MIDI FILE"; return false; }
    std::stable_sort(ev.begin(), ev.end(), [](const Raw& x, const Raw& y) { return x.tick < y.tick; });
    // channels become tracks in the order they first play
    int trackOf[16]; std::fill(std::begin(trackOf), std::end(trackOf), -1); int nt = 0;
    MidiSong s; s.name = name; s.num = num; s.den = den;
    for (auto& t : s.program) t = 0;
    for (const auto& e : ev) {
        if ((e.st & 0xF0) != 0x90 || e.d2 == 0) continue;
        if (trackOf[e.ch] < 0) { trackOf[e.ch] = nt < TRACKS ? nt++ : e.ch % TRACKS; }
    }
    for (int c = 0; c < 16; ++c) if (trackOf[c] < 0) trackOf[c] = c % TRACKS;
    for (const auto& e : ev) {
        const int t = trackOf[e.ch];
        if ((e.st & 0xF0) == 0xC0 && !s.used[t]) s.program[t] = e.d1;
        if (e.ch == 9) s.program[t] = -1;
        if ((e.st & 0xF0) == 0x90 && e.d2 > 0) {
            s.used[t] = true;
            if (s.trackName[t].empty() && e.trk < (int) trkNames.size()) s.trackName[t] = trkNames[(size_t) e.trk];
        }
        if ((e.st & 0xF0) == 0xC0 || (e.st & 0xF0) == 0xD0 || (e.st & 0xF0) == 0xA0) continue;
        s.ev.push_back({ e.tick / tpq, (uint8_t) t, (uint8_t) ((e.st & 0xF0) | (t & 0x0F)), e.d1, e.d2 });
        if ((e.st & 0xF0) == 0x80 || (e.st & 0xF0) == 0x90) s.length = std::max(s.length, e.tick / tpq);
    }
    std::sort(tempo.begin(), tempo.end());
    if (tempo.empty() || tempo[0].first > 0) s.tempo.push_back({ 0.0, 120.0 });
    for (const auto& t : tempo) s.tempo.push_back({ t.first / tpq, t.second });
    for (int t = 0; t < TRACKS; ++t) {
        std::string& nm = s.trackName[t];
        while (!nm.empty() && (nm.back() == ' ' || nm.back() == 0)) nm.pop_back();
        if (nm.empty() && s.used[t]) nm = s.program[t] < 0 ? "DRUMS" : std::string("TRACK ") + std::to_string(t + 1);
    }
    s.raw.assign(d, d + n);
    out = std::move(s);
    return true;
}

// the voices a song starts with: the CMI voice for each track's GM program
inline void defaultSongVoices(const MidiSong& s, const Bank& bank, int* voice) {
    for (int t = 0; t < TRACKS; ++t) {
        if (s.program[t] < 0) { voice[t] = GM_KIT; continue; }
        const char* want = gmVoice(s.program[t]);
        voice[t] = 0;
        for (int i = 0; i < bank.count.load(); ++i) if (const Sound* x = bank.at(i)) if (x->name == want) { voice[t] = i; break; }
    }
}

// the drum kit lookup a song engine uses
inline std::function<int(int)> kitLookup(const Bank& bank) {
    return [&bank](int note) {
        const char* want = gmDrum(note);
        for (int i = 0; i < bank.count.load(); ++i) if (const Sound* x = bank.at(i)) if (x->name == want) return i;
        return -1;
    };
}

// One instance's songs (imported MIDI files), each with the voices and mutes chosen for its tracks. Entries are
// only ever added (removing one keeps it alive), so the engine can hold a pointer to any of them.
struct SongEntry { MidiSong song; int voice[TRACKS] {}; bool mute[TRACKS] {}; bool removed = false; };
class SongList {
public:
    int add(MidiSong s, const Bank& bank) {
        auto e = std::make_unique<SongEntry>(); e->song = std::move(s);
        defaultSongVoices(e->song, bank, e->voice);
        std::lock_guard<std::mutex> g(lock); items.push_back(std::move(e)); return (int) items.size() - 1;
    }
    int size() const { return (int) items.size(); }
    SongEntry* at(int i) { return i >= 0 && i < (int) items.size() && !items[(size_t) i]->removed ? items[(size_t) i].get() : nullptr; }
    const SongEntry* at(int i) const { return i >= 0 && i < (int) items.size() && !items[(size_t) i]->removed ? items[(size_t) i].get() : nullptr; }
    void remove(int i) { if (auto* e = at(i)) e->removed = true; }
    int count() const { int n = 0; for (auto& e : items) n += !e->removed; return n; }
    // the i-th song still in the list
    int nth(int k) const { for (int i = 0; i < size(); ++i) if (at(i) && k-- == 0) return i; return -1; }
    int indexOf(const MidiSong* s) const { for (int i = 0; i < size(); ++i) if (at(i) && &at(i)->song == s) return i; return -1; }
private:
    std::vector<std::unique_ptr<SongEntry>> items; std::mutex lock;
};

// selecting a song on an engine: its voices and mutes go onto the track cards
inline void selectSong(Engine& eng, SongEntry* e) {
    if (e) { for (int t = 0; t < TRACKS; ++t) { eng.songVoice[t] = e->voice[t]; eng.songMute[t] = e->mute[t]; } }
    eng.songRun = false;
    eng.song = e ? &e->song : nullptr;
}

}  // namespace cmi
