// The drum pads: a General MIDI drum kit built on 48 pads (three banks of 16, each pad a GM drum note, so a MIDI
// file's kick, snare and hats land on the right sounds), its voices from your CMI library or from a folder of your
// own samples turned into CMI voices (8-bit, at the rates a Series II sampled at, through the channel card).
// Also: a random kit from the library, and 100 kit presets built from the IIx factory drum, cymbal and percussion
// disks (a voice your library doesn't have is replaced by another of the same kind that it does).
//
// The pads' kit plays every song's drum tracks, channel 10 MIDI and the pads themselves; a song's own kit changes
// (Page K) go on top of it for that song.
#pragma once
#include "Catalog.h"
#include "DrumKit.h"
#include "Factory.h"
#include <random>

namespace cmi {

inline std::vector<std::string> splitSemi(const std::string& s) { std::vector<std::string> o; size_t a = 0; while (a < s.size()) { size_t b = s.find(';', a); if (b == std::string::npos) b = s.size(); if (b > a) o.push_back(s.substr(a, b - a)); a = b + 1; } return o; }

enum class Part { None, Kick, Snare, Rim, Clap, ClosedHat, PedalHat, OpenHat, Tom, Crash, Ride, RideBell, China, Splash,
                  Tamb, Cowbell, Conga, Bongo, Timbale, Agogo, Cabasa, Shaker, Whistle, Guiro, Claves, Wood, Cuica, Triangle, Vibraslap, Count };
constexpr int PARTS = (int) Part::Count;
inline const char* partName(Part p) {
    static const char* n[PARTS] = { "--", "KICK", "SNARE", "RIM", "CLAP", "CL HAT", "PED HAT", "OPEN HAT", "TOM", "CRASH", "RIDE", "RIDE BELL", "CHINA", "SPLASH",
                                    "TAMB", "COWBELL", "CONGA", "BONGO", "TIMBALE", "AGOGO", "CABASA", "SHAKER", "WHISTLE", "GUIRO", "CLAVES", "WOOD", "CUICA", "TRIANGLE", "VIBRASLAP" };
    return n[(int) p];
}

// the GM drum notes each part plays, the main one first
inline std::vector<int> partNotes(Part p) {
    switch (p) {
        case Part::Kick: return { 36, 35 }; case Part::Snare: return { 38, 40 }; case Part::Rim: return { 37 }; case Part::Clap: return { 39 };
        case Part::ClosedHat: return { 42 }; case Part::PedalHat: return { 44 }; case Part::OpenHat: return { 46 };
        case Part::Tom: return { 41, 43, 45, 47, 48, 50 }; case Part::Crash: return { 49, 57 }; case Part::Ride: return { 51, 59 };
        case Part::RideBell: return { 53 }; case Part::China: return { 52 }; case Part::Splash: return { 55 }; case Part::Tamb: return { 54 };
        case Part::Cowbell: return { 56 }; case Part::Vibraslap: return { 58 }; case Part::Bongo: return { 60, 61 }; case Part::Conga: return { 63, 64, 62 };
        case Part::Timbale: return { 65, 66 }; case Part::Agogo: return { 67, 68 }; case Part::Cabasa: return { 69 }; case Part::Shaker: return { 70, 82 };
        case Part::Whistle: return { 71, 72 }; case Part::Guiro: return { 73, 74 }; case Part::Claves: return { 75 }; case Part::Wood: return { 76, 77 };
        case Part::Cuica: return { 78, 79 }; case Part::Triangle: return { 80, 81 }; default: return {};
    }
}
inline Part partOfNote(int n) { for (int p = 1; p < PARTS; ++p) for (int x : partNotes((Part) p)) if (x == n) return (Part) p; return Part::None; }

// the pads: bank 0..2, pad 0..15 (pad 0 bottom left, as on an MPC) -> a GM drum note
constexpr int PAD_BANKS = 20;   // A-C the GM kit, D-H channel 10's other notes, I-T channels 11 and 12
inline int padNote(int bank, int pad) {
    static const int n[3][16] = {
        { 36, 38, 42, 46, 37, 39, 44, 49, 41, 45, 48, 50, 51, 53, 54, 56 },   // the kit
        { 35, 40, 43, 47, 52, 55, 57, 59, 58, 60, 61, 62, 63, 64, 65, 66 },   // more kit, bongos, congas, timbales
        { 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82 } }; // percussion
    if (bank < 0 || bank >= PAD_BANKS || pad < 0 || pad >= 16) return -1;
    if (bank < 3) return n[bank][pad];
    if (bank < 8) { const int k = (bank - 3) * 16 + pad; return k < 35 ? k : k + 48; }   // channel 10: 0-34, then 83-127
    return 128 + (bank - 8) * 16 + pad;                                                     // channels 11 and 12
}
inline char padBankLetter(int bank) { return (char) ('A' + bank); }
// a pad's name: its GM drum, else where it plays ("CH10 12", "CH11 64")
inline std::string padName(int k) {
    if (k >= 35 && k <= 82) return gmDrumName(k);
    return "CH" + std::to_string(10 + k / 128) + " " + std::to_string(k % 128);
}

// ---- what a sound is, from its name (a CMI voice "HHOPEN1", a file "Kick 808 (hard).wav", "OH_02", "Tom Lo") ----
struct PartGuess { Part part = Part::None; bool strong = false; int pitch = 0; };   // pitch: -1 low, 0, +1 high (toms, congas)
inline PartGuess partOfName(const std::string& name) {
    std::string c; std::vector<std::string> tok; std::string cur;
    auto flush = [&] { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } };
    char prev = 0;
    for (char ch : name) {
        const unsigned char u = (unsigned char) ch;
        if (!std::isalnum(u)) { flush(); prev = 0; continue; }
        const char l = (char) std::tolower(u);
        if (prev && (std::isdigit((unsigned char) prev) != 0) != (std::isdigit(u) != 0)) flush();   // "bd01" -> bd, 01
        cur += l; c += l; prev = l;
    }
    flush();
    auto has = [&](const char* k) { return c.find(k) != std::string::npos; };
    auto tokIs = [&](std::initializer_list<const char*> ks) { for (const auto& t : tok) for (const char* k : ks) if (t == k) return true; return false; };
    auto tokEnds = [&](const char* k) { const size_t l = std::strlen(k); for (const auto& t : tok) if (t.size() >= l && t.compare(t.size() - l, l, k) == 0) return true; return false; };
    auto tokStarts = [&](const char* k) { for (const auto& t : tok) if (t.rfind(k, 0) == 0) return true; return false; };
    PartGuess g; g.strong = true;
    if (has("low") || has("floor") || tokIs({ "lo", "l", "lt", "lc", "fl" }) || has("lotom") || has("tomlo")) g.pitch = -1;
    else if (has("high") || tokIs({ "hi", "h", "ht", "hc" }) || has("hitom") || has("tomhi")) g.pitch = 1;
    const bool hat = has("hihat") || has("hat") || tokStarts("hh") || has("hhclos") || has("hhop") || tokIs({ "ch", "oh", "chh", "ohh", "phh" });
    auto is = [&](Part p) { g.part = p; return g; };
    if (has("cowbel")) return is(Part::Cowbell);
    if (has("agogo") || tokStarts("gogo")) return is(Part::Agogo);
    if (has("ridebell") || has("cymbell") || has("bellride") || (has("ride") && has("bell"))) return is(Part::RideBell);
    if (has("china") || tokStarts("pang")) return is(Part::China);
    if (has("splash") && !has("cymspl")) return is(Part::Splash);
    if (has("ride") || has("cymstick") || tokIs({ "rd" })) return is(Part::Ride);
    if (has("crash") || has("cymb") || has("cymspl") || has("cymbal") || tokStarts("cym") || tokStarts("gong") || tokIs({ "cy", "cr" })) return is(Part::Crash);
    if (hat) {
        if (has("open") || has("hhop") || has("ohh") || tokIs({ "oh", "o" }) || c == "hihat3") return is(Part::OpenHat);
        if (has("pedal") || has("foot") || has("hhped") || tokIs({ "phh", "ph", "ped" })) return is(Part::PedalHat);
        return is(Part::ClosedHat);
    }
    if (has("clap") || has("snap") || has("finger") || (tokIs({ "cp", "hc" }) && !has("conga"))) return is(Part::Clap);
    if (has("rimshot") || has("crosstix") || has("crossstick") || has("sidestick") || has("xstick") || tokStarts("rim") || tokStarts("rrim") || tokIs({ "rs" })) return is(Part::Rim);
    if (has("snare") || tokStarts("snar") || tokStarts("snr") || has("sdar") || has("snrel") || has("kocsnr") || tokIs({ "sd", "sn", "snr" })) return is(Part::Snare);
    if (has("kick") || has("kik") || has("bdrum") || has("bassdrum") || has("bassdr") || tokIs({ "bd", "kd", "bdr", "kck", "bassd" })) return is(Part::Kick);
    bool tomEnd = false; for (const char* k : { "floortom", "rototom", "ttom", "hitom", "lotom", "midtom", "lowtom", "hightom", "fltom", "racktom", "eltom", "syntom" }) tomEnd |= tokEnds(k);
    if (tokStarts("tom") || tomEnd || has("syndrum") || has("rototom") || has("taiko") || has("bikodrum") || tokIs({ "lt", "mt", "ht" })) return is(Part::Tom);
    if (has("tamb")) return is(Part::Tamb);
    if (has("conga") || tokStarts("cong") || has("cconghi") || has("cconglo") || tokIs({ "lc", "mc", "hc" })) return is(Part::Conga);
    if (tokStarts("bong")) return is(Part::Bongo);
    if (has("timbal")) return is(Part::Timbale);
    if (has("cabasa")) return is(Part::Cabasa);
    if (has("shak") || has("marac") || has("shkr") || tokIs({ "ma", "sh" })) return is(Part::Shaker);
    if (has("whist")) return is(Part::Whistle);
    if (has("guiro") || tokStarts("fish")) return is(Part::Guiro);
    if (has("claves") || tokIs({ "clave", "cl" })) return is(Part::Claves);
    if (has("cuica")) return is(Part::Cuica);
    if (has("triang") || tokIs({ "tri" })) return is(Part::Triangle);
    if (has("vibsl") || has("vbrsl") || has("vibraslap")) return is(Part::Vibraslap);
    if (has("woodbl") || tokStarts("wood") || has("block") || has("castanet")) return is(Part::Wood);
    g.strong = false;
    if (tokStarts("stick") || has("percstk")) return is(Part::Rim);
    if (has("drumbo")) return is(Part::Kick);
    return is(Part::None);
}
// a disk of drums, cymbals or percussion (the IIx's DRUMS, DRUMS1, CYMBALS, PERCUSN1/2, BIGMELU; or a folder so named)
inline bool drumDisk(const std::string& disk) {
    std::string d; for (char ch : disk) d += (char) std::toupper((unsigned char) ch);
    for (const char* k : { "DRUM", "CYMB", "PERC", "BIGME", "BIGML", "KIT", "BEAT" }) if (d.find(k) != std::string::npos) return true;
    return false;
}

