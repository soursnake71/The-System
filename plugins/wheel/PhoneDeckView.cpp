// Omega Wheel of Steel: the phone app's performance screen (android/wheel WheelView.java, Pal.java, WaveData.java)
// drawn with JUCE: the same layout algorithm, colours, text sizes and gestures, talking to the processor through its
// public API (parameters with host gestures, send(Cmd), status(), cuesOf(), deckTrack(), pad(), meter()).
//
// Sizes: the phone lays out in dp. Here one "dp" is chosen so the window is the user's phone (882 x 411 dp, a
// 3088 x 1440 screen at density 3.5) scaled to fit: a 2000 x 933 window is that phone pixel for pixel at 1 / 1.544.
// Windows of another shape take the same branches the phone code takes for that shape (a taller window gets the
// tablet mixer with the channel faders under the knobs).
#include "PhoneUI.h"
#include "WheelFonts.h"
#include <cmath>
#include <cstring>
#include <list>
#include <map>

namespace wheel {
namespace {

using Rf = juce::Rectangle<float>;
using G = juce::Graphics;
using juce::uint32;

constexpr float kPhoneWdp = 882.f, kPhoneHdp = 411.f;   // the user's phone, in dp
constexpr double kPi = 3.14159265358979323846;
constexpr double kRevSeconds = 1.8;                     // one platter turn at 33 1/3
constexpr float kZoomMin = 2, kZoomMax = 32;

Rf ltrb(float l, float t, float r, float b) { return Rf::leftTopRightBottom(l, t, r, b); }
float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
juce::String U(const char* s) { return juce::String::fromUTF8(s); }
uint32 nowMs() { return juce::Time::getMillisecondCounter(); }

// ------------------------------------------------------------------------------------------------ Pal.java
namespace pal {
constexpr uint32 BG = 0xff0a0b0d, PANEL = 0xff141619, PANEL2 = 0xff1b1e22, RAISED = 0xff24282d, WELL = 0xff07080a,
                 EDGE = 0xff2b3036, EDGE_HI = 0xff3a4048, TEXT = 0xffe8eaed, DIM = 0xff9098a2, FAINT = 0xff545b64,
                 AMBER = 0xffffa21f, RED = 0xffff3b30, GREEN = 0xff34d058, CYAN = 0xff2fd4ff, VIOLET = 0xffb06cff,
                 WHITE = 0xffffffff, BLACK = 0xff000000;
constexpr uint32 DECK[2] = { 0xff26a6ff, 0xffff8a1a };
constexpr uint32 DECK_DARK[2] = { 0xff0b2a44, 0xff442408 };
const char* const CAM_MAJ[12] = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };
const char* const CAM_MIN[12] = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };

juce::String camelot(int key) {
    if (key < 0 || key > 23) return {};
    return key >= 12 ? CAM_MIN[key - 12] : CAM_MAJ[key];
}
uint32 keyColour(int key) {
    if (key < 0) return FAINT;
    const int n = camelot(key).dropLastCharacters(1).getIntValue();
    const float hue = (float) std::fmod(0.40 + (n - 1) / 12.0, 1.0);
    return (key >= 12 ? juce::Colour::fromHSV(hue, .78f, .95f, 1.f) : juce::Colour::fromHSV(hue, .55f, 1.f, 1.f)).getARGB();
}
uint32 alpha(uint32 c, float a) { return (c & 0x00ffffffu) | ((uint32) (int) (clampf(a, 0, 1) * 255) << 24); }
uint32 mix(uint32 a, uint32 b, float t) {
    const auto ch = [t](uint32 x, uint32 y, int sh) {
        const float fa = (float) ((x >> sh) & 0xffu), fb = (float) ((y >> sh) & 0xffu);
        return (uint32) (int) (fa + (fb - fa) * t) << sh;
    };
    return 0xff000000u | ch(a, b, 16) | ch(a, b, 8) | ch(a, b, 0);
}
juce::String time(double s) {
    const bool neg = s < 0;
    s = std::abs(s);
    const int m = (int) (s / 60), sec = (int) (s - m * 60), tenth = (int) ((s - std::floor(s)) * 10);
    return juce::String(neg ? "-" : "") + (m < 10 ? "0" : "") + juce::String(m) + ":" + (sec < 10 ? "0" : "") + juce::String(sec) + "." + juce::String(tenth);
}
}  // namespace pal

// ------------------------------------------------------------------------------------------------ Ctl.java names
const char* const MODE_SHORT[4] = { "CUES", "LOOP", "ROLL", "TRANS" };
const char* const LOOP_NAMES[8] = { "1/4", "1/2", "1", "2", "4", "8", "16", "32" };
const char* const ROLL_NAMES[8] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4" };
const char* const TRANSPORT_NAMES[8] = { "PLAY", "CUE", "SYNC", "CENSOR", "NUDGE -", "NUDGE +", "LOOP IN", "OUT/EXIT" };
const char* const FX_NAMES[8] = { "FX OFF", "ECHO", "PING-PONG", "REVERB", "FLANGER", "PHASER", "CRUSH", "GATE" };
const char* const FX_PARAM_NAMES[8] = { "FX PARAM", "FEEDBACK", "FEEDBACK", "SIZE", "FEEDBACK", "FEEDBACK", "CRUSH", "CHOP" };
const char* const FX_BEAT_NAMES[9] = { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8" };
const float RANGES[3] = { .08f, .16f, .50f };

// MPC 4x4 (as WheelPlugin.cpp): deck A slots = pads 0,1,4,5,8,9,12,13; deck B = 2,3,6,7,10,11,14,15
int padIndexOf(int deck, int slot) { return (slot / 2) * 4 + deck * 2 + slot % 2; }

// ------------------------------------------------------------------------------------------------ fonts
juce::Typeface::Ptr boldFace() {
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(WheelFonts::DejaVuSansBold_ttf, (size_t) WheelFonts::DejaVuSansBold_ttfSize);
    return tf;
}
// Android's text sizes are em sizes: JUCE's point height is the same measure. DejaVu Sans Bold is set about 14 %
// narrower so it takes the room the phone's Roboto Bold takes (the labels fit the same buttons).
constexpr float kBoldNarrow = .86f;
juce::Font bold(float em) {
    em = std::max(1.f, em);
    auto tf = boldFace();
    return tf != nullptr ? juce::Font(juce::FontOptions(tf).withPointHeight(em).withHorizontalScale(kBoldNarrow))
                         : juce::Font(juce::FontOptions(em, juce::Font::bold).withPointHeight(em));
}
juce::String monoName() {
    static const juce::String name = [] {
        const auto all = juce::Font::findAllTypefaceNames();
        for (const char* c : { "Droid Sans Mono", "Roboto Mono", "DejaVu Sans Mono", "Menlo", "Consolas", "Liberation Mono", "Noto Sans Mono", "Courier New" })
            if (all.contains(c)) return juce::String(c);
        return juce::String();
    }();
    return name;
}
juce::Font mono(float em) {
    em = std::max(1.f, em);
    const auto n = monoName();
    if (n.isEmpty()) return bold(em);   // no system fonts (the MPC): the built-in one has fixed-width digits
    return juce::Font(juce::FontOptions(n, em, juce::Font::bold).withPointHeight(em));
}
float textW(const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth(f, s); }

enum class Align { left, centre, right };

// ------------------------------------------------------------------------------------------------ WaveData.java
struct WaveData {
    static constexpr double kCols = kWaveRate;
    static constexpr int kTile = 512, kMaxTiles = 28;
    int n = 0;
    std::vector<uint32> outer, core;
    std::vector<uint8_t> amp;
    int tileH = 0;
    std::map<int, juce::Image> tiles;
    std::list<int> lru;
    juce::Image ov;
    int ovW = 0, ovH = 0;

    void clear() { n = 0; outer.clear(); core.clear(); amp.clear(); tiles.clear(); lru.clear(); ov = {}; }

    static int c255(float v) { return std::max(0, std::min(255, (int) (v * 255 + .5f))); }
    static uint32 rgb(float r, float g, float b) { return 0xff000000u | ((uint32) c255(r) << 16) | ((uint32) c255(g) << 8) | (uint32) c255(b); }

    void set(const Wave& w) {
        clear();
        const size_t len = std::min(w.low.size(), std::min(w.mid.size(), w.high.size()));
        outer.resize(len); core.resize(len); amp.resize(len);
        for (size_t i = 0; i < len; ++i) {
            const float l = w.low[i] / 255.f, m = w.mid[i] / 255.f, h = w.high[i] / 255.f;
            const float p = i < w.peak.size() ? w.peak[i] / 255.f : std::max(l, std::max(m, h));
            const float r = l, g = m * .92f, b = h * 1.1f;
            const float mx = std::max(std::max(r, g), std::max(b, 1e-3f));
            const float bright = .42f + .58f * std::sqrt(p);
            const float cr = r / mx * bright, cg = g / mx * bright, cb = std::min(1.f, b / mx) * bright;
            outer[i] = rgb(cr, cg, cb);
            const float wt = .35f + .3f * h;
            core[i] = rgb(cr + (1 - cr) * wt, cg + (1 - cg) * wt, cb + (1 - cb) * wt);
            amp[i] = (uint8_t) std::max(0, std::min(255, (int) (std::pow((double) p, .8) * 255 + .5)));
        }
        n = (int) len;
    }

    static void put(juce::Image::BitmapData& bd, int x, int y, uint32 c) {
        auto* px = reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(x, y));
        px->setARGB((uint8_t) (c >> 24), (uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c);
    }

    // tile k (columns k * kTile ..), h px tall, transparent background
    juce::Image tile(int k, int h) {
        if (n == 0 || k < 0 || k * kTile >= n || h < 4) return {};
        if (h != tileH) { tiles.clear(); lru.clear(); tileH = h; }
        auto it = tiles.find(k);
        if (it != tiles.end()) { lru.remove(k); lru.push_front(k); return it->second; }
        juce::Image img(juce::Image::ARGB, kTile, h, true);
        {
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
            const float half = (float) h / 2.f;
            for (int x = 0; x < kTile; ++x) {
                const int i = k * kTile + x;
                if (i >= n) break;
                const float a = amp[(size_t) i] / 255.f * half;
                if (a < .5f) continue;
                const int top = (int) (half - a), bot = (int) std::ceil(half + a);
                const float ca = a * .45f;
                const int ctop = (int) (half - ca), cbot = (int) std::ceil(half + ca);
                const uint32 oc = outer[(size_t) i], cc = core[(size_t) i];
                for (int y = std::max(0, top); y < std::min(h, bot); ++y) put(bd, x, y, (y >= ctop && y < cbot) ? cc : oc);
            }
        }
        tiles[k] = img;
        lru.push_front(k);
        while ((int) lru.size() > kMaxTiles) { tiles.erase(lru.back()); lru.pop_back(); }
        return img;
    }

    // the whole track squeezed into w x h
    juce::Image overview(int w, int h) {
        if (n == 0 || w < 4 || h < 4) return {};
        if (ov.isValid() && ovW == w && ovH == h) return ov;
        juce::Image img(juce::Image::ARGB, w, h, true);
        {
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
            const float half = (float) h / 2.f;
            for (int x = 0; x < w; ++x) {
                const int i0 = (int) ((juce::int64) x * n / w), i1 = std::max(i0 + 1, (int) ((juce::int64) (x + 1) * n / w));
                int best = i0, bestA = -1;
                for (int i = i0; i < i1 && i < n; ++i) { const int a = amp[(size_t) i]; if (a > bestA) { bestA = a; best = i; } }
                const float a = (float) std::max(0, bestA) / 255.f * half;
                const int top = (int) (half - a), bot = (int) std::ceil(half + a);
                const uint32 c = outer[(size_t) std::min(best, n - 1)];
                for (int y = std::max(0, top); y < std::min(h, bot); ++y) put(bd, x, y, c);
            }
        }
        ov = img; ovW = w; ovH = h;
        return ov;
    }
};

}  // namespace

// ==================================================================================================== Impl
struct PhoneDeckView::Impl {
    // ---- parameters
    enum DK { kPitch, kRange, kRpm45, kKeylock, kSlip, kQuant, kTrim, kHi, kMid, kLow, kFilter, kFader, kPfl, kXfassign,
              kFxtype, kFxbeats, kFxdepth, kFxparam, kFxon, kDKCount };
    enum GK { G_XFADER, G_XFCURVE, G_FADERCURVE, G_HAMSTER, G_MASTER, G_PADMODE, kGKCount };
    struct Prm { juce::RangedAudioParameter* p = nullptr; std::atomic<float>* raw = nullptr; };

