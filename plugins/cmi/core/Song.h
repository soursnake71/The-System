// MIDI songs for the composer: Standard MIDI Files (format 0 or 1) read into MidiSong, each MIDI channel a track,
// each track given the CMI voice closest to its General MIDI program (changeable), and the factory songs.
#pragma once
#include "Library.h"
#include "Engine.h"

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
    int num = 4, den = 4; double minLen = 0;
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
                if (ml > (uint32_t) (end - q)) break;
                if (type == 0x51 && ml >= 3) tempo.push_back({ tick, 60000000.0 / std::max(1u, (uint32_t) (d[q] << 16 | d[q + 1] << 8 | d[q + 2])) });
                else if (type == 0x58 && ml >= 2) { num = d[q]; den = 1 << std::min<int>(d[q + 1], 6); }
                else if (type == 0x01 && ml > 17 && !std::memcmp(d + q, "OMEGA CMI LENGTH ", 17)) minLen = std::atof(std::string((const char*) d + q + 17, ml - 17).c_str());
                else if ((type == 0x03 || type == 0x01) && trkNames[t].empty()) trkNames[t].assign((const char*) d + q, ml);
                else if (type == 0x2F) { q += ml; break; }
                q += ml; continue;
            }
            if (st == 0xF0 || st == 0xF7) { const uint32_t sl = vlq(); if (sl > (uint32_t) (end - q)) break; q += sl; continue; }
            if (!(st & 0x80)) break;   // no running status to use: give up on this track
            const int hi = st & 0xF0, need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (q + (size_t) need > end) break;
            const uint8_t a = d[q] & 0x7F, b = need > 1 ? d[q + 1] & 0x7F : 0; q += (size_t) need;   // stray high bits in data bytes: some files have them
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
        if (trackOf[e.ch] < 0) trackOf[e.ch] = nt++;
    }
    for (int c = 0; c < 16; ++c) if (trackOf[c] < 0) trackOf[c] = nt < SONG_TRK ? nt++ : c;
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
    for (int t = 0; t < SONG_TRK; ++t) {
        std::string& nm = s.trackName[t];
        while (!nm.empty() && (nm.back() == ' ' || nm.back() == 0)) nm.pop_back();
        if (nm.empty() && s.used[t]) nm = s.program[t] < 0 ? "DRUMS" : std::string("TRACK ") + std::to_string(t + 1);
    }
    if (minLen > 0) { s.minLength = minLen; s.length = std::max(s.length, minLen); for (int t = 0; t < SONG_TRK; ++t) if (!s.trackName[t].empty()) s.used[t] = true; }
    s.raw.assign(d, d + n);
    out = std::move(s);
    return true;
}

// the voices a song starts with: the CMI voice for each track's GM program
inline void defaultSongVoices(const MidiSong& s, const Bank& bank, int* voice) {
    for (int t = 0; t < SONG_TRK; ++t) {
        if (s.program[t] < 0) { voice[t] = GM_KIT; continue; }
        const char* want = gmVoice(s.program[t]);
        voice[t] = NO_VOICE;
        for (int i = 0; i < bank.count.load(); ++i) if (const Sound* x = bank.at(i)) if (x->name == want) { voice[t] = i; break; }
    }
}

