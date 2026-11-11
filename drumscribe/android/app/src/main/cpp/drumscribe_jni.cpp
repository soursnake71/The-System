// JNI bridge: com.thesnakedevice.drumscribe.Engine <-> the Drum Scribe engine (drumscribe/core).
// One analysis at a time, on its own native thread: start() returns at once, the app polls progress() (JSON), may
// cancel(), then reads result(). MIDI files, a new tempo and the groove text are made from the stored result.json in
// the song's work folder, so they also work for songs analysed earlier (results are kept per song).
// The audio comes decoded by MediaCodec (Java) as a raw PCM file (16-bit or float, interleaved, its own rate); it is
// converted to 44.1 kHz stereo here (ds::to_stereo_44k, windowed-sinc resampler).
#include <jni.h>
#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pipeline.h"
#include "separate_impl.h"

namespace {

std::mutex g_m;                  // guards g_job / g_thread
std::unique_ptr<ds::Job> g_job;  // the current (or last) job
std::thread g_thread;

void log(const std::string& s) { __android_log_print(ANDROID_LOG_INFO, "DrumScribe", "%s", s.c_str()); }

std::string str(JNIEnv* env, jstring s)
{
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

void throw_java(JNIEnv* env, const std::string& msg)
{
    jclass c = env->FindClass("java/lang/RuntimeException");
    if (c) env->ThrowNew(c, msg.c_str());
}

// raw interleaved PCM file -> 44.1 kHz stereo
ds::Stereo read_pcm(const std::string& path, int sr, int channels, bool is_float)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("can't read the decoded audio");
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    const int bps = is_float ? 4 : 2;
    const size_t frames = bytes > 0 ? (size_t) bytes / (bps * channels) : 0;
    std::vector<float> pcm(frames * channels);
    if (is_float) {
        const size_t n = std::fread(pcm.data(), 4, pcm.size(), f);
        pcm.resize(n - n % channels);
    } else {
        std::vector<int16_t> buf(1 << 16);
        size_t o = 0, n;
        while (o < pcm.size() && (n = std::fread(buf.data(), 2, std::min(buf.size(), pcm.size() - o), f)) > 0)
            for (size_t i = 0; i < n; ++i) pcm[o++] = buf[i] / 32768.f;
        pcm.resize(o - o % channels);
    }
    std::fclose(f);
    if (pcm.empty()) throw std::runtime_error("The file has no audio that could be decoded.");
    return ds::to_stereo_44k(pcm.data(), pcm.size() / channels, channels, sr);
}

ds::json load_result(const std::string& work)
{
    std::ifstream in(ds::fs::path(work) / "result.json");
    if (!in) throw std::runtime_error("no result in " + work);
    return ds::json::parse(in);
}

}  // namespace

