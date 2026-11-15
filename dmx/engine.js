/* Omega DMX sound engine: the voices, the Omega 110 / Omega Traks sections, the DMX's mixer and the master. The app runs
   it in an AudioWorklet (dmx/worklet.js) and for WAV export; the plugin runs a line-for-line C++ port (the mixing part
   of plugins/dmx/DmxEngine.h). dmx/tools/compare.mjs checks they render the same.

   Per hit: the voice's sound (the card's output from dmx/circuit.js, or the recording when CIRCUIT is CLEAN) read from
   its START point, through the voice's chain as on the Omega 110: level, decay, low-pass with resonance, the pad's ADSR
   (its gate one step, or the key / note held), drive, a three-band EQ; then its place in the DMX's stereo mix (the
   card's mix resistors, moved by PAN), the card's REV7 and delay sends. A closed or medium hat chokes a ringing open hat.
   The seven voice groups go through their faders (sends after the faders), the REV7-style reverb and the TC 2290-style
   delay return to the mix, then the master EQ, the overdrive, the VOLUME fader and the metronome's 1 ms pulse.

   Parameters: the effects and master first, then 27 per card (the eight DMX voice cards): the VOICE MOD set (decay
   capacitor, decay on / off, decay start, filter set, DAC, reverse: these change how the card renders), the Omega
   controls (level, pan, decay, cutoff, resonance, start, mute, sends) and the pad EQ / ADSR / drive. */
