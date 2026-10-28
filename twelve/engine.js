/* Omega Twelve sound engine: vintage sampler models, lo-fi, timestretch, master chain and analysis.
   Plain JS with no DOM, used by the app's Web Worker, by Node tests and by tools/gen.mjs (which writes the
   parameter and preset tables for the C++ plugin, plugins/twelve/Twelve.h, a line-for-line port of this file).

   SP-1200 audio path after libmd12 (MIT, github.com/Mudb0y/libmd12): 26,041.67 Hz frame clock (20 MHz / 768),
   7th-order input anti-alias filter, truncating 12-bit converter, drop/repeat pitch with a 7-bit fraction counter
   and E-mu's semitone table, outputs 1-2 through an SSM2044 4-pole dynamic filter, 3-6 through fixed filters
   (poles and zeros in rad/s), 7-8 unfiltered. */
(function (root) {
'use strict';

const SP_SR = 20e6 / 768;
const SP_TUNE = [50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255];
const PROTO = {
  input:{poles:[[-3.1645569620e4,0],[-2.3809523810e4,4.6628955649e4],[-1.1574074074e4,7.4291800562e4],[-3.0864197531e3,8.2586975770e4],[-1.9405594406e5,0]],
         zeros:[[1.4005991967e1,1.0385249071e5],[2.2080491445e1,1.2148212766e5],[-7.1836348348e1,1.4125368878e5]]},
  3:{poles:[[-4.6549557989e4,0],[-1.6129032258e4,4.8387096774e4],[-4.2413850609e4,3.0520415770e4]], zeros:[]},
  4:{poles:[[-5.5372960884e4,0],[-1.9607843137e4,5.8823529412e4],[-5.1403645657e4,3.7527098089e4]], zeros:[]},
  5:{poles:[[-7.3190092409e4,0],[-2.3255813953e4,6.9767441860e4],[-6.1051842833e4,4.2379116726e4]], zeros:[]},
  6:{poles:[[-6.0667642111e4,4.8266457976e4],[-2.5641025641e4,7.6923076923e4],[-9.5952540933e4,0]], zeros:[]}
};

// pitch: 0 SP drop/repeat counter, 1 zero-order hold (variable clock), 2 linear, 3 cubic (clean)
// lo / hi: the machine's own sample-rate range (published specs); the Rate knob is held inside it
const MACHINES = [
  {name:'SP-1200',  rate:SP_SR, bits:12, pitch:0, ftype:0,  cutoff:1000,  res:.3,  lo:4000,  hi:SP_SR},
  {name:'SP-12',    rate:27500, bits:12, pitch:0, ftype:0,  cutoff:1300,  res:.3,  lo:4000,  hi:27500},
  {name:'MPC60',    rate:40000, bits:12, pitch:2, ftype:5,  cutoff:16000, res:0,   lo:4000,  hi:40000},
  {name:'S950',     rate:31250, bits:12, pitch:1, ftype:6,  cutoff:11000, res:0,   lo:7500,  hi:48000},
  {name:'S612',     rate:32000, bits:12, pitch:1, ftype:7,  cutoff:9000,  res:.1,  lo:4000,  hi:32000},
  {name:'Mirage',   rate:29411, bits:8,  pitch:1, ftype:12, cutoff:7000,  res:.25, lo:10000, hi:33000},
  {name:'Clean',    rate:48000, bits:16, pitch:3, ftype:5,  cutoff:20000, res:0,   lo:4000,  hi:52100},
  {name:'MPC3000',  rate:44100, bits:16, pitch:2, ftype:8,  cutoff:18000, res:0,   lo:4000,  hi:44100},
  {name:'S1000',    rate:44100, bits:16, pitch:3, ftype:9,  cutoff:18000, res:0,   lo:22050, hi:44100},
  {name:'RZ-1',     rate:20000, bits:8,  pitch:1, ftype:5,  cutoff:20000, res:0,   lo:4000,  hi:20000},
  {name:'SK-1',     rate:9380,  bits:8,  pitch:1, ftype:5,  cutoff:20000, res:0,   lo:4000,  hi:9380},
  {name:'Emax',     rate:27778, bits:12, pitch:1, ftype:10, cutoff:12000, res:.1,  lo:10000, hi:42000},
  {name:'EPS',      rate:31250, bits:13, pitch:2, ftype:11, cutoff:16000, res:0,   lo:6250,  hi:52100},
  {name:'EPS 16+',  rate:44600, bits:16, pitch:2, ftype:11, cutoff:18000, res:0,   lo:11200, hi:44600},
  {name:'DSS-1',    rate:48000, bits:12, pitch:1, ftype:13, cutoff:12000, res:.15, lo:16000, hi:48000},
  {name:'S-550',    rate:30000, bits:12, pitch:2, ftype:14, cutoff:10000, res:0,   lo:15000, hi:30000},
  {name:'SU700',    rate:44100, bits:16, pitch:2, ftype:15, cutoff:18000, res:0,   lo:11025, hi:44100},
  {name:'TX16W',    rate:33333, bits:12, pitch:1, ftype:5,  cutoff:20000, res:0,   lo:16700, hi:50000}
];
const FILTERS = ['SSM2044 (out 1-2)', 'Fixed (out 3)', 'Fixed (out 4)', 'Fixed (out 5)', 'Fixed (out 6)', 'None (out 7-8)', 'S950 6-pole', 'S612 4-pole',
  'MPC3000 2-pole', 'S1000 3-pole', 'Emax 4-pole', 'EPS 4-pole', 'Mirage 4-pole', 'DSS-1 4-pole', 'S-550 2-pole', 'SU700 4-pole'];
// each filter's cutoff range (the hardware's, from service manuals and data sheets where published)
const FILTER_RANGE = [[250, 14000], [20, 20000], [20, 20000], [20, 20000], [20, 20000], [20, 20000], [350, 16000], [50, 16000],
  [70, 20000], [20, 20000], [40, 16000], [20, 20000], [50, 14000], [30, 16000], [100, 10000], [20, 20000]];

// id, name, min, max, default, step, unit, group; choices for menus; log for frequency-like knobs
const P = (id, name, min, max, def, step, unit, group, extra) => Object.assign({id, name, min, max, def, step, unit, group}, extra || {});
const PARAMS = [
  P('machine', 'Sampler', 0, 6, 0, 1, '', 'sampler', {choices:MACHINES.map(m => m.name)}),
  P('bits', 'Bits', 2, 16, 12, 1, 'bit', 'sampler'),
  P('rate', 'Rate', 4000, 52100, SP_SR, 0, 'Hz', 'sampler', {log:1}),
  P('alias', 'Unfiltered', 0, 1, 0, 0, '%', 'sampler'),
  P('drive', 'Drive', 0, 1, 0, 0, '%', 'sampler'),
  P('mono', 'Mono', 0, 1, 1, 1, '', 'sampler', {toggle:1}),
  P('rpm', 'Alias', 0, 30, 0, .1, 'st', 'sampler'),
  P('pre', 'Preamp', 0, 1, 1, 1, '', 'sampler', {toggle:1}),
  P('calib', 'Calibration', 0, 1, 1, 1, '', 'sampler', {choices:['Service manual', 'Measured SP-12']}),
  P('ftype', 'Filter', 0, 15, 0, 1, '', 'filter', {choices:FILTERS}),
  P('fOn', 'Filter on', 0, 1, 1, 1, '', 'filter', {toggle:1}),
  P('cutoff', 'Cutoff', 100, 20000, 1000, 0, 'Hz', 'filter', {log:1}),
  P('res', 'Resonance', 0, 1, .3, 0, '%', 'filter'),
  P('fenv', 'Filter env', 0, 1, 1, 0, '%', 'filter'),
  P('retrig', 'Retrigger', 0, 1, 1, 1, '', 'filter', {toggle:1}),
  P('mix', 'Mix', 0, 1, 1, 0, '%', 'filter'),
  P('semis', 'Tune', -24, 24, 0, 1, 'st', 'tune'),
  P('cents', 'Fine', -100, 100, 0, 1, 'ct', 'tune'),
  P('a4', 'A4 ref', 415, 466, 440, .1, 'Hz', 'tune'),
  P('stretch', 'Stretch', .25, 5, 1, 0, 'x', 'time', {log:1}),
  P('grain', 'Grain', .5, 200, 60, 0, 'ms', 'time', {log:1}),
  P('smode', 'Stretch mode', 0, 2, 0, 1, '', 'time', {choices:['Smooth', 'Cyclic Classic', 'Cyclic Warp']}),
  P('crackle', 'Crackle', 0, 1, 0, 0, '%', 'lofi'),
  P('hiss', 'Tape hiss', 0, 1, 0, 0, '%', 'lofi'),
  P('wow', 'Wow', 0, 1, 0, 0, '%', 'lofi'),
  P('flutter', 'Flutter', 0, 1, 0, 0, '%', 'lofi'),
  P('tape', 'Saturation', 0, 1, 0, 0, '%', 'lofi'),
  P('age', 'Vinyl age', 0, 1, 0, 0, '%', 'lofi'),
  P('hum', 'Hum', 0, 1, 0, 0, '%', 'lofi'),
  P('humHz', 'Mains', 0, 1, 1, 1, '', 'lofi', {choices:['50 Hz', '60 Hz']}),
  P('input', 'Input', -24, 24, 0, .1, 'dB', 'io'),
  P('output', 'Output', -24, 24, 0, .1, 'dB', 'io'),
  P('link', 'Link in/out', 0, 1, 0, 1, '', 'io', {toggle:1}),
  P('bypass', 'Bypass', 0, 1, 0, 1, '', 'io', {toggle:1}),
  P('mOn', 'Master', 0, 1, 1, 1, '', 'master', {toggle:1}),
  P('lsF', 'Low freq', 20, 500, 90, 0, 'Hz', 'master', {log:1}),
  P('lsG', 'Low', -12, 12, 0, .1, 'dB', 'master'),
  P('p1F', 'Band 1 freq', 40, 1000, 250, 0, 'Hz', 'master', {log:1}),
  P('p1G', 'Band 1', -12, 12, 0, .1, 'dB', 'master'),
  P('p1Q', 'Band 1 Q', .3, 8, 1, 0, '', 'master', {log:1}),
  P('p2F', 'Band 2 freq', 200, 5000, 1200, 0, 'Hz', 'master', {log:1}),
  P('p2G', 'Band 2', -12, 12, 0, .1, 'dB', 'master'),
  P('p2Q', 'Band 2 Q', .3, 8, 1, 0, '', 'master', {log:1}),
  P('p3F', 'Band 3 freq', 1000, 16000, 5000, 0, 'Hz', 'master', {log:1}),
  P('p3G', 'Band 3', -12, 12, 0, .1, 'dB', 'master'),
  P('p3Q', 'Band 3 Q', .3, 8, 1, 0, '', 'master', {log:1}),
  P('hsF', 'High freq', 2000, 18000, 10000, 0, 'Hz', 'master', {log:1}),
  P('hsG', 'High', -12, 12, 0, .1, 'dB', 'master'),
  P('match', 'EQ match', 0, 1, 0, 0, '%', 'master'),
  P('cThr', 'Threshold', -40, 0, 0, .1, 'dB', 'master'),
  P('cRat', 'Ratio', 1, 10, 2, 0, ':1', 'master'),
  P('cAtk', 'Attack', .1, 100, 10, 0, 'ms', 'master', {log:1}),
  P('cRel', 'Release', 20, 1000, 150, 0, 'ms', 'master', {log:1}),
  P('cMake', 'Makeup', 0, 24, 0, .1, 'dB', 'master'),
  P('mGain', 'Gain', -12, 24, 0, .1, 'dB', 'master'),
  P('lim', 'Ceiling', -12, 0, -1, .1, 'dB', 'master')
];
const MASTER_IDS = PARAMS.filter(p => p.group === 'master').map(p => p.id);
const MATCH_BANDS = [31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];

function defaults(){ const o = {}; for (const p of PARAMS) o[p.id] = p.def; return o; }
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const dbToGain = d => Math.pow(10, d / 20);

/* ---------------- random numbers (same generator as the C++ port) ---------------- */
function rng(seed){
  let s = seed >>> 0;
  return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

/* ---------------- filters ---------------- */
// one biquad section, transposed direct form II
function bq(b0, b1, b2, a1, a2){ return {b0, b1, b2, a1, a2, z1:0, z2:0}; }
function bqRun(f, x){ const y = f.b0 * x + f.z1; f.z1 = f.b1 * x - f.a1 * y + f.z2; f.z2 = f.b2 * x - f.a2 * y; return y; }
// RBJ cookbook designs
function rbj(type, fs, f, q, gdb){
  const w = 2 * Math.PI * clamp(f, 1, fs * .49) / fs, cw = Math.cos(w), sw = Math.sin(w), al = sw / (2 * q), A = Math.pow(10, (gdb || 0) / 40);
  let b0, b1, b2, a0, a1, a2;
  if (type === 'lp'){ b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
  else if (type === 'hp'){ b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
  else if (type === 'peak'){ b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
  else if (type === 'ls'){ const s = 2 * Math.sqrt(A) * al; b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; }
  else { const s = 2 * Math.sqrt(A) * al; b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; }
  return bq(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
}
// analog poles/zeros (rad/s), optionally scaled, to digital sections (bilinear, pre-warped, unity gain at DC)
function sDesign(proto, fs, scale){
  const k0 = scale || 1, K = 2 * fs;
  const warp = ([re, im]) => { re *= k0; im *= k0; const w = Math.hypot(re, im); if (w > .9 * Math.PI * fs) return [re, im]; const k = K * Math.tan(w / K) / w; return [re * k, im * k]; };
  const quad = (c1, c0) => [K * K + c1 * K + c0, 2 * c0 - 2 * K * K, K * K - c1 * K + c0];
  const mag = p => Math.hypot(p[0], p[1]);
  const zs = proto.zeros.map(warp).sort((a, b) => mag(a) - mag(b));
  const secs = [];
  for (const [re, im] of proto.poles.map(warp).sort((a, b) => mag(a) - mag(b))){
    let b, a;
    if (im > 0){ a = quad(-2 * re, re * re + im * im); const z = zs.shift(); b = z ? quad(-2 * z[0], z[0] * z[0] + z[1] * z[1]) : [1, 2, 1]; }
    else { a = [K - re, -re - K, 0]; b = [1, 1, 0]; }
    const dc = (b[0] + b[1] + b[2]) / (a[0] + a[1] + a[2]);
    secs.push(bq(b[0] / dc / a[0], b[1] / dc / a[0], b[2] / dc / a[0], a[1] / a[0], a[2] / a[0]));
  }
  return secs;
}
function chainRun(secs, x){ for (let k = 0; k < secs.length; k++) x = bqRun(secs[k], x); return x; }

/* ---------------- converter: anti-alias filter, sample and hold at the machine clock, truncating quantizer ---------------- */
function quantize(v, bits){
  const q = Math.pow(2, bits - 1);
  return clamp(Math.floor(v * q), -q, q - 1) / q;
}
// input filter for a machine: the SP-1200 prototype, scaled to the machine's clock
function inputFilter(rate, fs){ return sDesign(PROTO.input, fs, rate / SP_SR); }
function driveGain(p){ return dbToGain(p.input) * (1 + p.drive * 1.5); }
// the input preamp: clean up to its knee, then a soft clip that comes in a little earlier on the negative side
function preamp(x){ const a = Math.abs(x), k = x >= 0 ? .9 : .85; if (a <= k) return x; const h = 1 - k; return Math.sign(x) * (k + h * Math.tanh((a - k) / h)); }
// what the machine actually does with the knobs: its rate range, the filter's cutoff range, the filter switch
function effective(p){
  const M = MACHINES[p.machine | 0] || MACHINES[0], q = Object.assign({}, p);
  q.rate = clamp(p.rate, M.lo, M.hi);
  if (p.fOn != null && p.fOn < .5) q.ftype = 5;
  const R = FILTER_RANGE[q.ftype | 0] || FILTER_RANGE[0]; q.cutoff = clamp(p.cutoff, R[0], R[1]);
  return q;
}
// the source through the input gain, drive and (if on) the preamp, ready for the converter at unity level
const preCache = [];
function preamped(src, p){
  const g = driveGain(p), on = p.pre > .5;
  for (const c of preCache) if (c.src === src && c.g === g && c.on === on) return c.out;
  const out = new Float32Array(src.length);
  for (let i = 0; i < src.length; i++){ const x = src[i] * g; out[i] = on ? preamp(x) : x; }
  preCache.unshift({src, g, on, out}); if (preCache.length > 4) preCache.pop();
  return out;
}

// the sampler's stored sample: filter, sample at `rate`, quantize (one channel)
function sampleBase(src, sr, p, unity){
  const rate = p.rate, aa = inputFilter(rate, sr), a = p.alias, g = unity ? 1 : driveGain(p);
  const m = new Float32Array(src.length);
  for (let i = 0; i < src.length; i++){ const x = src[i] * g, f = chainRun(aa, x); m[i] = f + (x - f) * a; }
  const n = Math.max(1, Math.floor(src.length * rate / sr)), out = new Float32Array(n), step = sr / rate;
  for (let k = 0; k < n; k++){
    const x = k * step, i = Math.floor(x), f = x - i;
    out[k] = quantize((m[i] || 0) * (1 - f) + (m[i + 1] || 0) * f, p.bits);
  }
  return out;
}

// the converter for a live input: same path, streaming, holding each sample until the next clock
class Converter {
  constructor(fs){ this.fs = fs; this.ph = 1; this.prev = 0; this.hold = 0; this.aa = []; this.rate = 0; }
  setup(p){
    // the alias trick on live input: sampled as if sped up, so the converter runs slower by that ratio
    const r = p.rate / Math.pow(2, (p.rpm || 0) / 12);
    if (r !== this.rate){ this.rate = r; this.aa = inputFilter(r, this.fs); }
    this.inc = Math.min(1, r / this.fs); this.alias = p.alias; this.bits = p.bits; this.g = driveGain(p); this.pre = p.pre > .5;
  }
  process(x){
    x *= this.g; if (this.pre) x = preamp(x);
    const f = chainRun(this.aa, x), v = f + (x - f) * this.alias;
    this.ph += this.inc;
    if (this.ph >= 1){ this.ph -= 1; const t = 1 - this.ph / this.inc; this.hold = quantize(this.prev + (v - this.prev) * clamp(t, 0, 1), this.bits); }
    this.prev = v;
    return this.hold;
  }
}

/* ---------------- pitch: how each machine plays its stored sample back ---------------- */
function totalCents(p, extraSemis){ return ((p.semis + (extraSemis || 0)) * 100 + p.cents + 1200 * Math.log2(p.a4 / 440)); }
// SP: the 7-bit fraction increment. On the hardware's own semitones (-16..+15, no fine tune) it is E-mu's table.
function spInc(cents){
  const st = cents / 100;
  if (Math.abs(st - Math.round(st)) < 1e-6 && st >= -16 && st <= 15) return SP_TUNE[Math.round(st) + 16] + 1;
  return clamp(Math.round(128 * Math.pow(2, cents / 1200)), 1, 1024);
}
// play `base` (at p.rate) back at host rate fs; returns a Float32Array
function playBase(base, p, fs, mode, cents){
  const rate = p.rate, out = [];
  if (mode === 0){
    const inc = spInc(cents), frames = Math.floor(base.length * 128 / inc), n = Math.max(1, Math.ceil(frames * fs / rate)), d = new Float32Array(n), step = rate / fs;
    let kp = 0;
    for (let i = 0; i < n; i++, kp += step){ const idx = (Math.floor(kp) * inc) >> 7; d[i] = idx < base.length ? base[idx] : 0; }
    return d;
  }
  const ratio = Math.pow(2, cents / 1200), step = rate * ratio / fs, n = Math.max(1, Math.ceil(base.length / step)), d = new Float32Array(n);
  let pos = 0;
  for (let i = 0; i < n; i++, pos += step){
    const k = Math.floor(pos), f = pos - k;
    if (mode === 1) d[i] = base[k] || 0;
    else if (mode === 2) d[i] = (base[k] || 0) * (1 - f) + (base[k + 1] || 0) * f;
    else d[i] = cubic(base, k, f);
  }
  return d;
}
function cubic(d, k, f){
  const y0 = d[k - 1] || 0, y1 = d[k] || 0, y2 = d[k + 1] || 0, y3 = d[k + 2] || 0;
  const c1 = .5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - .5 * y3, c3 = .5 * (y3 - y0) + 1.5 * (y1 - y2);
  return ((c3 * f + c2) * f + c1) * f + y1;
}
// the dry signal for Mix: the source at the same pitch, clean
function playDry(src, sr, fs, cents, n){
  const step = sr / fs * Math.pow(2, cents / 1200), d = new Float32Array(n);
  let pos = 0;
  for (let i = 0; i < n; i++, pos += step){ const k = Math.floor(pos); d[i] = k + 2 < src.length ? cubic(src, k, pos - k) : (src[k] || 0); }
  return d;
}

/* ---------------- output filters ---------------- */
function onePoleLp(fs, f){ const K = Math.tan(Math.PI * clamp(f, 1, fs * .49) / fs); return bq(K / (1 + K), K / (1 + K), 0, (K - 1) / (K + 1), 0); }
class OutFilter {
  constructor(fs){ this.fs = fs; this.type = -1; this.age = 1e9; this.lad = [0, 0, 0, 0]; this.secs = []; this.fe = 0; this.fs2 = 0; this.ss = 0; this.last = 0; this.cut = 0; this.resv = 0; }
  setup(p){
    const t = p.ftype | 0, fs = this.fs;
    if (t !== this.type || p.cutoff !== this.cut){
      this.type = t; this.cut = p.cutoff;
      if (t >= 1 && t <= 4) this.secs = sDesign(PROTO[t + 2], fs);
      else if (t === 6) this.secs = [.5176, .7071, 1.9319].map(q => rbj('lp', fs, p.cutoff, q));
      else this.secs = [];
    }
    // the digital filters: resonance raises the last section's Q
    if (t === 8 || t === 14) this.secs = [rbj('lp', fs, p.cutoff, .7071 + p.res * 9)];
    else if (t === 9) this.secs = [onePoleLp(fs, p.cutoff), rbj('lp', fs, p.cutoff, 1 + p.res * 8)];
    else if (t === 11 || t === 15) this.secs = [rbj('lp', fs, p.cutoff, .5412), rbj('lp', fs, p.cutoff, 1.3066 + p.res * (t === 15 ? 10 : 7))];
    this.drive = t === 10 ? 1 : t === 12 ? .9 : t === 13 ? 1.25 : .7;
    this.resv = p.res; this.env = p.fenv; this.retrig = p.retrig > .5;
    this.tau = Math.exp(-1 / (.075 * fs)); this.trigLen = .0094 * fs;
  }
  trigger(){ this.age = 0; }
  // SSM2044 / resonant ladder: nonlinear 4-pole transistor ladder, 2x oversampled
  ladder(x, fc){
    const fo = this.fs * 2, g = 1 - Math.exp(-2 * Math.PI * Math.min(fc, .45 * fo) / fo), l = this.lad, r = this.resv, dr = this.drive;
    for (let o = 0; o < 2; o++){
      const u = Math.tanh(x * dr - 4 * r * l[3]);
      const t0 = Math.tanh(l[0]), t1 = Math.tanh(l[1]), t2 = Math.tanh(l[2]);
      l[0] += g * (u - t0); l[1] += g * (t0 - t1); l[2] += g * (t1 - t2); l[3] += g * (t2 - Math.tanh(l[3]));
    }
    return l[3] / dr * (1 + 2 * r);
  }
  // transient detector for retriggering on hits inside a loop or a live input
  detect(x){
    const a = Math.abs(x);
    this.fe = a > this.fe ? a : this.fe * .995;
    this.ss += (a - this.ss) * .0005;
    this.since++;
    if (this.fe > .05 && this.fe > this.ss * 4 && this.since > .08 * this.fs){ this.since = 0; return true; }
    return false;
  }
  process(x){
    const t = this.type;
    if (t === 5) return x;
    if (t === 0 || t === 7){
      if (this.retrig && this.detect(x)) this.age = 0;
      const a = this.age++, open = a < this.trigLen ? 1 : Math.pow(this.tau, a - this.trigLen);
      let fc;
      if (t === 0){ const top = this.cut + (13000 - this.cut) * this.env; fc = this.cut + (top - this.cut) * open; }
      else fc = this.cut * Math.pow(2, this.env * 3 * open);
      return this.ladder(x, fc);
    }
    // the other analog 4-poles (Emax, Mirage, DSS-1): the same nonlinear ladder, each with its own drive, no sweep
    if (t === 10 || t === 12 || t === 13) return this.ladder(x, this.cut);
    return chainRun(this.secs, x);
  }
}
OutFilter.prototype.since = 1e9;

/* ---------------- lo-fi: crackle, hiss, wow and flutter, saturation, vinyl age, hum ---------------- */
class Lofi {
  constructor(fs, seed){
    this.fs = fs; this.r = rng(seed || 12345);
    this.dl = [new Float32Array(Math.ceil(fs * .05)), new Float32Array(Math.ceil(fs * .05))]; this.w = 0;
    this.ph = [0, 0, 0]; this.fn = 0; this.click = 0; this.clickLp = 0; this.surf = 0; this.hp = [0, 0];
    this.sat = [0, 0]; this.t = 0;
  }
  setup(p){
    const fs = this.fs;
    this.p = p;
    this.ageHp = [0, 1].map(() => rbj('hp', fs, 20 + p.age * 140, .707));
    this.ageLp = [0, 1].map(() => rbj('lp', fs, 19000 - p.age * 13500, .707));
    this.tapeLp = [0, 1].map(() => rbj('lp', fs, 20000 - p.tape * 9000, .6));
    this.bump = [0, 1].map(() => rbj('peak', fs, 90, 1, p.tape * 3));
    this.hissF = [0, 1].map(() => rbj('hp', fs, 1800, .5));
    this.on = p.crackle + p.hiss + p.wow + p.flutter + p.tape + p.age + p.hum > 0;
  }
  noise(){ return this.r() * 2 - 1; }
  process(l, r){
    const p = this.p, fs = this.fs;
    if (!this.on) return [l, r];
    // wow (slow, 0.55 Hz) and flutter (fast, ~7 Hz with a little randomness) move a short delay line
    if (p.wow > 0 || p.flutter > 0){
      this.ph[0] += .55 / fs; this.ph[1] += 7.1 / fs; this.fn += (this.noise() - this.fn) * .001;
      const wowD = p.wow * .0032 * fs, flD = p.flutter * .00012 * fs;
      const d = 2 + wowD * (1 + Math.sin(2 * Math.PI * this.ph[0])) + flD * (1 + Math.sin(2 * Math.PI * this.ph[1]) * .8 + this.fn * 4);
      const N = this.dl[0].length, w = this.w;
      this.dl[0][w] = l; this.dl[1][w] = r;
      const rp = w - d, i = Math.floor(rp), f = rp - i, i0 = (i % N + N) % N, i1 = (i0 + 1) % N;
      l = this.dl[0][i0] * (1 - f) + this.dl[0][i1] * f; r = this.dl[1][i0] * (1 - f) + this.dl[1][i1] * f;
      this.w = (w + 1) % N;
    }
    if (p.tape > 0){
      const k = 1 + p.tape * 4, n = Math.tanh(k);
      l = Math.tanh(bqRun(this.bump[0], l) * k) / n; r = Math.tanh(bqRun(this.bump[1], r) * k) / n;
      l = bqRun(this.tapeLp[0], l); r = bqRun(this.tapeLp[1], r);
    }
    if (p.age > 0){
      l = bqRun(this.ageLp[0], bqRun(this.ageHp[0], l)); r = bqRun(this.ageLp[1], bqRun(this.ageHp[1], r));
      const m = (l + r) * .5, s = p.age * .6; l += (m - l) * s; r += (m - r) * s;
    }
    if (p.crackle > 0){
      // surface noise, ticks and the odd pop
      this.surf += (this.noise() - this.surf) * .05;
      let c = this.surf * .004 * p.crackle;
      if (this.r() < p.crackle * p.crackle * 40 / fs){ const a = this.r(); this.click += (this.r() < .5 ? -1 : 1) * a * a * a * .9; }
      if (this.r() < p.crackle * .4 / fs) this.click += (this.r() < .5 ? -1 : 1) * .6;
      this.clickLp += (this.click - this.clickLp) * .45; c += this.clickLp * p.crackle; this.click *= .6;
      l += c; r += c * .92;
    }
    if (p.hiss > 0){ const a = p.hiss * p.hiss * .035; l += bqRun(this.hissF[0], this.noise()) * a; r += bqRun(this.hissF[1], this.noise()) * a; }
    if (p.hum > 0){
      const hz = p.humHz > .5 ? 60 : 50; this.ph[2] += hz / fs; if (this.ph[2] > 1) this.ph[2] -= 1;
      const x = 2 * Math.PI * this.ph[2], h = (Math.sin(x) + .5 * Math.sin(2 * x) + .25 * Math.sin(3 * x)) * p.hum * p.hum * .02;
      l += h; r += h;
    }
    return [l, r];
  }
}

/* ---------------- master: EQ match, parametric EQ, compressor, gain, look-ahead limiter ---------------- */
class Master {
  constructor(fs){ this.fs = fs; this.env = 0; this.L = Math.max(1, Math.round(fs * .002)); this.reset(); }
  reset(){
    const L = this.L;
    this.dl = [new Float32Array(L), new Float32Array(L)]; this.w = 0;
    this.mq = new Float64Array(L * 2 + 4); this.mi = new Int32Array(L * 2 + 4); this.mh = 0; this.mt = 0; this.n = 0;
    this.box = new Float64Array(L); this.bsum = L; this.box.fill(1); this.bw = 0; this.rg = 1; this.env = 0;
  }
  setup(p, match){
    const fs = this.fs, mk = () => {
      const s = [rbj('ls', fs, p.lsF, .707, p.lsG), rbj('peak', fs, p.p1F, p.p1Q, p.p1G), rbj('peak', fs, p.p2F, p.p2Q, p.p2G), rbj('peak', fs, p.p3F, p.p3Q, p.p3G), rbj('hs', fs, p.hsF, .707, p.hsG)];
      if (match && p.match > 0) MATCH_BANDS.forEach((f, i) => { if (f < fs * .45 && Math.abs(match[i]) > .01) s.push(rbj('peak', fs, f, 1.41, match[i] * p.match)); });
      return s;
    };
    const keep = this.eq ? this.eq.map(s => s.map(f => [f.z1, f.z2])) : null;
    this.eq = [mk(), mk()];
    if (keep) this.eq.forEach((s, c) => s.forEach((f, i) => { if (keep[c][i]){ f.z1 = keep[c][i][0]; f.z2 = keep[c][i][1]; } }));
    this.on = p.mOn > .5; this.thr = p.cThr; this.rat = p.cRat;
    this.atk = Math.exp(-1 / (p.cAtk * .001 * fs)); this.rel = Math.exp(-1 / (p.cRel * .001 * fs));
    this.post = dbToGain(p.cMake + p.mGain); this.ceil = dbToGain(p.lim); this.lrel = 1 - Math.exp(-1 / (.08 * fs));
  }
  process(l, r){
    if (!this.on) return [l, r];
    const e0 = this.eq[0], e1 = this.eq[1];
    for (let k = 0; k < e0.length; k++){ l = bqRun(e0[k], l); r = bqRun(e1[k], r); }
    // compressor: linked peak detector, 6 dB soft knee
    if (this.thr < 0 && this.rat > 1){
      const a = Math.max(Math.abs(l), Math.abs(r));
      this.env = a > this.env ? this.atk * this.env + (1 - this.atk) * a : this.rel * this.env + (1 - this.rel) * a;
      const x = 20 * Math.log10(this.env + 1e-9) - this.thr, s = 1 - 1 / this.rat;
      const red = x <= -3 ? 0 : x >= 3 ? x * s : s * (x + 3) * (x + 3) / 12;
      const g = dbToGain(-red); l *= g; r *= g;
    }
    l *= this.post; r *= this.post;
    return this.limit(l, r);
  }
  // look-ahead limiter: min gain over the next L samples, release, then a box average so the gain never jumps
  limit(l, r){
    const L = this.L, need = Math.min(1, this.ceil / (Math.max(Math.abs(l), Math.abs(r)) + 1e-12)), n = this.n++, cap = this.mq.length;
    while (this.mt !== this.mh && this.mq[(this.mt - 1 + cap) % cap] >= need) this.mt = (this.mt - 1 + cap) % cap;
    this.mq[this.mt] = need; this.mi[this.mt] = n; this.mt = (this.mt + 1) % cap;
    while (this.mi[this.mh] <= n - L) this.mh = (this.mh + 1) % cap;
    const m = this.mq[this.mh];
    this.rg = Math.min(m, this.rg + (1 - this.rg) * this.lrel);
    this.bsum += this.rg - this.box[this.bw]; this.box[this.bw] = this.rg; this.bw = (this.bw + 1) % L;
    const g = this.bsum / L, w = this.w, ol = this.dl[0][w], or = this.dl[1][w];
    this.dl[0][w] = l; this.dl[1][w] = r; this.w = (w + 1) % L;
    const c = this.ceil;
    return [clamp(ol * g, -c, c), clamp(or * g, -c, c)];
  }
}

/* ---------------- timestretch (WSOLA), pitch kept ---------------- */
// mode 0 smooth (WSOLA), 1 cyclic Classic (longer), 2 cyclic Warp (the cycles, played back at the original length)
function stretch(chs, factor, sr, grainMs, mode){
  if (Math.abs(factor - 1) < 1e-4) return chs;
  if (mode === true) mode = 1;
  if (mode === 2) return cyclicStretch(chs, factor, sr, grainMs).map(c => resampleTo(c, chs[0].length));
  if (mode) return cyclicStretch(chs, factor, sr, grainMs);
  const len = chs[0].length, N = Math.max(64, 2 * Math.round(grainMs / 1000 * sr / 2)), Hs = N / 2, Ha = Hs / factor, tol = Math.round(N / 4);
  const outLen = Math.max(1, Math.round(len * factor)), mono = new Float32Array(len);
  for (const c of chs) for (let i = 0; i < len; i++) mono[i] += c[i] / chs.length;
  const win = new Float32Array(N); for (let i = 0; i < N; i++) win[i] = .5 - .5 * Math.cos(2 * Math.PI * i / N);
  const out = chs.map(() => new Float32Array(outLen + N)), norm = new Float32Array(outLen + N);
  const at = i => i >= 0 && i < len ? mono[i] : 0;
  let prev = 0;
  for (let m = 0, op = 0; op < outLen; m++, op += Hs){
    let pos = Math.round(m * Ha);
    if (m > 0){
      // find the offset whose grain best continues the previous one (coarse, then fine)
      const nat = prev + Hs; let best = 0, bv = -Infinity;
      for (let d = -tol; d <= tol; d += 2){ let s = 0; for (let i = 0; i < N; i += 4) s += at(nat + i) * at(pos + d + i); if (s > bv){ bv = s; best = d; } }
      const c0 = best; bv = -Infinity;
      for (let d = c0 - 2; d <= c0 + 2; d++){ let s = 0; for (let i = 0; i < N; i += 2) s += at(nat + i) * at(pos + d + i); if (s > bv){ bv = s; best = d; } }
      pos += best;
    }
    pos = clamp(pos, 0, Math.max(0, len - 1));
    for (let c = 0; c < chs.length; c++){ const s = chs[c], o = out[c]; for (let i = 0; i < N && pos + i < len; i++) o[op + i] += s[pos + i] * win[i]; }
    for (let i = 0; i < N; i++) norm[op + i] += win[i];
    prev = pos;
  }
  return out.map(o => { const r = new Float32Array(outLen); for (let i = 0; i < outLen; i++) r[i] = o[i] / Math.max(norm[i], .5); return r; });
}

function resampleTo(c, n){
  const o = new Float32Array(n), step = (c.length - 1) / Math.max(1, n - 1);
  for (let i = 0; i < n; i++){ const x = i * step, k = Math.floor(x), f = x - k; o[i] = c[k] * (1 - f) + (c[k + 1] || 0) * f; }
  return o;
}
// the S950 / S1000 way: fixed cycles laid end to end with no search for a good join, just a short crossfade,
// which gives the stuttery, metallic sound of hardware timestretch
function cyclicStretch(chs, factor, sr, grainMs){
  const len = chs[0].length, G = Math.max(16, Math.round(grainMs / 1000 * sr)), X = Math.min(Math.round(.002 * sr), G >> 1);
  const outLen = Math.max(1, Math.round(len * factor));
  return chs.map(c => {
    const o = new Float32Array(outLen);
    for (let op = 0, k = 0; op < outLen; op += G, k++){
      const ip = Math.floor(op / factor);
      for (let i = 0; i < G + X && op + i < outLen; i++){
        const v = ip + i < len ? c[ip + i] : 0, w = i < X ? i / X : i >= G ? 1 - (i - G) / X : 1;
        o[op + i] += v * (k === 0 && i < X ? 1 : w);
      }
    }
    return o;
  });
}

/* ---------------- offline render: the whole chain for the sampler ---------------- */
// chs: source channels at sr; returns {pre:[L,R] before master, fs}
function renderPre(chs, sr, p0, fs, opts){
  opts = opts || {};
  const p = effective(p0);
  const mach = MACHINES[p.machine | 0] || MACHINES[0];
  let src = opts.stretched;
  if (!src){
    src = p.mono > .5 && chs.length > 1 ? [chs[0].map((v, i) => (v + chs[1][i]) * .5)] : chs.slice(0, 2);
    if (p.stretch !== 1) src = stretch(src, p.stretch, sr, p.grain, p.smode | 0);
  }
  const cents = totalCents(p, opts.semis), inGain = dbToGain(p.input), outGain = dbToGain(p.output - (p.link > .5 ? p.input : 0));
  const SPE = typeof TWSP !== 'undefined' ? TWSP : (typeof require === 'function' ? require('./sp1200.js') : null);
  const res = src.map(s => {
    let wet;
    if (mach.pitch === 0 && SPE){
      // the SP-1200 / SP-12: circuit-level engine (sp1200.js). Output 1-2, 3-6 or 7-8 by the Filter menu; the
      // 6-pole and 4-pole menu entries run output 7-8 into those filters.
      const ft = p.ftype | 0, out = ft === 0 ? 1 : ft <= 4 ? ft + 2 : 7;
      const codes = SPE.codesFor(preamped(s, p), sr, {clock:p.rate, level:1, bits:p.bits, rpm:p.rpm, calib:p.calib > .5 ? 'measured' : 'manual'});
      wet = SPE.playChannel(codes, {clock:p.rate, sp12:(p.machine | 0) === 1, cents:cents - p.rpm * 100, output:out, rest:p.cutoff, res:p.res,
        depth:p.fenv, retrigger:p.retrig > .5, calib:p.calib > .5 ? 'measured' : 'manual'}, fs);
      if (ft >= 6){ const f = new OutFilter(fs); f.setup(p); f.trigger(); for (let i = 0; i < wet.length; i++) wet[i] = f.process(wet[i]); }
      for (let i = 0; i < wet.length; i++) wet[i] /= 1 + p.drive * 1.5;
      const dry = p.mix < 1 ? playDry(s, sr, fs, cents, wet.length) : null;
      if (dry) for (let i = 0; i < wet.length; i++) wet[i] = wet[i] * p.mix + dry[i] * inGain * (1 - p.mix);
      return wet;
    }
    // the alias trick on any machine: sampled sped up by rpm semitones, played back down again
    const base = sampleBase(preamped(s, p), sr * Math.pow(2, p.rpm / 12), p, true); wet = playBase(base, p, fs, mach.pitch, cents - p.rpm * 100);
    const dry = p.mix < 1 ? playDry(s, sr, fs, cents, wet.length) : null;
    const f = new OutFilter(fs); f.setup(p); f.trigger();
    let dc = 0;
    for (let i = 0; i < wet.length; i++){
      let y = wet[i]; dc += (y - dc) * (6 / fs); y = f.process(y - dc);
      // drive pushes the converter; take it back out so drive changes the sound, not the level
      y /= 1 + p.drive * 1.5;
      wet[i] = dry ? y * p.mix + dry[i] * inGain * (1 - p.mix) : y;
    }
    return wet;
  });
  const L = res[0], R = res[1] || res[0].slice(), lo = new Lofi(fs, 777); lo.setup(p);
  for (let i = 0; i < L.length; i++){ const o = lo.process(L[i], R[i]); L[i] = o[0] * outGain; R[i] = o[1] * outGain; }
  return {pre:[L, R], stretched:src, fs};
}
function renderMaster(pre, p, match, fs){
  const m = new Master(fs); m.setup(p, match);
  const L = pre[0], R = pre[1], tail = m.L, oL = new Float32Array(L.length), oR = new Float32Array(L.length);
  // the limiter looks ahead, so run L extra samples and drop the first L
  for (let i = 0; i < L.length + tail; i++){
    const o = m.on ? m.process(i < L.length ? L[i] : 0, i < L.length ? R[i] : 0) : [L[i], R[i]];
    const j = m.on ? i - tail : i;
    if (j >= 0 && j < L.length){ oL[j] = o[0]; oR[j] = o[1]; }
  }
  return [oL, oR];
}

/* ---------------- analysis ---------------- */
function fft(re, im){
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++){ let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j){ let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1){
    const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len){
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++){
        const p = i + k, q = p + len / 2, tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
        re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}
function monoOf(chs){ if (chs.length === 1) return chs[0]; const m = new Float32Array(chs[0].length); for (const c of chs) for (let i = 0; i < m.length; i++) m[i] += c[i] / chs.length; return m; }
// mono, averaged down to about 11 kHz
function decimate(x, sr){
  const k = Math.max(1, Math.floor(sr / 11025)), n = Math.floor(x.length / k), d = new Float32Array(n);
  for (let i = 0; i < n; i++){ let s = 0; for (let j = 0; j < k; j++) s += x[i * k + j]; d[i] = s / k; }
  return {d, sr:sr / k};
}
// average power in octave bands (MATCH_BANDS), in dB
function bandSpectrum(chs, sr){
  const x = monoOf(chs), N = 4096, hop = 2048, frames = Math.max(1, Math.floor((x.length - N) / hop) + 1), take = Math.min(frames, 400);
  const pow = new Float64Array(N / 2), re = new Float64Array(N), im = new Float64Array(N);
  for (let t = 0; t < take; t++){
    const st = Math.floor(t * frames / take) * hop;
    for (let i = 0; i < N; i++){ re[i] = (x[st + i] || 0) * (.5 - .5 * Math.cos(2 * Math.PI * i / N)); im[i] = 0; }
    fft(re, im);
    for (let i = 0; i < N / 2; i++) pow[i] += re[i] * re[i] + im[i] * im[i];
  }
  return MATCH_BANDS.map(f => {
    const lo = Math.max(1, Math.floor(f / Math.SQRT2 * N / sr)), hi = Math.min(N / 2 - 1, Math.ceil(f * Math.SQRT2 * N / sr));
    let s = 1e-20; for (let i = lo; i <= hi; i++) s += pow[i];
    return hi >= lo ? 10 * Math.log10(s / take) : -200;
  });
}
// typical octave-band balance of a finished mix, used when there's no reference track
const TARGET_CURVE = [-6, -2, 0, 0, -1, -2, -3, -4.5, -7, -12];
// gains (dB) for the match EQ that move `cur` towards `ref`, ignoring overall level
function matchGains(ref, cur){
  const d = ref.map((r, i) => (r < -150 || cur[i] < -150) ? 0 : r - cur[i]);
  const mid = (d[3] + d[4] + d[5] + d[6] + d[7]) / 5;
  const c = d.map(v => clamp(v - mid, -12, 12));
  return c.map((v, i) => clamp(.25 * (c[i - 1] === undefined ? v : c[i - 1]) + .5 * v + .25 * (c[i + 1] === undefined ? v : c[i + 1]), -12, 12));
}
// integrated loudness (ITU-R BS.1770 K-weighting, 400 ms blocks, absolute and relative gates)
function loudness(chs, sr){
  const blk = Math.round(.4 * sr), hop = Math.round(.1 * sr), ms = [];
  const w = chs.slice(0, 2).map(c => {
    const a = rbj('hs', sr, 1681, .7071, 4), b = rbj('hp', sr, 38, .5), o = new Float32Array(c.length);
    for (let i = 0; i < c.length; i++) o[i] = bqRun(b, bqRun(a, c[i]));
    return o;
  });
  for (let s = 0; s + blk <= w[0].length; s += hop){ let e = 0; for (const c of w) for (let i = s; i < s + blk; i++) e += c[i] * c[i]; ms.push(e / blk); }
  if (!ms.length){ let e = 0; for (const c of w) for (const v of c) e += v * v; ms.push(e / Math.max(1, w[0].length)); }
  const lufs = m => -.691 + 10 * Math.log10(m + 1e-20);
  const g1 = ms.filter(m => lufs(m) > -70); if (!g1.length) return -70;
  const rel = lufs(g1.reduce((a, b) => a + b, 0) / g1.length) - 10, g2 = g1.filter(m => lufs(m) > rel);
  return lufs(g2.reduce((a, b) => a + b, 0) / Math.max(1, g2.length));
}
function peakDb(chs){ let p = 1e-9; for (const c of chs) for (const v of c) p = Math.max(p, Math.abs(v)); return 20 * Math.log10(p); }
// auto master: match EQ towards the reference (or a typical mix), gentle glue compression, then loudness to target
function autoMaster(pre, fs, refBands, target){
  target = target == null ? -14 : target;
  const cur = bandSpectrum(pre, fs), match = matchGains(refBands || TARGET_CURVE.map((v, i) => v + cur[4]), cur);
  const p = defaults();
  const set = {lsF:90, lsG:0, p1F:250, p1G:0, p1Q:1, p2F:1200, p2G:0, p2Q:1, p3F:5000, p3G:0, p3Q:1, hsF:10000, hsG:0, match:refBands ? .8 : .6, cRat:2, cAtk:20, cRel:200, cMake:0, lim:-1, mOn:1, mGain:0};
  Object.assign(p, set);
  // threshold a few dB under the average level so the compressor works ~2-4 dB
  let rms = 0, n = 0; for (const c of pre) for (let i = 0; i < c.length; i += 4){ rms += c[i] * c[i]; n++; }
  const rmsDb = 10 * Math.log10(rms / Math.max(1, n) + 1e-12);
  p.cThr = clamp(Math.round((rmsDb + 4) * 10) / 10, -40, 0);
  // sample up to 60 s from the middle for the loudness passes
  const seg = pre.map(c => { const len = Math.min(c.length, Math.round(60 * fs)), st = Math.floor((c.length - len) / 2); return c.subarray(st, st + len); });
  for (let it = 0; it < 3; it++){
    const out = renderMaster(seg, p, match, fs), lu = loudness(out, fs);
    if (!isFinite(lu) || lu <= -69) break;
    p.mGain = clamp(Math.round((p.mGain + (target - lu)) * 10) / 10, -12, 24);
  }
  const res = {}; for (const k of MASTER_IDS) res[k] = p[k];
  return {params:res, match};
}
// tempo: spectral-flux onsets, autocorrelation, then a bar-length check for loops
function detectTempo(chs, sr){
  const {d, sr:r} = decimate(monoOf(chs), sr), N = 1024, hop = 256, fr = r / hop, frames = Math.floor((d.length - N) / hop);
  if (frames < 32) return null;
  const re = new Float64Array(N), im = new Float64Array(N), prev = new Float64Array(N / 2), on = new Float64Array(frames);
  for (let t = 0; t < frames; t++){
    for (let i = 0; i < N; i++){ re[i] = d[t * hop + i] * (.5 - .5 * Math.cos(2 * Math.PI * i / N)); im[i] = 0; }
    fft(re, im); let s = 0;
    for (let i = 1; i < N / 2; i++){ const m = Math.log1p(100 * Math.hypot(re[i], im[i])); if (m > prev[i]) s += m - prev[i]; prev[i] = m; }
    on[t] = s;
  }
  let mean = 0; for (const v of on) mean += v / frames;
  for (let t = 0; t < frames; t++) on[t] = Math.max(0, on[t] - mean);
  const lag0 = Math.floor(60 * fr / 200), lag1 = Math.ceil(60 * fr / 55), ac = new Float64Array(lag1 + 2);
  for (let L = lag0; L <= lag1 + 1; L++){ let s = 0; for (let t = 0; t + L < frames; t++) s += on[t] * on[t + L]; ac[L] = s / (frames - L); }
  let best = lag0, bv = -1;
  for (let L = lag0 + 1; L <= lag1; L++){
    const bpm = 60 * fr / L, w = Math.exp(-.5 * Math.pow(Math.log2(bpm / 110) / 1.1, 2)), v = ac[L] * w;
    if (v > bv && ac[L] >= ac[L - 1] && ac[L] >= ac[L + 1]){ bv = v; best = L; }
  }
  const a = ac[best - 1], b = ac[best], c = ac[best + 1], den = a - 2 * b + c, off = den ? clamp(.5 * (a - c) / den, -.5, .5) : 0;
  let bpm = 60 * fr / (best + off);
  while (bpm < 70) bpm *= 2;
  while (bpm > 180) bpm /= 2;
  // a loop: a whole number of beats that lands near the estimate wins
  const secs = chs[0].length / sr;
  let loop = null;
  for (const beats of [4, 8, 16, 32, 2, 64]){ const lb = 60 * beats / secs; if (lb >= 70 && lb <= 180 && Math.abs(lb / bpm - 1) < .04){ loop = lb; break; } }
  if (loop) bpm = loop;
  return Math.round(bpm * 10) / 10;
}
// key: chroma from the spectrum, Krumhansl-Schmuckler profiles
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const KS_MAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const KS_MIN = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
function detectKey(chs, sr){
  const {d, sr:r} = decimate(monoOf(chs), sr), N = 4096, hop = 2048, frames = Math.floor((d.length - N) / hop) + 1;
  if (frames < 1) return null;
  const re = new Float64Array(N), im = new Float64Array(N), chroma = new Float64Array(12), take = Math.min(frames, 600);
  const lo = Math.ceil(55 * N / r), hi = Math.min(N / 2 - 1, Math.floor(2000 * N / r)), pc = new Int8Array(N / 2);
  for (let i = lo; i <= hi; i++) pc[i] = ((Math.round(12 * Math.log2(i * r / N / 440)) % 12) + 12 + 9) % 12;
  for (let t = 0; t < take; t++){
    const st = Math.floor(t * frames / take) * hop;
    for (let i = 0; i < N; i++){ re[i] = (d[st + i] || 0) * (.5 - .5 * Math.cos(2 * Math.PI * i / N)); im[i] = 0; }
    fft(re, im);
    for (let i = lo; i <= hi; i++) chroma[pc[i]] += Math.hypot(re[i], im[i]);
  }
  const corr = (a, b) => { const ma = a.reduce((s, v) => s + v, 0) / 12, mb = b.reduce((s, v) => s + v, 0) / 12; let n = 0, da = 0, db = 0; for (let i = 0; i < 12; i++){ n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; } return n / Math.sqrt(da * db + 1e-20); };
  let best = null;
  for (let k = 0; k < 12; k++) for (const [prof, q] of [[KS_MAJ, 'major'], [KS_MIN, 'minor']]){
    const rot = []; for (let i = 0; i < 12; i++) rot.push(prof[(i - k + 12) % 12]);
    const c = corr(chroma, rot);
    if (!best || c > best.c) best = {c, root:k, mode:q};
  }
  return {name:NOTE_NAMES[best.root] + ' ' + best.mode, root:best.root, mode:best.mode, confidence:Math.round(best.c * 100) / 100};
}

/* ---------------- presets: 500 of them, 25 families of 20 ---------------- */
const ADJ = ['Dusty', 'Warm', 'Crunchy', 'Smoky', 'Gritty', 'Bright', 'Dark', 'Heavy', 'Silky', 'Crispy', 'Muddy', 'Glassy', 'Woody', 'Velvet', 'Rusty', 'Hazy', 'Golden', 'Basement', 'Crate', 'Midnight',
  'Grainy', 'Soft', 'Punchy', 'Murky', 'Faded', 'Chunky', 'Thick', 'Thin', 'Wobbly', 'Rough'];
const SOUND_IDS = PARAMS.filter(p => p.group !== 'master').map(p => p.id);
function machinePatch(m){ const M = MACHINES[m]; return {machine:m, rate:M.rate, bits:M.bits, ftype:M.ftype, cutoff:M.cutoff, res:M.res}; }
const lerp = (r, a, b) => a + (b - a) * r();
const pick = (r, a) => a[Math.floor(r() * a.length) % a.length];
const FAMILIES = [
  ['SP-1200 Classic', r => ({...machinePatch(0), ftype:pick(r, [0, 1, 2, 3]), cutoff:lerp(r, 1200, 2600), fenv:lerp(r, .6, 1), drive:lerp(r, .05, .2)})],
  ['SP-1200 Out 1-2', r => ({...machinePatch(0), ftype:0, cutoff:lerp(r, 500, 3000), res:lerp(r, 0, .45), fenv:lerp(r, .3, 1)})],
  ['SP-1200 Out 3', r => ({...machinePatch(0), ftype:1, drive:lerp(r, 0, .25)})],
  ['SP-1200 Out 4', r => ({...machinePatch(0), ftype:2, drive:lerp(r, 0, .25)})],
  ['SP-1200 Out 5-6', r => ({...machinePatch(0), ftype:pick(r, [3, 4]), drive:lerp(r, 0, .25)})],
  ['SP-1200 Raw 7-8', r => ({...machinePatch(0), ftype:5, alias:lerp(r, 0, .6), drive:lerp(r, 0, .3)})],
  ['SP-1200 Pitched Down', r => ({...machinePatch(0), ftype:pick(r, [0, 1, 2]), semis:-Math.round(lerp(r, 3, 12)), drive:lerp(r, .05, .2)})],
  ['SP-1200 Pitched Up', r => ({...machinePatch(0), ftype:pick(r, [2, 3, 4, 5]), semis:Math.round(lerp(r, 2, 12))})],
  ['SP-12 Crunch', r => ({...machinePatch(1), ftype:pick(r, [0, 2, 5]), drive:lerp(r, .15, .4), alias:lerp(r, 0, .4)})],
  ['MPC60 Swing', r => ({...machinePatch(2), cutoff:lerp(r, 9000, 16000), ftype:pick(r, [5, 6]), drive:lerp(r, 0, .15)})],
  ['MPC60 Dusty', r => ({...machinePatch(2), ftype:6, cutoff:lerp(r, 5000, 10000), crackle:lerp(r, .1, .4), age:lerp(r, .1, .4)})],
  ['S950 Filtered', r => ({...machinePatch(3), cutoff:lerp(r, 2500, 9000)})],
  ['S950 Low Rate', r => ({...machinePatch(3), rate:pick(r, [15000, 22050, 20000]), cutoff:lerp(r, 3000, 7000)})],
  ['S612 Warm', r => ({...machinePatch(4), cutoff:lerp(r, 3500, 9000), res:lerp(r, 0, .3), tape:lerp(r, 0, .3)})],
  ['Mirage 8-bit', r => ({...machinePatch(5), cutoff:lerp(r, 2500, 9000), res:lerp(r, .1, .5), fenv:lerp(r, 0, .5)})],
  ['Vinyl Crate', r => ({...machinePatch(pick(r, [0, 2])), ftype:pick(r, [1, 2, 6]), cutoff:lerp(r, 7000, 12000), crackle:lerp(r, .3, .8), age:lerp(r, .2, .6), wow:lerp(r, 0, .2)})],
  ['Tape Warble', r => ({...machinePatch(pick(r, [0, 3, 6])), wow:lerp(r, .3, .8), flutter:lerp(r, .2, .6), tape:lerp(r, .2, .6), hiss:lerp(r, .1, .35)})],
  ['Cassette Deck', r => ({...machinePatch(6), tape:lerp(r, .3, .7), hiss:lerp(r, .25, .5), wow:lerp(r, .1, .35), flutter:lerp(r, .2, .5), age:lerp(r, .1, .3)})],
  ['AM Radio', r => ({...machinePatch(6), rate:lerp(r, 8000, 11025), bits:Math.round(lerp(r, 9, 12)), ftype:6, cutoff:lerp(r, 2800, 4500), age:lerp(r, .7, 1), hiss:lerp(r, .2, .4), hum:lerp(r, .1, .3), mono:1})],
  ['Bitcrushed', r => ({...machinePatch(6), bits:Math.round(lerp(r, 3, 8)), rate:lerp(r, 4000, 16000), pitchMode:1, ftype:5, alias:lerp(r, .5, 1)})],
  ['Phone Line', r => ({...machinePatch(6), rate:8000, bits:Math.round(lerp(r, 8, 10)), ftype:6, cutoff:3400, age:1, tape:lerp(r, .3, .6), mono:1})],
  ['Basement Boom Bap', r => ({...machinePatch(0), ftype:pick(r, [0, 1, 2]), semis:-Math.round(lerp(r, 0, 5)), crackle:lerp(r, .05, .3), tape:lerp(r, .1, .4), drive:lerp(r, .1, .25)})],
  ['Lo-Fi Keys', r => ({...machinePatch(pick(r, [3, 4, 6])), wow:lerp(r, .2, .5), flutter:lerp(r, .1, .3), crackle:lerp(r, .1, .3), hiss:lerp(r, .1, .3), age:lerp(r, .2, .5), mono:0})],
  ['Drum Break', r => ({...machinePatch(pick(r, [0, 1, 2])), ftype:pick(r, [0, 2, 3, 5]), drive:lerp(r, .1, .3), crackle:lerp(r, 0, .2), retrig:1})],
  ['Clean Character', r => ({...machinePatch(6), tape:lerp(r, 0, .3), age:lerp(r, 0, .15), mono:0})],
  ['Classic Samplers', r => { const m = 7 + Math.floor(r() * 11); const M = MACHINES[m]; return {...machinePatch(m), rate:lerp(r, Math.max(M.lo, M.hi * .6), M.hi), cutoff:M.cutoff * lerp(r, .4, 1), res:lerp(r, 0, .4), rpm:r() < .4 ? Math.round(lerp(r, 1, 8) * 10) / 10 : 0, drive:lerp(r, 0, .2)}; }],
  ['Styles', (r, k) => { const [name, m, o] = STYLES[k]; return {...machinePatch(m), ...o, _name:name}; }]
];
// 20 named sounds: [name, machine, settings]
const STYLES = [
  ['Dusty Chop', 0, {ftype:1, rpm:5.4, drive:.15}], ['45 Flip', 0, {ftype:0, rpm:12, cutoff:1400}], ['Bright Footwork', 0, {ftype:5, rpm:2}],
  ['Dark Motor', 0, {ftype:0, cutoff:500, res:.6}], ['Clean Swing', 7, {}], ['Heavy Dub', 3, {cutoff:3000}],
  ['Grain Fog', 13, {stretch:4, smode:1, grain:30}], ['Rot', 5, {bits:4, rate:10000}], ['Punch Break', 3, {rate:40000, cutoff:14000}],
  ['Plastic Bite', 16, {res:.7, cutoff:3000}], ['Amen Smear', 8, {stretch:2, smode:2, grain:25}], ['Box Kit', 9, {}],
  ['Ring Tone', 8, {stretch:2, smode:2, grain:1}], ['Mono Phone', 10, {ftype:14, cutoff:1800, res:.6, mono:1}], ['Grit 13', 13, {bits:13}],
  ['Analog Res', 14, {res:.55, cutoff:2500}], ['Freeze', 6, {stretch:5, smode:1, grain:150}], ['Toy', 10, {bits:8, fOn:0}],
  ['Smoke Break', 8, {stretch:1.5, smode:1, grain:3}], ['Submerged', 11, {cutoff:400, res:.7}]
];
function makePresets(){
  const out = [], base = defaults();
  FAMILIES.forEach(([fam, fn], fi) => {
    const r = rng(1200 + fi * 97);
    for (let k = 0; k < 20; k++){
      const v = Object.assign({}, base, {semis:0, cents:0, a4:440}, fn(r, k)), named = v._name;
      delete v.pitchMode; delete v._name;
      const vals = {}; for (const id of SOUND_IDS) vals[id] = round4(clampParam(id, v[id]));
      const adj = ADJ[Math.floor(r() * ADJ.length)];
      out.push({name:named || `${fam} ${String(k + 1).padStart(2, '0')} ${adj}`, family:fam, values:vals});
    }
  });
  return out;
}
const round4 = v => Math.round(v * 1e4) / 1e4;
function clampParam(id, v){ const p = PARAMS.find(q => q.id === id); v = clamp(v, p.min, p.max); return p.step >= 1 ? Math.round(v) : v; }
// random settings that still sound like a sampler (a family, then everything nudged)
function randomPatch(seed){
  const r = rng(seed), fam = FAMILIES[Math.floor(r() * (FAMILIES.length - 1))], v = Object.assign({}, defaults(), fn0(fam[1], r));
  v.semis = r() < .35 ? Math.round(lerp(r, -7, 5)) : 0;
  for (const id of ['crackle', 'hiss', 'wow', 'flutter', 'tape', 'age']) if (r() < .3) v[id] = clamp(v[id] + lerp(r, 0, .35), 0, 1);
  v.drive = clamp(v.drive + lerp(r, -.1, .1), 0, 1);
  const vals = {}; for (const id of SOUND_IDS) vals[id] = round4(clampParam(id, v[id]));
  return {name:'Random · ' + fam[0], values:vals};
}
function fn0(fn, r){ const o = fn(r, 0); delete o.pitchMode; delete o._name; return o; }

const api = {FILTER_RANGE, preamp, effective, preamped, resampleTo, onePoleLp, SP_SR, SP_TUNE, PROTO, MACHINES, FILTERS, PARAMS, MASTER_IDS, SOUND_IDS, MATCH_BANDS, TARGET_CURVE, NOTE_NAMES,
  defaults, rng, rbj, sDesign, quantize, sampleBase, Converter, spInc, playBase, OutFilter, Lofi, Master, stretch, renderPre, renderMaster,
  fft, bandSpectrum, matchGains, loudness, peakDb, autoMaster, detectTempo, detectKey, makePresets, randomPatch, totalCents, clamp, dbToGain};
if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.TW = api;
})(typeof self !== 'undefined' ? self : this);
