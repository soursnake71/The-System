// Fits the engine's part tolerances to a real machine: Nolting's SP-12 with noise through each output.
// usage: node twelve/tools/sp-fit.mjs measured.json   (made by twelve/tools/sp-measure.py from noise_20k_all_ch.wav)
import {createRequire} from 'module';
import fs from 'fs';
const req = createRequire(import.meta.url), SP = req('../sp1200.js'), TW = req('../engine.js');
const M = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')), fo = 176400, N = 1 << 16;
const at = (f, ys, x) => { for (let i = 1; i < f.length; i++) if (f[i] >= x){ const t = (x - f[i - 1]) / (f[i] - f[i - 1]); return ys[i - 1] + t * (ys[i] - ys[i - 1]); } return NaN; };
const response = make => { const re = new Float64Array(N), im = new Float64Array(N), c = make(); for (let i = 0; i < N; i++) re[i] = c(i ? 0 : 1); TW.fft(re, im); return f => 20 * Math.log10(Math.hypot(re[Math.round(f * N / fo)], im[Math.round(f * N / fo)]) + 1e-20); };
const band = []; for (let f = 2000; f <= 15000; f *= 1.05) band.push(f);
const fit = {};
for (const out of [3, 4, 5, 6]){
  let best = null;
  for (let sc = .80; sc <= 1.08; sc += .005){
    const H = response(() => { const c = new SP.Circuit(SP.NETS['out' + out], fo, sc); return x => c.process(x); }), h0 = H(200);
    const err = Math.sqrt(band.reduce((s, f) => s + (H(f) - h0 - at(M.f, M[out], f)) ** 2, 0) / band.length);
    if (!best || err < best.err) best = {sc, err};
  }
  const H1 = response(() => { const c = new SP.Circuit(SP.NETS['out' + out], fo, 1); return x => c.process(x); }), h1 = H1(200);
  const err1 = Math.sqrt(band.reduce((s, f) => s + (H1(f) - h1 - at(M.f, M[out], f)) ** 2, 0) / band.length);
  fit[out] = best.sc;
  console.log(`out${out}: as drawn ${err1.toFixed(2)} dB rms off the real machine; capacitors x${best.sc.toFixed(3)} -> ${best.err.toFixed(2)} dB rms (cutoff ${((1 / best.sc - 1) * 100).toFixed(1)} % higher)`);
}
// dynamic filter at rest (last frames, the filter has closed): fit the rest cutoff and the feedback
for (const out of [2, 1]){
  const T = M['t' + out], late = T.frames.slice(-15), avg = T.f.map((_, i) => late.reduce((s, fr) => s + fr[i], 0) / late.length);
  const ref = at(T.f, avg, 200), meas = f => at(T.f, avg, f) - ref, pts = []; for (let f = 400; f <= 6000; f *= 1.08) pts.push(f);
  let best = null;
  for (let rest = 700; rest <= 2200; rest += 25) for (const k of [0, .05, .1, .15, .2, .3, .4, .5]){
    const H = response(() => { const s = new SP.SSM2044(fo, k); return x => s.process(x * 1e-3, rest) * 1e3; }), h0 = H(200);
    const err = Math.sqrt(pts.reduce((s, f) => s + (H(f) - h0 - meas(f)) ** 2, 0) / pts.length);
    if (!best || err < best.err) best = {rest, k, err};
  }
  fit['rest' + out] = best.rest; fit['k' + out] = best.k;
  console.log(`out${out} at rest: cutoff ${best.rest} Hz, feedback ${best.k} -> ${best.err.toFixed(2)} dB rms`);
}
console.log(JSON.stringify(fit));
