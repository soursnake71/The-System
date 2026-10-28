// Renders the same signal with engine.js and with the C++ port (plugins/twelve/Twelve.h) and compares sample by sample.
// usage: node twelve/tools/compare.mjs path/to/twelve_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import {execFileSync} from 'child_process';
const TW = createRequire(import.meta.url)('../engine.js');
const [bin, dir] = process.argv.slice(2), sr = 44100, n = sr * 3;
const L = new Float32Array(n), R = new Float32Array(n);
for (let i = 0; i < n; i++){ const t = i / sr, b = t % (60 / 96); L[i] = Math.sin(2 * Math.PI * (55 + 140 * Math.exp(-b * 25)) * b) * Math.exp(-b * 9) * .7 + Math.sin(2 * Math.PI * 440 * t) * .1; R[i] = L[i] * .8 + Math.sin(2 * Math.PI * 659.3 * t) * .08; }
const hdr = new Float32Array([n, sr, 2]);
fs.writeFileSync(dir + '/cmp.in', Buffer.concat([hdr, L, R].map(a => Buffer.from(a.buffer))));
const cases = [
  ['SP-1200 default', {}],
  ['SP-1200 -5 st, out 3, master', {semis:-5, ftype:1, cThr:-18, lsG:3, p2G:-2, hsG:4, mGain:6}],
  ['SP-12 fine tune + A4 432', {machine:1, rate:27500, cents:37, a4:432, res:.6}],
  ['MPC60 linear, stretch 1.3, lo-fi', {machine:2, rate:40000, ftype:6, cutoff:6000, stretch:1.3, crackle:.6, hiss:.4, wow:.5, flutter:.5, tape:.5, age:.5, hum:.3, mix:.6, mono:0}],
  ['S950 zoh, Mirage ladder, alias', {machine:5, rate:29411, bits:8, ftype:7, cutoff:3000, res:.5, alias:.8, drive:.7, semis:7}],
  ['Clean cubic + match EQ', {machine:6, rate:48000, bits:16, ftype:5, semis:-3, match:1, mOn:1}],
  ['SP-1200 45 rpm trick 7 st, out 6, manual parts', {rpm:7, ftype:4, calib:0}],
  ['SP-12 out 1-2 retrigger, cyclic stretch', {machine:1, rate:27500, retrig:1, stretch:1.4, smode:1, grain:40, cutoff:1300}],
  ['SP-1200 6-pole on out 7-8, 8 bits, mix 0.5', {ftype:6, cutoff:5000, bits:8, mix:.5}],
  ['MPC3000 2-pole, alias 5.4 st, hot preamp, link', {machine:7, rate:44100, bits:16, ftype:8, cutoff:6000, res:.5, rpm:5.4, input:14, link:1}],
  ['S1000 3-pole, cyclic Warp 2x, 20-sample grain', {machine:8, rate:44100, bits:16, ftype:9, cutoff:9000, res:.3, stretch:2, smode:2, grain:.5, mono:0}],
  ['Emax ladder, DSS-1 rate clamp, Classic 1.5x', {machine:11, rate:60000, bits:12, ftype:13, cutoff:3000, res:.6, stretch:1.5, smode:1, grain:30}],
  ['EPS 13-bit, SU700 4-pole, filter off, preamp off', {machine:12, rate:31250, bits:13, ftype:15, fOn:0, pre:0}],
  ['SK-1 8-bit, S-550 2-pole clamp, SP alias 9 st', {machine:10, rate:9380, bits:8, ftype:14, cutoff:15000, res:.8}],
  ['SP-12 alias 9.5 st, preamp hot, Mirage 4-pole on out 7-8', {machine:1, rate:27500, rpm:9.5, input:10, ftype:12, cutoff:4000, res:.4}],
  ['SP-1200 decay 8 on out 3', {ftype:1, decay:8}],
  ['MPC60 decay 12, -7 st', {machine:2, rate:40000, ftype:6, cutoff:9000, decay:12, semis:-7}],
  ['SP-1200 out 1-2 decay 4', {ftype:0, decay:4, semis:3}],
];
const match = [3, 2, 1, 0, -1, -2, 1, 3, 2, -4];
let fail = 0;
for (const [name, over] of cases){
  const p = Object.assign(TW.defaults(), over);
  fs.writeFileSync(dir + '/cmp.p', TW.PARAMS.map(q => p[q.id]).concat(match).join('\n'));
  const info = execFileSync(bin, [dir + '/cmp.in', dir + '/cmp.p', '48000', dir + '/cmp.out']).toString().trim();
  const b = fs.readFileSync(dir + '/cmp.out'), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4), len = f[0];
  const js = TW.renderMaster(TW.renderPre([L, R], sr, p, 48000).pre, p, match, 48000);
  let err = 0, pk = 0;
  for (let c = 0; c < 2; c++) for (let i = 0; i < len; i++){ err = Math.max(err, Math.abs(f[1 + c * len + i] - js[c][i])); pk = Math.max(pk, Math.abs(js[c][i])); }
  const ok = len === js[0].length && err < 2e-3;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: length ${len}/${js[0].length}, max diff ${err.toExponential(2)} (peak ${pk.toFixed(3)})  [C++ ${info}]`);
}
console.log('JS tempo', TW.detectTempo([L, R], sr), 'key', TW.detectKey([L, R], sr).name);
process.exitCode = fail ? 1 : 0;
