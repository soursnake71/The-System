/* Omega 110 front panel, drawn in SVG at the hardware's proportions (2000 x 1160 units, measured off a photo of the
   machine). The plugin's editor (plugins/dr110/DR110Editor.cpp) draws the same layout from the same numbers.
   Exposes FP.build(svg) -> {lcd, buttons, knobs, power} and FP.LAYOUT. */
'use strict';
const FP = (() => {
const NS = 'http://www.w3.org/2000/svg';
const LAYOUT = {
  // buttons: id, x, y, w, h
  buttons: [
    ['shift', 105, 733, 155, 105], ['bank', 340, 733, 77, 105],
    ['n1', 465, 730, 75, 105], ['n2', 590, 730, 75, 105], ['n3', 712, 730, 75, 105], ['n4', 832, 730, 75, 105],
    ['n5', 952, 730, 75, 105], ['n6', 1070, 730, 75, 105], ['n7', 1188, 730, 75, 105], ['n8', 1305, 730, 75, 105],
    ['start', 1452, 695, 140, 132], ['stop', 1663, 695, 140, 132],
    ['acc', 110, 905, 186, 122], ['v0', 370, 905, 183, 122], ['v1', 628, 905, 178, 122], ['v2', 888, 905, 178, 122],
    ['v3', 1138, 905, 175, 122], ['v4', 1385, 905, 173, 122], ['v5', 1627, 905, 171, 122],
  ],
  knobs: [['tempo', 1185, 400], ['balance', 1380, 400], ['accent', 1575, 400], ['volume', 1768, 400]],
  knobR: 66,
  lcd: {x:165, y:172, w:690, h:406, gridX:310, gridY:240, cellW:33.4375, cellH:32.6, labelX:172, numY:236, numX0:323.5, numDx:32.75},
};
const el = (tag, attrs, parent, text) => {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (text !== undefined) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
};
const FONT = "'Helvetica Neue', Arial, 'Liberation Sans', sans-serif";
const txt = (p, x, y, s, size, o = {}) => el('text', Object.assign({x, y, 'font-size':size, 'font-family':FONT, 'font-weight':o.w || 700,
  'text-anchor':o.a || 'middle', fill:o.fill || '#1b1b1b', 'letter-spacing':o.ls || 0}, o.fit ? {textLength:o.fit, lengthAdjust:'spacingAndGlyphs'} : {}, o.extra || {}), p, s);
// the small legends over the keys: condensed, fitted to the width they have on the panel
const leg = (p, x, y, s, w) => txt(p, x, y, s, 24, {w:700, fit:w});

// the white line icons on the voice keys (drawn around the key's centre, about 120 x 80)
function icon(g, id, cx, cy){
  const st = {fill:'none', stroke:'#fff', 'stroke-width':5.5, 'stroke-linecap':'round', 'stroke-linejoin':'round'};
  const path = (d, o = {}) => el('path', Object.assign({d}, st, o), g);
  const T = (dx, dy) => `${cx + dx},${cy + dy}`;
  switch (id){
    case 'acc':   // the accent waves
      el('circle', {cx, cy:cy + 22, r:8, fill:'#fff'}, g);
      for (const r of [26, 46, 66]) path(`M${T(-r * .78, 22 - r * .62)} A${r},${r} 0 0 1 ${T(r * .78, 22 - r * .62)}`);
      break;
    case 'v0':    // bass drum, seen from the front at an angle
      el('ellipse', Object.assign({cx:cx - 12, cy, rx:34, ry:42}, st), g);
      path(`M${T(-12, -42)} L${T(26, -42)} A34,42 0 0 1 ${T(26, 42)} L${T(-12, 42)}`);
      el('ellipse', Object.assign({cx:cx - 12, cy, rx:20, ry:28}, st, {'stroke-width':4}), g);
      el('circle', {cx:cx + 26, cy:cy + 20, r:9, fill:'#fff'}, g);
      path(`M${T(26, 20)} L${T(26, 42)}`, {'stroke-width':4});
      break;
    case 'v1':    // snare drum
      el('ellipse', Object.assign({cx, cy:cy - 18, rx:52, ry:14}, st), g);
      path(`M${T(-52, -18)} L${T(-52, 16)} A52,14 0 0 0 ${T(52, 16)} L${T(52, -18)}`);
      for (const dx of [-30, 0, 30]) path(`M${T(dx, -4)} L${T(dx, 26)}`, {'stroke-width':3});
      break;
    case 'v2':    // open hi-hat: two cymbals apart on a stand
      path(`M${T(0, -44)} L${T(0, 44)}`);
      path(`M${T(-58, -12)} Q${T(0, -34)} ${T(58, -12)} Z`, {fill:'#fff'});
      path(`M${T(-58, 12)} Q${T(0, 34)} ${T(58, 12)} Z`, {fill:'#fff'});
      path(`M${T(-40, 44)} L${T(40, 44)}`);
      break;
    case 'v3':    // closed hi-hat: the cymbals together
      path(`M${T(0, -40)} L${T(0, 40)}`);
      path(`M${T(-58, 0)} Q${T(0, -26)} ${T(58, 0)} Q${T(0, 26)} ${T(-58, 0)} Z`, {fill:'#fff'});
      path(`M${T(-58, 0)} L${T(58, 0)}`, {stroke:'#1d6fc4', 'stroke-width':3});
      path(`M${T(-40, 40)} L${T(40, 40)}`);
      break;
    case 'v4':    // cymbal on a stand
      path(`M${T(-66, -16)} Q${T(0, -46)} ${T(66, -16)} Z`, {fill:'#fff'});
      path(`M${T(0, -30)} L${T(0, 46)}`);
      path(`M${T(-24, 46)} L${T(24, 46)}`);
      break;
    case 'v5':    // two hands clapping
      for (const s of [-1, 1]){
        const x0 = cx + s * 14;
        path(`M${x0},${cy + 44} L${x0},${cy - 6} Q${x0},${cy - 40} ${x0 + s * 10},${cy - 40} L${x0 + s * 30},${cy - 40} Q${x0 + s * 40},${cy - 40} ${x0 + s * 40},${cy - 26} L${x0 + s * 40},${cy + 44}`, {'stroke-width':4.5});
        for (const f of [10, 20, 30]) path(`M${x0 + s * f},${cy - 40} L${x0 + s * f},${cy - 8}`, {'stroke-width':3});
      }
      for (const [a, b, c, d] of [[-70, -30, -52, -18], [-74, 0, -54, 0], [70, -30, 52, -18], [74, 0, 54, 0]]) path(`M${T(a, b)} L${T(c, d)}`, {'stroke-width':4});
      break;
  }
}

function build(svg){
  svg.innerHTML = '';
  const defs = el('defs', {}, svg);
  const grad = (id, stops, o = {}) => { const g = el(o.radial ? 'radialGradient' : 'linearGradient', Object.assign({id}, o.attrs || {x1:0, y1:0, x2:0, y2:1}), defs);
    stops.forEach(([off, c]) => el('stop', {offset:off, 'stop-color':c}, g)); return g; };
  grad('gPanel', [[0, '#ececee'], [1, '#d8d8db']]);
  grad('gBtn', [[0, '#3a95e6'], [.5, '#1f76c9'], [1, '#155ea6']]);
  grad('gBtnDown', [[0, '#1a62a8'], [1, '#14548f']]);
  grad('gLcd', [[0, '#d9e4c8'], [1, '#c6d3b0']]);
  grad('gBlack', [[0, '#2c2c2c'], [1, '#0f0f0f']]);
  grad('gKnob', [[0, '#4a4a4a'], [.55, '#141414'], [1, '#050505']], {radial:true, attrs:{cx:.38, cy:.32, r:.75}});
  grad('gKnobSide', [[0, '#2a2a2a'], [1, '#000']]);
  grad('gGrille', [[0, '#cfcfd2'], [1, '#f4f4f6']]);
  const fl = el('filter', {id:'shadow', x:'-20%', y:'-20%', width:'140%', height:'150%'}, defs);
  el('feDropShadow', {dx:0, dy:5, stdDeviation:4, 'flood-color':'#000', 'flood-opacity':.45}, fl);
  // keys sit up off the panel: a soft shadow under each cap
  const ks = el('filter', {id:'keyShadow', x:'-20%', y:'-20%', width:'140%', height:'160%'}, defs);
  el('feDropShadow', {dx:0, dy:6, stdDeviation:5, 'flood-color':'#000', 'flood-opacity':.55}, ks);

  // case and panel
  el('rect', {x:0, y:0, width:2000, height:1160, fill:'#f3f3f4'}, svg);
  el('rect', {x:8, y:30, width:1932, height:1090, rx:22, fill:'url(#gPanel)', stroke:'#bdbdc1', 'stroke-width':3}, svg);
  // the right-hand upper panel edge and the lower key panel
  el('path', {d:'M993,45 L1895,62 L1895,640 L993,640 Z', fill:'#e4e4e7', stroke:'#c5c5c9', 'stroke-width':2}, svg);
  el('rect', {x:55, y:645, width:1840, height:455, rx:12, fill:'#e6e6e9', stroke:'#c7c7cb', 'stroke-width':2}, svg);

  // ---- black display block
  el('path', {d:'M10,45 L993,45 L993,640 L10,640 Z', fill:'url(#gBlack)'}, svg);
  // logo (the house mark: a framed Omega) and model name
  const lg = el('g', {}, svg), B = window.BRAND || {logo:'omega', model:'110', script:'Omega Rhythm'};
  if (B.logo === 'image'){
    // a friend's edition: the black block stays plain; their logo and name go on the silver panel (below)
  } else {
    el('rect', {x:125, y:72, width:58, height:56, fill:'none', stroke:'#e6e6e6', 'stroke-width':5}, lg);
    txt(lg, 154, 118, 'Ω', 44, {fill:'#e6e6e6', w:700});
    txt(lg, 198, 126, 'OMEGA', 64, {a:'start', fill:'none', w:900, fit:255, extra:{stroke:'#e6e6e6', 'stroke-width':4.5}});
  }
  if (B.logo !== 'image') txt(lg, 890, 137, B.model, 66, {a:'end', fill:'#f2f2f2', w:700, fit:118});
  // LCD bezel and glass
  el('rect', {x:103, y:150, width:815, height:452, rx:22, fill:'#1d1d1d', stroke:'#3b3b3b', 'stroke-width':4}, svg);
  el('rect', {x:150, y:162, width:720, height:428, rx:10, fill:'#0d0d0d'}, svg);
  const L = LAYOUT.lcd;
  el('rect', {x:L.x, y:L.y, width:L.w, height:L.h, rx:6, fill:'url(#gLcd)'}, svg);
  const lcd = el('g', {id:'lcd'}, svg);

  // ---- top right labels and the power switch
  el('rect', {x:1297, y:44, width:20, height:16, fill:'#cfcfd2', stroke:'#999'}, svg);
  el('rect', {x:1512, y:44, width:20, height:16, fill:'#cfcfd2', stroke:'#999'}, svg);
  txt(svg, 1313, 116, 'ACC TRIG OUT', 24, {fit:150}); el('path', {d:'M1298,58 L1316,58 L1307,78 Z', fill:'#2a2a2a'}, svg);
  txt(svg, 1522, 116, 'DC 9V', 25); el('path', {d:'M1513,58 L1531,58 L1522,78 Z', fill:'#2a2a2a'}, svg);
  txt(svg, 1700, 112, 'ON—OFF', 25); txt(svg, 1700, 138, 'POWER', 25); el('path', {d:'M1691,58 L1709,58 L1700,78 Z', fill:'#2a2a2a'}, svg);
  const power = el('g', {id:'power', class:'hit'}, svg);
  el('rect', {x:1660, y:26, width:80, height:30, rx:6, fill:'#202020'}, power);
  const powerKnob = el('rect', {id:'powerKnob', x:1664, y:29, width:34, height:24, rx:4, fill:'#9a9a9a', stroke:'#555'}, power);
  el('rect', {x:1640, y:10, width:120, height:140, fill:'transparent'}, power);
  // product name
  if (B.logo === 'image'){
    // on the silver panel: the logo in the 110 blue, the name in a thin wordmark in the 110 blue, then GRAPHIC
    el('image', {href:B.imageDark, x:1040, y:124, width:104, height:112, preserveAspectRatio:'xMidYMid meet'}, svg);
    txt(svg, 1158, 203, B.name, 74, {a:'start', fill:'#1f6fc4', w:400, fit:440, extra:{'font-family':"'Quicksand','Century Gothic','Avenir Next','Helvetica Neue',Arial,sans-serif"}});
    txt(svg, 1615, 203, 'GRAPHIC', 54, {a:'start', fill:'#2a2a2a', w:600, fit:235});
  } else {
    txt(svg, 1062, 203, B.script || 'Omega Rhythm', 80, {a:'start', fill:'#1f6fc4', w:900, fit:385, extra:{'font-style':'italic'}});
    txt(svg, 1475, 205, 'GRAPHIC', 60, {a:'start', fill:'#2a2a2a', w:600, fit:265});
  }

  // ---- knobs
  const knobs = {};
  const kLabels = {tempo:['TEMPO', '0', 'SLOW', 'FAST'], balance:['BALANCE', '0', '5', '5'], accent:['ACCENT', '5', '0', '10'], volume:['VOLUME', '5', '0', '10']};
  for (const [id, cx, cy] of LAYOUT.knobs){
    const [t, top, l, r] = kLabels[id];
    txt(svg, cx, 292, t, 36, {w:700, fit:t.length * 22});
    txt(svg, cx - 2, 330, top, 30, {w:600});
    txt(svg, cx + (id === 'tempo' ? -72 : -52), 494, l, id === 'tempo' ? 32 : 30, {w:600});
    txt(svg, cx + (id === 'tempo' ? 60 : 50), 494, r, id === 'tempo' ? 32 : 30, {w:600});
    for (let i = 0; i <= 10; i++){
      const a = (-135 + 27 * i) * Math.PI / 180, r1 = 80, r2 = i % 5 === 0 ? 96 : 92;
      el('line', {x1:cx + Math.sin(a) * r1, y1:cy - Math.cos(a) * r1, x2:cx + Math.sin(a) * r2, y2:cy - Math.cos(a) * r2, stroke:'#2a2a2a', 'stroke-width':3}, svg);
    }
    const g = el('g', {class:'knob hit', 'data-k':id}, svg);
    el('circle', {cx, cy:cy + 6, r:LAYOUT.knobR + 2, fill:'#000', opacity:.35}, g);
    el('circle', {cx, cy, r:LAYOUT.knobR, fill:'url(#gKnobSide)'}, g);
    // knurled edge
    for (let i = 0; i < 36; i++){ const a = i * 10 * Math.PI / 180; el('line', {x1:cx + Math.cos(a) * (LAYOUT.knobR - 6), y1:cy + Math.sin(a) * (LAYOUT.knobR - 6), x2:cx + Math.cos(a) * LAYOUT.knobR, y2:cy + Math.sin(a) * LAYOUT.knobR, stroke:'#2f2f2f', 'stroke-width':3}, g); }
    el('circle', {cx, cy, r:LAYOUT.knobR - 8, fill:'url(#gKnob)'}, g);
    const ptr = el('line', {x1:cx, y1:cy - 18, x2:cx, y2:cy - 50, stroke:'#ff7a1a', 'stroke-width':7, 'stroke-linecap':'round'}, g);
    el('circle', {cx, cy, r:LAYOUT.knobR + 20, fill:'transparent'}, g);
    knobs[id] = {g, ptr, cx, cy};
  }
  // speaker grille
  for (let i = 0; i < 6; i++){
    const y = 545 + i * 15.5;
    el('rect', {x:996, y, width:890, height:9, fill:'url(#gGrille)', stroke:'#b5b5b8', 'stroke-width':1.2}, svg);
  }

  // ---- key panel: the outline that groups the shifted keys, and the legends
  el('path', {d:'M68,860 L68,666 L1420,658 L1420,712 L1302,712 L300,716 L300,876 Z', fill:'none', stroke:'#2a5b8c', 'stroke-width':2.5}, svg);
  el('path', {d:'M300,716 L300,876 L68,876', fill:'none', stroke:'#2a5b8c', 'stroke-width':2.5}, svg);
  txt(svg, 378, 708, 'I·II', 26, {w:800, fit:38, extra:{'font-family':'Georgia, serif'}});
  // PLAY bracket
  el('path', {d:'M472,690 L472,680 L540,680 M590,680 L656,680 L656,690', fill:'none', stroke:'#1b1b1b', 'stroke-width':2.5}, svg);
  leg(svg, 565, 688, 'PLAY', 50);
  leg(svg, 502, 708, 'SONG', 62); leg(svg, 627, 708, 'PATTERN', 100);
  el('path', {d:'M748,690 L748,680 L822,680 M914,680 L988,680 L988,690', fill:'none', stroke:'#1b1b1b', 'stroke-width':2.5}, svg);
  leg(svg, 868, 688, 'WRITE', 70);
  leg(svg, 750, 708, 'SONG', 62); leg(svg, 869, 708, 'STEP', 54); leg(svg, 989, 708, 'TAP', 42);
  leg(svg, 1096, 690, '12', 26); el('line', {x1:1088, y1:712, x2:1128, y2:668, stroke:'#1b1b1b', 'stroke-width':2.5}, svg); leg(svg, 1118, 710, '16', 26);
  leg(svg, 1225, 686, 'PATTERN', 100); leg(svg, 1225, 708, 'CLEAR', 72);
  leg(svg, 1340, 686, 'MEASURE', 102); leg(svg, 1340, 708, 'RESET', 70);
  // lower legends
  leg(svg, 436, 892, 'DOWN', 70); el('path', {d:'M490,884 L522,884 M502,874 L490,884 L502,894', fill:'none', stroke:'#1b1b1b', 'stroke-width':6}, svg);
  leg(svg, 592, 892, 'MEASURE', 108); el('path', {d:'M664,884 L696,884 M684,874 L696,884 L684,894', fill:'none', stroke:'#1b1b1b', 'stroke-width':6}, svg);
  leg(svg, 726, 892, 'UP', 32);
  leg(svg, 976, 886, 'ENTER', 78);
  txt(svg, 1225, 884, 'D.C', 28, {extra:{'font-style':'italic', 'font-family':'Georgia, serif'}});
  leg(svg, 1712, 884, 'KNUCKLE', 104);            // the added voice: SHIFT + HAND CLAP
  const names = {acc:'ACCENT', v0:'BASS DRUM', v1:'SNARE DRUM', v2:'O.HIHAT', v3:'C.HIHAT', v4:'CYMBAL', v5:'HAND CLAP'};

  // ---- keys
  const buttons = {};
  for (const [id, x, y, w, h] of LAYOUT.buttons){
    const g = el('g', {class:'key hit', 'data-b':id}, svg);
    el('rect', {x:x - 7, y:y - 7, width:w + 14, height:h + 16, rx:9, fill:'#0d0d0d'}, g);
    const cap = el('g', {class:'cap', filter:'url(#keyShadow)'}, g);
    el('rect', {x, y, width:w, height:h, rx:5, fill:'url(#gBtn)', stroke:'#0f4a85', 'stroke-width':2}, cap);
    el('rect', {x:x + 5, y:y + 4, width:w - 10, height:6, rx:3, fill:'#fff', opacity:.25}, cap);
    const cx = x + w / 2, cy = y + h / 2;
    if (id === 'shift') txt(cap, cx, cy + 12, 'SHIFT', 34, {fill:'#fff'});
    else if (id === 'bank'){
      txt(cap, cx - 15, cy - 8, 'A', 28, {fill:'#fff'}); txt(cap, cx + 15, cy - 8, 'B', 28, {fill:'#fff'});
      txt(cap, cx - 15, cy + 30, 'C', 28, {fill:'#fff'}); txt(cap, cx + 15, cy + 30, 'D', 28, {fill:'#fff'});
      el('line', {x1:cx, y1:cy - 34, x2:cx, y2:cy + 36, stroke:'#fff', 'stroke-width':3}, cap);
      el('line', {x1:cx - 30, y1:cy + 1, x2:cx + 30, y2:cy + 1, stroke:'#fff', 'stroke-width':3}, cap);
    } else if (/^n\d$/.test(id)){
      // the outlined italic numerals
      txt(cap, cx, cy + 26, id[1], 74, {fill:'none', w:400, extra:{stroke:'#fff', 'stroke-width':3.5, 'font-style':'italic', 'font-family':"'Arial Narrow', Arial, sans-serif"}});
    } else if (id === 'start' || id === 'stop'){
      el('circle', {cx, cy:cy - 28, r:17, fill:id === 'start' ? '#fff' : 'none', stroke:'#fff', 'stroke-width':4}, cap);
      txt(cap, cx, cy + 34, id.toUpperCase(), 34, {fill:'#fff'});
    } else icon(cap, id, cx, cy);
    if (names[id]) txt(svg, cx, y + h + 54, names[id], 36, {w:700, fit:Math.min(names[id].length * 21, w + 40)});
    buttons[id] = {g, cap};
  }
  return {lcd, buttons, knobs, power, powerKnob};
}

/* ---- the LCD: grid, step numbers, row labels, the SONG / MEASURE / BANK / RHYTHM / MODE boxes. draw(state) redraws it. */
const INK = '#26302a', DIM = 'rgba(38,48,42,.12)';
// seven-segment digits: segments a b c d e f g
const SEG = {'0':'abcdef', '1':'bc', '2':'abged', '3':'abgcd', '4':'fgbc', '5':'afgcd', '6':'afgedc', '7':'abc', '8':'abcdefg', '9':'abcdfg',
  'A':'abcefg', 'b':'fedcg', 'C':'afed', 'd':'bcdeg', 'E':'afged', 'r':'eg', 'L':'fed', 'P':'abfge', 't':'fged', '-':'g', 'n':'ceg', 'o':'cdeg', 'S':'afgcd', 'H':'bcefg', 'F':'afge', 'U':'bcdef', 'c':'ged', ' ':''};
function seg7(g, x, y, w, h, ch, on = true){
  const t = Math.max(4, w * .16), segs = SEG[ch] || '';
  const H = h / 2, poly = {
    a:[[x + t, y], [x + w - t, y], [x + w - t * 1.5, y + t], [x + t * 1.5, y + t]],
    d:[[x + t * 1.5, y + h - t], [x + w - t * 1.5, y + h - t], [x + w - t, y + h], [x + t, y + h]],
    g:[[x + t, y + H], [x + t * 1.5, y + H - t / 2], [x + w - t * 1.5, y + H - t / 2], [x + w - t, y + H], [x + w - t * 1.5, y + H + t / 2], [x + t * 1.5, y + H + t / 2]],
    f:[[x, y + t], [x + t, y + t * 1.5], [x + t, y + H - t * .7], [x, y + H - t * .2]],
    b:[[x + w, y + t], [x + w, y + H - t * .2], [x + w - t, y + H - t * .7], [x + w - t, y + t * 1.5]],
    e:[[x, y + H + t * .2], [x + t, y + H + t * .7], [x + t, y + h - t * 1.5], [x, y + h - t]],
    c:[[x + w, y + H + t * .2], [x + w, y + h - t], [x + w - t, y + h - t * 1.5], [x + w - t, y + H + t * .7]],
  };
  for (const s of 'abcdefg') el('polygon', {points:poly[s].map(p => p.join(',')).join(' '), fill:on && segs.includes(s) ? INK : DIM}, g);
}
function drawLcd(g, d){
  g.innerHTML = '';
  if (!d.power) return;
  const L = LAYOUT.lcd;
  // step numbers
  for (let i = 0; i < 16; i++){
    const x = L.numX0 + i * L.numDx, hide = d.len === 12 && i >= 12;
    if (d.playStep === i){ el('rect', {x:x - 15, y:L.numY - 26, width:30, height:32, fill:INK}, g); txt(g, x, L.numY, String(i + 1), 25, {fill:'#cfdcbb', w:700}); }
    else txt(g, x, L.numY, String(i + 1), 25, {fill:hide ? DIM : INK, w:700});
  }
  // row labels
  const rows = ['C.HIHAT', 'O.HIHAT', 'SNARE DRUM', 'BASS DRUM'];
  el('rect', {x:L.labelX, y:L.gridY, width:L.gridX - L.labelX - 4, height:L.cellH * 5, fill:'none', stroke:INK, 'stroke-width':3}, g);
  for (let r = 0; r < 5; r++){
    const y = L.gridY + r * L.cellH;
    if (r) el('line', {x1:L.labelX, y1:y, x2:L.gridX - 4, y2:y, stroke:INK, 'stroke-width':2.5}, g);
    if (r < 4) txt(g, L.labelX + 7, y + L.cellH - 8, rows[r], 21, {a:'start', fill:INK, w:800, fit:r >= 2 ? 120 : 90});
    else { el('line', {x1:L.labelX + 42, y1:y + L.cellH - 7, x2:L.labelX + 52, y2:y + 8, stroke:INK, 'stroke-width':4}, g); el('line', {x1:L.labelX + 82, y1:y + L.cellH - 7, x2:L.labelX + 92, y2:y + 8, stroke:INK, 'stroke-width':4}, g); }
  }
  // grid
  el('rect', {x:L.gridX, y:L.gridY, width:L.cellW * 16, height:L.cellH * 5, fill:'none', stroke:INK, 'stroke-width':3}, g);
  for (let i = 1; i < 16; i++) el('line', {x1:L.gridX + i * L.cellW, y1:L.gridY, x2:L.gridX + i * L.cellW, y2:L.gridY + L.cellH * 5, stroke:INK, 'stroke-width':1.6}, g);
  for (let r = 1; r < 5; r++) el('line', {x1:L.gridX, y1:L.gridY + r * L.cellH, x2:L.gridX + L.cellW * 16, y2:L.gridY + r * L.cellH, stroke:INK, 'stroke-width':1.6}, g);
  // notes: [main bit, second bit] per row (the second voice shows as an open square)
  const map = [[8, 0], [4, 16], [2, 32], [1, 64], [128, 0]];
  for (let i = 0; i < d.len; i++) for (let r = 0; r < 5; r++){
    const bits = d.steps[i] || 0, [a, b] = map[r], x = L.gridX + i * L.cellW, y = L.gridY + r * L.cellH;
    const hasA = bits & a, hasB = b && (bits & b);
    if (hasA) el('rect', {x:x + 4, y:y + 4, width:L.cellW - 8, height:L.cellH - 8, fill:INK}, g);
    if (hasB){ if (hasA) el('rect', {x:x + 10, y:y + 10, width:L.cellW - 20, height:L.cellH - 20, fill:'#cfdcbb'}, g);
      else el('rect', {x:x + 6, y:y + 6, width:L.cellW - 12, height:L.cellH - 12, fill:'none', stroke:INK, 'stroke-width':3.5}, g); }
  }
  if (d.cursor >= 0 && d.blink) el('rect', {x:L.gridX + d.cursor * L.cellW + 1, y:L.gridY + 1, width:L.cellW - 2, height:L.cellH * 5 - 2, fill:'none', stroke:INK, 'stroke-width':5}, g);
  // invisible hit areas for tapping the grid
  for (let i = 0; i < 16; i++) for (let r = 0; r < 5; r++)
    el('rect', {x:L.gridX + i * L.cellW, y:L.gridY + r * L.cellH, width:L.cellW, height:L.cellH, fill:'transparent', class:'cell hit', 'data-step':i, 'data-row':r}, g);
  // lower boxes
  const box = (x, y, w, h) => el('rect', {x, y, width:w, height:h, fill:'none', stroke:INK, 'stroke-width':3, rx:3}, g);
  box(172, 418, 286, 154); el('line', {x1:222, y1:418, x2:222, y2:572, stroke:INK, 'stroke-width':3}, g); el('line', {x1:172, y1:442, x2:458, y2:442, stroke:INK, 'stroke-width':2.5}, g);
  box(470, 418, 135, 154); el('line', {x1:523, y1:418, x2:523, y2:572, stroke:INK, 'stroke-width':3}, g); el('line', {x1:470, y1:442, x2:605, y2:442, stroke:INK, 'stroke-width':2.5}, g);
  box(617, 418, 231, 154); el('line', {x1:670, y1:418, x2:670, y2:572, stroke:INK, 'stroke-width':3}, g); el('line', {x1:617, y1:442, x2:670, y2:442, stroke:INK, 'stroke-width':2.5}, g);
  txt(g, 197, 436, 'SONG', 15, {fill:INK, w:800}); txt(g, 340, 436, 'MEASURE', 15, {fill:INK, w:800});
  txt(g, 497, 436, 'BANK', 15, {fill:INK, w:800}); txt(g, 564, 436, 'RHYTHM', 15, {fill:INK, w:800}); txt(g, 644, 436, 'MODE', 15, {fill:INK, w:800});
  // SONG: I / II (two bars)
  if (d.song >= 0){ for (let k = 0; k <= d.song; k++) el('rect', {x:d.song ? 184 + k * 16 : 192, y:470, width:8, height:72, fill:INK}, g); }
  // MEASURE: three digits
  const ms = d.measureText || '   ';
  for (let k = 0; k < 3; k++) seg7(g, 252 + k * 66, 466, 46, 86, ms[k] || ' ', true);
  seg7(g, 478, 466, 38, 86, d.bank || ' ');
  seg7(g, 542, 466, 44, 86, d.rhythm || ' ');
  // MODE text
  (d.modeLines || []).forEach((s, k) => txt(g, 760, 488 + k * 34, s, 27, {fill:INK, w:800}));
  if (d.shift) txt(g, 644, 560, 'SHIFT', 13, {fill:INK, w:900});
  if (d.running) el('polygon', {points:'632,466 632,500 660,483', fill:INK}, g);
}
return {build, drawLcd, LAYOUT, seg7};
})();
