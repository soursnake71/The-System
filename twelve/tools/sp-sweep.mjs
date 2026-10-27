// The dynamic filter after a hit, real SP-12 against the engine: where the noise spectrum is 10 dB down, frame by frame.
// usage: node twelve/tools/sp-sweep.mjs measured.json [rest] [k]
import {createRequire} from 'module';
import fs from 'fs';
const req = createRequire(import.meta.url), SP = req('../sp1200.js'), TW = req('../engine.js');
const M = JSON.parse(fs.readFileSync(process.argv[2], 'utf8')), rest = +(process.argv[3] || 925), k = +(process.argv[4] || .3), capScale = +(process.argv[5] || 1);
const fs0 = 44100, fo = fs0 * 4, secs = .7, n = Math.round(secs * fo);
// model: white noise through the envelope-swept SSM2044, at 4x, then the same 1024-point frames at 44.1 kHz
let seed = 1; const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296 - .5;
const runs = [];
for (let r = 0; r < 6; r++){
  // the noise as the SP-12 stored it: anti-alias circuit, then held at its 27.5 kHz clock
  const aa = new SP.Circuit(SP.NETS.aa, fo), clk = 27500; let held = 0, nextT = 0;
  const env = new SP.Envelope(fo, capScale), ssm = new SP.SSM2044(fo, k), trig = Math.round(.0094 * fo), y = new Float64Array(n);
  for (let i = 0; i < n; i++){
    const a = aa.process(rnd()); if (i / fo >= nextT){ held = a; nextT += 1 / clk; }
    env.gain = i < trig ? 0 : 5; env.step(); y[i] = ssm.process(held * 1e-3, Math.min(rest * 2 ** env.octaves(), fo * .45)) * 1e3;
  }
  runs.push(SP.decimate(y, Math.floor(n / 4)));
}
const spec = (x, s) => { const N = 512, re = new Float64Array(N), im = new Float64Array(N), p = new Float64Array(N / 2); for (let o = 0; o + N <= 1024; o += 256){ for (let i = 0; i < N; i++){ re[i] = (x[s + o + i] || 0) * (.5 - .5 * Math.cos(2 * Math.PI * i / N)); im[i] = 0; } TW.fft(re, im); for (let i = 0; i < N / 2; i++) p[i] += re[i] ** 2 + im[i] ** 2; } return p; };
const down10 = (fr, f) => { const lo = fr.slice(2, 6).reduce((a, b) => a + b) / 4; for (let i = 6; i < fr.length; i++) if (fr[i] < lo - 10) return f[i]; return f[fr.length - 1]; };
const T = M.t2, rows = [];
for (let j = 0; j < T.frames.length; j += 3){
  const t = j * .01, fm = T.f, p = new Float64Array(fm.length);
  for (const z of runs) for (let d = -1; d <= 1; d++){ const q = spec(z, Math.max(0, Math.round((t + d * .01) * fs0))); for (let i = 0; i < p.length; i++) p[i] += q[i]; }
  const model = fm.map((_, i) => 10 * Math.log10(p[i] + 1e-30));
  // the recording: average three neighbouring frames (power)
  const nb = [j - 1, j, j + 1].filter(x => x >= 0 && x < T.frames.length).map(x => T.frames[x]);
  const real = fm.map((_, i) => 10 * Math.log10(nb.reduce((s2, fr) => s2 + 10 ** (fr[i] / 10), 0)));
  rows.push([t, down10(real, fm), down10(model, fm)]);
}
let err = 0; for (const [, a, b] of rows) err += Math.log2(b / a) ** 2;
console.log('time    real SP-12   engine     (frequency where the noise is 10 dB down, output 2)');
for (const [t, a, b] of rows) console.log(`${t.toFixed(2)}s  ${String(Math.round(a)).padStart(7)} Hz ${String(Math.round(b)).padStart(7)} Hz`);
console.log(`rms difference ${Math.sqrt(err / rows.length).toFixed(3)} octaves (rest ${rest} Hz, feedback ${k}, C111 x${capScale})`);
