/* Studio rack units, copied from The Omega System (../index.html): Lexicon 224 / AMS RMX16, Lexicon PrimeTime, Deltalab Effectron,
   Ensoniq DP/4, Alesis Microverb, A/DA Flanger, UREI 1176LN, gated room, Eventide H910, MXR 113 and helpers. */
/* FX rack. Every unit: mono input (like the hardware), stereo output, and it stops processing once its input
   has been silent for longer than its own tail. */
class RackUnit extends AudioWorkletProcessor{
  constructor(o){
    super(); this.fs = sampleRate; this.c = Object.assign({}, (o && o.processorOptions && o.processorOptions.cfg) || {});
    this.quiet = 1e12; this.asleep = true;
    this.port.onmessage = e => { if (e.data.type === 'cfg'){ Object.assign(this.c, e.data.cfg); this.update(); } };
  }
  update(){}
  tail(){ return 5; }
  reset(){}
  process(ins, outs){
    const inp = ins[0], L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length;
    const a = inp && inp.length ? inp[0] : null, b = inp && inp.length > 1 ? inp[1] : a;
    let has = false; if (a) for (let i = 0; i < N; i++) if (a[i] !== 0 || b[i] !== 0){ has = true; break; }
    if (has){ this.quiet = 0; if (this.asleep){ this.asleep = false; this.update(); } } else this.quiet += N;
    if (this.quiet > this.tail() * this.fs){
      if (!this.asleep){ this.asleep = true; this.reset(); }
      L.fill(0); if (R !== L) R.fill(0); return true;
    }
    this.run(a, b, L, R, N);
    return true;
  }
}
// shared parts
class DLine{
  constructor(n){ let m = 1; while (m < n + 4) m <<= 1; this.b = new Float32Array(m); this.m = m - 1; this.w = 0; }
  push(x){ this.b[this.w = (this.w + 1) & this.m] = x; }
  at(d){ return this.b[(this.w - d) & this.m]; }                     // integer delay, d >= 0
  lerp(d){ const i = Math.floor(d), f = d - i, a = this.b[(this.w - i) & this.m], b = this.b[(this.w - i - 1) & this.m]; return a + (b - a) * f; }
  clear(){ this.b.fill(0); }
}
const bq = {
  lp(fs, f, q){ const w = 2 * Math.PI * Math.min(f, fs * .45) / fs, al = Math.sin(w) / (2 * q), cw = Math.cos(w), a0 = 1 + al; return {b0:(1 - cw) / 2 / a0, b1:(1 - cw) / a0, b2:(1 - cw) / 2 / a0, a1:-2 * cw / a0, a2:(1 - al) / a0, z1:0, z2:0}; },
  hp(fs, f, q){ const w = 2 * Math.PI * Math.min(f, fs * .45) / fs, al = Math.sin(w) / (2 * q), cw = Math.cos(w), a0 = 1 + al; return {b0:(1 + cw) / 2 / a0, b1:-(1 + cw) / a0, b2:(1 + cw) / 2 / a0, a1:-2 * cw / a0, a2:(1 - al) / a0, z1:0, z2:0}; },
  run(q, x){ const y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
};
const tnh = x => x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x);
const sat = x => x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x);

/* Lexicon 224 / AMS RMX16.
   224 plate/hall/room: the Griesinger-style figure-eight tank (input diffusers, two cross-coupled halves with a modulated
   all-pass, damping and a second all-pass each, stereo taps all along both halves), as published by Dattorro (1997) from
   Lexicon practice. Original rate 29.8 kHz, delays scaled to ours; band-limited and quantized like the 224's converters.
   RMX16 Non-Lin 2 / Reverse / Ambience: diffused input into a dense multi-tap line whose envelope is set by the program
   (flat then cut off, rising then cut off, or a fast exponential), like the RMX16's tap-gain programs. */
class Verb224 extends RackUnit{
  constructor(o){
    super(o); const fs = this.fs, sc = fs / 29761; this.sc = sc;
    const D = n => new DLine(Math.ceil(n * sc * 1.6) + 64);
    this.pre = new DLine(Math.ceil(fs * .26));
    this.ind = [142, 107, 379, 277].map(n => ({d:D(n), n}));
    this.t = {apL:D(672 + 40), dL1:D(4453), apL2:D(1800), dL2:D(3720), apR:D(908 + 40), dR1:D(4217), apR2:D(2656), dR2:D(3163)};
    this.bw = 0; this.dmL = 0; this.dmR = 0; this.lfo = 0; this.fbL = 0; this.fbR = 0;
    this.taps = new DLine(Math.ceil(fs * 1.3)); this.tapSet = null;
    this.inLp = bq.lp(fs, 8000, .54); this.inLp2 = bq.lp(fs, 8000, 1.3); this.outL = bq.lp(fs, 8000, .7); this.outR = bq.lp(fs, 8000, .7);
    this.update();
  }
  reset(){ this.pre.clear(); this.ind.forEach(a => a.d.clear()); Object.values(this.t).forEach(d => d.clear()); this.taps.clear(); this.fbL = this.fbR = this.bw = this.dmL = this.dmR = 0; }
  tail(){ const C = this.c; return (this.tapSet ? 1.4 : 1 + 2 * this.rt) + (C.pre || 0) / 1000; }
  update(){
    const C = this.c, fs = this.fs, P = C.prog || 'vplate', size = C.size == null ? .5 : C.size;
    // 224 programs run at 20 kHz / 12 bit with ~8 kHz bandwidth; the RMX16 is 16 bit with 18 kHz bandwidth
    const cfg = {vplate:{scale:1, rt:.6 + size * 5, dd1:.7, lex:1}, pplate:{scale:.8, rt:.4 + size * 3, dd1:.75, lex:1},
      hall:{scale:1.35, rt:1 + size * 7, dd1:.6, lex:1}, room:{scale:.55, rt:.25 + size * 1.6, dd1:.6, lex:1}, chamber:{scale:.8, rt:.5 + size * 3, dd1:.65, lex:1},
      rplate:{scale:.9, rt:.5 + size * 4.5, dd1:.7, lex:0}, rhall:{scale:1.3, rt:1 + size * 6, dd1:.62, lex:0}}[P] || null;
    const bw = cfg && !cfg.lex || ['nonlin', 'reverse', 'ambience'].includes(P) ? 16000 : 8000;
    for (const f of [this.inLp, this.inLp2, this.outL, this.outR]) Object.assign(f, bq.lp(fs, bw, .7), {z1:f.z1, z2:f.z2});
    this.lex = cfg ? cfg.lex : 0;
    // TREBLE DECAY: high-frequency damping inside the tank (1 = bright)
    this.damp = .05 + (1 - (C.treble == null ? .6 : C.treble)) * .6;
    if (cfg){
      this.scale = cfg.scale; this.rt = cfg.rt;
      const half = (672 + 4453 + 1800 + 3720) / 29761 * cfg.scale;
      this.decay = Math.pow(10, -3 * half / cfg.rt); this.dd1 = cfg.dd1; this.dd2 = Math.max(.25, Math.min(.5, this.decay + .15));
      this.tapSet = null;
    } else {
      // RMX16 tap programs: length from SIZE (Non-Lin 2 runs 100..600 ms)
      const len = (P === 'ambience' ? .04 + size * .25 : .1 + size * .5) * fs, n = 96, taps = [];
      let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
      for (let i = 0; i < n; i++){
        const x = (i + rnd() * .9) / n, d = Math.max(1, Math.floor(x * len));
        const env = P === 'nonlin' ? (1 - .1 * x) * Math.min(1, (1 - x) * len / fs / .02) : P === 'reverse' ? x * x : Math.exp(-x * 5);
        taps.push({d, gL:env * (rnd() - .5) * 2, gR:env * (rnd() - .5) * 2});
      }
      const norm = 1.4 / Math.sqrt(taps.reduce((s, t) => s + t.gL * t.gL, 0));
      taps.forEach(t => { t.gL *= norm; t.gR *= norm; }); this.tapSet = taps; this.rt = len / fs;
    }
  }
  ap(dl, n, g, x){ const v = dl.at(n); const w = x + g * v; dl.push(w); return v - g * w; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, sc = this.sc * (this.scale || 1), preN = Math.max(0, Math.min(this.pre.m - 2, Math.round((C.pre || 0) / 1000 * fs)));
    const ret = C.ret == null ? .8 : C.ret, t = this.t, taps = this.tapSet;
    const n1 = 142 * this.sc, n2 = 107 * this.sc, n3 = 379 * this.sc, n4 = 277 * this.sc;
    const dl1 = 4453 * sc, al2 = 1800 * sc, dl2 = 3720 * sc, dr1 = 4217 * sc, ar2 = 2656 * sc, dr2 = 3163 * sc, exc = 16 * this.sc;
    const k = s => Math.round(s * sc);
    for (let i = 0; i < N; i++){
      let x = a ? (a[i] + b[i]) * .5 : 0;
      // converters: 9.5 kHz anti-alias filter, 12-bit-ish quantizing
      x = bq.run(this.inLp2, bq.run(this.inLp, x)); if (this.lex) x = Math.round(x * 2048) / 2048;
      this.pre.push(x); x = this.pre.at(preN);
      let yl = 0, yr = 0;
      if (taps){
        this.taps.push(this.ap(this.ind[1].d, n2, .75, this.ap(this.ind[0].d, n1, .75, x)));
        for (let j = 0; j < taps.length; j++){ const v = this.taps.at(taps[j].d); yl += v * taps[j].gL; yr += v * taps[j].gR; }
      } else {
        this.bw += .7 * (x - this.bw);
        let d = this.ap(this.ind[0].d, Math.round(n1), .75, this.bw); d = this.ap(this.ind[1].d, Math.round(n2), .75, d);
        d = this.ap(this.ind[2].d, Math.round(n3), .625, d); d = this.ap(this.ind[3].d, Math.round(n4), .625, d);
        this.lfo += 2 * Math.PI * 1 / fs; if (this.lfo > 6.2832) this.lfo -= 6.2832;
        const mL = 672 * sc + exc * Math.sin(this.lfo), mR = 908 * sc + exc * Math.cos(this.lfo);
        // left half
        let u = d + this.fbR, v = t.apL.lerp(mL); let w = u + this.dd1 * v; t.apL.push(w); u = v - this.dd1 * w;
        t.dL1.push(u); u = t.dL1.at(Math.round(dl1));
        this.dmL += (1 - this.damp) * (u - this.dmL); u = this.dmL * this.decay;
        u = this.ap(t.apL2, Math.round(al2), -this.dd2, u); t.dL2.push(u); const outL = t.dL2.at(Math.round(dl2)) * this.decay;
        // right half
        let r = d + this.fbL; v = t.apR.lerp(mR); w = r + this.dd1 * v; t.apR.push(w); r = v - this.dd1 * w;
        t.dR1.push(r); r = t.dR1.at(Math.round(dr1));
        this.dmR += (1 - this.damp) * (r - this.dmR); r = this.dmR * this.decay;
        r = this.ap(t.apR2, Math.round(ar2), -this.dd2, r); t.dR2.push(r); const outR = t.dR2.at(Math.round(dr2)) * this.decay;
        this.fbL = outL; this.fbR = outR;
        yl = .6 * (t.dR1.at(k(266)) + t.dR1.at(k(2974)) - t.apR2.at(k(1913)) + t.dR2.at(k(1996)) - t.dL1.at(k(1990)) - t.apL2.at(k(187)) - t.dL2.at(k(1066)));
        yr = .6 * (t.dL1.at(k(353)) + t.dL1.at(k(3627)) - t.apL2.at(k(1228)) + t.dL2.at(k(2673)) - t.dR1.at(k(2111)) - t.apR2.at(k(335)) - t.dR2.at(k(121)));
      }
      L[i] = bq.run(this.outL, yl) * ret; R[i] = bq.run(this.outR, yr) * ret;
    }
  }
}
registerProcessor('verb224', Verb224);
// low-pass of n poles (pairs of biquads + an optional single pole), used as anti-alias and reconstruction filters
class LPN{
  constructor(fs, f, n, qs){ this.fs = fs; this.n = n; this.qs = qs; this.set(f); }
  set(f){ if (f === this.f) return; this.f = f; const qs = this.qs || [.54, .65, 1.3]; this.b = []; for (let k = 0; k < Math.floor(this.n / 2); k++) this.b.push(bq.lp(this.fs, f, qs[k] || .707)); this.a = this.n % 2 ? 1 - Math.exp(-2 * Math.PI * Math.min(f, this.fs * .45) / this.fs) : 0; this.y = this.y || 0; }
  run(x){ for (const q of this.b) x = bq.run(q, x); if (this.a){ this.y += this.a * (x - this.y); x = this.y; } return x; }
}
// sample-and-hold at the unit's own clock (it runs slower than ours on the long ranges)
class Hold{ constructor(){ this.ph = 1; this.v = 0; } run(x, ratio){ this.ph += ratio; if (this.ph >= 1){ this.ph -= Math.floor(this.ph); this.v = x; } return this.v; } }
const tailOf = (ms, fb) => Math.min(20, ms / 1000 * (fb > .01 ? Math.log(1e-4) / Math.log(Math.min(.98, Math.abs(fb))) : 1) + .3);