// a voice whose sound starts late in its 16 KB (BDRUM and PERCSTK on the IIx disks start about 0.4 s in): no use on a pad
inline bool lateStart(const CatVoice& v) {
    constexpr size_t at = 0x1580;
    for (size_t i = at; i < v.vc.size(); ++i) if (std::abs((int) v.vc[i] - 128) > 8) return i - at > 3000;   // 0.1 s at 30.2 kHz
    return true;
}

// ---- the library's drums, by part ----
struct LibraryDrums {
    std::array<std::vector<int>, PARTS> by;   // catalogue indices, each name once
    explicit LibraryDrums(const Catalog& cat) {
        std::vector<std::string> seen;
        for (size_t i = 0; i < cat.voices.size(); ++i) {
            const auto& v = cat.voices[i];
            const PartGuess g = partOfName(v.name);
            if (g.part == Part::None || !(g.strong || drumDisk(v.disk))) continue;
            if (!drumDisk(v.disk) && v.looped && g.part != Part::Crash && g.part != Part::Ride) continue;   // a sustained voice named like a drum
            if (std::find(seen.begin(), seen.end(), v.name) != seen.end() ) continue;
            seen.push_back(v.name); by[(size_t) g.part].push_back((int) i);
        }
    }
    int count() const { int n = 0; for (const auto& v : by) n += (int) v.size(); return n; }
};