extern "C" {

// start(pcmPath, sampleRate, channels, isFloat, title, workDir, modelDir, quality, tempo, demucsThreads):
// false if a job is still running. pcmPath "" = use the stems cached in workDir. The PCM file is deleted once read.
JNIEXPORT jboolean JNICALL Java_com_thesnakedevice_drumscribe_Engine_start(
    JNIEnv* env, jclass, jstring jpcm, jint sr, jint channels, jboolean is_float, jstring jtitle, jstring jwork,
    jstring jmodels, jstring jquality, jdouble tempo, jint demucs_threads)
{
    std::lock_guard<std::mutex> l(g_m);
    if (g_job) {
        std::lock_guard<std::mutex> jl(g_job->m);
        if (g_job->state == "running" || g_job->state == "queued") return JNI_FALSE;
    }
    if (g_thread.joinable()) g_thread.join();
    // each Demucs worker needs ~1.2 GB: one on a phone (more only if asked); ONNX Runtime / iSTFT: up to 4 threads
    setenv("DS_DEMUCS_THREADS", std::to_string(std::max(1, (int) demucs_threads)).c_str(), 1);
    setenv("DS_LOW_MEMORY", "1", 1);  // models dropped as soon as they are done with (separate.cpp)
    setenv("DS_THREADS", std::to_string(std::max(1u, std::min(4u, std::thread::hardware_concurrency()))).c_str(), 1);
    g_job = std::make_unique<ds::Job>();
    ds::Job* job = g_job.get();
    job->work = str(env, jwork);
    job->quality = str(env, jquality).empty() ? "fast" : str(env, jquality);
    job->tempo = tempo;
    job->state = "running";
    job->stage = "decode";
    job->message = "Reading the audio";
    const std::string pcm = str(env, jpcm), title = str(env, jtitle), models = str(env, jmodels);
    const bool fl = is_float;
    g_thread = std::thread([job, pcm, sr, channels, fl, title, models] {
        try {
            ds::Stereo song;
            if (!pcm.empty() && !(ds::fs::exists(job->work / "stems" / "bass.wav") && ds::fs::exists(job->work / "source.json"))) {
                song = read_pcm(pcm, sr, channels, fl);
                job->set("decode", 0.05, "Audio ready: " + std::to_string((int) (song.size() / ds::kSR)) + " s");
            }
            if (!pcm.empty()) std::remove(pcm.c_str());
            ds::run_job_audio(*job, std::move(song), title, models);
            log("done: " + job->message);
        } catch (const std::exception& e) {
            if (!pcm.empty()) std::remove(pcm.c_str());
            std::lock_guard<std::mutex> jl(job->m);
            job->state = job->cancel ? "cancelled" : "error";
            job->error = e.what();
            job->message = job->cancel ? "Cancelled" : e.what();
            log("job ended: " + job->message);
        }
        ds::sep::release_models();  // give the ~1 GB of models back while the app is idle
    });
    return JNI_TRUE;
}

// {"state": idle|running|done|error|cancelled, "stage", "progress" 0..1, "message"}
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_drumscribe_Engine_progress(JNIEnv* env, jclass)
{
    std::lock_guard<std::mutex> l(g_m);
    ds::json j = {{"state", "idle"}, {"stage", ""}, {"progress", 0.0}, {"message", ""}};
    if (g_job) {
        std::lock_guard<std::mutex> jl(g_job->m);
        j = {{"state", g_job->state}, {"stage", g_job->stage}, {"progress", g_job->progress.load()},
             {"message", g_job->message}, {"work", g_job->work.string()}};
    }
    return jstr(env, j.dump());
}

JNIEXPORT void JNICALL Java_com_thesnakedevice_drumscribe_Engine_cancel(JNIEnv*, jclass)
{
    std::lock_guard<std::mutex> l(g_m);
    if (g_job) g_job->cancel = true;
}

// the finished job's result JSON ("" when there is none)
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_drumscribe_Engine_result(JNIEnv* env, jclass)
{
    std::lock_guard<std::mutex> l(g_m);
    if (!g_job) return jstr(env, "");
    std::lock_guard<std::mutex> jl(g_job->m);
    return jstr(env, g_job->state == "done" ? g_job->result.dump() : std::string());
}

// a MIDI file from workDir/result.json: part all|drums|bass, split = a track per kit piece, tempoMap = grid follows
// the band (false: constant tempo). Returns the file's path (inside workDir).
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_drumscribe_Engine_makeMidi(JNIEnv* env, jclass, jstring jwork,
                                                                            jstring jpart, jboolean split, jboolean tempo_map)
{
    try {
        const std::string work = str(env, jwork);
        return jstr(env, ds::make_midi(load_result(work), work, split, str(env, jpart), tempo_map).string());
    } catch (const std::exception& e) {
        throw_java(env, e.what());
        return nullptr;
    }
}

// the grid at a fixed tempo (bpm > 0) or detected again (0); the hits stay. Returns the new result JSON.
JNIEXPORT jstring JNICALL Java_com_thesnakedevice_drumscribe_Engine_retempo(JNIEnv* env, jclass, jstring jwork, jdouble bpm)
{
    try {
        const std::string work = str(env, jwork);
        return jstr(env, ds::retempo(load_result(work), work, bpm).dump());
    } catch (const std::exception& e) {
        throw_java(env, e.what());
        return nullptr;
    }
}

JNIEXPORT jstring JNICALL Java_com_thesnakedevice_drumscribe_Engine_grooveText(JNIEnv* env, jclass, jstring jwork)
{
    try {
        return jstr(env, ds::groove_text(load_result(str(env, jwork))["groove"]));
    } catch (const std::exception& e) {
        throw_java(env, e.what());
        return nullptr;
    }
}

}  // extern "C"
