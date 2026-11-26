// Omega Five SysEx: a C++ port of five/rxsysex.js (assembler, .syx files, the Yamaha bulk-dump librarian, Omega Five's
// own F0 7D 4F 35 messages, universal identity reply, the dispatcher) and of five/rxcart.js's bank image reader/writer.
#pragma once
#include "FiveCart.h"

namespace five {

using Bytes = std::vector<uint8_t>;
enum OmegaCmd { O_FULL = 1, O_PATTERN = 2, O_SONG = 3, O_VOICES = 4, O_FX = 5, O_PARAM = 6, O_REQUEST = 7, O_IDENTITY = 8 };
static const char* const PARAM_TARGETS[7] = { "voice", "channel", "rev", "dly", "global", "key", "midi" };
static const uint8_t IDENTITY_ID[9] = { 0x7D, 0x4F, 0x35, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00 };

/* ---------------- framing ---------------- */
class SysexAssembler {
public:
    size_t maxBytes = 16 * 1024 * 1024; Bytes buf; bool inMsg = false; int aborted = 0;
    // feeds bytes; complete F0..F7 messages go to `done`, realtime bytes are skipped
    template <class F> void push(const uint8_t* u8, size_t n, F&& done) {
        for (size_t i = 0; i < n; i++) {
            const uint8_t b = u8[i];
            if (b >= 0xF8) continue;
            if (b == 0xF0) { if (inMsg) aborted++; buf.clear(); buf.push_back(b); inMsg = true; continue; }
            if (!inMsg) continue;
            if (b == 0xF7) { buf.push_back(b); inMsg = false; done(buf); buf.clear(); continue; }
            if (b & 0x80) { aborted++; inMsg = false; buf.clear(); continue; }
            if (buf.size() >= maxBytes) { aborted++; inMsg = false; buf.clear(); continue; }
            buf.push_back(b);
        }
    }
};
inline std::vector<Bytes> parseSyx(const uint8_t* u8, size_t n) { std::vector<Bytes> out; SysexAssembler a; a.push(u8, n, [&](const Bytes& m) { out.push_back(m); }); return out; }
inline Bytes writeSyx(const std::vector<Bytes>& ms) { Bytes o; for (const auto& m : ms) o.insert(o.end(), m.begin(), m.end()); return o; }
inline bool isSysex(const Bytes& m) { return m.size() >= 2 && m[0] == 0xF0 && m.back() == 0xF7; }

/* ---------------- Yamaha ---------------- */
inline int yamahaChecksum(const uint8_t* d, size_t from, size_t to) { int s = 0; for (size_t i = from; i < to; i++) s += d[i]; return (-s) & 0x7F; }
inline bool isYamaha(const Bytes& m) { return m.size() >= 3 && m[0] == 0xF0 && m[1] == 0x43; }
inline bool yamahaDeviceOk(const Bytes& m, int deviceNo) { return !deviceNo || (m[2] & 0x0F) == ((deviceNo - 1) & 0x0F); }
// 'ok' | 'bad' | 'unknown-layout'
inline std::string verifyYamahaChecksum(const Bytes& m) {
    if (!isYamaha(m) || !isSysex(m)) return "unknown-layout";
    if ((m[2] >> 4) == 0 && m.size() >= 8) {
        const size_t count = (size_t) (m[4] << 7 | m[5]);
        if (m.size() == count + 8) {
            const int cs = m[m.size() - 2];
            if (yamahaChecksum(m.data(), 6, 6 + count) == cs || yamahaChecksum(m.data(), 4, 6 + count) == cs) return "ok";
            return "bad";
        }
    }
    return "unknown-layout";
}
inline std::string verifyBulk(const std::vector<Bytes>& ms) {
    bool all = !ms.empty(); for (const auto& m : ms) { const auto r = verifyYamahaChecksum(m); if (r == "bad") return "bad"; if (r != "ok") all = false; }
    return all ? "ok" : "unknown-layout";
}
inline Bytes dumpRequest(int deviceNo = 1, int formatByte = 0) { return { 0xF0, 0x43, (uint8_t) (0x20 | (((deviceNo ? deviceNo : 1) - 1) & 0x0F)), (uint8_t) (formatByte & 0x7F), 0xF7 }; }

inline void makeRxBulk(State& st, const std::vector<Bytes>& list) {
    auto& r = st.rxBulk; r.present = !list.empty(); r.messages.clear(); r.bytes = 0;
    for (const auto& m : list) { r.messages.push_back(b64encode(m.data(), m.size())); r.bytes += (long long) m.size(); }
    r.status = verifyBulk(list); r.count = (int) list.size();
    r.received = juce::Time::getCurrentTime().toISO8601(true).toStdString();
}
class BulkReceiver {
public:
    double gapMs = 2000, last = -1e300; std::vector<Bytes> list; bool active = false;
    void begin() { list.clear(); active = false; last = -1e300; }
    std::string push(State& st, const Bytes& m, double now) {
        if (!active || now - last > gapMs) { list.clear(); active = true; }
        last = now; list.push_back(m); makeRxBulk(st, list);
        return verifyYamahaChecksum(m);
    }
};
inline void storeBulk(State& st, const std::vector<Bytes>& ms) { std::vector<Bytes> l; for (const auto& m : ms) if (isYamaha(m)) l.push_back(m); if (l.empty()) st.rxBulk = RxBulk(); else makeRxBulk(st, l); }
inline std::vector<Bytes> rxBulkMessages(const State& st) { std::vector<Bytes> o; if (st.rxBulk.present) for (const auto& s : st.rxBulk.messages) o.push_back(b64decode(s)); return o; }
inline std::vector<Bytes> transmitBulk(const State& st, int deviceNo = 0) {
    auto l = rxBulkMessages(st); if (deviceNo) for (auto& m : l) if (isYamaha(m)) m[2] = (uint8_t) ((m[2] & 0xF0) | ((deviceNo - 1) & 0x0F)); return l;
}

/* ---------------- Omega messages ---------------- */
inline bool isOmega(const Bytes& m) { return m.size() >= 4 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 0x4F && m[3] == 0x35; }
inline Bytes omegaMessage(int cmd, const Bytes& payload) {
    Bytes body { (uint8_t) (cmd & 0x7F) }; body.insert(body.end(), payload.begin(), payload.end());
    Bytes o(SYSEX_HEADER, SYSEX_HEADER + 4); o.insert(o.end(), body.begin(), body.end()); o.push_back((uint8_t) yamahaChecksum(body.data(), 0, body.size())); o.push_back(0xF7); return o;
}
inline Bytes jsonPayload(const juce::var& v) { return pack7(toBytes(juce::JSON::toString(v, true))); }
inline juce::var fromJsonPayload(const uint8_t* d, size_t n) { const auto b = unpack7(d, n); juce::var v; juce::JSON::parse(fromBytes(b), v); return v; }
inline Bytes omegaFull(const State& s) { return omegaMessage(O_FULL, jsonPayload(cartToVar(s))); }
inline Bytes omegaPattern(const State& s, int n) { Bytes p { (uint8_t) (n & 0x7F) }; const auto j = jsonPayload(patternToVar(s.patterns[(size_t) n])); p.insert(p.end(), j.begin(), j.end()); return omegaMessage(O_PATTERN, p); }
inline Bytes omegaSong(const State& s, int n) { Bytes p { (uint8_t) (n & 0x7F) }; const auto j = jsonPayload(songToVar(s.songs[(size_t) n])); p.insert(p.end(), j.begin(), j.end()); return omegaMessage(O_SONG, p); }
inline Bytes omegaVoices(const State& s) {
    auto o = obj(); juce::Array<juce::var> v, c; for (const auto& x : s.voice) v.add(voiceToVar(x)); for (const auto& x : s.channel) c.add(channelToVar(x));
    put(o, "voice", v); put(o, "channel", c); return omegaMessage(O_VOICES, jsonPayload(o));
}
inline Bytes omegaFx(const State& s) { return omegaMessage(O_FX, jsonPayload(stateToVar(s)["fx"])); }
inline Bytes omegaRequest(int what, int n = 0) { return omegaMessage(O_REQUEST, { (uint8_t) (what & 0x7F), (uint8_t) (n & 0x7F) }); }
inline Bytes omegaIdentity() { Bytes p { 0, 1, 0, 0 }; for (const char* c = "OMEGA FIVE"; *c; c++) p.push_back((uint8_t) *c); return omegaMessage(O_IDENTITY, p); }
inline Bytes omegaParam(int target, int index, const std::string& name, double value) {
    long long x = (long long) jsround(value * 1000); const long long M = 1LL << 35; const long long u = x < 0 ? x + M : x;
    Bytes p { (uint8_t) target, (uint8_t) (index & 0x7F) }; for (char c : name) p.push_back((uint8_t) c); p.push_back(0);
    for (int k = 4; k >= 0; k--) p.push_back((uint8_t) ((u >> (7 * k)) & 0x7F));
    return omegaMessage(O_PARAM, p);
}
inline Bytes identityReply(int deviceNo) { Bytes o { 0xF0, 0x7E, (uint8_t) (deviceNo ? (deviceNo - 1) & 0x7F : 0x7F), 0x06, 0x02 }; o.insert(o.end(), IDENTITY_ID, IDENTITY_ID + 9); o.push_back(0xF7); return o; }
inline bool isIdentityRequest(const Bytes& m) { return m.size() == 6 && m[0] == 0xF0 && m[1] == 0x7E && m[3] == 0x06 && m[4] == 0x01 && m[5] == 0xF7; }

struct OmegaParsed { int cmd = 0; bool ok = false; std::string error; int n = 0; juce::var data; int target = 0, index = 0; std::string name; double value = 0; int what = 0; std::string identity; };
inline OmegaParsed parseOmega(const Bytes& m) {
    OmegaParsed r; const size_t h = 4;
    if (!isOmega(m) || !isSysex(m) || m.size() < 6) { r.error = "not an Omega Five message"; return r; }
    if (m[h] == 0x00 && m[h + 1] == 0x7B) { r.cmd = O_FULL; r.ok = true; r.data = fromJsonPayload(m.data() + h, m.size() - 1 - h); return r; }
    r.cmd = m[h];
    if (yamahaChecksum(m.data(), h, m.size() - 1) != 0) { r.error = "checksum error!"; return r; }
    const uint8_t* d = m.data() + h + 1; const size_t n = m.size() - h - 3;
    switch (r.cmd) {
        case O_FULL: case O_VOICES: case O_FX: r.data = fromJsonPayload(d, n); r.ok = !r.data.isVoid(); if (!r.ok) r.error = "bad data"; return r;
        case O_PATTERN: case O_SONG: if (n < 1) break; r.n = d[0]; r.data = fromJsonPayload(d + 1, n - 1); r.ok = !r.data.isVoid(); if (!r.ok) r.error = "bad data"; return r;
        case O_PARAM: {
            size_t z = 2; while (z < n && d[z] != 0) z++;
            if (z >= n || n != z + 6) { r.error = "bad parameter message"; return r; }
            long long u = 0; for (int k = 0; k < 5; k++) u = u * 128 + d[z + 1 + (size_t) k];
            const long long x = u >= (1LL << 34) ? u - (1LL << 35) : u;
            r.target = d[0]; r.index = d[1]; r.name.assign((const char*) d + 2, z - 2); r.value = x / 1000.0; r.ok = r.target < 7; if (!r.ok) r.error = "bad parameter message"; return r;
        }
        case O_REQUEST: if (n < 1) break; r.what = d[0]; r.n = n > 1 ? d[1] : 0; r.ok = true; return r;
        case O_IDENTITY: if (n < 4) break; r.identity.assign((const char*) d + 4, n - 4); r.ok = true; return r;
        default: r.error = "unknown command " + std::to_string(r.cmd); return r;
    }
    r.error = "bad data"; return r;
}
inline std::vector<Bytes> omegaReply(const State& s, int what, int n) {
    switch (what) {
        case O_FULL: return { omegaFull(s) };
        case O_PATTERN: return n < NUM_PATTERNS ? std::vector<Bytes> { omegaPattern(s, n) } : std::vector<Bytes> {};
        case O_SONG: return n < NUM_SONGS ? std::vector<Bytes> { omegaSong(s, n) } : std::vector<Bytes> {};
        case O_VOICES: return { omegaVoices(s) };
        case O_FX: return { omegaFx(s) };
        case O_IDENTITY: return { omegaIdentity() };
    }
    return {};
}
inline bool applyParam(State& st, int target, int i, const std::string& name, double value) {
    if (target == 0) { if (i >= NV) return false; const int k = voiceParamIndex(name); if (name == "path") { st.voice[(size_t) i].rec = value != 0; return true; } if (k < 0) return false; st.voice[(size_t) i][k] = clampd(value, VOICE_RANGES[k][0], VOICE_RANGES[k][1]); return true; }
    if (target == 1) {
        if (i >= NCH) return false; auto& c = st.channel[(size_t) i];
        if (name == "fader") c.fader = clampd(value, 0, 1); else if (name == "pan") c.pan = clampd(value, -1, 1); else if (name == "sendRev") c.sendRev = clampd(value, 0, 1);
        else if (name == "sendDly") c.sendDly = clampd(value, 0, 1); else if (name == "mute") c.mute = value != 0; else return false; return true;
    }
    if (target == 2) { const int k = revParamIndex(name); if (k < 0) return false; st.rev[(size_t) k] = clampd(value, REV_RANGES[k][0], REV_RANGES[k][1]); return true; }
    if (target == 3) { const int k = dlyParamIndex(name); if (k < 0) return false; st.dly[(size_t) k] = clampd(value, DLY_RANGES[k][0], DLY_RANGES[k][1]); return true; }
    if (target == 4) {
        if (name == "tempo") st.tempo = clampd(value, 40, 250); else if (name == "volume") st.volume = clampd(value, 0, 1); else if (name == "swing") st.swing = clampInt(value, 50, 75, 50);
        else if (name == "quantize") st.quantize = clampInt(value, 0, NUM_QUANTIZE - 1, Q_1_16); else if (name == "setup") st.setup = clampInt(value, 0, NUM_SETUPS - 1, 0); else return false; return true;
    }
    if (target == 5) {
        if (i >= NV) return false; auto& k = st.setups[(size_t) st.setup].keys[(size_t) i]; const int x = (int) jsround(value);
        if (name == "voice") k.voice = x; else if (name == "pitch") k.pitch = x; else if (name == "level") k.level = x; else if (name == "attack") k.attack = x; else if (name == "decay") k.decay = x;
        else if (name == "acc1") k.acc1 = x; else if (name == "acc2") k.acc2 = x; else if (name == "out") k.out = x; else return false; return true;
    }
    if (target == 6) {
        auto& m = st.midi; const int x = (int) jsround(value); const bool b = value != 0;
        if (name == "rxCh") m.rxCh = x; else if (name == "txCh") m.txCh = x; else if (name == "channelMsg") m.channelMsg = b; else if (name == "clockIn") m.clockIn = b;
        else if (name == "clockOut") m.clockOut = b; else if (name == "notesOut") m.notesOut = b; else if (name == "deviceNo") m.deviceNo = x; else if (name == "tunable") m.tunable = b;
        else if (name == "tunableVoice") m.tunableVoice = x; else if (name == "egVel") m.egVel = x; else if (name == "gateMs") m.gateMs = x; else if (name == "activeSense") m.activeSense = b;
        else if (name == "songSelect") m.songSelect = b; else return false; return true;
    }
    return false;
}
// applies a parsed message; returns a description or throws std::runtime_error. samplesKept: a full dump keeps nothing of the old state.
inline std::string applyOmega(State& st, const OmegaParsed& r) {
    if (!r.ok) throw std::runtime_error(r.error);
    switch (r.cmd) {
        case O_FULL: st = cartFromVar(r.data); return "all data";
        case O_PATTERN: if (r.n >= NUM_PATTERNS) throw std::runtime_error("pattern number out of range"); st.patterns[(size_t) r.n] = patternFromVar(r.data, r.n); return "pattern " + pad2(r.n);
        case O_SONG: if (r.n >= NUM_SONGS) throw std::runtime_error("song number out of range"); st.songs[(size_t) r.n] = songFromVar(r.data, r.n); return "song " + pad2(r.n);
        case O_VOICES: {
            if (auto* a = r.data["voice"].getArray()) for (int v = 0; v < NV && v < a->size(); v++) if ((*a)[v].isObject()) st.voice[(size_t) v] = voiceFromVar((*a)[v], v);
            if (auto* a = r.data["channel"].getArray()) for (int c = 0; c < NCH && c < a->size(); c++) if ((*a)[c].isObject()) st.channel[(size_t) c] = channelFromVar((*a)[c], c);
            return "voice data";
        }
        case O_FX: { auto w = obj(); put(w, "fx", r.data); const State t = stateFromVar(w); st.rev = t.rev; st.dly = t.dly; return "fx data"; }
        case O_PARAM: if (!applyParam(st, r.target, r.index, r.name, r.value)) throw std::runtime_error("bad parameter " + r.name); return std::string("param ") + PARAM_TARGETS[r.target] + " " + r.name;
    }
    return "";
}

struct SysexResult { std::string kind, message, error; std::vector<Bytes> reply; bool changed = false; };
inline SysexResult handleSysex(State& st, const Bytes& m, BulkReceiver& receiver, double nowMs) {
    SysexResult res; const int deviceNo = st.midi.deviceNo;
    if (!isSysex(m)) { res.kind = "invalid"; res.message = "bad SysEx"; return res; }
    if (isIdentityRequest(m)) {
        if (!sysexDeviceOk(m.data(), m.size(), deviceNo)) { res.kind = "ignored"; return res; }
        res.kind = "identity"; res.reply.push_back(identityReply(deviceNo)); return res;
    }
    if (isOmega(m)) {
        res.kind = "omega"; const auto r = parseOmega(m);
        if (!r.ok) { res.error = res.message = r.error; return res; }
        if (r.cmd == O_REQUEST) { res.reply = omegaReply(st, r.what, r.n); res.message = "dump request"; return res; }
        if (r.cmd == O_IDENTITY) { res.message = r.identity; return res; }
        try { res.message = "received " + applyOmega(st, r); res.changed = true; } catch (const std::exception& e) { res.error = res.message = e.what(); }
        return res;
    }
    if (isYamaha(m)) {
        if (!yamahaDeviceOk(m, deviceNo)) { res.kind = "ignored"; return res; }
        if ((m[2] >> 4) == 2) { res.kind = "yamaha-request"; res.reply = transmitBulk(st); res.message = res.reply.empty() ? "no bulk data" : "transmit bulk"; return res; }
        res.kind = "yamaha-bulk"; const auto cs = receiver.push(st, m, nowMs); res.changed = true;
        res.message = cs == "bad" ? "checksum error!" : "bulk " + std::to_string(receiver.list.size()); return res;
    }
    res.kind = "other"; return res;
}
struct SyxLoad { int yamaha = 0, omega = 0, other = 0; std::string status; std::vector<std::string> errors; };
inline SyxLoad loadSyx(State& st, const uint8_t* bytes, size_t n) {
    SyxLoad r; std::vector<Bytes> yam;
    for (const auto& m : parseSyx(bytes, n)) {
        if (isYamaha(m)) yam.push_back(m);
        else if (isOmega(m)) { const auto p = parseOmega(m); try { if (p.cmd != O_REQUEST && p.cmd != O_IDENTITY) applyOmega(st, p); r.omega++; } catch (const std::exception& e) { r.errors.push_back(e.what()); } }
        else r.other++;
    }
    if (!yam.empty()) { storeBulk(st, yam); r.status = st.rxBulk.status; }
    r.yamaha = (int) yam.size(); return r;
}

/* =====================================================================================================================
   RX5 waveform cartridge bank images (rxcart.js): parse, and build (new layout; raw bytes of unchanged voices reused)
   ===================================================================================================================== */
constexpr int BANK_SIZE = 131072, BANK_HEADER = 1024, BANK_ENTRY = 32, BANK_MAX_VOICES = 30, BANK_BASE_PITCH = 360;
struct BankVoice { std::string name; int bits = 12; VoiceParams params; std::vector<float> pcm; int out = 0, bank = 0, slot = 0; int loopStart = 0, loopEnd = 0; };
struct BankParse { bool ok = false; std::string error; std::vector<BankVoice> voices; bool checksumOk = true; int id = 0; };
inline int bankChecksum(const uint8_t* b) { int s = 0; for (int i = 0; i < BANK_HEADER - 2; i++) s += b[i]; return s & 0xFFFF; }
inline int gateFromCode(int c) { return c >= 92 ? 0 : (int) jsround(clampd(100 + (c * 6400.0) / 91, 100, 6500)); }
inline int codeFromGate(double ms) { return !(ms > 0) || ms >= 6500 ? 92 : (int) clampd(jsround(((ms - 100) * 91) / 6400), 0, 91); }
inline int byteToSample(int off, int bits) { return bits == 8 ? off : std::max(0, ((off - 1) * 2) / 3); }
inline BankParse parseBank(const uint8_t* u8, size_t size) {
    BankParse r;
    if (!size || size % BANK_SIZE || size > 4 * (size_t) BANK_SIZE) { r.error = "not an RX5 bank image (size must be 128 KB, 256 KB or 512 KB)"; return r; }
    for (size_t k = 0; k < size / BANK_SIZE; k++) {
        const uint8_t* b = u8 + k * BANK_SIZE;
        if (b[0] | b[1] | b[2] | b[3]) { r.error = "not an RX5 bank image (bank header)"; return r; }
        if (k == 0) r.id = b[4];
        const int count = std::min<int>(b[5], BANK_MAX_VOICES);
        for (int i = 0; i < count; i++) {
            const uint8_t* e = b + 6 + i * BANK_ENTRY; BankVoice v;
            std::string nm((const char*) e + 26, 6); while (!nm.empty() && (nm.back() == ' ' || nm.back() == 0)) nm.pop_back(); v.name = nm;
            v.bits = e[2] == 1 ? 12 : 8; auto& p = v.params; p = voiceDefault(0);
            p[vPitch] = (e[0] * 120 + e[1] - BANK_BASE_PITCH) * 10.0; p[vLevel] = clampd(e[23], 0, 31); p[vAr] = clampd(e[14], 1, 99); p[vD1r] = clampd(e[15], 0, 99); p[vRr] = clampd(e[16], 1, 99);
            p[vD2r] = clampd(e[17], 0, 99); p[vD1l] = clampd(e[18], 0, 60); p[vGate] = gateFromCode(e[19]); p[vBendRate] = clampd(e[20], 0, 60); p[vBendRange] = (int8_t) e[21] * 100.0;
            p[vReverse] = 0; p[vLoop] = (e[3] & 0x40) ? 0 : 1; p[vBits] = v.bits;
            auto a17 = [&](int o) { return (e[o] & 1) << 16 | e[o + 1] << 8 | e[o + 2]; };
            const int start = (e[3] & 1) << 16 | e[4] << 8; const int end = std::min(a17(11), BANK_SIZE), st = std::min(start, end);
            const uint8_t* raw = b + st; const int n = end - st;
            if (v.bits == 8) { v.pcm.resize((size_t) n); for (int j = 0; j < n; j++) v.pcm[(size_t) j] = (int8_t) raw[j] / 128.f; }
            else {
                const int m = std::max(0, ((n - 1) * 2) / 3); v.pcm.resize((size_t) m);
                for (int j = 0, q = 1; j < m; j += 2, q += 3) {
                    const int b1 = raw[q], b2 = q + 1 < n ? raw[q + 1] : 0, b3 = q + 2 < n ? raw[q + 2] : 0;
                    v.pcm[(size_t) j] = (int16_t) (b2 << 8 | (b1 & 0x0F) << 4) / 32768.f;
                    if (j + 1 < m) v.pcm[(size_t) j + 1] = (int16_t) (b3 << 8 | (b1 >> 4) << 4) / 32768.f;
                }
            }
            v.loopStart = byteToSample(std::max(0, a17(5) - st), v.bits); v.loopEnd = byteToSample(std::max(0, a17(8) - st), v.bits);
            v.out = e[24]; v.bank = (int) k; v.slot = i;
            r.voices.push_back(std::move(v));
        }
        if (((b[BANK_HEADER - 2] << 8) | b[BANK_HEADER - 1]) != bankChecksum(b)) r.checksumOk = false;
    }
    r.ok = true; return r;
}
// one 128 KB bank from voices (PCM at 25 kHz, max 30); throws if they don't fit
inline Bytes buildBank(const std::vector<BankVoice>& voices, int bankId = -1) {
    if (voices.size() > (size_t) BANK_MAX_VOICES) throw std::runtime_error("too many voices for one bank (max 30)");
    Bytes bank((size_t) BANK_SIZE, 0); bank[5] = (uint8_t) voices.size(); int at = BANK_HEADER;
    for (size_t i = 0; i < voices.size(); i++) {
        const auto& v = voices[i]; const int bits = v.bits == 8 ? 8 : 12; Bytes data; const auto& pcm = v.pcm;
        auto q = [](float x) { return (int) jsround(clampd(x, -1, 1) * 32767); };
        if (bits == 8) { for (float x : pcm) data.push_back((uint8_t) ((int) clampd(jsround(clampd(x, -1, 1) * 128), -128, 127) & 255)); }
        else {
            data.assign(1 + (pcm.size() + 1) / 2 * 3, 0);
            for (size_t j = 0, p = 1; j < pcm.size(); j += 2, p += 3) {
                const int w1 = ((int) clampd(jsround(q(pcm[j]) / 16.0), -2048, 2047) * 16) & 0xFFFF;
                const int w2 = j + 1 < pcm.size() ? ((int) clampd(jsround(q(pcm[j + 1]) / 16.0), -2048, 2047) * 16) & 0xFFFF : 0;
                data[p] = (uint8_t) ((((w2 >> 4) & 0x0F) << 4) | ((w1 >> 4) & 0x0F)); data[p + 1] = (uint8_t) (w1 >> 8); data[p + 2] = (uint8_t) (w2 >> 8);
            }
        }
        if (at + (int) data.size() > BANK_SIZE) throw std::runtime_error("voices don't fit in a 128 KB bank");
        std::copy(data.begin(), data.end(), bank.begin() + at);
        uint8_t* e = &bank[6 + i * BANK_ENTRY]; const auto& p = v.params;
        const int u = (int) clampd(jsround(p[vPitch] / 10), -BANK_BASE_PITCH, 240) + BANK_BASE_PITCH; e[0] = (uint8_t) (u / 120); e[1] = (uint8_t) (u % 120);
        e[2] = bits == 12 ? 1 : 0;
        e[14] = (uint8_t) clampd(jsround(p[vAr]), 1, 99); e[15] = (uint8_t) clampd(jsround(p[vD1r]), 0, 99); e[16] = (uint8_t) clampd(jsround(p[vRr]), 1, 99); e[17] = (uint8_t) clampd(jsround(p[vD2r]), 0, 99);
        e[18] = (uint8_t) clampd(jsround(p[vD1l]), 0, 60); e[19] = (uint8_t) codeFromGate(p[vGate]); e[20] = (uint8_t) clampd(jsround(p[vBendRate]), 0, 60);
        e[21] = (uint8_t) ((int) clampd(jsround(p[vBendRange] / 100), -60, 60) & 255); e[22] = 99; e[23] = (uint8_t) clampd(jsround(p[vLevel]), 0, 31); e[24] = (uint8_t) clampd(v.out, 0, 11);
        std::string nm = (v.name.empty() ? std::string("VOICE") : v.name).substr(0, 6); nm.resize(6, ' '); for (int k = 0; k < 6; k++) e[26 + k] = (uint8_t) (nm[(size_t) k] & 0x7F);
        const int n = (int) pcm.size(), end = at + (int) data.size(); const bool loop = p[vLoop] >= .5;
        auto s2b = [&](int k) { return bits == 8 ? k : 1 + (k / 2) * 3 + (k & 1 ? 2 : 0); };
        const int ls = loop ? at + s2b((int) std::floor(n * .7)) : at, le = loop ? at + s2b(n) : at;
        auto put17 = [&](int o, int a) { e[o] = (uint8_t) ((a >> 16) & 1); e[o + 1] = (uint8_t) ((a >> 8) & 255); e[o + 2] = (uint8_t) (a & 255); };
        e[3] = (uint8_t) ((loop ? 0 : 0x40) | ((at >> 16) & 1)); e[4] = (uint8_t) ((at >> 8) & 255);
        put17(5, std::min(ls, end)); put17(8, std::min(le, end)); put17(11, end);
        at = (end + 255) & ~255;
    }
    int cs = bankChecksum(bank.data());
    if (bankId < 0) { bank[4] = (uint8_t) ((cs & 255) ^ (cs >> 8)); } else bank[4] = (uint8_t) bankId;
    cs = bankChecksum(bank.data()); bank[BANK_HEADER - 2] = (uint8_t) (cs >> 8); bank[BANK_HEADER - 1] = (uint8_t) (cs & 255);
    return bank;
}

}  // namespace five
