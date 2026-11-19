// Checks every Omega 808 preset: keys exist, values are in range, pattern lengths match, counts meet the minimums.
// Usage: node eight/tools/presets-check.mjs
import {createRequire} from 'module';
import {existsSync} from 'fs';
import {dirname, join} from 'path';
import {fileURLToPath} from 'url';
const require = createRequire(import.meta.url);
const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..');
const P = require(join(root, 'presets.js'));

// the key list from CONTRACT.md, with ranges in the units presets.js uses
const R = {};
const add = (keys, min, max, int) => { for (const k of keys.split(/\s+/).filter(Boolean)) R[k] = {min, max, int}; };
add('master AC.level BD.level BD.tone BD.decay SD.level SD.tone SD.snappy LT.level LT.tuning MT.level MT.tuning HT.level HT.tuning RS.level CP.level CB.level CY.level CY.tone CY.decay OH.level OH.decay CH.level', 0, 1);
add('LT.sel MT.sel HT.sel RS.sel CP.sel', 0, 1, true);
add(`mod.BD.tune mod.BD.sweep mod.BD.longDecay mod.BD.click mod.BD.drive mod.SD.tune mod.SD.decay mod.SD.noiseColor mod.TOM.decay mod.TOM.sweep
  mod.CONGA.decay mod.RS.tune mod.CL.tune mod.CP.tail mod.CP.spread mod.MA.decay mod.CB.tune mod.CB.decay mod.HH.tune mod.CH.decay mod.CY.tune
  mod.AC.depth mod.AC.punch mod.TRIG.level mod.TRIG.tone mod.age mod.hiss swing`, 0, 1);
add('mod.TRIG.src', 0, 3, true);
add('mod.TRIG.width', 1, 40);
const SENDV = 'BD SD LT MT HT RS CP CB CY OH CH TRIG'.split(' ');
add(SENDV.map(v => `rev.send.${v} dly.send.${v}`).join(' '), 0, 1);
add('rev.program', 0, 11, true);
add('rev.time', 0.1, 20); add('rev.predelay', 0, 500); add('rev.hf', 0.1, 1); add('rev.diffusion rev.density rev.er rev.return', 0, 1);
add('rev.lpf', 500, 20000); add('rev.hpf', 20, 2000);
add('dly.time', 1, 2000); add('dly.sync dly.pingpong', 0, 1, true); add('dly.division', 0, 9, true);
add('dly.feedback dly.modDepth dly.duck dly.return', 0, 1); add('dly.hicut', 500, 20000); add('dly.lowcut', 20, 2000); add('dly.modRate', 0.01, 10);

let engine = null;
if (existsSync(join(root, 'engine.js'))){
  try { if (existsSync(join(root, 'fx.js'))) require(join(root, 'fx.js')); if (existsSync(join(root, 'seq.js'))) require(join(root, 'seq.js')); engine = require(join(root, 'engine.js')); } catch (e){ console.log('engine.js did not load: ' + e.message); }
}
const EP = engine && (engine.Eight808 || globalThis.Eight808) && (engine.Eight808 || globalThis.Eight808).PARAMS;
const EPK = EP ? new Map(EP.map(p => [p.k, p])) : null;

