/* The mix: studio effects of 1984 as sends and channel inserts, the cymbal bus, dub throws and drops.
   Knob values are stored 0..1 and mapped to each unit's real range (lin or exp); selectors store numbers. */
const FMTS = [];
const fmtId = f => { let i = FMTS.indexOf(f); if (i < 0){ FMTS.push(f); i = FMTS.length - 1; } return i; };
const ms = v => v >= 1000 ? (v / 1000).toFixed(2) + 's' : (v < 10 ? v.toFixed(1) : Math.round(v)) + 'ms', pc = v => Math.round(v * 100) + '%', hz = v => v >= 1000 ? (v / 1000).toFixed(1) + 'k' : (v < 10 ? v.toFixed(1) : Math.round(v)) + 'Hz';
// R: a knob over a real range
const RK = (k, l, g, min, max, curve = 'lin', fmt = null) => ({t:'k', k, l, g, min, max, curve, fmt:fmt ? fmtId(fmt) : null});
const toN = (c, x) => c.curve === 'exp' ? Math.log(x / c.min) / Math.log(c.max / c.min) : (x - c.min) / (c.max - c.min);
const toX = (c, v) => c.curve === 'exp' ? c.min * Math.pow(c.max / c.min, v) : c.min + (c.max - c.min) * v;
const SYNCS = [[0, 'Free'], [.25, '1/16'], [.5, '1/8'], [.75, '3/16'], [1, '1/4'], [1.5, '3/8'], [2, '1/2']];
const LX_PROGS = ['vplate', 'pplate', 'hall', 'room', 'chamber', 'rplate', 'rhall', 'ambience', 'nonlin', 'reverse'];
const EF_RANGES = ['fhi', 'fmid', 'flo', 'dbl', 'short', 'long'], EF_PANS = ['center', 'haasL', 'haasR', 'wide'];