// ---- a kit from the sounds chosen for each part: every GM note of the part gets one, the toms spread out and tuned
// in steps, a part with one sound plays it on its second note too (a tone apart) ----
struct PartChoice { std::vector<std::string> voices; float tune = 0, level = 1; };   // "NAME@DISK"; toms low to high
inline DrumKit kitFromParts(const std::array<PartChoice, PARTS>& ch, bool fillIn = true) {
    DrumKit k;
    auto set = [&](int note, const std::string& v, float tune, float level) {
        if (note < 0 || note > 127 || v.empty()) return;
        KitPiece& p = k.p[(size_t) note]; p.voice = v; p.tune = std::clamp(tune, -24.f, 24.f); p.level = std::clamp(level, 0.f, 2.f);
    };
    for (int pi = 1; pi < PARTS; ++pi) {
        const Part part = (Part) pi; const auto& c = ch[(size_t) pi];
        if (c.voices.empty()) continue;
        const auto notes = partNotes(part);
        if (part == Part::Tom) {
            // six GM toms (low floor .. high): over the sounds given, each group tuned in steps of two semitones
            const int n = (int) c.voices.size();
            for (int j = 0; j < 6; ++j) {
                const int s = std::min(n - 1, j * n / 6);
                int first = 0; while (first < 6 && std::min(n - 1, first * n / 6) != s) ++first;
                int last = first; while (last + 1 < 6 && std::min(n - 1, (last + 1) * n / 6) == s) ++last;
                const float centre = (first + last) / 2.f;
                set(notes[(size_t) j], c.voices[(size_t) s], c.tune + (j - centre) * (n == 1 ? 2.f : 2.5f), c.level);
            }
            continue;
        }
        for (size_t j = 0; j < notes.size(); ++j) {
            if (j < c.voices.size()) set(notes[j], c.voices[j], c.tune, c.level);
            else {   // the part's other notes from its first sound
                static const std::map<int, std::pair<float, float>> alt = {   // note -> tune, level
                    { 35, { -2, 1 } }, { 40, { 1, 0.95f } }, { 57, { 2, 0.95f } }, { 59, { -1, 0.9f } }, { 61, { -4, 1 } }, { 64, { -5, 1 } }, { 62, { 2, 0.8f } },
                    { 66, { -4, 1 } }, { 68, { -5, 1 } }, { 82, { 2, 0.9f } }, { 72, { -2, 1 } }, { 74, { -2, 1 } }, { 77, { -3, 1 } }, { 79, { 4, 1 } }, { 81, { 0, 1.05f } } };
                auto it = alt.find(notes[j]);
                const auto tl = it == alt.end() ? std::make_pair(0.f, 1.f) : it->second;
                if (fillIn) set(notes[j], c.voices[0], c.tune + tl.first, c.level * tl.second);
            }
        }
    }
    if (fillIn) {   // the closed hat on the pedal; the crash as splash and china, when the kit has none
        auto from = [&](int note, int src, float tune, float level) { if (k.p[(size_t) note].voice.empty() && !k.p[(size_t) src].voice.empty()) set(note, k.p[(size_t) src].voice, k.p[(size_t) src].tune + tune, k.p[(size_t) src].level * level); };
        from(44, 42, -1, 0.85f); from(55, 49, 5, 0.85f); from(52, 49, -3, 1); from(53, 51, 3, 1);
    }
    return k;
}

