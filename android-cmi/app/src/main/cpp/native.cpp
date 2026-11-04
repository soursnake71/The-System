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

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "OmegaCMI", __VA_ARGS__)

using namespace cmi;

namespace {

enum CmdType { C_MIDI, C_PREVIEW, C_ALLOFF };
struct Cmd { int type; uint8_t m[3]; int len; int a; };

constexpr int NPARAM = 23;
const float kDefaults[NPARAM] = { 0.85f, 255, 0, 10, 1, 0, 5.5f, 0.25f, 0, 0.6f, 0.3f, 0, 0, 1, 0.03f, 8, 2, 1, 1, 0.3f, 120, 0, 0 };

struct App {
    Kit kit;
    Engine engine;
    SongList songs;
    std::atomic<int> sound { 0 };
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
        engine.kitVoice = kitLookup(kit.bank);
        Library::factorySeq(engine.seq, Library::get());
        engine.extSync = false;
        loadSound(Library::get().find("ORCHHIT"));
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
        sound = i;
    }
    Controls controls() const {
        Controls c;
        c.level = param[0]; c.filter = param[1]; c.attack = param[2]; c.damp = param[3]; c.sustain = param[4]; c.vibDepth = param[5]; c.vibRate = param[6];
        c.vibDelay = param[7]; c.porta = param[8]; c.velVol = param[9]; c.velFilt = param[10]; c.transpose = param[11]; c.fine = param[12]; c.loop = param[13]; c.segTime = param[14];
        return c;
    }
    void render(float* out, int n) {
        uint32_t r = qr.load(std::memory_order_relaxed);
        const uint32_t w = qw.load(std::memory_order_acquire);
        engine.setKeyboard(sound.load(), controls());
        engine.setPoly((int) param[15].load()); engine.setBendRange(param[16]);
        engine.setOutput(param[17], param[18], param[19]);
        engine.tempo = param[20].load(); engine.seq.swing = param[21]; engine.songMode = param[22] > 0.5f;
        while (r != w) {
            const Cmd& c = q[r % q.size()];
            if (c.type == C_MIDI) engine.midi(c.m, c.len);
            else if (c.type == C_PREVIEW) engine.trackNote(c.a, 0, 0.9f);
            else if (c.type == C_ALLOFF) engine.allOff(true);
            ++r;
        }
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
                             { 88, 20, 40, 240, false, false }, { 89, 21, 0, 0.5f, false, false }, { 90, 22, 0, 1, true, true } };
    for (const auto& m : map) if (m.cc == cc) {
        float x = m.lo + v / 127.f * (m.hi - m.lo);
        if (m.toggle) x = v >= 64 ? 1.f : 0.f; else if (m.integer) x = std::round(x);
        app()->param[m.param] = x;
        return true;
    }
    if (cc == 104) { app()->engine.songGain = v / 127.0; return true; }
    return false;
}
// program n: the n-th voice in the list (the factory voices, then the ones you loaded)
static void programChange(int n) {
    int k = 0;
    for (int i = 0; i < app()->kit.bank.count.load(); ++i) {
        const bool user = i >= Library::get().builtIn;
        if (user && !app()->kit.userLoaded(i - Library::get().builtIn)) continue;
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
FN(jint, builtInCount)(JNIEnv*, jclass) { return Library::get().builtIn; }
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
    selectSong(app()->engine, app()->songs.at(i));
    return env->NewStringUTF("");
}
FN(jint, songCount)(JNIEnv*, jclass) { return app()->songs.count(); }
FN(jstring, songName)(JNIEnv* env, jclass, jint k) { const SongEntry* e = app()->songs.at(app()->songs.nth(k)); return env->NewStringUTF(e ? e->song->name.c_str() : ""); }
FN(void, selectSongAt)(JNIEnv*, jclass, jint k) { selectSong(app()->engine, k < 0 ? nullptr : app()->songs.at(app()->songs.nth(k))); }
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
    eng->setOutput(app()->param[17], app()->param[18], app()->param[19]); eng->extSync = false; eng->songGain = app()->engine.songGain.load();
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
        if (ended) { quiet = pk < 0.0005f ? quiet + blk : 0; if (quiet > fs * 0.3 || eng->activeVoices() == 0) break; }
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
FN(jboolean, undoSongEdit)(JNIEnv*, jclass) { return undoSong(app()->engine, app()->songs.at(curSongIdx())); }
FN(void, newSong)(JNIEnv*, jclass) { auto& sl = app()->songs; selectSong(app()->engine, sl.at(sl.add(emptySong("NEW SONG " + std::to_string(sl.count() + 1)), app()->kit.bank))); }
FN(jstring, songWhy)(JNIEnv* env, jclass, jint t) { const SongEntry* e = app()->songs.at(curSongIdx()); return env->NewStringUTF(e && t >= 0 && t < SONG_TRK ? e->why[t].c_str() : ""); }

// ---- mixer: strips 0 keyboard, 1..8 Page R tracks, 9..24 song tracks ----
FN(jfloat, mixGet)(JNIEnv*, jclass, jint i, jint what) {
    auto& e = app()->engine; if (i < 0 || i >= Engine::STRIPS) return 0;
    return what == 0 ? e.fader[(size_t) i].load() : what == 1 ? e.panOver[(size_t) i].load() : what == 2 ? (float) e.mute[(size_t) i].load() : what == 3 ? (float) e.solo[(size_t) i].load() : e.meter[(size_t) i].load();
}
FN(void, mixSet)(JNIEnv*, jclass, jint i, jint what, jfloat v) {
    auto& e = app()->engine; if (i < 0 || i >= Engine::STRIPS) return;
    if (what == 0) e.fader[(size_t) i] = clampd(v, 0, 1.5); else if (what == 1) e.panOver[(size_t) i] = v; else if (what == 2) e.mute[(size_t) i] = v > 0.5f; else if (what == 3) e.solo[(size_t) i] = v > 0.5f;
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

FN(jboolean, userLoaded)(JNIEnv*, jclass, jint k) { return app()->kit.userLoaded(k); }
FN(jint, userSlot)(JNIEnv*, jclass, jint k) { return app()->kit.userSlot(k); }
FN(jint, firstFreeUser)(JNIEnv*, jclass) { return app()->kit.firstFree(); }
FN(jint, userCount)(JNIEnv*, jclass) { return app()->kit.loadedCount(); }
FN(void, clearUser)(JNIEnv*, jclass, jint k) {
    auto* a = app();
    if (a->sound.load() == a->kit.userSlot(k)) a->loadSound(Library::get().find("ORCHHIT"));
    a->kit.clearUser(k);
}

// state: parameters, Page R, user voices
FN(jbyteArray, saveState)(JNIEnv* env, jclass) {
    auto* a = app(); std::vector<uint8_t> v;
    put(v, (uint32_t) 0x434D4934);   // "CMI4"
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
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!a->kit.userLoaded(k)) continue;
        const Sound* s = a->kit.user(k);
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
    uint32_t magic = 0; if (!get(p, e, magic) || (magic < 0x434D4931 || magic > 0x434D4934)) return;
    const bool v2 = magic != 0x434D4931, v3 = magic >= 0x434D4933;
    const int songTracks = magic >= 0x434D4934 ? SONG_TRK : 8;
    char nm[16]; if (!get(p, e, nm)) return; nm[15] = 0;
    float prm[NPARAM]; for (auto& x : prm) if (!get(p, e, x)) return;
    int pat = 0; get(p, e, pat);
    SeqData sd; if (!get(p, e, sd)) return;
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
            for (int t = 0; t < songTracks; ++t) { en->voice[t] = voice[t]; en->mute[t] = mute[t] != 0; }
            if (k == sel) pick = en;
        }
        selectSong(a->engine, pick);
    }
    int32_t k;
    while (get(p, e, k)) {
        Sound s; char un[16]; int32_t root, ls, le, sz, mode = 0; char cat[16] = "User";
        if (!get(p, e, un) || !get(p, e, s.rate) || !get(p, e, root) || !get(p, e, ls) || !get(p, e, le)) break;
        if (v2 && (!get(p, e, mode) || !get(p, e, cat))) break;
        if (!get(p, e, sz)) break;
        if (sz <= 0 || sz > RAM_SIZE || p + sz > e) break;
        un[15] = 0; cat[15] = 0; s.name = un; s.category = cat; s.title = s.category == "User" ? std::string("Sampled: ") + un : std::string(un) + " from " + cat;
        s.mode = mode == 1 ? Mode::Waveform : Mode::Sample; s.root = root; s.loopStart = ls; s.loopEnd = le;
        s.ram.assign(p, p + sz); p += sz;
        a->kit.setUser(k, std::move(s));
    }
    int idx = -1;
    for (int i = 0; i < a->kit.bank.count.load(); ++i) if (const Sound* s = a->kit.bank.at(i)) if (s->name == nm) { idx = i; break; }
    if (idx >= 0) a->sound = idx;
    for (int i = 0; i < NPARAM; ++i) a->param[i] = prm[i];
}
