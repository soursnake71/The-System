// Omega 808 engine tests: node eight/tools/test.mjs <outdir>   (renders every voice to WAV and checks it)
import {createRequire} from 'module';
import fs from 'fs';
import path from 'path';
const require = createRequire(import.meta.url);
const dir = path.dirname(new URL(import.meta.url).pathname);
const {Rev7, TC2290} = require(path.join(dir, '../fx.js'));
const {Seq808} = require(path.join(dir, '../seq.js'));
const {Eight808} = require(path.join(dir, '../engine.js'));
const out = process.argv[2] || 'eight-wav';
fs.mkdirSync(out, {recursive: true});
const SR = 48000;
let fails = 0;
const ok = (c, msg) => { console.log((c ? 'ok   ' : 'FAIL ') + msg); if (!c) fails++; };
function wav(file, L, R) {
  const n = L.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22);
  b.writeUInt32LE(SR, 24); b.writeUInt32LE(SR * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, L[i])) * 32767), 44 + i * 4); b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, R[i])) * 32767), 46 + i * 4); }
  fs.writeFileSync(path.join(out, file), b);
}
function render(v, acc, secs = 1.5, setup) {
  const e = new Eight808(SR);
  e.set('mod.hiss', 0);
  for (const k of Eight808.SEND_VOICES) { e.set('rev.send.' + k, 0); e.set('dly.send.' + k, 0); }
  if (setup) setup(e);
  const n = Math.round(SR * secs), L = new Float32Array(n), R = new Float32Array(n);
  e.trigger(v, acc, 1);
  for (let o = 0; o < n; o += 128) e.process(L, R, Math.min(128, n - o), o);
  return L;
}
const peak = a => { let p = 0; for (const x of a) p = Math.max(p, Math.abs(x)); return p; };
const finite = a => a.every(Number.isFinite);
const dur = a => { const p = peak(a), th = p * 0.01; let last = 0; for (let i = 0; i < a.length; i++) if (Math.abs(a[i]) > th) last = i; return last / SR; };
const trig = e => { e.set('mod.TRIG.level', 0.8); };
for (const v of Eight808.VOICES) {
  const s = v === 'TRIG' ? trig : null;
  const a = render(v, 0, 1.5, s), b = render(v, 1, 1.5, s);
  wav(v + '.wav', a, a); wav(v + '-accent.wav', b, b);
  ok(finite(a) && finite(b), v + ' finite');
  ok(peak(a) > 0.05 && peak(a) <= 1, v + ' peak ' + peak(a).toFixed(3));
  ok(peak(b) > peak(a), v + ' accent louder ' + peak(b).toFixed(3));
}
{ // BD pitch from zero crossings 60..300 ms
  const a = render('BD', 0); let z = 0; for (let i = Math.round(SR * 0.06); i < SR * 0.3; i++) if (a[i - 1] < 0 && a[i] >= 0) z++;
  const f = z / 0.24; ok(f >= 45 && f <= 65, 'BD fundamental ' + f.toFixed(1) + ' Hz');
}
ok(dur(render('CH', 0)) < dur(render('OH', 0)), 'CH shorter than OH (' + dur(render('CH', 0)).toFixed(3) + ' < ' + dur(render('OH', 0)).toFixed(3) + ')');
ok(dur(render('TRIG', 0, 1, trig)) < 0.06, 'TRIG click short (' + dur(render('TRIG', 0, 1, trig)).toFixed(3) + ' s)');
{ // seq: 16 steps per bar of 2 s at 120 BPM
  const s = new Seq808(SR); s.setTempo(120); s.play(true); let c = 0;
  for (let i = 0; i < SR * 2 - 1; i += 128) s.tick(Math.min(128, SR * 2 - 1 - i), () => c++);
  ok(c === 16, 'seq steps per bar at 120 BPM: ' + c);
}
{ // FX: output and decay
  const r = new Rev7(SR), n = SR * 4, x = new Float32Array(n), L = new Float32Array(n), R = new Float32Array(n); x[0] = 1;
  r.process(x, x, L, R, n, 0); const e1 = L.slice(0, SR).reduce((a, b) => a + b * b, 0), e2 = L.slice(3 * SR).reduce((a, b) => a + b * b, 0);
  ok(finite(L) && e1 > 1e-4 && e2 < e1 * 0.01, 'Rev7 output and decay');
  const d = new TC2290(SR); d.set('dly.sync', 0); d.set('dly.time', 250); d.set('dly.duck', 0); d.set('dly.modDepth', 0);
  const L2 = new Float32Array(n), R2 = new Float32Array(n); d.process(x, x, L2, R2, n, 0);
  let pi = 0; for (let i = 1; i < SR; i++) if (Math.abs(L2[i]) > Math.abs(L2[pi])) pi = i;
  ok(finite(L2) && Math.abs(pi / SR - 0.25) < 0.01, 'TC2290 echo at ' + (pi / SR).toFixed(3) + ' s');
}
console.log(fails ? fails + ' failed' : 'all passed');
process.exit(fails ? 1 : 0);
