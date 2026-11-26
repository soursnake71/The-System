/* Omega Five sound engine: 24 sample voices (no drum synthesis: the factory voices are recordings of a real RX5, five/kit/,
   loaded with setUserSample), the RX5-style 8/12-bit PCM playback path (EG, DAC, reconstruction filter, output stage),
   a 12-channel mixer, a REV7-style reverb and a TC 2290-style delay.
   Pure JS with no DOM, an ES module used by worklet.js, by Node tests (tools/test.mjs) and by tools/render.mjs, which
   writes the reference output that plugins/five/Five.h (a line-for-line C++ port of this file) must match.

   Porting rules: every DSP state is a double; outputs are stored as float32. Only Math.sin/cos/tan/exp/pow/sqrt/abs/
   floor/tanh/round are used. The PRNG is xorshift32 (uint32_t in C++). process() and trigger() never allocate; the
   delay lines are allocated in the constructor; voices start empty (silent) and setUserSample allocates (not real-time). */

export const ROM_SR = 25000;            // native rate the voice ROMs are rendered and stored at
const DAC_SR = 50000;                   // the playback DAC's sample-and-hold rate
const NV = 24, NCH = 12;
const TWO_PI = 2 * Math.PI, LN10 = Math.log(10);
const clamp = (x, a, b) => x < a ? a : x > b ? b : x;

export const VOICE_NAMES = ['BD 1', 'SD 1', 'RIM 1', 'TOM 1A', 'TOM 2A', 'TOM 3A', 'TOM 4A', 'HH CLS', 'RIDE', 'CRASH', 'TAMB', 'SHAKER',
  'BD 2', 'SD 2', 'RIM 2', 'TOM 1B', 'TOM 2B', 'TOM 3B', 'TOM 4B', 'HH OPN', 'RIDE CP', 'CHINA', 'CLAPS', 'COWBELL'];
export const CHANNEL_NAMES = ['BD', 'SD', 'RIM', 'TOM1', 'TOM2', 'TOM3', 'TOM4', 'HIHAT', 'RIDE', 'CRASH', 'TMBRN/CLAPS', 'SHKR/CWBEL'];

/* ---------------- random numbers: xorshift32 (C++: uint32_t s; s ^= s << 13; s ^= s >> 17; s ^= s << 5;) ---------------- */
export function rng(seed){
  let s = (seed >>> 0) || 0x9E3779B9;
  return () => {
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    return s / 4294967296;          // [0, 1)
  };
}

