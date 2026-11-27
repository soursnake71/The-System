// Omega XR10: the 50 preset rhythms named on the XR10's panel. Each has VARIATION A/B/C, FILL IN A/B/C,
// an INTRO and an ENDING, generated from a one-bar groove per style (original patterns, not transcriptions).
#pragma once
#include "sounds.h"

namespace xr {
struct StyleDef {
  const char* name;
  u8 beats, sub, swing, tempo;
  u8 kick, snare, tomset, latin;   // kick 0-9, snare 0-9 (KICK1.., SNARE1..), toms 0-3
  const char* lines;               // "K:x...x... S:....x..." ('X' accent, 'x' normal, 'o' ghost)
};
// instrument letters used in the grooves
inline int styleSound(char c, const StyleDef& s) {
  switch (c) {
    case 'K': return KICK1 + s.kick;
    case 'S': return SNARE1 + s.snare;
    case 'P': return CLAPS;   case 'H': return HATCL;   case 'M': return HATMD;   case 'O': return HATOP;
    case 'R': return RIDE;    case 'C': return CRASH1;  case 'r': return RIM1;    case 'k': return RIM2;
    case 'B': return COWBELL; case 's': return CONGA1;  case 'h': return CONGA2;  case 'c': return CONGA3;
    case 't': return CONGA4;  case 'V': return CLAVES;  case 'T': return TIMBH;   case 'L': return TIMBL;
    case 'A': return AGOGOH;  case 'a': return AGOGOL;  case 'G': return GUIRO1;  case 'g': return GUIRO2;
    case 'Z': return CABASA;  case 'N': return TAMB;    case 'W': return WHISTLE; case 'F': return FINGER;
    case 'I': return TRIH;    case 'Y': return HITS;    case 'Q': return SQKH;    case 'q': return SQKL;
    case 'v': return VIBRA;
    case '1': return TOM1L + s.tomset * 3; case '2': return TOM1M + s.tomset * 3; case '3': return TOM1H + s.tomset * 3;
  }
  return -1;
}
static const StyleDef STYLES[50] = {
  {"ROCK 1", 4, 4, 50, 120, 0, 0, 0, 0, "K:x.......x.x..... S:....x.......x... H:x.x.x.x.x.x.x.x."},
  {"ROCK 2", 4, 4, 50, 112, 1, 1, 0, 0, "K:x......xx.x..... S:....x.......x... H:xoxoxoxoxoxoxoxo"},
  {"ROCK 3", 4, 4, 50, 132, 2, 2, 1, 0, "K:x.x.....x.x..... S:....x.......x... H:x.x.x.x.x.x.x.O."},
  {"ROCK 4", 4, 4, 50, 104, 0, 4, 0, 0, "K:x.....x.x.....x. S:....x.......x... R:x.x.x.x.x.x.x.x."},
  {"ROCK 5", 4, 4, 50, 140, 3, 3, 1, 0, "K:x...x...x...x... S:....X.......X... H:xxxxxxxxxxxxxxxx"},
  {"DANCE 1", 4, 4, 50, 124, 6, 6, 3, 0, "K:x...x...x...x... S:....x.......x... P:....x.......x... O:..x...x...x...x. H:x...x...x...x..."},
  {"DANCE 2", 4, 4, 50, 120, 5, 5, 3, 0, "K:x...x...x...x... P:....x.......x... M:..x...x...x...x. H:x.x.x.x.x.x.x.x."},
  {"DANCE 3", 4, 4, 50, 118, 6, 7, 2, 0, "K:x...x...x..xx... S:....x.......x... H:xxxxxxxxxxxxxxxx N:....x.......x..."},
  {"DANCE 4", 4, 4, 52, 126, 6, 6, 3, 0, "K:x...x...x...x... P:....x.......x... O:..x...x...x...x. Z:oooooooooooooooo"},
  {"DANCE 5", 4, 4, 50, 132, 9, 5, 2, 0, "K:x...x...x...x... S:....x.......x... H:xxxxxxxxxxxxxxxx Y:X..............."},
  {"FUNK 1", 4, 4, 54, 100, 1, 1, 1, 0, "K:x.x.......x..x.. S:....x..o.o..x..o H:xxxxxxxxxxxxxxxx"},
  {"FUNK 2", 4, 4, 56, 98, 3, 3, 1, 0, "K:x......x..x..... S:....x.o.....x.o. H:x.x.x.x.x.x.x.O."},
  {"FUNK 3", 4, 4, 52, 104, 0, 4, 0, 0, "K:x..x..x...x..x.. S:....x.......x... H:xxxxxxxxxxxxxxxx B:x...x...x...x..."},
  {"FUNK 4", 4, 4, 58, 92, 1, 1, 1, 0, "K:x.x...x...xx.... S:.o..x..o.o..x... H:x.xxx.xxx.xxx.xx"},
  {"FUNK 5", 4, 4, 54, 108, 3, 2, 0, 0, "K:x.....x...x..x.. S:....x.......x.o. H:xxxxxxxxxxxxxx.x O:..............x."},
  {"ELECTRO 1", 4, 4, 50, 110, 5, 5, 3, 0, "K:x.....x...x..... S:....x.......x... P:....x.......x... H:x.x.x.x.x.x.x.x."},
  {"ELECTRO 2", 4, 4, 50, 112, 5, 5, 3, 0, "K:x..x..x...x..... S:....x.......x... H:xxxxxxxxxxxxxxxx B:..x.....x.x....."},
  {"ELECTRO 3", 4, 4, 50, 118, 9, 6, 2, 0, "K:x.........x.x... S:....x.......x... P:....x..x....x... H:x.x.x.x.x.x.x.x."},
  {"ELECTRO 4", 4, 4, 52, 104, 5, 8, 2, 0, "K:x..x.x....x..x.. S:....x.......x... H:xxxxxxxxxxxxxxxx"},
  {"ELECTRO 5", 4, 4, 50, 115, 7, 5, 3, 0, "K:x.....xx..x..... S:....x.......x... H:x.x.x.x.x.x.x.x. r:...x..x...x..x.."},
  {"DISCO 1", 4, 4, 50, 118, 0, 0, 0, 0, "K:x...x...x...x... S:....x.......x... O:..x...x...x...x. H:x...x...x...x..."},
  {"DISCO 2", 4, 4, 50, 120, 4, 1, 0, 0, "K:x...x...x...x... S:....x.......x... H:xx.xxx.xxx.xxx.x O:..x...x...x...x. N:x.x.x.x.x.x.x.x."},
  {"DISCO 3", 4, 4, 50, 124, 6, 6, 3, 0, "K:x...x...x...x... P:....x.......x... O:..x...x...x...x. B:x.x.x.x.x.x.x.x."},
  {"DISCO 4", 4, 4, 50, 116, 1, 3, 1, 0, "K:x...x...x...x... S:....x..o....x... H:xxxxxxxxxxxxxxxx h:..x.x...x..x.x.. c:......x.......x."},
  {"DISCO 5", 4, 4, 50, 128, 0, 0, 0, 0, "K:x...x...x...x... S:....x.......x... O:..X...X...X...X. Z:xoxoxoxoxoxoxoxo"},
  {"BEATBOX 1", 4, 4, 54, 92, 5, 5, 3, 0, "K:x.....x.x....... S:....x.......x... H:x.x.x.x.x.x.x.x."},
  {"BEATBOX 2", 4, 4, 56, 96, 8, 8, 3, 0, "K:x..x......x.x... S:....x.......x... P:....x.......x... H:xxxxxxxxxxxxxxxx"},
  {"SHUFFLE 1", 4, 3, 50, 112, 0, 0, 0, 0, "K:x.....x..... S:...x.....x.. H:x.xx.xx.xx.x"},
  {"SHUFFLE 2", 4, 3, 50, 96, 2, 2, 0, 0, "K:x..x..x..x.. S:...x.....x.. R:x.xx.xx.xx.x"},
  {"SHUFFLE 3", 4, 3, 50, 126, 1, 1, 1, 0, "K:x.....x.x... S:...x..o..x.o H:x.xx.xx.xx.x"},
  {"SAMBA 1", 4, 4, 50, 100, 2, 3, 0, 1, "K:x..xx..xx..xx..x Z:xoooxoooxoooxooo r:x..x..x...x..x.. c:......x.......x. A:x.x.x..x.x.x.x.."},
  {"SAMBA 2", 4, 4, 50, 108, 2, 3, 0, 1, "K:x..xx..xx..xx..x N:x.xxx.xxx.xxx.xx a:x.x...x.x.x...x. h:..x...x...x...x."},
  {"MAMBO", 4, 4, 50, 112, 0, 3, 0, 1, "K:x..x....x..x.... V:x..x..x...x.x... B:x.x.x.x.x.x.x.x. h:..x...x...x...x. s:....x.......x... c:......x.......x."},
  {"SALSA", 4, 4, 50, 104, 0, 3, 0, 1, "K:...x.......x.... V:x..x..x...x.x... B:x...x...x.x.x.x. s:....x.......x... h:..x.......x..... c:......xx......xx G:x.xxx.xxx.xxx.xx"},
  {"CHACHA 1", 4, 4, 50, 120, 0, 3, 0, 1, "K:x.......x....... B:x.x.x.x.x.x.x.x. G:x...x.x.x...x.x. c:......x.x.....xx h:....x.......x..."},
  {"CHACHA 2", 4, 4, 50, 124, 0, 3, 0, 1, "K:x.......x....... T:x...x...x.x.x... L:............x.x. G:x...x.x.x...x.x. c:......x.x.....xx"},
  {"RHUMBA", 4, 4, 50, 100, 0, 3, 0, 1, "K:x.....x.x.....x. V:x..x...x..x.x... c:...x.......x.... h:......x.x....xx. Z:x.x.x.x.x.x.x.x."},
  {"BEGUINE", 4, 4, 50, 112, 0, 3, 0, 1, "K:x.....x.x....... k:x...x.x.....x... h:..x.x.......x.x. c:x.....x.x.....x. Z:x.x.x.x.x.x.x.x."},
  {"BOSSA 1", 4, 4, 50, 128, 2, 3, 0, 1, "K:x..xx..xx..xx..x k:x..x..x...x..x.. H:x.x.x.x.x.x.x.x."},
  {"BOSSA 2", 4, 4, 50, 136, 2, 3, 0, 1, "K:x..xx..xx..xx..x k:..x..x..x..x..x. R:x.x.x.x.x.x.x.x. Z:xoxoxoxoxoxoxoxo"},
  {"WALTZ", 3, 4, 50, 140, 0, 3, 0, 0, "K:x........... S:....x...x... H:x...x...x..."},
  {"COUNTRY", 4, 4, 50, 120, 1, 3, 0, 0, "K:x.......x....... S:o.oxo.o.o.oxo.o. H:x.x.x.x.x.x.x.x."},
  {"MARCH", 4, 4, 50, 116, 0, 4, 0, 0, "K:x...x...x...x... S:X..ox.o.X..ox.ox C:x..............."},
  {"TWIST", 4, 4, 50, 152, 1, 1, 0, 0, "K:x.....x.x.....x. S:....x.......x... H:x.x.x.x.x.x.x.x."},
  {"SWING", 4, 3, 50, 140, 2, 3, 0, 0, "K:o.....o..... S:........o... R:x..x.xx..x.x M:...x.....x.."},
  {"SLOWROCK", 4, 3, 50, 70, 0, 9, 0, 0, "K:x.....x.x... S:...X.....X.. H:xxxxxxxxxxxx"},
  {"BALLAD", 4, 4, 50, 76, 0, 9, 0, 0, "K:x.......x.x..... k:....x.......x... H:x.x.x.x.x.x.x.x."},
  {"REGGAE", 4, 4, 58, 76, 1, 4, 1, 0, "K:........x....... k:........x....... H:..x...x...x...x."},
  {"JAZZ", 4, 3, 50, 120, 2, 3, 0, 0, "S:o..o..o.ox.. R:x..x.xx..x.x M:...x.....x.."},
  {"TRIBAL", 4, 4, 50, 110, 2, 3, 0, 0, "K:x..x..x.x..x..x. 1:x.....x...x..... 2:...x.....x..x... Z:xoxoxoxoxoxoxoxo N:....x.......x..."},
};

enum { P_VARA = 0, P_VARB, P_VARC, P_FILLA, P_FILLB, P_FILLC, P_INTRO, P_END, NPARTS };

// a grid being built: up to 16 instruments x 64 steps (4 bars of 16ths)
struct Grid {
  static const int MAXI = 16, MAXS = 64;
  s16 sound[MAXI];
  u8 vel[MAXI][MAXS];
  int ni = 0, steps = 16;
  int row(int snd) {
    for (int i = 0; i < ni; i++) if (sound[i] == snd) return i;
    if (ni >= MAXI) return -1;
    sound[ni] = (s16)snd;
    for (int s = 0; s < MAXS; s++) vel[ni][s] = 0;
    return ni++;
  }
  void set(int snd, int step, int v) { if (snd < 0 || step < 0 || step >= MAXS) return; int r = row(snd); if (r >= 0) vel[r][step] = (u8)v; }
  int get(int snd, int step) { for (int i = 0; i < ni; i++) if (sound[i] == snd) return vel[i][step]; return 0; }
  void clearSound(int snd, int from, int to) { for (int i = 0; i < ni; i++) if (sound[i] == snd) for (int s = from; s < to; s++) vel[i][s] = 0; }
  bool has(int snd) { for (int i = 0; i < ni; i++) if (sound[i] == snd) for (int s = 0; s < MAXS; s++) if (vel[i][s]) return true; return false; }
};

inline int velOf(char c) { return c == 'X' ? 127 : (c == 'x' ? 100 : (c == 'o' ? 60 : 0)); }

// parse the style's groove into bar 0 of g (steps = beats * sub)
inline void parseGroove(const StyleDef& st, Grid& g, int offset) {
  const char* p = st.lines;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    char ins = *p;
    p += 2;   // letter and ':'
    int snd = styleSound(ins, st), s = 0;
    while (*p && *p != ' ') { int v = velOf(*p); if (v && snd >= 0) g.set(snd, offset + s, v); s++; p++; }
  }
}

