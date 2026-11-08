// Omega CMI on Android: the engine from plugins/cmi/core, an AAudio output stream, and the JNI the Java screen uses.
// Everything that changes the engine from Java goes through a queue the audio callback drains, so the engine is
// only ever touched by the audio thread (pattern bytes and track settings are plain data and written directly).
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include "Disk.h"
#include "Song.h"
#include "Catalog.h"
#include "Factory.h"
#include "GridEdit.h"
#include "Record.h"
#include "Pen.h"
#include "Pads.h"
#include <map>
#include <sstream>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "OmegaCMI", __VA_ARGS__)

using namespace cmi;

namespace {

enum CmdType { C_MIDI, C_PREVIEW, C_ALLOFF, C_AUDITION };
struct Cmd { int type; uint8_t m[3]; int len; int a; };

constexpr int NPARAM = 25;   // 23: loop blend amount, 24: loop blend on
const float kDefaults[NPARAM] = { 0.85f, 255, 0, 10, 1, 0, 5.5f, 0.25f, 0, 0.6f, 0.3f, 0, 0, 1, 0.03f, 8, 2, 1, 1, 0.3f, 120, 0, 0, 0.5f, 1 };

struct App {
    Kit kit;
    Engine engine;
    SongList songs;
    Catalog catalog; std::mutex catLock;
    std::map<std::string, std::map<int, std::string>> remembered;   // songKey -> track -> "NAME@DISK" / "KIT"
    std::atomic<int> sound { NO_VOICE };
    std::atomic<float> param[NPARAM];
    // queue: many producers (UI, MIDI threads) under a mutex, one consumer (audio)
    std::array<Cmd, 2048> q; std::atomic<uint32_t> qw { 0 }, qr { 0 }; std::mutex qm;
    AAudioStream* stream = nullptr; std::mutex streamLock;
    std::atomic<int> bankMsb { 0 };
    std::vector<float> L, R;
    double rate = 48000;

    App() {
        for (int i = 0; i < NPARAM; ++i) param[i] = kDefaults[i];
        engine.bank = &kit.bank;
        engine.kitVoice = cmiKitLookup(kit.bank);
        Library::factorySeq(engine.seq, kit.bank);   // its voices come from the library folder (connectDefaults)
        engine.extSync = false;
    }
    // Page R's tracks and the keyboard: CMI voices from the library when they have none
    // factory presets (Page R): preset -> catalogue voice (-1 missing); presets matched by hand: "NAME@DISK"
    std::vector<int> presetCat; std::map<int, std::string> presetChoice; std::atomic<int> curPreset { -1 };
    void rebuildPresets() {   // under catLock
        const int n = (int) factoryPresets().size();
        std::vector<int> m((size_t) n, -1);
        if (!catalog.voices.empty()) for (int i = 0; i < n; ++i) { auto it = presetChoice.find(i); m[(size_t) i] = findFactory(catalog, i, it == presetChoice.end() ? "" : it->second); }
        presetCat = std::move(m);
    }
    bool loadPreset(int n) {
        int v = -1;
        { std::lock_guard<std::mutex> g(catLock);
          if (n < 0 || n >= (int) presetCat.size() || presetCat[(size_t) n] < 0) return false;
          v = loadCatalogVoice(kit, catalog, presetCat[(size_t) n]); }
        if (v < 0) return false;
        loadSound(v); curPreset = n; return true;
    }
    // the light pen and the recorder (core/Pen.h, core/Record.h)
    PenDrawing pen; int penSlot = -1; bool penLive = true;
    // the drum pads (core/Pads.h): the kit's name, its preset, samples coming in from a folder
    std::string padKitName = "CMI DRUM MAP"; int padPreset = -1; std::vector<KitSample> padIn;
    Recorder recorder;
    void connectDefaults() {
        std::lock_guard<std::mutex> g(catLock);
        fillLibraryPlaces(kit, catalog);   // a reopened project's library voices, back in their places
        { DrumKit pk = engine.padKit; if (padsReady(pk, kit, catalog)) { padKitName = kitPresets()[0].name; padPreset = 0; } applyPads(engine, pk); }   // the pads' sounds
        rebuildPresets();
        for (int i = 0; i < songs.size(); ++i) if (SongEntry* e = songs.at(i)) if (!e->kit.empty()) loadKitVoices(e->kit, kit, catalog);   // the kits' chosen voices
        if (SongEntry* e = songs.at(songs.indexOf(engine.song.load()))) { ensureConnected(*e, kit, catalog, remembered[songKey(e->song->name)]); refreshSong(engine, e); }   // the song on the new library
        connectPageR(engine.seq, kit, catalog);
        if (isBlank(kit, sound.load())) {
            int v = -1;
            for (const auto& w : splitBar(defaultKeyboardVoices())) if ((v = findVoice(kit.bank, w)) >= 0) break;
            if (v < 0) v = loadCatalogNamed(kit, catalog, defaultKeyboardVoices());
            if (v >= 0) loadSound(v);
        }
    }
    void push(const Cmd& c) {
        std::lock_guard<std::mutex> g(qm);
        const uint32_t w = qw.load(std::memory_order_relaxed);
        if (w - qr.load(std::memory_order_acquire) >= q.size()) return;
        q[w % q.size()] = c; qw.store(w + 1, std::memory_order_release);
    }
    void loadSound(int i) {
        const Sound* s = kit.bank.at(i); if (!s) return;
        const Controls& c = s->ctl;
        const float v[15] = { c.level, c.filter, c.attack, c.damp, c.sustain, c.vibDepth, c.vibRate, c.vibDelay, c.porta, c.velVol, c.velFilt, c.transpose, c.fine, c.loop, c.segTime };
        for (int k = 0; k < 15; ++k) param[k] = v[k];
        sound = i; curPreset = -1;
    }
    Controls controls() const {
        Controls c;
        c.level = param[0]; c.filter = param[1]; c.attack = param[2]; c.damp = param[3]; c.sustain = param[4]; c.vibDepth = param[5]; c.vibRate = param[6];
        c.vibDelay = param[7]; c.porta = param[8]; c.velVol = param[9]; c.velFilt = param[10]; c.transpose = param[11]; c.fine = param[12]; c.loop = param[13]; c.segTime = param[14];
        return c;
    }
    int audOn = -1, audLeft = 0;
    void render(float* out, int n) {
        uint32_t r = qr.load(std::memory_order_relaxed);
        const uint32_t w = qw.load(std::memory_order_acquire);
        engine.setKeyboard(sound.load(), controls());
        engine.setPoly((int) param[15].load()); engine.setBendRange(param[16]);
        engine.setOutput(param[17], param[18], param[19]);
        engine.tempo = param[20].load(); engine.seq.swing = param[21]; engine.songMode = param[22] > 0.5f; engine.loopBlend = param[24] > 0.5f ? param[23].load() : 0.f;
        while (r != w) {
            const Cmd& c = q[r % q.size()];
            if (c.type == C_MIDI) engine.midi(c.m, c.len);
            else if (c.type == C_PREVIEW) engine.trackNote(c.a, 0, 0.9f);
            else if (c.type == C_ALLOFF) engine.allOff(true);
            else if (c.type == C_AUDITION) {   // the grid: a note on a song track, for a moment
                if (audOn >= 0) engine.songNote(audOn >> 8, audOn & 0x7F, 0);
                engine.songNote(c.a >> 8, c.a & 0x7F, 100); audOn = c.a; audLeft = (int) (rate * 0.35);
            }
            ++r;
        }
        if (audOn >= 0 && (audLeft -= n) <= 0) { engine.songNote(audOn >> 8, audOn & 0x7F, 0); audOn = -1; }
        qr.store(r, std::memory_order_release);
        if ((int) L.size() < n) { L.resize((size_t) n); R.resize((size_t) n); }
        engine.render(L.data(), R.data(), n, nullptr);
        for (int i = 0; i < n; ++i) { out[2 * i] = L[(size_t) i]; out[2 * i + 1] = R[(size_t) i]; }
    }
};

App* app() { static App* a = new App(); return a; }

aaudio_data_callback_result_t dataCb(AAudioStream*, void*, void* audio, int32_t frames) {
    app()->render((float*) audio, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void openStream();
void errorCb(AAudioStream*, void*, aaudio_result_t err) {
    LOG("stream error %d, reopening", err);
    std::thread([] { std::lock_guard<std::mutex> g(app()->streamLock); if (app()->stream) { AAudioStream_close(app()->stream); app()->stream = nullptr; } openStream(); }).detach();
}
void openStream() {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, dataCb, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, errorCb, nullptr);
    AAudioStream* s = nullptr;
    if (AAudioStreamBuilder_openStream(b, &s) == AAUDIO_OK) {
        app()->rate = AAudioStream_getSampleRate(s);
        app()->engine.prepare(app()->rate);
        AAudioStream_setBufferSizeInFrames(s, AAudioStream_getFramesPerBurst(s) * 2);
        AAudioStream_requestStart(s);
        app()->stream = s;
        LOG("AAudio %d Hz, burst %d", (int) app()->rate, AAudioStream_getFramesPerBurst(s));
    }
    AAudioStreamBuilder_delete(b);
}

// ---- state ----
template <class T> void put(std::vector<uint8_t>& v, const T& x) { const uint8_t* p = (const uint8_t*) &x; v.insert(v.end(), p, p + sizeof(T)); }
template <class T> bool get(const uint8_t*& p, const uint8_t* e, T& x) { if (p + sizeof(T) > e) return false; std::memcpy(&x, p, sizeof(T)); p += sizeof(T); return true; }

}  // namespace

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_thesnakedevice_cmi_Native_##name

FN(void, start)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->streamLock); if (!app()->stream) openStream(); }
FN(void, stop)(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> g(app()->streamLock);
    if (app()->stream) { AAudioStream_requestStop(app()->stream); AAudioStream_close(app()->stream); app()->stream = nullptr; }
}
// the same default controllers as the plugin (Page 7 controls)
static bool ccParam(int cc, int v) {
    struct M { int cc, param; float lo, hi; bool toggle, integer; };
    static const M map[] = { { 7, 0, 0, 1, false, false }, { 74, 1, 0, 255, false, true }, { 73, 2, 0, 31, false, false }, { 72, 3, 0, 31, false, false },
                             { 69, 4, 0, 1, true, true }, { 77, 5, 0, 1, false, false }, { 76, 6, 0.5f, 12, false, false }, { 78, 7, 0, 2, false, false },
                             { 5, 8, 0, 1, false, false }, { 70, 9, 0, 1, false, false }, { 71, 10, 0, 1, false, false }, { 9, 11, -24, 24, false, true },
                             { 3, 12, -100, 100, false, false }, { 75, 13, 0, 1, true, true }, { 79, 14, 0.005f, 0.2f, false, false }, { 14, 15, 1, 8, false, true },
                             { 15, 16, 0, 12, false, true }, { 85, 17, 0, 1.5f, false, false }, { 86, 18, 0, 4, false, false }, { 87, 19, 0, 1, false, false },
                             { 88, 20, 40, 240, false, false }, { 89, 21, 0, 0.5f, false, false }, { 90, 22, 0, 1, true, true }, { 91, 23, 0, 1, false, false }, { 92, 24, 0, 1, true, true } };
    for (const auto& m : map) if (m.cc == cc) {
        float x = m.lo + v / 127.f * (m.hi - m.lo);
        if (m.toggle) x = v >= 64 ? 1.f : 0.f; else if (m.integer) x = std::round(x);
        app()->param[m.param] = x;
        return true;
    }
    if (cc == 104) { app()->engine.songGain = v / 127.0; return true; }
    return false;
}
// program n: the n-th voice loaded
static void programChange(int n) {
    if (app()->loadPreset(n)) return;   // program n: factory preset n, when it's in your library
    int k = 0;
    for (int i = 0; i < app()->kit.bank.count.load(); ++i) {
        if (isBlank(app()->kit, i)) continue;
        if (k++ == n) { app()->loadSound(i); return; }
    }
}

