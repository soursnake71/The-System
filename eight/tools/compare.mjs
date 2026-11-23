// Omega 808: checks the C++ port (plugins/eight/Engine.h, Fx.h, Seq.h) renders the same samples as the app's JS engine.
// node eight/tools/compare.mjs <path to eight_test> <work dir>
// eight_test writes raw float files; this renders the same scenarios with fx.js / seq.js / engine.js (driven the way
// worklet.js drives them) and needs every scenario within 1e-4 of the JS.
import {createRequire} from 'module';
import {execFileSync} from 'child_process';
import fs from 'fs';
import path from 'path';
const require = createRequire(import.meta.url);
const dir = path.dirname(new URL(import.meta.url).pathname);
require(path.join(dir, '../fx.js')); require(path.join(dir, '../seq.js'));
const {Eight808} = require(path.join(dir, '../engine.js'));
const {Seq808} = globalThis;
const [bin, work] = [process.argv[2], process.argv[3] || 'eight-compare'];
if (!bin) { console.error('usage: node eight/tools/compare.mjs <eight_test> <work dir>'); process.exit(2); }
fs.mkdirSync(work, {recursive: true});
execFileSync(bin, [work], {stdio: 'inherit'});
const SR = 48000;

// the worklet's process(), without the port
function machine() {
  const e = new Eight808(SR), s = new Seq808(SR);
  e.set('tempo', s.bpm);
  const set = (k, v) => { if (k === 'tempo') { s.setTempo(v); e.set('tempo', s.bpm); return; } e.set(k, v); if (k === 'swing') s.swing = v; };
  const process = (L, R, n, base) => {
    let done = 0;
    s.tick(n, (off, inf) => {
      if (off > done) { e.process(L, R, off - done, base + done); done = off; }
      e.trigFired = 0;
      if (inf.acc) e.trigger('AC', 1, 1);
      for (let i = 0; i < inf.n; i++) { const tr = Seq808.TRACKS[inf.tracks[i]], v = tr === 'TRIG' ? 'TRIG' : e.trackVoice(tr); e.trigger(v, inf.acc, 1); }
    });
    if (n > done) e.process(L, R, n - done, base + done);
  };
  return {e, s, set, process};
}
function seqScenario(modded) {
  const m = machine();
  m.set('rev.send.BD', 0.3); m.set('dly.send.SD', 0.4); m.set('dly.send.CH', 0.3); m.set('rev.program', 0);
  if (modded) {
    m.set('mod.drive', 0.5); m.set('mod.BD.longDecay', 0.5); m.set('mod.age', 1); m.set('swing', 0.5); m.set('CP.sel', 1); m.set('LT.sel', 1);
    m.set('mod.TRIG.src', 2); m.set('mod.TRIG.level', 0.6); m.set('rev.program', 11); m.set('dly.pingpong', 0); m.set('dly.sync', 0); m.set('dly.time', 230);
    m.set('SD.drive', 0); m.set('mod.SD.drive', 0.7); m.set('mod.CP.drive', 0.5);
    m.s.select({basic: 1, fill: 0, variation: 'AB', ifVar: 'B'}); m.s.fillEvery = 2;
  }
  m.s.play(true);
  const n = Math.floor(SR * 4.2), L = new Float32Array(n), R = new Float32Array(n);
  for (let o = 0; o < n; o += 128) m.process(L, R, Math.min(128, n - o), o);
  return [L, R];
}
const scen = [];
for (const v of Eight808.VOICES) for (const acc of [0, 1]) scen.push([v + (acc ? '-acc' : ''), () => {
  const e = new Eight808(SR);
  if (v === 'TRIG') e.set('mod.TRIG.level', 0.8);
  e.trigger(v, acc, 1);
  const n = SR, L = new Float32Array(n), R = new Float32Array(n);
  for (let o = 0; o < n; o += 128) e.process(L, R, Math.min(128, n - o), o);
  return [L, R];
}]);
scen.push(['seq', () => seqScenario(false)], ['seq-mod', () => seqScenario(true)]);

let fails = 0;
for (const [name, fn] of scen) {
  const [L, R] = fn();
  const raw = fs.readFileSync(path.join(work, name + '.f32'));
  const c = new Float32Array(raw.buffer, raw.byteOffset, raw.byteLength / 4);
  let d = 0, at = -1, pk = 0;
  if (c.length !== L.length * 2) { console.log(`FAIL ${name}: length ${c.length / 2} vs ${L.length}`); fails++; continue; }
  for (let i = 0; i < L.length; i++) {
    const a = Math.abs(c[i] - L[i]), b = Math.abs(c[L.length + i] - R[i]);
    pk = Math.max(pk, Math.abs(L[i]));
    if (!(a <= d)) { d = a; at = i; } if (!(b <= d)) { d = b; at = i; }
  }
  const good = d < 1e-4;
  if (!good) fails++;
  console.log(`${good ? 'ok  ' : 'FAIL'} ${name.padEnd(8)} max diff ${d.toExponential(2)}${good ? '' : ' at ' + at} (peak ${pk.toFixed(3)})`);
}
console.log(fails ? `${fails} scenarios differ` : 'C++ matches the JS engine');
process.exit(fails ? 1 : 0);
