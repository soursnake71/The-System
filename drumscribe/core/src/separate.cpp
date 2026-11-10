// Drum Scribe: neural source separation, all C++ at run time.
//   1. Demucs v4 (htdemucs "fast", htdemucs_ft "best": its drums model for the drums, its bass model for the bass)
//      through demucs.cpp (github.com/sevagh/demucs.cpp, MIT) with our own driver that reproduces
//      demucs.apply.apply_model exactly and runs the 7.8 s segments on all cores.
//   2. MDX23C DrumSep (aufr33 & jarredou) on the drum stem -> kick snare toms hh ride crash, the net exported to
//      ONNX (tools/export_drumsep_onnx.py) run with ONNX Runtime; STFT / iSTFT / chunking here, reproducing
//      python-audio-separator's MDXCSeparator.
// Models: tools/get_models.sh. Checked against PyTorch by tools/sep_check.cpp.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <onnxruntime_cxx_api.h>
#include <unsupported/Eigen/FFT>

#include "dsp.hpp"
#include "model.hpp"
#include "separate_impl.h"

namespace fs = std::filesystem;

namespace ds::sep {

const char* const kKitStems[6] = {"kick", "snare", "toms", "hh", "ride", "crash"};

std::string resolve_model_dir(const std::string& model_dir) {
    if (!model_dir.empty()) return model_dir;
    if (const char* e = std::getenv("DRUMSCRIBE_MODELS"); e && *e) return e;
#ifdef _WIN32
    if (const char* e = std::getenv("LOCALAPPDATA"); e && *e) return (fs::path(e) / "drumscribe" / "models").string();
#endif
    const char* home = std::getenv("HOME");
    return (fs::path(home ? home : ".") / ".cache" / "drumscribe" / "models").string();
}

int num_threads() {
    if (const char* e = std::getenv("DS_THREADS"); e && std::atoi(e) > 0) return std::atoi(e);
    return std::max(1u, std::thread::hardware_concurrency());
}

static void need_file(const fs::path& f) {
    if (!fs::exists(f))
        throw std::runtime_error("separation model missing: " + f.string() +
                                 " - run drumscribe/core/tools/get_models.sh [" + f.parent_path().string() +
                                 "] to download and convert the models");
}

// ------------------------------------------------------------------------------------------------ model cache
// Loading takes seconds and ~350 MB per Demucs model / ~450 MB for DrumSep, so the models of the last call stay
// loaded (release_models() frees them). All access is under g_mutex (separate() is serialized).
static std::mutex g_mutex;
static std::map<std::string, std::shared_ptr<demucscpp::demucs_model>> g_demucs;
static std::unique_ptr<Ort::Env> g_ort_env;
static std::unique_ptr<Ort::Session> g_ort_session;
static std::string g_ort_path;

static std::shared_ptr<demucscpp::demucs_model> get_demucs(const std::string& file) {
    auto it = g_demucs.find(file);
    if (it != g_demucs.end()) return it->second;
    need_file(file);
    auto m = std::make_shared<demucscpp::demucs_model>();
    // demucs.cpp chats on stdout while loading; keep stdout clean (the server may speak a protocol on it)
    std::ostringstream sink;
    auto* old = std::cout.rdbuf(sink.rdbuf());
    bool ok = false;
    try {
        ok = demucscpp::load_demucs_model(file, m.get());
    } catch (...) {
        std::cout.rdbuf(old);
        throw;
    }
    std::cout.rdbuf(old);
    if (!ok || !m->is_4sources) throw std::runtime_error("could not load Demucs model " + file);
    g_demucs[file] = m;
    return m;
}

void release_models() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_demucs.clear();
    g_ort_session.reset();
    g_ort_path.clear();
}

// ------------------------------------------------------------------------------------------------ Demucs
static constexpr int kSegment = 343980;     // int(44100 * 7.8): htdemucs training segment
static constexpr int kMaxShift = 22050;     // int(0.5 * 44100)
static constexpr double kOverlap = 0.25;

int demucs_default_shift() {
    if (const char* e = std::getenv("DS_DEMUCS_SHIFT"); e && *e) return std::clamp(std::atoi(e), 0, kMaxShift);
    return 11025;  // demucs draws random.randint(0, 22050); a fixed value keeps results reproducible
}

