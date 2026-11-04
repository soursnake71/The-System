// Your CMI library as a catalogue: every voice on every disk image (and every .VC file) in your library folder,
// indexed once, so a MIDI song can be connected to the original CMI voices it was made with, not stand-ins.
//
// Matching a track: what you chose for this song before (remembered), then a voice named in the track's name
// ("ORCH5", "ARR1"), then the voices famous records are known to use, then the track's role (from its name and
// General MIDI program) looked up in the library the way the CMI library is organised: by disk (STRINGS, CHORAL,
// BASSES, BRASS, DRUMS, CYMBALS...), preferring the well-known voices on it. Drum tracks get a kit: a CMI drum
// voice for each GM drum note. Only voices from your disks are used.
#pragma once
#include "Disk.h"
#include "Song.h"
#include <map>

namespace cmi {

struct CatVoice { std::string name, disk; std::vector<uint8_t> vc; uint64_t hash = 0; bool looped = false; };

class Catalog {
public:
    std::vector<CatVoice> voices;
    int disks = 0;

    static uint64_t fnv(const uint8_t* d, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; } return h; }
    // a file from the library folder: a disk image (all its voices) or a .VC file; anything else is ignored
    void add(const std::string& fileName, const uint8_t* d, size_t n, const std::string& folderName = "") {
        std::string lo = fileName; for (auto& c : lo) c = (char) std::tolower((unsigned char) c);
        auto ext = [&](const char* e) { const size_t l = std::strlen(e); return lo.size() > l && lo.compare(lo.size() - l, l, e) == 0; };
        auto put = [&](const std::string& name, const std::string& disk, const std::vector<uint8_t>& vc) {
            if (vc.size() < 0x1580 + 4096) return;
            const uint64_t h = fnv(vc.data() + 0x1580, vc.size() - 0x1580);
            for (const auto& v : voices) if (v.hash == h && v.name == name) return;   // the same voice in the VC folder and on the disk
            CatVoice c; c.name = name; c.disk = disk; c.vc = vc; c.hash = h; c.looped = vc.size() > 0x133B && vc[0x133B] == 1;
            voices.push_back(std::move(c));
        };
        if (ext(".imd") || ext(".img") || ext(".bin") || ext(".raw") || ext(".dsk")) {
            Disk dk = readDiskImage(d, n);
            if (!dk.err.empty()) return;
            ++disks;
            std::string label = dk.label; if (label.empty()) label = fileName.substr(0, fileName.find('.'));
            for (const auto& f : dk.files) if (f.type == "VC") put(f.name, label, f.data);
        } else if (ext(".vc")) {
            std::string nm = fileName.substr(0, fileName.size() - 3); for (auto& c : nm) c = (char) std::toupper((unsigned char) c);
            put(nm, folderName, std::vector<uint8_t>(d, d + n));
        }
    }
    int find(const std::string& name, const std::string& diskPrefix = "") const {
        for (size_t i = 0; i < voices.size(); ++i) if (voices[i].name == name && (diskPrefix.empty() || voices[i].disk.rfind(diskPrefix, 0) == 0)) return (int) i;
        return -1;
    }
    // into a Sound for a channel card
    bool sound(int i, Sound& out) const { if (i < 0 || i >= (int) voices.size()) return false; std::string err; return vcToSound(voices[(size_t) i].vc, voices[(size_t) i].name, voices[(size_t) i].disk, out, err); }
};