/* Lexicon PrimeTime Model 93: two taps (A left, B right), each up to 128 ms x DELAY MULTIPLY (1, 2, 4, 8), and the
   multiplier also sets the converter bandwidth (12 / 6 / 3 / 1.5 kHz, 30 dB/oct). VCO sweeps the clock over 2:1
   (triangle, 0.1-20 Hz); recirculation through a 6 dB/oct low-pass (800 Hz-15 kHz), optionally inverted. */
class PrimeTime extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = new DLine(Math.ceil(fs * 2.1)); this.aa = new LPN(fs, 12000, 5); this.rc = new LPN(fs, 12000, 5); this.h = new Hold(); this.lfo = 0; this.fl = 0; this.yA = 0; this.yB = 0; this.update(); }
  reset(){ this.dl.clear(); this.fl = this.yA = this.yB = 0; }
  tail(){ const C = this.c; return tailOf(Math.max(C.dA || 0, C.dB || 0), Math.max(C.fbA || 0, C.fbB || 0)); }
  update(){ const C = this.c, bw = 12000 / (C.mult || 1); this.aa.set(bw); this.rc.set(bw); }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, bw = 12000 / (C.mult || 1), clk = bw * 2.6 / fs, q = 2048;
    const fa = 1 - Math.exp(-2 * Math.PI * (C.hcut || 6000) / fs), inv = C.inv ? -1 : 1, ret = C.ret == null ? .8 : C.ret;
    for (let i = 0; i < N; i++){
      // VCO: triangle on the clock, 2:1 range -> delay scales between 1 and 0.5
      this.lfo += (C.vcoRate || .5) / fs; if (this.lfo >= 1) this.lfo -= 1;
      const tri = 1 - 2 * Math.abs(2 * this.lfo - 1), k = 1 - .5 * (C.vcoDepth || 0) * (tri * .5 + .5);
      const x = (a ? (a[i] + b[i]) * .5 : 0) + this.fl * inv;
      let w = this.aa.run(x); w = this.h.run(w, clk); w = Math.round(sat(w) * q) / q;
      this.dl.push(w);
      this.yA = this.rc.run(this.dl.lerp(Math.max(1, (C.dA || 0) / 1000 * fs * k)));
      const yB = this.dl.lerp(Math.max(1, (C.dB || 0) / 1000 * fs * k)); this.yB += (yB - this.yB) * .5;
      this.fl += fa * (this.yA * (C.fbA || 0) + this.yB * (C.fbB || 0) - this.fl);
      L[i] = this.yA * (C.mixA == null ? 1 : C.mixA) * ret; R[i] = this.yB * (C.mixB == null ? 1 : C.mixB) * ret;
    }
  }
}
registerProcessor('primetime', PrimeTime);

/* Deltalab Effectron ADM 1024: 1-bit adaptive delta modulation with pre/de-emphasis. The bit clock (250 kHz-1 MHz,
   swept by the LFO up to 5:1) sets the delay for each range's memory, so long delays run the coder slowly and get
   gritty. Feedback (+ or - phase) through a 12 kHz low-pass. Output can sit centre, to one side (Haas) or as the
   phase-shifted "stereo simulation" pair. */
const EF_BITS = {fhi:256, fmid:1024, flo:4096, dbl:16384, short:65536, long:262144};
class Effectron extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = new DLine(Math.ceil(fs * 4.2)); this.est = 0; this.step = .001; this.hist = 0; this.acc = 0; this.lfo = 0; this.fb = 0;
    this.pre = {x:0}; this.de = 0; this.rec = new LPN(fs, 15000, 4); this.fbl = 0; this.update(); }
  reset(){ this.dl.clear(); this.est = this.fb = this.fbl = this.de = 0; this.step = .001; }
  tail(){ const C = this.c; return tailOf(C.delay || 0, C.regen || 0) * 5; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, bits = EF_BITS[C.range] || EF_BITS.dbl, ret = C.ret == null ? .8 : C.ret;
    const fbA = 1 - Math.exp(-2 * Math.PI * 12000 / fs), grit = C.grit == null ? .5 : C.grit;
    for (let i = 0; i < N; i++){
      this.lfo += (C.lfoRate || .5) / fs; if (this.lfo >= 1) this.lfo -= 1;
      const sweep = Math.pow(5, -(C.width || 0) * (.5 + .5 * Math.sin(2 * Math.PI * this.lfo)));
      const dMs = Math.max(.25, (C.delay || 30) * sweep), clock = Math.min(1e6, bits / (dMs / 1000));
      // pre-emphasis (+~10 dB above 3 kHz) into the delta coder
      let x = (a ? (a[i] + b[i]) * .5 : 0) + this.fbl;
      const pe = x + 2.2 * (x - this.pre.x); this.pre.x = x;
      // ADM: one bit per clock tick, step grows on runs of 3 equal bits and shrinks otherwise; grit = slower syllabic recovery
      this.acc += clock / fs; let sum = 0, n = 0;
      const up = 1.35 + grit * .4, down = .9 - grit * .06, smin = 2e-4 * (1 + grit * 6);
      while (this.acc >= 1){ this.acc -= 1;
        const bit = pe >= this.est ? 1 : 0; this.hist = ((this.hist << 1) | bit) & 7;
        this.step = (this.hist === 7 || this.hist === 0) ? Math.min(.3, this.step * up) : Math.max(smin, this.step * down);
        this.est = this.est * .9998 + (bit ? this.step : -this.step); sum += this.est; n++; }
      const dec = n ? sum / n : this.est;
      // de-emphasis and the 15 kHz output filter, then the delay itself
      this.de += (dec - this.de) * .31; this.dl.push(this.de);
      const y = this.rec.run(this.dl.lerp(Math.max(1, dMs / 1000 * fs)));
      this.fbl += fbA * (y * (C.regen || 0) - this.fbl);
      const v = y * ret, m = C.pan || 'center';
      if (m === 'haasL'){ L[i] = v; R[i] = 0; } else if (m === 'haasR'){ L[i] = 0; R[i] = v; } else if (m === 'wide'){ L[i] = v; R[i] = -v; } else { L[i] = v * .7; R[i] = v * .7; }
    }
  }
}
registerProcessor('effectron', Effectron);
/* Ensoniq DP/4: four independent effect units (one ESP DSP each, 24-bit fixed point), routed Parallel (all four on the
   input, outputs summed), Series (1 > 2 > 3 > 4) or 2 x 2 (1 > 2 and 3 > 4 in parallel). Each unit runs one algorithm. */