// Matching tracks to voices automatically: the track's name first (what the composer called it: "SynthVox",
// "Timpani", "Str Ensem"...), preferring voices you've loaded from CMI disks (the real ARR1, ORCH5...), then its
// General MIDI program, then its range and how it's played (drums: short notes on the GM drum keys).
struct VoiceRule { const char* keys; const char* want; };
inline const std::vector<VoiceRule>& voiceRules() {
    static const std::vector<VoiceRule> r = {
        { "synthstr|synstr|synthstring|synthpad|pad", "SYNSTR|ENSEMBLE|STRINGS" },
        { "steelg|geetar|nylon|acoustic|accogtr|classical", "NYLONGTR" },
        { "grand|piano|pno|accogr", "PIANO" },
        { "synthdrum|syndrum|electom", "TOMHI|TOMLO" },
        { "synthbrass|synbrass", "BRASS|MBRASS" },
        { "synthbass|synbass|moog", "SYNBASS|BASSYNTH" },
        { "choir|choral|chorus|vox|voice|vocal|aah|ooh|singer|soprano|alto|tenor", "ARR1|SARARR1|AHH1|CHOIRAAH|MALECHR" },
        { "orchhit|orchestrahit|orchestraattack|orchattack|orchstab|stab|hit", "ORCH5|ORCH2|ORCHFZ1|ORCHHIT" },
        { "timp", "TIMPANI|TIMP" },
        { "taiko|tom", "TOMLO|TOMHI" },
        { "gun|shot|explo|blast|bang|boom|smash", "SMASH|GUNSHOT|SNARE1|ORCHHIT" },
        { "snare|sd", "SNARE1|SNARE" },
        { "kick|bassdrum|bd", "KICK1|KICK" },
        { "hihat|hat|hh", "HIHAT2|HHPEDAL" },
        { "clap", "CLAPGATE" },
        { "cymb|crash|ride|splash", "HHOPEN1" },
        { "cowbell", "COWBELL" }, { "conga|bongo", "CONGA" },
        { "drum|perc|kit|beat|rhythm", "@KIT" },
        { "pizz", "PIZZ" },
        { "cello", "CELLO2|CELLO" },
        { "string|strings|str|violin|viola|fiddle|ensem|orch", "ENSEMBLE|STRINGS" },
        { "trump|trp", "TRUMPET|BRASS" },
        { "brass|horn|tromb|tuba", "BRASS|MBRASS|TRUMPET" },
        { "sax|bassoon|fagott|oboe|clarin|reed|englishhorn", "SAX" },
        { "shaku", "SHAKUHCH|FLUTE" },
        { "panpipe|panflute|pipes", "PANPIPE" },
        { "flute|piccolo|recorder|whistle|ocarina", "FLUTE|PANPIPE" },
        { "slap", "SLAPBASS" }, { "fretless", "FRETLESS" },
        { "bass", "BASSGT|FRETLESS|SYNBASS" },
        { "sitar", "SITAR" }, { "koto|harp|dulcimer", "KOTO" }, { "kalimba|mbira|musicbox", "KALIMBA" },
        { "marimba|xylo|vibes|mallet", "MARIMBA" }, { "glock|celest", "GLOCK" }, { "tubular|chime|bell", "TUBEBELL|SYNBELL" },
        { "steeldrum|steelpan|pan drum", "STEELDRM" }, { "gong|tamtam", "GONG" }, { "glass", "GLASSBRK|BREAKGLS" },
        { "harpsi|clavecin", "HARPSI" }, { "clav", "CLAV" }, { "rhodes|epiano|ep|tine|wurl", "TINEPNO" },
        { "piano|pno", "PIANO" }, { "organ|org", "ORGAN" },
        { "distort|odrive|overdrive|lead|gtr|guitar", "GTRLEAD|NYLONGTR" },
        { "breath", "BREATH" }, { "hey|shout", "VOXHEY" },
        { "saw", "SAWTOOTH" }, { "square", "SQUARE" },
    };
    return r;
}

inline std::string compact(const std::string& s) { std::string o; for (char c : s) if (std::isalnum((unsigned char) c)) o += (char) std::tolower((unsigned char) c); return o; }

// a voice by name: your loaded voices first (exact, then a name starting with it), then the factory ones
inline int findVoice(const Bank& bank, const std::string& want) {
    const int n = bank.count.load(), built = Library::get().builtIn;
    for (int pass = 0; pass < 3; ++pass)
        for (int i = pass == 2 ? 0 : built; i < (pass == 2 ? built : n); ++i) {
            const Sound* x = bank.at(i);
            if (!x || (i >= built && Library::userOf(i) >= 0 && x == Library::get().blank[(size_t) Library::userOf(i)])) continue;
            if (pass == 0 ? x->name == want : pass == 1 ? x->name.rfind(want, 0) == 0 : x->name == want) return i;
        }
    return -1;
}

// what a track looks like: its range and note lengths
struct TrackShape { int notes = 0, lo = 127, hi = 0; double medianPitch = 60, meanLen = 0; bool drumKeys = false; };
inline TrackShape shapeOf(const MidiSong& s, int t) {
    TrackShape sh; std::vector<int> pitches; std::vector<double> starts(128, -1); double lenSum = 0; int lens = 0, onDrumKeys = 0;
    for (const auto& e : s.ev) {
        if (e.track != t) continue;
        const int hi = e.status & 0xF0;
        if (hi == 0x90 && e.d2) { pitches.push_back(e.d1); starts[e.d1] = e.beat; sh.lo = std::min(sh.lo, (int) e.d1); sh.hi = std::max(sh.hi, (int) e.d1); onDrumKeys += e.d1 >= 35 && e.d1 <= 81; }
        else if ((hi == 0x80 || hi == 0x90) && starts[e.d1] >= 0) { lenSum += e.beat - starts[e.d1]; ++lens; starts[e.d1] = -1; }
    }
    sh.notes = (int) pitches.size();
    if (!pitches.empty()) { std::nth_element(pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end()); sh.medianPitch = pitches[pitches.size() / 2]; }
    sh.meanLen = lens ? lenSum / lens : 0;
    sh.drumKeys = sh.notes > 0 && onDrumKeys == sh.notes;
    return sh;
}

// a part played one note at a time: its notes (on a melodic channel) almost never overlap. Such parts sound best mono
// on the CMI: a sampled note's tail doesn't run on under the next.
inline bool monoPart(const MidiSong& s, int t) {
    if (!s.used[t] || s.program[t] < 0) return false;
    std::vector<std::pair<double, double>> n; std::vector<double> start(128, -1);
    for (const auto& e : s.ev) {
        if (e.track != t) continue;
        const int hi = e.status & 0xF0;
        if (hi == 0x90 && e.d2) start[e.d1] = e.beat;
        else if ((hi == 0x80 || hi == 0x90) && start[e.d1] >= 0) { n.push_back({ start[e.d1], e.beat }); start[e.d1] = -1; }
    }
    if (n.size() < 4) return false;
    std::sort(n.begin(), n.end());
    int overlaps = 0; double end = -1;
    for (const auto& x : n) { if (x.first < end - 0.06) ++overlaps; end = std::max(end, x.second); }   // a little legato is still a line
    return overlaps * 50 < (int) n.size();   // under 2%
}

