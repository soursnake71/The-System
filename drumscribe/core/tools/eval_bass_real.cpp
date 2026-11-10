// eval_bass_real: bass transcription on real recordings without note annotations.
//
//   eval_bass_real <isolated_bass.wav> <demucs_bass.wav> [name] [resynth_out.wav]
//
// (a) agreement between the transcription of the isolated (clean, original multitrack) bass and of the Demucs stem:
//     note P/R/F (onset +-30 ms, same MIDI note), onset / release differences of the matches.
// (b) listen-free sanity checks of the isolated-stem transcription against the isolated signal itself:
//     - an independent reference pitch track (plain YIN, 46 ms window, 5 ms hop, 5-frame median) vs the transcribed
//       pitch (note + cents + bends) per frame: share of voiced frames covered by a note, within 50 cents, octave off;
//       share of note frames where the reference is silent;
//     - energy-onset peaks (log-energy rise, 10 ms windows) vs transcribed onsets (+-30 ms);
//     - slides, notes with bends, bend points, legato share (end == next onset), note lengths, accents;
//     - re-synthesis of the notes (harmonic tone with the transcribed pitch, bends and velocity) vs the original:
//       correlation of their low-band log-spectrograms.
// Prints one summary line per song (CSV-ish, prefixed "SUMMARY") plus details.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "ds/modules.h"

using namespace ds;

static Signal read_wav_mono(const std::string& path, int* srp) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
        throw std::runtime_error(path + ": not a WAV file");
    int fmt = 0, nch = 0, bits = 0, sr = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        uint32_t sz;
        std::memcpy(&sz, d.data() + pos + 4, 4);
        const char* id = d.data() + pos;
        const char* body = d.data() + pos + 8;
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t v;
            std::memcpy(&v, body, 2), fmt = v;
            std::memcpy(&v, body + 2, 2), nch = v;
            std::memcpy(&sr, body + 4, 4);
            std::memcpy(&v, body + 14, 2), bits = v;
            if (fmt == 0xFFFE) std::memcpy(&v, body + 24, 2), fmt = v;
        } else if (!std::memcmp(id, "data", 4)) {
            const int bps = bits / 8;
            size_t n = std::min<size_t>(sz, d.size() - pos - 8) / (bps * nch);
            Signal s(n);
            for (size_t i = 0; i < n; ++i) {
                double acc = 0;
                for (int c = 0; c < nch; ++c) {
                    const char* q = body + (i * nch + c) * bps;
                    float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy(&v, q, 4);
                    else if (fmt == 1 && bits == 16) { int16_t x; std::memcpy(&x, q, 2); v = x / 32768.f; }
                    else if (fmt == 1 && bits == 24) {
                        int32_t x = (uint8_t(q[0]) << 8) | (uint8_t(q[1]) << 16) | (uint8_t(q[2]) << 24);
                        v = (x >> 8) / 8388608.f;
                    } else if (fmt == 1 && bits == 32) { int32_t x; std::memcpy(&x, q, 4); v = x / 2147483648.f; }
                    else throw std::runtime_error(path + ": unsupported sample format");
                    acc += v;
                }
                s[i] = float(acc / nch);
            }
            *srp = sr;
            return s;
        }
        pos += 8 + sz + (sz & 1);
    }
    throw std::runtime_error(path + ": no data");
}

static void write_wav(const std::string& path, const Signal& x, int sr) {
    std::ofstream f(path, std::ios::binary);
    uint32_t n = uint32_t(x.size()), bytes = n * 4, riff = 36 + bytes, fsz = 16, rate = sr, br = sr * 4;
    uint16_t fmt = 3, ch = 1, ba = 4, bits = 32;
    f.write("RIFF", 4), f.write((char*)&riff, 4), f.write("WAVEfmt ", 8), f.write((char*)&fsz, 4);
    f.write((char*)&fmt, 2), f.write((char*)&ch, 2), f.write((char*)&rate, 4), f.write((char*)&br, 4);
    f.write((char*)&ba, 2), f.write((char*)&bits, 2), f.write("data", 4), f.write((char*)&bytes, 4);
    f.write((const char*)x.data(), bytes);
}

