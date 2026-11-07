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

struct KitPiece {
    std::string voice;   // "NAME@CATEGORY" (a loaded voice), empty: automatic
    float level = 1, pan = -1, tune = 0;   // level 0..2, pan 0..1 (-1: the track's), tune in semitones
    bool mute = false, solo = false;
    bool plain() const { return voice.empty() && level == 1 && pan < 0 && tune == 0 && !mute && !solo; }
};

struct DrumKit {
    std::array<KitPiece, 128> p {};
    bool empty() const { for (const auto& k : p) if (!k.plain()) return false; return true; }
    // "note:VOICE,level,pan,tune,mute,solo|..." (only the pieces changed)
    std::string save() const {
        std::ostringstream o; o.precision(4); bool first = true;
        for (int n = 0; n < 128; ++n) {
            const auto& k = p[(size_t) n]; if (k.plain()) continue;
            if (!first) o << "|"; first = false;
            o << n << ":" << k.voice << "," << k.level << "," << k.pan << "," << k.tune << "," << (k.mute ? 1 : 0) << "," << (k.solo ? 1 : 0);
        }
        return o.str();
    }
    void load(const std::string& s) {
        p = {};
        std::istringstream in(s); std::string tok;
        while (std::getline(in, tok, '|')) {
            const size_t c = tok.find(':'); if (c == std::string::npos) continue;
            const int n = std::atoi(tok.substr(0, c).c_str()); if (n < 0 || n > 127) continue;
            std::istringstream f(tok.substr(c + 1)); std::string v; KitPiece k; int i = 0;
            while (std::getline(f, v, ',')) {
                switch (i++) {
                    case 0: k.voice = v; break; case 1: k.level = (float) std::atof(v.c_str()); break; case 2: k.pan = (float) std::atof(v.c_str()); break;
                    case 3: k.tune = (float) std::atof(v.c_str()); break; case 4: k.mute = v == "1"; break; case 5: k.solo = v == "1"; break; default: break;
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