FN(void, midi)(JNIEnv* env, jclass, jbyteArray data, jint off, jint len) {
    jbyte* b = env->GetByteArrayElements(data, nullptr);
    // split a stream of messages (running status aware)
    static uint8_t status = 0;
    int i = off; const int end = off + len;
    while (i < end) {
        uint8_t s = (uint8_t) b[i];
        if (s >= 0xF8) {                                 // realtime: start / continue / stop run the song or Page R
            if (s == 0xFA || s == 0xFB) { if (app()->engine.song.load()) app()->engine.songRun = true; else app()->engine.seqRun = true; }
            else if (s == 0xFC) { app()->engine.songRun = false; app()->engine.seqRun = false; }
            ++i; continue;
        }
        if (s & 0x80) { status = s; ++i; } else if (!status) { ++i; continue; }
        const int hi = status & 0xF0;
        const int need = (hi == 0xC0 || hi == 0xD0) ? 1 : (status >= 0xF0 ? 0 : 2);
        if (status >= 0xF0) { status = 0; continue; }
        if (i + need > end) break;
        Cmd c {}; c.type = C_MIDI; c.m[0] = status; c.len = 1 + need;
        for (int k = 0; k < need; ++k) c.m[1 + k] = (uint8_t) b[i + k];
        i += need;
        if (hi == 0xC0) { programChange(app()->bankMsb * 128 + c.m[1]); continue; }   // program change: the voice list
        if (hi == 0xB0 && c.m[1] == 0) { app()->bankMsb = c.m[2]; continue; }          // bank select pages through it
        if (hi == 0xB0 && ccParam(c.m[1], c.m[2])) continue;                            // a control on its default CC
        app()->push(c);
    }
    env->ReleaseByteArrayElements(data, b, JNI_ABORT);
}
FN(void, noteOn)(JNIEnv*, jclass, jint n, jint v) { Cmd c {}; c.type = C_MIDI; c.m[0] = 0x90; c.m[1] = (uint8_t) n; c.m[2] = (uint8_t) std::max(1, (int) v); c.len = 3; app()->push(c); }
FN(void, noteOff)(JNIEnv*, jclass, jint n) { Cmd c {}; c.type = C_MIDI; c.m[0] = 0x80; c.m[1] = (uint8_t) n; c.len = 3; app()->push(c); }
FN(void, allOff)(JNIEnv*, jclass) { Cmd c {}; c.type = C_ALLOFF; app()->push(c); }
FN(void, preview)(JNIEnv*, jclass, jint t) { Cmd c {}; c.type = C_PREVIEW; c.a = t; app()->push(c); }

FN(jint, soundCount)(JNIEnv*, jclass) { return app()->kit.bank.count.load(); }
FN(jstring, soundInfo)(JNIEnv* env, jclass, jint i) {
    const Sound* s = app()->kit.bank.at(i);
    if (!s) return env->NewStringUTF("");
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d", s->name.c_str(), s->title.c_str(), s->category.c_str(), (int) s->rate, s->root, s->pages(),
                  s->loopStart, s->loopEnd, (int) s->mode);
    return env->NewStringUTF(buf);
}
FN(jint, wave)(JNIEnv* env, jclass, jint i, jbyteArray out) {
    const Sound* s = app()->kit.bank.at(i);
    if (!s) return 0;
    const jsize n = std::min<jsize>(env->GetArrayLength(out), (jsize) s->ram.size());
    env->SetByteArrayRegion(out, 0, n, (const jbyte*) s->ram.data());
    return n;
}
FN(jint, getSound)(JNIEnv*, jclass) { return app()->sound.load(); }
FN(void, loadSound)(JNIEnv*, jclass, jint i) { app()->loadSound(i); }
FN(jfloat, getParam)(JNIEnv*, jclass, jint i) { return i >= 0 && i < NPARAM ? app()->param[i].load() : 0.f; }
FN(void, setParam)(JNIEnv*, jclass, jint i, jfloat v) { if (i >= 0 && i < NPARAM) app()->param[i] = v; }
FN(jfloat, envTimeOf)(JNIEnv*, jclass, jfloat v) { return (jfloat) envTime(v); }

// Page R
FN(void, seqRun)(JNIEnv*, jclass, jboolean on) { app()->engine.seqRun = on; }
FN(jboolean, seqRunning)(JNIEnv*, jclass) { return app()->engine.seqRun.load(); }
FN(jint, seqStep)(JNIEnv*, jclass) { return app()->engine.seqStep.load(); }
FN(jint, seqPat)(JNIEnv*, jclass) { return app()->engine.seqPat.load(); }
FN(jint, seqBar)(JNIEnv*, jclass) { return app()->engine.seqBar.load(); }
FN(jint, getPattern)(JNIEnv*, jclass) { return app()->engine.curPattern.load(); }
FN(void, setPattern)(JNIEnv*, jclass, jint p) { app()->engine.curPattern = clampi(p, 0, NPAT - 1); }
FN(jint, getStep)(JNIEnv*, jclass, jint p, jint t, jint s) {
    const PStep& st = app()->engine.seq.pat[clampi(p, 0, NPAT - 1)].st[clampi(t, 0, TRACKS - 1)][clampi(s, 0, STEPS - 1)];
    return st.vel | ((st.note & 0xFF) << 8);
}
FN(void, setStep)(JNIEnv*, jclass, jint p, jint t, jint s, jint vel, jint note) {
    PStep& st = app()->engine.seq.pat[clampi(p, 0, NPAT - 1)].st[clampi(t, 0, TRACKS - 1)][clampi(s, 0, STEPS - 1)];
    st.note = (int8_t) clampi(note, -24, 24); st.vel = (uint8_t) clampi(vel, 0, 127);
}
FN(void, copyPattern)(JNIEnv*, jclass, jint from, jint to) { auto& q = app()->engine.seq; if (from >= 0 && from < NPAT && to >= 0 && to < NPAT) q.pat[to] = q.pat[from]; }
FN(jint, trackSound)(JNIEnv*, jclass, jint t) { return app()->engine.seq.trk[clampi(t, 0, TRACKS - 1)].sound; }
FN(void, setTrackSound)(JNIEnv*, jclass, jint t, jint s) { app()->engine.seq.trk[clampi(t, 0, TRACKS - 1)].sound = s; }
FN(jboolean, trackAudible)(JNIEnv*, jclass, jint t) { return app()->engine.seq.trk[clampi(t, 0, TRACKS - 1)].audible; }
FN(void, setTrackAudible)(JNIEnv*, jclass, jint t, jboolean a) { app()->engine.seq.trk[clampi(t, 0, TRACKS - 1)].audible = a; }
FN(jint, songLen)(JNIEnv*, jclass) { return app()->engine.seq.songLen; }
FN(void, setSongLen)(JNIEnv*, jclass, jint n) { app()->engine.seq.songLen = clampi(n, 1, SONG_MAX); }
FN(jint, songAt)(JNIEnv*, jclass, jint i) { return app()->engine.seq.song[clampi(i, 0, SONG_MAX - 1)]; }
FN(void, setSongAt)(JNIEnv*, jclass, jint i, jint p) { app()->engine.seq.song[clampi(i, 0, SONG_MAX - 1)] = clampi(p, 0, NPAT - 1); }
FN(void, channelEnv)(JNIEnv* env, jclass, jintArray out) {
    jint v[NCH];
    for (int i = 0; i < NCH; ++i) { const auto c = app()->engine.view(i); v[i] = c.state ? c.env : 0; }
    env->SetIntArrayRegion(out, 0, std::min<jsize>(NCH, env->GetArrayLength(out)), v);
}

