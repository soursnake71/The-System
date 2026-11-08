// Omega Synsonics for Android, all C++: a NativeActivity that draws the shared panel (core/Panel.h) straight into
// the window's pixels, plays the circuit model (core/Engine.h) through AAudio in low-latency mode, and takes
// multitouch so pads and buttons can be played together. Patterns, song, mixer, effects and the sample pad are kept
// between launches. Files go through Android's file picker: a small Java class (SynsonicsActivity) opens it and
// hands back a file descriptor; reading, writing, decoding and encoding are all done here.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Files.h"
#include "Panel.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Synsonics", __VA_ARGS__)

namespace {

struct Host : syn::PanelHost {
  syn::Engine& e;
  std::function<void(int)> onFile;
  explicit Host(syn::Engine& en) : e(en) {}
  float getParam(int id) override { return e.params[id]; }
  void setParam(int id, float v) override { e.params[id] = std::clamp(v, 0.f, 1.f); }
  void requestFile(int op) override { if (onFile) onFile(op); }
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
  JNIEnv* env = nullptr;  // this (main) thread stays attached

  // ---- files: the picker request in flight, and the worker that does the job
  int pendingOp = -1;
  std::thread worker;
  std::atomic<bool> busy{false};
  syn::SampleData* savedSample = nullptr;

