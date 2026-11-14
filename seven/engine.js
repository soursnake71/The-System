/* Omega 7: a 6-operator FM synthesizer modeled on the first-generation (Mark I) 1983 hardware, sound engine.
 *
 * The signal path follows the original boards:
 *   - operators computed the way the OPS chip does it: a log-sine table plus an exponential table (attenuation in
 *     1/256-octave steps, about 0.0235 dB), 14-bit signed operator outputs, 32 algorithms, feedback averaged over two
 *     samples, all at the hardware's own sample rate (9.4265 MHz / 192 = 49.096 kHz),
 *   - envelopes, pitch envelope, LFO, keyboard level / rate scaling and velocity as the EGS chip and the CPU do them
 *     (curves and tables after Music Synthesizer for Android, Apache 2.0, which were measured on real units),
 *   - the 16 voices summed into one accumulator, then the Mark I converter: a 12-bit DAC whose range is switched in
 *     steps of 6 dB (a floating-point DAC, 12-bit mantissa), held between samples (sample and hold) and smoothed by
 *     the analog output filter. The volume slider is analog, after all of that.
 * The C++ plugin (plugins/seven/Seven.h) is a line-for-line port; seven/tools/compare.mjs checks they agree.
 * Works in the browser, in an AudioWorklet and in Node (module.exports). */
(function(root){
'use strict';

const DX_RATE = 9426500 / 192;     // 49096.35 Hz
const N = 64;                      // control block: envelopes, LFO and pitch are updated every 64 samples
const VOICES = 16;

/* ---------------- tables ---------------- */
// quarter-wave log-sine: -log2(sin) in 1/256 octave, 4096 entries (14-bit phase per cycle)
const LOGSIN = new Int32Array(4096);
for (let i = 0; i < 4096; i++) LOGSIN[i] = Math.round(-Math.log2(Math.sin((i + 0.5) / 4096 * Math.PI / 2)) * 256);
// exponential: 2^(-i/256) as a 13-bit mantissa (8192 = full scale)
const EXPT = new Int32Array(256);
for (let i = 0; i < 256; i++) EXPT[i] = Math.round(8192 * Math.pow(2, -i / 256));
// LFO sine, Q24 signed, 4096 steps
const LFOSIN = new Int32Array(4096);
for (let i = 0; i < 4096; i++) LFOSIN[i] = Math.round(Math.sin(2 * Math.PI * i / 4096) * 16777216);

const LEVEL_LUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46];
function scaleOutLevel(l){ return l >= 20 ? 28 + l : LEVEL_LUT[l]; }
const EXP_SCALE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250];
function scaleCurve(group, depth, curve){
  let s;
  if (curve === 0 || curve === 3) s = (group * depth * 329) >> 12;
  else s = (EXP_SCALE[Math.min(group, 32)] * depth * 329) >> 15;
  return curve < 2 ? -s : s;
}
function scaleLevel(note, bp, ld, rd, lc, rc){
  const off = note - bp - 17;
  if (off >= 0) return scaleCurve(((off + 1) / 3) | 0, rd, rc);
  return scaleCurve(((-(off - 1)) / 3) | 0, ld, lc);
}
function scaleRate(note, sens){ const x = Math.min(31, Math.max(0, ((note / 3) | 0) - 7)); return (sens * x) >> 3; }
const VELOCITY = [0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
  206, 209, 211, 214, 216, 218, 220, 222, 224, 226, 228, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254, 254];
function scaleVelocity(vel, sens){ const v = VELOCITY[Math.max(0, Math.min(127, vel)) >> 1] - 239; return ((sens * v + 7) >> 3) << 4; }
const PMS_TAB = [0, 10, 20, 33, 55, 92, 153, 255];
const AMS_TAB = [0, 66, 109, 255];
const PITCH_RATE = [1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31,
  33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135,
  141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255];
const PITCH_LEVEL = [-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21,
  -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
  23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127];
const Q24 = 16777216;
const ENV_SRM = Math.floor(44100 / DX_RATE * Q24);                 // envelope rates were measured at 44.1 kHz
const LFO_UNIT = Math.round(N * 25190424 / DX_RATE);
const PENV_UNIT = Math.round(N * Q24 / (21.3 * DX_RATE));
const NOTE_LOG0 = 50857777;                                          // log2(8.1758 Hz) in Q24 (MIDI note 0)
const COARSE_LOG = [-Q24]; for (let i = 1; i < 32; i++) COARSE_LOG.push(Math.round(Math.log2(i) * Q24));

/* ---------------- algorithms ---------------- */
// [carriers, modulations "src>dst", feedback op, feedback source]
const ALG_DEF = [
  [[1, 3], '2>1 4>3 5>4 6>5', 6, 6], [[1, 3], '2>1 4>3 5>4 6>5', 2, 2], [[1, 4], '2>1 3>2 5>4 6>5', 6, 6], [[1, 4], '2>1 3>2 5>4 6>5', 6, 4],
  [[1, 3, 5], '2>1 4>3 6>5', 6, 6], [[1, 3, 5], '2>1 4>3 6>5', 6, 5], [[1, 3], '2>1 4>3 5>3 6>5', 6, 6], [[1, 3], '2>1 4>3 5>3 6>5', 4, 4],
  [[1, 3], '2>1 4>3 5>3 6>5', 2, 2], [[1, 4], '2>1 3>2 5>4 6>4', 3, 3], [[1, 4], '2>1 3>2 5>4 6>4', 6, 6], [[1, 3], '2>1 4>3 5>3 6>3', 2, 2],
  [[1, 3], '2>1 4>3 5>3 6>3', 6, 6], [[1, 3], '2>1 4>3 5>4 6>4', 6, 6], [[1, 3], '2>1 4>3 5>4 6>4', 2, 2], [[1], '2>1 3>1 5>1 4>3 6>5', 6, 6],
  [[1], '2>1 3>1 5>1 4>3 6>5', 2, 2], [[1], '2>1 3>1 4>1 5>4 6>5', 3, 3], [[1, 4, 5], '2>1 3>2 6>4 6>5', 6, 6], [[1, 2, 4], '3>1 3>2 5>4 6>4', 3, 3],
  [[1, 2, 4, 5], '3>1 3>2 6>4 6>5', 3, 3], [[1, 3, 4, 5], '2>1 6>3 6>4 6>5', 6, 6], [[1, 2, 4, 5], '3>2 6>4 6>5', 6, 6], [[1, 2, 3, 4, 5], '6>3 6>4 6>5', 6, 6],
  [[1, 2, 3, 4, 5], '6>4 6>5', 6, 6], [[1, 2, 4], '3>2 5>4 6>4', 6, 6], [[1, 2, 4], '3>2 5>4 6>4', 3, 3], [[1, 3, 6], '2>1 4>3 5>4', 5, 5],
  [[1, 2, 3, 5], '4>3 6>5', 6, 6], [[1, 2, 3, 6], '4>3 5>4', 5, 5], [[1, 2, 3, 4, 5], '6>5', 6, 6], [[1, 2, 3, 4, 5, 6], '', 6, 6]
];
// per algorithm, per op (0 = OP1): bitmask of modulating ops, carrier flag; fb op / source as indexes
const ALGS = ALG_DEF.map(([car, mods, fb, fbs]) => {
  const mod = [0, 0, 0, 0, 0, 0];
  for (const e of mods.split(' ').filter(Boolean)){ const [s, d] = e.split('>').map(Number); mod[d - 1] |= 1 << (s - 1); }
  return {mod, car:[1, 2, 3, 4, 5, 6].map(o => car.includes(o) ? 1 : 0), fb:fb - 1, fbs:fbs - 1};
});

