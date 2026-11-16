// Omega Wheel of Steel library: the music on the SD card (or any folder), crates, Serato data, decoding and analysis.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <functional>
#include "Track.h"

namespace wheel {

// one row in the browser (cheap: tags only, no audio decoded)
struct Entry {
    juce::File file;
    juce::String title, artist, album, keyText;   // keyText as tagged ("8A", "Am")
    double bpm = 0, length = 0;
    bool serato = false;
};

// a crate: a Serato .crate (from <drive>/_Serato_/Subcrates, nested crates "Parent%%Child"), a folder, or "All tracks"
struct Crate {
    juce::String name;           // display, with nesting shown as "Parent / Child"
    int depth = 0;
    enum Kind { all, serato, folder, playlist } kind = folder;
    juce::File source;           // the .crate file, the folder, or the .m3u/.m3u8
};

class Library {
public:
    Library();
    // the root to browse: an SD card / USB drive / music folder. Finds _Serato_ there (or in its parents, or in
    // <root>/Music/_Serato_) and lists its crates; folders under the root with audio become folder crates.
    void setRoot(const juce::File& root);
    juce::File root() const { return rootDir; }
    const juce::Array<Crate>& crates() const { return crateList; }
    // tracks in a crate (Serato crate paths are relative to the drive the _Serato_ folder is on; resolved here,
    // including Mac "/Volumes/X/..." and Windows "D:\..." paths found on another OS: matched by path suffix)
    juce::Array<Entry> list(const Crate& c);
    juce::Array<Entry> search(const juce::String& text, const Crate& c);   // title/artist/album contains, all words
    Entry readEntry(const juce::File& f);                                  // tags only

    // full load: decode (WAV, AIFF, FLAC, Ogg, MP3, plus OS codecs for M4A/AAC), Serato tags (cues, colours, loops,
    // beatgrid, BPM, key), then analysis where Serato gave none: BPM + first beat, key, and always the waveform.
    // Runs on the caller's thread (call it from a background thread). Returns null with err set on failure.
    std::shared_ptr<Track> load(const juce::File& f, juce::String& err, std::function<bool()> cancelled = {});

    juce::AudioFormatManager formats;
    static bool isAudio(const juce::File& f);
private:
    juce::File rootDir, seratoDir;
    juce::Array<Crate> crateList;
};

// building blocks (exposed for the tests)
juce::StringArray parseCrate(const juce::MemoryBlock& data);          // the track paths in a .crate
void readSeratoTags(const juce::File& f, Track& t, Entry* e);          // whatever Serato / ID3 / Vorbis tags say
double detectBpm(const std::vector<float>& mono, double sr, double& firstBeat);   // 60..190 folded to 70..180-ish
int detectKey(const std::vector<float>& mono, double sr);              // Krumhansl-Schmuckler on a chromagram
void buildWave(const Track& t, Wave& w);

}  // namespace wheel
