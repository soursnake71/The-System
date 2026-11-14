/* Omega Linn: a circuit model of the LinnDrum (1982), plus per-channel EQ / ADSR / drive, a REV7-style reverb, a
   TC 2290-style delay, a master EQ and an overdrive (the Omega 110's extended section).
   This file is the whole sound engine: the app runs it in an AudioWorklet and the plugin runs a line-for-line C++ port
   (plugins/linn/Linn.h). linn/tools/compare.mjs checks they render the same.

   The machine's sound path, as modelled (component values from the LinnDrum service manual, as used by MAME's
   linndrum driver):
   - The sounds are the owner's LinnDrum recordings (linn/samples/set1 and set2: SOUND SET 1 and 2, each filling the
     other's gaps), i.e. the voice outputs of a real machine. Each ROM "group" holds one sound, or several recordings of
     it at different sample clocks or dynamics (layers); a hit plays the layer recorded nearest the clock asked for,
     resampled to that clock. A group with no recording is EMPTY (silent).
   - Ten voice cores, as on the hardware. MUX DRUMS: bass, hi-hat, ride, crash, cabasa, tambourine, cowbell, claps, all
     clocked by one 74LS627 (the internal MUX TUNING trimmer: 250 kHz-2.33 MHz over 32 = about 24.8 kHz a voice at 25%).
     SNARE / SIDESTICK: one core (a new snare cuts the sidestick), clocked by the SNARE tuning knob (8-80 kHz).
     TOM / CONGA: one core for all five pitches (hi / mid / lo tom, hi / lo conga: a new one cuts the last), its clock
     from the knob of the pitch played (8-80 kHz). POLY (an Omega option) gives every channel its own core.
   - Dynamics, as the circuit makes them: the "1" variation of bass, hi-hat, ride, cabasa and tambourine goes through
     the 10k / 3.3k divider (x0.255); snare 1 / 2 / 3 set the snare DAC's reference current (R69-R72 with D2, D3). A sound
     recorded at that dynamic (soft / mid / hard) plays that recording instead, at its own level.
   - The hi-hat: one sample (the open hat) through a CEM3360 VCA. A hit charges the EG to 5 V and the 556 holds it there
     5.6 ms, then it discharges through 1M (open) or 1M in parallel with 10k + HIHAT DECAY (closed); the VCA gain is the
     EG x 0.549 over 1.93 V, clamped. That is how the closed hat is made: a slice of the open hat, shaped by the VCA.
     HAT MODE: LINN (the ROM counter isn't reset while the hat is still counting, so quick closed hats each play a later
     slice of the cymbal: the LinnDrum hi-hat), RESTART (every hit from the top), LOOP (the LM-1 trick: the hat ROM
     runs round all the time and a hit only opens the VCA).
   - The mixer: 15 channels, each with a VOLUME fader (audio taper) and PAN, through the MXR board's resistor network
     (the per-channel series resistors set the voices' balance), plus the CLICK channel (the metronome, a 1.1 ms
     pulse through its RC filter); MASTER VOLUME; the output op-amps' 15.9 kHz low-pass.
   Omega additions (all off or neutral by default): per-channel tune, decay, cut-off / resonance, reverse, start,
   output, sends, EQ, ADSR, drive, the effects and the master section. */
