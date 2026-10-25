// Value text for each parameter, matching what the web app shows on its knobs.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include "Spec.h"

namespace omega {

inline std::string fmtf(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
inline double q7(double v) { return std::round(std::fmax(0, std::fmin(1, v)) * 127) / 127; }

inline std::string formatValue(const ParamDef& p, double v) {
    switch (p.fmt) {
        case Fmt::pct: return fmtf("%.0f%%", std::round(v * 100));
        case Fmt::spct: return (v < 0 ? "-" : "") + fmtf("%.0f%%", std::round(std::fabs(v) * 100));
        case Fmt::ms: case Fmt::msv:
            if (v >= 1000) return fmtf("%.2f s", v / 1000);
            return v < 10 ? fmtf("%.1f ms", v) : fmtf("%.0f ms", std::round(v));
        case Fmt::hz: return v >= 1000 ? fmtf("%.1fk", v / 1000) : fmtf("%.0f Hz", std::round(v));
        case Fmt::hzv: return v < 1 ? fmtf("%.2f Hz", v) : fmtf("%.1f Hz", v);
        case Fmt::sec: return fmtf("%.1f s", v);
        case Fmt::db: return fmtf("%.0f dB", std::round(v));
        case Fmt::negdb: return fmtf("-%.0f dB", std::round(v));
        case Fmt::db1: return fmtf("%.1f dB", v);
        case Fmt::semis: return fmtf("%+.0f st", std::round(v));
        case Fmt::n1: return fmtf("%.1f", v);
        case Fmt::k10: return fmtf("%.1f", std::round(v * 127) / 12.7);
        case Fmt::time: { double t = .001 * std::pow(30000, std::pow(q7(v), .728)); return t < 1 ? fmtf("%.0f ms", std::round(t * 1000)) : fmtf("%.1f s", t); }
        case Fmt::st: { int st = (int) std::lround(q7(v) * 48) - 24; return fmtf("%+d st", st).replace(0, st == 0 ? 1 : 0, ""); }
        case Fmt::ct: return fmtf("+%.0f ct", std::round(v * 100));
        case Fmt::cents: return fmtf("+/-%.1f c", v);
        case Fmt::cutoff: { double f = 16.35 * std::pow(2, std::round(v * 127) / 12); return f >= 1000 ? fmtf("%.1fk", f / 1000) : fmtf("%.0f Hz", std::round(f)); }
        case Fmt::res10: return fmtf("%.1f", v * 10);
        case Fmt::pw: return fmtf("%.0f%%", std::round(1 + 98 * v));
        case Fmt::p5lfo: return fmtf("%.2f Hz", .04 * std::pow(500, v));
        case Fmt::wmmix: return v < .02 ? "LFO" : v > .98 ? "Noise" : fmtf("%.0f%% noise", std::round(v * 100));
        case Fmt::u7in: return fmtf("%+.0f dB", std::round(v * 50 - 20));
        case Fmt::u7out: return fmtf("%+.0f dB", std::round(v * 40 - 30));
        case Fmt::adaman: return fmtf("%.2f ms", .25 * std::pow(40, v));
        case Fmt::adarange: return v < .03 ? "off" : fmtf("%.1f:1", std::fmax(1, 40 * v));
        case Fmt::adaspeed: { double h = .02 * std::pow(400, v); return h < 1 ? fmtf("%.1f s", 1 / h) : fmtf("%.1f Hz", h); }
        case Fmt::toggle: return v >= .5 ? "On" : "Off";
        case Fmt::choice: { int i = (int) std::lround(v); return i >= 0 && i < (int) p.choices.size() ? p.choices[(size_t) i].label : ""; }
        case Fmt::plain: default: return fmtf("%.2f", v);
    }
}

}  // namespace omega
