/* Omega 7: the 32 voices in the internal memory when nothing has been stored yet. Original programs (no factory
 * ROM data), written with the same parameters as the panel. Each operator: ratio (or {hz}), output level, EG rates,
 * EG levels, and extras. Works in the browser and in Node; plugins/seven/Voices.inc is generated from this file. */
(function(root){
'use strict';
const DX = root.DX || require('./engine.js');

// op(ratio, level, rates, levels, extra): extra = {det, vel, rs, bp, ld, rd, lc, rc, ams}
function op(ratio, ol, r, l, x){ return Object.assign({ratio, ol, r, l}, x || {}); }
const OFF = op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]);
// voice(name, alg 1-32, fb, [op1..op6], extra) extra = {lfo:[speed, delay, pmd, amd, sync, wave], pms, peg:[r1..r4, l1..l4], oks, tr}
function voice(name, alg, fb, ops, x){
  x = x || {};
  const v = DX.initVoice();
  for (let i = 0; i < 6; i++){
    const o = ops[i] || OFF, b = DX.opBase(i + 1);
    v.set(o.r, b); v.set(o.l, b + 4);
    v[b + 8] = o.bp != null ? o.bp : 39; v[b + 9] = o.ld || 0; v[b + 10] = o.rd || 0; v[b + 11] = o.lc || 0; v[b + 12] = o.rc || 0;
    v[b + 13] = o.rs != null ? o.rs : 2; v[b + 14] = o.ams || 0; v[b + 15] = o.vel != null ? o.vel : 2; v[b + 16] = o.ol;
    if (o.hz){ const lg = Math.log10(o.hz), c = Math.floor(lg); v[b + 17] = 1; v[b + 18] = Math.min(3, c); v[b + 19] = Math.min(99, Math.round((lg - c) * 100)); }
    else {
      const c = o.ratio < 1 ? 0 : Math.floor(o.ratio), base = c === 0 ? 0.5 : c;
      v[b + 17] = 0; v[b + 18] = c; v[b + 19] = Math.max(0, Math.min(99, Math.round((o.ratio / base - 1) * 100)));
    }
    v[b + 20] = 7 + (o.det || 0);
  }
  const peg = x.peg || [99, 99, 99, 99, 50, 50, 50, 50], lfo = x.lfo || [35, 0, 0, 0, 1, 0];
  v.set(peg, 126);
  v[134] = alg - 1; v[135] = fb; v[136] = x.oks != null ? x.oks : 1;
  v[137] = lfo[0]; v[138] = lfo[1]; v[139] = lfo[2]; v[140] = lfo[3]; v[141] = lfo[4]; v[142] = lfo[5];
  v[143] = x.pms != null ? x.pms : 3; v[144] = 24 + (x.tr || 0);
  DX.setName(v, name.toUpperCase().slice(0, 10));
  return v;
}

