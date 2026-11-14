/* Omega Linn kits: which file goes on which sound (by its name), and how a recording becomes a ROM layer. Used by the
   ROM burner (linn/tools/make-rom.mjs, the owner's recordings in linn/samples) and by LOAD FOLDER in the app (the plugin
   has the same rules in plugins/linn/Kit.h).
   - Names: kick / bd / bass -> BASS (one named "kickme" / "alt", or a second kick, -> BASS ALT); snare / sd / snr ->
     SNARE; sidestick / stick / sst / rim -> SIDESTICK; an open hat (ohh, "open") -> HIHAT (a closed one only when there
     is no open one: the LinnDrum makes its closed hat from the open one); tom -> TOMS; conga / cga -> CONGAS; ride,
     crash, cabasa / cab, tamb, cowbell / cowb / cb, clap / cp -> their sounds. Anything else is left out.
   - Several snares, toms or congas become layers: the same ROM recorded at different tunings. A layer's sample clock is
     worked out from its length (the ROM's size over the recording's length: the hardware plays the whole ROM); a
     recording whose length doesn't fit the 8-80 kHz range is taken as played at the knob's factory setting.
   - Dynamics: a name with soft / mid / hard (or med, loud) is that dynamic of its sound (SNARE 1 / 2 / 3, BASS 1 / 2,
     RIDE 1 / 2...). Recordings of one sound with dynamics keep their levels to each other (scaled by the loudest).
   - The open hat is stored without the open-hat VCA's slow decay (the circuit puts it back when it plays). */