'use strict';
(function (root) {
const F = typeof module !== 'undefined' && module.exports ? require('./fx.js') : root.DMXFX;
const {SVF, Biquad, TCDelay, Rev7, onePole, decayCoef} = F;
const CARDS = ['bass', 'snare', 'hat', 'stom', 'ltom', 'perc1', 'perc2', 'cym'];          // dmx/circuit.js CARDS
const CARD_IDS = ['BA', 'SN', 'HH', 'ST', 'LT', 'P1', 'P2', 'CY'];
const CARD_NAMES = ['Bass', 'Snare', 'Hi-Hat', 'Small Toms', 'Large Toms', 'Perc 1', 'Perc 2', 'Cymbal'];
const NC = 8, NGRP = 7;                                    // cards; fader groups (bass snare hat toms cym perc1 perc2)
const RV_PROGS = ['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF'];
const DL_DIVS = [0, 0.125, 1 / 6, 0.25, 1 / 3, 0.375, 0.5, 2 / 3, 0.75, 1, 4 / 3, 1.5, 2, 4];
const DL_DIV_NAMES = ['OFF', '1/32', '1/16T', '1/16', '1/8T', '1/16.', '1/8', '1/4T', '1/8.', '1/4', '1/2T', '1/4.', '1/2', '1/1'];
const OD_MODES = ['OVERDRIVE', 'HARD CLIP', 'TUBE'];
const DECAY_MODS = ['STOCK', 'ON', 'OFF'], START_MODS = ['STOCK', 'EARLY', 'LATE'], FILTER_MODS = ['STOCK', 'LOW 10K', 'HIGH 16K', 'BYPASS'], DAC_MODS = ['AM6070', 'LINEAR 8', 'LINEAR 6'];

// [id, name, min, max, default]
const PARAMS = [
  ['rvProg', 'Reverb Program', 0, 4, 1], ['rvTime', 'Reverb Time', 0.3, 10, 1.6], ['rvHigh', 'Reverb High', 0.1, 1, 0.6], ['rvDelay', 'Reverb Delay', 0, 200, 12],
  ['rvDiff', 'Reverb Diffusion', 0, 10, 8], ['rvEr', 'Reverb ER Level', 0, 1, 0.35], ['rvCut', 'Reverb High Cut', 1000, 12000, 9000], ['rvLevel', 'Reverb Return', 0, 1, 0.5],
  ['dlTime', 'Delay Time', 10, 2000, 375], ['dlSync', 'Delay Sync', 0, 13, 0], ['dlFb', 'Delay Feedback', 0, 0.95, 0.35],
  ['dlHiCut', 'Delay High Cut', 1000, 20000, 6000], ['dlLoCut', 'Delay Low Cut', 20, 1000, 100],
  ['dlModRate', 'Delay Mod Rate', 0.05, 5, 0.6], ['dlModDepth', 'Delay Mod Depth', 0, 1, 0.2], ['dlDuck', 'Delay Ducking', 0, 1, 0.4],
  ['dlPing', 'Delay Ping-Pong', 0, 1, 1], ['dlLevel', 'Delay Return', 0, 1, 0.4],
  ['mLsF', 'Master Low Freq', 20, 500, 80], ['mLsG', 'Master Low Gain', -15, 15, 0],
  ['mP1F', 'Master Mid 1 Freq', 40, 8000, 250], ['mP1G', 'Master Mid 1 Gain', -15, 15, 0], ['mP1Q', 'Master Mid 1 Q', 0.3, 8, 1],
  ['mP2F', 'Master Mid 2 Freq', 200, 16000, 2500], ['mP2G', 'Master Mid 2 Gain', -15, 15, 0], ['mP2Q', 'Master Mid 2 Q', 0.3, 8, 1],
  ['mHsF', 'Master High Freq', 1000, 18000, 8000], ['mHsG', 'Master High Gain', -15, 15, 0],
  ['odDrive', 'Overdrive', 0, 1, 0], ['odMode', 'Overdrive Mode', 0, 2, 0], ['odMix', 'Overdrive Mix', 0, 1, 1], ['odOut', 'Overdrive Output', -12, 12, 0]];
const NG = PARAMS.length, NF = 28;
const SEND = [0.03, 0.18, 0.04, 0.12, 0.12, 0.08, 0.15, 0.06];
for (let c = 0; c < NC; c++){
  const n = CARD_NAMES[c] + ' ', id = CARD_IDS[c];
  PARAMS.push(
    [id + 'cap', n + 'Decay Cap', 0.25, 4, 1], [id + 'dmod', n + 'Decay Mod', 0, 2, 0], [id + 'early', n + 'Decay Start', 0, 2, 0],
    [id + 'filt', n + 'Filter Mod', 0, 3, 0], [id + 'dac', n + 'DAC Mod', 0, 2, 0], [id + 'rev', n + 'Reverse', 0, 1, 0], [id + 'tune', n + 'Tune', -40, 40, 0],
    [id + 'lv', n + 'Level', 0, 1.5, 1], [id + 'pan', n + 'Pan', -1, 1, 0], [id + 'dec', n + 'Decay', 0.02, 3, 3],
    [id + 'cut', n + 'Cutoff', 200, 20000, 20000], [id + 'res', n + 'Resonance', 0, 1, 0], [id + 'start', n + 'Start', 0, 0.9, 0],
    [id + 'mute', n + 'Mute', 0, 1, 0], [id + 'snd', n + 'Reverb Send', 0, 1, SEND[c]], [id + 'dls', n + 'Delay Send', 0, 1, 0],
    [id + 'lsF', n + 'Low Freq', 20, 1000, 100], [id + 'lsG', n + 'Low Gain', -15, 15, 0],
    [id + 'pkF', n + 'Mid Freq', 40, 16000, 1000], [id + 'pkG', n + 'Mid Gain', -15, 15, 0], [id + 'pkQ', n + 'Mid Q', 0.3, 10, 1],
    [id + 'hsF', n + 'High Freq', 1000, 18000, 6000], [id + 'hsG', n + 'High Gain', -15, 15, 0],
    [id + 'atk', n + 'Attack', 0, 1, 0], [id + 'dcy', n + 'Env Decay', 0.005, 4, 0.3], [id + 'sus', n + 'Sustain', 0, 1, 1], [id + 'rel', n + 'Release', 0.005, 4, 4],
    [id + 'drv', n + 'Drive', 0, 1, 0]);
}
// card field f: 0 cap 1 dmod 2 early 3 filt 4 dac 5 rev 6 tune | 7 lv 8 pan 9 dec 10 cut 11 res 12 start 13 mute 14 snd 15 dls | 16 lsF 17 lsG 18 pkF 19 pkG 20 pkQ 21 hsF 22 hsG | 23 atk 24 dcy 25 sus 26 rel 27 drv
// (TUNE moves the trimmer of every voice on the card, added to the voice's own TUNE)
const cp = (c, f) => NG + c * NF + f;
const RENDER_FIELDS = 7;                                   // the first seven change the card's render (VOICE MOD and TUNE)
const P = {}; PARAMS.forEach((q, i) => P[q[0]] = i);
function defaults(){ return PARAMS.map(q => q[4]); }
function mods(p, c){ return {cap:p[cp(c, 0)], dmod:Math.round(p[cp(c, 1)]), early:Math.round(p[cp(c, 2)]), filt:Math.round(p[cp(c, 3)]), dac:Math.round(p[cp(c, 4)]), rev:p[cp(c, 5)] >= 0.5, tune:p[cp(c, 6)]}; }

const MAXV = 32;
class Voice {
  constructor(){ this.on = false; this.svf = new SVF(); this.eq = [new Biquad(), new Biquad(), new Biquad()]; }
}
class Engine {
  constructor(sr){
    this.sr = sr; this.p = defaults(); this.fader = [.8, .8, .8, .8, .8, .8, .8, 0, .85]; this.circuit = false; this.tempo = 120;
    this.snd = []; for (let i = 0; i < 20; i++) this.snd.push(null);
    this.vs = []; for (let i = 0; i < MAXV; i++) this.vs.push(new Voice());
    this.gG = new Float64Array(NGRP); this.gT = new Float64Array(NGRP); this.vG = 0; this.vT = 0; this.metT = 0;
    this.fA = onePole(30, sr); this.chokeK = decayCoef(0.004, sr);
    this.bl = new Float64Array(NGRP); this.br = new Float64Array(NGRP); this.bs = new Float64Array(NGRP); this.bd = new Float64Array(NGRP);
    this.meq = [[new Biquad(), new Biquad(), new Biquad(), new Biquad()], [new Biquad(), new Biquad(), new Biquad(), new Biquad()]];
    this.rv = new Rev7(sr); this.dl = new TCDelay(sr); this.clk = 0; this.clkA = 0; this.meter = new Float64Array(20);
    this.update(); this.setFaders(this.fader); for (let g = 0; g < NGRP; g++) this.gG[g] = this.gT[g]; this.vG = this.vT;
  }
  static potLaw(x){ return (Math.pow(81, x) - 1) / 80; }
  // faders: the clean curve, or (circuit) the DMX's audio-taper pots; both .8 at .8
  gainOf(v){ return this.circuit ? Engine.potLaw(v) * (0.8 / Engine.potLaw(0.8)) : v * v * 1.25; }
  setFaders(f){ this.fader = f.slice(); for (let g = 0; g < NGRP; g++) this.gT[g] = this.gainOf(f[g]); this.metT = this.gainOf(f[7]); this.vT = this.gainOf(f[8]); }
  setCircuit(on){ this.circuit = !!on; this.setFaders(this.fader); }
  setTempo(bpm){ this.tempo = bpm; this.update(); }
  setAll(arr){ for (let i = 0; i < PARAMS.length && i < arr.length; i++) this.p[i] = arr[i]; this.update(); }
  // a kit voice: its sound (Float32Array at rate), card, fader group, open hat, its stock place in the mix, its pitch ratio
  setSound(vi, d, rate, card, group, open, panL, panR, ratio){ this.snd[vi] = d && d.length > 1 ? {d, rate, card, group, open, panL, panR, ratio} : null; }
  update(){
    const p = this.p, sr = this.sr;
    this.rv.setParams(Math.round(p[P.rvProg]), p[P.rvTime], p[P.rvHigh], p[P.rvDelay], p[P.rvDiff], p[P.rvEr], p[P.rvCut]); this.rvLevel = p[P.rvLevel];
    const div = DL_DIVS[Math.max(0, Math.min(13, Math.round(p[P.dlSync])))];
    this.dl.setParams(div > 0 ? 60 / this.tempo * div * 1000 : p[P.dlTime], p[P.dlFb], p[P.dlHiCut], p[P.dlLoCut], p[P.dlModRate], p[P.dlModDepth], p[P.dlDuck], p[P.dlPing] >= 0.5);
    this.dlLevel = p[P.dlLevel];
    const mf = [[0, P.mLsF, P.mLsG, -1], [1, P.mP1F, P.mP1G, P.mP1Q], [1, P.mP2F, P.mP2G, P.mP2Q], [2, P.mHsF, P.mHsG, -1]];
    for (let c = 0; c < 2; c++) for (let k = 0; k < 4; k++){ const [ty, f, g, q] = mf[k]; this.meq[c][k].set(ty, p[f], p[g], q < 0 ? 0.707 : p[q], sr); }
    this.od = p[P.odDrive]; this.odMode = Math.round(p[P.odMode]); this.odMix = p[P.odMix]; this.odOut = Math.pow(10, p[P.odOut] / 20);
    this.card = [];
    for (let c = 0; c < NC; c++){
      const q = id => p[P[CARD_IDS[c] + id]], a = q('atk'), su = q('sus'), r = q('rel'), mute = q('mute') >= 0.5;
      this.card.push({lv:mute ? 0 : q('lv'), pan:q('pan'), dk:q('dec') >= 2.999 ? 1 : decayCoef(q('dec') / 3, sr), cut:q('cut'), cutOn:q('cut') < 19999, q:0.707 + q('res') * 9.3,
        start:q('start'), snd:mute ? 0 : q('snd'), dls:mute ? 0 : q('dls'),
        eq:[[0, q('lsF'), q('lsG'), 0.707], [1, q('pkF'), q('pkG'), q('pkQ')], [2, q('hsF'), q('hsG'), 0.707]],
        envOn:!(a <= 0 && su >= 0.999 && r >= 3.999), envA:a > 0 ? 1 / (a * sr) : 1, envD:decayCoef(q('dcy') / 3, sr), envS:su, envR:r >= 3.999 ? 1 : decayCoef(r / 3, sr), drv:q('drv')});
    }
    for (const v of this.vs) if (v.on) this.setupVoice(v);
  }
  setupVoice(v){
    const C = this.card[v.card];
    v.amp = v.vel * C.lv; v.dk = C.dk; v.cutOn = C.cutOn; if (C.cutOn) v.svf.set(C.cut, C.q, this.sr);
    for (let k = 0; k < 3; k++){ const e = C.eq[k]; v.eq[k].set(e[0], e[1], e[2], e[3], this.sr); }
    v.envOn = C.envOn; v.envA = C.envA; v.envD = C.envD; v.envS = C.envS; v.envR = C.envR; v.drv = C.drv;
    v.gl = v.panL * Math.min(1, 1 - C.pan); v.gr = v.panR * Math.min(1, 1 + C.pan); v.snd = C.snd; v.dls = C.dls;
  }
  /* a hit at sample `offset` of the next process() call. vel 0..1; gate: samples until the pad's ADSR releases
     (-1: held until release()) */
  trigger(vi, vel, offset, gate){
    const s = this.snd[vi]; if (!s) return;
    if (s.group === 2 && !s.open) for (const v of this.vs) if (v.on && v.open && v.choke < 0) v.choke = offset;   // a closed hat cuts the open one
    let v = null; for (const x of this.vs) if (!x.on){ v = x; break; }
    if (!v){ v = this.vs[0]; for (const x of this.vs) if (x.pos > v.pos) v = x; }
    v.on = true; v.vi = vi; v.s = s.d; v.card = s.card; v.group = s.group; v.open = s.open; v.panL = s.panL; v.panR = s.panR; v.vel = vel;
    v.inc = s.rate / this.sr * s.ratio; v.startAt = offset; v.choke = -1; v.fade = 1; v.dec = 1; v.gate = gate;
    const C = this.card[s.card]; v.pos = Math.floor(C.start * (s.d.length - 1));
    v.svf.ic1 = 0; v.svf.ic2 = 0; for (const b of v.eq){ b.z1 = 0; b.z2 = 0; }
    this.setupVoice(v);
    if (v.envA < 1){ v.env = 0; v.stage = 0; } else { v.env = 1; v.stage = 1; }
  }
  release(vi){ for (const v of this.vs) if (v.on && v.vi === vi && v.stage < 3) v.stage = 3; }
  // the metronome's 1 ms pulse at sample `offset`, loud on the bar
  click(loud, offset){ this.clk = Math.max(1, Math.round(this.sr * 0.001)); this.clkAt = offset; this.clkLoud = loud; }
  overdrive(x){
    const g = 1 + this.od * 20; let y;
    if (this.odMode === 1) y = Math.max(-1, Math.min(1, x * g)) * 0.8;
    else if (this.odMode === 2) y = (Math.tanh(x * g + 0.3) - Math.tanh(0.3)) * 0.85;
    else y = Math.tanh(x * g) * 0.85;
    return (x * (1 - this.odMix) + y * this.odMix) * this.odOut;
  }
  // n samples into L and R (overwrites)
  process(L, R, n){
    const MET = [2.5 / (5 / 8300 * 3.8 * 2400), 1.25 / (5 / 8300 * 3.8 * 2400)];
    for (let s = 0; s < n; s++){
      this.bl.fill(0); this.br.fill(0); this.bs.fill(0); this.bd.fill(0);
      for (const v of this.vs){
        if (!v.on || s < v.startAt) continue;
        const d = v.s, a = Math.floor(v.pos);
        if (a >= d.length - 1){ v.on = false; continue; }
        let y = d[a] + (d[a + 1] - d[a]) * (v.pos - a); v.pos += v.inc;
        y *= v.amp * v.dec; v.dec *= v.dk;
        if (v.choke >= 0 && s >= v.choke){ y *= v.fade; v.fade *= this.chokeK; if (v.fade < 1e-4){ v.on = false; continue; } }
        if (v.cutOn){ v.svf.tick(y); y = v.svf.lp; }
        if (v.envOn){
          if (v.gate >= 0){ if (v.gate === 0 && v.stage < 3) v.stage = 3; else v.gate--; }
          let e = v.env;
          if (v.stage === 0){ e += v.envA; if (e >= 1){ e = 1; v.stage = 1; } }
          else if (v.stage === 1) e = v.envS + (e - v.envS) * v.envD;
          else e *= v.envR;
          v.env = e; y *= e;
          if (v.stage === 3 && e < 1e-5){ v.on = false; continue; }
        }
        if (v.drv > 0) y = Math.tanh(y * (1 + v.drv * 15)) / (1 + v.drv);
        y = v.eq[2].tick(v.eq[1].tick(v.eq[0].tick(y)));
        const ay = Math.abs(y); if (ay > this.meter[v.vi]) this.meter[v.vi] = ay;
        const g = v.group; this.bl[g] += y * v.gl; this.br[g] += y * v.gr; this.bs[g] += y * v.snd; this.bd[g] += y * v.dls;
      }
      let l = 0, r = 0, sr = 0, sd = 0;
      for (let g = 0; g < NGRP; g++){ this.gG[g] += (this.gT[g] - this.gG[g]) * this.fA; const k = this.gG[g]; l += this.bl[g] * k; r += this.br[g] * k; sr += this.bs[g] * k; sd += this.bd[g] * k; }
      const o = this.rv.tick(sr); l += o[0] * this.rvLevel; r += o[1] * this.rvLevel;
      const q = this.dl.tick(sd); l += q[0] * this.dlLevel; r += q[1] * this.dlLevel;
      for (let k = 0; k < 4; k++){ l = this.meq[0][k].tick(l); r = this.meq[1][k].tick(r); }
      if (this.od > 0){ l = this.overdrive(l); r = this.overdrive(r); }
      this.vG += (this.vT - this.vG) * this.fA; l *= this.vG; r *= this.vG;
      if (this.clk > 0 && s >= this.clkAt){ const c = MET[this.clkLoud ? 0 : 1] * this.metT; l += c; r += c; this.clk--; }
      L[s] = l; R[s] = r;
    }
    for (const v of this.vs) if (v.on){ v.startAt = 0; if (v.choke > 0) v.choke = 0; }
    this.clkAt = 0;
  }
}

const API = {CARDS, CARD_IDS, CARD_NAMES, NC, NGRP, NG, NF, RENDER_FIELDS, PARAMS, P, cp, defaults, mods, Engine, RV_PROGS, DL_DIVS, DL_DIV_NAMES, OD_MODES, DECAY_MODS, START_MODS, FILTER_MODS, DAC_MODS};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DMXE = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
