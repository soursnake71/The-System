// Omega XR10: a 16-bit PCM drum machine modelled on the Akai XR10's front panel and way of working
// (64 sounds, 50 preset rhythms with variation / fill in / intro / ending, 49 user patterns, 20 songs, pad banks,
// accent, timing correct, effect send), plus Yamaha RX5-style voice editing (pitch, bend, attack, decay, tone,
// reverse, loop, pan, level per sound) and a step grid.
// No libc: the same source runs in the VST3 and, compiled to WebAssembly, in the Android / web app.
#pragma once
#include "styles.h"
#include "analog.h"

namespace xr {
static const int PPQ = 96, NPAT = 99, NPRESET = 50, MAXEV = 1024, NSONG = 20, MAXSTEP = 99, NBANK = 4, NPADS = 15;
static const int MAXV = 48;                      // voice slots (polyphony limit + slots for fading voices)
static const int ROM_POOL = 4200000;             // 16-bit samples for the 64 sounds (~95 s)
static const int USER_POOL = 4 * 1024 * 1024;    // 16-bit samples for your own sounds (~95 s)

struct Event { u16 tick; u8 sound; u8 vel; };
struct Part { u8 bars; u8 r0; u16 count; Event ev[MAXEV]; };
struct Pattern { char name[10]; u8 beats, sub, swing, tempo; Part parts[NPARTS]; };
struct SongStep { u8 pattern, part, repeat, r0; };
struct Song { char name[10]; u8 len, r0; u16 tempo; SongStep steps[MAXSTEP]; };

// RX5-style voice parameters, per sound
// The XR10's own sound parameters, with its ranges and steps (service manual specifications, and Music Technology's
// review, June 1990, which gives tune as +25/-26 and sweep decay as 0-63): volume 0-31, tune (100 cents a step), fine -8..+7 (6.25 cents a step), pan L15..R15, effect send 0-31, decay and hold (the DCA) 0-31,
// sweep decay and depth 0-31, sweep polarity, reverse, velocity feel. Then the RX5-style extras the XR10 doesn't have.
enum { V_LEVEL, V_PAN, V_TUNE, V_FINE, V_SEND, V_DECAY, V_HOLD, V_SWDECAY, V_SWDEPTH, V_SWPOL, V_REVERSE, V_VELFEEL,
       V_ATTACK, V_TONE, V_LOOP, V_GROUP, NVP, NVP_XR10 = V_ATTACK };
struct ParamDef { const char* name; s16 lo, hi; };
static const ParamDef VPARAMS[NVP] = {
  {"LEVEL", 0, 31}, {"PAN", -15, 15}, {"TUNE", -26, 25}, {"FINE", -8, 7}, {"FX SEND", 0, 31}, {"DECAY", 0, 31}, {"HOLD", 0, 31},
  {"SWEEP DCY", 0, 63}, {"SWEEP DPT", 0, 31}, {"SWEEP", 0, 1}, {"REVERSE", 0, 1}, {"VELOCITY", 0, 1},
  {"ATTACK*", 0, 99}, {"TONE*", 0, 99}, {"LOOP*", 0, 1}, {"CHOKE GRP*", 0, 8}};
// the sample rate the voices, the digital mixer and the DAC run at. Neither the service manual nor the 1990 reviews print
// it. 12 MHz / 360 = 33.33 kHz is used because: the gate arrays and CPU run from one 12 MHz resonator (X1, CSA12.0MT);
// the output filters are 12 kHz (0.36 fs, the usual margin under a 16.7 kHz Nyquist; a 44.1 kHz DAC would have had
// ~20 kHz filters); and the XE8, Akai's drum module of the year before, is documented with 33 kHz (and 44 kHz) samples.
static const double MACHINE_RATE = ROM_RATE;

// settings the panels and the LCD both change (get / set by key)
enum {
  K_TEMPO, K_PATTERN, K_PART, K_REC, K_LEN, K_BEATS, K_SWING, K_CLICK, K_COUNTIN, K_TC, K_ACCENT, K_BANK, K_FXTYPE,
  K_FXTIME, K_FXLEVEL, K_POLY, K_MIDICH, K_MIDIMAP, K_SYNC, K_SONG, K_SOUND, K_PLAYING, K_VOLUME, K_MODE, K_SUB,
  K_CLICKLVL, K_SONGSTEP, K_PADVEL, K_ACCENTVEL, K_USERSAMPLE, K_MAINVAR, K_BAR, K_EFFECTVR, NKEYS
};
enum { M_PATTERN, M_SONG, M_SOUND, M_UTILITY };
// panel buttons
enum {
  B_MODE, B_ERASE, B_EDIT, B_ACCENT, B_PADBANK, B_TC, B_INTRO, B_BREAK, B_START, B_LEFT, B_RIGHT, B_UP, B_DOWN,
  B_MINUS, B_PLUS, B_ENTER, B_D0, B_D9 = B_D0 + 9, B_PAD1, B_PAD15 = B_PAD1 + 14, B_MODE_PATTERN, B_MODE_SONG,
  B_MODE_SOUND, B_MODE_UTILITY, NBUTTONS
};
static const char* const PART_CODES[NPARTS] = {"A", "B", "C", "FA", "FB", "FC", "IN", "EN"};
static const char* const PART_NAMES[NPARTS] = {"VAR A", "VAR B", "VAR C", "FILL A", "FILL B", "FILL C", "INTRO", "ENDING"};
static const char* const TC_NAMES[7] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "OFF"};
static const int TC_TICKS[7] = {96, 48, 32, 24, 16, 12, 1};
static const char* const FX_NAMES[7] = {"ROOM*", "HALL*", "PLATE*", "GATED*", "DELAY*", "EXT JACK", "OFF"};

struct Status {   // what the screens show, read by the UI
  char lcd[2][17];
  s32 mode, playing, recording, accent, bank, pattern, part, bar, beat, tempo10, sound, padHit[NPADS];
  s32 erase, editing, fxType, tc, song, songStep, cursor, blink, version, beats, sub, bars, padSound[NPADS];
};

// ---------------- string helpers (no libc) ----------------
struct Str {
  char* p; int n, cap;
  Str(char* buf, int c) : p(buf), n(0), cap(c) { p[0] = 0; }
  Str& s(const char* t) { while (*t && n < cap - 1) p[n++] = *t++; p[n] = 0; return *this; }
  Str& c(char ch) { if (n < cap - 1) p[n++] = ch; p[n] = 0; return *this; }
  Str& i(int v, int width = 0, char padc = '0') {
    char tmp[12]; int k = 0; bool neg = v < 0; unsigned u = neg ? -v : v;
    do { tmp[k++] = (char)('0' + u % 10); u /= 10; } while (u && k < 11);
    if (neg) tmp[k++] = '-';
    while (k < width) tmp[k++] = padc;
    while (k) c(tmp[--k]);
    return *this;
  }
  Str& sgn(int v) { if (v > 0) c('+'); return i(v); }
  Str& padTo(int w) { while (n < w && n < cap - 1) c(' '); return *this; }
  Str& right(const char* t, int w) { int l = 0; while (t[l]) l++; while (n + l < w) c(' '); return s(t); }
};
inline void copyName(char* dst, const char* src, int max) { int i = 0; for (; src[i] && i < max - 1; i++) dst[i] = src[i]; dst[i] = 0; }
inline void bytecopy(void* d, const void* s, u32 n) { u8* a = (u8*)d; const u8* b = (const u8*)s; for (u32 i = 0; i < n; i++) a[i] = b[i]; }
inline void bytezero(void* d, u32 n) { u8* a = (u8*)d; for (u32 i = 0; i < n; i++) a[i] = 0; }

// ---------------- effects ----------------
struct Comb {
  float* buf; int len, idx; float fb, damp, store;
  float run(float x) { float y = buf[idx]; store = y * (1 - damp) + store * damp; buf[idx] = x + store * fb; if (++idx >= len) idx = 0; return y; }
};
struct Allpass {
  float* buf; int len, idx;
  float run(float x) { float b = buf[idx]; float y = -x + b; buf[idx] = x + b * 0.5f; if (++idx >= len) idx = 0; return y; }
};
struct Fx {
  static const int CMAX = 7400, AMAX = 2600, DMAX = 192000 * 3 / 2 + 8;
  float cbuf[16][CMAX], abuf[8][AMAX], dl[DMAX], dr[DMAX];
  Comb comb[16]; Allpass ap[8];
  int type = 0, dIdx = 0, dLen = 1; float time = 0.5f, gateEnv = 0, gateHold = 0; double sr = 44100;
  float lpL = 0, lpR = 0;
  void setup(double rate, int t, float tm) {
    sr = rate; type = t; time = tm;
    static const int CL[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, AL[4] = {556, 441, 341, 225};
    double sc = sr / 44100.0;
    double size = type == 0 ? 0.55 : (type == 1 ? 1.0 : (type == 2 ? 0.8 : 0.9));
    for (int i = 0; i < 16; i++) {
      int L = (int)((CL[i % 8] + (i >= 8 ? 23 : 0)) * sc * size); if (L < 8) L = 8; if (L > CMAX) L = CMAX;
      comb[i].buf = cbuf[i]; comb[i].len = L; if (comb[i].idx >= L) comb[i].idx = 0;
      float fb = type == 0 ? 0.70f + 0.2f * time : (type == 1 ? 0.80f + 0.18f * time : (type == 2 ? 0.78f + 0.19f * time : 0.9f));
      comb[i].fb = fb; comb[i].damp = type == 2 ? 0.15f : (type == 0 ? 0.4f : 0.3f);
    }
    for (int i = 0; i < 8; i++) { int L = (int)((AL[i % 4] + (i >= 4 ? 23 : 0)) * sc); if (L > AMAX) L = AMAX; ap[i].buf = abuf[i]; ap[i].len = L; if (ap[i].idx >= L) ap[i].idx = 0; }
    dLen = (int)(sr * (0.06 + 0.9 * time)); if (dLen >= DMAX) dLen = DMAX - 1; if (dLen < 1) dLen = 1; if (dIdx >= dLen) dIdx = 0;
  }
  void clear() { bytezero(cbuf, sizeof cbuf); bytezero(abuf, sizeof abuf); bytezero(dl, sizeof dl); bytezero(dr, sizeof dr); for (auto& c : comb) c.store = 0; }
  void run(float inL, float inR, float& oL, float& oR) {
    if (type == 4) {   // stereo ping-pong delay
      float yl = dl[dIdx], yr = dr[dIdx];
      dl[dIdx] = (inL + inR) * 0.5f + yr * 0.45f; dr[dIdx] = yl * 0.9f;
      if (++dIdx >= dLen) dIdx = 0;
      lpL += 0.5f * (yl - lpL); lpR += 0.5f * (yr - lpR);
      oL = lpL; oR = lpR; return;
    }
    float in = (inL + inR) * 0.015f, l = 0, r = 0;
    for (int i = 0; i < 8; i++) l += comb[i].run(in);
    for (int i = 8; i < 16; i++) r += comb[i].run(in);
    for (int i = 0; i < 4; i++) l = ap[i].run(l);
    for (int i = 4; i < 8; i++) r = ap[i].run(r);
    if (type == 3) {   // gated: opens on hits, shuts after ~(time) seconds
      float a = (inL < 0 ? -inL : inL) + (inR < 0 ? -inR : inR);
      if (a > 0.05f) gateHold = (float)(sr * (0.12 + 0.35 * time));
      float target = gateHold > 0 ? 1.f : 0.f; if (gateHold > 0) gateHold -= 1;
      gateEnv += (target - gateEnv) * (target > gateEnv ? 0.01f : 0.004f);
      l *= gateEnv * 1.6f; r *= gateEnv * 1.6f;
    }
    oL = l; oR = r;
  }
};

struct Voice {
  int active = 0, sound = -1, fading = 0, rev = 0, loop = 0, group = 0;
  const s16* data = 0; int len = 0, loopStart = 0;
  double pos = 0, inc = 1, baseInc = 1, sweepNow = 0, sweepMul = 1;
  float gL = 0, gR = 0, gE = 0, env = 0, attackInc = 1, decayMul = 1, fadeMul = 1, lp = 0, lp2 = 0, lpCoef = 1;
  u32 age = 0, holdLeft = 0; int phase = 0;   // 0 attack, 1 hold, 2 decay
};

struct SampleRef { u32 start, len; };

struct Machine {
  // ---------- stored state (saved with the plugin / app) ----------
  Pattern pat[NPAT];
  Song songs[NSONG];
  s16 vp[NSOUNDS][NVP];
  u8 pads[NBANK][NPADS];
  s32 set[NKEYS];
  SampleRef user[NSOUNDS];
  u32 userUsed = 0;
  s16 userPool[USER_POOL];
  // ---------- ROM ----------
  s16 rom[ROM_POOL];
  SampleRef romRef[NSOUNDS];
  float renderBuf[44100 * 3];
  // ---------- runtime ----------
  double sr = 44100;                 // the host's rate
  double mr = MACHINE_RATE;          // the machine's own rate
  Voice v[MAXV];
  // output stage
  OutputChannel outL, outR, outE;
  Decimator decL, decR, decE;
  int os = 4;
  double machPhase = 1, dacL = 0, dacR = 0, dacE = 0, inductance = 0, dacVolts = 0.45;
  float mixL[64], mixR[64], mixE[64]; int mixPos = 64, mixLen = 64;
  u32 ageCounter = 0;
  Fx fx;
  int fxConfigured = -1;
  float fxTimeConfigured = -1;
  Status st;
  int playing = 0, recording = 0, held[NBUTTONS];
  double tick = 0, hostPpqLast = -1;
  int evIdx = 0, partLenTicks = 0, curPat = 0, curPart = 0, mainVar = 0, queuedPart = -1, queuedPat = -1, breakPending = 0, paused = 0;
  int crashNext = 0, songStepIdx = 0, songRepeat = 0, playingSong = 0, countIn = 0;
  int skipT[16], skipS[16], nSkip = 0;
  double clickPhase = 0; float clickEnv = 0, clickFreq = 1000;
  float volume = 0.8f, volSmooth = 0.8f;
  // UI state
  int menuPage = 0, editMode = 0, cursor = 0, typed = -1, typedDigits = 0, confirm = 0, lastPad = 0;
  int msgTimer = 0; char msg[2][17];
  int knobAccum = 0;
  int eraseUsed = 0;
  s32 padHitCount[NPADS];
  u32 samplesRun = 0;
  int hostPlaying = 0;