// the voice for each track; reason[t] says why (for the screen)
inline void autoSongVoices(const MidiSong& s, const Bank& bank, int* voice, std::string* reason = nullptr) {
    const char* gmNames[16] = { "piano", "chromatic", "organ", "guitar", "bass", "strings", "ensemble", "brass", "reed", "pipe", "synth lead", "synth pad", "synth fx", "ethnic", "percussive", "effects" };
    for (int t = 0; t < SONG_TRK; ++t) {
        voice[t] = NO_VOICE; std::string why;
        if (!s.used[t]) { if (reason) reason[t] = ""; continue; }
        if (s.program[t] < 0) { voice[t] = GM_KIT; if (reason) reason[t] = "drum channel"; continue; }
        const std::string nm = compact(s.trackName[t]);
        // words: split at spaces and punctuation, and at capitals ("SynthVox" -> synth, vox)
        std::vector<std::string> words;
        { std::string w; char prev = 0;
          for (char c : s.trackName[t]) {
              const bool an = std::isalnum((unsigned char) c);
              if (!an || (std::isupper((unsigned char) c) && prev && std::islower((unsigned char) prev))) { if (!w.empty()) words.push_back(w); w.clear(); }
              if (an) w += (char) std::tolower((unsigned char) c);
              prev = c;
          }
          if (!w.empty()) words.push_back(w); }
        auto matches = [&](const std::string& k) {
            if (k.size() > 3) return nm.find(k) != std::string::npos;
            for (const auto& w : words) if (w.rfind(k, 0) == 0) return true;   // a short key starts a word
            return false;
        };
        int pick = -1;
        for (const auto& r : voiceRules()) {
            std::string keys = r.keys; bool hit = false;
            size_t a = 0;
            while (a <= keys.size()) { size_t b2 = keys.find('|', a); if (b2 == std::string::npos) b2 = keys.size(); const std::string k = keys.substr(a, b2 - a); if (!k.empty() && matches(k)) { hit = true; break; } a = b2 + 1; }
            if (!hit) continue;
            std::string want = r.want; a = 0;
            while (a <= want.size()) {
                size_t b2 = want.find('|', a); if (b2 == std::string::npos) b2 = want.size();
                const std::string w = want.substr(a, b2 - a); a = b2 + 1;
                if (w == "@KIT") { const TrackShape sh = shapeOf(s, t); if (sh.drumKeys) { pick = GM_KIT; break; } continue; }
                const int i = findVoice(bank, w);
                if (i >= 0) { pick = i; break; }
            }
            if (pick != -1) { why = "name \"" + s.trackName[t] + "\""; break; }
        }
        if (pick == -1) {
            const char* g = gmVoice(s.program[t]);
            pick = findVoice(bank, g);
            why = std::string("GM ") + gmNames[(s.program[t] / 8) & 15] + " program " + std::to_string(s.program[t] + 1);
            // a low track on a high voice (or the other way round): by range instead
            const TrackShape sh = shapeOf(s, t);
            if (sh.medianPitch < 40 && s.program[t] >= 80 && s.program[t] < 104) { const int b2 = findVoice(bank, "SYNBASS"); if (b2 >= 0) { pick = b2; why = "synth part in the bass range"; } }
        }
        voice[t] = pick < 0 && pick != GM_KIT ? NO_VOICE : pick;
        if (reason) reason[t] = why;
    }
}

// ---- editing ----
struct SongNote { int track; double start, len; int pitch, vel; };

inline std::vector<SongNote> notesOf(const MidiSong& s) {
    std::vector<SongNote> out; std::vector<long> open((size_t) SONG_TRK * 128, -1);
    for (const auto& e : s.ev) {
        const int hi = e.status & 0xF0; const size_t k = (size_t) e.track * 128 + e.d1;
        if (hi == 0x90 && e.d2) { if (open[k] >= 0) out[(size_t) open[k]].len = std::max(0.01, e.beat - out[(size_t) open[k]].start); open[k] = (long) out.size(); out.push_back({ e.track, e.beat, 0.25, e.d1, e.d2 }); }
        else if ((hi == 0x80 || hi == 0x90) && open[k] >= 0) { out[(size_t) open[k]].len = std::max(0.01, e.beat - out[(size_t) open[k]].start); open[k] = -1; }
    }
    return out;
}

