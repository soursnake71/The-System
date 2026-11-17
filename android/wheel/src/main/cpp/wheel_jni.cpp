// Omega Wheel of Steel on Android: the JNI side of com.thesnakedevice.wheel.Native and the AAudio output stream.
// The engine itself is wheel_host.cpp (plugins/wheel/Engine.cpp + Analysis.cpp + SeratoTags.cpp underneath).
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "SeratoTags.h"
#include "wheel_host.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "wheel", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "wheel", __VA_ARGS__)

using namespace wheel;

namespace {

// one engine per process
Host& host()
{
    static Host* h = new Host();   // never destroyed: the audio callback may outlive static destruction
    return *h;
}

//==============================================================================================================
// AAudio

struct Audio {
    std::mutex mx;                       // start / stop / restart / offline pumping (never taken by the callback)
    AAudioStream* stream = nullptr;
    std::atomic<bool> running { false }; // a stream is open and started (the callback renders)
    bool wanted = false;                 // start() called and no stop() since
    int channels = 2;
    aaudio_format_t format = AAUDIO_FORMAT_PCM_FLOAT;
    std::vector<float> conv;             // I16 fallback
    std::atomic<double> lastLatency { 0 };
    std::atomic<int> restarts { 0 };
};
Audio& audio() { static Audio* a = new Audio(); return *a; }

aaudio_data_callback_result_t dataCallback(AAudioStream*, void*, void* data, int32_t frames)
{
    Audio& a = audio();
    if (frames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (a.format == AAUDIO_FORMAT_PCM_FLOAT) {
        host().renderInterleaved((float*) data, frames, a.channels);
    } else {
        // the conversion buffer is sized at open for 2 x the largest burst; render in pieces that fit
        auto* out = (int16_t*) data;
        const int per = std::max(1, (int) a.conv.size() / std::max(1, a.channels));
        for (int done = 0; done < frames;) {
            const int m = std::min(per, frames - done);
            host().renderInterleaved(a.conv.data(), m, a.channels);
            for (int i = 0; i < m * a.channels; ++i)
                out[(size_t) done * (size_t) a.channels + (size_t) i] = (int16_t) std::lround(std::max(-1.f, std::min(1.f, a.conv[(size_t) i])) * 32767.f);
            done += m;
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void errorCallback(AAudioStream*, void*, aaudio_result_t error);

void closeLocked(Audio& a)
{
    if (a.stream == nullptr) return;
    a.running.store(false);
    AAudioStream_requestStop(a.stream);
    AAudioStream_close(a.stream);   // returns once the callback has finished
    a.stream = nullptr;
}

AAudioStream* openStream(aaudio_sharing_mode_t sharing)
{
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || b == nullptr) return nullptr;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, sharing);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, dataCallback, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, errorCallback, nullptr);
    AAudioStream* s = nullptr;
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { LOGW("openStream (%s): %s", sharing == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared", AAudio_convertResultToText(r)); return nullptr; }
    return s;
}

bool openLocked(Audio& a)
{
    AAudioStream* s = openStream(AAUDIO_SHARING_MODE_EXCLUSIVE);
    if (s == nullptr) s = openStream(AAUDIO_SHARING_MODE_SHARED);
    if (s == nullptr) return false;
    const int32_t rate = AAudioStream_getSampleRate(s), burst = AAudioStream_getFramesPerBurst(s);
    a.channels = std::max(1, (int) AAudioStream_getChannelCount(s));
    a.format = AAudioStream_getFormat(s);
    if (a.format != AAUDIO_FORMAT_PCM_FLOAT && a.format != AAUDIO_FORMAT_PCM_I16) {
        LOGW("unexpected stream format %d", (int) a.format);
        AAudioStream_close(s);
        return false;
    }
    a.conv.assign((size_t) std::max(256, 2 * std::max(1, (int) burst)) * (size_t) a.channels, 0.f);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(s, 2 * burst);
    if (rate > 0 && std::abs(rate - host().sampleRate()) > 0.5) host().prepare(rate);   // no callback runs yet
    host().pumpOffline();
    a.stream = s;
    a.running.store(true);   // from here on the callback owns the engine
    const aaudio_result_t r = AAudioStream_requestStart(s);
    if (r != AAUDIO_OK) {
        LOGW("requestStart: %s", AAudio_convertResultToText(r));
        closeLocked(a);
        return false;
    }
    LOGI("AAudio: %d Hz, burst %d, buffer %d, %s, %d ch, %s", rate, burst, AAudioStream_getBufferSizeInFrames(s),
         AAudioStream_getSharingMode(s) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared", a.channels,
         a.format == AAUDIO_FORMAT_PCM_FLOAT ? "float" : "i16");
    return true;
}

// headphones / USB unplugged (or the route changed): reopen on the default device, never from the callback thread
void restart()
{
    Audio& a = audio();
    std::lock_guard<std::mutex> l(a.mx);
    if (! a.wanted) return;
    closeLocked(a);
    for (int tries = 0; tries < 5 && ! openLocked(a); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    a.restarts.fetch_add(1);
}

void errorCallback(AAudioStream* s, void*, aaudio_result_t error)
{
    LOGW("AAudio error: %s", AAudio_convertResultToText(error));
    (void) s;
    if (error == AAUDIO_ERROR_DISCONNECTED) std::thread(restart).detach();
}

// commands queue up while no stream renders: run them here so state still follows (and the queue never fills up)
void pumpIfIdle()
{
    Audio& a = audio();
    if (a.running.load()) return;
    std::unique_lock<std::mutex> l(a.mx, std::try_to_lock);
    if (l.owns_lock() && ! a.running.load()) host().pumpOffline();
}

//==============================================================================================================
// strings: Java's modified UTF-8 is not UTF-8, so text goes through UTF-16 both ways

std::string toStd(JNIEnv* env, jstring s)
{
    if (s == nullptr) return {};
    const jsize n = env->GetStringLength(s);
    const jchar* c = env->GetStringChars(s, nullptr);
    if (c == nullptr) return {};
    std::string out;
    out.reserve((size_t) n);
    for (jsize i = 0; i < n; ++i) {
        uint32_t cp = c[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (c[i + 1] - 0xDC00u); ++i; }
        else if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
        if (cp < 0x80) out += (char) cp;
        else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
        else { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
    }
    env->ReleaseStringChars(s, c);
    return out;
}

jstring toJava(JNIEnv* env, const std::string& s)
{
    std::u16string w;
    w.reserve(s.size());
    const auto* p = (const unsigned char*) s.data();
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        bool ok = len > 0 && i + (size_t) len <= n;
        for (int k = 1; ok && k < len; ++k) {
            if ((p[i + (size_t) k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (p[i + (size_t) k] & 0x3Fu);
        }
        if (! ok || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) { w += (char16_t) 0xFFFD; ++i; continue; }
        if (cp >= 0x10000) { cp -= 0x10000; w += (char16_t) (0xD800 + (cp >> 10)); w += (char16_t) (0xDC00 + (cp & 0x3FF)); }
        else w += (char16_t) cp;
        i += (size_t) len;
    }
    return env->NewString((const jchar*) w.data(), (jsize) w.size());
}

jobjectArray stringArray(JNIEnv* env, const std::vector<std::string>& v)
{
    jclass cls = env->FindClass("java/lang/String");
    if (cls == nullptr) return nullptr;
    jobjectArray a = env->NewObjectArray((jsize) v.size(), cls, nullptr);
    env->DeleteLocalRef(cls);
    if (a == nullptr) return nullptr;
    for (size_t i = 0; i < v.size(); ++i) {
        jstring s = toJava(env, v[i]);
        if (s == nullptr) return nullptr;   // OutOfMemoryError pending
        env->SetObjectArrayElement(a, (jsize) i, s);
        env->DeleteLocalRef(s);
    }
    return a;
}

int clampDeck(jint deck) { return deck == 1 ? 1 : 0; }

std::string bpmText(double bpm)
{
    if (! (bpm > 0)) return {};
    const double r = std::round(bpm * 10) / 10;
    char b[32];
    if (std::abs(r - std::round(r)) < 1e-9) std::snprintf(b, sizeof b, "%d", (int) std::lround(r));
    else std::snprintf(b, sizeof b, "%.1f", r);
    return b;
}

}  // namespace

//==============================================================================================================
// JNI

#define WHEEL_JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_thesnakedevice_wheel_Native_##name

WHEEL_JNI(jboolean, start)(JNIEnv*, jclass)
{
    host().collect();
    Audio& a = audio();
    std::lock_guard<std::mutex> l(a.mx);
    a.wanted = true;
    if (a.stream != nullptr && a.running.load()) return JNI_TRUE;
    closeLocked(a);
    return openLocked(a) ? JNI_TRUE : JNI_FALSE;
}

WHEEL_JNI(void, stop)(JNIEnv*, jclass)
{
    Audio& a = audio();
    {
        std::lock_guard<std::mutex> l(a.mx);
        a.wanted = false;
        closeLocked(a);
        host().pumpOffline();
    }
    host().collect();
}

WHEEL_JNI(jint, sampleRate)(JNIEnv*, jclass) { return (jint) std::lround(host().sampleRate()); }

WHEEL_JNI(jdouble, latencyMs)(JNIEnv*, jclass)
{
    Audio& a = audio();
    std::unique_lock<std::mutex> l(a.mx, std::try_to_lock);
    if (! l.owns_lock()) return a.lastLatency.load();
    if (a.stream == nullptr || ! a.running.load()) return 0;
    AAudioStream* s = a.stream;
    const double rate = AAudioStream_getSampleRate(s);
    if (rate <= 0) return 0;
    double ms = 0;
    int64_t framePos = 0, timeNs = 0;
    if (AAudioStream_getTimestamp(s, CLOCK_MONOTONIC, &framePos, &timeNs) == AAUDIO_OK) {
        // when the frame written next will be heard, from now
        const int64_t written = AAudioStream_getFramesWritten(s);
        timespec ts {};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const int64_t nowNs = (int64_t) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        const double presentNs = (double) timeNs + (double) (written - framePos) * 1e9 / rate;
        ms = (presentNs - (double) nowNs) / 1e6;
    }
    if (! (ms > 0 && ms < 1000)) ms = AAudioStream_getBufferSizeInFrames(s) * 1000.0 / rate;   // buffer only (no timestamp yet)
    a.lastLatency.store(ms);
    return ms;
}

WHEEL_JNI(jboolean, loadTrack)(JNIEnv* env, jclass, jint deck, jfloatArray left, jfloatArray right, jint sampleRate, jint fd,
                               jstring path, jstring title, jstring artist)
{
    host().collect();
    if (left == nullptr || sampleRate <= 0) return JNI_FALSE;
    size_t n = (size_t) env->GetArrayLength(left);
    if (right != nullptr) n = std::min(n, (size_t) env->GetArrayLength(right));
    n = std::min(n, (size_t) (Host::kMaxSeconds * sampleRate));
    if (n == 0) return JNI_FALSE;
    std::vector<float> L, R;
    try {
        L.resize(n);
        if (right != nullptr) R.resize(n);
    } catch (const std::bad_alloc&) { return JNI_FALSE; }
    env->GetFloatArrayRegion(left, 0, (jsize) n, L.data());
    if (right != nullptr) env->GetFloatArrayRegion(right, 0, (jsize) n, R.data());
    if (env->ExceptionCheck()) return JNI_FALSE;
    const std::string p = toStd(env, path), t = toStd(env, title), ar = toStd(env, artist);
    std::shared_ptr<Track> trk;
    try {
        FdSource src(fd);
        trk = Host::buildTrack(L.data(), right != nullptr ? R.data() : nullptr, n, sampleRate, fd >= 0 ? &src : nullptr, p, t, ar);
    } catch (const std::bad_alloc&) { return JNI_FALSE; }
    L = {}; R = {};
    if (trk == nullptr) return JNI_FALSE;
    LOGI("load deck %d: '%s' %.2f s, bpm %.2f, first beat %.3f, key %s%s", (int) clampDeck(deck), trk->title.c_str(), trk->length(), trk->bpm,
         trk->firstBeat, camelot(trk->key).c_str(), trk->fromSerato ? ", Serato" : "");
    const bool ok = host().load(clampDeck(deck), std::move(trk));
    pumpIfIdle();
    return ok ? JNI_TRUE : JNI_FALSE;
}

WHEEL_JNI(jobjectArray, readTags)(JNIEnv* env, jclass, jint fd, jstring fileName)
{
    Tags g;
    if (fd >= 0) {
        FdSource src(fd);
        g = readTags(src);
    }
    Track t;
    applyTags(g, t);
    std::string title = t.title, artist = t.artist;
    nameFromFileName(toStd(env, fileName), title, artist);
    const int key = parseKey(g.keyText);
    return stringArray(env, { title, artist, t.album, bpmText(t.bpm), key >= 0 ? camelot(key) : std::string(), g.serato ? "1" : "0" });
}

WHEEL_JNI(jobjectArray, parseCrate)(JNIEnv* env, jclass, jbyteArray crate)
{
    std::vector<std::string> paths;
    if (crate != nullptr) {
        const jsize n = env->GetArrayLength(crate);
        std::vector<uint8_t> b((size_t) n);
        env->GetByteArrayRegion(crate, 0, n, (jbyte*) b.data());
        paths = parseCrate(b.data(), b.size());
    }
    return stringArray(env, paths);
}

WHEEL_JNI(void, trackInfo)(JNIEnv* env, jclass, jint deck, jdoubleArray out7)
{
    if (out7 == nullptr) return;
    double v[jni::kTrackInfo];
    host().trackInfo(clampDeck(deck), v);
    const jsize n = std::min((jsize) jni::kTrackInfo, env->GetArrayLength(out7));
    env->SetDoubleArrayRegion(out7, 0, n, v);
}

WHEEL_JNI(jbyteArray, wave)(JNIEnv* env, jclass, jint deck, jint band)
{
    const std::vector<uint8_t> w = host().wave(clampDeck(deck), band);
    jbyteArray a = env->NewByteArray((jsize) w.size());
    if (a != nullptr && ! w.empty()) env->SetByteArrayRegion(a, 0, (jsize) w.size(), (const jbyte*) w.data());
    return a;
}

WHEEL_JNI(jobjectArray, trackText)(JNIEnv* env, jclass, jint deck)
{
    const auto t = host().trackText(clampDeck(deck));
    return stringArray(env, { t[0], t[1], t[2] });
}

WHEEL_JNI(void, status)(JNIEnv* env, jclass, jint deck, jdoubleArray out)
{
    if (out == nullptr) return;
    double v[jni::ST_COUNT];
    host().status(clampDeck(deck), v);
    const jsize n = std::min((jsize) jni::ST_COUNT, env->GetArrayLength(out));
    env->SetDoubleArrayRegion(out, 0, n, v);
}

WHEEL_JNI(void, cues)(JNIEnv* env, jclass, jint deck, jdoubleArray t, jintArray rgb)
{
    double tt[kCues];
    int32_t cc[kCues];
    host().cues(clampDeck(deck), tt, cc);
    if (t != nullptr) env->SetDoubleArrayRegion(t, 0, std::min((jsize) kCues, env->GetArrayLength(t)), tt);
    if (rgb != nullptr) {
        jint ci[kCues];
        for (int i = 0; i < kCues; ++i) ci[i] = (jint) cc[i];
        env->SetIntArrayRegion(rgb, 0, std::min((jsize) kCues, env->GetArrayLength(rgb)), ci);
    }
}

WHEEL_JNI(void, meters)(JNIEnv* env, jclass, jfloatArray out4)
{
    if (out4 == nullptr) return;
    float m[4];
    host().meters(m);
    env->SetFloatArrayRegion(out4, 0, std::min((jsize) 4, env->GetArrayLength(out4)), m);
}

WHEEL_JNI(void, cmd)(JNIEnv*, jclass, jint type, jint deck, jint i, jdouble v, jboolean on)
{
    if (deck < 0 || deck > 1) return;
    host().command(type, deck, i, v, on == JNI_TRUE);
    pumpIfIdle();
}

WHEEL_JNI(void, set)(JNIEnv*, jclass, jint id, jfloat value) { host().setValue(id, value); }

WHEEL_JNI(jfloat, get)(JNIEnv*, jclass, jint id) { return host().getValue(id); }
