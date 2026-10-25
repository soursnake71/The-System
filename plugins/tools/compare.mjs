// Runs the web app's own worklet code (index.html) in Node on the same test signal the C++ test uses, and prints
// the output level of each unit, so the ports can be checked against the originals.
import fs from 'fs'; import vm from 'vm'; import path from 'path'; import {fileURLToPath} from 'url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const src = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const code = name => { const i = src.indexOf('const ' + name + ' = String.raw`'); const s = src.indexOf('`', i) + 1; return src.slice(s, src.indexOf('`;', s)); };
const sr = 48000, block = 128, secs = 4, n = sr * secs;
const procs = {};
const ctx = {Math, Float32Array, Float64Array, Array, Object, String, Number, console, sampleRate:sr, currentFrame:0, currentTime:0,
  AudioWorkletProcessor:class { constructor(){ this.port = {postMessage(){}, onmessage:null}; } }, registerProcessor:(k, c) => { procs[k] = c; }};
vm.createContext(ctx);
const tnh = 'const tnh = x => x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x);\n';
vm.runInContext(tnh + code('RACK_CODE'), ctx); vm.runInContext(code('P5_CODE'), ctx); vm.runInContext(code('VOC_CODE'), ctx);
const ex = fs.readFileSync(path.join(root, 'plugins/gen/tables.inc'), 'utf8');
const T = JSON.parse(fs.readFileSync('/tmp/tables.json', 'utf8'));
const input = (name) => { const a = new Float32Array(n); for (let i = 0; i < n; i++){ const t = i / sr, ph = t % .5; let x = 0; if (t < 2) x = Math.sin(2 * Math.PI * 110 * t) * Math.exp(-ph * 8) * .4 + Math.sin(2 * Math.PI * 1830 * t) * Math.exp(-ph * 30) * .3; a[i] = x; } return a; };
const run = (proc, cfg, ins, notes) => {
  ctx.currentFrame = 0; const p = new procs[proc]({processorOptions:proc === 'svc350' ? cfg : {cfg}});
  const L = new Float32Array(n), R = new Float32Array(n);
  for (let pos = 0; pos < n; pos += block){
    ctx.currentFrame = pos; ctx.currentTime = pos / sr;
    if (notes){ if (pos === 0) for (const k of [60, 64, 67]) p.q.push({f:0, on:k}); if (pos <= sr * 2 && pos + block > sr * 2) for (const k of [60, 64, 67]) p.q.push({f:pos, off:k}); }
    const o = [[L.subarray(pos, pos + block), R.subarray(pos, pos + block)]];
    const i = ins ? ins.map(ch => [ch.subarray(pos, pos + block), ch.subarray(pos, pos + block)]) : [[]];
    p.process(i, o, {});
  }
  let s = 0, pk = 0; for (let i = 0; i < n; i++){ s += L[i] * L[i] + R[i] * R[i]; pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i])); }
  return {rms:Math.sqrt(s / (2 * n)), pk};
};
const only = process.argv[2];
const inp = input();
for (const [k, U] of Object.entries(T.rack)){
  const name = {lx:'Lexicon224', pt:'PrimeTime', ef:'Effectron', dp:'DP4', mv:'Microverb', ad:'ADAFlanger', u7:'UREI1176', pc:'PhilCollins', h9:'H910', mx:'MXR113'}[k];
  if (only && only !== name) continue;
  const cfg = {}; for (const x of U.knobs) cfg[x.key] = x.def; for (const m of U.menus) cfg[m.key] = m.def;
  if (k === 'pt'){ cfg.dA = cfg.dA; } if (k === 'u7') cfg.ret = 1.25;
  const r = run(U.proc, cfg, [inp]);
  console.log(name.padEnd(12), 'rms', r.rms.toFixed(4), ' peak', r.pk.toFixed(3));
}
if (!only || only === 'Prophet5'){ const r = run('prophet5', Object.assign({}, T.p5.init), null, true); console.log('Prophet5'.padEnd(12), 'rms', r.rms.toFixed(4), ' peak', r.pk.toFixed(3)); }
if (process.argv[2] === 'dbg'){ const U = T.rack.dp; const cfg = {}; for (const x of U.knobs) cfg[x.key] = x.def; for (const m of U.menus) cfg[m.key] = m.def;
  const p = new procs.dp4({processorOptions:{cfg}}); console.log(p.u.map(u => u.alg), p.c.cfg);
  const a = new Float32Array(128).fill(.5), L = new Float32Array(128), R = new Float32Array(128); for (let k = 0; k < 50; k++) p.process([[a, a]], [[L, R]]); console.log(L.slice(0, 4), p.quiet, p.asleep); }
