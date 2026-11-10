// Drum Scribe core: shared types. Everything is in seconds (double) at 44.1 kHz audio.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ds {

constexpr int kSR = 44100;
using Signal = std::vector<float>;  // mono

struct Stereo { Signal l, r; size_t size() const { return l.size(); } };

// progress(fraction 0..1 of this step, message)
using Progress = std::function<void(double, const std::string&)>;

// ---- drums
struct DrumHit {
    double time = 0;       // attack start (s), unquantized, sample accurate
    double end = 0;        // where the sound has died away or was choked (s): the MIDI note length
    std::string stem;      // kick snare toms hh ride crash
    std::string piece;     // kit piece, see kit.h
    int note = 0;          // General MIDI drum note
    int velocity = 0;      // 1..127
    float level_db = -180; // peak short-term level
    float rise_db = 0;
};

// ---- bass
struct PitchBend { double time; double semitones; };  // relative to the note's own pitch
struct BassNote {
    double time = 0, end = 0;  // onset and release (s)
    int note = 0;              // MIDI note (nearest semitone of the held pitch)
    double cents = 0;          // held pitch minus the note, cents
    int velocity = 0;
    bool accent = false;       // 303-style accent (much louder than its neighbours)
    bool slide_to_next = false;// the pitch glides into the next note without a new attack (303 slide)
    std::vector<PitchBend> bends;  // glides / vibrato inside the note (empty if steady)
};

// ---- timing grid
struct BeatGrid {
    std::vector<double> beats;      // every beat time (s)
    std::vector<int> downbeats;      // indices into beats where a bar starts
    int beats_per_bar = 4;
    double mean_bpm = 120;
    // tempo map: tempo from each beat to the next (bpm), same length as beats (last repeats)
    std::vector<double> bpm_at_beat;
};

}  // namespace ds
