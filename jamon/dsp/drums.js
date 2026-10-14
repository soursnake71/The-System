/* Drum voice building blocks, the acoustic-drum models and the Oberheim DMX converter chain: copied from The Omega System (../index.html). */
const KSR = 44100;
function kbuf(T){ return new Float32Array(Math.max(1, Math.floor(T * KSR))); }
function knorm(d, pk = .9){ let m = 0; for (let i = 0; i < d.length; i++) m = Math.max(m, Math.abs(d[i])); const g = m ? pk / m : 1, f = Math.min(220, d.length); for (let i = 0; i < d.length; i++) d[i] *= g * (i > d.length - f ? (d.length - i) / f : 1); return d; }
let kseed = 1;
const krnd = () => ((kseed = (kseed * 16807) % 2147483647) / 2147483647) * 2 - 1;
// bridged-T style resonator: a decaying 2-pole ring whose frequency can move (pitch sweeps), struck by an impulse
function ring(d, f0, fEnd, sweepTau, decay, amp = 1, startT = 0){
  let y1 = 0, y2 = 0; const n0 = Math.floor(startT * KSR);
  for (let i = n0; i < d.length; i++){
    const t = (i - n0) / KSR, f = fEnd + (f0 - fEnd) * Math.exp(-t / sweepTau), w = 2 * Math.PI * f / KSR, r = Math.exp(-1 / (decay * KSR));
    const x = i === n0 ? amp * Math.sin(w) * 1.5 : 0, y = 2 * r * Math.cos(w) * y1 - r * r * y2 + x;
    y2 = y1; y1 = y; d[i] += y;
  }
}
// oscillator with pitch envelope (VCO-style), wave 'sin' | 'tri' | 'sq'
function vco(d, f0, fEnd, sweepTau, decay, amp = 1, wave = 'sin', attack = 0){
  let ph = 0;
  for (let i = 0; i < d.length; i++){
    const t = i / KSR, f = fEnd + (f0 - fEnd) * Math.exp(-t / sweepTau); ph += f / KSR;
    const p = ph % 1, v = wave === 'tri' ? 1 - 4 * Math.abs(p - .5) : wave === 'sq' ? (p < .5 ? 1 : -1) : Math.sin(2 * Math.PI * p);
    d[i] += v * amp * Math.exp(-t / decay) * (attack ? Math.min(1, t / attack) : 1);
  }
}
// filtered noise with an envelope function
function noise(d, envFn, lpHz, hpHz, amp = 1, bpQ = 0){
  let lp = 0, lp2 = 0, hp = 0, xp = 0, b1 = 0, b2 = 0;
  const al = 1 - Math.exp(-2 * Math.PI * lpHz / KSR), ah = Math.exp(-2 * Math.PI * hpHz / KSR);
  const w = 2 * Math.PI * lpHz / KSR, rq = bpQ ? Math.exp(-w / (2 * bpQ)) : 0;
  for (let i = 0; i < d.length; i++){
    let x = krnd();
    if (bpQ){ const y = (1 - rq) * x + 2 * rq * Math.cos(w) * b1 - rq * rq * b2; b2 = b1; b1 = y; x = y * 2; }
    else { lp += al * (x - lp); lp2 += al * (lp - lp2); x = lp2; }
    if (hpHz){ const h = ah * (hp + x - xp); xp = x; hp = h; x = h; }
    d[i] += x * amp * envFn(i / KSR);
  }
}
// metallic source: six detuned square waves (Schmitt-trigger oscillators) through a band filter
function metal(d, freqs, envFn, hpHz, amp = 1, nz = 0){
  const ph = freqs.map(() => Math.random()); let hp1 = 0, x1 = 0, hp2 = 0, x2 = 0; const a = Math.exp(-2 * Math.PI * hpHz / KSR);
  for (let i = 0; i < d.length; i++){
    let s = 0; for (let k = 0; k < freqs.length; k++){ ph[k] += freqs[k] / KSR; s += (ph[k] % 1) < .5 ? 1 : -1; }
    s = s / freqs.length + nz * krnd();
    const h1 = a * (hp1 + s - x1); x1 = s; hp1 = h1; const h2 = a * (hp2 + h1 - x2); x2 = h1; hp2 = h2;
    d[i] += h2 * amp * envFn(i / KSR);
  }
}
// modal body (acoustic drums and percussion for the sample-based machines)
function modes(d, list, drop = 0, dropTau = .02, amp = 1){
  for (const [f, a, dec] of list){ const ph0 = Math.random() * .2; let ph = ph0;
    for (let i = 0; i < d.length; i++){ const t = i / KSR; ph += f * (1 + drop * Math.exp(-t / dropTau)) / KSR; d[i] += Math.sin(2 * Math.PI * ph) * a * amp * Math.exp(-t / dec); } }
}
function sweepLP(d, rest, peak, hold, tau){
  const z = [0, 0, 0, 0];
  for (let i = 0; i < d.length; i++){ const t = i / KSR, f = t < hold ? peak : rest + (peak - rest) * Math.exp(-(t - hold) / tau), a = 1 - Math.exp(-2 * Math.PI * Math.min(f, 20000) / KSR);
    let x = d[i]; for (let k = 0; k < 4; k++){ z[k] += a * (x - z[k]); x = z[k]; } d[i] = x; }
  return d;
}
const expE = tau => t => Math.exp(-t / tau);
const lpf1 = (d, hz) => { const a = 1 - Math.exp(-2 * Math.PI * hz / KSR); let y = 0; for (let i = 0; i < d.length; i++){ y += a * (d[i] - y); d[i] = y; } return d; };
const hpf1 = (d, hz) => { const a = Math.exp(-2 * Math.PI * hz / KSR); let y = 0, xp = 0; for (let i = 0; i < d.length; i++){ y = a * (y + d[i] - xp); xp = d[i]; d[i] = y; } return d; };
const shape = (d, k) => { const n = Math.tanh(k); for (let i = 0; i < d.length; i++) d[i] = Math.tanh(d[i] * k) / n; return d; };
// sample-playback converter chain: sample-and-hold at the machine's clock, quantizer, analog output filter
const MU = (x, mu) => Math.sign(x) * Math.log1p(mu * Math.abs(x)) / Math.log1p(mu), IMU = (y, mu) => Math.sign(y) * (Math.pow(1 + mu, Math.abs(y)) - 1) / mu;
function dac(d, {rate, bits, mu = 0, lp = 0, poles = 2}){
  const q = Math.pow(2, bits - 1); let held = 0, ph = 1;
  const out = new Float32Array(d.length);
  for (let i = 0; i < d.length; i++){
    ph += rate / KSR;
    if (ph >= 1){ ph -= 1; let x = Math.max(-1, Math.min(1, d[i])); held = mu ? IMU(Math.round(MU(x, mu) * q) / q, mu) : Math.round(x * q) / q; }
    out[i] = held;
  }
  if (lp) for (let k = 0; k < poles; k++) lpf1(out, lp);
  return out;
}