/* ---------------- voice data (VCED, 155 bytes + operator on/off) ---------------- */
const OP_PARAMS = [
  ['R1', 'EG RATE 1', 99], ['R2', 'EG RATE 2', 99], ['R3', 'EG RATE 3', 99], ['R4', 'EG RATE 4', 99],
  ['L1', 'EG LEVEL 1', 99], ['L2', 'EG LEVEL 2', 99], ['L3', 'EG LEVEL 3', 99], ['L4', 'EG LEVEL 4', 99],
  ['BP', 'BREAK POINT', 99], ['LD', 'L SCALE DEPTH', 99], ['RD', 'R SCALE DEPTH', 99], ['LC', 'L SCALE CURVE', 3], ['RC', 'R SCALE CURVE', 3],
  ['RS', 'RATE SCALING', 7], ['AMS', 'AMP MOD SENS', 3], ['KVS', 'KEY VELOCITY', 7], ['OL', 'OUTPUT LEVEL', 99], ['OM', 'OSC MODE', 1],
  ['FC', 'FREQ COARSE', 31], ['FF', 'FREQ FINE', 99], ['DET', 'DETUNE', 14]
];
const GLOBAL_PARAMS = [
  ['PR1', 'PITCH EG RATE 1', 99], ['PR2', 'PITCH EG RATE 2', 99], ['PR3', 'PITCH EG RATE 3', 99], ['PR4', 'PITCH EG RATE 4', 99],
  ['PL1', 'PITCH EG LEVEL 1', 99], ['PL2', 'PITCH EG LEVEL 2', 99], ['PL3', 'PITCH EG LEVEL 3', 99], ['PL4', 'PITCH EG LEVEL 4', 99],
  ['ALG', 'ALGORITHM', 31], ['FB', 'FEEDBACK', 7], ['OKS', 'OSC KEY SYNC', 1], ['LFS', 'LFO SPEED', 99], ['LFD', 'LFO DELAY', 99],
  ['LPMD', 'LFO PM DEPTH', 99], ['LAMD', 'LFO AM DEPTH', 99], ['LFKS', 'LFO KEY SYNC', 1], ['LFW', 'LFO WAVE', 5], ['LPMS', 'P MOD SENS', 7],
  ['TRNSP', 'TRANSPOSE', 48]
];
// every voice parameter, by its number in the edit buffer / parameter-change messages (0..155)
const VPARAMS = [];
for (let op = 6; op >= 1; op--) for (const [s, n, m] of OP_PARAMS) VPARAMS.push({id:VPARAMS.length, op, short:s, name:n, max:m});
for (const [s, n, m] of GLOBAL_PARAMS) VPARAMS.push({id:VPARAMS.length, op:0, short:s, name:n, max:m});
for (let i = 0; i < 10; i++) VPARAMS.push({id:VPARAMS.length, op:0, short:'N' + (i + 1), name:'NAME ' + (i + 1), max:127, min:32});
VPARAMS.push({id:155, op:0, short:'OPON', name:'OPERATOR ON/OFF', max:63});
const P = {PR:126, PL:130, ALG:134, FB:135, OKS:136, LFS:137, LFD:138, LPMD:139, LAMD:140, LFKS:141, LFW:142, LPMS:143, TRNSP:144, NAME:145, OPON:155};
function opBase(op){ return (6 - op) * 21; }   // op 1..6
const VOICE_SIZE = 156;

function initVoice(){
  const v = new Uint8Array(VOICE_SIZE);
  for (let op = 1; op <= 6; op++){
    const b = opBase(op);
    v.set([99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, op === 1 ? 99 : 0, 0, 1, 0, 7], b);
  }
  v.set([99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24], 126);
  setName(v, 'INIT VOICE');
  v[155] = 63;
  return v;
}
function setName(v, s){ for (let i = 0; i < 10; i++){ const c = i < s.length ? s.charCodeAt(i) : 32; v[145 + i] = c >= 32 && c < 128 ? c : 32; } }
function voiceName(v){ let s = ''; for (let i = 0; i < 10; i++){ const c = v[145 + i]; s += c >= 32 && c < 127 ? String.fromCharCode(c) : ' '; } return s; }
function clampVoice(v){
  for (let i = 0; i < 155; i++){ const p = VPARAMS[i], lo = p.min || 0; if (v[i] > p.max || v[i] < lo) v[i] = i >= 145 ? 32 : Math.min(p.max, Math.max(lo, v[i])); }
  v[155] &= 63;
  return v;
}

/* ---------------- system exclusive: bulk dumps (packed VMEM / single VCED) and parameter changes ---------------- */
function checksum(d, a, b){ let s = 0; for (let i = a; i < b; i++) s += d[i]; return (128 - (s & 127)) & 127; }
// one voice to the 128-byte packed (bank) form
function packVoice(v, out, o){
  for (let op = 6; op >= 1; op--){
    const b = opBase(op), p = (6 - op) * 17;
    for (let i = 0; i < 11; i++) out[o + p + i] = v[b + i];
    out[o + p + 11] = (v[b + 11] & 3) | ((v[b + 12] & 3) << 2);
    out[o + p + 12] = (v[b + 13] & 7) | ((v[b + 20] & 15) << 3);
    out[o + p + 13] = (v[b + 14] & 3) | ((v[b + 15] & 7) << 2);
    out[o + p + 14] = v[b + 16];
    out[o + p + 15] = (v[b + 17] & 1) | ((v[b + 18] & 31) << 1);
    out[o + p + 16] = v[b + 19];
  }
  for (let i = 0; i < 8; i++) out[o + 102 + i] = v[126 + i];
  out[o + 110] = v[134] & 31;
  out[o + 111] = (v[135] & 7) | ((v[136] & 1) << 3);
  for (let i = 0; i < 4; i++) out[o + 112 + i] = v[137 + i];
  out[o + 116] = (v[141] & 1) | ((v[142] & 7) << 1) | ((v[143] & 7) << 4);
  out[o + 117] = v[144];
  for (let i = 0; i < 10; i++) out[o + 118 + i] = v[145 + i] & 127;
}
function unpackVoice(d, o){
  const v = new Uint8Array(VOICE_SIZE);
  for (let op = 6; op >= 1; op--){
    const b = opBase(op), p = o + (6 - op) * 17;
    for (let i = 0; i < 11; i++) v[b + i] = d[p + i] & 127;
    v[b + 11] = d[p + 11] & 3; v[b + 12] = (d[p + 11] >> 2) & 3;
    v[b + 13] = d[p + 12] & 7; v[b + 20] = (d[p + 12] >> 3) & 15;
    v[b + 14] = d[p + 13] & 3; v[b + 15] = (d[p + 13] >> 2) & 7;
    v[b + 16] = d[p + 14] & 127;
    v[b + 17] = d[p + 15] & 1; v[b + 18] = (d[p + 15] >> 1) & 31;
    v[b + 19] = d[p + 16] & 127;
  }
  for (let i = 0; i < 8; i++) v[126 + i] = d[o + 102 + i] & 127;
  v[134] = d[o + 110] & 31;
  v[135] = d[o + 111] & 7; v[136] = (d[o + 111] >> 3) & 1;
  for (let i = 0; i < 4; i++) v[137 + i] = d[o + 112 + i] & 127;
  v[141] = d[o + 116] & 1; v[142] = (d[o + 116] >> 1) & 7; v[143] = (d[o + 116] >> 4) & 7;
  v[144] = d[o + 117] & 127;
  for (let i = 0; i < 10; i++) v[145 + i] = d[o + 118 + i] & 127;
  v[155] = 63;
  return clampVoice(v);
}
// 32-voice bank dump (4104 bytes)
function bankToSysex(bank, ch){
  const d = new Uint8Array(4104);
  d.set([0xF0, 0x43, (ch || 0) & 15, 0x09, 0x20, 0x00]);
  for (let i = 0; i < 32; i++) packVoice(bank[i], d, 6 + i * 128);
  d[4102] = checksum(d, 6, 4102); d[4103] = 0xF7;
  return d;
}
// single voice dump (163 bytes)
function voiceToSysex(v, ch){
  const d = new Uint8Array(163);
  d.set([0xF0, 0x43, (ch || 0) & 15, 0x00, 0x01, 0x1B]);
  for (let i = 0; i < 155; i++) d[6 + i] = v[i];
  d[161] = checksum(d, 6, 161); d[162] = 0xF7;
  return d;
}
// parameter change: voice (group 0) or function (group 2)
function paramChange(ch, group, param, value){ return [0xF0, 0x43, 0x10 | (ch & 15), (group << 2) | ((param >> 7) & 3), param & 127, value & 127, 0xF7]; }
/* Reads any file of voices: one or more sysex messages (bank or single voice dumps), or a bare 4096-byte bank /
 * 155-byte voice. Returns {bank:[32 voices] | null, voices:[...single voices], params:[[p, v]...], bad:checksum errors}. */