// the song with these notes (everything else it had, controllers, bends, tempo, stays)
inline void setNotes(MidiSong& s, const std::vector<SongNote>& notes) {
    std::vector<SongEvent> ev;
    for (const auto& e : s.ev) { const int hi = e.status & 0xF0; if (hi != 0x80 && hi != 0x90) ev.push_back(e); }
    if (s.minLength <= 0) for (bool& u : s.used) u = false;
    s.length = 0;
    for (const auto& n : notes) {
        if (n.track < 0 || n.track >= SONG_TRK || n.len <= 0) continue;
        const uint8_t t = (uint8_t) n.track, p = (uint8_t) clampi(n.pitch, 0, 127), v = (uint8_t) clampi(n.vel, 1, 127);
        ev.push_back({ std::max(0.0, n.start), t, (uint8_t) (0x90 | t), p, v });
        ev.push_back({ std::max(0.0, n.start) + n.len, t, (uint8_t) (0x80 | t), p, 0 });
        s.used[n.track] = true;
        s.length = std::max(s.length, n.start + n.len);
    }
    s.length = std::max(s.length, s.minLength);
    // in time; at the same moment, a note ends before the next one starts
    std::stable_sort(ev.begin(), ev.end(), [](const SongEvent& a, const SongEvent& b) {
        if (a.beat != b.beat) return a.beat < b.beat;
        auto rank = [](const SongEvent& e) { const int hi = e.status & 0xF0; return hi == 0x80 || (hi == 0x90 && !e.d2) ? 0 : hi == 0x90 ? 2 : 1; };
        return rank(a) < rank(b);
    });
    s.ev = std::move(ev);
}

// a Standard MIDI File (format 1, 480 ticks per quarter): a tempo track, then one track per used track
inline std::vector<uint8_t> writeSmf(const MidiSong& s, const std::string* voiceNames = nullptr) {
    std::vector<uint8_t> f;
    auto u32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int k = 3; k >= 0; --k) v.push_back((uint8_t) (x >> (8 * k))); };
    auto vlq = [](std::vector<uint8_t>& v, uint32_t x) { uint8_t b[5]; int n = 0; b[n++] = x & 0x7F; while (x >>= 7) b[n++] = (uint8_t) (0x80 | (x & 0x7F)); while (n) v.push_back(b[--n]); };
    auto track = [&](std::vector<uint8_t>& body) { f.insert(f.end(), { 'M', 'T', 'r', 'k' }); u32(f, (uint32_t) body.size()); f.insert(f.end(), body.begin(), body.end()); };
    const double tpq = 480;
    auto keep = [&](int t) { return s.used[t] || (s.minLength > 0 && !s.trackName[t].empty()); };
    int used = 0; for (int t = 0; t < SONG_TRK; ++t) used += keep(t);
    f.insert(f.end(), { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 }); f.push_back(0); f.push_back((uint8_t) (used + 1)); f.push_back(480 >> 8); f.push_back(480 & 0xFF);
    {
        std::vector<uint8_t> b; uint64_t last = 0;
        int dl = 0; for (int d = s.den; d > 1; d >>= 1) ++dl;
        b.insert(b.end(), { 0, 0xFF, 0x58, 4, (uint8_t) s.num, (uint8_t) dl, 24, 8 });
        if (s.minLength > 0) { const std::string m = "OMEGA CMI LENGTH " + std::to_string(s.minLength); b.push_back(0); b.push_back(0xFF); b.push_back(0x01); vlq(b, (uint32_t) m.size()); b.insert(b.end(), m.begin(), m.end()); }
        for (const auto& t : s.tempo) {
            const uint64_t tick = (uint64_t) std::llround(t.first * tpq);
            vlq(b, (uint32_t) (tick - std::min(tick, last))); last = tick;
            const uint32_t us = (uint32_t) std::lround(60000000.0 / std::max(1.0, t.second));
            b.insert(b.end(), { 0xFF, 0x51, 3, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us });
        }
        b.insert(b.end(), { 0, 0xFF, 0x2F, 0 });
        track(b);
    }
    for (int t = 0; t < SONG_TRK; ++t) {
        if (!keep(t)) continue;
        const int ch = s.program[t] < 0 ? 9 : (t >= 9 ? std::min(15, t + 1) : t);
        std::vector<uint8_t> b; uint64_t last = 0;
        std::string nm = s.trackName[t]; if (voiceNames && !voiceNames[t].empty()) nm += " (" + voiceNames[t] + ")";
        b.push_back(0); b.push_back(0xFF); b.push_back(0x03); vlq(b, (uint32_t) nm.size()); b.insert(b.end(), nm.begin(), nm.end());
        if (s.program[t] >= 0) { b.push_back(0); b.push_back((uint8_t) (0xC0 | ch)); b.push_back((uint8_t) s.program[t]); }
        for (const auto& e : s.ev) {
            if (e.track != t) continue;
            const uint64_t tick = (uint64_t) std::llround(e.beat * tpq);
            vlq(b, (uint32_t) (tick - std::min(tick, last))); last = std::max(last, tick);
            const int hi = e.status & 0xF0;
            b.push_back((uint8_t) (hi | ch)); b.push_back(e.d1);
            if (hi != 0xC0 && hi != 0xD0) b.push_back(e.d2);
        }
        b.insert(b.end(), { 0, 0xFF, 0x2F, 0 });
        track(b);
    }
    return f;
}