// ---- roles: what a track plays, and where the CMI library keeps it ----
struct Role { const char* name; const char* disks; const char* voices; bool sustained; };
inline const std::vector<Role>& roles() {
    static const std::vector<Role> r = {
        { "orch hit", "STRINGS", "ORCH5|ORCH2|ORCHFZ1", false },
        { "choir", "CHORAL|VOICE", "ARR1|SARARR1|SARARR|AHH1|ALTO1|OOHHI|TENOR2", true },
        { "strings", "STRINGS", "ENSEMBLE|STRINGS1|STRINGS|VIOLINS|VLNTREMG|TENSEMBL", true },
        { "pizzicato", "STRINGS|PLUCK", "PIZZ|PIZZ1|PIZZICAT", false },
        { "cello", "STRINGS", "CELLO|CELLO2|CELLO1", true },
        { "brass", "BRASS", "BRASS1|HORNENSM|PRAISBRS|FHORN1|TRPMID1", true },
        { "trumpet", "BRASS", "TRPHI1|TRPMID1|TRPHI2|SYNTRUMP", true },
        { "synth brass", "ANASYNTH|BRASS", "MBRASS|MBRASS2|SYNTRUMP|SYNTRP2", true },
        { "sax", "REEDS|WOODWIND", "SAX|TENSAX|ALTOSAX|SAXOPHON|CLARINET|OBOE|BASSOON", true },
        { "flute", "WOODWIND|REEDS", "FLUTE|FLUTE1|PANPIPE|SHAKU|RECORDER", true },
        { "bass", "BASSES", "BASSGT|BASS1|PICKBASS|BASS2|ACCBASS1", false },
        { "fretless", "BASSES", "BASSWOW|DBLBASS|BASSO", false },
        { "slap bass", "BASSES", "BASSPOP|BASSRICK", false },
        { "synth bass", "BASSES|ANASYNTH", "SYNBSS1|SYNBSS2|BASSYNT|BASSZOW|SYNBSS3", false },
        { "piano", "PIANO", "PIANO|PIANO1|GRAND|BRIGHT", false },
        { "electric piano", "ELECKEYB", "RHODES1|ELECPIAN|RHODES4|WURLITZ|PHASPIAN", false },
        { "clav", "ELECKEYB|KEYBRDS", "CLAV|CLAV2|DIRTCLAV", false },
        { "organ", "KEYBRDS|KYBD", "ORGAN1|ORGAN2|ELECORG1|LESLIE1|ORGMAJOR", true },
        { "harpsichord", "KEYBRDS|KYBD|PLUCK", "HARPSIM1|SPINET1|HARPSI", false },
        { "guitar", "GUITARS", "GUITAR1|CGUITAR|GTRAC|GUITAR2|LONGTR", false },
        { "dist guitar", "GUITARS", "FUZZGTR|FUZZGTR1|DIRTGTR|WAILGTR|STRATX", true },
        { "plucked", "PLUCK", "KOTO|SITAR|HARP|BANJO|MANDOLIN", false },
        { "mallets", "MALLETS", "MARIMBA|VIBMID|XYLOP1|GLOCK", false },
        { "steel drum", "MALLETS", "STLDRUM|STEEL1", false },
        { "bells", "BELLS", "CHIME|SYNBELL|BOWL1|MUSICBOX|GLASS4", false },
        { "synth lead", "ANASYNTH", "MSAWTOOF|TSQUARE1|MWA|ZOW1", true },
        { "synth pad", "ANASYNTH|STRINGS", "ENSEMBLE|TENSEMBL|MDEEYAWL|MBWOWSUS", true },
        { "timpani", "DRUMS|PERCUSS", "TYMPANI|TIMPANI", false },
        { "toms", "DRUMS", "TTOM|FLOORTOM|ROTOTOM|TOMNEW", false },
        { "effects", "EFFECTS", "SMASH|BOMB|XPLODE|BREAKGLS", false },
    };
    return r;
}

