/* Omega Solina: the Eminent / ARP Solina String Ensemble modeled from its service schematics (Service Information 0476,
   sheets 015.0212 schematic, 015.0213 wiring, 015.0214 signal flow), with an RX5-style mixer and an effects rack.
   This file is the engine: the web app runs it in an AudioWorklet, plugins/solina/Solina.h is a line-for-line C++ port,
   and solina/tools/compare.mjs checks the two render the same samples.

   Signal flow (sheet 015.0214):
     master oscillator + tuning -> 9x SAJ110 dividers -> sawtooth circuits -> 10x TDA0470 key gates (per-key sustain)
     -> gate output circuit: 5 key ranges x (8', 4'), each a diode + series R + 10k||C low-pass:
          8-1..8-5  18k/2n2, 12k/4n7, 10k/10n, 8k2/12n, 5k6/18n        (8' = Viola)
          4-1..4-5  10k/5n6, 8k2/10n, 5k6/22n, 8k2/47n, 5k6/68n        (4' = Violin)
        8' ranges 8-5,8-4 -> 180n/22k, 8-3..8-1 -> 270n/22k, both 10k -> 6k8 -> VIOLA tab
        4' ranges 4-5,4-4 -> 47n/22k -> 10k||4n7, 4-3..4-1 -> 47n/22k -> 18k||1n -> VIOLIN tab   (into 5k6||2k2 at the 709)
     -> formant circuit (TR4, TR5) -> TRUMPET / HORN tabs
     -> pre-amplifier (709) -> voltage controlled amplifier (TR3, CRESCENDO: 22uF via 5k6 + 25k log)
     -> low-pass filter (TR2: 22k, 22k+1k, 1n5, 220p = 12.3 kHz, Q 1.3)
     bass circuit: low-tone selection (lowest key of the bottom octave) -> clipper -> 5n8 (8' CELLO) / 10n (16' CONTRA BASS)
        into 100k||100k + diode -> TR1 low-pass (47k, 47k, 47n, 5n6 = 209 Hz, Q 1.45) -> VOLUME BASS 5k
     -> modulator circuits I-III: TCA350Y bucket brigades (185 stages) clocked by BC260B astables (39k, 560p = 33 kHz),
        clocks swept by C1/C2/C3 from the control circuit (chorus and tremolo relaxation oscillators, RC low-pass,
        phase shift, inverter); each line's output through two Sallen-Key low-passes (22k/22k+1k: 8n2+47p, 2n7+560p)
     -> output amplifier (741, VOLUME, expression pedal) -> HIGH OUTPUT, or LOW OUTPUT through the correction filter
        (TR7: 22k, 22k+1k, 27n, 1n5 = 1.11 kHz Q 2.1; TR11: 12n, 6n8 = 783 Hz Q 0.66)
   Keys: 15uF charged through 820R, released through 5k6 + 10k SUSTAIN LENGTH (or quickly with SUSTAIN off).

   Added around it (not in the Solina): an RX5-style mixer with a strip per section (level, pan, pitch, attack, decay,
   tone, ensemble on/off, echo and reverb sends, mute, solo), and master effects: phaser (4-stage OTA, Small Stone
   style), echo (analog-style, tempo sync, ping-pong), reverb (8-line feedback delay network), 3-band EQ and drive. */