// a new, empty song to write on the grid: 8 bars of 4/4 at 120
inline MidiSong emptySong(const std::string& name, double bpm = 120, int bars = 8) {
    MidiSong s; s.name = name; s.tempo.push_back({ 0.0, bpm }); s.num = 4; s.den = 4; s.minLength = s.length = bars * 4.0;
    const int prog[8] = { 48, 52, 33, 0, -1, 61, 73, 81 };
    const char* names[8] = { "Strings", "Choir", "Bass", "Piano", "Drums", "Brass", "Flute", "Lead" };
    for (int t = 0; t < SONG_TRK; ++t) {   // eight named tracks, eight more free
        s.program[t] = t < 8 ? prog[t] : 0; s.trackName[t] = t < 8 ? names[t] : "Track " + std::to_string(t + 1); s.used[t] = t < 8;
    }
    return s;
}

// the drum kit lookup a song engine uses
inline std::vector<std::string> splitBar(const std::string& s) { std::vector<std::string> o; size_t a = 0; while (a <= s.size()) { size_t b = s.find('|', a); if (b == std::string::npos) b = s.size(); if (b > a) o.push_back(s.substr(a, b - a)); a = b + 1; } return o; }

// GM drum notes on the CMI's drum disks (DRUMS, DRUMS1, CYMBALS, PERCUSS1/2, BIGMLTLU), best first
inline const char* drumVoices(int note) {
    switch (note) {
        case 35: case 36: return "KICK1|BDRUM1|BDRUM|KICK2|POWERKIK|KICK3";
        case 37: return "CROSSTIX|RRIM|STICK";
        case 38: case 40: return "SNARE1|LMSNARE|SNARE|MEDSNARE|SNARE2|SNAREA10";
        case 39: return "CLAP|CLAPS|CLAPTRAP|CLAP1";
        case 42: return "HHCLOSD|HIHAT1|HHCLOSD1|CLOSEDHH|HIHAT2";
        case 44: return "HHCLOSD1|HHCLOSD|HIHAT1|CLOSEDHH";
        case 46: return "HHOPEN1|HHOPNCL1|HIHAT3|HHOPNCL2";
        case 49: case 57: return "CYMSPLS1|CYMSPLS2|CYMB0";
        case 55: return "CYMSPLS2|CYMSPLS1|CYMB0";
        case 52: return "CYMB0|PANG|CYMSPLS1";
        case 51: case 59: return "CYMSTICK|CYMBELL1|CYMB0";
        case 53: return "CYMBELL1|CYMBELL2|CYMSTICK";
        case 41: case 43: return "FLOORTOM|TTOM|ROTOTOM";
        case 45: case 47: return "TTOM|TOMNEW|ROTOTOM";
        case 48: case 50: return "TOMNEW|TTOM|ROTOTOM";
        case 54: return "TAMBSHK|TAMBHIT|TAMBOUR1";
        case 56: return "COWBELL1|COWBELL2|COWBELOO";
        case 58: return "VIBSLP|VBRSLP";
        case 60: return "BONGHI|CCONGHI"; case 61: return "BONGLO|CCONGLO";
        case 62: case 63: return "CCONGHI|BONGHI"; case 64: return "CCONGLO|BONGLO";
        case 65: case 66: return "TIMBALI|CCONGLO";
        case 67: case 68: return "GOGO|SHTBELL";
        case 69: return "CABASA|CABASA1"; case 70: return "CASTANET|CABASA";
        case 71: case 72: return "WHIST2|WHISTLE";
        case 75: return "STICK|WOOD|PERCSTK"; case 76: case 77: return "WOOD|PERCSTK";
        case 80: case 81: return "TRIANGLE";
        default: return note < 41 ? "KICK1|BDRUM1|BDRUM" : note < 60 ? "TTOM|SNARE1" : "CCONGHI|BONGHI|GOGO";
    }
}

// the drum kit: a GM drum note -> the CMI drum voice loaded for it (connectSong loads them from the library)
inline std::function<int(int)> kitLookup(const Bank& bank) {
    return [&bank](int note) {
        for (const auto& v : splitBar(drumVoices(note))) { const int i = findVoice(bank, v); if (i >= 0) return i; }
        return -1;
    };
}

// ---- duplicates ----
// a song's title without the year in front, its file's version notes in brackets, and punctuation:
// "1984 - Frankie Goes To Hollywood - Relax (alt)" and "Frankie Goes To Hollywood - Relax" are the same song
inline std::string songKey(const std::string& name) {
    std::string s; int depth = 0;
    for (char c : name) { if (c == '(' || c == '[') ++depth; else if (c == ')' || c == ']') depth = std::max(0, depth - 1); else if (!depth) s += c; }
    // a year at the start ("1984 - ")
    { size_t i = 0; while (i < s.size() && s[i] == ' ') ++i;
      size_t d = i; while (d < s.size() && std::isdigit((unsigned char) s[d])) ++d;
      if (d - i == 4 && d < s.size() && (s[d] == ' ' || s[d] == '-' || s[d] == '_')) s = s.substr(d); }
    std::string k; for (char c : s) if (std::isalnum((unsigned char) c)) k += (char) std::tolower((unsigned char) c);
    // trailing version words
    for (const char* tail : { "alt", "alt1", "alt2", "remix", "maybe", "maybe1", "maybe2", "wip", "onlinesequencer" })
        { const std::string t = tail; if (k.size() > t.size() + 3 && k.compare(k.size() - t.size(), t.size(), t) == 0) k.resize(k.size() - t.size()); }
    return k;
}
inline size_t noteCount(const MidiSong& s) { size_t n = 0; for (const auto& e : s.ev) n += (e.status & 0xF0) == 0x90 && e.d2; return n; }