/* ---- acoustic sources for the sample-playback machines (LinnDrum, DMX, TR-505) ----
   These are not the original ROM recordings; they are modal/noise models of the same kinds of studio drums,
   then played through each machine's own converter and filter chain. o = per-machine character */
function acoustic(kind, o = {}){
  const tune = o.tune || 1, room = o.room || 0; let d;
  switch (kind){
    case 'kick': d = kbuf(.6); modes(d, [[58 * tune, 1, .16], [97 * tune, .35, .07], [151 * tune, .15, .04]], .9, .012);
      noise(d, expE(.004), 3500, 300, .45 * (o.beater || 1)); vco(d, 1800, 1800, 1, .0015, .3); shape(d, 1.2); break;
    case 'snare': d = kbuf(.5); modes(d, [[185 * tune, .8, .05], [330 * tune, .45, .035], [470 * tune, .25, .025]], .25, .006);
      noise(d, t => Math.exp(-t / (.11 + room * .1)), 9000, 1200, .8); break;
    case 'snare2': d = kbuf(.4); modes(d, [[215 * tune, .7, .04], [390 * tune, .4, .03]], .3, .005); noise(d, expE(.07), 7000, 1800, .9); shape(d, 1.6); break;
    case 'side': d = kbuf(.15); modes(d, [[420 * tune, .8, .025], [1250 * tune, .5, .012], [2300, .25, .008]]); noise(d, expE(.003), 8000, 1500, .3); break;
    case 'chh': case 'ohh': { d = kbuf(kind === 'chh' ? .14 : .8); const dec = kind === 'chh' ? .035 : .35;
      const parts = [3150, 3920, 4710, 5480, 6330, 7120, 8270, 9450, 10900].map((f, k) => [f * (1 + .013 * k), .3, dec * (1 - k * .05)]);
      modes(d, parts); noise(d, expE(dec * .8), 13000, 6000, 1.1); break; }
    case 'ride': d = kbuf(2); modes(d, [[365, .18, 1.2], [2970, .35, .9], [4120, .3, .8], [5460, .22, .6], [6810, .16, .5]]); noise(d, expE(.6), 12000, 5000, .35); noise(d, expE(.01), 9000, 3000, .6); break;
    case 'crash': d = kbuf(2.4); modes(d, [[440, .15, .8], [1210, .2, .9], [2650, .25, 1], [3980, .25, .9], [5310, .2, .8]]); noise(d, t => Math.exp(-t / .75) * Math.min(1, t / .004), 14000, 2800, 1.3); break;
    case 'tom': { d = kbuf(.9); const f = (o.f || 120) * tune; modes(d, [[f, 1, .28], [f * 1.59, .35, .14], [f * 2.14, .15, .08]], .18, .03); noise(d, expE(.006), 4000, 200, .3); break; }
    case 'conga': { d = kbuf(.45); const f = (o.f || 330) * tune; modes(d, [[f, 1, .14], [f * 1.5, .3, .06], [f * 2.3, .15, .03]], .08, .01); noise(d, expE(.002), 6000, 1000, .3); break; }
    case 'timbale': { d = kbuf(.6); const f = (o.f || 420) * tune; modes(d, [[f, 1, .2], [f * 1.53, .5, .14], [f * 2.4, .3, .1], [f * 3.6, .15, .07]]); noise(d, expE(.004), 9000, 2000, .4); break; }
    case 'cowbell': { d = kbuf(.4); const f = (o.f || 560) * tune; modes(d, [[f, .9, .09], [f * 1.505, .7, .07], [f * 2.52, .25, .04]]); noise(d, expE(.003), 5000, 1500, .2); break; }
    case 'clap': d = kbuf(.4); noise(d, t => (t < .03 ? Math.exp(-(t % .0095) / .003) : 0) + (t > .028 ? .7 * Math.exp(-(t - .028) / (.06 + room * .1)) : 0), 1200, 300, 1, 1.4); noise(d, t => t > .028 ? .25 * Math.exp(-(t - .028) / .05) : 0, 6000, 2000, 1); break;
    case 'tamb': d = kbuf(.45); for (let k = 0; k < 3; k++) modes(d, [[6200 + k * 700, .15, .09], [8700 + k * 450, .12, .07]]); noise(d, t => Math.exp(-t / .1) * (1 + .6 * Math.sin(t * 2 * Math.PI * 24)), 14000, 5500, .9); break;
    case 'cabasa': d = kbuf(.3); noise(d, t => Math.min(1, t / .015) * Math.exp(-t / .06), 12000, 4500, 1); break;
    case 'shaker': d = kbuf(.25); noise(d, t => Math.min(1, t / .02) * Math.exp(-t / .05), 10000, 3500, 1); break;
    case 'claves': d = kbuf(.2); modes(d, [[2480 * tune, 1, .05], [6600, .1, .01]]); break;
  }
  return d;
}
// LinnDrum LM-2: 8-bit samples through a companding (mu-law style) DAC, voice clock ~ 28 kHz, per-voice output filters
// Oberheim DMX: 8-bit companded samples, ~ 25 kHz clock, fixed filters; punchier, shorter recordings
function dmx(v){
  kseed = 3000 + v.charCodeAt(0) * 11 + v.length;
  const map = {bd:['kick', {tune:.95, beater:1.6}], sd:['snare2', {tune:1}], sd2:['snare', {tune:1.1, room:.6}], rim:['side', {tune:1.1}], chh:['chh', {}], ohh:['ohh', {}], hha:['chh', {}],
    cym:['crash', {}], t1:['tom', {f:85}], t2:['tom', {f:110}], t3:['tom', {f:145}], t4:['tom', {f:190}], clap:['clap', {room:.2}], tamb:['tamb', {}], shk:['shaker', {}], ride:['ride', {}]};
  const [k, o] = map[v]; let d = knorm(acoustic(k, o), 1);
  if (v === 'hha') for (let i = 0; i < d.length; i++) d[i] *= 1.4;
  const low = /^(bd|t\d|clap|shk)$/.test(v);
  d = dac(d, {rate:low ? 30000 : 45000, bits:8, mu:255, lp:low ? 10000 : 16000, poles:6});
  return {d:knorm(d), sr:KSR};
}
// TR-505: compact PCM (low bit depth, linear), fixed rate, bright band-limited sound

