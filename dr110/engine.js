/* Omega DR-110: a circuit model of the Boss DR-110 rhythm machine (1983), plus a mixer, an effects mixer and a
   REV7-style digital reverb. This file is the whole sound engine: the app runs it in an AudioWorklet and the
   plugin runs a line-for-line C++ port (plugins/dr110/DR110.h). dr110/tools/compare.mjs checks they render the same.

   The voices are fitted to recordings of a real DR-110 (dr110/samples, and kits/dr110):
   - BASS DRUM: a bridged-T resonator struck by the trigger pulse. 50 Hz; the pulse loads the network so the first
     cycles run high (about 130 Hz, falling to 50 Hz within 25 ms); ring decays with a ~19 ms time constant.
   - SNARE DRUM: a bridged-T ringing at 240 Hz (~9 ms) plus white noise, high-passed, through a VCA (~25 ms).
   - HI-HATS / CYMBAL: a bank of six square-wave oscillators (163, 230, 263, 299, 371.5 and 378 Hz: the lines in the
     recordings at 5571, 6285, 6685, 7103, 7183, 7800 Hz are their odd harmonics) through a high band-pass (~7.4 kHz)
     and, for the cymbal, a low band-pass (3.34 kHz). Closed hat ~26 ms, open hat ~0.28 s with a gate, cymbal ~0.22 s.
     The closed hat chokes the open hat.
   - HAND CLAP: noise through a ~1 kHz band-pass, four bursts 11 ms apart, then a ~30 ms tail with a longer reverb tail.
   - KNUCKLE (added, not on the original): two woody resonators (820 / 1650 Hz) struck by a short pulse and a noise click.
   - ACCENT: the accent bus raises the level of every voice on the step (ACCENT knob: 0 = none, 10 = +6 dB) and drives
     the voice amplifiers a little harder. BALANCE fades the drums (BD, SD, clap, knuckle) against the metal (hats, cymbal).
   Everything is per-sample, double precision, with a deterministic noise generator, so the C++ port matches. */