  std::string dataPath(const char* name) const { return std::string(app->activity->internalDataPath) + "/" + name; }
  static bool readAll(const std::string& path, std::vector<uint8_t>& d) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t b[16384];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
    std::fclose(f);
    return true;
  }
  static void writeAll(const std::string& path, const std::vector<uint8_t>& d) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
  }
  void loadState() {
    std::vector<uint8_t> d;
    if (readAll(dataPath("synsonics.state"), d)) engine.loadState(d.data(), d.size());
    std::vector<uint8_t> w, nm;
    if (readAll(dataPath("sample.wav"), w)) {
      std::string err;
      if (auto smp = syn::decodeAudio(w, err)) {
        if (readAll(dataPath("sample.name"), nm)) smp->name.assign(nm.begin(), nm.end());
        engine.setSample(smp);
        savedSample = smp.get();
      }
    }
  }
  void saveState() {
    writeAll(dataPath("synsonics.state"), engine.saveState());
    auto smp = engine.sample();
    if (smp.get() != savedSample) {  // the sample pad, only when it changed
      if (smp) {
        writeAll(dataPath("sample.wav"), syn::encodeWav(smp->l, smp->r.empty() ? smp->l : smp->r, int(smp->rate), 24));
        writeAll(dataPath("sample.name"), std::vector<uint8_t>(smp->name.begin(), smp->name.end()));
      } else {
        std::remove(dataPath("sample.wav").c_str());
      }
      savedSample = smp.get();
    }
  }

  // FILE page button: ask the Java side to show the picker
  void requestFile(int op) {
    if (busy) { panel.setStatus("Still working on the last file."); return; }
    jobject act = app->activity->clazz;
    jclass c = env->GetObjectClass(act);
    if (syn::isExport(op)) {
      std::string name = syn::exportName(engine, panel.exportOptions(), op);
      jstring mime = env->NewStringUTF(syn::opMime(op)), nm = env->NewStringUTF(name.c_str());
      env->CallVoidMethod(act, env->GetMethodID(c, "createDocument", "(ILjava/lang/String;Ljava/lang/String;)V"), op, mime, nm);
      env->DeleteLocalRef(mime); env->DeleteLocalRef(nm);
    } else {
      std::vector<const char*> m = op == syn::OpImportMidi
          ? std::vector<const char*>{"audio/midi", "audio/x-midi", "audio/mid", "application/x-midi", "audio/sp-midi", "application/octet-stream"}
          : std::vector<const char*>{"audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave", "audio/mpeg", "audio/mp3", "audio/x-mpeg"};
      jobjectArray arr = env->NewObjectArray(jsize(m.size()), env->FindClass("java/lang/String"), nullptr);
      for (size_t i = 0; i < m.size(); ++i) { jstring js = env->NewStringUTF(m[i]); env->SetObjectArrayElement(arr, jsize(i), js); env->DeleteLocalRef(js); }
      env->CallVoidMethod(act, env->GetMethodID(c, "openDocument", "(I[Ljava/lang/String;)V"), op, arr);
      env->DeleteLocalRef(arr);
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); panel.setStatus("Couldn't open the file picker."); return; }
    pendingOp = op;
  }

  std::string jstr(jstring s) {
    if (!s) return {};
    const char* u = env->GetStringUTFChars(s, nullptr);
    std::string r = u ? u : "";
    if (u) env->ReleaseStringUTFChars(s, u);
    return r;
  }

  // the picker answered (seen from the main loop): open the file and hand it to the worker
  void pollFileResult() {
    if (pendingOp < 0) return;
    jobject act = app->activity->clazz;
    jclass c = env->GetObjectClass(act);
    jfieldID fOp = env->GetFieldID(c, "resultOp", "I"), fUri = env->GetFieldID(c, "resultUri", "Ljava/lang/String;");
    int op = env->GetIntField(act, fOp);
    if (op == -1) return;
    env->SetIntField(act, fOp, -1);
    pendingOp = -1;
    if (op < 0) return;  // cancelled
    jstring juri = static_cast<jstring>(env->GetObjectField(act, fUri));
    bool exp = syn::isExport(op);
    jstring mode = env->NewStringUTF(exp ? "wt" : "r");
    int fd = env->CallIntMethod(act, env->GetMethodID(c, "openFd", "(Ljava/lang/String;Ljava/lang/String;)I"), juri, mode);
    std::string name = jstr(static_cast<jstring>(env->CallObjectMethod(act, env->GetMethodID(c, "displayName", "(Ljava/lang/String;)Ljava/lang/String;"), juri)));
    env->DeleteLocalRef(mode);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (fd < 0) { panel.setStatus("Couldn't open that file."); return; }
    if (worker.joinable()) worker.join();
    busy = true;
    syn::ExportOptions o = panel.exportOptions();
    int pattern = panel.editPattern();
    panel.setStatus(exp ? "Exporting " + name + "..." : "Loading " + name + "...");
    worker = std::thread([this, op, fd, name, o, pattern, exp] {
      std::string msg;
      if (exp) {
        std::vector<uint8_t> bytes;
        msg = syn::exportFile(engine, o, op, bytes);
        size_t done = 0;
        while (msg.empty() && done < bytes.size()) {
          ssize_t w = ::write(fd, bytes.data() + done, bytes.size() - done);
          if (w <= 0) { msg = "Couldn't write the file."; break; }
          done += size_t(w);
        }
        if (msg.empty()) msg = "Saved " + name + " (" + std::to_string(bytes.size() / 1024) + " KB).";
      } else {
        std::vector<uint8_t> data;
        uint8_t b[65536];
        ssize_t n;
        while ((n = ::read(fd, b, sizeof b)) > 0 && data.size() < size_t(256) << 20) data.insert(data.end(), b, b + n);
        std::string info;
        msg = syn::importFile(engine, op, data, name, pattern, info);
        if (msg.empty()) msg = "Loaded " + info + ".";
      }
      ::close(fd);
      panel.setStatus(msg);
      busy = false;
    });
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
    if (!env) return;
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
        if (env->ExceptionCheck()) env->ExceptionClear();
        noHaptics(env, decor);
      }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    panel.setInsets(insetTop, insetBottom);
  }

  // Holding a finger down is how this app is played, but Android treats it as a long press and buzzes (Samsung
  // phones do this on the NativeActivity's content view). Turn haptics and long-press off on every view in the window.
  // These only set flags on the views, so they're safe from this thread.
  static void noHaptics(JNIEnv* env, jobject view) {
    jclass vc = env->FindClass("android/view/View");
    jclass gc = env->FindClass("android/view/ViewGroup");
    if (!vc || !gc) { env->ExceptionClear(); return; }
    env->CallVoidMethod(view, env->GetMethodID(vc, "setHapticFeedbackEnabled", "(Z)V"), JNI_FALSE);
    env->CallVoidMethod(view, env->GetMethodID(vc, "setLongClickable", "(Z)V"), JNI_FALSE);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
    if (env->IsInstanceOf(view, gc)) {
      jint n = env->CallIntMethod(view, env->GetMethodID(gc, "getChildCount", "()I"));
      jmethodID at = env->GetMethodID(gc, "getChildAt", "(I)Landroid/view/View;");
      for (jint i = 0; i < n; ++i) {
        jobject child = env->CallObjectMethod(view, at, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (child) { noHaptics(env, child); env->DeleteLocalRef(child); }
      }
    }
    env->DeleteLocalRef(vc);
    env->DeleteLocalRef(gc);
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
  state->activity->vm->AttachCurrentThread(&a->env, nullptr);
  App* ap = a.get();
  a->host.onFile = [ap](int op) { ap->requestFile(op); };
  a->loadState();

  while (!state->destroyRequested) {
    android_poll_source* src = nullptr;
    int events = 0;
    // while visible, wake about 60 times a second for the LEDs; otherwise sleep until something happens
    int timeout = (a->hasWindow && a->resumed) ? 16 : -1;
    int r = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&src));
    if (r >= 0 && src) src->process(state, src);
    if (a->restartAudio.exchange(false) && a->resumed) { a->stopAudio(); a->startAudio(); }
    a->pollFileResult();
    if (a->hasWindow && a->resumed) a->draw(false);
  }
  a->stopAudio();
  if (a->worker.joinable()) a->worker.join();
  a->saveState();
  state->activity->vm->DetachCurrentThread();
}