  // ================= setup =================
  // pack: optional sample ROM ("XRPK": count, then per sound: u32 sound, u32 length, s16 data[length] at 44.1 kHz).
  // Sounds in the pack are your recordings; the rest are synthesized.
  bool fromPack[NSOUNDS];
  void init(double rate, const u8* pack = 0, u32 packLen = 0) {
    sr = rate > 8000 ? rate : 44100;
    const u8* entry[NSOUNDS];
    u32 entryLen[NSOUNDS];
    bool synthInPack[NSOUNDS];
    for (int i = 0; i < NSOUNDS; i++) { entry[i] = 0; entryLen[i] = 0; fromPack[i] = false; synthInPack[i] = false; }
    if (pack && packLen >= 8) {
      u32 hdr[2]; bytecopy(hdr, pack, 8);
      u32 rate = 0; if (packLen >= 12) bytecopy(&rate, pack + 8, 4);
      if (hdr[0] == 0x3250525a && rate == (u32)ROM_HZ) {   // "XRP2" at the machine's rate
        u32 at = 12;
        for (u32 k = 0; k < hdr[1] && at + 8 <= packLen; k++) {
          u32 e[2]; bytecopy(e, pack + at, 8); at += 8;
          // bit 16 marks a sound the pack holds pre-rendered from the synthesizer (so the app starts fast)
          u32 snd = e[0] & 0xffff;
          if (snd < (u32)NSOUNDS && at + e[1] * 2 <= packLen) { entry[snd] = pack + at; entryLen[snd] = e[1]; synthInPack[snd] = (e[0] >> 16) & 1; }
          at += e[1] * 2;
        }
      }
    }
    u32 at = 0;
    for (int i = 0; i < NSOUNDS; i++) {
      if (entry[i] && entryLen[i] > 1) {
        u32 n = entryLen[i];
        if (at + n > (u32)ROM_POOL) n = ROM_POOL - at;
        bytecopy(rom + at, entry[i], n * 2);
        romRef[i].start = at; romRef[i].len = n; at += n; fromPack[i] = !synthInPack[i];
        continue;
      }
      int n = renderSound(i, renderBuf, 44100 * 3);
      if (at + (u32)n > (u32)ROM_POOL) n = ROM_POOL - at;
      romRef[i].start = at; romRef[i].len = n;
      for (int k = 0; k < n; k++) { double x = renderBuf[k] * 32767.0; rom[at + k] = (s16)(x > 32767 ? 32767 : (x < -32768 ? -32768 : (x < 0 ? x - 0.5 : x + 0.5))); }
      at += n;
    }
    factoryReset();
    fx.clear();
    setRate(sr);
  }
  void setRate(double rate) {
    if (rate <= 8000) return;
    sr = rate; fxConfigured = -1;
    // the analog stage runs at about 176-192 kHz, then comes down to the host's rate
    os = (int)(192000.0 / sr + 0.5); if (os < 1) os = 1; if (os > 8) os = 8;
    if (inductance <= 0) inductance = LCLowpass::solveL();
    outL.design(sr * os, inductance, false); outR.design(sr * os, inductance, false); outE.design(sr * os, inductance, true);
    decL.design(os); decR.design(os); decE.design(os);
    // DAC full scale, calibrated so a full-scale sine reads the manual's output level with VOLUME at max
    dacVolts = part::OUT_FS_PEAK / stageGain(inductance);
  }
  void defaultVoice(int i) {
    s16* p = vp[i];
    bytezero(p, sizeof(s16) * NVP);
    p[V_LEVEL] = 26; p[V_DECAY] = 31; p[V_HOLD] = 31; p[V_SEND] = 3; p[V_VELFEEL] = 1; p[V_TONE] = 99;
    if (i < 10) { p[V_LEVEL] = 29; p[V_SEND] = 1; }
    else if (i < 20) { p[V_LEVEL] = 26; p[V_SEND] = 8; }
    else if (i >= HATCL && i <= HATOP) { p[V_LEVEL] = 20; p[V_PAN] = 4; p[V_GROUP] = 1; p[V_SEND] = 2; }
    else if (i >= TOM1L && i <= TOM4H) { int k = (i - TOM1L) % 3; p[V_PAN] = (s16)(6 - 6 * k); p[V_LEVEL] = 25; p[V_SEND] = 6; }
    else if (i == CRASH1 || i == CRASH2 || i == CHOKE) { p[V_LEVEL] = 20; p[V_PAN] = i == CRASH2 ? 8 : -8; p[V_GROUP] = 5; p[V_SEND] = 4; }
    else if (i == RIDE) { p[V_LEVEL] = 19; p[V_PAN] = 9; p[V_SEND] = 2; }
    else if (i == CLAPS) { p[V_SEND] = 8; p[V_LEVEL] = 25; }
    else if (i == GUIRO1 || i == GUIRO2) { p[V_GROUP] = 2; p[V_LEVEL] = 20; p[V_PAN] = -6; }
    else if (i == SQKL || i == SQKH) p[V_GROUP] = 3;
    else if (i == TRIL || i == TRIH) { p[V_GROUP] = 4; p[V_LEVEL] = 19; p[V_PAN] = 9; }
    else if (i >= CONGA1 && i <= CONGA4) { p[V_PAN] = (s16)(-9 + 6 * (i - CONGA1)); p[V_LEVEL] = 25; }
    else if (i == CABASA || i == TAMB) { p[V_LEVEL] = 19; p[V_PAN] = -5; }
    else if (i == COWBELL || i == AGOGOL || i == AGOGOH) { p[V_LEVEL] = 20; p[V_PAN] = 5; }
    else if (i == WHISTLE) p[V_LEVEL] = 19;
  }
  void defaultPads() {
    static const u8 B[NBANK][NPADS] = {
      {KICK1, SNARE1, CRASH1, TOM1L, TOM1M, TOM1H, HATCL, HATMD, HATOP, RIM1, CLAPS, RIDE, COWBELL, TAMB, CRASH2},
      {KICK1 + 5, SNARE1 + 5, CLAPS, TOM4L, TOM4M, TOM4H, HATCL, HATMD, HATOP, KICK1 + 6, SNARE1 + 6, RIM1, COWBELL, CLAVES, HITS},
      {CONGA3, CONGA2, CONGA1, CONGA4, TIMBL, TIMBH, AGOGOL, AGOGOH, CLAVES, CABASA, GUIRO1, GUIRO2, WHISTLE, SQKL, SQKH},
      {KICK1 + 2, SNARE1 + 9, CHOKE, TOM3L, TOM3M, TOM3H, TOM2L, TOM2M, TOM2H, RIM2, FINGER, VIBRA, TRIL, TRIH, SNARE1 + 7}};
    bytecopy(pads, B, sizeof pads);
  }
  void clearPattern(int p) {
    Pattern& P = pat[p];
    Str(P.name, 10).s("USER ").i(p + 1, 2);
    P.beats = 4; P.sub = 4; P.swing = 50; P.tempo = 120;
    for (int k = 0; k < NPARTS; k++) { P.parts[k].count = 0; P.parts[k].bars = k < 3 ? 2 : 1; }
  }
  void buildPresets() {
    for (int i = 0; i < NPRESET; i++) {
      Pattern& P = pat[i];
      const StyleDef& sd = STYLES[i];
      copyName(P.name, sd.name, 10);
      P.beats = sd.beats; P.sub = sd.sub; P.swing = sd.swing; P.tempo = sd.tempo;
      for (int k = 0; k < NPARTS; k++) {
        Part& pt = P.parts[k];
        pt.count = 0;
        pt.bars = (u8)buildStylePart(i, k, [&](int t, int s, int vel) { insertEvent(pt, t, s, vel); });
      }
    }
  }
  void factoryReset() {
    buildPresets();
    for (int p = NPRESET; p < NPAT; p++) clearPattern(p);
    for (int s = 0; s < NSONG; s++) { Str(songs[s].name, 10).s("SONG ").i(s + 1, 2); songs[s].len = 0; songs[s].tempo = 0; }
    for (int i = 0; i < NSOUNDS; i++) { defaultVoice(i); user[i].start = user[i].len = 0; }
    userUsed = 0;
    defaultPads();
    for (int k = 0; k < NKEYS; k++) set[k] = 0;
    set[K_TEMPO] = 1200; set[K_TC] = 3; set[K_FXTYPE] = 0; set[K_FXTIME] = 50; set[K_FXLEVEL] = 60; set[K_POLY] = 8;
    set[K_MIDICH] = 0; set[K_MIDIMAP] = 0; set[K_SYNC] = 1; set[K_CLICK] = 1; set[K_CLICKLVL] = 60; set[K_COUNTIN] = 0;
    set[K_PADVEL] = 0; set[K_ACCENTVEL] = 127; set[K_VOLUME] = 80; set[K_EFFECTVR] = 70; set[K_FXTYPE] = 5;
    curPat = 0; curPart = 0; mainVar = 0;
    set[K_TEMPO] = pat[0].tempo * 10;
  }

  // ================= events =================
  static int insertEvent(Part& pt, int t, int s, int vel) {
    if (pt.count >= MAXEV) return -1;
    int i = pt.count;
    while (i > 0 && pt.ev[i - 1].tick > t) { pt.ev[i] = pt.ev[i - 1]; i--; }
    pt.ev[i].tick = (u16)t; pt.ev[i].sound = (u8)s; pt.ev[i].vel = (u8)vel;
    pt.count++;
    return i;
  }
  static void removeEvent(Part& pt, int i) { for (int k = i; k < pt.count - 1; k++) pt.ev[k] = pt.ev[k + 1]; pt.count--; }
  int partTicks(int p, int part) { return pat[p].parts[part].bars * pat[p].beats * PPQ; }
  int barTicks(int p) { return pat[p].beats * PPQ; }
  bool patternEmpty(int p) { for (int k = 0; k < NPARTS; k++) if (pat[p].parts[k].count) return false; return true; }
  int firstEmptyUser() { for (int p = NPRESET; p < NPAT; p++) if (patternEmpty(p)) return p; return -1; }
  // editing a preset copies it to the first empty user pattern first
  bool ensureUser() {
    if (curPat >= NPRESET) return true;
    int d = firstEmptyUser();
    if (d < 0) { message("NO EMPTY USER", "PATTERN"); return false; }
    bytecopy(&pat[d], &pat[curPat], sizeof(Pattern));
    char nm[17]; Str(nm, 17).s("COPIED TO ").i(d + 1, 2);
    curPat = d;
    message(nm, pat[d].name);
    return true;
  }