// a catalogue voice as a kit piece's voice
inline std::string catVoiceName(const Catalog& cat, int ci) { return ci < 0 || ci >= (int) cat.voices.size() ? std::string() : cat.voices[(size_t) ci].name + "@" + cat.voices[(size_t) ci].disk; }

// ---- a random kit from the library: a sound for every part it has, hats from one disk, the toms one or two sounds
// spread, a little tuning here and there ----
inline DrumKit randomKit(const Catalog& cat, uint32_t seed) {
    LibraryDrums ld(cat);
    std::mt19937 r(seed);
    auto pick = [&](Part p) -> int { const auto& v = ld.by[(size_t) p]; return v.empty() ? -1 : v[(size_t) (r() % v.size())]; };
    auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(r) < p; };
    auto tuneBy = [&](int maxSemis) { return (float) ((int) (r() % (unsigned) (2 * maxSemis + 1)) - maxSemis); };
    std::array<PartChoice, PARTS> ch;
    auto take = [&](Part p, int ci, float tune = 0) { if (ci >= 0) { ch[(size_t) p].voices.push_back(catVoiceName(cat, ci)); ch[(size_t) p].tune = tune; } };
    take(Part::Kick, pick(Part::Kick), chance(0.35) ? tuneBy(3) : 0);
    take(Part::Snare, pick(Part::Snare), chance(0.4) ? tuneBy(3) : 0);
    if (chance(0.5)) take(Part::Snare, pick(Part::Snare));
    for (Part p : { Part::Rim, Part::Clap, Part::Crash, Part::Ride, Part::RideBell, Part::China, Part::Splash, Part::Tamb, Part::Cowbell, Part::Conga, Part::Bongo,
                    Part::Timbale, Part::Agogo, Part::Cabasa, Part::Shaker, Part::Whistle, Part::Guiro, Part::Claves, Part::Wood, Part::Cuica, Part::Triangle, Part::Vibraslap })
        take(p, pick(p));
    // hats: closed first, the open and pedal from the same disk when it has them
    const int chh = pick(Part::ClosedHat); take(Part::ClosedHat, chh);
    const std::string disk = chh >= 0 ? cat.voices[(size_t) chh].disk : std::string();
    for (Part p : { Part::OpenHat, Part::PedalHat }) {
        std::vector<int> same; for (int ci : ld.by[(size_t) p]) if (cat.voices[(size_t) ci].disk == disk) same.push_back(ci);
        take(p, !same.empty() ? same[(size_t) (r() % same.size())] : pick(p));
    }
    // toms: one or two sounds
    const int t1 = pick(Part::Tom);
    if (t1 >= 0) {
        ch[(size_t) Part::Tom].voices.push_back(catVoiceName(cat, t1));
        if (chance(0.4)) { const int t2 = pick(Part::Tom); if (t2 >= 0 && t2 != t1) ch[(size_t) Part::Tom].voices.push_back(catVoiceName(cat, t2)); }
        ch[(size_t) Part::Tom].tune = chance(0.4) ? tuneBy(4) : 0;
    }
    DrumKit k = kitFromParts(ch);
    // every pad of banks A-C sounds: a part the library has no voice for gets a drum of a near kind, retuned
    std::vector<int> any; for (int pi = 1; pi < PARTS; ++pi) for (int ci : ld.by[(size_t) pi]) any.push_back(ci);
    for (int b = 0; b < 3 && !any.empty(); ++b) for (int i = 0; i < 16; ++i) {
        const int n = padNote(b, i); KitPiece& p = k.p[(size_t) n];
        if (!p.voice.empty()) continue;
        static const Part near[] = { Part::Wood, Part::Cowbell, Part::Tamb, Part::Cabasa, Part::Rim, Part::Conga, Part::Bongo, Part::ClosedHat };
        int ci = -1; for (Part q : near) if (ci < 0 && !ld.by[(size_t) q].empty()) ci = ld.by[(size_t) q][(size_t) (r() % ld.by[(size_t) q].size())];
        if (ci < 0) ci = any[(size_t) (r() % any.size())];
        p.voice = catVoiceName(cat, ci); p.tune = (float) ((int) (r() % 13) - 6); p.level = 0.85f;
    }
    // round robin: the kick, snare and hats get another voice or two of their kind to cycle through
    for (int n : { 36, 38, 40, 42, 46 }) {
        const Part part = partOfNote(n); const auto& c = ld.by[(size_t) part]; KitPiece& p = k.p[(size_t) n];
        if (c.size() < 2 || p.voice.empty()) continue;
        for (int t = 0, got = 0; t < 6 && got < 2; ++t) { const std::string v = catVoiceName(cat, c[(size_t) (r() % c.size())]); if (v != p.voice && p.rr.find(v) == std::string::npos) { p.rr += (p.rr.empty() ? "" : ";") + v; ++got; } }
    }
    return k;
}

