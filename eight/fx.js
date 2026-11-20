/* Omega 808 effects: Rev7 (a Yamaha REV7-style digital reverb) and TC2290 (a TC Electronic 2290-style delay).
   DOM-free, deterministic, no allocation in the audio path; the VST3 port (plugins/eight/) follows this line for line.
   Both: constructor(sr), set(k, v) with the rev.* / dly.* keys of CONTRACT.md, process(inL, inR, outL, outR, n, off)
   which writes the wet signal only into outL/outR[off .. off+n). */
(function (g) {
  'use strict';

  function clamp(x, a, b) { return x < a ? a : x > b ? b : x; }

  // TPT one-pole (lowpass state; hp = x - lp)
  class OnePole {
    constructor() { this.s = 0; this.G = 0; }
    setFc(fc, sr) { const w = Math.tan(Math.PI * clamp(fc, 1, sr * 0.45) / sr); this.G = w / (1 + w); }
    lp(x) { const v = (x - this.s) * this.G; const y = v + this.s; this.s = y + v; return y; }
    hp(x) { return x - this.lp(x); }
  }

  /* ---------------- REV7 ----------------
     Topology (after the REV7's own block diagram: input filters, predelay, early reflection generator, a diffuse
     late section with HF damping set as a ratio of the low-frequency decay time):
     in -> HPF/LPF -> predelay line -> ER multi-tap (per-program pattern)  ------------------> ER out
                                   -> 4 series allpass diffusers -> 8-line FDN (Hadamard) -> late out
     type 0 = normal, 1 = gate reverb (late field through a hold gate), 2 = reverse gate (rising tap ramp). */
  const ER_T = [7.1, 11.3, 17.9, 23.1, 29.7, 35.3, 41.9, 47.3, 53.9, 61.7, 67.3, 79.1];   // ms at size 1
  const ER_G = [0.90, 0.82, 0.76, 0.70, 0.63, 0.58, 0.52, 0.47, 0.42, 0.37, 0.33, 0.28];
  const FDN_MS = [29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1];
  const AP_MS = [3.22, 2.43, 8.59, 6.28];
  const REV_TAPS = 24;

  const PROGRAMS = [
    {name:'Hall 1',       type:0, time:2.6, predelay:30, hf:0.70, diffusion:0.8, density:0.8, lpf:9000,  hpf:40,  er:0.30, size:1.4},
    {name:'Hall 2',       type:0, time:3.5, predelay:45, hf:0.60, diffusion:0.9, density:0.9, lpf:7000,  hpf:40,  er:0.25, size:1.6},
    {name:'Room 1',       type:0, time:1.2, predelay:10, hf:0.80, diffusion:0.7, density:0.7, lpf:10000, hpf:60,  er:0.45, size:0.8},
    {name:'Room 2',       type:0, time:0.8, predelay:5,  hf:0.90, diffusion:0.6, density:0.6, lpf:12000, hpf:80,  er:0.50, size:0.6},
    {name:'Vocal 1',      type:0, time:2.0, predelay:40, hf:0.55, diffusion:0.8, density:0.8, lpf:8000,  hpf:100, er:0.30, size:1.0},
    {name:'Vocal 2',      type:0, time:1.5, predelay:25, hf:0.60, diffusion:0.7, density:0.7, lpf:9000,  hpf:120, er:0.35, size:0.9},
    {name:'Plate 1',      type:0, time:2.2, predelay:0,  hf:0.85, diffusion:1.0, density:1.0, lpf:12000, hpf:60,  er:0.10, size:1.0},
    {name:'Plate 2',      type:0, time:3.0, predelay:15, hf:0.75, diffusion:1.0, density:1.0, lpf:10000, hpf:60,  er:0.10, size:1.2},
    {name:'Early Ref 1',  type:0, time:0.4, predelay:0,  hf:0.90, diffusion:0.5, density:0.6, lpf:12000, hpf:80,  er:1.00, size:0.8},
    {name:'Early Ref 2',  type:0, time:0.6, predelay:10, hf:0.90, diffusion:0.5, density:1.0, lpf:10000, hpf:80,  er:1.00, size:1.5},
    {name:'Gate Reverb',  type:1, time:0.25,predelay:0,  hf:0.90, diffusion:1.0, density:1.0, lpf:12000, hpf:80,  er:0.30, size:1.0},
    {name:'Reverse Gate', type:2, time:0.35,predelay:0,  hf:0.90, diffusion:1.0, density:1.0, lpf:11000, hpf:80,  er:1.00, size:1.0},
    {name:'Chamber',      type:0, time:1.7, predelay:20, hf:0.70, diffusion:0.8, density:0.7, lpf:9000,  hpf:80,  er:0.40, size:1.1},
    {name:'Large Hall',   type:0, time:5.0, predelay:60, hf:0.50, diffusion:0.9, density:0.9, lpf:6500,  hpf:30,  er:0.20, size:2.0},
    {name:'Small Room',   type:0, time:0.5, predelay:2,  hf:0.90, diffusion:0.5, density:0.5, lpf:14000, hpf:100, er:0.60, size:0.5},
    {name:'Gated Snare',  type:1, time:0.18,predelay:0,  hf:0.80, diffusion:1.0, density:1.0, lpf:10000, hpf:150, er:0.20, size:0.8},
    {name:'Echo Room',    type:0, time:1.0, predelay:80, hf:0.70, diffusion:0.5, density:0.4, lpf:8000,  hpf:150, er:0.60, size:0.9},
    {name:'Dark Plate',   type:0, time:2.8, predelay:10, hf:0.45, diffusion:1.0, density:1.0, lpf:5000,  hpf:60,  er:0.10, size:1.1},
    {name:'Ambience',     type:0, time:0.3, predelay:0,  hf:0.90, diffusion:0.9, density:0.9, lpf:14000, hpf:100, er:0.50, size:0.4},
    {name:'Cathedral',    type:0, time:8.0, predelay:70, hf:0.50, diffusion:0.9, density:0.9, lpf:6000,  hpf:30,  er:0.20, size:2.0}
  ];

  class Rev7 {
    constructor(sr) {
      this.sr = sr;
      this.bufLen = Math.ceil(sr * 1.4) + 4;
      this.buf = new Float32Array(this.bufLen);
      this.w = 0;
      this.apLen = new Int32Array(4);
      this.apBuf = [];
      this.apW = new Int32Array(4);
      for (let i = 0; i < 4; i++) { this.apLen[i] = Math.max(2, Math.round(AP_MS[i] * 0.001 * sr)); this.apBuf.push(new Float32Array(this.apLen[i])); }
      this.lineLen = Math.ceil(sr * 0.2) + 4;
      this.lines = [];
      for (let i = 0; i < 8; i++) this.lines.push(new Float32Array(this.lineLen));
      this.lw = 0;
      this.dLen = new Float64Array(8);       // fractional delay of each line (samples)
      this.fb = new Float64Array(8);         // DC gain of each line's damping
      this.dp = new Float64Array(8);         // damping pole
      this.ds = new Float64Array(8);         // damping state
      this.o = new Float64Array(8);
      this.tapD = new Float64Array(REV_TAPS);   // tap delays (samples, after the predelay)
      this.tapGL = new Float64Array(REV_TAPS);
      this.tapGR = new Float64Array(REV_TAPS);
      this.nTaps = 0; this.tapI = new Int32Array(REV_TAPS); this.dI = new Int32Array(8); this.preI = 0; this.outPk = 0; this.quiet = 0;
      this.lpfF = new OnePole(); this.lpfF2 = new OnePole(); this.hpfF = new OnePole();
      this.lfo = 0; this.lc = 1; this.ls = 0; this.lcw = Math.cos(2 * Math.PI * 0.7 / sr); this.lsw = Math.sin(2 * Math.PI * 0.7 / sr);
      this.gEnv = 0; this.gHold = 0; this.gGain = 0;
      // parameters
      this.type = 0; this.size = 1; this.time = 2; this.predelay = 0; this.hf = 0.8; this.diffusion = 0.8; this.density = 0.8;
      this.lpf = 10000; this.hpf = 60; this.er = 0.3;
      this.loadProgram(6);
    }
    loadProgram(i) {
      const p = PROGRAMS[clamp(Math.round(i), 0, PROGRAMS.length - 1)];
      this.type = p.type; this.size = p.size; this.time = p.time; this.predelay = p.predelay; this.hf = p.hf;
      this.diffusion = p.diffusion; this.density = p.density; this.lpf = p.lpf; this.hpf = p.hpf; this.er = p.er;
      this.update();
    }
    set(k, v) {
      switch (k) {
        case 'rev.program': this.loadProgram(v); return;
        case 'rev.time': this.time = v; break;
        case 'rev.predelay': this.predelay = v; break;
        case 'rev.hf': this.hf = v; break;
        case 'rev.diffusion': this.diffusion = v; break;
        case 'rev.density': this.density = v; break;
        case 'rev.lpf': this.lpf = v; break;
        case 'rev.hpf': this.hpf = v; break;
        case 'rev.er': this.er = v; break;
        default: return;
      }
      this.update();
    }
    update() {
      const sr = this.sr;
      this.lpfF.setFc(this.lpf, sr); this.lpfF2.setFc(this.lpf, sr); this.hpfF.setFc(this.hpf, sr);
      this.preS = clamp(this.predelay * 0.001 * sr, 0, sr * 0.3);
      // late field: decay time T at low frequencies, T*hf at high frequencies (the REV7's HF ratio)
      const T = this.type === 1 ? 1.6 : clamp(this.time, 0.1, 15);
      const Th = T * clamp(this.hf, 0.05, 1);
      for (let i = 0; i < 8; i++) {
        const d = clamp(FDN_MS[i] * this.size * 0.001 * sr, 8, this.lineLen - 8);
        this.dLen[i] = d;
        const gl = Math.pow(10, -3 * d / (T * sr));
        const gh = Math.pow(10, -3 * d / (Th * sr));
        this.fb[i] = gl;
        this.dp[i] = (gl - gh) / (gl + gh);
      }
      this.apG = 0.35 + 0.4 * clamp(this.diffusion, 0, 1);
      // early reflections
      if (this.type === 2) {
        const win = clamp(this.time, 0.08, 0.9) * sr;
        this.nTaps = REV_TAPS;
        for (let i = 0; i < REV_TAPS; i++) {
          const t = (i + 1) / REV_TAPS;
          this.tapD[i] = t * win;
          const gg = 0.15 + 0.85 * t;        // rising ramp: the sound swells then stops
          this.tapGL[i] = (i & 1) ? gg * 0.55 : gg;
          this.tapGR[i] = (i & 1) ? gg : gg * 0.55;
        }
      } else {
        this.nTaps = 4 + Math.round(clamp(this.density, 0, 1) * 8);
        for (let i = 0; i < this.nTaps; i++) {
          this.tapD[i] = ER_T[i] * this.size * 0.001 * sr;
          this.tapGL[i] = (i & 1) ? ER_G[i] * 0.6 : ER_G[i];
          this.tapGR[i] = (i & 1) ? ER_G[i] : ER_G[i] * 0.6;
        }
      }
      this.erNorm = 1 / Math.sqrt(this.nTaps * 0.45);
      // whole-sample tap and line delays: the taps don't move, so they don't need interpolating (only lines 0 and 5 are modulated)
      for (let i = 0; i < this.nTaps; i++) this.tapI[i] = Math.round(this.preS + this.tapD[i]);
      for (let k = 0; k < 8; k++) this.dI[k] = Math.round(this.dLen[k]);
      this.preI = Math.round(this.preS);
      this.holdS = clamp(this.time, 0.05, 1.0) * sr;
      this.gRel = Math.exp(-1 / (0.004 * sr));
      this.gAtt = Math.exp(-1 / (0.0005 * sr));
    }
    read(d) {
      // linear interpolation in the input line, d samples behind the write head
      let r = this.w - d;
      while (r < 0) r += this.bufLen;
      if (r >= this.bufLen) r -= this.bufLen;   // -1e-13 + len rounds to len
      const i = Math.floor(r), f = r - i;
      const j = i + 1 >= this.bufLen ? 0 : i + 1;
      return this.buf[i] + (this.buf[j] - this.buf[i]) * f;
    }
    readLine(k, d) {
      let r = this.lw - d;
      while (r < 0) r += this.lineLen;
      if (r >= this.lineLen) r -= this.lineLen;
      const i = Math.floor(r), f = r - i;
      const j = i + 1 >= this.lineLen ? 0 : i + 1;
      const L = this.lines[k];
      return L[i] + (L[j] - L[i]) * f;
    }
    process(inL, inR, outL, outR, n, off) {
      off = off || 0;
      const sr = this.sr, lfoInc = 2 * Math.PI * 0.7 / sr;
      const erW = this.type === 2 ? 1 : clamp(this.er, 0, 1);
      const lateW = this.type === 2 ? 0 : (1 - 0.8 * erW);
      // nothing coming in and the tail has died away: output silence without running the network
      let inPk = 0;
      for (let s = off; s < off + n; s++) { const a = Math.abs(inL[s]) + Math.abs(inR[s]); if (a > inPk) inPk = a; }
      // (quiet = samples with no input and no output; skip only once it's longer than the longest path through the network)
      if (inPk < 1e-7 && this.outPk < 1e-6) this.quiet += n; else this.quiet = 0;
      if (this.quiet > this.sr) { for (let s = off; s < off + n; s++) { outL[s] = 0; outR[s] = 0; } return; }
      { const k = 1 / Math.sqrt(this.lc * this.lc + this.ls * this.ls); this.lc *= k; this.ls *= k; }   // keep the phasor on the unit circle
      let outPk = 0;
      const buf = this.buf, bl = this.bufLen, nT = this.nTaps, tapI = this.tapI, gL = this.tapGL, gR = this.tapGR, lines = this.lines, ll = this.lineLen, dI = this.dI;
      for (let s = off; s < off + n; s++) {
        let x = (inL[s] + inR[s]) * 0.5;
        x = this.hpfF.hp(this.lpfF.lp(x));
        const w = this.w;
        buf[w] = x;
        // early reflections (after the predelay)
        let eL = 0, eR = 0;
        for (let t = 0; t < nT; t++) {
          let r = w - tapI[t]; if (r < 0) r += bl;
          const v = buf[r];
          eL += v * gL[t]; eR += v * gR[t];
        }
        eL *= this.erNorm; eR *= this.erNorm;
        let lL = 0, lR = 0;
        if (lateW > 0) {
          let r0 = w - this.preI; if (r0 < 0) r0 += bl;
          let d = buf[r0];
          // input diffusion
          for (let a = 0; a < 4; a++) {
            const b = this.apBuf[a], p = this.apW[a];
            const z = b[p];
            const v = d + this.apG * z;
            b[p] = v;
            d = z - this.apG * v;
            this.apW[a] = p + 1 >= this.apLen[a] ? 0 : p + 1;
          }
          // FDN: read, damp, mix (Hadamard 8), write back with the input
          { const c = this.lc * this.lcw - this.ls * this.lsw; this.ls = this.ls * this.lcw + this.lc * this.lsw; this.lc = c; }
          const m = this.ls * 0.0012 * sr * 0.5;
          for (let k = 0; k < 8; k++) {
            let y;
            if (k === 0 || k === 5) y = this.readLine(k, this.dLen[k] + (k === 0 ? m : -m));
            else { let r = this.lw - dI[k]; if (r < 0) r += ll; y = lines[k][r]; }
            // one-pole damping: DC gain fb, Nyquist gain fb*(1-p)/(1+p)
            const p = this.dp[k];
            this.ds[k] = this.fb[k] * (1 - p) * y + p * this.ds[k];
            this.o[k] = this.ds[k];
          }
          const o = this.o;
          let a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3];
          let a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
          let b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
          let b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
          const h = 0.35355339059327373;
          const c0 = (b0 + b4) * h, c1 = (b1 + b5) * h, c2 = (b2 + b6) * h, c3 = (b3 + b7) * h;
          const c4 = (b0 - b4) * h, c5 = (b1 - b5) * h, c6 = (b2 - b6) * h, c7 = (b3 - b7) * h;
          const inj = d * 0.5;
          const lw = this.lw;
          this.lines[0][lw] = c0 + inj; this.lines[1][lw] = c1 - inj; this.lines[2][lw] = c2 + inj; this.lines[3][lw] = c3 - inj;
          this.lines[4][lw] = c4 + inj; this.lines[5][lw] = c5 - inj; this.lines[6][lw] = c6 + inj; this.lines[7][lw] = c7 - inj;
          this.lw = lw + 1 >= this.lineLen ? 0 : lw + 1;
          lL = (o[0] - o[2] + o[4] - o[6]) * 0.6;
          lR = (o[1] - o[3] + o[5] - o[7]) * 0.6;
          if (this.type === 1) {
            // gate: opens on the input, holds for rev.time, then shuts fast
            const ax = Math.abs(x);
            this.gEnv = ax > this.gEnv ? ax : this.gEnv * 0.9995;
            if (this.gEnv > 0.01) this.gHold = this.holdS;
            else if (this.gHold > 0) this.gHold -= 1;
            const tgt = this.gHold > 0 ? 1 : 0;
            const c = tgt > this.gGain ? this.gAtt : this.gRel;
            this.gGain = tgt + (this.gGain - tgt) * c;
            lL *= this.gGain * 1.6; lR *= this.gGain * 1.6;
          }
        }
        this.w = this.w + 1 >= this.bufLen ? 0 : this.w + 1;
        const yl = erW * eL + lateW * lL, yr = erW * eR + lateW * lR;
        outL[s] = yl; outR[s] = yr;
        const a = Math.abs(yl) + Math.abs(yr); if (a > outPk) outPk = a;
      }
      this.outPk = outPk;
    }
  }
  Rev7.PROGRAMS = PROGRAMS;

  /* ---------------- TC 2290 ----------------
     Stereo delay with modulation, a filtered feedback loop (hi cut, low cut), ping-pong, tempo-synced divisions and
     the 2290's "dynamic delay": the repeats duck under the input and swell up when it stops. */
  const DIVISIONS = [
    {name:'1/32', beats:0.125}, {name:'1/16T', beats:1 / 6}, {name:'1/16', beats:0.25}, {name:'1/16D', beats:0.375},
    {name:'1/8T', beats:1 / 3}, {name:'1/8', beats:0.5}, {name:'1/8D', beats:0.75}, {name:'1/4T', beats:2 / 3},
    {name:'1/4', beats:1}, {name:'1/4D', beats:1.5}, {name:'1/2', beats:2}, {name:'1/2D', beats:3}, {name:'1/1', beats:4}
  ];
  const DLY_MAX_S = 4;

  class TC2290 {
    constructor(sr) {
      this.sr = sr;
      this.len = Math.ceil(sr * DLY_MAX_S) + 8;
      this.bL = new Float32Array(this.len); this.bR = new Float32Array(this.len);
      this.w = 0;
      this.time = 375; this.sync = 1; this.division = 6; this.feedback = 0.35; this.hicut = 6000; this.lowcut = 120;
      this.modDepth = 0.15; this.modRate = 0.6; this.pingpong = 1; this.duck = 0.3; this.bpm = 120;
      this.hcL = new OnePole(); this.hcR = new OnePole(); this.lcL = new OnePole(); this.lcR = new OnePole();
      this.cur = 0; this.lfo = 0; this.lc = 1; this.ls = 0; this.env = 0; this.quiet = 0; this.outPk = 0;
      this.update();
      this.cur = this.target;
    }
    setTempo(bpm) { this.bpm = clamp(bpm, 20, 400); this.update(); }
    set(k, v) {
      switch (k) {
        case 'dly.time': this.time = v; break;
        case 'dly.sync': this.sync = v >= 0.5 ? 1 : 0; break;
        case 'dly.division': this.division = clamp(Math.round(v), 0, DIVISIONS.length - 1); break;
        case 'dly.feedback': this.feedback = v; break;
        case 'dly.hicut': this.hicut = v; break;
        case 'dly.lowcut': this.lowcut = v; break;
        case 'dly.modDepth': this.modDepth = v; break;
        case 'dly.modRate': this.modRate = v; break;
        case 'dly.pingpong': this.pingpong = v >= 0.5 ? 1 : 0; break;
        case 'dly.duck': this.duck = v; break;
        default: return;
      }
      this.update();
    }
    update() {
      const sr = this.sr;
      const ms = this.sync ? DIVISIONS[this.division].beats * 60000 / this.bpm : this.time;
      this.target = clamp(ms * 0.001 * sr, 1, this.len - sr * 0.02 - 8);
      this.hcL.setFc(this.hicut, sr); this.hcR.setFc(this.hicut, sr);
      this.lcL.setFc(this.lowcut, sr); this.lcR.setFc(this.lowcut, sr);
      this.fbG = clamp(this.feedback, 0, 1) * 0.98;
      this.mDepth = clamp(this.modDepth, 0, 1) * 0.006 * sr;
      this.lfoInc = 2 * Math.PI * clamp(this.modRate, 0.01, 20) / sr; this.lcw = Math.cos(this.lfoInc); this.lsw = Math.sin(this.lfoInc);
      this.smooth = 1 - Math.exp(-1 / (0.06 * sr));
      this.envAtt = 1 - Math.exp(-1 / (0.002 * sr));
      this.envRel = 1 - Math.exp(-1 / (0.25 * sr));
    }
    readB(b, d) {
      let r = this.w - d;
      while (r < 0) r += this.len;
      if (r >= this.len) r -= this.len;
      const i = Math.floor(r), f = r - i;
      const i0 = i - 1 < 0 ? i - 1 + this.len : i - 1;
      const i2 = i + 1 >= this.len ? i + 1 - this.len : i + 1;
      const i3 = i + 2 >= this.len ? i + 2 - this.len : i + 2;
      const y0 = b[i0], y1 = b[i], y2 = b[i2], y3 = b[i3];
      // 4-point Hermite
      const c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
      return ((c3 * f + c2) * f + c1) * f + y1;
    }
    process(inL, inR, outL, outR, n, off) {
      off = off || 0;
      // skip the work once nothing has come in or out for longer than the delay line (an echo can't still be on its way)
      let inPk = 0;
      for (let s = off; s < off + n; s++) { const a = Math.abs(inL[s]) + Math.abs(inR[s]); if (a > inPk) inPk = a; }
      if (inPk < 1e-7 && this.outPk < 1e-6) this.quiet += n; else this.quiet = 0;
      if (this.quiet > this.len) { for (let s = off; s < off + n; s++) { outL[s] = 0; outR[s] = 0; } return; }
      { const k = 1 / Math.sqrt(this.lc * this.lc + this.ls * this.ls); this.lc *= k; this.ls *= k; }
      let outPk = 0;
      for (let s = off; s < off + n; s++) {
        this.cur += (this.target - this.cur) * this.smooth;
        { const c = this.lc * this.lcw - this.ls * this.lsw; this.ls = this.ls * this.lcw + this.lc * this.lsw; this.lc = c; }
        const mL = this.ls * this.mDepth, mR = this.lc * this.mDepth;
        const dL = this.readB(this.bL, this.cur + this.mDepth + mL);
        const dR = this.readB(this.bR, this.cur + this.mDepth + mR);
        const xl = inL[s], xr = inR[s];
        const ax = Math.abs(xl) + Math.abs(xr);
        this.env += (ax - this.env) * (ax > this.env ? this.envAtt : this.envRel);
        let fL = this.lcL.hp(this.hcL.lp(dL)) * this.fbG;
        let fR = this.lcR.hp(this.hcR.lp(dR)) * this.fbG;
        fL = Math.tanh(fL); fR = Math.tanh(fR);
        if (this.pingpong) {
          this.bL[this.w] = (xl + xr) * 0.5 + fR;
          this.bR[this.w] = fL;
        } else {
          this.bL[this.w] = xl + fL;
          this.bR[this.w] = xr + fR;
        }
        this.w = this.w + 1 >= this.len ? 0 : this.w + 1;
        let dk = this.env * 3; if (dk > 1) dk = 1;
        const gg = 1 - clamp(this.duck, 0, 1) * dk;
        outL[s] = dL * gg; outR[s] = dR * gg;
        const a = Math.abs(dL) + Math.abs(dR); if (a > outPk) outPk = a;
      }
      this.outPk = outPk;
    }
  }
  TC2290.DIVISIONS = DIVISIONS;

  g.Rev7 = Rev7; g.TC2290 = TC2290;
  if (typeof module !== 'undefined' && module.exports) module.exports = {Rev7, TC2290};
})(typeof globalThis !== 'undefined' ? globalThis : this);