// Page 8: mono float samples from a decoded file (loop points in samples, -1: none)
FN(jstring, loadSample)(JNIEnv* env, jclass, jfloatArray data, jint rate, jstring name, jint slot, jint root, jboolean loop, jint loopStart, jint loopEnd) {
    const jsize n = env->GetArrayLength(data);
    if (n <= 0 || rate <= 0) return env->NewStringUTF("EMPTY FILE");
    if (slot < 0 || slot >= Library::USER_SLOTS) return env->NewStringUTF("USER VOICES FULL");
    std::vector<float> x((size_t) n);
    env->GetFloatArrayRegion(data, 0, n, x.data());
    const char* nm = env->GetStringUTFChars(name, nullptr);
    Sound s = sampleToCmi(x, rate, nm, root, loop, loopStart, loopEnd);
    env->ReleaseStringUTFChars(name, nm);
    app()->kit.setUser(slot, std::move(s));
    return env->NewStringUTF("");
}
// a whole WAV file: its own root key and loop (smpl chunk) win over root / loop
FN(jstring, loadWav)(JNIEnv* env, jclass, jbyteArray data, jstring name, jint slot, jint root, jboolean loop) {
    if (slot < 0 || slot >= Library::USER_SLOTS) return env->NewStringUTF("USER VOICES FULL");
    const jsize n = env->GetArrayLength(data);
    std::vector<uint8_t> b((size_t) n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*) b.data());
    WavData w = parseWav(b.data(), b.size());
    if (!w.err.empty()) return env->NewStringUTF(w.err.c_str());
    if (w.mono.empty()) return env->NewStringUTF("EMPTY FILE");
    const char* nm = env->GetStringUTFChars(name, nullptr);
    Sound s = sampleToCmi(w.mono, w.rate, nm, w.root >= 0 ? w.root : root, loop, w.loopStart, w.loopEnd);
    env->ReleaseStringUTFChars(name, nm);
    app()->kit.setUser(slot, std::move(s));
    return env->NewStringUTF("");
}
// a CMI disk image (.IMD / .IMG): every voice on it, into the free user voices; returns "<count>" or the error
FN(jstring, loadDisk)(JNIEnv* env, jclass, jbyteArray data, jstring fileName) {
    const jsize n = env->GetArrayLength(data);
    std::vector<uint8_t> b((size_t) n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*) b.data());
    Disk dk = readDiskImage(b.data(), b.size());
    if (!dk.err.empty()) return env->NewStringUTF(dk.err.c_str());
    const char* fn = env->GetStringUTFChars(fileName, nullptr);
    const std::string label = dk.label.empty() ? std::string(fn) : dk.label;
    env->ReleaseStringUTFChars(fileName, fn);
    int count = 0;
    for (const auto& f : dk.files) {
        if (f.type != "VC") continue;
        const int slot = app()->kit.firstFree();
        if (slot < 0) break;
        Sound s; std::string err;
        if (vcToSound(f.data, f.name, label, s, err)) { app()->kit.setUser(slot, std::move(s)); ++count; }
    }
    return env->NewStringUTF(std::to_string(count).c_str());
}
// a .VC voice file into one slot; "" or the error
FN(jstring, loadVc)(JNIEnv* env, jclass, jbyteArray data, jstring name, jstring from, jint slot) {
    if (slot < 0 || slot >= Library::USER_SLOTS) return env->NewStringUTF("USER VOICES FULL");
    const jsize n = env->GetArrayLength(data);
    std::vector<uint8_t> b((size_t) n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*) b.data());
    const char* nm = env->GetStringUTFChars(name, nullptr); const char* fr = env->GetStringUTFChars(from, nullptr);
    Sound s; std::string err;
    const bool ok = vcToSound(b, nm, fr, s, err);
    env->ReleaseStringUTFChars(name, nm); env->ReleaseStringUTFChars(from, fr);
    if (!ok) return env->NewStringUTF(err.c_str());
    app()->kit.setUser(slot, std::move(s));
    return env->NewStringUTF("");
}
// ---- MIDI songs (Page R) ----
static int curSongIdx() { return app()->songs.indexOf(app()->engine.song.load()); }
FN(jstring, importMidi)(JNIEnv* env, jclass, jbyteArray data, jstring name) {
    const jsize n = env->GetArrayLength(data);
    std::vector<uint8_t> b((size_t) n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*) b.data());
    const char* nm = env->GetStringUTFChars(name, nullptr);
    std::string up = nm; for (auto& c : up) c = (char) (c == '_' || c == '-' ? ' ' : std::toupper((unsigned char) c));
    env->ReleaseStringUTFChars(name, nm);
    MidiSong ms; std::string err;
    if (!parseSmf(b.data(), b.size(), up, ms, err)) return env->NewStringUTF(err.c_str());
    int dup = 0;
    const int i = app()->songs.addUnique(std::move(ms), app()->kit.bank, dup);
    if (dup == 1) return env->NewStringUTF("DUPLICATE");
    {   // the original CMI voices, when a library is set
        std::lock_guard<std::mutex> g(app()->catLock);
        SongEntry* e = app()->songs.at(i);
        if (e) ensureConnected(*e, app()->kit, app()->catalog, app()->remembered[songKey(e->song->name)]);
    }
    selectSong(app()->engine, app()->songs.at(i));
    return env->NewStringUTF("");
}
FN(jint, songCount)(JNIEnv*, jclass) { return app()->songs.count(); }
FN(jstring, songName)(JNIEnv* env, jclass, jint k) { const SongEntry* e = app()->songs.at(app()->songs.nth(k)); return env->NewStringUTF(e ? e->song->name.c_str() : ""); }
FN(void, selectSongAt)(JNIEnv*, jclass, jint k) {
    SongEntry* e = k < 0 ? nullptr : app()->songs.at(app()->songs.nth(k));
    if (e) { std::lock_guard<std::mutex> g(app()->catLock); ensureConnected(*e, app()->kit, app()->catalog, app()->remembered[songKey(e->song->name)]); }
    selectSong(app()->engine, e);
}
FN(jint, currentSong)(JNIEnv*, jclass) {   // its place in the list, -1: Page R patterns
    const int i = curSongIdx(); if (i < 0) return -1;
    int k = 0; for (int j = 0; j < i; ++j) k += app()->songs.at(j) != nullptr; return k;
}
FN(void, removeSong)(JNIEnv*, jclass) { const int i = curSongIdx(); selectSong(app()->engine, nullptr); app()->songs.remove(i); }
/** name, bpm, num, den, length (beats), beat now */
FN(jstring, songInfo)(JNIEnv* env, jclass) {
    const MidiSong* s = app()->engine.song.load();
    if (!s) return env->NewStringUTF("");
    char buf[256]; const double beat = app()->engine.songBeat.load();
    std::snprintf(buf, sizeof buf, "%s\t%.1f\t%d\t%d\t%.3f\t%.3f", s->name.c_str(), s->bpmAt(beat), s->num, s->den, s->length, beat);
    return env->NewStringUTF(buf);
}
/** track name, voice, used, muted */
FN(jstring, songTrack)(JNIEnv* env, jclass, jint t) {
    const MidiSong* s = app()->engine.song.load();
    if (!s || t < 0 || t >= SONG_TRK) return env->NewStringUTF("");
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s\t%d\t%d\t%d", s->trackName[t].c_str(), app()->engine.songVoice[t], (int) s->used[t], (int) app()->engine.songMute[t]);
    return env->NewStringUTF(buf);
}
FN(void, setSongVoice)(JNIEnv*, jclass, jint t, jint v) { if (t < 0 || t >= SONG_TRK) return; if (auto* e = app()->songs.at(curSongIdx())) e->voice[t] = v; app()->engine.songVoice[t] = v; }
FN(void, setSongMute)(JNIEnv*, jclass, jint t, jboolean m) { if (t < 0 || t >= SONG_TRK) return; if (auto* e = app()->songs.at(curSongIdx())) e->mute[t] = m; app()->engine.songMute[t] = m; }
FN(jboolean, songMono)(JNIEnv*, jclass, jint t) { return t >= 0 && t < SONG_TRK && app()->engine.songMono[t]; }
FN(void, setSongMono)(JNIEnv*, jclass, jint t, jboolean m) {
    if (t < 0 || t >= SONG_TRK) return;
    if (auto* e = app()->songs.at(curSongIdx())) { e->mono[t] = m; app()->remembered[songKey(e->song->name)][200 + t] = m ? "MONO" : "POLY"; }
    app()->engine.songMono[t] = m;
}
FN(void, gmVoices)(JNIEnv*, jclass) { if (auto* e = app()->songs.at(curSongIdx())) { autoSongVoices(*e->song, app()->kit.bank, e->voice, e->why); selectSong(app()->engine, e); } }
FN(void, songPlay)(JNIEnv*, jclass, jboolean on) { app()->engine.songRun = on; }
FN(jboolean, songRunning)(JNIEnv*, jclass) { return app()->engine.songRun.load(); }
FN(jboolean, songLoop)(JNIEnv*, jclass) { return app()->engine.songLoop.load(); }
FN(void, setSongLoop)(JNIEnv*, jclass, jboolean on) { app()->engine.songLoop = on; }
/** the notes, four ints each: track, start and end (in 1/10000 of the song), pitch in the track's range (0..1000) */
FN(jintArray, songNotes)(JNIEnv* env, jclass) {
    const MidiSong* s = app()->engine.song.load();
    std::vector<jint> out;
    if (s) {
        int lo[SONG_TRK], hi[SONG_TRK]; for (int t = 0; t < SONG_TRK; ++t) { lo[t] = 127; hi[t] = 0; }
        for (const auto& e : s->ev) if ((e.status & 0xF0) == 0x90 && e.d2) { lo[e.track] = std::min(lo[e.track], (int) e.d1); hi[e.track] = std::max(hi[e.track], (int) e.d1); }
        const double len = std::max(1.0, s->length);
        for (size_t i = 0; i < s->ev.size() && out.size() < 4 * 6000; ++i) {
            const auto& e = s->ev[i];
            if ((e.status & 0xF0) != 0x90 || !e.d2) continue;
            double end = e.beat + 0.25;
            for (size_t k = i + 1; k < s->ev.size() && k < i + 400; ++k) { const auto& o = s->ev[k]; if (o.track == e.track && o.d1 == e.d1 && ((o.status & 0xF0) == 0x80 || ((o.status & 0xF0) == 0x90 && !o.d2))) { end = o.beat; break; } }
            out.push_back(e.track); out.push_back((jint) (e.beat / len * 10000)); out.push_back((jint) (std::min(end, len) / len * 10000));
            out.push_back((jint) (1000.0 * (e.d1 - lo[e.track]) / std::max(1, hi[e.track] - lo[e.track])));
        }
    }
    jintArray a = env->NewIntArray((jsize) out.size());
    env->SetIntArrayRegion(a, 0, (jsize) out.size(), out.data());
    return a;
}