// ---- 100 kit presets from the IIx factory disks ----
struct KitPreset { std::string name; DrumKit kit; };
inline const std::vector<KitPreset>& kitPresets() {
    static const std::vector<KitPreset> presets = [] {
        // the families: each with the IIx voices it draws from (by part) and its tuning
        struct Family {
            const char* names[10];
            const char *kick, *snare, *rim, *clap, *chh, *ohh, *tom, *crash, *ride, *perc;
            int kickTune, snareTune, tomTune, toms;   // a tuning centre (semitones), toms: how many tom sounds
        };
        static const Family fam[] = {
            { { "PAGE R CLASSIC", "PAGE R TIGHT", "PAGE R HEAVY", "PAGE R BRIGHT", "PAGE R LOOSE", "PAGE R DEEP", "PAGE R SNAP", "PAGE R RIM", "PAGE R 84", "PAGE R 85" },
              "KICK1|BDRUM1", "SNARE1|LMSNARE|SNARE2", "CROSSTIX", "CLAP|CLAPS", "HIHAT2|HIHAT1", "HHOPEN1", "TTOM|TOMNEW", "CYMSPLS1", "CYMSTICK", "TAMBHIT|COWBELL1", 0, 0, 0, 1 },
            { { "ARENA ROCK", "GARAGE ROCK", "HARD ROCK", "SOFT ROCK", "ROCK BALLAD", "STADIUM", "LIVE ROOM", "DRY ROCK", "PUB ROCK", "ROCK FILL" },
              "BDRUM1|KICK2|KICK4|KICK1", "MEDSNARE|SNARE|SNARER|SNARE3|SNARE4", "RRIM|CROSSTIX", "CLAPS", "HHCLOSD|HIHAT1|HHCLOSD1", "HHOPNCL1|HHOPEN1|HHOPNCL2", "FLOORTOM|TTOM|TOMNEW", "CYMSPLS1|CYMSPLS2|CYMB0", "CYMSTICK", "TAMBSHK|COWBELL2", -1, -1, -2, 2 },
            { { "POWER KIT", "GATED BIG", "THUNDER", "MONSTER", "WALL OF DRUMS", "HUGE ROOM", "POWER BALLAD", "DROP D", "STOMP", "TITAN" },
              "POWERKIK|KICK5|KICK6|KICK2", "SNARADA|SNAREA10|SNAREA11|SNAREA12|SNAREA13", "RRIM", "CLAPTRAP|CLAPS", "HHCLOSD1|HIHAT2", "HHOPNCL2|HHOPEN1", "FLOORTOM|BIKODRUM|TOMNEW", "GONG|CYMB0|CYMSPLS2", "CYMSTICK", "TAMBOUR1", -3, -3, -4, 2 },
            { { "SYNDRUM", "ELECTRO 1", "ELECTRO 2", "NEW ROMANTIC", "SYNTH POP", "ROBOT", "LASER TOMS", "COLD WAVE", "MACHINE", "NEON" },
              "KICK3|KICK7|KICK6|POWERKIK", "SNARE|SNARE5|SNARE6|SNARE9", "STICK|RRIM", "CLAPTRAP|CLAP", "CLOSEDHH|HIHAT2", "HHOPNCL1|HIHAT3", "SYNDRUM1|ROTOTOM", "CYMSPLS2|PANG", "CYMBELL2|CYMSTICK", "COWBELOO|SHTBELL", 0, 2, 3, 1 },
            { { "DISCO", "DISCO STRINGS", "MIRROR BALL", "SATURDAY", "FOUR ON FLOOR", "HI-NRG", "EURO DISCO", "BOOGIE", "ROLLER", "STUDIO 54" },
              "KICK1|KICK2|BDRUM1", "SNARE7|SNARE8|SNARE2|LMSNARE", "CROSSTIX", "CLAPS|CLAP|CLAPTRAP", "HIHAT1|HHCLOSD", "HHOPEN1|HHOPNCL1", "ROTOTOM|TOMNEW", "CYMSPLS1", "CYMSTICK|CYMBELL1", "TAMBSHK|COWBELL1|CABASA", 0, 1, 2, 1 },
            { { "FUNK", "TIGHT FUNK", "SLAP FUNK", "GHOST NOTES", "JAMES", "SYNCOPATE", "PLUCKY", "BRIGHT FUNK", "STREET", "UPTOWN" },
              "KICK4|KICK2|BDRUM1", "SNARE3|SNARE4|KOCSNR|SDAR|SNAREA10", "CROSSTIX|RRIM", "CLAP|FINGER", "HIHAT1|HHCLOSD1", "HHOPNCL2|HHOPNCL1", "TTOM|TOMNEW", "CYMSPLS2", "CYMSTICK|CYMBELL1", "COWBELL2|TAMBHIT|GOGO", 1, 3, 2, 1 },
            { { "BALLAD", "BRUSHY", "LATE NIGHT", "SOFT KIT", "CANDLE", "SLOW DANCE", "JAZZ CLUB", "BOSSA", "QUIET STORM", "TORCH SONG" },
              "BDRUM1|KICK1|KICK4", "SNREL|SDAR|SNARER|MEDSNARE", "CROSSTIX|RRIM", "FINGER|CLAPS", "HHCLOSD|HIHAT1", "HHOPEN1|HHOPNCL1", "TTOM|FLOORTOM", "CYMSPLS1|CYMB0", "CYMSTICK|CYMBELL1", "CABASA|TRIANGLE|SHTBELL", -1, -2, -1, 2 },
            { { "LATIN", "SALSA", "SAMBA", "MAMBO", "CUBANO", "CARNIVAL", "RUMBA", "CHA CHA", "MERENGUE", "BATUCADA" },
              "BDRUM1|KICK2", "SNARE2|SNARE3|LMSNARE", "STICK|CROSSTIX", "CLAPS|CLAP", "HIHAT1|HHCLOSD", "HHOPEN1", "TIMBALI|TTOM", "CYMSPLS1", "CYMBELL1|CYMSTICK",
              "CCONGHI|CCONGLO|BONGHI|BONGLO|TIMBALI|GOGO|CABASA|CABASA1|CASTANET|WOOD|COWBELL1|COWBELL2|VIBSLP|FISH|FISHS|TAMBOUR2|TRIANGLE", 0, 1, 1, 1 },
            { { "ORCHESTRAL", "TYMPANI", "CONCERT", "SYMPHONIC", "FILM SCORE", "EPIC", "PROCESSION", "BALLROOM", "MARCHING", "OVERTURE" },
              "BIKODRUM|DRUMBO|KICK2", "SNARER|MEDSNARE|SNARE|SDAR", "STICK|RRIM", "CLAPS", "HHCLOSD|HIHAT1", "HHOPEN1", "TYMPANI|BIKODRUM|FLOORTOM", "CYMB0|GONG|CYMSPLS2", "CYMBELL1|CYMSTICK", "TRIANGLE|TAMBOUR1|TAMBOUR2|SHTBELL", -2, -1, 0, 2 },
            { { "BIG MELU", "MELU DRUMS", "WORLD BEAT", "TABLA KIT", "INDUSTRIAL", "JUNKYARD", "TRIBAL", "SPACE KIT", "RECORD SCRATCH", "FAIRLIGHT FX" },
              "POWERKIK|DRUMBO|KICK7", "SNARE|SNAREA13|KOCSNR|SNARADA", "STICK|CROSSTIX", "CLAPTRAP", "CLOSEDHH|HHCLOSD1", "HHOPNCL2|HIHAT3", "SYNDRUM1|TABLA|BIKODRUM|TIMBALI", "PANG|GONG|CYMB0", "CYMBELL2", "GOGO|CABASA1|SHTBELL|WOOD|VBRSLP|CASTANET", -2, 0, 0, 2 },
        };
        // a voice's disk on the IIx library (for "NAME@DISK"; the catalogue finds it by name on any disk too)
        auto disk = [](const std::string& n) { for (const auto& p : factoryPresets()) if (n == p.name) return std::string(p.disk); return std::string(); };
        auto named = [&](const std::string& n) { const std::string d = disk(n); return d.empty() ? n : n + "@" + d; };
        std::vector<KitPreset> out;
        for (size_t f = 0; f < sizeof fam / sizeof fam[0]; ++f) {
            const Family& F = fam[f];
            for (int v = 0; v < 10; ++v) {
                std::mt19937 r((uint32_t) (f * 1000 + v * 37 + 11));
                auto one = [&](const char* list, int which) { const auto o = splitBar(list); return o.empty() ? std::string() : o[(size_t) (which % (int) o.size())]; };
                auto rnd = [&](const char* list) { const auto o = splitBar(list); return o.empty() ? std::string() : o[(size_t) (r() % o.size())]; };
                std::array<PartChoice, PARTS> ch;
                auto put = [&](Part p, const std::string& n, float tune = 0, float level = 1) { if (!n.empty()) { ch[(size_t) p].voices.push_back(named(n)); ch[(size_t) p].tune = tune; ch[(size_t) p].level = level; } };
                // variation v: the first is the family's sound, the others walk through its lists and tunings
                const float kt = (float) F.kickTune + (v == 0 ? 0 : (float) ((int) (r() % 5) - 2));
                const float st = (float) F.snareTune + (v == 0 ? 0 : (float) ((int) (r() % 5) - 2));
                put(Part::Kick, one(F.kick, v), kt);
                put(Part::Snare, one(F.snare, v), st);
                if (v % 3 == 1) put(Part::Snare, rnd(F.snare), st);
                put(Part::Rim, one(F.rim, v)); put(Part::Clap, one(F.clap, v + (int) (r() % 2)), 0, 0.9f);
                put(Part::ClosedHat, one(F.chh, v), (float) ((int) (r() % 3) - 1) * (v ? 1 : 0), 0.9f);
                put(Part::OpenHat, one(F.ohh, v), 0, 0.85f);
                put(Part::Tom, one(F.tom, v), (float) F.tomTune + (v ? (float) ((int) (r() % 5) - 2) : 0));
                if (F.toms > 1 && v % 2 == 0) { const std::string t2 = one(F.tom, v + 1); if (named(t2) != ch[(size_t) Part::Tom].voices[0]) ch[(size_t) Part::Tom].voices.push_back(named(t2)); }
                put(Part::Crash, one(F.crash, v), 0, 0.85f); put(Part::Ride, one(F.ride, v), 0, 0.85f);
                // percussion: each listed voice on its own part
                for (const auto& pv : splitBar(F.perc)) { const Part p = partOfName(pv).part; if (p != Part::None && ch[(size_t) p].voices.empty()) put(p, pv); }
                out.push_back({ F.names[v], kitFromParts(ch) });
            }
        }
        return out;
    }();
    return presets;
}