function parseSysexFile(bytes){
  const d = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const r = {bank:null, voices:[], params:[], fparams:[], bad:0, messages:0};
  if (d.length === 4096 && d[0] !== 0xF0){ r.bank = []; for (let i = 0; i < 32; i++) r.bank.push(unpackVoice(d, i * 128)); return r; }
  if ((d.length === 155 || d.length === 156) && d[0] !== 0xF0){ const v = new Uint8Array(VOICE_SIZE); v.set(d.subarray(0, 155)); v[155] = 63; r.voices.push(clampVoice(v)); return r; }
  let i = 0;
  while (i < d.length){
    if (d[i] !== 0xF0){ i++; continue; }
    let e = i + 1; while (e < d.length && d[e] !== 0xF7) e++;
    const m = d.subarray(i, Math.min(e + 1, d.length));
    i = e + 1;
    const res = parseSysexMessage(m);
    if (!res) continue;
    r.messages++;
    if (res.bad) r.bad++;
    if (res.bank) r.bank = res.bank;
    if (res.voice) r.voices.push(res.voice);
    if (res.param) r.params.push(res.param);
    if (res.fparam) r.fparams.push(res.fparam);
  }
  return r;
}
// one message: {bank} | {voice} | {param:[n, v]} | {fparam:[n, v]} | {request:'bank'|'voice'} | null
function parseSysexMessage(m){
  if (m.length < 6 || m[0] !== 0xF0 || m[1] !== 0x43) return null;
  const sub = m[2] >> 4;
  if (sub === 0){
    const fmt = m[3], cnt = (m[4] << 7) | m[5];
    if (fmt === 9 && m.length >= 4104){
      const bad = checksum(m, 6, 4102) !== (m[4102] & 127);
      const bank = []; for (let k = 0; k < 32; k++) bank.push(unpackVoice(m, 6 + k * 128));
      return {bank, bad};
    }
    if (fmt === 0 && cnt === 155 && m.length >= 163){
      const bad = checksum(m, 6, 161) !== (m[161] & 127);
      const v = new Uint8Array(VOICE_SIZE); for (let k = 0; k < 155; k++) v[k] = m[6 + k] & 127; v[155] = 63;
      return {voice:clampVoice(v), bad};
    }
    return null;
  }
  if (sub === 1 && m.length >= 7){
    const g = (m[3] >> 2) & 31, n = ((m[3] & 3) << 7) | m[4], val = m[5] & 127;
    if (g === 0 && n <= 155) return {param:[n, val]};
    if (g === 2) return {fparam:[n, val]};
    return null;
  }
  if (sub === 2) return {request:m[3] === 9 ? 'bank' : 'voice'};
  return null;
}

/* ---------------- function (global) settings ---------------- */
// DX function parameters 64-77 plus this machine's own (master tune, MIDI, converter)
const FUNC_DEF = [
  ['mono', 'POLY/MONO', 1, 0, 64], ['pbRange', 'P BEND RANGE', 12, 2, 65], ['pbStep', 'P BEND STEP', 12, 0, 66], ['portaMode', 'PORTA MODE', 1, 0, 67],
  ['gliss', 'PORTA GLISS', 1, 0, 68], ['portaTime', 'PORTA TIME', 99, 0, 69], ['mwRange', 'MOD WHEEL RANGE', 99, 50, 70], ['mwAssign', 'MOD WHEEL ASSIGN', 7, 1, 71],
  ['fcRange', 'FOOT CTRL RANGE', 99, 0, 72], ['fcAssign', 'FOOT CTRL ASSIGN', 7, 0, 73], ['bcRange', 'BREATH CTRL RANGE', 99, 0, 74], ['bcAssign', 'BREATH ASSIGN', 7, 0, 75],
  ['atRange', 'AFTERTOUCH RANGE', 99, 0, 76], ['atAssign', 'AFTERTOUCH ASSIGN', 7, 0, 77],
  ['tune', 'MASTER TUNE', 127, 64, -1], ['rxCh', 'MIDI RECEIVE CH', 16, 0, -1], ['txCh', 'MIDI TRANSMIT CH', 15, 0, -1], ['sysInfo', 'SYS INFO', 1, 1, -1],
  ['dac', 'CONVERTER', 1, 0, -1], ['volume', 'VOLUME', 99, 80, -1],
  // effects after the output stage (not on the 1983 instrument; the chorus, delay and reverb it was usually played through)
  // the delay is the Omega 110's TC 2290-style dynamic delay, the reverb its REV7-style reverb
  ['chorus', 'CHORUS DEPTH', 99, 0, -1], ['chorusRate', 'CHORUS RATE', 99, 30, -1], ['delayMix', 'DELAY RETURN', 99, 0, -1], ['delayTime', 'DELAY TIME MS', 2000, 375, -1],
  ['delayFb', 'DELAY FEEDBACK', 95, 35, -1], ['revMix', 'REVERB RETURN', 99, 0, -1], ['revSize', 'REVERB TIME', 100, 26, -1],
  // the converter's analog faults: 50 = as measured on a working unit, 0 = none, 99 = a tired one
  ['hiss', 'DAC NOISE', 99, 50, -1],
  ['rvProg', 'REVERB PROGRAM', 34, 0, -1], ['rvHigh', 'REVERB HIGH', 10, 6, -1], ['rvDelay', 'REVERB DELAY', 200, 22, -1], ['rvDiff', 'REVERB DIFFUSION', 10, 8, -1],
  ['rvEr', 'REVERB ER LEVEL', 99, 35, -1], ['rvCut', 'REVERB HIGH CUT', 120, 90, -1], ['dlSync', 'DELAY SYNC', 13, 0, -1], ['dlHiCut', 'DELAY HIGH CUT', 200, 60, -1],
  ['dlLoCut', 'DELAY LOW CUT', 99, 10, -1], ['dlModRate', 'DELAY MOD RATE', 99, 12, -1], ['dlModDepth', 'DELAY MOD DEPTH', 99, 20, -1], ['dlDuck', 'DELAY DUCKING', 99, 40, -1],
  ['dlPing', 'DELAY PING-PONG', 1, 1, -1]
];
function defaultFunc(){ const f = {}; for (const [k, , , d] of FUNC_DEF) f[k] = d; return f; }
const FUNC_BY_NUM = {}; for (const d of FUNC_DEF) if (d[4] >= 0) FUNC_BY_NUM[d[4]] = d[0];

/* ---------------- envelopes and LFO ---------------- */
class Env {
  constructor(){ this.rates = [0, 0, 0, 0]; this.levels = [0, 0, 0, 0]; this.outlevel = 0; this.rs = 0; this.level = 0; this.target = 0; this.inc = 0; this.ix = 0; this.rising = false; this.down = true; }
  init(r, l, ol, rs){ for (let i = 0; i < 4; i++){ this.rates[i] = r[i]; this.levels[i] = l[i]; } this.outlevel = ol; this.rs = rs; this.level = 0; this.down = true; this.advance(0); }
  update(r, l, ol, rs){
    for (let i = 0; i < 4; i++){ this.rates[i] = r[i]; this.levels[i] = l[i]; } this.outlevel = ol; this.rs = rs;
    if (this.down && this.ix < 4) this.retarget();
  }
  retarget(){
    let a = scaleOutLevel(this.levels[this.ix]) >> 1;
    a = (a << 6) + this.outlevel - 4256;
    if (a < 16) a = 16;
    this.target = a * 65536;
    this.rising = this.target > this.level;
    let q = (this.rates[this.ix] * 41) >> 6;
    q += this.rs; if (q > 63) q = 63;
    const inc = (4 + (q & 3)) * Math.pow(2, 2 + 6 + (q >> 2));
    this.inc = Math.floor(inc * ENV_SRM / Q24);
  }
  advance(ix){ this.ix = ix; if (ix < 4) this.retarget(); }
  keyDown(d){ if (this.down !== d){ this.down = d; this.advance(d ? 0 : 3); } }
  // one control block; returns the level in 1/256 octave * 65536
  tick(){
    if (this.ix < 3 || (this.ix < 4 && !this.down)){
      if (this.rising){
        const jump = 1716 * 65536;
        if (this.level < jump) this.level = jump;
        this.level += Math.floor((17 * Q24 - this.level) / Q24) * this.inc;
        if (this.level >= this.target){ this.level = this.target; this.advance(this.ix + 1); }
      } else {
        this.level -= this.inc;
        if (this.level <= this.target){ this.level = this.target; this.advance(this.ix + 1); }
      }
    }
    return this.level;
  }
}
class PitchEnv {
  constructor(){ this.rates = [0, 0, 0, 0]; this.levels = [0, 0, 0, 0]; this.level = 0; this.target = 0; this.inc = 0; this.ix = 0; this.rising = false; this.down = true; }
  set(r, l){ for (let i = 0; i < 4; i++){ this.rates[i] = r[i]; this.levels[i] = l[i]; } this.level = PITCH_LEVEL[l[3]] * 524288; this.down = true; this.advance(0); }
  update(r, l){ for (let i = 0; i < 4; i++){ this.rates[i] = r[i]; this.levels[i] = l[i]; } if (this.ix < 4) this.advance(this.ix); }
  advance(ix){
    this.ix = ix;
    if (ix < 4){ this.target = PITCH_LEVEL[this.levels[ix]] * 524288; this.rising = this.target > this.level; this.inc = PITCH_RATE[this.rates[ix]] * PENV_UNIT; }
  }
  keyDown(d){ if (this.down !== d){ this.down = d; this.advance(d ? 0 : 3); } }
  tick(){
    if (this.ix < 3 || (this.ix < 4 && !this.down)){
      if (this.rising){ this.level += this.inc; if (this.level >= this.target){ this.level = this.target; this.advance(this.ix + 1); } }
      else { this.level -= this.inc; if (this.level <= this.target){ this.level = this.target; this.advance(this.ix + 1); } }
    }
    return this.level;
  }
}
class Lfo {
  constructor(){ this.phase = 0; this.delta = 0; this.delayState = 0; this.delayInc = 0; this.delayInc2 = 0; this.wave = 0; this.sync = false; this.rand = 0; this.val = 0; this.delayVal = 0; }
  reset(v){
    const rate = v[P.LFS];
    let sr = rate === 0 ? 1 : (165 * rate) >> 6;
    sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
    this.delta = (LFO_UNIT * sr) >>> 0;
    let a = 99 - v[P.LFD];
    if (a === 99){ this.delayInc = 0xFFFFFFFF; this.delayInc2 = 0xFFFFFFFF; }
    else {
      a = (16 + (a & 15)) << (1 + (a >> 4));
      this.delayInc = LFO_UNIT * a;
      a &= 0xff80; a = Math.max(0x80, a);
      this.delayInc2 = LFO_UNIT * a;
    }
    this.wave = v[P.LFW]; this.sync = v[P.LFKS] !== 0;
  }
  keyDown(){ if (this.sync) this.phase = 0x7FFFFFFF; this.delayState = 0; }
  tick(){
    this.phase = (this.phase + this.delta) >>> 0;
    const ph = this.phase;
    let x;
    switch (this.wave){
      case 0: x = ph >>> 7; if (ph >>> 31) x = ~x; x &= 0xFFFFFF; break;           // triangle
      case 1: x = ((~ph) ^ 0x80000000) >>> 8; break;                               // saw down
      case 2: x = (ph ^ 0x80000000) >>> 8; break;                                  // saw up
      case 3: x = ((~ph) >>> 7) & 0x1000000; break;                                // square
      case 4: x = 8388608 + (LFOSIN[ph >>> 20] >> 1); break;                       // sine
      default: if (ph < this.delta) this.rand = (this.rand * 179 + 17) & 0xff; x = this.rand << 16;   // sample and hold
    }
    this.val = x;
    // delay
    const inc = this.delayState < 0x80000000 ? this.delayInc : this.delayInc2;
    const d = this.delayState + inc;
    if (d > 0xFFFFFFFF) this.delayVal = Q24;
    else { this.delayState = d; this.delayVal = d < 0x80000000 ? 0 : (d >>> 7) & 0xFFFFFF; }
  }
}

