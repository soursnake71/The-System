// Checks plugins/solina/Solina.h renders the same samples as solina/engine.js.
// node solina/tools/compare.mjs <build>/solina_test <tmpdir>
import {createRequire} from 'module';
import {execFileSync} from 'child_process';
import {readFileSync, writeFileSync} from 'fs';
import {dirname, join} from 'path';
import {fileURLToPath} from 'url';
const here = dirname(fileURLToPath(import.meta.url)), req = createRequire(import.meta.url);
const D = req(join(here, '..', 'engine.js')), PRE = req(join(here, '..', 'presets.js'));
const [bin, tmp] = process.argv.slice(2);
if (!bin || !tmp){ console.error('usage: node compare.mjs <solina_test> <tmpdir>'); process.exit(2); }

function scenario(fs, preset, extra, seed){
  let x = seed; const rnd = () => (x = (x * 1103515245 + 12345) % 2147483648) / 2147483648;
  const s = [`fs ${fs}`], v = Object.assign(D.defaults(), preset, extra);
  for (const p of D.PARAMS) s.push(`set ${p.id} ${v[p.id]}`);
  s.push('bpm 97');
  const chord = [36, 43, 48, 55, 60, 64, 67, 72, 79].filter(() => rnd() < .7);
  for (const n of chord) s.push(`on ${n} ${(.5 + rnd() * .5).toFixed(3)}`);
  s.push(`render ${Math.round(fs * .7)}`, 'mod 0.6', 'bend 0.3', 'pedal 1');
  for (const n of chord.slice(0, 3)) s.push(`off ${n}`);
  s.push(`render ${Math.round(fs * .4)}`, 'pedal 0', 'expr 0.7', 'on 41 0.9', 'on 84 1', `render ${Math.round(fs * .3)}`);
  for (const n of chord) s.push(`off ${n}`);
  s.push('off 41', 'off 84', 'bend 0', `render ${Math.round(fs * .8)}`);
  return s;
}
function runJs(lines){
  let e = null; const out = [];
  for (const l of lines){ const [c, a, b] = l.split(' ');
    if (c === 'fs') e = new D.SolinaEngine(+a);
    else if (c === 'set') e.set(a, +b); else if (c === 'on') e.noteOn(+a, +b); else if (c === 'off') e.noteOff(+a);
    else if (c === 'pedal') e.pedal(+a !== 0); else if (c === 'bend') e.bend(+a); else if (c === 'mod') e.modWheel(+a); else if (c === 'expr') e.expression(+a); else if (c === 'bpm') e.bpm = +a;
    else if (c === 'render'){ const n = +a, L = new Float64Array(n), R = new Float64Array(n); for (let o = 0; o < n; o += 128) e.process(L, R, o, Math.min(128, n - o)); for (let i = 0; i < n; i++) out.push(L[i], R[i]); } }
  return Float64Array.from(out);
}
const cases = [];
PRE.forEach((p, i) => { if (i % 3 === 0) cases.push([p.name, p.p, {}, 44100]); });
let sd = 7; const rnd = () => (sd = (sd * 1103515245 + 12345) % 2147483648) / 2147483648;
for (let i = 0; i < 6; i++) cases.push(['random ' + (i + 1), D.randomPatch(rnd), {}, i % 2 ? 48000 : 96000]);
cases.push(['every section, solo + mute', {violin:1, viola:1, trumpet:1, horn:1, cello:1, contra:1}, {vaSol:1, hnSol:1, cbMut:1, phOn:1, dlOn:1, dlSync:3, outSel:1, drive:.4, eqLow:4, eqMid:-3, eqHigh:6}, 48000]);
let worst = 0, fail = 0;
for (const [name, pre, extra, fs] of cases){
  const lines = scenario(fs, pre, extra, name.length * 31 + 5), f = join(tmp, 'solina.txt'), o = join(tmp, 'solina.f64');
  writeFileSync(f, lines.join('\n') + '\n'); execFileSync(bin, [f, o]);
  const c = new Float64Array(readFileSync(o).buffer.slice(0)), j = runJs(lines);
  let d = 0, pk = 0; for (let i = 0; i < j.length; i++){ d = Math.max(d, Math.abs(j[i] - c[i])); pk = Math.max(pk, Math.abs(j[i])); }
  const rel = d / Math.max(1e-9, pk), ok = c.length === j.length && rel < 1e-6 && pk > 1e-3;
  worst = Math.max(worst, rel); if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(36)} peak ${pk.toFixed(3)}  max diff ${d.toExponential(2)}`);
}
console.log(`${cases.length - fail}/${cases.length} match (worst relative difference ${worst.toExponential(2)})`);
process.exit(fail ? 1 : 0);
