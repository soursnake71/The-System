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
        if (ext(".imd") || ext(".img") || ext(".hfe") || ext(".bin") || ext(".raw") || ext(".dsk")) {
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

// which of the library folder's files the catalogue reads: 0 none, 1 a disk image (read first: its label names its
// voices' disk), 2 a .VC file. hfeOnly, when the folder has HFE images: only those, so each disk is read once and
// from its rawest copy (the IMG / IMD / VC copies of the same disks are left alone)
inline bool endsWithNoCase(const std::string& s, const char* e) { const size_t l = std::strlen(e); if (s.size() < l) return false; for (size_t i = 0; i < l; ++i) if (std::tolower((unsigned char) s[s.size() - l + i]) != e[i]) return false; return true; }
inline int libraryRole(const std::string& fileName, bool anyHfe, bool hfeOnly) {
    const bool hfe = endsWithNoCase(fileName, ".hfe");
    if (hfeOnly && anyHfe) return hfe ? 1 : 0;
    if (hfe || endsWithNoCase(fileName, ".imd") || endsWithNoCase(fileName, ".img") || endsWithNoCase(fileName, ".bin") || endsWithNoCase(fileName, ".raw") || endsWithNoCase(fileName, ".dsk")) return 1;
    return endsWithNoCase(fileName, ".vc") ? 2 : 0;
}

// ---- roles: what a track plays, and where the CMI library keeps it (voice names as on the IIx factory disks) ----
// disks: prefixes of the disk labels and file / folder names; voices: the best-known first
struct Role { const char* name; const char* disks; const char* voices; bool sustained; };
inline const std::vector<Role>& roles() {
    static const std::vector<Role> r = {
        { "orch hit", "STRINGS4", "ORCH5|ORCH2|ORCHFZ1|SFZAN1|LOSTAB1", false },
        { "choir", "CHORAL|VOICE", "ARR1|SARARR1|AHH1|ARR|AH2|OOHHI|ALTO1|TENOR2|MMM|LA", true },
        { "voice pad", "VOICE|CHORAL", "VOX|VOX1|AH2|MOAN|MMM|OOHHI", true },
        { "strings", "STRINGS", "STRNGENS|BIGSTRN1|STRSUS|STRMID|BIGSTRN2|STRCHOIR|NEWVLNS|FENSTRNG|BOBSTRNG|STRINGO", true },
        { "violin", "STRINGS", "VIOLSUS|VIOLMID|VNSMID|VLNHIA|VIOLHI2|VIOLINL1|STGESOLO|CFIDDLE", true },
        { "tremolo strings", "STRINGS4", "VTREMOLO|VLNTREMG|VLNTREM2|VLNSHIM|SHIMMER|VLNTREMA", true },
        { "pizzicato", "STRINGS", "PIZZOPEN|VPIZZ|PIZZLOW3|PIZZHI|PIZZLOW|VLNPIZZA|PIZZLOW1|VIOLAPIZ", false },
        { "cello", "STRINGS", "CELLO2|CELLO1|CELLO3|TRAMCHLO", true },
        { "contrabass", "STRINGS", "BOWDBASS|DBLBASS1|BOWDBAS4|LOWSTR1", true },
        { "brass", "BRASS", "BRASS1|PRAISBRS|HORNENSM|FHORN1|TRPMID1", true },
        { "trumpet", "BRASS", "TRPMID1|TRPHI1|TRPHI2|TRPTJ2|GS1TRUMP|TRIP1", true },
        { "trombone", "BRASS", "TROMBONE|TRMBNMID|FLGLBONE", true },
        { "tuba", "BRASS", "UPHONIUM|TROMBONE", true },
        { "french horn", "BRASS", "FHORN1|FHORN2|GS1HORN|OLDHORN|CORHI|HORNENSM", true },
        { "synth brass", "ANASYNTH|ANALOG|BRASS2", "MBRASS|MBRASS2|SYNTRUMP|SYNTRP2", true },
        { "sax", "REEDS", "SAX|TENOSAX1|TENRSAX|SOPSAX|SAXZ|DELSAX2|TENOSAXY", true },
        { "bari sax", "REEDS", "BARISAX|BARISAX1|BARISAX2", true },
        { "oboe", "REEDS", "OBOE1|OBOE2", true },
        { "bassoon", "REEDS", "BASSOONE|BASSCLAR", true },
        { "clarinet", "REEDS", "CLARMID2|CLARHI2|CLARMDHI|BASSCLAR|BASCLAR2", true },
        { "flute", "WOODWIND", "FLUTE|FLUTE2|FLUTE3|JAZFLUTE|FLTEE|FLTCF", true },
        { "pan flute", "WOODWIND", "NAYPRO|FLUPAD|OCARINA|FLUTE2", true },
        { "whistle", "WOODWIND|CHORAL|VOICE", "WHISTLE|WHIST2|WHIST5", true },
        { "harmonica", "WOODWIND", "HARMONKA|HARMNIKA", true },
        { "bagpipe", "WOODWIND", "BAGPIPE1|BAGPIPE2", true },
        { "bass", "BASS", "BASSGT|BASS1|BASS2|BASS3|BASSGTR2|BASS4", false },
        { "pick bass", "BASS", "PICKBASS|BASSRICK|BASSGT", false },
        { "fretless", "BASS", "BASSWOW|BASSO|BASSGT", false },
        { "slap bass", "BASS", "BASSPOP|BASSRICK", false },
        { "acoustic bass", "BASS|STRINGS", "ACCBASS1|DBLBASS|DBLBASS1", false },
        { "synth bass", "BASS|ANASYNTH|ANALOG", "SYNBSS1|SYNBSS2|SYNBSS3|BASSYNT|BASSZOW|BASS5", false },
        { "piano", "PIANO", "GRAND1LO|PIANOM1|P1|PIANOLL1|PIA440|GRAND1HI|PIANOH1", false },
        { "honky tonk", "PIANO", "HONKY1|PIANOLL2", false },
        { "electric piano", "ELECKEYB|ELECTKBD|PIANOS", "RHODES1|RHODES4|ELECPIAN|RHODES2|WURLITZ|PHASPIAN|WURLI", false },
        { "clav", "ELECKEYB|ELECTKBD|KEYBRDS", "CLAV|CLAV2|DIRTCLAV|MCLAV", false },
        { "organ", "KEYBRDS|KYBD", "ORGAN1|ORGAN2|ELECORG1|LESLIE1|ORGMAJOR|ORGAN3", true },
        { "church organ", "KEYBRDS|KYBD", "CHURHORG|ORGAN13|REGAL|ORGAN1", true },
        { "harmonium", "KEYBRDS|KYBD", "HARMONUM|MELODIUM", true },
        { "harpsichord", "KEYBRDS|KYBD", "HARPSIM1|SPINET1", false },
        { "celesta", "ELECKEYB|ELECTKBD|KEYBRDS", "CELESTE|CELESTE7|MUSICBOX", false },
        { "music box", "BELLS", "MUSICBOX|CELESTE", false },
        { "glockenspiel", "MALLETS", "GLOCK|XYLOP2", false },
        { "vibes", "MALLETS", "VIBMID|VIBLO|VIBHI|VIBUP|VIBOCT", false },
        { "marimba", "MALLETS", "MARIMBA|BASSMAR", false },
        { "xylophone", "MALLETS", "XYLOP1|XYLOPHNE|XYLOP2", false },
        { "steel drum", "MALLETS", "STLDRUM|STEEL1|STEEL2|STLDRUM2", false },
        { "tubular bells", "BELLS", "CHIME|CHYME|BELLTREE", false },
        { "bells", "BELLS", "SYNBELL|CHIME|BOWL1|GLASS4|SLBELLS|JINGBELL", false },
        { "guitar", "GUITAR", "CGUITAR|GTRAC|GUITAR1|GUITAR2|GUITAR3", false },
        { "steel guitar", "GUITAR", "GTARAA|GUITAR3|GTRAC|GUITAR1", false },
        { "electric guitar", "GUITAR", "GTRM1|GTRL1|STRATX|LONGTR|GTRHI1|GTRL2", false },
        { "muted guitar", "GUITAR", "GMUFF|GMUFFA|GMUFX", false },
        { "dist guitar", "GUITAR", "FUZZGTR|FUZZGTR1|DIRTGTR|WAILGTR|FUZZGTR2|FUZZGTR3", true },
        { "harp", "PLUCK|KEYBRDS", "HARPARP|HARPUPGD|HARPDNGD|FLUHARP", false },
        { "koto", "PLUCK", "KOTO|KOTO2|KOTSTRM|KOTOBEND", false },
        { "sitar", "PLUCK", "SITAR2|BAZOUKI", false },
        { "banjo", "PLUCK", "BANJO|BAZOUKI", false },
        { "plucked", "PLUCK", "ZITHER|BAZOUKI|ZITHER2|KOTO", false },
        { "kalimba", "PERCUS", "KALIMHI|KALIMLO", false },
        { "synth lead", "ANASYNTH|ANALOG", "MSAWTOOF|TSQUARE1|MWA|ZOW1|MDEEYAWL|MDWEEEEE", true },
        { "synth pad", "ANASYNTH|ANALOG|MODE1", "ENSEMBLE|TENSEMBL|MBWOWSUS|POLYMOOG|SWEEP1|WASH2", true },
        { "sweep pad", "MODE1|ANASYNTH|ANALOG", "SWEEP1|SWEEP2|SWEEP3|BASSWEEP|WASH2|MBWOWSUS", true },
        { "synth fx", "MODE1|ANASYNTH|ANALOG", "SAMPHOLD|MDOOTWA|SPACY|META|SATURN5", false },
        { "timpani", "DRUMS", "TYMPANI", false },
        { "toms", "DRUMS", "TTOM|FLOORTOM|ROTOTOM|TOMNEW", false },
        { "taiko", "DRUMS", "BIKODRUM|DRUMBO|FLOORTOM", false },
        { "synth drum", "BIGMLTLU|BIGMELU", "SYNDRUM1|ROTOTOM", false },
        { "effects", "EFFECTS|SFX", "SMASH|BOMB|XPLODE|BREAKGLS", false },
        { "seashore", "WEATHER", "SURF|WWIND|GALE", true },
        { "birds", "ANIMALS", "BIRD1|BIRD2|CHIRP|BIRD3", false },
        { "telephone", "EFFECTS|SFX|BELLS", "PHONE|WPHONE|TELBEL", false },
        { "helicopter", "EFFECTS|SFX", "HELICPTR", true },
        { "applause", "EFFECTS|SFX|VOICE", "PEOPLE|SHOUT", false },
        { "gunshot", "EFFECTS|SFX", "MGUN1|XPLODE|RICOCHET|BOMB", false },
        { "thunder", "WEATHER", "THNDR1|THNDRCRK|THNDR2", false },
        { "fret noise", "GUITAR", "GITCRS|SALTUP", false },
    };
    return r;
}
inline int roleIndex(const char* n) { const auto& r = roles(); for (size_t i = 0; i < r.size(); ++i) if (!std::strcmp(r[i].name, n)) return (int) i; return -1; }

// General MIDI programs 1-128 to roles
inline const char* gmRole(int p) {
    static const char* g[128] = {
        "piano", "piano", "piano", "honky tonk", "electric piano", "electric piano", "harpsichord", "clav",
        "celesta", "glockenspiel", "music box", "vibes", "marimba", "xylophone", "tubular bells", "plucked",
        "organ", "organ", "organ", "church organ", "harmonium", "harmonium", "harmonica", "harmonium",
        "guitar", "steel guitar", "electric guitar", "electric guitar", "muted guitar", "dist guitar", "dist guitar", "electric guitar",
        "acoustic bass", "bass", "pick bass", "fretless", "slap bass", "slap bass", "synth bass", "synth bass",
        "violin", "violin", "cello", "contrabass", "tremolo strings", "pizzicato", "harp", "timpani",
        "strings", "strings", "synth pad", "synth pad", "choir", "voice pad", "voice pad", "orch hit",
        "trumpet", "trombone", "tuba", "trumpet", "french horn", "brass", "synth brass", "synth brass",
        "sax", "sax", "sax", "bari sax", "oboe", "oboe", "bassoon", "clarinet",
        "flute", "flute", "flute", "pan flute", "whistle", "pan flute", "whistle", "pan flute",
        "synth lead", "synth lead", "synth lead", "synth lead", "synth lead", "voice pad", "synth lead", "synth lead",
        "synth pad", "sweep pad", "synth pad", "synth pad", "synth pad", "synth pad", "sweep pad", "sweep pad",
        "synth fx", "synth pad", "bells", "synth pad", "synth pad", "voice pad", "synth pad", "synth fx",
        "sitar", "banjo", "plucked", "koto", "kalimba", "bagpipe", "violin", "flute",
        "bells", "toms", "steel drum", "toms", "taiko", "toms", "synth drum", "effects",
        "fret noise", "effects", "seashore", "birds", "telephone", "helicopter", "applause", "gunshot",
    };
    return p >= 0 && p < 128 ? g[p] : nullptr;
}

// the role for a track: its name, then its General MIDI program (-1: drums)
inline int roleOf(const MidiSong& s, int t) {
    struct K { const char* keys; const char* role; };
    static const K byName[] = {
        { "orchhit|orchestrahit|orchestraattack|orchstab|stab|hit", "orch hit" }, { "choir|choral|aah|ooh|singer|chorus", "choir" },
        { "vox|voice|vocal", "voice pad" }, { "pizz", "pizzicato" }, { "trem", "tremolo strings" }, { "cello", "cello" },
        { "contrabass|doublebass|uprightbass|acoustbass|acobass|accobass", "acoustic bass" },
        { "violin|fiddle|viola", "violin" }, { "synthbrass|synbrass", "synth brass" }, { "trump|trp", "trumpet" },
        { "trombone|tromb", "trombone" }, { "tuba|euphon", "tuba" }, { "frenchhorn|frhorn|fhorn", "french horn" }, { "brass|horn", "brass" },
        { "barisax|baritone", "bari sax" }, { "sax", "sax" }, { "oboe|englishhorn", "oboe" }, { "bassoon", "bassoon" }, { "clarin", "clarinet" },
        { "panflute|panpipe|shaku|ocarina|recorder|pipe", "pan flute" }, { "flute|piccolo", "flute" }, { "whistle", "whistle" },
        { "harmonica", "harmonica" }, { "bagpipe", "bagpipe" },
        { "slap", "slap bass" }, { "fretless", "fretless" }, { "synthbass|synbass|moog|bassynth|synthbs", "synth bass" }, { "pickbass|pickedbass", "pick bass" },
        { "bass", "bass" },
        { "honky|ragtime|saloon", "honky tonk" }, { "rhodes|elecpiano|electricpiano|electpiano|wurl|tinepiano|fmpiano|dxpiano|ep", "electric piano" }, { "clav", "clav" },
        { "churchorgan|pipeorgan", "church organ" }, { "accordion|harmonium|reedorgan", "harmonium" }, { "organ", "organ" }, { "harpsi", "harpsichord" },
        { "celest", "celesta" }, { "musicbox", "music box" }, { "glock", "glockenspiel" }, { "vibe|vibra", "vibes" }, { "marimba", "marimba" },
        { "xylo", "xylophone" }, { "steeldrum|steelpan", "steel drum" }, { "tubular|chime", "tubular bells" }, { "bell", "bells" },
        { "piano|grand|pno", "piano" },
        { "distort|odrive|overdrive|fuzz|leadgtr|leadguitar", "dist guitar" }, { "muted|mute gtr|palm", "muted guitar" },
        { "steelg|steelstr|acousticg|acgtr|folk", "steel guitar" }, { "elecgtr|electricg|egtr|egit|cleangtr|jazzg", "electric guitar" },
        { "guitar|gtr|geetar|nylon", "guitar" }, { "harp", "harp" }, { "koto", "koto" }, { "sitar", "sitar" }, { "banjo", "banjo" },
        { "mandolin|zither|bouzouki|shamisen", "plucked" }, { "kalimba|mbira", "kalimba" },
        { "timp", "timpani" }, { "taiko", "taiko" }, { "syndrum|synthdrum|electrotom", "synth drum" }, { "tom", "toms" },
        { "string|str|ensem|orch", "strings" }, { "sweep", "sweep pad" }, { "pad|atmos|space|warm", "synth pad" },
        { "lead|solo|synth|saw|square|poly", "synth lead" }, { "sfx|fx|effect|noise", "effects" },
        { "gun|shot|explo|bomb|crash", "gunshot" }, { "thunder|storm|rain", "thunder" }, { "sea|wave|wind|surf", "seashore" }, { "bird", "birds" },
        { "phone", "telephone" }, { "helicop", "helicopter" }, { "applause|crowd|cheer", "applause" },
    };
    const std::string nm = compact(s.trackName[t]);
    // track names that say nothing: the same name on most tracks (often the song's title), or none
    int same = 0, usedN = 0; for (int u = 0; u < SONG_TRK; ++u) if (s.used[u]) { ++usedN; same += s.trackName[u] == s.trackName[t]; }
    const bool namesMean = !nm.empty() && !(usedN > 2 && same * 2 > usedN);
    if (namesMean)
        for (const auto& k : byName) {
            std::string keys = k.keys; size_t a = 0;
            while (a <= keys.size()) { size_t b = keys.find('|', a); if (b == std::string::npos) b = keys.size(); std::string key = keys.substr(a, b - a); a = b + 1;
                key = compact(key);
                if (!key.empty() && key.size() > 3 ? nm.find(key) != std::string::npos : (!key.empty() && nm.rfind(key, 0) == 0)) return roleIndex(k.role); }
        }
    const int p = s.program[t];
    if (p < 0) return -1;
    return roleIndex(gmRole(p));
}

// is a track a drum part, whatever its channel? (named so, and on the GM drum keys)
inline bool drumPart(const MidiSong& s, int t) {
    if (s.program[t] < 0) return true;
    const std::string nm = compact(s.trackName[t]);
    static const char* keys[] = { "drum", "kit", "perc", "kick", "snare", "hihat", "hat", "cymbal", "beat", "rhythm", "808", "909", "linn", "dmx" };
    bool named = false; for (const char* k : keys) named |= nm.find(k) != std::string::npos;
    if (!named || nm.find("tom") != std::string::npos && nm.find("drum") == std::string::npos) return false;
    if (nm.find("bass") != std::string::npos && nm.find("drum") == std::string::npos) return false;   // "bass" tracks aren't kits
    const TrackShape sh = shapeOf(s, t);
    return sh.drumKeys && sh.meanLen < 0.6;
}

// records whose CMI voices are well known (the orchestra stab, the ARR1 voice...): that voice for the one track it suits
struct Famous { const char* key; const char* voice; const char* roles; };
inline const std::vector<Famous>& famousSongs() {
    static const std::vector<Famous> f = {
        { "planetrock", "ORCH5", "orch hit|brass|synth brass|strings|effects" }, { "thedreaming", "ORCH5", "orch hit|brass|strings" },
        { "closetotheedit", "ORCH5", "orch hit|brass|strings|effects" }, { "ownerofalonelyheart", "ORCH5", "orch hit|brass|synth brass" },
        { "tearsforfearsshout", "ARR1", "choir|voice pad|synth pad|synth lead" }, { "crockettstheme", "ARR1", "choir|voice pad|synth lead|flute|clarinet" },
        { "miamivicetheme", "ARR1", "choir|voice pad|synth pad" },
    };
    return f;
}

struct TrackMatch { int catIndex = -1; bool kit = false; std::string why; };
// what the other tracks of the song already got (for variety): catalogue voice -> the role it was picked for
struct MatchContext { std::map<int, int> used; bool famousUsed = false; };

// the library voice for one track (remembered: "NAME@DISK" from an earlier choice, or empty)
inline TrackMatch matchTrack(const MidiSong& s, int t, const Catalog& cat, const std::string& remembered = "", MatchContext* ctx = nullptr) {
    TrackMatch m;
    if (!remembered.empty()) {
        const size_t at = remembered.find('@');
        const int i = cat.find(remembered.substr(0, at), at == std::string::npos ? "" : remembered.substr(at + 1));
        if (i >= 0) { m.catIndex = i; m.why = "your choice for this song"; return m; }
        if (remembered == "KIT") { m.kit = true; m.why = "your choice: drum kit"; return m; }
    }
    if (s.program[t] < 0) { m.kit = true; m.why = "drum channel: CMI drum disks"; return m; }
    if (drumPart(s, t)) { m.kit = true; m.why = "drum part (\"" + s.trackName[t] + "\"): CMI drum disks"; return m; }
    // a voice named in the track's name (a real voice name: not an everyday instrument word, not the song's title)
    int same = 0, usedN = 0; for (int u = 0; u < SONG_TRK; ++u) if (s.used[u]) { ++usedN; same += s.trackName[u] == s.trackName[t]; }
    static const char* generic[] = { "BASS", "ORGAN", "PIANO", "STRINGS", "STRING", "CHOIR", "BRASS", "DRUMS", "DRUM", "LEAD", "VOICE", "SYNTH", "GUITAR", "PAD",
                                     "BELLS", "FLUTE", "SAX", "HORN", "HORNS", "PERC", "SOLO", "MELODY", "CHORDS", "KEYS", "TRACK", "ELECTRIC", "CLASSIC", "ACOUSTIC",
                                     "CELLO", "VIOLIN", "CLAP", "SNARE", "KICK", "ORCH", "WIND", "WAVE", "SWEEP", "RANDOM", "META" };
    std::vector<std::string> words;
    if (!(usedN > 2 && same * 2 > usedN)) {
        std::string w; std::string titleUp = s.name; for (auto& c : titleUp) c = (char) std::toupper((unsigned char) c);
        for (char c : s.trackName[t] + " ") { if (std::isalnum((unsigned char) c)) w += (char) std::toupper((unsigned char) c); else { if (w.size() >= 3) words.push_back(w); w.clear(); } }
        for (const auto& wd : words) {
            bool gen = false; for (const char* g : generic) gen |= wd == g;
            if (gen || titleUp.find(wd) != std::string::npos) continue;
            const int i = cat.find(wd); if (i >= 0) { m.catIndex = i; m.why = "named in the track"; return m; }
        }
    }
    const int role = roleOf(s, t);
    if (role < 0) { m.kit = true; m.why = "drums"; return m; }
    const Role& r = roles()[(size_t) role];
    const TrackShape sh = shapeOf(s, t);
    // famous records: that voice, for the one track that suits it best (stabs, or the role it's known for)
    const std::string key = songKey(s.name);
    for (const auto& f : famousSongs())
        if (key.find(f.key) != std::string::npos && !(ctx && ctx->famousUsed))
            for (const auto& rn : splitBar(f.roles))
                if (rn == r.name && (rn == std::string("orch hit") || rn == std::string("choir") || std::string(f.voice) != "ORCH5" || sh.meanLen < 0.6)) {
                    const int i = cat.find(f.voice);
                    if (i >= 0) { if (ctx) ctx->famousUsed = true; m.catIndex = i; m.why = std::string("the ") + f.voice + " this record is known for"; return m; }
                }
    // the candidates: the role's well-known voices, then every voice on its disks; scored
    const bool longNotes = sh.meanLen >= 1.0 || r.sustained, shortNotes = sh.meanLen > 0 && sh.meanLen < 0.35;
    const auto known = splitBar(r.voices);
    std::map<int, double> score;
    for (size_t k = 0; k < known.size(); ++k) { const int i = cat.find(known[k]); if (i >= 0) score[i] = std::max(score[i], 12.0 - 0.8 * (double) k); }
    for (const auto& d : splitBar(r.disks))
        for (size_t i = 0; i < cat.voices.size(); ++i)
            if (cat.voices[i].disk.rfind(d, 0) == 0 && !score.count((int) i)) score[(int) i] = 3.0;
    if (score.empty()) { m.why = std::string("no ") + r.name + " in your library"; return m; }
    int best = -1; double bs = -1e9;
    for (auto& kv : score) {
        const CatVoice& v = cat.voices[(size_t) kv.first];
        double sc = kv.second;
        if (longNotes) sc += v.looped ? 2.5 : -2.0; else if (shortNotes) sc += v.looped ? -0.5 : 1.0;
        for (const auto& wd : words) if (wd.size() >= 4 && v.name.find(wd.substr(0, 4)) != std::string::npos) sc += 4;   // "VIOLIN 1" -> VIOLSUS
        if (ctx) { auto u = ctx->used.find(kv.first); if (u != ctx->used.end()) sc -= u->second == role ? 3.0 : 7.0; }   // variety
        if (sc > bs) { bs = sc; best = kv.first; }
    }
    m.catIndex = best;
    m.why = std::string(r.name) + (score[best] >= 6 ? " from the CMI library" : " from the " + cat.voices[(size_t) best].disk + " disk");
    if (ctx) ctx->used[best] = role;
    return m;
}

// Connects a song to the library: loads each track's voice into the user voices (once, reusing one already there)
// and sets the drum kit. remembered: track -> "NAME@DISK" (or "KIT"). Returns how many tracks got a library voice.
// a library voice into the user voices (reusing it if it is already there): its sound index, or -1
inline int loadCatalogVoice(Kit& kit, const Catalog& cat, int ci) {
    if (ci < 0 || ci >= (int) cat.voices.size()) return -1;
    const CatVoice& cv = cat.voices[(size_t) ci];
    for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->name == cv.name && kit.user(k)->category == cv.disk.substr(0, 10)) return kit.userSlot(k);
    const int slot = kit.firstFree(); if (slot < 0) return -1;
    Sound snd; if (!cat.sound(ci, snd)) return -1;
    kit.setUser(slot, std::move(snd)); return kit.userSlot(slot);
}

