// Omega Wheel of Steel on Android: the engine host (see wheel_host.h). The audio-thread logic mirrors
// plugins/wheel/WheelPlugin.cpp (exec, applySettings, platter auto-release, sync master, publishCues, processBlock).
#include "wheel_host.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include "Analysis.h"
#include "SeratoTags.h"

namespace wheel {

using namespace jni;

namespace {
template <typename T> T clampv(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }
bool same(double a, double b) { return std::abs(a - b) < 1e-9; }
constexpr auto rl = std::memory_order_relaxed;
}  // namespace

Host::Host()
{
    for (auto& v : vals) v.store(0, rl);
    for (int dk = 0; dk < 2; ++dk) {
        const DeckSettings ds;
        const ChannelSettings cs;
        const FxSettings fs;
        vals[deckId(dk, D_PITCH)].store(ds.pitch);
        vals[deckId(dk, D_RANGE)].store(ds.pitchRange);
        vals[deckId(dk, D_QUANTIZE)].store(ds.quantize ? 1.f : 0.f);
        vals[deckId(dk, D_START_TIME)].store(ds.startTime);
        vals[deckId(dk, D_BRAKE_TIME)].store(ds.brakeTime);
        vals[deckId(dk, D_TRIM)].store(cs.trim);
        vals[deckId(dk, D_FADER)].store(cs.fader);
        vals[deckId(dk, D_XF_ASSIGN)].store(dk == 0 ? 0.f : 2.f);
        vals[deckId(dk, D_FX_BEATS)].store(fs.beats);
        vals[deckId(dk, D_FX_DEPTH)].store(fs.depth);
        vals[deckId(dk, D_FX_PARAM)].store(fs.param);
        gridShift[dk].store(0);
        gridBpm[dk].store(0);
        for (int i = 0; i < kCues; ++i) { snap[dk].t[i].store(-1); snap[dk].rgb[i].store(0); pubT[dk][i] = -1; pubRgb[dk][i] = 0; }
    }
    const MixerSettings m;
    vals[globalId(G_XFADER)].store(m.xfader);
    vals[globalId(G_XF_CURVE)].store(m.xfCurve);
    vals[globalId(G_FADER_CURVE)].store(m.faderCurve);
    vals[globalId(G_MASTER)].store(m.master);
    vals[globalId(G_CUE_MIX)].store(m.cueMix);
    vals[globalId(G_CUE_GAIN)].store(m.cueGain);
    prepare(48000);
}

Host::~Host() = default;

void Host::prepare(double sampleRate)
{
    const double s = sampleRate > 0 ? sampleRate : 48000;
    sr.store(s);
    for (auto& b : buf) b.assign((size_t) kMaxBlock, 0.f);
    for (int dk = 0; dk < 2; ++dk) { deck[dk].prepare(s, kMaxBlock); fx[dk].prepare(s, kMaxBlock); }
    mixer.prepare(s, kMaxBlock);
}

//==============================================================================================================
// command ring

bool Host::push(Cmd&& c)
{
    while (pushLock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    const size_t t = tail.load(rl), h = head.load(std::memory_order_acquire);
    const bool ok = t - h < kQueue;
    if (ok) {
        q[t % kQueue] = std::move(c);
        tail.store(t + 1, std::memory_order_release);
    }
    pushLock.clear(std::memory_order_release);
    return ok;
}

void Host::drain()
{
    size_t h = head.load(rl);
    const size_t t = tail.load(std::memory_order_acquire);
    for (; h != t; ++h) {
        const Cmd c = std::move(q[h % kQueue]);   // the slot is left empty: a track's last reference is never in the ring
        head.store(h + 1, std::memory_order_release);
        exec(c);
    }
}

bool Host::command(int type, int dk, int i, double v, bool on)
{
    if (type < 0 || type >= C_COUNT || dk < 0 || dk > 1 || ! std::isfinite(v)) return false;
    Cmd c;
    c.type = type; c.deck = dk; c.i = i; c.v = v; c.on = on;
    return push(std::move(c));
}

void Host::exec(const Cmd& c)
{
    if (c.deck < 0 || c.deck > 1) return;
    const int dk = c.deck, slot = clampv(c.i, 0, kCues - 1);
    auto& d = deck[dk];
    switch (c.type) {
        case kLoad:
            d.load(c.track); fx[dk].reset();
            gridShift[dk].store(0); gridBpm[dk].store(0);
            handOn[dk] = touchHeld[dk] = false;
            break;
        case C_PLAY_PAUSE: d.playPause(); break;
        case C_PLAY: d.play(c.on); break;
        case C_CUE: d.cue(c.on); break;
        case C_HOT_CUE: d.hotCue(slot, c.on); break;
        case C_CLEAR_CUE: d.clearCue(slot); break;
        case C_AUTO_LOOP: d.autoLoop(c.v); break;
        case C_LOOP_IN: d.loopIn(); break;
        case C_LOOP_OUT: d.loopOut(); break;
        case C_RELOOP: d.reloop(); break;
        case C_LOOP_EXIT: d.loopExit(); break;
        case C_LOOP_HALVE: d.loopHalve(); break;
        case C_LOOP_DOUBLE: d.loopDouble(); break;
        case C_LOOP_MOVE: d.loopMove(c.v); break;
        case C_ROLL: d.roll(c.v, c.on); break;
        case C_SAVED_LOOP: d.savedLoop(slot); break;
        case C_TOUCH:
            touchHeld[dk] = c.on;
            if (c.on) {
                if (! handOn[dk]) { d.touch(true); handOn[dk] = true; }
                lastHand[dk] = now;
            } else if (handOn[dk]) { d.touch(false); handOn[dk] = false; }
            break;
        case C_MOVE:
            if (! handOn[dk]) { d.touch(true); handOn[dk] = true; }
            d.move(c.v);
            lastHand[dk] = now;
            break;
        case C_BEND: d.bend(c.v); break;
        case C_NUDGE: d.nudge((float) clampv(c.v, -1.0, 1.0)); break;
        case C_CENSOR: d.censor(c.on); break;
        case C_SEEK: d.seek(c.v); break;
        case C_SYNC:
            if (c.on || ! d.syncing()) { d.setSync(true); syncNow(dk, true); }
            else d.setSync(false);
            break;
        case C_SYNC_OFF: d.setSync(false); break;
        case C_GRID_SHIFT: d.gridShift(c.v); gridShift[dk].store(gridShift[dk].load(rl) + c.v); break;
        case C_GRID_BPM:
            if (c.v >= 0 && c.v < 1000) { d.gridBpm(c.v); gridBpm[dk].store(c.v > 0 ? c.v : 0.0); }
            break;
        default: break;
    }
}

//==============================================================================================================
// settings

void Host::setValue(int id, float v)
{
    if (id < 0 || id >= kIds || ! std::isfinite(v)) return;
    vals[id].store(v, rl);
}

float Host::getValue(int id) const { return id >= 0 && id < kIds ? vals[id].load(rl) : 0.f; }

void Host::applySettings()
{
    for (int dk = 0; dk < 2; ++dk) {
        const auto V = [&](int k) { return vals[deckId(dk, k)].load(rl); };
        const auto B = [&](int k) { return V(k) > .5f; };
        auto& s = deck[dk].set;
        s.pitch = clampv(V(D_PITCH), -1.f, 1.f);
        s.pitchRange = clampv(V(D_RANGE), .01f, 1.f);
        s.rpm45 = B(D_RPM45); s.keyLock = B(D_KEYLOCK); s.slip = B(D_SLIP); s.quantize = B(D_QUANTIZE);
        s.startTime = clampv(V(D_START_TIME), .05f, 4.f);
        s.brakeTime = clampv(V(D_BRAKE_TIME), .05f, 6.f);
        auto& c = ms.ch[dk];
        c.trim = clampv(V(D_TRIM), -12.f, 12.f);
        c.hi = clampv(V(D_HI), -26.f, 6.f); c.mid = clampv(V(D_MID), -26.f, 6.f); c.low = clampv(V(D_LOW), -26.f, 6.f);
        c.filter = clampv(V(D_FILTER), -1.f, 1.f);
        c.fader = clampv(V(D_FADER), 0.f, 1.f);
        c.pfl = B(D_PFL);
        c.xfAssign = clampv((int) std::lround(V(D_XF_ASSIGN)), 0, 2);
        auto& f = fxs[dk];
        f.type = (FxType) clampv((int) std::lround(V(D_FX_TYPE)), 0, (int) FxType::count - 1);
        f.beats = clampv(V(D_FX_BEATS), .0625f, 8.f);
        f.depth = clampv(V(D_FX_DEPTH), 0.f, 1.f);
        f.param = clampv(V(D_FX_PARAM), 0.f, 1.f);
        f.on = B(D_FX_ON) && f.type != FxType::off;
    }
    const auto G = [&](int k) { return vals[globalId(k)].load(rl); };
    ms.xfader = clampv(G(G_XFADER), -1.f, 1.f);
    ms.xfCurve = clampv(G(G_XF_CURVE), 0.f, 1.f);
    ms.faderCurve = clampv(G(G_FADER_CURVE), 0.f, 1.f);
    ms.hamster = G(G_HAMSTER) > .5f;
    ms.master = clampv(G(G_MASTER), -24.f, 6.f);
    ms.cueMix = clampv(G(G_CUE_MIX), 0.f, 1.f);
    ms.cueGain = clampv(G(G_CUE_GAIN), -24.f, 12.f);
}

//==============================================================================================================
// platter safety, sync

void Host::handlePlatters()
{
    const double s = sr.load(rl);
    for (int dk = 0; dk < 2; ++dk) {
        if (! handOn[dk]) continue;
        const double limit = (touchHeld[dk] ? kAutoRelease : kMoveRelease) * s;
        if ((double) (now - lastHand[dk]) > limit) { deck[dk].touch(false); handOn[dk] = touchHeld[dk] = false; }
    }
}

// the other deck when it plays (Serato-style master): a synced deck follows it
bool Host::master(int dk, double& bpm, double& phase) const
{
    const auto& o = deck[1 - dk];
    if (o.track() != nullptr && o.st.playing.load() && o.currentBpm() > 0
        && (! o.syncing() || dk == 1 || ! deck[dk].st.playing.load())) {
        bpm = o.currentBpm(); phase = o.beatPhase(); return true;
    }
    return false;
}

void Host::syncNow(int dk, bool phaseToo)
{
    double bpm = 0, ph = -1;
    if (master(dk, bpm, ph)) deck[dk].syncTo(bpm, ph < 0 ? 0 : ph, phaseToo && ph >= 0);
}

void Host::followSync()
{
    for (int dk = 0; dk < 2; ++dk) {
        auto& d = deck[dk];
        const bool playing = d.st.playing.load(), touched = d.st.touched.load();
        const double rate = d.st.rate.load(rl);
        bool phase = playing && ! wasPlaying[dk];   // phase-lock on start
        // and once more when the motor has spun up (start time > 0: the record lags while it gets to speed)
        if (phase) relock[dk] = true;
        else if (relock[dk] && rate > 0.2 && std::abs(rate - lastRate[dk]) < 1e-4) { phase = true; relock[dk] = false; }
        if (! playing || ! d.syncing()) relock[dk] = false;
        if (d.syncing() && ! touched) syncNow(dk, phase);
        wasPlaying[dk] = playing;
        lastRate[dk] = rate;
    }
}

//==============================================================================================================
// cues snapshot

void Host::publishCues()
{
    for (int dk = 0; dk < 2; ++dk) {
        const auto& d = deck[dk];
        const void* tr = d.track();
        bool changed = tr != pubTrk[dk];
        for (int i = 0; i < kCues && ! changed; ++i) changed = ! same(d.cues[(size_t) i].t, pubT[dk][i]) || d.cues[(size_t) i].rgb != pubRgb[dk][i];
        if (! changed) continue;
        auto& s = snap[dk];
        const uint32_t q0 = s.seq.load(rl);
        s.seq.store(q0 + 1, rl);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < kCues; ++i) {
            pubT[dk][i] = d.cues[(size_t) i].t; pubRgb[dk][i] = d.cues[(size_t) i].rgb;
            s.t[i].store(pubT[dk][i], rl); s.rgb[i].store(pubRgb[dk][i], rl);
        }
        s.trk.store(tr, rl);
        pubTrk[dk] = tr;
        s.seq.store(q0 + 2, std::memory_order_release);
    }
}

void Host::cues(int dk, double* t, int32_t* rgb)
{
    for (int i = 0; i < kCues; ++i) { t[i] = -1; rgb[i] = 0; }
    if (dk < 0 || dk > 1) return;
    const auto trk = currentTrack(dk);
    if (trk == nullptr) return;
    auto& s = snap[dk];
    double tt[kCues] {};
    uint32_t cc[kCues] {};
    const void* who = nullptr;
    for (int tries = 0; tries < 64; ++tries) {
        const uint32_t s1 = s.seq.load(std::memory_order_acquire);
        if (s1 & 1u) { std::this_thread::yield(); continue; }
        for (int i = 0; i < kCues; ++i) { tt[i] = s.t[i].load(rl); cc[i] = s.rgb[i].load(rl); }
        who = s.trk.load(rl);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(rl) == s1) break;
        who = nullptr;
    }
    if (who != trk.get()) {   // the audio thread hasn't taken this track yet
        for (int i = 0; i < kCues; ++i) { t[i] = trk->cues[(size_t) i].t; rgb[i] = (int32_t) (trk->cues[(size_t) i].rgb & 0xFFFFFF); }
        return;
    }
    for (int i = 0; i < kCues; ++i) { t[i] = tt[i] >= 0 ? tt[i] : -1; rgb[i] = (int32_t) (cc[i] & 0xFFFFFF); }
}

