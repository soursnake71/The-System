// Omega 808 effects: a line-for-line C++ port of eight/fx.js (Rev7, a Yamaha REV7-style digital reverb, and TC2290, a
// TC Electronic 2290-style delay). Same arithmetic in double precision, float delay buffers like the JS Float32Arrays,
// so eight/tools/compare.mjs can check the two render the same samples.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eight {

inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double jsround(double x) { return std::floor(x + 0.5); }   // JavaScript Math.round
static const double PI = 3.141592653589793;

// TPT one-pole (lowpass state; hp = x - lp)
struct OnePole {
  double s = 0, G = 0;
  void setFc(double fc, double sr) { double w = std::tan(PI * clampd(fc, 1, sr * 0.45) / sr); G = w / (1 + w); }
  double lp(double x) { double v = (x - s) * G; double y = v + s; s = y + v; return y; }
  double hp(double x) { return x - lp(x); }
};

/* ---------------- REV7 ---------------- */
static const double ER_T[12] = {7.1, 11.3, 17.9, 23.1, 29.7, 35.3, 41.9, 47.3, 53.9, 61.7, 67.3, 79.1};
static const double ER_G[12] = {0.90, 0.82, 0.76, 0.70, 0.63, 0.58, 0.52, 0.47, 0.42, 0.37, 0.33, 0.28};
static const double FDN_MS[8] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1};
static const double AP_MS[4] = {3.22, 2.43, 8.59, 6.28};
static const int REV_TAPS = 24;

struct RevProgram { const char* name; int type; double time, predelay, hf, diffusion, density, lpf, hpf, er, size; };
static const RevProgram REV_PROGRAMS[] = {
  {"Hall 1",       0, 2.6, 30, 0.70, 0.8, 0.8, 9000,  40,  0.30, 1.4},
  {"Hall 2",       0, 3.5, 45, 0.60, 0.9, 0.9, 7000,  40,  0.25, 1.6},
  {"Room 1",       0, 1.2, 10, 0.80, 0.7, 0.7, 10000, 60,  0.45, 0.8},
  {"Room 2",       0, 0.8, 5,  0.90, 0.6, 0.6, 12000, 80,  0.50, 0.6},
  {"Vocal 1",      0, 2.0, 40, 0.55, 0.8, 0.8, 8000,  100, 0.30, 1.0},
  {"Vocal 2",      0, 1.5, 25, 0.60, 0.7, 0.7, 9000,  120, 0.35, 0.9},
  {"Plate 1",      0, 2.2, 0,  0.85, 1.0, 1.0, 12000, 60,  0.10, 1.0},
  {"Plate 2",      0, 3.0, 15, 0.75, 1.0, 1.0, 10000, 60,  0.10, 1.2},
  {"Early Ref 1",  0, 0.4, 0,  0.90, 0.5, 0.6, 12000, 80,  1.00, 0.8},
  {"Early Ref 2",  0, 0.6, 10, 0.90, 0.5, 1.0, 10000, 80,  1.00, 1.5},
  {"Gate Reverb",  1, 0.25,0,  0.90, 1.0, 1.0, 12000, 80,  0.30, 1.0},
  {"Reverse Gate", 2, 0.35,0,  0.90, 1.0, 1.0, 11000, 80,  1.00, 1.0},
  {"Chamber",      0, 1.7, 20, 0.70, 0.8, 0.7, 9000,  80,  0.40, 1.1},
  {"Large Hall",   0, 5.0, 60, 0.50, 0.9, 0.9, 6500,  30,  0.20, 2.0},
  {"Small Room",   0, 0.5, 2,  0.90, 0.5, 0.5, 14000, 100, 0.60, 0.5},
  {"Gated Snare",  1, 0.18,0,  0.80, 1.0, 1.0, 10000, 150, 0.20, 0.8},
  {"Echo Room",    0, 1.0, 80, 0.70, 0.5, 0.4, 8000,  150, 0.60, 0.9},
  {"Dark Plate",   0, 2.8, 10, 0.45, 1.0, 1.0, 5000,  60,  0.10, 1.1},
  {"Ambience",     0, 0.3, 0,  0.90, 0.9, 0.9, 14000, 100, 0.50, 0.4},
  {"Cathedral",    0, 8.0, 70, 0.50, 0.9, 0.9, 6000,  30,  0.20, 2.0},
};
static const int NUM_REV_PROGRAMS = (int)(sizeof(REV_PROGRAMS) / sizeof(REV_PROGRAMS[0]));