// a kit's voices resolved on this library: each "NAME@DISK" the library has stays; one it lacks is replaced by
// another sound of the same part (the first the library has, so a preset always comes out the same here); none: the
// piece goes back to automatic (the CMI drum map). Returns how many were replaced.
inline int fitKitToLibrary(DrumKit& k, const Catalog& cat) {
    if (cat.voices.empty()) return 0;
    LibraryDrums ld(cat); int n = 0;
    auto one = [&](const std::string& v, int note) -> std::string {
        const size_t at = v.find('@');
        const std::string nm = v.substr(0, at), dk = at == std::string::npos ? "" : v.substr(at + 1);
        if (dk == "User") return v;
        int ci = cat.find(nm, dk); if (ci < 0) ci = cat.find(nm);
        if (ci < 0) { Part part = partOfName(nm).part; if (part == Part::None) part = partOfNote(note); const auto& c = ld.by[(size_t) part]; ci = c.empty() ? -1 : c[(size_t) (std::hash<std::string>()(nm) % c.size())]; }
        return ci < 0 ? std::string() : catVoiceName(cat, ci);
    };
    for (int note = 0; note < KIT_SLOTS; ++note) {
        KitPiece& p = k.p[(size_t) note];
        if (!p.rr.empty()) { std::string o; for (const auto& v : splitSemi(p.rr)) { const std::string r = one(v, note); if (!r.empty()) o += (o.empty() ? "" : ";") + r; } p.rr = o; }
        if (p.voice.empty()) continue;
        const size_t at = p.voice.find('@');
        const std::string nm = p.voice.substr(0, at), dk = at == std::string::npos ? "" : p.voice.substr(at + 1);
        if (dk == "User") continue;   // your own sample: it's loaded already
        int ci = cat.find(nm, dk); if (ci < 0) ci = cat.find(nm);
        if (ci < 0) {
            Part part = partOfName(nm).part; if (part == Part::None) part = partOfNote(note);
            const auto& c = ld.by[(size_t) part];
            ci = c.empty() ? -1 : c[(size_t) (std::hash<std::string>()(nm) % c.size())];
            ++n;
        }
        if (ci < 0) { p.voice.clear(); continue; }
        p.voice = catVoiceName(cat, ci);
    }
    return n;
}