const DP_Q = 1 / 8388608, dpq = x => Math.round(x / DP_Q) * DP_Q;    // 24-bit word
class DPUnit {
  constructor(fs){ this.fs = fs; this.alg = 'off'; this.dl = [new DLine(Math.ceil(fs * 2.1)), new DLine(Math.ceil(fs * 2.1))]; this.ph = 0; this.lp = [0, 0]; this.ap = new Float32Array(16); this.fb = [0, 0];
    const cm = [1116, 1188, 1277, 1356, 1422, 1491], s = fs / 44100; this.combs = [0, 1].map(c => cm.map(n => ({d:new DLine(Math.ceil(n * s * 2.2 + 50)), n:Math.round(n * s), f:0}))); this.aps = [0, 1].map(c => [556, 441, 341, 225].map(n => ({d:new DLine(Math.ceil(n * s + 30)), n:Math.round(n * s * (1 + c * .04))}))); this.pre = [new DLine(Math.ceil(fs * .2)), new DLine(Math.ceil(fs * .2))]; this.pp = 0; this.sh = [0, 0]; this.env = 0; }
  clear(){ this.dl.forEach(d => d.clear()); this.combs.flat().forEach(c => { c.d.clear(); c.f = 0; }); this.aps.flat().forEach(a => a.d.clear()); this.pre.forEach(d => d.clear()); this.fb = [0, 0]; this.lp = [0, 0]; this.ap.fill(0); }
  // x: parameter 1 (0..1), y: parameter 2 (0..1)
  run(l, r, x, y){
    const fs = this.fs, A = this.alg, out = this.sh;
    if (A === 'hall' || A === 'plate' || A === 'room'){
      const size = A === 'hall' ? 1 + x * 1.2 : A === 'plate' ? .7 + x * .7 : .35 + x * .5, damp = A === 'plate' ? .15 + y * .35 : .25 + y * .5, fbk = Math.min(.97, .7 + .26 * Math.min(1, size / 2.2));
      const pd = Math.round((A === 'hall' ? .03 : A === 'room' ? .005 : .012) * fs);
      for (let c = 0; c < 2; c++){ const P = this.pre[c]; P.push(c ? r : l); let inp = P.at(pd) * .3, sum = 0;
        for (const cb of this.combs[c]){ const n = Math.round(cb.n * size), o = cb.d.at(n); cb.f = o * (1 - damp) + cb.f * damp; cb.d.push(inp + cb.f * fbk); sum += o; }
        for (const a of this.aps[c]){ const o = a.d.at(a.n); a.d.push(sum + o * .5); sum = o - sum * .5; }
        out[c] = sum * (A === 'plate' ? .9 : .7); }
      return out;
    }
    if (A === 'chorus' || A === 'flange'){
      this.ph += (.08 + x * (A === 'chorus' ? 2.5 : 1.2)) / fs; if (this.ph > 1) this.ph -= 1;
      for (let c = 0; c < 2; c++){ const lfo = Math.sin(2 * Math.PI * (this.ph + c * .25)), base = A === 'chorus' ? .012 : .0025, dep = A === 'chorus' ? .004 : .002 * (.3 + y);
        const d = (base + dep * (1 + lfo) * .5) * fs, v = this.dl[c].lerp(d); this.dl[c].push((c ? r : l) + (A === 'flange' ? v * (y * 1.7 - .85) : 0)); out[c] = A === 'chorus' ? v * (.5 + y * .5) : v; }
      return out;
    }
    if (A === 'phaser'){
      this.ph += (.05 + x * 3) / fs; if (this.ph > 1) this.ph -= 1;
      for (let c = 0; c < 2; c++){ const sw = .5 - .5 * Math.cos(2 * Math.PI * (this.ph + c * .2)), f = 150 * Math.pow(25, sw), t = Math.tan(Math.PI * f / fs), a1 = (t - 1) / (t + 1);
        let v = (c ? r : l) + this.fb[c] * (y * .9); for (let s = 0; s < 6; s++){ const k = c * 8 + s, o = a1 * v + this.ap[k]; this.ap[k] = v - a1 * o; v = o; } this.fb[c] = v; out[c] = ((c ? r : l) + v) * .6; }
      return out;
    }
    if (A === 'delay' || A === 'pingpong'){
      const d = Math.max(1, (.02 + x * 1.18) * fs), fbk = y * .9;
      if (A === 'delay'){ for (let c = 0; c < 2; c++){ const v = this.dl[c].at(Math.round(d * (c ? 1.02 : 1))); this.lp[c] += (v - this.lp[c]) * .35; this.dl[c].push((c ? r : l) + this.lp[c] * fbk); out[c] = v; } }
      else { const vL = this.dl[0].at(Math.round(d)), vR = this.dl[1].at(Math.round(d)); this.dl[0].push((l + r) * .5 + vR * fbk); this.dl[1].push(vL * fbk); out[0] = vL; out[1] = vR; }
      return out;
    }
    if (A === 'pitch'){
      // two crossfaded read heads sweeping through a 60 ms window
      const semis = Math.round((x - .5) * 24), ratio = Math.pow(2, semis / 12), W = .06 * fs; this.pp = (this.pp + (1 - ratio) + W) % W;
      for (let c = 0; c < 2; c++){ this.dl[c].push(c ? r : l); const d1 = this.pp + 2, d2 = (this.pp + W / 2) % W + 2, g1 = Math.sin(Math.PI * this.pp / W), g2 = Math.sin(Math.PI * ((this.pp + W / 2) % W) / W);
        out[c] = (this.dl[c].lerp(d1) * g1 + this.dl[c].lerp(d2) * g2) * .8 + (c ? r : l) * y * .5; }
      return out;
    }
    if (A === 'dist'){
      const g = 1 + x * 40, tone = .05 + y * .6;
      for (let c = 0; c < 2; c++){ const v = Math.tanh((c ? r : l) * g) * .6; this.lp[c] += (v - this.lp[c]) * tone; out[c] = this.lp[c]; }
      return out;
    }
    if (A === 'rotary'){
      this.ph += (.7 + x * 6) / fs; if (this.ph > 1) this.ph -= 1; const m = (l + r) * .5, s = Math.sin(2 * Math.PI * this.ph), cs = Math.cos(2 * Math.PI * this.ph);
      this.dl[0].push(m); const v = this.dl[0].lerp((.002 + .0012 * (1 + s)) * fs); out[0] = v * (1 + .5 * y * cs) * .7; out[1] = v * (1 - .5 * y * cs) * .7; return out;
    }
    if (A === 'filter'){
      this.ph += (.05 + x * 4) / fs; if (this.ph > 1) this.ph -= 1;
      for (let c = 0; c < 2; c++){ const f = 120 * Math.pow(60, .5 - .5 * Math.cos(2 * Math.PI * this.ph)), g = 2 * Math.sin(Math.PI * Math.min(f, fs / 6) / fs), q = 1.9 - y * 1.75, k = c * 8;
        const hp = (c ? r : l) - this.ap[k] - q * this.ap[k + 1]; this.ap[k + 1] += g * hp; this.ap[k] += g * this.ap[k + 1]; out[c] = this.ap[k] * .8; }
      return out;
    }
    out[0] = 0; out[1] = 0; return out;
  }
}
class DP4 extends RackUnit{
  constructor(o){ super(o); this.u = [0, 1, 2, 3].map(() => new DPUnit(this.fs)); this.update(); }
  update(){ const C = this.c; for (let k = 0; k < 4; k++){ const a = C['a' + (k + 1)] || 'off'; if (this.u[k].alg !== a){ this.u[k].alg = a; this.u[k].clear(); } } }
  reset(){ this.u.forEach(u => u.clear()); }
  tail(){ return 8; }
  run(a, b, L, R, N){
    const C = this.c, cfg = C.cfg || 'par', ret = C.ret == null ? .8 : C.ret, U = this.u, v = this.v || (this.v = new Float64Array(2)), w = this.w2 || (this.w2 = new Float64Array(2));
    const par = k => [C['m' + k] == null ? 1 : C['m' + k], C['x' + k] == null ? .5 : C['x' + k], C['y' + k] == null ? .5 : C['y' + k]], P = [1, 2, 3, 4].map(par);
    const step = (k, o) => { const u = U[k]; if (u.alg === 'off') return; const [m, x, y] = P[k], r2 = u.run(o[0], o[1], x, y); o[0] = dpq(o[0] * (1 - m) + r2[0] * m); o[1] = dpq(o[1] * (1 - m) + r2[1] * m); };
    for (let i = 0; i < N; i++){
      const l = a ? a[i] : 0, r = b ? b[i] : 0; let oL = 0, oR = 0;
      if (cfg === 'ser'){ v[0] = l; v[1] = r; for (let k = 0; k < 4; k++) step(k, v); oL = v[0]; oR = v[1]; }
      else if (cfg === '2x2'){ v[0] = l; v[1] = r; step(0, v); step(1, v); w[0] = l; w[1] = r; step(2, w); step(3, w); oL = (v[0] + w[0]) * .5; oR = (v[1] + w[1]) * .5; }
      else { let n = 0; for (let k = 0; k < 4; k++){ if (U[k].alg === 'off') continue; v[0] = l; v[1] = r; step(k, v); oL += v[0]; oR += v[1]; n++; } if (n){ oL /= Math.sqrt(n); oR /= Math.sqrt(n); } }
      L[i] = oL * ret; R[i] = oR * ret;
    }
  }
}
registerProcessor('dp4', DP4);
/* Alesis Microverb: 16 programs (rooms, halls, plates, gated, reverse), 16-bit, band-limited like its ~31 kHz converters. */
class Microverb extends RackUnit{
  constructor(o){ super(o); const fs = this.fs, sc = fs / 44100; this.cb = [0, 1].map(c => [1557, 1617, 1491, 1422, 1277, 1356].map(n => ({d:new DLine(Math.ceil(n * sc * 2.6) + 10), n:Math.round(n * sc * (1 + c * .023)), f:0})));
    this.ap = [0, 1].map(c => [225, 556, 441, 341].map(n => ({d:new DLine(Math.ceil(n * sc) + 10), n:Math.round(n * sc * (1 + c * .03))}))); this.pre = [new DLine(Math.ceil(fs * 1.2)), new DLine(Math.ceil(fs * 1.2))]; this.bl = [0, 0, 0, 0]; this.gate = 0; this.update(); }
  reset(){ this.cb.flat().forEach(c => { c.d.clear(); c.f = 0; }); this.ap.flat().forEach(a => a.d.clear()); this.pre.forEach(p => p.clear()); }
  tail(){ return 6; }
  run(a, b, L, R, N){
    const C = this.c, P = (C.prog | 0), ret = C.ret == null ? .8 : C.ret, fs = this.fs;
    // program table: size, damping, pre-delay (ms), kind (0 normal, 1 gated, 2 reverse)
    const T = [[.45, .5, 2, 0], [.6, .45, 6, 0], [.8, .4, 12, 0], [1, .45, 18, 0], [1.25, .4, 25, 0], [1.55, .35, 35, 0], [.9, .15, 5, 0], [1.1, .55, 8, 0],
      [1.8, .3, 45, 0], [2.2, .25, 60, 0], [.7, .3, 0, 1], [1, .25, 0, 1], [.8, .3, 0, 2], [1.2, .3, 0, 2], [.35, .6, 0, 0], [1.4, .7, 20, 0]][P] || [1, .4, 10, 0];
    const [size, damp, pd, kind] = T, fbk = Math.min(.975, .72 + size * .14), pdS = Math.round(pd / 1000 * fs), bw = 1 - Math.exp(-2 * Math.PI * 11000 / fs);
    for (let i = 0; i < N; i++){
      const inL = a ? a[i] : 0, inR = b ? b[i] : 0; let lev = Math.abs(inL) + Math.abs(inR); this.gate = Math.max(lev > .01 ? 1 : 0, this.gate * Math.exp(-1 / (fs * .25)));
      const out = this.o || (this.o = [0, 0]);
      for (let c = 0; c < 2; c++){
        const P2 = this.pre[c]; let x = c ? inR : inL; this.bl[c] += (x - this.bl[c]) * bw; P2.push(this.bl[c]);
        let inp;
        if (kind === 2){ const len = Math.round(fs * .35 * size); let acc = 0; for (let k = 0; k < 4; k++){ const d = Math.round(len * (k + 1) / 4); acc += P2.at(d) * (k + 1) / 4; } inp = acc * .25; }
        else inp = P2.at(pdS) * .3;
        let s = 0; for (const cb of this.cb[c]){ const o = cb.d.at(Math.round(cb.n * size)); cb.f = o * (1 - damp) + cb.f * damp; cb.d.push(inp + cb.f * fbk); s += o; }
        for (const ap of this.ap[c]){ const o = ap.d.at(ap.n); ap.d.push(s + o * .5); s = o - s * .5; }
        if (kind === 1) s *= this.gate;
        out[c] = Math.round(s * .45 * 32767) / 32767;
      }
      L[i] = out[0] * ret; R[i] = out[1] * ret;
    }
  }
}
registerProcessor('microverb', Microverb);
/* A/DA Flanger (1977): input buffer and THRESHOLD noise gate, NE570 compander around a Reticon SAD1024 bucket-brigade
   line (512 stages: delay = 512 / (2 f_clk)), clocked by a VCO the triangle LFO sweeps exponentially over up to 40:1
   (0.25-10 ms). The BBD samples at its own clock (aliasing and a falling bandwidth on long delays), loses a little charge
   per transfer, adds hiss the expander pushes down; anti-alias and reconstruction low-passes either side. ENHANCE is the
   regeneration, EVEN / ODD its polarity; the output mixes the dry and delayed signals equally for the deepest notches. */