  // ================= voices =================
  const s16* sampleOf(int s, int& len) {
    if (user[s].len) { len = user[s].len; return userPool + user[s].start; }
    len = romRef[s].len; return rom + romRef[s].start;
  }
  // a voice as the XR10's gate arrays play it: the DMA steps through the 16-bit ROM at the machine rate (no
  // interpolation between samples), the TMW4008 applies the digital envelope (hold, then decay), the 5-bit digital
  // level, pan and effect-send controls, and mixes the 8 voices for the DAC.
  // Times for the 0-31 steps are not in the manual and are estimates.
  static double holdSeconds(int h) { return 0.008 * xm::pow(1.18, h); }       // 8 ms .. ~1.4 s (31 = hold to the end)
  static double decaySeconds(int d) { return 0.004 * xm::pow(1.22, d); }      // 4 ms .. ~1.9 s (31 = no decay)
  static double sweepSeconds(int d) { return 0.002 * xm::pow(1.12, d); }      // 2 ms .. ~2.5 s over 0-63
  void trigger(int s, int vel, bool fromSeq = false) {
    if (s < 0 || s >= NSOUNDS) return;
    (void)fromSeq;
    const s16* p = vp[s];
    int grp = p[V_GROUP];
    for (int i = 0; i < MAXV; i++) if (v[i].active && !v[i].fading && (v[i].sound == s || (grp && v[i].group == grp))) startFade(v[i]);
    int poly = set[K_POLY] < 1 ? 8 : set[K_POLY], live = 0, oldest = -1; u32 oa = 0xffffffff;
    for (int i = 0; i < MAXV; i++) if (v[i].active && !v[i].fading) { live++; if (v[i].age < oa) { oa = v[i].age; oldest = i; } }
    if (live >= poly && oldest >= 0) startFade(v[oldest]);
    int slot = -1;
    for (int i = 0; i < MAXV; i++) if (!v[i].active) { slot = i; break; }
    if (slot < 0) {
      float q = 1e9f;
      for (int i = 0; i < MAXV; i++) if (v[i].fading && v[i].env < q) { q = v[i].env; slot = i; }
      if (slot < 0) slot = 0;
    }
    Voice& o = v[slot];
    o.data = sampleOf(s, o.len);
    if (o.len <= 1) return;
    o.active = 1; o.fading = 0; o.sound = s; o.group = grp; o.age = ++ageCounter;
    o.rev = p[V_REVERSE]; o.loop = p[V_LOOP];
    o.loopStart = o.len / 2;
    o.pos = o.rev ? o.len - 1 : 0;
    // pitch: whole semitones and 1/16 semitone fine steps
    o.baseInc = xm::pow2((p[V_TUNE] + p[V_FINE] * 0.0625) / 12.0) * ROM_RATE / mr;
    double depth = p[V_SWDEPTH];
    o.sweepNow = p[V_SWPOL] ? depth : -depth;   // UP starts below and rises to the note, DOWN starts above and falls
    o.sweepMul = depth > 0 ? xm::exp(-1.0 / (sweepSeconds(p[V_SWDECAY]) * mr)) : 0;
    o.inc = o.baseInc * (depth > 0 ? xm::pow2(o.sweepNow / 12.0) : 1.0);
    // velocity: the XR10 keeps 15 levels (MIDI velocities are quantized to them). Velocity feel makes the attack less
    // percussive at lower velocities (the review's description); how much is an estimate: up to 6 ms of ramp.
    int vq = (vel * 15 + 126) / 127; if (vq < 1) vq = 1; if (vq > 15) vq = 15;
    double g = p[V_LEVEL] / 31.0 * vq / 15.0;
    int pan = p[V_PAN];
    o.gL = (float)(g * (pan <= 0 ? 1.0 : (15 - pan) / 15.0));
    o.gR = (float)(g * (pan >= 0 ? 1.0 : (15 + pan) / 15.0));
    o.gE = (float)(g * p[V_SEND] / 31.0);
    // envelope (the DCA): attack (extra), hold, decay
    double atk = p[V_ATTACK] / 99.0 * 0.25;
    if (p[V_VELFEEL]) atk += 0.006 * (15 - vq) / 14.0;
    o.attackInc = atk <= 0.0005 ? 1.f : (float)(1.0 / (atk * mr));
    o.env = atk <= 0.0005 ? 1.f : 0.f; o.phase = atk <= 0.0005 ? 1 : 0;
    o.holdLeft = p[V_HOLD] >= 31 ? 0xffffffffu : (u32)(holdSeconds(p[V_HOLD]) * mr);
    o.decayMul = p[V_DECAY] >= 31 ? 1.f : (float)xm::exp(-1.0 / (decaySeconds(p[V_DECAY]) * mr));
    if (o.loop && o.holdLeft == 0xffffffffu) o.holdLeft = (u32)(1.5 * mr);
    int tone = p[V_TONE];
    double fc = tone >= 99 ? 0 : 150.0 * xm::pow(120.0, tone / 98.0);
    o.lpCoef = fc <= 0 ? 1.f : (float)(1.0 - xm::exp(-xm::TAU * fc / mr));
    o.lp = o.lp2 = 0;
  }
  void startFade(Voice& o) { if (!o.fading) { o.fading = 1; o.fadeMul = (float)xm::exp(-1.0 / (0.002 * mr)); } }
  void renderVoices(float* L, float* R, float* E, int n) {
    for (int k = 0; k < MAXV; k++) {
      Voice& o = v[k];
      if (!o.active) continue;
      for (int i = 0; i < n; i++) {
        int ip = (int)o.pos;
        float x = (ip >= 0 && ip < o.len ? o.data[ip] : 0) * (1.0f / 32768.0f);
        if (o.lpCoef < 1) { o.lp += o.lpCoef * (x - o.lp); o.lp2 += o.lpCoef * (o.lp - o.lp2); x = o.lp2; }
        if (o.phase == 0) { o.env += o.attackInc; if (o.env >= 1) { o.env = 1; o.phase = 1; } }
        else if (o.phase == 1) { if (o.holdLeft != 0xffffffffu && o.holdLeft-- == 0) o.phase = 2; }
        else o.env *= o.decayMul;
        if (o.fading) o.env *= o.fadeMul;
        float y = x * o.env;
        L[i] += y * o.gL; R[i] += y * o.gR; E[i] += y * o.gE;
        if (o.sweepMul > 0) { o.sweepNow *= o.sweepMul; if ((i & 7) == 0) o.inc = o.baseInc * xm::pow2(o.sweepNow / 12.0); }
        if (o.rev) {
          o.pos -= o.inc;
          if (o.pos < 0) { if (o.loop) o.pos += o.len - o.loopStart; else { o.active = 0; break; } }
        } else {
          o.pos += o.inc;
          if (o.pos >= o.len) { if (o.loop) o.pos -= (o.len - o.loopStart); else { o.active = 0; break; } }
        }
        if (o.env < 0.00003f && (o.phase == 2 || o.fading)) { o.active = 0; break; }
      }
    }
  }

  // ================= sequencer =================
  Part& part() { return pat[curPat].parts[curPart]; }
  int effTick(int t) {
    int sw = pat[curPat].swing;
    if (sw > 50 && t % 48 == 24) return t - 24 + sw * 48 / 100;
    return t;
  }
  void seekEvents() { Part& p = part(); evIdx = 0; while (evIdx < p.count && effTick(p.ev[evIdx].tick) < tick) evIdx++; }
  void setPart(int pt, double at) {
    curPart = pt; partLenTicks = partTicks(curPat, curPart); tick = at; while (tick >= partLenTicks) tick -= partLenTicks;
    seekEvents(); nSkip = 0;
  }
  void start(bool fromHost = false, double ppq = 0) {
    for (auto& o : v) if (o.active) startFade(o);
    playing = 1; paused = 0; breakPending = 0; queuedPart = -1; crashNext = 0; endingNext = 0;
    playingSong = set[K_MODE] == M_SONG && songs[set[K_SONG]].len > 0 && !recording;
    if (playingSong) {
      Song& sg = songs[set[K_SONG]];
      if (sg.tempo && !fromHost) set[K_TEMPO] = sg.tempo * 10;
      songStepIdx = 0; songRepeat = 0;
      curPat = sg.steps[0].pattern; mainVar = sg.steps[0].part <= P_VARC ? sg.steps[0].part : mainVar;
      setPart(sg.steps[0].part, 0);
      if (fromHost) locateSong(ppq);
    } else {
      setPart(curPart, fromHost ? ppq * PPQ : 0);
    }
    countIn = recording && set[K_COUNTIN] && !fromHost ? barTicks(curPat) : 0;
    if (countIn) tick = -countIn;
  }
  void stop() {
    playing = 0; paused = 0; breakPending = 0; queuedPart = -1;
    if (curPart >= P_FILLA) curPart = curPart == P_INTRO || curPart == P_END ? mainVar : curPart - 3;
    if (queuedPat >= 0) { curPat = queuedPat; queuedPat = -1; }
  }
  void locateSong(double ppq) {
    Song& sg = songs[set[K_SONG]];
    double t = ppq * PPQ;
    for (int i = 0; i < sg.len; i++) {
      int len = partTicks(sg.steps[i].pattern, sg.steps[i].part) * (sg.steps[i].repeat ? sg.steps[i].repeat : 1);
      if (t < len) {
        songStepIdx = i; curPat = sg.steps[i].pattern;
        int one = partTicks(curPat, sg.steps[i].part);
        songRepeat = (int)(t / one);
        setPart(sg.steps[i].part, t - songRepeat * one);
        return;
      }
      t -= len;
    }
    playing = 0;
  }
  // called when the playhead reaches the end of the current part
  void partEnd() {
    const int over = 0;   // the caller puts the playhead past the start
    if (curPart == P_END) { stop(); return; }
    if (playingSong) {
      Song& sg = songs[set[K_SONG]];
      SongStep& ss = sg.steps[songStepIdx];
      if (++songRepeat < (ss.repeat ? ss.repeat : 1)) { setPart(curPart, over); return; }
      songRepeat = 0;
      if (++songStepIdx >= sg.len) { stop(); songStepIdx = 0; return; }
      SongStep& nx = sg.steps[songStepIdx];
      curPat = nx.pattern; if (nx.part <= P_VARC) mainVar = nx.part;
      setPart(nx.part, over);
      return;
    }
    if (breakPending) { breakPending = 0; paused = 1; setPart(mainVar, 0); return; }
    int next = curPart;
    if (curPart == P_INTRO) next = mainVar;
    else if (curPart >= P_FILLA && curPart <= P_FILLC) { next = curPart - 3; mainVar = next; crashNext = 1; }
    if (queuedPat >= 0) { curPat = queuedPat; queuedPat = -1; }
    if (queuedPart >= 0) { next = queuedPart; queuedPart = -1; }
    setPart(next, over);
  }
  void barLine() {
    int bt = barTicks(curPat);
    double barStart = xm::floor(tick / bt) * bt, frac = tick - barStart;
    // ENDING pressed while playing, and queued variation changes, happen on the bar line
    if (endingNext && !playingSong) {
      endingNext = 0; setPart(P_END, 0); tick = frac;
    } else if (queuedPart >= 0 && curPart <= P_VARC && queuedPart <= P_VARC) {
      int pt = queuedPart; queuedPart = -1; mainVar = pt;
      setPart(pt, (double)(((int)barStart) % partTicks(curPat, pt))); tick += frac;
    }
    if (crashNext) { crashNext = 0; trigger(CRASH1, 110, true); }
  }
  void fireEvent(const Event& e) {
    for (int k = 0; k < nSkip; k++) if (skipT[k] == e.tick && skipS[k] == e.sound) { skipT[k] = skipT[--nSkip]; skipS[k] = skipS[nSkip]; return; }
    trigger(e.sound, e.vel, true);
  }
  void click(bool accent) { clickPhase = 0; clickEnv = set[K_CLICKLVL] / 99.0f * 0.5f; clickFreq = accent ? 1600.f : 1100.f; }
  bool clickOn() { return set[K_CLICK] == 2 || (set[K_CLICK] == 1 && recording); }
  void fireUpTo(double t) { Part& p = part(); while (evIdx < p.count && effTick(p.ev[evIdx].tick) < t) { fireEvent(p.ev[evIdx]); evIdx++; } }
  void seqSample() {
    if (!playing || paused) return;
    double dt = set[K_TEMPO] / 10.0 * PPQ / (60.0 * mr);
    double prev = tick;
    tick += dt;
    if (tick < 0) {   // count-in clicks
      if (xm::floor(tick / PPQ) != xm::floor(prev / PPQ) || prev == -countIn) click(((int)xm::floor(tick / PPQ) + countIn / PPQ) % pat[curPat].beats == 0);
      return;
    }
    bool boundary = prev <= 0;
    fireUpTo(tick < partLenTicks ? tick : partLenTicks);
    if (tick >= partLenTicks) {
      double over = tick - partLenTicks;
      partEnd();
      if (!playing || paused) return;
      tick = over;
      boundary = true;
    }
    if (boundary || xm::floor(tick / PPQ) != xm::floor(prev / PPQ)) {
      int beat = (int)xm::floor(tick / PPQ);
      if ((beat * PPQ) % barTicks(curPat) == 0) barLine();
      if (clickOn()) click(beat % pat[curPat].beats == 0);
    }
    fireUpTo(tick);
  }