const FXU = {
  // ---- returns (sends)
  pt:{name:'Lexicon PrimeTime', maker:'Model 93 digital delay · 1978', proc:'primetime', ret:true, short:'PrimeTime',
    ctl:[SEL('sync', 'Delay A', 'Delay', SYNCS), RK('dA', 'A time', 'Delay', 1, 1024, 'exp', ms), SEL('syncB', 'Delay B', 'Delay', SYNCS), RK('dB', 'B time', 'Delay', 1, 1024, 'exp', ms),
      SEL('mult', 'Delay ×', 'Delay', [[1, '×1'], [2, '×2'], [4, '×4'], [8, '×8']]),
      RK('fbA', 'Recirc A', 'Recirculate', 0, .95, 'lin', pc), RK('fbB', 'Recirc B', 'Recirculate', 0, .95, 'lin', pc), RK('hcut', 'HF Cut', 'Recirculate', 800, 15000, 'exp', hz), SEL('inv', 'Phase', 'Recirculate', [[0, 'Normal'], [1, 'Invert']]),
      RK('vcoRate', 'Rate', 'VCO', .1, 20, 'exp', hz), RK('vcoDepth', 'Depth', 'VCO', 0, 1, 'lin', pc), RK('mixA', 'Out A', 'Output', 0, 1, 'lin', pc), RK('mixB', 'Out B', 'Output', 0, 1, 'lin', pc)],
    def:{sync:.5, dA:180, syncB:.75, dB:240, mult:4, fbA:.3, fbB:.15, hcut:4500, inv:0, vcoRate:.5, vcoDepth:0, mixA:1, mixB:.8},
    presets:{'Echo throw 1/8 + 3/16':{sync:.5, syncB:.75, mult:4, fbA:.45, fbB:.35, hcut:3500}, 'Dub 1/4 wash':{sync:1, syncB:1.5, mult:8, fbA:.7, fbB:.5, hcut:1800},
      'Vocal slapback 110 ms':{sync:0, syncB:0, dA:110, dB:125, mult:1, fbA:.1, fbB:0, hcut:8000}, 'Doubling 25 ms':{sync:0, syncB:0, dA:25, dB:33, mult:1, fbA:0, fbB:0, vcoRate:1.1, vcoDepth:.2, hcut:12000},
      'Synth echo 3/16':{sync:.75, syncB:.75, mult:4, fbA:.35, fbB:.35, hcut:5000, mixB:.6}, 'Flange (VCO swept)':{sync:0, syncB:0, dA:6, dB:8, mult:1, fbA:.7, vcoRate:.25, vcoDepth:.8, mixB:0}}},
  ef:{name:'Deltalab Effectron', maker:'ADM 1024 · 1980', proc:'effectron', ret:true, short:'Effectron',
    ctl:[SEL('range', 'Range', 'Delay', EF_RANGES.map((r, i) => [i, ['Fl hi', 'Fl mid', 'Fl lo', 'Double', 'Short', 'Long'][i]])), RK('delay', 'Delay', 'Delay', .25, 1024, 'exp', ms),
      RK('regen', 'Regen', 'Delay', -.95, .95, 'lin', pc), RK('lfoRate', 'Rate', 'Modulation', .1, 10, 'exp', hz), RK('width', 'Width', 'Modulation', 0, 1, 'lin', pc), RK('grit', 'ADM grit', 'Modulation', 0, 1, 'lin', pc),
      SEL('pan', 'Output', 'Output', EF_PANS.map((p, i) => [i, ['Centre', 'Haas L', 'Haas R', 'Wide'][i]]))],
    def:{range:3, delay:22, regen:0, lfoRate:.5, width:0, grit:.4, pan:2},
    presets:{'Haas double 22 ms':{range:3, delay:22, regen:0, pan:2}, 'Stereo widener':{range:3, delay:16, pan:3}, 'Slapback 110 ms':{range:4, delay:110, regen:.15, pan:0},
      'Jet flange':{range:1, delay:3, width:.8, lfoRate:.2, regen:.75, pan:0}, 'Long gritty echo':{range:5, delay:620, regen:.45, grit:.8, pan:0}}},
  h9:{name:'Eventide H910 Harmonizer', maker:'Pitch change and delay · 1975', proc:'h910', ret:true, short:'H910',
    ctl:[RK('wings', 'Wings', 'Pitch', 0, 1, 'lin', pc), RK('cents', 'Detune', 'Pitch', 0, 50, 'lin', v => '±' + v.toFixed(1) + 'c'), SEL('splice', 'Splice', 'Pitch', [[0, 'H910 raw'], [1, 'H949 smooth']]),
      RK('dL', 'Delay L', 'Delay', 0, 112.5, 'lin', ms), RK('dR', 'Delay R', 'Delay', 0, 112.5, 'lin', ms), RK('spread', 'Spread', 'Delay', 0, 1, 'lin', pc), RK('fb', 'Feedback', 'Delay', 0, 1.1, 'lin', pc)],
    def:{wings:.7, cents:9, splice:0, dL:15, dR:25, spread:.8, fb:0},
    presets:{'Vocal wings ±9 c':{cents:9, dL:15, dR:25, spread:.85, splice:0}, 'Wide doubler':{cents:6, dL:30, dR:45, spread:1, splice:1}, 'Robot thickener':{cents:17, dL:7.5, dR:7.5, spread:1},
      'Feedback spiral':{cents:30, dL:60, dR:90, fb:.75}}},
  lx:{name:'Lexicon 224 / AMS RMX16', maker:'Digital reverb · 1978 / 1982', proc:'verb224', ret:true, short:'224 / RMX16',
    ctl:[SEL('prog', 'Program', 'Program', LX_PROGS.map((p, i) => [i, ['224 Vocal Plate', '224 Perc Plate', '224 Hall', '224 Room', '224 Chamber', 'RMX Plate A1', 'RMX Hall C1', 'RMX Ambience', 'RMX Nonlin 2', 'RMX Reverse'][i]])),
      RK('size', 'Size', 'Program', 0, 1, 'lin', pc), RK('pre', 'Pre-delay', 'Program', 0, 250, 'lin', ms), RK('treble', 'Treble', 'Program', 0, 1, 'lin', pc)],
    def:{prog:1, size:.4, pre:10, treble:.6},
    presets:{'Snare plate':{prog:1, size:.35, pre:5, treble:.65}, 'Vocal plate':{prog:0, size:.45, pre:25}, 'Strings hall':{prog:2, size:.7, pre:40, treble:.5},
      'Gated snare (Nonlin 2)':{prog:8, size:.4, pre:0}, 'Tight ambience':{prog:7, size:.3, pre:0, treble:.8}, 'Reverse build':{prog:9, size:.6, pre:0}}},
  pc:{name:'Gated Room', maker:'Live room + listen-mic crush + gate · 1980s', proc:'gatedroom', ret:true, short:'Gated room',
    ctl:[RK('size', 'Size', 'Room', 0, 1, 'lin', pc), RK('decay', 'Decay', 'Room', .3, 5, 'exp', v => v.toFixed(1) + 's'), RK('crush', 'Crush', 'Room', 0, 1, 'lin', pc), RK('tone', 'Tone', 'Room', 0, 1, 'lin', pc),
      RK('thresh', 'Threshold', 'Gate', -60, 0, 'lin', v => Math.round(v) + 'dB'), RK('hold', 'Hold', 'Gate', 20, 800, 'exp', ms), RK('snap', 'Release', 'Gate', 1, 200, 'exp', ms), RK('range', 'Range', 'Gate', 0, 80, 'lin', v => '−' + Math.round(v) + 'dB')],
    def:{size:.7, decay:2.2, crush:.75, tone:.6, thresh:-26, hold:300, snap:15, range:60},
    presets:{'Big 80s snare':{size:.7, decay:2.2, crush:.75, thresh:-26, hold:360, snap:22}, 'Tight gate':{size:.55, decay:1.4, crush:.8, thresh:-24, hold:120, snap:3}, 'Tom fill thunder':{size:.95, decay:3.5, crush:.85, thresh:-34, hold:480, snap:14},
      'Natural room':{size:.7, decay:1.6, crush:.35, thresh:-60, hold:800, snap:200, range:0}}},
  // ---- channel inserts
  phase90:{name:'MXR Phase 90', maker:'4-stage phaser · 1974', proc:'phase90', short:'Phase 90',
    ctl:[RK('speed', 'Speed', 'Phase 90', 0, 1, 'lin', v => (.1 * Math.pow(100, v)).toFixed(2) + 'Hz'), SEL('block', 'Version', 'Phase 90', [[0, 'Script'], [1, 'Block']]), RK('mix', 'Depth', 'Phase 90', 0, 1, 'lin', pc)],
    def:{speed:.3, block:0, mix:1},
    presets:{'Slow script swirl':{speed:.18, block:0}, 'Medium sweep':{speed:.35, block:0}, 'Fast warble (block)':{speed:.62, block:1}}},
  smallstone:{name:'EHX Small Stone', maker:'4-stage OTA phaser · 1975', proc:'smallstone', short:'Small Stone',
    ctl:[RK('rate', 'Rate', 'Small Stone', 0, 1, 'lin', v => (.07 * Math.pow(170, v)).toFixed(2) + 'Hz'), SEL('color', 'Color', 'Small Stone', [[0, 'Off'], [1, 'On']]), RK('mix', 'Depth', 'Small Stone', 0, 1, 'lin', pc)],
    def:{rate:.2, color:1, mix:1},
    presets:{'Cymbal swoosh (color, slow)':{rate:.15, color:1}, 'Hi-hat shimmer':{rate:.3, color:1, mix:.8}, 'Gentle sweep':{rate:.22, color:0}}},
  mistress:{name:'EHX Electric Mistress', maker:'Bucket-brigade flanger · 1976', proc:'mistress', short:'Mistress',
    ctl:[RK('rate', 'Rate', 'Electric Mistress', 0, 1, 'lin', v => (.03 * Math.pow(100, v)).toFixed(2) + 'Hz'), RK('range', 'Range', 'Electric Mistress', 0, 1, 'lin', pc), RK('color', 'Color', 'Electric Mistress', 0, 1, 'lin', pc),
      SEL('matrix', 'Mode', 'Electric Mistress', [[0, 'Flange'], [1, 'Filter Matrix']]), RK('mix', 'Mix', 'Electric Mistress', 0, 1, 'lin', pc)],
    def:{rate:.2, range:.6, color:.55, matrix:0, mix:.5},
    presets:{'Slow jet on cymbals':{rate:.12, range:.75, color:.7, matrix:0, mix:.5}, 'Filter matrix (static metal)':{range:.35, color:.75, matrix:1, mix:.5}, 'Soft chorus-flange':{rate:.3, range:.4, color:.3, mix:.4}}},
  adaflanger:{name:'A/DA Flanger', maker:'SAD1024 flanger · 1977', proc:'adaflanger', short:'A/DA',
    ctl:[RK('manual', 'Manual', 'A/DA', 0, 1, 'lin', v => (.25 * Math.pow(40, v)).toFixed(2) + 'ms'), RK('range', 'Range', 'A/DA', 0, 1, 'lin', pc), RK('speed', 'Speed', 'A/DA', 0, 1, 'lin', v => (.02 * Math.pow(400, v)).toFixed(2) + 'Hz'),
      RK('enh', 'Enhance', 'A/DA', 0, .97, 'lin', pc), SEL('odd', 'Phase', 'A/DA', [[0, 'Even'], [1, 'Odd']]), RK('thresh', 'Threshold', 'A/DA', 0, 1, 'lin', pc), RK('mix', 'Mix', 'A/DA', 0, 1, 'lin', pc)],
    def:{manual:.45, range:.7, speed:.3, enh:.5, odd:0, thresh:0, mix:.5},
    presets:{'Classic jet':{manual:.5, range:.75, speed:.25, enh:.6}, 'Slow 40:1 sweep':{manual:.95, range:1, speed:.12, enh:.55}, 'Metallic odd':{manual:.35, range:.6, speed:.3, enh:.85, odd:1}}},
  chipmunk:{name:'Chipmunk Voice', maker:'Sped-up tape trick, live', proc:'chipmunk', short:'Chipmunk',
    ctl:[SEL('speed', 'Tape speed', 'Chipmunk', [[5, '×1.33'], [7, '×1.5'], [9, '×1.7'], [12, '×2'], [19, '×3']]), RK('fine', 'Fine', 'Chipmunk', -2, 2, 'lin', v => (v > 0 ? '+' : '') + v.toFixed(1) + 'st'),
      RK('tape', 'Tape', 'Chipmunk', 0, 1, 'lin', pc), RK('mix', 'Mix', 'Chipmunk', 0, 1, 'lin', pc)],
    def:{speed:12, fine:0, tape:.3, mix:1},
    presets:{'Tape ×2 (classic chipmunk)':{speed:12, fine:0, tape:.35}, 'Tape ×1.5 (little voice)':{speed:7, tape:.25}, 'Squeaky ×1.7':{speed:9, fine:.5, tape:.3}, 'Tiny ×3':{speed:19, tape:.2}, 'Double-track (half wet)':{speed:12, tape:.3, mix:.5}}},
  urei1176:{name:'UREI 1176LN', maker:'FET limiter · 1967', proc:'urei1176', short:'1176',
    ctl:[RK('input', 'Input', '1176', 0, 1, 'lin', v => Math.round(v * 50 - 20) + 'dB'), RK('output', 'Output', '1176', 0, 1, 'lin', v => Math.round(v * 40 - 30) + 'dB'), RK('attack', 'Attack', '1176', 1, 7, 'lin', v => v.toFixed(1)),
      RK('release', 'Release', '1176', 1, 7, 'lin', v => v.toFixed(1)), SEL('ratio', 'Ratio', '1176', [[4, '4'], [8, '8'], [12, '12'], [20, '20'], [99, 'All']]), RK('mix', 'Mix', '1176', 0, 1, 'lin', pc)],
    def:{input:.55, output:.62, attack:5, release:4, ratio:4, mix:1},
    presets:{'Drum smash (all buttons)':{input:.75, output:.45, attack:6, release:7, ratio:99, mix:.6}, 'Bass even-out 8:1':{input:.6, output:.55, attack:3, release:3, ratio:8}, 'Vocal 4:1':{input:.55, output:.55, attack:5, release:5, ratio:4}}}
};
const RET_U = ['pt', 'ef', 'h9', 'lx', 'pc'], INS_U = ['chipmunk', 'phase90', 'smallstone', 'mistress', 'adaflanger', 'urei1176'];
// stored state for a unit: knobs normalised, selectors raw
function fxVals(store, u){ const U = FXU[u], o = store[u] || (store[u] = {}); for (const c of U.ctl) if (o[c.k] == null){ const x = U.def[c.k]; o[c.k] = c.t === 'k' ? toN(c, x) : x; } return o; }
function fxPreset(store, u, name){ const U = FXU[u], P = U.presets[name]; if (!P) return; const o = store[u] = {}; for (const c of U.ctl){ const x = P[c.k] != null ? P[c.k] : U.def[c.k]; o[c.k] = c.t === 'k' ? clamp(toN(c, x), 0, 1) : x; } o._preset = name; }
// what the worklet gets
function fxCfg(u, vals, bpm){
  const U = FXU[u], c = {ret:1};
  for (const k of U.ctl) c[k.k] = k.t === 'k' ? toX(k, vals[k.k]) : vals[k.k];
  if (u === 'pt'){ if (c.sync) c.dA = c.sync * 60000 / bpm; if (c.syncB) c.dB = c.syncB * 60000 / bpm;
    const need = Math.max(c.dA, c.dB); if (need > 128 * c.mult) c.mult = [1, 2, 4, 8].find(m => 128 * m >= need) || 8; c.dA = Math.min(c.dA, 128 * c.mult); c.dB = Math.min(c.dB, 128 * c.mult); }
  if (u === 'ef'){ c.range = EF_RANGES[c.range] || 'dbl'; c.pan = EF_PANS[c.pan] || 'center'; const r = {fhi:[.25, 1], fmid:[1, 4], flo:[4, 16], dbl:[16, 64], short:[64, 256], long:[256, 1024]}[c.range]; c.delay = clamp(c.delay, r[0], r[1]); }
  if (u === 'h9') c.splice = c.splice ? 'smooth' : 'raw';
  if (u === 'lx') c.prog = LX_PROGS[c.prog] || 'vplate';
  if (u === 'urei1176' && c.ratio === 99) c.ratio = 'all';
  return c;
}
// the voices that go to the cymbal bus by default
const CYM_DEF = {d808:['oh', 'ch', 'cy'], dmx:['chh', 'hha', 'ohh', 'cym', 'ride']};
/* Whole-mix setups. Typical 1984 New York 12-inch mix moves (not documented settings from the record):
   cymbals and hats through a phaser or flanger, a plate and a gated room on the drums, the vocoder echoed and widened. */