    WheelProcessor& proc;
    PhoneHost& host;
    PhoneDeckView& view;
    Prm dpar[2][kDKCount], gpar[kGKCount];
    float dp = 1, ts = 1, pxScale = 1;

    Impl(WheelProcessor& p, PhoneHost& h, PhoneDeckView& v) : proc(p), host(h), view(v) {
        static const char* const dk[kDKCount] = { "pitch", "range", "rpm45", "keylock", "slip", "quant", "trim", "hi", "mid", "low", "filter",
                                                  "fader", "pfl", "xfassign", "fxtype", "fxbeats", "fxdepth", "fxparam", "fxon" };
        static const char* const gk[kGKCount] = { "xfader", "xfcurve", "fadercurve", "hamster", "master", "padmode" };
        for (int d = 0; d < 2; ++d)
            for (int k = 0; k < kDKCount; ++k) dpar[d][k] = prm(deckParam(d, dk[k]));
        for (int k = 0; k < kGKCount; ++k) gpar[k] = prm(gk[k]);
        juce::PropertiesFile::Options o;
        o.applicationName = "Omega Wheel of Steel";
        o.millisecondsBeforeSaving = 800;
        settings = std::make_unique<juce::PropertiesFile>(
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Omega Wheel of Steel.settings"), o);
        const float z = (float) settings->getDoubleValue("waveZoom", 0);
        waveZoom = z <= 0 ? 0 : clampf(z, kZoomMin, kZoomMax);
        for (int d = 0; d < 2; ++d) for (int i = 0; i < 8; ++i) { live[d].cueT[i] = -1; live[d].cueRgb[i] = 0; }
        focus = proc.focusDeck();
        procFocus = focus;
        build();
    }
    ~Impl() { if (settings) settings->saveIfNeeded(); }

    Prm prm(const juce::String& id) const { Prm r; r.p = proc.apvts.getParameter(id); r.raw = proc.apvts.getRawParameterValue(id); return r; }
    static float val(const Prm& p) { return p.raw != nullptr ? p.raw->load() : 0.f; }        // in the parameter's units
    static float nrm(const Prm& p) { return p.p != nullptr ? p.p->getValue() : 0.f; }        // 0..1
    float get(int d, int k) const { return val(dpar[d][k]); }
    bool on(int d, int k) const { return get(d, k) > .5f; }
    static void setN(const Prm& p, float n, bool gesture) {
        if (p.p == nullptr) return;
        n = clampf(n, 0, 1);
        if (std::abs(p.p->getValue() - n) < 1e-7f) return;
        if (gesture) p.p->beginChangeGesture();
        p.p->setValueNotifyingHost(n);
        if (gesture) p.p->endChangeGesture();
    }
    static void setV(const Prm& p, float x, bool gesture) { if (p.p != nullptr) setN(p, p.p->convertTo0to1(x), gesture); }
    static void begin(const Prm& p) { if (p.p != nullptr) p.p->beginChangeGesture(); }
    static void end(const Prm& p) { if (p.p != nullptr) p.p->endChangeGesture(); }
    void toggle(int d, int k) { setV(dpar[d][k], on(d, k) ? 0.f : 1.f, true); }
    int padMode() const { return (int) proc.padMode(); }
    void setPadMode(int m) { setV(gpar[G_PADMODE], (float) (((m % 4) + 4) % 4), true); }
    void send(Cmd::Type t, int deck, int i = 0, double v = 0, bool o = false) {
        Cmd c {}; c.type = t; c.deck = deck; c.i = i; c.v = v; c.on = o; proc.send(c);
    }
    void cmdSync(int d) { send(Cmd::sync, d, 0, 0, false); }   // on = false: toggle

    // ---- UI-only settings (the waveform zoom), shared by every instance
    std::unique_ptr<juce::PropertiesFile> settings;
    float waveZoom = 0, autoZoom = 8;
    float zoomSec() const { return waveZoom > 0 ? waveZoom : autoZoom; }
    void setWaveZoom(float s) {
        waveZoom = clampf(s, kZoomMin, kZoomMax);
        if (settings) settings->setValue("waveZoom", (double) waveZoom);
    }

    // ---- focus (the deck the MIDI surface works on): a tap on a header picks it here; the processor's own focus
    // (the MPC Sample's B1 / B2) wins when it changes
    int focus = 0, procFocus = 0;

    // ---- live state, refreshed every frame
    struct Live {
        std::shared_ptr<const Track> trk;
        bool has = false;
        double len = 0, bpm0 = 0, first = 0;
        int key = -1;
        juce::String title, artist;
        double pos = 0, bpm = 0, angle = 0, cue = 0, loopA = -1, loopB = -1;
        bool playing = false, touched = false, looping = false, rolling = false, censor = false, synced = false;
        double cueT[8];
        uint32 cueRgb[8];
        bool loading = false;
        juce::String err, errSeen;
        uint32 errAt = 0;
        uint32 loadStart = 0;
    };
    Live live[2];
    WaveData wave[2];
    float meters[4] {}, meterHold[4] {};
    bool hasTrack(int d) const { return live[d].has; }
    bool playing(int d) const { return live[d].playing; }

    void poll() {
        for (int d = 0; d < 2; ++d) {
            auto& L = live[d];
            const auto& st = proc.status(d);
            auto t = proc.deckTrack(d);
            if (t != L.trk) {
                L.trk = t;
                L.has = t != nullptr;
                if (t != nullptr) {
                    wave[d].set(t->wave);
                    L.len = t->length(); L.bpm0 = t->bpm; L.first = t->firstBeat; L.key = t->key;
                    L.title = juce::String::fromUTF8(t->title.c_str());
                    L.artist = juce::String::fromUTF8(t->artist.c_str());
                    if (L.title.isEmpty()) L.title = juce::File(juce::String::fromUTF8(t->path.c_str())).getFileNameWithoutExtension();
                } else {
                    wave[d].clear();
                    L.len = L.bpm0 = L.first = 0; L.key = -1; L.title.clear(); L.artist.clear();
                }
            }
            L.pos = st.pos.load(); L.bpm = st.bpmNow.load(); L.angle = st.angle.load(); L.cue = st.cuePoint.load();
            L.loopA = st.loopA.load(); L.loopB = st.loopB.load();
            L.playing = st.playing.load(); L.touched = st.touched.load(); L.looping = st.looping.load();
            L.rolling = st.rolling.load(); L.censor = st.censoring.load(); L.synced = st.synced.load();
            const auto cues = proc.cuesOf(d);
            for (int i = 0; i < 8; ++i) { L.cueT[i] = L.has && cues[(size_t) i].set() ? cues[(size_t) i].t : -1; L.cueRgb[i] = cues[(size_t) i].rgb & 0xffffffu; }
            const bool ld = proc.loading[d].load();
            if (ld && !L.loading) L.loadStart = nowMs();
            L.loading = ld;
            const auto& e = proc.loadError[d];
            if (e != L.errSeen) { L.errSeen = e; L.err = e; L.errAt = nowMs(); }
            if (L.err.isNotEmpty() && nowMs() - L.errAt > 6000) L.err.clear();
        }
        for (int i = 0; i < 4; ++i) { meters[i] = proc.meter(i); meterHold[i] = std::max(meters[i], meterHold[i] * .93f); }
        const int pf = proc.focusDeck();
        if (pf != procFocus) { procFocus = pf; focus = pf; }
        for (auto* c : ctrls) c->tick();
        for (auto* c : sheetCtrls) c->tick();
    }

    // ---- the hand on the record: the platter and the waveform of a deck share it (as Ctl.touch)
    bool hand[2][2] {};
    static constexpr int HAND_PLATTER = 0, HAND_WAVE = 1;
    void touch(int d, int src, bool o) {
        const bool before = hand[d][0] || hand[d][1];
        hand[d][src] = o;
        const bool after = hand[d][0] || hand[d][1];
        if (before != after) send(Cmd::touch, d, 0, 0, after);
    }
    bool handOn(int d) const { return hand[d][0] || hand[d][1] || live[d].touched; }
    void moveRecord(int d, double seconds) { if (!juce::exactlyEqual(seconds, 0.0)) send(Cmd::move, d, 0, seconds, true); }

    // ============================================================================================== controls
    struct Ctrl {
        explicit Ctrl(Impl& o) : v(o) {}
        virtual ~Ctrl() = default;
        Impl& v;
        Rf r;
        int pointer = -1;
        // takes a finger; false = let it fall through
        virtual bool down(int id, float, float) { if (pointer >= 0) return false; pointer = id; return true; }
        virtual void move(float, float) {}
        virtual void up(bool) { pointer = -1; }
        virtual bool hit(float x, float y) const { return r.contains(x, y); }
        virtual void tick() {}
        virtual void draw(G&) = 0;
        virtual bool wheel(float) { return false; }   // mouse wheel over it (desktop)
        bool held() const { return pointer >= 0; }
    };
    using Press = std::function<void(bool)>;
    using Lit = std::function<bool()>;
    using Txt = std::function<juce::String()>;

    struct Btn : Ctrl {
        Txt label; uint32 color; Lit lit; Press press; float textMax = 13;
        Btn(Impl& o, Txt l, uint32 c, Lit li, Press p) : Ctrl(o), label(std::move(l)), color(c), lit(std::move(li)), press(std::move(p)) {}
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            if (press) press(true);
            return true;
        }
        void up(bool cancel) override { Ctrl::up(cancel); if (press) press(false); }
        void draw(G& g) override { v.drawButton(g, r, label(), color, lit && lit(), held(), textMax); }
    };