class Rev7 {
 public:
  explicit Rev7(double sr_) : sr(sr_) {
    bufLen = (int)std::ceil(sr * 1.4) + 4;
    buf.assign(bufLen, 0.f);
    for (int i = 0; i < 4; i++) { apLen[i] = std::max(2, (int)jsround(AP_MS[i] * 0.001 * sr)); apBuf[i].assign(apLen[i], 0.f); apW[i] = 0; }
    lineLen = (int)std::ceil(sr * 0.2) + 4;
    for (int i = 0; i < 8; i++) { lines[i].assign(lineLen, 0.f); dLen[i] = fb[i] = dp[i] = ds[i] = o[i] = 0; dI[i] = 0; }
    for (int i = 0; i < REV_TAPS; i++) { tapD[i] = tapGL[i] = tapGR[i] = 0; tapI[i] = 0; }
    lcw = std::cos(2 * PI * 0.7 / sr); lsw = std::sin(2 * PI * 0.7 / sr);
    loadProgram(6);
  }
  void loadProgram(double i) {
    const RevProgram& p = REV_PROGRAMS[(int)clampd(jsround(i), 0, NUM_REV_PROGRAMS - 1)];
    type = p.type; size = p.size; time = p.time; predelay = p.predelay; hf = p.hf;
    diffusion = p.diffusion; density = p.density; lpf = p.lpf; hpf = p.hpf; er = p.er;
    update();
  }
  void set(const std::string& k, double v) {
    if (k == "rev.program") { loadProgram(v); return; }
    else if (k == "rev.time") time = v;
    else if (k == "rev.predelay") predelay = v;
    else if (k == "rev.hf") hf = v;
    else if (k == "rev.diffusion") diffusion = v;
    else if (k == "rev.density") density = v;
    else if (k == "rev.lpf") lpf = v;
    else if (k == "rev.hpf") hpf = v;
    else if (k == "rev.er") er = v;
    else return;
    update();
  }
  void update() {
    lpfF.setFc(lpf, sr); lpfF2.setFc(lpf, sr); hpfF.setFc(hpf, sr);
    preS = clampd(predelay * 0.001 * sr, 0, sr * 0.3);
    const double T = type == 1 ? 1.6 : clampd(time, 0.1, 15);
    const double Th = T * clampd(hf, 0.05, 1);
    for (int i = 0; i < 8; i++) {
      const double d = clampd(FDN_MS[i] * size * 0.001 * sr, 8, lineLen - 8);
      dLen[i] = d;
      const double gl = std::pow(10.0, -3 * d / (T * sr));
      const double gh = std::pow(10.0, -3 * d / (Th * sr));
      fb[i] = gl;
      dp[i] = (gl - gh) / (gl + gh);
    }
    apG = 0.35 + 0.4 * clampd(diffusion, 0, 1);
    if (type == 2) {
      const double win = clampd(time, 0.08, 0.9) * sr;
      nTaps = REV_TAPS;
      for (int i = 0; i < REV_TAPS; i++) {
        const double t = (double)(i + 1) / REV_TAPS;
        tapD[i] = t * win;
        const double gg = 0.15 + 0.85 * t;
        tapGL[i] = (i & 1) ? gg * 0.55 : gg;
        tapGR[i] = (i & 1) ? gg : gg * 0.55;
      }
    } else {
      nTaps = 4 + (int)jsround(clampd(density, 0, 1) * 8);
      for (int i = 0; i < nTaps; i++) {
        tapD[i] = ER_T[i] * size * 0.001 * sr;
        tapGL[i] = (i & 1) ? ER_G[i] * 0.6 : ER_G[i];
        tapGR[i] = (i & 1) ? ER_G[i] : ER_G[i] * 0.6;
      }
    }
    erNorm = 1 / std::sqrt(nTaps * 0.45);
    for (int i = 0; i < nTaps; i++) tapI[i] = (int)jsround(preS + tapD[i]);
    for (int k = 0; k < 8; k++) dI[k] = (int)jsround(dLen[k]);
    preI = (int)jsround(preS);
    holdS = clampd(time, 0.05, 1.0) * sr;
    gRel = std::exp(-1 / (0.004 * sr));
    gAtt = std::exp(-1 / (0.0005 * sr));
  }
  // writes the wet signal into outL/outR[off .. off+n)
  void process(const float* inL, const float* inR, float* outL, float* outR, int n, int off = 0) {
    const double erW = type == 2 ? 1 : clampd(er, 0, 1);
    const double lateW = type == 2 ? 0 : (1 - 0.8 * erW);
    double inPk = 0;
    for (int s = off; s < off + n; s++) { const double a = std::fabs((double)inL[s]) + std::fabs((double)inR[s]); if (a > inPk) inPk = a; }
    if (inPk < 1e-7 && outPk < 1e-6) quiet += n; else quiet = 0;
    if (quiet > sr) { for (int s = off; s < off + n; s++) { outL[s] = 0; outR[s] = 0; } return; }
    { const double k = 1 / std::sqrt(lc * lc + ls * ls); lc *= k; ls *= k; }
    double opk = 0;
    const int bl = bufLen, nT = nTaps, ll = lineLen;
    for (int s = off; s < off + n; s++) {
      double x = ((double)inL[s] + (double)inR[s]) * 0.5;
      { double v = (x - lpfF.s) * lpfF.G, y = v + lpfF.s; lpfF.s = y + v; x = y;
        v = (x - hpfF.s) * hpfF.G; y = v + hpfF.s; hpfF.s = y + v; x -= y; }
      const int wi = w;
      buf[wi] = (float)x;
      double eL = 0, eR = 0;
      for (int t = 0; t < nT; t++) {
        int r = wi - tapI[t]; if (r < 0) r += bl;
        const double v = buf[r];
        eL += v * tapGL[t]; eR += v * tapGR[t];
      }
      eL *= erNorm; eR *= erNorm;
      double lL = 0, lR = 0;
      if (lateW > 0) {
        int r0 = wi - preI; if (r0 < 0) r0 += bl;
        double d = buf[r0];
        for (int a = 0; a < 4; a++) {
          std::vector<float>& b = apBuf[a]; const int p = apW[a];
          const double z = b[p];
          const double v = d + apG * z;
          b[p] = (float)v;
          d = z - apG * v;
          apW[a] = p + 1 >= apLen[a] ? 0 : p + 1;
        }
        { const double c = lc * lcw - ls * lsw; ls = ls * lcw + lc * lsw; lc = c; }
        const double m = ls * 0.0012 * sr * 0.5;
        for (int k = 0; k < 8; k++) {
          double y;
          if (k == 0 || k == 5) {
            double r = lw - (dLen[k] + (k == 0 ? m : -m)); while (r < 0) r += ll; if (r >= ll) r -= ll;
            const int i = (int)std::floor(r); const double f = r - i; const int j = i + 1 >= ll ? 0 : i + 1;
            const std::vector<float>& Lk = lines[k];
            y = (double)Lk[i] + ((double)Lk[j] - (double)Lk[i]) * f;
          } else { int r = lw - dI[k]; if (r < 0) r += ll; y = lines[k][r]; }
          const double p = dp[k];
          ds[k] = fb[k] * (1 - p) * y + p * ds[k];
          o[k] = ds[k];
        }
        const double a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3];
        const double a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
        const double b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
        const double b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
        const double h = 0.35355339059327373;
        const double c0 = (b0 + b4) * h, c1 = (b1 + b5) * h, c2 = (b2 + b6) * h, c3 = (b3 + b7) * h;
        const double c4 = (b0 - b4) * h, c5 = (b1 - b5) * h, c6 = (b2 - b6) * h, c7 = (b3 - b7) * h;
        const double inj = d * 0.5;
        const int lwi = lw;
        lines[0][lwi] = (float)(c0 + inj); lines[1][lwi] = (float)(c1 - inj); lines[2][lwi] = (float)(c2 + inj); lines[3][lwi] = (float)(c3 - inj);
        lines[4][lwi] = (float)(c4 + inj); lines[5][lwi] = (float)(c5 - inj); lines[6][lwi] = (float)(c6 + inj); lines[7][lwi] = (float)(c7 - inj);
        lw = lwi + 1 >= lineLen ? 0 : lwi + 1;
        lL = (o[0] - o[2] + o[4] - o[6]) * 0.6;
        lR = (o[1] - o[3] + o[5] - o[7]) * 0.6;
        if (type == 1) {
          const double ax = std::fabs(x);
          gEnv = ax > gEnv ? ax : gEnv * 0.9995;
          if (gEnv > 0.01) gHold = holdS;
          else if (gHold > 0) gHold -= 1;
          const double tgt = gHold > 0 ? 1 : 0;
          const double c = tgt > gGain ? gAtt : gRel;
          gGain = tgt + (gGain - tgt) * c;
          lL *= gGain * 1.6; lR *= gGain * 1.6;
        }
      }
      w = w + 1 >= bufLen ? 0 : w + 1;
      const double yl = erW * eL + lateW * lL, yr = erW * eR + lateW * lR;
      outL[s] = (float)yl; outR[s] = (float)yr;
      const double a = std::fabs(yl) + std::fabs(yr); if (a > opk) opk = a;
    }
    outPk = opk;
  }

 private:
  double sr;
  int bufLen = 0, w = 0, lineLen = 0, lw = 0;
  std::vector<float> buf, apBuf[4], lines[8];
  int apLen[4] = {}, apW[4] = {};
  double dLen[8], fb[8], dp[8], ds[8], o[8];
  double tapD[REV_TAPS], tapGL[REV_TAPS], tapGR[REV_TAPS];
  int nTaps = 0, tapI[REV_TAPS], dI[8], preI = 0;
  double outPk = 0, quiet = 0;
  OnePole lpfF, lpfF2, hpfF;
  double lc = 1, ls = 0, lcw = 0, lsw = 0;
  double gEnv = 0, gHold = 0, gGain = 0;
  int type = 0;
  double size = 1, time = 2, predelay = 0, hf = 0.8, diffusion = 0.8, density = 0.8, lpf = 10000, hpf = 60, er = 0.3;
  double preS = 0, apG = 0, erNorm = 1, holdS = 0, gRel = 0, gAtt = 0;
};

