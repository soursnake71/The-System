// The drum kit: every piece the engine writes, with its General MIDI note.
#pragma once
#include <string>
#include <vector>

namespace ds {
struct KitPiece { const char* id; int note; const char* label; const char* stem; };
inline const std::vector<KitPiece>& kit_pieces()
{
    static const std::vector<KitPiece> k = {
        {"kick", 36, "Kick", "kick"},           {"snare", 38, "Snare", "snare"},
        {"side_stick", 37, "Side stick", "snare"}, {"hh_closed", 42, "Hi-hat closed", "hh"},
        {"hh_pedal", 44, "Hi-hat pedal", "hh"}, {"hh_open", 46, "Hi-hat open", "hh"},
        {"tom_high", 50, "Tom high", "toms"},   {"tom_himid", 48, "Tom hi-mid", "toms"},
        {"tom_lowmid", 47, "Tom low-mid", "toms"}, {"tom_low", 45, "Tom low", "toms"},
        {"floor_high", 43, "Floor tom high", "toms"}, {"floor_low", 41, "Floor tom low", "toms"},
        {"crash1", 49, "Crash 1", "crash"},     {"crash2", 57, "Crash 2", "crash"},
        {"ride", 51, "Ride", "ride"},           {"ride_bell", 53, "Ride bell", "ride"},
    };
    return k;
}
inline int note_of(const std::string& piece)
{
    for (auto& p : kit_pieces()) if (piece == p.id) return p.note;
    return 0;
}
inline const char* label_of(int note)
{
    for (auto& p : kit_pieces()) if (note == p.note) return p.label;
    return "?";
}
inline const std::vector<std::string>& stem_names()
{
    static const std::vector<std::string> s = {"kick", "snare", "toms", "hh", "ride", "crash"};
    return s;
}
}  // namespace ds