std::vector<Stereo> demucs_run(const std::string& model_file, const Stereo& song, const std::vector<int>& sources,
                               int shift_offset, const Progress& p) {
    const long L = (long)song.size();
    std::vector<Stereo> res(sources.size());
    for (auto& s : res) s.l.assign(L, 0.f), s.r.assign(L, 0.f);
    if (L == 0) return res;
    auto model = get_demucs(model_file);
    const float* ch[2] = {song.l.data(), song.r.size() == song.l.size() ? song.r.data() : song.l.data()};

    // demucs.api: ref = wav.mean(0); wav = (wav - ref.mean()) / (ref.std() + 1e-8)
    double mean = 0;
    for (long i = 0; i < L; ++i) mean += 0.5 * ((double)ch[0][i] + ch[1][i]);
    mean /= L;
    double var = 0;
    for (long i = 0; i < L; ++i) {
        double d = 0.5 * ((double)ch[0][i] + ch[1][i]) - mean;
        var += d * d;
    }
    const double sd = (L > 1 ? std::sqrt(var / (L - 1)) : 0.0) + 1e-8;

    // apply_model with shifts=1: the track zero-padded by max_shift each side, then a window starting at `offset`
    const int offset = shift_offset < 0 ? demucs_default_shift() : std::min(shift_offset, kMaxShift);
    const long P = L + 2L * kMaxShift;  // padded_mix length
    std::vector<float> padded[2];
    for (int c = 0; c < 2; ++c) {
        padded[c].assign(P, 0.f);
        for (long i = 0; i < L; ++i) padded[c][kMaxShift + i] = (float)((ch[c][i] - mean) / sd);
    }
    const long s0 = offset, Ls = L + kMaxShift - offset;  // the shifted chunk inside padded_mix

    // split: segments of 343980 every 257985 samples, triangle weights
    const long stride = (long)((1 - kOverlap) * kSegment);
    std::vector<long> offs;
    for (long o = 0; o < Ls; o += stride) offs.push_back(o);
    std::vector<float> weight(kSegment);
    {
        const int h = kSegment / 2;
        for (int k = 0; k < h; ++k) weight[k] = (float)(k + 1);
        for (int k = h; k < kSegment; ++k) weight[k] = (float)(kSegment - k);
        const float mx = *std::max_element(weight.begin(), weight.end());
        for (auto& w : weight) w /= mx;  // transition_power 1
    }
    const int ns = (int)sources.size();
    std::vector<std::vector<float>> acc(ns * 2, std::vector<float>(Ls, 0.f));
    std::vector<float> sum_w(Ls, 0.f);
    for (long o : offs) {
        const long n = std::min<long>(kSegment, Ls - o);
        for (long k = 0; k < n; ++k) sum_w[o + k] += weight[k];
    }

    const int nthreads = std::max(1, std::min<int>(num_threads(), (int)offs.size()));
    std::atomic<int> next{0};
    std::mutex mx;
    std::condition_variable cv;
    int done = 0;
    std::vector<float> frac(nthreads, 0.f);  // progress inside the segment each worker is on (racy reads are fine)
    std::exception_ptr err;

    auto worker = [&](int wi) {
        try {
            demucscpp::demucs_segment_buffers buf(2, kSegment, 4);
            demucscpp::stft_buffers stft_buf(buf.padded_segment_samples);
            for (;;) {
                const int ci = next.fetch_add(1);
                if (ci >= (int)offs.size()) break;
                const long o = offs[ci];
                const long n = std::min<long>(kSegment, Ls - o);
                // TensorChunk.padded(343980): real context around a short chunk, zeros outside padded_mix
                const long delta = kSegment - n;
                const long start = s0 + o - delta / 2;
                for (int c = 0; c < 2; ++c)
                    for (long j = 0; j < kSegment; ++j) {
                        const long idx = start + j;
                        buf.mix(c, j) = (idx >= 0 && idx < P) ? padded[c][idx] : 0.f;
                    }
                demucscpp::model_inference(*model, buf, stft_buf,
                                           [&frac, wi](float f, const std::string&) { frac[wi] = f; }, 0.f, 1.f);
                // center_trim to n, weight, accumulate (at most 2 segments overlap: a+b == b+a, so the
                // result doesn't depend on which thread finishes first)
                const long t0 = delta / 2;
                std::lock_guard<std::mutex> lk(mx);
                for (int si = 0; si < ns; ++si)
                    for (int c = 0; c < 2; ++c) {
                        float* a = acc[si * 2 + c].data() + o;
                        for (long k = 0; k < n; ++k) a[k] += weight[k] * buf.targets_out(sources[si], c, t0 + k);
                    }
                frac[wi] = 0.f;
                ++done;
                cv.notify_all();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(mx);
            if (!err) err = std::current_exception();
            next = (int)offs.size();
            cv.notify_all();
        }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < nthreads; ++i) th.emplace_back(worker, i);
    {
        std::unique_lock<std::mutex> lk(mx);
        while (done < (int)offs.size() && !err) {
            cv.wait_for(lk, std::chrono::milliseconds(500));
            if (p) {
                double f = done;
                for (int i = 0; i < nthreads; ++i) f += std::clamp(frac[i], 0.f, 1.f);
                p(std::min(1.0, f / offs.size()), "demucs");
            }
        }
    }
    for (auto& t : th) t.join();
    if (err) std::rethrow_exception(err);

    // out /= sum_weight; out = shifted_out[..., max_shift - offset:][:L]; out * std + mean
    const long b = kMaxShift - offset;
    for (int si = 0; si < ns; ++si)
        for (int c = 0; c < 2; ++c) {
            Signal& dst = c ? res[si].r : res[si].l;
            const auto& a = acc[si * 2 + c];
            for (long i = 0; i < L; ++i) dst[i] = (float)((a[b + i] / sum_w[b + i]) * sd + mean);
        }
    return res;
}

// ------------------------------------------------------------------------------------------------ DrumSep
static constexpr int kNfft = 2048, kHop = 512, kDimF = 1024;
static constexpr int kChunk = kHop * (256 - 1);     // 130560: hop_length * (dim_t - 1)
static constexpr int kFrames = kChunk / kHop + 1;   // 256
static constexpr int kNumStems = 6;

int drumsep_overlap(Quality q) {
    if (const char* e = std::getenv("DS_DRUMSEP_OVERLAP"); e && std::atoi(e) > 0) return std::atoi(e);
    return q == Quality::best ? 4 : 2;
}

namespace {
// torch.stft / torch.istft (n_fft 2048, hop 512, periodic Hann, center=True, reflect pad, onesided)
struct Stft {
    Eigen::FFT<float> fft;
    std::vector<float> win, frame, env;
    std::vector<std::complex<float>> spec;
    Stft() : win(kNfft), frame(kNfft) {
        fft.SetFlag(Eigen::FFT<float>::HalfSpectrum);
        for (int i = 0; i < kNfft; ++i) win[i] = (float)(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / kNfft));
        // istft window envelope for a kChunk output (sum of win^2 over the frames)
        env.assign(kChunk + kNfft, 0.f);
        for (int t = 0; t < kFrames; ++t)
            for (int i = 0; i < kNfft; ++i) env[t * kHop + i] += win[i] * win[i];
    }
    // x: kChunk samples -> out[(2*ch + reim) * kDimF * kFrames + f * kFrames + t] for bins f < kDimF
    void forward(const float* x, float* out_re, float* out_im) {
        const int pad = kNfft / 2;
        for (int t = 0; t < kFrames; ++t) {
            for (int i = 0; i < kNfft; ++i) {
                long j = (long)t * kHop + i - pad;
                if (j < 0) j = -j;                              // reflect
                if (j >= kChunk) j = 2L * (kChunk - 1) - j;
                frame[i] = x[j] * win[i];
            }
            fft.fwd(spec, frame);
            for (int f = 0; f < kDimF; ++f) {
                out_re[(long)f * kFrames + t] = spec[f].real();
                out_im[(long)f * kFrames + t] = spec[f].imag();
            }
        }
    }
    // re/im [kDimF][kFrames] (bin 1024 = 0) -> kChunk samples
    void inverse(const float* re, const float* im, float* y, std::vector<float>& ola) {
        ola.assign(kChunk + kNfft, 0.f);
        spec.assign(kNfft / 2 + 1, {0.f, 0.f});
        for (int t = 0; t < kFrames; ++t) {
            for (int f = 0; f < kDimF; ++f) spec[f] = {re[(long)f * kFrames + t], im[(long)f * kFrames + t]};
            spec[0] = {spec[0].real(), 0.f};  // C2R ignores the imaginary part of DC (and Nyquist, which is 0)
            spec[kNfft / 2] = {0.f, 0.f};
            fft.inv(frame, spec, kNfft);       // scaled by 1/n like torch.fft.irfft
            float* o = ola.data() + (long)t * kHop;
            for (int i = 0; i < kNfft; ++i) o[i] += frame[i] * win[i];
        }
        const int pad = kNfft / 2;
        for (int i = 0; i < kChunk; ++i) y[i] = ola[pad + i] / env[pad + i];
    }
};
}  // namespace

