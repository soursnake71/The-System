// Drum Scribe core: the module interfaces. Each module lives in src/<module>.cpp and is tested by
// tests/test_<module>.cpp against exact ground truth.
#pragma once
#include <nlohmann/json.hpp>
#include "types.h"

namespace ds {

// ---------------------------------------------------------------- audio.cpp
// decode any audio/video file with ffmpeg (subprocess) to 44.1 kHz stereo float; throws std::runtime_error
Stereo load_audio(const std::string& path);
void save_wav(const std::string& path, const Signal& mono, int sr = kSR);          // 32-bit float WAV
void save_wav(const std::string& path, const Stereo& st, int sr = kSR);
Signal to_mono(const Stereo& s);

// ---------------------------------------------------------------- separate.cpp (DS_WITH_SEPARATION)
enum class Quality { fast, best };
struct Stems {
    Stereo drums, bass;                    // from Demucs
    std::map<std::string, Signal> kit;     // from DrumSep: kick snare toms hh ride crash (mono, at the drums' level)
};
Stems separate(const Stereo& song, Quality q, const std::string& model_dir, const Progress& p);

// ---------------------------------------------------------------- drums.cpp
std::vector<DrumHit> transcribe_drums(const std::map<std::string, Signal>& kit, int sr, const Progress& p = {});

// ---------------------------------------------------------------- bass.cpp
std::vector<BassNote> transcribe_bass(const Signal& bass, int sr, const Progress& p = {});

// ---------------------------------------------------------------- beats.cpp
// beat and downbeat tracking from the transcribed hits (and bass onsets); fixed_bpm > 0 forces a constant grid
BeatGrid track_beats(const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass, double duration,
                     double fixed_bpm = 0);

// ---------------------------------------------------------------- groove.cpp
// the maths of the groove: tempo stability, swing, microtiming per instrument, velocity dynamics, syncopation,
// a 16-step groove template, kick/snare/bass push-pull, bass/kick lock. JSON for the apps, text for humans.
nlohmann::json analyze_groove(const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass, const BeatGrid& g,
                              double duration);
std::string groove_text(const nlohmann::json& groove);

// ---------------------------------------------------------------- midi.cpp
struct MidiOptions {
    bool tempo_map = true;      // true: a tempo change on every beat so the DAW grid follows the band;
                                // false: one constant tempo (fixed_bpm, or the grid's mean) - for DAWs that ignore file tempo
    double fixed_bpm = 0;
    bool split_drums = false;   // one track per kit piece
    bool include_drums = true, include_bass = true;
    std::string title = "Drum Scribe";
};
// Standard MIDI File, format 1, 960 PPQ: conductor track (tempo map, time signature, markers per bar, the groove
// summary as text, GM System On SysEx), drum track(s) on channel 10 with note lengths, bass track on channel 2 with
// pitch bend (+-12 semitone range set by RPN 0), portamento CC65/CC5 and legato overlaps for slides.
void write_midi(const std::string& path, const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass,
                const BeatGrid& grid, const nlohmann::json& groove, const MidiOptions& o);

}  // namespace ds
