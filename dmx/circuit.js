/* Omega DMX circuit model: one voice card of the DMX, from the schematics as documented in MAME's oberheim/dmx.cpp
   (with sound/dac76, flt_biquad, va_eg and machine/rescap). plugins/dmx/Circuit.h is a line-for-line C++ port;
   dmx/tools/compare.mjs checks they render the same samples.

   A voice card: a 555 (not astable) clocks a counter through the sample ROM; each 8-bit code goes to an AM6070 / DAC76
   companding DAC (sign, 3-bit chord, 4-bit step) whose reference current sets the level: three trigger modes (TR0, TR1,
   both) pick it through R8 / R9 / R12 / R17 and a diode, and on cards with decay an RC envelope (C3) discharges it once the
   counter's bit 6 (early) or bit 10 (late) goes high. The DAC's held output (a zero-order hold at the clock) goes through
   three unity-gain Sallen-Key low-passes (LOW ~10 kHz or HIGH ~16 kHz sets). The board mixes the cards to left and right
   through per-card resistors into 27k summers, each card through an audio-taper fader.

   Here a sound from a kit is first written into the card's ROM (resampled to the card's own clock at the trimmer's centre,
   peak-normalised and encoded to the nearest DAC76 code, cut to the ROM's size), then played through the card. */