// ---- your own samples as a kit: each turned into a CMI voice the way a Series II sampled a drum (8-bit, at up to its
// 30.2 kHz, a lower rate for a longer sound so it fits the card's 16 KB, filtered first), named and put on its pad ----
struct KitSample { std::string file; std::vector<float> mono; double rate = 0; };
inline Sound drumToCmi(const std::vector<float>& in, double srcRate, const std::string& name, const std::string& file) {
    // trim the silence before and after, level to full scale (the CMI's input gain set for each sample)
    size_t a = 0, b = in.size(); float pk = 0;
    for (float v : in) pk = std::max(pk, std::fabs(v));
    const float floor = std::max(1e-5f, pk * 0.003f);
    while (a < b && std::fabs(in[a]) < floor) ++a;
    while (b > a && std::fabs(in[b - 1]) < floor) --b;
    std::vector<float> x(in.begin() + (long) a, in.begin() + (long) b);
    if (x.empty()) x.assign(64, 0.f);
    const float g = pk > 1e-6f ? 0.97f / pk : 1.f; for (auto& v : x) v *= g;
    const double dur = x.size() / srcRate;
    double rate = std::min({ 30200.0, srcRate, (double) RAM_SIZE / std::max(dur, 1e-3) });
    rate = std::max(rate, 8000.0);   // under that a sound is cut short (with a fade) rather than sampled duller still
    const size_t n = (size_t) std::min<double>(RAM_SIZE, std::floor(dur * rate));
    gen::Vec y(n);
    std::vector<float> f = x;
    if (rate < srcRate - 1) { gen::BQ q1 = gen::BQ::lp(srcRate, rate * 0.45, 0.54), q2 = gen::BQ::lp(srcRate, rate * 0.45, 1.3); for (auto& v : f) v = (float) q2.run(q1.run(v)); }
    for (size_t i = 0; i < n; ++i) {
        const double p = i * srcRate / rate; const size_t k = (size_t) p; const double fr = p - k;
        const float u = k < f.size() ? f[k] : 0.f, w = k + 1 < f.size() ? f[k + 1] : u;
        y[i] = (float) (u + (w - u) * fr);
    }
    if (n < (size_t) std::floor(dur * rate)) { const size_t fl = std::min<size_t>(n, (size_t) (rate * 0.03)); for (size_t i = 0; i < fl; ++i) y[n - 1 - i] *= (float) i / (float) fl; }
    const double secs = n / rate;
    Sound s = gen::sampled(name.c_str(), ("Kit sample: " + file).c_str(), "User", rate, 60, y, -1, secs, Controls());
    s.ctl.attack = 0; s.ctl.sustain = 1;
    return s;
}

// a voice name (eight letters) for a file, not yet used among the loaded voices
inline std::string kitSampleName(const Bank& bank, const std::string& file) {
    std::string stem = file.substr(0, file.find_last_of('.'));
    const size_t sl = stem.find_last_of("/\\"); if (sl != std::string::npos) stem = stem.substr(sl + 1);
    std::string nm; for (char c : stem) if (std::isalnum((unsigned char) c)) nm += (char) std::toupper((unsigned char) c);
    if (nm.empty()) nm = "DRUM";
    nm = nm.substr(0, 8);
    auto used = [&](const std::string& v) {
        for (int k = 0; k < Library::USER_SLOTS; ++k) { const Sound* x = bank.at(Library::slotOf(k)); if (x && x != Library::get().blank[(size_t) k] && x->name == v && x->category == "User") return true; }
        return false;
    };
    if (!used(nm)) return nm;
    for (int i = 2; i < 1000; ++i) { const std::string d = std::to_string(i); const std::string c = nm.substr(0, 8 - d.size()) + d; if (!used(c)) return c; }
    return nm;
}

