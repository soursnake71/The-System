/* Omega Twelve SP engine: the E-mu SP-1200 / SP-12 audio path simulated at circuit level.

   Analog parts run as circuits, not fitted curves: each one is its schematic (component values from E-mu's service
   manuals: SP-1200 SK103 rev D, SP-12 S-7600-001; transcribed with sources in libmd12, MIT, github.com/Mudb0y/libmd12),
   turned into equations by modified nodal analysis and stepped with the trapezoidal rule at 4x the host rate:
     - the sampling input's seventh-order anti-aliasing filter (three state-variable sections and a lossy integrator),
     - outputs 3-6: an RC section and two unity-gain Sallen-Key stages each, with their own values,
     - outputs 1-2: the SSM2044 four-pole filter (nonlinear transconductance stages, three 0.01 uF poles and one 820 pF,
       fed through the 33k/200 divider) and its current-to-voltage output stage with the 10 uF coupling capacitor,
     - the filter's cutoff control: the 10 uF capacitor that the trigger discharges through 100 ohms and a 1N914 and
       that recharges through 30k, into the pin 13 resistor network and the chip's exponential converter.
   Digital parts follow the hardware: 20 MHz / 768 frame clock, eight channels sharing one DAC in 4.8 us slots,
   sample-and-hold, truncating 12-bit converter, E-mu's semitone table on a 7-bit fraction counter that drops or repeats
   samples, the 8-bit level DAC, five silent codes at the start of a sound and the last five never played.

   Calibration "measured" fits the parts' tolerances to a real machine: John Nolting's recordings of an SP-12 with noise
   through each output (ccrma.stanford.edu/~nolting/220c/). See twelve/tools/sp-validate.mjs. */