static Ort::Session& get_drumsep(const std::string& file) {
    if (g_ort_session && g_ort_path == file) return *g_ort_session;
    need_file(file);
    if (!g_ort_env) g_ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "drumscribe");
    Ort::SessionOptions so;
    so.SetIntraOpNumThreads(num_threads());
    so.SetInterOpNumThreads(1);
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    g_ort_session.reset();
    g_ort_session = std::make_unique<Ort::Session>(*g_ort_env, fs::path(file).c_str(), so);
    g_ort_path = file;
    return *g_ort_session;
}

std::vector<Stereo> drumsep_run(const std::string& onnx_file, const Stereo& drums, int overlap, const Progress& p) {
    const long L = (long)drums.size();
    std::vector<Stereo> res(kNumStems);
    for (auto& s : res) s.l.assign(L, 0.f), s.r.assign(L, 0.f);
    if (L == 0) return res;
    Ort::Session* sess;
    try {
        sess = &get_drumsep(onnx_file);
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("could not load DrumSep model " + onnx_file + ": " + e.what());
    }
    const float* ch[2] = {drums.l.data(), drums.r.size() == drums.l.size() ? drums.r.data() : drums.l.data()};

    // MDXCSeparator.demix: hop = chunk // overlap; zero padding chunk-hop in front, pad + chunk-hop behind
    overlap = std::clamp(overlap, 1, kChunk);
    const long hop = kChunk / overlap;
    const long pad = hop - (((L - kChunk) % hop) + hop) % hop;  // python's modulo
    const long lead = kChunk - hop;
    const long PL = L + pad + 2 * lead;
    std::vector<float> mix[2];
    for (int c = 0; c < 2; ++c) {
        mix[c].assign(PL, 0.f);
        std::copy(ch[c], ch[c] + L, mix[c].begin() + lead);
    }
    const long nchunks = (PL - kChunk) / hop + 1;  // unfold(1, chunk, hop)
    std::vector<std::vector<float>> acc(kNumStems * 2, std::vector<float>(PL, 0.f));

    const long plane = (long)kDimF * kFrames;
    std::vector<float> in(4 * plane);
    std::array<int64_t, 4> in_shape{1, 4, kDimF, kFrames};
    Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* in_names[] = {"spec"};
    const char* out_names[] = {"out"};
    const int nth = std::max(1, std::min(num_threads(), 2 * kNumStems));
    std::vector<Stft> stft(nth);
    std::vector<std::vector<float>> ychunk(kNumStems * 2, std::vector<float>(kChunk));

    for (long k = 0; k < nchunks; ++k) {
        const long st = k * hop;
        stft[0].forward(mix[0].data() + st, in.data(), in.data() + plane);
        stft[0].forward(mix[1].data() + st, in.data() + 2 * plane, in.data() + 3 * plane);
        std::vector<Ort::Value> outs;
        try {
            Ort::Value x = Ort::Value::CreateTensor<float>(mi, in.data(), in.size(), in_shape.data(), in_shape.size());
            outs = sess->Run(Ort::RunOptions{nullptr}, in_names, &x, 1, out_names, 1);
        } catch (const Ort::Exception& e) {
            throw std::runtime_error(std::string("DrumSep inference failed: ") + e.what());
        }
        auto shape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 5 || shape[1] != kNumStems || shape[2] != 4 || shape[3] != kDimF || shape[4] != kFrames)
            throw std::runtime_error("DrumSep model output has an unexpected shape (re-export it with "
                                     "tools/export_drumsep_onnx.py)");
        const float* o = outs[0].GetTensorData<float>();
        // iSTFT of the 12 (stem, channel) spectrograms on worker threads
        std::atomic<int> nx{0};
        auto job = [&](int wi) {
            std::vector<float> ola;
            for (int j; (j = nx.fetch_add(1)) < kNumStems * 2;) {
                const float* base = o + (long)j * 2 * plane;  // [stem][ch*2 + reim]
                stft[wi].inverse(base, base + plane, ychunk[j].data(), ola);
            }
        };
        std::vector<std::thread> th;
        for (int i = 1; i < nth; ++i) th.emplace_back(job, i);
        job(0);
        for (auto& t : th) t.join();
        for (int j = 0; j < kNumStems * 2; ++j) {
            float* a = acc[j].data() + st;
            const float* y = ychunk[j].data();
            for (long i = 0; i < kChunk; ++i) a[i] += y[i];
        }
        if (p) p((double)(k + 1) / nchunks, "drumsep");
    }
    // accumulated / overlap, then [chunk - hop : -(pad + chunk - hop)]
    for (int s = 0; s < kNumStems; ++s)
        for (int c = 0; c < 2; ++c) {
            Signal& dst = c ? res[s].r : res[s].l;
            const auto& a = acc[s * 2 + c];
            for (long i = 0; i < L; ++i) dst[i] = a[lead + i] / (float)overlap;
        }
    return res;
}

}  // namespace ds::sep