/* ---------------- a voice: six operators ---------------- */
function oscLogFreq(note, mode, coarse, fine, det){
  let lf;
  if (mode === 0){
    lf = NOTE_LOG0 + Math.floor(note * Q24 / 12);
    const dr = 0.0209 * Math.exp(-0.396 * (lf / Q24)) / 7;
    lf += Math.trunc(dr * lf * (det - 7));
    lf += COARSE_LOG[coarse & 31];
    if (fine) lf += Math.floor(0.5 + Q24 * Math.log2(1 + 0.01 * fine));
  } else {
    lf = Math.floor(4458616 * ((coarse & 3) * 100 + fine) / 8);
    lf += det > 7 ? 13457 * (det - 7) : 0;
  }
  return lf;
}
function phaseInc(logfreq){ return Math.round(Math.pow(2, logfreq / Q24) / DX_RATE * 4294967296) >>> 0; }

class Voice {
  constructor(){
    this.env = [0, 1, 2, 3, 4, 5].map(() => new Env());
    this.penv = new PitchEnv();
    this.phase = new Float64Array(6); this.inc = new Float64Array(6);
    this.base = new Int32Array(6); this.fixed = new Uint8Array(6);
    this.att0 = new Int32Array(6); this.att1 = new Int32Array(6); this.out = new Int32Array(6);
    this.ams = new Uint8Array(6);
    this.fb0 = 0; this.fb1 = 0;
    this.note = -1; this.key = -1; this.vel = 0; this.active = false; this.held = false; this.sustained = false; this.age = 0;
    this.pitch = 0; this.target = 0;        // portamento, note pitch in Q24 (1 octave = 1<<24)
  }
  setup(v, note, vel, porta){
    this.note = note; this.vel = vel;
    for (let op = 1; op <= 6; op++){
      const b = opBase(op), k = op - 1;
      const r = [v[b], v[b + 1], v[b + 2], v[b + 3]], l = [v[b + 4], v[b + 5], v[b + 6], v[b + 7]];
      this.env[k].init(r, l, this.opOutLevel(v, b), scaleRate(note, v[b + 13]));
      this.att0[k] = 4095; this.att1[k] = 4095;
    }
    this.statics(v, note);
    this.penv.set([v[126], v[127], v[128], v[129]], [v[130], v[131], v[132], v[133]]);
    if (v[P.OKS]) for (let k = 0; k < 6; k++) this.phase[k] = 0;
    this.fb0 = this.fb1 = 0;
    this.target = Math.floor(note * Q24 / 12);
    if (porta < 0) this.pitch = this.target;
    this.active = true; this.held = true; this.sustained = false;
  }
  opOutLevel(v, b){
    let ol = scaleOutLevel(v[b + 16]);
    ol += scaleLevel(this.note, v[b + 8], v[b + 9], v[b + 10], v[b + 11], v[b + 12]);
    ol = Math.min(127, ol) << 5;
    ol += scaleVelocity(this.vel, v[b + 15]);
    return Math.max(0, ol);
  }
  statics(v, note){
    for (let op = 1; op <= 6; op++){
      const b = opBase(op), k = op - 1;
      this.fixed[k] = v[b + 17];
      // ratio-mode pitch is relative to the played note (added later with portamento), fixed is absolute
      this.base[k] = this.fixed[k] ? oscLogFreq(note, 1, v[b + 18], v[b + 19], v[b + 20]) : oscLogFreq(note, 0, v[b + 18], v[b + 19], v[b + 20]) - NOTE_LOG0 - Math.floor(note * Q24 / 12);
      this.ams[k] = AMS_TAB[v[b + 14]];
    }
  }
  // live edits reach the sounding note
  update(v){
    for (let op = 1; op <= 6; op++){
      const b = opBase(op);
      this.env[op - 1].update([v[b], v[b + 1], v[b + 2], v[b + 3]], [v[b + 4], v[b + 5], v[b + 6], v[b + 7]], this.opOutLevel(v, b), scaleRate(this.note, v[b + 13]));
    }
    this.statics(v, this.note);
    this.penv.update([v[126], v[127], v[128], v[129]], [v[130], v[131], v[132], v[133]]);
  }
  keyUp(){ this.held = false; for (const e of this.env) e.keyDown(false); this.penv.keyDown(false); }
  retrigger(note){   // mono legato: new pitch, envelopes carry on
    this.note = note; this.target = Math.floor(note * Q24 / 12); this.held = true; this.sustained = false;
  }
}

/* ---------------- the synth: voices, LFO, controllers, converter, output stage ---------------- */
class Synth {
  constructor(fs){
    this.fs = fs || 48000;
    this.voice = initVoice();                 // the edit buffer
    this.func = defaultFunc();
    this.voices = []; for (let i = 0; i < VOICES; i++) this.voices.push(new Voice());
    this.lfo = new Lfo(); this.lfo.reset(this.voice);
    this.ctl = {wheel:0, foot:0, breath:0, at:0, bend:0, sustain:false, portaSw:true, volume:127, expression:127};
    this.stack = [];                          // held keys in order (mono mode)
    this.lastPitch = -1; this.clock = 0;
    this.blockPos = 0;
    this.pitchMod = 0; this.ampMod = 0; this.egMod = 127;
    this.recalcCtl();
    // output stage
    this.zoh = 4;                              // the converter's sample and hold, seen at 4x the rate
    this.fin = DX_RATE * this.zoh;
    this.bq = [lowpass(16000, this.fin, 0.5412), lowpass(16000, this.fin, 1.3066)];
    this.rs = makeResampler(this.fin, this.fs);
    this.fx = new Fx(this.fs);
    this.dac = new Mk1Dac();
    this.peak = 0;
  }
  setRate(fs){ this.fs = fs; this.rs = makeResampler(this.fin, fs); this.fx = new Fx(fs); }

  /* ----- voice edits ----- */
  loadVoice(v){
    const x = new Uint8Array(VOICE_SIZE); x.set(v.length > VOICE_SIZE ? v.subarray(0, VOICE_SIZE) : v); if (v.length < VOICE_SIZE) x[155] = 63;
    this.voice = clampVoice(x); this.voiceChanged();
  }
  setParam(n, val){
    if (n < 0 || n > 155) return;
    const p = VPARAMS[n]; val = Math.max(p.min || 0, Math.min(p.max, val | 0));
    if (this.voice[n] === val) return;
    this.voice[n] = val;
    this.voiceChanged();
  }
  voiceChanged(){ this.lfo.reset(this.voice); for (const vc of this.voices) if (vc.active) vc.update(this.voice); }
  setFunc(k, val){
    const d = FUNC_DEF.find(x => x[0] === k); if (!d) return;
    this.func[k] = Math.max(0, Math.min(d[2], val | 0));
    if (k === 'mono'){ this.allNotesOff(true); }
    this.recalcCtl();
  }