class ADAFlanger extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = new DLine(Math.ceil(fs * .03)); this.aa = new LPN(fs, 11000, 4); this.rc = new LPN(fs, 11000, 4); this.hi = new Hold(); this.ho = new Hold();
    this.lfo = .25; this.fb = 0; this.ce = 1e-3; this.ee = 1e-3; this.gate = 0; this.ge = 0; this.dc = 0; this.ldel = 0; }
  reset(){ this.dl.clear(); this.fb = 0; this.ce = this.ee = 1e-3; this.gate = this.ge = 0; }
  tail(){ return .6 + Math.abs(this.c.enh || 0) * 2; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, ret = C.ret == null ? .8 : C.ret, man = C.manual == null ? .4 : C.manual, rng = C.range == null ? .6 : C.range;
    const rate = .02 * Math.pow(400, C.speed == null ? .3 : C.speed), enh = Math.min(.97, C.enh || 0) * (C.odd ? -1 : 1);
    const thr = C.thresh ? Math.pow(10, (-80 + C.thresh * 60) / 20) : 0, mix = C.mix == null ? .5 : C.mix, st = !!C.stereo;
    const ca = 1 - Math.exp(-1 / (fs * .012)), ga = 1 - Math.exp(-1 / (fs * .002)), gr = 1 - Math.exp(-1 / (fs * .15));
    for (let i = 0; i < N; i++){
      const x0 = a ? (a[i] + b[i]) * .5 : 0;
      // THRESHOLD: gate on the input level, fast open, slow close
      const lv = Math.abs(x0); this.ge += (lv > this.ge ? ga : gr) * (lv - this.ge); const go = !thr || this.ge > thr ? 1 : 0; this.gate += (go - this.gate) * (go ? .01 : .0004);
      const x = x0 * this.gate;
      // LFO triangle -> exponential clock sweep; MANUAL sets the longest delay, RANGE how far up the sweep goes (40:1 max)
      this.lfo += rate / fs; if (this.lfo >= 1) this.lfo -= 1; const tri = 1 - 2 * Math.abs(2 * this.lfo - 1);
      const dMax = .25 * Math.pow(40, man), dMs = Math.max(.25, dMax / Math.pow(1 + 39 * rng, tri * .5 + .5)), clk = 512 / (2 * dMs / 1000);
      this.ldel += (dMs - this.ldel) * .002;
      // compressor (2:1): divide by the square root of the average level
      let v = this.aa.run(x + sat(this.fb));
      this.ce += ca * (Math.abs(v) - this.ce); v = v / Math.sqrt(Math.max(this.ce, 1e-4)) * .1;
      // BBD: sampled at its clock, held, delayed, charge loss, hiss
      const r = clk / fs; v = r < 1 ? this.hi.run(v, r) : v; this.dl.push(v);
      let y = this.dl.lerp(Math.max(1, this.ldel / 1000 * fs)) * .985 + (Math.random() - .5) * 2e-4;
      y = r < 1 ? this.ho.run(y, r) : y; y = this.rc.run(y);
      // expander: multiply by its own level (undoes the 2:1)
      this.ee += ca * (Math.abs(y) - this.ee); y = y * this.ee * 100;
      this.dc += (y - this.dc) * .0005; y -= this.dc;
      this.fb = y * enh;
      const wet = y * mix * 1.4, dry = x * (1 - mix) * 1.4;
      L[i] = (dry + wet) * ret; R[i] = (dry + (st ? -wet : wet)) * ret;
    }
  }
}
registerProcessor('adaflanger', ADAFlanger);
/* UREI 1176LN (rev D/E), circuit-level: input transformer (low-end bloom and gentle saturation) -> the FET gain cell, a
   2N5457 used as a voltage-controlled resistor in a shunt attenuator, so its distortion (mostly 2nd harmonic) grows with
   the gain reduction -> class-A line amp and output transformer (asymmetric soft clip). It is a FEEDBACK design: the side
   chain listens after the gain cell, through the ratio divider (4, 8, 12, 20; all buttons in biases it into a ~20:1 snarl
   with overshoot and extra distortion). Attack 20-800 us, release 50 ms-1.1 s with the program-dependent second time
   constant; knobs run 1-7 like the hardware (7 = fastest). The threshold is fixed, so INPUT sets how hard it works. */
