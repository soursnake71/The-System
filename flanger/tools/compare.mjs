// Renders the same signal with engine.js and with the C++ circuit model (plugins/flanger/M117.h) and compares
// sample by sample, for every preset plus some edge cases.
// usage: node flanger/tools/compare.mjs path/to/flanger_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import {execFileSync} from 'child_process';
const req = createRequire(import.meta.url);
const M = req('../engine.js');
const presets = JSON.parse(fs.readFileSync(new URL('../presets.json', import.meta.url)));
const [bin, dir] = process.argv.slice(2), sr = 44100, n = sr * 2;
const L = new Float32Array(n), R = new Float32Array(n);
// a plucked-guitar-ish chord plus noise bursts (flanging needs broadband material)
let seed = 1;
const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296 - .5;
for (let i = 0; i < n; i++){
  const t = i / sr, b = t % .5;
  let s = 0; for (const f of [110, 164.8, 220, 277.2]) s += Math.sin(2 * Math.PI * f * t) * Math.exp(-b * 3) * .15;
  L[i] = s + rnd() * .3 * Math.exp(-b * 30); R[i] = s * .9 + rnd() * .3 * Math.exp(-b * 30);
}
fs.writeFileSync(dir + '/fl.in', Buffer.concat([new Float32Array([n, 2]), L, R].map(a => Buffer.from(a.buffer))));
const cases = presets.map(p => [p.name, p.values, 44100]);
cases.push(['defaults at 96k', {}, 96000], ['bypassed', {bypass:1}, 48000], ['full regen, top of sweep', {manual:0, width:0, regen:10}, 48000],
  ['bottom of sweep, mix 100', {manual:10, width:0, mix:100}, 32000]);
const blocks = [512, 64, 1, 300, 128, 7, 1024];
let fail = 0;
for (const [name, over, fsr] of cases){
  const p = Object.assign(M.defaults(), over);
  fs.writeFileSync(dir + '/fl.p', M.PARAMS.map(q => p[q.id]).join('\n'));
  execFileSync(bin, [dir + '/fl.in', dir + '/fl.p', String(fsr), dir + '/fl.out']);
  const b = fs.readFileSync(dir + '/fl.out'), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  const e = new M.Engine(); M.PARAMS.forEach((q, i) => e.set(i, p[q.id])); e.prepare(fsr);
  const oL = new Float32Array(n), oR = new Float32Array(n);
  for (let pos = 0, k = 0; pos < n; k++){ const len = Math.min(blocks[k % 7], n - pos); e.process(L, R, oL, oR, len, pos); pos += len; }
  let err = 0, pk = 0, diff = 0;
  for (let i = 0; i < n; i++){
    err = Math.max(err, Math.abs(oL[i] - f[1 + i]), Math.abs(oR[i] - f[1 + n + i]));
    pk = Math.max(pk, Math.abs(oL[i])); diff = Math.max(diff, Math.abs(oL[i] - L[i]));
  }
  const ok = err < 1e-4 && Number.isFinite(pk) && pk < 4;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(28)} max diff ${err.toExponential(2)}  peak ${pk.toFixed(3)}  changed ${diff.toFixed(3)}`);
}
console.log(fail ? `${fail} failed` : 'all match');
process.exit(fail ? 1 : 0);