const MIX_PRESETS = {
  'Electro 12-inch (Small Stone on cymbals)':{cym:{ins:'smallstone', p:'Cymbal swoosh (color, slow)'}, ret:{pt:'Echo throw 1/8 + 3/16', ef:'Haas double 22 ms', h9:'Vocal wings ±9 c', lx:'Snare plate', pc:'Big 80s snare'},
    snd:{d808:{lx:.25, pc:.3}, dmx:{lx:.25, pc:.3}, voc:{pt:.3, h9:.35, lx:.2}, juno:{lx:.3}, rs09:{lx:.35, ef:.3}, proone:{pt:.15}, sh101:{pt:.25, lx:.15}, tb303:{}, pbass:{}, cym:{lx:.15}}},
  'Electric Mistress jet on cymbals':{cym:{ins:'mistress', p:'Slow jet on cymbals'}, ret:{pt:'Synth echo 3/16', ef:'Stereo widener', h9:'Wide doubler', lx:'Gated snare (Nonlin 2)', pc:'Tight gate'},
    snd:{d808:{lx:.3, pc:.2}, dmx:{lx:.3, pc:.2}, voc:{pt:.25, h9:.3}, juno:{lx:.2, ef:.3}, rs09:{ef:.4}, proone:{pt:.2}, sh101:{pt:.3}, tb303:{}, pbass:{}, cym:{}}},
  'Phase 90 hats, dry drums':{cym:{ins:'phase90', p:'Slow script swirl'}, ret:{pt:'Vocal slapback 110 ms', ef:'Haas double 22 ms', h9:'Vocal wings ±9 c', lx:'Vocal plate', pc:'Natural room'},
    snd:{d808:{pc:.15}, dmx:{pc:.15}, voc:{pt:.25, h9:.3, lx:.25}, juno:{lx:.25}, rs09:{lx:.3}, proone:{}, sh101:{pt:.2}, tb303:{}, pbass:{}, cym:{}}},
  'Dub version':{cym:{ins:'mistress', p:'Filter matrix (static metal)'}, ret:{pt:'Dub 1/4 wash', ef:'Long gritty echo', h9:'Feedback spiral', lx:'Strings hall', pc:'Tom fill thunder'},
    snd:{d808:{pt:.15, lx:.2}, dmx:{pt:.15, lx:.2}, voc:{pt:.45, lx:.3}, juno:{pt:.25, lx:.4}, rs09:{lx:.5}, proone:{pt:.2}, sh101:{pt:.35}, tb303:{pt:.1}, pbass:{}, cym:{pt:.2}}},
  'Dry (no effects)':{cym:{ins:'none'}, ret:{}, snd:{}}
};
// how each unit's panel looks
const FX_LOOK = {
  pt:{bg:'#1d1d1f', ink:'#e9e9e9', knob:'#2c2c2c', ptr:'#f2f2f2', ring:'#555'}, ef:{bg:'#17191d', ink:'#cfe0ff', knob:'#33363c', ptr:'#cfe0ff', ring:'#445'},
  h9:{bg:'#1a1a1a', ink:'#f0f0f0', knob:'#3a3a3a', ptr:'#fff', ring:'#555'}, lx:{bg:'#d9d4c7', ink:'#222', knob:'#3a3a3a', ptr:'#fff', ring:'#999'}, pc:{bg:'#2b2f33', ink:'#e9e9e9', knob:'#555', ptr:'#fff', ring:'#666'},
  phase90:{bg:'#f07a1a', ink:'#1a1a1a', knob:'#1c1c1c', ptr:'#fff', ring:'#a24f0c'}, smallstone:{bg:'#c9c9c4', ink:'#1a1a1a', knob:'#1c1c1c', ptr:'#fff', ring:'#888'},
  chipmunk:{bg:'#3b2a1d', ink:'#f7e25a', knob:'#d8c39a', ptr:'#3b2a1d', ring:'#7a5a3a'},
  mistress:{bg:'#c4c6c9', ink:'#7a1414', knob:'#1c1c1c', ptr:'#fff', ring:'#888'}, adaflanger:{bg:'#26282b', ink:'#e9e9e9', knob:'#555', ptr:'#fff', ring:'#666'},
  urei1176:{bg:'#1b1b1b', ink:'#d9d9d9', knob:'#c9c9c9', ptr:'#111', ring:'#555'}
};