//==============================================================================================================
// render

void Host::process(int n)
{
    drain();
    applySettings();
    handlePlatters();
    followSync();
    float* aL = buf[0].data(); float* aR = buf[1].data();
    float* bL = buf[2].data(); float* bR = buf[3].data();
    float* oL = buf[4].data(); float* oR = buf[5].data();
    double fxBpm[2];
    for (int dk = 0; dk < 2; ++dk) {
        const double b = deck[dk].currentBpm();
        fxBpm[dk] = b > 0 ? b : 120;
        // beat-locked FX (flanger / phaser / gate) follow the grid
        const auto* t = deck[dk].track();
        const double bl = deck[dk].beatLength();
        if (t != nullptr && bl > 0) fx[dk].setBeatPos((deck[dk].st.pos.load(rl) - (t->firstBeat + gridShift[dk].load(rl))) / bl);
    }
    deck[0].render(aL, aR, n); deck[1].render(bL, bR, n);
    fx[0].process(aL, aR, n, fxs[0], fxBpm[0]); fx[1].process(bL, bR, n, fxs[1], fxBpm[1]);
    mixer.process(aL, aR, bL, bR, oL, oR, nullptr, nullptr, n, ms);
    now += n;
    publishCues();
}

void Host::render(float* L, float* R, int n)
{
    for (int done = 0; done < n;) {
        const int m = std::min(kMaxBlock, n - done);
        process(m);
        std::memcpy(L + done, buf[4].data(), sizeof(float) * (size_t) m);
        std::memcpy(R + done, buf[5].data(), sizeof(float) * (size_t) m);
        done += m;
    }
}