// a folder of samples onto the pads: each one's part from its name (kick, snare, hat... and the short names drum
// machines use: BD SD CH OH CP RS LT MT HT CY...), toms in order low to high, the rest on the free percussion pads.
// The samples become your voices (Page 8); the pads' kit gets them. Returns what happened, for the screen.
inline std::string importKitSamples(std::vector<KitSample>& files, Kit& kit, DrumKit& pads, bool replaceKit) {
    std::sort(files.begin(), files.end(), [](const KitSample& a, const KitSample& b) { return a.file < b.file; });
    std::array<PartChoice, PARTS> ch;
    std::vector<std::pair<int, std::string>> toms;   // pitch hint, voice
    std::vector<std::string> others;
    int loaded = 0, full = 0;
    for (auto& f : files) {
        if (f.mono.empty() || f.rate <= 0) continue;
        const int slot = kit.firstFree(); if (slot < 0) { ++full; continue; }
        const std::string nm = kitSampleName(kit.bank, f.file);
        kit.setUser(slot, drumToCmi(f.mono, f.rate, nm, f.file));
        ++loaded;
        const std::string v = nm + "@User";
        const PartGuess g = partOfName(f.file);
        if (g.part == Part::Tom) toms.push_back({ g.pitch, v });
        else if (g.part == Part::None) others.push_back(v);
        else ch[(size_t) g.part].voices.push_back(v);
    }
    std::stable_sort(toms.begin(), toms.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& t : toms) ch[(size_t) Part::Tom].voices.push_back(t.second);
    DrumKit k = kitFromParts(ch);
    if (!replaceKit) for (int n = 0; n < KIT_SLOTS; ++n) if (k.p[(size_t) n].voice.empty()) k.p[(size_t) n] = pads.p[(size_t) n];
    // the sounds it couldn't place: onto pads still free, bank D onwards
    int placed = 0;
    for (int b = 3; b < PAD_BANKS && placed < (int) others.size(); ++b)
        for (int i = 0; i < 16 && placed < (int) others.size(); ++i) { const int n = padNote(b, i); if (k.p[(size_t) n].voice.empty()) k.p[(size_t) n].voice = others[(size_t) placed++]; }
    pads = k;
    std::string parts;
    for (int p = 1; p < PARTS; ++p) if (!ch[(size_t) p].voices.empty()) parts += (parts.empty() ? "" : " ") + std::string(partName((Part) p));
    std::string msg = std::to_string(loaded) + (loaded == 1 ? " SAMPLE" : " SAMPLES") + " ON THE PADS";
    if (!parts.empty()) msg += ": " + parts;
    if (!others.empty()) msg += ", " + std::to_string(others.size()) + " UNNAMED ON FREE PADS";
    if (full) msg += ", " + std::to_string(full) + " LEFT OUT (VOICES FULL)";
    return msg;
}

// the pads ready to play: an empty kit (a new install, or cleared) starts as preset 1 (PAGE R CLASSIC) from your
// library; every pad left on the CMI drum map gets its voice loaded from the library (the map only finds voices
// loaded). Returns true if the kit was set to preset 1. Under the catalogue's lock.
inline bool padsReady(DrumKit& pads, Kit& kit, const Catalog& cat, bool defaultKit = true) {
    if (cat.voices.empty()) return false;
    bool set = false;
    if (defaultKit && pads.empty() && !kitPresets().empty()) { pads = kitPresets()[0].kit; fitKitToLibrary(pads, cat); set = true; }
    loadKitVoices(pads, kit, cat);
    for (int n = 27; n <= 87; ++n) {
        if (!pads.p[(size_t) n].voice.empty()) continue;
        bool have = false; for (const auto& v : splitBar(drumVoices(n))) if (findVoice(kit.bank, v) >= 0) { have = true; break; }
        if (!have) for (const auto& v : splitBar(drumVoices(n))) { const int ci = cat.find(v); if (ci >= 0 && !lateStart(cat.voices[(size_t) ci])) { loadCatalogVoice(kit, cat, ci); break; } }
    }
    return set;
}

// CLEAR ALL's engine part: the mixer, the effects, the kit's lift and feel, the click, back to how they start
inline void resetEngineSettings(Engine& eng) {
    for (int i = 0; i < Engine::STRIPS; ++i) { eng.fader[(size_t) i] = 1; eng.panOver[(size_t) i] = -1; eng.mute[(size_t) i] = false; eng.solo[(size_t) i] = false; eng.fxSend[(size_t) i] = 0.15f; }
    eng.fxOn = true; eng.fx.preset(0); eng.kickLift = 2.0f; eng.humanize = 0.f; eng.ch10Kit = true; eng.clickOn = false; eng.songLoop = false;
}

// a kit as a short text for the screen: "KICK1 SNARE1 HIHAT2 ..."
inline std::string kitSummary(const DrumKit& k) {
    std::string s;
    for (int n : { 36, 38, 42, 46, 49 }) { const auto& v = k.p[(size_t) n].voice; if (!v.empty()) s += (s.empty() ? "" : " ") + v.substr(0, v.find('@')); }
    return s;
}

}  // namespace cmi
