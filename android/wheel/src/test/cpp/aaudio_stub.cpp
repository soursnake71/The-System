// AAudio + liblog stand-ins so libwheel.so's JNI code runs on a Linux JVM (the Robolectric launch test, built by
// ../build-host-lib.sh): a thread pulls audio through the data callback like the device would.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>
struct AAudioStreamBuilderStruct { AAudioStream_dataCallback cb = nullptr; void* user = nullptr; int ch = 2; };
struct AAudioStreamStruct { AAudioStream_dataCallback cb; void* user; int ch; std::atomic<bool> run { false }; std::thread t; int64_t written = 0; };
extern "C" {
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** b) { *b = (AAudioStreamBuilder*) new AAudioStreamBuilderStruct; return AAUDIO_OK; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*, aaudio_direction_t) {}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder*, aaudio_performance_mode_t) {}
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*, aaudio_sharing_mode_t) {}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*, aaudio_format_t) {}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder* b, int32_t c) { ((AAudioStreamBuilderStruct*) b)->ch = c; }
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder* b, AAudioStream_dataCallback cb, void* u) { auto* x = (AAudioStreamBuilderStruct*) b; x->cb = cb; x->user = u; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder*, AAudioStream_errorCallback, void*) {}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b, AAudioStream** s) {
  auto* x = (AAudioStreamBuilderStruct*) b; auto* st = new AAudioStreamStruct; st->cb = x->cb; st->user = x->user; st->ch = x->ch; *s = (AAudioStream*) st; return AAUDIO_OK; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder* b) { delete (AAudioStreamBuilderStruct*) b; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStart(AAudioStream* s) {
  auto* st = (AAudioStreamStruct*) s; st->run = true;
  st->t = std::thread([st] { std::vector<float> buf(256 * 2); while (st->run) { if (st->cb) st->cb((AAudioStream*) st, st->user, buf.data(), 256); st->written += 256; std::this_thread::sleep_for(std::chrono::microseconds(5333)); } });
  return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStop(AAudioStream* s) { auto* st = (AAudioStreamStruct*) s; st->run = false; if (st->t.joinable()) st->t.join(); return AAUDIO_OK; }
aaudio_result_t AAudioStream_close(AAudioStream* s) { AAudioStream_requestStop(s); delete (AAudioStreamStruct*) s; return AAUDIO_OK; }
int32_t AAudioStream_getSampleRate(AAudioStream*) { return 48000; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream*) { return 256; }
int32_t AAudioStream_getChannelCount(AAudioStream* s) { return ((AAudioStreamStruct*) s)->ch; }
aaudio_format_t AAudioStream_getFormat(AAudioStream*) { return AAUDIO_FORMAT_PCM_FLOAT; }
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream*, int32_t n) { return n; }
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream*) { return 512; }
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream*) { return AAUDIO_SHARING_MODE_SHARED; }
aaudio_result_t AAudioStream_getTimestamp(AAudioStream*, clockid_t, int64_t*, int64_t*) { return AAUDIO_ERROR_INVALID_STATE; }
int64_t AAudioStream_getFramesWritten(AAudioStream* s) { return ((AAudioStreamStruct*) s)->written; }
const char* AAudio_convertResultToText(aaudio_result_t) { return "stub"; }
int __android_log_print(int, const char* tag, const char* fmt, ...) { va_list a; va_start(a, fmt); fprintf(stderr, "[%s] ", tag); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a); return 0; }
}