// ---- exports, written straight to the file the system picker opened (a file descriptor) ----
static bool writeAll(int fd, const void* p, size_t n) { const uint8_t* b = (const uint8_t*) p; while (n) { const ssize_t w = ::write(fd, b, n); if (w <= 0) return false; b += w; n -= (size_t) w; } return true; }
// the song (or one track, track >= 0) as a 16-bit stereo WAV at 44.1 kHz, rendered in pieces so it never sits in memory
FN(jstring, exportSongWav)(JNIEnv* env, jclass, jint fd, jint track) {
    const SongEntry* e = app()->songs.at(curSongIdx());
    const double fs = 44100;
    auto eng = std::make_unique<Engine>();
    eng->bank = &app()->kit.bank; eng->kitVoice = kitLookup(app()->kit.bank); eng->prepare(fs);
    eng->applyFx(app()->engine.fxSettings());   // the REV7 as set now
    eng->setOutput(app()->param[17], app()->param[18], app()->param[19]); eng->extSync = false; eng->songGain = app()->engine.songGain.load(); eng->loopBlend = app()->param[24] > 0.5f ? app()->param[23].load() : 0.f;
    size_t total = 0, maxLen;
    if (e) {
        for (int t = 0; t < SONG_TRK; ++t) { eng->songVoice[t] = e->voice[t]; eng->songMute[t] = e->mute[t] || (track >= 0 && t != track); }
        eng->song = e->song.get(); eng->songRun = true; maxLen = (size_t) (fs * 60 * 30);
    } else {
        eng->seq = app()->engine.seq; eng->songMode = true; eng->tempo = app()->param[20].load(); eng->seqRun = true;
        maxLen = (size_t) (eng->seq.songLen * STEPS / (app()->param[20] / 60.0 * 4.0) * fs);
    }
    uint8_t hdr[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
    if (!writeAll(fd, hdr, 44)) { ::close(fd); return env->NewStringUTF("CAN'T WRITE THE FILE"); }
    const int blk = 1024; std::vector<float> L(blk), R(blk); std::vector<int16_t> pcm(blk * 2);
    int quiet = 0;
    while (total < maxLen + (size_t) (fs * 30)) {
        if (!e && total >= maxLen) eng->seqRun = false;
        eng->render(L.data(), R.data(), blk);
        float pk = 0;
        for (int i = 0; i < blk; ++i) { pcm[(size_t) (2 * i)] = (int16_t) std::lround(clampd(L[(size_t) i], -1, 1) * 32767); pcm[(size_t) (2 * i + 1)] = (int16_t) std::lround(clampd(R[(size_t) i], -1, 1) * 32767); pk = std::max({ pk, std::fabs(L[(size_t) i]), std::fabs(R[(size_t) i]) }); }
        if (!writeAll(fd, pcm.data(), pcm.size() * 2)) { ::close(fd); return env->NewStringUTF("CAN'T WRITE THE FILE"); }
        total += blk;
        const bool ended = e ? !eng->songRun.load() : total >= maxLen;
        if (ended) { quiet = pk < 0.0005f ? quiet + blk : 0; if (quiet > fs * 0.3 || (eng->activeVoices() == 0 && !eng->fxOn.load())) break; }   // the REV7 rings out
    }
    // the sizes, now that they're known (when the file can seek)
    const uint32_t data = (uint32_t) (total * 4), riff = 36 + data;
    if (::lseek(fd, 4, SEEK_SET) == 4) { writeAll(fd, &riff, 4); if (::lseek(fd, 40, SEEK_SET) == 40) writeAll(fd, &data, 4); }
    ::close(fd);
    char msg[64]; std::snprintf(msg, sizeof msg, "EXPORTED %.0f S", total / fs);
    return env->NewStringUTF(msg);
}
FN(jstring, exportSongMidi)(JNIEnv* env, jclass, jint fd) {
    const SongEntry* e = app()->songs.at(curSongIdx());
    if (!e) { ::close(fd); return env->NewStringUTF("NO SONG SELECTED"); }
    std::string names[SONG_TRK];
    for (int t = 0; t < SONG_TRK; ++t) { const Sound* s = app()->kit.bank.at(e->voice[t]); names[t] = e->voice[t] == GM_KIT ? "DRUM KIT" : s ? s->name : ""; }
    const auto b = writeSmf(*e->song, names);
    const bool ok = writeAll(fd, b.data(), b.size()); ::close(fd);
    return env->NewStringUTF(ok ? "EXPORTED MIDI" : "CAN'T WRITE THE FILE");
}
FN(jint, songTrackCount)(JNIEnv*, jclass) { return SONG_TRK; }
/** every note: five ints each, track, start and length (1/480 beat), pitch, velocity */
FN(jintArray, songNoteList)(JNIEnv* env, jclass) {
    const MidiSong* s = app()->engine.song.load();
    std::vector<jint> out;
    if (s) for (const auto& n : notesOf(*s)) { out.push_back(n.track); out.push_back((jint) std::lround(n.start * 480)); out.push_back((jint) std::lround(n.len * 480)); out.push_back(n.pitch); out.push_back(n.vel); }
    jintArray a = env->NewIntArray((jsize) out.size()); env->SetIntArrayRegion(a, 0, (jsize) out.size(), out.data()); return a;
}
FN(void, setSongNoteList)(JNIEnv* env, jclass, jintArray notes) {
    const jsize n = env->GetArrayLength(notes);
    std::vector<jint> v((size_t) n); env->GetIntArrayRegion(notes, 0, n, v.data());
    std::vector<SongNote> list;
    for (jsize i = 0; i + 4 < n; i += 5) list.push_back({ v[(size_t) i], v[(size_t) i + 1] / 480.0, v[(size_t) i + 2] / 480.0, v[(size_t) i + 3], v[(size_t) i + 4] });
    editSong(app()->engine, app()->songs.at(curSongIdx()), list);
}
FN(void, auditionSong)(JNIEnv*, jclass, jint t, jint note) { Cmd c {}; c.type = C_AUDITION; c.a = (t << 8) | (note & 0x7F); app()->push(c); }
// the grid's edits (GridEdit.h): notes five ints each (track, start, length in 1/480 beat, pitch, velocity) and a
// selection flag each; returns six ints per note (the five and the flag). ops: 1 erase, 2 transpose a, 3 move a beats,
// 4 quantize to a, 5 velocity +a, 6 legato, 7 duplicate (snap a), 8 copy, 9 paste at beat a, 10 select all,
// 11 select the box a..b beats, c..d pitches (add if e)
FN(jintArray, gridOp)(JNIEnv* env, jclass, jintArray notes, jbooleanArray sel, jint track, jint op, jfloat a, jfloat b, jfloat c, jfloat d, jboolean e) {
    const jsize n = env->GetArrayLength(notes), ns = env->GetArrayLength(sel);
    std::vector<jint> v((size_t) n); env->GetIntArrayRegion(notes, 0, n, v.data());
    std::vector<jboolean> sv((size_t) ns); if (ns) env->GetBooleanArrayRegion(sel, 0, ns, sv.data());
    GridEdit ge; std::vector<SongNote> list;
    for (jsize i = 0; i + 4 < n; i += 5) list.push_back({ v[(size_t) i], v[(size_t) i + 1] / 480.0, v[(size_t) i + 2] / 480.0, v[(size_t) i + 3], v[(size_t) i + 4] });
    ge.setNotes(list);
    for (size_t i = 0; i < ge.sel.size() && i < sv.size(); ++i) ge.sel[i] = sv[i] ? 1 : 0;
    switch (op) {
        case 1: ge.erase(); break; case 2: ge.transpose(track, (int) a); break; case 3: ge.move(track, a); break;
        case 4: ge.quantize(track, a); break; case 5: ge.velocity(track, (int) a); break; case 6: ge.legato(track); break;
        case 7: ge.duplicate(a); break; case 8: ge.copy(); break; case 9: ge.paste(track, a); break; case 10: ge.selectAll(track); break;
        case 11: ge.selectRect(track, a, b, (int) c, (int) d, e); break; case 12: ge.humanize(track, a, (int) b, (uint32_t) std::rand()); break; default: break;
    }
    std::vector<jint> out;
    for (size_t i = 0; i < ge.notes.size(); ++i) { const auto& x = ge.notes[i]; out.insert(out.end(), { x.track, (jint) std::lround(x.start * 480), (jint) std::lround(x.len * 480), x.pitch, x.vel, ge.sel[i] ? 1 : 0 }); }
    jintArray r = env->NewIntArray((jsize) out.size()); env->SetIntArrayRegion(r, 0, (jsize) out.size(), out.data()); return r;
}
FN(void, songPlayFrom)(JNIEnv*, jclass, jdouble beat) { app()->engine.songStartReq = beat; app()->engine.songRun = true; }
FN(jint, clipboardSize)(JNIEnv*, jclass) { return (jint) GridEdit::clipboard().size(); }
FN(jboolean, undoSongEdit)(JNIEnv*, jclass) { return undoSong(app()->engine, app()->songs.at(curSongIdx())); }
FN(void, newSong)(JNIEnv*, jclass) { auto& sl = app()->songs; selectSong(app()->engine, sl.at(sl.add(emptySong("NEW SONG " + std::to_string(sl.count() + 1)), app()->kit.bank))); }
FN(jstring, songWhy)(JNIEnv* env, jclass, jint t) { const SongEntry* e = app()->songs.at(curSongIdx()); return env->NewStringUTF(e && t >= 0 && t < SONG_TRK ? e->why[t].c_str() : ""); }

// ---- mixer: strips 0 keyboard, 1..8 Page R tracks, 9..24 song tracks ----
// strip STRIPS: the REV7 return (fader = its OUTPUT knob, meter = its return); what 5: a strip's send to the REV7
FN(jfloat, mixGet)(JNIEnv*, jclass, jint i, jint what) {
    auto& e = app()->engine;
    if (i == Engine::STRIPS) return what == 0 ? (float) e.fx.get("ret") : what == 4 ? e.fxMeter.load() : what == 2 ? (float) !e.fxOn.load() : 0.f;
    if (i < 0 || i >= Engine::STRIPS) return 0;
    if (what == 5) return e.fxSend[(size_t) i].load();
    return what == 0 ? e.fader[(size_t) i].load() : what == 1 ? e.panOver[(size_t) i].load() : what == 2 ? (float) e.mute[(size_t) i].load() : what == 3 ? (float) e.solo[(size_t) i].load() : e.meter[(size_t) i].load();
}
FN(void, mixSet)(JNIEnv*, jclass, jint i, jint what, jfloat v) {
    auto& e = app()->engine;
    if (i == Engine::STRIPS) { if (what == 0) e.fx.set("ret", clampd(v, 0, 1.5)); else if (what == 2) e.fxOn = v < 0.5f; return; }
    if (i < 0 || i >= Engine::STRIPS) return;
    if (what == 5) { e.fxSend[(size_t) i] = clampd(v, 0, 1); return; }
    if (what == 0) e.fader[(size_t) i] = clampd(v, 0, 1.5); else if (what == 1) e.panOver[(size_t) i] = v; else if (what == 2) e.mute[(size_t) i] = v > 0.5f; else if (what == 3) e.solo[(size_t) i] = v > 0.5f;
}
// ---- the effects section: the REV7 ----
FN(jint, fxCount)(JNIEnv*, jclass) { return Rev7Fx::spec().params.size(); }
// id, name, min, max, step, toggle 1/0, choices ("label=value|..."), shown 1/0
FN(jstring, fxParam)(JNIEnv* env, jclass, jint i) {
    const auto& ps = Rev7Fx::spec().params; if (i < 0 || i >= (int) ps.size()) return env->NewStringUTF("");
    const auto& p = ps[(size_t) i]; std::ostringstream o;
    o << p.id << "\t" << p.name << "\t" << p.min << "\t" << p.max << "\t" << p.step << "\t" << (p.toggle ? 1 : 0) << "\t";
    for (size_t k = 0; k < p.choices.size(); ++k) o << (k ? "|" : "") << p.choices[k].label << "=" << p.choices[k].value;
    o << "\t" << (Rev7Fx::shown(p) && p.id != "prog" ? 1 : 0);
    return env->NewStringUTF(o.str().c_str());
}
FN(jfloat, fxGet)(JNIEnv*, jclass, jint i) { return (float) app()->engine.fx.get(i); }
FN(void, fxSet)(JNIEnv*, jclass, jint i, jfloat v) { app()->engine.fx.set(i, v); }
FN(jstring, fxFormat)(JNIEnv* env, jclass, jint i) {
    const auto& ps = Rev7Fx::spec().params; if (i < 0 || i >= (int) ps.size()) return env->NewStringUTF("");
    return env->NewStringUTF(Rev7Fx::format(ps[(size_t) i], app()->engine.fx.get(i)).c_str());
}
FN(jboolean, fxRelevant)(JNIEnv*, jclass, jint i) { const auto& ps = Rev7Fx::spec().params; return i >= 0 && i < (int) ps.size() && app()->engine.fx.relevant(ps[(size_t) i].id); }
FN(jint, fxPresetCount)(JNIEnv*, jclass) { return Rev7Fx::spec().presets.size(); }
FN(jstring, fxPresetName)(JNIEnv* env, jclass, jint k) { const auto& pr = Rev7Fx::spec().presets; return env->NewStringUTF(k >= 0 && k < (int) pr.size() ? pr[(size_t) k].name.c_str() : ""); }
FN(void, fxLoadPreset)(JNIEnv*, jclass, jint k) { app()->engine.fx.preset(k); }
// the dice: a random preset, or (settings) a random program with its settings rolled; what it rolled
FN(jstring, fxDice)(JNIEnv* env, jclass, jboolean settings) { return env->NewStringUTF((settings ? app()->engine.fx.rollSettings() : app()->engine.fx.rollPreset()).c_str()); }
FN(jint, fxCurrentPreset)(JNIEnv*, jclass) { return app()->engine.fx.curPreset.load(); }
FN(void, fxNoPreset)(JNIEnv*, jclass) { app()->engine.fx.curPreset = -1; }
FN(jstring, fxProgName)(JNIEnv* env, jclass, jint n) { return env->NewStringUTF(omega::rev7ProgName(n)); }
FN(jboolean, fxOn)(JNIEnv*, jclass) { return app()->engine.fxOn.load(); }
FN(void, setFxOn)(JNIEnv*, jclass, jboolean on) { app()->engine.fxOn = on; }
// on, the sends and the REV7's settings as text (kept in the app's settings)
FN(jstring, fxState)(JNIEnv* env, jclass) {
    auto& e = app()->engine; std::ostringstream o; o << (e.fxOn.load() ? 1 : 0) << "|";
    for (int i = 0; i < Engine::STRIPS; ++i) o << (i ? "," : "") << e.fxSend[(size_t) i].load();
    o << "|" << e.fx.save();
    return env->NewStringUTF(o.str().c_str());
}
FN(void, setFxState)(JNIEnv* env, jclass, jstring text) {
    const char* c = env->GetStringUTFChars(text, nullptr); std::string s = c; env->ReleaseStringUTFChars(text, c);
    const size_t a = s.find('|'); if (a == std::string::npos) return; const size_t b = s.find('|', a + 1); if (b == std::string::npos) return;
    auto& e = app()->engine; e.fxOn = s.substr(0, a) == "1";
    std::istringstream in(s.substr(a + 1, b - a - 1)); std::string tok; int i = 0;
    while (std::getline(in, tok, ',') && i < Engine::STRIPS) e.fxSend[(size_t) i++] = (float) clampd(std::atof(tok.c_str()), 0, 1);
    e.fx.load(s.substr(b + 1));
}

FN(jfloat, masterMeter)(JNIEnv*, jclass) { return std::max(app()->engine.meterL.load(), app()->engine.meterR.load()); }
FN(jint, removeDuplicateSongs)(JNIEnv*, jclass) { const int c = curSongIdx(); const int n = app()->songs.removeDuplicates(); if (!app()->songs.at(c)) selectSong(app()->engine, nullptr); return n; }
// a voice drawn with the pen: two 128-point cycles; into a new slot (asNew) or over the last drawn one
static int lastDrawn = -1;
FN(jstring, saveDrawn)(JNIEnv* env, jclass, jfloatArray a, jfloatArray b, jboolean asNew) {
    float s0[PAGE], s1[PAGE];
    env->GetFloatArrayRegion(a, 0, PAGE, s0); env->GetFloatArrayRegion(b, 0, PAGE, s1);
    auto& kit = app()->kit;
    int slot = asNew || lastDrawn < 0 || !kit.userLoaded(lastDrawn) ? kit.firstFree() : lastDrawn;
    if (slot < 0) return env->NewStringUTF("USER VOICES FULL");
    int n = 1; for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->name.rfind("DRAW", 0) == 0) ++n;
    const std::string name = slot == lastDrawn && kit.userLoaded(slot) ? kit.user(slot)->name : "DRAW" + std::to_string(n);
    kit.setUser(slot, drawnVoice(name, s0, s1)); lastDrawn = slot;
    app()->loadSound(kit.userSlot(slot));
    return env->NewStringUTF("");
}
FN(jboolean, hasDrawn)(JNIEnv*, jclass) { return lastDrawn >= 0; }