/* ---------------- filters ---------------- */
// first-order bilinear (pre-warped) low/high pass
class OnePole {
  constructor(){ this.b0 = 1; this.b1 = 0; this.a1 = 0; this.x1 = 0; this.y1 = 0; }
  lp(fs, f){ const k = Math.tan(Math.PI * clamp(f, 1, fs * .49) / fs); this.b0 = k / (1 + k); this.b1 = this.b0; this.a1 = (k - 1) / (k + 1); return this; }
  hp(fs, f){ const k = Math.tan(Math.PI * clamp(f, 1, fs * .49) / fs); this.b0 = 1 / (1 + k); this.b1 = -this.b0; this.a1 = (k - 1) / (k + 1); return this; }
  run(x){ const y = this.b0 * x + this.b1 * this.x1 - this.a1 * this.y1; this.x1 = x; this.y1 = y; return y; }
  reset(){ this.x1 = 0; this.y1 = 0; }
}
// RBJ biquad, transposed direct form II; 'bp' has 0 dB peak gain
class Biquad {
  constructor(){ this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  set(type, fs, f, q){
    const w = TWO_PI * clamp(f, 1, fs * .49) / fs, cw = Math.cos(w), al = Math.sin(w) / (2 * q), a0 = 1 + al;
    let b0, b1, b2;
    if (type === 'lp'){ b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
    else if (type === 'hp'){ b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; }
    else { b0 = al; b1 = 0; b2 = -al; }
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0; this.a1 = -2 * cw / a0; this.a2 = (1 - al) / a0;
    return this;
  }
  run(x){ const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
  reset(){ this.z1 = 0; this.z2 = 0; }
}
// the factory kit: voice v plays five/kit/FACTORY_MAP[v] (25 kHz 16-bit mono recordings of a real RX5), loaded with {rec:true}
export const FACTORY_MAP = ['BDrum1-RX5.wav', 'SDrum1-RX5.wav', 'Rim1-RX5.wav', 'Tom1-RX5.wav', 'Tom2-RX5.wav', 'Tom3-RX5.wav', 'Tom4-RX5.wav',
  'CHat-RX5.wav', 'RideEdge-RX5.wav', 'Crash-RX5.wav', 'Tamb-RX5.wav', 'Shaker-RX5.wav', 'BDrum2-RX5.wav', 'SDrum2-RX5.wav', 'Rim2-RX5.wav',
  'ETom1-RX5.wav', 'ETom2-RX5.wav', 'ETom3-RX5.wav', 'ETom4-RX5.wav', 'OHat-RX5.wav', 'RideBell-RX5.wav', 'China-RX5.wav', 'Clap-RX5.wav',
  'Cowbell-RX5.wav'];

/* ---------------- recording pre-emphasis ----------------
   A recording already carries the real machine's playback response. At pitch 0 this engine's path (linear interpolation
   25 -> 50 kHz, 50 kHz ZOH, 3-pole 12 kHz filter) droops -1.0 dB at 5 kHz, -5.5 dB at 10 kHz, -7.2 dB at 11 kHz; REC_EQ, a
   7-tap linear-phase FIR at 25 kHz (DC gain 1, least-squares fit to the inverse up to 11 kHz), flattens it to within
   +-0.25 dB to 10 kHz (-0.6 dB at 11 kHz). Applied once to a 'rec' ROM when it is loaded (zero-phase: centred taps). */
const REC_EQ = [-.0426, .1175, -.2817, 1.4136, -.2817, .1175, -.0426];
function recEq(x){
  const n = x.length, h = (REC_EQ.length - 1) / 2, y = new Float32Array(n);
  for (let i = 0; i < n; i++){
    let s = 0;
    for (let k = 0; k < REC_EQ.length; k++){ const j = i + k - h; if (j >= 0 && j < n) s += REC_EQ[k] * x[j]; }
    y[i] = s;
  }
  return y;
}

/* ---------------- the ROM word ----------------
   RX5 voices are 25 kHz signed PCM, 8- or 12-bit per voice. 'linear' (default): two's complement, rounded to nearest,
   step = 2^-(bits-1). 'float' (the YM3014 floating-point DAC reading): blocks of 64 samples share a gain shift
   exp 0..3 (0 for blocks peaking at >= 1/2, 3 for the quietest), mantissa of `bits` bits, step = 2^-(bits-1) / 2^exp.
   Writes into `out` (same length as x) so it can requantize a preallocated ROM in place. */
export function quantizeRom(x, bits = 12, mode = 'linear', out){
  const n = x.length, M = Math.pow(2, bits - 1);
  out = out || new Float32Array(n);
  for (let b = 0; b < n; b += 64){
    const e = Math.min(n, b + 64);
    let ex = 0;
    if (mode === 'float'){ let pk = 0; for (let i = b; i < e; i++){ const a = Math.abs(x[i]); if (a > pk) pk = a; } ex = pk >= .5 ? 0 : pk >= .25 ? 1 : pk >= .125 ? 2 : 3; }
    const step = Math.pow(2, -ex) / M;
    for (let i = b; i < e; i++) out[i] = clamp(Math.floor(x[i] / step + .5), -M, M - 1) * step;
  }
  return out;
}

/* ---------------- parameters ---------------- */
export const RANGES = {
  // RX5 Edit Voice ranges (owner's manual pp.34-38): pitch +24/-36 st in cents, D1L 01-60, gate 0.1-6.5 s (0 = off),
  // bend rate 0-60, bend range +-60 st, level 0-31; accent 1/2 level offsets +-31 (Key Assign #05); bits 8|12.
  // Rates 1-99 as on the panel; 0 is accepted for d1r/d2r and means "hold".
  voice:{pitch:[-3600, 2400], level:[0, 31], ar:[1, 99], d1r:[0, 99], d1l:[0, 60], d2r:[0, 99], rr:[1, 99], gate:[0, 6500],
    bendRate:[0, 60], bendRange:[-6000, 6000], reverse:[0, 1], loop:[0, 1], acc1:[-31, 31], acc2:[-31, 31], bits:[8, 12]},
  channel:{fader:[0, 1], pan:[-1, 1], sendRev:[0, 1], sendDly:[0, 1], mute:[0, 1]},
  rev:{program:[0, 10], time:[.3, 30], hiRatio:[.1, 1], delay:[0, 200], diffusion:[0, 10], density:[0, 100], erLevel:[0, 1],
    revLevel:[0, 1], hpf:[20, 1000], lpf:[1000, 16000], gateTime:[10, 500], level:[0, 1]},
  dly:{on:[0, 1], timeL:[1, 2000], timeR:[1, 2000], sync:[0, 5], tempo:[40, 250], feedback:[0, 120], pingpong:[0, 1],
    hiCut:[1000, 20000], loCut:[20, 1000], modRate:[.05, 10], modDepth:[0, 1], duckThresh:[-60, 0], duckRelease:[10, 2000],
    duckDepth:[0, 40], spread:[0, 1], mix:[0, 1]},
  master:{volume:[0, 1], click:[0, 1]}
};
// gate is in ms (0 = no gate: the sound plays out); rates 0..99 (0 = hold for d1r/d2r, slowest for rr)
// path: 'rx' = the ROM word + whole playback path; 'rec' = a recording of a real RX5 (setUserSample opts.rec): no 12-bit word, no output saturation
const VD = (o) => Object.assign({pitch:0, level:25, ar:99, d1r:0, d1l:60, d2r:0, rr:45, gate:0, bendRate:0, bendRange:0, reverse:0, loop:0,
  acc1:2, acc2:4, bits:12, path:'rx'}, o);
export const VOICE_DEFAULTS = VOICE_NAMES.map((_, v) => VD(v === 19 ? {rr:70} : {}));
// the RX5's factory stereo positions (15 = left, 8 = centre, 1 = right) as pan -1..1
const POS = [8, 9, 9, 12, 10, 6, 4, 13, 3, 11, 5, 8];
export const CHANNEL_DEFAULTS = POS.map(p => ({fader:1, pan:(8 - p) / 7, sendRev:0, sendDly:0, mute:false}));
export const MASTER_DEFAULTS = {volume:1, click:.5};

/* =====================================================================================================================
   Playback slot: one voice on one channel (ROM read by linear interpolation, pitch bend, reverse/loop, 5-segment EG)
   ===================================================================================================================== */
// level steps are 1.5 dB, so the default accents (+2 / +4 steps) are +3 / +6 dB; DAC_HEAD puts level 25 at about -3 dB into the output stage
const EG_FLOOR = 1e-4, FADE_STEP = 1 / (.002 * DAC_SR), DAC_HEAD = 1.45, OUT_K = 2;
const atkTime = r => 2 * Math.pow(2, -r / 8);                            // s, 0..1 linear ramp
const t60 = r => 12 * Math.pow(2, -r / 8.5);                             // s to -60 dB
const decCoef = r => r <= 0 ? 1 : Math.exp(-3 * LN10 / (t60(r) * DAC_SR));
const relCoef = r => Math.exp(-3 * LN10 / (t60(r) * DAC_SR));
const egLevel = l => l >= 60 ? 1 : l <= 0 ? 0 : Math.pow(10, -(60 - l) * 1.2 / 20);   // 1.2 dB per step
const levelGain = l => l <= 0 ? 0 : Math.pow(10, (l - 31) * 1.5 / 20);

class Slot {
  constructor(){
    this.active = false; this.v = -1; this.rom = null; this.len = 0; this.pos = 0; this.inc = 0; this.baseInc = 0;
    this.pitch = 0; this.bend = 0; this.bendT = 0; this.bendC = 0; this.rev = false; this.loop = false; this.ll = 0;
    this.stage = 0; this.lvl = 0; this.atk = 0; this.L1 = 1; this.c1 = 1; this.c2 = 1; this.cr = 1; this.gateN = 0; this.n = 0;
    this.gain = 0; this.fade = 1; this.rec = false;
  }
  copyFrom(o){ for (const k in o) this[k] = o[k]; }
  // rec: the ROM is a recording of a real RX5 (path 'rec'): the channel then skips the output-stage saturation
  start(v, rom, p, vel, acc, rec){
    this.v = v; this.rom = rom; this.len = rom.length; this.pos = 0; this.rec = rec;
    this.rev = p.reverse >= .5; this.loop = p.loop >= .5; this.ll = (this.len - 1) - Math.floor(this.len * .7);
    this.baseInc = ROM_SR / DAC_SR * Math.pow(2, p.pitch / 1200); this.inc = this.baseInc;
    // bend: linear sweep from the pitch toward pitch + range at 25 * 2^(rate/6) cents per second, held inside +24/-36 st
    this.pitch = p.pitch; this.bend = 0; this.bendT = p.bendRange;
    this.bendC = p.bendRate > 0 && p.bendRange !== 0 ? 25 * Math.pow(2, p.bendRate / 6) / DAC_SR : 0;
    this.stage = p.ar >= 99 ? 1 : 0; this.lvl = this.stage ? 1 : 0; this.atk = p.ar >= 99 ? 1 : 1 / (atkTime(p.ar) * DAC_SR);
    this.L1 = egLevel(p.d1l); this.c1 = decCoef(p.d1r); this.c2 = decCoef(p.d2r); this.cr = relCoef(p.rr);
    this.gateN = p.gate > 0 ? Math.round(p.gate * DAC_SR / 1000) : 0; this.n = 0;
    const lv = clamp(p.level + (acc === 1 ? p.acc1 : acc === 2 ? p.acc2 : 0), 0, 31);
    this.gain = DAC_HEAD * levelGain(lv) * (vel / 127);
    this.fade = 1; this.active = this.len >= 2;
  }
  // one DAC tick: read, then EG, then advance the address
  tick(){
    const L = this.len, R = this.rom, q = this.rev ? (L - 1) - this.pos : this.pos;
    const i = Math.floor(q), f = q - i, a = R[i], b = i + 1 < L ? R[i + 1] : 0;
    const s = (a + (b - a) * f) * this.lvl * this.gain;
    this.n++;
    if (this.gateN > 0 && this.stage < 3 && this.n >= this.gateN) this.stage = 3;
    if (this.stage === 0){ this.lvl += this.atk; if (this.lvl >= 1){ this.lvl = 1; this.stage = 1; } }
    else if (this.stage === 1){
      if (this.lvl <= this.L1) this.stage = 2;
      else if (this.c1 < 1){ this.lvl *= this.c1; if (this.lvl <= this.L1){ this.lvl = this.L1; this.stage = 2; } }
    }
    else if (this.stage === 2){ if (this.c2 < 1){ this.lvl *= this.c2; if (this.lvl < EG_FLOOR) this.active = false; } }
    else { this.lvl *= this.cr; if (this.lvl < EG_FLOOR) this.active = false; }
    if (this.bendC > 0){
      if (this.bendT > this.bend){ this.bend += this.bendC; if (this.bend >= this.bendT){ this.bend = this.bendT; this.bendC = 0; } }
      else { this.bend -= this.bendC; if (this.bend <= this.bendT){ this.bend = this.bendT; this.bendC = 0; } }
      this.inc = ROM_SR / DAC_SR * Math.pow(2, clamp(this.pitch + this.bend, -3600, 2400) / 1200);
    }
    this.pos += this.inc;
    if (this.pos >= L - 1){
      if (this.loop && this.ll > 0){ while (this.pos >= L - 1) this.pos -= this.ll; }
      else this.active = false;
    }
    return s;
  }
}

/* =====================================================================================================================
   REV7-style reverb: converter (anti-alias LP, sample-and-hold at 31.25 kHz, 16-bit word), input HPF/LPF, predelay,
   per-program early-reflection tap pattern, 4 series allpass diffusers, 8-line modulated FDN with Householder feedback
   and per-line HF absorption; GATE / REVERSE GATE are dense tap clusters over gateTime. Output 16-bit + reconstruction LP.
   ===================================================================================================================== */
const P = (name, type, size, erSpan, erTaps, time, hiRatio, delay, diffusion, density, erLevel, revLevel, hpf, lpf, gateTime) =>
  ({name, type, size, erSpan, erTaps, time, hiRatio, delay, diffusion, density, erLevel, revLevel, hpf, lpf, gateTime});
// type 0 reverb, 1 early reflections, 2 gate, 3 reverse gate
export const REV_PROGRAMS = [
  P('HALL1', 0, 1.35, 80, 12, 2.6, .6, 30, 8, 80, .45, .8, 20, 12000, 200),
  P('HALL2', 0, 1.8, 110, 14, 4.2, .5, 45, 9, 90, .4, .8, 40, 10000, 200),
  P('ROOM1', 0, .6, 35, 10, .9, .7, 8, 7, 70, .6, .7, 20, 14000, 200),
  P('ROOM2', 0, .8, 50, 12, 1.5, .55, 15, 8, 80, .55, .75, 60, 11000, 200),
  P('VOCAL', 0, 1, 50, 10, 2.2, .45, 60, 6, 60, .35, .8, 100, 9000, 200),
  P('PLATE1', 0, .9, 0, 0, 2, .8, 10, 10, 100, 0, .85, 20, 16000, 200),
  P('PLATE2', 0, 1.1, 0, 0, 3.2, .7, 25, 10, 100, 0, .85, 80, 12000, 200),
  P('ER1', 1, .7, 70, 16, .6, .6, 5, 5, 60, 1, .12, 20, 14000, 200),
  P('ER2', 1, 1, 140, 10, .8, .6, 20, 3, 40, 1, .1, 20, 12000, 200),
  P('GATE', 2, 1, 0, 32, 1, .8, 0, 8, 100, 0, 1, 40, 12000, 180),
  P('REV GATE', 3, 1, 0, 32, 1, .8, 0, 8, 100, 0, 1, 40, 12000, 250)
];
export const REV_DEFAULTS = {program:0, time:2.6, hiRatio:.6, delay:30, diffusion:8, density:80, erLevel:.45, revLevel:.8, hpf:20, lpf:12000, gateTime:200, level:1};
const REV_KEYS = ['time', 'hiRatio', 'delay', 'diffusion', 'density', 'erLevel', 'revLevel', 'hpf', 'lpf', 'gateTime'];
const AP_REF = 29761, AP_LEN = [142, 107, 379, 277];
const FDN_MS = [29.7, 37.1, 41.1, 43.7, 53.0, 59.9, 67.7, 73.1];
const FDN_IN = [1, -1, 1, -1, 1, -1, 1, -1], FDN_L = [1, 1, -1, 1, -1, -1, 1, -1], FDN_R = [1, -1, 1, 1, -1, 1, -1, -1];
const MAX_TAPS = 32, REV_SR = 31250;
const q16 = x => clamp(Math.floor(x * 32768 + .5), -32768, 32767) / 32768;

export class Rev7 {
  constructor(sr){
    this.sr = sr;
    this.L = Math.ceil(.75 * sr) + 4; this.inBuf = new Float64Array(this.L); this.dfBuf = new Float64Array(this.L); this.w = 0;
    this.apBuf = AP_LEN.map(n => new Float64Array(Math.ceil(n * sr / AP_REF) + 2)); this.apN = new Int32Array(4); this.apI = new Int32Array(4); this.apG = 0;
    this.DL = Math.ceil(.21 * sr) + 8; this.lines = FDN_MS.map(() => new Float64Array(this.DL)); this.dw = 0;
    this.len = new Float64Array(8); this.fb = new Float64Array(8); this.fp = new Float64Array(8); this.z = new Float64Array(8);
    this.tapT = new Int32Array(MAX_TAPS); this.tapL = new Float64Array(MAX_TAPS); this.tapR = new Float64Array(MAX_TAPS); this.nT = 0;
    this.aa = new Biquad().set('lp', sr, Math.min(13500, .45 * sr), .707);
    this.hpf = new Biquad(); this.lpf = new Biquad();
    this.rcL = new Biquad().set('lp', sr, Math.min(13500, .45 * sr), .707); this.rcR = new Biquad().set('lp', sr, Math.min(13500, .45 * sr), .707);
    this.shInc = REV_SR / sr; this.shPh = 0; this.sh = 0;
    const w = TWO_PI * .55 / sr; this.lcw = Math.cos(w); this.lsw = Math.sin(w); this.lc = 1; this.ls = 0; this.depth = .00035 * sr;
    this.oL = 0; this.oR = 0;
    this.p = Object.assign({}, REV_DEFAULTS); this.set('program', 0);
  }
  set(name, val){
    if (name === 'program'){
      let i = typeof val === 'string' ? REV_PROGRAMS.findIndex(q => q.name === val.toUpperCase()) : Math.round(+val);
      i = clamp(i | 0, 0, REV_PROGRAMS.length - 1); this.p.program = i;
      for (const k of REV_KEYS) this.p[k] = REV_PROGRAMS[i][k];
    } else if (RANGES.rev[name] && Number.isFinite(+val)) this.p[name] = clamp(+val, RANGES.rev[name][0], RANGES.rev[name][1]);
    else return;
    this.rebuild();
  }
  rebuild(){
    const p = this.p, sr = this.sr, pr = REV_PROGRAMS[p.program];
    this.type = pr.type;
    this.hpf.set('hp', sr, p.hpf, .707); this.lpf.set('lp', sr, p.lpf, .707);
    this.pd = Math.round(p.delay * sr / 1000);
    this.apG = .07 * p.diffusion;
    for (let i = 0; i < 4; i++){ this.apN[i] = Math.max(1, Math.round(AP_LEN[i] * sr / AP_REF)); if (this.apI[i] >= this.apN[i]) this.apI[i] = 0; }
    const scale = pr.size * (1.25 - .5 * p.density / 100);
    for (let i = 0; i < 8; i++){
      const n = Math.round(FDN_MS[i] * scale * sr / 1000), g = Math.pow(10, -3 * n / (p.time * sr)), gh = Math.pow(10, -3 * n / (p.time * p.hiRatio * sr));
      this.len[i] = n; this.fp[i] = (g - gh) / (g + gh); this.fb[i] = g * (1 - this.fp[i]);
    }
    // tap patterns (deterministic per program)
    const R = rng(101 + p.program), N = pr.type >= 2 ? MAX_TAPS : pr.erTaps;
    let e = 0;
    for (let k = 0; k < N; k++){
      let t, g;
      if (pr.type < 2){ t = pr.erSpan * Math.pow((k + 1) / N, 1.3) * (.85 + .3 * R()); g = 1 - .7 * k / N; }
      else { t = p.gateTime * (k + .8 * R()) / N; g = pr.type === 2 ? (k < .85 * N ? 1 : (N - k) / (.15 * N)) : .08 + .92 * Math.pow(k / N, 1.5); }
      const s = R() < .5 ? -1 : 1, a = R();
      this.tapT[k] = Math.max(1, Math.round(t * sr / 1000));
      this.tapL[k] = s * g * (.3 + .7 * a); this.tapR[k] = s * g * (1 - .7 * a);
      e += g * g;
    }
    const nrm = e > 0 ? 1 / Math.sqrt(e) : 0;
    for (let k = 0; k < N; k++){ this.tapL[k] *= nrm; this.tapR[k] *= nrm; }
    this.nT = N;
  }
  reset(){
    this.inBuf.fill(0); this.dfBuf.fill(0); for (const b of this.apBuf) b.fill(0); for (const b of this.lines) b.fill(0); this.z.fill(0);
    this.aa.reset(); this.hpf.reset(); this.lpf.reset(); this.rcL.reset(); this.rcR.reset(); this.sh = 0; this.oL = this.oR = 0;
  }
  process(x){
    const L = this.L, w = this.w;
    // converter: AA filter, S&H at 31.25 kHz, 16-bit word, then the program's HPF / LPF
    const a = this.aa.run(x);
    this.shPh += this.shInc; if (this.shPh >= 1){ this.shPh -= Math.floor(this.shPh); this.sh = q16(a); }
    this.inBuf[w] = this.lpf.run(this.hpf.run(this.sh));
    let r = w - this.pd; if (r < 0) r += L;
    let d = this.inBuf[r];
    // diffusers (Schroeder allpasses)
    const g = this.apG;
    for (let i = 0; i < 4; i++){
      const b = this.apBuf[i], j = this.apI[i], v = b[j], s = d - g * v;
      b[j] = s; d = v + g * s;
      this.apI[i] = j + 1 >= this.apN[i] ? 0 : j + 1;
    }
    this.dfBuf[w] = d;
    // early reflections / gate cluster
    let eL = 0, eR = 0;
    const gate = this.type >= 2, src = gate ? this.dfBuf : this.inBuf, base = gate ? 0 : this.pd;
    for (let k = 0; k < this.nT; k++){ let j = w - base - this.tapT[k]; if (j < 0) j += L; const s = src[j]; eL += s * this.tapL[k]; eR += s * this.tapR[k]; }
    // FDN
    let fL = 0, fR = 0;
    if (!gate){
      const c = this.lc, sn = this.ls, DL = this.DL, dw = this.dw;
      this.lc = c * this.lcw - sn * this.lsw; this.ls = sn * this.lcw + c * this.lsw;
      let sum = 0;
      for (let i = 0; i < 8; i++){
        const m = ((i & 1) ? sn : c) * ((i & 2) ? -this.depth : this.depth);
        let pos = dw - (this.len[i] + m); if (pos < 0) pos += DL;
        const i0 = Math.floor(pos), f = pos - i0, i1 = i0 + 1 >= DL ? 0 : i0 + 1, B = this.lines[i];
        const y = B[i0] + (B[i1] - B[i0]) * f;
        const z = this.fb[i] * y + this.fp[i] * this.z[i]; this.z[i] = z;
        sum += z; fL += z * FDN_L[i]; fR += z * FDN_R[i];
      }
      const h = sum * .25;
      for (let i = 0; i < 8; i++) this.lines[i][dw] = this.z[i] - h + d * FDN_IN[i] * .5;
      this.dw = dw + 1 >= DL ? 0 : dw + 1;
    }
    this.w = w + 1 >= L ? 0 : w + 1;
    const eg = gate ? this.p.revLevel : this.p.erLevel, rg = this.p.revLevel * .35;
    this.oL = this.rcL.run(q16(eL * eg + fL * rg));
    this.oR = this.rcR.run(q16(eR * eg + fR * rg));
  }
}

/* =====================================================================================================================
   TC 2290-style delay: stereo lines with smoothed (tape-like gliding) time, modulation, loop hi-cut / lo-cut, soft
   limiter inside the feedback loop (up to 120 %), ping-pong, ducking keyed by the dry (send) input, spread and mix.
   ===================================================================================================================== */
export const DLY_SYNC = ['off', '1/4', '1/8', '1/8.', '1/16', '1/4T'];
const SYNC_DIV = [0, 1, .5, .75, .25, 2 / 3];
export const DLY_DEFAULTS = {on:1, timeL:375, timeR:500, sync:0, tempo:120, feedback:35, pingpong:0, hiCut:6000, loCut:120,
  modRate:.6, modDepth:.1, duckThresh:0, duckRelease:300, duckDepth:12, spread:1, mix:.6};

export class TcDelay {
  constructor(sr){
    this.sr = sr; this.L = Math.ceil(2.5 * sr) + 4; this.bL = new Float64Array(this.L); this.bR = new Float64Array(this.L); this.w = 0;
    this.lpL = new OnePole(); this.lpR = new OnePole(); this.hpL = new OnePole(); this.hpR = new OnePole();
    this.lc = 1; this.ls = 0; this.env = 0; this.dg = 1; this.oL = 0; this.oR = 0;
    this.envC = Math.exp(-1 / (.01 * sr)); this.atkC = 1 - Math.exp(-1 / (.002 * sr)); this.smC = 1 - Math.exp(-1 / (.06 * sr));
    this.p = Object.assign({}, DLY_DEFAULTS); this.rebuild(); this.dL = this.tL; this.dR = this.tR;
  }
  set(name, val){
    if (name === 'sync' && typeof val === 'string') val = Math.max(0, DLY_SYNC.indexOf(val));
    if (!RANGES.dly[name] || !Number.isFinite(+val)) return;
    const was = this.p.on;
    this.p[name] = clamp(+val, RANGES.dly[name][0], RANGES.dly[name][1]);
    if (name === 'sync') this.p.sync = Math.round(this.p.sync);
    if (name === 'on' && this.p.on >= .5 && was < .5) this.reset();
    this.rebuild();
  }
  rebuild(){
    const p = this.p, sr = this.sr, sync = SYNC_DIV[p.sync | 0];
    const msL = sync ? 60000 / p.tempo * sync : p.timeL, msR = sync ? 60000 / p.tempo * sync : p.timeR;
    this.depth = p.modDepth * 5 * sr / 1000;          // modDepth 1 = 5 ms
    const mx = this.L - 4 - this.depth;
    this.tL = clamp(msL * sr / 1000, 1, mx); this.tR = clamp(msR * sr / 1000, 1, mx);
    const w = TWO_PI * p.modRate / sr; this.lcw = Math.cos(w); this.lsw = Math.sin(w);
    this.lpL.lp(sr, p.hiCut); this.lpR.lp(sr, p.hiCut); this.hpL.hp(sr, p.loCut); this.hpR.hp(sr, p.loCut);
    this.fbk = p.feedback / 100; this.thr = Math.pow(10, p.duckThresh / 20); this.duckG = Math.pow(10, -p.duckDepth / 20);
    this.relC = 1 - Math.exp(-1 / (p.duckRelease / 1000 * sr));
  }
  reset(){ this.bL.fill(0); this.bR.fill(0); this.lpL.reset(); this.lpR.reset(); this.hpL.reset(); this.hpR.reset(); this.env = 0; this.dg = 1; this.dL = this.tL; this.dR = this.tR; }
  read(B, d){
    let pos = this.w - d; if (pos < 0) pos += this.L;
    const i0 = Math.floor(pos), f = pos - i0, i1 = i0 + 1 >= this.L ? 0 : i0 + 1;
    return B[i0] + (B[i1] - B[i0]) * f;
  }
  process(xL, xR){
    if (this.p.on < .5){ this.oL = 0; this.oR = 0; return; }
    // ducking: peak follower on the dry input, instant attack, 10 ms fall; gain glides down in 2 ms, up over release
    const k = Math.max(Math.abs(xL), Math.abs(xR));
    this.env = k > this.env ? k : this.env * this.envC;
    const tg = this.p.duckThresh < 0 && this.env > this.thr ? this.duckG : 1;
    this.dg += (tg - this.dg) * (tg < this.dg ? this.atkC : this.relC);
    // time glide and modulation
    this.dL += (this.tL - this.dL) * this.smC; this.dR += (this.tR - this.dR) * this.smC;
    const c = this.lc, s = this.ls; this.lc = c * this.lcw - s * this.lsw; this.ls = s * this.lcw + c * this.lsw;
    const yL = this.read(this.bL, this.dL + this.depth * .5 * (1 + s)), yR = this.read(this.bR, this.dR + this.depth * .5 * (1 + c));
    const fL = this.hpL.run(this.lpL.run(yL)) * this.fbk, fR = this.hpR.run(this.lpR.run(yR)) * this.fbk;
    let wL, wR;
    if (this.p.pingpong >= .5){ wL = .5 * (xL + xR) + fR; wR = fL; } else { wL = xL + fL; wR = xR + fR; }
    this.bL[this.w] = Math.tanh(wL); this.bR[this.w] = Math.tanh(wR);
    this.w = this.w + 1 >= this.L ? 0 : this.w + 1;
    const g = this.p.mix * this.dg, m = (yL + yR) * .5, sd = (yL - yR) * .5 * this.p.spread;
    this.oL = (m + sd) * g; this.oR = (m - sd) * g;
  }
}

/* =====================================================================================================================
   Engine: 12 channels x (current + fading slot) -> 50 kHz DAC S&H (run at K x host rate) -> 3-pole 12 kHz
   reconstruction LP -> decimate -> 8 Hz coupling HPF -> op-amp saturation -> fader / pan / sends -> mix + FX returns
   ===================================================================================================================== */
const QMAX = 256;
export class Engine {
  constructor(sr, opts){
    this.sr = sr = sr > 0 ? sr : 48000;
    this.dac = opts && opts.dac === 'float' ? 'float' : 'linear';
    this.K = Math.max(1, Math.round(192000 / sr)); this.dacInc = DAC_SR / (sr * this.K); this.dacPh = 0; this.tk = new Uint8Array(this.K);
    // src: the voice at full resolution (synthesized or user); roms: its quantized ROM words, requantized in place
    this.src = []; this.roms = []; this.user = new Array(NV).fill(null);
    this.recData = new Uint8Array(NV);                                      // 1: the user sample is a recording of a real RX5 (opts.rec)
    for (let v = 0; v < NV; v++){ this.src.push(new Float32Array(0)); this.roms.push(new Float32Array(0)); }   // silent until loaded
    this.cur = []; this.old = []; this.lp1 = []; this.lp2 = []; this.hp = [];
    const fo = sr * this.K, fc = Math.min(12000, .45 * fo);
    for (let ch = 0; ch < NCH; ch++){
      this.cur.push(new Slot()); this.old.push(new Slot());
      this.lp1.push(new OnePole().lp(fo, fc)); this.lp2.push(new Biquad().set('lp', fo, fc, 1)); this.hp.push(new OnePole().hp(sr, 8));
    }
    this.held = new Float64Array(NCH); this.on = new Uint8Array(NCH); this.peak = new Float64Array(NCH);
    this.gC = new Float64Array(NCH); this.pL = new Float64Array(NCH); this.pR = new Float64Array(NCH); this.sRev = new Float64Array(NCH); this.sDly = new Float64Array(NCH);
    this.pkDec = Math.exp(-1 / (.15 * sr));
    this.qT = new Int32Array(QMAX); this.qType = new Int32Array(QMAX); this.qV = new Int32Array(QMAX); this.qVel = new Int32Array(QMAX); this.qAcc = new Int32Array(QMAX); this.qN = 0;
    this.revFx = new Rev7(sr); this.dlyFx = new TcDelay(sr);
    this.ckN = 0; this.ckPh = 0; this.ckW = 0; this.ckEnv = 0; this.ckDec = Math.exp(-1 / (.008 * sr)); this.ckA = 1;
    this.mL = 0; this.mR = 0; this.mtr = {l:0, r:0};
    this.voices = VOICE_DEFAULTS.map(o => Object.assign({}, o));
    this.channels = CHANNEL_DEFAULTS.map(o => Object.assign({}, o));
    this.master = Object.assign({}, MASTER_DEFAULTS);
    for (let v = 0; v < NV; v++) this.requant(v);
    for (let ch = 0; ch < NCH; ch++) this.mixCh(ch);
    this.mixMaster();
  }
  mixCh(ch){
    const c = this.channels[ch], a = (c.pan + 1) * Math.PI / 4;
    this.gC[ch] = c.mute ? 0 : c.fader * c.fader; this.pL[ch] = Math.cos(a); this.pR[ch] = Math.sin(a);
    this.sRev[ch] = c.sendRev; this.sDly[ch] = c.sendDly;
  }
  mixMaster(){ this.volG = this.master.volume * this.master.volume; this.clkG = this.master.click * this.master.click * .5; }

  /* ---------------- control ---------------- */
  push(type, v, vel, acc, time){
    if (this.qN >= QMAX) return;
    const k = this.qN++;
    this.qType[k] = type; this.qV[k] = v; this.qVel[k] = vel; this.qAcc[k] = acc; this.qT[k] = Math.max(0, Math.floor(time || 0));
  }
  trigger(v, vel = 127, accent = 0, time = 0){
    v |= 0; if (v < 0 || v >= NV || !(vel > 0)) return;
    this.push(0, v, clamp(Math.round(vel), 1, 127), clamp(accent | 0, 0, 2), time);
  }
  release(v, time = 0){ v |= 0; if (v >= 0 && v < NV) this.push(1, v, 0, 0, time); }
  clickTick(accented, time = 0){ this.push(2, 0, 0, accented ? 1 : 0, time); }
  fire(k){
    const type = this.qType[k], v = this.qV[k];
    if (type === 0){
      const ch = v % NCH, a = this.cur[ch], b = this.old[ch];
      if (a.active){ b.copyFrom(a); b.fade = 1; }
      a.start(v, this.roms[v], this.voices[v], this.qVel[k], this.qAcc[k], this.isRec(v));
      this.on[ch] = 1;
    } else if (type === 1){
      const a = this.cur[v % NCH];
      if (a.active && a.v === v) a.stage = 3;
    } else {
      const f = this.qAcc[k] ? 2000 : 1000;
      this.ckN = Math.round(.03 * this.sr); this.ckPh = 0; this.ckW = TWO_PI * f / this.sr; this.ckEnv = 1; this.ckA = this.qAcc[k] ? 1 : .6;
    }
  }
  // path 'rec' on a recording: no 12-bit word (the recording already carries the real converter's); otherwise the ROM word
  isRec(v){ return this.voices[v].path === 'rec' && this.recData[v] === 1; }
  requant(v){ if (this.isRec(v)) this.roms[v].set(this.src[v]); else quantizeRom(this.src[v], this.voices[v].bits, this.dac, this.roms[v]); }
  cut(v){ const ch = v % NCH; if (this.cur[ch].v === v) this.cur[ch].active = false; if (this.old[ch].v === v) this.old[ch].active = false; }
  setVoiceParam(v, name, value){
    if (name === 'path' && v >= 0 && v < NV){
      const x = value === 'rec' ? 'rec' : 'rx';
      if (x !== this.voices[v].path){ this.voices[v].path = x; this.cut(v); this.requant(v); }
      return;
    }
    const r = RANGES.voice[name];
    if (!r || v < 0 || v >= NV || !Number.isFinite(+value)) return;
    let x = clamp(+value, r[0], r[1]);
    if (name === 'gate' && x > 0) x = Math.max(100, x);                 // 0 = off, else 0.1..6.5 s
    if (name === 'bits'){ x = x < 10 ? 8 : 12; if (x !== this.voices[v].bits){ this.voices[v].bits = x; this.cut(v); this.requant(v); } return; }
    this.voices[v][name] = x;
  }
  setChannel(ch, o){
    if (ch < 0 || ch >= NCH || !o) return;
    for (const k in o){
      if (k === 'mute'){ this.channels[ch].mute = !!o[k] && o[k] !== '0'; continue; }
      if (RANGES.channel[k] && Number.isFinite(+o[k])) this.channels[ch][k] = clamp(+o[k], RANGES.channel[k][0], RANGES.channel[k][1]);
    }
    this.mixCh(ch);
  }
  // playback converter reading: 'linear' (default) or 'float' (block floating point); requantizes every ROM
  setDac(mode){ this.dac = mode === 'float' ? 'float' : 'linear'; for (let v = 0; v < NV; v++){ this.cut(v); this.requant(v); } }
  setTempo(bpm){ this.dlyFx.set('tempo', bpm); }
  setFx(which, name, value){ if (which === 'rev') this.revFx.set(name, value); else if (which === 'dly') this.dlyFx.set(name, typeof value === 'boolean' ? +value : value); }
  setMaster(o){
    if (!o) return;
    for (const k in o) if (RANGES.master[k] && Number.isFinite(+o[k])) this.master[k] = clamp(+o[k], 0, 1);
    this.mixMaster();
  }
  // user sample: linear resample to ROM_SR, quantized like the factory ROM (voice bits, DAC mode); null restores the synthesized voice.
  // opts.rec: the data is a recording of a real RX5 (already through its DAC, filter and output stage, ideally at 25 kHz): the voice's
  // path is set to 'rec' (no 12-bit requantize, no output-stage saturation; still the 50 kHz ZOH + reconstruction filter), and the
  // ROM copy gets REC_EQ, which undoes this engine's playback droop (linear interpolation 25 -> 50 kHz, ZOH, 12 kHz filter) so the
  // recording comes out with its own response at pitch 0. userSample(v) returns the data before REC_EQ. Without opts.rec: path 'rx'.
  setUserSample(v, data, sr, opts){
    if (v < 0 || v >= NV) return;
    this.cut(v);
    const rec = !!(opts && opts.rec) && !!data && data.length >= 2;
    this.recData[v] = rec ? 1 : 0; this.voices[v].path = rec ? 'rec' : 'rx';
    if (!data || data.length < 2){ this.user[v] = null; this.src[v] = new Float32Array(0); }
    else {
      const n = Math.max(2, Math.floor((data.length - 1) * ROM_SR / sr) + 1), out = new Float32Array(n), step = sr / ROM_SR;
      for (let k = 0; k < n; k++){
        const x = k * step, i = Math.floor(x), f = x - i, a = data[i], b = i + 1 < data.length ? data[i + 1] : a;
        out[k] = clamp(a + (b - a) * f, -1, 1);
      }
      this.user[v] = out; this.src[v] = rec ? recEq(out) : out;
    }
    this.roms[v] = new Float32Array(this.src[v].length); this.requant(v);
  }
  userSample(v){ return this.user[v] || null; }
  voiceActive(ch){ return Math.min(1, this.peak[ch] * 2.5); }
  meters(){ this.mtr.l = this.mL; this.mtr.r = this.mR; this.mL = 0; this.mR = 0; return this.mtr; }
  getState(){
    return {voices:this.voices.map(o => Object.assign({}, o)), channels:this.channels.map(o => Object.assign({}, o)),
      rev:Object.assign({}, this.revFx.p), dly:Object.assign({}, this.dlyFx.p), master:Object.assign({}, this.master), dac:this.dac};
  }
  setState(s){
    if (!s) return;
    if (s.dac && s.dac !== this.dac) this.setDac(s.dac);
    if (Array.isArray(s.voices)) s.voices.forEach((o, v) => { if (o) for (const k in o) this.setVoiceParam(v, k, o[k]); });
    if (Array.isArray(s.channels)) s.channels.forEach((o, ch) => this.setChannel(ch, o));
    if (s.rev){ if ('program' in s.rev) this.revFx.set('program', s.rev.program); for (const k in s.rev) if (k !== 'program') this.revFx.set(k, s.rev[k]); }
    if (s.dly) for (const k in s.dly) this.setFx('dly', k, s.dly[k]);
    if (s.master) this.setMaster(s.master);
  }

  /* ---------------- audio ---------------- */
  // per channel: K oversampled DAC sub-steps (S&H update on DAC ticks, 3-pole LP), then HPF and the output stage
  channel(ch){
    const a = this.cur[ch], b = this.old[ch], f1 = this.lp1[ch], f2 = this.lp2[ch], tk = this.tk;
    let h = this.held[ch], y = 0;
    for (let k = 0; k < this.K; k++){
      if (tk[k]){
        h = 0;
        if (a.active) h += a.tick();
        if (b.active){ h += b.tick() * b.fade; b.fade -= FADE_STEP; if (b.fade <= 0) b.active = false; }
      }
      y = f2.run(f1.run(h));
    }
    this.held[ch] = h;
    // op-amp output stage, rails at +-OUT_K:  o = OUT_K * u / (1 + |u|^2.5)^0.4,  u = x / OUT_K
    // (a 'rec' voice already went through a real output stage: the latest note's slot decides, a.rec)
    const u = this.hp[ch].run(y) / OUT_K, au = Math.abs(u);
    const o = a.rec ? OUT_K * u : OUT_K * u / Math.pow(1 + au * au * Math.sqrt(au), .4);
    if (!a.active && !b.active && h === 0){
      const hp = this.hp[ch];
      if (Math.abs(f1.x1) + Math.abs(f1.y1) + Math.abs(f2.z1) + Math.abs(f2.z2) + Math.abs(hp.x1) + Math.abs(hp.y1) < 1e-9){
        f1.reset(); f2.reset(); hp.reset(); this.on[ch] = 0;
      }
    }
    return o;
  }
  process(outL, outR, n){
    const rv = this.revFx, dl = this.dlyFx, K = this.K;
    for (let i = 0; i < n; i++){
      // 1. events due at this sample, in the order they were queued
      if (this.qN) for (let k = 0; k < this.qN; k++) if (this.qT[k] === i) this.fire(k);
      // 2. the shared 50 kHz DAC clock over the K sub-steps
      for (let k = 0; k < K; k++){ this.dacPh += this.dacInc; if (this.dacPh >= 1){ this.dacPh -= 1; this.tk[k] = 1; } else this.tk[k] = 0; }
      // 3. channels -> dry bus and sends
      let dL = 0, dR = 0, sRv = 0, sDL = 0, sDR = 0;
      for (let ch = 0; ch < NCH; ch++){
        let o = 0;
        if (this.on[ch]){
          o = this.channel(ch) * this.gC[ch];
          const l = o * this.pL[ch], r = o * this.pR[ch];
          dL += l; dR += r; sRv += o * this.sRev[ch]; sDL += l * this.sDly[ch]; sDR += r * this.sDly[ch];
        }
        const ao = Math.abs(o); this.peak[ch] = ao > this.peak[ch] ? ao : this.peak[ch] * this.pkDec;
      }
      // 4. effects
      rv.process(sRv); dl.process(sDL, sDR);
      // 5. metronome click
      let ck = 0;
      if (this.ckN > 0){ ck = Math.sin(this.ckPh) * this.ckEnv * this.ckA * this.clkG; this.ckPh += this.ckW; this.ckEnv *= this.ckDec; this.ckN--; }
      // 6. master
      const L = (dL + rv.oL * rv.p.level + dl.oL) * this.volG + ck, R = (dR + rv.oR * rv.p.level + dl.oR) * this.volG + ck;
      outL[i] = L; outR[i] = R;
      const aL = Math.abs(L), aR = Math.abs(R); if (aL > this.mL) this.mL = aL; if (aR > this.mR) this.mR = aR;
    }
    // carry later events into the next block
    if (this.qN){
      let m = 0;
      for (let k = 0; k < this.qN; k++) if (this.qT[k] >= n){
        this.qT[m] = this.qT[k] - n; this.qType[m] = this.qType[k]; this.qV[m] = this.qV[k]; this.qVel[m] = this.qVel[k]; this.qAcc[m] = this.qAcc[k]; m++;
      }
      this.qN = m;
    }
  }
}
