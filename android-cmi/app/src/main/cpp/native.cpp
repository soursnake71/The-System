// Omega CMI on Android: the engine from plugins/cmi/core, an AAudio output stream, and the JNI the Java screen uses.
// Everything that changes the engine from Java goes through a queue the audio callback drains, so the engine is
// only ever touched by the audio thread (pattern bytes and track settings are plain data and written directly).
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <mutex>
#include <thread>
#include "Library.h"

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
    std::atomic<int> sound { 0 };
    std::atomic<float> param[NPARAM];
    // queue: many producers (UI, MIDI threads) under a mutex, one consumer (audio)
    std::array<Cmd, 2048> q; std::atomic<uint32_t> qw { 0 }, qr { 0 }; std::mutex qm;
    AAudioStream* stream = nullptr; std::mutex streamLock;
    std::vector<float> L, R;
    double rate = 48000;

    App() {
        for (int i = 0; i < NPARAM; ++i) param[i] = kDefaults[i];
        engine.bank = &kit.bank;
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
FN(void, midi)(JNIEnv* env, jclass, jbyteArray data, jint off, jint len) {
    jbyte* b = env->GetByteArrayElements(data, nullptr);
    // split a stream of messages (running status aware)
    static uint8_t status = 0;
    int i = off; const int end = off + len;
    while (i < end) {
        uint8_t s = (uint8_t) b[i];
        if (s >= 0xF8) { ++i; continue; }               // realtime
        if (s & 0x80) { status = s; ++i; } else if (!status) { ++i; continue; }
        const int hi = status & 0xF0;
        const int need = (hi == 0xC0 || hi == 0xD0) ? 1 : (status >= 0xF0 ? 0 : 2);
        if (status >= 0xF0) { status = 0; continue; }
        if (i + need > end) break;
        Cmd c {}; c.type = C_MIDI; c.m[0] = status; c.len = 1 + need;
        for (int k = 0; k < need; ++k) c.m[1 + k] = (uint8_t) b[i + k];
        i += need;
        if (hi == 0xC0) { app()->loadSound(c.m[1]); continue; }   // program change: a library voice
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

// Page 8: mono float samples from a decoded file
FN(jstring, loadSample)(JNIEnv* env, jclass, jfloatArray data, jint rate, jstring name, jint slot, jint root, jboolean loop) {
    const jsize n = env->GetArrayLength(data);
    if (n <= 0 || rate <= 0) return env->NewStringUTF("EMPTY FILE");
    std::vector<float> x((size_t) n);
    env->GetFloatArrayRegion(data, 0, n, x.data());
    const char* nm = env->GetStringUTFChars(name, nullptr);
    Sound s = sampleToCmi(x, rate, nm, root, loop);
    env->ReleaseStringUTFChars(name, nm);
    app()->kit.setUser(slot, std::move(s));
    app()->loadSound(app()->kit.userSlot(slot));
    return env->NewStringUTF("");
}
FN(jboolean, userLoaded)(JNIEnv*, jclass, jint k) { return app()->kit.userLoaded(k); }
FN(jint, userSlot)(JNIEnv*, jclass, jint k) { return app()->kit.userSlot(k); }

// state: parameters, Page R, user voices
FN(jbyteArray, saveState)(JNIEnv* env, jclass) {
    auto* a = app(); std::vector<uint8_t> v;
    put(v, (uint32_t) 0x434D4931);   // "CMI1"
    const Sound* cur = a->kit.bank.at(a->sound.load());
    char nm[16] {}; if (cur) std::strncpy(nm, cur->name.c_str(), 15); put(v, nm);
    for (int i = 0; i < NPARAM; ++i) put(v, a->param[i].load());
    put(v, a->engine.curPattern.load());
    put(v, a->engine.seq);
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!a->kit.userLoaded(k)) continue;
        const Sound* s = a->kit.user(k);
        put(v, (int32_t) k); char un[16] {}; std::strncpy(un, s->name.c_str(), 15); put(v, un);
        put(v, s->rate); put(v, (int32_t) s->root); put(v, (int32_t) s->loopStart); put(v, (int32_t) s->loopEnd); put(v, (int32_t) s->ram.size());
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
    uint32_t magic = 0; if (!get(p, e, magic) || magic != 0x434D4931) return;
    char nm[16]; if (!get(p, e, nm)) return; nm[15] = 0;
    float prm[NPARAM]; for (auto& x : prm) if (!get(p, e, x)) return;
    int pat = 0; get(p, e, pat);
    SeqData sd; if (!get(p, e, sd)) return;
    a->engine.seq = sd; a->engine.curPattern = clampi(pat, 0, NPAT - 1);
    int32_t k;
    while (get(p, e, k)) {
        Sound s; char un[16]; int32_t root, ls, le, sz;
        if (!get(p, e, un) || !get(p, e, s.rate) || !get(p, e, root) || !get(p, e, ls) || !get(p, e, le) || !get(p, e, sz)) break;
        if (sz <= 0 || sz > RAM_SIZE || p + sz > e) break;
        un[15] = 0; s.name = un; s.title = std::string("Sampled: ") + un; s.category = "User"; s.root = root; s.loopStart = ls; s.loopEnd = le;
        s.ram.assign(p, p + sz); p += sz;
        a->kit.setUser(k, std::move(s));
    }
    int idx = -1;
    for (int i = 0; i < a->kit.bank.count.load(); ++i) if (const Sound* s = a->kit.bank.at(i)) if (s->name == nm) { idx = i; break; }
    if (idx >= 0) a->sound = idx;
    for (int i = 0; i < NPARAM; ++i) a->param[i] = prm[i];
}
