// Omega SID 64 on Android: the plugin's engine (SidSynth.h) and C64 screen (ScreenUI.h) behind JNI, played
// through an AAudio low-latency stream. The Java side draws the monitor and the keyboard and forwards touches,
// on-screen keys and MIDI.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <memory>
#include <mutex>
#include <thread>
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
};

std::unique_ptr<AppHost> host;
std::unique_ptr<ScreenUI> ui;
AAudioStream* stream = nullptr;
std::mutex streamLock;
std::vector<float> bufL, bufR;
std::atomic<bool> wanted { false };
double preparedRate = 0;
std::atomic<int> pendingProgram { -1 };   // program change from MIDI, applied on the UI thread

void openStream();

aaudio_data_callback_result_t onAudio(AAudioStream*, void*, void* data, int32_t frames) {
    float* out = static_cast<float*>(data);
    int done = 0;
    while (done < frames) {
        const int n = std::min(frames - done, (int) bufL.size());
        host->s.render(bufL.data(), bufR.data(), n);
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
    const int prog = pendingProgram.exchange(-1);
    if (prog >= 0) { host->s.applyPreset(prog); ui->showPreset(prog); }
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
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_panic(JNIEnv*, jclass) { if (host) host->s.post(Synth::EV_PANIC, 0, 0); }
// raw MIDI bytes (USB / Bluetooth devices): notes, bend, mod wheel, sustain, program change
JNIEXPORT void JNICALL Java_com_thesnakedevice_sid_Native_midi(JNIEnv* env, jclass, jbyteArray data, jint off, jint len) {
    if (!host) return;
    jbyte* b = env->GetByteArrayElements(data, nullptr);
    static int status = 0;   // running status
    for (int i = off; i < off + len;) {
        int s = (uint8_t) b[i];
        if (s >= 0xF8) { ++i; continue; }          // clock / realtime
        if (s & 0x80) { status = s; ++i; } else s = status;
        const int type = s & 0xF0;
        const int need = (type == 0xC0 || type == 0xD0) ? 1 : (s >= 0xF0 ? 0 : 2);
        if (s >= 0xF0) { while (i < off + len && !((uint8_t) b[i] & 0x80)) ++i; continue; }   // sysex etc.
        if (i + need > off + len) break;
        const int d1 = b[i] & 127, d2 = need > 1 ? b[i + 1] & 127 : 0; i += need;
        Synth& sy = host->s;
        switch (type) {
            case 0x90: if (d2) sy.post(Synth::EV_ON, d1, d2 / 127.f); else sy.post(Synth::EV_OFF, d1, 0); break;
            case 0x80: sy.post(Synth::EV_OFF, d1, 0); break;
            case 0xE0: sy.post(Synth::EV_BEND, 0, ((d2 << 7 | d1) - 8192) / 8192.f); break;
            case 0xB0:
                if (d1 == 1) sy.post(Synth::EV_WHEEL, 0, d2 / 127.f);
                else if (d1 == 123 || d1 == 120) sy.post(Synth::EV_ALLOFF, 0, 0);
                else if (d1 == 74) sy.set(P::cut, d2 / 127.f * 2047.f);
                else if (d1 == 71) sy.set(P::res, std::round(d2 / 127.f * 15.f));
                break;
            case 0xC0: if (d1 < presetCount()) pendingProgram = d1; break;
            default: break;
        }
    }
    env->ReleaseByteArrayElements(data, b, JNI_ABORT);
}

}  // extern "C"
