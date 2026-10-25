// Roland SVC-350-style vocoder (ported from VOC_CODE in index.html), plus the speech player that feeds it.
// Engines: SVC350Fx (an effect: your track is the voice, the carrier is a built-in Prophet-5 played by MIDI or the
// sidechain), Vox (an instrument: Prophet-5 carrier + talking machine, like the Omega Vox app) and Speech.
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "../common/Spec.h"
#include "Common.h"
#include "Prophet5.h"
#include "TTS.h"

namespace omega::dsp {

struct SVCParams { bool on = true; double bands[10] {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}; double cons = .8, bal = 0, harm = .5, growl = 0, sib = .7, vol = 1, clev = 0; bool ens = false, hold = false; };

class SVCCore {
public:
    SVCParams P;
    void prepare(double sr) {
        fs = sr;
        sLpA = 1 - std::exp(-2 * PI * 2500 / fs); lLpA = 1 - std::exp(-2 * PI * 800 / fs);
        gA = 1 - std::exp(-2 * PI * 600 / fs); gRel = std::exp(-1 / (.05 * fs));
        static const double CA[10] = {.068, .047, .033, .022, .015, .01, .0068, .0047, .0033, .0022}, CS[10] = {.056, .039, .027, .018, .012, .0082, .0056, .0039, .0027, .0018};
        for (int k = 0; k < 10; ++k) {
            const double ca = CA[k] * 1e-6, cs = CS[k] * 1e-6; double fa1, fa2, fs1, fs2;
            A1[k] = mfb(33e3, 1.2e3, 220e3, ca, fa1); A2[k] = mfb(27e3, 1e3, 180e3, ca, fa2);
            S1[k] = mfb(180e3, 1.2e3, 220e3, cs, fs1); S2[k] = mfb(22e3, 1e3, 180e3, cs, fs2);
            const double fa = std::sqrt(fa1 * fa2), fsy = std::sqrt(fs1 * fs2);
            nA[k] = 1 / (mag(A1[k], fa) * mag(A2[k], fa)); nS[k] = 1 / (mag(S1[k], fsy) * mag(S2[k], fsy));
            SK[k] = lp2(1 / (2 * PI * 1e5 * std::sqrt(.033e-6 * .022e-6)), std::sqrt(.033e-6 * .022e-6) / (2 * .022e-6));
            pk[k] = 0;
        }
        relN = std::exp(-1 / (470e3 * .022e-6 * fs)); relH = std::exp(-1 / (8 * fs));
        auto rc = [&](double R) { const double RC = R * 2.2e-9; return RC / (RC + 1 / fs); };
        hpA[0] = rc(4.7e3); hpA[1] = rc(12e3); hpA[2] = rc(10e3); hpA[3] = (1e4 * 1e-8) / (1e4 * 1e-8 + 1 / fs);
        hk1 = hp2(1 / (2 * PI * 2.2e-9 * std::sqrt(12e3 * 22e3)), .5 * std::sqrt(22e3 / 12e3));
        hk2 = hp2(1 / (2 * PI * 2.2e-9 * std::sqrt(6.8e3 * 47e3)), .5 * std::sqrt(47e3 / 6.8e3));
        consLp = 1 - std::exp(-1 / (33e3 * 2.2e-9 * fs));
        cAtk = 1 - std::exp(-1 / (.001 * fs)); cRel = std::exp(-1 / (.2 * fs));
        detA = 1 - std::exp(-1 / (150e3 * .1e-6 * fs)); gateA = 1 - std::exp(-1 / (.004 * fs));
        hA = 1 - std::exp(-2 * PI * 1500 / fs);
        buf.assign(4096, 0.f);
        eA = 1 - std::exp(-2 * PI * 7000 / fs);   // (the app's envelope and ensemble filters share this coefficient)
    }
    // c: carrier, m: voice (modulator); writes stereo
    void run(const float* car, const float* mod, float* L, float* R, int N) {
        const double rel = P.hold ? relH : relN, ref = .3, cg = P.clev != 0 ? std::pow(10, P.clev / 20) : 1;
        for (int i = 0; i < N; ++i) {
            double c = (car ? car[i] : 0) * cg;
            if (P.growl > 0) {
                gLp += (c - gLp) * gA;
                if (gLp > 0) { if (!gPos) { gPos = 1; gFF = -gFF; } } else gPos = 0;
                const double ac = std::fabs(c); gPk = ac > gPk ? ac : gPk * gRel;
                gSub += (gFF * gPk - gSub) * .2;
                const double G = P.growl, drv = 1 + G * 9, pkk = std::max(gPk, 1e-4);
                c = std::tanh((c + gSub * G) / pkk * drv) / std::tanh(drv) * pkk / (1 + G * 1.4);
            }
            const double m = mod ? mod[i] : 0;
            if (!P.on) { L[i] = R[i] = (float) c; continue; }
            const double am = std::fabs(m);
            if (!P.hold) { if (am > cPk) cPk += (am - cPk) * cAtk; else cPk *= cRel; gC = std::min(10.0, ref / std::max(cPk, 1e-4)); }
            const double mc = m * gC;
            det += (std::fabs(c) - det) * detA;
            gate += ((det > .002 ? 1 : 0) - gate) * gateA;
            hLp += (c - hLp) * hA; double cc = hLp + (c - hLp) * P.harm * 2;
            if (P.sib > 0) {
                sLp += (mc - sLp) * sLpA; lLp += (mc - lLp) * lLpA;
                const double hf = mc - sLp; hE += (std::fabs(hf) - hE) * eA; lE += (std::fabs(lLp) - lE) * eA;
                const double u = std::min(1.0, std::max(0.0, (hE / (lE + 1e-4) - .55) * 1.6)) * P.sib;
                uv += (u - uv) * .02;
                if (uv > .001) cc = cc * (1 - uv) + (rnd() * 2 - 1) * std::max(det * 2.2, .02) * uv;
            }
            double sum = 0;
            for (int k = 0; k < 10; ++k) {
                const double a = A2[k].run(A1[k].run(mc)) * nA[k];
                const double r = a > 0 ? a : 0;
                if (r > pk[k]) pk[k] = r; else pk[k] *= rel;
                const double e = SK[k].run(pk[k]);
                const double vca = std::min(1.0, std::max(0.0, e * 5 - .003)) * P.bands[k];
                sum += S2[k].run(S1[k].run(cc) * vca) * nS[k];
            }
            double x = m;
            for (int s = 0; s < 3; ++s) { const double y = hpA[s] * (hs[s * 2] + x - hs[s * 2 + 1]); hs[s * 2 + 1] = x; hs[s * 2] = y; x = y; }
            x = hk2.run(hk1.run(x));
            { const double y = hpA[3] * (hs[6] + x - hs[7]); hs[7] = x; hs[6] = y; x = y; }
            consY += (x * std::min(1.0, det * 40) - consY) * consLp;
            const double ex = std::min(2.5, cPk / ref) * gate;
            const double voc = (sum * 4 + consY * 12 * P.cons) * ex * 2.5;
            const double o = (voc * (1 - P.bal) + m * P.bal) * P.vol;
            if (P.ens) {
                buf[(size_t) (w % 4096)] = (float) o;
                ph1 = std::fmod(ph1 + .25 / fs, 1.0); ph2 = std::fmod(ph2 + 4.5 / fs, 1.0);
                const double tri = 1 - 4 * std::fabs(ph1 - .5), sn = std::sin(2 * PI * ph2);
                const double dl = .0045 + .0018 * tri + .00035 * sn, dr = .0045 - .0018 * tri - .00035 * sn;
                eL += (tap(dl) - eL) * eA; eR += (tap(dr) - eR) * eA;
                w++;
                L[i] = (float) (o * .7 + eL * .7); R[i] = (float) (o * .7 + eR * .7);
            } else { L[i] = R[i] = (float) o; }
        }
    }
private:
    struct Bq { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Bq mfb(double R1, double R2, double R3, double C, double& f) const {
        const double c1 = 2 / (R3 * C), c0 = (R1 + R2) / (R1 * R2 * R3 * C * C), g = -1 / (R1 * C), w0 = std::sqrt(c0), K = w0 / std::tan(w0 / (2 * fs)), d = K * K + c1 * K + c0;
        Bq q; q.b0 = g * K / d; q.b1 = 0; q.b2 = -g * K / d; q.a1 = (2 * c0 - 2 * K * K) / d; q.a2 = (K * K - c1 * K + c0) / d; f = w0 / (2 * PI); return q;
    }
    Bq lp2(double f, double Q) const { const double w0 = 2 * PI * f, K = w0 / std::tan(w0 / (2 * fs)), d = K * K + w0 / Q * K + w0 * w0;
        Bq q; q.b0 = w0 * w0 / d; q.b1 = 2 * w0 * w0 / d; q.b2 = w0 * w0 / d; q.a1 = (2 * w0 * w0 - 2 * K * K) / d; q.a2 = (K * K - w0 / Q * K + w0 * w0) / d; return q; }
    Bq hp2(double f, double Q) const { const double w0 = 2 * PI * f, K = w0 / std::tan(w0 / (2 * fs)), d = K * K + w0 / Q * K + w0 * w0;
        Bq q; q.b0 = K * K / d; q.b1 = -2 * K * K / d; q.b2 = K * K / d; q.a1 = (2 * w0 * w0 - 2 * K * K) / d; q.a2 = (K * K - w0 / Q * K + w0 * w0) / d; return q; }
    double mag(const Bq& q, double f) const {
        const double w = 2 * PI * f / fs, cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
        const double nr = q.b0 + q.b1 * cr + q.b2 * c2r, ni = q.b1 * ci + q.b2 * c2i, dr = 1 + q.a1 * cr + q.a2 * c2r, di = q.a1 * ci + q.a2 * c2i;
        return std::hypot(nr, ni) / std::hypot(dr, di);
    }
    double tap(double d) const { const double r = (double) w - d * fs; const long i = (long) std::floor(r); const double f = r - (double) i; const long n = 4096;
        return buf[(size_t) (((i % n) + n) % n)] * (1 - f) + buf[(size_t) ((((i + 1) % n) + n) % n)] * f; }
    double fs = 48000, sLp = 0, sLpA = 0, lLp = 0, lLpA = 0, hE = 0, lE = 0, eA = 0, uv = 0, gLp = 0, gA = 0, gFF = 1, gSub = 0, gPk = 0, gRel = 0; int gPos = 0;
    Bq A1[10], A2[10], S1[10], S2[10], SK[10]; double nA[10] {}, nS[10] {}, pk[10] {};
    double relN = 0, relH = 0, hpA[4] {}, hs[8] {}; Bq hk1, hk2; double consLp = 0, consY = 0;
    double cPk = 0, cAtk = 0, cRel = 0, gC = 1, det = 0, detA = 0, gate = 0, gateA = 0, hLp = 0, hA = 0;
    std::vector<float> buf; long w = 0; double ph1 = 0, ph2 = 0, eL = 0, eR = 0; Rand rnd;
};

template <class Get> void svcRead(SVCParams& P, Get v) {
    P.on = v("von") >= .5; for (int k = 0; k < 10; ++k) P.bands[k] = v(("b" + std::to_string(k + 1)).c_str());
    P.cons = v("cons"); P.bal = v("bal"); P.harm = v("harm"); P.growl = v("growl"); P.sib = v("sib"); P.vol = v("vvol"); P.clev = v("clev");
    P.ens = v("ens") >= .5; P.hold = v("hold") >= .5;
}

/* Speech player: renders the words on a worker thread (one take per key pitch when the keys set the pitch) and plays
   them back resampled to the host rate. */
class SpeechPlayer {
public:
    SpeechPlayer() { worker = std::thread([this] { loop(); }); }
    ~SpeechPlayer() { { std::lock_guard<std::mutex> l(m); quit = true; } cv.notify_all(); if (worker.joinable()) worker.join(); }
    void configure(const std::string& text, int voice, double semi, double speed, bool perNote) {
        std::lock_guard<std::mutex> l(m);
        if (text == want.text && voice == want.voice && semi == want.semi && speed == want.speed && perNote == want.perNote) return;
        want = {text, voice, semi, speed, perNote}; gen++; queue.clear(); queue.push_back(-1);
        if (perNote) for (int d = 0; d <= 24; ++d) { if (60 + d < 128) queue.push_back(60 + d); if (d && 60 - d >= 0) queue.push_back(60 - d); }
        cv.notify_all();
    }
    void request(int note) { std::lock_guard<std::mutex> l(m); queue.insert(queue.begin(), note); cv.notify_all(); }
    // the take for a note (or the plain one); never blocks the audio thread
    std::shared_ptr<const std::vector<float>> take(int note) {
        std::unique_lock<std::mutex> l(slotLock, std::try_to_lock);
        if (!l.owns_lock()) return {};
        if (note >= 0 && note < 128 && slots[(size_t) note]) return slots[(size_t) note];
        return base;
    }
    bool hasNote(int note) { std::unique_lock<std::mutex> l(slotLock, std::try_to_lock); return l.owns_lock() && note >= 0 && note < 128 && slots[(size_t) note] != nullptr; }
private:
    struct Want { std::string text; int voice = -1; double semi = 0, speed = 1; bool perNote = false; };
    void loop() {
        Rand rnd;
        for (;;) {
            Want w; int note; unsigned g;
            { std::unique_lock<std::mutex> l(m); cv.wait(l, [this] { return quit || !queue.empty(); }); if (quit) return;
              note = queue.front(); queue.erase(queue.begin()); w = want; g = gen; }
            const auto* V = tts::voices(); const auto& vc = V[std::max(0, std::min(5, w.voice))];
            tts::Opts o; o.robot = vc.robot; o.speed = w.speed; o.sr = 22050;
            o.pitch = note < 0 ? vc.pitch * std::pow(2, w.semi / 12) : 440 * std::pow(2, (note - 69) / 12.0);
            auto out = std::make_shared<const std::vector<float>>(tts::render(w.text, o, rnd));
            std::lock_guard<std::mutex> l(m);
            if (g != gen) { if (note < 0) {} continue; }
            std::lock_guard<std::mutex> s(slotLock);
            if (note < 0) { base = out; for (auto& x : slots) x.reset(); } else slots[(size_t) note] = out;
        }
    }
    std::thread worker; std::mutex m, slotLock; std::condition_variable cv; bool quit = false; unsigned gen = 0;
    Want want; std::vector<int> queue; std::shared_ptr<const std::vector<float>> base; std::array<std::shared_ptr<const std::vector<float>>, 128> slots;
};

// one voice playing a take: start, stop (with a short fade), loop
struct SpeechVoice {
    std::shared_ptr<const std::vector<float>> buf; double pos = 0, rate = 1, fade = 1; bool on = false, releasing = false, loop = false;
    void start(std::shared_ptr<const std::vector<float>> b, double hostSr, bool lp) { if (!b) return; buf = std::move(b); pos = 0; rate = 22050 / hostSr; on = true; releasing = false; fade = 1; loop = lp; }
    void stop() { if (on) releasing = true; }
    void render(float* out, int n, double gain) {
        for (int i = 0; i < n; ++i) {
            double s = 0;
            if (on && buf) {
                const auto& B = *buf; const size_t len = B.size();
                if (pos >= (double) len - 1) { if (loop && !releasing) pos = 0; else { on = false; } }
                if (on) { const size_t k = (size_t) pos; const double f = pos - (double) k; s = B[k] * (1 - f) + B[std::min(len - 1, k + 1)] * f; pos += rate; }
                if (releasing) { fade -= 1.0 / 600; if (fade <= 0) { on = false; fade = 0; } }
                s *= fade;
            }
            out[i] = (float) (s * gain);
        }
    }
};

/* SVC-350 effect */
class SVC350Fx : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int maxBlock) override {
        fs = sr; svc.prepare(sr); car.prepare(sr);
        // the carrier: the Prophet-5 "Vocoder carrier" program
        car.C.oAsaw = true; car.C.oApul = true; car.C.oApw = .3; car.C.oBsaw = true; car.C.oBfine = .07; car.C.mA = .9; car.C.mB = .8; car.C.mN = .03;
        car.C.cut = .95; car.C.res = .05; car.C.fenv = 0; car.C.fkbd = true; car.C.fa = 0; car.C.fd = .4; car.C.fs = 1; car.C.fr = .2; car.C.aa = 0; car.C.ad = .4; car.C.as = 1; car.C.ar = .18; car.C.vint = .45;
        cb.assign((size_t) maxBlock + 16, 0.f); mb.assign((size_t) maxBlock + 16, 0.f); changed();
    }
    void changed() override {
        svcRead(svc.P, [this](const char* k) { return v(k); });
        car.C.cut = v("ccut"); car.C.mN = v("cnoise"); car.C.uni = false;
        const int d = (int) v("drone"); if (d != drone) { if (drone > 0 && droning) car.noteOff(drone); droning = false; drone = d; }
    }
    void noteOn(int n, float) override { if (droning) { car.noteOff(drone); droning = false; } car.noteOn(n); held++; }
    void noteOff(int n) override { car.noteOff(n); held = std::max(0, held - 1); }
    void allNotesOff() override { car.allOff(); held = 0; droning = false; }
    void pitchBend(double s) override { car.bend = s; }
    void process(const float* inL, const float* inR, const float* scL, const float* scR, float* L, float* R, int n, const Transport&) override {
        if ((int) cb.size() < n) { cb.resize((size_t) n); mb.resize((size_t) n); }
        const bool side = v("carrier") >= .5;
        if (side) for (int i = 0; i < n; ++i) cb[(size_t) i] = scL ? (scL[i] + scR[i]) * .5f : 0.f;
        else {
            if (drone > 0 && held == 0 && !droning) { car.noteOn(drone); droning = true; }
            car.render(cb.data(), n);
        }
        for (int i = 0; i < n; ++i) mb[(size_t) i] = inL ? (inL[i] + inR[i]) * .5f : 0.f;
        svc.run(cb.data(), mb.data(), L, R, n);
    }
private:
    SVCCore svc; P5Core car; std::vector<float> cb, mb; double fs = 48000; int drone = 0, held = 0; bool droning = false;
};