'use strict';
(function (root) {
const NV = 15;                        // mixer channels with a sound
const CHS = ['BS', 'SN', 'SS', 'HH', 'HT', 'MT', 'LT', 'RD', 'CR', 'CB', 'TB', 'HC', 'LC', 'CW', 'CP'];
const CH_NAMES = ['Bass', 'Snare', 'Sidestick', 'Hihat', 'Hi Tom', 'Mid Tom', 'Lo Tom', 'Ride', 'Crash', 'Cabasa', 'Tamb', 'Hi Conga', 'Lo Conga', 'Cowbell', 'Claps'];
const CH_KEYS = ['BASS', 'SNARE', 'SIDESTICK', 'HIHAT', 'HI TOM', 'MID TOM', 'LO TOM', 'RIDE', 'CRASH', 'CABASA', 'TAMB', 'HI CONGA', 'LO CONGA', 'COWBELL', 'CLAPS'];
// the hardware voice core of each channel (one sound at a time per core)
const CORE = [0, 1, 1, 2, 3, 3, 3, 4, 5, 6, 7, 3, 3, 8, 9];
// ROM groups: the sounds in the sound ROMs
const GROUPS = ['BASS', 'BASS ALT', 'SNARE', 'SIDESTICK', 'HIHAT', 'TOMS', 'CONGAS', 'RIDE', 'CRASH', 'CABASA', 'TAMB', 'COWBELL', 'CLAPS'];
const NG_ROM = GROUPS.length;
const G = {BASS:0, BASS2:1, SNARE:2, SIDESTICK:3, HAT:4, TOM:5, CONGA:6, RIDE:7, CRASH:8, CABASA:9, TAMB:10, COWBELL:11, CLAPS:12};
const ROM_SIZE = [4096, 4096, 4096, 4096, 16384, 8192, 8192, 32768, 32768, 4096, 4096, 4096, 4096];
// the group each channel plays (the bass: BASS or BASS ALT by the BASS CHIP switch)
const CH_GROUP = [0, 2, 3, 4, 5, 5, 5, 7, 8, 9, 10, 6, 6, 11, 12];
// the 23 sounds the keys play: [name, channel, dynamics: 0 full, 1 the x0.745 divider, 2-4 snare reference currents, hat: 5 closed / 6 open]
const SOUNDS = [
  ['BASS 1', 0, 1], ['BASS 2', 0, 0], ['SNARE 1', 1, 2], ['SNARE 2', 1, 3], ['SNARE 3', 1, 4], ['SIDESTICK', 2, 4],
  ['HIHAT 1', 3, 1], ['HIHAT 2', 3, 0], ['HIHAT OPEN', 3, 0], ['HI TOM', 4, 0], ['MID TOM', 5, 0], ['LO TOM', 6, 0],
  ['RIDE 1', 7, 1], ['RIDE 2', 7, 0], ['CRASH', 8, 0], ['CABASA 1', 9, 1], ['CABASA 2', 9, 0], ['TAMB 1', 10, 1], ['TAMB 2', 10, 0],
  ['HI CONGA', 11, 0], ['LO CONGA', 12, 0], ['COWBELL', 13, 0], ['CLAPS', 14, 0]];
const NS = SOUNDS.length;
const S_OPEN = 8;
const HAT_MODES = ['LINN', 'RESTART', 'LM-1 LOOP'];
const RV_PROGS = ['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF'];

/* ---- the circuit's numbers (see the header) */
const VPLUS = 15, VCC = 5;
const par = (a, b) => a * b / (a + b);
const div = (r1, r2) => r2 / (r1 + r2);            // MAME's RES_VOLTAGE_DIVIDER(r1, r2)
function potLaw(x){ const B = 81, A = 1 / 80; return A * Math.pow(B, x) - A; }     // an ideal audio-taper pot (10% midpoint)
const ls627 = (lo, hi, cv) => lo + (hi - lo) / VCC * Math.max(0, Math.min(VCC, cv));
// the MUX DRUMS clock per voice (the 74LS627 over 8 voices x 4 ticks), from the internal trimmer (0-100)
function muxClock(t){
  const bot = t * 10e3 / 100, top = 10e3 - bot, p = par(Math.max(bot, 1e-6), 100e3);
  return ls627(250e3, 2330e3, VPLUS * div(18e3 + top, p)) / 32;
}
// the snare and tom / conga clocks share the CV stage (U95B and the 10k into the timer's CV input)
const stCv = v => Math.max(0, Math.min(VCC, v * (1 + 30e3 / 47e3) * div(10e3, 100e3)));
function snareClock(k){
  const bot = k * 100e3 / 100, top = 100e3 - bot, p = bot > 0 ? par(bot, 97e3) : 0;
  const v = VPLUS * p / (top + p);
  return ls627(8e3, 80e3, stCv(v * div(75e3, 22e3)));
}
function tomClock(k){ const bot = k * 100e3 / 100, top = 100e3 - bot; return ls627(8e3, 80e3, stCv(VPLUS * bot / (390e3 + top + bot))); }
const MUX_DEFAULT = muxClock(25);                    // the trimmer's factory setting
const MUX_ATT = div(10e3, 3.3e3 + 125);             // R60 / R62 with the CD4053's on-resistance
// snare / sidestick DAC reference current for D2, D3 (R70, R69, R72, R71), relative to both high
function snareIref(d2, d3){
  const R0 = 3.3e3, R1 = 380e3, R2 = 22e3, R3 = 5.6e3, a = R0 * R2 * R3, b = R0 * R1 * R3, c = R0 * R1 * R2, d = R1 * R2 * R3;
  return (a * VPLUS + b * d2 * VCC + c * d3 * VCC) / (R0 * (a + b + c + d));
}
const IREF_MAX = snareIref(1, 1);
const DYN = [1, MUX_ATT, snareIref(1, 0) / IREF_MAX, snareIref(0, 1) / IREF_MAX, 1];
// the mixer (MXR board): series resistor before each VOLUME fader
const R_PRE = [0, 0, 0, 5.1e3, 0, 0, 0, 10e3, 5.1e3, 10e3, 10e3, 0, 0, 5.1e3, 2.4e3, 0];
function mixGain(ch, vol, pan){
  if (vol <= 0) return [0, 0];
  const RV = 5e3, RP = 10e3, R1 = 5.6e3, R2 = 5.6e3, R3 = 15e3, R4 = 15e3, RF = 33e3;
  const rb = Math.max(1e-3, RV * potLaw(vol / 100)), rt = RV - rb, pl = pan * RP / 100, pr = RP - pl;
  const r0 = R_PRE[ch] + rt, rR = R1 + (pr > 0 ? par(pr, R3) : 0), rL = R2 + (pl > 0 ? par(pl, R4) : 0);
  const rw = 1 / (1 / rb + 1 / rL + 1 / rR), i0 = 1 / (r0 + rw), vp = 1 - i0 * r0;
  return [(vp - vp / rL * R2) * RF / R4, (vp - vp / rR * R1) * RF / R3];
}
const MIX_NORM = 1 / mixGain(0, 100, 50)[0];
function masterGain(pot){ const rb = Math.max(1e-3, 10e3 * potLaw(pot / 100)), rt = 10e3 - rb; return div(rt, par(rb, 10e3)); }
const MASTER_NORM = 1 / masterGain(90);
// the hi-hat EG and VCA
const HAT_HOLD = 1.1 * 510e3 * 0.01e-6;            // the 556 (R8, C4)
function hatTau(open, decay){ const r = open ? 1e6 : par(1e6, 10e3 + 125 + 100e3 * decay / 100); return r * 1e-6; }
const hatVca = eg => Math.max(0, Math.min(1.93, eg * div(8.2e3, 10e3))) / 1.93;
// the tempo knob: the 556 astable (R1 5.1k, R2 18k + TEMPO 100k, C2 0.1u), scaled so its range is 40-250 BPM
const tempoHz = k => 1 / (0.693 * (5.1e3 + 2 * (18e3 + (100 - k) * 1e3)) * 0.1e-6);
const BPM_K = 250 / tempoHz(100);
const tempoOfKnob = k => tempoHz(k) * BPM_K;
function knobOfTempo(b){ let lo = 0, hi = 100; for (let i = 0; i < 40; i++){ const m = (lo + hi) / 2; if (tempoOfKnob(m) < b) lo = m; else hi = m; } return (lo + hi) / 2; }

// parameters: [id, name, min, max, default]
const PARAMS = [
  ['tempo', 'Tempo', 40, 250, 120],
  ['volume', 'Master Volume', 0, 100, 90],
  ['mux', 'Mux Tuning (trimmer)', 0, 100, 25],
  ['hatDecay', 'Hihat Decay', 0, 100, 50],
  ['tSN', 'Snare Tuning', 0, 100, 25], ['tHT', 'Hi Toms Tuning', 0, 100, 60], ['tMT', 'Mid Toms Tuning', 0, 100, 50],
  ['tLT', 'Lo Toms Tuning', 0, 100, 40], ['tHC', 'Hi Congas Tuning', 0, 100, 52], ['tLC', 'Lo Congas Tuning', 0, 100, 40],
  ['hatMode', 'Hat Mode', 0, 2, 0],
  ['bassChip', 'Bass Chip', 0, 1, 0],
  ['kit', 'Sound Set', 0, 1, 0],
  ['poly', 'Poly Voices', 0, 1, 0],
  ['clickLv', 'Click Volume', 0, 100, 70], ['clickPan', 'Click Pan', 0, 100, 50],
  ['rvProg', 'Reverb Program', 0, 4, 1],
  ['rvTime', 'Reverb Time', 0.3, 10, 1.6],
  ['rvHigh', 'Reverb High', 0.1, 1, 0.6],
  ['rvDelay', 'Reverb Delay', 0, 200, 12],
  ['rvDiff', 'Reverb Diffusion', 0, 10, 8],
  ['rvEr', 'Reverb ER Level', 0, 1, 0.35],
  ['rvCut', 'Reverb High Cut', 1000, 12000, 9000],
  ['rvLevel', 'Reverb Return', 0, 1, 0.5],
];
const NG = PARAMS.length;
const NF = 11;
const SEND = [0.03, 0.18, 0.1, 0.03, 0.12, 0.12, 0.12, 0.03, 0.05, 0.03, 0.05, 0.08, 0.08, 0.05, 0.2];
const OUT_DEFAULT = [0, 1, 2, 3, 4, 4, 4, 5, 5, 7, 7, 6, 6, 7, 7];    // outputs 1-8: bass, snare, sidestick, hihat, toms, cymbals, congas, percussion
for (let v = 0; v < NV; v++){
  const n = CH_NAMES[v] + ' ', id = CHS[v];
  PARAMS.push([id + 'lv', n + 'Volume', 0, 100, 100], [id + 'pan', n + 'Pan', 0, 100, 50], [id + 'tune', n + 'Tune', -12, 12, 0],
    [id + 'dec', n + 'Decay', 0.02, 3, 3], [id + 'mute', n + 'Mute', 0, 1, 0], [id + 'snd', n + 'Reverb Send', 0, 1, SEND[v]],
    [id + 'rev', n + 'Reverse', 0, 1, 0], [id + 'cut', n + 'Cutoff', 200, 20000, 20000], [id + 'res', n + 'Resonance', 0, 1, 0],
    [id + 'start', n + 'Start', 0, 0.9, 0], [id + 'out', n + 'Output', 0, 8, 0]);
}
const vp = (v, f) => NG + v * NF + f;            // f: 0 lv 1 pan 2 tune 3 dec 4 mute 5 snd 6 rev 7 cut 8 res 9 start 10 out (0 = its own, 1-8)
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
const NG2 = PARAMS.length;
for (let v = 0; v < NV; v++){
  const n = CH_NAMES[v] + ' ', id = CHS[v];
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
const TUNE_PARAM = [-1, 4, 4, -1, 5, 6, 7, -1, -1, -1, -1, 8, 9, -1, -1];   // the knob (param index) each channel's clock follows; -1: the mux clock
const P = {}; PARAMS.forEach((q, i) => P[q[0]] = i);
function defaults(){ return PARAMS.map(q => q[4]); }

const TAU = 2 * Math.PI;
class SVF {
  constructor(){ this.ic1 = 0; this.ic2 = 0; this.a1 = 0; this.a2 = 0; this.a3 = 0; this.k = 1; this.bp = 0; this.lp = 0; this.hp = 0; }
  set(f, q, sr){ const g = Math.tan(Math.PI * Math.min(f, sr * 0.45) / sr); this.k = 1 / q; this.a1 = 1 / (1 + g * (g + this.k)); this.a2 = g * this.a1; this.a3 = g * this.a2; }
  tick(x){
    const v3 = x - this.ic2, v1 = this.a1 * this.ic1 + this.a2 * v3, v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2; this.bp = v1; this.lp = v2; this.hp = x - this.k * v1 - v2; return v1;
  }
}
const onePole = (f, sr) => 1 - Math.exp(-TAU * f / sr);
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

/* ---- the sound ROM: "OLIN", version 2, layer count (u16), then per layer [group u8, bits u8, set u8 (0 any, 1, 2),
   dynamic u8 (0 none, 1 soft, 2 mid, 3 hard), rate u32, clock u32, length u32], then the samples (16-bit little-endian).
   A layer's clock is the sample clock the hardware ran at when it was recorded. */
function parseRom(bytes){
  const n = bytes[5] | bytes[6] << 8, out = []; for (let g = 0; g < NG_ROM; g++) out.push([]);
  let off = 8 + n * 16;
  const u32 = o => (bytes[o] | bytes[o + 1] << 8 | bytes[o + 2] << 16 | bytes[o + 3] << 24) >>> 0;
  for (let i = 0; i < n; i++){
    const b = 8 + i * 16, g = bytes[b], kit = bytes[b + 2], dyn = bytes[b + 3] - 1, rate = u32(b + 4), clk = u32(b + 8), len = u32(b + 12);
    const d = new Float64Array(len);
    for (let j = 0; j < len; j++){ let x = bytes[off + j * 2] | bytes[off + j * 2 + 1] << 8; if (x >= 32768) x -= 65536; d[j] = x / 32768; }
    off += len * 2;
    if (g < NG_ROM) out[g].push({rate, clk, kit, dyn, data:d});
  }
  return out;
}
// the dynamic a sound asks for (0 soft, 1 mid, 2 hard) by its DYN entry
const WANT = [2, 0, 0, 1, 2];

class Engine {
  constructor(sr){
    this.sr = sr; this.p = defaults(); this.songGain = 1;
    this.rom = []; for (let g = 0; g < NG_ROM; g++) this.rom.push([]);
    // one player per channel (CORE[] decides which ones cut each other)
    this.act = new Array(NV).fill(false); this.pos = new Float64Array(NV); this.inc = new Float64Array(NV); this.amp = new Float64Array(NV);
    this.lay = new Int32Array(NV).fill(-1); this.dk = new Float64Array(NV); this.dec = new Float64Array(NV); this.cutOn = new Array(NV).fill(false);
    this.vflt = []; for (let v = 0; v < NV; v++) this.vflt.push(new SVF());
    this.eq = []; for (let v = 0; v < NV; v++) this.eq.push([new Biquad(), new Biquad(), new Biquad()]);
    this.env = new Float64Array(NV); this.stage = new Int32Array(NV).fill(4); this.envOn = new Array(NV).fill(false);
    this.envA = new Float64Array(NV); this.envD = new Float64Array(NV); this.envS = new Float64Array(NV); this.envR = new Float64Array(NV);
    this.drv = new Float64Array(NV); this.dls = new Float64Array(NV);
    this.gL = new Float64Array(NV); this.gR = new Float64Array(NV); this.send = new Float64Array(NV); this.outCh = new Int32Array(NV);
    this.tail = new Float64Array(NV);
    // the hi-hat EG
    this.eg = 0; this.egHold = 0; this.egOpen = false; this.egK = 0; this.pickGain = 1;
    this.meq = [[new Biquad(), new Biquad(), new Biquad(), new Biquad()], [new Biquad(), new Biquad(), new Biquad(), new Biquad()]];
    this.dl = new TCDelay(sr); this.rv = new Rev7(sr);
    this.clk = 0; this.clkHp = 0; this.clkX = 0; this.beepN = 0; this.beepPh = 0;
    this.lpL = 0; this.lpR = 0; this.dcL = 0; this.dcR = 0; this.meter = new Float64Array(NV);
    this.update();
  }
  set(i, v){ this.p[i] = v; this.update(); }
  setAll(arr){ for (let i = 0; i < PARAMS.length && i < arr.length; i++) this.p[i] = arr[i]; this.update(); }
  // a ROM group: layers [{rate, clk, data (Float64Array, -1..1)}]
  setGroup(g, layers){ if (g < 0 || g >= NG_ROM) return; this.rom[g] = layers || []; for (let v = 0; v < NV; v++) if (this.group(v) === g){ this.act[v] = false; this.lay[v] = -1; } }
  group(v){ return v === 0 && this.p[P.bassChip] >= 0.5 ? G.BASS2 : CH_GROUP[v]; }
  // the sample clock the hardware runs channel v at
  clock(v){ const k = TUNE_PARAM[v]; if (k < 0) return muxClock(this.p[P.mux]); return v <= 2 ? snareClock(this.p[k]) : tomClock(this.p[k]); }
  update(){
    const sr = this.sr, p = this.p;
    for (let v = 0; v < NV; v++){
      const g = p[vp(v, 4)] >= 0.5 ? 0 : 1, m = mixGain(v, p[vp(v, 0)], 100 - p[vp(v, 1)]);   // PAN 0 = left
      this.gL[v] = g * m[0] * MIX_NORM; this.gR[v] = g * m[1] * MIX_NORM;
      this.send[v] = g * p[vp(v, 5)];
      const o = Math.round(p[vp(v, 10)]); this.outCh[v] = o >= 1 && o <= 8 ? o - 1 : OUT_DEFAULT[v];
      this.dk[v] = p[vp(v, 3)] >= 2.999 ? 1 : decayCoef(p[vp(v, 3)] / 3, sr);
      const cut = p[vp(v, 7)]; this.cutOn[v] = cut < 19999;
      this.vflt[v].set(cut, 0.707 + p[vp(v, 8)] * 9.3, sr);
    }
    const ck = mixGain(15, p[P.clickLv], 100 - p[P.clickPan]); this.cL = ck[0] * MIX_NORM; this.cR = ck[1] * MIX_NORM;
    this.clkA = onePole(1150, sr); this.clkN = Math.round(1.1e-3 * sr);
    this.poly = p[P.poly] >= 0.5; this.hatMode = Math.round(p[P.hatMode]);
    this.egK = decayCoef(hatTau(this.egOpen, p[P.hatDecay]), sr); this.egHoldN = Math.round(HAT_HOLD * sr);
    this.master = masterGain(p[P.volume]) * MASTER_NORM * 0.5;
    this.lpA = onePole(1 / (TAU * 10e3 * 1000e-12), sr);       // the output op-amps' RC: 10k, 1000p
    this.dcA = onePole(8, sr);
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
    const dv = DL_DIVS[Math.max(0, Math.min(13, Math.round(p[P.dlSync])))];
    this.dl.setParams(dv > 0 ? 60 / p[P.tempo] * dv * 1000 : p[P.dlTime], p[P.dlFb], p[P.dlHiCut], p[P.dlLoCut], p[P.dlModRate], p[P.dlModDepth], p[P.dlDuck], p[P.dlPing] >= 0.5);
    this.dlLevel = p[P.dlLevel];
  }
  /* the layer to play: the SOUND SET's (the other set's if it has none), the recording of the dynamic asked for if there
     is one (it carries its own level: gain 1; else the loudest, through the circuit's dynamics: pickGain), recorded
     nearest clock f */
  pick(g, f, dyn){
    const L = this.rom[g], kit = this.p[P.kit] >= 0.5 ? 2 : 1, want = WANT[dyn];
    let any = false, hasDyn = false, exact = false, top = -1;
    for (let i = 0; i < L.length; i++) if (L[i].kit === 0 || L[i].kit === kit) any = true;
    const inSet = i => !any || L[i].kit === 0 || L[i].kit === kit;
    for (let i = 0; i < L.length; i++) if (inSet(i) && L[i].dyn >= 0){ hasDyn = true; if (L[i].dyn === want) exact = true; if (L[i].dyn > top) top = L[i].dyn; }
    const use = i => inSet(i) && (!hasDyn || L[i].dyn === (exact ? want : top));
    this.pickGain = hasDyn && exact ? 1 : DYN[dyn];
    let best = -1, bd = 1e9;
    for (let i = 0; i < L.length; i++){ if (!use(i)) continue; const d = Math.abs(Math.log(f / L[i].clk)); if (d < bd){ bd = d; best = i; } }
    return best;
  }
  /* a hit of sound s (0..22) */
  trigger(s){
    if (s < 0 || s >= NS) return;
    const v = SOUNDS[s][1], dyn = SOUNDS[s][2], p = this.p, sr = this.sr, g = this.group(v);
    if (!this.poly) for (let k = 0; k < NV; k++) if (k !== v && CORE[k] === CORE[v]) this.act[k] = false;
    if (v === 3){                       // the hat: the EG charges, open or closed decides its discharge
      this.egOpen = s === S_OPEN; this.eg = VCC; this.egHold = this.egHoldN;
      this.egK = decayCoef(hatTau(this.egOpen, p[P.hatDecay]), sr);
    }
    const f = this.clock(v), li = this.pick(g, f, dyn);
    if (li < 0){ this.act[v] = false; return; }       // an EMPTY sound: silent
    const L = this.rom[g][li];
    this.inc[v] = L.rate / sr * (f / L.clk) * Math.pow(2, p[vp(v, 2)] / 12);
    const start = Math.floor(p[vp(v, 9)] * L.data.length);
    const keep = v === 3 && this.lay[v] === li && ((this.hatMode === 0 && this.act[v]) || this.hatMode === 2);
    if (!keep) this.pos[v] = start;
    this.lay[v] = li;
    this.amp[v] = this.pickGain;
    this.dec[v] = 1; this.act[v] = true;
    this.stage[v] = 0; if (this.envA[v] < 1) this.env[v] = 0; else { this.env[v] = 1; this.stage[v] = 1; }
  }
  release(v){ if (v >= 0 && v < NV) this.stage[v] = 3; }
  // the metronome (the 556 click; first: the pattern's first beat, two clicks' worth)
  click(first){ this.clk = this.clkN; this.clkX = first ? 2 : 1; }
  // the firmware's beep (empty pattern, erase, copy)
  beep(){ this.beepN = Math.round(0.07 * this.sr); }
  overdrive(x){
    const g = 1 + this.od * 20; let y;
    if (this.odMode === 1) y = Math.max(-1, Math.min(1, x * g)) * 0.8;
    else if (this.odMode === 2) y = (Math.tanh(x * g + 0.3) - Math.tanh(0.3)) * 0.85;
    else y = Math.tanh(x * g) * 0.85;
    return (x * (1 - this.odMix) + y * this.odMix) * this.odOut;
  }
  read(v){
    const L = this.rom[this.group(v)][this.lay[v]];
    if (!L){ this.act[v] = false; return 0; }
    const d = L.data, n = d.length;
    let ps = this.pos[v];
    if (v === 3 && this.hatMode === 2 && ps >= n) ps -= n * Math.floor(ps / n);     // LM-1 LOOP: the hat ROM runs round
    const i = Math.floor(ps);
    if (i >= n){ this.act[v] = false; return 0; }
    const rev = this.p[vp(v, 6)] >= 0.5;
    const a = d[rev ? n - 1 - i : i], b = i + 1 < n ? d[rev ? n - 2 - i : i + 1] : 0;
    this.pos[v] = ps + this.inc[v];
    return a + (b - a) * (ps - i);
  }
  /* render n samples into L and R (overwrites). outs (optional): 16 arrays, the eight outputs as L/R pairs, dry, after
     the mixer and MASTER VOLUME; the sounds stay in the mix as well. */
  process(L, R, off, n, outs){
    const sr = this.sr, grp = new Float64Array(16);
    for (let s = 0; s < n; s++){
      let l = 0, r = 0, snd = 0, dsnd = 0;
      if (outs) grp.fill(0);
      // the hat EG
      if (this.egHold > 0) this.egHold--; else this.eg *= this.egK;
      for (let v = 0; v < NV; v++){
        if (!this.act[v] && this.stage[v] === 4) continue;
        let y = 0;
        if (this.act[v]){
          y = this.read(v) * this.amp[v] * this.dec[v]; this.dec[v] *= this.dk[v];
          if (v === 3) y *= hatVca(this.eg);
          this.tail[v] = 1;
        } else if (this.tail[v] < 1e-9){ this.stage[v] = 4; continue; }
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
        if (this.drv[v] > 0){ const dd = this.drv[v]; y = Math.tanh(y * (1 + dd * 15)) / (1 + dd); }
        const eq = this.eq[v]; y = eq[2].tick(eq[1].tick(eq[0].tick(y)));
        if (!this.act[v]) this.tail[v] = this.tail[v] * 0.999 + Math.abs(y);
        const ay = Math.abs(y); if (ay > this.meter[v]) this.meter[v] = ay;
        l += y * this.gL[v]; r += y * this.gR[v]; snd += y * this.send[v]; dsnd += y * this.dls[v];
        if (outs){ const o = this.outCh[v]; grp[o * 2] += y * this.gL[v]; grp[o * 2 + 1] += y * this.gR[v]; }
      }
      const rv = this.rv.tick(snd);
      l += rv[0] * this.rvLevel; r += rv[1] * this.rvLevel;
      const dl = this.dl.tick(dsnd);
      l += dl[0] * this.dlLevel; r += dl[1] * this.dlLevel;
      // the click: a 1.1 ms pulse into its RC high-pass, on its mixer channel
      let c = 0;
      if (this.clk > 0){ this.clk--; c = this.clkX; }
      this.clkHp += this.clkA * (c - this.clkHp); const cy = (c - this.clkHp) * 0.6;
      l += cy * this.cL; r += cy * this.cR;
      if (this.beepN > 0){ this.beepN--; this.beepPh += 2000 / sr; if (this.beepPh >= 1) this.beepPh -= 1; const b = this.beepPh < 0.5 ? 0.08 : -0.08; l += b; r += b; }
      for (let k = 0; k < 4; k++){ l = this.meq[0][k].tick(l); r = this.meq[1][k].tick(r); }
      if (this.od > 0){ l = this.overdrive(l); r = this.overdrive(r); }
      const m = this.master * this.songGain;
      l *= m; r *= m;
      // the output stage: the op-amps' 15.9 kHz low-pass and the output coupling
      this.lpL += this.lpA * (l - this.lpL); this.lpR += this.lpA * (r - this.lpR);
      this.dcL += this.dcA * (this.lpL - this.dcL); this.dcR += this.dcA * (this.lpR - this.dcR);
      L[off + s] = Math.tanh(this.lpL - this.dcL); R[off + s] = Math.tanh(this.lpR - this.dcR);
      if (outs) for (let k = 0; k < 16; k++) outs[k][off + s] = Math.tanh(grp[k] * m);
    }
  }
}

/* ---- TC 2290-style dynamic delay (as in the Omega 110 / Omega Traks) */
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

/* ---- REV7-style digital reverb (as in the Omega 110 / Omega Traks) */
const RV_LINES = [[31.7, 37.1, 41.3, 43.9, 53.3, 59.9, 67.7, 73.1]];
const RV_SIZE = [1.0, 0.42, 0.7, 0.55, 0.3];
const RV_TAPS = [
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
      const b = Math.log(10) / 4 * Math.log10(g) * (1 - 1 / (high * high));
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
    this.pre[this.preW] = x;
    this.bw += this.bwA * (this.readPre(this.preD) - this.bw);
    let d = this.bw;
    for (let i = 0; i < 4; i++){
      const buf = this.ap[i], w = this.apw[i], z = buf[w], v = d + this.apG * z;
      buf[w] = v; d = z - this.apG * v; this.apw[i] = w + 1 >= this.apLen[i] ? 0 : w + 1;
    }
    let eL = 0, eR = 0;
    for (let k = 0; k < 8; k++){ eL += this.readPre(this.preD + this.tapL[k][0]) * this.tapL[k][1]; eR += this.readPre(this.preD + this.tapR[k][0]) * this.tapR[k][1]; }
    this.preW = this.preW + 1 >= this.pre.length ? 0 : this.preW + 1;
    const o = [0, 0, 0, 0, 0, 0, 0, 0];
    for (let i = 0; i < 8; i++){
      const y = this.lines[i][this.lw[i]];
      this.lz[i] = y * (1 - this.damp[i]) + this.lz[i] * this.damp[i];
      o[i] = this.lz[i] * this.g[i];
    }
    for (let h = 1; h < 8; h *= 2) for (let i = 0; i < 8; i += h * 2) for (let j = i; j < i + h; j++){ const a = o[j], b = o[j + h]; o[j] = a + b; o[j + h] = a - b; }
    const sc = 1 / Math.sqrt(8);
    let lL = 0, lR = 0;
    for (let i = 0; i < 8; i++){
      const y = this.lz[i];
      if (i & 1) lR += y * ((i & 2) ? -1 : 1); else lL += y * ((i & 2) ? -1 : 1);
      this.lines[i][this.lw[i]] = o[i] * sc + d * ((i & 4) ? -0.5 : 0.5);
      this.lw[i] = this.lw[i] + 1 >= this.len[i] ? 0 : this.lw[i] + 1;
    }
    const l = eL * this.er * 0.5 + lL * this.late * 0.5, r = eR * this.er * 0.5 + lR * this.late * 0.5;
    this.cutL += this.cutA * (l - this.cutL); this.cutR += this.cutA * (r - this.cutR);
    this.out[0] = this.cutL; this.out[1] = this.cutR; return this.out;
  }
}

const API = {Biquad, SVF, NG, NF, NG2, vp, vq, DL_DIVS, DL_DIV_NAMES, OD_MODES, CORE, CHS, CH_NAMES, CH_KEYS, GROUPS, G, NG_ROM, ROM_SIZE, CH_GROUP,
  SOUNDS, NS, S_OPEN, HAT_MODES, Engine, Rev7, PARAMS, P, NV, RV_PROGS, OUT_DEFAULT, TUNE_PARAM, parseRom, defaults,
  muxClock, snareClock, tomClock, hatTau, hatVca, HAT_HOLD, MUX_DEFAULT, mixGain, masterGain, tempoOfKnob, knobOfTempo, DYN};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OLINN = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