(function (root) {
'use strict';

const CLOCK = 20e6 / 768, SLOT = 4.8e-6, VT = 0.025852;
const TABLE = [50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255];

/* ---------------- netlists (service manual values) ---------------- */
// R/C: two nodes and a value; X: ideal op-amp (+in, -in, out); 'in' is driven, 'out' is read
const NETS = {
  // SK103 p14, "Input ADC": preamp output to the LF398 sample-and-hold
  aa: `R in s1 10k; R s1 v11 10k; R v13 s1 10k; X 0 s1 v11; R v11 s12 19.1k; R s12 v12 21k; C s12 v12 1000p; X 0 s12 v12;
       R v12 s13 19.1k; C s13 v13 1000p; X 0 s13 v13; R v11 s2 107k; R v12 s2 118k; R v13 s2 14.7k;
       R s2 v21 10k; R v23 s2 10k; X 0 s2 v21; R v21 s22 13.3k; R s22 v22 43.2k; C s22 v22 1000p; X 0 s22 v22;
       R v22 s23 13.3k; C s23 v23 1000p; X 0 s23 v23; R v21 s3 124k; R v22 s3 402k; R v23 s3 47.5k;
       R s3 v31 10k; R v33 s3 10k; X 0 s3 v31; R v31 s32 12.1k; R s32 v32 162k; C s32 v32 1000p; X 0 s32 v32;
       R v32 s33 12.1k; C s33 v33 1000p; X 0 s33 v33; R v31 s4 6.19k; R v32 s4 82.5k; R v33 s4 3.92k;
       R s4 out 31.6k; C s4 out 1000p; X 0 s4 out`,
  // SK103 p17, "Output Channels": outputs 3 to 6
  out3: `R in na 7.5k; C na 0 4700p; R na nb 3.6k; R nb nc 6.2k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;
         R o1 nd 6.2k; R nd ne 6.2k; C ne 0 1000p; C nd out 0.01u; X ne out out`,
  out4: `R in na 6.2k; C na 0 4700p; R na nb 3.0k; R nb nc 5.1k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;
         R o1 nd 5.1k; R nd ne 5.1k; C ne 0 1000p; C nd out 0.01u; X ne out out`,
  out5: `R in na 5.1k; C na 0 4700p; R na nb 2.4k; R nb nc 4.3k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;
         R o1 nd 4.3k; R nd ne 4.3k; C ne 0 1000p; C nd out 0.01u; X ne out out`,
  out6: `R in na 4.3k; C na 0 4700p; R na nb 2.2k; R nb nc 3.9k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;
         R o1 nd 3.9k; R nd ne 3.9k; C ne 0 1000p; C nd out 0.01u; X ne out out`,
  // SK103 p16: SSM2044 output current (as a Norton source through R141 3.3k) to the I/V stage
  dynout: `R in nout 3.3k; C nout nsum 10u; R nsum out 15k; C nsum out 470p; X 0 nsum out`
};
const UNIT = {p:1e-12, n:1e-9, u:1e-6, m:1e-3, k:1e3, M:1e6};
function val(s){ const m = /^([\d.]+)(Meg|[pnumkM])?$/.exec(s); return parseFloat(m[1]) * (m[2] === 'Meg' ? 1e6 : m[2] ? UNIT[m[2]] : 1); }

/* ---------------- linear circuit -> discrete state space ---------------- */
// MNA with trapezoidal capacitor companions; the state is each capacitor's companion current, so a step is
// s' = A s + B u, y = C s + D u. capScale multiplies every capacitor (part tolerance / calibration).
function compile(text, fs, capScale){
  const els = text.split(/[;\n]/).map(l => l.trim()).filter(Boolean).map(l => l.split(/\s+/));
  const nodes = new Map(); const id = n => { if (n === '0') return -1; if (!nodes.has(n)) nodes.set(n, nodes.size); return nodes.get(n); };
  for (const e of els) for (const n of e.slice(1, e[0] === 'X' ? 4 : 3)) id(n);
  id('in'); id('out');
  const N = nodes.size, ops = els.filter(e => e[0] === 'X'), caps = els.filter(e => e[0] === 'C');
  const M = N + 1 + ops.length, G = Array.from({length:M}, () => new Float64Array(M)), T = 1 / fs;
  const stamp = (a, b, g) => { if (a >= 0) G[a][a] += g; if (b >= 0) G[b][b] += g; if (a >= 0 && b >= 0){ G[a][b] -= g; G[b][a] -= g; } };
  // trapezoidal steps squeeze high frequencies; scaling every capacitor makes the response exact at 13 kHz,
  // where the anti-alias notch and the output filters' corners are
  const w0 = 2 * Math.PI * 13000, warp = w0 / (2 / T * Math.tan(w0 * T / 2));
  const gc = caps.map(e => 2 * val(e[3]) * (capScale || 1) * warp / T);
  for (const e of els) if (e[0] === 'R') stamp(id(e[1]), id(e[2]), 1 / val(e[3]));
  caps.forEach((e, k) => stamp(id(e[1]), id(e[2]), gc[k]));
  const vin = id('in'); G[vin][N] += 1; G[N][vin] = 1;   // source current and v(in) = u
  ops.forEach((e, k) => { const r = N + 1 + k, p = id(e[1]), q = id(e[2]), o = id(e[3]); G[o][r] += 1; if (p >= 0) G[r][p] += 1; if (q >= 0) G[r][q] -= 1; });
  // solve G x = b for each unit right-hand side: companion k injects into its nodes, u drives row N
  const solve = b => {
    const A = G.map(r => Float64Array.from(r)), x = Float64Array.from(b);
    for (let c = 0; c < M; c++){
      let p = c; for (let r = c + 1; r < M; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
      [A[c], A[p]] = [A[p], A[c]]; [x[c], x[p]] = [x[p], x[c]];
      for (let r = c + 1; r < M; r++){ const f = A[r][c] / A[c][c]; if (!f) continue; for (let k = c; k < M; k++) A[r][k] -= f * A[c][k]; x[r] -= f * x[c]; }
    }
    for (let c = M - 1; c >= 0; c--){ let s = x[c]; for (let k = c + 1; k < M; k++) s -= A[c][k] * x[k]; x[c] = s / A[c][c]; }
    return x;
  };
  const nc = caps.length, cols = [];
  for (let k = 0; k <= nc; k++){
    const b = new Float64Array(M);
    if (k < nc){ const a = id(caps[k][1]), c = id(caps[k][2]); if (a >= 0) b[a] += 1; if (c >= 0) b[c] -= 1; } else b[N] = 1;
    cols.push(solve(b));
  }
  const vcap = (x, k) => { const a = id(caps[k][1]), c = id(caps[k][2]); return (a >= 0 ? x[a] : 0) - (c >= 0 ? x[c] : 0); };
  const A = new Float64Array(nc * nc), B = new Float64Array(nc), C = new Float64Array(nc), out = id('out');
  for (let j = 0; j <= nc; j++) for (let k = 0; k < nc; k++){
    const v = 2 * gc[k] * vcap(cols[j], k) - (j === k ? 1 : 0);
    if (j < nc) A[k * nc + j] = v; else B[k] = v;
  }
  for (let j = 0; j < nc; j++) C[j] = cols[j][out];
  return {n:nc, A, B, C, D:cols[nc][out]};
}
class Circuit {
  constructor(text, fs, capScale){ Object.assign(this, compile(text, fs, capScale)); this.s = new Float64Array(this.n); this.t = new Float64Array(this.n); }
  process(u){
    const n = this.n, s = this.s, t = this.t, A = this.A; let y = this.D * u;
    for (let k = 0; k < n; k++) y += this.C[k] * s[k];
    for (let k = 0; k < n; k++){ let v = this.B[k] * u; const r = k * n; for (let j = 0; j < n; j++) v += A[r + j] * s[j]; t[k] = v; }
    this.s = t; this.t = s;
    return y;
  }
  reset(){ this.s.fill(0); }
  // steady-state gain at DC (for normalising)
  dc(){ let y = 0; for (let i = 0; i < 200000; i++) y = this.process(1); this.reset(); return y; }
}

/* ---------------- the SSM2044 and its control circuit ---------------- */
// Cutoff control (SK103 p16): C111 10u charges toward +15 V through R135 30k; a trigger pulls the GAIN line low for
// ~9.4 ms and discharges it through R98 100 and D9 1N914; C111 feeds pin 13 through R125 15k into R118 390 to ground,
// R112 39k to -15 V and R104 220k to the rest-cutoff trimmer. The exponential converter: an octave per Vt ln 2.
class Envelope {
  constructor(fs, capScale){
    this.h = 1 / fs; this.C = 10e-6 * (capScale || 1); this.v = 0; this.gain = 5;
    this.Is = 2.52e-9; this.nVt = 1.752 * VT;
    this.gsum = 1 / 15e3 + 1 / 390 + 1 / 39e3 + 1 / 220e3;
    // settle to rest with the GAIN line high (time constant ~0.1 s)
    const n = Math.ceil(1.5 / this.h); for (let i = 0; i < n; i++) this.step();
    this.v13rest = this.pin13();
  }
  pin13(){ return (this.v / 15e3 - 15 / 39e3) / this.gsum; }   // trimmer contribution is folded into the rest cutoff
  diode(vd){   // current through R98 and D9 for a drop vd across both (Newton)
    if (vd <= 0) return 0;
    let i = Math.max(1e-12, (vd - .6) / 100);
    for (let k = 0; k < 20; k++){ const f = i * 100 + this.nVt * Math.log(i / this.Is + 1) - vd, d = 100 + this.nVt / (i + this.Is); const ni = Math.max(1e-15, i - f / d); if (Math.abs(ni - i) < 1e-12) { i = ni; break; } i = ni; }
    return i;
  }
  step(){
    // implicit Euler on the capacitor, linearising the diode at the present voltage
    const v = this.v, v13 = this.pin13();
    const iR = (15 - v) / 30e3 - (v - v13) / 15e3, id = this.diode(v - this.gain), dv = 1e-4, gd = (this.diode(v + dv - this.gain) - id) / dv;
    const g = 1 / 30e3 + (1 / 15e3) * (1 - (1 / 15e3) / this.gsum) + gd;
    this.v = v + this.h * (iR - id) / (this.C + this.h * g);
    return this.v;
  }
  // octaves above the rest cutoff
  octaves(){ return (this.v13rest - this.pin13()) / (VT * Math.LN2); }
}
// Four transconductance stages (three 0.01 uF poles and an 820 pF one), tanh nonlinearity, a little global feedback.
// Semi-implicit: each stage linearised at its last operating point, so it stays stable when the filter opens fully.
class SSM2044 {
  constructor(fs, res){ this.h = 1 / fs; this.v = new Float64Array(4); this.g = [1, 1, 1, 1]; this.k = res; this.ratio = [1, 1, 1, .01e-6 / 820e-12]; }
  process(x, fc){
    const w = 2 * Math.PI * fc, v = this.v, h = this.h, tv = 2 * VT;
    let inp = x - this.k * v[3];
    for (let s = 0; s < 4; s++){
      const d = inp - v[s], a = d / tv, ge = Math.abs(a) < 1e-6 ? 1 : Math.tanh(a) / a;
      const hw = h * w * this.ratio[s] * ge;
      v[s] = (v[s] + hw * inp) / (1 + hw);
      inp = v[s];
    }
    return v[3];
  }
  reset(){ this.v.fill(0); }
}

/* ---------------- resampling ---------------- */
function lowpassFir(taps, cut){   // cut: fraction of the sample rate; Blackman-windowed sinc, unity DC
  const h = new Float64Array(taps), m = (taps - 1) / 2; let s = 0;
  for (let i = 0; i < taps; i++){ const x = i - m, w = .42 - .5 * Math.cos(2 * Math.PI * i / (taps - 1)) + .08 * Math.cos(4 * Math.PI * i / (taps - 1)); h[i] = (x ? Math.sin(2 * Math.PI * cut * x) / (Math.PI * x) : 2 * cut) * w; s += h[i]; }
  for (let i = 0; i < taps; i++) h[i] /= s;
  return h;
}
const OS = 4;
const DECIM = lowpassFir(127, .45 / OS);
function decimate(y, n){   // y at OS*fs -> n samples at fs
  const out = new Float32Array(n), m = (DECIM.length - 1) / 2;
  for (let i = 0; i < n; i++){ const c = i * OS; let s = 0; for (let k = 0; k < DECIM.length; k++){ const j = c + k - m; if (j >= 0 && j < y.length) s += DECIM[k] * y[j]; } out[i] = s; }
  return out;
}

/* ---------------- calibrations ---------------- */
// manual: the parts as drawn. measured: part tolerances fitted to Nolting's SP-12 with twelve/tools/sp-fit.mjs and
// sp-sweep.mjs: output 3-6 capacitors 6-9 % under nominal (each output then within 0.26 dB rms of the real one from
// 2 to 15 kHz), C111 15 % under (the filter sweep after a hit then within 0.24 octave rms), the dynamic filter's stage
// frequency at rest 925 Hz with feedback 0.3 (1.4 dB rms). rest is the RT6 trimmer.
const CALIB = {
  manual:  {fixedCap:[1, 1, 1, 1], rest:1000, res:.3, dynCap:1, gain12:1},
  measured:{fixedCap:[.925, .94, .92, .91], rest:925, res:.3, dynCap:.85, gain12:1}
};

/* ---------------- the machine ---------------- */
// sample a recording the way the machine does: preamp level, anti-alias circuit, sample-and-hold, 12-bit codes
function sampleCodes(src, sr, o){
  const fo = sr * OS, aa = new Circuit(NETS.aa, Math.max(fo, CLOCK * 4)), foA = Math.max(fo, CLOCK * 4);
  const aaDc = aa.dc(), clock = o.clock || CLOCK, gain = (o.level == null ? 1 : o.level) / aaDc;
  const n = src.length, dur = n / sr, frames = Math.min(o.maxFrames || 1 << 20, Math.floor(dur * clock));
  const codes = new Uint16Array(frames + 10).fill(0x800);   // five silent codes at the start, five spare at the end
  const bits = o.bits || 12, drop = 12 - Math.max(1, Math.min(12, bits)), off = o.adcOffset == null ? .5 : o.adcOffset;
  let prev = 0, k = 0, tNext = 0;
  const steps = Math.ceil(dur * foA);
  for (let i = 0; i < steps && k < frames; i++){
    const t = i / foA, x = t * sr, j = Math.floor(x), f = x - j;
    const u = ((src[j] || 0) * (1 - f) + (src[j + 1] || 0) * f) * gain;
    const y = aa.process(u);
    while (k < frames && tNext <= t){
      const a = 1 - (t - tNext) * foA, held = prev + (y - prev) * Math.max(0, Math.min(1, a));
      let c = Math.floor(held * 2048 + 2048 + off - .5);
      c = Math.max(0, Math.min(4095, c));
      if (drop) c = (c >> drop) << drop;
      codes[5 + k] = c; k++; tNext = k / clock;
    }
    prev = y;
  }
  return codes.subarray(0, 5 + k + 5);
}
// sampling is the slow part and doesn't depend on pitch or the output, so keep the last few results.
// rpm: the 45 rpm trick, the source played that many semitones fast into the sampler (played back down again later)
const codeCache = [];
function codesFor(src, sr, o){
  const key = [src.length, sr, o.clock, o.level, o.bits, o.rpm || 0, o.adcOffset].join('|');
  for (const c of codeCache) if (c.src === src && c.key === key) return c.codes;
  let s = src, rate = sr;
  if (o.rpm) rate = sr * Math.pow(2, o.rpm / 12);   // same samples, faster clock in = sped up
  const codes = sampleCodes(s, rate, o);
  codeCache.unshift({src, key, codes}); if (codeCache.length > 4) codeCache.pop();
  return codes;
}
function spStep(cents, sp12){
  const st = cents / 100;
  if (Math.abs(st - Math.round(st)) < 1e-6 && st >= -16 && st <= 15){ const b = TABLE[Math.round(st) + 16]; return sp12 ? ((b | 1) + 1) : b + 1; }
  return Math.max(1, Math.min(1024, Math.round(128 * Math.pow(2, cents / 1200))));
}
// play codes on one channel (0-7) and run its output: returns Float32Array at fs
function playChannel(codes, o, fs){
  const fo = fs * OS, clock = o.clock || CLOCK, step = spStep(o.cents || 0, o.sp12), ch = o.channel | 0;
  const last = codes.length - 5, frames = Math.floor((last - 5) * 128 / step) + 5;
  const len = Math.ceil((frames / clock + .02) * fo), y = new Float64Array(len);
  const level = Math.max(0, Math.min(255, Math.round((o.velocity == null ? 1 : o.velocity) * 255))) / 255;
  const tick = 1.2288e-3, decayTicks = o.decay == null || o.decay >= 1 ? 0 : Math.max(1, Math.round(o.decay * 400));
  const cal = CALIB[o.calib || 'manual'], slot = ch * SLOT;
  // the held DAC output with its steps at their true times, box-filtered into the 4x grid
  let held = 0, k = 0, tk = slot, lvl = level, nextTick = tick, ticks = 0;
  const val = idx => idx < last ? (codes[idx] - 2048) / 2048 : 0;
  for (let i = 0; i < len; i++){
    const t0 = i / fo, t1 = (i + 1) / fo; let acc = 0, tp = t0;
    while (tk < t1){
      acc += held * (tk - tp); tp = tk;
      if (decayTicks) while (nextTick <= tk){ nextTick += tick; if (++ticks % decayTicks === 0) lvl = Math.max(0, lvl - 1 / 255); }
      held = k < frames ? val(5 + ((k * step) >> 7)) * lvl : 0;
      k++; tk = slot + k / clock;
    }
    acc += held * (t1 - tp); y[i] = acc * fo;
  }
  // the output this channel is wired to
  const out = o.output | 0;
  if (out >= 3 && out <= 6){ const c = new Circuit(NETS['out' + out], fo, cal.fixedCap[out - 3]); for (let i = 0; i < len; i++) y[i] = c.process(y[i]); }
  else if (out <= 2){
    const env = new Envelope(fo, cal.dynCap), ssm = new SSM2044(fo, o.res == null ? cal.res : o.res), io = new Circuit(NETS.dynout, fo);
    const rest = o.rest || cal.rest, trig = Math.round((o.trigger || .0094) * fo), depth = o.depth == null ? 1 : o.depth;
    // full-scale hold output taken as 5 V; R114 33k / R117 200 divide it into pin 1, so loud sounds reach the
    // tanh region of the transconductance stages (about +-30 mV against 2 Vt = 52 mV)
    const pin1 = 5 * 200 / (33e3 + 200), ioNorm = -3.3e3 / 15e3 * cal.gain12;
    const onsets = o.retrigger ? onsetsOf(y, fo) : [];
    let next = 0, tEnd = trig;
    for (let i = 0; i < len; i++){
      if (next < onsets.length && onsets[next] === i){ tEnd = i + trig; next++; }
      env.gain = i < tEnd ? 0 : 5; env.step();
      const fc = Math.min(rest * Math.pow(2, depth * env.octaves()), fo * .45);
      const s = ssm.process(y[i] * pin1, fc) / pin1;
      // I/V stage: the current through R141 3.3k and the 10 uF into the 15k || 470 pF inverting stage
      y[i] = io.process(s * ioNorm);
    }
  }
  return decimate(y, Math.ceil(len / OS));
}
function onsetsOf(y, fo){   // pad-hit stand-ins inside a loop: sharp rises, at least 80 ms apart
  const out = []; let fe = 0, ss = 0, last = -1e9;
  for (let i = 0; i < y.length; i++){ const a = Math.abs(y[i]); fe = a > fe ? a : fe * (1 - 30 / fo); ss += (a - ss) * (2 / fo); if (fe > .05 && fe > ss * 4 && i - last > .08 * fo){ if (i) out.push(i); last = i; } }
  return out;
}

const api = {CLOCK, SLOT, TABLE, NETS, CALIB, Circuit, Envelope, SSM2044, compile, sampleCodes, codesFor, playChannel, spStep, decimate, lowpassFir, OS};
if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.TWSP = api;
})(typeof self !== 'undefined' ? self : this);