// the role for a track: its name, then its General MIDI program
inline int roleOf(const MidiSong& s, int t) {
    auto R = [](const char* n) { const auto& r = roles(); for (size_t i = 0; i < r.size(); ++i) if (!std::strcmp(r[i].name, n)) return (int) i; return 0; };
    struct K { const char* keys; const char* role; };
    static const K byName[] = {
        { "orchhit|orchestrahit|orchestraattack|orchstab|stab|hit", "orch hit" }, { "choir|choral|chorus|vox|voice|vocal|aah|ooh|singer", "choir" },
        { "pizz", "pizzicato" }, { "cello", "cello" }, { "synthbrass|synbrass", "synth brass" }, { "trump|trp", "trumpet" },
        { "brass|horn|tromb|tuba", "brass" }, { "sax|bassoon|oboe|clarin|reed", "sax" }, { "flute|piccolo|recorder|pipe|shaku|whistle", "flute" },
        { "slap", "slap bass" }, { "fretless", "fretless" }, { "synthbass|synbass|moog", "synth bass" }, { "bass", "bass" },
        { "rhodes|elecpiano|electricpiano|epiano1|wurl|tinepiano", "electric piano" }, { "clav", "clav" }, { "organ", "organ" }, { "harpsi", "harpsichord" },
        { "piano|grand|pno", "piano" }, { "distort|odrive|overdrive|fuzz|lead gtr", "dist guitar" }, { "guitar|gtr|geetar|nylon|steelg", "guitar" },
        { "koto|sitar|harp|banjo|mandolin", "plucked" }, { "marimba|vibe|xylo|mallet|glock|celest", "mallets" }, { "steeldrum|steelpan", "steel drum" },
        { "bell|chime", "bells" }, { "timp", "timpani" }, { "tom|taiko", "toms" }, { "string|str|violin|viola|ensem|orch", "strings" },
        { "pad|sweep|atmos|space", "synth pad" }, { "lead|solo|synth|saw|square", "synth lead" }, { "gun|shot|explo|bomb|crash|fx|effect", "effects" },
    };
    const std::string nm = compact(s.trackName[t]);
    // track names that say nothing: the same name on most tracks (often the song's title), or none
    int same = 0, usedN = 0; for (int u = 0; u < SONG_TRK; ++u) if (s.used[u]) { ++usedN; same += s.trackName[u] == s.trackName[t]; }
    const bool namesMean = !nm.empty() && !(usedN > 2 && same * 2 > usedN);
    for (const auto& k : byName) {
        if (!namesMean) break;
        std::string keys = k.keys; size_t a = 0;
        while (a <= keys.size()) { size_t b = keys.find('|', a); if (b == std::string::npos) b = keys.size(); const std::string key = keys.substr(a, b - a); a = b + 1;
            if (!key.empty() && key.size() > 3 ? nm.find(key) != std::string::npos : (!key.empty() && nm.rfind(key, 0) == 0)) return R(k.role); }
    }
    const int p = s.program[t];
    if (p < 0) return -1;
    static const char* gm[16] = { "piano", "mallets", "organ", "guitar", "bass", "strings", "strings", "brass", "sax", "flute", "synth lead", "synth pad", "synth pad", "plucked", "toms", "effects" };
    if (p == 4 || p == 5) return R("electric piano"); if (p == 6) return R("harpsichord"); if (p == 7) return R("clav");
    if (p == 14) return R("bells"); if (p == 29 || p == 30) return R("dist guitar"); if (p == 35) return R("fretless"); if (p == 36 || p == 37) return R("slap bass");
    if (p == 38 || p == 39) return R("synth bass"); if (p == 42 || p == 43) return R("cello"); if (p == 45) return R("pizzicato"); if (p == 47) return R("timpani");
    if (p == 52 || p == 53 || p == 54 || p == 85 || p == 91) return R("choir"); if (p == 55) return R("orch hit"); if (p == 56 || p == 59) return R("trumpet");
    if (p == 62 || p == 63) return R("synth brass"); if (p == 114) return R("steel drum");
    return R(gm[p / 8]);
}

// records whose CMI voices are well known (the orchestra stab, the ARR1 voice...): those voices first, for the
// tracks whose role fits
struct Famous { const char* key; const char* voice; const char* roles; };
inline const std::vector<Famous>& famousSongs() {
    static const std::vector<Famous> f = {
        { "planetrock", "ORCH5", "orch hit|brass|synth brass|strings|effects" }, { "thedreaming", "ORCH5", "orch hit|brass|strings" },
        { "closetotheedit", "ORCH5", "orch hit|brass|strings|effects" }, { "ownerofalonelyheart", "ORCH5", "orch hit|brass|synth brass" },
        { "tearsforfearsshout", "ARR1", "choir|synth pad|synth lead" }, { "crockettstheme", "ARR1", "choir|synth lead|flute" },
        { "miamivicetheme", "ARR1", "choir|synth pad" },
    };
    return f;
}