  /* ----- performance ----- */
  recalcCtl(){
    const f = this.func, c = this.ctl;
    let pm = 0, am = 0, eg = 0, anyEg = false;
    const add = (val, range, assign) => {
      const t = Math.floor(val * range / 100);
      if (assign & 1) pm = Math.max(pm, t);
      if (assign & 2) am = Math.max(am, t);
      if (assign & 4){ eg = Math.max(eg, t); anyEg = true; }
    };
    add(c.wheel, f.mwRange, f.mwAssign); add(c.foot, f.fcRange, f.fcAssign); add(c.breath, f.bcRange, f.bcAssign); add(c.at, f.atRange, f.atAssign);
    this.pitchMod = pm; this.ampMod = am; this.egMod = anyEg ? eg : 127;
  }
  noteOn(note, vel){
    if (vel === 0) return this.noteOff(note);
    note &= 127;
    const tr = this.voice[P.TRNSP] - 24;
    const n = Math.max(0, Math.min(127, note + tr));
    const portaOn = this.func.portaTime > 0 && this.ctl.portaSw;
    this.lfo.keyDown();
    this.clock++;
    if (this.func.mono){
      this.stack = this.stack.filter(x => x !== note); this.stack.push(note);
      const vc = this.voices[0];
      const legato = vc.active && vc.held;
      if (legato){ vc.retrigger(n); vc.key = note; if (!portaOn) vc.pitch = vc.target; vc.update(this.voice); }
      else {
        // full-time portamento glides from the last note even when not legato; fingered only when legato
        const from = vc.active || this.lastPitch >= 0 ? (vc.active ? vc.pitch : this.lastPitch) : -1;
        vc.setup(this.voice, n, vel, portaOn && this.func.portaMode === 1 && from >= 0 ? 0 : -1);
        if (portaOn && this.func.portaMode === 1 && from >= 0) vc.pitch = from;
        vc.key = note; vc.age = this.clock;
      }
      this.lastPitch = vc.target;
      return;
    }
    // poly: a free voice, else the oldest released, else the oldest
    let best = -1, bestScore = Infinity;
    for (let i = 0; i < VOICES; i++){
      const vc = this.voices[i];
      const score = !vc.active ? -1e9 + i : (vc.held || vc.sustained ? vc.age : vc.age - 1e8);
      if (score < bestScore){ bestScore = score; best = i; }
    }
    const vc = this.voices[best];
    const glide = portaOn && this.lastPitch >= 0;
    vc.setup(this.voice, n, vel, glide ? 0 : -1);
    if (glide) vc.pitch = this.lastPitch;
    vc.key = note; vc.age = this.clock;
    this.lastPitch = vc.target;
  }
  noteOff(note){
    note &= 127;
    if (this.func.mono){
      const wasTop = this.stack.length && this.stack[this.stack.length - 1] === note;
      this.stack = this.stack.filter(x => x !== note);
      const vc = this.voices[0];
      if (!wasTop || !vc.active || vc.key !== note) return;
      if (this.stack.length){
        const k = this.stack[this.stack.length - 1], n = Math.max(0, Math.min(127, k + this.voice[P.TRNSP] - 24));
        vc.retrigger(n); vc.key = k; if (!(this.func.portaTime > 0 && this.ctl.portaSw)) vc.pitch = vc.target; vc.update(this.voice);
        this.lastPitch = vc.target;
        return;
      }
      if (this.ctl.sustain){ vc.sustained = true; vc.held = false; return; }
      vc.keyUp(); return;
    }
    for (const vc of this.voices){
      if (vc.active && vc.held && vc.key === note){
        if (this.ctl.sustain){ vc.held = false; vc.sustained = true; }
        else vc.keyUp();
      }
    }
  }
  sustain(on){
    this.ctl.sustain = on;
    if (!on) for (const vc of this.voices) if (vc.active && vc.sustained){ vc.sustained = false; vc.keyUp(); }
  }
  allNotesOff(hard){
    this.stack = [];
    for (const vc of this.voices){ if (vc.active){ vc.sustained = false; vc.keyUp(); if (hard) vc.active = false; } }
  }
  setBend(v){ this.ctl.bend = Math.max(-8192, Math.min(8191, v)); }       // -8192..8191
  setCtl(k, v){ this.ctl[k] = v; this.recalcCtl(); }

  /* ----- MIDI in (one complete message) ----- */
  midi(m){
    const st = m[0];
    if (st === 0xF0){ return this.sysex(m); }
    const ch = st & 15, ty = st & 0xF0, rx = this.func.rxCh;
    if (ty < 0x80 || st >= 0xF0) return null;
    if (rx > 0 && ch !== rx - 1) return null;
    const a = m[1] | 0, b = m[2] | 0;
    switch (ty){
      case 0x90: if (b) this.noteOn(a, b); else this.noteOff(a); break;
      case 0x80: this.noteOff(a); break;
      case 0xE0: this.setBend(((b << 7) | a) - 8192); break;
      case 0xD0: this.setCtl('at', a); break;
      case 0xA0: this.setCtl('at', b); break;
      case 0xC0: return {program:a};
      case 0xB0:
        switch (a){
          case 1: this.setCtl('wheel', b); break;
          case 2: this.setCtl('breath', b); break;
          case 4: this.setCtl('foot', b); break;
          case 5: this.setFunc('portaTime', Math.round(b * 99 / 127)); return {func:'portaTime'};
          case 7: this.ctl.volume = b; break;
          case 11: this.ctl.expression = b; break;
          case 64: this.sustain(b >= 64); break;
          case 65: this.ctl.portaSw = b >= 64; break;
          case 120: this.allNotesOff(true); break;
          case 121: this.ctl.wheel = this.ctl.foot = this.ctl.breath = this.ctl.at = 0; this.ctl.bend = 0; this.ctl.expression = 127; this.sustain(false); this.recalcCtl(); break;
          case 123: this.allNotesOff(false); break;
          case 126: this.setFunc('mono', 1); return {func:'mono'};
          case 127: this.setFunc('mono', 0); return {func:'mono'};
        }
        break;
    }
    return null;
  }
  sysex(m){
    if (!this.func.sysInfo) return null;
    const r = parseSysexMessage(m);
    if (!r) return null;
    if (r.param){ this.setParam(r.param[0], r.param[1]); return {param:r.param}; }
    if (r.fparam){ const k = FUNC_BY_NUM[r.fparam[0]]; if (k){ this.setFunc(k, r.fparam[1]); return {func:k}; } return null; }
    if (r.voice){ this.loadVoice(r.voice); return {voice:r.voice, bad:r.bad}; }
    if (r.bank) return {bank:r.bank, bad:r.bad};
    if (r.request) return {request:r.request};
    return null;
  }