class UREI1176 extends RackUnit{
  constructor(o){ super(o); this.gr = 0; this.slow = 0; this.yp = 0; this.hpL = 0; this.hpR = 0; this.dcL = 0; this.dcR = 0; this.ins = !!(o && o.processorOptions && o.processorOptions.insert); this.meter = 0; }
  reset(){ this.gr = this.slow = this.yp = 0; }
  tail(){ return this.ins ? 1e9 : 2; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, inG = Math.pow(10, ((C.input == null ? .5 : C.input) * 50 - 20) / 20), outG = Math.pow(10, ((C.output == null ? .5 : C.output) * 40 - 30) / 20);
    const all = C.ratio === 'all', ratio = all ? 20 : +(C.ratio || 4), thr = .126;                      // fixed threshold about -18 dBFS
    const atk = (20 + (7 - (C.attack == null ? 4 : C.attack)) / 6 * 780) * 1e-6 * (all ? 1.6 : 1), rel = (.05 + (7 - (C.release == null ? 4 : C.release)) / 6 * 1.05);
    const aA = 1 - Math.exp(-1 / (fs * atk)), aR = 1 - Math.exp(-1 / (fs * rel)), aS = 1 - Math.exp(-1 / (fs * rel * 5)), hpA = 1 - Math.exp(-2 * Math.PI * 18 / fs);
    const mix = C.mix == null ? 1 : C.mix, slope = 1 - 1 / ratio;
    for (let i = 0; i < N; i++){
      let l = a ? a[i] : 0, r = b ? b[i] : l; const dl = l, dr = r;
      // input transformer: coupling high-pass and core saturation on big low end
      this.hpL += hpA * (l - this.hpL); this.hpR += hpA * (r - this.hpR); l -= this.hpL * .9; r -= this.hpR * .9;
      l = tnh(l * .5) / .5 * inG; r = tnh(r * .5) / .5 * inG;                                             // transformer sees line level; INPUT pot after it
      // side chain: the rectified audio (no RMS smoothing, so fast settings ripple on bass like the real one). The loop is a
      // feedback loop, which settles at over x (1 - 1/ratio) and moves ratio times faster than its RC network alone
      const lv = Math.max(Math.abs(l), Math.abs(r), 1e-6), over = 20 * Math.log10(lv / thr);
      const tgt = over > 0 ? Math.min(40, over * slope * (all ? 1.25 : 1) + (all ? 2 : 0)) : 0;
      if (tgt > this.gr) this.gr += Math.min(.9, aA * ratio * .3) * (tgt - this.gr); else { this.slow += aS * (this.gr - this.slow); this.gr += aR * (Math.max(tgt, this.slow * .6) - this.gr); }
      // FET cell: gain from the side-chain voltage, 2nd-harmonic distortion rising with the reduction
      const g = Math.pow(10, -this.gr / 20), d2 = Math.min(.25, this.gr * .006) * (all ? 2.2 : 1);
      let yl = l * g, yr = r * g; yl += d2 * yl * yl; yr += d2 * yr * yr;
      // line amp and output transformer: asymmetric soft clip, DC blocked
      yl = tnh(yl * 1.2 + .1 * yl * yl) / 1.2 * outG * 2.7; yr = tnh(yr * 1.2 + .1 * yr * yr) / 1.2 * outG * 2.7;
      this.dcL += (yl - this.dcL) * .0004; this.dcR += (yr - this.dcR) * .0004; yl -= this.dcL; yr -= this.dcR;
      const ret = this.ins ? 1 : (C.ret == null ? .8 : C.ret);
      L[i] = (yl * mix + dl * (1 - mix)) * ret; R[i] = (yr * mix + dr * (1 - mix)) * ret;
    }
    this.meter = this.gr; if ((currentFrame & 4095) < N) this.port.postMessage({gr:this.gr});
  }
}
registerProcessor('urei1176', UREI1176);
/* Phil Collins Simulator: the 1979-80 Townhouse sound (Hugh Padgham, Peter Gabriel's third album, then "In the Air
   Tonight"). The drums excite a live stone room: room mics (early reflections off the walls, size-scaled) and the room's
   tail (a four-line feedback delay network, damped like a hard room). That room signal goes through the SSL 4000's
   talkback / listen-mic compressor, a brutal fast-attack, near-infinite-ratio crusher that pulls the room's decay up
   to the level of the hit. Then the SSL channel gate, keyed by the dry drum (not the room), lets the huge room swell and
   snaps it shut when the hit falls below the threshold: hold, then a very fast release, down by the gate's range. */
class PCSim extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.er = new DLine(Math.ceil(fs * .25)); this.fd = [0, 1, 2, 3].map(() => new DLine(Math.ceil(fs * .25))); this.lp = [0, 0, 0, 0];
    this.ce = 0; this.ke = 0; this.hold = 0; this.gg = 0; this.hp = [0, 0]; this.tl = [0, 0]; }
  reset(){ this.er.clear(); this.fd.forEach(d => d.clear()); this.lp.fill(0); this.ce = this.ke = this.gg = this.hold = 0; }
  tail(){ return 1 + (this.c.decay || 2); }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, size = .35 + (C.size == null ? .7 : C.size) * .9, rt = C.decay || 2, crush = C.crush == null ? .8 : C.crush;
    const ER = [7.1, 11.3, 13.9, 17.2, 19.7, 23.1, 29.3, 31.7, 37.1, 41.9], EG = [.9, .8, .75, .7, .62, .55, .5, .44, .4, .35];
    const FL = [29.7, 37.1, 41.1, 43.7].map(ms => Math.round(ms * size * fs / 1000)), FG = FL.map(n => Math.pow(10, -3 * n / fs / rt));
    const dampA = .25 + (C.tone == null ? .6 : C.tone) * .7, hpA = 1 - Math.exp(-2 * Math.PI * 110 / fs);
    const cA = 1 - Math.exp(-1 / (fs * .0002)), cR = 1 - Math.exp(-1 / (fs * (.04 + (1 - crush) * .25))), cthr = Math.pow(10, (-18 - crush * 24) / 20), cmk = Math.pow(10, crush * 26 / 20);
    const thr = Math.pow(10, (C.thresh == null ? -30 : C.thresh) / 20), holdN = (C.hold == null ? 280 : C.hold) / 1000 * fs, rel = 1 - Math.exp(-1 / (fs * Math.max(.001, (C.snap == null ? 8 : C.snap) / 1000)));
    const kA = 1 - Math.exp(-1 / (fs * .0005)), kR = 1 - Math.exp(-1 / (fs * .02)), range = Math.pow(10, -(C.range == null ? 60 : C.range) / 20), ret = C.ret == null ? .8 : C.ret, dir = C.direct || 0;
    for (let i = 0; i < N; i++){
      const x = a ? (a[i] + b[i]) * .5 : 0;
      // room mics: early reflections, alternating sides
      this.er.push(x); let el = 0, eR = 0;
      for (let k = 0; k < 10; k++){ const v = this.er.at(Math.round(ER[k] * size * fs / 1000)) * EG[k]; if (k & 1) eR += v; else el += v; }
      // the room's tail: 4-line FDN, Hadamard feedback, damped
      const o = this.fd.map((d, k) => d.at(FL[k]));
      for (let k = 0; k < 4; k++){ this.lp[k] += dampA * (o[k] - this.lp[k]); }
      const p = this.lp, h = [p[0] + p[1] + p[2] + p[3], p[0] - p[1] + p[2] - p[3], p[0] + p[1] - p[2] - p[3], p[0] - p[1] - p[2] + p[3]];
      const inj = (el + eR) * .25 + x * .3;
      for (let k = 0; k < 4; k++) this.fd[k].push(inj + h[k] * .5 * FG[k]);
      let wl = el * .5 + o[0] + o[2] * .6, wr = eR * .5 + o[1] + o[3] * .6;
      // SSL desk low cut on the room mics
      this.hp[0] += hpA * (wl - this.hp[0]); wl -= this.hp[0]; this.hp[1] += hpA * (wr - this.hp[1]); wr -= this.hp[1];
      // talkback / listen-mic compressor: crushes the room so its decay comes up to the hit
      const lv = Math.max(Math.abs(wl), Math.abs(wr)); this.ce += (lv > this.ce ? cA : cR) * (lv - this.ce);
      const cg = this.ce > cthr ? Math.pow(cthr / this.ce, .95) : 1; wl = tnh(wl * cg * cmk); wr = tnh(wr * cg * cmk);
      // SSL channel gate, keyed by the dry drum: open fast, hold, then snap shut
      const kl = Math.abs(x); this.ke += (kl > this.ke ? kA : kR) * (kl - this.ke);
      if (this.ke > thr){ this.hold = holdN; this.gg += (1 - this.gg) * .5; } else if (this.hold > 0) this.hold--; else this.gg += (range - this.gg) * rel;
      L[i] = (wl * this.gg * .5 + x * dir) * ret; R[i] = (wr * this.gg * .5 + x * dir) * ret;
    }
  }
}
registerProcessor('gatedroom', PCSim);
/* Gate stepper: a tempo-locked 16-step volume gate (the chopped, pulsing synth and guitar parts of early-80s synth-pop
   and late-80s dance records). The step clock comes from the song's tempo and the bar start, so it lands on the beat;
   each step is off, accent or full, opens for its duty length with a short attack and release, with swing. */
class GateStep extends AudioWorkletProcessor{
  constructor(o){ super(); this.c = Object.assign({on:0, steps:[1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0], rate:16, duty:.6, depth:1, atk:.002, rel:.02, swing:0, mix:1, bpm:120, t0:0}, (o && o.processorOptions && o.processorOptions.cfg) || {}); this.g = 1;
    this.port.onmessage = e => { if (e.data.type === 'cfg') Object.assign(this.c, e.data.cfg); }; }
  process(ins, outs){
    const inp = ins[0], L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, C = this.c;
    const a = inp && inp[0] ? inp[0] : null, b = inp && inp[1] ? inp[1] : a; if (!a){ L.fill(0); if (R !== L) R.fill(0); return true; }
    if (!C.on){ L.set(a); if (R !== L) R.set(b); this.g = 1; return true; }
    const fs = sampleRate, spb = (C.rate || 16) / 4, st = C.steps, n = st.length || 16, bound = 1 + (C.swing || 0) * .66, duty = C.duty == null ? .6 : C.duty;
    const aA = 1 - Math.exp(-1 / (fs * Math.max(.0005, C.atk || .002))), aR = 1 - Math.exp(-1 / (fs * Math.max(.001, C.rel || .02))), dep = C.depth == null ? 1 : C.depth, mix = C.mix == null ? 1 : C.mix;
    for (let i = 0; i < N; i++){
      const t = currentTime + i / fs - (C.t0 || 0), pos = t * (C.bpm || 120) / 60 * spb, pair = Math.floor(pos / 2), ph = pos - pair * 2;
      let k, f; if (ph < bound){ k = pair * 2; f = ph / bound; } else { k = pair * 2 + 1; f = (ph - bound) / (2 - bound); }
      const v = st[((k % n) + n) % n] || 0, open = f < duty ? v : 0, tgt = 1 - dep * (1 - open);
      this.g += (tgt > this.g ? aA : aR) * (tgt - this.g);
      const gg = 1 - mix + mix * this.g; L[i] = a[i] * gg; R[i] = b[i] * gg;
    }
    return true;
  }
}
registerProcessor('gatestep', GateStep);
/* Resonance tamer + clipper for the master (in the spirit of soothe2 and a mastering clipper): 18 log-spaced bands track
   the spectrum; a band poking out above its neighbours by more than the threshold gets a matching dynamic cut, eased in
   and out. Then an oversampled clipper (soft-to-hard knee) with a ceiling. */
