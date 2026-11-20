// Omega 808: drives worklet.js in Node with a fake AudioWorklet scope and checks that every message in CONTRACT.md
// does something: set, setMany, hit, play, tempo, bank, select, tap, edit, sample, clock, pos.
// Usage: node eight/tools/worklet-check.mjs
import fs from 'fs';
import vm from 'vm';
const dir = new URL('..', import.meta.url).pathname;
const posted = [];
let Proc = null;
const scope = {sampleRate: 48000, currentTime: 0, console, Math, Float32Array, Float64Array, Int32Array, Uint8Array, Array, Object,
  registerProcessor: (n, c) => { Proc = c; },
  AudioWorkletProcessor: class { constructor() { this.port = {postMessage: m => posted.push(m), onmessage: null}; } }};
scope.globalThis = scope;
vm.createContext(scope);
for (const f of ['fx.js', 'seq.js', 'engine.js', 'worklet.js']) vm.runInContext(fs.readFileSync(dir + f, 'utf8'), scope, {filename: f});
const p = new Proc();
const send = m => p.port.onmessage({data: m});
const L = new Float32Array(128), R = new Float32Array(128);
function run(blocks) { let e = 0; for (let b = 0; b < blocks; b++) { p.process([], [[L, R]]); for (let i = 0; i < 128; i++) e += L[i] * L[i] + R[i] * R[i]; scope.currentTime += 128 / 48000; } return e; }
const steps = () => posted.filter(m => m.t === 'step');
let fail = 0;
const ok = (c, what) => { console.log((c ? 'ok   ' : 'FAIL ') + what); if (!c) fail++; };

ok(posted[0] && posted[0].t === 'ready' && posted[0].params.length > 100, 'ready with params');
send({t: 'hit', v: 'BD', acc: 1, vel: 1}); ok(run(40) > 1, 'hit plays a voice');
send({t: 'set', k: 'BD.level', v: 0}); run(400); send({t: 'hit', v: 'BD', acc: 0, vel: 1}); ok(run(40) < 0.5, 'set BD.level 0 silences BD');
send({t: 'setMany', p: {'BD.level': 0.8, 'rev.time': 5, 'rev.program': 0}}); ok(Math.abs(p.e.get('rev.time') - 5) < 1e-9, 'setMany: rev.time survives rev.program in the same message');
// a bank with a BD on every step, and play
const all = 'xxxxxxxxxxxxxxxx', one = {scale: 0, len: 16, parts: 1, steps: {BD: all}};
send({t: 'bank', bank: {tempo: 120, basic: [{A: one, B: one}], fill: []}});
send({t: 'select', basic: 0, variation: 'A'}); send({t: 'play', on: true});
posted.length = 0; run(375);   // 1 s
ok(steps().length >= 7 && steps().length <= 9, `play at 120 BPM: ${steps().length} steps in 1 s (8 expected)`);
ok(steps().every(m => m.hits.includes('BD')), 'steps carry their hits');
send({t: 'tempo', bpm: 240}); posted.length = 0; run(375); ok(steps().length >= 15 && steps().length <= 17, `tempo 240: ${steps().length} steps in 1 s`);
send({t: 'clock', bpm: 60}); posted.length = 0; run(375); ok(steps().length >= 3 && steps().length <= 5, `clock 60: ${steps().length} steps in 1 s`);
send({t: 'set', k: 'tempo', v: 120}); posted.length = 0; run(375); ok(steps().length >= 7 && steps().length <= 9, `set tempo 120: ${steps().length} steps`);
// edit while playing: add an SD on every step
for (let s = 0; s < 16; s++) send({t: 'edit', which: 'basic', n: 0, var: 'A', track: 'SD', step: s, on: true});
posted.length = 0; run(200); ok(steps().some(m => m.hits.includes('SD')), 'edit takes effect while playing');
send({t: 'pos', step: 5}); posted.length = 0; run(2); ok(steps().length === 0 || steps()[0].step === 5, 'pos jumps to a step');
// tap: fill at the next measure
send({t: 'bank', bank: {tempo: 120, basic: [{A: one, B: one}], fill: [{A: {scale: 0, len: 16, parts: 1, steps: {CP: all}}, B: one}]}});
send({t: 'tap'}); posted.length = 0; run(375 * 3); ok(steps().some(m => m.which === 'fill' && m.hits.includes('CP')), 'tap plays the fill');
// sample replaces a voice
send({t: 'play', on: false}); run(200);
const d = new Float32Array(4800); d.fill(0.5);
send({t: 'set', k: 'BD.level', v: 1}); send({t: 'sample', v: 'BD', data: d, sr: 48000}); send({t: 'hit', v: 'BD', acc: 0, vel: 1});
run(1); ok(Math.abs(L[64]) > 0.1, 'sample plays in place of the voice');
send({t: 'sample', v: 'BD', clear: true}); ok(!p.e.smp[0], 'sample clear');
send({t: 'play', on: false}); posted.length = 0; run(100); ok(steps().length === 0, 'stop');
console.log(fail ? `${fail} failed` : 'all worklet messages OK');
process.exit(fail ? 1 : 0);