void Host::renderInterleaved(float* out, int frames, int channels)
{
    if (channels <= 0) return;
    for (int done = 0; done < frames;) {
        const int m = std::min(kMaxBlock, frames - done);
        process(m);
        const float* L = buf[4].data();
        const float* R = buf[5].data();
        float* o = out + (size_t) done * (size_t) channels;
        if (channels == 2) {
            for (int i = 0; i < m; ++i) { o[2 * i] = L[i]; o[2 * i + 1] = R[i]; }
        } else if (channels == 1) {
            for (int i = 0; i < m; ++i) o[i] = 0.5f * (L[i] + R[i]);
        } else {
            for (int i = 0; i < m; ++i) {
                float* f = o + (size_t) i * (size_t) channels;
                f[0] = L[i]; f[1] = R[i];
                for (int c = 2; c < channels; ++c) f[c] = 0;
            }
        }
        done += m;
    }
}

void Host::pumpOffline()
{
    drain();
    publishCues();
}

//==============================================================================================================
// readers

void Host::status(int dk, double* out) const
{
    for (int i = 0; i < ST_COUNT; ++i) out[i] = 0;
    if (dk < 0 || dk > 1) return;
    const auto& st = deck[dk].st;
    out[ST_POS] = st.pos.load(rl);
    out[ST_RATE] = st.rate.load(rl);
    out[ST_ANGLE] = st.angle.load(rl);
    out[ST_BPM] = st.bpmNow.load(rl);
    out[ST_PLAYING] = st.playing.load(rl) ? 1 : 0;
    out[ST_TOUCHED] = st.touched.load(rl) ? 1 : 0;
    out[ST_LOOPING] = st.looping.load(rl) ? 1 : 0;
    out[ST_LOOP_A] = st.loopA.load(rl);
    out[ST_LOOP_B] = st.loopB.load(rl);
    out[ST_CUE] = st.cuePoint.load(rl);
    out[ST_PEAK_L] = st.peakL.load(rl);
    out[ST_PEAK_R] = st.peakR.load(rl);
    out[ST_SHADOW] = st.shadow.load(rl);
    out[ST_SYNCED] = st.synced.load(rl) ? 1 : 0;
    out[ST_CENSOR] = st.censoring.load(rl) ? 1 : 0;
    out[ST_ROLLING] = st.rolling.load(rl) ? 1 : 0;
}