/* ================= Roland TR-808 (1980), all analog =================
   Rendered per hit from the panel settings (cached), following published analyses of the circuit
   (Werner, Abel & Smith, DAFx-14, for the bass drum; Sound On Sound "Synth Secrets"; the 808 service notes' block diagram):
   BD: bridged-T band-pass (~49.5 Hz) struck by the trigger pulse, whose retriggering lifts the pitch for the first few ms;
       DECAY sets the feedback (ring time), TONE the low-pass on the click.  SD: two bridged-T resonators (~238 and ~476 Hz),
       TONE balances them, SNAPPY adds the high-passed noise burst.  LT/MT/HT and LC/MC/HC: bridged-T with a small pitch
       drop (toms add a little filtered noise), TUNING sets the pitch.  RS: two bridged-T rings (~455 and ~1667 Hz) through
       a very short VCA.  CL: ~2.5 kHz ring.  CP: noise through a ~1 kHz band-pass, three sawtooth envelope retriggers
       then the "reverb" tail.  MA: high-passed noise.  CB: two of the square oscillators (540 + 800 Hz) through a band-pass
       with a two-stage decay.  CH / OH / CY: six square oscillators (205.3, 304.4, 369.6, 522.7, 540, 800 Hz) summed into
       band-passes (~3.44 and ~7.1 kHz) and high-passes; CY TONE balances the bands, OH and CY have DECAY.
   ACCENT: the common accent level raises the trigger voltage, so accented hits are louder and drive harder.
   The frequencies are the commonly published ones; component tolerances of a given 808 move them a little. */