  /* ----- sound ----- */
  // envelopes, LFO, pitch and portamento for the next 64 samples
  control(){
    const v = this.voice, f = this.func, lfo = this.lfo;
    lfo.tick();
    const lv = lfo.val, ld = lfo.delayVal;
    const pmdDepth = (v[P.LPMD] * 165) >> 6, amdDepth = (v[P.LAMD] * 165) >> 6;
    const sens = PMS_TAB[v[P.LPMS]] * (lv - 8388608);
    const pm1 = Math.abs(Math.trunc(pmdDepth * (ld >> 12) * sens / 134217728));
    const pm2 = Math.abs(Math.trunc(this.pitchMod * sens / 16384));
    const lfoPitch = Math.max(pm1, pm2) * (sens < 0 ? -1 : 1);
    const am1 = Math.trunc(Math.trunc(amdDepth * ld / 256) * lv / Q24);
    const am2 = Math.trunc(this.ampMod * lv / 128);
    let amd = Math.max(am1, am2);
    amd = Math.max(Q24 - (this.egMod + 1) * 131072, amd);
    // bend: continuous over the range, or in steps
    let bendSemi = this.ctl.bend / 8192 * f.pbRange;
    if (f.pbStep > 0) bendSemi = Math.round(bendSemi / f.pbStep) * f.pbStep;
    const global = Math.round(bendSemi * Q24 / 12) + Math.round((f.tune - 64) * 75 / 64 / 1200 * Q24);
    const porta = f.portaTime > 0 && this.ctl.portaSw ? Math.max(1, Math.round(1000 * Math.pow(2, -f.portaTime / 10) / 12 * Q24 * N / DX_RATE)) : 0;
    for (const vc of this.voices){
      if (!vc.active) continue;
      // portamento toward the note, in semitone steps when glissando is on
      if (porta && vc.pitch !== vc.target){
        if (vc.pitch < vc.target) vc.pitch = Math.min(vc.target, vc.pitch + porta); else vc.pitch = Math.max(vc.target, vc.pitch - porta);
      } else vc.pitch = vc.target;
      let np = vc.pitch;
      if (f.gliss && porta){ const st = Q24 / 12; np = Math.round(np / st) * st; }
      const penv = vc.penv.tick();
      const ratioPitch = NOTE_LOG0 + np + penv + lfoPitch + global;
      let silent = !vc.held && !vc.sustained;
      const alg = ALGS[v[P.ALG]], on = v[P.OPON];
      for (let k = 0; k < 6; k++){
        const lev = vc.env[k].tick();
        let att = 4096 - Math.floor(lev / 65536);
        if (vc.ams[k]) att += Math.trunc(amd * vc.ams[k] / 256) >> 12;
        if (att < 0) att = 0; if (att > 4095) att = 4095;
        if (!(on >> (6 - (k + 1)) & 1)) att = 4095;
        vc.att0[k] = vc.att1[k]; vc.att1[k] = att;
        vc.inc[k] = phaseInc(vc.fixed[k] ? vc.base[k] + penv : vc.base[k] + ratioPitch);
        if (alg.car[k] && (att < 3900 || vc.att0[k] < 3900)) silent = false;
      }
      if (silent) vc.active = false;
    }
  }
  // one sample at the hardware rate: the converter's output (signed 16-bit range)
  dxSample(){
    if (this.blockPos === 0) this.control();
    const i = this.blockPos; this.blockPos = (this.blockPos + 1) & (N - 1);
    const alg = ALGS[this.voice[P.ALG]], fbAmt = this.voice[P.FB], fbShift = fbAmt ? Math.pow(2, 12 + fbAmt) : 0;
    const mk1 = this.func.dac === 0, amt = this.func.hiss / 50, dac = this.dac;
    let acc = 0, live = 0;
    for (const vc of this.voices){
      if (!vc.active) continue;
      live++;
      const out = vc.out;
      let sum = 0;
      for (let k = 5; k >= 0; k--){
        const a = vc.att0[k] + (((vc.att1[k] - vc.att0[k]) * i) >> 6);
        let ph = vc.phase[k];
        const mm = alg.mod[k];
        if (mm){ let m = 0; for (let s = 0; s < 6; s++) if (mm >> s & 1) m += out[s]; ph += m * 2097152; }
        if (k === alg.fb && fbShift) ph += (vc.fb0 + vc.fb1) * fbShift;
        const idx = (ph >>> 18) & 16383;
        let y;
        if (a >= 4095) y = 0;
        else {
          const q = idx & 4095, t = (idx & 4096 ? LOGSIN[4095 - q] : LOGSIN[q]) + a;
          y = EXPT[t & 255] >> (t >> 8);
          if (idx & 8192) y = -y;
        }
        out[k] = y;
        if (k === alg.fbs){ vc.fb0 = vc.fb1; vc.fb1 = y; }
        if (alg.car[k]) sum += y;
        vc.phase[k] = (vc.phase[k] + vc.inc[k]) >>> 0;
      }
      // Mark I: each voice has its own slot on the converter, the slots add up in the analog stage
      if (mk1) acc += dac.voice(sum, amt); else acc += sum;
    }
    if (mk1) return dac.out(acc, VOL_STEPS[Math.round(this.func.volume / 99 * 7)], VOICES - live, amt);
    // clean 16-bit converter: one digital mix that saturates at 16 bits
    if (acc > 32767) acc = 32767; else if (acc < -32768) acc = -32768;
    return acc;
  }
  // render n samples at the host rate into out (mono; the hardware has one output)
  render(out, off, n){
    const r = this.rs, bq = this.bq, z = this.zoh;
    // Mark I: the slider picks one of 8 DAC reference voltages (applied in the converter); clean: a smooth slider
    const vol = this.func.dac === 0 ? 1 : this.func.volume / 99, gain = 2 / 32768 * vol * vol * (this.ctl.volume / 127) * (this.ctl.expression / 127);
    for (let j = 0; j < n; j++){
      while (r.have < r.need){
        const x = this.dxSample();
        for (let s = 0; s < z; s++) r.push(biquad(bq[1], biquad(bq[0], x)));
      }
      const y = r.next() * gain;
      out[off + j] = y;
      const ay = y < 0 ? -y : y; if (ay > this.peak) this.peak = ay;
    }
  }
  // the synth then the effects, in stereo
  renderStereo(L, R, off, n){
    this.render(L, off, n);
    this.fx.process(L, R, off, n, this.func);
  }
  activeVoices(){ let c = 0; for (const v of this.voices) if (v.active) c++; return c; }
}

/* ---------------- the Mark I converter, analog side included ----------------
 * As on the board (service manual; K. Shirriff's die analysis of the OPS chip):
 *   - the OPS sends the 16 voices to the DAC one after another (16 x 49.096 kHz); each voice's 15-bit sum is shifted
 *     left until it has two leading zeros (0-3 places) and its top 12 bits go to a 12-bit DAC (BA9221); a resistor
 *     divider switched by a 4066 divides the result back by 1, 2, 4 or 8 (a floating-point DAC: 12-bit resolution
 *     riding on each voice), then sample-and-holds and the ~16 kHz Sallen-Key filter add the voices up,
 *   - the volume slider picks one of 8 reference voltages for the DAC (a 4051), so volume moves in 8 steps and the
 *     DAC's noise moves with it, while the op-amps after it add a steady floor.
 * Modeled faults, fixed per unit like real component tolerances: each DAC bit's weight a little off (grit on quiet and
 * decaying notes), each divider step off by up to about 1 % (1 % metal-film resistors: the decay distorts in four
 * stages as it passes through the ranges), DAC noise per voice slot divided down with the voice's range (so the
 * noise follows each note) and the output stage's hiss. `amount` scales the faults: 1 = a typical unit. */
const MK1 = {
  bitErr:[0.0012, -0.0008, 0.0006, -0.0004, 0.0003, -0.0002, 0.00015, -0.0001, 0, 0, 0, 0],   // MSB first (fraction of the bit's weight)
  rangeErr:[0, -0.0072, 0.0058, -0.0091],                 // gain error of the divide-by-1/2/4/8 steps (largest step = reference)
  dacNoise:0.30,                                          // DAC + its op-amp, rms in 12-bit LSBs, per voice slot, before the divider
  floorNoise:1.05                                         // the output stage, rms in 16-bit LSBs, after the volume
};
// the 8 reference voltages the volume slider picks (dB: off, -36, -28, -21, -15, -10, -5, 0)
const VOL_STEPS = [0, -36, -28, -21, -15, -10, -5, 0].map((d, i) => i ? Math.pow(10, d / 20) : 0);
class Mk1Dac {
  constructor(){
    // the analog value of every 12-bit code, from the bit weights
    this.ladder = new Float64Array(4096); this.ideal = new Float64Array(4096);
    for (let c = 0; c < 4096; c++){
      let v = 0, id = 0;
      const u = c ^ 2048;                                 // two's complement -> offset binary
      for (let b = 11; b >= 0; b--) if (u >> b & 1){ const w = Math.pow(2, b); v += w * (1 + MK1.bitErr[11 - b]); id += w; }
      this.ladder[c] = v - 2048; this.ideal[c] = id - 2048;
    }
    this.seed = 0x2545F491;
  }
  // roughly gaussian, unit variance (sum of four uniforms)
  noise(){
    let s = 0;
    for (let i = 0; i < 4; i++){ let x = this.seed; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; this.seed = x; s += x / 4294967296; }
    return (s - 2) * 1.7320508075688772;
  }
  // one voice slot: 15-bit sum -> 12-bit code + range, with its noise
  voice(v, amount){
    if (v > 16383) v = 16383; else if (v < -16384) v = -16384;
    let e = 0;
    while (e < 3 && (v >= (2048 << e) || v < -(2048 << e))) e++;
    const c = (v >> e) & 4095;
    const code = this.ideal[c] + (this.ladder[c] - this.ideal[c]) * amount + this.noise() * MK1.dacNoise * amount;
    return code * Math.pow(2, e) * (1 + MK1.rangeErr[e] * amount);
  }
  // the analog sum through the volume reference, plus the silent slots' DAC noise and the output stage's floor
  out(sum, vol, idle, amount){
    const idleNoise = this.noise() * MK1.dacNoise * amount * Math.sqrt(idle);
    return (sum + idleNoise) * vol + this.noise() * MK1.floorNoise * amount;
  }
}