// GM drum notes on the CMI's drum disks
inline const char* drumVoices(int note) {
    switch (note) {
        case 35: case 36: return "KICK1|BDRUM|BDRUM1|KICK2|POWERKIK";
        case 37: return "CROSSTIX|RRIM";
        case 38: case 40: return "SNARE1|LMSNARE|SNARE|MEDSNARE|SNARE2";
        case 39: return "CLAP|CLAPS|HANDCLAP";
        case 42: case 44: return "HHCLOSD|HIHAT1|CLOSEDHH|HHCLOSD1|HIHAT2";
        case 46: return "HHOPEN1|HHOPNCL1|HIHAT3";
        case 49: case 57: case 55: case 52: return "CYMSPLS1|CYMB0|CYMSPLS2";
        case 51: case 59: case 53: return "CYMBELL1|CYMBELL2|CYMSTICK";
        case 41: case 43: case 45: return "FLOORTOM|TTOM|ROTOTOM";
        case 47: case 48: case 50: return "TTOM|TOMNEW|ROTOTOM";
        case 56: return "COWBELL|GOGO|SHTBELL";
        case 54: case 69: case 70: return "CABASA1|TAMB|SHAKER";
        default: return note < 41 ? "KICK1|BDRUM" : note < 60 ? "TTOM|SNARE1" : "CONGA|BONGO|TIMBALI|GOGO";
    }
}

struct TrackMatch { int catIndex = -1; bool kit = false; std::string why; };

inline std::vector<std::string> splitBar(const std::string& s) { std::vector<std::string> o; size_t a = 0; while (a <= s.size()) { size_t b = s.find('|', a); if (b == std::string::npos) b = s.size(); if (b > a) o.push_back(s.substr(a, b - a)); a = b + 1; } return o; }

// the library voice for one track (remembered: "NAME@DISK" from an earlier choice, or empty)
inline TrackMatch matchTrack(const MidiSong& s, int t, const Catalog& cat, const std::string& remembered = "") {
    TrackMatch m;
    if (!remembered.empty()) {
        const size_t at = remembered.find('@');
        const int i = cat.find(remembered.substr(0, at), at == std::string::npos ? "" : remembered.substr(at + 1));
        if (i >= 0) { m.catIndex = i; m.why = "your choice for this song"; return m; }
        if (remembered == "KIT") { m.kit = true; m.why = "your choice: drum kit"; return m; }
    }
    if (s.program[t] < 0) { m.kit = true; m.why = "drum channel: CMI drum disks"; return m; }
    // a voice named in the track's name (a real voice name: not an everyday instrument word, not the same name on
    // most tracks, which is usually the song's title)
    int same = 0, usedN = 0; for (int u = 0; u < SONG_TRK; ++u) if (s.used[u]) { ++usedN; same += s.trackName[u] == s.trackName[t]; }
    static const char* generic[] = { "BASS", "ORGAN", "PIANO", "STRINGS", "STRING", "CHOIR", "BRASS", "DRUMS", "DRUM", "LEAD", "VOICE", "SYNTH", "GUITAR", "PAD",
                                     "BELLS", "FLUTE", "SAX", "HORN", "HORNS", "PERC", "SOLO", "MELODY", "CHORDS", "KEYS", "TRACK", "ELECTRIC", "CLASSIC", "ACOUSTIC" };
    if (!(usedN > 2 && same * 2 > usedN)) {
        std::string w; std::vector<std::string> words;
        for (char c : s.trackName[t] + " ") { if (std::isalnum((unsigned char) c)) w += (char) std::toupper((unsigned char) c); else { if (w.size() >= 3) words.push_back(w); w.clear(); } }
        for (const auto& wd : words) {
            bool gen = false; for (const char* g : generic) gen |= wd == g;
            if (gen || s.name.find(wd) != std::string::npos) continue;
            std::string titleUp = s.name; for (auto& c : titleUp) c = (char) std::toupper((unsigned char) c);
            if (titleUp.find(wd) != std::string::npos) continue;
            const int i = cat.find(wd); if (i >= 0) { m.catIndex = i; m.why = "named in the track"; return m; }
        }
    }
    const int role = roleOf(s, t);
    if (role < 0) { m.kit = true; m.why = "drums"; return m; }
    const Role& r = roles()[(size_t) role];
    if (!std::strcmp(r.name, "toms") && shapeOf(s, t).drumKeys && compact(s.trackName[t]).find("drum") != std::string::npos) { m.kit = true; m.why = "drum part"; return m; }
    // famous records
    const std::string key = songKey(s.name);
    for (const auto& f : famousSongs())
        if (key.find(f.key) != std::string::npos)
            for (const auto& rn : splitBar(f.roles)) if (rn == r.name) { const int i = cat.find(f.voice); if (i >= 0) { m.catIndex = i; m.why = std::string("the ") + f.voice + " this record is known for"; return m; } }
    // the role's well-known voices, then any voice on its disks (looped for sustained parts)
    for (const auto& v : splitBar(r.voices)) { const int i = cat.find(v); if (i >= 0) { m.catIndex = i; m.why = std::string(r.name) + " from the CMI library"; return m; } }
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& d : splitBar(r.disks))
            for (size_t i = 0; i < cat.voices.size(); ++i)
                if (cat.voices[i].disk.rfind(d, 0) == 0 && (pass == 1 || cat.voices[i].looped == r.sustained)) { m.catIndex = (int) i; m.why = std::string(r.name) + " from the " + cat.voices[i].disk + " disk"; return m; }
    m.why = std::string("no ") + r.name + " in your library";
    return m;
}