/* Omega Vox: Prophet-5 carrier + talking machine -> vocoder */
class Vox : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int maxBlock) override { fs = sr; svc.prepare(sr); car.prepare(sr); cb.assign((size_t) maxBlock + 16, 0.f); mb.assign((size_t) maxBlock + 16, 0.f); sb.assign((size_t) maxBlock + 16, 0.f); changed(); }
    void changed() override {
        p5Read(car.C, [this](const char* k) { return v(k); }); car.wheel = std::max(v("wheel"), wheelCC);
        svcRead(svc.P, [this](const char* k) { return v(k); });
        player.configure(text, (int) v("voice"), v("semi"), v("speed"), false);
    }
    void setText(const std::string& t) override { text = t; player.configure(text, (int) v("voice"), v("semi"), v("speed"), false); }
    void noteOn(int n, float) override { car.noteOn(n); if (held++ == 0 && (int) v("trig") != 2) voice.start(player.take(-1), fs, (int) v("trig") >= 1); }
    void noteOff(int n) override { car.noteOff(n); held = std::max(0, held - 1); if (held == 0 && (int) v("trig") == 1) voice.stop(); }
    void allNotesOff() override { car.allOff(); held = 0; voice.stop(); }
    void pitchBend(double s) override { car.bend = s; }
    void modWheel(double w) override { wheelCC = w; car.wheel = std::max(v("wheel"), w); }
    void process(const float*, const float*, const float* scL, const float* scR, float* L, float* R, int n, const Transport&) override {
        if ((int) cb.size() < n) { cb.resize((size_t) n); mb.resize((size_t) n); sb.resize((size_t) n); }
        // "Always loop": the words keep going; otherwise a new phrase starts with the first key
        if ((int) v("trig") == 2 && !voice.on) voice.start(player.take(-1), fs, true);
        car.render(cb.data(), n);
        voice.render(sb.data(), n, v("sgain"));
        const int src = (int) v("modsrc");
        for (int i = 0; i < n; ++i) { const float ext = scL ? (scL[i] + scR[i]) * .5f : 0.f; mb[(size_t) i] = src == 0 ? sb[(size_t) i] : src == 1 ? ext : sb[(size_t) i] + ext; }
        svc.run(cb.data(), mb.data(), L, R, n);
        // a soft ceiling: the vocoder can peak hard on loud chords
        auto lim = [](float x) { const float ax = std::fabs(x); return ax <= .8f ? x : std::copysign(.8f + .2f * (float) std::tanh((ax - .8f) / .2f), x); };
        for (int i = 0; i < n; ++i) { L[i] = lim(L[i]); R[i] = lim(R[i]); }
    }
