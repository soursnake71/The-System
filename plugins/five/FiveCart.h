// Omega Five cartridge files (*.o5cart) and the full-memory SysEx wrapper: the seq.js JSON layout
// {format:'omega-five-cart', version, state:{...}, samples:[null | {name, sr, length, pcm16 (base64 16-bit LE)}]},
// plain or gzip, so the plugin reads what the web app writes and vice versa. Uses juce_core (JSON, gzip).
#pragma once
#include "FiveSeq.h"
#include <juce_core/juce_core.h>

namespace five {

/* ---------------- bytes helpers ---------------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string b64encode(const uint8_t* u8, size_t n) {
    std::string s; s.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t a = u8[i], b = i + 1 < n ? u8[i + 1] : 0, c = i + 2 < n ? u8[i + 2] : 0, v = (a << 16) | (b << 8) | c;
        s += B64[v >> 18]; s += B64[(v >> 12) & 63]; s += i + 1 < n ? B64[(v >> 6) & 63] : '='; s += i + 2 < n ? B64[v & 63] : '=';
    }
    return s;
}
inline std::vector<uint8_t> b64decode(const std::string& in) {
    std::string s; for (char c : in) if (std::isalnum((unsigned char) c) || c == '+' || c == '/') s += c;
    auto val = [](char c) -> int { const char* p = std::strchr(B64, c); return p && c ? (int) (p - B64) : 0; };
    std::vector<uint8_t> out; out.reserve(s.size() * 3 / 4);
    for (size_t i = 0; i < s.size(); i += 4) {
        const uint32_t n = (uint32_t) (val(s[i]) << 18 | (i + 1 < s.size() ? val(s[i + 1]) : 0) << 12 | (i + 2 < s.size() ? val(s[i + 2]) : 0) << 6 | (i + 3 < s.size() ? val(s[i + 3]) : 0));
        out.push_back((uint8_t) (n >> 16));
        if (i + 2 < s.size()) out.push_back((uint8_t) (n >> 8));
        if (i + 3 < s.size()) out.push_back((uint8_t) n);
    }
    return out;
}
inline std::vector<uint8_t> pack7(const std::vector<uint8_t>& u8) {
    std::vector<uint8_t> out; out.reserve(u8.size() * 8 / 7 + 8);
    for (size_t i = 0; i < u8.size(); i += 7) {
        const size_t n = std::min<size_t>(7, u8.size() - i); uint8_t msb = 0;
        for (size_t k = 0; k < n; k++) msb |= (uint8_t) ((u8[i + k] >> 7) << k);
        out.push_back(msb);
        for (size_t k = 0; k < n; k++) out.push_back(u8[i + k] & 0x7f);
    }
    return out;
}
inline std::vector<uint8_t> unpack7(const uint8_t* d, size_t len) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < len; i += 8) {
        const uint8_t msb = d[i]; const size_t n = std::min<size_t>(7, len - i - 1);
        for (size_t k = 0; k < n; k++) out.push_back((uint8_t) (d[i + 1 + k] | (((msb >> k) & 1) << 7)));
    }
    return out;
}
inline std::vector<uint8_t> toBytes(const juce::String& s) { const auto* p = s.toRawUTF8(); return std::vector<uint8_t>(p, p + std::strlen(p)); }
inline juce::String fromBytes(const std::vector<uint8_t>& b) { return juce::String::fromUTF8((const char*) b.data(), (int) b.size()); }

/* ---------------- state <-> JSON ---------------- */
inline juce::var obj() { return juce::var(new juce::DynamicObject()); }
inline void put(juce::var& o, const char* k, const juce::var& v) { o.getDynamicObject()->setProperty(k, v); }
inline juce::var num(double x) { return x == std::floor(x) && std::fabs(x) < 1e15 ? juce::var((juce::int64) x) : juce::var(x); }
inline double getD(const juce::var& o, const char* k, double d) { const auto& v = o[k]; return v.isVoid() || v.isUndefined() || !(v.isDouble() || v.isInt() || v.isInt64() || v.isBool() || v.isString()) ? d : v.isString() ? v.toString().getDoubleValue() : (double) v; }
inline bool has(const juce::var& o, const char* k) { return o.isObject() && o.getDynamicObject()->hasProperty(k); }
inline juce::String getS(const juce::var& o, const char* k, const juce::String& d) { return has(o, k) ? o[k].toString() : d; }

inline juce::var patternToVar(const Pattern& p) {
    auto o = obj(); put(o, "name", juce::String(p.name)); put(o, "beats", p.beats); put(o, "unit", p.unit); put(o, "bars", p.bars); put(o, "swing", p.swing);
    juce::Array<juce::var> ev; for (const auto& e : p.events) { auto x = obj(); put(x, "t", e.t); put(x, "v", e.v); put(x, "vel", e.vel); put(x, "acc", e.acc); ev.add(x); }
    put(o, "events", ev); juce::Array<juce::var> vol; for (int c : p.volume) vol.add(c); put(o, "volume", vol);
    return o;
}
inline Pattern patternFromVar(const juce::var& p, int i) {
    Pattern q = newPattern(i);
    if (!p.isObject()) return q;
    q.name = getS(p, "name", q.name).toStdString(); q.beats = clampInt(getD(p, "beats", 4), 1, BEATS_MAX, 4);
    q.unit = cleanUnit(getD(p, "unit", 4));
    q.bars = clampInt(getD(p, "bars", 1), 1, BARS_MAX, 1); q.swing = snapSwing(getD(p, "swing", 50));
    if (auto* a = p["volume"].getArray()) for (int c = 0; c < NCH && c < a->size(); c++) q.volume[(size_t) c] = clampInt((double) (*a)[c], 0, 31, 31);
    const int len = patLen(q);
    if (auto* a = p["events"].getArray()) for (const auto& e : *a) {
        if (!e.isObject()) continue;
        const double t = getD(e, "t", -1), v = getD(e, "v", -1);
        if (!(t >= 0 && t < len && v >= 0 && v < NV)) continue;
        Event x; x.t = (int) t; x.v = (int) v; x.vel = clampInt(getD(e, "vel", 100), 1, 127, 100); x.acc = clampInt(getD(e, "acc", 0), 0, 2, 0); q.events.push_back(x);
    }
    sortEvents(q);
    return q;
}
inline juce::var stepToVar(const SongStep& s) {
    auto o = obj();
    switch (s.type) {
        case SongStep::Pat: put(o, "type", "pat"); put(o, "n", s.n); break;
        case SongStep::Rep: put(o, "type", "rep"); put(o, "from", s.from); put(o, "count", s.count); break;
        case SongStep::Tempo: put(o, "type", "tempo"); put(o, "bpm", num(s.bpm)); break;
        case SongStep::Vol: put(o, "type", "vol"); put(o, "ch", s.ch); put(o, "level", s.level); break;
        case SongStep::Mark: put(o, "type", "mark"); break;
    }
    return o;
}
inline bool stepFromVar(const juce::var& v, SongStep& s) {
    const auto t = getS(v, "type", "");
    if (t == "pat") s = SongStep::pat((int) getD(v, "n", 0));
    else if (t == "rep") s = SongStep::rep((int) getD(v, "from", 0), (int) getD(v, "count", 2));
    else if (t == "tempo") s = SongStep::tempo(getD(v, "bpm", 120));
    else if (t == "vol") s = SongStep::vol((int) getD(v, "ch", 0), (int) getD(v, "level", 31));
    else if (t == "mark") s = SongStep::mark();
    else return false;
    s = cleanStep(s); return true;
}
inline juce::var songToVar(const Song& s) {
    auto o = obj(); put(o, "name", juce::String(s.name)); put(o, "tempo", num(s.tempo));
    juce::Array<juce::var> st; for (const auto& x : s.steps) st.add(stepToVar(x)); put(o, "steps", st); return o;
}
inline Song songFromVar(const juce::var& g, int i) {
    Song d = newSong(i); if (!g.isObject()) return d;
    d.name = getS(g, "name", d.name).toStdString(); const double t = getD(g, "tempo", 120); d.tempo = clampd(t != 0 && std::isfinite(t) ? t : 120, 40, 250);
    if (auto* a = g["steps"].getArray()) for (const auto& v : *a) { SongStep s; if ((int) d.steps.size() < SONG_MAX && stepFromVar(v, s)) d.steps.push_back(s); }
    return d;
}
inline juce::var voiceToVar(const VoiceParams& p) {
    auto o = obj(); for (int k = 0; k < vCount; k++) put(o, VOICE_PARAM_NAMES[k], num(p[k]));
    put(o, "path", p.rec ? "rec" : "rx"); return o;
}
inline VoiceParams voiceFromVar(const juce::var& o, int v) {
    VoiceParams p = voiceDefault(v);
    for (int k = 0; k < vCount; k++) if (has(o, VOICE_PARAM_NAMES[k])) { const double x = getD(o, VOICE_PARAM_NAMES[k], p[k]); if (std::isfinite(x)) p[k] = clampd(x, VOICE_RANGES[k][0], VOICE_RANGES[k][1]); }
    if (has(o, "path")) p.rec = o["path"].toString() == "rec";
    return p;
}
inline juce::var channelToVar(const ChannelParams& c) {
    auto o = obj(); put(o, "fader", num(c.fader)); put(o, "pan", num(c.pan)); put(o, "sendRev", num(c.sendRev)); put(o, "sendDly", num(c.sendDly)); put(o, "mute", c.mute); return o;
}
inline ChannelParams channelFromVar(const juce::var& o, int ch) {
    ChannelParams c = channelDefault(ch); if (!o.isObject()) return c;
    c.fader = clampd(getD(o, "fader", c.fader), 0, 1); c.pan = clampd(getD(o, "pan", c.pan), -1, 1); c.sendRev = clampd(getD(o, "sendRev", c.sendRev), 0, 1);
    c.sendDly = clampd(getD(o, "sendDly", c.sendDly), 0, 1); if (has(o, "mute")) { const auto m = o["mute"]; c.mute = m.isString() ? m.toString() != "0" && m.toString().isNotEmpty() : (bool) m; }
    return c;
}

inline juce::var stateToVar(const State& s) {
    auto o = obj();
    put(o, "version", CART_VERSION);
    juce::Array<juce::var> pats, songs, chains, nm, setups, voice, channel;
    for (const auto& p : s.patterns) pats.add(patternToVar(p));
    for (const auto& g : s.songs) songs.add(songToVar(g));
    for (const auto& c : s.chains) { auto x = obj(); put(x, "name", juce::String(c.name)); juce::Array<juce::var> l; for (int n : c.songs) l.add(n); put(x, "songs", l); chains.add(x); }
    for (int n : s.noteMap) nm.add(n);
    put(o, "patterns", pats); put(o, "songs", songs); put(o, "chains", chains); put(o, "noteMap", nm);
    {
        const auto& m = s.midi; auto x = obj();
        put(x, "rxCh", m.rxCh); put(x, "txCh", m.txCh); juce::Array<juce::var> vt; for (int c : m.voiceTxCh) vt.add(c); put(x, "voiceTxCh", vt);
        put(x, "channelMsg", m.channelMsg); put(x, "clockIn", m.clockIn); put(x, "clockOut", m.clockOut); put(x, "notesOut", m.notesOut);
        put(x, "deviceNo", m.deviceNo); put(x, "tunable", m.tunable); put(x, "tunableVoice", m.tunableVoice); put(x, "egVel", m.egVel); put(x, "gateMs", m.gateMs);
        put(x, "activeSense", m.activeSense); put(x, "songSelect", m.songSelect);
        put(o, "midi", x);
    }
    for (const auto& su : s.setups) {
        auto x = obj(); put(x, "name", juce::String(su.name)); juce::Array<juce::var> keys;
        for (const auto& k : su.keys) { auto y = obj(); put(y, "voice", k.voice); put(y, "pitch", k.pitch); put(y, "level", k.level); put(y, "attack", k.attack); put(y, "decay", k.decay); put(y, "acc1", k.acc1); put(y, "acc2", k.acc2); put(y, "out", k.out); keys.add(y); }
        put(x, "keys", keys); setups.add(x);
    }
    put(o, "setups", setups); put(o, "setup", s.setup);
    if (s.rxBulk.present) {
        auto x = obj(); put(x, "received", juce::String(s.rxBulk.received)); juce::Array<juce::var> ms; for (const auto& m : s.rxBulk.messages) ms.add(juce::String(m));
        put(x, "messages", ms); put(x, "status", juce::String(s.rxBulk.status)); put(x, "bytes", (juce::int64) s.rxBulk.bytes); put(x, "count", s.rxBulk.count); put(o, "rxBulk", x);
    } else put(o, "rxBulk", juce::var());
    put(o, "sync", juce::String(s.sync)); put(o, "tempo", num(s.tempo)); put(o, "quantize", s.quantize); put(o, "swing", s.swing);
    { auto c = obj(); put(c, "on", s.clickOn); put(c, "level", num(s.clickLevel)); put(o, "click", c); }
    put(o, "volume", num(s.volume));
    for (const auto& v : s.voice) voice.add(voiceToVar(v));
    for (const auto& c : s.channel) channel.add(channelToVar(c));
    put(o, "voice", voice); put(o, "channel", channel);
    { juce::Array<juce::var> w; for (int id : s.wave) w.add(id >= 0 ? juce::var(id) : juce::var()); put(o, "wave", w); }
    { auto fx = obj(), r = obj(), d = obj(); for (int k = 0; k < rCount; k++) put(r, REV_PARAM_NAMES[k], num(s.rev[(size_t) k])); for (int k = 0; k < dCount; k++) put(d, DLY_PARAM_NAMES[k], num(s.dly[(size_t) k])); put(fx, "rev", r); put(fx, "dly", d); put(o, "fx", fx); }
    return o;
}

inline State stateFromVar(const juce::var& src) {
    State s;
    if (!src.isObject()) return s;
    if (auto* a = src["patterns"].getArray()) for (int i = 0; i < NUM_PATTERNS && i < a->size(); i++) if ((*a)[i].isObject()) s.patterns[(size_t) i] = patternFromVar((*a)[i], i);
    if (auto* a = src["songs"].getArray()) for (int i = 0; i < NUM_SONGS && i < a->size(); i++) if ((*a)[i].isObject()) s.songs[(size_t) i] = songFromVar((*a)[i], i);
    if (auto* a = src["chains"].getArray()) for (int i = 0; i < NUM_CHAINS && i < a->size(); i++) {
        const auto& c = (*a)[i]; if (!c.isObject()) continue;
        s.chains[(size_t) i].name = getS(c, "name", s.chains[(size_t) i].name).toStdString();
        std::vector<int> l; if (auto* q = c["songs"].getArray()) for (const auto& x : *q) l.push_back((int) (double) x); chainSet(s, i, l);
    }
    if (auto* a = src["noteMap"].getArray()) for (int v = 0; v < NV && v < a->size(); v++) s.noteMap[(size_t) v] = clampInt((double) (*a)[v], 0, 127, s.noteMap[(size_t) v]);
    if (src["midi"].isObject()) {
        const auto& m = src["midi"]; auto& d = s.midi;
        auto b = [&](const char* k, bool def) { return has(m, k) ? (bool) m[k] : def; };
        d.rxCh = clampInt(getD(m, "rxCh", 10), 1, 16, 10); d.txCh = clampInt(getD(m, "txCh", 10), 1, 16, 10);
        if (auto* a = m["voiceTxCh"].getArray()) for (int v = 0; v < NV && v < a->size(); v++) d.voiceTxCh[(size_t) v] = clampInt((double) (*a)[v], 0, 16, 0);
        d.channelMsg = b("channelMsg", true); d.clockIn = b("clockIn", false); d.clockOut = b("clockOut", false); d.notesOut = b("notesOut", false);
        d.deviceNo = clampInt(getD(m, "deviceNo", 1), 0, 16, 1); d.tunable = b("tunable", false); d.tunableVoice = clampInt(getD(m, "tunableVoice", 0), 0, NV - 1, 0);
        d.egVel = (int) getD(m, "egVel", 0); d.gateMs = (int) getD(m, "gateMs", 10); d.activeSense = b("activeSense", true); d.songSelect = b("songSelect", true);
    }
    if (auto* a = src["setups"].getArray()) for (int i = 0; i < NUM_SETUPS && i < a->size(); i++) {
        const auto& su = (*a)[i]; if (!su.isObject()) continue; Setup d = newSetup(i);
        if (has(su, "name")) d.name = su["name"].toString().substring(0, 16).toStdString();
        if (auto* ks = su["keys"].getArray()) for (int j = 0; j < NV && j < ks->size(); j++) {
            const auto& o = (*ks)[j]; if (!o.isObject()) continue; auto& k = d.keys[(size_t) j];
            k.voice = clampInt(getD(o, "voice", k.voice), 0, NV - 1, k.voice); k.pitch = clampInt(getD(o, "pitch", 0), -3600, 2400, 0);
            k.level = clampInt(getD(o, "level", 0), -31, 31, 0); k.attack = clampInt(getD(o, "attack", 0), -63, 63, 0); k.decay = clampInt(getD(o, "decay", 0), -63, 63, 0);
            k.acc1 = clampInt(getD(o, "acc1", 2), -31, 31, 2); k.acc2 = clampInt(getD(o, "acc2", 4), -31, 31, 4); k.out = clampInt(getD(o, "out", k.out), 0, NCH - 1, k.out);
        }
        s.setups[(size_t) i] = d;
    }
    if (has(src, "setup")) s.setup = clampInt(getD(src, "setup", 0), 0, NUM_SETUPS - 1, 0);
    if (src["rxBulk"].isObject() && src["rxBulk"]["messages"].isArray()) {
        const auto& r = src["rxBulk"]; s.rxBulk.present = true; s.rxBulk.received = getS(r, "received", "").toStdString(); s.rxBulk.status = getS(r, "status", "").toStdString();
        for (const auto& m : *r["messages"].getArray()) s.rxBulk.messages.push_back(m.toString().toStdString());
        s.rxBulk.bytes = (long long) getD(r, "bytes", 0); s.rxBulk.count = (int) getD(r, "count", (double) s.rxBulk.messages.size());
    }
    { const auto sy = getS(src, "sync", "int"); if (sy == "int" || sy == "midi" || sy == "tape" || sy == "ext") s.sync = sy.toStdString(); }
    if (has(src, "tempo") && getD(src, "tempo", 0) != 0) s.tempo = clampd(getD(src, "tempo", 120), 40, 250);
    if (has(src, "quantize")) s.quantize = clampInt(getD(src, "quantize", Q_1_16), 0, NUM_QUANTIZE - 1, Q_1_16);
    if (has(src, "swing")) s.swing = snapSwing(getD(src, "swing", 50));
    if (src["click"].isObject()) { s.clickOn = has(src["click"], "on") && (bool) src["click"]["on"]; s.clickLevel = clampd(getD(src["click"], "level", .5), 0, 1); }
    if (has(src, "volume")) s.volume = clampd(getD(src, "volume", 0), 0, 1);
    if (auto* a = src["voice"].getArray()) for (int v = 0; v < NV && v < a->size(); v++) if ((*a)[v].isObject()) s.voice[(size_t) v] = voiceFromVar((*a)[v], v);
    if (auto* a = src["channel"].getArray()) for (int c = 0; c < NCH && c < a->size(); c++) if ((*a)[c].isObject()) s.channel[(size_t) c] = channelFromVar((*a)[c], c);
    if (auto* a = src["wave"].getArray()) for (int v = 0; v < NV && v < a->size(); v++) { const auto& x = (*a)[v]; s.wave[(size_t) v] = x.isVoid() || x.isUndefined() ? -1 : (x.isInt() || x.isInt64() || x.isDouble()) ? clampInt((double) x, -1, 255, -1) : -1; }
    if (src["fx"].isObject()) {
        const auto& r = src["fx"]["rev"]; const auto& d = src["fx"]["dly"];
        if (r.isObject()) {
            if (has(r, "program")) { const auto pv = r["program"]; int i = 0; if (pv.isString()) { for (int k = 0; k < NUM_REV_PROGRAMS; k++) if (pv.toString().toUpperCase() == REV_PROGRAMS[k].name) i = k; } else i = (int) clampd(jsround((double) pv), 0, 10);
                s.rev[rProgram] = i; for (int k = rTime; k <= rGateTime; k++) s.rev[(size_t) k] = programValue(REV_PROGRAMS[i], k); }
            for (int k = 1; k < rCount; k++) if (has(r, REV_PARAM_NAMES[k])) s.rev[(size_t) k] = clampd(getD(r, REV_PARAM_NAMES[k], s.rev[(size_t) k]), REV_RANGES[k][0], REV_RANGES[k][1]);
        }
        if (d.isObject()) for (int k = 0; k < dCount; k++) if (has(d, DLY_PARAM_NAMES[k])) {
            double x; if (k == dSync && d["sync"].isString()) { x = 0; for (int j = 0; j < 6; j++) if (d["sync"].toString() == DLY_SYNC[j]) x = j; } else x = getD(d, DLY_PARAM_NAMES[k], s.dly[(size_t) k]);
            s.dly[(size_t) k] = clampd(x, DLY_RANGES[k][0], DLY_RANGES[k][1]);
        }
    }
    return s;
}

inline juce::String pcm16(const std::vector<float>& d) {
    std::vector<uint8_t> b(d.size() * 2);
    for (size_t i = 0; i < d.size(); i++) { const int16_t v = (int16_t) jsround(clampd(d[i], -1, 1) * 32767); b[2 * i] = (uint8_t) (v & 255); b[2 * i + 1] = (uint8_t) ((v >> 8) & 255); }
    return juce::String(b64encode(b.data(), b.size()));
}
inline std::vector<float> fromPcm16(const juce::var& x) {
    const auto b = b64decode(x.toString().toStdString()); std::vector<float> d(b.size() / 2);
    for (size_t i = 0; i < d.size(); i++) d[i] = (float) ((int16_t) (b[2 * i] | b[2 * i + 1] << 8) / 32767.0);
    return d;
}
inline juce::var samplesToVar(const State& s) {
    juce::Array<juce::var> a;
    for (const auto& x : s.samples) {
        if (x.data.empty()) { a.add(juce::var()); continue; }
        auto o = obj(); put(o, "name", juce::String(x.name)); put(o, "sr", num(x.sr)); put(o, "length", (int) x.data.size()); put(o, "pcm16", pcm16(x.data));
        if (x.lib >= 0) put(o, "lib", x.lib);
        a.add(o);
    }
    return a;
}
inline void samplesFromVar(const juce::var& list, State& s) {
    if (auto* a = list.getArray()) for (int v = 0; v < NV && v < a->size(); v++) {
        const auto& x = (*a)[v]; if (!x.isObject() || !has(x, "pcm16")) continue;
        auto& d = s.samples[(size_t) v];
        d.name = getS(x, "name", "").toStdString(); d.sr = getD(x, "sr", 44100); if (!(d.sr > 0)) d.sr = 44100;
        d.data = fromPcm16(x["pcm16"]); d.lib = has(x, "lib") ? (int) getD(x, "lib", -1) : -1;
    }
}
// the cartridge library (a loaded kit): [{id, name, kind, sr, length, pcm16, file, source}]
inline juce::var libraryToVar(const State& s) {
    juce::Array<juce::var> a;
    for (const auto& w : s.library) {
        auto o = obj(); put(o, "id", w.id); put(o, "name", juce::String(w.name)); put(o, "kind", juce::String(w.kind)); put(o, "sr", num(w.sr));
        put(o, "length", (int) w.data.size()); put(o, "pcm16", pcm16(w.data)); put(o, "file", juce::String(w.file)); put(o, "source", juce::String(w.source)); a.add(o);
    }
    return a;
}
inline void libraryFromVar(const juce::var& list, State& s) {
    s.library.clear();
    if (auto* a = list.getArray()) for (const auto& x : *a) {
        if (!x.isObject() || !has(x, "pcm16")) continue;
        LibWave w; w.id = (int) getD(x, "id", 0); w.name = getS(x, "name", "").toStdString(); w.kind = getS(x, "kind", "user").toStdString();
        w.sr = getD(x, "sr", 25000); if (!(w.sr > 0)) w.sr = 25000; w.file = getS(x, "file", "").toStdString(); w.source = getS(x, "source", "").toStdString();
        w.data = fromPcm16(x["pcm16"]); s.library.push_back(std::move(w));
    }
}
inline juce::var cartToVar(const State& s) {
    auto o = obj(); put(o, "format", "omega-five-cart"); put(o, "version", CART_VERSION); put(o, "state", stateToVar(s)); put(o, "samples", samplesToVar(s));
    put(o, "library", libraryToVar(s)); return o;
}
// throws std::runtime_error on a bad cart
inline State cartFromVar(juce::var o) {
    if (!o.isObject()) throw std::runtime_error("not an Omega Five cartridge");
    if (o["format"].toString() != "omega-five-cart") { auto w = obj(); put(w, "format", "omega-five-cart"); put(w, "version", 0); put(w, "state", o); put(w, "samples", o["samples"]); o = w; }
    if (getD(o, "version", 0) > CART_VERSION) throw std::runtime_error("cartridge version is newer than this plugin");
    State s = stateFromVar(o["state"]); samplesFromVar(o["samples"], s); libraryFromVar(o["library"], s);
    return s;
}
inline juce::String cartJSON(const State& s) { return juce::JSON::toString(cartToVar(s), true); }
inline juce::MemoryBlock saveCart(const State& s, bool gzip = true) {
    const auto json = cartJSON(s); juce::MemoryBlock mb;
    if (!gzip) { mb.append(json.toRawUTF8(), json.getNumBytesAsUTF8()); return mb; }
    { juce::MemoryOutputStream mo(mb, false); juce::GZIPCompressorOutputStream gz(mo, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP); gz.write(json.toRawUTF8(), json.getNumBytesAsUTF8()); gz.flush(); }
    return mb;
}
inline State loadCart(const void* data, size_t size) {
    const auto* u8 = (const uint8_t*) data; juce::String text;
    if (size >= 2 && u8[0] == 0x1f && u8[1] == 0x8b) {
        juce::MemoryInputStream mi(data, size, false); juce::GZIPDecompressorInputStream gz(&mi, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryBlock out; juce::MemoryOutputStream mo(out, false); mo.writeFromInputStream(gz, -1); mo.flush();
        text = juce::String::fromUTF8((const char*) out.getData(), (int) out.getSize());
    } else text = juce::String::fromUTF8((const char*) data, (int) size);
    juce::var v; const auto r = juce::JSON::parse(text, v);
    if (r.failed()) throw std::runtime_error(("bad cartridge JSON: " + r.getErrorMessage()).toStdString());
    return cartFromVar(v);
}

/* ---------------- legacy full-memory dump F0 7D 4F 35 <pack7 JSON> F7 ---------------- */
static const uint8_t SYSEX_HEADER[4] = { 0xF0, 0x7D, 0x4F, 0x35 };
inline std::vector<uint8_t> sysexDump(const State& s) {
    const auto p = pack7(toBytes(juce::JSON::toString(cartToVar(s), true)));
    std::vector<uint8_t> out(SYSEX_HEADER, SYSEX_HEADER + 4); out.insert(out.end(), p.begin(), p.end()); out.push_back(0xF7); return out;
}

}  // namespace five