    // a knob: norm() is the parameter's 0..1 (the phone's EQ / trim / master mappings are the parameters' ranges)
    struct Knob : Ctrl {
        Prm prm; Txt label; uint32 color; bool bipolar; float def; bool eq; float lastY = 0; uint32 lastDown = 0;
        Knob(Impl& o, Prm p, Txt l, uint32 c, bool bi, float df, bool isEq = false)
            : Ctrl(o), prm(p), label(std::move(l)), color(c), bipolar(bi), def(df), eq(isEq) {}
        float norm() const { return nrm(prm); }
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            begin(prm);
            lastY = y;
            const uint32 now = nowMs();
            if (now - lastDown < 300) setN(prm, def, false);
            lastDown = now;
            return true;
        }
        void move(float, float y) override {
            const float dn = (lastY - y) / (160 * v.dp);
            lastY = y;
            setTo(norm() + dn);
        }
        void setTo(float n) {
            n = clampf(n, 0, 1);
            if (bipolar && std::abs(n - .5f) < .012f) n = .5f;
            setN(prm, n, false);
        }
        void up(bool cancel) override { Ctrl::up(cancel); end(prm); }
        bool wheel(float dy) override { begin(prm); setTo(norm() + dy * .1f); end(prm); return true; }
        void draw(G& g) override {
            const float lh = std::min(11 * v.ts, r.getHeight() * .26f);
            const float size = std::min(r.getWidth(), r.getHeight() - lh) * .86f;
            const float cx = r.getCentreX(), cy = r.getY() + (r.getHeight() - lh) / 2.f, rad = size / 2.f;
            const float n = norm();
            g.setColour(juce::Colour(held() ? pal::EDGE_HI : pal::RAISED));
            g.fillEllipse(cx - rad, cy - rad, 2 * rad, 2 * rad);
            float sw = std::max(2 * v.dp, rad * .16f);
            const Rf oval(cx - rad * .78f, cy - rad * .78f, rad * 1.56f, rad * 1.56f);
            arc(g, oval, 135, 270, sw, pal::WELL, true);
            const bool killed = eq && n <= .001f;
            const uint32 c = killed ? pal::RED : color;
            if (bipolar) { const float a0 = 270, sweep = (n - .5f) * 270; arc(g, oval, sweep < 0 ? a0 + sweep : a0, std::abs(sweep), sw, c, true); }
            else arc(g, oval, 135, n * 270, sw, c, true);
            const double ang = (135 + n * 270) * kPi / 180;
            sw = std::max(2 * v.dp, rad * .12f);
            line(g, cx + (float) std::cos(ang) * rad * .2f, cy + (float) std::sin(ang) * rad * .2f,
                 cx + (float) std::cos(ang) * rad * .62f, cy + (float) std::sin(ang) * rad * .62f, sw, pal::TEXT, true);
            v.label(g, killed ? juce::String("KILL") : label(), cx, r.getBottom() - lh * .2f, lh * .92f, killed ? pal::RED : pal::DIM, Align::centre, r.getWidth());
        }
    };

    // a vertical fader: channel fader (0 bottom .. 1 top) or pitch (-1 top .. +1 bottom, like a Technics: down = faster)
    struct VFader : Ctrl {
        Prm prm; bool pitch; uint32 color; float grabOff = 0; uint32 lastDown = 0;
        VFader(Impl& o, Prm p, bool isPitch, uint32 c) : Ctrl(o), prm(p), pitch(isPitch), color(c) {}
        float value() const { return val(prm); }
        float pos() const { const float x = value(); return pitch ? (x + 1) / 2 : 1 - x; }   // 0 top .. 1 bottom
        float capH() const { return std::min(r.getHeight() * .2f, std::max(18 * v.dp, r.getWidth() * .55f)); }
        float trackTop() const { return r.getY() + capH() / 2; }
        float trackLen() const { return std::max(1.f, r.getHeight() - capH()); }
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            const float capY = trackTop() + pos() * trackLen();
            grabOff = std::abs(y - capY) < capH() ? y - capY : 0;
            const uint32 now = nowMs();
            if (now - lastDown < 300) { setV(prm, pitch ? 0.f : 1.f, true); grabOff = 0; lastDown = 0; pointer = -1; return true; }
            lastDown = now;
            begin(prm);
            if (juce::exactlyEqual(grabOff, 0.f)) setFromY(y);
            return true;
        }
        void setFromY(float y) {
            const float p = clampf((y - grabOff - trackTop()) / trackLen(), 0, 1);
            if (pitch) { float x = p * 2 - 1; if (std::abs(x) < .025f) x = 0; setV(prm, x, false); }   // centre detent
            else setV(prm, 1 - p, false);
        }
        void move(float, float y) override { setFromY(y); }
        void up(bool cancel) override { Ctrl::up(cancel); end(prm); }
        bool wheel(float dy) override {
            float p = pos() - dy * .1f;
            if (pitch) { float x = clampf(p, 0, 1) * 2 - 1; if (std::abs(x) < .025f) x = 0; setV(prm, x, true); }
            else setV(prm, 1 - clampf(p, 0, 1), true);
            return true;
        }
        void draw(G& g) override {
            const float cx = r.getCentreX();
            g.setColour(juce::Colour(pal::WELL));
            g.fillRoundedRectangle(ltrb(cx - 3 * v.dp, trackTop(), cx + 3 * v.dp, trackTop() + trackLen()), 3 * v.dp);
            for (int i = 0; i <= 10; ++i) {   // scale ticks
                const float y = trackTop() + trackLen() * (float) i / 10.f;
                const bool mid = pitch && i == 5;
                const float w = mid ? r.getWidth() * .45f : r.getWidth() * .28f;
                line(g, cx - w, y, cx - 6 * v.dp, y, v.dp, mid ? pal::TEXT : pal::FAINT, false);
                line(g, cx + 6 * v.dp, y, cx + w, y, v.dp, mid ? pal::TEXT : pal::FAINT, false);
            }
            const float capY = trackTop() + pos() * trackLen(), ch = capH(), cw = std::min(r.getWidth(), 64 * v.dp);
            const Rf cap = ltrb(cx - cw / 2, capY - ch / 2, cx + cw / 2, capY + ch / 2);
            g.setColour(juce::Colour(held() ? pal::mix(pal::RAISED, color, .35f) : 0xff3b4047u));
            g.fillRoundedRectangle(cap, 4 * v.dp);
            g.setColour(juce::Colour(pitch && juce::exactlyEqual(value(), 0.f) ? pal::GREEN : color));
            g.fillRect(ltrb(cap.getX() + 4 * v.dp, capY - 1.5f * v.dp, cap.getRight() - 4 * v.dp, capY + 1.5f * v.dp));
        }
    };

    // the crossfader: the cap jumps to the finger (cut-in scratching)
    struct XFader : Ctrl {
        explicit XFader(Impl& o) : Ctrl(o) {}
        const Prm& prm() const { return v.gpar[G_XFADER]; }
        float capW() const { return std::min(r.getWidth() * .14f, std::max(28 * v.dp, r.getHeight() * .7f)); }
        float left() const { return r.getX() + capW() / 2; }
        float len() const { return std::max(1.f, r.getWidth() - capW()); }
        bool down(int id, float x, float y) override { if (!Ctrl::down(id, x, y)) return false; begin(prm()); set(x); return true; }
        void move(float x, float) override { set(x); }
        void up(bool cancel) override { Ctrl::up(cancel); end(prm()); }
        void set(float x) { setV(prm(), clampf((x - left()) / len(), 0, 1) * 2 - 1, false); }
        bool wheel(float dy) override { setV(prm(), clampf(val(prm()) - dy * .2f, -1, 1), true); return true; }
        void draw(G& g) override {
            const float cy = r.getCentreY(), dp = v.dp;
            g.setColour(juce::Colour(pal::WELL));
            g.fillRoundedRectangle(ltrb(r.getX(), cy - 4 * dp, r.getRight(), cy + 4 * dp), 4 * dp);
            for (int i = 0; i <= 8; ++i) {
                const float x = left() + len() * (float) i / 8.f;
                const uint32 c = i == 4 ? pal::TEXT : pal::FAINT;
                line(g, x, r.getY() + 2 * dp, x, r.getY() + r.getHeight() * .22f, dp, c, false);
                line(g, x, r.getBottom() - r.getHeight() * .22f, x, r.getBottom() - 2 * dp, dp, c, false);
            }
            const float p = (val(prm()) + 1) / 2;
            const float x = left() + p * len(), cw = capW();
            const Rf cap = ltrb(x - cw / 2, r.getY() + r.getHeight() * .12f, x + cw / 2, r.getBottom() - r.getHeight() * .12f);
            g.setColour(juce::Colour(held() ? 0xff5a616bu : 0xff40464fu));
            g.fillRoundedRectangle(cap, 5 * dp);
            g.setColour(juce::Colour(pal::WHITE));
            g.fillRect(ltrb(x - 1.5f * dp, cap.getY() + 4 * dp, x + 1.5f * dp, cap.getBottom() - 4 * dp));
            const bool ham = val(v.gpar[G_HAMSTER]) > .5f;   // hamster: the ends swap
            v.label(g, ham ? "B" : "A", r.getX() + 8 * dp, cy + 5 * v.ts, 13 * v.ts, pal::DECK[ham ? 1 : 0], Align::centre, 0);
            v.label(g, ham ? "A" : "B", r.getRight() - 8 * dp, cy + 5 * v.ts, 13 * v.ts, pal::DECK[ham ? 0 : 1], Align::centre, 0);
            if (ham) v.label(g, "HAMSTER", r.getCentreX(), r.getY() + r.getHeight() * .2f, 9 * v.ts, pal::AMBER, Align::centre, 0);
        }
    };

    // a platter: each finger on it scratches that deck (angle around the centre)
    struct Platter : Ctrl {
        int deck; float cx = 0, cy = 0, rad = 0; double lastAng = 0;
        Platter(Impl& o, int d) : Ctrl(o), deck(d) {}
        bool hit(float x, float y) const override { const float dx = x - cx, dy = y - cy; return dx * dx + dy * dy <= rad * rad * 1.04f; }
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            lastAng = std::atan2((double) (y - cy), (double) (x - cx));
            v.touch(deck, HAND_PLATTER, true);
            return true;
        }
        void move(float x, float y) override {
            const float dx = x - cx, dy = y - cy;
            const double a = std::atan2((double) dy, (double) dx);
            double sum = 0;
            if (dx * dx + dy * dy > rad * rad * .02f) {   // too near the spindle the angle means nothing
                double dd = a - lastAng;
                while (dd > kPi) dd -= 2 * kPi;
                while (dd < -kPi) dd += 2 * kPi;
                sum = dd;
            }
            lastAng = a;
            if (!juce::exactlyEqual(sum, 0.0)) v.moveRecord(deck, sum / (2 * kPi) * kRevSeconds);   // clockwise = forward
        }
        void up(bool cancel) override { Ctrl::up(cancel); v.touch(deck, HAND_PLATTER, false); }
        void draw(G& g) override { v.drawPlatter(g, *this); }
    };

    // the scrolling waveform: drag sideways to scratch. The hand goes on the record once the finger moves or after
    // kHoldMs, so a second finger landing first makes it a pinch (zoom) without stopping the record.
    struct WaveC : Ctrl {
        static constexpr uint32 kHoldMs = 90;
        int deck; float lastX = 0, startX = 0; uint32 downAt = 0; bool engaged = false;
        WaveC(Impl& o, int d) : Ctrl(o), deck(d) {}
        float pxPerSec() const { return r.getWidth() / v.zoomSec(); }
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;   // taken even with no track, so a pinch can start here
            lastX = startX = x;
            downAt = nowMs();
            engaged = false;
            return true;
        }
        void engage() { if (!engaged && held() && v.hasTrack(deck)) { engaged = true; v.touch(deck, HAND_WAVE, true); } }
        void move(float x, float) override {
            if (!engaged) { if (std::abs(x - startX) < 4 * v.dp) return; engage(); if (!engaged) return; }
            const float dx = x - lastX;
            lastX = x;
            if (!juce::exactlyEqual(dx, 0.f)) v.moveRecord(deck, -dx / pxPerSec());
        }
        void tick() override { if (held() && !engaged && nowMs() - downAt > kHoldMs) engage(); }
        void up(bool cancel) override {
            Ctrl::up(cancel);
            if (engaged) v.touch(deck, HAND_WAVE, false);
            engaged = false;
        }
        void draw(G& g) override { v.drawWave(g, *this); }
    };

    // the whole-track strip: touch to seek
    struct Overview : Ctrl {
        int deck;
        Overview(Impl& o, int d) : Ctrl(o), deck(d) {}
        bool down(int id, float x, float y) override { if (!v.hasTrack(deck) || !Ctrl::down(id, x, y)) return false; seek(x); return true; }
        void move(float x, float) override { seek(x); }
        void seek(float x) {
            const double len = v.live[deck].len;
            if (len <= 0) return;
            v.send(Cmd::seek, deck, 0, (double) clampf((x - r.getX()) / r.getWidth(), 0, 1) * len);
        }
        void draw(G& g) override { v.drawOverview(g, *this); }
    };

    // the deck header: tap = MIDI focus on this deck; with a track picked in the library and the deck stopped, loads it
    struct Header : Ctrl {
        int deck;
        Header(Impl& o, int d) : Ctrl(o), deck(d) {}
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            v.focus = deck;
            if (!v.playing(deck) && v.host.loadSelected) v.host.loadSelected(deck);
            return true;
        }
        void draw(G& g) override { v.drawHeader(g, *this); }
    };

    struct Pad : Ctrl {
        int deck, slot; uint32 downAt = 0; bool cleared = false;
        Pad(Impl& o, int d, int s) : Ctrl(o), deck(d), slot(s) {}
        int index() const { return padIndexOf(deck, slot); }
        bool down(int id, float x, float y) override {
            if (!Ctrl::down(id, x, y)) return false;
            downAt = nowMs();
            cleared = false;
            v.proc.pad(index(), true, 1.f);
            return true;
        }
        void up(bool cancel) override { Ctrl::up(cancel); if (!cleared) v.proc.pad(index(), false, 0.f); }
        void tick() override {
            // a long press on a hot cue pad clears that cue
            if (held() && !cleared && v.padMode() == (int) PadMode::cues && v.live[deck].cueT[slot] >= 0 && nowMs() - downAt > 750) {
                v.proc.pad(index(), false, 0.f);
                v.send(Cmd::clearCue, deck, slot, 0, true);
                cleared = true;
            }
        }
        void draw(G& g) override { v.drawPad(g, *this); }
    };

    // ---- the controls
    std::vector<std::unique_ptr<Ctrl>> owned;
    std::vector<Ctrl*> ctrls, sheetCtrls;
    Header* headers[2] {};
    WaveC* waves[2] {};
    Overview* overviews[2] {};
    Platter* platters[2] {};
    VFader* pitchF[2] {};
    VFader* chFader[2] {};
    Btn* transport[2][8] {};
    Btn* modeTabs[2][4] {};
    Btn* fxBtns[2][3] {};
    Knob* fxDepth[2] {};
    Btn* loopBtns[2][5] {};
    Pad* pads[2][8] {};
    Knob* eqK[2][4] {};
    Knob* master = nullptr;
    Knob* curve = nullptr;
    XFader* xfader = nullptr;
    Btn* libBtn = nullptr;
    Btn* setupBtn = nullptr;
    Btn* pfl[2] {};

    // the setup sheet (SETUP): trim, FX parameter, crossfader assign, 33 / 45, quantize, cue, hamster, fader curve
    bool sheetOpen = false;
    Rf sheetR, sheetTitleR, sheetCol[2], sheetMid;
    float sheetLabelY[3] {};
    Knob* trimK[2] {};
    Knob* fxParamK[2] {};
    Btn* xfAssign[2][3] {};
    Btn* deckSw[2][3] {};
    Btn* hamsterBtn = nullptr;
    Btn* doneBtn = nullptr;
    Knob* chCurve = nullptr;

    // regions for drawing panels
    Rf mixRect, deckMid[2], deckBot[2], meterR[4], statusR;

    template <class T, class... A> T* mk(bool sheet, A&&... a) {
        auto p = std::make_unique<T>(*this, std::forward<A>(a)...);
        T* raw = p.get();
        owned.push_back(std::move(p));
        (sheet ? sheetCtrls : ctrls).push_back(raw);
        return raw;
    }
    Btn* btn(bool sheet, const juce::String& l, uint32 c, Lit lit, Press p) { return mk<Btn>(sheet, Txt([l] { return l; }), c, std::move(lit), std::move(p)); }
    Btn* btn(bool sheet, Txt l, uint32 c, Lit lit, Press p) { return mk<Btn>(sheet, std::move(l), c, std::move(lit), std::move(p)); }
    Knob* knob(bool sheet, const Prm& p, const juce::String& l, uint32 c, bool bi, float def, bool isEq = false) {
        return mk<Knob>(sheet, p, Txt([l] { return l; }), c, bi, def, isEq);
    }

    int fxType(int d) const { return juce::jlimit(0, 7, juce::roundToInt(get(d, kFxtype))); }
    int fxBeatIndex(int d) const { return juce::jlimit(0, 8, juce::roundToInt(get(d, kFxbeats))); }
    int rangeIndex(int d) const { return juce::jlimit(0, 2, juce::roundToInt(get(d, kRange))); }

    void build() {
        for (int d = 0; d < 2; ++d) {
            const int dk = d;
            const uint32 col = pal::DECK[d];
            headers[d] = mk<Header>(false, d);
            overviews[d] = mk<Overview>(false, d);
            waves[d] = mk<WaveC>(false, d);
            platters[d] = mk<Platter>(false, d);
            pitchF[d] = mk<VFader>(false, dpar[d][kPitch], true, col);
            chFader[d] = mk<VFader>(false, dpar[d][kFader], false, col);
            auto** t = transport[d];
            t[0] = btn(false, "CUE", pal::AMBER, [this, dk] { return hasTrack(dk) && !playing(dk) && std::abs(live[dk].pos - live[dk].cue) < .03; },
                       [this, dk](bool dn) { send(Cmd::cue, dk, 0, 0, dn); });
            t[1] = btn(false, Txt([this, dk] { return playing(dk) ? U("\xe2\x9d\x9a\xe2\x9d\x9a") : U("\xe2\x96\xb6"); }), pal::GREEN,
                       [this, dk] { return playing(dk); }, [this, dk](bool dn) { if (dn) send(Cmd::playPause, dk); });
            t[2] = btn(false, "SYNC", pal::CYAN, [this, dk] { return live[dk].synced; }, [this, dk](bool dn) { if (dn) cmdSync(dk); });
            t[3] = btn(false, "KEY", pal::AMBER, [this, dk] { return on(dk, kKeylock); }, [this, dk](bool dn) { if (dn) toggle(dk, kKeylock); });
            t[4] = btn(false, "SLIP", pal::VIOLET, [this, dk] { return on(dk, kSlip); }, [this, dk](bool dn) { if (dn) toggle(dk, kSlip); });
            t[5] = btn(false, Txt([this, dk] { return U("\xc2\xb1") + juce::String(juce::roundToInt(RANGES[rangeIndex(dk)] * 100)) + "%"; }), pal::DIM,
                       [] { return false; }, [this, dk](bool dn) { if (dn) setV(dpar[dk][kRange], (float) ((rangeIndex(dk) + 1) % 3), true); });
            t[6] = btn(false, U("\xe2\x97\x80 NDG"), pal::TEXT, nullptr, [this, dk](bool dn) { send(Cmd::nudge, dk, 0, dn ? -1 : 0, dn); });
            t[7] = btn(false, U("NDG \xe2\x96\xb6"), pal::TEXT, nullptr, [this, dk](bool dn) { send(Cmd::nudge, dk, 0, dn ? 1 : 0, dn); });
            t[1]->textMax = 22; t[0]->textMax = 16;
            for (int m = 0; m < 4; ++m) {
                const int mm = m;
                modeTabs[d][m] = btn(false, MODE_SHORT[m], col, [this, mm] { return padMode() == mm; }, [this, mm](bool dn) { if (dn) setPadMode(mm); });
                modeTabs[d][m]->textMax = 11;
            }
            fxBtns[d][0] = btn(false, Txt([this, dk] { return juce::String(FX_NAMES[fxType(dk)]); }), pal::TEXT, nullptr,
                               [this, dk](bool dn) { if (dn) setV(dpar[dk][kFxtype], (float) ((fxType(dk) + 1) % 8), true); });
            fxBtns[d][1] = btn(false, Txt([this, dk] { return juce::String(FX_BEAT_NAMES[fxBeatIndex(dk)]); }), pal::TEXT, nullptr,
                               [this, dk](bool dn) { if (dn) setV(dpar[dk][kFxbeats], (float) ((fxBeatIndex(dk) + 1) % 9), true); });
            fxBtns[d][2] = btn(false, "FX ON", pal::RED, [this, dk] { return on(dk, kFxon); }, [this, dk](bool dn) { if (dn) toggle(dk, kFxon); });
            for (auto* b : fxBtns[d]) b->textMax = 11;
            fxDepth[d] = knob(false, dpar[d][kFxdepth], "DEPTH", col, false, .5f);
            auto** l = loopBtns[d];
            l[0] = btn(false, "IN", pal::GREEN, [this, dk] { return live[dk].looping; }, [this, dk](bool dn) { if (dn) send(Cmd::loopIn, dk); });
            l[1] = btn(false, "OUT", pal::GREEN, [this, dk] { return live[dk].looping; }, [this, dk](bool dn) { if (dn) send(Cmd::loopOut, dk); });
            l[2] = btn(false, U("\xc2\xbd"), pal::TEXT, nullptr, [this, dk](bool dn) { if (dn) send(Cmd::loopHalve, dk); });
            l[3] = btn(false, U("\xc3\x97" "2"), pal::TEXT, nullptr, [this, dk](bool dn) { if (dn) send(Cmd::loopDouble, dk); });
            l[4] = btn(false, Txt([this, dk] { return juce::String(live[dk].looping ? "EXIT" : "RELOOP"); }), pal::GREEN, [this, dk] { return live[dk].looping; },
                       [this, dk](bool dn) { if (dn) send(live[dk].looping ? Cmd::loopExit : Cmd::reloop, dk); });
            for (int s = 0; s < 8; ++s) pads[d][s] = mk<Pad>(false, d, s);
            eqK[d][0] = knob(false, dpar[d][kHi], "HI", col, true, .5f, true);
            eqK[d][1] = knob(false, dpar[d][kMid], "MID", col, true, .5f, true);
            eqK[d][2] = knob(false, dpar[d][kLow], "LOW", col, true, .5f, true);
            eqK[d][3] = knob(false, dpar[d][kFilter], "FILTER", pal::VIOLET, true, .5f);
        }
        master = knob(false, gpar[G_MASTER], "MASTER", pal::TEXT, false, gpar[G_MASTER].p != nullptr ? gpar[G_MASTER].p->convertTo0to1(0) : .8f);
        curve = knob(false, gpar[G_XFCURVE], "XF CURVE", pal::AMBER, false, .5f);
        xfader = mk<XFader>(false);
        libBtn = btn(false, "LIBRARY", pal::TEXT, nullptr, [this](bool dn) { if (dn && host.openLibrary) host.openLibrary(); });
        libBtn->textMax = 14;
        for (int d = 0; d < 2; ++d) {
            const int dk = d;
            // headphone cue (PFL): on the mixer, under each channel
            pfl[d] = btn(false, d == 0 ? "PFL A" : "PFL B", pal::DECK[d], [this, dk] { return on(dk, kPfl); }, [this, dk](bool dn) { if (dn) toggle(dk, kPfl); });
            pfl[d]->textMax = 12;
        }
        setupBtn = btn(false, "SETUP", pal::DIM, [this] { return sheetOpen; }, [this](bool dn) { if (dn) openSheet(); });
        setupBtn->textMax = 12;

        for (int d = 0; d < 2; ++d) {
            const int dk = d;
            const uint32 col = pal::DECK[d];
            trimK[d] = knob(true, dpar[d][kTrim], "TRIM", col, true, .5f);
            fxParamK[d] = mk<Knob>(true, dpar[d][kFxparam], Txt([this, dk] { return juce::String(FX_PARAM_NAMES[fxType(dk)]); }), pal::RED, false, .5f);
            const char* names[3] = { "A", "THRU", "B" };
            for (int k = 0; k < 3; ++k) {
                const int kk = k;
                xfAssign[d][k] = btn(true, names[k], k == 1 ? pal::TEXT : pal::DECK[k / 2], [this, dk, kk] { return juce::roundToInt(get(dk, kXfassign)) == kk; },
                                     [this, dk, kk](bool dn) { if (dn) setV(dpar[dk][kXfassign], (float) kk, true); });
            }
            deckSw[d][0] = btn(true, Txt([this, dk] { return juce::String(on(dk, kRpm45) ? "45 RPM" : "33 RPM"); }), pal::AMBER,
                               [this, dk] { return on(dk, kRpm45); }, [this, dk](bool dn) { if (dn) toggle(dk, kRpm45); });
            deckSw[d][1] = btn(true, "QUANTIZE", pal::CYAN, [this, dk] { return on(dk, kQuant); }, [this, dk](bool dn) { if (dn) toggle(dk, kQuant); });
            deckSw[d][2] = btn(true, "PFL CUE", col, [this, dk] { return on(dk, kPfl); }, [this, dk](bool dn) { if (dn) toggle(dk, kPfl); });
        }
        hamsterBtn = btn(true, "HAMSTER", pal::AMBER, [this] { return val(gpar[G_HAMSTER]) > .5f; },
                         [this](bool dn) { if (dn) setV(gpar[G_HAMSTER], val(gpar[G_HAMSTER]) > .5f ? 0.f : 1.f, true); });
        chCurve = knob(true, gpar[G_FADERCURVE], "CURVE", pal::AMBER, false, .3f);
        doneBtn = btn(true, "DONE", pal::GREEN, nullptr, [this](bool dn) { if (dn) closeSheet(); });
        doneBtn->textMax = 14;
    }

    void openSheet() { releaseAll(); sheetOpen = true; view.repaint(); }
    void closeSheet() { releaseAll(); sheetOpen = false; view.repaint(); }

    // ============================================================================================== layout
    void layout(float W, float H) {
        dp = clampf(std::min(W / kPhoneWdp, H / kPhoneHdp), .6f, 6.f);
        const float g = 3 * dp;
        ts = dp * clampf(std::min(W / (800 * dp), H / (360 * dp)), .85f, 2.2f);
        autoZoom = clampf(W / dp / 110.f, 6, 12);

        const float topH = clampf(H * .34f, 110 * dp, 330 * dp);
        const float hdrH = clampf(topH * .18f, 20 * dp, 42 * dp);
        const float ovH = clampf(topH * .085f, 8 * dp, 22 * dp);
        const float waveH = (topH - 2 * hdrH - 2 * ovH) / 2;
        float y = 0;
        const auto row = [&](Ctrl* c, float h) { c->r = ltrb(0, y, W, y + h); y += h; };
        row(headers[0], hdrH); row(overviews[0], ovH); row(waves[0], waveH);
        row(waves[1], waveH); row(overviews[1], ovH); row(headers[1], hdrH);

        const float botH = clampf(H * .29f, 96 * dp, 290 * dp);
        const float midTop = topH + g, midH = H - topH - botH - 2 * g;
        const float mixW = clampf(W * .27f, 196 * dp, 440 * dp);
        const float deckW = (W - mixW) / 2 - g;
        mixRect = ltrb(deckW + g, midTop, deckW + g + mixW, H);

        for (int d = 0; d < 2; ++d) {
            const float x0 = d == 0 ? 0 : W - deckW, x1 = x0 + deckW;
            deckMid[d] = ltrb(x0, midTop, x1, midTop + midH);
            deckBot[d] = ltrb(x0, H - botH, x1, H);

            // platter at the outer edge, pitch fader at the mixer side, buttons between
            const float diam = std::min(midH - 2 * g, deckW * .5f);
            Platter& p = *platters[d];
            p.rad = diam / 2;
            p.cx = d == 0 ? x0 + g + p.rad + 2 * dp : x1 - g - p.rad - 2 * dp;
            p.cy = midTop + midH / 2;
            p.r = ltrb(p.cx - p.rad, p.cy - p.rad, p.cx + p.rad, p.cy + p.rad);
            const float pitchW = clampf(deckW * .13f, 32 * dp, 64 * dp);
            const float px0 = d == 0 ? x1 - pitchW - g : x0 + g;
            pitchF[d]->r = ltrb(px0, midTop + 14 * ts, px0 + pitchW, midTop + midH - g);
            const float bx0 = d == 0 ? p.r.getRight() + 2 * g : px0 + pitchW + 2 * g;
            const float bx1 = d == 0 ? px0 - 2 * g : p.r.getX() - 2 * g;
            const float bw = (bx1 - bx0 - g) / 2, bh = (midH - 3 * g - 2 * g) / 4;
            for (int i = 0; i < 8; ++i) {
                const int rowI = i / 2, colI = i % 2;
                const float bx = bx0 + (float) colI * (bw + g), by = midTop + g + (float) rowI * (bh + g);
                transport[d][i]->r = ltrb(bx, by, bx + bw, by + bh);
            }

            // under the deck: mode tabs + FX on a toolbar, pads and the loop block
            const float tbH = clampf(botH * .22f, 24 * dp, 48 * dp);
            const float by0 = H - botH + g;
            const float tabsW = deckW * .5f;
            const float tx = x0 + g;
            const float tw = (tabsW - 3 * g) / 4;
            for (int m = 0; m < 4; ++m) modeTabs[d][m]->r = ltrb(tx + (float) m * (tw + g), by0, tx + (float) m * (tw + g) + tw, by0 + tbH);
            const float fx0 = tx + tabsW + 2 * g, fx1 = x1 - g;
            const float knobW = tbH * 1.3f;
            const float fxw = fx1 - fx0 - knobW - 3 * g;
            fxBtns[d][0]->r = ltrb(fx0, by0, fx0 + fxw * .45f, by0 + tbH);
            fxBtns[d][1]->r = ltrb(fxBtns[d][0]->r.getRight() + g, by0, fxBtns[d][0]->r.getRight() + g + fxw * .22f, by0 + tbH);
            fxBtns[d][2]->r = ltrb(fxBtns[d][1]->r.getRight() + g, by0, fx0 + fxw + g * 2, by0 + tbH);
            fxDepth[d]->r = ltrb(fx1 - knobW, by0 - g, fx1, by0 + tbH + g);

            const float py0 = by0 + tbH + 2 * g, py1 = H - g;
            const float loopW = clampf(deckW * .28f, 76 * dp, 220 * dp);
            const float padX0 = d == 0 ? x0 + g : x0 + g + loopW + g;
            const float padX1 = d == 0 ? x1 - g - loopW - g : x1 - g;
            const float pw = (padX1 - padX0 - 3 * g) / 4, ph = (py1 - py0 - g) / 2;
            for (int s = 0; s < 8; ++s) {
                const int colI = s % 4, rowI = s / 4;   // bottom row = slots 1-4, like the pads under your fingers
                const float sx = padX0 + (float) colI * (pw + g), sy = py1 - (float) (rowI + 1) * ph - (float) rowI * g;
                pads[d][s]->r = ltrb(sx, sy, sx + pw, sy + ph);
            }
            const float lx0 = d == 0 ? x1 - g - loopW : x0 + g;
            const float lw = (loopW - g) / 2, lh = (py1 - py0 - 2 * g) / 3;
            loopBtns[d][0]->r = ltrb(lx0, py0, lx0 + lw, py0 + lh);
            loopBtns[d][1]->r = ltrb(lx0 + lw + g, py0, lx0 + loopW, py0 + lh);
            loopBtns[d][2]->r = ltrb(lx0, py0 + lh + g, lx0 + lw, py0 + 2 * lh + g);
            loopBtns[d][3]->r = ltrb(lx0 + lw + g, py0 + lh + g, lx0 + loopW, py0 + 2 * lh + g);
            loopBtns[d][4]->r = ltrb(lx0, py0 + 2 * (lh + g), lx0 + loopW, py1);
        }

        // the mixer
        const float mx0 = mixRect.getX() + g, mx1 = mixRect.getRight() - g, mw = mx1 - mx0;
        float my = mixRect.getY() + g;
        const float row0 = clampf(mixRect.getHeight() * .14f, 30 * dp, 56 * dp);
        libBtn->r = ltrb(mx0, my, mx0 + mw * .42f, my + row0);
        const float kw = (mw * .58f - g) / 2;
        master->r = ltrb(libBtn->r.getRight() + g, my - g, libBtn->r.getRight() + g + kw, my + row0 + g);
        curve->r = ltrb(master->r.getRight(), my - g, mx1, my + row0 + g);
        my += row0 + 2 * g;
        const float xfH = clampf(mixRect.getHeight() * .2f, 44 * dp, 96 * dp);
        const float statusH = 12 * ts;
        statusR = ltrb(mx0, mixRect.getBottom() - statusH - g, mx1, mixRect.getBottom() - g);
        xfader->r = ltrb(mx0, statusR.getY() - xfH, mx1, statusR.getY() - g);
        // PFL A | SETUP | PFL B, just above the crossfader
        const float rowH = clampf(mixRect.getHeight() * .1f, 22 * dp, 40 * dp);
        const float rowY = xfader->r.getY() - g - rowH;
        const float pflW = mw * .34f;
        pfl[0]->r = ltrb(mx0, rowY, mx0 + pflW, rowY + rowH);
        pfl[1]->r = ltrb(mx1 - pflW, rowY, mx1, rowY + rowH);
        setupBtn->r = ltrb(pfl[0]->r.getRight() + g, rowY, pfl[1]->r.getX() - g, rowY + rowH);
        const float meterW = clampf(mw * .12f, 20 * dp, 44 * dp);
        const float hw = (mw - meterW - 2 * g) / 2;
        const float cellH = clampf(mixRect.getHeight() * .17f, 36 * dp, 92 * dp);
        const float faderTop = my + 2 * cellH + g, faderBot = rowY - g;
        // short screens (phones): the channel fader goes beside the knobs, so it keeps a useful throw
        const bool side = faderBot - faderTop < 80 * dp;
        for (int d = 0; d < 2; ++d) {
            const float hx = d == 0 ? mx0 : mx1 - hw;
            const float kw2 = side ? hw * .62f : hw;                      // knob block width
            const float kx0 = side ? (d == 0 ? hx : hx + hw - kw2) : hx;  // knobs on the outer side
            const float ch = side ? (faderBot - my) / 2 : cellH;
            for (int k = 0; k < 4; ++k) {
                const int colI = k % 2, rowI = k / 2;
                const float kx = kx0 + (float) colI * kw2 / 2, ky = my + (float) rowI * ch;
                eqK[d][k]->r = ltrb(kx, ky, kx + kw2 / 2, ky + ch);
            }
            if (side) {
                const float fw = hw - kw2 - g;
                const float fx = d == 0 ? hx + kw2 + g : hx;
                chFader[d]->r = ltrb(fx, my, fx + fw, faderBot);
            } else {
                const float fw = clampf(hw * .42f, 28 * dp, 60 * dp);
                const float fcx = hx + hw / 2;
                chFader[d]->r = ltrb(fcx - fw / 2, faderTop, fcx + fw / 2, faderBot);
            }
        }
        const float mcx = mx0 + hw + g, mtop = my, mbot = faderBot;
        const float bw4 = (meterW - 3 * dp) / 4;
        for (int i = 0; i < 4; ++i) meterR[i] = ltrb(mcx + (float) i * (bw4 + dp), mtop, mcx + (float) i * (bw4 + dp) + bw4, mbot);

        layoutSheet(W, H, g);
    }

    void layoutSheet(float W, float H, float g) {
        const float mxs = clampf(W * .04f, 8 * dp, 80 * dp), mys = clampf(H * .05f, 8 * dp, 80 * dp);
        const float sw = std::min(W - 2 * mxs, 980 * dp), sh = std::min(H - 2 * mys, 520 * dp);
        sheetR = ltrb((W - sw) / 2, (H - sh) / 2, (W + sw) / 2, (H + sh) / 2);
        const float pad = clampf(sw * .015f, 6 * dp, 16 * dp);
        const float titleH = clampf(sh * .12f, 30 * dp, 52 * dp);
        sheetTitleR = ltrb(sheetR.getX() + pad, sheetR.getY() + pad, sheetR.getRight() - pad, sheetR.getY() + pad + titleH);
        doneBtn->r = ltrb(sheetTitleR.getRight() - clampf(sw * .14f, 84 * dp, 160 * dp), sheetTitleR.getY(), sheetTitleR.getRight(), sheetTitleR.getBottom());
        const float top = sheetTitleR.getBottom() + pad, bot = sheetR.getBottom() - pad;
        const float inner = sheetR.getWidth() - 2 * pad, colW = inner * .37f;
        sheetCol[0] = ltrb(sheetR.getX() + pad, top, sheetR.getX() + pad + colW, bot);
        sheetCol[1] = ltrb(sheetR.getRight() - pad - colW, top, sheetR.getRight() - pad, bot);
        sheetMid = ltrb(sheetCol[0].getRight() + pad, top, sheetCol[1].getX() - pad, bot);
        const float capH = clampf(sh * .055f, 13 * dp, 22 * dp);
        const float h = bot - top;
        const float knobsH = (h - 3 * capH - 2 * g) * .44f, rowH = (h - 3 * capH - 2 * g - knobsH) / 2;
        for (int d = 0; d < 2; ++d) {
            const Rf c = sheetCol[d];
            float y = c.getY();
            sheetLabelY[0] = y + capH * .8f;
            y += capH;
            const float kw = std::min(c.getWidth() / 2, knobsH * 1.25f);
            const float kcx0 = c.getX() + c.getWidth() * .25f, kcx1 = c.getX() + c.getWidth() * .75f;
            trimK[d]->r = ltrb(kcx0 - kw / 2, y, kcx0 + kw / 2, y + knobsH);
            fxParamK[d]->r = ltrb(kcx1 - kw / 2, y, kcx1 + kw / 2, y + knobsH);
            y += knobsH + g;
            sheetLabelY[1] = y + capH * .8f;
            y += capH;
            const float bw = (c.getWidth() - 2 * g) / 3;
            for (int k = 0; k < 3; ++k) xfAssign[d][k]->r = ltrb(c.getX() + (float) k * (bw + g), y, c.getX() + (float) k * (bw + g) + bw, y + rowH);
            y += rowH + g;
            sheetLabelY[2] = y + capH * .8f;
            y += capH;
            for (int k = 0; k < 3; ++k) deckSw[d][k]->r = ltrb(c.getX() + (float) k * (bw + g), y, c.getX() + (float) k * (bw + g) + bw, y + rowH);
        }
        float y = sheetMid.getY() + capH;
        hamsterBtn->r = ltrb(sheetMid.getX(), y, sheetMid.getRight(), y + rowH);
        y += rowH + g + capH;
        const float kh = std::min(knobsH, sheetMid.getBottom() - y - capH * 2);
        const float kw = std::min(sheetMid.getWidth(), kh * 1.25f);
        chCurve->r = ltrb(sheetMid.getCentreX() - kw / 2, y, sheetMid.getCentreX() + kw / 2, y + kh);
    }

    // ============================================================================================== touch
    std::map<int, Ctrl*> owner;            // finger (MouseInputSource index) -> the control it holds
    std::map<int, juce::Point<float>> fingerAt;
    int pinchA = -1, pinchB = -1;
    float pinchD0 = 0, pinchZ0 = 0;
    uint32 zoomShownAt = 0;

    bool inWaves(float x, float y) const { return waves[0]->r.contains(x, y) || waves[1]->r.contains(x, y); }

    void pointerDown(int id, float x, float y) {
        fingerAt[id] = { x, y };
        if (owner.count(id) != 0) pointerUp(id, x, y);   // a source that never sent its up
        if (sheetOpen) {
            if (!sheetR.contains(x, y)) { if (owner.empty()) closeSheet(); return; }
            for (int i = (int) sheetCtrls.size() - 1; i >= 0; --i) {
                Ctrl* c = sheetCtrls[(size_t) i];
                if (c->hit(x, y)) { if (c->down(id, x, y)) owner[id] = c; break; }
            }
            return;
        }
        if (pinchA < 0 && startPinch(id, x, y)) return;
        if (pinchA >= 0 && inWaves(x, y)) return;   // a third finger on the waves while zooming: ignored
        for (int i = (int) ctrls.size() - 1; i >= 0; --i) {
            Ctrl* c = ctrls[(size_t) i];
            if (c->hit(x, y)) { if (c->down(id, x, y)) owner[id] = c; break; }
        }
    }
    void pointerMove(int id, float x, float y) {
        fingerAt[id] = { x, y };
        if (pinchA >= 0 && (id == pinchA || id == pinchB)) { movePinch(); return; }
        auto it = owner.find(id);
        if (it != owner.end() && it->second->pointer == id) it->second->move(x, y);
    }
    void pointerUp(int id, float x, float y) {
        fingerAt.erase(id);
        if (id == pinchA || id == pinchB) { endPinch(); return; }
        auto it = owner.find(id);
        if (it == owner.end()) return;
        Ctrl* c = it->second;
        owner.erase(it);
        if (c->pointer == id) { c->move(x, y); c->up(false); }
    }
    void releaseAll() {
        for (auto& [id, c] : owner) if (c->pointer >= 0) c->up(true);
        owner.clear();
        endPinch();
    }

    // pinch on the waveforms: both decks zoom together (2..32 s across), remembered in the settings file
    bool startPinch(int id, float x, float y) {
        if (!inWaves(x, y)) return false;
        int other = -1;
        float ox = 0;
        for (auto& [fid, c] : owner)
            if (auto* w = dynamic_cast<WaveC*>(c)) { other = fid; ox = w->lastX; break; }
        if (other < 0) return false;
        Ctrl* w = owner[other];
        owner.erase(other);
        if (w->pointer >= 0) w->up(true);
        pinchA = other;
        pinchB = id;
        pinchD0 = std::max(24 * dp, std::abs(x - ox));
        pinchZ0 = zoomSec();
        zoomShownAt = nowMs();
        return true;
    }
    void movePinch() {
        auto a = fingerAt.find(pinchA), b = fingerAt.find(pinchB);
        if (a == fingerAt.end() || b == fingerAt.end()) return;
        const float dd = std::max(24 * dp, std::abs(a->second.x - b->second.x));
        setWaveZoom(pinchZ0 * pinchD0 / dd);   // fingers apart = fewer seconds = zoom in
        zoomShownAt = nowMs();
    }
    void endPinch() {
        if (pinchA < 0) return;
        pinchA = pinchB = -1;
        zoomShownAt = nowMs();
    }
    void zoomBy(float factor) {   // trackpad pinch / ctrl + wheel
        setWaveZoom(zoomSec() * factor);
        zoomShownAt = nowMs();
    }

    // ============================================================================================== drawing
    static void arc(G& g, Rf oval, float startDeg, float sweepDeg, float w, uint32 c, bool roundCap) {
        if (sweepDeg <= 0) return;
        juce::Path p;
        const float a0 = juce::degreesToRadians(startDeg + 90.f);
        p.addCentredArc(oval.getCentreX(), oval.getCentreY(), oval.getWidth() / 2, oval.getHeight() / 2, 0, a0, a0 + juce::degreesToRadians(sweepDeg), true);
        g.setColour(juce::Colour(c));
        g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, roundCap ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
    }
    static void line(G& g, float x0, float y0, float x1, float y1, float w, uint32 c, bool roundCap) {
        juce::Path p;
        p.startNewSubPath(x0, y0);
        p.lineTo(x1, y1);
        g.setColour(juce::Colour(c));
        g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::mitered, roundCap ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
    }
    static void circle(G& g, float cx, float cy, float rr, uint32 c) { g.setColour(juce::Colour(c)); g.fillEllipse(cx - rr, cy - rr, 2 * rr, 2 * rr); }

    static juce::String ellipsize(const juce::String& s, float maxW, const juce::Font& f) {
        const juce::String dots = U("\xe2\x80\xa6");
        const float room = std::max(0.f, maxW - textW(f, dots));
        int lo = 0, hi = s.length();
        while (lo < hi) { const int mid = (lo + hi + 1) / 2; if (textW(f, s.substring(0, mid)) <= room) lo = mid; else hi = mid - 1; }
        return lo <= 0 ? juce::String() : s.substring(0, lo) + dots;
    }
    static void drawText(G& g, const juce::String& s, const juce::Font& f, float x, float y, uint32 c, Align a) {
        if (s.isEmpty()) return;
        const float w = a == Align::left ? 0 : textW(f, s);
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, s, a == Align::left ? x : a == Align::centre ? x - w / 2 : x - w, y);
        g.setColour(juce::Colour(c));
        ga.draw(g);
    }
    // text with a baseline at y, shrunk then ellipsised to fit maxW (0 = no limit)
    void label(G& g, const juce::String& text, float x, float y, float size, uint32 c, Align a, float maxW) const {
        if (text.isEmpty()) return;
        juce::String s = text;
        juce::Font f = bold(size);
        if (maxW > 0) {
            const float w = textW(f, s);
            if (w > maxW) {
                f = bold(std::max(size * .75f, size * maxW / w));
                if (textW(f, s) > maxW) s = ellipsize(s, maxW, f);
            }
        }
        drawText(g, s, f, x, y, c, a);
    }
    void panel(G& g, Rf r, uint32 c, float rad) const {
        g.setColour(juce::Colour(c));
        g.fillRoundedRectangle(r.reduced(1), rad);
    }

    void drawButton(G& g, Rf r, const juce::String& s, uint32 color, bool lit, bool pressed, float textMax) const {
        const float rad = std::min(6 * dp, r.getHeight() * .2f);
        g.setColour(juce::Colour(lit ? pal::mix(pal::PANEL, color, .30f) : (pressed ? pal::EDGE_HI : pal::RAISED)));
        g.fillRoundedRectangle(r, rad);
        const float sw = lit ? 2 * dp : dp;
        g.setColour(juce::Colour(lit ? color : (pressed ? pal::TEXT : pal::EDGE)));
        g.drawRoundedRectangle(r.reduced(sw / 2), rad, sw);
        const float size = std::min(textMax * ts, r.getHeight() * .42f);
        label(g, s, r.getCentreX(), r.getCentreY() + size * .36f, size, lit ? pal::WHITE : (color == pal::DIM ? pal::TEXT : pal::mix(pal::TEXT, color, .25f)),
              Align::centre, r.getWidth() - 6 * dp);
    }

    double pitchPercent(int d) const {
        const auto& L = live[d];
        if (L.bpm0 > 0 && L.bpm > 0 && !L.touched) return (L.bpm / L.bpm0 - 1) * 100;
        return (double) get(d, kPitch) * RANGES[rangeIndex(d)] * 100;
    }
    static juce::String signedFixed(double x, int dec) {
        const juce::String a(std::abs(x), dec);
        const bool zero = a.containsOnly("0.");
        return juce::String(x < 0 && !zero ? "-" : "+") + a;
    }

    float flag(G& g, const juce::String& s, uint32 c, float rx, Rf r, float size) const {
        const juce::Font f = bold(size * .85f);
        const float w = textW(f, s) + 8 * dp;
        const Rf t = ltrb(rx - w, r.getY() + r.getHeight() * .22f, rx, r.getBottom() - r.getHeight() * .22f);
        g.setColour(juce::Colour(c));
        g.drawRoundedRectangle(t, 3 * dp, 1.5f * dp);
        label(g, s, t.getCentreX(), t.getCentreY() + size * .3f, size * .85f, c, Align::centre, 0);
        return rx - w - 4 * dp;
    }

    void drawHeader(G& g, Header& h) const {
        const int d = h.deck;
        const Rf r = h.r;
        const auto& L = live[d];
        g.setColour(juce::Colour(pal::PANEL));
        g.fillRect(r);
        const float H = r.getHeight(), pad = 4 * dp;
        // deck tag: white when it has the MIDI focus
        const Rf tag = ltrb(r.getX() + pad, r.getY() + 2 * dp, r.getX() + pad + H * 1.1f, r.getBottom() - 2 * dp);
        g.setColour(juce::Colour(focus == d ? pal::WHITE : pal::DECK[d]));
        g.fillRoundedRectangle(tag, 4 * dp);
        label(g, d == 0 ? "A" : "B", tag.getCentreX(), tag.getCentreY() + H * .24f, H * .66f, focus == d ? pal::DECK[d] : pal::BLACK, Align::centre, 0);
        const float x = tag.getRight() + 2 * pad;

        // right side, right to left: time left, BPM + pitch, key
        const float big = std::min(H * .72f, 22 * ts);
        const bool has = hasTrack(d);
        const double len = L.len, left = has ? std::max(0.0, len - L.pos) : 0;
        juce::Font mf = mono(big);
        const uint32 tc = has && left < 30 && len > 0 ? (((nowMs() / 400) % 2 == 0 || !playing(d)) ? pal::RED : 0xff7a1d18u) : pal::TEXT;
        float rx = r.getRight() - pad;
        drawText(g, has ? pal::time(-left) : juce::String("--:--.-"), mf, rx, r.getCentreY() + big * .36f, tc, Align::right);
        rx -= textW(mf, "-00:00.0") + 3 * pad;
        const juce::String bs = has && L.bpm > 0 ? juce::String(L.bpm, 1) : juce::String("---.-");
        drawText(g, bs, mf, rx, r.getCentreY() + big * .36f, pal::DECK[d], Align::right);
        rx -= textW(mf, "000.0") + 2 * pad;
        // pitch %
        const float small = big * .62f;
        mf = mono(small);
        drawText(g, signedFixed(pitchPercent(d), 1) + "%", mf, rx, r.getCentreY() + small * .36f, pal::DIM, Align::right);
        rx -= textW(mf, "+00.0%") + 2 * pad;
        if (has && L.key >= 0 && L.key < 24) {
            const float kw = textW(bold(small), "12A") + 2 * pad;
            const Rf kr = ltrb(rx - kw, r.getY() + H * .2f, rx, r.getBottom() - H * .2f);
            g.setColour(juce::Colour(pal::keyColour(L.key)));
            g.fillRoundedRectangle(kr, 3 * dp);
            label(g, pal::camelot(L.key), kr.getCentreX(), kr.getCentreY() + small * .36f, small, pal::BLACK, Align::centre, 0);
            rx -= kw + 2 * pad;
        }
        // state flags
        float fx = rx;
        if (L.synced) fx = flag(g, "SYNC", pal::CYAN, fx, r, small);
        if (L.looping) fx = flag(g, "LOOP", pal::GREEN, fx, r, small);
        if (on(d, kKeylock)) fx = flag(g, "KEY", pal::AMBER, fx, r, small);
        if (on(d, kSlip)) fx = flag(g, "SLIP", pal::VIOLET, fx, r, small);
        if (on(d, kRpm45)) fx = flag(g, "45", pal::AMBER, fx, r, small);
        if (on(d, kPfl)) fx = flag(g, "PFL", pal::DECK[d], fx, r, small);
        if (L.censor) fx = flag(g, "CENSOR", pal::RED, fx, r, small);

        // title + artist (or loading / error)
        const float tw = fx - x - pad;
        const float ty = r.getCentreY() + H * .2f;
        if (L.loading) {
            label(g, "LOADING" + juce::String::repeatedString(".", 1 + (int) ((nowMs() - L.loadStart) / 300 % 3)), x, ty, std::min(H * .5f, 15 * ts), pal::AMBER, Align::left, tw);
        } else if (L.err.isNotEmpty()) {
            label(g, L.err, x, ty, std::min(H * .5f, 15 * ts), pal::RED, Align::left, tw);
        } else if (has) {
            const float ts1 = std::min(H * .52f, 16 * ts);
            const float titleW = std::min(tw, textW(bold(ts1), L.title));
            label(g, L.title, x, ty, ts1, pal::TEXT, Align::left, tw);
            if (L.artist.isNotEmpty() && tw - titleW > 40 * dp)
                label(g, L.artist, x + titleW + 3 * pad, ty, ts1 * .8f, pal::DIM, Align::left, tw - titleW - 3 * pad);
        } else {
            const juce::String sel = host.selectedTitle ? host.selectedTitle() : juce::String();
            label(g, sel.isNotEmpty() ? "TAP HERE TO LOAD  " + sel : U("NO TRACK  \xc2\xb7  open LIBRARY"), x, ty, std::min(H * .48f, 14 * ts),
                  sel.isNotEmpty() ? pal::DECK[d] : pal::FAINT, Align::left, tw);
        }
    }

    void drawPitchReadout(G& g) const {
        for (int d = 0; d < 2; ++d) {
            const Rf p = pitchF[d]->r;
            const double pct = (double) get(d, kPitch) * RANGES[rangeIndex(d)] * 100;
            label(g, signedFixed(pct, 1), p.getCentreX(), p.getY() - 3 * dp, 11 * ts, juce::exactlyEqual(pct, 0.0) ? pal::GREEN : pal::DECK[d], Align::centre, p.getWidth() + 8 * dp);
        }
    }

    void drawWave(G& g, WaveC& w) {
        const int d = w.deck;
        const Rf r = w.r;
        const auto& L = live[d];
        g.setColour(juce::Colour(pal::WELL));
        g.fillRect(r);
        const float cx = r.getCentreX(), pps = w.pxPerSec();
        const double pos = L.pos;
        if (!hasTrack(d)) {
            g.setColour(juce::Colour(pal::EDGE));
            g.fillRect(ltrb(r.getX(), r.getCentreY() - dp / 2, r.getRight(), r.getCentreY() + dp / 2));
            return;
        }
        {
            G::ScopedSaveState save(g);
            g.reduceClipRegion(r.getSmallestIntegerContainer());
            const double t0 = pos - (cx - r.getX()) / pps, t1 = pos + (r.getRight() - cx) / pps;
            const auto X = [&](double t) { return (float) (cx + (t - pos) * pps); };
            // loop shading
            if (L.looping && L.loopB > L.loopA) {
                g.setColour(juce::Colour(0x3334d058u));
                g.fillRect(ltrb(X(L.loopA), r.getY(), X(L.loopB), r.getBottom()));
            }
            // waveform tiles
            WaveData& wd = wave[d];
            const int th = std::max(8, juce::roundToInt(r.getHeight() * pxScale));
            const double cps = WaveData::kCols;
            const int k0 = (int) std::floor(std::max(0.0, t0) * cps / WaveData::kTile), k1 = (int) std::floor(std::max(0.0, t1) * cps / WaveData::kTile);
            const float tileW = (float) (WaveData::kTile / cps * pps);
            g.setImageResamplingQuality(G::mediumResamplingQuality);
            g.setOpacity(1.f);   // drawImage takes the current colour's alpha (the loop shading's)
            for (int k = k0; k <= k1; ++k) {
                const juce::Image img = wd.tile(k, th);
                if (!img.isValid()) continue;
                g.drawImage(img, Rf(X(k * WaveData::kTile / cps), r.getY(), tileW, r.getHeight()), juce::RectanglePlacement::stretchToFit);
            }
            // beatgrid: beat ticks, bar lines every 4 beats
            const double bpm = L.bpm0, first = L.first;
            if (bpm > 20) {
                const double beat = 60.0 / bpm;
                const auto b0 = (juce::int64) std::floor((t0 - first) / beat), b1 = (juce::int64) std::ceil((t1 - first) / beat);
                if (b1 - b0 < 400) {
                    for (auto b = b0; b <= b1; ++b) {
                        const double t = first + (double) b * beat;
                        if (t < 0) continue;
                        const float x = X(t);
                        const bool bar = ((b % 4) + 4) % 4 == 0;
                        const float sw = bar ? 2 * dp : dp, tick = r.getHeight() * (bar ? .2f : .1f);
                        g.setColour(juce::Colour(bar ? 0xaaffffffu : 0x44ffffffu));
                        g.fillRect(ltrb(x - sw / 2, r.getY(), x + sw / 2, r.getY() + tick));
                        g.fillRect(ltrb(x - sw / 2, r.getBottom() - tick, x + sw / 2, r.getBottom()));
                        if (bar) { g.setColour(juce::Colour(0x22ffffffu)); g.fillRect(ltrb(x - dp / 2, r.getY() + tick, x + dp / 2, r.getBottom() - tick)); }
                    }
                }
            }
            // hot cues
            for (int i = 0; i < 8; ++i) {
                const double t = L.cueT[i];
                if (t < 0 || t < t0 - 1 || t > t1 + 1) continue;
                const float x = X(t);
                const uint32 c = 0xff000000u | L.cueRgb[i];
                g.setColour(juce::Colour(c));
                g.fillRect(ltrb(x - dp, r.getY(), x + dp, r.getBottom()));
                const float s = std::min(14 * dp, r.getHeight() * .3f);
                const Rf box = ltrb(x, r.getY(), x + s * 1.1f, r.getY() + s);
                g.fillRect(box);
                label(g, juce::String(i + 1), box.getCentreX(), box.getBottom() - s * .2f, s * .8f, pal::BLACK, Align::centre, 0);
            }
            // the cue point: an orange triangle top and bottom
            if (L.cue >= t0 && L.cue <= t1) {
                const float x = X(L.cue), s = std::min(9 * dp, r.getHeight() * .18f);
                juce::Path p;
                p.addTriangle(x - s, r.getY(), x + s, r.getY(), x, r.getY() + s);
                p.addTriangle(x - s, r.getBottom(), x + s, r.getBottom(), x, r.getBottom() - s);
                g.setColour(juce::Colour(pal::AMBER));
                g.fillPath(p);
            }
            // playhead
            g.setColour(juce::Colour(pal::WHITE));
            g.fillRect(ltrb(cx - 1.2f * dp, r.getY(), cx + 1.2f * dp, r.getBottom()));
            if (handOn(d)) {
                g.setColour(juce::Colour(pal::alpha(pal::DECK[d], .25f)));
                g.fillRect(ltrb(cx - 6 * dp, r.getY(), cx + 6 * dp, r.getBottom()));
            }
        }
        // the deck's colour on the left edge
        g.setColour(juce::Colour(pal::DECK[d]));
        g.fillRect(ltrb(r.getX(), r.getY(), r.getX() + 3 * dp, r.getBottom()));
    }

    void drawOverview(G& g, Overview& o) {
        const int d = o.deck;
        const Rf r = o.r;
        const auto& L = live[d];
        g.setColour(juce::Colour(0xff050607u));
        g.fillRect(r);
        if (!hasTrack(d)) return;
        const juce::Image img = wave[d].overview(std::max(4, juce::roundToInt(r.getWidth() * pxScale)), std::max(4, juce::roundToInt(r.getHeight() * pxScale)));
        if (img.isValid()) { g.setImageResamplingQuality(G::mediumResamplingQuality); g.setOpacity(1.f); g.drawImage(img, r, juce::RectanglePlacement::stretchToFit); }
        const double len = L.len;
        if (len <= 0) return;
        const float px = (float) (r.getX() + L.pos / len * r.getWidth());
        g.setColour(juce::Colour(0x99000000u));
        g.fillRect(ltrb(r.getX(), r.getY(), px, r.getBottom()));
        if (L.looping) {
            g.setColour(juce::Colour(0x6634d058u));
            g.fillRect(ltrb((float) (r.getX() + L.loopA / len * r.getWidth()), r.getY(), (float) (r.getX() + L.loopB / len * r.getWidth()) + dp, r.getBottom()));
        }
        for (int i = 0; i < 8; ++i) {
            if (L.cueT[i] < 0) continue;
            g.setColour(juce::Colour(0xff000000u | L.cueRgb[i]));
            const float x = (float) (r.getX() + L.cueT[i] / len * r.getWidth());
            g.fillRect(ltrb(x - dp, r.getY(), x + dp, r.getBottom()));
        }
        g.setColour(juce::Colour(pal::WHITE));
        g.fillRect(ltrb(px - dp, r.getY(), px + dp, r.getBottom()));
    }

    void drawPlatter(G& g, Platter& p) const {
        const int d = p.deck;
        const float cx = p.cx, cy = p.cy, R = p.rad;
        const auto& L = live[d];
        // rim with strobe dots
        circle(g, cx, cy, R, handOn(d) ? pal::DECK[d] : 0xff5c626au);
        circle(g, cx, cy, R * .955f, 0xff2a2e33u);
        const double ang = L.angle;
        const float rot = (float) ((ang - std::floor(ang)) * 2 * kPi);
        {
            G::ScopedSaveState save(g);
            g.addTransform(juce::AffineTransform::rotation(rot, cx, cy));
            const float dotR = std::max(1.2f * dp, R * .018f);
            for (int i = 0; i < 48; ++i) {
                const double a = i * kPi * 2 / 48;
                circle(g, cx + (float) std::cos(a) * R * .91f, cy + (float) std::sin(a) * R * .91f, dotR, 0xffc8ccd2u);
            }
            // vinyl and grooves
            circle(g, cx, cy, R * .86f, 0xff0c0d0fu);
            g.setColour(juce::Colour(0xff1d2024u));
            const float gw = std::max(1.f, R * .006f);
            for (float k = .44f; k < .84f; k += .045f) g.drawEllipse(cx - R * k, cy - R * k, 2 * R * k, 2 * R * k, gw);
            // sheen
            const Rf oval(cx - R * .65f, cy - R * .65f, R * 1.3f, R * 1.3f);
            arc(g, oval, 200, 40, R * .12f, 0x14ffffffu, false);
            arc(g, oval, 20, 40, R * .12f, 0x14ffffffu, false);
            // the marker line (a sticker on the record)
            line(g, cx, cy - R * .40f, cx, cy - R * .84f, std::max(2 * dp, R * .03f), pal::WHITE, true);
            // label
            const bool has = hasTrack(d);
            circle(g, cx, cy, R * .36f, has ? pal::DECK[d] : pal::DECK_DARK[d]);
            if (has) {
                label(g, L.title.toUpperCase(), cx, cy - R * .12f, R * .085f, 0xff101010u, Align::centre, R * .56f);
                label(g, L.artist, cx, cy + R * .22f, R * .07f, 0xff202020u, Align::centre, R * .5f);
            }
        }
        circle(g, cx, cy, std::max(2.5f * dp, R * .03f), 0xffd8dbe0u);
        // loading: a turning arc around the rim (the plugin's loader has no progress figure)
        if (L.loading) {
            const float a0 = (float) ((nowMs() - L.loadStart) % 1200) / 1200.f * 360.f - 90.f;
            arc(g, Rf(cx - R * .97f, cy - R * .97f, R * 1.94f, R * 1.94f), a0, 90, R * .05f, pal::AMBER, false);
        }
    }

    static float luminance(uint32 c) {
        return (.299f * (float) ((c >> 16) & 0xffu) + .587f * (float) ((c >> 8) & 0xffu) + .114f * (float) (c & 0xffu)) / 255.f;
    }

    void drawPad(G& g, Pad& p) const {
        const int d = p.deck, s = p.slot, mode = padMode();
        const auto& L = live[d];
        const Rf r = p.r;
        juce::String l;
        uint32 col;
        bool lit;
        switch (mode) {
            case (int) PadMode::cues: {
                const bool set = L.cueT[s] >= 0;
                col = set ? 0xff000000u | L.cueRgb[s] : pal::FAINT;
                l = "CUE " + juce::String(s + 1);
                lit = set;
                break;
            }
            case (int) PadMode::loops: l = LOOP_NAMES[s]; col = pal::GREEN; lit = false; break;
            case (int) PadMode::rolls: l = ROLL_NAMES[s]; col = pal::CYAN; lit = L.rolling && p.held(); break;
            default:
                l = TRANSPORT_NAMES[s];
                col = pal::AMBER;
                lit = (s == 0 && L.playing) || (s == 2 && L.synced) || (s == 3 && L.censor) || (s == 7 && L.looping);
                if (s == 7 && L.looping) l = "EXIT";
        }
        const float rad = std::min(6 * dp, r.getHeight() * .16f);
        const bool pressed = p.held();
        const bool cues = mode == (int) PadMode::cues;
        if (cues && lit) g.setColour(juce::Colour(pressed ? pal::mix(col, pal::WHITE, .4f) : pal::mix(pal::PANEL, col, .85f)));
        else g.setColour(juce::Colour(pressed ? pal::mix(pal::RAISED, col, .6f) : lit ? pal::mix(pal::PANEL, col, .35f) : pal::RAISED));
        g.fillRoundedRectangle(r, rad);
        g.setColour(juce::Colour(pressed ? pal::WHITE : (cues ? (lit ? pal::mix(col, pal::WHITE, .3f) : pal::EDGE) : pal::mix(pal::EDGE, col, .4f))));
        g.drawRoundedRectangle(r, rad, pressed ? 2.5f * dp : dp);
        const bool dark = cues && lit && luminance(col) > .55f;
        const float size = std::min(14 * ts, r.getHeight() * .34f);
        label(g, l, r.getCentreX(), r.getCentreY() + size * .36f, size, dark ? 0xff101010u : pal::WHITE, Align::centre, r.getWidth() - 6 * dp);
        if (pressed && cues && lit && !p.cleared) {
            const float k = std::min(1.f, (float) (nowMs() - p.downAt) / 750.f);
            if (k > .25f) { g.setColour(juce::Colour(pal::RED)); g.fillRect(ltrb(r.getX() + rad, r.getBottom() - 4 * dp, r.getX() + rad + (r.getWidth() - 2 * rad) * k, r.getBottom() - 2 * dp)); }
        }
    }

    void drawMeters(G& g) const {
        const int segs = 18;
        for (int i = 0; i < 4; ++i) {
            const Rf r = meterR[i];
            if (r.getHeight() < 10) continue;
            const float lvl = meterHold[i];
            const double db = lvl > 1e-6f ? 20 * std::log10((double) lvl) : -100;
            const float frac = (float) std::max(0.0, std::min(1.0, (db + 36) / 39));   // -36 .. +3 dB
            const int lit = juce::roundToInt(frac * segs);
            const float sh = r.getHeight() / segs;
            for (int k = 0; k < segs; ++k) {
                const float y1 = r.getBottom() - (float) k * sh, y0 = y1 - sh + dp;
                const uint32 c = k >= segs - 2 ? pal::RED : k >= segs - 5 ? pal::AMBER : pal::GREEN;
                g.setColour(juce::Colour(k < lit ? c : pal::alpha(c, .12f)));
                g.fillRect(ltrb(r.getX(), y0, r.getRight(), y1));
            }
        }
    }

    bool zoomVisible() const { return zoomShownAt != 0 && (pinchA >= 0 || nowMs() - zoomShownAt <= 1200); }
    Rf zoomRect() const {
        const float size = std::min(20 * ts, waves[0]->r.getHeight() * .5f);
        const float w = textW(bold(size), "00.0 s") + 16 * dp;
        const float cx = waves[0]->r.getCentreX(), cy = waves[0]->r.getBottom();
        return ltrb(cx - w / 2, cy - size * .9f, cx + w / 2, cy + size * .9f);
    }
    // the zoom while pinching (and a moment after)
    void drawZoom(G& g) const {
        if (!zoomVisible()) return;
        const float z = zoomSec();
        const juce::String s = (z < 10 ? juce::String(z, 1) : juce::String(juce::roundToInt(z))) + " s";
        const float size = std::min(20 * ts, waves[0]->r.getHeight() * .5f);
        const Rf t = zoomRect();
        g.setColour(juce::Colour(0xe6000000u));
        g.fillRoundedRectangle(t, 6 * dp);
        g.setColour(juce::Colour(pal::WHITE));
        g.drawRoundedRectangle(t, 6 * dp, 2 * dp);
        label(g, s, t.getCentreX(), waves[0]->r.getBottom() + size * .36f, size, pal::WHITE, Align::centre, 0);
    }

    void drawSheet(G& g) const {
        g.fillAll(juce::Colour(0xd8000000u));
        g.setColour(juce::Colour(pal::PANEL2));
        g.fillRoundedRectangle(sheetR, 10 * dp);
        g.setColour(juce::Colour(pal::EDGE_HI));
        g.drawRoundedRectangle(sheetR, 10 * dp, 2 * dp);
        const float th = std::min(sheetTitleR.getHeight() * .55f, 20 * ts);
        label(g, "SETUP", sheetTitleR.getX(), sheetTitleR.getCentreY() + th * .36f, th, pal::WHITE, Align::left, 0);
        const float hx = sheetTitleR.getX() + textW(bold(th), "SETUP") + 12 * dp;
        label(g, "pinch the waveforms to zoom", hx, sheetTitleR.getCentreY() + th * .3f, th * .6f, pal::DIM, Align::left, doneBtn->r.getX() - hx - 8 * dp);
        const float cap = std::min(11 * ts, sheetCol[0].getHeight() * .05f);
        const char* caps[3] = { "", "CROSSFADER ASSIGN", "DECK" };
        for (int d = 0; d < 2; ++d) {
            const Rf col = sheetCol[d];
            g.setColour(juce::Colour(pal::DECK[d]));
            g.fillRect(ltrb(col.getX(), col.getY() + 2 * dp, col.getX() + 3 * dp, col.getBottom()));
            for (int k = 0; k < 3; ++k) {
                const juce::String s = k == 0 ? "DECK " + juce::String(d == 0 ? "A" : "B") + U("  \xc2\xb7  TRIM  \xc2\xb7  ") + fxBtns[d][0]->label() : juce::String(caps[k]);
                label(g, s, col.getX() + 8 * dp, sheetLabelY[k], cap, k == 0 ? pal::DECK[d] : pal::DIM, Align::left, col.getWidth() - 8 * dp);
            }
        }
        label(g, "MIXER", sheetMid.getCentreX(), sheetMid.getY() + cap * 1.1f, cap, pal::DIM, Align::centre, sheetMid.getWidth());
        label(g, "CHANNEL FADERS", sheetMid.getCentreX(), chCurve->r.getY() - cap * .4f, cap, pal::DIM, Align::centre, sheetMid.getWidth());
        for (auto* k : sheetCtrls) k->draw(g);
    }

    void paint(G& g) {
        pxScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto clip = g.getClipBounds().toFloat();
        g.fillAll(juce::Colour(pal::BG));
        for (int d = 0; d < 2; ++d) {
            panel(g, deckMid[d], pal::PANEL, 6 * dp);
            panel(g, deckBot[d], pal::PANEL, 6 * dp);
            if (focus == d) {
                const Rf m = deckMid[d];
                g.setColour(juce::Colour(pal::WHITE));
                g.fillRect(ltrb(m.getX() + 10 * dp, m.getY() + 1 * dp, m.getX() + 60 * dp, m.getY() + 4 * dp));
            }
        }
        panel(g, mixRect, pal::PANEL2, 6 * dp);
        for (auto* k : ctrls)
            if (k->r.expanded(2 * dp).intersects(clip)) k->draw(g);
        drawMeters(g);
        drawPitchReadout(g);
        const juce::String ms = host.midiStatus ? host.midiStatus() : juce::String();
        label(g, ms, statusR.getCentreX(), statusR.getBottom() - 2 * dp, std::min(10 * ts, statusR.getHeight()), pal::DIM, Align::centre, statusR.getWidth());
        drawZoom(g);
        if (sheetOpen) drawSheet(g);
    }

    // ============================================================================================== frames
    // Every frame polls the engine; what changed is repainted: everything when a setting, a button state or a held
    // control changed, otherwise only the moving parts (waveforms, overviews, headers, platters, meters, held pads).
    juce::uint64 lastSig = 0;
    double lastPos[2] { -1, -1 }, lastAngle[2] { -1, -1 }, lastBpm[2] { -1, -1 };
    float lastMeter[4] { -1, -1, -1, -1 };
    bool lastZoomVis = false;

    static void mixIn(juce::uint64& h, juce::uint64 x) { h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
    static void mixF(juce::uint64& h, double x) { juce::uint64 u; std::memcpy(&u, &x, sizeof u); mixIn(h, u); }

    juce::uint64 signature() const {
        juce::uint64 h = 1469598103934665603ull;
        for (auto* p : proc.getParameters()) mixF(h, (double) p->getValue());
        mixIn(h, (juce::uint64) padMode());
        for (int d = 0; d < 2; ++d) {
            const auto& L = live[d];
            const bool bits[] = { L.has, L.playing, L.looping, L.synced, L.censor, L.rolling, L.loading, handOn(d), L.err.isNotEmpty() };
            juce::uint64 m = 0;
            for (const bool bit : bits) m = (m << 1) | (bit ? 1u : 0u);
            mixIn(h, m);
            mixIn(h, (juce::uint64) (juce::pointer_sized_int) L.trk.get());
            mixF(h, L.cue); mixF(h, L.loopA); mixF(h, L.loopB);
            for (int i = 0; i < 8; ++i) { mixF(h, L.cueT[i]); mixIn(h, L.cueRgb[i]); }
            mixIn(h, (juce::uint64) L.err.hashCode64());
            // CUE lights when stopped at the cue point
            mixIn(h, (juce::uint64) (hasTrack(d) && !playing(d) && std::abs(L.pos - L.cue) < .03));
        }
        mixIn(h, (juce::uint64) focus);
        mixIn(h, (juce::uint64) sheetOpen);
        for (auto* c : ctrls) mixIn(h, (juce::uint64) (c->pointer + 1));
        for (auto* c : sheetCtrls) mixIn(h, (juce::uint64) (c->pointer + 1));
        mixIn(h, (juce::uint64) zoomVisible());
        if (zoomVisible()) mixF(h, zoomSec());
        if (host.midiStatus) mixIn(h, (juce::uint64) host.midiStatus().hashCode64());
        if (host.selectedTitle) mixIn(h, (juce::uint64) host.selectedTitle().hashCode64());
        return h;
    }

    void frame() {
        poll();
        const auto sig = signature();
        if (sig != lastSig) {
            lastSig = sig;
            view.repaint();
            for (int d = 0; d < 2; ++d) { lastPos[d] = live[d].pos; lastAngle[d] = live[d].angle; lastBpm[d] = live[d].bpm; }
            for (int i = 0; i < 4; ++i) lastMeter[i] = meterHold[i];
            return;
        }
        if (sheetOpen) return;   // the sheet covers the moving parts
        const auto rep = [this](Rf r) { view.repaint(r.expanded(2 * dp).getSmallestIntegerContainer()); };
        for (int d = 0; d < 2; ++d) {
            const auto& L = live[d];
            if (!juce::exactlyEqual(L.pos, lastPos[d]) || !juce::exactlyEqual(L.bpm, lastBpm[d])) {
                lastPos[d] = L.pos; lastBpm[d] = L.bpm;
                rep(headers[d]->r); rep(overviews[d]->r); rep(waves[d]->r);
            } else if (hasTrack(d) && L.len - L.pos < 30) rep(headers[d]->r);   // the blinking time
            else if (L.loading) rep(headers[d]->r);
            if (!juce::exactlyEqual(L.angle, lastAngle[d]) || L.loading) { lastAngle[d] = L.angle; rep(platters[d]->r); }
            for (auto* p : pads[d]) if (p->held()) rep(p->r);
        }
        bool m = false;
        for (int i = 0; i < 4; ++i) if (std::abs(meterHold[i] - lastMeter[i]) > 1e-5f) { lastMeter[i] = meterHold[i]; m = true; }
        if (m) rep(meterR[0].getUnion(meterR[3]));
        if (zoomVisible()) { rep(waves[0]->r); rep(waves[1]->r); }
    }
};