  // the order notes were recorded in, for the 8-notes-a-step rule (only for the part being recorded)
  u32 recOrder[MAXEV], recCounter = 0; int recPat = -1, recPartIdx = -1, recSeen = -1;
  void removeEventTracked(Part& p, int i) { for (int k = i; k < p.count - 1; k++) recOrder[k] = recOrder[k + 1]; removeEvent(p, i); }
  // record a hit at the playhead
  void recordHit(int s, int vel) {
    if (!playing || !recording || tick < 0 || paused) return;
    if (!ensureUser()) return;
    Part& p = part();
    int q = TC_TICKS[set[K_TC]];
    int t = (int)(q <= 1 ? tick : xm::floor(tick / q + 0.5) * q);
    if (t >= partLenTicks) t -= partLenTicks;
    for (int i = 0; i < p.count; i++) if (p.ev[i].tick == t && p.ev[i].sound == s) { p.ev[i].vel = (u8)vel; return; }
    // 8 notes at most on one step: a ninth erases the earliest one recorded there (as the review describes)
    if (recPat != curPat || recPartIdx != curPart || recSeen != p.count) { for (int i = 0; i < p.count; i++) recOrder[i] = 0; recPat = curPat; recPartIdx = curPart; }
    { int same = 0, first = -1; u32 oldest = 0xffffffffu;
      for (int i = 0; i < p.count; i++) if (p.ev[i].tick == t) { same++; if (recOrder[i] < oldest) { oldest = recOrder[i]; first = i; } }
      if (same >= 8 && first >= 0) { removeEventTracked(p, first); if (first < evIdx) evIdx--; } }
    int idx = insertEvent(p, t, s, vel);
    if (idx < 0) { message("PART FULL", ""); return; }
    for (int i = p.count - 1; i > idx; i--) recOrder[i] = recOrder[i - 1];
    recOrder[idx] = ++recCounter;
    recSeen = p.count;
    if (idx < evIdx) evIdx++;
    else if (nSkip < 16) { skipT[nSkip] = t; skipS[nSkip] = s; nSkip++; }   // ahead of the playhead: don't play it twice
  }

  // ================= pads, MIDI =================
  int padVelocity(int touch) {
    if (set[K_ACCENT]) return set[K_ACCENTVEL];
    return set[K_PADVEL] && touch > 0 ? touch : 64;   // the XR10's pads aren't dynamic: a fixed mid velocity
  }
  void hitSound(int s, int vel) {
    if (held[B_ERASE] && playing && recording) {
      if (ensureUser()) { Part& p = part(); for (int i = p.count - 1; i >= 0; i--) if (p.ev[i].sound == s) { removeEvent(p, i); if (i < evIdx) evIdx--; } }
      eraseUsed = 1;
      char l[17]; Str(l, 17).s("ERASE ").s(SOUND_NAMES[s]);
      message(l, PART_NAMES[curPart]);
      return;
    }
    trigger(s, vel);
    recordHit(s, vel);
    set[K_SOUND] = s;
  }
  void pad(int i, int down, int touchVel = 0) {
    if (i < 0 || i >= NPADS) return;
    held[B_PAD1 + i] = down;
    if (!down) return;
    lastPad = i;
    padHitCount[i]++;
    int mode = set[K_MODE];
    // pads 10-15 pick variation / fill while not recording in pattern and song modes
    if (i >= 9 && !recording && (mode == M_PATTERN || mode == M_SONG)) {
      static const int map[6] = {P_VARC, P_VARB, P_VARA, P_FILLC, P_FILLB, P_FILLA};
      selectPart(map[i - 9]);
      return;
    }
    if (mode == M_SOUND) { menuPage = menuPage < 0 ? 0 : menuPage; }
    hitSound(pads[set[K_BANK]][i], padVelocity(touchVel));
  }
  void selectPart(int pt) {
    if (pt <= P_VARC) {
      if (playing && !paused && curPart <= P_VARC) queuedPart = pt;
      else if (playing && !paused) { queuedPart = pt; mainVar = pt; }
      else { mainVar = pt; curPart = pt; if (paused) setPart(pt, 0); }
    } else if (pt <= P_FILLC) {
      if (playing && !paused) {
        // the fill starts now, at this point in the bar, and leads into the matching variation
        double inBar = tick - xm::floor(tick / barTicks(curPat)) * barTicks(curPat);
        mainVar = pt - 3; setPart(pt, inBar);
      } else { mainVar = pt - 3; curPart = mainVar; message(PART_NAMES[pt], "NEXT START"); queuedPart = -1; fillOnStart = pt; }
    }
  }
  int fillOnStart = -1;
  int midiToSound(int note) {
    if (set[K_MIDIMAP] == 1) { int s = note - 36; return s >= 0 && s < NSOUNDS ? s : -1; }
    if (set[K_MIDIMAP] == 2) { int p = note - 36; return p >= 0 && p < NPADS ? pads[set[K_BANK]][p] : -1; }
    switch (note) {
      case 35: return KICK1 + 1; case 36: return KICK1; case 37: return RIM2; case 38: return SNARE1; case 39: return CLAPS;
      case 40: return SNARE1 + 1; case 41: return TOM1L; case 42: return HATCL; case 43: return TOM1L; case 44: return HATMD;
      case 45: return TOM1M; case 46: return HATOP; case 47: return TOM1M; case 48: return TOM1H; case 49: return CRASH1;
      case 50: return TOM1H; case 51: return RIDE; case 52: return CHOKE; case 53: return RIDE; case 54: return TAMB;
      case 55: return CRASH2; case 56: return COWBELL; case 57: return CRASH2; case 58: return VIBRA; case 59: return RIDE;
      case 60: return CONGA2; case 61: return CONGA3; case 62: return CONGA1; case 63: return CONGA2; case 64: return CONGA3;
      case 65: return TIMBH; case 66: return TIMBL; case 67: return AGOGOH; case 68: return AGOGOL; case 69: return CABASA;
      case 70: return CABASA; case 71: return WHISTLE; case 72: return WHISTLE; case 73: return GUIRO2; case 74: return GUIRO1;
      case 75: return CLAVES; case 76: return CLAVES; case 77: return CLAVES; case 78: return SQKH; case 79: return SQKL;
      case 80: return TRIH; case 81: return TRIL; case 82: return CABASA; case 83: return TAMB; case 84: return FINGER;
      case 85: return RIM1; case 86: return CONGA4; case 87: return CONGA4;
    }
    if (note >= 88 && note < 96) return SNARE1 + 2 + (note - 88);
    if (note >= 96 && note < 105) return TOM2L + (note - 96);   // toms 2-4 and the orchestra hit have no GM note of their own
    if (note == 105) return HITS;
    if (note >= 24 && note < 34) return KICK1 + (note - 24);
    return -1;
  }
  void midi(int status, int d1, int d2) {
    int ch = status & 15, type = status & 0xf0;
    if (set[K_MIDICH] > 0 && ch != set[K_MIDICH] - 1) return;
    if (type == 0x90 && d2 > 0) { int s = midiToSound(d1); if (s >= 0) hitSound(s, d2); }
    else if (type == 0xc0) { if (d1 < NPAT) selectPattern(d1); }
    else if (status == 0xfa) { if (!playing) start(); }
    else if (status == 0xfc) { if (playing) stop(); }
  }
  void selectPattern(int p) {
    if (p < 0 || p >= NPAT) return;
    if (playing && !paused) { queuedPat = p; return; }
    curPat = p; queuedPat = -1;
    if (!hostPlaying) set[K_TEMPO] = pat[p].tempo * 10;
    if (curPart >= P_FILLA) curPart = mainVar;
    partLenTicks = partTicks(curPat, curPart);
  }

  // ================= host transport (plugin) =================
  void host(int isPlaying, double ppq, double bpm) {
    if (set[K_SYNC] != 1) { if (hostPlaying && !isPlaying) hostPlaying = 0; return; }
    if (bpm > 20) set[K_TEMPO] = (s32)(bpm * 10 + 0.5);
    if (isPlaying && !hostPlaying) { hostPlaying = 1; start(true, ppq < 0 ? 0 : ppq); hostPpqLast = ppq; return; }
    if (!isPlaying && hostPlaying) { hostPlaying = 0; if (playing) stop(); return; }
    if (isPlaying && playing && !playingSong && !paused && curPart <= P_VARC) {
      // re-lock to the host if it jumped (loop, locate)
      double want = ppq * PPQ; while (want < 0) want += partLenTicks;
      double tw = want - xm::floor(want / partLenTicks) * partLenTicks;
      double diff = tw - tick; if (diff < 0) diff = -diff;
      if (diff > 24 && diff < partLenTicks - 24) setPart(curPart, tw);
    }
    hostPpqLast = ppq;
  }