class Soother extends AudioWorkletProcessor{
  constructor(o){ super(); this.fs = sampleRate; this.c = Object.assign({on:0, depth:.5, sharp:.5, speed:.5, clip:0, drive:0, ceil:-.3, hard:.5}, (o && o.processorOptions && o.processorOptions.cfg) || {});
    const fs = this.fs; this.F = []; for (let k = 0; k < 18; k++) this.F.push(200 * Math.pow(12000 / 200, k / 17));
    this.det = this.F.map(f => this.bp(f, 2.2)); this.ds = this.F.map(() => [0, 0]); this.env = new Float32Array(18); this.gain = new Float32Array(18);
    this.eq = this.F.map(() => ({b0:1, b1:0, b2:0, a1:0, a2:0})); this.es = this.F.map(() => [[0, 0], [0, 0]]); this.n = 0; this.prev = [0, 0];
    this.port.onmessage = e => { if (e.data.type === 'cfg') Object.assign(this.c, e.data.cfg); }; }
  bp(f, Q){ const w = 2 * Math.PI * f / this.fs, al = Math.sin(w) / (2 * Q), a0 = 1 + al; return {b0:al / a0, b2:-al / a0, a1:-2 * Math.cos(w) / a0, a2:(1 - al) / a0}; }
  peak(f, Q, dB){ const A = Math.pow(10, dB / 40), w = 2 * Math.PI * f / this.fs, al = Math.sin(w) / (2 * Q), a0 = 1 + al / A; return {b0:(1 + al * A) / a0, b1:-2 * Math.cos(w) / a0, b2:(1 - al * A) / a0, a1:-2 * Math.cos(w) / a0, a2:(1 - al / A) / a0}; }
  process(ins, outs){
    const inp = ins[0], L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, C = this.c;
    const a = inp && inp[0] ? inp[0] : null, b = inp && inp[1] ? inp[1] : a;
    if (!a){ L.fill(0); if (R !== L) R.fill(0); return true; }
    if (!C.on && !C.clip){ L.set(a); if (R !== L) R.set(b); return true; }
    const rel = Math.exp(-1 / (this.fs * (.02 + (1 - C.speed) * .25))), Q = 1.5 + C.sharp * 5;
    const g = Math.pow(10, (C.drive || 0) / 20), ceil = Math.pow(10, (C.ceil == null ? -.3 : C.ceil) / 20), h = 1 + C.hard * 8;
    const clip = x => { const u = x * g / ceil; return Math.sign(u) * Math.pow(Math.tanh(Math.pow(Math.abs(u), h)), 1 / h) * ceil; };
    for (let i = 0; i < N; i++){
      let l = a[i], r = b[i];
      if (C.on){
        const m = (l + r) * .5;
        for (let k = 0; k < 18; k++){ const d = this.det[k], s = this.ds[k]; const y = d.b0 * m + s[0]; s[0] = -d.a1 * y + s[1]; s[1] = d.b2 * m - d.a2 * y; const e = Math.abs(y); this.env[k] = e > this.env[k] ? e : this.env[k] * rel; }
        if ((this.n++ & 31) === 0){
          for (let k = 0; k < 18; k++){ const nb = (this.env[Math.max(0, k - 2)] + this.env[Math.max(0, k - 1)] + this.env[Math.min(17, k + 1)] + this.env[Math.min(17, k + 2)]) / 4 + 1e-6;
            const over = 20 * Math.log10((this.env[k] + 1e-6) / nb) - (6 - C.depth * 4); const target = over > 0 ? -Math.min(12, over * (.4 + C.depth)) : 0;
            this.gain[k] += (target - this.gain[k]) * .2; this.eq[k] = this.peak(this.F[k], Q, this.gain[k]); }
        }
        for (let k = 0; k < 18; k++){ if (this.gain[k] > -.05) continue; const q = this.eq[k], sL = this.es[k][0], sR = this.es[k][1];
          let y = q.b0 * l + sL[0]; sL[0] = q.b1 * l - q.a1 * y + sL[1]; sL[1] = q.b2 * l - q.a2 * y; l = y;
          y = q.b0 * r + sR[0]; sR[0] = q.b1 * r - q.a1 * y + sR[1]; sR[1] = q.b2 * r - q.a2 * y; r = y; }
      }
      if (C.clip){
        // 2x oversampled: the midpoint is clipped too and averaged in
        const ml = (l + this.prev[0]) * .5, mr = (r + this.prev[1]) * .5; this.prev[0] = l; this.prev[1] = r;
        l = (clip(l) + clip(ml)) * .5; r = (clip(r) + clip(mr)) * .5;
      }
      L[i] = l; R[i] = r;
    }
    return true;
  }
}
registerProcessor('soother', Soother);
/* Sidechain key: an envelope follower on the key track; its output is a negative gain offset fed into the ducked
   track's gain, so the track dips with the key's level (threshold, ratio-style depth, attack, release). */
class SCKey extends AudioWorkletProcessor{
  constructor(o){ super(); this.fs = sampleRate; this.c = Object.assign({amount:.5, rel:.18, thr:.1}, (o && o.processorOptions && o.processorOptions.cfg) || {}); this.env = 0; this.port.onmessage = e => { if (e.data.type === 'cfg') Object.assign(this.c, e.data.cfg); }; }
  process(ins, outs){
    const k = ins[0] && ins[0][0], k2 = ins[0] && ins[0][1], o = outs[0][0], N = o.length, C = this.c, at = Math.exp(-1 / (this.fs * .003)), re = Math.exp(-1 / (this.fs * Math.max(.02, C.rel)));
    for (let i = 0; i < N; i++){ const x = k ? Math.max(Math.abs(k[i]), k2 ? Math.abs(k2[i]) : 0) : 0; this.env = x > this.env ? x + (this.env - x) * at : x + (this.env - x) * re;
      const over = Math.max(0, this.env - C.thr * .3) / Math.max(.02, C.thr); o[i] = -Math.min(.95, C.amount * .95 * Math.min(1, over)); }
    if (outs[0][1]) outs[0][1].set(o);
    return true;
  }
}
registerProcessor('sckey', SCKey);

/* Eventide H910 Harmonizer: 11-bit delay memory between a 2:1 compressor and expander (it pumps), read at a different
   rate than it is written; the read point sweeps a ~30 ms window and jumps back at the edge with no de-glitch (the
   splice click). Two channels here, one shifted up and one down, for the classic doubled "wings". Up to 112.5 ms
   delay each side; feedback goes past unity. SMOOTH splicing models the later de-glitched H949. */
class H910 extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = [new DLine(Math.ceil(fs * .2)), new DLine(Math.ceil(fs * .2))]; this.p = [0, 0]; this.fb = [0, 0]; this.env = 1e-3; this.envO = [1e-3, 1e-3]; this.lp = [new LPN(fs, 9000, 4), new LPN(fs, 9000, 4)]; this.update(); }
  reset(){ this.dl.forEach(d => d.clear()); this.fb = [0, 0]; this.p = [0, 0]; }
  tail(){ const C = this.c; return tailOf(Math.max(C.dL || 0, C.dR || 0) + 30, C.fb || 0); }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, W = .03 * fs, cents = C.cents == null ? 9 : C.cents, smooth = C.splice === 'smooth';
    const ratio = [Math.pow(2, cents / 1200), Math.pow(2, -cents / 1200)], del = [(C.dL || 0) / 1000 * fs, (C.dR || 0) / 1000 * fs];
    const sp = C.spread == null ? 1 : C.spread, ret = C.ret == null ? .8 : C.ret, q = 1024;
    const atk = 1 - Math.exp(-1 / (.004 * fs)), rel = 1 - Math.exp(-1 / (.08 * fs));
    const out = [0, 0];
    for (let i = 0; i < N; i++){
      const x0 = a ? (a[i] + b[i]) * .5 : 0;
      for (let c = 0; c < 2; c++){
        const x = x0 + this.fb[c] * (C.fb || 0);
        // compressor (2:1) ahead of the converter
        const ax = Math.abs(x); this.env += (ax > this.env ? atk : rel) * (ax - this.env);
        const g = 1 / Math.sqrt(Math.max(this.env, 1e-4)) * .1;
        this.dl[c].push(Math.round(sat(x * g) * q) / q);
        // read point moves at (1 - ratio) per sample inside the window
        this.p[c] += 1 - ratio[c]; if (this.p[c] < 0) this.p[c] += W; else if (this.p[c] >= W) this.p[c] -= W;
        let y;
        if (smooth){ const p2 = (this.p[c] + W / 2) % W, w1 = 1 - Math.abs(2 * this.p[c] / W - 1), w2 = 1 - w1;
          y = this.dl[c].lerp(del[c] + this.p[c] + 2) * w1 + this.dl[c].lerp(del[c] + p2 + 2) * w2; }
        else y = this.dl[c].lerp(del[c] + this.p[c] + 2);
        y = this.lp[c].run(y);
        // expander after the D/A (its own detector, slower: the pumping)
        const ay = Math.abs(y); this.envO[c] += (ay > this.envO[c] ? atk * .5 : rel * .5) * (ay - this.envO[c]);
        y *= Math.min(600, this.envO[c] * 141);
        this.fb[c] = y; out[c] = y;
      }
      const l = out[0] * (.5 + .5 * sp) + out[1] * (.5 - .5 * sp), r = out[1] * (.5 + .5 * sp) + out[0] * (.5 - .5 * sp);
      const wg = (C.wings == null ? .7 : C.wings) * ret; L[i] = l * wg; R[i] = r * wg;
    }
  }
}
registerProcessor('h910', H910);

