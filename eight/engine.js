/* Omega 808 engine: TR-808 voices modelled on their circuits (bridged-T resonators as TPT state-variable bandpasses
   excited by the trigger pulse, the six-square metal bank, the shared noise source), the classic accent bus, the
   Egyptian Lover trigger-out click, sample slots, mixer and Rev7 / TC2290 sends. DOM-free and deterministic. */
(function (g) {
  'use strict';
  const VOICES = ['BD','SD','LT','MT','HT','LC','MC','HC','RS','CL','CP','MA','CB','CY','OH','CH','TRIG'];
  const SENDV = ['BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH','TRIG'];
  // which knob section / send each voice uses
  const OWNER = ['BD','SD','LT','MT','HT','LT','MT','HT','RS','RS','CP','CP','CB','CY','OH','CH','TRIG'];
  const TOM_F = [[75,110],[110,160],[160,230],[160,230],[230,320],[320,460]];   // LT MT HT LC MC HC
  const TOM_TAU = [0.35, 0.28, 0.22, 0.18, 0.15, 0.12];
  const METAL_F = [205.3, 304.4, 369.6, 522.7, 540, 800];
  function clamp(x, a, b) { return x < a ? a : x > b ? b : x; }

  const P = [];
  function add(k, label, group, def, min, max, step, opts) { const o = {k, label, group, min: min == null ? 0 : min, max: max == null ? 1 : max, def}; if (step) o.step = step; if (opts) o.opts = opts; P.push(o); }
  add('master','Master Volume','Faceplate',0.75); add('AC.level','Accent','Faceplate',0.7);
  add('BD.level','BD Level','Faceplate',0.75); add('BD.tone','BD Tone','Faceplate',0.45); add('BD.decay','BD Decay','Faceplate',0.55);
  add('SD.level','SD Level','Faceplate',0.7); add('SD.tone','SD Tone','Faceplate',0.5); add('SD.snappy','SD Snappy','Faceplate',0.55);
  for (const t of ['LT','MT','HT']) { add(t + '.level', t + ' Level','Faceplate',0.7); add(t + '.tuning', t + ' Tuning','Faceplate',0.5); add(t + '.sel', t + ' Tom/Conga','Faceplate',0,0,1,1,['Tom','Conga']); }
  add('RS.level','RS Level','Faceplate',0.65); add('RS.sel','Rim Shot/Claves','Faceplate',0,0,1,1,['Rim Shot','Claves']);
  add('CP.level','CP Level','Faceplate',0.7); add('CP.sel','Hand Clap/Maracas','Faceplate',0,0,1,1,['Hand Clap','Maracas']);
  add('CB.level','CB Level','Faceplate',0.6); add('CY.level','CY Level','Faceplate',0.65); add('CY.tone','CY Tone','Faceplate',0.55); add('CY.decay','CY Decay','Faceplate',0.6);
  add('OH.level','OH Level','Faceplate',0.65); add('OH.decay','OH Decay','Faceplate',0.45); add('CH.level','CH Level','Faceplate',0.7);
  add('mod.BD.tune','Tune','Bass Drum',0.5); add('mod.BD.sweep','Pitch Sweep','Bass Drum',0.5); add('mod.BD.longDecay','Long Decay Mod','Bass Drum',0);
  add('mod.BD.click','Click','Bass Drum',0.3); add('mod.BD.drive','Drive','Bass Drum',0);
  add('mod.SD.tune','Tune','Snare',0.5); add('mod.SD.decay','Decay','Snare',0.5); add('mod.SD.noiseColor','Noise Color','Snare',0.5);
  add('mod.TOM.decay','Tom Decay','Toms/Congas',0.5); add('mod.TOM.sweep','Tom Sweep','Toms/Congas',0.5); add('mod.CONGA.decay','Conga Decay','Toms/Congas',0.5);
  add('mod.RS.tune','Rim Tune','Rim/Clap',0.5); add('mod.CL.tune','Claves Tune','Rim/Clap',0.5); add('mod.CP.tail','Clap Tail','Rim/Clap',0.5);
  add('mod.CP.spread','Clap Spread','Rim/Clap',0.5); add('mod.MA.decay','Maracas Decay','Rim/Clap',0.5);
  add('mod.CB.tune','Cowbell Tune','Cowbell/Metal',0.5); add('mod.CB.decay','Cowbell Decay','Cowbell/Metal',0.5); add('mod.HH.tune','Metal Osc Tune','Cowbell/Metal',0.5);
  add('mod.CH.decay','CH Decay','Cowbell/Metal',0.5); add('mod.CY.tune','Cymbal Tune','Cowbell/Metal',0.5);
  add('mod.AC.depth','Accent Depth','Accent',0.5); add('mod.AC.punch','Accent Punch','Accent',0);
  add('mod.TRIG.src','Trigger Source','Trigger Out',0,0,3,1,['CB','CP','AC','TRIG']); add('mod.TRIG.width','Pulse Width (ms)','Trigger Out',20,1,40);
  add('mod.TRIG.level','Click Into Mix','Trigger Out',0); add('mod.TRIG.tone','Coupling Cap','Trigger Out',0.5);
  add('mod.age','Component Age','Machine',0.25); add('mod.hiss','Hiss','Machine',0.15); add('swing','Swing','Machine',0);
  for (const v of SENDV) add('rev.send.' + v, v + ' Reverb', 'Reverb Sends', v === 'SD' ? 0.12 : v === 'CP' ? 0.18 : 0);
  for (const v of SENDV) add('dly.send.' + v, v + ' Delay', 'Delay Sends', 0);
  const progs = g.Rev7 ? g.Rev7.PROGRAMS : [{name:'Plate 1', time:2.2, predelay:0, hf:0.85, diffusion:1, density:1, lpf:12000, hpf:60, er:0.1}];
  const RP = progs[Math.min(6, progs.length - 1)];
  add('rev.program','Program','REV7',Math.min(6, progs.length - 1),0,progs.length - 1,1,progs.map(p => p.name));
  add('rev.time','Rev Time (s)','REV7',RP.time,0.1,12); add('rev.predelay','Predelay (ms)','REV7',RP.predelay,0,250); add('rev.hf','HF Ratio','REV7',RP.hf,0.1,1);
  add('rev.diffusion','Diffusion','REV7',RP.diffusion); add('rev.density','Density','REV7',RP.density); add('rev.lpf','LPF (Hz)','REV7',RP.lpf,1000,16000);
  add('rev.hpf','HPF (Hz)','REV7',RP.hpf,20,1000); add('rev.er','ER/Rev Balance','REV7',RP.er); add('rev.return','Reverb Return','REV7',0.6);
  const divs = g.TC2290 ? g.TC2290.DIVISIONS.map(d => d.name) : ['1/8D'];
  add('dly.time','Delay Time (ms)','TC 2290',375,1,3000); add('dly.sync','Tempo Sync','TC 2290',1,0,1,1,['Off','On']);
  add('dly.division','Division','TC 2290',Math.max(0, divs.indexOf('1/8D')),0,divs.length - 1,1,divs);
  add('dly.feedback','Feedback','TC 2290',0.35); add('dly.hicut','Hi Cut (Hz)','TC 2290',6000,1000,20000); add('dly.lowcut','Low Cut (Hz)','TC 2290',120,20,2000);
  add('dly.modDepth','Mod Depth','TC 2290',0.15); add('dly.modRate','Mod Rate (Hz)','TC 2290',0.6,0.05,10); add('dly.pingpong','Ping Pong','TC 2290',1,0,1,1,['Off','On']);
  add('dly.duck','Dynamic (Duck)','TC 2290',0.3); add('dly.return','Delay Return','TC 2290',0.5);
  const IDX = {}; P.forEach((p, i) => { IDX[p.k] = i; });

  // TPT state-variable filter (Simper)
  class SVF {
    constructor() { this.ic1 = 0; this.ic2 = 0; this.a1 = 0; this.a2 = 0; this.a3 = 0; this.k = 1; this.g = 0; this.bp = 0; this.lp = 0; this.hp = 0; }
    set(fc, Q, sr) { const gg = Math.tan(Math.PI * clamp(fc, 5, sr * 0.45) / sr); this.g = gg; this.k = 1 / Q; this.a1 = 1 / (1 + gg * (gg + this.k)); this.a2 = gg * this.a1; this.a3 = gg * this.a2; }
    tick(x) { const v3 = x - this.ic2, v1 = this.a1 * this.ic1 + this.a2 * v3, v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
      this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2; this.bp = v1; this.lp = v2; this.hp = x - this.k * v1 - v2; return v1; }
    reset() { this.ic1 = 0; this.ic2 = 0; }
  }
  class OP { constructor() { this.s = 0; this.G = 0; } set(fc, sr) { const w = Math.tan(Math.PI * clamp(fc, 1, sr * 0.45) / sr); this.G = w / (1 + w); }
    lp(x) { const v = (x - this.s) * this.G, y = v + this.s; this.s = y + v; return y; } hp(x) { return x - this.lp(x); } }
  function sq(ph, dt) {   // polyBLEP square
    let y = ph < 0.5 ? 1 : -1;
    let t = ph; if (t < dt) { t /= dt; y += t + t - t * t - 1; } else if (t > 1 - dt) { t = (t - 1) / dt; y -= t * t + t + t + 1; }
    t = ph + 0.5; if (t >= 1) t -= 1;
    if (t < dt) { t /= dt; y -= t + t - t * t - 1; } else if (t > 1 - dt) { t = (t - 1) / dt; y += t * t + t + t + 1; }
    return y;
  }

  class Eight808 {
    constructor(sr) {
      this.sr = sr;
      this.p = new Float64Array(P.length);
      for (let i = 0; i < P.length; i++) this.p[i] = P[i].def;
      this.rng = 808;
      const NV = VOICES.length;
      this.tolF = new Float64Array(NV); this.tolD = new Float64Array(NV);
      let r = 0x13579bdf;
      for (let i = 0; i < NV; i++) { r ^= r << 13; r ^= r >>> 17; r ^= r << 5; this.tolF[i] = ((r >>> 0) / 4294967296) * 2 - 1;
        r ^= r << 13; r ^= r >>> 17; r ^= r << 5; this.tolD[i] = ((r >>> 0) / 4294967296) * 2 - 1; }
      // per voice: resonators (2), pulse, envelopes
      this.res1 = []; this.res2 = []; this.f1 = []; this.f2 = []; this.op1 = []; this.op2 = [];
      for (let i = 0; i < NV; i++) { this.res1.push(new SVF()); this.res2.push(new SVF()); this.f1.push(new SVF()); this.f2.push(new SVF()); this.op1.push(new OP()); this.op2.push(new OP()); }
      this.pulseN = new Int32Array(NV); this.pulseH = new Float64Array(NV); this.pulseH2 = new Float64Array(NV);
      this.env = new Float64Array(NV); this.env2 = new Float64Array(NV); this.envC = new Float64Array(NV); this.env2C = new Float64Array(NV);
      this.sw = new Float64Array(NV); this.swC = new Float64Array(NV); this.amp = new Float64Array(NV); this.t = new Int32Array(NV);
      this.active = new Uint8Array(NV); this.choke = 0;
      this.mph = new Float64Array(6); this.cph = new Float64Array(2);
      // samples
      this.smp = []; for (let i = 0; i < NV; i++) this.smp.push(null);
      this.spos = new Float64Array(NV); this.srate = new Float64Array(NV); this.senv = new Float64Array(NV); this.senvC = new Float64Array(NV);
      this.trigHigh = 0; this.trigAmp = 0; this.trigHP = new OP(); this.trigFired = 0;
      this.rev = g.Rev7 ? new g.Rev7(sr) : null; this.dly = g.TC2290 ? new g.TC2290(sr) : null;
      this.bufN = 4096;
      this.rIn = new Float32Array(this.bufN); this.dIn = new Float32Array(this.bufN);
      this.rL = new Float32Array(this.bufN); this.rR = new Float32Array(this.bufN); this.dL = new Float32Array(this.bufN); this.dR = new Float32Array(this.bufN);
      this.gain = new Float64Array(NV); this.rs = new Float64Array(NV); this.ds = new Float64Array(NV);
      for (let i = 0; i < P.length; i++) this.apply(P[i].k, this.p[i]);
      this.dirty = 1;
    }
    get(k) { const i = IDX[k]; return i == null ? 0 : this.p[i]; }
    v(k) { return this.p[IDX[k]]; }
    set(k, val) {
      if (k === 'tempo') { if (this.dly) this.dly.setTempo(val); return; }
      const i = IDX[k]; if (i == null) return;
      const d = P[i]; val = clamp(+val, d.min, d.max); if (d.step) val = Math.round(val);
      this.p[i] = val; this.apply(k, val); this.dirty = 1;
      if (k === 'rev.program' && g.Rev7) {
        const pr = g.Rev7.PROGRAMS[val];
        for (const kk of ['time','predelay','hf','diffusion','density','lpf','hpf','er']) this.p[IDX['rev.' + kk]] = pr[kk];
      }
    }
    apply(k, val) {
      if (k.startsWith('rev.') && !k.startsWith('rev.send') && k !== 'rev.return' && this.rev) this.rev.set(k, val);
      if (k.startsWith('dly.') && !k.startsWith('dly.send') && k !== 'dly.return' && this.dly) this.dly.set(k, val);
    }
    trackVoice(tr) {
      if (tr === 'LT' || tr === 'MT' || tr === 'HT') return this.v(tr + '.sel') >= 0.5 ? tr.charAt(0) + 'C' : tr;
      if (tr === 'RS') return this.v('RS.sel') >= 0.5 ? 'CL' : 'RS';
      if (tr === 'CP') return this.v('CP.sel') >= 0.5 ? 'MA' : 'CP';
      return tr;
    }
    loadSample(v, data, sr) {
      const i = VOICES.indexOf(v); if (i < 0) return;
      const n = Math.max(1, Math.floor(data.length * this.sr / sr)), out = new Float32Array(n), r = sr / this.sr;
      for (let j = 0; j < n; j++) { const x = j * r, a = Math.floor(x), f = x - a; out[j] = (data[a] || 0) + ((data[a + 1] || 0) - (data[a] || 0)) * f; }
      this.smp[i] = out; this.spos[i] = 1e12;
    }
    clearSample(v) { const i = VOICES.indexOf(v); if (i >= 0) this.smp[i] = null; }
    noise() { let x = this.rng; x ^= x << 13; x ^= x >>> 17; x ^= x << 5; this.rng = x >>> 0; return (this.rng / 2147483648) - 1; }
    tf(i) { return 1 + this.v('mod.age') * this.tolF[i] * 0.05; }
    td(i) { return 1 + this.v('mod.age') * this.tolD[i] * 0.2; }
    ec(tau) { return Math.exp(-1 / (Math.max(1e-4, tau) * this.sr)); }
    // continuous-time ring amplitude 1 from a pulse of n samples into the SVF bandpass
    exc(svf, amp, n) { return amp / (2 * svf.g * n); }
    prepare() {
      // mix trims per voice (BD SD LT MT HT LC MC HC RS CL CP MA CB CY OH CH TRIG). The bright metal voices and the maracas
      // sound far louder than they measure, and much louder on phone speakers that can't play the bass drum's 54 Hz
      const TRIM = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.7, 0.85, 0.55, 0.45, 0.45, 1];
      const sr = this.sr, v = k => this.v(k);
      for (let i = 0; i < VOICES.length; i++) {
        const o = OWNER[i];
        const lv = o === 'TRIG' ? v('mod.TRIG.level') : v(o + '.level');
        this.gain[i] = lv * lv * 1.4 * TRIM[i];
        this.rs[i] = v('rev.send.' + o); this.ds[i] = v('dly.send.' + o);
      }
      // BD
      this.bdF = 52 * Math.pow(2, (v('mod.BD.tune') - 0.5) * 2) * this.tf(0);
      this.bdTau = 0.05 * Math.pow(16, v('BD.decay')) * (1 + v('mod.BD.longDecay') * 15) * this.td(0);
      this.op1[0].set(120 * Math.pow(2, v('BD.tone') * 6.5), sr);
      // SD
      const st = Math.pow(2, (v('mod.SD.tune') - 0.5)) * this.tf(1), sdm = 0.4 + 1.2 * v('mod.SD.decay');
      this.res1[1].set(180 * st, Math.PI * 180 * st * 0.06 * sdm, sr); this.res2[1].set(330 * st, Math.PI * 330 * st * 0.045 * sdm, sr);
      this.op1[1].set(600 + 2500 * v('SD.tone'), sr); this.op2[1].set(3000 * Math.pow(2, v('mod.SD.noiseColor') * 2.3), sr);
      this.envC[1] = this.ec(0.09 * (0.5 + v('mod.SD.decay')) * this.td(1));
      // toms / congas
      for (let j = 0; j < 6; j++) {
        const i = 2 + j, o = OWNER[i], fr = TOM_F[j], f = fr[0] * Math.pow(fr[1] / fr[0], v(o + '.tuning')) * this.tf(i);
        const dm = j < 3 ? 0.3 + 1.4 * v('mod.TOM.decay') : 0.3 + 1.4 * v('mod.CONGA.decay');
        this.f1[i].set(f, 1, sr); // store base freq in f1.g? keep separate:
        this.tomF = this.tomF || new Float64Array(6); this.tomF[j] = f;
        this.tomTau = this.tomTau || new Float64Array(6); this.tomTau[j] = TOM_TAU[j] * dm * this.td(i);
        if (this.sw[i] < 1e-4) this.res1[i].set(f, Math.PI * f * this.tomTau[j], sr);
      }
      this.op1[2].set(1500, sr);
      // RS, CL
      const rt = Math.pow(2, v('mod.RS.tune') - 0.5) * this.tf(8);
      this.res1[8].set(455 * rt, Math.PI * 455 * rt * 0.012, sr); this.res2[8].set(1667 * rt, Math.PI * 1667 * rt * 0.007, sr); this.op1[8].set(250, sr);
      const ct = Math.pow(2, v('mod.CL.tune') - 0.5) * this.tf(9);
      this.res1[9].set(2500 * ct, Math.PI * 2500 * ct * 0.02, sr);
      // CP, MA
      this.f1[10].set(1100, 2.5, sr);
      this.cpSpread = Math.round((0.005 + 0.012 * v('mod.CP.spread')) * sr);
      this.envC[10] = this.ec(0.0025); this.env2C[10] = this.ec(0.03 + 0.4 * Math.pow(v('mod.CP.tail'), 1.5));
      this.f1[11].set(6000, 0.8, sr); this.envC[11] = this.ec(0.025 * (0.4 + 1.2 * v('mod.MA.decay')) * this.td(11));
      // CB
      this.cbT = Math.pow(2, v('mod.CB.tune') - 0.5) * this.tf(12);
      this.f1[12].set(900, 1.2, sr); this.op1[12].set(300, sr);
      this.envC[12] = this.ec(0.012); this.env2C[12] = this.ec((0.05 + 0.3 * v('mod.CB.decay')) * this.td(12));
      // metal
      this.mT = Math.pow(2, v('mod.HH.tune') - 0.5);
      const cyt = Math.pow(2, v('mod.CY.tune') - 0.5);
      this.f1[13].set(3440 * cyt, 1.5, sr); this.op1[13].set(7000 * cyt, sr);
      this.envC[13] = this.ec((0.3 + 1.7 * Math.pow(v('CY.decay'), 1.5)) * this.td(13)); this.env2C[13] = this.ec((0.15 + 0.4 * v('CY.decay')) * this.td(13));
      this.f1[14].set(7100, 1.5, sr); this.op1[14].set(6500, sr);
      this.envC[14] = this.choke ? this.ec(0.004) : this.ec((0.06 + 0.6 * v('OH.decay') * v('OH.decay')) * this.td(14));
      this.f1[15].set(7100, 1.5, sr); this.op1[15].set(6500, sr);
      this.envC[15] = this.ec((0.008 + 0.04 * v('mod.CH.decay')) * this.td(15));
      // TRIG
      this.trigHP.set(20 * Math.pow(2, v('mod.TRIG.tone') * 7), sr);
      // samples: decay knob where the voice has one
      for (let i = 0; i < VOICES.length; i++) {
        const o = OWNER[i];
        const dk = o === 'BD' ? v('BD.decay') : o === 'CY' ? v('CY.decay') : o === 'OH' ? v('OH.decay') : 1;
        this.senvC[i] = dk >= 0.999 ? 1 : this.ec(0.03 * Math.pow(200, dk));
        const tu = (o === 'LT' || o === 'MT' || o === 'HT') ? v(o + '.tuning') : 0.5;
        this.srate[i] = Math.pow(2, (tu - 0.5) * 2);
      }
      this.hissG = v('mod.hiss') * 0.003;
      this.dirty = 0;
    }
    trigger(voice, acc, vel) {
      if (this.dirty) this.prepare();
      const i = typeof voice === 'number' ? voice : VOICES.indexOf(voice);
      if (vel == null) vel = 1;
      const a = acc ? Math.min(1.5, this.v('AC.level') * 2 * this.v('mod.AC.depth')) : 0;
      const V = vel * (0.62 + 0.55 * a), punch = acc ? this.v('mod.AC.punch') * this.v('AC.level') : 0;
      const src = Math.round(this.v('mod.TRIG.src'));
      if (voice === 'AC') { if (src === 2) this.fireTrig(V); return; }
      if (i < 0) return;
      if (i === 16) { this.fireTrig(V); return; }
      if ((src === 0 && i === 12) || (src === 1 && (i === 10 || i === 11))) this.fireTrig(V);
      if (this.smp[i]) { this.spos[i] = 0; this.senv[i] = V; this.active[i] = 1; if (i === 15) this.choke = 1; return; }
      const sr = this.sr;
      this.amp[i] = V; this.t[i] = 0; this.active[i] = 1;
      if (i === 0) { this.pulseN[i] = Math.max(4, Math.round(0.001 * sr * (1 - 0.4 * punch))); this.sw[i] = V * (1 + punch); this.swC[i] = this.ec(0.004);
        this.res1[0].set(this.bdF * (1 + this.v('mod.BD.sweep') * 1.2 * this.sw[i]), Math.PI * this.bdF * this.bdTau, sr);
        this.pulseH[i] = this.exc(this.res1[0], V, this.pulseN[i]); }
      else if (i === 1) { this.pulseN[i] = Math.round(0.0005 * sr); this.pulseH[i] = this.exc(this.res1[1], V, this.pulseN[i]); this.pulseH2[i] = this.exc(this.res2[1], V, this.pulseN[i]); this.env[i] = V; }
      else if (i <= 7) { const j = i - 2; this.pulseN[i] = Math.round(0.0007 * sr); this.sw[i] = V; this.swC[i] = this.ec(0.02);
        this.res1[i].set(this.tomF[j], Math.PI * this.tomF[j] * this.tomTau[j], sr); this.pulseH[i] = this.exc(this.res1[i], V, this.pulseN[i]); this.env[i] = V; this.envC[i] = this.ec(0.04); }
      else if (i === 8) { this.pulseN[i] = Math.max(2, Math.round(0.0002 * sr)); this.pulseH[i] = this.exc(this.res1[8], V, this.pulseN[i]); this.pulseH2[i] = this.exc(this.res2[8], V, this.pulseN[i]); }
      else if (i === 9) { this.pulseN[i] = Math.max(2, Math.round(0.0001 * sr)); this.pulseH[i] = this.exc(this.res1[9], V, this.pulseN[i]); }
      else if (i === 10) { this.env[i] = V; this.env2[i] = 0; }
      else if (i === 11) { this.env[i] = V; this.env2[i] = 0; }
      else if (i === 12) { this.env[i] = V * 0.75; this.env2[i] = V * 0.25; }
      else if (i === 13) { this.env[i] = V; this.env2[i] = V; }
      else if (i === 14) { this.choke = 0; this.dirty = 1; this.env[i] = V; }
      else if (i === 15) { this.env[i] = V; this.choke = 1; this.dirty = 1; }
    }
    fireTrig(V) { this.trigHigh = Math.max(1, Math.round(this.v('mod.TRIG.width') * 0.001 * this.sr)); this.trigAmp = V; this.trigFired = 1; this.active[16] = 1; }
    voice(i) {
      const sr = this.sr; let y = 0;
      if (this.smp[i]) {
        const d = this.smp[i], p = this.spos[i];
        if (p >= d.length - 1) { this.active[i] = 0; return 0; }
        const a = Math.floor(p), f = p - a; y = (d[a] + (d[a + 1] - d[a]) * f) * this.senv[i];
        this.spos[i] = p + this.srate[i]; this.senv[i] *= this.senvC[i];
        if (i === 14 && this.choke) this.senv[i] *= 0.995;
        return y;
      }
      const t = this.t[i]++; const pl = t < this.pulseN[i];
      switch (i) {
        case 0: {
          const r = this.res1[0];
          if (this.sw[0] > 1e-4) { this.sw[0] *= this.swC[0]; const f = this.bdF * (1 + this.v('mod.BD.sweep') * 1.2 * this.sw[0]); r.set(f, Math.PI * f * this.bdTau, sr); }
          const x = pl ? this.pulseH[0] : 0;
          y = r.tick(x) + (pl ? this.amp[0] * this.v('mod.BD.click') * 0.6 : 0);
          y = this.op1[0].lp(y);
          const dr = this.v('mod.BD.drive'); if (dr > 0.001) { const k = 1 + 6 * dr; y = Math.tanh(y * k) / Math.tanh(k); }
          if (t > sr * 0.05 && Math.abs(r.ic1) + Math.abs(r.ic2) < 1e-6) this.active[0] = 0;
          return y * 0.9;
        }
        case 1: {
          const b = this.res1[1].tick(pl ? this.pulseH[1] : 0) * (1.1 - 0.6 * this.v('SD.tone')) + this.res2[1].tick(pl ? this.pulseH2[1] : 0) * (0.4 + 0.6 * this.v('SD.tone'));
          const nz = this.op2[1].lp(this.op1[1].hp(this.noise())) * this.env[1] * this.v('SD.snappy') * 1.6;
          this.env[1] *= this.envC[1];
          if (t > sr * 1.5) this.active[1] = 0;
          return (b * 0.7 + nz) * 0.8;
        }
        case 8: { let x = this.res1[8].tick(pl ? this.pulseH[8] : 0) * 0.8 - this.res2[8].tick(pl ? this.pulseH2[8] : 0) * 0.5;
          x = this.op1[8].hp(x); if (t > sr * 0.3) this.active[8] = 0; return Math.tanh(2 * x) * 0.55; }
        case 9: { if (t > sr * 0.3) this.active[9] = 0; return this.res1[9].tick(pl ? this.pulseH[9] : 0) * 0.7; }
        case 10: {
          const sp = this.cpSpread;
          if (t < 3 * sp) { if (t % sp === 0) this.env[10] = this.amp[10]; } else if (t === 3 * sp) this.env2[10] = this.amp[10] * 0.7;
          const e = this.env[10] + this.env2[10]; this.env[10] *= this.envC[10]; this.env2[10] *= this.env2C[10];
          if (t > 3 * sp && e < 1e-5) this.active[10] = 0;
          return this.f1[10].tick(this.noise()) * e * 1.6;
        }
        case 11: { this.env2[11] += (1 - this.env2[11]) * 0.02; const e = this.env[11] * this.env2[11]; this.env[11] *= this.envC[11];
          this.f1[11].tick(this.noise()); if (this.env[11] < 1e-5) this.active[11] = 0; return this.f1[11].hp * e * 0.9; }
        case 12: {
          const f0 = 540 * this.cbT / sr, f1 = 800 * this.cbT / sr;
          this.cph[0] += f0; if (this.cph[0] >= 1) this.cph[0] -= 1; this.cph[1] += f1; if (this.cph[1] >= 1) this.cph[1] -= 1;
          const x = (sq(this.cph[0], f0) + sq(this.cph[1], f1)) * 0.5;
          const e = this.env[12] + this.env2[12]; this.env[12] *= this.envC[12]; this.env2[12] *= this.env2C[12];
          if (e < 1e-5) this.active[12] = 0;
          return this.op1[12].hp(this.f1[12].tick(x)) * e * 1.2;
        }
        case 16: {
          const x = this.trigHigh > 0 ? this.trigAmp : 0; if (this.trigHigh > 0) this.trigHigh--;
          const yy = Math.tanh(this.trigHP.hp(x) * 3) * 0.8;
          if (this.trigHigh === 0 && Math.abs(this.trigHP.s - x) < 1e-5) this.active[16] = 0;
          return yy;
        }
        default: {
          // toms / congas
          const j = i - 2, r = this.res1[i];
          if (this.sw[i] > 1e-4) { this.sw[i] *= this.swC[i]; const dep = j < 3 ? 0.6 * this.v('mod.TOM.sweep') : 0.05;
            const f = this.tomF[j] * (1 + dep * this.sw[i]); r.set(f, Math.PI * f * this.tomTau[j], sr); }
          y = r.tick(pl ? this.pulseH[i] : 0);
          if (j < 3) { y += this.op1[2].lp(this.noise()) * this.env[i] * 0.25; this.env[i] *= this.envC[i]; }
          if (t > sr * 0.1 && Math.abs(r.ic1) + Math.abs(r.ic2) < 1e-6) this.active[i] = 0;
          return y * 0.85;
        }
      }
    }
    process(outL, outR, n, off) {
      off = off || 0;
      if (this.dirty) this.prepare();
      const sr = this.sr;
      for (let s0 = 0; s0 < n; s0 += this.bufN) {
        const m = Math.min(this.bufN, n - s0);
        for (let s = 0; s < m; s++) {
          let dry = this.noise() * this.hissG, rv = 0, dl = 0;
          // shared metal bank for CY, OH, CH
          if (this.active[13] || this.active[14] || this.active[15]) {
            let x = 0;
            for (let k = 0; k < 6; k++) { const f = METAL_F[k] * this.mT / sr; this.mph[k] += f; if (this.mph[k] >= 1) this.mph[k] -= 1; x += sq(this.mph[k], f); }
            x /= 6;
            if (this.active[13] && !this.smp[13]) {
              const lo = this.f1[13].tick(x) * this.env[13], hi = this.op1[13].hp(x) * this.env2[13]; this.env[13] *= this.envC[13]; this.env2[13] *= this.env2C[13];
              const tn = this.v('CY.tone'); const y = (lo * (1.2 * (1 - tn) + 0.2) + hi * (0.4 + tn)) * 0.9;
              if (this.env[13] < 1e-5) this.active[13] = 0;
              dry += y * this.gain[13]; rv += y * this.gain[13] * this.rs[13]; dl += y * this.gain[13] * this.ds[13];
            }
            for (let h = 14; h <= 15; h++) if (this.active[h] && !this.smp[h]) {
              const y = this.op1[h].hp(this.f1[h].tick(x)) * this.env[h] * 1.8; this.env[h] *= this.envC[h];
              if (this.env[h] < 1e-5) this.active[h] = 0;
              dry += y * this.gain[h]; rv += y * this.gain[h] * this.rs[h]; dl += y * this.gain[h] * this.ds[h];
            }
          }
          for (let i = 0; i < 17; i++) {
            if (!this.active[i]) continue;
            if ((i >= 13 && i <= 15) && !this.smp[i]) continue;
            const y = this.voice(i) * this.gain[i];
            dry += y; rv += y * this.rs[i]; dl += y * this.ds[i];
          }
          this.rIn[s] = rv; this.dIn[s] = dl;
          outL[off + s0 + s] = dry; outR[off + s0 + s] = dry;
        }
        if (this.rev) this.rev.process(this.rIn, this.rIn, this.rL, this.rR, m, 0);
        if (this.dly) this.dly.process(this.dIn, this.dIn, this.dL, this.dR, m, 0);
        const rr = this.v('rev.return'), dr = this.v('dly.return'), ms = this.v('master');
        for (let s = 0; s < m; s++) {
          const j = off + s0 + s;
          let l = outL[j], r = outR[j];
          if (this.rev) { l += this.rL[s] * rr; r += this.rR[s] * rr; }
          if (this.dly) { l += this.dL[s] * dr; r += this.dR[s] * dr; }
          outL[j] = Math.tanh(l * ms * 1.2); outR[j] = Math.tanh(r * ms * 1.2);
        }
      }
    }
  }
  Eight808.PARAMS = P; Eight808.VOICES = VOICES; Eight808.SEND_VOICES = SENDV;
  g.Eight808 = Eight808;
  if (typeof module !== 'undefined' && module.exports) module.exports = {Eight808};
})(typeof globalThis !== 'undefined' ? globalThis : this);