namespace ds {

Stems separate(const Stereo& song, Quality q, const std::string& model_dir, const Progress& p) {
    using namespace sep;
    const fs::path dir = resolve_model_dir(model_dir);
    // check every file first: fail before minutes of work
    std::vector<fs::path> need;
    if (q == Quality::best)
        need = {dir / kDemucsFtDrums, dir / kDemucsFtBass};
    else
        need = {dir / kDemucsFast};
    need.push_back(dir / kDrumSep);
    for (auto& f : need) need_file(f);
    if (!song.r.empty() && song.r.size() != song.l.size())
        throw std::runtime_error("separate: left and right channels differ in length");

    std::lock_guard<std::mutex> lk(g_mutex);
    // keep only this quality's Demucs models in memory
    for (auto it = g_demucs.begin(); it != g_demucs.end();)
        it = (std::find(need.begin(), need.end(), fs::path(it->first)) == need.end()) ? g_demucs.erase(it) : ++it;

    // progress shares (measured on a 4-core CPU: htdemucs ~1x, DrumSep at overlap 2 ~1x, 4 ~2x the Demucs time)
    const int ov = drumsep_overlap(q);
    const double w_demucs = (q == Quality::best ? 2.0 : 1.0), w_sep = 0.5 * ov;
    const double tot = w_demucs + w_sep;
    auto sub = [&](double a, double b, const char* msg) -> Progress {
        if (!p) return {};
        return [&p, a, b, msg](double f, const std::string&) { p((a + f * (b - a)) / tot, msg); };
    };

    Stems out;
    if (q == Quality::best) {
        const double h = w_demucs / 2;
        out.drums = std::move(demucs_run((dir / kDemucsFtDrums).string(), song, {0}, -1,
                                         sub(0, h, "separating the drums (htdemucs_ft)"))[0]);
        out.bass = std::move(demucs_run((dir / kDemucsFtBass).string(), song, {1}, -1,
                                        sub(h, w_demucs, "separating the bass (htdemucs_ft)"))[0]);
    } else {
        auto r = demucs_run((dir / kDemucsFast).string(), song, {0, 1}, -1,
                            sub(0, w_demucs, "separating drums and bass (htdemucs)"));
        out.drums = std::move(r[0]);
        out.bass = std::move(r[1]);
    }
    if (song.r.empty()) out.drums.r.clear(), out.bass.r.clear();  // mono in, mono out

    // DrumSep on the drum stem scaled to 0.5 peak (as the prototype did), scaled back: the kit pieces stay at
    // the drum stem's level, no per-stem normalization; each piece is the mean of its two channels
    float peak = 0;
    for (float v : out.drums.l) peak = std::max(peak, std::fabs(v));
    for (float v : out.drums.r) peak = std::max(peak, std::fabs(v));
    const float gain = peak > 0 ? 0.5f / peak : 1.f;
    Stereo scaled = out.drums;
    for (auto& v : scaled.l) v *= gain;
    for (auto& v : scaled.r) v *= gain;
    auto kit = drumsep_run((dir / kDrumSep).string(), scaled, ov, sub(w_demucs, tot, "splitting the kit (DrumSep)"));
    for (int s = 0; s < kNumStems; ++s) {
        Signal m(kit[s].l.size());
        for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (kit[s].l[i] + kit[s].r[i]) / gain;
        out.kit[kKitStems[s]] = std::move(m);
    }
    if (p) p(1.0, "separation done");
    return out;
}

}  // namespace ds
