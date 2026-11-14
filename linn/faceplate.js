/* Omega Linn front panel: the LinnDrum's panel as SVG (2000 x 1090 units between two 60-unit wooden cheeks), the same
   layout as the plugin's (plugins/linn/LinnEditor.cpp, namespace lay). FP.build(svg) draws it and returns the parts the
   app animates: keys (by machine key id), knobs, the mixer's pan and level sliders, the two displays and the LEDs. */
'use strict';
(function (root) {
const NS = 'http://www.w3.org/2000/svg';
const CHEEK = 60, H = 1090, W = 2000;
const KM = root.OLMACH ? root.OLMACH.K : {SONGPAT:0, SONGNUM:1, END:2, DELETE:3, INSERT:4, LEFT:5, RIGHT:6, ENTER:7, D1:8, STORE:15, LOAD:16, EXTSYNC:17, BPM:18, PLAY:19, DRUM:20};
// ---- the layout (the plugin's LinnEditor.cpp has the same numbers)
const FN_X = [262, 372, 454, 536, 618, 700, 782, 864], FN_Y = 432, FN_S = 62;      // SONG/PATTERN, then the 7 dual keys
const COL_X = [372, 454, 536, 618, 700, 782, 864];                                // the 1-7 keys and the LED columns above them
const TIM_Y = 552, EC_Y = 604, DIG_Y = 692, DIG_S = 62;
const DISP = [[222, 528], [222, 646]];                                            // STEP#/BEAT, PATTERN (x, y of a 104 x 64 window)
const CAS_X = 115, STORE_Y = 432, LOAD_Y = 690;
const TEMPO = [1042, 424, 36], BPM_Y = 548, EXT_Y = 692, SIDE_S = 62;
const TUNE_X = [1180, 1268, 1356, 1444, 1532, 1620, 1708], TUNE_Y = 420, TUNE_R = 30;
const VOL = [1873, 424, 38];
const MIX_X0 = 1160, MIX_DX = 51.6, PAN_Y = [548, 694], LVL_Y = [800, 1020];
const DRUM_X = [104, 210, 316, 422, 528, 634, 740, 846], DRUM_TOP = 868, DRUM_BOT = 994, DRUM_S = 70;
const PLAY = [1042, 950, 84];
const BOXES = [[40, 330, 150, 440], [205, 330, 760, 440], [980, 330, 125, 440], [1120, 330, 650, 180], [1785, 330, 175, 180],
  [1120, 525, 840, 555], [40, 790, 925, 290], [980, 790, 125, 290]];
const FN_TOP = ['SONG', 'SONG#', 'END', 'DELETE', 'INSERT', '←', '→', 'ENTER'];
const FN_BOT = ['PATTERN', 'ERROR', 'BEFORE', 'AFTER', 'COPY', 'LENGTH', 'ERASE', 'RECORD'];
const EC_N = ['1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T', 'HI'];
const MIX_N = [['BASS'], ['SNARE'], ['SIDE', 'STICK'], ['HIHAT'], ['HI'], ['MID'], ['LO'], ['RIDE'], ['CRASH'], ['CABA', 'SA'], ['TAMB'], ['HI'], ['LO'], ['COW', 'BELL'], ['CLAPS'], ['CLICK']];
const TUNE_N = ['SNARE', 'HI', 'MID', 'LO', 'HI', 'LO', 'HIHAT'];
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
const line = (p, d, w = 3) => el('path', {d, stroke:'var(--ln)', 'stroke-width':w, fill:'none', class:'ln'}, p);

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
  el('rect', {x, y, width:104, height:64, rx:4, fill:'#050505'}, p);
  el('rect', {x:x + 5, y:y + 5, width:94, height:54, rx:3, fill:'#1d0507'}, p);
  const digits = [];
  for (let i = 0; i < 2; i++){
    const polys = segPolys(x + 15 + i * 42, y + 10, 31, 44), segs = {};
    for (const k in polys) segs[k] = el('polygon', {points:polys[k].join(' '), class:'seg'}, p);
    digits.push(segs);
  }
  return digits;
}
function led(p, x, y, r = 6){
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
function knob(p, cx, cy, r){
  const g = el('g', {class:'knob'}, p);
  el('circle', {cx, cy:cy + 4, r:r + 3, fill:'rgba(0,0,0,.6)'}, g);
  el('circle', {cx, cy, r:r + 2, fill:'url(#gKnurl)'}, g);
  el('circle', {cx, cy, r:r * 0.78, fill:'url(#gKnob)'}, g);
  const ptr = el('line', {x1:cx, y1:cy - r * 0.15, x2:cx, y2:cy - r * 0.92, stroke:'#f4f4f2', 'stroke-width':Math.max(3, r * 0.12), 'stroke-linecap':'round'}, g);
  el('circle', {cx, cy, r:r + 22, fill:'transparent', class:'hit'}, g);
  return {g, ptr, cx, cy};
}
function keyCap(p, id, cx, cy, s){
  const g = el('g', {class:'key', 'data-key':id}, p), x = cx - s / 2, y = cy - s / 2;
  el('rect', {x:x - 5, y:y - 4, width:s + 10, height:s + 12, rx:4, fill:'#000'}, g);
  const c = el('g', {class:'cap'}, g);
  el('rect', {x, y, width:s, height:s, rx:3, fill:'url(#gKey)', stroke:'#3c3c3c', 'stroke-width':1.5}, c);
  el('rect', {x:x + 4, y:y + 4, width:s - 8, height:3.5, rx:1.5, fill:'rgba(255,255,255,.55)'}, c);   // the light edge along the top
  el('rect', {x:x - 12, y:y - 12, width:s + 24, height:s + 24, fill:'transparent', class:'hit'}, g);
  return g;
}
function slider(p, x, y0, y1, kind){
  const g = el('g', {class:'slider'}, p);
  el('rect', {x:x - 4, y:y0 - 4, width:8, height:y1 - y0 + 8, rx:3, fill:'#000', stroke:'#2b2b2b', 'stroke-width':1}, g);
  const cap = el('g', {}, g);
  if (kind === 'pan'){ el('rect', {x:x - 13, y:-9, width:26, height:18, rx:3, fill:'url(#gCap)', stroke:'#000', 'stroke-width':1.5}, cap); el('rect', {x:x - 13, y:-1, width:26, height:2, fill:'#111'}, cap); }
  else { el('rect', {x:x - 15, y:-13, width:30, height:26, rx:3, fill:'url(#gCap)', stroke:'#000', 'stroke-width':1.5}, cap); el('rect', {x:x - 15, y:-1.5, width:30, height:3, fill:'#111'}, cap); }
  el('rect', {x:x - 24, y:y0 - 18, width:48, height:y1 - y0 + 36, fill:'transparent', class:'hit'}, g);
  return {g, cap, y0, y1};
}

function build(svg, brand){
  svg.innerHTML = '';
  svg.setAttribute('viewBox', `0 0 ${W + 2 * CHEEK} ${H}`);
  const defs = el('defs', {}, svg);
  defs.innerHTML = `<linearGradient id="gWood" x1="0" x2="1"><stop offset="0" stop-color="#7a3c15"/><stop offset=".55" stop-color="#c4752f"/><stop offset="1" stop-color="#9a5320"/></linearGradient>
    <radialGradient id="gKnob" cx="38%" cy="30%" r="80%"><stop offset="0" stop-color="#d6d6d4"/><stop offset=".5" stop-color="#8e8e8c"/><stop offset="1" stop-color="#3c3c3a"/></radialGradient>
    <pattern id="gKnurlP" width="5" height="5" patternUnits="userSpaceOnUse"><rect width="5" height="5" fill="#222"/><rect width="2.5" height="5" fill="#3a3a3a"/></pattern>
    <radialGradient id="gKnurl" cx="50%" cy="40%" r="60%"><stop offset="0" stop-color="#4a4a4a"/><stop offset="1" stop-color="#121212"/></radialGradient>
    <linearGradient id="gKey" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2c2c2c"/><stop offset="1" stop-color="#0f0f0f"/></linearGradient>
    <linearGradient id="gCap" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f2f2f0"/><stop offset=".5" stop-color="#b4b4b2"/><stop offset="1" stop-color="#6e6e6c"/></linearGradient>
    <pattern id="gGrain" width="24" height="24" patternUnits="userSpaceOnUse"><rect width="24" height="24" fill="#141414"/><rect x="3" y="5" width="2" height="2" fill="rgba(255,255,255,.05)"/><rect x="15" y="11" width="2" height="2" fill="rgba(0,0,0,.3)"/><rect x="9" y="19" width="2" height="2" fill="rgba(255,255,255,.04)"/><rect x="20" y="2" width="2" height="2" fill="rgba(0,0,0,.2)"/></pattern>
    <linearGradient id="gSheen" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgba(255,255,255,.12)"/><stop offset="1" stop-color="rgba(255,255,255,0)"/></linearGradient>`;
  wood(svg, 0); wood(svg, CHEEK + W);
  const p = el('g', {transform:`translate(${CHEEK},0)`}, svg);
  el('rect', {x:0, y:0, width:W, height:H, fill:'url(#gGrain)'}, p);
  el('rect', {x:0, y:0, width:W, height:70, fill:'url(#gSheen)'}, p);
  const ui = {keys:{}, knobs:[], pan:[], lvl:[], leds:{tim:[], ec:[]}};
  // the back-edge jacks' legend
  const jacks = [[700, 'IN', 'OUT', 'CASSETTE'], [840, 'IN', 'OUT', 'SYNC'], [990, 'TRIGGER', 'OUTPUT'], [1100, 'REMOTE', 'PLAY/STOP']];
  for (const [x, a, b, c] of jacks){ if (c){ T(p, a, x - 32, 34, 14); T(p, b, x + 32, 34, 14); T(p, c, x, 58, 14); } else { T(p, a, x, 34, 14); T(p, b, x, 58, 14); } }
  for (let i = 0; i < 5; i++) T(p, String(i + 1), 1210 + i * 64, 34, 14);
  line(p, 'M1180 52H1236M1394 52H1450', 2); T(p, 'TRIGGER INPUTS', 1316, 58, 14);
  // the logo: the Omega's, where the hardware has its own, in the same style (solid first word, outlined second)
  const logo = (brand && brand.logo) || ['Omega', 'Drum'];
  line(p, 'M30 205H236M30 232H236', 3.5); line(p, `M${236 + 690} 205H1970M${236 + 690} 232H1970`, 3.5);
  const lg = el('text', {x:250, y:262, 'font-size':118, class:'logo'}, p);
  el('tspan', {class:'l1'}, lg, logo[0]); el('tspan', {class:'l2'}, lg, logo[1]);
  for (const [x, y, w, h] of BOXES) el('rect', {x, y, width:w, height:h, rx:10, fill:'none', stroke:'var(--ln)', 'stroke-width':3.2, class:'ln'}, p);
  // CASSETTE
  T(p, 'CASSETTE', CAS_X, 358, 19);
  ui.keys[KM.STORE] = keyCap(p, KM.STORE, CAS_X, STORE_Y, FN_S);
  T(p, 'STORE', CAS_X, 500, 15); ui.leds.store = led(p, CAS_X, 520);
  T(p, 'VERIFY', CAS_X, 566, 15); line(p, `M${CAS_X} 530V550M${CAS_X} 575V598`, 2);
  ui.leds.load = led(p, CAS_X, 610); T(p, 'LOAD', CAS_X, 640, 15);
  ui.keys[KM.LOAD] = keyCap(p, KM.LOAD, CAS_X, LOAD_Y, FN_S);
  // CREATE RHYTHM PATTERN / SONG
  T(p, 'CREATE RHYTHM PATTERN / SONG', 585, 358, 19);
  FN_X.forEach((x, i) => {
    T(p, FN_TOP[i], x, 392, i === 5 || i === 6 ? 26 : 15);
    if (i === 1){ T(p, 'ERROR', x, 487, 13); T(p, 'CORRECT', x, 500, 13); } else T(p, FN_BOT[i], x, 492, 14);
    ui.keys[i] = keyCap(p, i, x, FN_Y, FN_S);
  });
  ui.leds.song = led(p, 308, 386); ui.leds.pat = led(p, 314, 487);
  // the TIMING and ERROR CORRECT LED rows, over the 1-7 keys
  line(p, `M${COL_X[0] - 30} 510H${COL_X[6] + 54}V634H${COL_X[0] - 30}Z`, 2.4);
  T(p, 'STRAIGHT', COL_X[0] - 6, 530, 13, 'start'); line(p, `M${COL_X[1] + 10} 525H${COL_X[4] + 30}`, 2); el('path', {d:`M${COL_X[1] + 10} 525l10 -6v12z M${COL_X[4] + 30} 525l-10 -6v12z`, fill:'var(--ln)', class:'lnf'}, p);
  T(p, 'SHUFFLE', COL_X[5] + 26, 530, 13);
  T(p, 'TIMING', COL_X[2] + 40, 522, 12);
  for (let i = 0; i < 6; i++){ ui.leds.tim.push(led(p, COL_X[i] - 14, TIM_Y)); T(p, 'ABCDEF'[i], COL_X[i] + 6, TIM_Y + 7, 17, 'start'); }
  for (let i = 0; i < 7; i++){ ui.leds.ec.push(led(p, COL_X[i] - 20, EC_Y)); T(p, EC_N[i], COL_X[i] - 8, EC_Y + 6, 14, 'start'); }
  ui.dispStep = display2(p, DISP[0][0], DISP[0][1]); T(p, 'STEP#/BEAT', DISP[0][0] + 52, 610, 13);
  ui.dispPat = display2(p, DISP[1][0], DISP[1][1]); T(p, 'PATTERN', DISP[1][0] + 52, 728, 13);
  COL_X.forEach((x, i) => { T(p, String(i + 1), x, 651, 15); ui.keys[KM.D1 + i] = keyCap(p, KM.D1 + i, x, DIG_Y, DIG_S); });
  line(p, `M${COL_X[0] - 30} 748H${COL_X[1]}M${COL_X[5]} 748H${COL_X[6] + 30}`, 2); T(p, 'SELECT RHYTHM PATTERN', 618, 754, 15);
  // TEMPO
  T(p, 'TEMPO', 1042, 358, 19);
  ui.knobs.push(knob(p, TEMPO[0], TEMPO[1], TEMPO[2]));
  ui.keys[KM.BPM] = keyCap(p, KM.BPM, 1042, BPM_Y, SIDE_S); T(p, 'BPM/TRIG.', 1042, 600, 14);
  ui.leds.ext = led(p, 1042, 626);
  ui.keys[KM.EXTSYNC] = keyCap(p, KM.EXTSYNC, 1042, EXT_Y, SIDE_S); T(p, 'EXT. SYNC', 1042, 752, 14);
  // TUNING and VOLUME
  T(p, 'TUNING', 1445, 358, 19); line(p, 'M1140 352H1395M1495 352H1750', 2.4);
  TUNE_X.forEach((x, i) => { ui.knobs.push(knob(p, x, TUNE_Y, TUNE_R)); if (i < 6) T(p, TUNE_N[i], x, 476, 14); });
  T(p, 'HIHAT', TUNE_X[6], 470, 13); T(p, 'DECAY', TUNE_X[6], 486, 13);
  line(p, `M${TUNE_X[1] - 20} 488H${TUNE_X[3] + 20}M${TUNE_X[4] - 20} 488H${TUNE_X[5] + 20}`, 2);
  T(p, 'TOMS', TUNE_X[2], 503, 13); T(p, 'CONGAS', (TUNE_X[4] + TUNE_X[5]) / 2, 503, 13);
  T(p, 'VOLUME', VOL[0], 358, 19);
  ui.knobs.push(knob(p, VOL[0], VOL[1], VOL[2]));
  // MIXER: pan sliders over level faders, 16 channels
  for (let i = 0; i < 16; i++){
    const x = MIX_X0 + i * MIX_DX;
    ui.pan.push(slider(p, x, PAN_Y[0], PAN_Y[1], 'pan'));
    const n = MIX_N[i]; if (n.length === 2){ T(p, n[0], x, 722, 11); T(p, n[1], x, 735, 11); } else T(p, n[0], x, 728, 11);
    ui.lvl.push(slider(p, x, LVL_Y[0], LVL_Y[1], 'lvl'));
  }
  T(p, 'LEFT', 1131, 558, 11, 'middle', {transform:'rotate(-90 1131 558)'}); T(p, 'RIGHT', 1131, 690, 11, 'middle', {transform:'rotate(-90 1131 690)'});
  T(p, 'LEFT', 1950, 558, 11, 'middle', {transform:'rotate(-90 1950 558)'}); T(p, 'RIGHT', 1950, 690, 11, 'middle', {transform:'rotate(-90 1950 690)'});
  const mx = i => MIX_X0 + i * MIX_DX;
  line(p, `M${mx(4) - 16} 712V706H${mx(6) + 16}V712M${mx(11) - 16} 712V706H${mx(12) + 16}V712`, 2); T(p, 'TOMS', mx(5), 702, 11); T(p, 'CONGAS', (mx(11) + mx(12)) / 2, 702, 11);
  line(p, `M${mx(0) - 16} 748H${mx(3)}M${mx(5)} 748H${mx(8) + 16}M${mx(9) - 16} 748H${mx(10)}M${mx(13)} 748H${mx(14) + 16}`, 2);
  T(p, 'DRUMS', mx(4), 753, 12); T(p, 'PERCUSSION', (mx(11) + mx(12)) / 2, 753, 12);
  line(p, `M${MIX_X0 - 20} 1050H1470M1610 1050H1940`, 2.4); T(p, 'MIXER', 1540, 1057, 19);
  // the drum keys: percussion above / drums below on the upper row, the bass, snares and crash on the lower one
  // the upper row: percussion names above (bracketed over their pair), drum names below
  const top = [['CABASA', 0, 1, '1', '2'], ['TAMBOURINE', 2, 3, '1', '2'], ['CONGA', 4, 5, 'HI', 'LO'], ['COWBELL', 6, 6], ['CLAPS', 7, 7]];
  for (const [n, a, b, la, lb] of top){
    if (a === b){ T(p, n, DRUM_X[a], 822, 14); continue; }
    line(p, `M${DRUM_X[a] - 30} 820V812H${DRUM_X[b] + 30}V820`, 2); T(p, n, (DRUM_X[a] + DRUM_X[b]) / 2, 808, 13);
    T(p, la, DRUM_X[a], 826, 13); T(p, lb, DRUM_X[b], 826, 13);
  }
  const mid = [['HIHAT', 0, 2, ['1', '2', 'OPEN']], ['TOMS', 3, 5, ['HI', 'MID', 'LO']], ['RIDE', 6, 7, ['1', '2']]];
  for (const [n, a, b, ls] of mid){
    ls.forEach((s, k) => T(p, s, DRUM_X[a + k], 923, 13));
    const c = (DRUM_X[a] + DRUM_X[b]) / 2; line(p, `M${DRUM_X[a] - 34} 926V936H${c - 34}M${c + 34} 936H${DRUM_X[b] + 34}V926`, 2); T(p, n, c, 941, 13);
  }
  ui.leds.perc = led(p, DRUM_X[7] + 44, 826, 6);
  for (let k = 0; k < 8; k++) ui.keys[KM.DRUM + 8 + k] = keyCap(p, KM.DRUM + 8 + k, DRUM_X[k], DRUM_TOP, DRUM_S);
  for (let k = 0; k < 8; k++) ui.keys[KM.DRUM + k] = keyCap(p, KM.DRUM + k, DRUM_X[k], DRUM_BOT, DRUM_S);
  // the lower row: SIDESTICK, SNARE 1 2 3, BASS 1 2, CRASH, PERCUSSION
  const bot = [['SIDESTICK', 0, 0], ['SNARE', 1, 3, ['1', '2', '3']], ['BASS', 4, 5, ['1', '2']], ['CRASH', 6, 6], ['PERCUSSION', 7, 7]];
  for (const [n, a, b, ls] of bot){
    if (a === b){ T(p, n, DRUM_X[a], 1052, 14); continue; }
    ls.forEach((s, k) => T(p, s, DRUM_X[a + k], 1048, 13));
    const c = (DRUM_X[a] + DRUM_X[b]) / 2; line(p, `M${DRUM_X[a] - 34} 1052V1062H${c - 40}M${c + 40} 1062H${DRUM_X[b] + 34}V1052`, 2); T(p, n, c, 1067, 13);
  }
  // PLAY/STOP
  ui.leds.play = led(p, PLAY[0], 850, 7);
  ui.keys[KM.PLAY] = keyCap(p, KM.PLAY, PLAY[0], PLAY[1], PLAY[2]); T(p, 'PLAY/STOP', PLAY[0], 1052, 16);
  ui.sel = null;
  return ui;
}
function setDigits(digits, s){
  for (let i = 0; i < 2; i++){ const on = SEGS[s[i]] || ''; for (const k in digits[i]) digits[i][k].classList.toggle('on', on.includes(k)); }
}
function setLed(g, on){ g.classList.toggle('on', !!on); }
function setKnob(k, n){ k.ptr.setAttribute('transform', `rotate(${-150 + 300 * Math.max(0, Math.min(1, n))} ${k.cx} ${k.cy})`); }
// a slider at n (0 = bottom / RIGHT for pan, 1 = top)
function setSlider(s, n){ const y = s.y1 - Math.max(0, Math.min(1, n)) * (s.y1 - s.y0); s.cap.setAttribute('transform', `translate(0 ${y.toFixed(1)})`); }
root.FP = {build, setDigits, setLed, setKnob, setSlider, CHEEK, W:W + 2 * CHEEK, H, MIX_N};
})(typeof globalThis !== 'undefined' ? globalThis : this);
