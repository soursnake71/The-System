// Omega Synsonics for Android, all C++: a NativeActivity that draws the shared panel (core/Panel.h) straight into
// the window's pixels, plays the circuit model (core/Engine.h) through AAudio in low-latency mode, and takes
// multitouch so pads and buttons can be played together. Memories, tempo and knobs are kept between launches.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Panel.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Synsonics", __VA_ARGS__)

namespace {

struct Host : syn::PanelHost {
  syn::Engine& e;
  explicit Host(syn::Engine& en) : e(en) {}
  float getParam(int id) override { return e.params[id]; }
  void setParam(int id, float v) override { e.params[id] = std::clamp(v, 0.f, 1.f); }
};

struct App {
  android_app* app = nullptr;
  syn::Engine engine;
  Host host{engine};
  syn::Panel panel{engine, host};
  AAudioStream* stream = nullptr;
  std::atomic<bool> restartAudio{false};
  bool resumed = false, hasWindow = false;
  std::vector<float> bufL, bufR;
  // double tap detection
  double lastTapTime = 0;
  float lastTapX = 0, lastTapY = 0;
  int insetTop = 0, insetBottom = 0;

  std::string statePath() const { return std::string(app->activity->internalDataPath) + "/synsonics.state"; }
  void loadState() {
    FILE* f = std::fopen(statePath().c_str(), "rb");
    if (!f) return;
    std::vector<uint8_t> d;
    uint8_t b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
    std::fclose(f);
    engine.loadState(d.data(), d.size());
  }
  void saveState() {
    auto d = engine.saveState();
    std::string tmp = statePath() + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), statePath().c_str());
  }

  // ---- audio
  static aaudio_data_callback_result_t onAudio(AAudioStream*, void* user, void* data, int32_t frames) {
    App* a = static_cast<App*>(user);
    float* out = static_cast<float*>(data);
    int done = 0;
    while (done < frames) {
      int n = std::min<int>(frames - done, int(a->bufL.size()));
      float* p[2] = {a->bufL.data(), a->bufR.data()};
      a->engine.process(p, n);
      for (int i = 0; i < n; ++i) { out[2 * (done + i)] = a->bufL[i]; out[2 * (done + i) + 1] = a->bufR[i]; }
      done += n;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }
  static void onAudioError(AAudioStream*, void* user, aaudio_result_t err) {
    LOGI("audio error %d, restarting", err);
    static_cast<App*>(user)->restartAudio = true;  // reopen from the main thread (e.g. headphones unplugged)
  }
  void startAudio() {
    if (stream) return;
    bufL.assign(1024, 0); bufR.assign(1024, 0);
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setDataCallback(b, onAudio, this);
    AAudioStreamBuilder_setErrorCallback(b, onAudioError, this);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream);
    if (r != AAUDIO_OK) {  // exclusive mode refused: shared
      AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
      r = AAudioStreamBuilder_openStream(b, &stream);
    }
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { stream = nullptr; LOGI("no audio stream: %s", AAudio_convertResultToText(r)); return; }
    engine.setSampleRate(AAudioStream_getSampleRate(stream));
    // two bursts of buffering: low latency without glitches
    AAudioStream_setBufferSizeInFrames(stream, AAudioStream_getFramesPerBurst(stream) * 2);
    AAudioStream_requestStart(stream);
    LOGI("audio %d Hz, burst %d", AAudioStream_getSampleRate(stream), AAudioStream_getFramesPerBurst(stream));
  }
  void stopAudio() {
    if (!stream) return;
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    stream = nullptr;
  }

  // ---- window
  // targetSdk 35 draws edge to edge: ask the decor view how much the status and navigation bars cover
  void readInsets() {
    JNIEnv* env = nullptr;
    JavaVM* vm = app->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
    jobject act = app->activity->clazz;
    jclass actC = env->GetObjectClass(act);
    jobject win = env->CallObjectMethod(act, env->GetMethodID(actC, "getWindow", "()Landroid/view/Window;"));
    if (win) {
      jobject decor = env->CallObjectMethod(win, env->GetMethodID(env->GetObjectClass(win), "getDecorView", "()Landroid/view/View;"));
      if (decor) {
        jobject ins = env->CallObjectMethod(decor, env->GetMethodID(env->GetObjectClass(decor), "getRootWindowInsets", "()Landroid/view/WindowInsets;"));
        if (ins) {
          jclass ic = env->GetObjectClass(ins);
          insetTop = env->CallIntMethod(ins, env->GetMethodID(ic, "getSystemWindowInsetTop", "()I"));
          insetBottom = env->CallIntMethod(ins, env->GetMethodID(ic, "getSystemWindowInsetBottom", "()I"));
        }
      }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    vm->DetachCurrentThread();
    panel.setInsets(insetTop, insetBottom);
  }

  void draw(bool force) {
    ANativeWindow* w = app->window;
    if (!w) return;
    int ww = ANativeWindow_getWidth(w), wh = ANativeWindow_getHeight(w);
    if (!force && !panel.changed(ww, wh)) return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(w, &buf, nullptr) != 0) return;
    syn::Canvas c;
    c.attach(static_cast<uint32_t*>(buf.bits), buf.width, buf.height, buf.stride, true);
    panel.render(c, true);  // window buffers rotate, so every posted frame is drawn whole
    ANativeWindow_unlockAndPost(w);
  }

  static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

  int32_t onInput(AInputEvent* ev) {
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int32_t action = AMotionEvent_getAction(ev);
    int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
    size_t idx = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    switch (kind) {
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_POINTER_DOWN: {
        int id = AMotionEvent_getPointerId(ev, idx);
        float x = AMotionEvent_getX(ev, idx), y = AMotionEvent_getY(ev, idx);
        double t = now();
        if (t - lastTapTime < 0.3 && std::hypot(x - lastTapX, y - lastTapY) < 60) panel.pointerDoubleTap(x, y);
        lastTapTime = t; lastTapX = x; lastTapY = y;
        panel.pointerDown(id, x, y);
        break;
      }
      case AMOTION_EVENT_ACTION_MOVE:
        for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); ++i)
          panel.pointerMove(AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
        break;
      case AMOTION_EVENT_ACTION_UP:
      case AMOTION_EVENT_ACTION_POINTER_UP:
        panel.pointerUp(AMotionEvent_getPointerId(ev, idx));
        break;
      case AMOTION_EVENT_ACTION_CANCEL:
        panel.pointerCancelAll();
        break;
    }
    draw(false);
    return 1;
  }

  void onCmd(int32_t cmd) {
    switch (cmd) {
      case APP_CMD_INIT_WINDOW:
        if (app->window) {
          ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
          hasWindow = true;
          readInsets();
          draw(true);
        }
        break;
      case APP_CMD_WINDOW_RESIZED:
      case APP_CMD_CONFIG_CHANGED:
      case APP_CMD_CONTENT_RECT_CHANGED:
      case APP_CMD_WINDOW_REDRAW_NEEDED:
        if (app->window) { readInsets(); draw(true); }
        break;
      case APP_CMD_TERM_WINDOW: hasWindow = false; break;
      case APP_CMD_RESUME: resumed = true; startAudio(); break;
      case APP_CMD_PAUSE:
        resumed = false;
        panel.pointerCancelAll();
        stopAudio();
        saveState();
        break;
      case APP_CMD_SAVE_STATE: saveState(); break;
      case APP_CMD_DESTROY: stopAudio(); break;
    }
  }
};

}  // namespace

void android_main(android_app* state) {
  auto a = std::make_unique<App>();
  a->app = state;
  state->userData = a.get();
  state->onAppCmd = [](android_app* s, int32_t cmd) { static_cast<App*>(s->userData)->onCmd(cmd); };
  state->onInputEvent = [](android_app* s, AInputEvent* e) { return static_cast<App*>(s->userData)->onInput(e); };
  ANativeActivity_setWindowFlags(state->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
  a->loadState();

  while (!state->destroyRequested) {
    android_poll_source* src = nullptr;
    int events = 0;
    // while visible, wake about 60 times a second for the LEDs; otherwise sleep until something happens
    int timeout = (a->hasWindow && a->resumed) ? 16 : -1;
    int r = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&src));
    if (r >= 0 && src) src->process(state, src);
    if (a->restartAudio.exchange(false) && a->resumed) { a->stopAudio(); a->startAudio(); }
    if (a->hasWindow && a->resumed) a->draw(false);
  }
  a->stopAudio();
  a->saveState();
}