// ---- the CMI library (catalogue of the library folder's disks and .VC files) ----
// which library file the catalogue reads (core libraryRole): 0 none, 1 a disk image, 2 a .VC
FN(jint, libraryRole)(JNIEnv* env, jclass, jstring name, jboolean hfeFirst) {
    const char* c = env->GetStringUTFChars(name, nullptr); const int r = libraryRole(c, hfeFirst); env->ReleaseStringUTFChars(name, c); return r;
}
FN(jint, catalogHfeDisks)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->catLock); return app()->catalog.hfeDisks; }
FN(jint, catalogHfeSetAside)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->catLock); return app()->catalog.hfeSetAside; }
FN(void, catalogClear)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->catLock); app()->catalog = Catalog(); }
FN(void, catalogAdd)(JNIEnv* env, jclass, jbyteArray data, jstring fileName, jstring folder) {
    const jsize n = env->GetArrayLength(data); std::vector<uint8_t> b((size_t) n); env->GetByteArrayRegion(data, 0, n, (jbyte*) b.data());
    const char* fn = env->GetStringUTFChars(fileName, nullptr); const char* fo = env->GetStringUTFChars(folder, nullptr);
    { std::lock_guard<std::mutex> g(app()->catLock); app()->catalog.add(fn, b.data(), b.size(), fo); }
    env->ReleaseStringUTFChars(fileName, fn); env->ReleaseStringUTFChars(folder, fo);
}
// the catalogue kept in a file, so the library is scanned once
FN(jboolean, catalogSave)(JNIEnv* env, jclass, jstring path) {
    const char* p = env->GetStringUTFChars(path, nullptr); std::string fn = p; env->ReleaseStringUTFChars(path, p);
    std::vector<uint8_t> b; { std::lock_guard<std::mutex> g(app()->catLock); b = app()->catalog.save(); }
    std::ofstream f(fn, std::ios::binary); f.write((const char*) b.data(), (std::streamsize) b.size()); return (bool) f;
}
FN(jboolean, catalogLoad)(JNIEnv* env, jclass, jstring path) {
    const char* p = env->GetStringUTFChars(path, nullptr); std::string fn = p; env->ReleaseStringUTFChars(path, p);
    std::ifstream f(fn, std::ios::binary); if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    std::lock_guard<std::mutex> g(app()->catLock); return app()->catalog.load(b.data(), b.size());
}
FN(jint, catalogCount)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->catLock); return (jint) app()->catalog.voices.size(); }
FN(jint, catalogDisks)(JNIEnv*, jclass) { std::lock_guard<std::mutex> g(app()->catLock); return app()->catalog.disks; }
/** "NAME\tDISK\tlooped" per voice, newline separated */
FN(jstring, catalogList)(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> g(app()->catLock); std::string o;
    for (const auto& v : app()->catalog.voices) o += v.name + "\t" + v.disk + "\t" + (v.looped ? "1" : "0") + "\n";
    return env->NewStringUTF(o.c_str());
}
FN(jint, connectToLibrary)(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> g(app()->catLock);
    SongEntry* e = app()->songs.at(curSongIdx());
    if (!e || app()->catalog.voices.empty()) return 0;
    ensureConnected(*e, app()->kit, app()->catalog, app()->remembered[songKey(e->song->name)], true);
    int n = 0; for (int t = 0; t < SONG_TRK; ++t) n += e->song->used[t] && (e->voice[t] == GM_KIT || !isBlank(app()->kit, e->voice[t]));
    refreshSong(app()->engine, e);
    return n;
}
// a library voice into the user voices (or the one already there): its sound number, or -1
FN(jint, loadCatalogVoice)(JNIEnv*, jclass, jint ci) { std::lock_guard<std::mutex> g(app()->catLock); return loadCatalogVoice(app()->kit, app()->catalog, ci); }
// Page R's tracks and the keyboard: CMI voices from the library when they have none
FN(void, connectDefaults)(JNIEnv*, jclass) { app()->connectDefaults(); }
FN(jboolean, isBlankSound)(JNIEnv*, jclass, jint i) { return isBlank(app()->kit, i); }
// ---- factory presets (Page R) ----
FN(jint, presetCount)(JNIEnv*, jclass) { return (jint) factoryPresets().size(); }
// name, disk file, category, note, found (1/0), matched by hand (1/0), where it was found
FN(jstring, presetInfo)(JNIEnv* env, jclass, jint n) {
    const auto& ps = factoryPresets(); if (n < 0 || n >= (int) ps.size()) return env->NewStringUTF("");
    const FactoryPreset& p = ps[(size_t) n]; auto* a = app();
    std::lock_guard<std::mutex> g(a->catLock);
    const int ci = n < (int) a->presetCat.size() ? a->presetCat[(size_t) n] : -1;
    std::string where = ci >= 0 ? a->catalog.voices[(size_t) ci].name + " on " + a->catalog.voices[(size_t) ci].disk : "";
    const std::string o = std::string(p.name) + "\t" + p.file + "\t" + p.category + "\t" + factoryNote(p.name) + "\t" + (ci >= 0 ? "1" : "0") + "\t" + (a->presetChoice.count(n) ? "1" : "0") + "\t" + where;
    return env->NewStringUTF(o.c_str());
}
// one char per preset: '1' found, '0' missing, '*' matched by hand
FN(jstring, presetFound)(JNIEnv* env, jclass) {
    auto* a = app(); std::lock_guard<std::mutex> g(a->catLock);
    std::string o; const int n = (int) factoryPresets().size();
    for (int i = 0; i < n; ++i) o += i < (int) a->presetCat.size() && a->presetCat[(size_t) i] >= 0 ? (a->presetChoice.count(i) ? '*' : '1') : '0';
    return env->NewStringUTF(o.c_str());
}
FN(jboolean, loadPreset)(JNIEnv*, jclass, jint n) { return app()->loadPreset(n); }
FN(jint, currentPreset)(JNIEnv*, jclass) { return app()->curPreset.load(); }
FN(void, choosePresetVoice)(JNIEnv*, jclass, jint n, jint ci) {
    auto* a = app(); std::lock_guard<std::mutex> g(a->catLock);
    if (ci >= 0 && ci < (int) a->catalog.voices.size()) a->presetChoice[n] = a->catalog.voices[(size_t) ci].name + "@" + a->catalog.voices[(size_t) ci].disk; else a->presetChoice.erase(n);
    a->rebuildPresets();
}
FN(jstring, getPresetChoices)(JNIEnv* env, jclass) {
    auto* a = app(); std::lock_guard<std::mutex> g(a->catLock);
    std::string o; for (auto& kv : a->presetChoice) o += std::to_string(kv.first) + "=" + kv.second + ";";
    return env->NewStringUTF(o.c_str());
}
FN(void, setPresetChoices)(JNIEnv* env, jclass, jstring text) {
    const char* c = env->GetStringUTFChars(text, nullptr); std::istringstream in(c); env->ReleaseStringUTFChars(text, c);
    auto* a = app(); std::lock_guard<std::mutex> g(a->catLock);
    std::string tok;
    while (std::getline(in, tok, ';')) { const size_t eq = tok.find('='); if (eq != std::string::npos) a->presetChoice[std::atoi(tok.substr(0, eq).c_str())] = tok.substr(eq + 1); }
    a->rebuildPresets();
}