// Connects a song to the library: loads each track's voice into the user voices (once, reusing one already there)
// and sets the drum kit. remembered: track -> "NAME@DISK" (or "KIT"). Returns how many tracks got a library voice.
inline int connectSong(SongEntry& e, Kit& kit, const Catalog& cat, const std::map<int, std::string>& remembered = {}) {
    auto loadVoice = [&](int ci) -> int {
        const CatVoice& cv = cat.voices[(size_t) ci];
        for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->name == cv.name && kit.user(k)->category == cv.disk.substr(0, 10)) return kit.userSlot(k);
        const int slot = kit.firstFree(); if (slot < 0) return -1;
        Sound snd; if (!cat.sound(ci, snd)) return -1;
        kit.setUser(slot, std::move(snd)); return kit.userSlot(slot);
    };
    int n = 0; bool needKit = false;
    for (int t = 0; t < SONG_TRK; ++t) {
        if (!e.song->used[t]) continue;
        auto it = remembered.find(t);
        const TrackMatch m = matchTrack(*e.song, t, cat, it == remembered.end() ? "" : it->second);
        if (m.kit) { e.voice[t] = GM_KIT; e.why[t] = m.why; needKit = true; ++n; continue; }
        if (m.catIndex < 0) { e.why[t] = m.why + " (kept " + std::string("the voice it had") + ")"; continue; }
        const int v = loadVoice(m.catIndex);
        if (v < 0) { e.why[t] = "user voices full"; continue; }
        e.voice[t] = v; e.why[t] = cat.voices[(size_t) m.catIndex].name + " (" + cat.voices[(size_t) m.catIndex].disk + "): " + m.why; ++n;
    }
    // the drum kit: the CMI drum voices the song's drum notes need
    if (needKit) {
        bool want[128] {};
        for (const auto& ev : e.song->ev) if ((ev.status & 0xF0) == 0x90 && ev.d2 && e.voice[ev.track] == GM_KIT) want[ev.d1] = true;
        for (int note = 0; note < 128; ++note) {
            if (!want[note]) continue;
            for (const auto& v : splitBar(drumVoices(note))) { const int ci = cat.find(v); if (ci >= 0) { loadVoice(ci); break; } }
        }
    }
    return n;
}

// the kit lookup when CMI drum voices are loaded: they come first, the built-in drums after
inline std::function<int(int)> cmiKitLookup(const Bank& bank) {
    return [&bank](int note) {
        for (const auto& v : splitBar(drumVoices(note))) { const int i = findVoice(bank, v); if (i >= Library::get().builtIn) return i; }
        return kitLookup(bank)(note);
    };
}

}  // namespace cmi