// writes the part to events through add(tick, sound, vel); returns bars
template <class Add>
inline int buildStylePart(const StyleDef& st, int part, Add add) {
  const int bar = st.beats * st.sub, sub = st.sub;
  Grid g;
  g.ni = 0;
  int bars = 2;
  const int K = KICK1 + st.kick, S = SNARE1 + st.snare;
  const int tL = st.latin ? TIMBL : TOM1L + st.tomset * 3, tM = st.latin ? CONGA2 : TOM1M + st.tomset * 3, tH = st.latin ? TIMBH : TOM1H + st.tomset * 3;
  auto mainBars = [&](int variation) {
    parseGroove(st, g, 0);
    parseGroove(st, g, bar);
    // bar 2: a pickup kick near the end when there's room
    if (!st.latin && g.has(K)) { int s = bar + bar - (sub == 4 ? 3 : 2); if (!g.get(K, s) && !g.get(S, s)) g.set(K, s, 90); }
    if (variation >= 1) {
      if (g.has(HATCL)) {   // denser hats, an open hat into the bar line
        for (int b = 0; b < 2; b++) {
          bool eighths = true;
          for (int s = 1; s < bar; s += 2) if (g.get(HATCL, b * bar + s)) eighths = false;
          if (eighths && sub == 4) for (int s = 1; s < bar; s += 2) if (!g.get(HATCL, b * bar + s)) g.set(HATCL, b * bar + s, 55);
        }
        int s = 2 * bar - (sub == 4 ? 2 : 1);
        g.clearSound(HATCL, s, s + 1); g.set(HATOP, s, 100);
      } else if (g.has(RIDE) && !g.has(HATMD)) {
        for (int b = 0; b < 2; b++) for (int beat = 1; beat < st.beats; beat += 2) g.set(HATMD, b * bar + beat * sub, 90);
      }
      if (!st.latin && g.has(S) && sub == 4) { int s = bar + 7; if (!g.get(S, s)) g.set(S, s, 55); s = 2 * bar - 1; if (!g.get(S, s) && !g.get(K, s)) g.set(S, s, 50); }
      if (st.latin) { for (int b = 0; b < 2; b++) { int s = b * bar + bar - sub; g.set(TIMBH, s + (sub == 4 ? 2 : 1), 90); g.set(TIMBH, s + (sub == 4 ? 3 : 2), 100); } }
    }
    if (variation >= 2) {
      g.set(CRASH1, 0, 110);
      if (!st.latin) {
        // ride instead of hats
        for (int s = 0; s < 2 * bar; s++) {
          int v = g.get(HATCL, s) ? g.get(HATCL, s) : g.get(HATOP, s);
          if (v && (sub == 3 || s % 2 == 0)) g.set(RIDE, s, s % sub == 0 ? 110 : 85);
        }
        g.clearSound(HATCL, 0, 2 * bar); g.clearSound(HATOP, 0, 2 * bar - 2);
        for (int b = 0; b < 2; b++) for (int beat = 1; beat < st.beats; beat += 2) { if (!g.get(TAMB, b * bar + beat * sub)) g.set(TAMB, b * bar + beat * sub, 90); }
      } else {
        g.set(WHISTLE, bar, 90);
        for (int s = 0; s < 2 * bar; s += sub) if (!g.has(AGOGOH) || true) g.set(s % (2 * sub) ? AGOGOL : AGOGOH, s, 90);
      }
    }
  };
  auto fill = [&](int kind) {   // one bar
    bars = 1;
    // groove for the first part of the bar
    Grid gg; gg.ni = 0;
    parseGroove(st, gg, 0);
    int keepTo = kind == 0 ? (st.beats - 1) * sub : (kind == 1 ? (st.beats / 2) * sub : sub);
    if (kind == 2) keepTo = 1;
    for (int i = 0; i < gg.ni; i++) for (int s = 0; s < keepTo; s++) if (gg.vel[i][s]) g.set(gg.sound[i], s, gg.vel[i][s]);
    int n = bar - keepTo;
    for (int k = 0; k < n; k++) {
      int s = keepTo + k;
      double prog = n > 1 ? (double)k / (n - 1) : 1;
      int v = 70 + (int)(57 * prog);
      if (kind == 0) g.set(S, s, v);
      else {
        int seg = (int)(prog * 3.999);    // high, mid, low (and snare on the way)
        int snd = kind == 2 && k % 2 == 0 && seg < 2 ? S : (seg == 0 ? tH : (seg == 1 ? tM : tL));
        if (kind == 1 && k % 4 == 3) snd = S;
        if (seg == 3) snd = k % 2 ? tL : S;
        g.set(snd, s, v);
      }
      if (s % sub == 0) g.set(K, s, 100);
    }
    g.set(K, 0, 110);
  };
  switch (part) {
    case P_VARA: mainBars(0); break;
    case P_VARB: mainBars(1); break;
    case P_VARC: mainBars(2); break;
    case P_FILLA: fill(0); break;
    case P_FILLB: fill(1); break;
    case P_FILLC: fill(2); break;
    case P_INTRO:
      // a stick count, then fill A
      bars = 2;
      for (int b = 0; b < st.beats; b++) g.set(RIM2, b * sub, b == 0 ? 127 : 100);
      fill(0);
      // fill() wrote bar 1: move it to bar 2
      for (int i = 0; i < g.ni; i++) {
        if (g.sound[i] == RIM2) continue;
        for (int s = bar - 1; s >= 0; s--) { g.vel[i][s + bar] = g.vel[i][s]; g.vel[i][s] = 0; }
      }
      bars = 2;
      break;
    case P_END:
      fill(2);
      bars = 2;
      g.set(CRASH1, bar, 127); g.set(K, bar, 120); g.set(S, bar, 110);
      break;
  }
  const int tps = 96 / sub;
  for (int i = 0; i < g.ni; i++)
    for (int s = 0; s < bars * bar && s < Grid::MAXS; s++)
      if (g.vel[i][s]) add(s * tps, g.sound[i], g.vel[i][s]);
  return bars;
}
template <class Add>
inline int buildStylePart(int styleIdx, int part, Add add) { return buildStylePart(STYLES[styleIdx], part, add); }