// the first of "A|B|C" the library has, loaded: its sound index, or -1
inline int loadCatalogNamed(Kit& kit, const Catalog& cat, const std::string& names) {
    for (const auto& v : splitBar(names)) { const int ci = cat.find(v); if (ci >= 0) return loadCatalogVoice(kit, cat, ci); }
    return -1;
}

inline bool isBlank(const Kit& kit, int sound) {
    const int k = sound - kit.userSlot(0);
    return k < 0 || k >= Library::USER_SLOTS || !kit.userLoaded(k);
}

// Page R's tracks that have no voice yet get their CMI voices: one loaded, else from the library. Returns how many.
inline int connectPageR(SeqData& d, Kit& kit, const Catalog& cat) {
    int n = 0;
    for (int t = 0; t < TRACKS; ++t) {
        if (!isBlank(kit, d.trk[t].sound)) continue;
        int v = -1;
        for (const auto& w : splitBar(Library::trackVoices(t))) if ((v = findVoice(kit.bank, w)) >= 0) break;   // a voice already loaded
        if (v < 0) v = loadCatalogNamed(kit, cat, Library::trackVoices(t));
        if (v >= 0) { d.trk[t].sound = v; ++n; }
    }
    return n;
}

// the keyboard's voice when nothing is chosen: the orchestra hit, if the library has one
inline const char* defaultKeyboardVoices() { return "ORCH5|ORCH2|ORCHHIT|ORCHFZ1"; }