static double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}
static double pctl(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * (v.size() - 1) + 0.5))];
}

// ---- reference pitch: plain YIN on a 4x decimated signal (independent of the engine's tracker)
struct RefPitch { double hop; std::vector<double> midi, edb; };  // NaN = unvoiced; edb = frame level rel. loud part
static RefPitch ref_pitch(const Signal& x, int sr) {
    const int D = 4;
    const double srd = double(sr) / D;
    // decimate with a simple windowed-sinc low-pass
    const int half = 32;
    std::vector<double> h(2 * half + 1);
    double hs = 0;
    for (int j = -half; j <= half; ++j) {
        double fc = 0.45 / D, t = j;
        double v = t == 0 ? 2 * fc : std::sin(2 * M_PI * fc * t) / (M_PI * t);
        v *= 0.54 + 0.46 * std::cos(M_PI * j / half);
        h[j + half] = v, hs += v;
    }
    std::vector<double> y(x.size() / D);
    for (size_t m = 0; m < y.size(); ++m) {
        double a = 0;
        for (int j = -half; j <= half; ++j) {
            long i = long(m) * D + j;
            if (i >= 0 && i < long(x.size())) a += h[j + half] * x[i];
        }
        y[m] = a / hs;
    }
    const int W = int(0.046 * srd), T = int(srd / 30), hop = int(0.005 * srd);
    RefPitch r;
    r.hop = double(hop) / srd;
    std::vector<double> pw;
    for (size_t s0 = 0; s0 + W + T < y.size(); s0 += hop) {
        double e = 0;
        for (int j = 0; j < W; ++j) e += y[s0 + j] * y[s0 + j];
        pw.push_back(e / W);
    }
    double ref = pctl(pw, 0.99);
    for (double p : pw) r.edb.push_back(10 * std::log10(p / ref + 1e-12));
    std::vector<double> d(T + 1), dp(T + 1);
    size_t fi = 0;
    for (size_t s0 = 0; s0 + W + T < y.size(); s0 += hop, ++fi) {
        if (pw[fi] < ref * 1e-4) { r.midi.push_back(NAN); continue; }  // -40 dB
        for (int t = 1; t <= T; ++t) {
            double a = 0;
            for (int j = 0; j < W; ++j) { double v = y[s0 + j] - y[s0 + j + t]; a += v * v; }
            d[t] = a;
        }
        double run = 0;
        for (int t = 1; t <= T; ++t) { run += d[t]; dp[t] = run > 0 ? d[t] * t / run : 1; }
        int tb = -1;
        for (int t = int(srd / 500); t < T; ++t)
            if (dp[t] < 0.15 && dp[t] <= dp[t + 1]) { tb = t; break; }
        if (tb < 0) { r.midi.push_back(NAN); continue; }
        double den = dp[tb - 1] - 2 * dp[tb] + dp[tb + 1];
        double tf = tb + (den > 0 ? 0.5 * (dp[tb - 1] - dp[tb + 1]) / den : 0);
        r.midi.push_back(69 + 12 * std::log2(srd / tf / 440));
    }
    // 5-frame median on voiced runs
    std::vector<double> m2 = r.midi;
    for (size_t i = 0; i < r.midi.size(); ++i) {
        if (std::isnan(r.midi[i])) continue;
        std::vector<double> v;
        for (size_t j = i >= 2 ? i - 2 : 0; j <= std::min(r.midi.size() - 1, i + 2); ++j)
            if (!std::isnan(r.midi[j])) v.push_back(r.midi[j]);
        m2[i] = median(v);
    }
    r.midi = m2;
    return r;
}

