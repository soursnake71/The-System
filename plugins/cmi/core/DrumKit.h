// A song's drum kit: for each GM drum note, the CMI voice that plays it (or automatic: the CMI drum map), and its
// level, pan, tuning, mute and solo. Kept with the song (and remembered for the song's title), applied to the
// engine when the song is selected.
#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace cmi {

// the kit's places: channel 10's 128 notes (the GM drums among them), then channel 11's and 12's (the pads' extra banks)
constexpr int KIT_SLOTS = 384;

struct KitPiece {
    std::string voice;   // "NAME@CATEGORY" (a loaded voice), empty: automatic
    float level = 1, pan = -1, tune = 0;   // level 0..2, pan 0..1 (-1: the track's), tune in semitones
    bool mute = false, solo = false;
    float a = -1, d = -1, s = -1, r = -1;   // its envelope: attack, decay (0..31), sustain level (0..1), release (0..31); -1: the kit's
    float f = -1, lr = -1, ld = -1;         // its filter (0..255) and LFO (rate Hz, depth semitones); -1: the kit's
    std::string rr;                          // round robin: more voices it cycles through, "NAME@CAT;NAME@CAT"
    bool plain() const { return voice.empty() && level == 1 && pan < 0 && tune == 0 && !mute && !solo && a < 0 && d < 0 && s < 0 && r < 0 && f < 0 && lr < 0 && ld < 0 && rr.empty(); }
};

struct DrumKit {
    std::array<KitPiece, KIT_SLOTS> p {};
    float env[4] = { -1, -1, -1, -1 };   // the whole kit's envelope (attack, decay, sustain, release; -1: each voice's own)
    float fx[3] = { -1, -1, -1 };        // the whole kit's filter and LFO (cutoff, rate, depth; -1: each voice's own)
    bool rrOn = false;                   // round robin: pads with more voices cycle through them, hit by hit
    bool empty() const { for (float e : env) if (e >= 0) return false; for (float e : fx) if (e >= 0) return false; if (rrOn) return false; for (const auto& k : p) if (!k.plain()) return false; return true; }
    // "note:VOICE,level,pan,tune,mute,solo|..." (only the pieces changed)
    std::string save() const {
        std::ostringstream o; o.precision(4); bool first = true;
        if (env[0] >= 0 || env[1] >= 0 || env[2] >= 0 || env[3] >= 0) { o << "env:" << env[0] << "," << env[1] << "," << env[2] << "," << env[3]; first = false; }
        if (fx[0] >= 0 || fx[1] >= 0 || fx[2] >= 0) { if (!first) o << "|"; o << "fx:" << fx[0] << "," << fx[1] << "," << fx[2]; first = false; }
        if (rrOn) { if (!first) o << "|"; o << "rr:1"; first = false; }
        for (int n = 0; n < KIT_SLOTS; ++n) {
            const auto& k = p[(size_t) n]; if (k.plain()) continue;
            if (!first) o << "|"; first = false;
            o << n << ":" << k.voice << "," << k.level << "," << k.pan << "," << k.tune << "," << (k.mute ? 1 : 0) << "," << (k.solo ? 1 : 0);
            if (k.a >= 0 || k.d >= 0 || k.s >= 0 || k.r >= 0 || k.f >= 0 || k.lr >= 0 || k.ld >= 0 || !k.rr.empty()) o << "," << k.a << "," << k.d << "," << k.s << "," << k.r;
            if (k.f >= 0 || k.lr >= 0 || k.ld >= 0 || !k.rr.empty()) o << "," << k.f << "," << k.lr << "," << k.ld << "," << k.rr;
        }
        return o.str();
    }
    void load(const std::string& s) {
        p = {}; for (float& e : env) e = -1; for (float& e : fx) e = -1; rrOn = false;
        std::istringstream in(s); std::string tok;
        while (std::getline(in, tok, '|')) {
            if (tok.rfind("fx:", 0) == 0) { std::istringstream f(tok.substr(3)); std::string v; int i = 0; while (std::getline(f, v, ',') && i < 3) fx[i++] = (float) std::atof(v.c_str()); continue; }
            if (tok == "rr:1") { rrOn = true; continue; }
            if (tok.rfind("env:", 0) == 0) { std::istringstream f(tok.substr(4)); std::string v; int i = 0; while (std::getline(f, v, ',') && i < 4) env[i++] = (float) std::atof(v.c_str()); continue; }
            const size_t c = tok.find(':'); if (c == std::string::npos) continue;
            const int n = std::atoi(tok.substr(0, c).c_str()); if (n < 0 || n >= KIT_SLOTS) continue;
            std::istringstream f(tok.substr(c + 1)); std::string v; KitPiece k; int i = 0;
            while (std::getline(f, v, ',')) {
                switch (i++) {
                    case 0: k.voice = v; break; case 1: k.level = (float) std::atof(v.c_str()); break; case 2: k.pan = (float) std::atof(v.c_str()); break;
                    case 3: k.tune = (float) std::atof(v.c_str()); break; case 4: k.mute = v == "1"; break; case 5: k.solo = v == "1"; break;
                    case 6: k.a = (float) std::atof(v.c_str()); break; case 7: k.d = (float) std::atof(v.c_str()); break;
                    case 8: k.s = (float) std::atof(v.c_str()); break; case 9: k.r = (float) std::atof(v.c_str()); break;
                    case 10: k.f = (float) std::atof(v.c_str()); break; case 11: k.lr = (float) std::atof(v.c_str()); break; case 12: k.ld = (float) std::atof(v.c_str()); break;
                    case 13: k.rr = v; break; default: break;
                }
            }
            p[(size_t) n] = k;
        }
    }
};

// General MIDI drum names, short (for the kit screen)
inline const char* gmDrumName(int n) {
    static const char* names[47] = {
        "AC KICK", "KICK", "STICK", "SNARE", "CLAP", "E SNARE", "LO FL TOM", "CL HAT", "HI FL TOM", "PED HAT", "LO TOM", "OPEN HAT",
        "LO MID TOM", "HI MID TOM", "CRASH", "HI TOM", "RIDE", "CHINA", "RIDE BELL", "TAMBOURIN", "SPLASH", "COWBELL", "CRASH 2", "VIBRASLAP",
        "RIDE 2", "HI BONGO", "LO BONGO", "MUTE CONGA", "HI CONGA", "LO CONGA", "HI TIMBAL", "LO TIMBAL", "HI AGOGO", "LO AGOGO", "CABASA", "MARACAS",
        "SH WHISTL", "LG WHISTL", "SH GUIRO", "LG GUIRO", "CLAVES", "HI WOOD", "LO WOOD", "MUTE CUICA", "OPEN CUICA", "MUTE TRI", "OPEN TRI" };
    return n >= 35 && n <= 81 ? names[n - 35] : "DRUM";
}

}  // namespace cmi
