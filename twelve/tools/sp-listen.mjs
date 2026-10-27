// Full chain against the real machine: Nolting's original kick, snare and pianos sampled and played back by the engine at
// the SP-12's lowest tuning, compared with what his SP-12 made of them (each pair a/b is one SP-12 recording and one
// Matlab model; which is which wasn't published, so both are scored). Log-spectrogram distance, level-matched.
// usage: node twelve/tools/sp-listen.mjs <dir with kick1.wav, kick1_a.wav, ...> [out wav dir]
import {createRequire} from 'module';
import fs from 'fs';
const req = createRequire(import.meta.url), SP = req('../sp1200.js'), TW = req('../engine.js'), C = req('../codecs.js');
const dir = process.argv[2], outDir = process.argv[3];
const load = f => { const b = fs.readFileSync(`${dir}/${f}`); const r = C.parseWav(b.buffer.slice(b.byteOffset, b.byteOffset + b.length)); return {x:r.chs[0], sr:r.sr}; };
const N = 1024, H = 256;
function spectro(x){
  const fr = [], re = new Float64Array(N), im = new Float64Array(N);
  for (let s = 0; s + N <= x.length; s += H){ for (let i = 0; i < N; i++){ re[i] = x[s + i] * (.5 - .5 * Math.cos(2 * Math.PI * i / N)); im[i] = 0; } TW.fft(re, im); const p = new Float64Array(N / 2); for (let i = 0; i < N / 2; i++) p[i] = re[i] ** 2 + im[i] ** 2; fr.push(p); }
  return fr;
}
function distance(a, b){   // align by the onset, match levels, compare dB above the floor (-60 dB re peak), 60 Hz-20 kHz
  const on = x => { let pk = 0; for (const v of x) pk = Math.max(pk, Math.abs(v)); for (let i = 0; i < x.length; i++) if (Math.abs(x[i]) > pk * .1) return i; return 0; };
  const A = spectro(a.subarray(on(a))), B = spectro(b.subarray(on(b))), n = Math.min(A.length, B.length, 120);
  const db = fr => fr.map(p => Array.from(p, v => 10 * Math.log10(v + 1e-20)));
  const dA = db(A.slice(0, n)), dB = db(B.slice(0, n));
  const pk = d => Math.max(...d.flat()); const off = pk(dA) - pk(dB), floor = pk(dA) - 60;
  let s = 0, c = 0;
  for (let t = 0; t < n; t++) for (let k = 2; k < 465; k++){ const x = Math.max(dA[t][k], floor), y = Math.max(dB[t][k] + off, floor); s += (x - y) ** 2; c++; }
  return Math.sqrt(s / c);
}
const results = [];
for (const name of ['kick1', 'snare1', 'piano1', 'piano2']){
  const o = load(name + '.wav'), A = load(name + '_a.wav'), B = load(name + '_b.wav');
  const codes = SP.sampleCodes(o.x, o.sr, {clock:27500});
  const line = [name.padEnd(7)];
  for (const out of [7, 6, 5, 4, 3]){
    const y = SP.playChannel(codes, {clock:27500, sp12:true, cents:+(process.env.CENTS || -700), output:out, calib:'measured'}, 44100);
    const da = distance(A.x, y), db = distance(B.x, y);
    line.push(`out${out} a ${da.toFixed(1)} b ${db.toFixed(1)}`);
    if (outDir && out === 7) fs.writeFileSync(`${outDir}/${name}_engine.wav`, C.encodeWav([y], 44100, 16));
  }
  // and the two recordings against each other, and the plain original detuned without any SP (a floor for "different")
  line.push(`| a-b ${distance(A.x, B.x).toFixed(1)}`);
  console.log(line.join('  '));
}
console.log('dB rms log-spectrogram distance (lower is closer)');
