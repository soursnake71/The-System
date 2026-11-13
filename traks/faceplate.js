/* Omega Traks front panel: the Drumtraks' panel as SVG (2000 x 916 units between two 60-unit wooden cheeks), the same
   layout as the plugin's (plugins/traks/TraksEditor.cpp, namespace lay). FP.build(svg) draws it and returns the parts
   the app animates: keys (by machine key id), knobs, the two displays, the function LEDs, RUN and cassette LEDs. */
'use strict';
(function (root) {
const NS = 'http://www.w3.org/2000/svg';
const CHEEK = 60, H = 916;
const KM = root.OTMACH ? root.OTMACH.K : {SAVE:0, LOAD:1, SLOWER:2, FASTER:3, SONGFN:4, ENTER:5, D0:6, LEFT:16, RIGHT:17, PATFN:18, RUN:19, ACCENT:20, INST:21};
// [id, cx, cy, size, style, cap]  style 0 dark top key, 1 keypad, 2 big key with the light bar
const KEYS = [[KM.SAVE, 254, 190, 56, 0], [KM.LOAD, 358, 190, 56, 0], [KM.SLOWER, 256, 392, 56, 0], [KM.FASTER, 358, 392, 56, 0],
  [KM.SONGFN, 521, 147, 52, 0], [KM.ENTER, 636, 386, 52, 0], [KM.PATFN, 1247, 142, 52, 0],
  [KM.D0 + 1, 936, 194, 48, 1, '1'], [KM.D0 + 2, 1014, 194, 48, 1, '2'], [KM.D0 + 3, 1090, 194, 48, 1, '3'],
  [KM.D0 + 4, 936, 271, 48, 1, '4'], [KM.D0 + 5, 1014, 271, 48, 1, '5'], [KM.D0 + 6, 1090, 271, 48, 1, '6'],
  [KM.D0 + 7, 936, 347, 48, 1, '7'], [KM.D0 + 8, 1014, 347, 48, 1, '8'], [KM.D0 + 9, 1090, 347, 48, 1, '9'],
  [KM.LEFT, 936, 422, 48, 1, '<'], [KM.D0, 1014, 422, 48, 1, '0'], [KM.RIGHT, 1090, 422, 48, 1, '>'],
  [KM.RUN, 238, 772, 80, 2], [KM.ACCENT, 416, 772, 80, 2]];
const INST_X = [604, 708, 802, 905, 1000, 1104, 1200, 1302, 1398, 1504, 1603, 1701, 1800];
INST_X.forEach((x, v) => KEYS.push([KM.INST + v, x, 772, 80, 2]));
const KEY_NAMES = ['BASS', 'SNARE', 'RIM', 'TOM1', 'TOM2', 'CRASH', 'RIDE', 'CLOSED', 'OPEN', 'CLAPS', 'TAMB', 'COWBELL', 'CABASA'];
const KNOBS = [[217, 630, 27], [400, 630, 27], [1741, 150, 32], [1741, 336, 32]];     // VOLUME, TUNING (per instrument), METRONOME, MASTER
const BOXES = [[196, 62, 222, 206], [196, 276, 222, 194], [431, 62, 433, 408], [873, 62, 280, 408], [1164, 62, 431, 408], [1610, 62, 228, 408],
  [172, 558, 313, 330], [552, 558, 101, 330], [657, 558, 194, 330], [858, 558, 189, 330], [1056, 558, 192, 330], [1254, 558, 192, 330], [1455, 558, 394, 330]];
const SONG_Y = [186, 206, 226, 246, 265, 284, 303, 322, 341];
const PAT_Y = [181, 200, 219, 238, 258, 277, 296, 315, 334];
const SONG_NAMES = ['SONG#', 'ERASE  SONG', 'COPY/APPEND  TO', 'BUILD  MODE', 'PATTERN#', 'VOL  CHANGE', 'TEMPO  CHANGE', 'INSERT', 'DELETE'];
const PAT_NAMES = ['PATTERN#', 'RECORD  MODE', 'ERASE  INSTRUMENT', 'ERROR  CORRECT', 'SWING%', 'TIME  SIGNATURE', '#OF  MEASURES', 'ERASE  PATTERN', 'COPY / APPEND  TO'];
const SEGS = {'0':'abcdef', O:'abcdef', '1':'bc', '2':'abged', '3':'abgcd', '4':'fgbc', '5':'afgcd', S:'afgcd', '6':'afgedc', '7':'abc', '8':'abcdefg', '9':'abcdfg',
  '-':'g', E:'afged', C:'adef', c:'ged', h:'fegc', H:'fbgec', n:'egc', P:'abfge', o:'cdeg', F:'afge', r:'eg', d:'bcdeg', L:'fed', U:'bcdef', u:'cde', A:'abcefg', t:'fged', b:'fedcg', _:'d'};

function el(tag, attrs, parent, text){
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (text !== undefined) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}
const T = (p, s, x, y, size, anchor = 'middle', extra = {}) => el('text', Object.assign({x, y, 'font-size':size, 'text-anchor':anchor, class:'pt'}, extra), p, s);

function segPolys(x, y, w, h){
  const t = w * 0.17, Hh = h / 2, sl = w * 0.12;
  const P = (px, py) => `${(px + sl * (1 - (py - y) / h)).toFixed(1)},${py.toFixed(1)}`;
  return {
    a:[P(x + t, y), P(x + w - t, y), P(x + w - t * 1.6, y + t), P(x + t * 1.6, y + t)],
    d:[P(x + t * 1.6, y + h - t), P(x + w - t * 1.6, y + h - t), P(x + w - t, y + h), P(x + t, y + h)],
    g:[P(x + t, y + Hh), P(x + t * 1.6, y + Hh - t / 2), P(x + w - t * 1.6, y + Hh - t / 2), P(x + w - t, y + Hh), P(x + w - t * 1.6, y + Hh + t / 2), P(x + t * 1.6, y + Hh + t / 2)],
    f:[P(x, y + t), P(x + t, y + t * 1.6), P(x + t, y + Hh - t * 0.7), P(x, y + Hh - t * 0.2)],
    b:[P(x + w, y + t), P(x + w, y + Hh - t * 0.2), P(x + w - t, y + Hh - t * 0.7), P(x + w - t, y + t * 1.6)],
    e:[P(x, y + Hh + t * 0.2), P(x + t, y + Hh + t * 0.7), P(x + t, y + h - t * 1.6), P(x, y + h - t)],
    c:[P(x + w, y + Hh + t * 0.2), P(x + w, y + h - t), P(x + w - t, y + h - t * 1.6), P(x + w - t, y + Hh + t * 0.7)]};
}
function display2(p, x, y){
  el('rect', {x, y, width:88, height:56, rx:4, fill:'#050505'}, p);
  el('rect', {x:x + 4, y:y + 4, width:80, height:48, rx:3, fill:'#1d0507'}, p);
  const digits = [];
  for (let i = 0; i < 2; i++){
    const polys = segPolys(x + 12 + i * 37, y + 9, 28, 38), segs = {};
    for (const k in polys) segs[k] = el('polygon', {points:polys[k].join(' '), class:'seg'}, p);
    digits.push(segs);
  }
  return digits;
}
function led(p, x, y, r = 5){
  const g = el('g', {class:'led'}, p);
  el('circle', {cx:x, cy:y, r:r * 2.2, class:'glow'}, g);
  el('circle', {cx:x, cy:y, r, class:'dot'}, g);
  el('ellipse', {cx:x - r * 0.15, cy:y - r * 0.35, rx:r * 0.3, ry:r * 0.25, fill:'#fff', opacity:0.35}, g);
  return g;
}
function wood(p, x){
  el('rect', {x, y:0, width:CHEEK, height:H, rx:6, fill:'url(#gWood)'}, p);
  for (let i = 0; i < 9; i++){ const gx = x + 6 + ((i * 37 + x) % 48); el('line', {x1:gx, y1:4, x2:gx + (i % 3) - 1, y2:H - 4, stroke:'rgba(64,26,5,.19)', 'stroke-width':1.5 + (i % 3)}, p); }
  el('line', {x1:x + CHEEK * 0.7, y1:6, x2:x + CHEEK * 0.7, y2:H - 6, stroke:'rgba(255,255,255,.25)', 'stroke-width':2}, p);
}
function knob(p, cx, cy, r, scale){
  const g = el('g', {class:'knob'}, p);
  if (scale) for (let i = 0; i <= 10; i++){
    const a = (-135 + 27 * i) * Math.PI / 180;
    el('circle', {cx:cx + Math.sin(a) * (r + 10), cy:cy - Math.cos(a) * (r + 10), r:2.5, fill:'#f0f0ee'}, g);
    if (i % 2 === 0 || i === 1 || i === 9) T(g, String(i), cx + Math.sin(a) * (r + 25), cy - Math.cos(a) * (r + 25) + 6, 15);
  }
  el('circle', {cx, cy:cy + 3, r:r + 2, fill:'rgba(0,0,0,.5)'}, g);
  el('circle', {cx, cy, r:r + 2, fill:'#e8e8e8'}, g);
  el('circle', {cx, cy, r:r * 0.86, fill:'url(#gKnob)'}, g);
  const ptr = el('line', {x1:cx, y1:cy - r * 0.2, x2:cx, y2:cy - r * 0.95, stroke:'#f0f0ee', 'stroke-width':Math.max(3, r * 0.13), 'stroke-linecap':'round'}, g);
  el('circle', {cx, cy, r:r + 18, fill:'transparent', class:'hit'}, g);
  return {g, ptr, cx, cy};
}
function keyCap(p, [id, cx, cy, s, style, cap]){
  const g = el('g', {class:'key', 'data-key':id}, p), x = cx - s / 2, y = cy - s / 2;
  if (style === 1){
    el('rect', {x:x - 3, y:y - 3, width:s + 6, height:s + 8, rx:4, fill:'#0b0b0b'}, g);
    const c = el('g', {class:'cap'}, g);
    el('rect', {x, y, width:s, height:s, rx:3, fill:'url(#gPad)', stroke:'#f4f4f4', 'stroke-width':1.5}, c);
    if (cap === '<' || cap === '>'){ const d = cap === '<' ? -1 : 1; el('polygon', {points:`${cx + d * 9},${cy} ${cx - d * 7},${cy - 9} ${cx - d * 7},${cy + 9}`, fill:'#8a8a8a'}, c); }
    else T(c, cap, cx, cy + 8, 22, 'middle', {fill:'#9a9a9a'});
  } else {
    el('rect', {x:x - 5, y:y - 5, width:s + 10, height:s + 12, rx:5, fill:'#000'}, g);
    const c = el('g', {class:'cap'}, g);
    el('rect', {x, y, width:s, height:s, rx:3, fill:'url(#gKey)', stroke:'#4a4a4a', 'stroke-width':1.5}, c);
    if (style === 2) el('rect', {x:x + s * 0.76, y:y + s * 0.1, width:s * 0.1, height:s * 0.8, rx:2, fill:'url(#gBar)'}, c);
  }
  el('rect', {x:x - 8, y:y - 8, width:s + 16, height:s + 16, fill:'transparent', class:'hit'}, g);
  return g;
}

function build(svg, brand){
  svg.innerHTML = '';
  const defs = el('defs', {}, svg);
  defs.innerHTML = `<linearGradient id="gWood" x1="0" x2="1"><stop offset="0" stop-color="#7a3c15"/><stop offset=".55" stop-color="#c4752f"/><stop offset="1" stop-color="#9a5320"/></linearGradient>
    <radialGradient id="gKnob" cx="35%" cy="30%" r="80%"><stop offset="0" stop-color="#3c3c3c"/><stop offset="1" stop-color="#050505"/></radialGradient>
    <linearGradient id="gKey" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#333"/><stop offset="1" stop-color="#151515"/></linearGradient>
    <linearGradient id="gPad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#dcdcdc"/><stop offset="1" stop-color="#a9a9a9"/></linearGradient>
    <linearGradient id="gBar" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#bdbdbd"/></linearGradient>
    <pattern id="gGrain" width="24" height="24" patternUnits="userSpaceOnUse"><rect width="24" height="24" fill="#161616"/><rect x="3" y="5" width="2" height="2" fill="rgba(255,255,255,.05)"/><rect x="15" y="11" width="2" height="2" fill="rgba(0,0,0,.25)"/><rect x="9" y="19" width="2" height="2" fill="rgba(255,255,255,.04)"/><rect x="20" y="2" width="2" height="2" fill="rgba(0,0,0,.2)"/></pattern>
    <linearGradient id="gSheen" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgba(255,255,255,.13)"/><stop offset="1" stop-color="rgba(255,255,255,0)"/></linearGradient>`;
  wood(svg, 0); wood(svg, CHEEK + 2000);
  const p = el('g', {transform:`translate(${CHEEK},0)`}, svg);
  el('rect', {x:0, y:0, width:2000, height:H, fill:'url(#gGrain)'}, p);
  el('rect', {x:0, y:0, width:2000, height:60, fill:'url(#gSheen)'}, p);
  for (const [x, y, w, h] of BOXES) el('rect', {x, y, width:w, height:h, rx:16, fill:'none', stroke:'#f0f0ee', 'stroke-width':3.2}, p);
  const ui = {keys:{}, knobs:[], leds:{song:[], pat:[]}};
  T(p, 'CASSETTE', 307, 113, 17); ui.leds.save = led(p, 256, 144); ui.leds.load = led(p, 358, 144);
  T(p, 'SAVE', 254, 243, 16); T(p, 'LOAD', 358, 243, 16);
  T(p, 'TEMPO', 307, 323, 17); T(p, 'SLOWER', 256, 446, 15); T(p, 'FASTER', 358, 446, 15);
  T(p, 'SELECT  SONG  FUNCTIONS', 561, 153, 17, 'start');
  SONG_NAMES.forEach((n, i) => { const sub = i >= 4; ui.leds.song.push(led(p, sub ? 612 : 572, SONG_Y[i], 4.5)); T(p, n, sub ? 628 : 588, SONG_Y[i] + 6, 16, 'start'); });
  el('path', {d:`M572 252V386H606` + [4, 5, 6, 7, 8].map(i => `M572 ${SONG_Y[i]}H604`).join(''), stroke:'#f0f0ee', 'stroke-width':2.5, fill:'none'}, p);
  T(p, 'ENTER', 698, 393, 16);
  ui.dispL = display2(p, 888, 72); ui.dispR = display2(p, 1047, 72);
  T(p, 'SELECT  PATTERN  FUNCTIONS', 1294, 148, 17, 'start');
  PAT_NAMES.forEach((n, i) => { const sub = i === 2 || i === 3; ui.leds.pat.push(led(p, sub ? 1337 : 1293, PAT_Y[i], 4.5)); T(p, n, sub ? 1352 : 1310, PAT_Y[i] + 6, 16, 'start'); });
  el('path', {d:`M1293 206V${PAT_Y[3]}M1293 ${PAT_Y[2]}H1330M1293 ${PAT_Y[3]}H1330`, stroke:'#f0f0ee', 'stroke-width':2.5, fill:'none'}, p);
  T(p, 'METRONOME', 1741, 232, 15); T(p, 'VOLUME', 1741, 258, 15); T(p, 'MASTER', 1741, 418, 15); T(p, 'VOLUME', 1741, 444, 15);
  // the logo: the Omega's, where the hardware has its own, in the same style
  T(p, (brand && brand.logo) || 'omegatraks', 1810, 530, 58, 'end', {class:'pt logo'});
  for (let i = 0; i < 3; i++){ const r = 8 + i * 7, ry = 12 + i * 7; el('path', {d:`M${1822 + r * Math.cos(-1.07)} ${514 + ry * Math.sin(-1.07)} A${r} ${ry} 0 0 1 ${1822 + r * Math.cos(1.03)} ${514 + ry * Math.sin(1.03)}`, stroke:'#f0f0ee', 'stroke-width':2.2, fill:'none'}, p); }
  T(p, 'VOLUME', 217, 678, 15); T(p, 'TUNING', 400, 678, 15);
  el('path', {d:'M240 682V694H260M358 694H378V682', stroke:'#f0f0ee', 'stroke-width':2, fill:'none'}, p);
  T(p, 'PER INSTRUMENT', 309, 700, 13);
  ui.leds.run = led(p, 246, 722, 5);
  T(p, 'RUN/STOP', 238, 845, 15); T(p, 'ACCENT', 416, 845, 15);
  ['CH 1', 'CH 2', 'CH 3', 'CH 4', 'CH 5', 'CH 6'].forEach((n, c) => T(p, n, BOXES[7 + c][0] + BOXES[7 + c][2] / 2, 620, 17));
  ui.sel = [];
  KEY_NAMES.forEach((n, v) => { T(p, n, INST_X[v] - 6, 840, 14); ui.sel.push(el('rect', {x:INST_X[v] - 46, y:858, width:80, height:5, rx:2, fill:'rgba(255,255,255,.18)', opacity:0}, p)); });
  for (const [a, b, c] of [[1094, 1194, 1146], [1292, 1392, 1344]]) el('path', {d:`M${a} 848V862H${c - 40}M${c + 40} 862H${b}V848`, stroke:'#f0f0ee', 'stroke-width':2, fill:'none'}, p);
  T(p, 'CYMBAL', 1146, 868, 13); T(p, 'HI HAT', 1344, 868, 13);
  for (const k of KEYS) ui.keys[k[0]] = keyCap(p, k);
  KNOBS.forEach(([x, y, r], i) => ui.knobs.push(knob(p, x, y, r, i >= 2)));
  return ui;
}
function setDigits(digits, s){
  for (let i = 0; i < 2; i++){ const on = SEGS[s[i]] || ''; for (const k in digits[i]) digits[i][k].classList.toggle('on', on.includes(k)); }
}
function setLed(g, on){ g.classList.toggle('on', !!on); }
function setKnob(k, n){ k.ptr.setAttribute('transform', `rotate(${-135 + 270 * Math.max(0, Math.min(1, n))} ${k.cx} ${k.cy})`); }
root.FP = {build, setDigits, setLed, setKnob, KEYS, KNOBS, KEY_NAMES, CHEEK, W:2000 + 2 * CHEEK, H};
})(typeof globalThis !== 'undefined' ? globalThis : this);