/* ---------------- TC 2290 ---------------- */
struct Division { const char* name; double beats; };
static const Division DIVISIONS[] = {
  {"1/32", 0.125}, {"1/16T", 1.0 / 6}, {"1/16", 0.25}, {"1/16D", 0.375},
  {"1/8T", 1.0 / 3}, {"1/8", 0.5}, {"1/8D", 0.75}, {"1/4T", 2.0 / 3},
  {"1/4", 1}, {"1/4D", 1.5}, {"1/2", 2}, {"1/2D", 3}, {"1/1", 4}};
static const int NUM_DIVISIONS = (int)(sizeof(DIVISIONS) / sizeof(DIVISIONS[0]));
static const double DLY_MAX_S = 4;

class TC2290 {
 public:
  explicit TC2290(double sr_) : sr(sr_) {
    len = (int)std::ceil(sr * DLY_MAX_S) + 8;
    bL.assign(len, 0.f); bR.assign(len, 0.f);
    update();
    cur = target;
  }
  void setTempo(double b) { bpm = clampd(b, 20, 400); update(); }
  void set(const std::string& k, double v) {
    if (k == "dly.time") time = v;
    else if (k == "dly.sync") sync = v >= 0.5 ? 1 : 0;
    else if (k == "dly.division") division = (int)clampd(jsround(v), 0, NUM_DIVISIONS - 1);
    else if (k == "dly.feedback") feedback = v;
    else if (k == "dly.hicut") hicut = v;
    else if (k == "dly.lowcut") lowcut = v;
    else if (k == "dly.modDepth") modDepth = v;
    else if (k == "dly.modRate") modRate = v;
    else if (k == "dly.pingpong") pingpong = v >= 0.5 ? 1 : 0;
    else if (k == "dly.duck") duck = v;
    else return;
    update();
  }
  void update() {
    const double ms = sync ? DIVISIONS[division].beats * 60000 / bpm : time;
    target = clampd(ms * 0.001 * sr, 1, len - sr * 0.02 - 8);
    hcL.setFc(hicut, sr); hcR.setFc(hicut, sr);
    lcL.setFc(lowcut, sr); lcR.setFc(lowcut, sr);
    fbG = clampd(feedback, 0, 1) * 0.98;
    mDepth = clampd(modDepth, 0, 1) * 0.006 * sr;
    lfoInc = 2 * PI * clampd(modRate, 0.01, 20) / sr; lcw = std::cos(lfoInc); lsw = std::sin(lfoInc);
    smooth = 1 - std::exp(-1 / (0.06 * sr));
    envAtt = 1 - std::exp(-1 / (0.002 * sr));
    envRel = 1 - std::exp(-1 / (0.25 * sr));
  }
  void process(const float* inL, const float* inR, float* outL, float* outR, int n, int off = 0) {
    double inPk = 0;
    for (int s = off; s < off + n; s++) { const double a = std::fabs((double)inL[s]) + std::fabs((double)inR[s]); if (a > inPk) inPk = a; }
    if (inPk < 1e-7 && outPk < 1e-6) quiet += n; else quiet = 0;
    if (quiet > len) { for (int s = off; s < off + n; s++) { outL[s] = 0; outR[s] = 0; } return; }
    { const double k = 1 / std::sqrt(lc * lc + ls * ls); lc *= k; ls *= k; }
    double opk = 0;
    for (int s = off; s < off + n; s++) {
      cur += (target - cur) * smooth;
      { const double c = lc * lcw - ls * lsw; ls = ls * lcw + lc * lsw; lc = c; }
      const double mL = ls * mDepth, mR = lc * mDepth;
      double dL = 0, dR = 0;
      for (int c = 0; c < 2; c++) {
        const std::vector<float>& b = c ? bR : bL;
        double r = w - (cur + mDepth + (c ? mR : mL)); while (r < 0) r += len; if (r >= len) r -= len;
        const int i = (int)std::floor(r); const double f = r - i;
        const int i0 = i - 1 < 0 ? i - 1 + len : i - 1, i2 = i + 1 >= len ? i + 1 - len : i + 1, i3 = i + 2 >= len ? i + 2 - len : i + 2;
        const double y0 = b[i0], y1 = b[i], y2 = b[i2], y3 = b[i3];
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        const double y = ((c3 * f + c2) * f + c1) * f + y1;
        if (c) dR = y; else dL = y;
      }
      const double xl = inL[s], xr = inR[s];
      const double ax = std::fabs(xl) + std::fabs(xr);
      env += (ax - env) * (ax > env ? envAtt : envRel);
      double fL, fR;
      { double v = (dL - hcL.s) * hcL.G, y = v + hcL.s; hcL.s = y + v; double z = y;
        v = (z - lcL.s) * lcL.G; y = v + lcL.s; lcL.s = y + v; fL = (z - y) * fbG;
        v = (dR - hcR.s) * hcR.G; y = v + hcR.s; hcR.s = y + v; z = y;
        v = (z - lcR.s) * lcR.G; y = v + lcR.s; lcR.s = y + v; fR = (z - y) * fbG; }
      fL = std::tanh(fL); fR = std::tanh(fR);
      if (pingpong) { bL[w] = (float)((xl + xr) * 0.5 + fR); bR[w] = (float)fL; }
      else { bL[w] = (float)(xl + fL); bR[w] = (float)(xr + fR); }
      w = w + 1 >= len ? 0 : w + 1;
      double dk = env * 3; if (dk > 1) dk = 1;
      const double gg = 1 - clampd(duck, 0, 1) * dk;
      outL[s] = (float)(dL * gg); outR[s] = (float)(dR * gg);
      const double a = std::fabs(dL) + std::fabs(dR); if (a > opk) opk = a;
    }
    outPk = opk;
  }

 private:
  double sr;
  int len = 0, w = 0;
  std::vector<float> bL, bR;
  double time = 375; int sync = 1, division = 6; double feedback = 0.35, hicut = 6000, lowcut = 120;
  double modDepth = 0.15, modRate = 0.6; int pingpong = 1; double duck = 0.3, bpm = 120;
  OnePole hcL, hcR, lcL, lcR;
  double cur = 0, lc = 1, ls = 0, env = 0, quiet = 0, outPk = 0;
  double target = 0, fbG = 0, mDepth = 0, lfoInc = 0, lcw = 0, lsw = 0, smooth = 0, envAtt = 0, envRel = 0;
};

}  // namespace eight