'use strict';
(function(G){

const CH = ['vn', 'va', 'tp', 'hn', 'ce', 'cb'];
const CH_NAMES = ['Violin', 'Viola', 'Trumpet', 'Horn', 'Cello', 'Contra Bass'];
const CH_TABS = ['violin', 'viola', 'trumpet', 'horn', 'cello', 'contra'];

// {id, name, min, max, def, step (0 = continuous, 1 = integer / switch), unit, group, cc (default MIDI CC, -1 none), choices}
const PARAMS = [];
const P_ = (id, name, min, max, def, step, unit, group, cc, choices) => PARAMS.push({id, name, min, max, def, step, unit, group, cc: cc == null ? -1 : cc, choices: choices || []});
// the panel: tabs, crescendo, sustain, bass volume, modulation, tuning, output
P_('violin', 'Violin', 0, 1, 1, 1, '', 'panel', 80);
P_('viola', 'Viola', 0, 1, 1, 1, '', 'panel', 81);
P_('trumpet', 'Trumpet', 0, 1, 0, 1, '', 'panel', 82);
P_('horn', 'Horn', 0, 1, 0, 1, '', 'panel', 83);
P_('cello', 'Cello', 0, 1, 1, 1, '', 'panel', 84);
P_('contra', 'Contra Bass', 0, 1, 0, 1, '', 'panel', 85);
P_('cresc', 'Crescendo', 0, 1, .3, 0, '%', 'panel', 73);
P_('sus', 'Sustain', 0, 1, 1, 1, '', 'panel', 86);
P_('susLen', 'Sustain Length', 0, 1, .5, 0, '%', 'panel', 72);
P_('bassVol', 'Bass Volume', 0, 1, .7, 0, '%', 'panel', 75);
P_('ens', 'Modulation', 0, 1, 1, 1, '', 'panel', 87);
P_('tune', 'Tuning', -50, 50, 0, 0, 'ct', 'panel', 76);
P_('outSel', 'Output', 0, 1, 0, 1, '', 'panel', 88, ['High', 'Low']);
P_('volume', 'Volume', 0, 1, .8, 0, '%', 'panel', 7);
// the control circuit's trimmers (chorus and tremolo oscillators, depth)
P_('chorusHz', 'Chorus Rate', .2, 2, .75, 0, 'Hz', 'ensemble', 77);
P_('tremHz', 'Tremolo Rate', 3, 9, 6.3, 0, 'Hz', 'ensemble', 78);
P_('depth', 'Ensemble Depth', 0, 1, .35, 0, '%', 'ensemble', 93);
P_('trem', 'Tremolo', 0, 1, .25, 0, '%', 'ensemble', 92);
P_('wide', 'Stereo Lines', 0, 1, 0, 1, '', 'ensemble', 89);
// RX5-style mixer: one strip per section
const CC_BASE = {Lvl: 20, Pan: 26, Ech: 102, Rev: 108};
for (let c = 0; c < 6; c++){
  const k = CH[c], g = 'mixer';
  P_(k + 'Lvl', CH_NAMES[c] + ' Level', 0, 1, .8, 0, '%', g, CC_BASE.Lvl + c);
  P_(k + 'Pan', CH_NAMES[c] + ' Pan', -1, 1, 0, 0, '', g, CC_BASE.Pan + c);
  P_(k + 'Pit', CH_NAMES[c] + ' Pitch', -12, 12, 0, 1, 'st', g, -1);
  P_(k + 'Atk', CH_NAMES[c] + ' Attack', 0, 1, 0, 0, '%', g, -1);
  P_(k + 'Dec', CH_NAMES[c] + ' Decay', 0, 1, .5, 0, '%', g, -1);
  P_(k + 'Ton', CH_NAMES[c] + ' Tone', 0, 1, 1, 0, '%', g, -1);
  P_(k + 'Ens', CH_NAMES[c] + ' Ensemble', 0, 1, 1, 1, '', g, -1);
  P_(k + 'Ech', CH_NAMES[c] + ' Echo Send', 0, 1, 0, 0, '%', g, CC_BASE.Ech + c);
  P_(k + 'Rev', CH_NAMES[c] + ' Reverb Send', 0, 1, .25, 0, '%', g, CC_BASE.Rev + c);
  P_(k + 'Mut', CH_NAMES[c] + ' Mute', 0, 1, 0, 1, '', g, -1);
  P_(k + 'Sol', CH_NAMES[c] + ' Solo', 0, 1, 0, 1, '', g, -1);
}
// effects
P_('phOn', 'Phaser', 0, 1, 0, 1, '', 'phaser', 94);
P_('phRate', 'Phaser Rate', .02, 6, .4, 0, 'Hz', 'phaser', 14);
P_('phDepth', 'Phaser Depth', 0, 1, .8, 0, '%', 'phaser', 15);
P_('phFb', 'Phaser Color', 0, .95, .4, 0, '%', 'phaser', 16);
P_('phMix', 'Phaser Mix', 0, 1, .5, 0, '%', 'phaser', 17);
P_('dlOn', 'Echo', 0, 1, 1, 1, '', 'echo', 95);
P_('dlTime', 'Echo Time', 20, 1500, 375, 0, 'ms', 'echo', 18);
P_('dlSync', 'Echo Sync', 0, 6, 0, 1, '', 'echo', -1, ['Free', '1/4', '1/8', '1/8 dot', '1/16', '1/4 trip', '1/2']);
P_('dlFb', 'Echo Feedback', 0, .95, .4, 0, '%', 'echo', 19);
P_('dlTone', 'Echo Tone', 0, 1, .6, 0, '%', 'echo', 9);
P_('dlPing', 'Ping-Pong', 0, 1, 1, 1, '', 'echo', -1);
P_('dlRet', 'Echo Return', 0, 1, .7, 0, '%', 'echo', 3);
P_('rvOn', 'Reverb', 0, 1, 1, 1, '', 'reverb', 96);
P_('rvSize', 'Reverb Size', 0, 1, .6, 0, '%', 'reverb', 13);
P_('rvDecay', 'Reverb Decay', .3, 10, 2.8, 0, 's', 'reverb', 12);
P_('rvDamp', 'Reverb Damping', 0, 1, .45, 0, '%', 'reverb', 70);
P_('rvPre', 'Pre-delay', 0, 200, 20, 0, 'ms', 'reverb', 71);
P_('rvRet', 'Reverb Return', 0, 1, .6, 0, '%', 'reverb', 91);
P_('eqLow', 'Bass EQ', -12, 12, 0, 0, 'dB', 'master', 110);
P_('eqMid', 'Mid EQ', -12, 12, 0, 0, 'dB', 'master', 111);
P_('eqHigh', 'Treble EQ', -12, 12, 0, 0, 'dB', 'master', 74);
P_('drive', 'Drive', 0, 1, 0, 0, '%', 'master', 112);
P_('master', 'Master', 0, 1, .8, 0, '%', 'master', 113);
// playing
P_('velSens', 'Velocity', 0, 1, 0, 0, '%', 'play', -1);
P_('bendRange', 'Bend Range', 0, 12, 2, 1, 'st', 'play', -1);
P_('modDest', 'Mod Wheel', 0, 2, 0, 1, '', 'play', -1, ['Ensemble depth', 'Tremolo', 'Phaser rate']);

const PID = {}; PARAMS.forEach((p, i) => PID[p.id] = i);
const defaults = () => { const o = {}; for (const p of PARAMS) o[p.id] = p.def; return o; };

// top-octave generator divisors C..B (the master oscillator divided to the top octave), 8' groups, 4' groups
const TOS = [478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253];
const G8_R = [18000, 12000, 10000, 8200, 5600], G8_C = [2.2e-9, 4.7e-9, 10e-9, 12e-9, 18e-9];
const G4_R = [10000, 8200, 5600, 8200, 5600], G4_C = [5.6e-9, 10e-9, 22e-9, 47e-9, 68e-9];
// the key ranges of the ten gate circuits, measured from the keyboard drawing (panel A): 1 is the top of the keyboard
const groupOf = n => n >= 75 ? 0 : n >= 65 ? 1 : n >= 54 ? 2 : n >= 42 ? 3 : 4;
const SYNC_BEATS = [0, 1, .5, .75, .25, 2 / 3, 2];
const RV_LEN = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
// level makeup per section so each tab alone at the stock settings plays at about the same loudness
const CAL = [4.26, 1.93, 1.59, .89, 2.24, 1.93];

class SolinaEngine{
  constructor(fs){
    this.fs = fs;
    this.p = defaults();
    this.keys = new Map();            // note -> {down, held, v, g:[4]}
    this.sustainPedal = false; this.bendSt = 0; this.mod = 0; this.expr = 1; this.bpm = 120;
    this.cyc = new Float64Array(12);
    this.detune = TOS.map((d, pc) => (TOS[9] / d) / Math.pow(2, (pc - 9) / 12));
    this.fTop = TOS.map((d, pc) => 440 * Math.pow(2, (pc - 9) / 12 + 4) * this.detune[pc]);
    this.shR = TOS.map((d, pc) => .35 * Math.pow(2, (pc - 6) / 12)); this.shA = this.shR.map(r => Math.exp(-.5 / r));
    this.wc = new Float64Array(160); this.ws = new Float64Array(160).fill(-1); this.n = 0;
    this.cresc = 0;
    // gate output circuit: 5 ranges x 2 footages (one-pole low-pass, level of the divider)
    const lpk = (R, C) => { const Rt = R * 10000 / (R + 10000); return 1 - Math.exp(-1 / (Rt * C * fs)); };
    this.g8k = G8_R.map((R, i) => lpk(R, G8_C[i])); this.g8a = G8_R.map(R => 10000 / (R + 10000));
    this.g4k = G4_R.map((R, i) => lpk(R, G4_C[i])); this.g4a = G4_R.map(R => 10000 / (R + 10000));
    // per section and range: the range bus before the gate-output RC (ch 0-3: violin 4', viola 8', trumpet, horn)
    this.bus = new Float64Array(4 * 5 * 2); this.bz = new Float64Array(4 * 5 * 2);
    // register circuit couplings (high-passes) and the violin's series RC networks into the 709's 5k6||2k2
    const hp1 = (R, C) => Math.exp(-1 / (R * C * fs));
    this.hpVaLo = hp1(22000, 180e-9); this.hpVaHi = hp1(22000, 270e-9); this.hpVn = hp1(22000, 47e-9);
    this.shVnLo = this.shelf(10000, 4.7e-9, 1580); this.shVnHi = this.shelf(18000, 1e-9, 1580);
    this.hz = new Float64Array(32);    // high-pass and shelf states per section (8 each)
    // formant circuit
    this.fTr = this.bp(1350, 1.3); this.fHo = this.bp(680, 1.1); this.fz = new Float64Array(4);
    // pre-ensemble low-pass (TR2) on the two stereo buses (ensemble, dry)
    this.lpPre = this.lp2(1 / (2 * Math.PI * Math.sqrt(22000 * 23000 * 1.5e-9 * 220e-12)), 1.305);
    this.preZ = new Float64Array(8);
    // bass circuit
    this.bassF = this.lp2(1 / (2 * Math.PI * Math.sqrt(47000 * 47000 * 47e-9 * 5.6e-9)), 1.45);
    this.bassZ = new Float64Array(4); this.bassG = [0, 0]; this.bassNote = -1; this.bassV = 1;
    // modulators: three bucket brigades on each side, two low-passes after each
    this.bufL = new Float64Array(16384); this.bufR = new Float64Array(16384); this.w = 0;
    this.post1 = this.lp2(11400, .707); this.post2 = this.lp2(1 / (2 * Math.PI * Math.sqrt(22000 * 23000 * 2.7e-9 * 560e-12)), 1.1);
    this.postZ = new Float64Array(24);
    this.phC = 0; this.phT = 0; this.sC = 1; this.sT = 1; this.lf = new Float64Array(7);
    this.lfk = [1.33, .234, 2.84, 4.18, 1.30, 41.8, .482].map(f => 1 - Math.exp(-2 * Math.PI * f / fs));
    // output correction filter (LOW OUTPUT)
    this.corr1 = this.lp2(1 / (2 * Math.PI * Math.sqrt(22000 * 23000 * 27e-9 * 1.5e-9)), 2.12);
    this.corr2 = this.lp2(1 / (2 * Math.PI * Math.sqrt(22000 * 23000 * 12e-9 * 6.8e-9)), .664);
    this.corrZ = new Float64Array(8);
    // mixer
    this.tz = new Float64Array(6); this.sm = new Float64Array(6 * 3); this.smInit = false;
    this.smk = 1 - Math.exp(-1 / (.01 * fs));
    // phaser
    this.phz = new Float64Array(8); this.phLast = [0, 0]; this.phPh = 0;
    // echo
    this.dlN = 1 << Math.ceil(Math.log2(1.6 * fs)); this.dlL = new Float64Array(this.dlN); this.dlR = new Float64Array(this.dlN);
    this.dlW = 0; this.dlT = -1; this.dlZ = [0, 0];
    // reverb
    const sc = fs / 44100; this.rvMax = RV_LEN.map(l => Math.ceil(l * 2 * sc) + 2);
    this.rvBuf = this.rvMax.map(n => new Float64Array(n)); this.rvW = new Int32Array(8); this.rvLp = new Float64Array(8);
    this.preN = 1 << Math.ceil(Math.log2(.21 * fs)); this.preBuf = new Float64Array(this.preN); this.preW = 0;
    this.rvCfg = '';
    // EQ
    this.eqCfg = ''; this.eqZ = new Float64Array(12);
    this.chs = new Float64Array(6); this.cv = new Float64Array(3); this.yl = new Float64Array(3); this.yr = new Float64Array(3);
    this.rvO = new Float64Array(8); this.rvH = new Float64Array(8); this.idle = 0;
    this.update();
  }
  // RBJ-free analog prototypes with the bilinear transform (prewarped), as in the Omega System's Solina
  lp2(f, Q){ const fs = this.fs, w0 = 2 * Math.PI * Math.min(f, fs * .45), K = w0 / Math.tan(w0 / (2 * fs)), d = K * K + w0 / Q * K + w0 * w0;
    return [w0 * w0 / d, 2 * w0 * w0 / d, w0 * w0 / d, (2 * w0 * w0 - 2 * K * K) / d, (K * K - w0 / Q * K + w0 * w0) / d]; }
  bp(f, Q){ const w0 = 2 * Math.PI * f / this.fs, al = Math.sin(w0) / (2 * Q), a0 = 1 + al; return [al / a0, 0, -al / a0, -2 * Math.cos(w0) / a0, (1 - al) / a0]; }
  // R||C in series into a load Rl: H(s) = Rl (1 + sRC) / (R + Rl + sRC Rl), bilinear
  shelf(R, C, Rl){ const K = 2 * this.fs, t = R * C, b0 = Rl + Rl * t * K, b1 = Rl - Rl * t * K, a0 = R + Rl + t * Rl * K, a1 = R + Rl - t * Rl * K;
    return [b0 / a0, b1 / a0, a1 / a0]; }
  peq(type, f, gdb, Q){ const fs = this.fs, A = Math.pow(10, gdb / 40), w = 2 * Math.PI * f / fs, c = Math.cos(w), s = Math.sin(w);
    let b0, b1, b2, a0, a1, a2;
    if (type === 0){ const al = s / 2 * Math.sqrt(2), sa = 2 * Math.sqrt(A) * al;   // low shelf, S = 1
      b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
      a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa; }
    else if (type === 2){ const al = s / 2 * Math.sqrt(2), sa = 2 * Math.sqrt(A) * al;  // high shelf
      b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
      a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa; }
    else { const al = s / (2 * Q); b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0]; }
  bq(q, z, o, x){ const y = q[0] * x + z[o]; z[o] = q[1] * x - q[3] * y + z[o + 1]; z[o + 1] = q[2] * x - q[4] * y; return y; }

  set(id, v){ if (id in this.p){ this.p[id] = v; this.update(); } }
  setAll(o){ for (const k in o) if (k in this.p) this.p[k] = o[k]; this.update(); }
  // derived values: envelopes, gains, effect coefficients (control rate)
  update(){
    const p = this.p, fs = this.fs;
    this.atkK = []; this.relK = [];
    for (let c = 0; c < 6; c++){
      const k = CH[c];
      this.atkK[c] = 1 - Math.exp(-1 / (820 * 15e-6 * Math.pow(2, p[k + 'Atk'] * 8) * fs));
      const tr = (p.sus ? 15e-6 * (5600 + p.susLen * 10000) : .012) * Math.pow(2, (p[k + 'Dec'] - .5) * 4);
      this.relK[c] = Math.exp(-1 / (tr * fs));
    }
    const tauC = (5600 + 25000 * (Math.pow(10, 2 * p.cresc) - 1) / 99) * 22e-6; this.crK = 1 - Math.exp(-1 / (tauC * fs));
    let solo = false; for (let c = 0; c < 6; c++) if (p[CH[c] + 'Sol']) solo = true;
    this.on = []; this.gain = []; this.panL = []; this.panR = []; this.tk = [];
    for (let c = 0; c < 6; c++){
      const k = CH[c], on = p[CH_TABS[c]] && !p[k + 'Mut'] && (!solo || p[k + 'Sol']);
      this.on[c] = on ? 1 : 0;
      const l = p[k + 'Lvl'] / .8; this.gain[c] = on ? l * l * CAL[c] : 0;
      const th = (p[k + 'Pan'] + 1) * Math.PI / 4; this.panL[c] = Math.cos(th) * Math.SQRT2; this.panR[c] = Math.sin(th) * Math.SQRT2;
      const fc = 150 * Math.pow(2, p[k + 'Ton'] * 7.1); this.tk[c] = p[k + 'Ton'] >= 1 ? 1 : 1 - Math.exp(-2 * Math.PI * fc / fs);
    }
    this.bassVolG = p.bassVol * 1.6;
    // tuning and bend: the master oscillator's frequency
    this.fMul = Math.pow(2, (p.tune / 100 + this.bendSt) / 12);
    // reverb lengths and decay
    const rk = p.rvSize + ':' + p.rvDecay + ':' + p.rvDamp;
    if (rk !== this.rvCfg){ this.rvCfg = rk; const sc = fs / 44100 * (.5 + 1.5 * p.rvSize);
      this.rvLen = RV_LEN.map((l, i) => Math.min(this.rvMax[i] - 1, Math.max(8, Math.round(l * sc))));
      this.rvG = this.rvLen.map(l => Math.pow(10, -3 * l / (p.rvDecay * fs)));
      this.rvDk = 1 - Math.exp(-2 * Math.PI * (18000 * Math.pow(.06, p.rvDamp)) / fs); }
    const ek = p.eqLow + ':' + p.eqMid + ':' + p.eqHigh;
    if (ek !== this.eqCfg){ this.eqCfg = ek; this.eq = [this.peq(0, 150, p.eqLow, .7), this.peq(1, 1000, p.eqMid, .7), this.peq(2, 6000, p.eqHigh, .7)];
      this.eqFlat = !p.eqLow && !p.eqMid && !p.eqHigh; }
    this.dlK = 1 - Math.exp(-2 * Math.PI * (600 * Math.pow(25, p.dlTone)) / fs);
    this.driveK = 1 + 9 * p.drive;
    const ml = p.master / .8, vl = p.volume / .8; this.outG = ml * ml * vl * vl;
  }

  noteOn(n, v){ if (v <= 0){ this.noteOff(n); return; } let k = this.keys.get(n); if (!k){ k = {down:false, held:false, v:1, g:[0, 0, 0, 0]}; this.keys.set(n, k); }
    k.down = true; k.held = true; k.v = 1 - this.p.velSens * (1 - v); this.idle = 0; }
  noteOff(n){ const k = this.keys.get(n); if (!k) return; k.held = false; if (!this.sustainPedal) k.down = false; }
  pedal(on){ this.sustainPedal = on; if (!on) for (const k of this.keys.values()) if (!k.held) k.down = false; }
  bend(v){ this.bendSt = v * this.p.bendRange; this.update(); }       // v -1..1
  modWheel(v){ this.mod = v; }
  expression(v){ this.expr = v; }
  allOff(){ for (const k of this.keys.values()){ k.down = false; k.held = false; } this.sustainPedal = false; }

  // a divider output through its sawtooth circuit: exponential ramp for the first half cycle, nothing for the second
  wave(m){
    if (m < 0 || m > 150) return 0;
    if (this.ws[m] === this.n) return this.wc[m];
    const pc = m % 12, oct = Math.floor(m / 12) - 1, kk = 8 - oct, div = kk >= 0 ? (1 << kk) : Math.pow(2, kk);
    const ph = (this.cyc[pc] / div) % 1, dt = this.fTop[pc] * this.fMul / div / this.fs;
    let y = 0;
    if (dt < .5){
      const r = this.shR[pc], a = this.shA[pc];
      y = ph < .5 ? Math.exp(-ph / r) : 0;
      y += .5 * this.blep(ph, dt); y -= .5 * a * this.blep((ph + .5) % 1, dt);
      y -= .3;
    }
    this.ws[m] = this.n; this.wc[m] = y; return y;
  }
  // the bass circuit: the clipped square differentiated by C into 50k (100k||100k), negative spikes cut by the diode
  spike(m, tau){
    if (m < 0 || m > 150) return 0;
    const pc = m % 12, oct = Math.floor(m / 12) - 1, kk = 8 - oct, div = kk >= 0 ? (1 << kk) : Math.pow(2, kk);
    const ph = (this.cyc[pc] / div) % 1, f = this.fTop[pc] * this.fMul / div, dt = f / this.fs;
    if (dt >= .5) return 0;
    const x = ph / (f * tau); return Math.exp(-x) + this.blep(ph, dt) * .5 - f * tau;
  }
  blep(t, dt){ if (t < dt){ const x = t / dt; return x + x - x * x - 1; } if (t > 1 - dt){ const x = (t - 1) / dt; return x * x + x + x + 1; } return 0; }

  // renders n samples into L, R (Float32Array or Float64Array) starting at offset o
  process(L, R, o, n){
    const p = this.p, fs = this.fs;
    if (!this.keys.size && this.bassG[0] < 1e-5 && this.bassG[1] < 1e-5 && this.idle > fs * 12){
      for (let i = 0; i < n; i++){ L[o + i] = 0; R[o + i] = 0; } this.cresc = 0; return; }
    const bus = this.bus, bz = this.bz, hz = this.hz, cyc = this.cyc, step = this.fMul / fs;
    const on = this.on, gain = this.gain, smk = this.smk, sm = this.sm;
    if (!this.smInit){ this.smInit = true; for (let c = 0; c < 6; c++){ sm[c * 3] = gain[c]; sm[c * 3 + 1] = this.panL[c]; sm[c * 3 + 2] = this.panR[c]; } }
    const pit = CH.map(k => p[k + 'Pit'] | 0), ensOn = CH.map(k => p[k + 'Ens']), ech = CH.map(k => p[k + 'Ech']), rev = CH.map(k => p[k + 'Rev']);
    // mod wheel
    const depth = Math.min(1, p.depth + (p.modDest === 0 ? this.mod * .65 : 0)), tremA = Math.min(1, p.trem + (p.modDest === 1 ? this.mod : 0));
    const phRate = p.phRate * (p.modDest === 2 ? 1 + this.mod * 6 : 1);
    const lf = this.lf, kc = this.lfk;
    // echo time
    let dlMs = p.dlTime; if (p.dlSync > 0) dlMs = Math.min(1500, SYNC_BEATS[p.dlSync] * 60000 / this.bpm);
    const dlTarget = dlMs * .001 * fs; if (this.dlT < 0) this.dlT = dlTarget;
    const rvLen = this.rvLen, rvG = this.rvG, rvDk = this.rvDk, rvBuf = this.rvBuf, rvW = this.rvW, rvLp = this.rvLp;
    const preD = Math.max(1, Math.round(p.rvPre * .001 * fs));
    let alive = false;
    for (let i = 0; i < n; i++){
      this.n++;
      for (let pc = 0; pc < 12; pc++){ cyc[pc] += this.fTop[pc] * step; if (cyc[pc] >= 1048576) cyc[pc] -= 1048576; }
      bus.fill(0);
      // keyed voices: each section has its own keying envelope (RX5 attack / decay; stock = the 15uF circuit)
      let any = false, low = 999;
      for (const [nt, k] of this.keys){
        let live = false;
        for (let c = 0; c < 4; c++){
          k.g[c] = k.down ? k.g[c] + (1 - k.g[c]) * this.atkK[c] : k.g[c] * this.relK[c];
          if (k.g[c] > 1e-4) live = true;
        }
        if (!live && !k.down){ this.keys.delete(nt); continue; }
        if (k.down && nt < 48 && nt < low) low = nt;
        const gr = groupOf(nt), v = k.v;
        if (on[0] && k.g[0] > 1e-5) bus[0 * 10 + 5 + gr] += k.g[0] * v * this.wave(nt + pit[0] + 12);           // violin 4'
        if (on[1] && k.g[1] > 1e-5) bus[1 * 10 + gr] += k.g[1] * v * this.wave(nt + pit[1]);                    // viola 8'
        if (on[2] && k.g[2] > 1e-5){ const m = nt + pit[2]; bus[2 * 10 + gr] += k.g[2] * v * this.wave(m); bus[2 * 10 + 5 + gr] += k.g[2] * v * this.wave(m + 12); }
        if (on[3] && k.g[3] > 1e-5){ const m = nt + pit[3]; bus[3 * 10 + gr] += k.g[3] * v * this.wave(m); bus[3 * 10 + 5 + gr] += k.g[3] * v * this.wave(m + 12); }
        if (k.g[0] > .01 || k.g[1] > .01 || k.g[2] > .01 || k.g[3] > .01) any = true;
      }
      // gate output circuit: each range's RC low-pass, per section
      for (let c = 0; c < 4; c++){
        if (!on[c]) continue;
        for (let g = 0; g < 5; g++){
          const i8 = c * 10 + g, i4 = c * 10 + 5 + g;
          bz[i8] += (bus[i8] * this.g8a[g] - bz[i8]) * this.g8k[g];
          bz[i4] += (bus[i4] * this.g4a[g] - bz[i4]) * this.g4k[g];
        }
      }
      // register circuit
      // viola (8'): ranges 8-5,8-4 through 180n, 8-3..8-1 through 270n (22k), two 10k into the node, 6k8 to the tab
      let va = 0, vn = 0;
      if (on[1]){ const b = 10, a = bz[b + 3] + bz[b + 4], c3 = bz[b] + bz[b + 1] + bz[b + 2];
        const ya = this.hpVaLo * (hz[0] + a - hz[1]); hz[0] = ya; hz[1] = a;
        const yb = this.hpVaHi * (hz[2] + c3 - hz[3]); hz[2] = yb; hz[3] = c3;
        va = (ya + yb) * .5 * .19; }
      // violin (4'): ranges 4-5,4-4 -> 47n/22k -> 10k||4n7; 4-3..4-1 -> 47n/22k -> 18k||1n
      if (on[0]){ const b = 5, a = bz[b + 3] + bz[b + 4], c3 = bz[b] + bz[b + 1] + bz[b + 2];
        const ya = this.hpVn * (hz[8] + a - hz[9]); hz[8] = ya; hz[9] = a;
        const yb = this.hpVn * (hz[10] + c3 - hz[11]); hz[10] = yb; hz[11] = c3;
        const s1 = this.shVnLo, s2 = this.shVnHi;
        const za = s1[0] * ya + hz[12]; hz[12] = s1[1] * ya - s1[2] * za;
        const zb = s2[0] * yb + hz[13]; hz[13] = s2[1] * yb - s2[2] * zb;
        vn = (za + zb) * .5; }
      // formant circuit (TR4 buffer, TR5 formant): all ranges of both footages
      let tp = 0, hn = 0;
      if (on[2]){ let s = 0; for (let g = 0; g < 10; g++) s += bz[20 + g]; tp = this.bq(this.fTr, this.fz, 0, s * .5) * 2.2 * .19; }
      if (on[3]){ let s = 0; for (let g = 0; g < 10; g++) s += bz[30 + g]; hn = this.bq(this.fHo, this.fz, 2, s * .5) * 2.4 * .19; }
      // crescendo: the shared VCA ramps up from silence, and re-arms once everything has died away
      if (any) this.cresc += (1 - this.cresc) * this.crK; else this.cresc *= .999;
      const cr = this.cresc * .45 * 2.2, chs = this.chs;
      chs[0] = vn * cr; chs[1] = va * cr; chs[2] = tp * cr; chs[3] = hn * cr;
      // bass circuit: lowest held key below C3, cello 8' / contrabass 16', own sustain, TR1 low-pass, VOLUME BASS
      if (low < 999) this.bassNote = low;
      let bass = 0, bc = 0, bb = 0;
      for (let c = 0; c < 2; c++) this.bassG[c] = low < 999 ? this.bassG[c] + (1 - this.bassG[c]) * this.atkK[4 + c] : this.bassG[c] * this.relK[4 + c];
      if (this.bassNote >= 0 && (this.bassG[0] > 1e-5 || this.bassG[1] > 1e-5)){
        if (on[4]) bc = this.spike(this.bassNote + pit[4], 5.8e-9 * 50000) * this.bassG[0];
        if (on[5]) bb = this.spike(this.bassNote + pit[5] - 12, 10e-9 * 50000) * this.bassG[1];
        alive = true;
      }
      bc = this.bq(this.bassF, this.bassZ, 0, bc) * this.bassVolG * .45; bb = this.bq(this.bassF, this.bassZ, 2, bb) * this.bassVolG * .45;
      chs[4] = bc; chs[5] = bb;
      // RX5-style mixer: tone, fader, pan, ensemble or dry bus, echo and reverb sends
      let eL = 0, eR = 0, dL = 0, dR = 0, sE = 0, sR = 0;
      for (let c = 0; c < 6; c++){
        const j = c * 3; sm[j] += (gain[c] - sm[j]) * smk; sm[j + 1] += (this.panL[c] - sm[j + 1]) * smk; sm[j + 2] += (this.panR[c] - sm[j + 2]) * smk;
        let x = chs[c];
        if (this.tk[c] < 1){ this.tz[c] += (x - this.tz[c]) * this.tk[c]; x = this.tz[c]; }
        x *= sm[j];
        if (ensOn[c]){ eL += x * sm[j + 1]; eR += x * sm[j + 2]; } else { dL += x * sm[j + 1]; dR += x * sm[j + 2]; }
        sE += x * ech[c]; sR += x * rev[c];
      }
      if (any || this.keys.size) alive = true;
      // TR2 low-pass on both buses
      const pz = this.preZ, q = this.lpPre;
      eL = this.bq(q, pz, 0, eL); eR = this.bq(q, pz, 2, eR); dL = this.bq(q, pz, 4, dL); dR = this.bq(q, pz, 6, dR);
      // control circuit: square relaxation oscillators -> RC filters -> phase shift -> inverter (C1, C2, C3)
      this.phC += p.chorusHz / fs; if (this.phC >= .5){ this.phC -= .5; this.sC = -this.sC; }
      this.phT += p.tremHz / fs; if (this.phT >= .5){ this.phT -= .5; this.sT = -this.sT; }
      lf[0] += (this.sC * .453 - lf[0]) * kc[0]; lf[1] += (lf[0] - lf[1]) * kc[1]; lf[2] += (lf[1] - lf[2]) * kc[2];
      lf[3] += (this.sT * .548 - lf[3]) * kc[3]; lf[4] += (lf[3] - lf[4]) * kc[4]; lf[5] += (lf[4] - lf[5]) * kc[5];
      const chv = lf[2] * 3.4, trv = lf[5] * 8.8 * tremA;
      const c1 = chv + trv; lf[6] += (c1 - lf[6]) * kc[6]; const c2 = -1.83 * lf[6], c3 = -(c1 + c2);
      // modulator circuits: clock follows the control voltage, delay = 185 stages / (2 f_clk)
      let oL, oR;
      const wi = this.w & 16383; this.bufL[wi] = eL; this.bufR[wi] = eR;
      if (p.ens){
        const cv = this.cv, yl = this.yl, yr = this.yr, pz2 = this.postZ; cv[0] = c1; cv[1] = c2; cv[2] = c3;
        for (let j = 0; j < 3; j++){
          const fclk = 33000 * (1 + depth * Math.max(-.9, Math.min(.9, cv[j]))), d = 185 / (2 * fclk) * fs;
          const r = this.w - d, i0 = Math.floor(r), f = r - i0;
          const a1 = this.bufL[i0 & 16383], b1 = this.bufL[(i0 + 1) & 16383], a2 = this.bufR[i0 & 16383], b2 = this.bufR[(i0 + 1) & 16383];
          yl[j] = this.bq(this.post2, pz2, j * 8 + 2, this.bq(this.post1, pz2, j * 8, a1 + (b1 - a1) * f));
          yr[j] = this.bq(this.post2, pz2, j * 8 + 6, this.bq(this.post1, pz2, j * 8 + 4, a2 + (b2 - a2) * f));
        }
        if (p.wide){ oL = (yl[0] + yl[1] * .5) * .8; oR = (yr[2] + yr[1] * .5) * .8; }
        else { oL = (yl[0] + yl[1] + yl[2]) * .5; oR = (yr[0] + yr[1] + yr[2]) * .5; }
      } else { oL = eL; oR = eR; }
      this.w++;
      oL += dL; oR += dR;
      // output amplifier: volume and expression pedal; LOW OUTPUT through the correction filter
      oL *= this.expr; oR *= this.expr;
      if (p.outSel){ const cz = this.corrZ; oL = this.bq(this.corr2, cz, 2, this.bq(this.corr1, cz, 0, oL)) * 1.4; oR = this.bq(this.corr2, cz, 6, this.bq(this.corr1, cz, 4, oR)) * 1.4; }
      // phaser: four first-order all-pass stages swept by a triangle, colour = feedback
      if (p.phOn){
        this.phPh += phRate / fs; if (this.phPh >= 1) this.phPh -= 1;
        for (let s = 0; s < 2; s++){
          let t = (this.phPh + s * .25) % 1; t = t < .5 ? t * 2 : 2 - t * 2;
          const f = 180 * Math.pow(4500 / 180, t * p.phDepth + (1 - p.phDepth) * .3), tn = Math.tan(Math.PI * Math.min(f, fs * .45) / fs), a = (tn - 1) / (tn + 1);
          const xin = s ? oR : oL; let x = xin + this.phLast[s] * p.phFb;
          for (let st = 0; st < 4; st++){ const zi = s * 4 + st, y = a * x + this.phz[zi]; this.phz[zi] = x - a * y; x = y; }
          this.phLast[s] = x;
          const y = xin * (1 - p.phMix) + x * p.phMix;
          if (s) oR = y; else oL = y;
        }
      }
      // echo
      let eRL = 0, eRR = 0;
      if (p.dlOn){
        this.dlT += (dlTarget - this.dlT) * .0003;
        const N = this.dlN, r = this.dlW - this.dlT, i0 = Math.floor(r), f = r - i0;
        const xl = this.dlL[(i0 & (N - 1))], xl2 = this.dlL[((i0 + 1) & (N - 1))], xr = this.dlR[(i0 & (N - 1))], xr2 = this.dlR[((i0 + 1) & (N - 1))];
        let yl = xl + (xl2 - xl) * f, yr = xr + (xr2 - xr) * f;
        this.dlZ[0] += (yl - this.dlZ[0]) * this.dlK; this.dlZ[1] += (yr - this.dlZ[1]) * this.dlK; yl = this.dlZ[0]; yr = this.dlZ[1];
        const wi2 = this.dlW & (N - 1);
        if (p.dlPing){ this.dlL[wi2] = Math.tanh(sE + yr * p.dlFb); this.dlR[wi2] = Math.tanh(yl * p.dlFb); }
        else { this.dlL[wi2] = Math.tanh(sE + yl * p.dlFb); this.dlR[wi2] = Math.tanh(sE + yr * p.dlFb); }
        this.dlW++;
        eRL = yl * p.dlRet; eRR = yr * p.dlRet;
        if (Math.abs(yl) + Math.abs(yr) > 1e-6) alive = true;
      }
      // reverb: pre-delay, 8-line feedback delay network (Hadamard), damping in each line
      let rRL = 0, rRR = 0;
      if (p.rvOn){
        const PN = this.preN; this.preBuf[this.preW & (PN - 1)] = sR + (eRL + eRR) * .25;
        const rin = this.preBuf[(this.preW - preD) & (PN - 1)]; this.preW++;
        const o = this.rvO, h = this.rvH;
        for (let j = 0; j < 8; j++){ const b = rvBuf[j], ln = rvLen[j], ri = rvW[j] - ln; o[j] = b[ri < 0 ? ri + b.length : ri]; rvLp[j] += (o[j] - rvLp[j]) * rvDk; }
        // fast Walsh-Hadamard of the damped outputs
        for (let j = 0; j < 8; j++) h[j] = rvLp[j];
        for (let len = 1; len < 8; len <<= 1) for (let a = 0; a < 8; a += len << 1) for (let b = a; b < a + len; b++){ const u = h[b], v = h[b + len]; h[b] = u + v; h[b + len] = u - v; }
        for (let j = 0; j < 8; j++){ const b = rvBuf[j]; b[rvW[j]] = h[j] * .35355339059327373 * rvG[j] + rin * (j & 1 ? -.5 : .5); rvW[j] = rvW[j] + 1 >= b.length ? 0 : rvW[j] + 1; }
        rRL = (o[0] + o[2] + o[4] + o[6]) * .5 * p.rvRet; rRR = (o[1] + o[3] + o[5] + o[7]) * .5 * p.rvRet;
        if (Math.abs(rRL) + Math.abs(rRR) > 1e-6) alive = true;
      }
      oL += eRL + rRL; oR += eRR + rRR;
      // EQ, drive, master
      if (!this.eqFlat){ const z = this.eqZ; for (let b = 0; b < 3; b++){ oL = this.bq(this.eq[b], z, b * 4, oL); oR = this.bq(this.eq[b], z, b * 4 + 2, oR); } }
      if (p.drive > 0){ const k = this.driveK, m = (1 + p.drive * 2) / k; oL = Math.tanh(oL * k) * m; oR = Math.tanh(oR * k) * m; }
      L[o + i] = oL * this.outG; R[o + i] = oR * this.outG;
    }
    if (alive) this.idle = 0; else this.idle += n;
  }
}

// a random sound with the Solina's character: some tabs on, an ensemble, sensible mixer and effects
function randomPatch(rnd){
  rnd = rnd || Math.random;
  const r = (a, b) => a + (b - a) * rnd(), pick = a => a[Math.floor(rnd() * a.length)], o = defaults();
  const tabs = ['violin', 'viola', 'trumpet', 'horn', 'cello', 'contra'];
  for (const t of tabs) o[t] = rnd() < .45 ? 1 : 0;
  if (!o.violin && !o.viola && !o.trumpet && !o.horn) o[pick(['violin', 'viola', 'trumpet', 'horn'])] = 1;
  o.cresc = Math.pow(rnd(), 1.5); o.sus = rnd() < .8 ? 1 : 0; o.susLen = rnd(); o.bassVol = r(.3, 1); o.ens = rnd() < .9 ? 1 : 0;
  o.chorusHz = r(.3, 1.6); o.tremHz = r(4.5, 8); o.depth = r(.15, .8); o.trem = r(0, .6); o.wide = rnd() < .5 ? 1 : 0;
  o.outSel = rnd() < .15 ? 1 : 0;
  for (let c = 0; c < 6; c++){ const k = CH[c];
    o[k + 'Lvl'] = r(.55, .95); o[k + 'Pan'] = rnd() < .5 ? 0 : r(-.7, .7); o[k + 'Pit'] = rnd() < .85 ? 0 : pick([-12, 12, 7, -5]);
    o[k + 'Atk'] = rnd() < .5 ? 0 : Math.pow(rnd(), 2) * .8; o[k + 'Dec'] = r(.3, .8); o[k + 'Ton'] = rnd() < .5 ? 1 : r(.5, 1);
    o[k + 'Ens'] = rnd() < .85 ? 1 : 0; o[k + 'Ech'] = rnd() < .5 ? 0 : r(0, .6); o[k + 'Rev'] = r(0, .7); }
  o.phOn = rnd() < .35 ? 1 : 0; o.phRate = Math.exp(r(Math.log(.05), Math.log(2))); o.phDepth = r(.4, 1); o.phFb = r(0, .8); o.phMix = r(.35, .55);
  o.dlOn = rnd() < .5 ? 1 : 0; o.dlSync = rnd() < .6 ? pick([1, 2, 3, 5]) : 0; o.dlTime = r(120, 700); o.dlFb = r(.15, .7); o.dlTone = r(.3, .9); o.dlPing = rnd() < .5 ? 1 : 0; o.dlRet = r(.3, .8);
  o.rvOn = rnd() < .85 ? 1 : 0; o.rvSize = rnd(); o.rvDecay = r(.8, 7); o.rvDamp = r(.2, .8); o.rvPre = r(0, 60); o.rvRet = r(.3, .8);
  o.eqLow = Math.round(r(-4, 5)); o.eqMid = Math.round(r(-4, 3)); o.eqHigh = Math.round(r(-5, 4)); o.drive = rnd() < .2 ? r(0, .5) : 0;
  return o;
}

const api = {SolinaEngine, PARAMS, PID, CH, CH_NAMES, CH_TABS, defaults, randomPatch, groupOf};
G.SolinaDSP = api;
if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