/* ---------------- effects: stereo chorus, then the Omega 110's TC 2290-style delay and REV7-style reverb ---------------- */
const TAU = 2 * Math.PI;
const onePole = (f, sr) => 1 - Math.exp(-TAU * f / sr);
// 0-4: the Omega 110's REV7-style programs; 5-34: the full REV7 model's 30 factory programs (rev7.js, from the service manual)
const RV_PROGS = ['HALL', 'ROOM', 'VOCAL', 'PLATE', 'EARLY REF', 'LARGE HALL', 'SMALL HALL', 'VOCAL PLATE', 'PERCUSSION PLATE', 'EARLY REF. 1', 'EARLY REF. 2', 'DELAY L,R',
  'STEREO ECHO', 'STEREO FLANGE', 'REVERB FLANGE', 'CHORUS A', 'CHORUS B', 'STEREO PHASING', 'TREMOLO', 'SYMPHONIC', 'REVERB ROOM', 'ECHO ROOM', 'STRINGS',
  'ELECTRIC BASS A', 'ELECTRIC BASS B', 'KICK', 'SNARE', 'GATE REVERB', 'REVERSE GATE', 'PIANO', 'ORGAN', 'BRASS', 'GUITAR', 'HANDCLAPS', 'LIVE REFERENCE'];
const REV7_FULL = 5;
// what a full-REV7 program sets the shared controls to (REVERB TIME, HIGH, DELAY, DIFFUSION), so they show its values
function rev7Controls(prog){
  const R = (typeof globalThis !== 'undefined' && globalThis.DXR) || require('./rev7.js');
  const v = R.factoryValues(prog - REV7_FULL + 1), P7 = R.P;
  return {revSize:Math.round(v[P7.RT] * 10), rvHigh:Math.round(v[P7.HI] * 10), rvDelay:Math.round(v[P7.IDLY]), rvDiff:Math.round(v[P7.DIFF])};
}
// delay sync divisions, in beats: off, 1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2, 1/1
const DL_DIVS = [0, 0.125, 1 / 6, 0.25, 1 / 3, 0.375, 0.5, 2 / 3, 0.75, 1, 4 / 3, 1.5, 2, 4];
const DL_DIV_NAMES = ['MS', '1/32', '1/16T', '1/16', '1/8T', '1/16D', '1/8', '1/4T', '1/8D', '1/4', '1/2T', '1/4D', '1/2', '1/1'];

/* TC 2290-style dynamic delay: stereo, modulated, filtered feedback, ping-pong, and ducking (the repeats duck under the
   dry signal and swell up in the gaps, the 2290's "dynamic delay"). Time in ms or synced to the tempo. */
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

/* REV7-style digital reverb. Yamaha's REV7: programs Hall, Room, Vocal, Plate and Early Reflections; RT, HIGH (the
   high-frequency RT as a ratio), DELAY (pre-delay), ER level, diffusion, high cut, and its ~12 kHz bandwidth.
   Built as: pre-delay -> 12 kHz band limit -> four input diffusers -> early reflections (a 16-tap pattern per program)
   and an 8-line feedback delay network with a Hadamard matrix and per-line absorption filters set from RT and HIGH. */
const RV_LINES = [31.7, 37.1, 41.3, 43.9, 53.3, 59.9, 67.7, 73.1];   // ms, before the program's size
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
    this.out = [0, 0]; this.o = new Float64Array(8); this.tapL = []; this.tapR = []; this.preD = 1; this.apG = 0.6; this.er = 0.35; this.late = 1;
  }
  setParams(prog, rt, high, delayMs, diff, er, cut){
    const sr = this.sr; prog = Math.max(0, Math.min(4, prog));
    this.preD = Math.max(1, Math.floor(delayMs * sr / 1000));
    const size = RV_SIZE[prog];
    for (let i = 0; i < 8; i++){
      const n = Math.max(2, Math.floor(RV_LINES[i] * size * sr / 1000));
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
    const o = this.o;
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

class Fx {
  constructor(fs){
    this.fs = fs; this.tempo = 120;
    this.chSize = 8192; this.chL = new Float64Array(this.chSize); this.chW = 0; this.chPh = 0;
    this.dl = new TCDelay(fs); this.rv = new Rev7(fs); this.key = '';
    this.full = null; this.fullProg = -1; this.fullBase = null;     // the full REV7 model, made when first used
    this.send = new Float32Array(0); this.wl = new Float32Array(0); this.wr = new Float32Array(0);
  }
  // the full REV7: a program change loads its factory settings; afterwards the shared controls edit it
  setupFull(f){
    if (!this.full){
      const R = (typeof globalThis !== 'undefined' && globalThis.DXR) || require('./rev7.js');
      this.R = R; this.full = new R.Rev7Full(); this.full.prepare(this.fs);
    }
    const P7 = this.R.P, now = [f.revSize, f.rvHigh, f.rvDelay, f.rvDiff];
    if (f.rvProg !== this.fullProg){ this.fullProg = f.rvProg; this.full.loadFactory(f.rvProg - REV7_FULL + 1); this.fullBase = now; return; }
    const b = this.fullBase;
    if (now[0] !== b[0]) this.full.set(P7.RT, Math.max(3, now[0]) / 10);
    if (now[1] !== b[1]) this.full.set(P7.HI, Math.max(1, now[1]) / 10);
    if (now[2] !== b[2]) this.full.set(P7.IDLY, Math.max(0.1, now[2]));
    if (now[3] !== b[3]) this.full.set(P7.DIFF, now[3]);
    this.fullBase = now;
  }
  // the delay and reverb settings, from the function values (only when they change)
  setup(f){
    const div = DL_DIVS[Math.max(0, Math.min(13, f.dlSync))];
    const ms = div > 0 ? 60 / Math.max(20, this.tempo) * div * 1000 : Math.max(10, f.delayTime);
    const key = [ms, f.delayFb, f.dlHiCut, f.dlLoCut, f.dlModRate, f.dlModDepth, f.dlDuck, f.dlPing, f.rvProg, f.revSize, f.rvHigh, f.rvDelay, f.rvDiff, f.rvEr, f.rvCut].join();
    if (key === this.key) return;
    this.key = key;
    this.dl.setParams(ms, f.delayFb / 100, Math.max(10, f.dlHiCut) * 100, Math.max(2, f.dlLoCut) * 10, Math.max(1, f.dlModRate) * 0.05, f.dlModDepth / 99, f.dlDuck / 99, f.dlPing >= 1);
    this.rv.setParams(f.rvProg, Math.max(3, f.revSize) / 10, Math.max(1, f.rvHigh) / 10, f.rvDelay, f.rvDiff, f.rvEr / 99, Math.max(10, f.rvCut) * 100);
  }
  process(L, R, off, n, f){
    const fs = this.fs;
    const cd = f.chorus / 99 * 3, cr = 0.1 * Math.pow(2, f.chorusRate / 99 * 6) / fs;
    const dm = f.delayMix / 99, rm = f.revMix / 99, full = rm > 0 && f.rvProg >= REV7_FULL;
    if (dm > 0 || rm > 0) this.setup(f);
    if (full){
      this.setupFull(f);
      if (this.send.length < n){ this.send = new Float32Array(n); this.wl = new Float32Array(n); this.wr = new Float32Array(n); }
    }
    for (let j = off; j < off + n; j++){
      const x = L[j];
      let l = x, r = x;
      if (cd > 0){
        // two taps 7 ms +- depth, swept by a triangle in opposite phases
        const cm = this.chSize - 1;
        this.chL[this.chW & cm] = x;
        const tri = p => { const q = p - Math.floor(p); return q < 0.5 ? q * 4 - 1 : 3 - q * 4; };
        const tapd = d => { const pos = this.chW - d * fs / 1000, ip = Math.floor(pos), fr = pos - ip; return this.chL[ip & cm] * (1 - fr) + this.chL[(ip + 1) & cm] * fr; };
        const a = tapd(7 + cd * tri(this.chPh)), b = tapd(7 + cd * tri(this.chPh + 0.5));
        l = 0.7 * (x + a); r = 0.7 * (x + b);
        this.chW++; this.chPh += cr; if (this.chPh >= 1) this.chPh -= 1;
      }
      // sends, as on the Omega 110: the delay and the reverb each take the (mono) signal, their returns add up
      const send = (l + r) * 0.5;
      let ol = l, or = r;
      if (dm > 0){ const d = this.dl.tick(send); ol += d[0] * dm; or += d[1] * dm; }
      if (full) this.send[j - off] = send;
      else if (rm > 0){ const v = this.rv.tick(send); ol += v[0] * rm; or += v[1] * rm; }
      L[j] = ol; R[j] = or;
    }
    if (full){
      this.full.process(this.send, this.wl, this.wr, 0, n);
      for (let j = 0; j < n; j++){ L[off + j] += this.wl[j] * rm; R[off + j] += this.wr[j] * rm; }
    }
  }
}

/* ---------------- the dice: a random voice that plays (seeded, the same in the plugin) ---------------- */
function randomVoice(seed){
  let st = (seed >>> 0) || 1;
  const rnd = () => { st ^= st << 13; st >>>= 0; st ^= st >>> 17; st ^= st << 5; st >>>= 0; return st / 4294967296; };
  const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
  const v = initVoice();
  const alg = ri(0, 31), A = ALGS[alg];
  const COARSE = [0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8, 9, 11, 14];
  const perc = rnd() < 0.4;                       // plucked / struck, or held
  for (let op = 1; op <= 6; op++){
    const b = opBase(op), car = A.car[op - 1];
    v[b] = car ? (perc ? ri(85, 99) : ri(45, 95)) : ri(40, 99);
    v[b + 1] = ri(20, 85); v[b + 2] = ri(10, 75); v[b + 3] = ri(30, 80);
    v[b + 4] = car ? 99 : ri(75, 99); v[b + 5] = perc ? ri(40, 85) : ri(75, 99); v[b + 6] = perc ? ri(0, 30) : ri(60, 95); v[b + 7] = 0;
    v[b + 8] = ri(30, 60); v[b + 9] = ri(0, 30); v[b + 10] = ri(0, 40); v[b + 11] = ri(0, 3); v[b + 12] = ri(0, 3);
    v[b + 13] = ri(0, 4); v[b + 14] = rnd() < 0.15 ? ri(1, 2) : 0; v[b + 15] = ri(0, 4);
    v[b + 16] = car ? ri(88, 99) : ri(40, 86);
    const fixed = !car && rnd() < 0.06;
    v[b + 17] = fixed ? 1 : 0;
    v[b + 18] = fixed ? ri(1, 3) : COARSE[ri(0, COARSE.length - 1)];
    v[b + 19] = rnd() < 0.8 ? 0 : ri(1, 99);
    v[b + 20] = ri(4, 10);
  }
  v[P.ALG] = alg; v[P.FB] = ri(0, 7); v[P.OKS] = 1;
  v[P.LFS] = ri(15, 55); v[P.LFD] = ri(0, 60); v[P.LPMD] = ri(0, 8); v[P.LAMD] = rnd() < 0.2 ? ri(5, 30) : 0; v[P.LFKS] = 0; v[P.LFW] = [0, 4, 4, 0, 1, 2][ri(0, 5)]; v[P.LPMS] = ri(1, 4);
  for (let i = 0; i < 4; i++){ v[P.PR + i] = 99; v[P.PL + i] = 50; }
  if (rnd() < 0.15){ v[P.PR] = ri(70, 99); v[P.PL + 3] = ri(44, 56); v[P.PL + 0] = v[P.PL + 3]; }
  v[P.TRNSP] = 24;
  setName(v, 'RANDOM ' + String(seed % 1000).padStart(3, '0'));
  return v;
}

/* ---------------- output stage pieces ---------------- */
function lowpass(fc, fs, q){
  const w = 2 * Math.PI * fc / fs, cw = Math.cos(w), al = Math.sin(w) / (2 * q), a0 = 1 + al;
  return {b0:(1 - cw) / 2 / a0, b1:(1 - cw) / a0, b2:(1 - cw) / 2 / a0, a1:-2 * cw / a0, a2:(1 - al) / a0, z1:0, z2:0};
}
function biquad(f, x){ const y = f.b0 * x + f.z1; f.z1 = f.b1 * x - f.a1 * y + f.z2; f.z2 = f.b2 * x - f.a2 * y; return y; }
/* Windowed-sinc resampler from the output stage's rate down to the host rate. A ring of input samples; for each
 * output sample the kernel is centred on its position in input samples. */
function makeResampler(fin, fout){
  const fc = Math.min(20000, 0.45 * fout) / fin;       // cycles per input sample
  const Z = 8, W = Math.ceil(Z / (2 * fc)), RES = 64;
  const tab = new Float64Array(W * RES + 2);
  for (let i = 0; i <= W * RES + 1; i++){
    const x = i / RES;
    if (x >= W){ tab[i] = 0; continue; }
    const s = x === 0 ? 1 : Math.sin(Math.PI * 2 * fc * x) / (Math.PI * 2 * fc * x);
    const wv = 0.42 + 0.5 * Math.cos(Math.PI * x / W) + 0.08 * Math.cos(2 * Math.PI * x / W);
    tab[i] = 2 * fc * s * wv;
  }
  const size = 1024, buf = new Float64Array(size);
  const r = {
    step:fin / fout, pos:0, have:0, need:W + 1, count:0, W, RES, tab, buf, size,
    push(x){ buf[r.count & (size - 1)] = x; r.count++; r.have = r.count; },
    // next output sample: input index pos (fractional) needs inputs up to floor(pos) + W
    next(){
      const p = r.pos, ip = Math.floor(p);
      let acc = 0;
      for (let k = ip - W + 1; k <= ip + W; k++){
        if (k < 0) continue;
        const x = Math.abs(p - k) * RES, xi = Math.floor(x), xf = x - xi;
        if (xi >= W * RES) continue;
        acc += buf[k & (size - 1)] * (tab[xi] + (tab[xi + 1] - tab[xi]) * xf);
      }
      r.pos += r.step;
      r.need = Math.floor(r.pos) + W + 1;
      return acc;
    }
  };
  return r;
}

/* ---------------- display helpers ---------------- */
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function noteName(n){ return NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 2); }    // MIDI 60 = C3, as on the hardware
function bpName(bp){ const n = bp + 9; return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1); }
const CURVES = ['-LIN', '-EXP', '+EXP', '+LIN'];
const WAVES = ['TRIANGL', 'SAW DWN', 'SAW UP', 'SQUARE', 'SINE', 'S/HOLD'];
function freqText(v, op){
  const b = opBase(op), c = v[b + 18], f = v[b + 19];
  if (v[b + 17]){ const hz = Math.pow(10, (c & 3) + f / 100); return (hz < 10 ? hz.toFixed(3) : hz < 100 ? hz.toFixed(2) : hz < 1000 ? hz.toFixed(1) : hz.toFixed(0)) + 'Hz'; }
  const r = (c === 0 ? 0.5 : c) * (1 + f / 100);
  return r.toFixed(2);
}