inline int connectSong(SongEntry& e, Kit& kit, const Catalog& cat, const std::map<int, std::string>& remembered = {}) {
    auto loadVoice = [&](int ci) -> int { return loadCatalogVoice(kit, cat, ci); };
    int n = 0; bool needKit = false;
    MatchContext ctx;
    for (int t = 0; t < SONG_TRK; ++t) {
        if (!e.song->used[t]) continue;
        auto it = remembered.find(t);
        const TrackMatch m = matchTrack(*e.song, t, cat, it == remembered.end() ? "" : it->second, &ctx);
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

// the kit lookup: the CMI drum voices loaded
inline std::function<int(int)> cmiKitLookup(const Bank& bank) { return kitLookup(bank); }

// a kit's chosen voices that aren't loaded yet: loaded from the library (by name and disk); the kit then names them
// as loaded ("NAME@CATEGORY")
inline void loadKitVoices(DrumKit& k, Kit& kit, const Catalog& cat) {
    for (auto& p : k.p) {
        if (p.voice.empty() || findVoiceAt(kit.bank, p.voice) >= 0) continue;
        const size_t at = p.voice.find('@');
        int ci = cat.find(p.voice.substr(0, at), at == std::string::npos ? "" : p.voice.substr(at + 1));
        if (ci < 0) ci = cat.find(p.voice.substr(0, at));
        const int v = loadCatalogVoice(kit, cat, ci);
        if (v >= 0) if (const Sound* s = kit.bank.at(v)) p.voice = s->name + "@" + s->category;
    }
}

}  // namespace cmi
