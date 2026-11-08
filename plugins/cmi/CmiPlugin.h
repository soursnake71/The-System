// Omega CMI as a plugin: the keyboard instrument on MIDI channel 1 (or every channel), Page R's eight tracks
// following the host's transport (or their own clock), the library as programs, user samples on Page 8.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Library.h"
#include "core/Song.h"
#include "core/Catalog.h"
#include "core/Factory.h"
#include "core/Record.h"
#include "core/Pen.h"
#include "core/Pads.h"
#include "core/SampleEdit.h"

namespace cmi {

struct PDef { const char* id; const char* name; float min, max, def, step; };
// the Page 7 controls of the keyboard instrument, then the machine's own settings
// the controllers every control answers to before you teach it others (MIDI learn on Page 7)
inline const std::vector<std::pair<int, const char*>>& defaultCCs() {
    static const std::vector<std::pair<int, const char*>> d = {
        { 7, "level" }, { 74, "filter" }, { 73, "attack" }, { 72, "damp" }, { 69, "sustain" }, { 77, "vib" }, { 76, "vibRate" }, { 78, "vibDelay" },
        { 5, "porta" }, { 70, "velVol" }, { 71, "velFilt" }, { 9, "transpose" }, { 3, "fine" }, { 75, "loop" }, { 79, "seg" }, { 14, "poly" }, { 15, "bend" },
        { 85, "master" }, { 86, "hiss" }, { 87, "spread" }, { 88, "tempo" }, { 89, "swing" }, { 90, "songMode" }, { 102, "sync" }, { 103, "multi" }, { 104, "songLevel" }, { 91, "blend" }, { 92, "blendOn" },
    };
    return d;
}
inline const std::vector<PDef>& paramDefs();
inline int paramIndex(const char* id);

inline const std::vector<PDef>& paramDefs() {
    static const std::vector<PDef> d = {
        { "sound", "Sound", 0, MAX_SOUNDS - 1, NO_VOICE, 1 },
        { "level", "Level", 0, 1, 0.85f, 0 },
        { "filter", "Filter", 0, 255, 255, 1 },
        { "attack", "Attack", 0, 31, 0, 0 },
        { "damp", "Damping", 0, 31, 10, 0 },
        { "sustain", "Sustain", 0, 1, 1, 1 },
        { "vib", "Vibrato depth", 0, 1, 0, 0 },
        { "vibRate", "Vibrato speed", 0.5f, 12, 5.5f, 0 },
        { "vibDelay", "Vibrato delay", 0, 2, 0.25f, 0 },
        { "porta", "Portamento", 0, 1, 0, 0 },
        { "velVol", "Velocity > level", 0, 1, 0.6f, 0 },
        { "velFilt", "Velocity > filter", 0, 1, 0.3f, 0 },
        { "transpose", "Transpose", -24, 24, 0, 1 },
        { "fine", "Fine tune", -100, 100, 0, 0 },
        { "loop", "Loop", 0, 1, 1, 1 },
        { "seg", "Segment time", 0.005f, 0.2f, 0.03f, 0 },
        { "poly", "Voices", 1, 8, 8, 1 },
        { "bend", "Bend range", 0, 12, 2, 1 },
        { "master", "Master", 0, 1.5f, 1, 0 },
        { "hiss", "Noise floor", 0, 4, 1, 0 },
        { "spread", "Stereo spread", 0, 1, 0.3f, 0 },
        { "multi", "Multitimbral MIDI", 0, 1, 0, 1 },
        { "sync", "Sync to host", 0, 1, 1, 1 },
        { "tempo", "Speed (BPM)", 40, 240, 120, 0 },
        { "swing", "Swing", 0, 0.5f, 0, 0 },
        { "songMode", "Song mode", 0, 1, 0, 1 },
        { "songLevel", "MIDI song level", 0, 1, 0.5f, 0 },
        { "blend", "Loop blend", 0, 1, 0.5f, 0 },   // crossfade at loop points; 0: the original card (none)
        { "blendOn", "Loop blend on", 0, 1, 1, 1 },  // the switch: off plays loops as the original card did
    };
    return d;
}

inline int paramIndex(const char* id) { const auto& d = paramDefs(); for (size_t i = 0; i < d.size(); ++i) if (!std::strcmp(d[i].id, id)) return (int) i; return -1; }

class CmiProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    CmiProcessor();
    ~CmiProcessor() override = default;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return (int) factoryPresets().size(); }   // the factory presets
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- for the editor ----
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;
    Kit kit;
    Engine engine;
    float get(const char* id) const { auto* p = apvts.getRawParameterValue(id); return p ? p->load() : 0.f; }
    void set(const char* id, float v);
    void loadSound(int index);                   // a sound and the controls it was saved with
    juce::String loadSample(const juce::File& f, int slot, int root, bool loop);   // "" or the error
    juce::String importFiles(const juce::Array<juce::File>& filesOrFolders, int root, bool loop, int& imported);   // a summary
    bool oldNumbers = false;                  // the state being read counted the old built-in voices first
    std::atomic<bool> showCatalog { true };   // Page 2: the library folder's voices, or only the ones loaded
    void connectDefaults();                   // Page R's tracks and the keyboard: CMI voices from the library
    int loadCatalogVoice(int catIndex);       // a library voice into the user voices: its sound index, or -1
    void preview(int track) { previewReq = track; }
    // MIDI songs
    SongList songs;
    juce::String importMidi(const juce::File& f);          // "" or the error; the song is selected
    void chooseSong(int i);                                // -1: the Page R patterns
    int currentSong() const { return songs.indexOf(engine.song.load()); }
    void setSongVoice(int track, int voice);
    // the CMI library: every voice on the disks in the library folder
    Catalog catalog; juce::CriticalSection catLock;
    int connectToLibrary(int songIndex);                   // the song's tracks to library voices; how many matched
    void useCatalogVoice(int track, int catIndex);         // a library voice for one track (loaded, remembered)
    std::map<int, std::string> rememberedFor(const MidiSong& s) const;
    void remember(const MidiSong& s, int track, const std::string& what);
    void setSongMute(int track, bool m) { if (auto* e = songs.at(currentSong())) { e->mute[track] = m; engine.songMute[track] = m; } }
    // a track one note at a time (or chords); remembered for the song
    void setSongMono(int track, bool m) { if (auto* e = songs.at(currentSong())) { e->mono[track] = m; engine.songMono[track] = m; remember(*e->song, 200 + track, m ? "MONO" : "POLY"); } }
    void seqPlay(bool on) { engine.seqRun = on; }
    std::atomic<float> meter { 0 };

    // MIDI control: controller -> parameter (index into paramDefs, -1 none), MIDI learn, the last controller seen
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnParam { -1 }, lastCC { -1 };
    void resetCCMap();
    int ccFor(int param) const { for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() == param) return c; return -1; }

    // the library folder: where your songs, disks and samples live (remembered for every project too)
    juce::File libraryFolder() const { const juce::ScopedLock l(infoLock); return libFolder; }
    // the song folder: your MIDI songs, to browse and load
    juce::File songFolder() const { const juce::ScopedLock l(infoLock); return songDir; }
    void setSongFolder(const juce::File& f);
    std::vector<juce::File> songFiles() const { const juce::ScopedLock l(infoLock); return songList; }
    void rescanSongs();
    int loadedSongFor(const juce::File& f) const;          // the song's index if it's loaded, else -1
    void setLibraryFolder(const juce::File& f);
    std::vector<juce::File> libraryFiles() const { const juce::ScopedLock l(infoLock); return libFiles; }
    void rescanLibrary(bool force = true);   // not forced: the catalogue kept from the last scan, if it's of this folder
    bool hfeOnly = true, libFromHfe = false;   // the library: its HFE images first (the other copies fill in)
    void setHfeOnly(bool on) { hfeOnly = on; if (settings) { settings->setValue("libraryHfeOnly", on); settings->saveIfNeeded(); } rescanLibrary(true); }
    // the grid's audition: a note on a song track, played for a moment
    void playSongFrom(double beat) { engine.songStartReq = beat; engine.songRun = true; }
    void audition(int track, int note) { auditionReq = (track << 8) | (note & 0x7F); }
    std::atomic<int> auditionReq { -1 }; int auditionOn = -1; int64_t auditionLeft = 0;
    float loopBlendNow() const { return get("blendOn") > 0.5f ? get("blend") : 0.f; }
    // ---- factory presets (Page R): the IIx factory library, each found in your library folder ----
    std::vector<int> presetCat;               // preset -> catalogue voice, -1 missing (under catLock)
    std::map<int, std::string> presetChoice;  // presets you matched by hand: "NAME@DISK"
    std::atomic<int> curPreset { -1 };
    void rebuildPresets();                    // after the library or a choice changes
    bool loadPreset(int n);                   // onto the keyboard; false: not in your library
    void choosePresetVoice(int n, int catIndex);   // match a preset to a library voice by hand (-1: automatic)
    // a voice drawn on Page 5: into a new user voice, or over the one drawn last
    juce::String saveDrawnVoice(const float* start, const float* end, bool asNew);
    int lastDrawn = -1;
    // ---- the drum kit (the selected song's) ----
    std::vector<int> kitNotes(bool all) const;             // the GM notes its kit tracks play (all: 35-81)
    KitPiece kitPiece(int note) const;
    void setKitPiece(int note, const KitPiece& p);         // into the song, the engine, and remembered for the song
    void setKitVoice(int note, int soundIndex);            // -1: automatic
    void setKitCatalogVoice(int note, int catIndex);
    void auditionKit(int note);
    void resetKit();
    // ---- the drum pads (core/Pads.h): the kit every song's drums, channel 10 and the pads play ----
    juce::String padKitName = "CMI DRUM MAP"; int padPreset = -1, padBank = 0, padSel = 0;
    const DrumKit& pads() const { return engine.padKit; }
    void setPads(DrumKit k, const juce::String& name, bool fit);   // its voices loaded (fit: from this library)
    void setPadPiece(int note, const KitPiece& p);
    void setPadVoice(int note, int soundIndex);                   // -1: automatic
    void setPadCatalogVoice(int note, int catIndex);
    void hitPad(int note, int vel) { engine.padPush(note, vel); }
    juce::String padPresetLoad(int n);
    juce::String padRandom();
    juce::String padImport(const juce::Array<juce::File>& filesOrFolders, bool replaceKit);
    void padsClear() { setPads(DrumKit(), "CMI DRUM MAP", false); padPreset = -1; }
    int unloadUnusedLibrary();                                   // library voices nothing uses; how many went
    juce::String recordPads();
    PadEditor padEd;                                             // the sample editor (Page W), a pad's sound
    bool editPad(int note) { if (padEd.open) padEd.close(); return padEd.begin(kit, engine, note); }
    void editCommit() { padEd.commit(kit, engine); }
    void setPadEnv(int note, int i, float v);                    // note -1: the whole kit; v -1: not set
    void resetAll(bool factory);                                 // CLEAR ALL (folders, library, songs, samples stay) / FACTORY RESET                                   // REC on the pads: the drum track of the song (stop: keep it)
    // your samples folder (remembered for every project): its sounds load as your voices
    juce::File samplesFolder() const { return samplesDir; }
    void setSamplesFolder(const juce::File& f);
    int loadSamplesFolder();                                     // the ones not loaded yet; how many
    // ---- recording ----
    Recorder recorder; RecordSettings& rec = recorder.rs; bool& recCountIn = recorder.countIn; std::atomic<bool>& recording = recorder.on;
    juce::String startRecording(int track);                // from the start of the song view (recFromBeat)
    juce::String stopRecording();                          // the notes onto the track; a summary
    void pollRecording();
    double recFromBeat = 0;
    // ---- the light pen ----
    PenDrawing pen; int penSlot = -1; bool penLive = true;
    void penChanged();                                     // the live PEN voice (the keyboard plays it)
    juce::String penSaveAs();                              // a new voice, kept with the project
    juce::String penExport(const juce::File& f);           // a WAV with its loop and root
    bool penFrom(int soundIndex);

    // exports (run on a background thread; done() comes back on the message thread)
    void exportAudio(const juce::File& f, bool stems, std::function<void(juce::String)> done);
    void exportMidi(const juce::File& f, std::function<void(juce::String)> done);
    std::atomic<bool> exporting { false };

private:
    void timerCallback() override;
    juce::String makeSound(const juce::File& f, int root, bool loop, Sound& out);
    juce::String soundsFrom(const juce::File& f, int root, bool loop, std::vector<Sound>& out);
    Controls controlsNow() const;
    void writeSeq(juce::XmlElement&) const;
    void readSeq(const juce::XmlElement&);

    int program = 0;
    std::atomic<int> pendingProgram { -1 }, previewReq { -1 }, bankMsb { 0 };
    std::array<std::atomic<float>, 128> ccVal; std::atomic<uint64_t> ccDirty[2];
    juce::File samplesDir;
    juce::File libFolder; std::vector<juce::File> libFiles; juce::File songDir; std::vector<juce::File> songList; juce::CriticalSection infoLock;
    std::unique_ptr<juce::PropertiesFile> settings;
    int dupSkipped = 0;
    juce::ThreadPool pool { 1 };
    juce::AudioFormatManager formats;
    std::vector<float> bufL, bufR;
    std::vector<std::atomic<float>*> raw;
};

}  // namespace cmi
