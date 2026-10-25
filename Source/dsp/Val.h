// A small JSON value for the messages the F.A.R.P. page sends to its sound engine.
// Typed arrays travel as {"$f":"<base64 float32 LE>"} or {"$u":"<base64 bytes>"}.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace farp {

struct Val {
    enum Type { Null, Bool, Num, Str, Arr, Obj, F32, U8 };
    Type type = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<Val> a;
    std::map<std::string, Val> o;
    std::shared_ptr<std::vector<float>> f;
    std::shared_ptr<std::vector<uint8_t>> u;

    bool isNull() const { return type == Null; }
    bool isNum() const { return type == Num; }
    bool isObj() const { return type == Obj; }
    bool isArr() const { return type == Arr; }
    bool isStr() const { return type == Str; }

    static const Val& none() { static Val v; return v; }
    const Val& operator[](const char* k) const {
        if (type != Obj) return none();
        auto it = o.find(k);
        return it == o.end() ? none() : it->second;
    }
    const Val& operator[](size_t i) const { return (type == Arr && i < a.size()) ? a[i] : none(); }
    bool has(const char* k) const { return type == Obj && o.count(k) > 0; }
    size_t size() const { return type == Arr ? a.size() : type == F32 ? (f ? f->size() : 0) : type == U8 ? (u ? u->size() : 0) : 0; }

    // JS-like coercions
    double num(double def = 0) const {
        switch (type) {
            case Num: return n;
            case Bool: return b ? 1 : 0;
            case Null: return def;
            case Str: { if (s.empty()) return 0; char* e = nullptr; double d = std::strtod(s.c_str(), &e); return (e && *e == 0) ? d : std::nan(""); }
            default: return def;
        }
    }
    bool truthy() const {
        switch (type) {
            case Null: return false;
            case Bool: return b;
            case Num: return n != 0 && !std::isnan(n);
            case Str: return !s.empty();
            default: return true;
        }
    }
    const std::string& str() const { return s; }

    // ---------- parsing ----------
    static Val parse(const std::string& t) { size_t i = 0; Val v; parseValue(t, i, v); return v; }

private:
    static void ws(const std::string& t, size_t& i) { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
    static void parseString(const std::string& t, size_t& i, std::string& out) {
        i++; // opening quote
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\' && i < t.size()) {
                char e = t[i++];
                switch (e) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned cp = 0; for (int k = 0; k < 4 && i < t.size(); k++) { char h = t[i++]; cp = cp * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10); }
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                            unsigned lo = 0; i += 2; for (int k = 0; k < 4; k++) { char h = t[i++]; lo = lo * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10); }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                        break;
                    }
                    default: out += e;
                }
            } else out += c;
        }
        i++; // closing quote
    }
    static void parseValue(const std::string& t, size_t& i, Val& v) {
        ws(t, i);
        if (i >= t.size()) return;
        char c = t[i];
        if (c == '{') {
            v.type = Obj; i++; ws(t, i);
            if (i < t.size() && t[i] == '}') { i++; return; }
            while (i < t.size()) {
                ws(t, i); std::string k; parseString(t, i, k); ws(t, i); i++; // ':'
                Val child; parseValue(t, i, child);
                v.o[k] = std::move(child);
                ws(t, i);
                if (i < t.size() && t[i] == ',') { i++; continue; }
                i++; break;
            }
            // typed-array wrappers
            if (v.o.size() == 1) {
                auto it = v.o.begin();
                if (it->first == "$f" && it->second.type == Str) { Val r; r.type = F32; r.f = std::make_shared<std::vector<float>>(); auto bytes = b64(it->second.s); r.f->resize(bytes.size() / 4); if (!bytes.empty()) std::memcpy(r.f->data(), bytes.data(), r.f->size() * 4); v = std::move(r); }
                else if (it->first == "$u" && it->second.type == Str) { Val r; r.type = U8; r.u = std::make_shared<std::vector<uint8_t>>(b64(it->second.s)); v = std::move(r); }
            }
        } else if (c == '[') {
            v.type = Arr; i++; ws(t, i);
            if (i < t.size() && t[i] == ']') { i++; return; }
            while (i < t.size()) {
                Val child; parseValue(t, i, child); v.a.push_back(std::move(child)); ws(t, i);
                if (i < t.size() && t[i] == ',') { i++; continue; }
                i++; break;
            }
        } else if (c == '"') { v.type = Str; parseString(t, i, v.s); }
        else if (c == 't') { v.type = Bool; v.b = true; i += 4; }
        else if (c == 'f') { v.type = Bool; v.b = false; i += 5; }
        else if (c == 'n') { v.type = Null; i += 4; }
        else { v.type = Num; const char* st = t.c_str() + i; char* e = nullptr; v.n = std::strtod(st, &e); i += (size_t)(e - st); }
    }
public:
    static std::vector<uint8_t> b64(const std::string& s) {
        static int T[256]; static bool init = false;
        if (!init) { for (int k = 0; k < 256; k++) T[k] = -1; const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int k = 0; k < 64; k++) T[(unsigned char)A[k]] = k; init = true; }
        std::vector<uint8_t> out; out.reserve(s.size() * 3 / 4);
        int val = 0, bits = -8;
        for (unsigned char ch : s) { int d = T[ch]; if (d < 0) continue; val = (val << 6) + d; bits += 6; if (bits >= 0) { out.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; } }
        return out;
    }
    static std::string enc64(const uint8_t* p, size_t n) {
        const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string r; r.reserve((n + 2) / 3 * 4);
        for (size_t i = 0; i < n; i += 3) {
            unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
            r += A[(v >> 18) & 63]; r += A[(v >> 12) & 63]; r += i + 1 < n ? A[(v >> 6) & 63] : '='; r += i + 2 < n ? A[v & 63] : '=';
        }
        return r;
    }
};

// Builds the small JSON replies the engine posts back to the page (scope, meters, engine state).
struct JsonOut {
    std::string s;
    void raw(const char* t) { s += t; }
    void num(double v) {
        if (!std::isfinite(v)) { s += "0"; return; }
        char b[32]; std::snprintf(b, sizeof b, "%.5g", v); s += b;
    }
    void key(const char* k) { if (!s.empty() && s.back() != '{' && s.back() != '[') s += ','; s += '"'; s += k; s += "\":"; }
    void str(const std::string& v) {
        s += '"';
        for (char c : v) { if (c == '"' || c == '\\') { s += '\\'; s += c; } else if ((unsigned char)c < 32) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); s += b; } else s += c; }
        s += '"';
    }
};

} // namespace farp