  // ================= audio =================
  // the machine side, at the machine rate: sequencer, voices, the digital mixer (8 voices summed with one bit of
  // headroom and saturated to 16 bits, an estimate) and the metronome, into the three DAC channels
  void machineChunk() {
    const int n = 16;
    for (int i = 0; i < n; i++) mixL[i] = mixR[i] = mixE[i] = 0;
    for (int i = 0; i < n; i++) seqSample();
    renderVoices(mixL, mixR, mixE, n);
    for (int i = 0; i < n; i++) {
      float c = 0;
      if (clickEnv > 0.0001f) { clickPhase += clickFreq / mr; c = (float)(xm::sin(xm::TAU * clickPhase) * clickEnv); clickEnv *= 0.9977f; }
      mixL[i] = dac16((mixL[i] + c) * 0.5f); mixR[i] = dac16((mixR[i] + c) * 0.5f); mixE[i] = dac16(mixE[i] * 0.5f);
    }
    mixPos = 0; mixLen = n;
  }
  static float dac16(float x) {
    float q = xm::floor(x * 32768.f + 0.5f);
    if (q > 32767.f) q = 32767.f; if (q < -32768.f) q = -32768.f;
    return q * (1.f / 32768.f);
  }
  float extL[4096], extR[4096];
  void process(float* L, float* R, float* auxL, float* auxR, int n) {
    while (n > 0) {
      int c = n > 1024 ? 1024 : n;
      block(L, R, auxL, auxR, c);
      L += c; R += c; if (auxL) { auxL += c; auxR += c; }
      n -= c;
    }
  }
  // the analog side: the DAC holds each value until the next one (zero-order hold); the output stage runs at os x the
  // host rate and is brought down to the host rate. Output: 1.0 = 3.16 V (+12 dBu) at the jack.
  void block(float* L, float* R, float* auxL, float* auxR, int n) {
    int ft = set[K_FXTYPE]; float ftm = set[K_FXTIME] / 99.f;
    if (ft <= 4 && (ft != fxConfigured || ftm != fxTimeConfigured)) { if (ft != fxConfigured) fx.clear(); fx.setup(sr, ft, ftm); fxConfigured = ft; fxTimeConfigured = ftm; }
    outL.setKnob(set[K_VOLUME] / 100.0); outR.setKnob(set[K_VOLUME] / 100.0); outE.setKnob(set[K_EFFECTVR] / 100.0);
    const double step = mr / (sr * os), toHost = 1.0 / 3.16;
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < os; k++) {
        machPhase += step;
        if (machPhase >= 1) {
          machPhase -= 1;
          if (mixPos >= mixLen) machineChunk();
          dacL = mixL[mixPos] * dacVolts; dacR = mixR[mixPos] * dacVolts; dacE = mixE[mixPos] * dacVolts; mixPos++;
        }
        decL.push(outL.run(dacL)); decR.push(outR.run(dacR)); decE.push(outE.run(dacE));
      }
      float l = (float)(decL.out() * toHost), r = (float)(decR.out() * toHost), e = (float)(decE.out() * toHost);
      if (ft <= 4) {   // the internal effect is an extra (the XR10 sends to an outboard unit): fed from the EFFECT jack
        float fl, fr; fx.run(e, e, fl, fr);
        float lv = set[K_FXLEVEL] / 99.f * 1.4f; l += fl * lv; r += fr * lv;
      }
      L[i] = l; R[i] = r;
      if (auxL) { auxL[i] = e; auxR[i] = e; }
    }
    samplesRun += n;
    if (msgTimer > 0) msgTimer -= n;
  }

  // ================= user samples =================
  // loads mono float audio at rate srcRate into sound s (resampled to 44.1 kHz, 16-bit, max 10 s)
  // loads mono float audio at srcRate into sound s: band-limited resampling to the machine rate, 16-bit, max 10 s
  static double resampleAt(const float* d, int len, double t, double cut) {   // windowed sinc around position t
    const int R = 16;
    int c = (int)xm::floor(t);
    double acc = 0;
    for (int k = c - R + 1; k <= c + R; k++) {
      if (k < 0 || k >= len) continue;
      double x = t - k, w = 0.5 + 0.5 * xm::cos(xm::PI * x / R);
      double si = x == 0 ? 1 : xm::sin(xm::PI * cut * x) / (xm::PI * cut * x);
      acc += d[k] * cut * si * w;
    }
    return acc;
  }
  bool loadSample(int s, const float* data, int len, double srcRate) {
    if (s < 0 || s >= NSOUNDS || len <= 0) return false;
    double ratio = srcRate / ROM_RATE;
    int outLen = (int)(len / ratio); if (outLen > (int)(ROM_RATE * 10)) outLen = (int)(ROM_RATE * 10);
    if (userUsed + outLen > (u32)USER_POOL) compactUser(s);
    if (userUsed + outLen > (u32)USER_POOL) { message("SAMPLE MEMORY", "FULL"); return false; }
    double cut = ratio > 1 ? 0.96 / ratio : 1.0;
    double peak = 0;
    for (int i = 0; i < outLen; i++) { double y = resampleAt(data, len, i * ratio, cut); double a = xm::fabs(y); if (a > peak) peak = a; }
    double g = peak > 0 ? 0.98 / peak : 1;
    for (int i = 0; i < outLen; i++) {
      double y = resampleAt(data, len, i * ratio, cut) * g * 32767;
      userPool[userUsed + i] = (s16)(y > 32767 ? 32767 : (y < -32768 ? -32768 : (y < 0 ? y - 0.5 : y + 0.5)));
    }
    for (auto& o : v) if (o.active && o.sound == s) o.active = 0;
    user[s].start = userUsed; user[s].len = outLen; userUsed += outLen;
    message("SAMPLE LOADED", SOUND_NAMES[s]);
    return true;
  }
  void clearSample(int s) { for (auto& o : v) if (o.active && o.sound == s) o.active = 0; user[s].len = 0; compactUser(-1); }
  void compactUser(int except) {
    for (auto& o : v) if (o.active && user[o.sound].len) o.active = 0;
    u32 at = 0;
    for (int pass = 0; pass < NSOUNDS; pass++) {   // in order of start
      int best = -1; u32 bs = 0xffffffff;
      for (int s = 0; s < NSOUNDS; s++) if (s != except && user[s].len && user[s].start >= at && user[s].start < bs) { bs = user[s].start; best = s; }
      if (best < 0) break;
      for (u32 k = 0; k < user[best].len; k++) userPool[at + k] = userPool[user[best].start + k];
      user[best].start = at; at += user[best].len;
    }
    if (except >= 0) user[except].len = 0;
    userUsed = at;
  }

  // ================= state =================
  struct Writer { u8* p; u32 n, cap; void put(const void* d, u32 len) { if (p && n + len <= cap) bytecopy(p + n, d, len); n += len; } };
  struct Reader { const u8* p; u32 n, len; bool ok = true; void get(void* d, u32 l) { if (n + l > len) { ok = false; bytezero(d, l); return; } bytecopy(d, p + n, l); n += l; } };
  static const u32 MAGIC = 0x30315258;   // "XR10"
  template <class W> void save(W& w) {
    u32 hdr[2] = {MAGIC, 2};   // 2: XR10 voice parameters, samples at the machine rate
    w.put(hdr, 8);
    w.put(set, sizeof set);
    w.put(vp, sizeof vp);
    w.put(pads, sizeof pads);
    w.put(songs, sizeof songs);
    s32 cp[3] = {curPat, curPart, mainVar}; w.put(cp, sizeof cp);
    for (int p = NPRESET; p < NPAT; p++) {
      Pattern& P = pat[p];
      w.put(P.name, 10); w.put(&P.beats, 4);
      for (int k = 0; k < NPARTS; k++) { w.put(&P.parts[k], 4); w.put(P.parts[k].ev, P.parts[k].count * sizeof(Event)); }
    }
    w.put(user, sizeof user); w.put(&userUsed, 4);
    w.put(userPool, userUsed * 2);
  }
  bool load(const u8* data, u32 len) {
    Reader r{data, 0, len};
    u32 hdr[2]; r.get(hdr, 8);
    if (!r.ok || hdr[0] != MAGIC) return false;
    stop(); for (auto& o : v) o.active = 0;
    if (hdr[1] < 2) {   // older files: different voice parameters and sample rate, keep the patterns and songs
      s32 oldSet[32]; r.get(oldSet, sizeof oldSet); for (int k = 0; k < 32; k++) set[k] = oldSet[k];
      set[K_EFFECTVR] = 70; set[K_FXTYPE] = 5;
      u8 skip[64 * 13 * 2]; r.get(skip, sizeof skip);
      for (int i = 0; i < NSOUNDS; i++) defaultVoice(i);
    } else { r.get(set, sizeof set); r.get(vp, sizeof vp); }
    r.get(pads, sizeof pads); r.get(songs, sizeof songs);
    s32 cp[3]; r.get(cp, sizeof cp);
    for (int p = NPRESET; p < NPAT; p++) {
      Pattern& P = pat[p];
      r.get(P.name, 10); P.name[9] = 0; r.get(&P.beats, 4);
      for (int k = 0; k < NPARTS; k++) { r.get(&P.parts[k], 4); if (P.parts[k].count > MAXEV) P.parts[k].count = 0; r.get(P.parts[k].ev, P.parts[k].count * sizeof(Event)); }
    }
    r.get(user, sizeof user); r.get(&userUsed, 4);
    if (userUsed > (u32)USER_POOL) userUsed = 0;
    r.get(userPool, userUsed * 2);
    if (hdr[1] < 2) { for (int s = 0; s < NSOUNDS; s++) user[s].len = 0; userUsed = 0; }   // 44.1 kHz samples: load them again
    for (int s = 0; s < NSOUNDS; s++) if (user[s].start + user[s].len > userUsed) user[s].len = 0;
    for (int k = 0; k < NKEYS; k++) if (set[k] < -100000 || set[k] > 100000) set[k] = 0;
    curPat = xm::clampi(cp[0], 0, NPAT - 1); mainVar = xm::clampi(cp[2], 0, 2); curPart = mainVar;
    set[K_PLAYING] = 0; set[K_REC] = 0; recording = 0;
    partLenTicks = partTicks(curPat, curPart);
    return true;
  }

  // ================= keys (shared by the LCD menus and the panels) =================
  s32 get(int k) {
    switch (k) {
      case K_PATTERN: return curPat;
      case K_PART: return curPart;
      case K_MAINVAR: return mainVar;
      case K_REC: return recording;
      case K_PLAYING: return playing;
      case K_LEN: return pat[curPat].parts[curPart].bars;
      case K_BEATS: return pat[curPat].beats;
      case K_SWING: return pat[curPat].swing;
      case K_SUB: return pat[curPat].sub;
      case K_USERSAMPLE: return user[set[K_SOUND]].len > 0;
      case K_BAR: return playing && tick >= 0 ? (int)(tick / barTicks(curPat)) : 0;
    }
    return k >= 0 && k < NKEYS ? set[k] : 0;
  }
  void put(int k, s32 val) {
    switch (k) {
      case K_TEMPO: set[k] = xm::clampi(val, 400, 2960); return;   // 40-296, as the XR10
      case K_PATTERN: selectPattern(val); return;
      case K_PART: selectPart(xm::clampi(val, 0, NPARTS - 1)); return;
      case K_REC: recording = val ? 1 : 0; if (recording) ensureUser(); return;
      case K_PLAYING: if (val && !playing) start(); else if (!val && playing) stop(); return;
      case K_LEN: if (ensureUser()) { Part& p = part(); int nb = xm::clampi(val, 1, 4); p.bars = (u8)nb; int lim = nb * barTicks(curPat); while (p.count && p.ev[p.count - 1].tick >= lim) p.count--; partLenTicks = partTicks(curPat, curPart); if (tick >= partLenTicks) setPart(curPart, 0); } return;
      case K_BEATS: if (ensureUser()) { Pattern& P = pat[curPat]; P.beats = (u8)xm::clampi(val, 2, 8); for (int i = 0; i < NPARTS; i++) { Part& p = P.parts[i]; int lim = p.bars * P.beats * PPQ; while (p.count && p.ev[p.count - 1].tick >= lim) p.count--; } partLenTicks = partTicks(curPat, curPart); if (tick >= partLenTicks) setPart(curPart, 0); } return;
      case K_SWING: if (ensureUser()) pat[curPat].swing = (u8)xm::clampi(val, 50, 75); return;
      case K_SUB: if (ensureUser()) pat[curPat].sub = (u8)(val == 3 ? 3 : 4); return;
      case K_TC: set[k] = xm::clampi(val, 0, 6); return;
      case K_BANK: set[k] = xm::clampi(val, 0, NBANK - 1); return;
      case K_FXTYPE: set[k] = xm::clampi(val, 0, 6); return;
      case K_POLY: set[k] = xm::clampi(val, 1, 32); return;
      case K_MIDICH: set[k] = xm::clampi(val, 0, 16); return;
      case K_MIDIMAP: set[k] = xm::clampi(val, 0, 2); return;
      case K_SONG: set[k] = xm::clampi(val, 0, NSONG - 1); set[K_SONGSTEP] = 0; return;
      case K_SOUND: set[k] = xm::clampi(val, 0, NSOUNDS - 1); return;
      case K_MODE: setMode(xm::clampi(val, 0, 3)); return;
      case K_VOLUME: set[k] = xm::clampi(val, 0, 100); return;
      case K_EFFECTVR: set[k] = xm::clampi(val, 0, 100); return;
      case K_CLICK: set[k] = xm::clampi(val, 0, 2); return;
      case K_SYNC: set[k] = xm::clampi(val, 0, 1); return;
    }
    if (k >= 0 && k < NKEYS) set[k] = xm::clampi(val, 0, 127);
  }
  void setParam(int s, int p, int val) { if (s >= 0 && s < NSOUNDS && p >= 0 && p < NVP) vp[s][p] = (s16)xm::clampi(val, VPARAMS[p].lo, VPARAMS[p].hi); }
  // step grid for the RX5 panel: one row per sound used in the part, then the bank's pads, 16 rows at most
  static const int GRID_ROWS = 16;
  u8 gRow[GRID_ROWS]; s8 gPad[GRID_ROWS];
  int gridRows() {
    Part& p = part();
    int n = 0;
    bool used[NSOUNDS];
    for (int s = 0; s < NSOUNDS; s++) used[s] = false;
    for (int i = 0; i < p.count; i++) used[p.ev[i].sound] = true;
    // sounds in the part, in pad order first, then by number
    for (int k = 0; k < NPADS && n < GRID_ROWS; k++) { int s = pads[set[K_BANK]][k]; if (used[s]) { bool dup = false; for (int j = 0; j < n; j++) dup |= gRow[j] == s; if (!dup) { gRow[n] = (u8)s; gPad[n++] = (s8)k; } } }
    for (int s = 0; s < NSOUNDS && n < GRID_ROWS; s++) if (used[s]) { bool dup = false; for (int j = 0; j < n; j++) dup |= gRow[j] == s; if (!dup) { gRow[n] = (u8)s; gPad[n++] = -1; } }
    for (int k = 0; k < NPADS && n < GRID_ROWS; k++) { int s = pads[set[K_BANK]][k]; bool dup = false; for (int j = 0; j < n; j++) dup |= gRow[j] == s; if (!dup) { gRow[n] = (u8)s; gPad[n++] = (s8)k; } }
    return n;
  }
  int gridSteps() { return pat[curPat].beats * pat[curPat].sub; }
  // out: 16 rows x 32 steps (0 off, 1 ghost, 2 normal, 3 accent), then 16 row sounds
  void grid(int bar, u8* out) {
    int nr = gridRows();
    Part& p = part();
    int steps = gridSteps(), tps = PPQ / pat[curPat].sub, base = bar * barTicks(curPat);
    for (int i = 0; i < GRID_ROWS * 32 + GRID_ROWS; i++) out[i] = 0;
    for (int r = 0; r < GRID_ROWS; r++) out[GRID_ROWS * 32 + r] = r < nr ? gRow[r] : 255;
    for (int i = 0; i < p.count; i++) {
      int t = p.ev[i].tick - base;
      if (t < -tps / 2 || t >= steps * tps) continue;
      int s = (t + tps / 2) / tps; if (s >= steps) continue;
      for (int r = 0; r < nr; r++) if (gRow[r] == p.ev[i].sound) { int vel = p.ev[i].vel; out[r * 32 + s] = (u8)(vel >= 120 ? 3 : (vel >= 80 ? 2 : 1)); }
    }
  }
  void gridToggle(int row, int bar, int step) {
    int nr = gridRows();
    if (row < 0 || row >= nr) return;
    int s = gRow[row];
    if (!ensureUser()) return;
    Part& p = part();
    int tps = PPQ / pat[curPat].sub, t = bar * barTicks(curPat) + step * tps;
    if (t >= partLenTicks || t < 0) return;
    for (int i = 0; i < p.count; i++) {
      int d = p.ev[i].tick - t;
      if (p.ev[i].sound == s && d > -tps / 2 && d <= tps / 2) {
        if (p.ev[i].vel < 120) { p.ev[i].vel = 127; trigger(s, 127); }
        else { removeEvent(p, i); if (i < evIdx) evIdx--; }
        return;
      }
    }
    int idx = insertEvent(p, t, s, 100);
    if (idx >= 0 && idx < evIdx) evIdx++;
    if (!playing) trigger(s, 100);
  }
  // change a row's sound: moves the part's notes to the new sound (and the pad, when the row is a pad)
  void gridAssign(int row, int ns) {
    int nr = gridRows();
    if (row < 0 || row >= nr || ns < 0 || ns >= NSOUNDS) return;
    int s = gRow[row], pd = gPad[row];
    Part& p0 = part();
    bool has = false; for (int i = 0; i < p0.count; i++) has |= p0.ev[i].sound == s;
    if (has && ensureUser()) { Part& p = part(); for (int i = 0; i < p.count; i++) if (p.ev[i].sound == s) p.ev[i].sound = (u8)ns; }
    if (pd >= 0) pads[set[K_BANK]][pd] = (u8)ns;
    trigger(ns, 100);
  }
  // songs
  void songSet(int song, int step, int pattern, int prt, int rep) {
    Song& sg = songs[xm::clampi(song, 0, NSONG - 1)];
    if (step >= sg.len) { if (sg.len >= MAXSTEP) return; step = sg.len++; }
    sg.steps[step].pattern = (u8)xm::clampi(pattern, 0, NPAT - 1); sg.steps[step].part = (u8)xm::clampi(prt, 0, NPARTS - 1); sg.steps[step].repeat = (u8)xm::clampi(rep, 1, 99);
  }
  void songInsert(int song, int step) {
    Song& sg = songs[xm::clampi(song, 0, NSONG - 1)];
    if (sg.len >= MAXSTEP) return;
    if (sg.len == 0) { sg.len = 1; sg.steps[0] = {(u8)curPat, (u8)mainVar, 1, 0}; return; }
    step = xm::clampi(step, 0, sg.len - 1);
    for (int i = sg.len; i > step + 1; i--) sg.steps[i] = sg.steps[i - 1];
    sg.steps[step + 1] = sg.steps[step]; sg.len++;
  }
  void songTempo(int song, int bpm) { songs[xm::clampi(song, 0, NSONG - 1)].tempo = (u16)(bpm >= 30 ? xm::clampi(bpm, 30, 300) : 0); }
  void clearPart() { if (ensureUser()) { part().count = 0; evIdx = 0; message("PART CLEARED", PART_NAMES[curPart]); } }
  void songDelete(int song, int step) {
    Song& sg = songs[xm::clampi(song, 0, NSONG - 1)];
    if (step < 0 || step >= sg.len) return;
    for (int i = step; i < sg.len - 1; i++) sg.steps[i] = sg.steps[i + 1];
    sg.len--;
  }

  // ================= MIDI files =================
  // the note a sound is written as (the inverse of the note map in use)
  int soundToNote(int s) {
    if (set[K_MIDIMAP] == 1) return 36 + s;
    if (set[K_MIDIMAP] == 2) { for (int p = 0; p < NPADS; p++) if (pads[set[K_BANK]][p] == s) return 36 + p; }
    int saved = set[K_MIDIMAP]; set[K_MIDIMAP] = 0;
    int found = -1;
    for (int n = 35; n <= 87 && found < 0; n++) if (midiToSound(n) == s) found = n;
    for (int n = 0; n < 128 && found < 0; n++) if (midiToSound(n) == s) found = n;
    set[K_MIDIMAP] = saved;
    return found < 0 ? 38 : found;
  }
  struct Bytes {
    u8* p; u32 n, cap;
    void b(u32 v) { if (n < cap) p[n] = (u8)v; n++; }
    void be32(u32 v) { b(v >> 24); b(v >> 16); b(v >> 8); b(v); }
    void vlq(u32 v) { u8 t[5]; int k = 0; t[k++] = v & 0x7f; while (v >>= 7) t[k++] = (u8)(0x80 | (v & 0x7f)); while (k) b(t[--k]); }
  };
  // a Standard MIDI File (type 0, 96 PPQ, drums on channel 10) of the current part (song = 0) or the whole song (song = 1).
  // Returns the length (which may be more than cap: call again with a bigger buffer).
  u32 exportMidi(u8* out, u32 cap, int wholeSong) {
    Bytes w{out, 0, cap};
    w.be32(0x4d546864); w.be32(6); w.b(0); w.b(0); w.b(0); w.b(1); w.b(0); w.b(PPQ);
    w.be32(0x4d54726b); u32 lenAt = w.n; w.be32(0);
    u32 last = 0;
    auto delta = [&](u32 t) { w.vlq(t > last ? t - last : 0); if (t > last) last = t; };
    Song& sg = songs[set[K_SONG]];
    int firstPat = wholeSong && sg.len ? sg.steps[0].pattern : curPat;
    int bpm = wholeSong && sg.tempo ? sg.tempo : set[K_TEMPO] / 10;
    u32 uspq = (u32)(60000000.0 / (bpm > 0 ? bpm : 120));
    delta(0); w.b(0xff); w.b(0x51); w.b(3); w.b(uspq >> 16); w.b(uspq >> 8); w.b(uspq);
    delta(0); w.b(0xff); w.b(0x58); w.b(4); w.b(pat[firstPat].beats); w.b(2); w.b(24); w.b(8);
    // note offs follow each hit by a 32nd note; both streams are in time order, so merge them
    static const int OFFLEN = 12;
    u32 offT[256]; u8 offN[256]; int oh = 0, ot = 0;
    auto flushTo = [&](u32 t) { while (oh != ot && offT[oh] <= t) { delta(offT[oh]); w.b(0x89); w.b(offN[oh]); w.b(0); oh = (oh + 1) & 255; } };
    u32 base = 0;
    auto emitPart = [&](int p, int prt) {
      Part& pt = pat[p].parts[prt];
      int sw = pat[p].swing;
      for (int i = 0; i < pt.count; i++) {
        int t0 = pt.ev[i].tick;
        if (sw > 50 && t0 % 48 == 24) t0 = t0 - 24 + sw * 48 / 100;
        u32 t = base + t0;
        flushTo(t);
        int note = soundToNote(pt.ev[i].sound);
        delta(t); w.b(0x99); w.b(note); w.b(pt.ev[i].vel ? pt.ev[i].vel : 1);
        if (((ot + 1) & 255) == oh) { delta(t); w.b(0x89); w.b(offN[oh]); w.b(0); oh = (oh + 1) & 255; }
        offT[ot] = t + OFFLEN; offN[ot] = (u8)note; ot = (ot + 1) & 255;
      }
      base += pt.bars * pat[p].beats * PPQ;
    };
    if (wholeSong && sg.len) { for (int i = 0; i < sg.len; i++) for (int r = 0; r < (sg.steps[i].repeat ? sg.steps[i].repeat : 1); r++) emitPart(sg.steps[i].pattern, sg.steps[i].part); }
    else emitPart(curPat, curPart);
    flushTo(0xffffffff);
    delta(base); w.b(0xff); w.b(0x2f); w.b(0);
    u32 trackLen = w.n - lenAt - 4;
    if (lenAt + 4 <= cap) { out[lenAt] = (u8)(trackLen >> 24); out[lenAt + 1] = (u8)(trackLen >> 16); out[lenAt + 2] = (u8)(trackLen >> 8); out[lenAt + 3] = (u8)trackLen; }
    return w.n;
  }
  // reads a Standard MIDI File (type 0 or 1) into the current part, copying a preset to a user pattern first.
  // Returns the bars read, 0 when nothing could be used, -1 when it isn't a MIDI file.
  int importMidi(const u8* d, u32 len) {
    auto be = [&](u32 at, int nb) { u32 v = 0; for (int i = 0; i < nb; i++) v = (v << 8) | (at + i < len ? d[at + i] : 0); return v; };
    if (len < 14 || be(0, 4) != 0x4d546864) return -1;
    int ntrk = (int)be(10, 2), div = (int)be(12, 2);
    if (div <= 0 || (div & 0x8000)) return -1;
    // pass 1 counts channel-10 notes (if there are any, only they are used); pass 2 reads
    int tempoBpm = 0, num = 0, den = 0, usedBars = 0;
    u32 maxTick = 0;
    bool any10 = false;
    Part tmp; tmp.count = 0;
    int beatsWanted = 4;
    for (int pass = 0; pass < 3; pass++) {
      u32 at = 14;
      for (int t = 0; t < ntrk && at + 8 <= len; t++) {
        u32 id = be(at, 4), clen = be(at + 4, 4); at += 8;
        u32 end = at + clen > len ? len : at + clen;
        if (id != 0x4d54726b) { at = end; t--; continue; }
        u32 p = at, tick = 0; int run = 0;
        while (p < end) {
          u32 dv = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = d[p++]; dv = (dv << 7) | (c & 0x7f); if (!(c & 0x80)) break; }
          tick += dv;
          if (p >= end) break;
          int st = d[p];
          if (st == 0xff) {
            int ty = p + 1 < end ? d[p + 1] : 0; p += 2;
            u32 ml = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = d[p++]; ml = (ml << 7) | (c & 0x7f); if (!(c & 0x80)) break; }
            if (pass == 0 && ty == 0x51 && ml >= 3 && !tempoBpm) tempoBpm = (int)(60000000.0 / (double)be(p, 3) + 0.5);
            if (pass == 0 && ty == 0x58 && ml >= 2 && !num) { num = d[p]; den = 1 << d[p + 1]; }
            p += ml; continue;
          }
          if (st == 0xf0 || st == 0xf7) { p++; u32 ml = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = d[p++]; ml = (ml << 7) | (c & 0x7f); if (!(c & 0x80)) break; } p += ml; continue; }
          if (st & 0x80) { run = st; p++; } else if (!run) break;
          int type = run & 0xf0, ch = run & 15;
          int nd = (type == 0xc0 || type == 0xd0) ? 1 : 2;
          int a = p < end ? d[p] : 0, b = nd > 1 && p + 1 < end ? d[p + 1] : 0;
          p += nd;
          if (type == 0x90 && b > 0) {
            if (pass == 0) { if (ch == 9) any10 = true; }
            else if (!any10 || ch == 9) {
              u32 t96 = (u32)((double)tick * PPQ / div + 0.5);
              if (pass == 1) { if (t96 > maxTick) maxTick = t96; }
              else {
                int s = midiToSound(a);
                if (s < 0) s = midiToSoundGM(a);
                int lim = usedBars * beatsWanted * PPQ;
                if (s >= 0 && (int)t96 < lim) insertEvent(tmp, (int)t96, s, b);
              }
            }
          }
        }
        at = end;
      }
      if (pass == 1) {
        if (num && den) { beatsWanted = num * 4 / den; if (num * 4 % den) beatsWanted = num; }
        beatsWanted = xm::clampi(beatsWanted, 2, 8);
        int bt = beatsWanted * PPQ;
        usedBars = xm::clampi((int)((maxTick + bt - PPQ / 8) / bt), 1, 4);   // the XR10's patterns are up to 4 bars
      }
    }
    if (!tmp.count) return 0;
    if (!ensureUser()) return 0;
    Pattern& P = pat[curPat];
    if (P.beats != beatsWanted) { P.beats = (u8)beatsWanted; for (int i = 0; i < NPARTS; i++) { Part& q = P.parts[i]; int lim = q.bars * P.beats * PPQ; while (q.count && q.ev[q.count - 1].tick >= lim) q.count--; } }
    tmp.bars = (u8)usedBars;
    bytecopy(&P.parts[curPart], &tmp, 4 + tmp.count * sizeof(Event));
    if (tempoBpm >= 30 && tempoBpm <= 300) { P.tempo = (u8)(tempoBpm > 255 ? 255 : tempoBpm); if (!hostPlaying) set[K_TEMPO] = tempoBpm * 10; }
    partLenTicks = partTicks(curPat, curPart);
    if (playing) setPart(curPart, 0);
    char l[17]; Str(l, 17).s("MIDI ").i(usedBars).s(usedBars > 1 ? " BARS" : " BAR").s(maxTick >= (u32)(4 * beatsWanted * PPQ) ? " (CUT)" : "");
    message(l, PART_NAMES[curPart]);
    return usedBars;
  }
  int midiToSoundGM(int note) { int m = set[K_MIDIMAP]; set[K_MIDIMAP] = 0; int s = midiToSound(note); set[K_MIDIMAP] = m; return s; }

  // ================= sample names =================
  // which of the 64 sounds a file name means ("07 KICK7.wav", "Hat CL.wav", "tom2 m.aif", "23.wav"); -1 if none
  static int matchSoundName(const char* name) {
    char n[64]; int k = 0;
    for (int i = 0; name[i] && k < 63; i++) {
      char c = name[i];
      if (c == '.' && k > 0) { bool ext = true; for (int j = i + 1; name[j]; j++) if (name[j] == '.') ext = false; if (ext) break; }
      if (c >= 'a' && c <= 'z') c = (char)(c - 32);
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) n[k++] = c;
    }
    n[k] = 0;
    static const char* const alias[][2] = {
      {"TRIANGLEL", "TRIANGL"}, {"TRIANGLEH", "TRIANGH"}, {"TAMBOURINE", "TAMBRINE"}, {"TAMB", "TAMBRINE"}, {"VIBRASLAP", "VIBRASLP"},
      {"TIMBALESL", "TIMBALL"}, {"TIMBALESH", "TIMBALH"}, {"TIMBALEL", "TIMBALL"}, {"TIMBALEH", "TIMBALH"}, {"HATCLOSED", "HATCL"},
      {"CLOSEDHAT", "HATCL"}, {"HATOPEN", "HATOP"}, {"OPENHAT", "HATOP"}, {"HATMID", "HATMD"}, {"HATPEDAL", "HATMD"}, {"RIMSHOT", "RIMSHOT1"},
      {"CLAP", "CLAPS"}, {"FINGERSNAP", "FINGER"}, {"SNAP", "FINGER"}, {"COWBEL", "COWBELL"}, {"CLAVE", "CLAVES"}};
    char sn[16];
    int best = -1, bestLen = 0;
    // length of nd if it's in hay (KICK1 doesn't count inside KICK10: a name ending in a digit can't be followed by one)
    auto contains = [&](const char* hay, const char* nd) {
      int l = 0; while (nd[l]) l++;
      bool endsDigit = l && nd[l - 1] >= '0' && nd[l - 1] <= '9';
      for (int i = 0; hay[i]; i++) {
        int j = 0; while (j < l && hay[i + j] == nd[j]) j++;
        if (j == l && !(endsDigit && hay[i + l] >= '0' && hay[i + l] <= '9')) return l;
      }
      return 0;
    };
    for (int s = 0; s < NSOUNDS; s++) {
      int m = 0; const char* src = SOUND_NAMES[s];
      for (int i = 0; src[i] && m < 15; i++) if (src[i] != ' ') sn[m++] = src[i];
      sn[m] = 0;
      int l = contains(n, sn);
      if (l > bestLen) { bestLen = l; best = s; }
    }
    for (auto& a : alias) {
      int l = contains(n, a[0]);
      if (l > bestLen) { for (int s = 0; s < NSOUNDS; s++) { int m = 0; const char* src = SOUND_NAMES[s]; for (int i = 0; src[i] && m < 15; i++) if (src[i] != ' ') sn[m++] = src[i]; sn[m] = 0; bool eq = true; for (int i = 0; eq && (sn[i] || a[1][i]); i++) eq = sn[i] == a[1][i]; if (eq) { best = s; bestLen = l; } } }
    }
    if (best < 0 && n[0] >= '0' && n[0] <= '9') {   // just a number
      int v = 0, i = 0; while (n[i] >= '0' && n[i] <= '9' && i < 3) v = v * 10 + (n[i++] - '0');
      if (v >= 1 && v <= NSOUNDS) best = v - 1;
    }
    return best;
  }

  // ================= front panel =================
  void message(const char* a, const char* b) { Str(msg[0], 17).s(a); Str(msg[1], 17).s(b); msgTimer = (int)(sr * 1.4); }
  void setMode(int m) {
    set[K_MODE] = m; menuPage = 0; editMode = 0; cursor = 0; typed = -1; typedDigits = 0; confirm = 0; msgTimer = 0;
  }
  // menu pages per mode
  int pageCount() {
    switch (set[K_MODE]) {
      case M_PATTERN: return editMode ? 10 : 1;
      case M_SONG: return 3;
      case M_SOUND: return NVP + 2;   // pad assign, 13 params, sample
      default: return 13;
    }
  }
  void button(int b, int down) {
    if (b < 0 || b >= NBUTTONS) return;
    if (b >= B_PAD1 && b <= B_PAD15) { pad(b - B_PAD1, down); return; }
    held[b] = down;
    if (b == B_ERASE && !down) {
      if (!eraseUsed && !playing && set[K_MODE] == M_PATTERN) { confirm = 1; message("", ""); msgTimer = 0; }
      if (!eraseUsed && set[K_MODE] == M_SONG) { songDelete(set[K_SONG], set[K_SONGSTEP]); if (set[K_SONGSTEP] > 0 && set[K_SONGSTEP] >= songs[set[K_SONG]].len) set[K_SONGSTEP]--; }
      eraseUsed = 0;
      return;
    }
    if (!down) return;
    int mode = set[K_MODE];
    if (b >= B_MODE_PATTERN && b <= B_MODE_UTILITY) { setMode(b - B_MODE_PATTERN); return; }
    switch (b) {
      case B_MODE: setMode((mode + 1) % 4); return;
      case B_START:
        if (playing) { stop(); return; }
        if (fillOnStart >= 0) { start(); setPart(fillOnStart, 0); fillOnStart = -1; return; }
        start(); return;
      case B_INTRO:
        if (!playing) { start(); if (!playingSong) setPart(P_INTRO, 0); }
        else if (!playingSong) { queuedPart = -1; endingNext = 1; message("ENDING", "AT BAR END"); }
        return;
      case B_BREAK:
        if (!playing) return;
        if (paused) { paused = 0; setPart(mainVar, 0); return; }
        breakPending = 1; message("BREAK", "AT BAR END"); return;
      case B_ACCENT: set[K_ACCENT] = !set[K_ACCENT]; message(set[K_ACCENT] ? "ACCENT ON" : "ACCENT OFF", ""); return;
      case B_PADBANK: set[K_BANK] = (set[K_BANK] + 1) % NBANK; { char l[17]; Str(l, 17).s("PAD BANK ").i(set[K_BANK] + 1); message(l, ""); } return;
      case B_TC: set[K_TC] = (set[K_TC] + 1) % 7; { char l[17]; Str(l, 17).s("TIMING CORRECT"); char m[17]; Str(m, 17).s(TC_NAMES[set[K_TC]]); message(l, m); } return;
      case B_ERASE: eraseUsed = 0; if (confirm) { confirm = 0; } return;
    }
    if (confirm && mode == M_PATTERN) {   // clear the part?
      if (b == B_PLUS || b == B_ENTER) { clearPart(); confirm = 0; return; }
      if (b == B_MINUS) { confirm = 0; return; }
    }
    if (b == B_EDIT) {
      if (mode == M_PATTERN) { editMode = !editMode; menuPage = 0; }
      else if (mode == M_SONG) { menuPage = (menuPage + 1) % 3; }
      else if (mode == M_SOUND) { menuPage = (menuPage + 1) % pageCount(); }
      else menuPage = (menuPage + 1) % pageCount();
      return;
    }
    if (b == B_UP || b == B_DOWN) {
      int pc = pageCount();
      if (mode == M_PATTERN && !editMode) { put(K_TEMPO, set[K_TEMPO] + (b == B_UP ? 10 : -10)); return; }
      menuPage = (menuPage + (b == B_UP ? pc - 1 : 1)) % pc; cursor = 0; return;
    }
    if (b == B_LEFT || b == B_RIGHT) {
      int d = b == B_RIGHT ? 1 : -1;
      if (mode == M_SONG && menuPage == 0) { cursor = (cursor + 4 + d) % 4; return; }
      if (mode == M_PATTERN && editMode && menuPage == 8) { cursor = xm::clampi(cursor + d, 0, 7); return; }
      if (mode == M_SOUND) { put(K_SOUND, (set[K_SOUND] + NSOUNDS + d) % NSOUNDS); return; }
      if (mode == M_PATTERN && !editMode) { int np = (curPat + NPAT + d) % NPAT; selectPattern(np); return; }
      int pc = pageCount(); menuPage = (menuPage + pc + d) % pc; return;
    }
    if (b >= B_D0 && b <= B_D9) { digit(b - B_D0); return; }
    if (b == B_MINUS || b == B_PLUS) { adjust(b == B_PLUS ? 1 : -1, true); return; }
    if (b == B_ENTER) { enter(); return; }
  }
  int endingNext = 0;
  void digit(int d) {
    if (typed < 0 || typedDigits >= 2) { typed = 0; typedDigits = 0; }
    typed = typed * 10 + d; typedDigits++;
    int mode = set[K_MODE];
    if (typedDigits == 2 && !(mode == M_SOUND && menuPage > 0) && !(mode == M_SONG && menuPage == 1) && !(mode == M_UTILITY)) enter();
  }
  void enter() {
    int mode = set[K_MODE];
    if (typed >= 0) {
      int val = typed; typed = -1; typedDigits = 0;
      if (mode == M_PATTERN && !editMode) { if (val >= 1 && val <= NPAT) selectPattern(val - 1); return; }
      if (mode == M_PATTERN && editMode && menuPage == 6) { copyPartTarget = xm::clampi(val - 1, NPRESET, NPAT - 1); return; }
      if (mode == M_PATTERN && editMode && menuPage == 7) { copyPatTarget = xm::clampi(val - 1, NPRESET, NPAT - 1); return; }
      if (mode == M_SOUND && menuPage == 0) { if (val >= 1 && val <= NSOUNDS) { set[K_SOUND] = val - 1; pads[set[K_BANK]][lastPad] = (u8)(val - 1); trigger(val - 1, 100); } return; }
      if (mode == M_SOUND) { if (val >= 1 && val <= NSOUNDS) { set[K_SOUND] = val - 1; trigger(val - 1, 100); } return; }
      if (mode == M_SONG && menuPage == 0) {
        Song& sg = songs[set[K_SONG]]; int st = set[K_SONGSTEP];
        if (cursor == 0) { set[K_SONGSTEP] = xm::clampi(val - 1, 0, sg.len > 0 ? sg.len - 1 : 0); return; }
        if (sg.len == 0) songInsert(set[K_SONG], 0);
        SongStep& ss = sg.steps[st];
        if (cursor == 1 && val >= 1 && val <= NPAT) ss.pattern = (u8)(val - 1);
        if (cursor == 3 && val >= 1) ss.repeat = (u8)val;
        return;
      }
      if (mode == M_SONG && menuPage == 1) { songs[set[K_SONG]].tempo = (u16)(val >= 30 ? val : 0); return; }
      if (mode == M_SONG && menuPage == 2) { if (val >= 1 && val <= NSONG) put(K_SONG, val - 1); return; }
      return;
    }
    if (mode == M_SONG && menuPage == 0) { songInsert(set[K_SONG], set[K_SONGSTEP]); if (songs[set[K_SONG]].len > 1) set[K_SONGSTEP]++; return; }
    if (mode == M_PATTERN && editMode) {
      if (menuPage == 6 && copyPartTarget >= NPRESET) {   // copy part
        Part& src = part(); int dPart = copyPartDest;
        bytecopy(&pat[copyPartTarget].parts[dPart], &src, sizeof(Part));
        char l[17]; Str(l, 17).s("COPIED TO ").i(copyPartTarget + 1, 2); message(l, PART_NAMES[dPart]);
      }
      if (menuPage == 7 && copyPatTarget >= NPRESET) {
        char nm[10]; copyName(nm, pat[copyPatTarget].name, 10);
        bytecopy(&pat[copyPatTarget], &pat[curPat], sizeof(Pattern));
        char l[17]; Str(l, 17).s("COPIED TO ").i(copyPatTarget + 1, 2); message(l, pat[curPat].name);
      }
      if (menuPage == 9) { if (curPat >= NPRESET) { clearPattern(curPat); message("PATTERN CLEARED", ""); } else message("PRESET PATTERN", "CAN'T CLEAR"); }
      if (menuPage == 0) { put(K_REC, !recording); }
    }
    if (mode == M_UTILITY && menuPage == 12) { if (utilConfirm) { factoryReset(); message("ALL INITIALIZED", ""); utilConfirm = 0; } else utilConfirm = 1; }
    if (mode == M_SOUND && menuPage == NVP + 1) { /* load is done by the app */ }
  }
  int copyPartTarget = NPRESET, copyPartDest = 0, copyPatTarget = NPRESET, utilConfirm = 0;
  // the DATA knob, -/N and +/Y
  void knob(int delta) { adjust(delta, false); }
  void adjust(int d, bool fromButton) {
    int mode = set[K_MODE];
    if (mode == M_PATTERN && !editMode) {
      if (fromButton) { selectPattern((curPat + NPAT + d) % NPAT); return; }
      put(K_TEMPO, set[K_TEMPO] + d * 10); return;
    }
    if (mode == M_PATTERN) {
      switch (menuPage) {
        case 0: put(K_REC, d > 0); return;
        case 1: put(K_LEN, part().bars + d); return;
        case 2: put(K_BEATS, pat[curPat].beats + d); return;
        case 3: put(K_SWING, pat[curPat].swing + d); return;
        case 4: put(K_CLICK, set[K_CLICK] + d); return;
        case 5: put(K_COUNTIN, d > 0); return;
        case 6: copyPartTarget = xm::clampi(copyPartTarget + d, NPRESET, NPAT - 1); return;
        case 7: copyPatTarget = xm::clampi(copyPatTarget + d, NPRESET, NPAT - 1); return;
        case 8: if (ensureUser()) { char& ch = pat[curPat].name[cursor]; int c = ch ? ch : ' '; c = charStep(c, d); ch = (char)c; for (int i = 0; i < cursor; i++) if (!pat[curPat].name[i]) pat[curPat].name[i] = ' '; pat[curPat].name[9] = 0; } return;
        case 9: return;
      }
    }
    if (mode == M_SONG) {
      Song& sg = songs[set[K_SONG]];
      if (menuPage == 0) {
        int st = set[K_SONGSTEP];
        if (cursor == 0) { set[K_SONGSTEP] = xm::clampi(st + d, 0, sg.len > 0 ? sg.len - 1 : 0); return; }
        if (sg.len == 0) songInsert(set[K_SONG], 0);
        SongStep& ss = sg.steps[st];
        if (cursor == 1) ss.pattern = (u8)((ss.pattern + NPAT + d) % NPAT);
        if (cursor == 2) ss.part = (u8)((ss.part + NPARTS + d) % NPARTS);
        if (cursor == 3) ss.repeat = (u8)xm::clampi(ss.repeat + d, 1, 99);
        return;
      }
      if (menuPage == 1) { int t = sg.tempo + d; sg.tempo = (u16)(t < 30 ? (d > 0 ? 30 : 0) : (t > 300 ? 300 : t)); return; }
      if (menuPage == 2) { put(K_SONG, set[K_SONG] + d); return; }
    }
    if (mode == M_SOUND) {
      int s = set[K_SOUND];
      if (menuPage == 0) { int ns = (pads[set[K_BANK]][lastPad] + NSOUNDS + d) % NSOUNDS; pads[set[K_BANK]][lastPad] = (u8)ns; set[K_SOUND] = ns; if (fromButton) trigger(ns, 100); return; }
      if (menuPage <= NVP) { int p = menuPage - 1; setParam(s, p, vp[s][p] + d); return; }
      if (menuPage == NVP + 1 && d < 0 && user[s].len) { clearSample(s); message("ROM SOUND", SOUND_NAMES[s]); }
      return;
    }
    // utility
    switch (menuPage) {
      case 0: put(K_TC, set[K_TC] + d); return;
      case 1: put(K_ACCENTVEL, xm::clampi(set[K_ACCENTVEL] + d, 1, 127)); return;
      case 2: put(K_PADVEL, d > 0); return;
      case 3: put(K_CLICKLVL, xm::clampi(set[K_CLICKLVL] + d, 0, 99)); return;
      case 4: put(K_FXTYPE, set[K_FXTYPE] + d); return;
      case 5: put(K_FXTIME, xm::clampi(set[K_FXTIME] + d, 0, 99)); return;
      case 6: put(K_FXLEVEL, xm::clampi(set[K_FXLEVEL] + d, 0, 99)); return;
      case 7: put(K_POLY, set[K_POLY] + d); return;
      case 8: put(K_MIDICH, set[K_MIDICH] + d); return;
      case 9: put(K_MIDIMAP, set[K_MIDIMAP] + d); return;
      case 10: put(K_SYNC, set[K_SYNC] + d); return;
      case 11: put(K_BANK, set[K_BANK] + d); return;
      case 12: if (d > 0) { if (utilConfirm) { factoryReset(); message("ALL INITIALIZED", ""); utilConfirm = 0; } else utilConfirm = 1; } else utilConfirm = 0; return;
    }
  }
  static int charStep(int c, int d) {
    static const char set_[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+/#&.!";
    int n = sizeof(set_) - 1, i = 0;
    for (; i < n; i++) if (set_[i] == c) break;
    if (i >= n) i = 0;
    return set_[(i + n + d) % n];
  }

  // ================= LCD =================
  void lcd() {
    char* a = st.lcd[0]; char* b = st.lcd[1];
    Str l1(a, 17), l2(b, 17);
    int mode = set[K_MODE];
    bool blink = (samplesRun / (u32)(sr * 0.3)) % 2;
    if (msgTimer > 0) { l1.s(msg[0]); l2.s(msg[1]); }
    else if (confirm && mode == M_PATTERN) { l1.s("ERASE ").s(PART_NAMES[curPart]); l2.s("SURE? -/N  +/Y"); }
    else if (mode == M_PATTERN && !editMode) {
      int shownPat = typed >= 0 ? typed : curPat + 1;
      l1.i(set[K_TEMPO] / 10, 3).c(':');
      if (typed >= 0) { l1.i(typed, typedDigits == 1 ? 1 : 2).s(blink ? "_" : " "); while (l1.n < 7) l1.c(' '); }
      else l1.i(shownPat, 2);
      {
        // the name squeezed into what's left of the line: spaces dropped, a trailing number kept
        int room = 16 - 8 - (PART_CODES[curPart][1] ? 2 : 1);
        char nm[12]; int k = 0; const char* src = pat[curPat].name; int len = 0; while (src[len]) len++;
        bool tight = len > room;
        for (int i = 0; i < len && k < 11; i++) if (!(tight && src[i] == ' ')) nm[k++] = src[i];
        nm[k] = 0;
        if (k > room) { char last = nm[k - 1]; if (last >= '0' && last <= '9') { nm[room - 1] = last; } nm[room] = 0; }
        l1.c(':').s(nm).padTo(16 - (PART_CODES[curPart][1] ? 3 : 2)).c(':').s(PART_CODES[curPart]);
      }
      l2.s("BANK").i(set[K_BANK] + 1, 2).c(':');
      if (queuedPat >= 0) l2.s("NEXT ").i(queuedPat + 1, 2);
      else if (recording) l2.s(playing ? (blink ? "REC  " : "     ") : "REC RDY");
      else if (paused) l2.s("BREAK");
      else if (playing) { int bt = barTicks(curPat); int bar = tick < 0 ? 0 : (int)(tick / bt) + 1, beat = tick < 0 ? (int)((tick + countIn) / PPQ) + 1 : (int)((tick - (bar - 1) * bt) / PPQ) + 1; l2.s("BAR").i(bar, 2).c(' ').i(beat); }
      else l2.s("NEXT--+--");
      if (set[K_ACCENT]) { l2.padTo(15).c('>'); }
    } else if (mode == M_PATTERN) {
      l1.s("P").i(curPat + 1, 2).c(':').s(PART_NAMES[curPart]).padTo(16);
      switch (menuPage) {
        case 0: l2.s("RECORD    ").s(recording ? "ON" : "OFF"); break;
        case 1: l2.s("LENGTH  ").i(part().bars).s(part().bars > 1 ? " BARS" : " BAR"); break;
        case 2: l2.s("TIME SIG    ").i(pat[curPat].beats).s("/4"); break;
        case 3: l2.s("SWING      ").i(pat[curPat].swing).c('%'); break;
        case 4: l2.s("CLICK  ").s(set[K_CLICK] == 0 ? "OFF" : (set[K_CLICK] == 1 ? "REC ONLY" : "ALWAYS")); break;
        case 5: l2.s("COUNT IN   ").s(set[K_COUNTIN] ? "ON" : "OFF"); break;
        case 6: l2.s("CPY PART>P").i(copyPartTarget + 1, 2).c(' ').s(PART_CODES[curPart]); copyPartDest = curPart; break;
        case 7: l2.s("CPY PATT>P").i(copyPatTarget + 1, 2); break;
        case 8: { l2.s("NAME:"); for (int i = 0; i < 9; i++) { char ch = pat[curPat].name[i]; if (!ch) { for (; i < 9; i++) l2.c(i == cursor && blink ? '_' : ' '); break; } l2.c(i == cursor && blink ? '_' : ch); } break; }
        case 9: l2.s("CLEAR PATT ENTER"); break;
      }
    } else if (mode == M_SONG) {
      Song& sg = songs[set[K_SONG]];
      l1.s("SONG").i(set[K_SONG] + 1, 2).c(':').s(sg.name);
      if (playing && playingSong) { l1.padTo(13); l1.i(songStepIdx + 1, 2); }
      if (menuPage == 0) {
        if (sg.len == 0) l2.s("STEP01 EMPTY  +");
        else {
          SongStep& ss = sg.steps[xm::clampi(set[K_SONGSTEP], 0, sg.len - 1)];
          bool b0 = cursor == 0 && blink, b1 = cursor == 1 && blink, b2 = cursor == 2 && blink, b3 = cursor == 3 && blink;
          if (b0) l2.s("  "); else l2.i(set[K_SONGSTEP] + 1, 2);
          l2.s(":P"); if (b1) l2.s("  "); else l2.i(ss.pattern + 1, 2);
          l2.c(' '); if (b2) l2.s("  "); else l2.s(PART_CODES[ss.part]);
          l2.padTo(10).c('x'); if (b3) l2.s("  "); else l2.i(ss.repeat, 2);
          l2.padTo(14).i(sg.len, 2);
        }
      } else if (menuPage == 1) { l2.s("SONG TEMPO "); if (sg.tempo) l2.i(sg.tempo); else l2.s("PATT"); }
      else l2.s("SELECT SONG  ").i(set[K_SONG] + 1, 2);
    } else if (mode == M_SOUND) {
      int s = set[K_SOUND];
      l1.s("SND").i(s + 1, 2).c(':').s(SOUND_NAMES[s]);
      if (typed >= 0) { l1.padTo(14).i(typed, 2); }
      if (menuPage == 0) l2.s("PAD").i(lastPad + 1, 2).s(" B").i(set[K_BANK] + 1).s("=").s(SOUND_NAMES[pads[set[K_BANK]][lastPad]]);
      else if (menuPage <= NVP) {
        int p = menuPage - 1, val = vp[s][p];
        l2.s(VPARAMS[p].name);
        char tmp[8]; Str t(tmp, 8);
        if (p == V_REVERSE || p == V_LOOP || p == V_VELFEEL) t.s(val ? "ON" : "OFF");
        else if (p == V_SWPOL) t.s(val ? "DOWN" : "UP");
        else if (p == V_PAN) { if (val == 0) t.s("CENTER"); else if (val < 0) t.s("L").i(-val); else t.s("R").i(val); }
        else if (p == V_TUNE || p == V_FINE) t.sgn(val);
        else if (p == V_GROUP) { if (val) t.i(val); else t.s("OFF"); }
        else if (p == V_TONE && val >= 99) t.s("OPEN");
        else t.i(val);
        l2.right(tmp, 16);
      } else l2.s("SAMPLE ").s(user[s].len ? "USER -/N" : "ROM");
    } else {
      l1.s("UTILITY ").i(menuPage + 1, 2);
      switch (menuPage) {
        case 0: l2.s("TIMING CR ").s(TC_NAMES[set[K_TC]]); break;
        case 1: l2.s("ACCENT VEL  ").i(set[K_ACCENTVEL]); break;
        case 2: l2.s("PAD VELOCITY ").s(set[K_PADVEL] ? "ON" : "OFF"); break;
        case 3: l2.s("CLICK LEVEL  ").i(set[K_CLICKLVL]); break;
        case 4: l2.s("FX TYPE  ").s(FX_NAMES[set[K_FXTYPE]]); break;
        case 5: l2.s("FX TIME      ").i(set[K_FXTIME]); break;
        case 6: l2.s("FX LEVEL     ").i(set[K_FXLEVEL]); break;
        case 7: l2.s("VOICES       ").i(set[K_POLY]); break;
        case 8: l2.s("MIDI CH    "); if (set[K_MIDICH]) l2.i(set[K_MIDICH]); else l2.s("OMNI"); break;
        case 9: l2.s("MIDI MAP ").s(set[K_MIDIMAP] == 0 ? "GM" : (set[K_MIDIMAP] == 1 ? "36=SND1" : "36=PAD1")); break;
        case 10: l2.s("SYNC     ").s(set[K_SYNC] ? "HOST/EXT" : "INTERNAL"); break;
        case 11: l2.s("PAD BANK     ").i(set[K_BANK] + 1); break;
        case 12: l2.s(utilConfirm ? "INIT ALL? +/Y" : "INIT ALL    +/Y"); break;
      }
    }
    l1.padTo(16); l2.padTo(16);
    a[16] = 0; b[16] = 0;
  }
  Status& status() {
    lcd();
    st.mode = set[K_MODE]; st.playing = playing; st.recording = recording; st.accent = set[K_ACCENT]; st.bank = set[K_BANK];
    st.pattern = curPat; st.part = curPart; st.tempo10 = set[K_TEMPO]; st.sound = set[K_SOUND];
    st.bar = get(K_BAR); st.beat = playing && tick >= 0 ? (int)(tick / PPQ) % pat[curPat].beats : 0;
    for (int i = 0; i < NPADS; i++) { st.padHit[i] = padHitCount[i]; st.padSound[i] = pads[set[K_BANK]][i]; }
    st.erase = held[B_ERASE]; st.editing = editMode; st.fxType = set[K_FXTYPE]; st.tc = set[K_TC]; st.song = set[K_SONG];
    st.songStep = playingSong ? songStepIdx : set[K_SONGSTEP]; st.cursor = cursor; st.version = 1; st.beats = pat[curPat].beats;
    st.sub = pat[curPat].sub; st.bars = part().bars;
    return st;
  }
};
}  // namespace xr
