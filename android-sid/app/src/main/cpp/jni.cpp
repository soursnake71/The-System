// Omega SID 64 on Android: the plugin's engine (SidSynth.h) and C64 screen (ScreenUI.h) behind JNI, played
// through an AAudio low-latency stream. The Java side draws the monitor and the keyboard and forwards touches,
// on-screen keys and MIDI.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <memory>
#include <mutex>
#include <thread>
#include <time.h>
#include <array>
#include "ScreenUI.h"

using namespace sid;
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "OmegaSid", __VA_ARGS__)

namespace {

struct AppHost : ScreenUI::Host {
    Synth s;
    Synth& synth() override { return s; }
    float get(int id) override { return s.get(id); }
    void set(int id, float v) override { s.set(id, v); }
    void loadPreset(int i) override { s.applyPreset(i); }
    void initPatch() override { s.initPatch(); }
    void randomize() override { s.randomize((uint32_t) std::rand() | 1u); }
    bool isPlugin() const override { return false; }
    // the file picker and the microphone live on the Java side: ask it (SidView polls these each frame)
    std::atomic<int> request { 0 };            // 1 open a sample, 2 start recording, 3 stop recording, 4 save a .mid, 5 open a .mid
    std::atomic<bool> rec { false }; std::atomic<float> recSec { 0 };
    void loadSampleFile() override { request = 1; }
    void setRecording(bool on) override { request = on ? 2 : 3; }
    std::string midiToSave, midiName, saveMime = "audio/midi";   // the file to save (a .mid, or an exported .wav / .mp3)
    void saveMidiFile(const std::string& bytes, const std::string& name) override { midiToSave = bytes; midiName = name; saveMime = "audio/midi"; request = 4; }
    void saveAudioFile(const std::string& bytes, const std::string& name, const std::string& mime) override { midiToSave = bytes; midiName = name; saveMime = mime; request = 4; }
    void loadMidiFile() override { request = 5; }
    // songs: the folder and its files come from the Java side (Songs.java); opening one is a request it answers
    std::string songDir; std::vector<std::string> songNames; std::atomic<int> songIdx { -1 };
    void chooseSongFolder() override { request = 6; }
    void rescanSongFolder() override { request = 7; }
    std::string songFolderName() override { return songDir; }
    std::vector<std::string> songFiles() override { return songNames; }
    void openSongFile(int i) override { if (i >= 0) { songIdx = i; request = 8; } else request = 9; }
    bool recording() override { return rec.load(); }
    float recordedSeconds() override { return recSec.load(); }
};

std::unique_ptr<AppHost> host;
std::unique_ptr<ScreenUI> ui;
AAudioStream* stream = nullptr;
std::mutex streamLock;
std::vector<float> bufL, bufR;
std::atomic<bool> wanted { false };
double preparedRate = 0;
// MIDI out (the sequencer's notes and clock): the audio thread stamps each message with the time it should be heard
// (CLOCK_MONOTONIC, which is Java's System.nanoTime) and the UI thread hands them to the devices
struct OutMsg { int64_t when; uint8_t b[3]; int n; };
std::array<OutMsg, 1024> outRing; std::atomic<int> outW { 0 }, outR { 0 };
int64_t nowNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t) t.tv_sec * 1000000000LL + t.tv_nsec; }

void openStream();