void Host::meters(float* out4) const
{
    out4[0] = mixer.meter[0].load(rl);
    out4[1] = mixer.meter[1].load(rl);
    out4[2] = mixer.masterL.load(rl);
    out4[3] = mixer.masterR.load(rl);
}

//==============================================================================================================
// tracks (message side)

std::shared_ptr<Track> Host::buildTrack(const float* L, const float* R, size_t n, double trackSr, ByteSource* tags,
                                        const std::string& path, const std::string& title, const std::string& artist)
{
    if (L == nullptr || n == 0 || ! (trackSr > 0) || trackSr > 1e6) return nullptr;
    n = std::min(n, (size_t) (kMaxSeconds * trackSr));
    auto t = std::make_shared<Track>();
    t->sr = trackSr;
    t->path = path;
    t->L.assign(L, L + n);
    if (R != nullptr) t->R.assign(R, R + n); else t->R = t->L;
    for (size_t i = 0; i < n; ++i) {   // NaN / inf from a broken decoder would poison every filter downstream
        if (! std::isfinite(t->L[i])) t->L[i] = 0;
        if (! std::isfinite(t->R[i])) t->R[i] = 0;
    }
    Tags tg;
    if (tags != nullptr) tg = readTags(*tags);
    applyTags(tg, *t);
    if (t->title.empty()) t->title = title;
    if (t->artist.empty()) t->artist = artist;
    if (t->title.empty() && ! path.empty()) { std::string a; nameFromFileName(path, t->title, a); if (t->artist.empty()) t->artist = a; }
    analyseTrack(*t, tg.hasGrid, hintBpm(tg));
    return t;
}

