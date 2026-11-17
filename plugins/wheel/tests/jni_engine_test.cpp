// The Android engine host (android/wheel/src/main/cpp/wheel_host.cpp, everything behind Native.java except JNI and
// AAudio) on the desktop: tags from bytes, track building + analysis, load, transport, cues, sync, scratch with the
// platter auto-release, settings, rendering in odd block sizes, and track lifetime.
//   <build>/wheel_host_test
#include "../../../android/wheel/src/main/cpp/wheel_host.h"
#include "../Analysis.h"
#include "../SeratoTags.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace wheel;
using namespace wheel::jni;

namespace {
int failures = 0;
void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}
constexpr double kPi = 3.14159265358979323846;

// kick on every beat, hats on the off-beats, a bass line
void groove(std::vector<float>& L, std::vector<float>& R, double bpm, double secs, double sr, double start = 0)
{
    const size_t n = (size_t) (secs * sr);
    L.assign(n, 0); R.assign(n, 0);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1, 1);
    const double bl = 60.0 / bpm;
    double kp = 0;
    for (size_t i = 0; i < n; ++i) {
        const double ts = (double) i / sr - start;
        if (ts < 0) continue;
        const double ph = std::fmod(ts, bl), beat = std::floor(ts / bl);
        kp += 2 * kPi * (50 + 120 * std::exp(-ph * 30)) / sr;
        double v = 0.7 * std::exp(-ph * 9) * std::sin(kp);
        const double hph = std::fmod(ts + bl / 2, bl);
        v += 0.15 * std::exp(-hph * 60) * u(rng) * (hph < 0.08 ? 1 : 0);
        v += 0.2 * std::sin(2 * kPi * (((int) beat % 8 < 4) ? 55.0 : 65.41) * ts);
        L[i] = R[i] = (float) (0.8 * v);
    }
}

void put32(std::vector<uint8_t>& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back((uint8_t) (v >> s)); }
void putSyncsafe(std::vector<uint8_t>& b, uint32_t v) { for (int s = 21; s >= 0; s -= 7) b.push_back((uint8_t) ((v >> s) & 0x7f)); }
void putF(std::vector<uint8_t>& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(b, v); }
std::string b64(const std::vector<uint8_t>& d)
{
    static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < d.size(); i += 3) {
        uint32_t v = (uint32_t) d[i] << 16;
        if (i + 1 < d.size()) v |= (uint32_t) d[i + 1] << 8;
        if (i + 2 < d.size()) v |= d[i + 2];
        s += a[(v >> 18) & 63]; s += a[(v >> 12) & 63];
        s += i + 1 < d.size() ? a[(v >> 6) & 63] : '=';
        s += i + 2 < d.size() ? a[v & 63] : '=';
    }
    return s;
}

// an ID3v2.4 tag (UTF-8 text frames, Serato GEOBs) as the head of an "MP3"
std::vector<uint8_t> id3Tag(double gridBpm, float gridFirst, double cueSec, uint32_t cueRgb, const std::string& cueName)
{
    std::vector<uint8_t> frames;
    auto frame = [&](const char* id, const std::vector<uint8_t>& body) {
        frames.insert(frames.end(), id, id + 4);
        putSyncsafe(frames, (uint32_t) body.size());
        frames.push_back(0); frames.push_back(0);
        frames.insert(frames.end(), body.begin(), body.end());
    };
    auto text = [&](const char* id, const std::string& s) {
        std::vector<uint8_t> b { 3 };
        b.insert(b.end(), s.begin(), s.end());
        frame(id, b);
    };
    auto geob = [&](const std::string& desc, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> b { 0 };
        const std::string mime = "application/octet-stream";
        b.insert(b.end(), mime.begin(), mime.end()); b.push_back(0);
        b.push_back(0);   // file name
        b.insert(b.end(), desc.begin(), desc.end()); b.push_back(0);
        b.insert(b.end(), data.begin(), data.end());
        frame("GEOB", b);
    };
    text("TIT2", "Caf\xC3\xA9 Tagged");
    text("TPE1", "Tag Artist");
    text("TALB", "Tag Album");
    text("TKEY", "8A");
    text("TBPM", "64");
    {
        std::vector<uint8_t> g { 1, 0 };
        put32(g, 1);
        putF(g, gridFirst); putF(g, (float) gridBpm);
        g.push_back(0);
        geob("Serato BeatGrid", g);
    }
    {
        std::vector<uint8_t> raw { 1, 1 };
        const char* cue = "CUE";
        raw.insert(raw.end(), cue, cue + 4);
        std::vector<uint8_t> p { 0, 0 };
        put32(p, (uint32_t) std::lround(cueSec * 1000));
        p.push_back(0);
        p.push_back((uint8_t) (cueRgb >> 16)); p.push_back((uint8_t) (cueRgb >> 8)); p.push_back((uint8_t) cueRgb);
        p.push_back(0); p.push_back(0);
        p.insert(p.end(), cueName.begin(), cueName.end()); p.push_back(0);
        put32(raw, (uint32_t) p.size());
        raw.insert(raw.end(), p.begin(), p.end());
        raw.push_back(0);
        const std::string enc = b64(raw);
        std::vector<uint8_t> m { 1, 1 };
        m.insert(m.end(), enc.begin(), enc.end()); m.push_back(0);
        geob("Serato Markers2", m);
    }
    std::vector<uint8_t> tag { 'I', 'D', '3', 4, 0, 0 };
    putSyncsafe(tag, (uint32_t) frames.size());
    tag.insert(tag.end(), frames.begin(), frames.end());
    for (int i = 0; i < 64; ++i) tag.push_back(0xFF);   // "audio"
    return tag;
}