aaudio_data_callback_result_t onAudio(AAudioStream*, void*, void* data, int32_t frames) {
    float* out = static_cast<float*>(data);
    int done = 0;
    const double sr = preparedRate > 0 ? preparedRate : 48000;
    const int64_t base = nowNs() + (int64_t) (AAudioStream_getBufferSizeInFrames(stream) / sr * 1e9);   // when this buffer is heard
    while (done < frames) {
        const int n = std::min(frames - done, (int) bufL.size());
        host->s.render(bufL.data(), bufR.data(), n);
        for (int i = 0; i < host->s.midiOutCount; ++i) {
            const auto& e = host->s.midiOutBuf[(size_t) i];
            const int w = outW.load(std::memory_order_relaxed);
            if (w - outR.load(std::memory_order_acquire) >= (int) outRing.size()) break;
            outRing[(size_t) (w & 1023)] = { base + (int64_t) ((done + e.offset) / sr * 1e9), { e.b[0], e.b[1], e.b[2] }, e.n };
            outW.store(w + 1, std::memory_order_release);
        }
        for (int i = 0; i < n; ++i) { out[2 * (done + i)] = bufL[(size_t) i]; out[2 * (done + i) + 1] = bufR[(size_t) i]; }
        done += n;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
// the device went away (headphones, USB): reopen on another thread, as AAudio asks
void onError(AAudioStream*, void*, aaudio_result_t err) {
    LOG("stream error %d", err);
    if (err == AAUDIO_ERROR_DISCONNECTED) std::thread([] {
        std::lock_guard<std::mutex> l(streamLock);
        if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); stream = nullptr; }
        if (wanted) openStream();
    }).detach();
}
void openStream() {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, onAudio, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, onError, nullptr);
    if (AAudioStreamBuilder_openStream(b, &stream) != AAUDIO_OK) { AAudioStreamBuilder_delete(b); stream = nullptr; return; }
    AAudioStreamBuilder_delete(b);
    const double sr = AAudioStream_getSampleRate(stream);
    if (sr != preparedRate) { host->s.prepare(sr); preparedRate = sr; }
    // two bursts of buffering: low latency without crackles on most phones
    AAudioStream_setBufferSizeInFrames(stream, AAudioStream_getFramesPerBurst(stream) * 2);
    AAudioStream_requestStart(stream);
    LOG("audio %d Hz, burst %d", (int) sr, AAudioStream_getFramesPerBurst(stream));
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_init(JNIEnv* env, jclass, jstring state) {
    if (host) return;
    host = std::make_unique<AppHost>();
    bufL.assign(1024, 0.f); bufR.assign(1024, 0.f);
    host->s.applyPreset(0);
    host->s.set(P::quality, 0);   // 2x oversampling: plenty on a phone, and it saves battery
    if (state) {
        const char* c = env->GetStringUTFChars(state, nullptr);
        if (c && *c) host->s.loadState(c);
        env->ReleaseStringUTFChars(state, c);
    }
    ui = std::make_unique<ScreenUI>(*host);
    ui->touchMode = true;
    ui->showPreset(host->s.program.load());
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_start(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> l(streamLock);
    wanted = true;
    if (!stream) openStream();
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_stop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> l(streamLock);
    wanted = false;
    if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); stream = nullptr; }
}
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_sid_Native_saveState(JNIEnv* env, jclass) {
    return env->NewStringUTF(host ? host->s.saveState().c_str() : "");
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_render(JNIEnv* env, jclass, jintArray px) {
    if (!ui) return;
    const int prog = host->s.pendingProgram.exchange(-1);
    { int id; float v; while (host->s.popParamChange(id, v)) {} }   // (no host copy to update here: the engine holds them)
    if (prog >= 0) { host->s.applyPreset(prog); ui->showPreset(prog); }
    host->s.pollSample();
    if (env->GetArrayLength(px) < ScreenUI::W * ui->height()) return;   // a too-small array: draw nothing rather than overrun
    jint* p = env->GetIntArrayElements(px, nullptr);
    ui->render(reinterpret_cast<uint32_t*>(p));
    env->ReleaseIntArrayElements(px, p, 0);
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_touch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    if (!ui) return;
    if (action == 0) ui->down(x, y); else if (action == 1) ui->move(x, y); else ui->up(x, y);
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_wheel(JNIEnv*, jclass, jfloat x, jfloat y, jfloat steps) { if (ui) ui->wheel(x, y, steps); }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_noteOn(JNIEnv*, jclass, jint n, jfloat v) { if (host) host->s.post(Synth::EV_ON, n, v); }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_noteOff(JNIEnv*, jclass, jint n) { if (host) host->s.post(Synth::EV_OFF, n, 0); }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_seqToggle(JNIEnv*, jclass) {
    if (host) host->s.post(host->s.seqPlay.load() ? Synth::EV_SEQ_STOP : Synth::EV_SEQ_PLAY, 0, 0);
}
JNIEXPORT jboolean JNICALL Java_com_thesnakedevice_sid_Native_seqRunning(JNIEnv*, jclass) { return host && host->s.seqPlay.load(); }
// how many text rows the phone's screen shows (25 to 80): the pixel array passed to render holds 320 x rows x 8
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_setRows(JNIEnv*, jclass, jint rows) { if (ui) ui->setRows(rows); }
// the big controls beside the screen: the page buttons and the panel for the selected setting
// "page|selected id|name|value|kind|0..1 position|preset name|preset number"
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_sid_Native_editState(JNIEnv* env, jclass) {
    if (!ui || !host) return env->NewStringUTF("");
    const int id = ui->selected;
    std::string st = std::to_string(ui->page) + "|" + std::to_string(id) + "|";
    if (id >= 0) {
        const ParamInfo& p = params()[id];
        const float v = host->get(id);
        st += std::string(p.section) + " " + p.name + "|" + formatValue(id, v) + "|" + std::to_string((int) p.kind) + "|" + std::to_string(toNorm(id, v));
    } else st += "|||0";
    st += "|" + host->s.presetName + "|" + std::to_string(host->s.program.load() + 1);
    return env->NewStringUTF(st.c_str());
}
// 0 set the selected setting to v (0..1), 1 step it by v, 2 reset it, 3 next / previous preset by v, 4 go to page v,
// 5 next / previous page by v
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_editAction(JNIEnv*, jclass, jint action, jfloat v) {
    if (!ui) return;
    switch (action) {
        case 0: ui->setSelectedNorm(v); break;
        case 1: ui->stepSelected(v < 0 ? -1 : 1); break;
        case 2: ui->resetSelected(); break;
        case 3: ui->stepPreset(v < 0 ? -1 : 1); break;
        case 4: ui->setPage((int) v); break;
        case 5: ui->stepPage(v < 0 ? -1 : 1); break;
        default: break;
    }
}
JNIEXPORT jint JNICALL Java_com_thesnakedevice_sid_Native_pollRequest(JNIEnv*, jclass) { return host ? host->request.exchange(0) : 0; }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_setRecordingState(JNIEnv*, jclass, jboolean on, jfloat secs) {
    if (host) { host->rec = on; host->recSec = secs; }
}
// a sound sampled or decoded on the Java side (UI thread): it becomes the source and is converted for the C64
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_setSample(JNIEnv* env, jclass, jfloatArray data, jint rate, jstring name) {
    if (!host || !data) return;
    const jsize n = env->GetArrayLength(data);
    std::vector<float> v((size_t) n);
    env->GetFloatArrayRegion(data, 0, n, v.data());
    const char* c = env->GetStringUTFChars(name, nullptr);
    std::string nm = c ? c : "SAMPLE"; env->ReleaseStringUTFChars(name, c);
    host->s.setSource(std::move(v), rate, nm);
    if (host->s.get(P::dmode) < .5f) host->s.set(P::dmode, DIGI_VOL4);
    host->s.pollSample();
    if (ui) ui->message("SAMPLED: " + nm);
}
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_panic(JNIEnv*, jclass) { if (host) host->s.post(Synth::EV_PANIC, 0, 0); }
// raw MIDI bytes from a device (its own thread): into the engine's parser, which the audio thread runs
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_midi(JNIEnv* env, jclass, jbyteArray data, jint off, jint len) {
    if (!host || len <= 0) return;
    std::vector<uint8_t> b((size_t) len);
    env->GetByteArrayRegion(data, off, len, reinterpret_cast<jbyte*>(b.data()));
    host->s.postMidi(b.data(), len);
}
// MIDI out for the UI thread to send: up to max messages as (time ns, bytes); returns how many
JNIEXPORT jint JNICALL Java_com_thesnakedevice_sid_Native_midiOut(JNIEnv* env, jclass, jlongArray times, jbyteArray bytes, jintArray lens) {
    const int max = env->GetArrayLength(times);
    std::vector<jlong> t((size_t) max); std::vector<jbyte> b((size_t) max * 3); std::vector<jint> l((size_t) max);
    int n = 0, r = outR.load(std::memory_order_relaxed); const int w = outW.load(std::memory_order_acquire);
    while (r != w && n < max) {
        const OutMsg& m = outRing[(size_t) (r & 1023)];
        t[(size_t) n] = m.when; for (int k = 0; k < 3; ++k) b[(size_t) n * 3 + (size_t) k] = (jbyte) m.b[k]; l[(size_t) n] = m.n; ++n; ++r;
    }
    outR.store(r, std::memory_order_release);
    if (n) { env->SetLongArrayRegion(times, 0, n, t.data()); env->SetByteArrayRegion(bytes, 0, n * 3, b.data()); env->SetIntArrayRegion(lens, 0, n, l.data()); }
    return n;
}
// MIDI files: the pattern the screen asked to save, and a file the user picked
JNIEXPORT jbyteArray JNICALL Java_com_thesnakedevice_sid_Native_midiFileToSave(JNIEnv* env, jclass) {
    const std::string& m = host ? host->midiToSave : std::string();
    jbyteArray a = env->NewByteArray((jsize) m.size());
    env->SetByteArrayRegion(a, 0, (jsize) m.size(), reinterpret_cast<const jbyte*>(m.data()));
    return a;
}
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_sid_Native_midiFileName(JNIEnv* env, jclass) { return env->NewStringUTF(host ? host->midiName.c_str() : "pattern.mid"); }
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_sid_Native_saveMime(JNIEnv* env, jclass) { return env->NewStringUTF(host ? host->saveMime.c_str() : "audio/midi"); }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_midiFileLoaded(JNIEnv* env, jclass, jbyteArray data) {
    if (!host || !data) return;
    const jsize n = env->GetArrayLength(data);
    std::string b((size_t) n, '\0'); env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(&b[0]));
    if (host->midiFileLoaded) host->midiFileLoaded(b);
}

// the song folder's listing (UI thread), the file the screen asked for, and a song read from a file
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_setSongFiles(JNIEnv* env, jclass, jstring folder, jobjectArray names) {
    if (!host) return;
    auto str = [&](jstring j) { std::string r; if (!j) return r; const char* c = env->GetStringUTFChars(j, nullptr); if (c) r = c; env->ReleaseStringUTFChars(j, c); return r; };
    host->songDir = str(folder);
    host->songNames.clear();
    const jsize n = names ? env->GetArrayLength(names) : 0;
    for (jsize i = 0; i < n; ++i) { auto j = (jstring) env->GetObjectArrayElement(names, i); host->songNames.push_back(str(j)); env->DeleteLocalRef(j); }
}
JNIEXPORT jint JNICALL Java_com_thesnakedevice_sid_Native_songIndex(JNIEnv*, jclass) { return host ? host->songIdx.load() : -1; }
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_songFileLoaded(JNIEnv* env, jclass, jbyteArray data, jstring name) {
    if (!host || !data) return;
    const jsize n = env->GetArrayLength(data);
    std::string b((size_t) n, '\0'); env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(&b[0]));
    std::string nm = "SONG";
    if (name) { const char* c = env->GetStringUTFChars(name, nullptr); if (c) nm = c; env->ReleaseStringUTFChars(name, c); }
    if (host->songFileLoaded) host->songFileLoaded(b, nm);
}

}  // extern "C"