const VIB = [34, 30, 5, 0, 0, 4];     // a gentle delayed vibrato (sine)
const FACTORY = [
  // 1 brass: three slightly detuned carriers driven by one feedback saw, plus a pair for the bite
  voice('Brass Sect', 22, 7, [
    op(1, 97, [70, 60, 40, 62], [99, 92, 90, 0], {vel:1}), op(1, 74, [55, 45, 30, 60], [90, 99, 92, 0], {vel:3, rd:20, rc:0}),
    op(1, 94, [66, 60, 40, 62], [99, 93, 90, 0], {det:3, vel:1}), op(1, 92, [64, 60, 40, 62], [99, 93, 90, 0], {det:-3, vel:1}),
    op(1, 90, [68, 60, 40, 62], [99, 93, 90, 0], {det:1, vel:1}), op(1, 78, [52, 40, 30, 58], [94, 99, 90, 0], {vel:3})
  ], {lfo:VIB, pms:3, peg:[84, 95, 95, 60, 47, 50, 50, 50]}),
  // 2 electric piano: a tine pair, a body pair, and a soft bell on top
  voice('Tine Piano', 5, 6, [
    op(1, 98, [96, 25, 25, 67], [99, 75, 0, 0], {vel:2, rs:3}), op(1, 60, [95, 50, 35, 78], [99, 75, 0, 0], {vel:7, rs:3, rd:30}),
    op(1, 96, [95, 20, 20, 50], [99, 95, 0, 0], {det:1, vel:2, rs:3}), op(14, 58, [95, 29, 20, 50], [99, 95, 0, 0], {vel:7, rs:4, rd:40}),
    op(1, 92, [95, 20, 20, 50], [99, 95, 0, 0], {det:-1, vel:2, rs:3}), op(1, 64, [95, 29, 20, 50], [99, 95, 0, 0], {vel:6, rs:3})
  ]),
  // 3 organ: six drawbars, a touch of key click on the top one
  voice('Drawbars', 32, 2, [
    op(0.5, 92, [99, 80, 99, 90], [99, 99, 99, 0], {vel:0}), op(1, 94, [99, 80, 99, 90], [99, 99, 99, 0], {vel:0}),
    op(1.5, 84, [99, 80, 99, 90], [99, 99, 99, 0], {vel:0}), op(2, 88, [99, 80, 99, 90], [99, 99, 99, 0], {vel:0}),
    op(3, 82, [99, 80, 99, 90], [99, 99, 99, 0], {vel:0}), op(4, 80, [99, 70, 99, 90], [99, 92, 92, 0], {vel:0})
  ], {lfo:[30, 0, 3, 0, 0, 4], pms:2, oks:0}),
  // 4 guitar: a plucked string with a bright decaying attack
  voice('Nylon Gtr', 7, 5, [
    op(1, 99, [99, 40, 22, 60], [99, 75, 0, 0], {vel:2, rs:3}), op(3, 70, [99, 55, 30, 60], [99, 60, 0, 0], {vel:5, rs:3, rd:40}),
    op(1, 92, [99, 45, 22, 60], [99, 72, 0, 0], {det:2, vel:2, rs:3}), op(5, 62, [99, 70, 30, 60], [99, 50, 0, 0], {vel:6, rs:4}),
    op(1, 66, [99, 60, 30, 60], [99, 60, 0, 0], {vel:5, rs:3}), op(2, 55, [99, 70, 30, 60], [99, 40, 0, 0], {vel:4})
  ]),
  // 5 bass: an octave-down carrier pushed hard by a fast-decaying modulator
  voice('Solid Bass', 16, 6, [
    op(0.5, 99, [96, 40, 25, 70], [99, 85, 0, 0], {vel:2, rs:2}), op(0.5, 80, [99, 50, 30, 70], [99, 80, 0, 0], {vel:5}),
    op(1, 70, [99, 60, 30, 70], [99, 60, 0, 0], {vel:5}), op(3, 52, [99, 70, 30, 70], [99, 50, 0, 0], {vel:6}),
    op(1, 72, [99, 55, 30, 70], [99, 70, 0, 0], {vel:4}), op(1, 60, [99, 55, 30, 70], [99, 70, 0, 0], {vel:2})
  ], {tr:-12}),
  // 6 strings: slow swell, detuned carriers, saw-ish modulator
  voice('Strings', 2, 7, [
    op(1, 96, [44, 30, 20, 42], [99, 94, 92, 0], {vel:1, det:2}), op(1, 74, [40, 30, 20, 42], [95, 92, 90, 0], {vel:1}),
    op(1, 95, [42, 30, 20, 42], [99, 94, 92, 0], {vel:1, det:-2}), op(1, 72, [40, 30, 20, 42], [95, 92, 90, 0], {vel:1, det:-1}),
    op(2, 60, [40, 30, 20, 42], [95, 92, 90, 0], {vel:1}), op(1, 70, [40, 30, 20, 42], [95, 92, 90, 0], {vel:1})
  ], {lfo:[32, 40, 6, 0, 0, 0], pms:3}),
  // 7 ensemble: three soft voices, each a gentle pair
  voice('Choir Pad', 5, 3, [
    op(1, 95, [40, 30, 25, 40], [99, 95, 93, 0], {vel:1, det:3}), op(2, 58, [40, 30, 25, 40], [90, 85, 85, 0], {vel:1}),
    op(1, 94, [40, 30, 25, 40], [99, 95, 93, 0], {vel:1, det:-3}), op(1, 64, [40, 30, 25, 40], [90, 85, 85, 0], {vel:1}),
    op(2, 88, [40, 30, 25, 40], [99, 95, 93, 0], {vel:1}), op(3, 56, [40, 30, 25, 40], [90, 85, 85, 0], {vel:1})
  ], {lfo:[28, 30, 7, 0, 0, 4], pms:3}),
  // 8 synth brass: one big stack with a swelling filter-like modulator
  voice('Synth Brass', 18, 6, [
    op(1, 99, [80, 50, 40, 60], [99, 95, 95, 0], {vel:1}), op(1, 76, [48, 38, 30, 55], [92, 99, 88, 0], {vel:3}),
    op(1, 68, [60, 40, 30, 60], [96, 90, 85, 0], {vel:2}), op(2, 60, [55, 40, 30, 60], [95, 85, 80, 0], {vel:2}),
    op(1, 55, [60, 40, 30, 60], [95, 85, 80, 0], {vel:2, det:4}), op(1, 50, [60, 40, 30, 60], [95, 85, 80, 0], {vel:2})
  ], {lfo:VIB, pms:2, peg:[70, 99, 99, 60, 46, 50, 50, 50]}),
  // 9 reed: ratio 1:2 for odd harmonics, breathy attack
  voice('Clarinet', 2, 2, [
    op(1, 98, [62, 50, 40, 65], [99, 97, 95, 0], {vel:1}), op(2, 76, [56, 45, 40, 65], [96, 92, 90, 0], {vel:3}),
    op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]),
    op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0])
  ], {lfo:[33, 45, 6, 0, 0, 4], pms:3}),
  // 10 lead: a feedback sawtooth with a second one detuned
  voice('Saw Lead', 31, 7, [
    op(1, 90, [92, 60, 50, 70], [99, 95, 95, 0], {vel:1, det:2}), op(1, 88, [92, 60, 50, 70], [99, 95, 95, 0], {vel:1, det:-2}),
    op(2, 72, [92, 60, 50, 70], [99, 95, 95, 0], {vel:1}), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]),
    op(1, 96, [92, 60, 50, 70], [99, 95, 95, 0], {vel:1}), op(1, 82, [92, 60, 50, 70], [99, 97, 95, 0], {vel:1})
  ], {lfo:[35, 40, 8, 0, 0, 4], pms:3}),
  // 11 pad: glassy partials over a soft body
  voice('Glass Pad', 5, 4, [
    op(1, 95, [35, 25, 20, 30], [99, 95, 93, 0], {vel:1}), op(3.5, 58, [35, 25, 20, 30], [99, 90, 80, 0], {vel:2}),
    op(1, 90, [35, 25, 20, 30], [99, 95, 93, 0], {vel:1, det:3}), op(7, 48, [40, 25, 20, 30], [99, 85, 75, 0], {vel:2}),
    op(2, 80, [35, 25, 20, 30], [99, 95, 93, 0], {vel:1, det:-3}), op(1, 60, [35, 25, 20, 30], [99, 90, 85, 0], {vel:1})
  ], {lfo:[25, 50, 4, 10, 0, 0], pms:2}),
  // 12 ethnic: a plucked koto-like string
  voice('Koto Pluck', 2, 4, [
    op(1, 99, [99, 48, 30, 60], [99, 70, 0, 0], {vel:2, rs:3}), op(3, 74, [99, 60, 35, 60], [99, 55, 0, 0], {vel:5, rs:3}),
    op(1, 90, [99, 52, 30, 60], [99, 65, 0, 0], {vel:2, rs:3, det:1}), op(1, 70, [99, 66, 35, 60], [99, 50, 0, 0], {vel:5}),
    op(4, 58, [99, 70, 35, 60], [99, 40, 0, 0], {vel:5}), op(1, 50, [99, 70, 35, 60], [99, 40, 0, 0], {vel:3})
  ]),
  // 13 percussive: marimba bars
  voice('Marimba', 5, 0, [
    op(1, 99, [99, 50, 35, 55], [99, 60, 0, 0], {vel:3, rs:3}), op(4, 66, [99, 72, 40, 60], [99, 45, 0, 0], {vel:6, rs:4}),
    op(4, 72, [99, 70, 40, 60], [99, 40, 0, 0], {vel:4, rs:4}), op(1, 50, [99, 72, 40, 60], [99, 40, 0, 0], {vel:4}),
    op(10, 50, [99, 80, 50, 60], [99, 30, 0, 0], {vel:5, rs:5}), op(1, 40, [99, 80, 40, 60], [99, 30, 0, 0], {vel:4})
  ], {oks:1}),
  // 14 sound effect: wind made of a noisy feedback operator swept by the S/H LFO
  voice('Space Wind', 16, 7, [
    op(1, 92, [30, 25, 20, 30], [99, 90, 85, 0], {vel:0, ams:2}), op(7, 70, [30, 25, 20, 30], [99, 90, 85, 0], {vel:0}),
    op(3, 60, [30, 25, 20, 30], [99, 90, 85, 0], {vel:0}), op(5, 66, [30, 25, 20, 30], [99, 90, 85, 0], {vel:0}),
    op(11, 90, [30, 25, 20, 30], [99, 95, 90, 0], {vel:0}), op(13, 99, [30, 25, 20, 30], [99, 99, 99, 0], {vel:0})
  ], {lfo:[12, 0, 30, 30, 0, 5], pms:5}),
  // 15 drums: a snare from fixed-frequency noise and a tuned body
  voice('Snare Drum', 16, 7, [
    op(1, 99, [99, 52, 40, 60], [99, 0, 0, 0], {hz:180, vel:4, rs:0}), op(1, 85, [99, 60, 40, 60], [99, 0, 0, 0], {hz:330, vel:3, rs:0}),
    op(1, 90, [99, 50, 40, 60], [99, 0, 0, 0], {hz:2500, vel:3, rs:0}), op(1, 85, [99, 50, 40, 60], [99, 0, 0, 0], {hz:6300, vel:2, rs:0}),
    op(1, 95, [99, 50, 40, 60], [99, 0, 0, 0], {hz:4000, vel:2, rs:0}), op(1, 99, [99, 50, 40, 60], [99, 0, 0, 0], {hz:7900, vel:0, rs:0})
  ], {peg:[99, 70, 99, 99, 62, 50, 50, 50]}),
  // 16 bells: tubular bell partials
  voice('Tube Bells', 5, 3, [
    op(1, 99, [95, 30, 20, 30], [99, 80, 0, 0], {vel:2, rs:2}), op(3.5, 72, [95, 35, 20, 30], [99, 75, 0, 0], {vel:5, rs:2}),
    op(1, 96, [95, 28, 20, 30], [99, 80, 0, 0], {vel:2, det:3, rs:2}), op(3.5, 70, [95, 35, 20, 30], [99, 75, 0, 0], {vel:5, det:-3}),
    op(2, 86, [95, 32, 20, 30], [99, 70, 0, 0], {vel:2}), op(5.19, 66, [95, 40, 20, 30], [99, 60, 0, 0], {vel:4})
  ]),
  // 17 effect: laser zap, a pitch envelope dive
  voice('Laser Zap', 1, 7, [
    op(1, 99, [99, 50, 40, 70], [99, 60, 0, 0], {vel:2}), op(1, 80, [99, 60, 40, 70], [99, 50, 0, 0], {vel:3}),
    op(2, 90, [99, 50, 40, 70], [99, 60, 0, 0], {vel:2}), op(3, 70, [99, 60, 40, 70], [99, 50, 0, 0]),
    op(1, 60, [99, 60, 40, 70], [99, 50, 0, 0]), op(1, 70, [99, 60, 40, 70], [99, 50, 0, 0])
  ], {peg:[99, 45, 99, 99, 99, 0, 0, 50]}),
  // 18 effect: a slow swept pad (LFO on the modulators)
  voice('Sweep Pad', 1, 5, [
    op(1, 96, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0}), op(1, 74, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0, ams:2}),
    op(1, 94, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0, det:4}), op(2, 70, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0, ams:3}),
    op(1, 68, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0, ams:1}), op(1, 64, [30, 25, 25, 30], [99, 95, 95, 0], {vel:0})
  ], {lfo:[18, 30, 3, 60, 0, 0], pms:2}),
  // 19 percussive: vibraphone with tremolo
  voice('Vibraphone', 5, 0, [
    op(1, 99, [95, 30, 25, 40], [99, 75, 0, 0], {vel:2, ams:1, rs:2}), op(4, 64, [99, 55, 30, 50], [99, 40, 0, 0], {vel:6}),
    op(1, 92, [95, 30, 25, 40], [99, 75, 0, 0], {vel:2, ams:1, det:1}), op(1, 50, [99, 50, 30, 50], [99, 50, 0, 0], {vel:5}),
    op(4, 72, [99, 60, 30, 50], [99, 40, 0, 0], {vel:4, ams:1}), op(10, 52, [99, 70, 30, 50], [99, 30, 0, 0], {vel:5})
  ], {lfo:[38, 0, 0, 30, 0, 4], pms:0}),
  // 20 ethnic: a buzzing sitar-like string
  voice('Sitar Buzz', 3, 6, [
    op(1, 99, [99, 30, 25, 50], [99, 80, 0, 0], {vel:2, rs:2}), op(1, 74, [99, 34, 25, 50], [99, 80, 0, 0], {vel:4}),
    op(5, 70, [99, 40, 25, 50], [99, 70, 0, 0], {vel:4}), op(1, 90, [99, 30, 25, 50], [99, 80, 0, 0], {vel:2, det:2}),
    op(3, 64, [99, 34, 25, 50], [99, 75, 0, 0], {vel:4}), op(7, 70, [99, 30, 25, 50], [99, 80, 0, 0], {vel:4})
  ], {peg:[90, 60, 99, 99, 54, 50, 50, 50]}),
  // 21 lead: square-ish lead (ratio 1:2)
  voice('Square Lead', 1, 0, [
    op(1, 97, [90, 60, 50, 70], [99, 96, 95, 0], {vel:1}), op(2, 82, [90, 60, 50, 70], [99, 96, 95, 0], {vel:2}),
    op(1, 70, [90, 60, 50, 70], [99, 96, 95, 0], {vel:1, det:5}), op(2, 76, [90, 60, 50, 70], [99, 96, 95, 0], {vel:2}),
    op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0])
  ], {lfo:[36, 40, 7, 0, 0, 0], pms:3}),
  // 22 pad: warm, slow, wide
  voice('Warm Pad', 22, 5, [
    op(1, 94, [34, 28, 22, 32], [99, 96, 94, 0], {vel:0}), op(1, 64, [30, 28, 22, 32], [99, 92, 90, 0], {vel:0}),
    op(1, 92, [34, 28, 22, 32], [99, 96, 94, 0], {vel:0, det:4}), op(0.5, 92, [34, 28, 22, 32], [99, 96, 94, 0], {vel:0, det:-4}),
    op(2, 84, [34, 28, 22, 32], [99, 96, 94, 0], {vel:0}), op(1, 66, [30, 28, 22, 32], [99, 92, 90, 0], {vel:0})
  ], {lfo:[24, 50, 4, 0, 0, 4], pms:3}),
  // 23 reed / pipe: flute with breath
  voice('Flute', 7, 7, [
    op(1, 99, [60, 45, 40, 62], [99, 96, 95, 0], {vel:1}), op(1, 56, [70, 50, 40, 62], [92, 88, 85, 0], {vel:3}),
    op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]),
    op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0]), op(1, 0, [99, 99, 99, 99], [99, 99, 99, 0])
  ], {lfo:[34, 50, 7, 3, 0, 4], pms:3}),
  // 24 brass: solo trumpet
  voice('Trumpet', 16, 6, [
    op(1, 99, [70, 50, 40, 65], [99, 95, 94, 0], {vel:1}), op(1, 76, [56, 40, 35, 62], [90, 99, 94, 0], {vel:3}),
    op(1, 70, [60, 40, 35, 62], [95, 92, 90, 0], {vel:3}), op(3, 50, [60, 40, 35, 62], [95, 90, 88, 0], {vel:3}),
    op(2, 60, [60, 40, 35, 62], [95, 92, 90, 0], {vel:3}), op(1, 58, [60, 40, 35, 62], [95, 92, 90, 0], {vel:2})
  ], {lfo:[35, 45, 6, 0, 0, 4], pms:3, peg:[80, 99, 99, 60, 45, 50, 50, 50]}),
  // 25 brass: french horns, softer
  voice('Horns', 16, 5, [
    op(1, 99, [56, 45, 40, 60], [99, 95, 94, 0], {vel:1}), op(1, 64, [46, 40, 35, 58], [88, 99, 94, 0], {vel:3}),
    op(1, 60, [50, 40, 35, 58], [95, 92, 90, 0], {vel:2}), op(2, 46, [50, 40, 35, 58], [95, 90, 88, 0], {vel:2}),
    op(1, 54, [50, 40, 35, 58], [95, 92, 90, 0], {vel:2}), op(0.5, 56, [50, 40, 35, 58], [95, 92, 90, 0], {vel:2})
  ], {lfo:[33, 50, 4, 0, 0, 4], pms:3, tr:-12}),
  // 26 ensemble: voices "aah"
  voice('Voices Aah', 2, 1, [
    op(1, 95, [42, 32, 26, 40], [99, 96, 94, 0], {vel:1}), op(3, 58, [42, 32, 26, 40], [92, 90, 88, 0], {vel:1}),
    op(1, 92, [42, 32, 26, 40], [99, 96, 94, 0], {vel:1, det:4}), op(1, 70, [42, 32, 26, 40], [92, 90, 88, 0], {vel:1}),
    op(5, 52, [42, 32, 26, 40], [92, 90, 88, 0], {vel:1}), op(1, 60, [42, 32, 26, 40], [92, 90, 88, 0], {vel:1})
  ], {lfo:[30, 45, 7, 0, 0, 4], pms:3}),
  // 27 strings: slow, with a darker body
  voice('Slow Strng', 1, 7, [
    op(1, 96, [36, 26, 20, 36], [99, 95, 93, 0], {vel:0, det:3}), op(1, 70, [32, 26, 20, 36], [94, 92, 90, 0], {vel:0}),
    op(1, 95, [36, 26, 20, 36], [99, 95, 93, 0], {vel:0, det:-3}), op(1, 68, [32, 26, 20, 36], [94, 92, 90, 0], {vel:0}),
    op(2, 60, [32, 26, 20, 36], [94, 92, 90, 0], {vel:0}), op(1, 74, [32, 26, 20, 36], [94, 92, 90, 0], {vel:0})
  ], {lfo:[31, 50, 6, 0, 0, 0], pms:3}),
  // 28 bass: slap with a pitch blip
  voice('Slap Bass', 16, 7, [
    op(1, 99, [99, 50, 30, 70], [99, 82, 0, 0], {vel:2}), op(1, 82, [99, 72, 35, 70], [99, 60, 0, 0], {vel:6}),
    op(3, 74, [99, 80, 40, 70], [99, 40, 0, 0], {vel:6}), op(5, 54, [99, 80, 40, 70], [99, 30, 0, 0], {vel:6}),
    op(1, 76, [99, 60, 40, 70], [99, 60, 0, 0], {vel:5}), op(2, 66, [99, 70, 40, 70], [99, 50, 0, 0], {vel:4})
  ], {tr:-12, peg:[99, 85, 99, 99, 54, 50, 50, 50]}),
  // 29 guitar: clean electric
  voice('Clean Gtr', 7, 4, [
    op(1, 99, [99, 38, 24, 58], [99, 80, 0, 0], {vel:2, rs:3}), op(1, 74, [99, 50, 30, 60], [99, 60, 0, 0], {vel:5}),
    op(1, 94, [99, 42, 24, 58], [99, 78, 0, 0], {vel:2, det:2, rs:3}), op(3, 64, [99, 60, 30, 60], [99, 50, 0, 0], {vel:5}),
    op(2, 60, [99, 60, 30, 60], [99, 50, 0, 0], {vel:5}), op(1, 50, [99, 60, 30, 60], [99, 50, 0, 0], {vel:3})
  ]),
  // 30 organ: percussive organ with a 3rd-harmonic click
  voice('Perc Organ', 32, 0, [
    op(1, 95, [99, 80, 99, 85], [99, 99, 99, 0], {vel:0}), op(2, 86, [99, 80, 99, 85], [99, 99, 99, 0], {vel:0}),
    op(3, 90, [99, 55, 30, 85], [99, 0, 0, 0], {vel:2}), op(0.5, 84, [99, 80, 99, 85], [99, 99, 99, 0], {vel:0}),
    op(4, 70, [99, 80, 99, 85], [99, 99, 99, 0], {vel:0}), op(6, 62, [99, 80, 99, 85], [99, 99, 99, 0], {vel:0})
  ], {lfo:[32, 0, 3, 0, 0, 4], pms:2, oks:0}),
  // 31 keyboard: harpsichord
  voice('Harpsichrd', 5, 6, [
    op(1, 98, [99, 40, 30, 70], [99, 80, 0, 0], {vel:0, rs:3}), op(4, 74, [99, 50, 30, 70], [99, 70, 0, 0], {vel:0, rs:3}),
    op(2, 92, [99, 42, 30, 70], [99, 78, 0, 0], {vel:0, rs:3, det:2}), op(5, 72, [99, 50, 30, 70], [99, 70, 0, 0], {vel:0, rs:3}),
    op(1, 86, [99, 44, 30, 70], [99, 76, 0, 0], {vel:0, rs:3, det:-2}), op(3, 76, [99, 50, 30, 70], [99, 70, 0, 0], {vel:0})
  ]),
  // 32 brass: a bright fanfare stack
  voice('Fanfare', 22, 7, [
    op(1, 97, [76, 60, 40, 64], [99, 94, 92, 0], {vel:1}), op(2, 70, [60, 45, 30, 60], [90, 99, 92, 0], {vel:3}),
    op(1, 94, [74, 60, 40, 64], [99, 94, 92, 0], {det:4, vel:1}), op(2, 90, [72, 60, 40, 64], [99, 94, 92, 0], {det:-4, vel:1}),
    op(1, 92, [76, 60, 40, 64], [99, 94, 92, 0], {vel:1}), op(1, 82, [56, 40, 30, 58], [94, 99, 92, 0], {vel:3})
  ], {lfo:VIB, pms:3, peg:[84, 95, 95, 60, 47, 50, 50, 50]})
];
// the labels printed under the 32 buttons (voice families)
const GROUPS = ['BRASS', 'KEYBOARD', 'ORGAN', 'GUITAR', 'BASS', 'STRINGS', 'ENSEMBLE', 'BRASS', 'REED/PIPE', 'SYN LEAD', 'SYN PAD', 'ETHNIC', 'PERCUSSIVE', 'SOUND EFF',
  'DRUMS', 'BELLS', 'SOUND EFF', 'FX', 'PERCUSSIVE', 'ETHNIC', 'SYN LEAD', 'SYN PAD', 'REED/PIPE', 'BRASS', 'BRASS', 'ENSEMBLE', 'STRINGS', 'BASS', 'GUITAR', 'ORGAN',
  'KEYBOARD', 'BRASS'];
const api = {FACTORY, GROUPS, makeVoice:voice, op};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
root.DXV = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