// a library voice on one track: loaded (or reused), remembered for this song
FN(void, useCatalogVoice)(JNIEnv*, jclass, jint t, jint ci) {
    std::lock_guard<std::mutex> g(app()->catLock);
    SongEntry* e = app()->songs.at(curSongIdx()); auto& cat = app()->catalog; auto& kit = app()->kit;
    if (!e || t < 0 || t >= SONG_TRK || ci < 0 || ci >= (int) cat.voices.size()) return;
    const CatVoice& cv = cat.voices[(size_t) ci];
    const int v = loadCatalogVoice(kit, cat, ci);
    if (v < 0) return;
    e->voice[t] = v; app()->engine.songVoice[t] = v; e->why[t] = cv.name + " (" + cv.disk + "): your choice";
    app()->remembered[songKey(e->song->name)][t] = cv.name + "@" + cv.disk.substr(0, 10);
}
FN(void, rememberKit)(JNIEnv*, jclass, jint t) { SongEntry* e = app()->songs.at(curSongIdx()); if (e) app()->remembered[songKey(e->song->name)][t] = "KIT"; }
// the remembered choices as text ("songkey|t=NAME@DISK;..." per line), to keep in the app's settings
FN(jstring, getRemembered)(JNIEnv* env, jclass) {
    std::string o; for (auto& kv : app()->remembered) { o += kv.first + "|"; for (auto& tv : kv.second) o += std::to_string(tv.first) + "=" + tv.second + ";"; o += "\n"; }
    return env->NewStringUTF(o.c_str());
}
FN(void, setRemembered)(JNIEnv* env, jclass, jstring text) {
    const char* c = env->GetStringUTFChars(text, nullptr); std::istringstream in(c); env->ReleaseStringUTFChars(text, c);
    std::string line;
    while (std::getline(in, line)) {
        const size_t bar = line.find('|'); if (bar == std::string::npos) continue;
        auto& m = app()->remembered[line.substr(0, bar)];
        std::istringstream ts(line.substr(bar + 1)); std::string tok;
        while (std::getline(ts, tok, ';')) { const size_t eq = tok.find('='); if (eq != std::string::npos) m[std::atoi(tok.substr(0, eq).c_str())] = tok.substr(eq + 1); }
    }
    // the songs' kits as you left them
    auto* a = app();
    for (int i = 0; i < a->songs.size(); ++i) if (SongEntry* e = a->songs.at(i)) { auto it = a->remembered.find(songKey(e->song->name)); if (it != a->remembered.end()) { if (it->second.count(-1)) e->kit.load(it->second[-1]); applyMonoChoices(*e, it->second); } }
    if (SongEntry* e = a->songs.at(a->songs.indexOf(a->engine.song.load()))) { applyKit(a->engine, e->kit); for (int t = 0; t < SONG_TRK; ++t) a->engine.songMono[t] = e->mono[t]; }
}

// ---- the drum kit (the selected song's): each GM drum's voice, level, pan, tune, mute, solo ----
static SongEntry* curEntry() { return app()->songs.at(curSongIdx()); }
static void kitChanged(SongEntry* e) { applyKit(app()->engine, e->kit); app()->remembered[songKey(e->song->name)][-1] = e->kit.save(); }
FN(jintArray, kitNotes)(JNIEnv* env, jclass, jboolean all) {
    std::vector<jint> o;
    if (all) { for (int n = 35; n <= 81; ++n) o.push_back(n); }
    else if (SongEntry* e = curEntry()) for (int n : kitNotesOf(*e)) o.push_back(n);
    jintArray r = env->NewIntArray((jsize) o.size()); env->SetIntArrayRegion(r, 0, (jsize) o.size(), o.data()); return r;
}
// voice (chosen, or "" ), level, pan, tune, mute, solo, the voice playing it, the GM name
FN(jstring, kitPiece)(JNIEnv* env, jclass, jint n) {
    SongEntry* e = curEntry(); if (!e || n < 0 || n > 127) return env->NewStringUTF("");
    const KitPiece& k = e->kit.p[(size_t) n]; auto& eng = app()->engine;
    int v = eng.kitSound[(size_t) n].load(); if (v < 0 && eng.kitVoice) v = eng.kitVoice(n);
    const Sound* snd = v >= 0 ? app()->kit.bank.at(v) : nullptr;
    std::ostringstream o; o << k.voice << "\t" << k.level << "\t" << k.pan << "\t" << k.tune << "\t" << (k.mute ? 1 : 0) << "\t" << (k.solo ? 1 : 0) << "\t" << (snd ? snd->name : "--") << "\t" << gmDrumName(n);
    return env->NewStringUTF(o.str().c_str());
}
FN(void, setKitPiece)(JNIEnv*, jclass, jint n, jfloat level, jfloat pan, jfloat tune, jboolean mute, jboolean solo) {
    SongEntry* e = curEntry(); if (!e || n < 0 || n > 127) return;
    KitPiece& k = e->kit.p[(size_t) n]; k.level = std::clamp(level, 0.f, 2.f); k.pan = pan; k.tune = std::clamp(tune, -24.f, 24.f); k.mute = mute; k.solo = solo;
    kitChanged(e);
}
static void auditionKitNote(int n) { app()->engine.padTap(n); }
FN(void, setKitVoice)(JNIEnv*, jclass, jint n, jint v) {
    SongEntry* e = curEntry(); if (!e || n < 0 || n > 127) return;
    const Sound* s = v >= 0 && !isBlank(app()->kit, v) ? app()->kit.bank.at(v) : nullptr;
    e->kit.p[(size_t) n].voice = s ? s->name + "@" + s->category : std::string();
    kitChanged(e); auditionKitNote(n);
}
FN(void, setKitCatalogVoice)(JNIEnv*, jclass, jint n, jint ci) {
    int v; { std::lock_guard<std::mutex> g(app()->catLock); v = loadCatalogVoice(app()->kit, app()->catalog, ci); }
    if (v < 0) return;
    SongEntry* e = curEntry(); if (!e || n < 0 || n > 127) return;
    const Sound* s = app()->kit.bank.at(v); e->kit.p[(size_t) n].voice = s ? s->name + "@" + s->category : std::string();
    kitChanged(e); auditionKitNote(n);
}
FN(void, auditionKit)(JNIEnv*, jclass, jint n) { auditionKitNote(n); }
FN(void, resetKit)(JNIEnv*, jclass) { SongEntry* e = curEntry(); if (!e) return; e->kit = DrumKit(); kitChanged(e); }
FN(jfloat, kitMeter)(JNIEnv*, jclass, jint n) { return n >= 0 && n < 128 ? app()->engine.kitMeter[(size_t) n].load() : 0.f; }

