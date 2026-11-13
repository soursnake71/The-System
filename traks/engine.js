/* Omega Traks: a circuit model of the Sequential Circuits Drumtraks (1984), plus per-pad EQ / ADSR / drive, a
   REV7-style reverb, a TC 2290-style delay, a master EQ and an overdrive (the Omega 110's extended section).
   This file is the whole sound engine: the app runs it in an AudioWorklet and the plugin runs a line-for-line C++
   port (plugins/traks/Traks.h). traks/tools/compare.mjs checks they render the same.

   The machine's sound path, as modelled:
   - 13 sounds in ROM (traks/rom/traks.rom), each stored as 8-bit companded codes (mu-law segment coding: sign,
     3-bit chord, 4-bit step: about 12 bits of range from 8 bits of memory) at their own sample clock (the Drumtraks
     recordings in traks/samples, 44.1 kHz; sounds you load, 25 kHz).
   - TUNING changes the sound's sample clock: higher is shorter and brighter, lower is longer and darker, and the
     playback is drop-sample (the DAC holds each code until the next clock), so retuned sounds alias the way the
     hardware does. VOLUME scales the DAC's reference (a multiplying DAC), so level comes before the analog stages.
   - Six voice channels with their own outputs: CH1 bass; CH2 snare, rim; CH3 tom 1, tom 2; CH4 crash, ride;
     CH5 closed, open hi-hat; CH6 claps, tambourine, cowbell, cabasa. A channel plays one sound at a time: a new hit
     cuts the sound before it (so the closed hat chokes the open hat, the tambourine cuts the claps).
   - Each channel's output stage: a 2-pole reconstruction low-pass (about 9.5 kHz), then the mix.
   - ACCENT raises a hit by ACCENT LEVEL dB; MIDI velocity scales the hit (the Drumtraks is velocity-sensitive
     over MIDI). The metronome is a short click on the mix (METRONOME VOLUME).
   Omega additions (all off or neutral by default): DAC mode (stock companded, linear 8-bit, 6-bit), interpolated
   playback, poly (every sound its own voice), output filter bypass, per-sound pan, decay, reverse, start point,
   low-pass with resonance, sends, the pad EQ / ADSR / drive, the effects and the master section. */