/* the dice can roll one part of the sound, as on the Omega 110: the rest stays as it is */
const DICE_PARTS = ['all', 'alg', 'freq', 'env', 'lfo', 'fx'];
const DICE_NAMES = ['ALL', 'ALGORITHM', 'FREQUENCIES', 'ENVELOPES', 'LFO', 'EFFECTS'];
function rollPart(base, seed, part){
  if (part === 'all') return randomVoice(seed);
  const r = randomVoice(seed), v = Uint8Array.from(base);
  const take = i => { v[i] = r[i]; };
  for (let op = 1; op <= 6; op++){
    const b = opBase(op);
    if (part === 'alg'){ take(b + 16); take(b + 15); }
    if (part === 'freq') for (let i = 17; i <= 20; i++) take(b + i);
    if (part === 'env'){ for (let i = 0; i < 8; i++) take(b + i); take(b + 13); }
    if (part === 'lfo') take(b + 14);
  }
  if (part === 'alg'){ take(P.ALG); take(P.FB); }
  if (part === 'env') for (let i = 0; i < 8; i++) take(P.PR + i);
  if (part === 'lfo') for (let i = P.LFS; i <= P.LPMS; i++) take(i);
  return v;
}
// random effect settings (function values)
function randomFx(seed){
  let st = (seed >>> 0) || 1;
  const rnd = () => { st ^= st << 13; st >>>= 0; st ^= st >>> 17; st ^= st << 5; st >>>= 0; return st / 4294967296; };
  const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
  const syncs = [0, 0, 3, 6, 8, 9];
  return {chorus:rnd() < 0.5 ? 0 : ri(20, 65), chorusRate:ri(10, 60), delayMix:rnd() < 0.4 ? 0 : ri(15, 55), delayTime:ri(120, 700), dlSync:syncs[ri(0, 5)],
    delayFb:ri(15, 70), dlHiCut:ri(20, 90), dlLoCut:ri(5, 30), dlModRate:ri(5, 40), dlModDepth:ri(0, 60), dlDuck:ri(0, 80), dlPing:ri(0, 1),
    revMix:ri(10, 70), rvProg:ri(0, 34), revSize:ri(8, 60), rvHigh:ri(3, 9), rvDelay:ri(0, 80), rvDiff:ri(4, 10), rvEr:ri(10, 80), rvCut:ri(40, 120)};
}

const api = {REV7_FULL, rev7Controls, DICE_PARTS, DICE_NAMES, rollPart, randomFx, Fx, TCDelay, Rev7, RV_PROGS, DL_DIVS, DL_DIV_NAMES, Mk1Dac, MK1, VOL_STEPS, randomVoice, DX_RATE, N, VOICES, ALGS, ALG_DEF, VPARAMS, P, FUNC_DEF, FUNC_BY_NUM, opBase, initVoice, setName, voiceName, clampVoice, packVoice, unpackVoice,
  bankToSysex, voiceToSysex, paramChange, parseSysexFile, parseSysexMessage, checksum, defaultFunc, Synth, Voice, Env, Lfo, PitchEnv, noteName, bpName, CURVES, WAVES,
  freqText, oscLogFreq, phaseInc, makeResampler, LOGSIN, EXPT, VOICE_SIZE};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
root.DX = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