// ---- the drum pads (core/Pads.h): the kit every song's drums, MIDI channel 10 and the pads play ----
static void padsSet(DrumKit k, const std::string& name, bool fit) {
    auto* a = app();
    { std::lock_guard<std::mutex> g(a->catLock); if (fit) fitKitToLibrary(k, a->catalog); padsReady(k, a->kit, a->catalog, false); }
    applyPads(a->engine, k); a->padKitName = name;
}
FN(jint, padNote)(JNIEnv*, jclass, jint bank, jint pad) { return padNote(bank, pad); }
// voice (chosen or ""), level, pan, tune, mute, solo, the voice playing it, the GM name, the part
FN(jstring, padInfo)(JNIEnv* env, jclass, jint n) {
    if (n < 0 || n > 127) return env->NewStringUTF("");
    auto& eng = app()->engine; const KitPiece& k = eng.padKit.p[(size_t) n];
    int v = eng.kitSound[(size_t) n].load(); if (v < 0 && eng.kitVoice) v = eng.kitVoice(n);
    const Sound* snd = v >= 0 ? app()->kit.bank.at(v) : nullptr;
    std::ostringstream o; o << k.voice << "\t" << k.level << "\t" << k.pan << "\t" << k.tune << "\t" << (k.mute ? 1 : 0) << "\t" << (k.solo ? 1 : 0) << "\t" << (snd ? snd->name : "--") << "\t" << gmDrumName(n) << "\t" << partName(partOfNote(n));
    return env->NewStringUTF(o.str().c_str());
}
FN(void, setPadPiece)(JNIEnv*, jclass, jint n, jfloat level, jfloat pan, jfloat tune, jboolean mute, jboolean solo) {
    if (n < 0 || n > 127) return;
    DrumKit k = app()->engine.padKit; KitPiece& p = k.p[(size_t) n];
    p.level = std::clamp(level, 0.f, 2.f); p.pan = pan; p.tune = std::clamp(tune, -24.f, 24.f); p.mute = mute; p.solo = solo;
    applyPads(app()->engine, k);
}
FN(void, setPadVoice)(JNIEnv*, jclass, jint n, jint v) {
    if (n < 0 || n > 127) return;
    DrumKit k = app()->engine.padKit;
    const Sound* s = v >= 0 && !isBlank(app()->kit, v) ? app()->kit.bank.at(v) : nullptr;
    if (v < 0) k.p[(size_t) n] = KitPiece(); else k.p[(size_t) n].voice = s ? s->name + "@" + s->category : std::string();
    applyPads(app()->engine, k); app()->padPreset = -1; app()->engine.padTap(n);
}
FN(void, setPadCatalogVoice)(JNIEnv*, jclass, jint n, jint ci) {
    int v; { std::lock_guard<std::mutex> g(app()->catLock); v = loadCatalogVoice(app()->kit, app()->catalog, ci); }
    if (v < 0 || n < 0 || n > 127) return;
    DrumKit k = app()->engine.padKit; const Sound* s = app()->kit.bank.at(v); k.p[(size_t) n].voice = s ? s->name + "@" + s->category : std::string();
    applyPads(app()->engine, k); app()->padPreset = -1; app()->engine.padTap(n);
}
FN(void, hitPad)(JNIEnv*, jclass, jint n, jint vel) { app()->engine.padPush(n, vel); }
FN(void, tapPad)(JNIEnv*, jclass, jint n) { app()->engine.padTap(n); }
// the library's sounds of the kind a pad plays: "catalogue index \t name \t disk" each
FN(jobjectArray, padPartVoices)(JNIEnv* env, jclass, jint n) {
    std::vector<std::string> o;
    { std::lock_guard<std::mutex> g(app()->catLock); const Catalog& cat = app()->catalog; LibraryDrums ld(cat);
      const Part part = partOfNote(n);
      if (part != Part::None) for (int ci : ld.by[(size_t) part]) o.push_back(std::to_string(ci) + "\t" + cat.voices[(size_t) ci].name + "\t" + cat.voices[(size_t) ci].disk);
      for (int pi = 1; pi < PARTS; ++pi) if ((Part) pi != part) for (int ci : ld.by[(size_t) pi]) o.push_back(std::to_string(ci) + "\t" + cat.voices[(size_t) ci].name + "\t" + std::string(partName((Part) pi))); }
    jobjectArray r = env->NewObjectArray((jsize) o.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < o.size(); ++i) { jstring js = env->NewStringUTF(o[i].c_str()); env->SetObjectArrayElement(r, (jsize) i, js); env->DeleteLocalRef(js); }
    return r;
}
FN(jint, padPresetCount)(JNIEnv*, jclass) { return (jint) kitPresets().size(); }
FN(jstring, padPresetName)(JNIEnv* env, jclass, jint i) { const auto& ps = kitPresets(); return env->NewStringUTF(i >= 0 && i < (int) ps.size() ? ps[(size_t) i].name.c_str() : ""); }
FN(jstring, padPresetLoad)(JNIEnv* env, jclass, jint n) {
    const auto& ps = kitPresets(); if (ps.empty()) return env->NewStringUTF("");
    n = ((n % (int) ps.size()) + (int) ps.size()) % (int) ps.size();
    padsSet(ps[(size_t) n].kit, ps[(size_t) n].name, true); app()->padPreset = n;
    return env->NewStringUTF(("KIT " + std::to_string(n + 1) + ": " + ps[(size_t) n].name).c_str());
}
FN(jint, padPreset)(JNIEnv*, jclass) { return app()->padPreset; }
FN(jstring, padKitName)(JNIEnv* env, jclass) { return env->NewStringUTF(app()->padKitName.c_str()); }
FN(jstring, padRandom)(JNIEnv* env, jclass) {
    DrumKit k; { std::lock_guard<std::mutex> g(app()->catLock); if (app()->catalog.voices.empty()) return env->NewStringUTF("NO LIBRARY YET: SET YOUR LIBRARY FOLDER (FIL)"); k = randomKit(app()->catalog, (uint32_t) std::random_device {}()); }
    padsSet(k, "RANDOM KIT", false); app()->padPreset = -1;
    return env->NewStringUTF(("RANDOM KIT: " + kitSummary(app()->engine.padKit)).c_str());
}
FN(void, padsClear)(JNIEnv*, jclass) { padsSet(DrumKit(), "CMI DRUM MAP", false); app()->padPreset = -1; }
FN(jboolean, ch10Kit)(JNIEnv*, jclass) { return app()->engine.ch10Kit.load(); }
FN(void, setCh10Kit)(JNIEnv*, jclass, jboolean on) { app()->engine.ch10Kit = on; }
// samples from a folder onto the pads: begin, each WAV, end
FN(void, padImportBegin)(JNIEnv*, jclass) { app()->padIn.clear(); }
FN(jboolean, padImportAdd)(JNIEnv* env, jclass, jstring name, jbyteArray data) {
    const jsize n = env->GetArrayLength(data); std::vector<uint8_t> v((size_t) n); env->GetByteArrayRegion(data, 0, n, (jbyte*) v.data());
    WavData w = parseWav(v.data(), v.size(), 48000 * 6);
    if (!w.err.empty() || w.mono.empty()) return false;
    const char* nm = env->GetStringUTFChars(name, nullptr); KitSample k; k.file = nm; env->ReleaseStringUTFChars(name, nm);
    k.mono = std::move(w.mono); k.rate = w.rate; app()->padIn.push_back(std::move(k));
    return true;
}
FN(jstring, padImportEnd)(JNIEnv* env, jclass, jboolean replace) {
    auto* a = app(); if (a->padIn.empty()) return env->NewStringUTF("NO WAV FILES THERE");
    DrumKit k = a->engine.padKit;
    const std::string m = importKitSamples(a->padIn, a->kit, k, replace); a->padIn.clear();
    padsSet(k, replace ? "YOUR SAMPLES" : a->padKitName + " +", false); a->padPreset = -1;
    return env->NewStringUTF(m.c_str());
}
// your own voices and the library's: how many; whether one is from the library; unloading the unused
FN(jint, ownVoiceCount)(JNIEnv*, jclass) { return app()->kit.ownCount(); }
FN(jint, libraryVoiceCount)(JNIEnv*, jclass) { return app()->kit.libraryCount(); }
FN(jboolean, userFromLibrary)(JNIEnv*, jclass, jint k) { return app()->kit.userLoaded(k) && app()->kit.user(k)->fromLibrary; }
FN(jboolean, userWaiting)(JNIEnv*, jclass, jint k) { return app()->kit.userLoaded(k) && app()->kit.user(k)->waiting; }
FN(jint, unloadUnusedLibrary)(JNIEnv*, jclass) {
    auto* a = app(); std::lock_guard<std::mutex> g(a->catLock);
    const int n = unloadUnusedLibrary(a->kit, voicesInUse(a->kit, a->sound.load(), a->engine.seq, a->songs, &a->engine.padKit));
    pushKit(a->engine); return n;
}

// ---- recording (core/Record.h): the keyboard and MIDI in onto the selected track, a count-in, the click ----
FN(jstring, recStart)(JNIEnv* env, jclass, jint track, jdouble from, jboolean countIn, jdouble quant, jboolean replace) {
    auto* a = app();
    if (curSongIdx() < 0) Recorder::newSong(a->songs, a->engine, a->kit.bank, a->param[20].load());
    a->recorder.countIn = countIn; a->recorder.rs.quant = quant; a->recorder.rs.replace = replace;
    return env->NewStringUTF(a->recorder.start(a->engine, curEntry(), track, from).c_str());
}
// REC on the pads: onto the song's drum track (a new song if none; track 10 made the kit if it has none), click on
FN(jstring, recordPads)(JNIEnv* env, jclass) {
    auto* a = app();
    if (a->recorder.on.load()) return env->NewStringUTF(a->recorder.stop(a->engine, curEntry()).c_str());
    if (curSongIdx() < 0) Recorder::newSong(a->songs, a->engine, a->kit.bank, a->param[20].load());
    SongEntry* e = curEntry(); if (!e) return env->NewStringUTF("NO SONG");
    int t = -1; for (int k = 0; k < SONG_TRK && t < 0; ++k) if (e->voice[k] == GM_KIT) t = k;
    if (t < 0) { t = 9; e->voice[t] = GM_KIT; a->engine.songVoice[t] = GM_KIT; }
    a->engine.clickOn = true;
    return env->NewStringUTF(a->recorder.start(a->engine, e, t, 0).c_str());
}
FN(jfloat, kickLift)(JNIEnv*, jclass) { return app()->engine.kickLift.load(); }
FN(void, setKickLift)(JNIEnv*, jclass, jfloat v) { app()->engine.kickLift = std::clamp(v, 1.f, 2.83f); }
FN(jfloat, humanize)(JNIEnv*, jclass) { return app()->engine.humanize.load(); }
FN(void, setHumanize)(JNIEnv*, jclass, jfloat v) { app()->engine.humanize = std::clamp(v, 0.f, 1.f); }
FN(void, recPoll)(JNIEnv*, jclass) { if (app()->recorder.on.load()) app()->recorder.poll(app()->engine); }
FN(jstring, recStop)(JNIEnv* env, jclass) { return env->NewStringUTF(app()->recorder.stop(app()->engine, curEntry()).c_str()); }
FN(jboolean, recording)(JNIEnv*, jclass) { return app()->recorder.on.load(); }
FN(jboolean, clickOn)(JNIEnv*, jclass) { return app()->engine.clickOn.load(); }
FN(void, setClick)(JNIEnv*, jclass, jboolean on) { app()->engine.clickOn = on; }

// ---- the light pen (core/Pen.h): the drawing, its tools, the live PEN voice, saving ----
FN(jfloatArray, penGet)(JNIEnv* env, jclass) { jfloatArray r = env->NewFloatArray(2 * PAGE); env->SetFloatArrayRegion(r, 0, PAGE, app()->pen.c[0]); env->SetFloatArrayRegion(r, PAGE, PAGE, app()->pen.c[1]); return r; }
FN(void, penLine)(JNIEnv*, jclass, jint box, jint x0, jfloat v0, jint x1, jfloat v1) { app()->pen.line(box, x0, v0, x1, v1); }
// ops: 0..7 a shape (sine, saw, square, triangle, noise, clear, pulse, organ), 10 smooth, 11 full, 12 invert, 13 reverse, 14 copy to the other
FN(void, penOp)(JNIEnv*, jclass, jint box, jint op) {
    auto& p = app()->pen;
    if (op >= 0 && op <= 7) p.shape(box, op, (uint32_t) std::rand()); else if (op == 10) p.smooth(box); else if (op == 11) p.normalise(box);
    else if (op == 12) p.invert(box); else if (op == 13) p.reverse(box); else if (op == 14) p.copy(box, 1 - box);
}
FN(void, penLive)(JNIEnv*, jclass) { auto* a = app(); if (!a->penLive) return; a->penSlot = penToSlot(a->kit, a->pen, a->penSlot); if (a->penSlot >= 0) a->loadSound(a->kit.userSlot(a->penSlot)); }
FN(jboolean, penIsLive)(JNIEnv*, jclass) { return app()->penLive; }
FN(void, setPenLive)(JNIEnv*, jclass, jboolean on) { app()->penLive = on; }
FN(jint, penSlotNow)(JNIEnv*, jclass) { return app()->penSlot; }
FN(jstring, penSaveAs)(JNIEnv* env, jclass) { std::string nm; const int slot = penSaveAs(app()->kit, app()->pen, nm); return env->NewStringUTF(slot < 0 ? "USER VOICES FULL" : (nm + " SAVED (VOICE " + std::to_string(slot + 1) + ")").c_str()); }
FN(jboolean, penFromKeyboard)(JNIEnv*, jclass) { auto* a = app(); const int v = a->sound.load(); const Sound* s = a->kit.bank.at(v); if (!s || isBlank(a->kit, v)) return false; return a->pen.fromSound(*s); }
FN(jstring, penExportWav)(JNIEnv* env, jclass, jint fd) {
    const auto b = voiceWav(app()->pen.voice("PEN"));
    const bool ok = writeAll(fd, b.data(), b.size()); ::close(fd);
    return env->NewStringUTF(ok ? "EXPORTED" : "CAN'T WRITE THE FILE");
}
FN(jstring, penState)(JNIEnv* env, jclass) { return env->NewStringUTF(app()->pen.save().c_str()); }
FN(void, setPenState)(JNIEnv* env, jclass, jstring t) { const char* c = env->GetStringUTFChars(t, nullptr); app()->pen.load(c); env->ReleaseStringUTFChars(t, c); }