// One instance's songs (imported MIDI files), each with the voices and mutes chosen for its tracks. Songs are never
// changed in place: an edit makes a new version and the old ones stay alive (for undo, and for the audio thread,
// which may still be reading one), so the engine can hold a pointer to any of them.
struct SongEntry {
    std::unique_ptr<MidiSong> song;
    std::vector<std::unique_ptr<MidiSong>> undo, retired;
    int voice[SONG_TRK] {}; bool mute[SONG_TRK] {}; std::string why[SONG_TRK];
    DrumKit kit;   // the drum kit's pieces (GM_KIT tracks)
    uint64_t connectedGen = 0;   // the library scan its tracks were matched to (0: not yet)
    bool mono[SONG_TRK] {};      // a track played one note at a time (from the start: the parts whose notes never overlap)
    bool removed = false;
};
class SongList {
public:
    int add(MidiSong s, const Bank& bank) {
        auto e = std::make_unique<SongEntry>();
        s.id = ++nextId;
        e->song = std::make_unique<MidiSong>(std::move(s));
        autoSongVoices(*e->song, bank, e->voice, e->why);
        for (int t = 0; t < SONG_TRK; ++t) e->mono[t] = monoPart(*e->song, t);
        std::lock_guard<std::mutex> g(lock); items.push_back(std::move(e)); return (int) items.size() - 1;
    }
    int size() const { return (int) items.size(); }
    SongEntry* at(int i) { return i >= 0 && i < (int) items.size() && !items[(size_t) i]->removed ? items[(size_t) i].get() : nullptr; }
    const SongEntry* at(int i) const { return i >= 0 && i < (int) items.size() && !items[(size_t) i]->removed ? items[(size_t) i].get() : nullptr; }
    void remove(int i) { if (auto* e = at(i)) e->removed = true; }
    int count() const { int n = 0; for (auto& e : items) n += !e->removed; return n; }
    int nth(int k) const { for (int i = 0; i < size(); ++i) if (at(i) && k-- == 0) return i; return -1; }
    int indexOf(const MidiSong* s) const { if (!s) return -1; for (int i = 0; i < size(); ++i) if (at(i) && at(i)->song->id == s->id) return i; return -1; }
    // the same song already here (same file, or another version of the same title)
    int findDuplicate(const MidiSong& s) const {
        const std::string k = songKey(s.name);
        for (int i = 0; i < size(); ++i) if (const SongEntry* e = at(i)) if (e->song->raw == s.raw || (!k.empty() && songKey(e->song->name) == k)) return i;
        return -1;
    }
    // adds a song unless it's a duplicate; another version with more notes replaces the one here.
    // Returns the song's index; dup tells what happened (0 new, 1 skipped, 2 replaced the old version)
    int addUnique(MidiSong s, const Bank& bank, int& dup) {
        const int d = findDuplicate(s);
        if (d < 0) { dup = 0; return add(std::move(s), bank); }
        if (at(d)->song->raw == s.raw || noteCount(s) <= noteCount(*at(d)->song)) { dup = 1; return d; }
        remove(d); dup = 2; return add(std::move(s), bank);
    }
    // keeps the fullest version of each song; returns how many went
    int removeDuplicates() {
        int gone = 0;
        for (int i = 0; i < size(); ++i) {
            SongEntry* a = at(i); if (!a) continue;
            for (int j = i + 1; j < size(); ++j) {
                SongEntry* b = at(j); if (!b) continue;
                if (a->song->raw != b->song->raw && songKey(a->song->name) != songKey(b->song->name)) continue;
                if (noteCount(*b->song) > noteCount(*a->song)) { remove(i); ++gone; break; }
                remove(j); ++gone;
            }
        }
        return gone;
    }
private:
    std::vector<std::unique_ptr<SongEntry>> items; std::mutex lock; uint32_t nextId = 0;
};