'use strict';
(function (root) {
const NV = 7;                                    // voices: BD SD OH CH CY HC KN (the order of the buttons)
const VOICES = ['BD', 'SD', 'OH', 'CH', 'CY', 'HC', 'KN'];
const VOICE_NAMES = ['Bass Drum', 'Snare Drum', 'Open Hihat', 'Closed Hihat', 'Cymbal', 'Hand Clap', 'Knuckle'];
const GROUP = [0, 0, 1, 1, 1, 0, 0];             
const OUTS = [0, 1, 3, 3, 3, 2, 2];              // separate outputs (the rear panel): BD, SD, CP (clap, knuckle), HH/CY             // 0 = drums, 1 = metal (the BALANCE knob)
const VGAIN = [2.07, 0.2, 0.81, 1.05, 1.15, 0.26, 0.053];  // voice levels relative to each other (as the DR-110 mix)
const METAL = [163.25, 230.5, 263.0, 299.25, 371.5, 378.0];
const RV_PROGS = ['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF'];

// parameters: [id, name, min, max, default]. Globals first, then six per voice (level, pan, tune, decay, mute, send).
const PARAMS = [
  ['tempo', 'Tempo', 40, 250, 120],
  ['balance', 'Balance', -5, 5, 0],
  ['accent', 'Accent', 0, 10, 5],
  ['volume', 'Volume', 0, 10, 7],
  ['src', 'Source', 0, 1, 0],                     // 0 circuit model, 1 the recordings
  ['rvProg', 'Reverb Program', 0, 4, 0],
  ['rvTime', 'Reverb Time', 0.3, 10, 2.6],
  ['rvHigh', 'Reverb High', 0.1, 1, 0.6],
  ['rvDelay', 'Reverb Delay', 0, 200, 22],
  ['rvDiff', 'Reverb Diffusion', 0, 10, 8],
  ['rvEr', 'Reverb ER Level', 0, 1, 0.35],
  ['rvCut', 'Reverb High Cut', 1000, 12000, 9000],
  ['rvLevel', 'Reverb Return', 0, 1, 0.5],
];
for (let v = 0; v < NV; v++){
  PARAMS.push([VOICES[v] + 'lv', VOICE_NAMES[v] + ' Level', 0, 1.5, 1]);
  PARAMS.push([VOICES[v] + 'pan', VOICE_NAMES[v] + ' Pan', -1, 1, 0]);
  PARAMS.push([VOICES[v] + 'tune', VOICE_NAMES[v] + ' Tune', -12, 12, 0]);
  PARAMS.push([VOICES[v] + 'dec', VOICE_NAMES[v] + ' Decay', 0.25, 4, 1]);
  PARAMS.push([VOICES[v] + 'mute', VOICE_NAMES[v] + ' Mute', 0, 1, 0]);
  PARAMS.push([VOICES[v] + 'snd', VOICE_NAMES[v] + ' Reverb Send', 0, 1, v === 1 || v === 5 ? 0.25 : 0.1]);
}
// the voice-mod panel (after the voice parameters so voice indices stay NG + v * 6 + f). Defaults = the stock machine.
// Pitch and decay knobs on that panel are the voices' own Tune / Decay above.
PARAMS.push(
  ['bdSnap', 'Kick Snap', 0, 1, 0.5], ['bdBoost', 'Kick Decay Boost', 0, 1, 0], ['bdBass', 'Kick Bass Boost', 0, 1, 0],
  ['sdSnap', 'Snare Snap', 0, 1, 0.5], ['sdBoost', 'Snare Decay Boost', 0, 1, 0], ['sdMix', 'Snare Mix', 0, 1, 0.5],
  ['hhFilter', 'HiHat Filter', 0, 1, 0.5], ['hhMix', 'HiHat Noise Mix', 0, 1, 0],
  ['cyFilter', 'Cymbal Filter', 0, 1, 0.5],
  ['hcFilter', 'Clap Filter', 0, 1, 0.5], ['hcBoost', 'Clap Decay Boost', 0, 1, 0],
  ['swTarget', 'Trig Sweep Target', 0, 1, 0], ['swDecay', 'Trig Sweep Decay', 0, 1, 0.3], ['swAmount', 'Trig Sweep Amount', 0, 1, 0],
  ['supply', 'Supply', 0, 1, 0]);              // 0: 9 V adapter (regulated 6 V rail), 1: four AA cells (5.4 V)
const P = {}; PARAMS.forEach((q, i) => P[q[0]] = i);
const NG = 13;                                   // number of global parameters (voice v, field f is NG + v * 6 + f)
const vp = (v, f) => NG + v * 6 + f;
function defaults(){ return PARAMS.map(q => q[4]); }

const TAU = 2 * Math.PI;
// bass drum fit (dr110/tools/fit.mjs against samples/bd.wav)
let BD_PULSE = 0.006, BD_SWEEP = 2.5, BD_PTAU = 0.004, BD_TAU = 0.024;
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
const decayCoef = (tau, sr) => Math.exp(-1 / (tau * sr));
// PolyBLEP square wave, +-1
class Square {
  constructor(ph){ this.ph = ph; this.inc = 0; }
  tick(){
    const t = this.ph, dt = this.inc; let y = t < 0.5 ? 1 : -1;
    y += blep(t, dt); let t2 = t + 0.5; if (t2 >= 1) t2 -= 1; y -= blep(t2, dt);
    this.ph += dt; if (this.ph >= 1) this.ph -= 1; return y;
  }
}
function blep(t, dt){
  if (t < dt){ const x = t / dt; return x + x - x * x - 1; }
  if (t > 1 - dt){ const x = (t - 1) / dt; return x * x + x + x + 1; }
  return 0;
}

class Engine {
  constructor(sr){
    this.sr = sr; this.p = defaults(); this.seed = 0x2545F491;
    this.samples = [];                           // per voice: {d: Float32Array, sr, acc: optional accented take}
    for (let v = 0; v < NV; v++) this.samples.push(null);
    // voice state
    this.on = new Array(NV).fill(0); this.acc = new Array(NV).fill(0); this.t = new Array(NV).fill(0);
    this.sp = new Array(NV).fill(-1); this.sbuf = new Array(NV).fill(null); this.srate = new Array(NV).fill(0);
    // BD
    this.bd = new SVF(); this.bdPitch = 0; this.bdPulse = 0; this.bdPulseN = 1; this.bdOut = new SVF();
    // SD
    this.sdTone = new SVF(); this.sdToneE = 0; this.sdNoiseE = 0; this.sdHp = 0; this.sdLp = 0; this.sdPulse = 0;
    // metal: one oscillator bank per metal voice (so each can be tuned), filters, envelopes
    this.osc = [[], [], []];
    for (let m = 0; m < 3; m++) for (let i = 0; i < 6; i++) this.osc[m].push(new Square(((i * 0.137 + m * 0.311) % 1)));
    this.mHi = [new SVF(), new SVF(), new SVF()]; this.mHi2 = [new SVF(), new SVF(), new SVF()]; this.mLo = new SVF();
    this.mHp = [0, 0, 0]; this.mE = [0, 0, 0]; this.cyLoE = 0; this.ohChoke = 1;
    // HC
    this.hcBp = new SVF(); this.hcHp = 0; this.hcBurst = 0; this.hcTail = 0; this.hcLong = 0;
    // KN
    this.kn1 = new SVF(); this.kn2 = new SVF(); this.knPulse = 0; this.knClickE = 0; this.knClick = new SVF();
    // output
    this.sw = 0; this.grp = new Float64Array(8); this.dcL = 0; this.dcR = 0; this.meter = new Float64Array(NV);
    this.rv = new Rev7(sr);
    this.update();
  }
  noise(){ let x = this.seed; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; this.seed = x; return x / 4294967296 * 2 - 1; }
  set(i, v){ this.p[i] = v; this.update(); }
  setAll(arr){ for (let i = 0; i < PARAMS.length && i < arr.length; i++) this.p[i] = arr[i]; this.update(); }
  setSample(v, d, sr, accD){ this.samples[v] = d ? {d, sr, acc:accD || null} : null; }
  // derived coefficients (called whenever a parameter changes)
  update(){
    const sr = this.sr, p = this.p;
    this.tn = []; this.dk = [];
    for (let v = 0; v < NV; v++){ this.tn.push(Math.pow(2, p[vp(v, 2)] / 12)); this.dk.push(p[vp(v, 3)]); }
    // per voice gain: level, BALANCE, mute; pan constant power
    const b = p[P.balance] / 5;
    this.gL = []; this.gR = []; this.send = [];
    for (let v = 0; v < NV; v++){
      const bal = GROUP[v] === 0 ? (b > 0 ? 1 - b : 1) : (b < 0 ? 1 + b : 1);
      const g = p[vp(v, 4)] >= 0.5 ? 0 : p[vp(v, 0)] * bal;
      const a = (p[vp(v, 1)] + 1) * Math.PI / 4;
      this.gL.push(g * Math.cos(a) * Math.SQRT2); this.gR.push(g * Math.sin(a) * Math.SQRT2);
      this.send.push(p[vp(v, 4)] >= 0.5 ? 0 : p[vp(v, 5)] * p[vp(v, 0)] * bal);
    }
    this.accGain = 1 + p[P.accent] / 10;
    // the voice amplifiers run off the 6 V rail (Q19 / RD6.8EB2 regulator); on batteries VB is 5.4 V: less headroom
    this.head = p[P.supply] >= 0.5 ? 1.08 : 1.2;
    const on = i => p[i] >= 0.5;
    this.bdPulseK = 1.5 - p[P.bdSnap]; this.bdClick = Math.max(0, p[P.bdSnap] - 0.5) * 2;
    this.bdDk = this.dk[0] * (on(P.bdBoost) ? 2.5 : 1); this.bdBassK = on(P.bdBass) ? 1.6 : 1;
    this.sdMixT = Math.min(1, 2 * (1 - p[P.sdMix])); this.sdMixN = Math.min(1, 2 * p[P.sdMix]);
    this.sdDk = this.dk[1] * (on(P.sdBoost) ? 2.5 : 1); this.sdQk = on(P.sdBoost) ? 1.6 : 1;
    this.hhF = Math.pow(2, (p[P.hhFilter] - 0.5) * 2); this.hhMix = p[P.hhMix];
    this.cyF = Math.pow(2, (p[P.cyFilter] - 0.5) * 2);
    this.hcQ = 1.2 * Math.pow(4, p[P.hcFilter] - 0.5); this.hcDk = this.dk[5] * (on(P.hcBoost) ? 2.5 : 1);
    this.swBd = !on(P.swTarget); this.swD = decayCoef(0.02 + p[P.swDecay] * 0.5, sr); this.swAmt = p[P.swAmount];
    const vol = p[P.volume] / 10; this.master = vol * vol * 2;
    // fixed filters
    this.sdTone.set(240 * this.tn[1], Math.PI * 240 * this.tn[1] * 0.009 * this.dk[1] * this.sdQk, sr);
    this.sdHpA = onePole(1200 * this.tn[1] * (0.25 + 1.5 * p[P.sdSnap]), sr); this.sdLpA = onePole(11000, sr);
    for (let m = 0; m < 3; m++){
      const t = this.tn[2 + m];
      for (let i = 0; i < 6; i++) this.osc[m][i].inc = METAL[i] * t / sr;
      const fc = 7800 * Math.sqrt(t) * (m === 2 ? this.cyF : this.hhF);
      this.mHi[m].set(fc, 1.2, sr); this.mHi2[m].set(fc, 1.2, sr);
    }
    this.mLo.set(3342 * Math.sqrt(this.tn[4]) * this.cyF, 3, sr);
    this.mHpA = onePole(5000, sr);
    this.chTau = 0.03 * this.dk[3]; this.chD = decayCoef(this.chTau, sr); this.ohD = decayCoef(0.36 * this.dk[2], sr); this.ohGate = 0.62 * this.dk[2];
    this.ohRel = decayCoef(0.05, sr); this.chokeD = decayCoef(0.003, sr);
    this.cyD = decayCoef(0.03 * this.dk[4], sr); this.cyLoD = decayCoef(0.35 * this.dk[4], sr);
    this.hcBp.set(1000 * this.tn[5], this.hcQ, sr); this.hcHpA = onePole(2500 * this.tn[5], sr);
    this.hcBurstD = decayCoef(0.005, sr); this.hcTailD = decayCoef(0.06 * this.hcDk, sr); this.hcLongD = decayCoef(0.12 * this.hcDk, sr);
    this.kn1.set(820 * this.tn[6], Math.PI * 820 * this.tn[6] * 0.018 * this.dk[6], sr);
    this.kn2.set(1650 * this.tn[6], Math.PI * 1650 * this.tn[6] * 0.011 * this.dk[6], sr);
    this.knClick.set(3000 * this.tn[6], 0.9, sr); this.knClickD = decayCoef(0.0015, sr);
    this.sdToneD = decayCoef(0.009, sr); this.sdNoiseD = decayCoef(0.025 * this.sdDk, sr);
    this.bdPitchD = decayCoef(BD_PTAU, sr);
    this.bdQ = Math.PI * 50 * BD_TAU * this.bdDk * (this.bdBassK > 1 ? 1.15 : 1);
    this.bdOut.set(700, 0.707, sr);
    this.dcA = onePole(18, sr);
    this.rv.setParams(Math.round(p[P.rvProg]), p[P.rvTime], p[P.rvHigh], p[P.rvDelay], p[P.rvDiff], p[P.rvEr], p[P.rvCut]);
    this.rvLevel = p[P.rvLevel];
  }
  // a hit: voice 0..6, accented or not
  trigger(v, accent){
    const sr = this.sr;
    this.on[v] = 1; this.acc[v] = accent ? 1 : 0; this.t[v] = 0;
    if (accent) this.sw = 1;                       // TRIG SWEEP: started by the accent trigger
    const s = this.p[P.src] >= 0.5 ? this.samples[v] : null;
    if (s){ this.sbuf[v] = accent && s.acc ? s.acc : s.d; this.sp[v] = 0; this.srate[v] = s.sr / sr * this.tn[v]; }
    else this.sp[v] = -1;
    switch (v){
      case 0: this.bdPitch = 1; this.bdPulse = Math.max(1, Math.round(BD_PULSE * this.bdPulseK * sr)); this.bdPulseN = this.bdPulse; break;
      case 1: this.sdToneE = 1; this.sdNoiseE = 1; this.sdPulse = Math.max(1, Math.round(0.0005 * sr)); break;
      case 2: this.mE[0] = 1; this.ohChoke = 1; break;
      case 3: this.mE[1] = 1; this.ohChoke = 0; if (!this.swBd && this.swAmt > 0) this.chD = decayCoef(this.chTau * (1 + this.swAmt * this.sw * 4), sr); break;   // the closed hat cuts the open hat
      case 4: this.mE[2] = 1; this.cyLoE = 1; break;
      case 5: this.hcBurst = 1; this.hcTail = 0; this.hcLong = 0; break;
      case 6: this.knPulse = Math.max(1, Math.round(0.0003 * sr)); this.knClickE = 1; break;
    }
  }
  // the rear panel's SWEEP trigger (ACC starts it too)
  sweep(){ this.sw = 1; }
  // the voice amplifier: level plus accent; accent drives it toward its rails
  amp(v, x){ const h = this.head; if (!this.acc[v]) return h * Math.tanh(x / h); return h * Math.tanh(x * this.accGain * 1.15 / h) / 1.15; }
  // sample playback (Source: recordings)
  playSample(v){
    const b = this.sbuf[v], pos = this.sp[v]; if (!b) return 0;
    const i = Math.floor(pos); if (i + 1 >= b.length){ this.sp[v] = -1; return 0; }
    const fr = pos - i, y = b[i] + (b[i + 1] - b[i]) * fr; this.sp[v] = pos + this.srate[v];
    return this.acc[v] ? y * (b === this.samples[v].acc ? 1 : this.accGain) : y * 0.85;
  }
  // render n samples into L and R (overwrites). outs (optional): 8 arrays, the separate outputs BD, SD, CP, HH/CY as
  // L/R pairs, dry (no reverb), after level, pan and VOLUME; the voices stay in the mix as well.
  process(L, R, off, n, outs){
    const sr = this.sr, src = this.p[P.src] >= 0.5;
    const ins = new Float64Array(NV);
    for (let s = 0; s < n; s++){
      // ---- BD: bridged-T struck by the trigger pulse; the pulse loads the network and pulls the pitch up
      let bd = 0;
      const swp = this.swBd ? 1 + this.swAmt * this.sw * 2 : 1; this.sw *= this.swD;
      { const f = 50 * this.tn[0] * swp * (1 + BD_SWEEP * this.bdPitch);
        this.bd.set(f, this.bdQ * (1 + BD_SWEEP * this.bdPitch), sr);
        const x = this.bdPulse > 0 ? Math.sin(Math.PI * this.bdPulse / this.bdPulseN) : 0; if (this.bdPulse > 0) this.bdPulse--;
        this.bdPitch *= this.bdPitchD;
        bd = this.bd.tick(x * 0.9) * 2.2;
        bd = this.bdOut.tick(bd) * this.bdBassK + x * this.bdClick * 0.35; }
      // ---- SD: bridged-T at 240 Hz plus high-passed noise
      let sd = 0;
      { const x = this.sdPulse > 0 ? 1 : 0; if (this.sdPulse > 0) this.sdPulse--;
        const tone = this.sdTone.tick(x * 1.4) * 1.6;
        const w = this.noise();
        this.sdHp += this.sdHpA * (w - this.sdHp); this.sdLp += this.sdLpA * ((w - this.sdHp) - this.sdLp);
        sd = tone * this.sdMixT + this.sdLp * this.sdNoiseE * 1.1 * this.sdMixN; this.sdNoiseE *= this.sdNoiseD; }
      // ---- metal: square banks through the band-passes
      const metal = [0, 0, 0];
      for (let m = 0; m < 3; m++){
        let sum = 0; const os = this.osc[m];
        for (let i = 0; i < 6; i++) sum += os[i].tick();
        sum *= 1 / 6;
        if (m < 2 && this.hhMix > 0) sum = sum * (1 - this.hhMix) + this.noise() * this.hhMix;
        const hi = this.mHi2[m].tick(this.mHi[m].tick(sum));
        this.mHp[m] += this.mHpA * (hi - this.mHp[m]);
        const h = hi - this.mHp[m];
        if (m === 2){ const lo = this.mLo.tick(sum); metal[m] = h * (this.mE[2] * 0.75 + this.cyLoE * 0.25) * 3.2 + lo * this.cyLoE * 0.08; }
        else metal[m] = h * this.mE[m] * 3.2;
      }
      this.mE[1] *= this.chD;
      this.mE[0] *= this.t[2] < this.ohGate * sr ? this.ohD : this.ohRel;
      if (this.ohChoke < 1){ this.mE[0] *= this.chokeD; }
      this.mE[2] *= this.cyD; this.cyLoE *= this.cyLoD;
      // ---- HC: four bursts 11 ms apart, then the tail
      let hc = 0;
      { const w = this.noise(); const bp = this.hcBp.tick(w); this.hcHp += this.hcHpA * (w - this.hcHp);
        const t = this.t[5];
        if (this.on[5]){ const b11 = Math.round(0.011 * sr);
          if (t > 0 && t <= 3 * b11 && t % b11 === 0){ this.hcBurst = 1; if (t === 3 * b11){ this.hcTail = 1; this.hcLong = 1; } } }
        const e = this.hcBurst * 1.5 + this.hcTail * 1.1 + this.hcLong * 0.1;
        hc = (bp * 1.6 + (w - this.hcHp) * 0.35) * e;
        this.hcBurst *= this.hcBurstD; this.hcTail *= this.hcTailD; this.hcLong *= this.hcLongD; }
      // ---- KN: two woody resonators and a click
      let kn = 0;
      { const x = this.knPulse > 0 ? 1 : 0; if (this.knPulse > 0) this.knPulse--;
        const w = this.noise();
        kn = this.kn1.tick(x * 2.5) * 1.3 + this.kn2.tick(x * 2.5) * 0.7 + this.knClick.tick(w) * this.knClickE * 0.6;
        this.knClickE *= this.knClickD; }
      ins[0] = bd * VGAIN[0]; ins[1] = sd * VGAIN[1]; ins[2] = metal[0] * VGAIN[2]; ins[3] = metal[1] * VGAIN[3]; ins[4] = metal[2] * VGAIN[4]; ins[5] = hc * VGAIN[5]; ins[6] = kn * VGAIN[6];
      // ---- mixer
      let l = 0, r = 0, snd = 0;
      const g = this.grp; g.fill(0);
      for (let v = 0; v < NV; v++){
        let y = src && this.sp[v] >= 0 ? this.playSample(v) : (src && this.samples[v] ? 0 : ins[v]);
        if (!this.on[v]) y = 0;
        y = this.amp(v, y);
        const ay = Math.abs(y); if (ay > this.meter[v]) this.meter[v] = ay;
        l += y * this.gL[v]; r += y * this.gR[v]; snd += y * this.send[v];
        g[OUTS[v] * 2] += y * this.gL[v]; g[OUTS[v] * 2 + 1] += y * this.gR[v];
        this.t[v]++;
      }
      const rv = this.rv.tick(snd);
      l += rv[0] * this.rvLevel; r += rv[1] * this.rvLevel;
      l *= this.master; r *= this.master;
      this.dcL += this.dcA * (l - this.dcL); this.dcR += this.dcA * (r - this.dcR);
      L[off + s] = Math.tanh(l - this.dcL); R[off + s] = Math.tanh(r - this.dcR);
      if (outs) for (let k = 0; k < 8; k++) outs[k][off + s] = Math.tanh(g[k] * this.master);
    }
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

const API = {OUTS, setBd(a, b, c, d){ BD_PULSE = a; BD_SWEEP = b; BD_PTAU = c; BD_TAU = d; }, Engine, Rev7, PARAMS, P, NG, NV, VOICES, VOICE_NAMES, RV_PROGS, METAL, vp, defaults};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DR110 = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