'use strict';
const DMXC = (() => {
  const VCC = 5, R3 = 1e3, R4 = 10e3, T1_MAX = 10e3, R5 = 3.3e3, R_555 = 5e3, VD_PITCH = .48, VD = .6, R8 = 2.7e3, R9 = 5.6e3;
  const R_FB = 27e3, MIX_NORM = 27e3 / 10e3;            // the summers' 27k feedback; normalised so the bass card (10k / 10k) is 1 a side
  const LEVEL = [0, 33, 99, 231, 495, 1023, 2079, 4191], FULL = 8031;
  // cards: c2 pitch cap, c3 decay cap, r12 / r17 trigger resistors, decay (on / tr12 / off), early decay (on / tr1), filter, ROM size (samples)
  const CARDS = {
    bass:  {c2:3.3e-9, c3:3.3e-6, r12:750,  r17:47,    pitch:false, decay:'on',   early:'on',  filter:'low',  rom:4096,  mix:[10e3, 10e3]},
    snare: {c2:2.2e-9, c3:0,      r12:1500, r17:15,    pitch:false, decay:'off',  early:'tr1', filter:'high', rom:4096,  mix:[8.2e3, 20e3]},
    hat:   {c2:3.3e-9, c3:2.2e-6, r12:6800, r17:100e6, pitch:false, decay:'tr12', early:'tr1', filter:'high', rom:4096,  mix:[20e3, 8.2e3]},
    stom:  {c2:3.3e-9, c3:6.8e-6, r12:33e3, r17:18e3,  pitch:true,  decay:'on',   early:'on',  filter:'low',  rom:4096,  mix:[6.8e3, 100e3]},
    ltom:  {c2:3.3e-9, c3:6.8e-6, r12:33e3, r17:18e3,  pitch:true,  decay:'on',   early:'on',  filter:'low',  rom:4096,  mix:[100e3, 6.8e3]},
    perc1: {c2:3.3e-9, c3:0,      r12:1500, r17:30,    pitch:false, decay:'off',  early:'tr1', filter:'high', rom:2048,  mix:[6.8e3, 100e3]},
    perc2: {c2:4.7e-9, c3:0,      r12:1500, r17:30,    pitch:false, decay:'off',  early:'tr1', filter:'low',  rom:2048,  mix:[100e3, 6.8e3]},
    cym:   {c2:3.3e-9, c3:0,      r12:680,  r17:100,   pitch:false, decay:'off',  early:'tr1', filter:'high', rom:16384, mix:[8.2e3, 20e3]}};
  // Sallen-Key stages [R1, R2, C1 (feedback), C2 (to ground)]
  const FILTERS = {low:[[6.8e3, 6.2e3, 10e-9, 4.7e-9], [9.1e3, 9.1e3, 10e-9, 560e-12], [5.1e3, 5.1e3, 47e-9, 200e-12]],
                   high:[[4.7e3, 4.7e3, 10e-9, 4.7e-9], [5.6e3, 5.6e3, 10e-9, 560e-12], [3.3e3, 3.3e3, 47e-9, 200e-12]]};
  const par = (...r) => 1 / r.reduce((s, x) => s + 1 / x, 0);
  const potLaw = x => (Math.pow(81, x) - 1) / 80;      // RES_AUDIO_POT_LAW: audio taper, 10% at the midpoint

  // the 555's control voltage for a trigger mode (1 = TR0, 2 = TR1, 3 = both); pitch cards (toms) shift it, the others sit at 2/3 VCC
  function cv(card, mode){
    if (!card.pitch || mode === 3) return VCC * 2 / 3;
    if (mode === 1){ const a = 1 + card.r12 / card.r17; return (a * R5 + card.r12) * (2 * VCC - 3 * VD_PITCH) / (3 * a * R5 + 3 * card.r12 + 2 * a * R_555) + VD_PITCH; }
    return (R5 + card.r12) * (2 * VCC - 3 * VD_PITCH) / (3 * R5 + 3 * card.r12 + 2 * R_555) + VD_PITCH;
  }
  // sample clock: tune 0..100 (50 = the trimmer's centre; higher = faster)
  function rate(card, mode, tune){
    const trim = 100 - tune, rc = trim * T1_MAX / 100 + R4, v = cv(card, mode);
    const rcCharge = rc * card.c2, rcDis = par(rc, R3) * card.c2, ve = VCC * (R3 / (rc + R3));
    const tHigh = -rcCharge * Math.log((VCC - v) / (VCC - v / 2)), tLow = -rcDis * Math.log((ve - v / 2) / (ve - v));
    return 1 / (tHigh + tLow);
  }
  // the DAC's reference current for a trigger mode, as a fraction of full scale
  function iref(card, mode){
    const max = VCC / (R8 + R9);
    if (card.pitch || mode === 3) return 1;
    const a = card.r12, b = card.r17;
    if (mode === 1) return (a * b * VCC + R8 * a * VD + R8 * b * VD) / (R8 * a * b + a * b * R9 + R8 * b * R9 + R8 * a * R9) / max;
    return (a * VCC + R8 * VD) / (a * R8 + R8 * R9 + a * R9) / max;
  }
  // decay: [time constant in s, counter value it starts at] or null
  function decay(card, mode){
    const on = card.decay === 'on' || (card.decay === 'tr12' && mode !== 3); if (!on || !card.c3) return null;
    const r = card.pitch || mode === 3 ? R8 + R9 : mode === 1 ? R8 + par(R9, card.r12, card.r17) : R8 + par(R9, card.r12);
    const early = card.early === 'on' || (card.early === 'tr1' && mode === 1);
    return [r * card.c3, early ? 64 : 1024];
  }
  // DAC76 decode of an 8-bit code (bit 7 sign, 6..4 chord, 3..0 step) to [-1, 1]
  function decode(code){
    const chord = (code >> 4) & 7, step = code & 15, v = (LEVEL[chord] + step * (2 << chord)) / FULL;
    return code & 128 ? v : -v;
  }
  const MAG = Array.from({length:128}, (_, c) => (LEVEL[c >> 4] + (c & 15) * (2 << (c >> 4))) / FULL);   // magnitudes, ascending
  function encode(x){
    const m = Math.min(1, Math.abs(x)); let lo = 0, hi = 127;
    while (hi - lo > 1){ const mid = (lo + hi) >> 1; if (MAG[mid] <= m) lo = mid; else hi = mid; }
    const c = m - MAG[lo] <= MAG[hi] - m ? lo : hi;
    return (x >= 0 ? 128 : 0) | c;
  }
  // windowed-sinc resampler (Blackman, 16 zero crossings a side), with the cutoff at the lower Nyquist
  function resample(x, from, to){
    const n = Math.floor(x.length * to / from), out = new Float64Array(n), ratio = from / to, fc = Math.min(1, to / from), W = 16;
    for (let i = 0; i < n; i++){
      const c = i * ratio, k0 = Math.floor(c - W / fc), k1 = Math.ceil(c + W / fc); let s = 0;
      for (let k = Math.max(0, k0); k <= Math.min(x.length - 1, k1); k++){
        const d = (k - c) * fc, w = Math.abs(d) >= W ? 0 : .42 + .5 * Math.cos(Math.PI * d / W) + .08 * Math.cos(2 * Math.PI * d / W);
        s += x[k] * fc * (d === 0 ? 1 : Math.sin(Math.PI * d) / (Math.PI * d)) * w;
      }
      out[i] = s;
    }
    return out;
  }
  // the ROM: the sound at the card's own clock (trimmer centred, its full-level mode), peak-normalised, as DAC76 codes
  function makeRom(x, sr, card){
    const y = resample(x, sr, rate(card, 3, 50)); let pk = 0; for (const v of y) pk = Math.max(pk, Math.abs(v));
    const n = Math.min(card.rom, y.length), rom = new Uint8Array(card.rom);
    for (let i = 0; i < n; i++) rom[i] = encode(pk > 0 ? y[i] / pk : 0);
    for (let i = n; i < card.rom; i++) rom[i] = 0;
    let last = n; while (last > 0 && (rom[last - 1] & 127) === 0) last--;
    return {rom, len:last};
  }
  // MAME's biquad: bilinear transform of a unity-gain Sallen-Key low-pass
  function skCoefs([r1, r2, c1, c2], fs){
    const fc = 1 / (2 * Math.PI * Math.sqrt(r1 * r2 * c1 * c2)), q = Math.sqrt(r1 * r2 * c1 * c2) / (r1 * c2 + r2 * c2);
    if (fc >= fs / 2) return null;
    const K = Math.tan(Math.PI * fc / fs), nrm = 1 / (1 + K / q + K * K), b0 = K * K * nrm;
    return [b0, 2 * b0, b0, 2 * (K * K - 1) * nrm, (1 - K / q + K * K) * nrm];
  }
  const OS = 4, TAPS = 63;
  const decim = (() => {   // the decimator's low-pass: windowed sinc at 0.45 of the output rate
    const h = new Float64Array(TAPS), fc = .45 / OS * 2; let s = 0;
    for (let i = 0; i < TAPS; i++){ const d = i - (TAPS - 1) / 2, w = .42 - .5 * Math.cos(2 * Math.PI * i / (TAPS - 1)) + .08 * Math.cos(4 * Math.PI * i / (TAPS - 1));
      h[i] = (d === 0 ? fc : Math.sin(Math.PI * fc * d) / (Math.PI * d)) * w; s += h[i]; }
    for (let i = 0; i < TAPS; i++) h[i] /= s; return h;
  })();
  // play a ROM through the card: tune 0..100, trigger mode 1..3; returns the card's output at fs (1 = full-scale DAC)
  function render(R, card, mode, tune, fs){
    const f = rate(card, mode, tune), F = fs * OS, I0 = iref(card, mode), dec = decay(card, mode);
    const tEnd = R.len / f, n = Math.ceil((tEnd + .01) * F) + TAPS, x = new Float64Array(n);
    const val = k => k < R.len ? decode(R.rom[k]) : 0;
    for (let i = 0; i < n; i++){
      const t0 = i / F, t1 = (i + 1) / F, k0 = Math.floor(t0 * f), k1 = Math.floor(t1 * f); let v;
      if (k0 === k1) v = val(k0);
      else { v = val(k0) * ((k0 + 1) / f - t0); for (let k = k0 + 1; k < k1; k++) v += val(k) / f; v += val(k1) * (t1 - k1 / f); v *= F; }
      let g = I0; const tm = (i + .5) / F;
      if (dec){ const td = dec[1] / f; if (tm > td) g *= Math.exp(-(tm - td) / dec[0]); }
      x[i] = v * g;
    }
    for (const st of FILTERS[card.filter]){
      const c = skCoefs(st, F); if (!c) continue; let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      for (let i = 0; i < n; i++){ const y = c[0] * x[i] + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2; x2 = x1; x1 = x[i]; y2 = y1; y1 = y; x[i] = y; }
    }
    const m = Math.floor((n - TAPS) / OS), out = new Float32Array(m);
    for (let j = 0; j < m; j++){ let s = 0; const b = j * OS; for (let k = 0; k < TAPS; k++) s += decim[k] * x[b + k]; out[j] = s; }
    return out;
  }
  // which card and trigger mode a kit voice gets: by fader group, toms split small / large, hats by kind
  function cardFor(kit, vi){
    const v = kit.voices[vi], g = v[2];
    if (g === 'toms'){ const toms = kit.voices.map((x, i) => [x, i]).filter(([x]) => x[2] === 'toms'), at = toms.findIndex(([, i]) => i === vi); return ['toms', at < Math.ceil(toms.length / 2) ? 'stom' : 'ltom', 3]; }
    if (g === 'hat'){ const s = (v[0] + ' ' + v[1]).toLowerCase(); return ['hat', 'hat', /open/.test(s) ? 3 : /half|medium/.test(s) ? 2 : 1]; }
    return [g, g, 3];
  }
  // the board: fader (audio taper) times the card's left / right mix resistor into the 27k summers
  const MIX_K = .8 / potLaw(.8);                       // so a fader at .8 sits where the clean engine's does
  function mixGains(card, fader){ const v = potLaw(fader) * MIX_K; return [v * R_FB / card.mix[0] / MIX_NORM, v * R_FB / card.mix[1] / MIX_NORM]; }
  const MET_MIX = 10e3, MET_V = [2.5 / (VCC / (R8 + R9) * 3.8 * 2400), 1.25 / (VCC / (R8 + R9) * 3.8 * 2400)];   // the metronome's ~2.5 V / ~1.25 V, 1 ms pulse
  // 8 / 16 / 24-bit PCM or 32-bit float WAV to mono doubles at the file's own rate (the plugin reads its kits the same way)
  function parseWav(buf){
    const dv = new DataView(buf instanceof ArrayBuffer ? buf : buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
    const str = (o, n) => String.fromCharCode(...Array.from({length:n}, (_, i) => dv.getUint8(o + i)));
    if (str(0, 4) !== 'RIFF' || str(8, 4) !== 'WAVE') return null;
    let tag = 0, ch = 0, sr = 0, bits = 0, at = -1, len = 0;
    for (let i = 12; i + 8 <= dv.byteLength;){ const id = str(i, 4), sz = dv.getUint32(i + 4, true);
      if (id === 'fmt '){ tag = dv.getUint16(i + 8, true); ch = dv.getUint16(i + 10, true); sr = dv.getUint32(i + 12, true); bits = dv.getUint16(i + 22, true); if (tag === 0xFFFE && sz >= 40) tag = dv.getUint16(i + 32, true); }
      else if (id === 'data'){ at = i + 8; len = Math.min(sz, dv.byteLength - at); }
      i += 8 + sz + (sz & 1); }
    if (at < 0 || !ch || !sr || !(tag === 1 || tag === 3)) return null;
    const bps = bits / 8, frames = Math.floor(len / (bps * ch)), x = new Float64Array(frames);
    for (let f = 0; f < frames; f++){ let s = 0;
      for (let c = 0; c < ch; c++){ const o = at + (f * ch + c) * bps;
        s += tag === 3 && bits === 32 ? dv.getFloat32(o, true) : bits === 16 ? dv.getInt16(o, true) / 32768 : bits === 24 ? ((dv.getUint8(o) << 8 | dv.getUint8(o + 1) << 16 | dv.getUint8(o + 2) << 24) >> 8) / 8388608 : bits === 8 ? (dv.getUint8(o) - 128) / 128 : 0; }
      x[f] = s / ch; }
    return {x, sr};
  }
  // the card's place in the stereo mix (fader full): its left / right resistors into the 27k summers
  const pan = card => [R_FB / card.mix[0] / MIX_NORM, R_FB / card.mix[1] / MIX_NORM];
  return {CARDS, FILTERS, OS, parseWav, pan, rate, iref, decay, decode, encode, makeRom, render, cardFor, potLaw, mixGains, MIX_K, MET_V, MET_MIX, resample};
})();
if (typeof module !== 'undefined') module.exports = DMXC;