// selecting a song on an engine: its voices and mutes go onto the track cards
// a loaded voice by "NAME@CATEGORY" (or just a name); -1 if it isn't loaded
inline int findVoiceAt(const Bank& bank, const std::string& v) {
    const size_t at = v.find('@');
    const std::string nm = v.substr(0, at), cat = at == std::string::npos ? "" : v.substr(at + 1);
    const int n = bank.count.load();
    for (int i = 0; i < n; ++i) {
        const Sound* x = bank.at(i);
        if (!x || i == NO_VOICE || (Library::userOf(i) >= 0 && x == Library::get().blank[(size_t) Library::userOf(i)])) continue;
        if (x->name == nm && (cat.empty() || x->category == cat)) return i;
    }
    return cat.empty() ? -1 : findVoiceAt(bank, nm);
}
// a song's kit piece over the pads' one: the song's sound (its tuning with it) if it chose one, else the pads' with
// the song's tuning added; levels multiply; the song's pan, mute and solo
inline KitPiece mergePiece(const KitPiece& pad, const KitPiece& song) {
    if (song.plain()) return pad;
    KitPiece m = pad;
    if (!song.voice.empty()) { m.voice = song.voice; m.tune = song.tune; } else m.tune += song.tune;
    m.level *= song.level; if (song.pan >= 0) m.pan = song.pan; m.mute |= song.mute; m.solo |= song.solo;
    if (song.a >= 0) m.a = song.a; if (song.d >= 0) m.d = song.d; if (song.s >= 0) m.s = song.s; if (song.r >= 0) m.r = song.r;
    if (song.f >= 0) m.f = song.f; if (song.lr >= 0) m.lr = song.lr; if (song.ld >= 0) m.ld = song.ld; if (!song.rr.empty()) m.rr = song.rr;
    return m;
}
// the kits into the engine: the pads' kit with the song's changes on top
inline void pushKit(Engine& eng) {
    eng.kitRROn = eng.padKit.rrOn;
    for (int n = 0; n < KIT_SLOTS; ++n) {
        const KitPiece p = mergePiece(eng.padKit.p[(size_t) n], eng.songKit.p[(size_t) n]);
        eng.kitSound[(size_t) n] = p.voice.empty() || !eng.bank ? -1 : findVoiceAt(*eng.bank, p.voice);
        eng.kitLevel[(size_t) n] = p.level; eng.kitPan[(size_t) n] = p.pan; eng.kitTune[(size_t) n] = p.tune;
        eng.kitMute[(size_t) n] = p.mute; eng.kitSolo[(size_t) n] = p.solo;
        const float* ke = eng.padKit.env;   // a pad's own envelope, else the kit's
        eng.kitEnvA[(size_t) n] = p.a >= 0 ? p.a : ke[0]; eng.kitEnvD[(size_t) n] = p.d >= 0 ? p.d : ke[1];
        eng.kitEnvS[(size_t) n] = p.s >= 0 ? p.s : ke[2]; eng.kitEnvR[(size_t) n] = p.r >= 0 ? p.r : ke[3];
        const float* kf = eng.padKit.fx;
        eng.kitFilt[(size_t) n] = p.f >= 0 ? p.f : kf[0]; eng.kitLfoR[(size_t) n] = p.lr >= 0 ? p.lr : kf[1]; eng.kitLfoD[(size_t) n] = p.ld >= 0 ? p.ld : kf[2];
        int rn = 0;
        if (!p.rr.empty() && eng.bank) { size_t a = 0; while (a < p.rr.size() && rn < 4) { size_t b = p.rr.find(';', a); if (b == std::string::npos) b = p.rr.size();
            const int v = findVoiceAt(*eng.bank, p.rr.substr(a, b - a)); if (v >= 0) eng.kitRR[(size_t) n][(size_t) rn++] = v; a = b + 1; } }
        eng.kitRRn[(size_t) n] = rn;
    }
}
inline void applyKit(Engine& eng, const DrumKit& songKit) { eng.songKit = songKit; pushKit(eng); }
inline void applyPads(Engine& eng, const DrumKit& pads) { eng.padKit = pads; pushKit(eng); }
// the GM drum notes a song's kit tracks play, low to high
inline std::vector<int> kitNotesOf(const SongEntry& e) {
    bool want[128] {};
    for (const auto& ev : e.song->ev) if ((ev.status & 0xF0) == 0x90 && ev.d2 && e.voice[ev.track] == GM_KIT) want[ev.d1] = true;
    std::vector<int> o; for (int n = 0; n < 128; ++n) if (want[n]) o.push_back(n);
    return o;
}

inline void selectSong(Engine& eng, SongEntry* e) {
    if (e) { for (int t = 0; t < SONG_TRK; ++t) { eng.songVoice[t] = e->voice[t]; eng.songMute[t] = e->mute[t]; eng.songMono[t] = e->mono[t]; } applyKit(eng, e->kit); }
    else applyKit(eng, DrumKit());   // no song: the pads' kit
    eng.songRun = false;
    eng.song = e ? e->song.get() : nullptr;
}

// an edit: the song with these notes becomes the current version (the engine plays on from where it is)
inline void editSong(Engine& eng, SongEntry* e, const std::vector<SongNote>& notes) {
    if (!e) return;
    auto ns = std::make_unique<MidiSong>(*e->song);
    setNotes(*ns, notes);
    ns->raw = writeSmf(*ns);
    const bool playing = eng.song.load() && eng.song.load()->id == ns->id;
    e->undo.push_back(std::move(e->song));
    if (e->undo.size() > 200) { e->retired.push_back(std::move(e->undo.front())); e->undo.erase(e->undo.begin()); }
    e->song = std::move(ns);
    if (playing) eng.song = e->song.get();
}
inline bool undoSong(Engine& eng, SongEntry* e) {
    if (!e || e->undo.empty()) return false;
    const bool playing = eng.song.load() && eng.song.load()->id == e->song->id;
    e->retired.push_back(std::move(e->song));
    e->song = std::move(e->undo.back()); e->undo.pop_back();
    if (playing) eng.song = e->song.get();
    return true;
}

