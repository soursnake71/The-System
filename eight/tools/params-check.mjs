// Omega 808: checks that every parameter changes the sound (no dead knobs), that the sound switches swap voices
// and that swing moves the sequencer's steps. For each Eight808.PARAMS entry it renders the voices that parameter acts
// on at the parameter's minimum and at its maximum and compares them. Usage: node eight/tools/params-check.mjs
import {createRequire} from 'module';
const require = createRequire(import.meta.url);
const dir = new URL('..', import.meta.url).pathname;
Object.assign(globalThis, require(dir + 'fx.js'));
const {Seq808} = require(dir + 'seq.js');
const {Eight808} = require(dir + 'engine.js');
const SR = 48000;

function voicesFor(k) {
  const [a, b] = k.split('.');
  const ALL = ['BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH'];
  if (['master', 'mod.age', 'mod.drive', 'mod.hiss', 'AC.level', 'mod.AC.depth', 'mod.AC.punch'].includes(k)) return ALL;
  if (k.endsWith('.sel')) return [a];       // played through trackVoice, so the switch picks the voice
  if (a === 'mod') return ({BD:['BD'], SD:['SD'], TOM:['LT','MT','HT'], CONGA:['LC','MC','HC'], RS:['RS'], CL:['CL'], MA:['MA'], CB:['CB'],
    HH:['OH','CH','CY'], CH:['CH'], CY:['CY'], TRIG:['TRIG'],
    CP: k === 'mod.CP.drive' || k === 'mod.CP.tail' || k === 'mod.CP.spread' ? ['CP'] : ['CP']})[b] || ALL;
  if (a === 'rev' || a === 'dly') return b === 'send' ? [k.split('.')[2]] : ['SD'];
  return [a];
}
function render(k, val) {
  const e = new Eight808(SR), [a, b] = k.split('.');
  // make the parameter audible: open the effect it lives in, put the trigger-out click in the mix
  if (a === 'rev') { e.set('rev.return', 1); if (b !== 'send') e.set('rev.send.SD', 1); }
  if (a === 'dly') { e.set('dly.return', 1); if (b !== 'send') e.set('dly.send.SD', 1); if (k === 'dly.time') e.set('dly.sync', 0); if (k === 'dly.sync') e.set('dly.time', 600); }
  if (k.startsWith('mod.TRIG') || k.endsWith('.TRIG')) e.set('mod.TRIG.level', 1);
  if (k === 'mod.TRIG.src') e.set('mod.TRIG.width', 20);
  e.set(k, val);
  const acc = /^(AC\.|mod\.AC\.)/.test(k) ? 1 : 0;
  const N = SR * (a === 'rev' || a === 'dly' ? 3 : 1.5), L = new Float32Array(N), R = new Float32Array(N);
  const vs = voicesFor(k);
  if (k === 'mod.TRIG.src') { e.trigger('CB', 0, 1); e.process(L, R, SR / 4, 0); e.trigger('CP', 0, 1); e.process(L, R, SR / 4, SR / 4); e.trigger('AC', 1, 1); e.process(L, R, N - SR / 2, SR / 2); return [L, R]; }
  for (const v of vs) e.trigger(k.endsWith('.sel') ? e.trackVoice(v) : v, acc, 1);
  e.process(L, R, N, 0);
  return [L, R];
}
function rel(x, y) {
  let d = 0, p = 0, q = 0;
  for (let c = 0; c < 2; c++) for (let i = 0; i < x[c].length; i++) { const u = x[c][i], w = y[c][i]; d += (u - w) * (u - w); p += u * u; q += w * w; }
  return Math.sqrt(d / Math.max(p, q, 1e-12));
}
let dead = 0, bad = 0;
for (const P of Eight808.PARAMS) {
  if (P.k === 'swing') continue;
  const lo = render(P.k, P.min), hi = render(P.k, P.max);
  for (const [nm, s] of [['min', lo], ['max', hi]]) if (s.some(ch => ch.some(x => !isFinite(x)))) { bad++; console.log('NaN  ', P.k, nm); }
  const r = rel(lo, hi);
  if (r < 1e-3) { dead++; console.log('DEAD ', P.k, r.toExponential(2)); }
  else if (process.argv.includes('-v')) console.log('ok   ', P.k, r.toFixed(3));
}
// swing: the sequencer's step times
function stepTimes(sw) { const s = new Seq808(SR); s.swing = sw; s.play(true); const t = []; let at = 0; for (let b = 0; b < 200; b++) { s.tick(128, (off) => t.push(at + off)); at += 128; } return t.slice(0, 4).join(','); }
if (stepTimes(0) === stepTimes(1)) { dead++; console.log('DEAD  swing'); }
console.log(`${Eight808.PARAMS.length} params: ${dead} dead, ${bad} with NaN/Inf`);
process.exit(dead || bad ? 1 : 0);