'use strict';
(function (root) {
const D = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.OLINN;
const {G, ROM_SIZE} = D;
const RULES = [   // [group, words matched anywhere (4+ letters) or as a whole word (short ones)]
  [G.SIDESTICK, ['sidestick', 'side stick', 'xstick', 'cross stick', 'sst', 'stick', 'rim', 'rimshot', 'rs']],
  [G.SNARE, ['snare', 'sd', 'snr', 'sn']],
  [G.TOM, ['tom', 'toms']],
  [G.CONGA, ['conga', 'cga', 'cng']],
  [G.RIDE, ['ride', 'rd']],
  [G.CRASH, ['crash', 'cr', 'cymbal', 'cym']],
  [G.CABASA, ['cabasa', 'cab', 'cbs', 'cbsa', 'shaker']],
  [G.TAMB, ['tambourine', 'tamb', 'tmb', 'tam']],
  [G.COWBELL, ['cowbell', 'cowb', 'cow', 'cb', 'bell']],
  [G.CLAPS, ['claps', 'clap', 'clp', 'cp', 'handclap']],
  [G.HAT, ['hihat', 'hi hat', 'hi-hat', 'hat', 'hh', 'ohh', 'chh', 'oh', 'ch']],
  [G.BASS, ['kick', 'bassdrum', 'bass drum', 'bd', 'bass', 'kik', 'kck']],
];
function words(name){ return ' ' + name.toLowerCase().replace(/\.[a-z0-9]+$/, '').replace(/[_\-.()[\]]+/g, ' ').replace(/([a-z])([0-9])/g, '$1 $2').replace(/\s+/g, ' ').trim() + ' '; }
function groupOf(name){
  const w = words(name);
  for (const [g, list] of RULES) for (const k of list){
    if (k.length >= 4 ? w.includes(k) : new RegExp('(^| )' + k + '[a-z]{0,3}( |$)').test(w)) return g;
  }
  return -1;
}
const isOpen = name => { const w = words(name); return /(^| )(open|ohh?|op)( |$)|open|ohh/.test(w); };
const isAlt = name => /kickme|alt|kick ?2|bd ?2|bass ?2/.test(words(name).replace(/ /g, ''));
// the dynamic a name says: 0 soft, 1 mid, 2 hard, -1 none (not for toms and congas: there "mid" / "low" are pitches)
function dynOf(name, g){
  if (g === G.TOM || g === G.CONGA) return -1;
  const w = words(name);
  if (/(^| )(soft|quiet|light|lo vel|low vel|pp|p)( |$)/.test(w)) return 0;
  if (/(^| )(mid|med|medium|mf)( |$)/.test(w)) return 1;
  if (/(^| )(hard|loud|accent|acc|ff|f)( |$)/.test(w)) return 2;
  return -1;
}
/* files: [name, ...] -> {groups: [[index, ...] per group], dyn: [dynamic per file], left: [index, ...]} */
function classify(names){
  const groups = []; for (let g = 0; g < D.NG_ROM; g++) groups.push([]);
  const left = [], hats = [], kicks = [], dyn = names.map(() => -1);
  names.forEach((n, i) => {
    const g = groupOf(n);
    if (g >= 0) dyn[i] = dynOf(n, g);
    if (g < 0) left.push(i);
    else if (g === G.HAT) hats.push(i);
    else if (g === G.BASS && dyn[i] < 0) kicks.push(i);
    else if (g === G.SNARE || g === G.TOM || g === G.CONGA) groups[g].push(i);
    else if (dyn[i] >= 0 ? !groups[g].some(j => dyn[j] === dyn[i] || dyn[j] < 0) : !groups[g].length) groups[g].push(i);
    else left.push(i);
  });
  // the hat: the open one (the closed hat is made from it)
  const open = hats.filter(i => isOpen(names[i]));
  const hat = open.length ? open[0] : hats.length ? hats[0] : -1;
  if (hat >= 0) groups[G.HAT].push(hat);
  for (const i of hats) if (i !== hat) left.push(i);
  // the kicks: the plain one on BASS, an "alt" / "kickme" (or a second kick) on BASS ALT
  const plain = kicks.filter(i => !isAlt(names[i])).sort((a, b) => names[a].length - names[b].length), alts = kicks.filter(i => isAlt(names[i]));
  const main = plain.length ? plain[0] : alts.length ? alts[0] : -1, alt = alts.find(i => i !== main) ?? plain.find(i => i !== main) ?? -1;
  if (main >= 0 && !groups[G.BASS].length) groups[G.BASS].push(main); else if (main >= 0) left.push(main);
  if (alt >= 0) groups[G.BASS2].push(alt);
  for (const i of kicks) if (i !== main && i !== alt) left.push(i);
  // a group with dynamics: files without one are left out (they'd clash)
  for (const list of groups) if (list.some(i => dyn[i] >= 0)) for (let k = list.length - 1; k >= 0; k--) if (dyn[list[k]] < 0 && (list.length > 1)){ left.push(list[k]); list.splice(k, 1); }
  return {groups, dyn, left};
}
const DEFAULT_CLOCK = g => g === G.SNARE || g === G.SIDESTICK ? D.snareClock(25) : g === G.TOM ? D.tomClock(50) : g === G.CONGA ? D.tomClock(46) : D.MUX_DEFAULT;
const TUNED = g => g === G.SNARE || g === G.SIDESTICK || g === G.TOM || g === G.CONGA;
const peakOf = mono => { let pk = 1e-9; for (const x of mono) pk = Math.max(pk, Math.abs(x)); return pk; };
/* a recording (mono, any rate) -> a ROM layer {rate, clk, dyn, data: Float64Array}; open: an open hat; dyn: its
   dynamic (-1 none); norm: the peak it's scaled by (the loudest of its sound's dynamics; its own peak if not given) */
function makeLayer(g, mono, rate, open, dyn = -1, norm = 0){
  const pk = peakOf(mono), scale = norm > 0 ? norm : pk;
  let st = 0; while (st < mono.length - 1 && Math.abs(mono[st]) / pk < 0.002) st++;
  let end = mono.length; while (end > st + 16 && Math.abs(mono[end - 1]) / pk < 0.002) end--;
  const n = end - st, d = new Float64Array(n);
  for (let i = 0; i < n; i++) d[i] = mono[st + i] / scale;
  let clk = DEFAULT_CLOCK(g);
  if (TUNED(g)){ const est = ROM_SIZE[g] * rate / n; if (est >= 8000 && est <= 80000) clk = est; }
  if (g === G.HAT && open){
    // take the open-hat VCA's decay back out (hold, then 1M x 1u from 5 V)
    const hold = D.HAT_HOLD * rate, k = Math.exp(-1 / (D.hatTau(true, 0) * rate)); let eg = 5;
    for (let i = 0; i < n; i++){ if (i >= hold) eg *= k; d[i] /= Math.max(0.05, D.hatVca(eg)); }
    let p2 = 1e-9; for (const x of d) p2 = Math.max(p2, Math.abs(x)); for (let i = 0; i < n; i++) d[i] /= p2;
  }
  return {rate, clk:Math.round(clk), dyn, data:d};
}
/* every file of a kit -> layers [{g, rate, clk, dyn, data, i}] (recs: [{mono, rate}] in the order of names) */
function buildKit(names, recs){
  const {groups, dyn, left} = classify(names), out = [];
  groups.forEach((list, g) => {
    const withDyn = list.some(i => dyn[i] >= 0);
    const norm = withDyn ? Math.max(...list.map(i => peakOf(recs[i].mono))) : 0;
    const ls = list.map(i => Object.assign(makeLayer(g, recs[i].mono, recs[i].rate, g === G.HAT && isOpen(names[i]), dyn[i], norm), {g, i}));
    // the dynamics of one sound were recorded at one tuning: they share the loudest one's clock (a soft hit's tail
    // fades under the noise sooner, so its length says less)
    if (withDyn && TUNED(g)){ const top = ls.reduce((a, b) => b.dyn > a.dyn ? b : a); for (const L of ls) L.clk = top.clk; }
    ls.sort((a, b) => a.dyn - b.dyn || a.clk - b.clk); out.push(...ls);
  });
  return {layers:out, left};
}
const API = {classify, groupOf, isOpen, dynOf, makeLayer, buildKit, DEFAULT_CLOCK, TUNED, words, peakOf};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OLKIT = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
