// MXR M-117 Flanger circuit model: a line-for-line port of plugins/flanger/M117.h (read that file for the
// circuit notes). Runs in the AudioWorklet (worklet.js) and in Node (tools/compare.mjs checks it against the C++).
(function(root){
'use strict';
const PARAMS = [
  {id:'manual', name:'Manual', min:0, max:10, def:5, unit:''},
  {id:'width', name:'Width', min:0, max:10, def:5, unit:''},
  {id:'speed', name:'Speed', min:0, max:10, def:3, unit:''},
  {id:'regen', name:'Regen', min:0, max:10, def:5, unit:''},
  {id:'bypass', name:'Bypass', min:0, max:1, def:0, unit:''},
  {id:'mix', name:'Mix', min:0, max:100, def:50, unit:'%'},
  {id:'spread', name:'Stereo', min:0, max:180, def:0, unit:'deg'},
  {id:'level', name:'Level', min:-24, max:12, def:0, unit:'dB'},
];
const P = {}; PARAMS.forEach((q, i) => P[q.id] = i);

const kStages = 1024, kCells = kStages / 2;
const kClockHi = 2048000.0, kClockLo = 42667.0;
const kLfoLo = 0.06, kLfoHi = 6.0;
const kAntiAlias = 7500.0, kPostPole = 12000.0;
const kRegenMax = 0.95, kCti = 0.035, kBucketClip = 1.4, kHiss = 0.00006, kCoupling = 16.0;
const PI = 3.141592653589793;

function sat(x, lim){
  let u = x / lim;
  if (u > 3) u = 3; else if (u < -3) u = -3;
  return lim * u * (27 + u * u) / (27 + 9 * u * u);
}

class LP2 {
  constructor(){ this.a1 = 0; this.a2 = 0; this.a3 = 0; this.ic1 = 0; this.ic2 = 0; }
  set(fc, q, fs){ const g = Math.tan(PI * fc / fs), k = 1 / q; this.a1 = 1 / (1 + g * (g + k)); this.a2 = g * this.a1; this.a3 = g * this.a2; }
  reset(){ this.ic1 = this.ic2 = 0; }
  run(x){
    const v3 = x - this.ic2, v1 = this.a1 * this.ic1 + this.a2 * v3, v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2;
    return v2;
  }
}

class LP1 {
  constructor(){ this.G = 0; this.s = 0; }
  set(fc, fs){ const g = Math.tan(PI * fc / fs); this.G = g / (1 + g); }
  reset(){ this.s = 0; }
  run(x){ const v = (x - this.s) * this.G, y = v + this.s; this.s = y + v; return y; }
}

class Channel {
  constructor(){
    this.cell = new Float64Array(kCells);
    this.dc = new LP1(); this.post1 = new LP1(); this.pre = new LP2(); this.post = new LP2();
    this.reset();
  }
  reset(){
    this.cell.fill(0);
    this.head = 0; this.ph = 0; this.hold = 0; this.cti = 0; this.xPrev = 0; this.wet = 0;
    this.dc.reset(); this.post1.reset(); this.pre.reset(); this.post.reset();
  }
}

class Engine {
  constructor(){
    this.fs = 48000;
    this.p = PARAMS.map(q => q.def);
    this.ch = [new Channel(), new Channel()];
    this.lfo = 0; this.smooth = 0.001;
    this.sManual = .5; this.sWidth = .5; this.sRegen = 0; this.sMix = .5; this.sLevel = 1; this.sOn = 1;
    this.rng = 0x9E3779B9;
  }
  prepare(sampleRate){
    const fs = this.fs = sampleRate, nyq = fs * 0.45;
    for (const c of this.ch){
      c.dc.set(kCoupling, fs);
      c.pre.set(kAntiAlias < nyq ? kAntiAlias : nyq, 0.7071067811865476, fs);
      c.post.set(kAntiAlias < nyq ? kAntiAlias : nyq, 0.7071067811865476, fs);
      c.post1.set(kPostPole < nyq ? kPostPole : nyq, fs);
    }
    this.smooth = 1 - Math.exp(-1 / (0.025 * fs));
    this.reset();
  }
  reset(){
    for (const c of this.ch) c.reset();
    const p = this.p;
    this.lfo = 0; this.rng = 0x9E3779B9;
    this.sManual = p[0] / 10; this.sWidth = p[1] / 10; this.sRegen = this.regenGain(); this.sMix = p[5] / 100; this.sLevel = this.levelGain();
    this.sOn = p[4] >= 0.5 ? 0 : 1;
  }
  set(i, v){ if (typeof i === 'string') i = P[i]; if (i >= 0 && i < PARAMS.length) this.p[i] = +v; }
  lfoHz(){ return kLfoLo * Math.pow(kLfoHi / kLfoLo, this.p[2] / 10); }
  static clockFor(cv){ return kClockHi * Math.pow(kClockLo / kClockHi, cv); }
  regenGain(){ const r = this.p[3] / 10; return kRegenMax * r; }
  levelGain(){ return Math.pow(10.0, this.p[7] / 20); }
  cvAt(phase){
    if (phase >= 1) phase -= 1;
    const tri = 1 - 4 * Math.abs(phase - 0.5);
    let cv = this.sManual + 0.5 * this.sWidth * tri;
    cv = 0.5 + 0.5 * sat(2 * cv - 1, 1.0);
    return cv;
  }
  noise(){
    let r = this.rng;
    r = (r ^ (r << 13)) >>> 0; r = (r ^ (r >>> 17)) >>> 0; r = (r ^ (r << 5)) >>> 0;
    this.rng = r;
    return (r / 4294967296.0) * 2 - 1;
  }
  run(c, x, cv){
    const inp = x - c.dc.run(x);
    const bbdIn = c.pre.run(inp + this.sRegen * c.wet);
    const ticks = Engine.clockFor(cv) / this.fs;
    let acc = 0, tPrev = 0, t = (1 - c.ph) / ticks;
    const cell = c.cell;
    while (t <= 1){
      acc += c.hold * (t - tPrev);
      const s = sat(c.xPrev + (bbdIn - c.xPrev) * t, kBucketClip);
      const out = cell[c.head];
      cell[c.head] = s;
      c.head = c.head + 1 === kCells ? 0 : c.head + 1;
      c.cti += (out - c.cti) * (1 - kCti);
      c.hold = c.cti;
      tPrev = t;
      t += 1 / ticks;
    }
    acc += c.hold * (1 - tPrev);
    c.ph += ticks; c.ph -= Math.floor(c.ph);
    c.xPrev = bbdIn;
    const y = c.post1.run(c.post.run(acc + kHiss * this.noise()));
    c.wet = y;
    return y;
  }
  // inL/inR/outL/outR: Float32Arrays (in and out may be the same array)
  process(inL, inR, outL, outR, n, o = 0){
    const p = this.p;
    const dl = this.lfoHz() / this.fs;
    const tManual = p[0] / 10, tWidth = p[1] / 10, tRegen = this.regenGain(), tMix = p[5] / 100, tLevel = this.levelGain();
    const tOn = p[4] >= 0.5 ? 0 : 1, off = p[6] / 360, sm = this.smooth;
    for (let i = o; i < o + n; i++){
      this.sManual += (tManual - this.sManual) * sm; this.sWidth += (tWidth - this.sWidth) * sm; this.sRegen += (tRegen - this.sRegen) * sm;
      this.sMix += (tMix - this.sMix) * sm; this.sLevel += (tLevel - this.sLevel) * sm; this.sOn += (tOn - this.sOn) * sm;
      this.lfo += dl; if (this.lfo >= 1) this.lfo -= 1;
      const xl = inL[i], xr = inR[i];
      const yl = this.run(this.ch[0], xl, this.cvAt(this.lfo));
      const yr = this.run(this.ch[1], xr, this.cvAt(this.lfo + off));
      const gl = (xl * (1 - this.sMix) + yl * this.sMix) * this.sLevel, gr = (xr * (1 - this.sMix) + yr * this.sMix) * this.sLevel;
      outL[i] = xl + (gl - xl) * this.sOn;
      outR[i] = xr + (gr - xr) * this.sOn;
    }
  }
}

const api = {PARAMS, P, Engine, defaults(){ const o = {}; PARAMS.forEach(q => o[q.id] = q.def); return o; }};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.M117 = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