// a new groove for the dice button: a kit, a feel, and lines for kick, snare, hats and up to two percussion parts,
// then the same variations, fills, intro and ending as the presets
struct RandomStyle {
  StyleDef def;
  char name[10], lines[400];
  void make(u32 seed, int number) {
    xm::Rng r; r.seed(seed * 2654435761u + 12345);
    for (int i = 0; i < 4; i++) r.next();
    bool trip = r.uni() < 0.15;
    int sub = trip ? 3 : 4, steps = 4 * sub;
    char* o = lines; int n = 0;
    auto put = [&](char c) { if (n < 398) o[n++] = c; };
    auto line = [&](char ins, const char* pat) { if (n) put(' '); put(ins); put(':'); for (int i = 0; pat[i]; i++) put(pat[i]); };
    char k[17], s[17], h[17], p[17];
    for (int i = 0; i < 16; i++) k[i] = s[i] = h[i] = p[i] = '.';
    k[steps] = s[steps] = h[steps] = p[steps] = 0;
    // kick: the downbeat, then likelier on strong positions
    for (int i = 0; i < steps; i++) {
      double pr = i == 0 ? 1 : (i % (2 * sub) == 0 ? 0.55 : (i % sub == 0 ? 0.3 : (trip ? 0.18 : (i % 2 == 0 ? 0.22 : 0.1))));
      if (r.uni() < pr) k[i] = 'x';
    }
    // snare: the backbeats, sometimes ghosts
    for (int i = 0; i < steps; i++) {
      if (i == sub || i == 3 * sub) s[i] = 'X';
      else if (r.uni() < 0.1 && k[i] == '.') s[i] = 'o';
    }
    // hats or ride
    int hatMode = (int)(r.uni() * 4);
    char hatIns = 'H';
    for (int i = 0; i < steps; i++) {
      if (hatMode == 0) h[i] = (trip ? i % 3 != 1 : i % 2 == 0) ? 'x' : '.';
      else if (hatMode == 1) h[i] = i % sub == 0 ? 'x' : 'o';
      else if (hatMode == 2) h[i] = i % sub == sub / 2 ? 'x' : '.';
      else { hatIns = 'R'; h[i] = (i % 2 == 0) ? 'x' : '.'; }
    }
    if (hatMode == 2) hatIns = 'O';
    line('K', k); line('S', s); line(hatIns, h);
    if (hatMode == 2) { char c[17]; for (int i = 0; i < steps; i++) c[i] = i % sub == 0 ? 'x' : '.'; c[steps] = 0; line('H', c); }
    // up to two percussion parts
    static const char perc[] = "BNZPchVrAa";
    int np = (int)(r.uni() * 3);
    for (int q = 0; q < np; q++) {
      char ins = perc[(int)(r.uni() * 10)];
      for (int i = 0; i < steps; i++) p[i] = r.uni() < (ins == 'Z' || ins == 'N' ? 0.5 : 0.25) ? 'x' : '.';
      if (ins == 'P') for (int i = 0; i < steps; i++) p[i] = (i == sub || i == 3 * sub) ? 'x' : '.';
      line(ins, p);
    }
    o[n] = 0;
    name[0] = 'R'; name[1] = 'A'; name[2] = 'N'; name[3] = 'D'; name[4] = ' ';
    name[5] = (char)('0' + (number / 10) % 10); name[6] = (char)('0' + number % 10); name[7] = 0;
    def.name = name; def.beats = 4; def.sub = (u8)sub; def.swing = (u8)(trip ? 50 : 50 + (int)(r.uni() * 3) * 4);
    def.tempo = (u8)(84 + (int)(r.uni() * 52)); def.kick = (u8)(r.uni() * 10); def.snare = (u8)(r.uni() * 10);
    def.tomset = (u8)(r.uni() * 4); def.latin = 0; def.lines = lines;
  }
};
}  // namespace xr