function bp2(d, f, Q, gain = 1){
  const w = 2 * Math.PI * f / KSR, al = Math.sin(w) / (2 * Q), a0 = 1 + al, b0 = al / a0, a1 = -2 * Math.cos(w) / a0, a2 = (1 - al) / a0;
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let i = 0; i < d.length; i++){ const x = d[i], y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; d[i] = y * gain; }
  return d;
}
const SQ808 = [205.3, 304.4, 369.6, 522.7, 540, 800];
function sq6(n){ const d = new Float32Array(n), ph = SQ808.map(() => Math.random()); for (let i = 0; i < n; i++){ let s = 0; for (let k = 0; k < 6; k++){ ph[k] += SQ808[k] / KSR; s += (ph[k] % 1) < .5 ? 1 : -1; } d[i] = s / 6; } return d; }
// p: {tone, decay, snappy, tune} 0..1 as on the panel; acc 0..1 accent amount on this hit
function tr808(v, p = {}, acc = 0){
  kseed = 808 + v.charCodeAt(0) * 23 + v.length;
  const tone = p.tone == null ? .5 : p.tone, dec = p.decay == null ? .5 : p.decay, tune = p.tune == null ? .5 : p.tune, sn = p.snappy == null ? .5 : p.snappy;
  const A = 1 + acc * .5; let d;
  const tom = (lo, hi, ring0, noiseAmt) => { const f = lo + (hi - lo) * tune; d = kbuf(ring0 * 6); ring(d, f * 1.12, f, .025, ring0, 1);
    if (noiseAmt) noise(d, t => Math.exp(-t / (ring0 * .45)), 2800, 300, noiseAmt * .05); };
  switch (v){
    case 'bd': {
      const tau = .05 + dec * dec * 1.1; d = kbuf(Math.min(4, tau * 6 + .05));
      ring(d, 49.5 * 1.5, 49.5, .0035, tau, 1);
      const click = kbuf(.02); vco(click, 1, 1, 1, .0008, .9, 'sq'); lpf1(lpf1(click, 180 * Math.pow(30, tone)), 180 * Math.pow(30, tone));
      for (let i = 0; i < click.length; i++) d[i] += click[i] * (.25 + tone * .6);
      shape(d, 1 + acc * .8); hpf1(d, 18); break; }
    case 'sd': {
      d = kbuf(.6); ring(d, 238 * 1.03, 238, .01, .07, (1 - tone) * .8 + .25); ring(d, 476 * 1.03, 476, .01, .045, tone * .7 + .15);
      const nz = kbuf(.6); noise(nz, t => Math.exp(-t / (.06 + sn * .12)), 9000, 1800, 1); for (let i = 0; i < d.length; i++) d[i] += nz[i] * (.08 + sn * .85);
      shape(d, 1.1 + acc * .6); break; }
    case 'lt': tom(80, 100, .2, 1); break;
    case 'mt': tom(120, 160, .16, 1); break;
    case 'ht': tom(165, 220, .13, 1); break;
    case 'lc': tom(165, 220, .12, 0); break;
    case 'mc': tom(250, 310, .1, 0); break;
    case 'hc': tom(370, 455, .08, 0); break;
    case 'rs': d = kbuf(.06); ring(d, 455, 455, 1, .012, .9); ring(d, 1667, 1667, 1, .008, .9); for (let i = 0; i < d.length; i++) d[i] *= Math.exp(-i / KSR / .006) * 1.3; hpf1(d, 250); shape(d, 1.6); break;
    case 'cl': d = kbuf(.12); ring(d, 2500, 2500, 1, .022, 1); break;
    case 'cp': d = kbuf(.6); noise(d, t => (t < .03 ? (1 - (t % .01) / .01) * Math.exp(-(t % .01) / .004) : 0) + (t >= .03 ? .55 * Math.exp(-(t - .03) / .11) : 0), 1000, 0, 1, 1.6); hpf1(d, 300); break;
    case 'ma': d = kbuf(.08); noise(d, t => Math.min(1, t / .004) * Math.exp(-t / .022), 14000, 5000, 1); break;
    case 'cb': {
      d = kbuf(.6); let a = 0, b = 0;
      for (let i = 0; i < d.length; i++){ const t = i / KSR; a += 540 / KSR; b += 800 / KSR; d[i] = ((a % 1 < .5 ? 1 : -1) + (b % 1 < .5 ? 1 : -1)) * .5 * (t < .005 ? 1 : .55 * Math.exp(-(t - .005) / .055) + .45 * Math.exp(-(t - .005) / .004)); }
      bp2(d, 2640, .9); lpf1(d, 7000); break; }
    case 'ch': case 'oh': {
      const T = v === 'ch' ? .25 : (.15 + dec * .9) * 3; d = sq6(Math.floor(T * KSR)); bp2(d, 7100, 1.2); hpf1(hpf1(d, 6000), 6000);
      const tau = v === 'ch' ? .035 : .08 + dec * dec * .55; for (let i = 0; i < d.length; i++) d[i] *= Math.exp(-i / KSR / tau) * (i < 40 ? i / 40 : 1);
      break; }
    case 'cy': {
      const tau = .25 + dec * 1.3; d = sq6(Math.floor(tau * 5 * KSR)); const lo = d.slice(), hi = d;
      bp2(lo, 3440, 1.5); bp2(hi, 7100, 1.2); hpf1(hpf1(hi, 5000), 5000);
      for (let i = 0; i < d.length; i++){ const t = i / KSR; d[i] = lo[i] * (1 - tone) * 1.3 * Math.exp(-t / tau) + hi[i] * (.5 + tone) * Math.exp(-t / (tau * .5)); }
      hpf1(d, 800); break; }
    default: d = kbuf(.01);
  }
  return {d:knorm(d, .9 * Math.min(1, .78 * A)), sr:KSR};
}