/* MXR Model 113: 10-bit subranging converter inside an NE571 2:1 compander with pre/de-emphasis and a limiter;
   6-pole anti-alias and 4-pole reconstruction filters switched with the bandwidth range (20/10/5/2.5 kHz), which
   also sets the maximum delay (160/320/640/1280 ms with four memory boards). LFO sweeps the clock 4:1. */
class MXR113 extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = new DLine(Math.ceil(fs * 1.35)); this.aa = new LPN(fs, 20000, 6, [.52, .71, 1.93]); this.rc = new LPN(fs, 20000, 4, [.54, 1.31]); this.h = new Hold();
    this.env = 1e-3; this.envO = 1e-3; this.pe = 0; this.de = 0; this.fb = 0; this.lfo = 0; this.update(); }
  reset(){ this.dl.clear(); this.fb = this.de = this.pe = 0; }
  tail(){ const C = this.c; return tailOf(C.delay || 0, C.regen || 0); }
  update(){ const bw = this.c.bw || 10000; this.aa.set(Math.min(bw, this.fs * .45)); this.rc.set(Math.min(bw, this.fs * .45)); }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, bw = C.bw || 10000, ret = C.ret == null ? .8 : C.ret, q = 512;
    const atk = 1 - Math.exp(-1 / (.005 * fs)), rel = 1 - Math.exp(-1 / (.05 * fs)), clk = Math.min(1, bw * 2.5 / fs);
    for (let i = 0; i < N; i++){
      this.lfo += (C.lfoRate || .3) / fs; if (this.lfo >= 1) this.lfo -= 1;
      const k = Math.pow(4, -(C.lfoDepth || 0) * (.5 + .5 * Math.sin(2 * Math.PI * this.lfo)));
      let x = (a ? (a[i] + b[i]) * .5 : 0) + this.fb;
      const p = x + 1.5 * (x - this.pe); this.pe = x; x = p;                      // pre-emphasis
      const ax = Math.abs(x); this.env += (ax > this.env ? atk : rel) * (ax - this.env);
      x = x / Math.sqrt(Math.max(this.env, 1e-4)) * .12;                          // NE571 compressor
      x = Math.max(-.9, Math.min(.9, x));                                         // brick-wall limiter
      x = this.aa.run(x); x = this.h.run(x, clk / k); x = Math.round(x * q) / q;   // 10-bit converter at the clock
      this.dl.push(x);
      let y = this.rc.run(this.dl.lerp(Math.max(1, (C.delay || 100) / 1000 * fs * k)));
      const ay = Math.abs(y); this.envO += (ay > this.envO ? atk : rel) * (ay - this.envO);
      y *= Math.min(300, this.envO * 70);                                          // expander
      this.de += (y - this.de) * .4; y = this.de;                                 // de-emphasis
      this.fb = y * (C.regen || 0);
      const v = y * ret * (C.inv ? -1 : 1); L[i] = v; R[i] = v;
    }
  }
}
registerProcessor('mxr113', MXR113);

/* The Wheel of Steel: an SL-1200 MK2-style direct-drive deck. Motor reaches 33 1/3 in about 0.7 s and brakes in about
   half that; quartz pitch +/-8 %. Touching the record hands control to your finger (the record follows it through a
   few-ms of slip), and letting go lets the motor pull it back to speed. */
/* Mastering bus compressor in the style of the SSL G-series quad bus compressor: a feed-forward VCA compressor, stereo
   linked, with stepped attack (0.1-30 ms), release (0.1-1.2 s or Auto, program dependent), ratio 2:1 / 4:1 / 10:1,
   make-up gain, a sidechain high-pass so the kick doesn't pump the whole mix, and a mix knob for parallel compression. */
const SSL_ATK = [.1, .3, 1, 3, 10, 30], SSL_REL = [.1, .3, .6, 1.2, 0];
class SSLBus extends AudioWorkletProcessor{
  constructor(o){ super(); this.fs = sampleRate; this.c = Object.assign({on:0, thr:-12, ratio:4, atk:3, rel:4, makeup:0, hp:0, mix:1}, (o && o.processorOptions && o.processorOptions.cfg) || {});
    this.gr = 0; this.slow = 0; this.hx = [0, 0]; this.hy = [0, 0]; this.n = 0; this.port.onmessage = e => { if (e.data.type === 'cfg') Object.assign(this.c, e.data.cfg); }; }
  process(ins, outs){
    const inp = ins[0], L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, C = this.c, fs = this.fs;
    const a = inp && inp.length ? inp[0] : null, b = inp && inp.length > 1 ? inp[1] : a;
    if (!a){ L.fill(0); if (R !== L) R.fill(0); return true; }
    if (!C.on){ L.set(a); if (R !== L) R.set(b); this.gr = 0; return true; }
    const aA = 1 - Math.exp(-1 / (SSL_ATK[C.atk] / 1000 * fs)), auto = !SSL_REL[C.rel];
    const hpA = C.hp ? Math.exp(-2 * Math.PI * C.hp / fs) : 0, slope = 1 - 1 / C.ratio, mk = Math.pow(10, C.makeup / 20), mix = C.mix;
    const sA = 1 - Math.exp(-1 / (2 * fs));
    for (let i = 0; i < N; i++){
      let l = a[i], r = b[i];
      // sidechain high-pass (one pole) before the detector
      if (hpA){ const yl = hpA * (this.hy[0] + l - this.hx[0]); this.hx[0] = l; this.hy[0] = yl; const yr = hpA * (this.hy[1] + r - this.hx[1]); this.hx[1] = r; this.hy[1] = yr; l = yl; r = yr; }
      const pk = Math.max(Math.abs(l), Math.abs(r)), db = 20 * Math.log10(pk + 1e-9);
      // soft knee of about 4 dB around the threshold
      const over = db - C.thr, want = over <= -2 ? 0 : over >= 2 ? over * slope : slope * (over + 2) * (over + 2) / 8;
      if (want > this.gr) this.gr += (want - this.gr) * aA;
      else {
        // Auto: fast recovery from short peaks, slow from sustained squeeze
        this.slow += (this.gr - this.slow) * sA;
        const rt = auto ? .1 + Math.min(1.1, this.slow * .25) : SSL_REL[C.rel];
        this.gr += (want - this.gr) * (1 - Math.exp(-1 / (rt * fs)));
      }
      const g = Math.pow(10, -this.gr / 20) * mk;
      L[i] = a[i] * (1 - mix + mix * g); R[i] = b[i] * (1 - mix + mix * g);
    }
    if ((this.n += N) >= 1024){ this.n = 0; this.port.postMessage({gr:this.gr}); }
    return true;
  }
}
registerProcessor('sslbus', SSLBus);
class Deck extends AudioWorkletProcessor{
  constructor(){
    super(); this.ch = null; this.sr = 44100; this.len = 0; this.pos = 0; this.rate = 0;
    this.motor = false; this.pitch = 1; this.speed = 1; this.cut = 1; this.cutG = 1; this.vol = .9; this.nudge = 1;
    this.hand = false; this.tgt = 0; this.t1 = 0; this.rep = 0;
    // loops, rolls, censor and slip: sp is the "shadow" position that keeps playing underneath
    this.la = -1; this.lb = -1; this.roll = false; this.censor = false; this.slip = false; this.sp = 0;
    this.port.onmessage = e => { const d = e.data;
      if (d.type === 'load'){ this.ch = d.ch; this.sr = d.sr; this.len = d.ch[0].length; this.pos = this.tgt = this.t1 = this.sp = 0; this.rate = 0; this.la = this.lb = -1; }
      else if (d.type === 'set') Object.assign(this, d.v);
      else if (d.type === 'hand'){ this.hand = d.on; if (d.on) this.tgt = this.t1 = this.pos; else if (this.slip) this.back(); }
      else if (d.type === 'move') this.tgt = Math.max(0, Math.min(this.len - 2, this.tgt + d.d * this.sr));
      else if (d.type === 'seek'){ this.pos = this.tgt = this.t1 = this.sp = Math.max(0, Math.min(this.len - 2, d.s * this.sr)); }
      else if (d.type === 'loop'){ if (d.off){ const wasSlip = this.slip && this.la >= 0; this.la = this.lb = -1; if (wasSlip) this.back(); } else { this.la = d.a * this.sr; this.lb = d.b * this.sr; if (this.pos > this.lb || this.pos < this.la) this.pos = this.la; } }
      else if (d.type === 'roll'){ if (d.on){ this.sp = this.pos; this.roll = true; this.la = d.a * this.sr; this.lb = d.b * this.sr; this.pos = this.la; } else { this.roll = false; this.la = this.lb = -1; this.back(); } }
      else if (d.type === 'censor'){ if (d.on){ this.sp = this.pos; this.censor = true; } else { this.censor = false; this.back(); } } };
  }
  back(){ this.pos = this.tgt = this.t1 = Math.max(0, Math.min(this.len - 2, this.sp)); }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length;
    if (!this.ch){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const k = this.sr / sampleRate, want = this.motor ? this.speed * this.pitch * this.nudge * k : 0;
    const acc = 1 - Math.exp(-1 / (.17 * sampleRate)), brk = 1 - Math.exp(-1 / (.09 * sampleRate)), fol = 1 - Math.exp(-1 / ((this.smooth || .006) * sampleRate));
    const a = this.ch[0], b = this.ch[1] || a, n = this.len, looping = this.la >= 0;
    const event = this.hand || this.censor || this.roll || (looping && this.slip), keepShadow = this.censor || this.roll || this.slip;
    const G = Math.round(.06 * sampleRate);
    if (!this.ga){ this.ga = [0, G / 2]; this.gs = [0, 0]; }
    const kl = this.keylock && !this.hand && !this.censor && this.motor && Math.abs(want - k) > k * .002;
    for (let i = 0; i < N; i++){
      if (this.hand){ this.t1 += (this.tgt - this.t1) * fol; const np = this.pos + (this.t1 - this.pos) * fol; this.rate = np - this.pos; this.pos = np; }
      else { const w = this.censor ? -want : want; this.rate += (w - this.rate) * (Math.abs(w) >= Math.abs(this.rate) && Math.sign(w) === Math.sign(this.rate || w) ? acc : brk * 3); this.pos += this.rate; }
      if (looping && this.rate > 0 && this.pos >= this.lb) this.pos -= (this.lb - this.la);
      if (event && keepShadow) this.sp += want; else if (!event) this.sp = this.pos;
      if (this.pos < 0){ this.pos = 0; this.rate = 0; } else if (this.pos > n - 2){ this.pos = n - 2; this.rate = 0; }
      this.cutG += (this.cut - this.cutG) * .03;
      const g = this.cutG * this.vol;
      if (kl){
        // key lock: grains read at natural pitch (k per sample) from where the playhead is, Hann windows overlap by half
        let l = 0, r = 0;
        for (let q = 0; q < 2; q++){
          if (this.ga[q] >= G){ this.ga[q] -= G; this.gs[q] = this.pos; }
          const ph = this.ga[q] / G, w = .5 - .5 * Math.cos(2 * Math.PI * ph), rp = this.gs[q] + this.ga[q] * k * .999 + 0;
          const j0 = Math.min(n - 2, Math.max(0, rp | 0)), fr = rp - (rp | 0);
          l += (a[j0] + (a[j0 + 1] - a[j0]) * fr) * w; r += (b[j0] + (b[j0 + 1] - b[j0]) * fr) * w;
          this.ga[q]++;
        }
        L[i] = l * g; R[i] = r * g;
      } else {
        const i0 = this.pos | 0, f = this.pos - i0;
        L[i] = (a[i0] + (a[i0 + 1] - a[i0]) * f) * g; R[i] = (b[i0] + (b[i0 + 1] - b[i0]) * f) * g;
      }
    }
    if (++this.rep % 4 === 0) this.port.postMessage({pos:this.pos / this.sr, rate:this.rate / k, sp:this.sp / this.sr});
    return true;
  }
}
registerProcessor('deck', Deck);