// transcribed pitch at time t (NaN if no note sounds)
static double note_pitch_at(const std::vector<BassNote>& ns, double t) {
    for (const auto& n : ns) {
        if (t < n.time || t >= n.end) continue;
        double p = n.note + n.cents / 100;
        double b = 0;
        for (size_t i = 0; i < n.bends.size(); ++i) {
            if (n.bends[i].time > t) break;
            b = n.bends[i].semitones;
        }
        return p + b;
    }
    return NAN;
}

static std::vector<double> energy_onsets(const Signal& x, int sr) {
    const int w = sr / 100, hop = sr / 200;
    std::vector<double> e;
    for (size_t i = 0; i + w < x.size(); i += hop) {
        double a = 0;
        for (int j = 0; j < w; ++j) a += double(x[i + j]) * x[i + j];
        e.push_back(10 * std::log10(a / w + 1e-12));
    }
    double ref = pctl(e, 0.99);
    std::vector<double> on;
    for (size_t k = 4; k + 4 < e.size(); ++k) {
        double rise = e[k + 2] - e[k - 3];  // over 25 ms
        if (rise < 6 || e[k + 2] < ref - 35) continue;
        bool mx = true;
        for (size_t j = k - 3; j <= k + 3; ++j)
            if (j != k && e[j + 2] - e[j - 3] > rise) mx = false;
        if (!mx) continue;
        if (!on.empty() && k * double(hop) / sr - on.back() < 0.06) continue;
        on.push_back(double(k) * hop / sr);
    }
    return on;
}

static Signal resynth(const std::vector<BassNote>& ns, size_t N, int sr) {
    Signal y(N, 0.f);
    for (const auto& n : ns) {
        long a = long(n.time * sr), b = std::min<long>(long(N), long(n.end * sr) + sr / 100);
        double ph = 0, amp = std::pow(10.0, (n.velocity - 127) * 45.0 / 126 / 20);
        for (long i = a; i < b; ++i) {
            double t = double(i) / sr, bend = 0;
            for (auto& q : n.bends) { if (q.time > t) break; bend = q.semitones; }
            double f = 440 * std::pow(2.0, (n.note + n.cents / 100 + bend - 69) / 12);
            ph += 2 * M_PI * f / sr;
            double env = std::min(1.0, (i - a) / (0.003 * sr)) * (i > long(n.end * sr) ? std::exp(-(t - n.end) / 0.003) : 1);
            double v = 0;
            for (int k = 1; k <= 8 && k * f < 2000; ++k) v += std::sin(k * ph) / k;
            y[i] += float(0.3 * amp * env * v);
        }
    }
    return y;
}

// low-band (40-400 Hz) log-magnitude spectrogram correlation, 4096-point frames (naive DFT on the band bins only)
static double spec_corr(const Signal& a, const Signal& b, int sr) {
    const int N = 4096, hop = 2048;
    const int k0 = int(40.0 * N / sr), k1 = int(400.0 * N / sr);
    std::vector<double> A, B;
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1));
    for (size_t s = 0; s + N < std::min(a.size(), b.size()); s += hop) {
        for (int pass = 0; pass < 2; ++pass) {
            const Signal& x = pass ? b : a;
            for (int k = k0; k <= k1; ++k) {
                double re = 0, im = 0, w = 2 * M_PI * k / N;
                for (int i = 0; i < N; i += 2) {  // every 2nd sample is plenty below 400 Hz
                    re += win[i] * x[s + i] * std::cos(w * i);
                    im -= win[i] * x[s + i] * std::sin(w * i);
                }
                (pass ? B : A).push_back(std::log10(re * re + im * im + 1e-6));
            }
        }
    }
    double ma = 0, mb = 0;
    for (size_t i = 0; i < A.size(); ++i) ma += A[i], mb += B[i];
    ma /= A.size(), mb /= B.size();
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < A.size(); ++i) {
        sab += (A[i] - ma) * (B[i] - mb), saa += (A[i] - ma) * (A[i] - ma), sbb += (B[i] - mb) * (B[i] - mb);
    }
    return sab / std::sqrt(saa * sbb + 1e-30);
}