const errs = [];
const err = s => { if (errs.length < 200) errs.push(s); };
function checkParams(where, p){
  for (const [k, v] of Object.entries(p)){
    const r = R[k];
    if (!r){ err(`${where}: unknown key ${k}`); continue; }
    if (typeof v !== 'number' || !isFinite(v)) err(`${where}: ${k} is not a number`);
    else if (v < r.min || v > r.max) err(`${where}: ${k}=${v} outside ${r.min}..${r.max}`);
    else if (r.int && v !== Math.round(v)) err(`${where}: ${k}=${v} should be whole`);
    if (EPK){
      const e = EPK.get(k);
      if (!e) err(`${where}: ${k} is not in Eight808.PARAMS`);
      else if (v < e.min || v > e.max) err(`${where}: ${k}=${v} outside the engine's ${e.min}..${e.max}`);
    }
  }
}
const TRACKS = new Set(['AC', 'BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'CP', 'CB', 'CY', 'OH', 'CH', 'TRIG']);
function checkPattern(where, p){
  if (!p || typeof p !== 'object') return err(`${where}: no pattern`);
  if (![0, 1, 2, 3].includes(p.scale)) err(`${where}: scale ${p.scale}`);
  if (!(p.len >= 1 && p.len <= 16 && p.len === Math.round(p.len))) err(`${where}: len ${p.len}`);
  if (![1, 2].includes(p.parts)) err(`${where}: parts ${p.parts}`);
  const n = p.len * p.parts;
  if (!p.steps || !Object.keys(p.steps).length) err(`${where}: empty`);
  for (const [t, s] of Object.entries(p.steps || {})){
    if (!TRACKS.has(t)) err(`${where}: unknown track ${t}`);
    if (typeof s !== 'string' || s.length !== n) err(`${where}: ${t} has ${s && s.length} steps, wants ${n}`);
    else if (/[^x.]/.test(s)) err(`${where}: ${t} has characters other than x and .`);
  }
  // something to play on the beat: a kick, snare, clap, rim or tom
  if (!['BD', 'SD', 'CP', 'RS', 'LT', 'MT', 'HT', 'TRIG', 'CB'].some(t => p.steps && p.steps[t])) err(`${where}: no drums`);
}
function checkRhythm(where, r){ checkPattern(where + ' A', r.A); checkPattern(where + ' B', r.B); }

const names = new Set();
for (const k of P.EIGHT_KITS){ if (names.has('k' + k.name)) err('duplicate kit ' + k.name); names.add('k' + k.name); checkParams(`kit "${k.name}"`, k.params); }
for (const r of P.EIGHT_RHYTHMS){
  if (names.has('r' + r.name)) err('duplicate rhythm ' + r.name); names.add('r' + r.name);
  if (!(r.tempo >= 30 && r.tempo <= 300)) err(`rhythm "${r.name}": tempo ${r.tempo}`);
  if (!(r.swing >= 0 && r.swing <= 1)) err(`rhythm "${r.name}": swing ${r.swing}`);
  checkRhythm(`rhythm "${r.name}"`, r.rhythm);
}
for (const b of P.EIGHT_BANKS){
  const k = b.bank;
  if (!k.basic || k.basic.length !== 12) err(`bank "${b.name}": ${k.basic && k.basic.length} basic rhythms`);
  if (!k.fill || k.fill.length !== 4) err(`bank "${b.name}": ${k.fill && k.fill.length} fills`);
  if (![0, 2, 4, 8, 12, 16].includes(k.fillEvery)) err(`bank "${b.name}": fillEvery ${k.fillEvery}`);
  (k.basic || []).forEach((r, i) => checkRhythm(`bank "${b.name}" basic ${i + 1}`, r));
  (k.fill || []).forEach((r, i) => checkRhythm(`bank "${b.name}" fill ${i + 1}`, r));
}
for (const f of P.EIGHT_FX) checkParams(`fx "${f.name}"`, f.params);

const cats = o => [...new Set(o.map(x => x.cat))];
const need = [['kits', P.EIGHT_KITS.length, 120], ['rhythms', P.EIGHT_RHYTHMS.length, 150], ['banks', P.EIGHT_BANKS.length, 12], ['fx', P.EIGHT_FX.length, 30]];
for (const [what, n, min] of need) if (n < min) err(`${what}: ${n}, wants at least ${min}`);
const trigUse = P.EIGHT_RHYTHMS.filter(r => r.rhythm.A.steps.TRIG || r.rhythm.B.steps.TRIG).length;
const scales = [...new Set(P.EIGHT_RHYTHMS.map(r => r.rhythm.A.scale + '/' + r.rhythm.A.len + 'x' + r.rhythm.A.parts))];

console.log(`kits ${P.EIGHT_KITS.length} in ${cats(P.EIGHT_KITS).length} categories: ${cats(P.EIGHT_KITS).join(', ')}`);
console.log(`rhythms ${P.EIGHT_RHYTHMS.length} in ${cats(P.EIGHT_RHYTHMS).length} categories: ${cats(P.EIGHT_RHYTHMS).join(', ')}`);
console.log(`  with the TRIG track: ${trigUse}; shapes (scale/len x parts): ${scales.join(' ')}`);
console.log(`banks ${P.EIGHT_BANKS.length}, fx ${P.EIGHT_FX.length}`);
console.log(EP ? `checked against Eight808.PARAMS (${EP.length} params)` : 'engine.js not there yet: checked against the CONTRACT.md key list only');
if (errs.length){ console.log(`\n${errs.length} problem(s):\n` + errs.join('\n')); process.exit(1); }
console.log('all presets OK');
