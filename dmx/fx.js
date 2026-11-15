/* Omega DMX effects and filters: the Omega 110's and Omega Traks' (traks/engine.js), copied unchanged so the machines
   sound alike: the Simper SVF, the RBJ biquad, the TC 2290-style dynamic delay and the REV7-style reverb.
   plugins/dmx/Fx.h is the same code in C++. */
'use strict';
(function (root) {
const TAU = 2 * Math.PI;
// Simper / Cytomic trapezoidal state-variable filter (stable when its frequency moves every sample)
class SVF {
  constructor(){ this.ic1 = 0; this.ic2 = 0; this.a1 = 0; this.a2 = 0; this.a3 = 0; this.k = 1; this.bp = 0; this.lp = 0; this.hp = 0; }
  set(f, q, sr){ const g = Math.tan(Math.PI * Math.min(f, sr * 0.45) / sr); this.k = 1 / q; this.a1 = 1 / (1 + g * (g + this.k)); this.a2 = g * this.a1; this.a3 = g * this.a2; }
  tick(x){
    const v3 = x - this.ic2, v1 = this.a1 * this.ic1 + this.a2 * v3, v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2; this.bp = v1; this.lp = v2; this.hp = x - this.k * v1 - v2; return v1;
  }
}
const onePole = (f, sr) => 1 - Math.exp(-TAU * f / sr);
// RBJ biquad (transposed direct form II): 0 low shelf, 1 peak, 2 high shelf. off when the gain is ~0 dB.
class Biquad {
  constructor(){ this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; this.on = false; }
  set(type, f, gainDb, q, sr){
    this.on = Math.abs(gainDb) >= 0.01;
    if (!this.on){ this.z1 = 0; this.z2 = 0; return; }
    const A = Math.pow(10, gainDb / 40), w = TAU * Math.min(f, sr * 0.45) / sr, cw = Math.cos(w), sw = Math.sin(w);
    let b0, b1, b2, a0, a1, a2;
    if (type === 1){ const al = sw / (2 * q); b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
    else {
      const al = sw / 2 * Math.SQRT2, sa = 2 * Math.sqrt(A) * al;
      if (type === 0){ b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; }
      else { b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; }
    }
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0; this.a1 = a1 / a0; this.a2 = a2 / a0;
  }
  tick(x){ if (!this.on) return x; const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
}
const decayCoef = (tau, sr) => Math.exp(-1 / (tau * sr));
/* ---- TC 2290-style dynamic delay: stereo, modulated, filtered feedback, ping-pong, and ducking (the repeats duck
   under the dry signal and swell up in the gaps, the 2290's "dynamic delay"). Time in ms or synced to the tempo. */
class TCDelay {
  constructor(sr){
    this.sr = sr; this.n = Math.ceil(sr * 3) + 8; this.bL = new Float64Array(this.n); this.bR = new Float64Array(this.n); this.w = 0;
    this.cur = -1; this.target = sr * 0.375; this.glide = onePole(3, sr); this.ph = 0; this.env = 0;
    this.lpL = 0; this.lpR = 0; this.hpL = 0; this.hpR = 0; this.out = [0, 0];
    this.envA = onePole(1 / (TAU * 0.005), sr); this.envR = onePole(1 / (TAU * 0.25), sr);
    this.setParams(375, 0.35, 6000, 100, 0.6, 0.2, 0.4, true);
  }
  setParams(ms, fb, hiCut, loCut, rate, depth, duck, ping){
    const sr = this.sr;
    this.target = Math.max(4, Math.min(this.n - 8 - sr * 0.004, ms * sr / 1000)); if (this.cur < 0) this.cur = this.target;
    this.fb = fb; this.lpA = onePole(hiCut, sr); this.hpA = onePole(loCut, sr); this.inc = rate / sr; this.depth = depth * 0.003 * sr; this.duck = duck; this.ping = ping;
  }
  read(b, d){ let pos = this.w - d; while (pos < 0) pos += this.n; const i = Math.floor(pos), fr = pos - i, j = i + 1 >= this.n ? 0 : i + 1; return b[i] + (b[j] - b[i]) * fr; }
  tick(x){
    const ax = Math.abs(x); this.env += (ax > this.env ? this.envA : this.envR) * (ax - this.env);
    this.cur += (this.target - this.cur) * this.glide;
    this.ph += this.inc; if (this.ph >= 1) this.ph -= 1;
    const m = Math.sin(TAU * this.ph) * this.depth;
    const yl = this.read(this.bL, this.cur + this.depth + m), yr = this.read(this.bR, this.cur + this.depth - m);
    this.lpL += this.lpA * (yl - this.lpL); this.hpL += this.hpA * (this.lpL - this.hpL);
    this.lpR += this.lpA * (yr - this.lpR); this.hpR += this.hpA * (this.lpR - this.hpR);
    const fl = (this.lpL - this.hpL) * this.fb, fr = (this.lpR - this.hpR) * this.fb;
    if (this.ping){ this.bL[this.w] = x + fr; this.bR[this.w] = fl; } else { this.bL[this.w] = x + fl; this.bR[this.w] = x + fr; }
    this.w = this.w + 1 >= this.n ? 0 : this.w + 1;
    const g = 1 / (1 + this.duck * this.env * 12);
    this.out[0] = yl * g; this.out[1] = yr * g; return this.out;
  }
}

/* ---- REV7-style digital reverb. Yamaha's REV7 (1983): programs Hall, Room, Vocal, Plate and Early Reflections; RT,
   HIGH (the high-frequency RT as a ratio), DELAY (pre-delay), ER level, diffusion, high cut, and its ~12 kHz bandwidth.
   Built as: pre-delay -> 12 kHz band limit -> four input diffusers -> early reflections (a 16-tap pattern per program)
   and an 8-line feedback delay network with a Hadamard matrix and per-line absorption filters set from RT and HIGH. */
const RV_LINES = [  // ms, before the program's size
  [31.7, 37.1, 41.3, 43.9, 53.3, 59.9, 67.7, 73.1],
];
const RV_SIZE = [1.0, 0.42, 0.7, 0.55, 0.3];
const RV_TAPS = [   // [ms, gain] x 8 per side; L uses them as given, R offsets them
  [[7.1, .84], [11.3, -.71], [17.9, .62], [23.3, -.55], [31.7, .47], [41.9, -.41], [53.1, .33], [67.3, -.27]],
  [[3.1, .9], [5.3, -.8], [7.9, .7], [11.1, -.62], [13.9, .55], [17.3, -.47], [21.1, .4], [26.9, -.33]],
  [[5.3, .8], [9.1, -.7], [13.7, .62], [19.1, -.55], [25.3, .47], [31.1, -.41], [39.7, .35], [47.3, -.3]],
  [[1.3, .7], [2.9, -.66], [4.1, .6], [5.9, -.55], [7.3, .5], [9.7, -.45], [11.9, .41], [14.3, -.37]],
  [[9.7, .95], [16.3, -.88], [24.1, .8], [33.7, -.72], [44.9, .64], [57.1, -.56], [71.3, .48], [89.9, -.4]],
];
const RV_AP = [4.771, 3.595, 12.73, 9.307];
class Rev7 {
  constructor(sr){
    this.sr = sr;
    this.pre = new Float64Array(Math.ceil(sr * 0.4) + 2); this.preW = 0;
    this.lines = []; this.lw = []; this.len = []; this.g = []; this.damp = []; this.lz = [];
    const maxLen = Math.ceil(sr * 0.08) + 2;
    for (let i = 0; i < 8; i++){ this.lines.push(new Float64Array(maxLen)); this.lw.push(0); this.len.push(1); this.g.push(0); this.damp.push(0); this.lz.push(0); }
    this.ap = []; this.apw = []; this.apLen = [];
    for (let i = 0; i < 4; i++){ const n = Math.max(1, Math.floor(RV_AP[i] * sr / 1000)); this.ap.push(new Float64Array(n)); this.apw.push(0); this.apLen.push(n); }
    this.bwA = onePole(12000, sr); this.bw = 0; this.cutA = 0; this.cutL = 0; this.cutR = 0;
    this.out = [0, 0]; this.tapL = []; this.tapR = []; this.preD = 1; this.apG = 0.6; this.er = 0.35; this.prog = -1;
  }
  setParams(prog, rt, high, delayMs, diff, er, cut){
    const sr = this.sr; prog = Math.max(0, Math.min(4, prog));
    this.preD = Math.max(1, Math.floor(delayMs * sr / 1000));
    const size = RV_SIZE[prog];
    for (let i = 0; i < 8; i++){
      const n = Math.max(2, Math.floor(RV_LINES[0][i] * size * sr / 1000));
      if (n !== this.len[i]){ this.len[i] = n; this.lines[i].fill(0); this.lw[i] = 0; this.lz[i] = 0; }
      const sec = n / sr, g = Math.pow(10, -3 * sec / rt);
      this.g[i] = prog === 4 ? g * 0.35 : g;
      let b = Math.log(10) / 4 * Math.log10(g) * (1 - 1 / (high * high));
      this.damp[i] = Math.max(0, Math.min(0.95, b));
    }
    this.apG = prog === 3 ? 0.75 * diff / 10 : 0.68 * diff / 10;
    this.tapL = []; this.tapR = [];
    for (const [ms, gn] of RV_TAPS[prog]){ this.tapL.push([Math.floor(ms * sr / 1000), gn]); this.tapR.push([Math.floor(ms * 1.137 * sr / 1000), -gn * 0.95]); }
    this.er = prog === 4 ? 1 : er; this.late = prog === 4 ? 0.25 : 1;
    this.cutA = onePole(cut, sr);
  }
  readPre(d){ let i = this.preW - d; while (i < 0) i += this.pre.length; return this.pre[i]; }
  tick(x){
    // pre-delay line (the ER taps read from it too)
    this.pre[this.preW] = x;
    this.bw += this.bwA * (this.readPre(this.preD) - this.bw);
    let d = this.bw;
    for (let i = 0; i < 4; i++){
      const buf = this.ap[i], w = this.apw[i], z = buf[w], v = d + this.apG * z;
      buf[w] = v; d = z - this.apG * v; this.apw[i] = w + 1 >= this.apLen[i] ? 0 : w + 1;
    }
    // early reflections
    let eL = 0, eR = 0;
    for (let k = 0; k < 8; k++){ eL += this.readPre(this.preD + this.tapL[k][0]) * this.tapL[k][1]; eR += this.readPre(this.preD + this.tapR[k][0]) * this.tapR[k][1]; }
    this.preW = this.preW + 1 >= this.pre.length ? 0 : this.preW + 1;
    // FDN: read, absorb, Hadamard, write
    const o = [0, 0, 0, 0, 0, 0, 0, 0];
    for (let i = 0; i < 8; i++){
      // each line is a ring buffer of length len[i]: the oldest sample sits at the write position
      const y = this.lines[i][this.lw[i]];
      this.lz[i] = y * (1 - this.damp[i]) + this.lz[i] * this.damp[i];
      o[i] = this.lz[i] * this.g[i];
    }
    // fast Walsh-Hadamard over 8, scaled to stay unitary
    for (let h = 1; h < 8; h *= 2) for (let i = 0; i < 8; i += h * 2) for (let j = i; j < i + h; j++){ const a = o[j], b = o[j + h]; o[j] = a + b; o[j + h] = a - b; }
    const sc = 1 / Math.sqrt(8);
    let lL = 0, lR = 0;
    for (let i = 0; i < 8; i++){
      const y = this.lz[i];
      if (i & 1) lR += y * ((i & 2) ? -1 : 1); else lL += y * ((i & 2) ? -1 : 1);
      this.lines[i][this.lw[i]] = o[i] * sc + d * ((i & 4) ? -0.5 : 0.5);
      this.lw[i] = this.lw[i] + 1 >= this.len[i] ? 0 : this.lw[i] + 1;
    }
    let l = eL * this.er * 0.5 + lL * this.late * 0.5, r = eR * this.er * 0.5 + lR * this.late * 0.5;
    this.cutL += this.cutA * (l - this.cutL); this.cutR += this.cutA * (r - this.cutR);
    this.out[0] = this.cutL; this.out[1] = this.cutR; return this.out;
  }
}

const API = {TAU, SVF, onePole, Biquad, decayCoef, TCDelay, Rev7, RV_PROGS:['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF']};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DMXFX = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