private:
    SVCCore svc; P5Core car; SpeechPlayer player; SpeechVoice voice; std::vector<float> cb, mb, sb; std::string text; double fs = 48000, wheelCC = 0; int held = 0;
};

/* Omega Speech: the talking machine on its own */
class Speech : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int) override { fs = sr; changed(); }
    void changed() override { player.configure(text, (int) v("voice"), v("semi"), v("speed"), v("keys") >= .5); }
    void setText(const std::string& t) override { text = t; changed(); }
    void noteOn(int n, float vel) override {
        const bool keys = v("keys") >= .5; if (keys && !player.hasNote(n)) player.request(n);
        voice.start(player.take(keys ? n : -1), fs, (int) v("trig") >= 1); note = n; velo = vel;
    }
    void noteOff(int n) override { if (n == note && (int) v("trig") == 1) voice.stop(); }
    void allNotesOff() override { voice.stop(); }
    void process(const float*, const float*, const float*, const float*, float* L, float* R, int n, const Transport&) override {
        if ((int) v("trig") == 2 && !voice.on) voice.start(player.take(-1), fs, true);
        voice.render(L, n, v("sgain") * (v("trig") == 2 ? 1.0 : .4 + .6 * velo));
        for (int i = 0; i < n; ++i) R[i] = L[i];
    }
private:
    SpeechPlayer player; SpeechVoice voice; std::string text; double fs = 48000; int note = -1; float velo = 1;
};

}  // namespace omega::dsp