// a song file's place in the song list if it's loaded (by title, as duplicates are told apart), else -1
FN(jint, songLoadedAs)(JNIEnv* env, jclass, jstring fileBase) {
    const char* c = env->GetStringUTFChars(fileBase, nullptr);
    std::string up = c; for (auto& ch : up) ch = (char) (ch == '_' || ch == '-' ? ' ' : std::toupper((unsigned char) ch));
    env->ReleaseStringUTFChars(fileBase, c);
    const std::string k = songKey(up); int pos = 0;
    for (int i = 0; i < app()->songs.size(); ++i) if (const SongEntry* e = app()->songs.at(i)) { if (songKey(e->song->name) == k) return pos; ++pos; }
    return -1;
}

FN(jboolean, userLoaded)(JNIEnv*, jclass, jint k) { return app()->kit.userLoaded(k); }
FN(jint, userSlot)(JNIEnv*, jclass, jint k) { return app()->kit.userSlot(k); }
FN(jint, firstFreeUser)(JNIEnv*, jclass) { return app()->kit.firstFree(); }
FN(jint, userCount)(JNIEnv*, jclass) { return app()->kit.loadedCount(); }
FN(void, clearUser)(JNIEnv*, jclass, jint k) {
    auto* a = app();
    a->kit.clearUser(k);
}

// state: parameters, Page R, user voices
FN(jbyteArray, saveState)(JNIEnv* env, jclass) {
    auto* a = app(); std::vector<uint8_t> v;
    put(v, (uint32_t) 0x434D4938);   // "CMI8": + the pads' kit, library voices kept by name; CMI7: + loop blend on (CMI6: + loop blend; CMI5: voice numbers without built-in voices)
    const Sound* cur = a->kit.bank.at(a->sound.load());
    char nm[16] {}; if (cur) std::strncpy(nm, cur->name.c_str(), 15); put(v, nm);
    for (int i = 0; i < NPARAM; ++i) put(v, a->param[i].load());
    put(v, a->engine.curPattern.load());
    put(v, a->engine.seq);
    {
        const int cur = a->songs.indexOf(a->engine.song.load());
        int32_t n = 0, sel = -1; for (int i = 0; i < a->songs.size(); ++i) if (a->songs.at(i)) { if (i == cur) sel = n; ++n; }
        put(v, n); put(v, sel);
        for (int i = 0; i < a->songs.size(); ++i) {
            const SongEntry* e = a->songs.at(i); if (!e) continue;
            char nm[32] {}; std::strncpy(nm, e->song->name.c_str(), 31); put(v, nm);
            for (int t = 0; t < SONG_TRK; ++t) { put(v, (int32_t) e->voice[t]); put(v, (uint8_t) e->mute[t]); }
            put(v, (int32_t) e->song->raw.size()); v.insert(v.end(), e->song->raw.begin(), e->song->raw.end());
        }
    }
    {   // the pads
        auto str = [&](const std::string& x) { put(v, (int32_t) x.size()); v.insert(v.end(), x.begin(), x.end()); };
        str(a->engine.padKit.save()); str(a->padKitName); put(v, (int32_t) a->padPreset); put(v, (uint8_t) a->engine.ch10Kit.load());
    }
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!a->kit.userLoaded(k)) continue;
        const Sound* s = a->kit.user(k);
        if (s->fromLibrary) {   // read again from the library folder: its name and disk only
            put(v, (int32_t) k); char un[16] {}; std::strncpy(un, s->name.c_str(), 15); put(v, un);
            put(v, s->rate); put(v, (int32_t) s->root); put(v, (int32_t) s->loopStart); put(v, (int32_t) s->loopEnd);
            put(v, (int32_t) s->mode); char cat[16] {}; std::strncpy(cat, s->category.c_str(), 15); put(v, cat);
            put(v, (int32_t) 0);
            continue;
        }
        put(v, (int32_t) k); char un[16] {}; std::strncpy(un, s->name.c_str(), 15); put(v, un);
        put(v, s->rate); put(v, (int32_t) s->root); put(v, (int32_t) s->loopStart); put(v, (int32_t) s->loopEnd);
        put(v, (int32_t) s->mode); char cat[16] {}; std::strncpy(cat, s->category.c_str(), 15); put(v, cat);
        put(v, (int32_t) s->ram.size());
        v.insert(v.end(), s->ram.begin(), s->ram.end());
    }
    jbyteArray out = env->NewByteArray((jsize) v.size());
    env->SetByteArrayRegion(out, 0, (jsize) v.size(), (const jbyte*) v.data());
    return out;
}
FN(void, loadState)(JNIEnv* env, jclass, jbyteArray data) {
    const jsize n = env->GetArrayLength(data);
    std::vector<uint8_t> v((size_t) n);
    env->GetByteArrayRegion(data, 0, n, (jbyte*) v.data());
    const uint8_t* p = v.data(); const uint8_t* e = p + v.size();
    auto* a = app();
    uint32_t magic = 0; if (!get(p, e, magic) || (magic < 0x434D4931 || magic > 0x434D4938)) return;
    const int nParams = magic >= 0x434D4937 ? NPARAM : magic >= 0x434D4936 ? 24 : 23;
    const bool old = magic < 0x434D4935;   // saved with the built-in voices counted first
    auto fix = [old](int i) { return old ? Library::fromOldIndex(i) : i; };
    const bool v2 = magic != 0x434D4931, v3 = magic >= 0x434D4933;
    const int songTracks = magic >= 0x434D4934 ? SONG_TRK : 8;
    char nm[16]; if (!get(p, e, nm)) return; nm[15] = 0;
    float prm[NPARAM]; for (int i = 0; i < NPARAM; ++i) prm[i] = kDefaults[i];
    for (int i = 0; i < nParams; ++i) if (!get(p, e, prm[i])) return;
    int pat = 0; get(p, e, pat);
    SeqData sd; if (!get(p, e, sd)) return;
    if (old) for (auto& tr : sd.trk) tr.sound = fix(tr.sound);
    for (int k2 = 0; k2 < Library::USER_SLOTS; ++k2) a->kit.clearUser(k2);
    a->engine.seq = sd; a->engine.curPattern = clampi(pat, 0, NPAT - 1);
    if (v3) {
        int32_t n = 0, sel = -1; if (!get(p, e, n) || !get(p, e, sel)) return;
        SongEntry* pick = nullptr;
        for (int32_t k = 0; k < n; ++k) {
            char nm[32]; int32_t voice[SONG_TRK] {}; uint8_t mute[SONG_TRK] {}; int32_t sz;
            if (!get(p, e, nm)) return;
            for (int t = 0; t < songTracks; ++t) if (!get(p, e, voice[t]) || !get(p, e, mute[t])) return;
            if (!get(p, e, sz) || sz <= 0 || p + sz > e) return;
            nm[31] = 0; MidiSong ms; std::string err;
            const bool ok = parseSmf(p, (size_t) sz, nm, ms, err); p += sz;
            if (!ok) continue;
            SongEntry* en = a->songs.at(a->songs.add(std::move(ms), a->kit.bank));
            for (int t = 0; t < songTracks; ++t) { en->voice[t] = voice[t] == GM_KIT ? GM_KIT : fix(voice[t]); en->mute[t] = mute[t] != 0; }
            if (k == sel) pick = en;
        }
        selectSong(a->engine, pick);
    }
    if (magic >= 0x434D4938) {   // the pads
        auto str = [&](std::string& x) { int32_t n = 0; if (!get(p, e, n) || n < 0 || p + n > e) return false; x.assign((const char*) p, (size_t) n); p += n; return true; };
        std::string kt, nmk; int32_t pre = -1; uint8_t c10 = 1;
        if (!str(kt) || !str(nmk) || !get(p, e, pre) || !get(p, e, c10)) return;
        DrumKit pk; pk.load(kt); applyPads(a->engine, pk); a->padKitName = nmk; a->padPreset = pre; a->engine.ch10Kit = c10 != 0;
    }
    int32_t k;
    while (get(p, e, k)) {
        Sound s; char un[16]; int32_t root, ls, le, sz, mode = 0; char cat[16] = "User";
        if (!get(p, e, un) || !get(p, e, s.rate) || !get(p, e, root) || !get(p, e, ls) || !get(p, e, le)) break;
        if (v2 && (!get(p, e, mode) || !get(p, e, cat))) break;
        if (!get(p, e, sz)) break;
        un[15] = 0; cat[15] = 0;
        if (sz == 0) { a->kit.keepPlace(k, un, cat); continue; }   // a library voice: back when the library is read
        if (sz < 0 || sz > RAM_SIZE || p + sz > e) break; s.name = un; s.category = cat; s.title = s.category == "User" ? std::string("Sampled: ") + un : std::string(un) + " from " + cat;
        s.mode = mode == 1 ? Mode::Waveform : Mode::Sample; s.root = root; s.loopStart = ls; s.loopEnd = le;
        s.ram.assign(p, p + sz); p += sz;
        a->kit.setUser(k, std::move(s));
    }
    int idx = -1;
    for (int i = 0; i < a->kit.bank.count.load(); ++i) if (const Sound* s = a->kit.bank.at(i)) if (s->name == nm) { idx = i; break; }
    if (idx >= 0) a->sound = idx;
    for (int i = 0; i < NPARAM; ++i) a->param[i] = prm[i];
    a->connectDefaults();
}