std::shared_ptr<const Track> Host::currentTrack(int dk)
{
    std::lock_guard<std::mutex> l(msg);
    return current[dk];
}

bool Host::load(int dk, std::shared_ptr<const Track> t)
{
    if (dk < 0 || dk > 1 || t == nullptr) return false;
    collect();
    for (int tries = 0; tries < 1000; ++tries) {
        {
            std::lock_guard<std::mutex> l(msg);
            Cmd c;
            c.type = kLoad; c.deck = dk; c.track = t;
            if (push(std::move(c))) {
                keep.push_back(t);
                current[dk] = t;
                serial[dk] = ++serialCounter;
                gridShift[dk].store(0); gridBpm[dk].store(0);
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void Host::collect()
{
    std::lock_guard<std::mutex> l(msg);
    keep.erase(std::remove_if(keep.begin(), keep.end(), [](const std::shared_ptr<const Track>& t) { return t.use_count() == 1; }), keep.end());
}

size_t Host::keptTracks()
{
    std::lock_guard<std::mutex> l(msg);
    return keep.size();
}

void Host::trackInfo(int dk, double* out)
{
    out[0] = 0; out[1] = 0; out[2] = -1; out[3] = 0; out[4] = 0; out[5] = 0; out[6] = 0;
    if (dk < 0 || dk > 1) return;
    collect();
    std::shared_ptr<const Track> t;
    uint64_t ser;
    { std::lock_guard<std::mutex> l(msg); t = current[dk]; ser = serial[dk]; }
    out[6] = (double) ser;
    if (t == nullptr) return;
    const double gb = gridBpm[dk].load(rl);
    out[0] = gb > 0 ? gb : t->bpm;
    out[1] = t->firstBeat + gridShift[dk].load(rl);
    out[2] = t->key;
    out[3] = t->sr;
    out[4] = t->length();
    out[5] = 1;
}

std::vector<uint8_t> Host::wave(int dk, int band)
{
    if (dk < 0 || dk > 1) return {};
    collect();
    const auto t = currentTrack(dk);
    if (t == nullptr) return {};
    switch (band) {
        case 0: return t->wave.low;
        case 1: return t->wave.mid;
        case 2: return t->wave.high;
        case 3: return t->wave.peak;
        default: return {};
    }
}

std::array<std::string, 3> Host::trackText(int dk)
{
    if (dk < 0 || dk > 1) return {};
    collect();
    const auto t = currentTrack(dk);
    if (t == nullptr) return {};
    return { t->title, t->artist, t->path };
}

}  // namespace wheel
