// Omega Wheel of Steel on Android: the engine host behind Native.java, without JNI or AAudio so it builds and runs in
// the host-side tests too. Two decks -> beat FX -> mixer, rendered exactly like WheelPlugin's processBlock.
//
// Threads: the audio callback calls render*(); any number of other threads call command() / setValue() and the readers
// (status, cues, meters); "message side" calls (load, trackInfo, wave, trackText, collect) are serialised by a mutex.
// Commands go through a lock-free ring (producers share a tiny spinlock, the audio thread is the only consumer).
// Tracks: every track handed to the audio thread stays referenced in a message-side list, and is freed only by
// collect() once nothing else holds it, so the audio thread never frees a track.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Engine.h"

namespace wheel {

struct ByteSource;

// the constants of Native.java (keep in step)
namespace jni {
enum St { ST_POS, ST_RATE, ST_ANGLE, ST_BPM, ST_PLAYING, ST_TOUCHED, ST_LOOPING, ST_LOOP_A, ST_LOOP_B, ST_CUE, ST_PEAK_L, ST_PEAK_R,
          ST_SHADOW, ST_SYNCED, ST_CENSOR, ST_ROLLING, ST_COUNT };
enum C { C_PLAY_PAUSE, C_PLAY, C_CUE, C_HOT_CUE, C_CLEAR_CUE, C_AUTO_LOOP, C_LOOP_IN, C_LOOP_OUT, C_RELOOP, C_LOOP_EXIT, C_LOOP_HALVE,
         C_LOOP_DOUBLE, C_LOOP_MOVE, C_ROLL, C_SAVED_LOOP, C_TOUCH, C_MOVE, C_BEND, C_NUDGE, C_CENSOR, C_SEEK, C_SYNC, C_SYNC_OFF,
         C_GRID_SHIFT, C_GRID_BPM, C_COUNT };
enum D { D_PITCH, D_RANGE, D_RPM45, D_KEYLOCK, D_SLIP, D_QUANTIZE, D_START_TIME, D_BRAKE_TIME, D_TRIM, D_HI, D_MID, D_LOW, D_FILTER,
         D_FADER, D_PFL, D_XF_ASSIGN, D_FX_TYPE, D_FX_BEATS, D_FX_DEPTH, D_FX_PARAM, D_FX_ON, D_COUNT };
enum G { G_XFADER, G_XF_CURVE, G_FADER_CURVE, G_HAMSTER, G_MASTER, G_CUE_MIX, G_CUE_GAIN, G_COUNT };
constexpr int deckId(int deck, int k) { return deck * 32 + k; }
constexpr int globalId(int k) { return 64 + k; }
constexpr int kIds = 64 + G_COUNT;
constexpr int kTrackInfo = 7;
}  // namespace jni

class Host {
public:
    static constexpr int kMaxBlock = 512;          // render chunk (any callback size is split into these)
    static constexpr double kMaxSeconds = 20 * 60;  // longest track kept
    static constexpr double kAutoRelease = 2.0;     // a touched platter with no touch / move for this long lets go
    static constexpr double kMoveRelease = 0.06;    // C_MOVE without C_TOUCH: the hand lifts this long after the last move

    Host();
    ~Host();

    // ---- audio side
    void prepare(double sampleRate);               // not while a render runs
    double sampleRate() const { return sr.load(std::memory_order_relaxed); }
    void render(float* L, float* R, int n);        // any n; L / R get the master
    void renderInterleaved(float* out, int frames, int channels);   // float, any channel count (1: L+R / 2)
    // with no audio callback running (stream stopped): run the queued commands now. The caller guarantees that no
    // render() runs at the same time.
    void pumpOffline();

    // ---- any thread
    bool command(int type, int deck, int i, double v, bool on);   // false: unknown / queue full
    void setValue(int id, float v);
    float getValue(int id) const;
    void status(int deck, double* out) const;      // jni::ST_COUNT values
    void cues(int deck, double* t, int32_t* rgb);  // kCues each
    void meters(float* out4) const;

    // ---- message side (serialised internally)
    // a decoded track, analysed: copies the samples (capped at 20 minutes), reads tags from 'tags' (may be null),
    // falls back to title / artist, detects BPM + first beat and key where the tags gave none, builds the waveform
    static std::shared_ptr<Track> buildTrack(const float* L, const float* R, size_t n, double trackSr, ByteSource* tags,
                                             const std::string& path, const std::string& title, const std::string& artist);
    bool load(int deck, std::shared_ptr<const Track> t);           // queues it for the audio thread (waits if the queue is full)
    void trackInfo(int deck, double* out7);
    std::vector<uint8_t> wave(int deck, int band);
    std::array<std::string, 3> trackText(int deck);
    void collect();                                // frees the tracks only the keep list holds
    size_t keptTracks();                           // (tests)

private:
    struct Cmd { int type = -1; int deck = 0; int i = 0; double v = 0; bool on = false; std::shared_ptr<const Track> track; };
    static constexpr int kLoad = 1000;
    static constexpr size_t kQueue = 1024;

    // command ring
    Cmd q[kQueue];
    std::atomic<size_t> head { 0 }, tail { 0 };
    std::atomic_flag pushLock = ATOMIC_FLAG_INIT;
    bool push(Cmd&& c);
    void drain();
    void exec(const Cmd& c);

    // engine (audio thread)
    std::atomic<double> sr { 48000 };
    Deck deck[2];
    Fx fx[2];
    Mixer mixer;
    FxSettings fxs[2];
    MixerSettings ms;
    std::vector<float> buf[6];
    int64_t now = 0;
    bool wasPlaying[2] {}, handOn[2] {}, touchHeld[2] {}, relock[2] {};
    double lastRate[2] {};
    int64_t lastHand[2] {};
    std::atomic<float> vals[jni::kIds];
    std::atomic<double> gridShift[2], gridBpm[2];
    void process(int n);
    void applySettings();
    void handlePlatters();
    bool master(int dk, double& bpm, double& phase) const;
    void syncNow(int dk, bool phaseToo);
    void followSync();

    // hot cues (audio thread writes, readers read; seqlock over atomics)
    struct Snap { std::atomic<uint32_t> seq { 0 }; std::atomic<const void*> trk { nullptr }; std::atomic<double> t[kCues]; std::atomic<uint32_t> rgb[kCues]; } snap[2];
    const void* pubTrk[2] {};
    double pubT[2][kCues];
    uint32_t pubRgb[2][kCues];
    void publishCues();

    // message side
    std::mutex msg;
    std::shared_ptr<const Track> current[2];
    uint64_t serial[2] {};
    uint64_t serialCounter = 0;
    std::vector<std::shared_ptr<const Track>> keep;
    std::shared_ptr<const Track> currentTrack(int dk);
};

}  // namespace wheel