void render(Host& h, double secs, int block = 480)
{
    const int total = (int) std::lround(secs * h.sampleRate());
    std::vector<float> out((size_t) block * 2);
    for (int done = 0; done < total; done += block) h.renderInterleaved(out.data(), std::min(block, total - done), 2);
}
double st(Host& h, int deck, int k) { double s[ST_COUNT]; h.status(deck, s); return s[k]; }
}  // namespace

int main()
{
    const double sr = 44100;

    // ---- tags from bytes
    const auto tag = id3Tag(128, 0.1f, 1.5, 0xCC0000, "Drop");
    {
        MemorySource src(tag.data(), tag.size());
        const Tags g = readTags(src);
        check(g.title == "Caf\xC3\xA9 Tagged" && g.artist == "Tag Artist" && g.album == "Tag Album", "ID3v2.4 text frames (UTF-8)");
        check(g.hasGrid && std::abs(g.gridBpm - 128) < 1e-6 && std::abs(g.gridFirst - 0.1) < 1e-6 && g.serato, "Serato BeatGrid");
        check(std::abs(g.cues[0].t - 1.5) < 1e-9 && g.cues[0].rgb == 0xCC0000 && g.cues[0].name == "Drop" && ! g.cues[1].set(), "Serato Markers2 cue");
        check(parseKey(g.keyText) == 21 && camelot(21) == "8A" && keyName(21) == "Am" && g.tagBpm == 64, "TKEY / TBPM");
        check(parseKey("F#m") == 18 && parseKey("Gb") == 6 && parseKey("1m") == 21 && parseKey("A minor") == 21 && parseKey("xyz") == -1
                  && parseKey("B\xE2\x99\xAD") == 10, "parseKey");
    }
    {
        std::vector<uint8_t> crate;
        const char* v = "vrsn";
        crate.insert(crate.end(), v, v + 4); put32(crate, 0);
        const std::u16string path = u"Music/é.mp3";
        std::vector<uint8_t> p;
        const char* pt = "ptrk";
        p.insert(p.end(), pt, pt + 4); put32(p, (uint32_t) path.size() * 2);
        for (char16_t c : path) { p.push_back((uint8_t) (c >> 8)); p.push_back((uint8_t) c); }
        const char* ot = "otrk";
        crate.insert(crate.end(), ot, ot + 4); put32(crate, (uint32_t) p.size());
        crate.insert(crate.end(), p.begin(), p.end());
        const auto paths = parseCrate(crate.data(), crate.size());
        check(paths.size() == 1 && paths[0] == "Music/\xC3\xA9.mp3", "parseCrate (UTF-16BE ptrk)");
    }
    {
        std::string t, a;
        nameFromFileName("/sdcard/Music/01 - Some Artist - Some Title.mp3", t, a);
        check(t == "Some Title" && a == "Some Artist", "nameFromFileName");
    }

    // ---- tracks
    std::vector<float> L, R;
    groove(L, R, 128, 30, sr, 0.1);
    MemorySource tagSrc(tag.data(), tag.size());
    auto tA = Host::buildTrack(L.data(), R.data(), L.size(), sr, &tagSrc, "/x/a.mp3", "fallback", "fallback");
    check(tA && tA->title == "Caf\xC3\xA9 Tagged" && tA->bpm == 128 && std::abs(tA->firstBeat - 0.1) < 1e-6 && tA->key == 21 && tA->fromSerato,
          "buildTrack: tags win (grid, key, title)");
    check(tA && tA->wave.peak.size() == (size_t) std::ceil(30.0 * kWaveRate) && tA->wave.low.size() == tA->wave.peak.size(), "buildTrack: waveform");
    groove(L, R, 120, 30, sr, 0.25);
    auto tB = Host::buildTrack(L.data(), nullptr, L.size(), sr, nullptr, "/x/b.wav", "B title", "B artist");
    check(tB && tB->title == "B title" && tB->artist == "B artist" && tB->R.size() == tB->L.size(), "buildTrack: fallback text, mono");
    check(tB && std::abs(tB->bpm - 120) < 0.05, "buildTrack: BPM detected (" + std::to_string(tB ? tB->bpm : 0) + ")");
    check(tB && std::abs(tB->firstBeat - 0.25) < 0.03, "buildTrack: first beat (" + std::to_string(tB ? tB->firstBeat : 0) + ")");
    check(Host::buildTrack(L.data(), R.data(), 0, sr, nullptr, "", "", "") == nullptr && Host::buildTrack(L.data(), R.data(), 10, 0, nullptr, "", "", "") == nullptr,
          "buildTrack refuses empty input");

    static Host h;
    h.prepare(48000);
    double info[kTrackInfo];
    h.trackInfo(0, info);
    check(info[5] == 0 && info[2] == -1, "trackInfo: empty deck");
    check(h.load(0, tA) && h.load(1, tB), "load queued");
    h.trackInfo(0, info);
    const double serA = info[6];
    check(info[0] == 128 && std::abs(info[1] - 0.1) < 1e-6 && info[2] == 21 && info[3] == sr && std::abs(info[4] - 30) < 1e-6 && info[5] == 1,
          "trackInfo: bpm, first beat, key, sr, length");
    {
        double t[kCues]; int32_t rgb[kCues];
        h.cues(0, t, rgb);
        check(std::abs(t[0] - 1.5) < 1e-9 && rgb[0] == 0xCC0000 && t[1] < 0, "cues before the audio thread took the track");
    }
    const auto txt = h.trackText(0);
    check(txt[0] == "Caf\xC3\xA9 Tagged" && txt[1] == "Tag Artist" && txt[2] == "/x/a.mp3", "trackText");
    check(h.wave(0, 3).size() == tA->wave.peak.size() && h.wave(0, 4).empty() && h.wave(1, 0).size() == tB->wave.low.size(), "wave bands");

    // ---- transport
    h.renderInterleaved(std::vector<float>(2 * 1000).data(), 1000, 2);   // > kMaxBlock: split
    check(std::abs(st(h, 0, ST_POS) - 0.1) < 1e-3 && st(h, 0, ST_PLAYING) == 0, "loaded, parked at the first beat");
    h.command(C_PLAY_PAUSE, 0, 0, 0, false);
    render(h, 2.0);
    check(st(h, 0, ST_PLAYING) == 1 && std::abs(st(h, 0, ST_RATE) - 1) < 0.01, "playing at rate 1");
    check(st(h, 0, ST_POS) > 1.6 && st(h, 0, ST_POS) < 2.1, "position advances, after the motor start (" + std::to_string(st(h, 0, ST_POS)) + ")");
    float m[4];
    h.meters(m);
    check(m[0] > 0.05f && m[2] > 0.01f, "meters (deck A, master)");
    check(std::abs(st(h, 0, ST_BPM) - 128) < 0.5, "status bpm");

    // pitch setting (8 % range): +50 % of the fader = +4 %
    h.setValue(deckId(0, D_PITCH), 0.5f);
    render(h, 1.5);
    check(std::abs(st(h, 0, ST_RATE) - 1.04) < 0.005 && h.getValue(deckId(0, D_PITCH)) == 0.5f, "pitch fader * range");
    h.setValue(deckId(0, D_PITCH), 0);
    render(h, 1.0);
    check(h.getValue(999) == 0 && h.getValue(deckId(1, D_XF_ASSIGN)) == 2 && h.getValue(deckId(0, D_RANGE)) == .08f, "defaults / bad id");

    // ---- hot cues
    h.command(C_HOT_CUE, 0, 2, 0, true);    // empty slot while playing: set here
    h.command(C_HOT_CUE, 0, 2, 0, false);
    render(h, 0.05);
    {
        double t[kCues]; int32_t rgb[kCues];
        h.cues(0, t, rgb);
        check(t[2] > 0 && std::abs(t[0] - 1.5) < 1e-9, "hot cue set on the audio thread shows in cues() (" + std::to_string(t[2]) + ")");
        h.command(C_CLEAR_CUE, 0, 2, 0, false);
        render(h, 0.05);
        h.cues(0, t, rgb);
        check(t[2] < 0, "clear cue");
    }
    h.command(C_HOT_CUE, 0, 0, 0, true);    // jump
    h.command(C_HOT_CUE, 0, 0, 0, false);
    render(h, 0.02);
    check(std::abs(st(h, 0, ST_POS) - 1.5) < 60.0 / 128, "hot cue jump (quantized: keeps the beat phase) (" + std::to_string(st(h, 0, ST_POS)) + ")");

    // ---- sync: B (120) follows A (128)
    h.command(C_SYNC, 1, 0, 0, false);
    h.command(C_PLAY_PAUSE, 1, 0, 0, false);
    render(h, 3.0);
    check(st(h, 1, ST_SYNCED) == 1 && std::abs(st(h, 1, ST_BPM) - 128) < 0.3, "sync: B plays at A's tempo (" + std::to_string(st(h, 1, ST_BPM)) + ")");
    {
        // phase: beats line up (positions relative to each first beat, in beats)
        const double pa = (st(h, 0, ST_POS) - 0.1) / (60.0 / 128), pb = (st(h, 1, ST_POS) - tB->firstBeat) / (60.0 / tB->bpm);
        double d = std::fmod(pa - pb, 1.0);
        if (d < -0.5) d += 1;
        if (d > 0.5) d -= 1;
        check(std::abs(d) < 0.03, "sync: phase locked (" + std::to_string(d) + " beats)");
    }
    h.command(C_SYNC, 1, 0, 0, false);   // toggle off: the tempo stays (latched) until the pitch fader moves
    render(h, 0.05);
    check(st(h, 1, ST_SYNCED) == 1 && std::abs(st(h, 1, ST_BPM) - 128) < 0.3, "sync off: tempo latched");
    h.setValue(deckId(1, D_PITCH), 0.1f);
    render(h, 0.05);
    check(st(h, 1, ST_SYNCED) == 0, "sync off: pitch fader takes over");
    h.setValue(deckId(1, D_PITCH), 0);
    h.command(C_PLAY_PAUSE, 1, 0, 0, false);

    // ---- scratch: hand on, move, hold still, auto-release after 2 s
    h.command(C_TOUCH, 0, 0, 0, true);
    render(h, 0.2);
    check(st(h, 0, ST_TOUCHED) == 1 && std::abs(st(h, 0, ST_RATE)) < 0.05, "touch stops the record");
    const double p0 = st(h, 0, ST_POS);
    for (int i = 0; i < 20; ++i) { h.command(C_MOVE, 0, 0, -0.01, false); render(h, 0.01); }
    render(h, 0.1);
    check(st(h, 0, ST_POS) < p0 - 0.15, "moving the hand drags the record back (" + std::to_string(p0 - st(h, 0, ST_POS)) + " s)");
    render(h, 1.5);
    check(st(h, 0, ST_TOUCHED) == 1, "held still: still touched after 1.5 s");
    render(h, 1.5);
    check(st(h, 0, ST_TOUCHED) == 0 && st(h, 0, ST_RATE) > 0.9, "auto-release 2 s after the last touch / move");
    h.command(C_MOVE, 0, 0, 0.05, false);   // a move without touch: lifts by itself
    render(h, 0.02);
    check(st(h, 0, ST_TOUCHED) == 1, "move without touch puts the hand on");
    render(h, 0.3);
    check(st(h, 0, ST_TOUCHED) == 0, "...and lifts it shortly after");
    h.command(C_TOUCH, 0, 0, 0, true);
    render(h, 0.1);
    h.command(C_TOUCH, 0, 0, 0, false);
    render(h, 0.05);
    check(st(h, 0, ST_TOUCHED) == 0, "touch off");

    // ---- loops, censor, grid edits
    h.command(C_AUTO_LOOP, 0, 0, 4, false);
    render(h, 0.05);
    check(st(h, 0, ST_LOOPING) == 1 && std::abs(st(h, 0, ST_LOOP_B) - st(h, 0, ST_LOOP_A) - 4 * 60.0 / 128) < 1e-3, "auto loop 4 beats");
    h.command(C_LOOP_EXIT, 0, 0, 0, false);
    h.command(C_CENSOR, 0, 0, 0, true);
    render(h, 0.2);
    check(st(h, 0, ST_CENSOR) == 1 && st(h, 0, ST_LOOPING) == 0, "loop exit, censor");
    h.command(C_CENSOR, 0, 0, 0, false);
    h.command(C_GRID_SHIFT, 0, 0, 0.01, false);
    h.command(C_GRID_BPM, 0, 0, 127.5, false);
    render(h, 0.05);
    h.trackInfo(0, info);
    check(std::abs(info[0] - 127.5) < 1e-9 && std::abs(info[1] - 0.11) < 1e-6, "grid edits show in trackInfo");
    h.command(C_GRID_BPM, 0, 0, 0, false);
    render(h, 0.05);
    h.trackInfo(0, info);
    check(info[0] == 128, "grid bpm 0 = the track's own");

    // ---- mixer settings
    h.setValue(deckId(0, D_FADER), 0);
    render(h, 3);
    h.meters(m);
    check(m[0] < 1e-3f, "channel fader down silences deck A's meter (" + std::to_string(m[0]) + ")");
    h.setValue(deckId(0, D_FADER), 1);
    h.setValue(deckId(0, D_FX_TYPE), 1); h.setValue(deckId(0, D_FX_ON), 1);
    std::vector<float> buf(2 * 777);
    bool finite = true;
    for (int k = 0; k < 200; ++k) { h.renderInterleaved(buf.data(), 777, 2); for (float v : buf) finite = finite && std::isfinite(v) && std::abs(v) < 4; }
    check(finite, "echo on, odd blocks: output finite and bounded");
    std::vector<float> mono(300);
    h.renderInterleaved(mono.data(), 300, 1);
    std::vector<float> six(6 * 300);
    h.renderInterleaved(six.data(), 300, 6);
    check(six[2] == 0 && six[5] == 0, "1 / 6 channel output");

    // ---- track lifetime: a track goes only when nothing but the keep list holds it
    std::weak_ptr<const Track> wA = tA;
    tA.reset();
    groove(L, R, 100, 5, sr);
    auto tC = Host::buildTrack(L.data(), R.data(), L.size(), sr, nullptr, "/x/c.wav", "C", "");
    auto tD = Host::buildTrack(L.data(), R.data(), L.size(), sr, nullptr, "/x/d.wav", "D", "");
    h.load(0, tC);
    h.trackInfo(0, info);
    check(info[6] > serA, "serial changes on load");
    render(h, 0.05);
    h.collect();
    check(! wA.expired(), "the replaced track is still held (the deck fades it out)");
    h.load(0, tD);
    tC.reset(); tD.reset();
    render(h, 0.05);
    h.collect();
    check(wA.expired(), "freed on the message side once only the keep list held it");
    check(h.keptTracks() == 3, "keep list: B, C (deck's previous), D (" + std::to_string(h.keptTracks()) + ")");

    // ---- offline pump (no audio callback): commands still run
    static Host h2;
    h2.load(0, tB);
    for (int i = 0; i < 3000; ++i) h2.command(C_NUDGE, 0, 0, 0, false);   // more than the queue holds
    h2.pumpOffline();
    double t2[kCues]; int32_t c2[kCues];
    h2.cues(0, t2, c2);
    check(h2.command(C_PLAY, 0, 0, 0, true) && ! h2.command(99, 0, 0, 0, false) && ! h2.command(C_PLAY, 2, 0, 0, true), "queue drains offline; bad commands refused");

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