struct Match { int tp = 0; std::vector<double> don, doff; };
static Match match(const std::vector<BassNote>& ref, const std::vector<BassNote>& est) {
    Match m;
    std::vector<char> used(est.size(), 0);
    for (const auto& r : ref) {
        int bj = -1;
        double bd = 0.030;
        for (size_t j = 0; j < est.size(); ++j)
            if (!used[j] && est[j].note == r.note && std::fabs(est[j].time - r.time) <= bd)
                bd = std::fabs(est[j].time - r.time), bj = int(j);
        if (bj < 0) continue;
        used[bj] = 1;
        ++m.tp;
        m.don.push_back(1e3 * std::fabs(est[bj].time - r.time));
        m.doff.push_back(1e3 * std::fabs(est[bj].end - r.end));
    }
    return m;
}

struct Stats { int notes, slides, bent, bendpts, accents; double legato, len_med, len_p10, len_p90; };
static Stats stats(const std::vector<BassNote>& ns) {
    Stats s{int(ns.size()), 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<double> len;
    int leg = 0;
    for (size_t i = 0; i < ns.size(); ++i) {
        s.slides += ns[i].slide_to_next;
        s.bent += !ns[i].bends.empty();
        s.bendpts += int(ns[i].bends.size());
        s.accents += ns[i].accent;
        len.push_back(1e3 * (ns[i].end - ns[i].time));
        if (i + 1 < ns.size() && std::fabs(ns[i].end - ns[i + 1].time) < 0.002) ++leg;
    }
    s.legato = ns.size() > 1 ? double(leg) / (ns.size() - 1) : 0;
    s.len_med = pctl(len, 0.5), s.len_p10 = pctl(len, 0.1), s.len_p90 = pctl(len, 0.9);
    return s;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <isolated_bass.wav> <demucs_bass.wav> [name] [resynth_out.wav]\n", argv[0]);
        return 2;
    }
    const std::string name = argc > 3 ? argv[3] : argv[1];
    int sri = 0, srd = 0;
    Signal iso = read_wav_mono(argv[1], &sri), dem = read_wav_mono(argv[2], &srd);
    auto ni = transcribe_bass(iso, sri), nd = transcribe_bass(dem, srd);
    // (a) agreement
    Match m = match(ni, nd);
    double P = nd.empty() ? 0 : double(m.tp) / nd.size(), R = ni.empty() ? 0 : double(m.tp) / ni.size();
    double F = P + R > 0 ? 2 * P * R / (P + R) : 0;
    // (b) pitch vs the reference track of the isolated stem
    RefPitch rp = ref_pitch(iso, sri);
    int voiced = 0, covered = 0, within = 0, octave = 0, noteframes = 0, silentnote = 0, gapvoiced = 0;
    for (size_t k = 0; k < rp.midi.size(); ++k) {
        double t = (k + 0.5) * rp.hop + 0.023;  // frame centre
        double tp = note_pitch_at(ni, t);
        bool v = !std::isnan(rp.midi[k]);
        // a note sounding where the stem is silent (> 30 dB below its loud part): a release that comes too late
        if (!std::isnan(tp)) { ++noteframes; if (rp.edb[k] < -30) ++silentnote; }
        if (std::isnan(tp) && rp.edb[k] > -20 && v) ++gapvoiced;
        if (!v) continue;
        ++voiced;
        if (std::isnan(tp)) continue;
        ++covered;
        double d = tp - rp.midi[k];
        if (std::fabs(d) <= 0.5) ++within;
        else if (std::fabs(std::fabs(d) - 12) <= 0.5 || std::fabs(std::fabs(d) - 24) <= 0.5 || std::fabs(std::fabs(d) - 19.02) <= 0.5) ++octave;
    }
    // onsets vs energy onsets
    auto eo = energy_onsets(iso, sri);
    int ohit = 0;
    for (double t : eo)
        for (auto& n : ni)
            if (std::fabs(n.time - t) < 0.03) { ++ohit; break; }
    int nhit = 0;
    for (auto& n : ni)
        for (double t : eo)
            if (std::fabs(n.time - t) < 0.03) { ++nhit; break; }
    Signal rs = resynth(ni, iso.size(), sri);
    if (argc > 4) write_wav(argv[4], rs, sri);
    double sc = spec_corr(iso, rs, sri);
    Stats si = stats(ni), sd = stats(nd);
    std::printf("== %s\n", name.c_str());
    std::printf("  agreement iso vs demucs: iso %d demucs %d match %d  P %.3f R %.3f F %.3f  onset MAE %.1f ms  end MAE %.1f ms\n",
                int(ni.size()), int(nd.size()), m.tp, P, R, F, m.don.empty() ? 0 : std::accumulate(m.don.begin(), m.don.end(), 0.0) / m.don.size(),
                m.doff.empty() ? 0 : std::accumulate(m.doff.begin(), m.doff.end(), 0.0) / m.doff.size());
    std::printf("  iso pitch vs ref YIN: voiced frames %d, covered %.3f, within 50c %.3f (of covered), octave %.3f; note frames over silence (<-30 dB) %.3f, loud voiced frames without a note %d\n",
                voiced, voiced ? double(covered) / voiced : 0, covered ? double(within) / covered : 0,
                covered ? double(octave) / covered : 0, noteframes ? double(silentnote) / noteframes : 0, gapvoiced);
    std::printf("  energy onsets %d: found by notes %.3f; notes on an energy onset %.3f\n", int(eo.size()),
                eo.empty() ? 0 : double(ohit) / eo.size(), ni.empty() ? 0 : double(nhit) / ni.size());
    for (int pass = 0; pass < 2; ++pass) {
        const Stats& s = pass ? sd : si;
        std::printf("  %s: notes %d slides %d bent %d (pts %d) accents %d legato %.2f len ms p10/med/p90 %.0f/%.0f/%.0f\n",
                    pass ? "demucs" : "iso   ", s.notes, s.slides, s.bent, s.bendpts, s.accents, s.legato, s.len_p10,
                    s.len_med, s.len_p90);
    }
    std::printf("  resynth low-band log-spectrogram corr %.3f\n", sc);
    std::printf("SUMMARY %s notes_iso=%d notes_dem=%d F=%.3f within50=%.3f octave=%.3f slides_iso=%d slides_dem=%d bent_iso=%d bent_dem=%d legato_iso=%.2f legato_dem=%.2f silent=%.3f acc_iso=%d acc_dem=%d onsetP=%.2f onsetR=%.2f corr=%.3f\n",
                name.c_str(), int(ni.size()), int(nd.size()), F, covered ? double(within) / covered : 0,
                covered ? double(octave) / covered : 0, si.slides, sd.slides, si.bent, sd.bent, si.legato, sd.legato, noteframes ? double(silentnote) / noteframes : 0,
                si.accents, sd.accents, ni.empty() ? 0 : double(nhit) / ni.size(), eo.empty() ? 0 : double(ohit) / eo.size(), sc);
    if (std::getenv("EVAL_DUMP")) {
        for (int pass = 0; pass < 2; ++pass) {
            std::printf("  -- %s notes\n", pass ? "demucs" : "iso");
            for (auto& n : pass ? nd : ni) {
                std::vector<double> v;
                for (size_t k = 0; k < rp.midi.size(); ++k) {
                    double t = (k + 0.5) * rp.hop + 0.023;
                    if (t >= n.time && t < n.end && !std::isnan(rp.midi[k])) v.push_back(rp.midi[k]);
                }
                std::printf("   %7.3f-%7.3f n=%d c=%+.0f v=%d%s%s bends=%zu  ref %.2f (%zu fr)\n", n.time, n.end, n.note,
                            n.cents, n.velocity, n.slide_to_next ? " SLIDE" : "", n.accent ? " ACC" : "", n.bends.size(),
                            v.empty() ? 0.0 : median(v), v.size());
            }
        }
    }
    return 0;
}
