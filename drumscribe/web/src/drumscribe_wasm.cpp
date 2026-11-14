// Drum Scribe in the browser: the C++ engine (drumscribe/core) as WebAssembly, driven step by step from a Web Worker
// (drumscribe/web/worker.js). Everything runs here except the DrumSep net itself, which onnxruntime-web runs in
// JavaScript between ds_drumsep_input() and ds_drumsep_add() (the STFT / iSTFT / chunking stay here).
//   ds_set_audio        decoded PCM at the file's own rate -> 44.1 kHz stereo (src/resample.cpp)
//   ds_demucs           htdemucs (demucs.cpp) -> drum and bass stems
//   ds_drumsep_*        MDX23C DrumSep -> kick snare toms hh ride crash   (Full mode)
//   ds_quick_kit        the drum stem split by frequency instead          (Quick mode, src/quick_kit.h)
//   ds_analyze          drums, bass, beats, groove (src/pipeline.h analyze) -> result JSON
//   ds_midi             a MIDI file from the result (src/pipeline.h make_midi)
#include <emscripten.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "pipeline.h"
#include "quick_kit.h"
#include "separate_impl.h"

using namespace ds;

EM_JS(void, js_progress, (const char* stage, double f), {
    if (Module.onProgress) Module.onProgress(UTF8ToString(stage), f);
});

namespace {
Stereo g_song, g_drums, g_bass;
std::unique_ptr<sep::DrumSepDemix> g_demix;
float g_gain = 1;
std::vector<float> g_in, g_out;
std::map<std::string, Signal> g_kit;
json g_result;
std::string g_text, g_error;
const fs::path kWork = "/work";

template <class F> int guarded(F&& f)
{
    try {
        return f();
    } catch (const std::exception& e) {
        g_error = e.what();
    } catch (...) {
        g_error = "unknown error";
    }
    return -1;
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* ds_error() { return g_error.c_str(); }
EMSCRIPTEN_KEEPALIVE float* ds_alloc_floats(int n) { return new float[(size_t) n]; }
EMSCRIPTEN_KEEPALIVE void ds_free_floats(float* p) { delete[] p; }

// interleaved float PCM, any rate and channel count -> 44.1 kHz stereo; returns the length in samples
EMSCRIPTEN_KEEPALIVE int ds_set_audio(const float* pcm, int frames, int channels, int sr)
{
    return guarded([&] {
        g_song = to_stereo_44k(pcm, (size_t) frames, channels, sr);
        return (int) g_song.size();
    });
}

// Demucs v4 htdemucs (the engine's "fast" quality) -> drum + bass stems; the mix and the model are freed afterwards
EMSCRIPTEN_KEEPALIVE int ds_demucs(const char* model_file)
{
    return guarded([&] {
        auto r = sep::demucs_run(model_file, g_song, {0, 1}, -1, [](double f, const std::string&) { js_progress("demucs", f); });
        g_drums = std::move(r[0]);
        g_bass = std::move(r[1]);
        g_song = Stereo{};
        sep::release_models();
        return 0;
    });
}

// DrumSep (as separate() runs it: the drum stem at 0.5 peak, overlap 2 = the engine's "fast"): returns the chunk count
EMSCRIPTEN_KEEPALIVE int ds_drumsep_begin(int overlap)
{
    return guarded([&] {
        g_gain = sep::drumsep_input_gain(g_drums);
        Stereo scaled = g_drums;
        for (auto& v : scaled.l) v *= g_gain;
        for (auto& v : scaled.r) v *= g_gain;
        g_demix = std::make_unique<sep::DrumSepDemix>(scaled, overlap);
        g_in.assign(sep::DrumSepDemix::kInputSize, 0.f);
        g_out.assign(sep::DrumSepDemix::kOutputSize, 0.f);
        return (int) g_demix->chunks();
    });
}
// the net's input for chunk k ([1][4][1024][256] floats); JS writes the net's output into ds_drumsep_output()
EMSCRIPTEN_KEEPALIVE float* ds_drumsep_input(int k)
{
    g_demix->input(k, g_in.data());
    return g_in.data();
}
EMSCRIPTEN_KEEPALIVE float* ds_drumsep_output() { return g_out.data(); }
EMSCRIPTEN_KEEPALIVE int ds_drumsep_add(int k)
{
    return guarded([&] {
        g_demix->add(k, g_out.data());
        return 0;
    });
}
EMSCRIPTEN_KEEPALIVE int ds_drumsep_end()
{
    return guarded([&] {
        auto kit = g_demix->finish();
        g_demix.reset();
        std::vector<float>().swap(g_in);
        std::vector<float>().swap(g_out);
        g_kit = sep::drumsep_kit(kit, g_gain);
        return 0;
    });
}

// Quick mode: no DrumSep, the drum stem split by frequency bands
EMSCRIPTEN_KEEPALIVE int ds_quick_kit()
{
    return guarded([&] {
        g_kit = quick::kit_from_drum_stem(g_drums);
        return 0;
    });
}

// drums, bass, beats, groove; tempo > 0 forces a constant grid. Returns the result JSON (valid until the next call).
EMSCRIPTEN_KEEPALIVE const char* ds_analyze(const char* title, double tempo)
{
    const int rc = guarded([&] {
        fs::create_directories(kWork);
        Job job;
        job.work = kWork;
        job.tempo = tempo;
        const double duration = (double) g_drums.size() / kSR;
        g_drums = Stereo{};
        g_result = analyze(job, g_kit, to_mono(g_bass), duration, title);
        g_kit.clear();
        g_bass = Stereo{};
        g_text = g_result.dump();
        return 0;
    });
    return rc ? nullptr : g_text.c_str();
}

// writes the MIDI file into the virtual file system and returns its path (part: all | drums | bass)
EMSCRIPTEN_KEEPALIVE const char* ds_midi(int split, const char* part, int tempo_map)
{
    const int rc = guarded([&] {
        g_text = make_midi(g_result, kWork, split != 0, part, tempo_map != 0).string();
        return 0;
    });
    return rc ? nullptr : g_text.c_str();
}

}  // extern "C"