// ==================================================================================================== the Component
PhoneDeckView::PhoneDeckView(WheelProcessor& p, PhoneHost& h) : d(std::make_unique<Impl>(p, h, *this)) {
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setPaintingIsUnclipped(false);
    startTimerHz(60);
}
PhoneDeckView::~PhoneDeckView() { stopTimer(); d->releaseAll(); }

void PhoneDeckView::paint(juce::Graphics& g) { d->paint(g); }
void PhoneDeckView::resized() { d->layout((float) getWidth(), (float) getHeight()); repaint(); }
void PhoneDeckView::timerCallback() { d->frame(); }

void PhoneDeckView::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() && e.source.isMouse()) return;
    d->pointerDown(e.source.getIndex(), e.position.x, e.position.y);
    d->frame();
}
void PhoneDeckView::mouseDrag(const juce::MouseEvent& e) { d->pointerMove(e.source.getIndex(), e.position.x, e.position.y); }
void PhoneDeckView::mouseUp(const juce::MouseEvent& e) { d->pointerUp(e.source.getIndex(), e.position.x, e.position.y); d->frame(); }

void PhoneDeckView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const float dy = (w.isReversed ? -1.f : 1.f) * (std::abs(w.deltaY) >= std::abs(w.deltaX) ? w.deltaY : -w.deltaX);
    if (juce::exactlyEqual(dy, 0.f)) return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { d->zoomBy(std::exp(-dy * 1.5f)); return; }   // up = zoom in
    if (d->sheetOpen) {
        for (auto* c : d->sheetCtrls) if (c->hit(e.position.x, e.position.y) && c->wheel(dy)) return;
        return;
    }
    for (int i = (int) d->ctrls.size() - 1; i >= 0; --i) {
        auto* c = d->ctrls[(size_t) i];
        if (c->hit(e.position.x, e.position.y)) { c->wheel(dy); return; }
    }
}
void PhoneDeckView::mouseMagnify(const juce::MouseEvent&, float scale) {
    if (scale > 0) d->zoomBy(1.f / scale);   // fingers apart = fewer seconds
}
bool PhoneDeckView::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey && d->sheetOpen) { d->closeSheet(); return true; }
    return false;
}

}  // namespace wheel