/* ================= phasers and flangers for the cymbals (new for the Jam-On Rig) ================= */
// first-order all-pass section: y = a x + x1 - a y1, a = (t - 1) / (t + 1), t = tan(pi fc / fs)
class AP1{ constructor(){ this.x1 = 0; this.y1 = 0; } run(x, a){ const y = a * x + this.x1 - a * this.y1; this.x1 = x; this.y1 = y; return y; } }
const apCoef = (f, fs) => { const t = Math.tan(Math.PI * Math.min(f, fs * .45) / fs); return (t - 1) / (t + 1); };
/* MXR Phase 90 (1974): four JFET all-pass stages swept together by one LFO (SPEED about 0.1-10 Hz). The JFETs' resistance
   follows the LFO exponentially, so the two notches sweep roughly 200 Hz-2 kHz. Output = input + phase-shifted signal,
   equal parts. SCRIPT (the original "script logo") has no feedback; BLOCK adds the later feedback resistor for a sharper sweep. */
class Phase90 extends RackUnit{
  constructor(o){ super(o); this.ap = [new AP1(), new AP1(), new AP1(), new AP1()]; this.lfo = .25; this.fb = 0; }
  reset(){ this.ap.forEach(s => { s.x1 = s.y1 = 0; }); this.fb = 0; }
  tail(){ return .5; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, rate = .1 * Math.pow(100, C.speed == null ? .35 : C.speed), fbk = C.block ? .32 : 0, ret = C.ret == null ? .8 : C.ret, mix = C.mix == null ? 1 : C.mix;
    for (let i = 0; i < N; i++){
      const x = a ? (a[i] + b[i]) * .5 : 0;
      this.lfo += rate / fs; if (this.lfo >= 1) this.lfo -= 1;
      const tri = 1 - 2 * Math.abs(2 * this.lfo - 1), sh = tri * tri * (3 - 2 * tri);      // the JFET bias rounds the triangle
      const co = apCoef(200 * Math.pow(10, sh), fs);
      let y = sat((x + this.fb * fbk) * .9) / .9;
      for (const s of this.ap) y = s.run(y, co);
      this.fb = y;
      const o = (x * (1 - mix * .5) + y * mix * .5) * 1.25 * ret;
      L[i] = o; R[i] = o;
    }
  }
}
registerProcessor('phase90', Phase90);
/* Electro-Harmonix Small Stone (1975, Bartolini design): four OTA (CA3094) all-pass stages driven by a triangle LFO
   (RATE about 0.07-12 Hz); the COLOR switch feeds the output back into the input, giving the resonant, vowel-like sweep it
   is known for. OTA stages sweep wider than the Phase 90, roughly 100 Hz-5 kHz. */
class SmallStone extends RackUnit{
  constructor(o){ super(o); this.ap = [new AP1(), new AP1(), new AP1(), new AP1()]; this.lfo = 0; this.fb = 0; }
  reset(){ this.ap.forEach(s => { s.x1 = s.y1 = 0; }); this.fb = 0; }
  tail(){ return .6; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, rate = .07 * Math.pow(170, C.rate == null ? .3 : C.rate), fbk = C.color ? .62 : .1, ret = C.ret == null ? .8 : C.ret, mix = C.mix == null ? 1 : C.mix;
    for (let i = 0; i < N; i++){
      const x = a ? (a[i] + b[i]) * .5 : 0;
      this.lfo += rate / fs; if (this.lfo >= 1) this.lfo -= 1;
      const tri = 1 - 2 * Math.abs(2 * this.lfo - 1), co = apCoef(100 * Math.pow(50, tri), fs);
      let y = sat(x - this.fb * fbk);
      for (const s of this.ap) y = s.run(y, co);
      this.fb = y;
      const o = (x * (1 - mix * .5) + y * mix * .5) * 1.2 * ret;
      L[i] = o; R[i] = o;
    }
  }
}
registerProcessor('smallstone', SmallStone);
/* Electro-Harmonix Electric Mistress (1976): SAD1024 bucket-brigade flanger. RATE sets a slow triangle sweep (about
   0.03-3 Hz, it is known for very slow, long jets), RANGE the depth of the sweep over ~0.5-8 ms, COLOR the regeneration.
   FILTER MATRIX stops the sweep: RANGE then sets a fixed comb, the static metallic tone used on cymbals and hats.
   The BBD is clocked from the sweep, sampled and held at its clock, with its filtering and a little hiss. */
class Mistress extends RackUnit{
  constructor(o){ super(o); const fs = this.fs; this.dl = new DLine(Math.ceil(fs * .02)); this.lfo = .5; this.fb = 0; this.hi = new Hold(); this.ho = new Hold(); this.aa = new LPN(fs, 9000, 3); this.rc = new LPN(fs, 9000, 3); this.d = 2; }
  reset(){ this.dl.clear(); this.fb = 0; }
  tail(){ return .8; }
  run(a, b, L, R, N){
    const C = this.c, fs = this.fs, rate = .03 * Math.pow(100, C.rate == null ? .2 : C.rate), rng = C.range == null ? .6 : C.range, col = (C.color == null ? .55 : C.color) * .85;
    const ret = C.ret == null ? .8 : C.ret, mix = C.mix == null ? .5 : C.mix, fm = !!C.matrix;
    for (let i = 0; i < N; i++){
      const x = a ? (a[i] + b[i]) * .5 : 0;
      this.lfo += rate / fs; if (this.lfo >= 1) this.lfo -= 1; const tri = 1 - 2 * Math.abs(2 * this.lfo - 1);
      const dMs = fm ? .5 + 7.5 * rng * rng : .5 + 7.5 * Math.pow(Math.max(.02, rng), 1.5) * (tri * .9 + .1);
      this.d += (dMs - this.d) * .003;
      const clk = 512 / (2 * this.d / 1000), r = clk / fs;
      let v = this.aa.run(x + sat(this.fb * col)); v = r < 1 ? this.hi.run(v, r) : v; this.dl.push(v);
      let y = this.dl.lerp(Math.max(1, this.d / 1000 * fs)) * .98 + (Math.random() - .5) * 1.5e-4; y = r < 1 ? this.ho.run(y, r) : y; y = this.rc.run(y);
      this.fb = y;
      const o = (x * (1 - mix) + y * mix) * 1.4 * ret;
      L[i] = o; R[i] = o;
    }
  }
}
registerProcessor('mistress', Mistress);