// ---- rendering to audio ----
// The song (or one track of it) played on a fresh engine, as fast as the computer can, until it ends and its
// voices have died away. master / hiss / spread as the instrument is set.
struct Rendered { std::vector<float> L, R; double fs = 48000; };
inline Rendered renderSong(const Bank& bank, const SongEntry& e, double fs, int onlyTrack = -1, double master = 1, double hiss = 1, double spread = 0.3, float blend = 0.5f, const Engine::FxSettings* fx = nullptr) {
    Rendered r; r.fs = fs;
    auto eng = std::make_unique<Engine>();
    eng->bank = const_cast<Bank*>(&bank); eng->kitVoice = kitLookup(bank);
    if (fx) eng->applyFx(*fx);
    eng->prepare(fs); eng->setOutput(master, hiss, spread); eng->loopBlend = blend;
    eng->extSync = false;
    for (int t = 0; t < SONG_TRK; ++t) { eng->songVoice[t] = e.voice[t]; eng->songMute[t] = e.mute[t] || (onlyTrack >= 0 && t != onlyTrack); }
    eng->song = e.song.get(); eng->songRun = true;
    const int blk = 512; std::vector<float> l(blk), rr(blk);
    const size_t cap = (size_t) (fs * 60 * 30);   // half an hour at most
    int quiet = 0;
    while (r.L.size() < cap) {
        eng->render(l.data(), rr.data(), blk);
        r.L.insert(r.L.end(), l.begin(), l.end()); r.R.insert(r.R.end(), rr.begin(), rr.end());
        if (!eng->songRun.load()) {
            float pk = 0; for (int i = 0; i < blk; ++i) pk = std::max({ pk, std::fabs(l[(size_t) i]), std::fabs(rr[(size_t) i]) });
            quiet = pk < 0.0005f ? quiet + blk : 0;
            if (quiet > fs * 0.3 || (eng->activeVoices() == 0 && !eng->fxOn.load())) break;   // the REV7's tail rings out
        }
    }
    return r;
}

// Page R's song (patterns in song order) rendered once through
inline Rendered renderPageR(const Bank& bank, const SeqData& seq, double bpm, double fs, double master = 1, double hiss = 1, double spread = 0.3, float blend = 0.5f, const Engine::FxSettings* fx = nullptr) {
    Rendered r; r.fs = fs;
    auto eng = std::make_unique<Engine>();
    eng->bank = const_cast<Bank*>(&bank); if (fx) eng->applyFx(*fx); eng->prepare(fs); eng->setOutput(master, hiss, spread); eng->loopBlend = blend;
    eng->seq = seq; eng->extSync = false; eng->songMode = true; eng->tempo = bpm; eng->seqRun = true;
    const size_t len = (size_t) (seq.songLen * STEPS / (bpm / 60.0 * 4.0) * fs);
    const int blk = 512; std::vector<float> l(blk), rr(blk);
    int quiet = 0;
    while (r.L.size() < len + (size_t) (fs * 12)) {
        if (r.L.size() >= len) eng->seqRun = false;
        eng->render(l.data(), rr.data(), blk);
        r.L.insert(r.L.end(), l.begin(), l.end()); r.R.insert(r.R.end(), rr.begin(), rr.end());
        float pk = 0; for (int i = 0; i < blk; ++i) pk = std::max({ pk, std::fabs(l[(size_t) i]), std::fabs(rr[(size_t) i]) });
        quiet = pk < 0.0005f ? quiet + blk : 0;
        if (r.L.size() > len + (size_t) (fs * 0.5) && eng->activeVoices() == 0 && (!eng->fxOn.load() || quiet > fs * 0.3)) break;
    }
    return r;
}

// a WAV file's bytes: 16 or 24-bit PCM, stereo
inline std::vector<uint8_t> wavBytes(const Rendered& r, int bits = 24) {
    const uint32_t n = (uint32_t) r.L.size(), bps = (uint32_t) bits / 8, data = n * 2 * bps, fs = (uint32_t) std::lround(r.fs);
    std::vector<uint8_t> o; o.reserve(44 + data);
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((uint8_t) (v >> (8 * k))); };
    auto u16 = [&](uint16_t v) { o.push_back((uint8_t) v); o.push_back((uint8_t) (v >> 8)); };
    o.insert(o.end(), { 'R', 'I', 'F', 'F' }); u32(36 + data); o.insert(o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    u32(16); u16(1); u16(2); u32(fs); u32(fs * 2 * bps); u16((uint16_t) (2 * bps)); u16((uint16_t) bits);
    o.insert(o.end(), { 'd', 'a', 't', 'a' }); u32(data);
    const double full = bits == 24 ? 8388607.0 : 32767.0;
    for (uint32_t i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c) {
            const int32_t v = (int32_t) std::lround(clampd(c ? r.R[i] : r.L[i], -1, 1) * full);
            for (uint32_t k = 0; k < bps; ++k) o.push_back((uint8_t) (v >> (8 * k)));
        }
    return o;
}

}  // namespace cmi