'use strict';
(function (root) {
const NV = 13;
const VOICES = ['BS', 'SN', 'RM', 'T1', 'T2', 'CR', 'RD', 'CH', 'OH', 'CP', 'TB', 'CB', 'CA'];
const VOICE_NAMES = ['Bass', 'Snare', 'Rim', 'Tom 1', 'Tom 2', 'Crash', 'Ride', 'Closed Hat', 'Open Hat', 'Claps', 'Tamb', 'Cowbell', 'Cabasa'];
const KEY_NAMES = ['BASS', 'SNARE', 'RIM', 'TOM1', 'TOM2', 'CRASH', 'RIDE', 'CLOSED', 'OPEN', 'CLAPS', 'TAMB', 'COWBELL', 'CABASA'];
const CHAN = [0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 5];   // CH1..CH6
const VGAIN = [1.0, 0.85, 0.6, 0.62, 0.62, 0.55, 0.5, 0.55, 0.5, 0.7, 0.45, 0.5, 0.45];
const RV_PROGS = ['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF'];
const DAC_MODES = ['COMPANDED 8-BIT', 'LINEAR 8-BIT', 'LINEAR 6-BIT'];

// parameters: [id, name, min, max, default]
const PARAMS = [
  ['tempo', 'Tempo', 40, 250, 120],
  ['volume', 'Master Volume', 0, 10, 7],
  ['metro', 'Metronome Volume', 0, 10, 5],
  ['accent', 'Accent Level', 0, 12, 6],
  ['dac', 'DAC Mode', 0, 2, 0],
  ['interp', 'Interpolation', 0, 1, 0],
  ['poly', 'Poly Voices', 0, 1, 0],
  ['filt', 'Output Filter Bypass', 0, 1, 0],
  ['rvProg', 'Reverb Program', 0, 4, 1],
  ['rvTime', 'Reverb Time', 0.3, 10, 1.6],
  ['rvHigh', 'Reverb High', 0.1, 1, 0.6],
  ['rvDelay', 'Reverb Delay', 0, 200, 12],
  ['rvDiff', 'Reverb Diffusion', 0, 10, 8],
  ['rvEr', 'Reverb ER Level', 0, 1, 0.35],
  ['rvCut', 'Reverb High Cut', 1000, 12000, 9000],
  ['rvLevel', 'Reverb Return', 0, 1, 0.5],
];
const NG = PARAMS.length;                         // 16 globals; then NF per voice
const NF = 11;
const SEND = [0.03, 0.18, 0.1, 0.12, 0.12, 0.05, 0.03, 0.03, 0.05, 0.2, 0.05, 0.05, 0.03];
for (let v = 0; v < NV; v++){
  const n = VOICE_NAMES[v] + ' ', id = VOICES[v];
  PARAMS.push([id + 'lv', n + 'Volume', 0, 1, 0.8], [id + 'pan', n + 'Pan', -1, 1, 0], [id + 'tune', n + 'Tuning', -12, 12, 0],
    [id + 'dec', n + 'Decay', 0.02, 3, 3], [id + 'mute', n + 'Mute', 0, 1, 0], [id + 'snd', n + 'Reverb Send', 0, 1, SEND[v]],
    [id + 'rev', n + 'Reverse', 0, 1, 0], [id + 'cut', n + 'Cutoff', 200, 20000, 20000], [id + 'res', n + 'Resonance', 0, 1, 0],
    [id + 'start', n + 'Start', 0, 0.9, 0], [id + 'out', n + 'Output', 0, 6, 0]);
}
const vp = (v, f) => NG + v * NF + f;            // f: 0 lv 1 pan 2 tune 3 dec 4 mute 5 snd 6 rev 7 cut 8 res 9 start 10 out (0 = its channel, 1-6 = CH1-CH6)
// master parametric EQ, overdrive / clipper, TC 2290-style delay (as on the Omega 110)
PARAMS.push(
  ['mLsF', 'Master Low Freq', 20, 500, 80], ['mLsG', 'Master Low Gain', -15, 15, 0],
  ['mP1F', 'Master Mid 1 Freq', 40, 8000, 250], ['mP1G', 'Master Mid 1 Gain', -15, 15, 0], ['mP1Q', 'Master Mid 1 Q', 0.3, 8, 1],
  ['mP2F', 'Master Mid 2 Freq', 200, 16000, 2500], ['mP2G', 'Master Mid 2 Gain', -15, 15, 0], ['mP2Q', 'Master Mid 2 Q', 0.3, 8, 1],
  ['mHsF', 'Master High Freq', 1000, 18000, 8000], ['mHsG', 'Master High Gain', -15, 15, 0],
  ['odDrive', 'Overdrive', 0, 1, 0], ['odMode', 'Overdrive Mode', 0, 2, 0], ['odMix', 'Overdrive Mix', 0, 1, 1], ['odOut', 'Overdrive Output', -12, 12, 0],
  ['dlTime', 'Delay Time', 10, 2000, 375], ['dlSync', 'Delay Sync', 0, 13, 0], ['dlFb', 'Delay Feedback', 0, 0.95, 0.35],
  ['dlHiCut', 'Delay High Cut', 1000, 20000, 6000], ['dlLoCut', 'Delay Low Cut', 20, 1000, 100],
  ['dlModRate', 'Delay Mod Rate', 0.05, 5, 0.6], ['dlModDepth', 'Delay Mod Depth', 0, 1, 0.2], ['dlDuck', 'Delay Ducking', 0, 1, 0.4],
  ['dlPing', 'Delay Ping-Pong', 0, 1, 1], ['dlLevel', 'Delay Return', 0, 1, 0.4]);
const NG2 = PARAMS.length;                        // then 13 per pad: EQ (7), ADSR (4), drive, delay send
for (let v = 0; v < NV; v++){
  const n = VOICE_NAMES[v] + ' ', id = VOICES[v];
  PARAMS.push([id + 'lsF', n + 'Low Freq', 20, 1000, 100], [id + 'lsG', n + 'Low Gain', -15, 15, 0],
    [id + 'pkF', n + 'Mid Freq', 40, 16000, 1000], [id + 'pkG', n + 'Mid Gain', -15, 15, 0], [id + 'pkQ', n + 'Mid Q', 0.3, 10, 1],
    [id + 'hsF', n + 'High Freq', 1000, 18000, 6000], [id + 'hsG', n + 'High Gain', -15, 15, 0],
    [id + 'atk', n + 'Attack', 0, 1, 0], [id + 'dcy', n + 'Env Decay', 0.005, 4, 0.3], [id + 'sus', n + 'Sustain', 0, 1, 1], [id + 'rel', n + 'Release', 0.005, 4, 4],
    [id + 'drv', n + 'Drive', 0, 1, 0], [id + 'dls', n + 'Delay Send', 0, 1, 0]);
}
const vq = (v, f) => NG2 + v * 13 + f;           // f: 0 lsF 1 lsG 2 pkF 3 pkG 4 pkQ 5 hsF 6 hsG 7 atk 8 dcy 9 sus 10 rel 11 drv 12 dls
const DL_DIVS = [0, 0.125, 1 / 6, 0.25, 1 / 3, 0.375, 0.5, 2 / 3, 0.75, 1, 4 / 3, 1.5, 2, 4];
const DL_DIV_NAMES = ['OFF', '1/32', '1/16T', '1/16', '1/8T', '1/16.', '1/8', '1/4T', '1/8.', '1/4', '1/2T', '1/4.', '1/2', '1/1'];
const OD_MODES = ['OVERDRIVE', 'HARD CLIP', 'TUBE'];
const P = {}; PARAMS.forEach((q, i) => P[q[0]] = i);
function defaults(){ return PARAMS.map(q => q[4]); }

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

// the companding DAC: code -> level (mu-law segment decode, 14-bit result over 32768)
const MU = new Float64Array(256);
for (let c = 0; c < 256; c++){ const u = ~c & 0xFF; let t = ((u & 0x0F) << 3) + 0x84; t <<= (u & 0x70) >> 4; MU[c] = ((u & 0x80) ? 0x84 - t : t - 0x84) / 32768; }
function muEncode(x){
  let pcm = Math.max(-32767, Math.min(32767, Math.floor(x * 32767 + 0.5)));
  const sign = pcm < 0 ? 0x80 : 0; if (pcm < 0) pcm = -pcm; if (pcm > 32635) pcm = 32635; pcm += 0x84;
  let exp = 7; for (let m = 0x4000; (pcm & m) === 0 && exp > 0; m >>= 1) exp--;
  return (~(sign | (exp << 4) | ((pcm >> (exp + 3)) & 0x0F))) & 0xFF;
}
// the ROM image: "OTRK", version, count, then [rate u32, length u32] per sound, then the codes
function parseRom(bytes){
  const n = bytes[5], out = []; let off = 8 + n * 8;
  for (let i = 0; i < n; i++){
    const b = 8 + i * 8, rate = bytes[b] | bytes[b + 1] << 8 | bytes[b + 2] << 16 | (bytes[b + 3] << 24 >>> 0), len = (bytes[b + 4] | bytes[b + 5] << 8 | bytes[b + 6] << 16 | bytes[b + 7] << 24) >>> 0;
    out.push({rate, codes:bytes.subarray(off, off + len)}); off += len;
  }
  return out;
}

class Engine {
  constructor(sr){
    this.sr = sr; this.p = defaults(); this.songGain = 1;
    this.rom = []; for (let v = 0; v < NV; v++) this.rom.push({codes:new Uint8Array(1), rate:25000});
    this.act = new Array(NV).fill(false); this.pos = new Float64Array(NV); this.inc = new Float64Array(NV); this.amp = new Float64Array(NV);
    this.dk = new Float64Array(NV); this.dec = new Float64Array(NV); this.cutOn = new Array(NV).fill(false);
    this.oflt = []; this.vflt = []; for (let v = 0; v < NV; v++){ this.oflt.push(new SVF()); this.vflt.push(new SVF()); }
    this.eq = []; for (let v = 0; v < NV; v++) this.eq.push([new Biquad(), new Biquad(), new Biquad()]);
    this.env = new Float64Array(NV); this.stage = new Int32Array(NV); this.envOn = new Array(NV).fill(false);
    this.envA = new Float64Array(NV); this.envD = new Float64Array(NV); this.envS = new Float64Array(NV); this.envR = new Float64Array(NV);
    this.drv = new Float64Array(NV); this.dls = new Float64Array(NV);
    this.meq = [[new Biquad(), new Biquad(), new Biquad(), new Biquad()], [new Biquad(), new Biquad(), new Biquad(), new Biquad()]];
    this.dl = new TCDelay(sr); this.rv = new Rev7(sr);
    this.clk = 0; this.clkPh = 0; this.clkF = 2000; this.clkD = decayCoef(0.008, sr);
    this.grp = new Float64Array(12); this.dcL = 0; this.dcR = 0; this.meter = new Float64Array(NV);
    this.update();
  }
  set(i, v){ this.p[i] = v; this.update(); }
  setAll(arr){ for (let i = 0; i < PARAMS.length && i < arr.length; i++) this.p[i] = arr[i]; this.update(); }
  // a sound's ROM: 8-bit codes and the sample clock they were stored at
  setRom(v, codes, rate){ if (v >= 0 && v < NV && codes && codes.length > 1){ this.rom[v] = {codes, rate}; this.act[v] = false; } }
  update(){
    const sr = this.sr, p = this.p;
    this.gL = []; this.gR = []; this.send = []; this.outCh = [];
    for (let v = 0; v < NV; v++){
      const g = p[vp(v, 4)] >= 0.5 ? 0 : 1;
      const a = (p[vp(v, 1)] + 1) * Math.PI / 4;
      this.gL.push(g * Math.cos(a) * Math.SQRT2); this.gR.push(g * Math.sin(a) * Math.SQRT2);
      this.send.push(g * p[vp(v, 5)]);
      const o = Math.round(p[vp(v, 10)]); this.outCh.push(o >= 1 && o <= 6 ? o - 1 : CHAN[v]);
      this.dk[v] = p[vp(v, 3)] >= 2.999 ? 1 : decayCoef(p[vp(v, 3)] / 3, sr);
      this.oflt[v].set(9500, 0.707, sr);
      const cut = p[vp(v, 7)]; this.cutOn[v] = cut < 19999;
      this.vflt[v].set(cut, 0.707 + p[vp(v, 8)] * 9.3, sr);
    }
    this.accGain = Math.pow(10, p[P.accent] / 20);
    this.dacMode = Math.round(p[P.dac]); this.interp = p[P.interp] >= 0.5; this.poly = p[P.poly] >= 0.5; this.filtOn = p[P.filt] < 0.5;
    const vol = p[P.volume] / 10; this.master = vol * vol * 1.2;
    this.metro = p[P.metro] / 10 * 0.35;
    this.dcA = onePole(12, sr);
    this.rv.setParams(Math.round(p[P.rvProg]), p[P.rvTime], p[P.rvHigh], p[P.rvDelay], p[P.rvDiff], p[P.rvEr], p[P.rvCut]);
    this.rvLevel = p[P.rvLevel];
    for (let v = 0; v < NV; v++){
      const e = this.eq[v];
      e[0].set(0, p[vq(v, 0)], p[vq(v, 1)], 0.707, sr); e[1].set(1, p[vq(v, 2)], p[vq(v, 3)], p[vq(v, 4)], sr); e[2].set(2, p[vq(v, 5)], p[vq(v, 6)], 0.707, sr);
      const a = p[vq(v, 7)], d = p[vq(v, 8)], su = p[vq(v, 9)], r = p[vq(v, 10)];
      this.envOn[v] = !(a <= 0 && su >= 0.999 && r >= 3.999);
      this.envA[v] = a > 0 ? 1 / (a * sr) : 1; this.envD[v] = decayCoef(d / 3, sr); this.envS[v] = su; this.envR[v] = r >= 3.999 ? 1 : decayCoef(r / 3, sr);
      this.drv[v] = p[vq(v, 11)];
      this.dls[v] = p[vp(v, 4)] >= 0.5 ? 0 : p[vq(v, 12)];
    }
    const mf = [[0, P.mLsF, P.mLsG, -1], [1, P.mP1F, P.mP1G, P.mP1Q], [1, P.mP2F, P.mP2G, P.mP2Q], [2, P.mHsF, P.mHsG, -1]];
    for (let c = 0; c < 2; c++) for (let k = 0; k < 4; k++){ const [ty, f, g, q] = mf[k]; this.meq[c][k].set(ty, p[f], p[g], q < 0 ? 0.707 : p[q], sr); }
    this.od = p[P.odDrive]; this.odMode = Math.round(p[P.odMode]); this.odMix = p[P.odMix]; this.odOut = Math.pow(10, p[P.odOut] / 20);
    const div = DL_DIVS[Math.max(0, Math.min(13, Math.round(p[P.dlSync])))];
    this.dl.setParams(div > 0 ? 60 / p[P.tempo] * div * 1000 : p[P.dlTime], p[P.dlFb], p[P.dlHiCut], p[P.dlLoCut], p[P.dlModRate], p[P.dlModDepth], p[P.dlDuck], p[P.dlPing] >= 0.5);
    this.dlLevel = p[P.dlLevel];
  }
  // VOLUME knob position (0..1) -> gain at the DAC reference: 0.8 (the default) is unity
  static lvGain(lv){ const x = lv / 0.8; return x * x; }
  /* a hit. vel: MIDI velocity as 0..1 (1 from the keys); lv / tune: the note's own VOLUME and TUNING when it was
     recorded with them (lv < 0, tune < -50: the sound's current settings) */
  trigger(v, accent, vel = 1, lv = -1, tune = -99){
    if (v < 0 || v >= NV) return;
    const p = this.p, sr = this.sr, r = this.rom[v];
    if (!this.poly) for (let k = 0; k < NV; k++) if (CHAN[k] === CHAN[v]) this.act[k] = false;
    const t = tune < -50 ? p[vp(v, 2)] : tune, l = lv < 0 ? p[vp(v, 0)] : lv;
    this.inc[v] = r.rate * Math.pow(2, t / 12) / sr;
    this.pos[v] = Math.floor(p[vp(v, 9)] * r.codes.length);
    this.amp[v] = Engine.lvGain(l) * vel * (accent ? this.accGain : 1) * VGAIN[v];
    this.dec[v] = 1; this.act[v] = true;
    this.stage[v] = 0; if (this.envA[v] < 1) this.env[v] = 0; else { this.env[v] = 1; this.stage[v] = 1; }
  }
  // the end of a hit (key up, note off): the pad's ADSR goes to its release
  release(v){ if (v >= 0 && v < NV) this.stage[v] = 3; }
  // the metronome: a short click, higher on the first beat of the measure
  click(first){ this.clk = 1; this.clkF = first ? 2600 : 2000; this.clkPh = 0; }
  overdrive(x){
    const g = 1 + this.od * 20; let y;
    if (this.odMode === 1) y = Math.max(-1, Math.min(1, x * g)) * 0.8;
    else if (this.odMode === 2) y = (Math.tanh(x * g + 0.3) - Math.tanh(0.3)) * 0.85;
    else y = Math.tanh(x * g) * 0.85;
    return (x * (1 - this.odMix) + y * this.odMix) * this.odOut;
  }
  // the DAC: the code at the read position (held until the next sample clock, or interpolated)
  read(v){
    const c = this.rom[v].codes, n = c.length, ps = this.pos[v], i = Math.floor(ps);
    if (i >= n){ this.act[v] = false; return 0; }
    const rev = this.p[vp(v, 6)] >= 0.5;
    let y = MU[c[rev ? n - 1 - i : i]];
    if (this.interp && i + 1 < n){ const y2 = MU[c[rev ? n - 2 - i : i + 1]]; y += (y2 - y) * (ps - i); }
    if (this.dacMode === 1) y = Math.floor(y * 127 + 0.5) / 127;
    else if (this.dacMode === 2) y = Math.floor(y * 31 + 0.5) / 31;
    this.pos[v] = ps + this.inc[v];
    return y;
  }
  /* render n samples into L and R (overwrites). outs (optional): 12 arrays, the six channel outputs CH1..CH6 as L/R
     pairs, dry (no effects), after level, pan and MASTER VOLUME; the sounds stay in the mix as well. */
  process(L, R, off, n, outs){
    const sr = this.sr;
    for (let s = 0; s < n; s++){
      let l = 0, r = 0, snd = 0, dsnd = 0;
      const g = this.grp; g.fill(0);
      for (let v = 0; v < NV; v++){
        if (!this.act[v] && this.stage[v] === 4) continue;
        let y = 0;
        if (this.act[v]){ y = this.read(v) * this.amp[v] * this.dec[v]; this.dec[v] *= this.dk[v]; }
        else if (Math.abs(this.oflt[v].ic1) + Math.abs(this.oflt[v].ic2) + Math.abs(this.vflt[v].ic1) + Math.abs(this.vflt[v].ic2) < 1e-9){ this.stage[v] = 4; continue; }
        // the channel's output stage, then the Omega's filter
        if (this.filtOn){ this.oflt[v].tick(y); y = this.oflt[v].lp; }
        if (this.cutOn[v]){ this.vflt[v].tick(y); y = this.vflt[v].lp; }
        if (this.envOn[v]){
          let e = this.env[v];
          switch (this.stage[v]){
            case 0: e += this.envA[v]; if (e >= 1){ e = 1; this.stage[v] = 1; } break;
            case 1: e = this.envS[v] + (e - this.envS[v]) * this.envD[v]; break;
            case 3: e *= this.envR[v]; break;
          }
          this.env[v] = e; y *= e;
        }
        if (this.drv[v] > 0){ const d = this.drv[v]; y = Math.tanh(y * (1 + d * 15)) / (1 + d); }
        const eq = this.eq[v]; y = eq[2].tick(eq[1].tick(eq[0].tick(y)));
        const ay = Math.abs(y); if (ay > this.meter[v]) this.meter[v] = ay;
        l += y * this.gL[v]; r += y * this.gR[v]; snd += y * this.send[v]; dsnd += y * this.dls[v];
        const o = this.outCh[v]; g[o * 2] += y * this.gL[v]; g[o * 2 + 1] += y * this.gR[v];
      }
      const rv = this.rv.tick(snd);
      l += rv[0] * this.rvLevel; r += rv[1] * this.rvLevel;
      const dl = this.dl.tick(dsnd);
      l += dl[0] * this.dlLevel; r += dl[1] * this.dlLevel;
      for (let k = 0; k < 4; k++){ l = this.meq[0][k].tick(l); r = this.meq[1][k].tick(r); }
      if (this.od > 0){ l = this.overdrive(l); r = this.overdrive(r); }
      const m = this.master * this.songGain;
      l *= m; r *= m;
      if (this.clk > 1e-4){ this.clkPh += this.clkF / sr; if (this.clkPh >= 1) this.clkPh -= 1; const c = (this.clkPh < 0.5 ? 1 : -1) * this.clk * this.metro; l += c; r += c; this.clk *= this.clkD; }
      this.dcL += this.dcA * (l - this.dcL); this.dcR += this.dcA * (r - this.dcR);
      L[off + s] = Math.tanh(l - this.dcL); R[off + s] = Math.tanh(r - this.dcR);
      if (outs) for (let k = 0; k < 12; k++) outs[k][off + s] = Math.tanh(g[k] * m);
    }
  }
}

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


const API = {Biquad, SVF, NG, NF, NG2, vp, vq, DL_DIVS, DL_DIV_NAMES, OD_MODES, DAC_MODES, CHAN, VGAIN, Engine, Rev7, PARAMS, P, NV, VOICES, VOICE_NAMES, KEY_NAMES, RV_PROGS, MU, muEncode, parseRom, defaults};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OTRAKS = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
