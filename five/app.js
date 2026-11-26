/* RX5: faceplate, LCD menus, AudioWorklet host, MIDI, files. ES module. */
const $ = s => document.querySelector(s);
const P = () => $('#p');
function el(tag, cls, css, html){ const e = document.createElement(tag); if (cls) e.className = cls; if (css) Object.assign(e.style, css); if (html != null) e.innerHTML = html; return e; }

/* ================= panel geometry (from the photo, 1300 x 973) ================= */
const W = 1300, H = 973;
const GX = k => 98.3 + 32.9 * k;
const LEFT_SL = [['VOLUME', GX(0), 0], ['CLICK', GX(1), 1], ['TEMPO', GX(2), 0], ['DATA', GX(3), 1]];
const CH_X = Array.from({length:12}, (_, i) => GX(23 + i));
const CH_LBL = ['BD', 'SD', 'RIM', 'TOM 1', 'TOM 2', 'TOM 3', 'TOM 4', 'HI HAT', 'RIDE', 'CRASH', 'TMBRN<br>CLAPS', 'SHKR<br>CWBEL'];
const MODE_KEYS = [['PTN', 140], ['SONG', 191], ['EDIT\nVOICE', 243], ['CAS', 294], ['CART', 345], ['MIDI', 396], ['REV', 447], ['DAMP', 497], ['DLY', 547],
  ['MULTI', 820], ['INT\nSYNC', 921], ['MIDI\nSYNC', 972], ['TAPE\nSYNC', 1022], ['EXT\nSYNC', 1073], ['RUN', 1174]];
const FX = [328, 423, 518, 613, 707], FY = [571, 606, 641, 676];
const FKEYS = [
  ['PATTERN', 'SONG'], ['REAL TIME WRITE', 'EDIT SONG'], ['STEP WRITE', 'INSERT'], ['EDIT VOICE'], ['KEY ASSIGN'],
  ['QUANTIZE', 'DELETE'], ['SWING', 'REPEAT'], ['CLICK', 'TEMPO CHANGE'], ['CHAIN'], ['CASSETTE', 'CARTRIDGE'],
  ['EDIT PATTERN', 'VOLUME CHANGE'], ['CLEAR'], ['COPY'], ['SYNC'], ['MIDI'],
  ['REVERSE'], ['DAMP'], ['TEMPO'], ['UTILITY'], ['JOB']];
const KX = [800, 893, 987];
const KEYPAD = [['7', '1/32'], ['8', '1/48'], ['9', 'OFF'], ['4', '1/12'], ['5', '1/16'], ['6', '1/24'], ['1', '1/4'], ['2', '1/6'], ['3', '1/8'], ['-1', 'NO'], ['0', '1/2'], ['+1', 'YES']];
const PAD_X = [135, 231, 327, 422, 517, 612, 707, 801, 896, 991, 1085, 1180];
const PAD_LBL = ['BD', 'SD', 'RIM', 'TOM 1', 'TOM 2', 'TOM 3', 'TOM 4', 'HI HAT', 'RIDE', 'CRASH', 'TAMBOURINE<br>CLAPS', 'SHAKER<br>COWBELL'];
// printed legend: [x, [[header, menu id, [[text, item number] ...]] ...]]; an entry may be a list of [text, no] on one line
const LEGEND = [
  [128, [['SONG', 'song', [['01 SEARCH MARK', 1]]], ['EDIT SONG', 'esong', [['01 SEARCH MARK', 1], ['02 SET MARK', 2], ['03 SONG NAME', 3], ['04 INITIAL TEMPO', 4]]]]],
  [200, [['EDIT VOICE', 'voice', [['01 SELECT VOICE', 1], ['  WAVEFORM', 1.1], ['02 PITCH', 2], ['03 EG', 3.1], [[['( 1 AR', 3.1], [' 2 D1R', 3.2], [' 3 D1L', 3.3]]], [[['  4 D2R', 3.4], [' 5 RR', 3.5], [' 6 GT)', 3.6]]], ['04 BEND', 4.1], [[['( 1 RATE', 4.1], [' 2 RANGE)', 4.2]]]]]]],
  [292, [[' ', 'voice', [['05 VOICE LEVEL', 5], ['06 LOOP', 6], ['07 STORE VOICE', 7], ['08 INITIALIZE VOICE', 8], ['09 RECALL EDIT', 9], ['10 REVERSE', 10], ['11 LOAD WAV', 11], ['12 BITS', 12]]]]],
  [372, [['CHAIN', 'chain', [['01 PLAY', 1], ['02 EDIT', 2], ['03 CHAIN NAME', 3], ['04 CLEAR', 4]]]]],
  [430, [['UTILITY', 'util', [['01 REMAINING PATTERN', 1], ['02 REMAINING SONG', 2], ['03 MEMORY PROTECT', 3], ['04 CHANGE VOICE', 4], ['05 CLEAR ALL PATTERNS', 5], ['06 CLEAR ALL SONGS', 6]]]]],
  [521, [['KEY ASSIGN', 'key', [['01 VOICE ASSIGN', 1], ['02 PARAM ASSIGN(*)', 2.1], ['03 MULTI VOICE', 3], ['04 MULTI STEP(*)', 4], ['05 ACCENT LEVEL', 5.1], ['06 OUTPUT CH ASSIGN', 6]]]]],
  [614, [[' ', 'key', [['07 SAVE KEY DATA', 7], ['08 LOAD KEY DATA', 8], ['09 COPY VOICE', 9], [[['*1 PITCH', 2.1], [' 2 LEVEL', 2.2]]], [[['  3 ATTACK', 2.3], [' 4 DECAY', 2.4]]], ['10 PAN', 10], ['11 REV  12 DLY SEND', 11], ['13 PAD VELOCITY', 13]]]]],
  [688, [['CASSETTE/CARTRIDGE', 'cart', [['01-04 SAVE SLOT', 1], ['11-14 LOAD SLOT', 11], ['21 SAVE FILE', 21], ['22 LOAD FILE', 22], ['23 LOAD WAV VOICE', 23], ['24 VOICE BANK', 24], ['25 LOAD KIT', 25], ['30 FORMAT SLOT', 30]]]]],
  [790, [['MIDI', 'midi', [['01 CH MESSAGE', 1], ['02 RECEIVE CH', 2], ['03 TRANSMIT CH', 3], ['04 NOTE NUMBER ASSIGN', 4.1], ['05 INITIALIZE NOTE', 5], ['06 EG VELOCITY', 6], ['07 GATE TIME', 7]]]]],
  [893, [[' ', 'midi', [['08 DEVICE NUMBER', 8], ['09 RECEIVE BULK', 9], ['10 TRANSMIT BULK', 10], ['11 CLOCK OUT', 11], ['12 NOTES OUT', 12], ['13 IN PORT', 13], ['14 OUT PORT', 14], ['15 SYX FILE', 15]]]]],
  [972, [['JOB', 'job', [['01 IMPORT MIDI', 1], ['02 EXPORT MIDI', 2], ['03 EXPORT WAV', 3], ['04 RANDOMIZE', 4], ['05 INIT ALL', 5]]]]],
];

const VOICE_NAMES = ['BD 1', 'SD 1', 'RIM 1', 'TOM 1A', 'TOM 2A', 'TOM 3A', 'TOM 4A', 'HH CLS', 'RIDE', 'CRASH', 'TAMB', 'SHAKER',
  'BD 2', 'SD 2', 'RIM 2', 'TOM 1B', 'TOM 2B', 'TOM 3B', 'TOM 4B', 'HH OPN', 'RIDE CP', 'CHINA', 'CLAPS', 'COWBELL'];
const QUANT = [['1/2', 48], ['1/4', 24], ['1/6', 16], ['1/8', 12], ['1/12', 8], ['1/16', 6], ['1/24', 4], ['1/32', 3], ['1/48', 2], ['1/96', 1], ['OFF', 1]];
const KEY_Q = {0:0, 1:1, 2:2, 3:3, 4:4, 5:5, 6:6, 7:7, 8:8, 9:10};

/* ================= build the faceplate ================= */
const sliders = {};
function buildPanel(){
  const sl = $('#sliders'), p = P();
  // wells cut into the ribs, then the sliders and their printed names on the lip
  // the ribs: grooves every 32.9 px, the sliders run in grooves 0-3 and 23-34 (cut wider there, with scale notches)
  const slotG = k => (k >= 0 && k <= 3) || (k >= 23 && k <= 34);
  for (let k = -3; k <= 36; k++){
    const g = GX(k), left = g + 4.5, w = 24;
    if (slotG(k) || slotG(k + 1)){
      p.insertBefore(el('div', 'rib cut', {left:left + 'px', width:w + 'px'}), sl);
      const nl = slotG(k) ? left + 3 : left, nw = w - (slotG(k) ? 3 : 0) - (slotG(k + 1) ? 3 : 0);
      p.insertBefore(el('div', 'rib lo', {left:nl + 'px', width:nw + 'px'}), sl);
    } else p.insertBefore(el('div', 'rib', {left:left + 'px', width:w + 'px'}), sl);
  }
  for (const [name, x, lo] of LEFT_SL){
    makeSlider(name, x - 15, 160, 138);
    p.appendChild(el('div', 'sl', {left:x + 'px', top:(lo ? 326 : 316) + 'px'}, name));
  }
  CH_X.forEach((x, i) => { makeSlider('CH' + i, x - 15, 160, 138); p.appendChild(el('div', 'sl', {left:x + 'px', top:'317px', lineHeight:'1.1'}, CH_LBL[i])); });
  const mk = $('#modekeys');
  for (const [name, x] of MODE_KEYS){
    const b = el('button', 'mk', {left:x + 'px'}); b.textContent = name; b.dataset.k = name.replace('\n', ' ');
    mk.appendChild(b);
  }
  // legend
  const lg = $('#legend');
  for (const [x, groups] of LEGEND){
    const col = el('div', 'legend', {left:x + 'px', top:'460px'});
    for (const [h, m, items] of groups){
      const hd = el('b', h.trim() ? 'h' : '', null, h.trim() || '&nbsp;'); hd.dataset.m = m; col.appendChild(hd);
      for (const it of items){
        if (Array.isArray(it[0])){ const line = el('em'); for (const [t, n] of it[0]){ const u = el('u', '', {display:'inline'}, t.replace(/ /g, '&nbsp;')); u.dataset.m = m; u.dataset.i = n; line.appendChild(u); } col.appendChild(line); }
        else { const u = el('u', '', null, it[0]); u.dataset.m = m; u.dataset.i = it[1]; col.appendChild(u); }
      }
    }
    lg.appendChild(col);
  }
  fitLegend(); if (document.fonts && document.fonts.ready) document.fonts.ready.then(fitLegend);
  // lcd cells
  for (const r of [0, 1]){ const row = $('#lcd' + r); for (let i = 0; i < 16; i++) row.appendChild(el('i')); }
  // accent, function keys, keypad, transport
  const k = $('#keys');
  const a1 = el('button', 'big acc', {left:'95px', top:'563px'}, '<small>Y</small>ACCENT 1'); a1.dataset.k = 'ACC1';
  const a2 = el('button', 'big acc', {left:'95px', top:'634px'}, '<small>Z</small>ACCENT 2'); a2.dataset.k = 'ACC2';
  k.append(a1, a2, el('div', 'sl', {left:'136px', top:'696px', fontSize:'11px'}, '&harr;'));
  FKEYS.forEach((lb, i) => {
    const b = el('button', 'fk', {left:FX[i % 5] + 'px', top:FY[i / 5 | 0] + 'px'}, lb.map(t => `<span>${t}</span>`).join(''));
    b.dataset.k = lb[0]; k.appendChild(b);
  });
  k.append(el('div', 'rule', {left:'566px', top:'558px', width:'1px', height:'132px'}), el('div', 'rule', {left:'283px', top:'658px', width:'283px', height:'1px'}),
    el('div', 'rule', {left:'283px', top:'658px', width:'1px', height:'8px'}), el('div', 'sl', {left:'389px', top:'657px', fontSize:'10px'}, '&bull;'));
  KEYPAD.forEach(([n, q], i) => {
    const red = n === '-1' || n === '+1';
    const b = el('button', 'kp' + (red ? ' red' : ''), {left:KX[i % 3] + 'px', top:FY[i / 3 | 0] + 'px'});
    b.textContent = red ? `${n}    ${q}` : `${n}      ${q}`; b.dataset.k = red ? (n === '-1' ? 'NO' : 'YES') : 'N' + n; k.appendChild(b);
  });
  const st = el('button', 'big stop', {left:'1140px', top:'562px'}, 'STOP/<br>CONTINUE'); st.dataset.k = 'STOP';
  const go = el('button', 'big start', {left:'1140px', top:'634px'}, 'START'); go.dataset.k = 'START';
  k.append(st, go, el('div', 'sl', {left:'1181px', top:'624px'}, 'SHIFT/COMPARE'), el('div', 'sl', {left:'1181px', top:'696px'}, 'ENTER'));
  // pads
  const pd = $('#pads');
  for (let i = 0; i < 24; i++){
    const b = el('div', 'pad', {left:PAD_X[i % 12] + 'px', top:(i < 12 ? 721 : 794) + 'px'}, `<b>${String.fromCharCode(65 + i)}</b><i class="fl"></i>`);
    b.dataset.pad = i; pd.appendChild(b);
  }
  PAD_X.forEach((x, i) => pd.appendChild(el('div', 'pl', {left:x + 'px', top:'867px'}, `${PAD_LBL[i]}<br>${i + 1}`)));
}
// the magnifier: a copy of one legend column at 3x, near where it was tapped, kept inside the panel
const MAG = 3;
function magnify(col){
  const mg = $('#mag'), inner = mg.firstElementChild;
  inner.innerHTML = col.innerHTML; inner.style.zoom = MAG;
  if (U.page === 'menu'){ const it = curItem(); if (it) inner.querySelectorAll(`u[data-m="${U.menu}"]`).forEach(u => u.classList.toggle('on', +u.dataset.i === it.no)); }
  mg.style.display = 'block';
  const w = mg.offsetWidth, h = mg.offsetHeight, x = parseFloat(col.style.left), cx = x + col.offsetWidth / 2;
  mg.style.left = Math.round(Math.max(12, Math.min(W - w - 12, cx - w / 2))) + 'px';
  mg.style.top = Math.round(Math.max(12, Math.min(H - h - 12, 470 - h / 2))) + 'px';
}
// the printed legend in narrow columns: phones without the narrow font draw it wider, so each column is squeezed
// sideways to the room it has before the next column (or the logo); lines then never run into each other
function fitLegend(){
  const cols = [...document.querySelectorAll('#legend > .legend')].sort((a, b) => parseFloat(a.style.left) - parseFloat(b.style.left));
  cols.forEach((c, i) => {
    c.style.transform = ''; c.style.transformOrigin = '0 0';
    const left = parseFloat(c.style.left), next = i + 1 < cols.length ? parseFloat(cols[i + 1].style.left) : 1040;
    const room = next - left - 3, w = Math.max(...[...c.querySelectorAll('b, u, em')].map(e => e.scrollWidth), c.scrollWidth);
    if (w > room) c.style.transform = `scaleX(${(room / w).toFixed(3)})`;
  });
}
function makeSlider(id, left, top, h){
  const s = el('div', 'slider', {left:left + 'px', top:top + 'px', height:h + 'px'});
  const cap = el('div', 'cap'); s.appendChild(cap);
  s.dataset.s = id; $('#sliders').appendChild(s);
  sliders[id] = {el:s, cap, h, v:0};
}
function setSlider(id, v){ const s = sliders[id]; if (!s) return; s.v = Math.max(0, Math.min(1, v)); s.cap.style.top = (22 + (s.h - 40) * (1 - s.v)) + 'px'; }

/* ================= fit to the screen =================
   Like Omega 110 (and the screen rules in CLAUDE.md): nothing ever scrolls. Landscape: a rail of page keys on the left,
   then the machine or one page; portrait: the machine on top, the page keys under it, then the page (MACHINE shows a
   big LCD with its keys there). FOLD: the bare machine, with EXPAND in the corner. The page code is further down. */
let scale = 1, panelBox = {x:0, y:0, w:W, h:H};
function fitPanel(x, y, w, h, align){
  // short landscape areas: the plain ribs above the sliders and the strip under the pad names are cropped
  // (down to just above the fader caps, so the machine reaches the top edge of the screen)
  const crop = w / h > 1.4 && h < 800, top = crop ? 148 : 0, bot = crop ? 70 : 0, ph = H - top - bot;
  scale = Math.min(w / W, h / ph);
  const px = x + (w - W * scale) / 2, py = (align === 'top' ? y : y + (h - ph * scale) / 2) - top * scale;
  P().style.transform = `translate(${Math.round(px)}px, ${Math.round(py)}px) scale(${scale})`;
  panelBox = {x:px, y:py + top * scale, w:W * scale, h:ph * scale};
  return panelBox;
}
function fit(){
  const vw = innerWidth, vh = innerHeight, land = vw >= vh, folded = document.body.classList.contains('folded');
  const rail = $('#rail'), pv = $('#pageView'), place = (e, x, y, w, h) => Object.assign(e.style, {left:x + 'px', top:y + 'px', width:w + 'px', height:h + 'px'});
  $('#hint').style.display = 'none';
  document.body.classList.toggle('land', land); document.body.classList.toggle('port', !land);
  if (folded){ rail.classList.add('mini'); place(rail, 6, 6, 86, 34); pv.style.display = 'none'; $('#stage').style.display = ''; fitPanel(0, 0, vw, vh); hideLoupe(); return; }
  rail.classList.remove('mini');
  if (land){
    // 11 keys: one column on tall screens, two on short ones (a phone on its side) so each key stays tappable
    // 11 keys in one narrow column (the machine gets the width); two columns only on very short screens
    const two = vh / 11 < 26, rw = Math.round(two ? Math.max(110, Math.min(140, vw * .14)) : Math.max(70, Math.min(96, vw * .085)));
    rail.style.gridTemplateColumns = two ? '1fr 1fr' : '1fr';
    place(rail, 0, 0, rw, vh);
    if (view === 'machine'){ $('#stage').style.display = ''; pv.style.display = 'none'; fitPanel(rw + 2, 0, vw - rw - 2, vh); }
    else { $('#stage').style.display = 'none'; hideLoupe(); showView(rw + 4, 0, vw - rw - 4, vh); }
  } else {
    rail.style.gridTemplateColumns = '';
    $('#stage').style.display = '';
    const b = fitPanel(0, 0, vw, Math.min(vh * .5, vw * H / W), 'top');
    const ry = Math.round(b.y + b.h + 4), rh = Math.round(Math.min(96, Math.max(70, vh * .11)));
    place(rail, 0, ry, vw, rh);
    showView(0, ry + rh + 4, vw, vh - ry - rh - 4);
  }
}

/* ================= modules ================= */
const SQ = await import('./seq.js');
let ENG = {}; try { ENG = await import('./engine.js'); } catch (e) { console.warn('engine.js', e.message); }
const opt = async f => { try { return await import(f); } catch (e) { return null; } };
const RXS = await opt('./rxsysex.js'), RXC = await opt('./rxcart.js'), KITS = await opt('./kits.js');
const NCHAINS = SQ.NUM_CHAINS || 3;

/* ================= small helpers ================= */
const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
const p2 = n => String(n).padStart(2, '0'), p3 = n => String(n).padStart(3, '0');
const L16 = s => (String(s) + ' '.repeat(16)).slice(0, 16);
const LR = (a, b) => { a = String(a); b = String(b); return L16(a + ' '.repeat(Math.max(1, 16 - a.length - b.length)) + b).slice(0, 16); };
const songL = n => String.fromCharCode(65 + n);
const vname = v => (SQ.VOICE_NAMES || VOICE_NAMES)[v];
const QN = i => (SQ.QUANTIZE ? SQ.QUANTIZE[i].name : QUANT[i][0]);
const QT = i => (SQ.QUANTIZE ? SQ.QUANTIZE[i].ticks : QUANT[i][1]);
function toast(msg, ms = 2600){ const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toast.h); toast.h = setTimeout(() => t.classList.remove('show'), ms); }

/* ================= defaults the engine owns (adapters: engine.js may name these differently) ================= */
const FALLBACK_VOICE = {pitch:0, level:25, ar:99, d1r:0, d1l:60, d2r:0, rr:45, gate:0, bendRate:0, bendRange:0, reverse:0, loop:0, acc1:2, acc2:4, bits:12};
function voiceDefault(v){
  const D = ENG.VOICE_DEFAULTS;
  const d = D ? (Array.isArray(D) ? D[v] : typeof D === 'function' ? D(v) : D[v] || D) : null;
  return Object.assign({}, FALLBACK_VOICE, d || {});
}
const STEREO = [8, 9, 9, 12, 10, 6, 4, 13, 3, 11, 5, 8]; // hardware stereo positions, 15 = left .. 1 = right
function chanDefault(ch){
  const D = ENG.CHANNEL_DEFAULTS;
  const d = D ? (Array.isArray(D) ? D[ch] : D) : null;
  return Object.assign({fader:1, pan:(8 - STEREO[ch]) / 7, sendRev:0.12, sendDly:0, mute:false}, d || {});
}
const FX_FALLBACK = {
  rev:{program:0, time:2.2, hiRatio:0.6, delay:20, diffusion:7, density:80, erLevel:0.6, revLevel:0.8, hpf:60, lpf:9000, gateTime:250},
  dly:{timeL:375, timeR:375, sync:0, feedback:35, pingpong:0, hiCut:8000, loCut:120, modRate:0.5, modDepth:0.1, duckThresh:-24, duckRelease:200, spread:0.8, mix:0.3}};
function fxDefault(kind){ const D = kind === 'rev' ? ENG.REV_DEFAULTS : ENG.DLY_DEFAULTS; return Object.assign({}, FX_FALLBACK[kind], D || {}); }
const REV_PROGRAMS = (ENG.REV_PROGRAMS || ['HALL1', 'HALL2', 'ROOM1', 'ROOM2', 'VOCAL', 'PLATE1', 'PLATE2', 'ER1', 'ER2', 'GATE', 'REV GATE']).map(p => typeof p === 'string' ? p : p.name);
const RG = (grp, k, d) => (ENG.RANGES && ENG.RANGES[grp] && ENG.RANGES[grp][k]) || d;
// a reverb program loads its own parameters (as the engine does), keeping the return level
function setRevProgram(i){
  const pr = ENG.REV_PROGRAMS && ENG.REV_PROGRAMS[i], o = {program:i, level:fget('rev', 'level')};
  if (pr && typeof pr === 'object') for (const k of ['time', 'hiRatio', 'delay', 'diffusion', 'density', 'erLevel', 'revLevel', 'hpf', 'lpf', 'gateTime']) o[k] = pr[k];
  S.fx = S.fx || {}; S.fx.rev = o; post({t:'fx', kind:'rev', k:'program', val:i}); for (const k in o) if (k !== 'program') post({t:'fx', kind:'rev', k, val:o[k]}); dirty();
}
const DLY_SYNC = SQ.DLY_SYNC || ['off', '1/4', '1/8', '1/8.', '1/16', '1/4T'];
const RX5_NOTES = SQ.RX5_NOTES || (SQ.NOTE_PRESETS && SQ.NOTE_PRESETS.rx5) || null;
const NOTE_PRESETS = SQ.NOTE_PRESETS || {gm:SQ.MIDI_GM_NOTES, ...(RX5_NOTES ? {rx5:RX5_NOTES} : {})};

/* ================= state ================= */
let S = SQ.newState();
// panel settings that are not part of the cartridge
const U = {mode:'pattern', ptn:0, song:0, chain:0, voice:0, ch:0, pad:0, page:'ptn', menu:null, item:{}, songStep:0, chainStep:0, stepPos:0, stepVoice:0,
  multi:{on:false, v:0, param:0}, padVel:0, protect:false, stored:{},
  midiIn:'all', midiOut:'', bulk:0, syx:0, rnd:0, exp:0, impAt:-1, slot:0, tunable:false};
const IDB = (() => {
  let db = null;
  const open = () => db || (db = new Promise((res, rej) => { const r = indexedDB.open('omega-five', 1); r.onupgradeneeded = () => r.result.createObjectStore('kv'); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); }));
  const tx = async (mode, f) => { const d = await open(); return new Promise((res, rej) => { const t = d.transaction('kv', mode), st = t.objectStore('kv'), r = f(st); t.oncomplete = () => res(r && r.result); t.onerror = () => rej(t.error); }); };
  return {get:k => tx('readonly', s => s.get(k)), set:(k, v) => tx('readwrite', s => s.put(v, k)), del:k => tx('readwrite', s => s.delete(k))};
})();
let saveT = 0;
function dirty(){ clearTimeout(saveT); saveT = setTimeout(saveNow, 700); }
async function saveNow(){ try { await IDB.set('state', {state:S, ui:U}); } catch (e) { console.warn('autosave', e); } }
async function loadSaved(){
  try {
    const r = await IDB.get('state');
    if (r && r.state){ S = SQ.normalizeState(r.state); }
    if (r && r.ui){ const keep = {page:'ptn', menu:null}; Object.assign(U, r.ui, keep); }
    // older versions kept the WAVEFORM picks in the UI state (U.wave): move them into the cartridge (S.wave)
    if (U.wave && Object.keys(U.wave).length && !(S.wave || []).some(x => x != null)){ S.wave = new Array(24).fill(null); for (const k in U.wave) if (+k >= 0 && +k < 24) S.wave[+k] = U.wave[k]; }
    delete U.wave;
  } catch (e) { console.warn('restore', e); }
  U.mode = U.mode === 'song' ? 'song' : 'pattern'; U.page = U.mode === 'song' ? 'songplay' : 'ptn';
}
const vget = (v, k) => { const o = S.voice[v] || {}; return o[k] !== undefined ? o[k] : voiceDefault(v)[k]; };
function vset(v, k, val){ (S.voice[v] = S.voice[v] || {})[k] = val; post({t:'voice', v, k, val}); dirty(); }
const cget = (ch, k) => { const o = S.channel[ch] || {}; return o[k] !== undefined ? o[k] : chanDefault(ch)[k]; };
function cset(ch, k, val){ S.channel[ch] = Object.assign(chanDefault(ch), S.channel[ch], {[k]:val}); post({t:'channel', ch, o:S.channel[ch]}); dirty(); }
const fget = (kind, k) => { const o = (S.fx && S.fx[kind]) || {}; return o[k] !== undefined ? o[k] : fxDefault(kind)[k]; };
function fset(kind, k, val){ S.fx = S.fx || {}; (S.fx[kind] = S.fx[kind] || {})[k] = val; post({t:'fx', kind, k, val}); dirty(); }
function sset(k, val){ S[k] = val; post({t:'set', k, v:val}); dirty(); }
const pat = () => S.patterns[U.ptn], song = () => S.songs[U.song];
function patChanged(n = U.ptn){ post({t:'pat', n, p:S.patterns[n]}); dirty(); }
function songChanged(n = U.song){ post({t:'song', n, s:S.songs[n]}); dirty(); }
function chainChanged(n = U.chain){ post({t:'chain', n, c:S.chains[n]}); dirty(); }
function allChanged(){ post({t:'state', state:S}); if (kitLoaded) romsNow().then(roms => post({t:'roms', roms})); dirty(); paintSliders(); }
function protectedMsg(){ if (U.protect){ flash('MEMORY PROTECTED', 'UTILITY 03: OFF'); return true; } return false; }

/* ================= the factory kit (RX5 recordings in kit/, 25 kHz) ================= */
// voice -> file; engine.js exports FACTORY_MAP (array or {v:file}); this list is the fallback
const KIT_FALLBACK = ['BDrum1', 'SDrum1', 'Rim1', 'Tom1', 'Tom2', 'Tom3', 'Tom4', 'CHat', 'RideEdge', 'Crash', 'Tamb', 'Shaker',
  'BDrum2', 'SDrum2', 'Rim2', 'ETom1', 'ETom2', 'ETom3', 'ETom4', 'OHat', 'RideBell', 'China', 'Clap', 'Cowbell'].map(n => n + '-RX5.wav');
function factoryMap(){
  const F = ENG.FACTORY_MAP;
  const get = v => { const x = F && (Array.isArray(F) ? F[v] : F[v] ?? F[String(v)]); return x ? (typeof x === 'string' ? x : x.file || x.name) : KIT_FALLBACK[v]; };
  return Array.from({length:24}, (_, v) => get(v));
}
// WAV (PCM 8/16/24/32, float 32) -> mono Float32Array
function parseWav(buf){
  const dv = new DataView(buf), str = (o, n) => String.fromCharCode(...new Uint8Array(buf, o, n));
  if (str(0, 4) !== 'RIFF' || str(8, 4) !== 'WAVE') throw new Error('not a WAV file');
  let o = 12, fmt = null, data = null;
  while (o + 8 <= buf.byteLength){
    const id = str(o, 4), len = dv.getUint32(o + 4, true), b = o + 8;
    if (id === 'fmt ') fmt = {tag:dv.getUint16(b, true), ch:dv.getUint16(b + 2, true), sr:dv.getUint32(b + 4, true), bits:dv.getUint16(b + 14, true)};
    else if (id === 'data') data = [b, Math.min(len, buf.byteLength - b)];
    o = b + len + (len & 1);
  }
  if (!fmt || !data) throw new Error('WAV without fmt/data');
  const bps = fmt.bits / 8, n = Math.floor(data[1] / (bps * fmt.ch)), out = new Float32Array(n), fl = fmt.tag === 3 || (fmt.tag === 0xFFFE && fmt.bits === 32 && false);
  for (let i = 0; i < n; i++){
    let acc = 0;
    for (let c = 0; c < fmt.ch; c++){
      const p = data[0] + (i * fmt.ch + c) * bps;
      acc += fl ? dv.getFloat32(p, true) : bps === 2 ? dv.getInt16(p, true) / 32768 : bps === 1 ? (dv.getUint8(p) - 128) / 128 : bps === 3 ? ((dv.getUint8(p) | dv.getUint8(p + 1) << 8 | dv.getInt8(p + 2) << 16) / 8388608) : dv.getInt32(p, true) / 2147483648;
    }
    out[i] = acc / fmt.ch;
  }
  return {data:out, sr:fmt.sr};
}
let kitLoaded = false, FACTORY = [];   // FACTORY: the factory kit's waveforms (library for KEY ASSIGN / EDIT VOICE)
const kitReady = (async () => {
  const roms = new Array(24).fill(null);
  try {
    if (!KITS) throw new Error('kits.js missing');
    const kit = await KITS.loadFactoryKit(p => fetch(p));
    FACTORY = kit.waveforms.map(w => ({id:w.id, name:w.name, sr:w.sr, data:w.data, file:w.file}));
    for (const x of kit.assignments){ const w = kit.waveforms[x.libIndex]; roms[x.v] = {data:w.data, sr:w.sr}; }
  } catch (e) {
    console.warn('factory kit via kits.js:', e.message);
    const map = factoryMap();
    await Promise.all(map.map(async (f, v) => { try { const r = await fetch('kit/' + f); if (!r.ok) throw new Error(r.status); roms[v] = parseWav(await r.arrayBuffer()); FACTORY.push({id:v, name:f.replace(/-RX5\.wav$/, ''), ...roms[v], file:f}); } catch (err) { console.warn('kit', f, err.message); } }));
  }
  kitLoaded = true; return roms;
})();
// the waveform library: the loaded kit's waveforms (S.library, saved in the cartridge) first, then the factory ones
// whose ids it doesn't use. A voice plays its user sample, else the waveform picked in EDIT VOICE (S.wave[v] = id,
// saved in the cartridge; the plugin reads the same ids), else its factory one.
function waveLib(){ const L = (S.library || []).filter(w => w && w.data), ids = new Set(L.map(w => w.id)); return L.concat(FACTORY.filter(w => !ids.has(w.id))); }
const findWave = id => id == null ? null : waveLib().find(x => x.id === id) || null;
async function romsNow(){
  const roms = (await kitReady).slice();
  for (let v = 0; v < 24; v++){ const w = findWave(S.wave && S.wave[v]); if (w) roms[v] = {data:w.data, sr:w.sr}; }
  return roms;
}
function setWave(v, id){
  if (!findWave(id)) return;
  if (!Array.isArray(S.wave)) S.wave = new Array(24).fill(null);
  S.wave[v] = id; dirty();
  romsNow().then(roms => post({t:'roms', roms}));
}
const waveName = v => { if (S.samples[v]) return 'USR ' + (S.samples[v].name || '').slice(0, 6); const w = findWave(S.wave && S.wave[v]); return (w ? w.name : 'INT ' + vname(v)).slice(0, 10); };
async function loadKitFiles(files){
  if (!KITS) return flash('LOAD KIT', 'not available');
  if (protectedMsg()) return;
  flash('LOADING KIT', files.length + ' file(s)', 30000);
  const list = await Promise.all([...files].map(async f => ({name:f.webkitRelativePath || f.name, bytes:new Uint8Array(await f.arrayBuffer())})));
  const kit = await KITS.loadKit(list, {});
  KITS.applyKit(null, S, kit);
  allChanged();
  const n = kit.assignments.length;
  flash('KIT ' + n + ' VOICES', (kit.waveforms.length - n) + ' in library', 2600);
  if (KITS.formatKitReport) console.log(KITS.formatKitReport(kit, VOICE_NAMES));
  if (kit.report.warnings.length) toast(kit.report.warnings.slice(0, 3).join('; '), 6000);
}

/* ================= audio: the worklet ================= */
let ctx = null, node = null, audioReady = null, pos = null, meter = {peak:[0, 0], ch:[], max:0};
const queue = [];
function post(m){ if (node) node.port.postMessage(m); else if (m.t !== 'state') queue.push(m); }
function audio(){
  if (audioReady) { if (ctx && ctx.state !== 'running') ctx.resume().catch(() => {}); return audioReady; }
  audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    await ctx.audioWorklet.addModule('worklet.js');
    const roms = await romsNow();   // voices are silent until the factory kit is in
    node = new AudioWorkletNode(ctx, 'omega-five', {numberOfInputs:0, outputChannelCount:[2], processorOptions:{state:S, roms}});
    node.connect(ctx.destination);
    node.port.onmessage = ev => fromWorklet(ev.data);
    for (const m of queue.splice(0)) if (m.t === 'hit') node.port.postMessage(m);
    post({t:'ext', on:S.sync === 'midi' || S.sync === 'ext'}); post({t:'midiOut', clock:!!S.midi.clockOut, notes:!!S.midi.notesOut});
    if (ctx.state !== 'running') await ctx.resume().catch(() => {});
  })().catch(e => { console.error(e); flash('AUDIO ERROR', String(e.message || e).slice(0, 16)); audioReady = null; });
  return audioReady;
}
function fromWorklet(m){
  if (m.t === 'pos'){
    pos = m.pos; meter.peak = m.peak; meter.ch = m.ch; meter.max = Math.max(meter.max, m.peak[0], m.peak[1]);
    for (const v of m.fired) flashPad(v);
    if (!pos || !pos.running) U.cueShown = -1;
    paintLeds(); if (U.page === 'ptn' || U.page === 'songplay' || U.page === 'rtw' || (U.page === 'menu' && U.menu === 'chain')) paintLcd();
  } else if (m.t === 'rec'){
    // the worklet recorded into its copy; mirror it here
    SQ.stepInsert(S, m.pn, m.e.t, m.e.v, m.e.vel, m.e.acc); dirty();
  } else if (m.t === 'midi') midiFromWorklet(m.ev);
  else if (m.t === 'ended'){ paintLeds(); paintLcd(); midiSend([0xFC]); }
}
const running = () => !!(pos && pos.running);
window.__five = {get meter(){ return meter; }, get pos(){ return pos; }, get state(){ return S; }, get ui(){ return U; }, audio, key:k => keyDown(k, true), keyUp:k => keyUp(k), lcd:() => lcdText.join('\n'), get ctx(){ return ctx; }};

/* ================= transport ================= */
function playTarget(){
  if (U.mode === 'song') return U.chainPlay ? {mode:'chain', n:U.chain} : {mode:'song', n:U.song};
  return {mode:'pattern', n:U.ptn};
}
async function start(){
  if (!kitLoaded){ flash('LOADING KIT', 'please wait', 900); return; }
  await audio();
  const t = playTarget();
  if (S.sync === 'midi' || S.sync === 'ext'){ flash('WAITING FOR', 'MIDI START'); return; }
  post({t:'transport', cmd:'start', ...t}); post({t:'tempo', bpm:U.mode === 'song' && U.tempoTouched ? S.tempo : null});
  midiSend([0xFA]); pos = Object.assign({}, pos || {}, {running:true});
}
function stop(){ post({t:'transport', cmd:'stop'}); midiSend([0xFC]); if (pos) pos.running = false; paintLeds(); }
async function cont(){ await audio(); post({t:'transport', cmd:'cont', ...playTarget()}); midiSend([0xFB]); }
function damp(){ post({t:'damp'}); U.dampT = performance.now(); paintLeds(); setTimeout(paintLeds, 260); }

/* ================= LCD ================= */
let lcdText = [L16('RX5'), L16('')], lcdCur = -1, flashT = 0, flashTx = null;
function setLcd(a, b, cur = -1){
  a = L16(a); b = L16(b); lcdText = [a, b]; lcdCur = cur;
  for (const r of [0, 1]){ const cells = $('#lcd' + r).children, s = r ? b : a; for (let i = 0; i < 16; i++){ const c = cells[i]; if (c.textContent !== s[i]) c.textContent = s[i]; c.classList.toggle('cur', r === 1 && i === cur); } }
}
function unflash(){ if (flashTx){ flashTx = null; clearTimeout(flashT); } }
function flash(a, b, ms = 1400){ flashTx = [a, b]; clearTimeout(flashT); flashT = setTimeout(() => { flashTx = null; paintLcd(); }, ms); setLcd(a, b); }
function paintLcd(){
  if (flashTx){ setLcd(flashTx[0], flashTx[1]); return; }
  const pg = page(); const r = pg.lcd(); setLcd(r[0], r[1], r[2] == null ? -1 : r[2]);
  // the legend item and function key of the open page
  document.querySelectorAll('.legend u.on, .fk.sel').forEach(e => e.classList.remove('on', 'sel'));
  if (U.page === 'menu'){ const it = curItem(); if (it) document.querySelectorAll(`.legend u[data-m="${U.menu}"]`).forEach(u => { if (+u.dataset.i === it.no) u.classList.add('on'); }); }
  const fk = PAGE_KEY[U.page === 'menu' ? U.menu : U.page]; if (fk) { const b = document.querySelector(`.fk[data-k="${fk}"]`); if (b) b.classList.add('sel'); }
}
const PAGE_KEY = {rtw:'REAL TIME WRITE', step:'STEP WRITE', voice:'EDIT VOICE', key:'KEY ASSIGN', quant:'QUANTIZE', swing:'SWING', click:'CLICK', chain:'CHAIN', cart:'CASSETTE',
  eptn:'EDIT PATTERN', clear:'CLEAR', copy:'COPY', sync:'SYNC', midi:'MIDI', reverse:'REVERSE', tempo:'TEMPO', util:'UTILITY', job:'JOB', esong:'REAL TIME WRITE', ptn:'PATTERN', songplay:'PATTERN'};

/* ================= numeric entry ================= */
let digits = '', digitT = 0;
function typed(d, max = 2){ clearTimeout(digitT); digits = (digits + d).slice(-max); digitT = setTimeout(() => { digits = ''; }, 1600); return digits; }

/* ================= pages ================= */
// a page: {lcd() -> [line1, line2, cursor?], inc(d), digit(d), yes(), no(), pad(i, v, vel), data(x 0..1), move(d)}
function numItem(no, name, get, set, min, max, opts = {}){
  const step = opts.step || 1, fine = opts.fine || step, fmt = opts.fmt || (x => String(Math.round(x)));
  const snap = x => clamp(+(Math.round(x / fine) * fine).toFixed(4), min, max);
  return {no, name, ctx:opts.ctx, pad:opts.pad, l2:() => fmt(get()),
    inc:(d, sh) => set(snap(get() + d * (sh ? fine : step))),
    data:x => set(snap(min + (max - min) * (opts.curve ? Math.pow(x, opts.curve) : x))),
    digit:opts.digits ? d => set(clamp(+typed(d, opts.digits), min, max)) : null};
}
function listItem(no, name, get, set, list, opts = {}){
  return {no, name, ctx:opts.ctx, l2:() => list[clamp(get(), 0, list.length - 1)], inc:d => set(clamp(get() + d, 0, list.length - 1)), data:x => set(Math.round(x * (list.length - 1))), pad:opts.pad};
}
function actItem(no, name, run, opts = {}){
  return {no, name, ctx:opts.ctx, l2:opts.l2 || (() => ' sure? NO / YES'), yes:run, inc:opts.inc, data:opts.data, pad:opts.pad, noFn:opts.noFn};
}
const onoff = ['OFF', 'ON'];
const padSelVoice = (i, v) => { U.voice = v; };
const padSelCh = (i, v) => { U.ch = v % 12; };
const fmtCents = c => (c >= 0 ? '+' : '-') + String(Math.abs(Math.round(c))).padStart(4, '0') + ' cent';
const fmtPan = p => { const n = Math.round(p * 7); return n === 0 ? 'CENTER' : n < 0 ? 'LEFT ' + -n : 'RIGHT ' + n; };
const pct = x => Math.round(x * 100) + ' %';
const vctx = () => vname(U.voice), cctx = () => CH_LBL[U.ch].replace('<br>', '/').slice(0, 7);
function vItem(no, name, k, min, max, opts = {}){ return numItem(no, name, () => vget(U.voice, k), x => vset(U.voice, k, x), min, max, Object.assign({ctx:vctx, pad:padSelVoice}, opts)); }
function vBool(no, name, k){ return listItem(no, name, () => vget(U.voice, k) ? 1 : 0, x => vset(U.voice, k, x), onoff, {ctx:vctx, pad:padSelVoice}); }

const MENUS = {
  song:[actItem(1, 'SEARCH MARK', () => searchMark(true), {l2:() => 'YES: next mark'})],
  esong:[
    {no:0, name:'EDIT SONG', hidden:true},
    actItem(1, 'SEARCH MARK', () => searchMark(false), {l2:() => 'YES: next mark'}),
    actItem(2, 'SET MARK', () => { if (protectedMsg()) return; SQ.songInsertStep(S, U.song, U.songStep, {type:'mark'}); songChanged(); flash('MARK SET', 'at step ' + p3(U.songStep + 1)); }, {l2:() => 'YES: at s' + p3(U.songStep + 1)}),
    {no:3, name:'SONG NAME', l2:() => song().name, yes:() => openName(() => song().name, n => { SQ.renameSong(S, U.song, n); songChanged(); })},
    numItem(4, 'INITIAL TEMPO', () => song().tempo, x => { SQ.setSongTempo(S, U.song, x); songChanged(); }, 40, 250, {fmt:x => x.toFixed(1) + ' BPM', fine:0.1, digits:3}),
  ],
  voice:[
    {no:1, name:'SELECT VOICE', ctx:vctx, l2:() => vname(U.voice) + (S.samples[U.voice] ? ' (USER)' : ' (INT)'), inc:d => { U.voice = (U.voice + d + 24) % 24; }, data:x => { U.voice = Math.round(x * 23); }, pad:padSelVoice},
    {no:1.1, name:'WAVEFORM', ctx:vctx, pad:padSelVoice, l2:() => waveName(U.voice), inc:d => { const L = waveLib(); if (!L.length) return; const cur = S.wave && S.wave[U.voice] != null ? S.wave[U.voice] : U.voice;
      const i = L.findIndex(x => x.id === cur); setWave(U.voice, L[(Math.max(0, i) + d + L.length) % L.length].id); }},
    vItem(2, 'PITCH', 'pitch', ...RG('voice', 'pitch', [-3600, 2400]), {step:100, fine:10, fmt:fmtCents}),
    vItem(3.1, 'EG AR', 'ar', ...RG('voice', 'ar', [1, 99])), vItem(3.2, 'EG D1R', 'd1r', ...RG('voice', 'd1r', [0, 99])), vItem(3.3, 'EG D1L', 'd1l', ...RG('voice', 'd1l', [0, 60])),
    vItem(3.4, 'EG D2R', 'd2r', ...RG('voice', 'd2r', [0, 99])), vItem(3.5, 'EG RR', 'rr', ...RG('voice', 'rr', [1, 99])),
    vItem(3.6, 'EG GATE', 'gate', 0, 6500, {step:100, fine:10, fmt:x => x < 100 ? 'OFF (plays out)' : String(Math.round(x)).padStart(4, '0') + ' ms'}),
    vItem(4.1, 'BEND RATE', 'bendRate', ...RG('voice', 'bendRate', [0, 60])), vItem(4.2, 'BEND RANGE', 'bendRange', ...RG('voice', 'bendRange', [-6000, 6000]), {step:100, fine:10, fmt:fmtCents}),
    vItem(5, 'VOICE LEVEL', 'level', 0, 31), vBool(6, 'LOOP', 'loop'),
    actItem(7, 'STORE VOICE', () => { U.stored[U.voice] = Object.assign(voiceDefault(U.voice), S.voice[U.voice]); dirty(); flash('STORED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    actItem(8, 'INIT VOICE', () => { S.voice[U.voice] = voiceDefault(U.voice); post({t:'voiceAll', v:U.voice, p:S.voice[U.voice]}); dirty(); flash('INITIALIZED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    actItem(9, 'RECALL EDIT', () => { const s = U.stored[U.voice]; if (!s){ flash('NOTHING STORED', vname(U.voice)); return; } S.voice[U.voice] = Object.assign({}, s); post({t:'voiceAll', v:U.voice, p:S.voice[U.voice]}); dirty(); flash('RECALLED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    vBool(10, 'REVERSE', 'reverse'),
    listItem(12, 'BITS', () => vget(U.voice, 'bits') < 10 ? 0 : 1, x => vset(U.voice, 'bits', x ? 12 : 8), ['8 BIT', '12 BIT'], {ctx:vctx, pad:padSelVoice}),
    actItem(11, 'LOAD WAV', () => pickFile('audio/*,.wav,.aif,.aiff', loadWav), {ctx:vctx, pad:padSelVoice, l2:() => 'YES: pick a file'}),
  ],
  chain:[
    {no:1, name:'PLAY', ctx:() => 'CHAIN ' + (U.chain + 1), l2:() => S.chains[U.chain].songs.length + ' songs   YES', noFn:() => { U.chain = (U.chain + 1) % NCHAINS; }, yes:() => { U.mode = 'song'; U.chainPlay = true; start(); }},
    {no:2, name:'EDIT', ctx:() => 'C' + (U.chain + 1) + ' #' + p2(U.chainStep + 1), l2:chainL2, inc:chainInc, move:d => chainMove(d), data:x => chainSetSong(Math.round(x * 19)), digit:d => { const n = +typed(d) - 1; if (n >= 0 && n < 20) chainSetSong(n); }},
    {no:3, name:'CHAIN NAME', l2:() => S.chains[U.chain].name, yes:() => openName(() => S.chains[U.chain].name, n => { S.chains[U.chain].name = n.slice(0, 16); chainChanged(); })},
    actItem(4, 'CLEAR', () => { if (protectedMsg()) return; SQ.chainSet(S, U.chain, []); U.chainStep = 0; chainChanged(); flash('CHAIN CLEARED', 'CHAIN ' + (U.chain + 1)); }, {ctx:() => 'CHAIN ' + (U.chain + 1)}),
  ],
  util:[
    {no:1, name:'REMAIN PATTERN', l2:() => { const u = S.patterns.filter(p => p.events.length).length; return (100 - u) + ' free ' + u + ' used'; }},
    {no:2, name:'REMAIN SONG', l2:() => { const u = S.songs.filter(s => s.steps.length).length; return (20 - u) + ' free ' + u + ' used'; }},
    listItem(3, 'MEM PROTECT', () => U.protect ? 1 : 0, x => { U.protect = !!x; dirty(); }, onoff),
    {no:4, name:'CHANGE VOICE', ctx:vctx, pad:padSelVoice, l2:() => S.samples[U.voice] ? 'USER>INT? YES' : 'INTERNAL VOICE', yes:() => { if (!S.samples[U.voice]) return; S.samples[U.voice] = null; post({t:'sample', v:U.voice, s:null}); dirty(); flash('INTERNAL VOICE', vname(U.voice)); }},
    actItem(5, 'CLR ALL PTNS', () => { if (protectedMsg()) return; for (let i = 0; i < 100; i++) SQ.clearPattern(S, i); allChanged(); flash('ALL PATTERNS', 'CLEARED'); }),
    actItem(6, 'CLR ALL SONGS', () => { if (protectedMsg()) return; for (let i = 0; i < 20; i++) S.songs[i] = SQ.newSong(i); allChanged(); flash('ALL SONGS', 'CLEARED'); }),
  ],
  key:[
    {no:1, name:'VOICE ASSIGN', ctx:() => 'key' + String.fromCharCode(65 + U.pad), l2:() => ':' + (S.samples[keyOf(U.pad).voice] ? 'Usr-' : 'Int-') + vname(keyOf(U.pad).voice), pad:i => { U.pad = i; },
      inc:d => keySet(U.pad, 'voice', (keyOf(U.pad).voice + d + 24) % 24), data:x => keySet(U.pad, 'voice', Math.round(x * 23))},
    keyItem(2.1, 'KEY PITCH', 'pitch', -3600, 2400, {step:100, fine:10, fmt:fmtCents}),
    keyItem(2.2, 'KEY LEVEL', 'level', -31, 31, {fmt:sgn}), keyItem(2.3, 'KEY ATTACK', 'attack', -63, 63, {fmt:sgn}), keyItem(2.4, 'KEY DECAY', 'decay', -63, 63, {fmt:sgn}),
    {no:3, name:'MULTI VOICE', l2:() => U.multi.on ? vname(U.multi.v) + ' A-X' : 'OFF', inc:d => { const k = (U.multi.on ? U.multi.v + 1 : 0) + d; U.multi.on = k > 0 && k <= 24; if (U.multi.on) U.multi.v = (k - 1 + 24) % 24; dirty(); paintLeds(); },
      pad:(i, v) => { if (!U.multi.on){ U.multi.v = v; } }},
    {no:4, name:'MULTI STEP', l2:() => ['PITCH', 'LEVEL', 'ATTACK', 'DECAY'][U.multi.param | 0] + ' ' + sgn(multiStep()), inc:(d, sh) => { const k = ['pitch', 'level', 'attack', 'decay'][U.multi.param | 0]; U.multi[k + 'Step'] = clamp(multiStep() + d * (k === 'pitch' ? (sh ? 10 : 100) : 1), k === 'pitch' ? -1200 : -10, k === 'pitch' ? 1200 : 10); dirty(); },
      noFn:() => { U.multi.param = ((U.multi.param | 0) + 1) % 4; dirty(); }, data:x => { U.multi.param = Math.min(3, Math.floor(x * 4)); dirty(); }},
    keyItem(5.1, 'ACCENT 1', 'acc1', -31, 31, {fmt:x => sgn(x) + ' level'}), keyItem(5.2, 'ACCENT 2', 'acc2', -31, 31, {fmt:x => sgn(x) + ' level'}),
    keyItem(6, 'OUTPUT CH', 'out', 0, 11, {fmt:x => 'ch ' + p2(x + 1) + ' ' + STEP_CH[x]}),
    {no:7, name:'SAVE KEY DATA', l2:() => 'to set ' + (U.slot % 3 + 1) + '    YES', noFn:() => { U.slot = (U.slot + 1) % 3; }, yes:() => { const to = U.slot % 3; if (to !== (S.setup | 0)) S.setups[to] = JSON.parse(JSON.stringify(S.setups[S.setup | 0])); S.setup = to; keysChanged(); flash('KEY DATA SAVED', 'set ' + (to + 1)); }},
    {no:8, name:'LOAD KEY DATA', l2:() => 'set ' + (U.slot % 3 + 1) + (U.slot % 3 === (S.setup | 0) ? '*' : ' ') + '     YES', noFn:() => { U.slot = (U.slot + 1) % 3; }, yes:() => { S.setup = U.slot % 3; keysChanged(); flash('KEY DATA LOADED', 'set ' + (S.setup + 1)); }},
    {no:9, name:'COPY VOICE', ctx:vctx, pad:padSelVoice, l2:() => '>' + vname(U.copyTo || 0) + '  YES', noFn:() => { U.copyTo = ((U.copyTo || 0) + 1) % 24; },
      yes:() => { const to = U.copyTo || 0; S.voice[to] = Object.assign(voiceDefault(U.voice), S.voice[U.voice]); post({t:'voiceAll', v:to, p:S.voice[to]}); dirty(); flash('COPIED', vname(U.voice) + '>' + vname(to)); }},
    numItem(10, 'PAN', () => cget(U.ch, 'pan'), x => cset(U.ch, 'pan', x), -1, 1, {step:1 / 7, fmt:fmtPan, ctx:cctx, pad:padSelCh}),
    numItem(11, 'REV SEND', () => cget(U.ch, 'sendRev'), x => cset(U.ch, 'sendRev', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
    numItem(12, 'DLY SEND', () => cget(U.ch, 'sendDly'), x => cset(U.ch, 'sendDly', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
    numItem(13, 'PAD VELOCITY', () => U.padVel, x => { U.padVel = x; dirty(); }, 0, 127, {fmt:x => x ? 'fixed ' + x : 'TOUCH (pad pos)'}),
  ],
  cart:[
    ...[0, 1, 2, 3].map(i => ({no:1 + i, name:'SAVE SLOT ' + (i + 1), l2:() => slotInfo(i, 'SAVE'), yes:() => slotSave(i)})),
    ...[0, 1, 2, 3].map(i => ({no:11 + i, name:'LOAD SLOT ' + (i + 1), l2:() => slotInfo(i, 'LOAD'), yes:() => slotLoad(i)})),
    actItem(21, 'SAVE FILE', () => saveCartFile(), {l2:() => 'YES: .o5cart'}),
    actItem(22, 'LOAD FILE', () => pickFile('.o5cart,.json,application/json,application/gzip', loadCartFile), {l2:() => 'YES: pick a file'}),
    actItem(23, 'LOAD WAV', () => pickFile('audio/*,.wav,.aif,.aiff', loadWav), {ctx:vctx, pad:padSelVoice, l2:() => 'YES: into voice'}),
    actItem(25, 'LOAD KIT', () => pickFile('.zip,.bwpreset,.wav,.wave,.aif,.aiff,audio/*', null, loadKitFiles, U.kitDir), {l2:() => (U.kitDir ? 'FOLDER' : 'FILES ') + ' YES NO:<>',
      noFn:() => { U.kitDir = !U.kitDir; }}),
    {no:24, name:'VOICE BANK', l2:() => ['LOAD .bin', 'SAVE .bin'][U.bankDir | 0] + ' YES', noFn:() => { const d = 1; U.bankDir = (U.bankDir | 0) ? 0 : 1; }, yes:() => voiceBank(U.bankDir | 0)},
    {no:30, name:'FORMAT SLOT', l2:() => 'slot ' + (U.slot % 4 + 1) + ' NO/YES', noFn:() => { const d = 1; U.slot = (U.slot + d + 4) % 4; }, yes:async () => { await IDB.del('cart' + U.slot % 4); slotCache[U.slot % 4] = null; flash('SLOT ' + (U.slot % 4 + 1), 'FORMATTED'); }},
  ],
  midi:[
    listItem(1, 'CH MESSAGE', () => S.midi.channelMsg !== false ? 1 : 0, x => { S.midi.channelMsg = !!x; dirty(); }, onoff),
    numItem(2, 'RECEIVE CH', () => S.midi.rxCh || 10, x => { S.midi.rxCh = x; dirty(); }, 1, 16, {fmt:x => 'ch ' + p2(x)}),
    numItem(3, 'TRANSMIT CH', () => S.midi.txCh || 10, x => { S.midi.txCh = x; dirty(); }, 1, 16, {fmt:x => 'ch ' + p2(x)}),
    {no:4.1, name:'NOTE VOICE', ctx:vctx, pad:padSelVoice, l2:() => noteName(S.noteMap[U.voice]) + ' ' + p3(S.noteMap[U.voice]) + ' (learn)',
      inc:(d, sh) => { S.noteMap[U.voice] = clamp(S.noteMap[U.voice] + d * (sh ? 12 : 1), 0, 127); dirty(); }, data:x => { S.noteMap[U.voice] = Math.round(x * 127); dirty(); }},
    {no:4.2, name:'NOTE PITCH', l2:() => S.midi.tunable ? vname(S.midi.tunableVoice | 0) + ' C1-C6' : 'OFF (VOICE mode)', inc:d => { S.midi.tunable = d > 0; dirty(); },
      pad:(i, v) => { S.midi.tunableVoice = v; dirty(); }},
    {no:5, name:'INIT NOTE', l2:() => (U.notePreset ? 'GM' : 'RX5') + ' table   YES', noFn:() => { U.notePreset = U.notePreset ? 0 : 1; },
      yes:() => { const src = U.notePreset ? NOTE_PRESETS.gm : (NOTE_PRESETS.rx5 || NOTE_PRESETS.gm); S.noteMap = src.slice(); dirty(); flash('NOTES INITIALIZED', U.notePreset ? 'GM' : 'RX5'); }},
    numItem(6, 'EG VELOCITY', () => S.midi.egVel | 0, x => { S.midi.egVel = x; dirty(); }, 0, 4, {fmt:x => x ? p2(x) : 'OFF'}),
    numItem(7, 'GATE TIME', () => S.midi.gateMs || 10, x => { S.midi.gateMs = x; dirty(); }, 10, 1000, {step:10, fmt:x => (x / 1000).toFixed(2) + ' s'}),
    numItem(8, 'DEVICE NUMBER', () => S.midi.deviceNo | 0, x => { S.midi.deviceNo = x; dirty(); }, 0, 16, {fmt:x => x ? p2(x) : 'OFF'}),
    {no:9, name:'RECEIVE BULK', l2:() => U.bulkWait ? 'waiting...   NO' : 'YES wait NO req', yes:() => { U.bulkWait = true; if (RXS && RXS.BulkReceiver) M.rx = new RXS.BulkReceiver(); flash('RECEIVE BULK', 'waiting for data', 1800); },
      noFn:() => { if (U.bulkWait){ U.bulkWait = false; return; } if (RXS && RXS.dumpRequest && midiOutPort()){ U.bulkWait = true; midiSend(RXS.dumpRequest(S.midi.deviceNo || 1)); flash('DUMP REQUEST', 'sent, waiting'); } else flash('NO MIDI OUT', 'MIDI 14: port'); }},
    {no:10, name:'TRANSMIT BULK', l2:() => bulkName() + ' YES', noFn:() => { U.bulk = (U.bulk + 1) % BULKS.length; }, data:x => { U.bulk = Math.round(x * (BULKS.length - 1)); }, yes:() => transmitBulk()},
    listItem(11, 'CLOCK OUT', () => S.midi.clockOut ? 1 : 0, x => { S.midi.clockOut = !!x; post({t:'midiOut', clock:!!S.midi.clockOut, notes:!!S.midi.notesOut}); dirty(); }, onoff),
    listItem(12, 'NOTES OUT', () => S.midi.notesOut ? 1 : 0, x => { S.midi.notesOut = !!x; post({t:'midiOut', clock:!!S.midi.clockOut, notes:!!S.midi.notesOut}); dirty(); }, onoff),
    {no:13, name:'INPUT PORT', l2:() => portLabel(0), inc:d => portInc(0, d)},
    {no:14, name:'OUTPUT PORT', l2:() => portLabel(1), inc:d => portInc(1, d)},
    {no:15, name:'SYX FILE', l2:() => (U.syx ? 'SAVE ' + bulkName() : 'LOAD .syx') + ' YES', noFn:() => { if (!U.syx) { U.syx = 1; U.bulk = 0; } else if (++U.bulk >= BULKS.length){ U.syx = 0; U.bulk = 0; } }, yes:() => U.syx ? saveSyx() : pickFile('.syx,application/octet-stream', loadSyx)},
  ],
  job:[
    {no:1, name:'IMPORT MIDI', l2:() => 'to ' + p2(impStart()) + ' SONG ' + songL(U.song), noFn:() => { const d = 1; U.impAt = clamp(impStart() + d, 0, 99); }, yes:() => pickFile('.mid,.midi,.smf,audio/midi,audio/x-midi', importMidi)},
    {no:2, name:'EXPORT MIDI', l2:() => expName() + ' YES', noFn:() => { const d = 1; U.exp = U.exp ? 0 : 1; }, yes:() => exportMidi()},
    {no:3, name:'EXPORT WAV', l2:() => expName() + ' YES', noFn:() => { const d = 1; U.exp = U.exp ? 0 : 1; }, yes:() => exportWav()},
    {no:4, name:'RANDOMIZE', l2:() => ['ALL', 'PATTERN ' + p2(U.ptn), 'VOICES', 'FX'][U.rnd] + ' YES', noFn:() => { const d = 1; U.rnd = (U.rnd + d + 4) % 4; }, yes:() => doRandom()},
    actItem(5, 'INIT ALL', () => { if (protectedMsg()) return; stop(); S = SQ.newState(); U.ptn = 0; U.song = 0; allChanged(); flash('MEMORY', 'INITIALIZED'); }),
  ],
  eptn:[
    numItem(1, 'BEATS', () => pat().beats, x => { if (protectedMsg()) return; SQ.setBeats(S, U.ptn, x, pat().unit); patChanged(); }, 1, 32, {digits:2, ctx:() => 'P' + p2(U.ptn)}),
    listItem(2, 'UNIT', () => [4, 8, 16].indexOf(pat().unit), x => { if (protectedMsg()) return; SQ.setBeats(S, U.ptn, pat().beats, [4, 8, 16][x]); patChanged(); }, ['1/4', '1/8', '1/16'], {ctx:() => 'P' + p2(U.ptn)}),
    numItem(3, 'BARS', () => pat().bars, x => { if (protectedMsg()) return; SQ.setBars(S, U.ptn, x); patChanged(); }, 1, 99, {digits:2, ctx:() => 'P' + p2(U.ptn)}),
    {no:4, name:'NAME', ctx:() => 'P' + p2(U.ptn), l2:() => pat().name, yes:() => openName(() => pat().name, n => { SQ.renamePattern(S, U.ptn, n); patChanged(); })},
    numItem(5, 'VOLUME', () => pat().volume[U.ch], x => { pat().volume[U.ch] = x; patChanged(); }, 0, 31, {ctx:cctx, pad:padSelCh}),
  ],
  rev:[
    listItem(1, 'PROGRAM', () => fget('rev', 'program'), x => setRevProgram(x), REV_PROGRAMS),
    numItem(2, 'REV TIME', () => fget('rev', 'time'), x => fset('rev', 'time', x), 0.3, 30, {step:0.1, fmt:x => x.toFixed(1) + ' s', curve:2}),
    numItem(3, 'HIGH RATIO', () => fget('rev', 'hiRatio'), x => fset('rev', 'hiRatio', x), 0.1, 1, {step:0.1, fmt:x => x.toFixed(1)}),
    numItem(4, 'DELAY', () => fget('rev', 'delay'), x => fset('rev', 'delay', x), 0, 200, {fmt:x => x.toFixed(0) + ' ms'}),
    numItem(5, 'DIFFUSION', () => fget('rev', 'diffusion'), x => fset('rev', 'diffusion', x), 0, 10),
    numItem(6, 'DENSITY', () => fget('rev', 'density'), x => fset('rev', 'density', x), 0, 100, {fmt:x => x + ' %'}),
    numItem(7, 'ER LEVEL', () => fget('rev', 'erLevel'), x => fset('rev', 'erLevel', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(8, 'REV LEVEL', () => fget('rev', 'revLevel'), x => fset('rev', 'revLevel', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(9, 'HPF', () => fget('rev', 'hpf'), x => fset('rev', 'hpf', x), 20, 1000, {step:10, fmt:x => x < 25 ? 'THRU' : x + ' Hz', curve:2}),
    numItem(10, 'LPF', () => fget('rev', 'lpf'), x => fset('rev', 'lpf', x), 1000, 16000, {step:500, fmt:x => x >= 16000 ? 'THRU' : (x / 1000).toFixed(1) + ' kHz', curve:2}),
    numItem(11, 'GATE TIME', () => fget('rev', 'gateTime'), x => fset('rev', 'gateTime', x), 10, 500, {step:10, fmt:x => x + ' ms'}),
    numItem(12, 'RETURN LEVEL', () => fget('rev', 'level'), x => fset('rev', 'level', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(13, 'REV SEND', () => cget(U.ch, 'sendRev'), x => cset(U.ch, 'sendRev', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
  ],
  dly:[
    listItem(0, 'DELAY', () => fget('dly', 'on') ? 1 : 0, x => fset('dly', 'on', x), onoff),
    numItem(1, 'TIME L', () => fget('dly', 'timeL'), x => fset('dly', 'timeL', x), 1, 2000, {step:1, fmt:x => x + ' ms', curve:2}),
    numItem(2, 'TIME R', () => fget('dly', 'timeR'), x => fset('dly', 'timeR', x), 1, 2000, {step:1, fmt:x => x + ' ms', curve:2}),
    listItem(3, 'TEMPO SYNC', () => fget('dly', 'sync'), x => fset('dly', 'sync', x), DLY_SYNC.map(s => s === 'off' ? 'OFF' : s)),
    numItem(4, 'FEEDBACK', () => fget('dly', 'feedback'), x => fset('dly', 'feedback', x), 0, 120, {fmt:x => x + ' %'}),
    listItem(5, 'PING PONG', () => fget('dly', 'pingpong') ? 1 : 0, x => fset('dly', 'pingpong', x), onoff),
    numItem(6, 'HI CUT', () => fget('dly', 'hiCut'), x => fset('dly', 'hiCut', x), 1000, 20000, {step:500, fmt:x => (x / 1000).toFixed(1) + ' kHz', curve:2}),
    numItem(7, 'LO CUT', () => fget('dly', 'loCut'), x => fset('dly', 'loCut', x), 20, 1000, {step:10, fmt:x => x + ' Hz', curve:2}),
    numItem(8, 'MOD RATE', () => fget('dly', 'modRate'), x => fset('dly', 'modRate', x), 0.05, 10, {step:0.05, fmt:x => x.toFixed(2) + ' Hz'}),
    numItem(9, 'MOD DEPTH', () => fget('dly', 'modDepth'), x => fset('dly', 'modDepth', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(10, 'DUCK THRESH', () => fget('dly', 'duckThresh'), x => fset('dly', 'duckThresh', x), -60, 0, {fmt:x => x <= -60 ? 'OFF' : x + ' dB'}),
    numItem(11, 'DUCK RELEASE', () => fget('dly', 'duckRelease'), x => fset('dly', 'duckRelease', x), 10, 2000, {step:10, fmt:x => x + ' ms', curve:2}),
    numItem(11.1, 'DUCK DEPTH', () => fget('dly', 'duckDepth'), x => fset('dly', 'duckDepth', x), 0, 40, {fmt:x => x + ' dB'}),
    numItem(12, 'SPREAD', () => fget('dly', 'spread'), x => fset('dly', 'spread', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(13, 'MIX', () => fget('dly', 'mix'), x => fset('dly', 'mix', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(14, 'DLY SEND', () => cget(U.ch, 'sendDly'), x => cset(U.ch, 'sendDly', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
  ],
};
function curItem(){ const list = MENUS[U.menu]; if (!list) return null; const i = clamp(U.item[U.menu] | 0, 0, list.length - 1); return list[i]; }
function openMenu(m, no){
  U.page = 'menu'; U.menu = m; digits = '';
  const list = MENUS[m];
  if (no != null){ const i = list.findIndex(it => it.no === no); if (i >= 0) U.item[m] = i; }
  else if (U.item[m] == null) U.item[m] = list[0].hidden ? 0 : 0;
  paintAll();
}
function itemNo(it){ if (it.hidden) return ''; const n = it.no, a = Math.floor(n), b = Math.round((n - a) * 10); return p2(a) + (b ? '-' + b : ''); }
const MENU_TITLE = {rev:'REV', dly:'DLY'};
const menuPage = {
  lcd(){
    const it = curItem(); if (!it) return ['', ''];
    if (U.menu === 'esong' && it.no === 0) return songEditLcd();
    const head = (MENU_TITLE[U.menu] ? MENU_TITLE[U.menu] + ' ' : '') + itemNo(it) + ' ' + it.name;
    const c = it.ctx ? it.ctx() : '';
    return [c && head.length + c.length < 16 ? LR(head, c) : head, it.l2 ? it.l2() : ''];
  },
  inc(d, sh){ const it = curItem(); if (U.menu === 'esong' && it.no === 0) return songEditInc(d, sh); if (it.inc) it.inc(d, sh); },
  digit(d){
    const it = curItem();
    if (U.menu === 'esong' && it.no === 0) return songEditDigit(d);
    if (it.digit) return it.digit(d);
    // two digits pick the item; sub items (03-1 ..) follow with a third digit
    const s = typed(d, 3), list = MENUS[U.menu];
    const n2 = +s.slice(-2), n3 = s.length === 3 ? +s.slice(0, 2) + (+s[2]) / 10 : -1;
    let i = n3 > 0 ? list.findIndex(x => Math.abs(x.no - n3) < 1e-6) : -1;
    if (i < 0 && s.length >= 2) i = list.findIndex(x => Math.floor(x.no) === n2 && !x.hidden);
    if (i < 0 && s.length === 1) return;
    if (i >= 0) U.item[U.menu] = i;
  },
  // the -1/NO and +1/YES keys: actions run on YES (NO cycles their choice), values step
  pm(d, sh){
    const it = curItem(); if (U.menu === 'esong' && it.no === 0) return songEditInc(d, sh);
    if (it.yes){ if (d > 0) it.yes(); else if (it.noFn) it.noFn(); else flash('CANCELLED', ''); }
    else if (it.inc) it.inc(d, sh); else if (it.noFn && d < 0) it.noFn();
  },
  move(d){ const it = curItem(); if (U.menu === 'esong' && it.no === 0) return songEditMove(d); if (it.move) return it.move(d); const list = MENUS[U.menu]; U.item[U.menu] = (clamp(U.item[U.menu] | 0, 0, list.length - 1) + d + list.length) % list.length; digits = ''; },
  pad(i, v){ const it = curItem(); if (U.menu === 'esong' && it.no === 0) return songEditPad(i, v); if (it.pad) it.pad(i, v); },
  data(x){ const it = curItem(); if (U.menu === 'esong' && it.no === 0) return songEditData(x); if (it.data) it.data(x); },
};

/* ---------- song editor (EDIT SONG) ---------- */
const STEP_CH = ['BD', 'SD', 'RIM', 'TOM1', 'TOM2', 'TOM3', 'TOM4', 'HH', 'RIDE', 'CRSH', 'TB/C', 'SH/C'];
function stepDesc(st){
  if (!st) return '-- END --';
  switch (st.type){
    case 'pat': return 'PTN ' + p2(st.n) + ' ' + S.patterns[st.n].name.slice(0, 8);
    case 'rep': return 'REPEAT s' + p3(st.from + 1) + ' x' + st.count;
    case 'tempo': return 'TEMPO ' + st.bpm.toFixed(1);
    case 'vol': return 'VOL ' + STEP_CH[st.ch] + ' ' + st.level;
    case 'mark': return '=== MARK ===';
  }
  return '?';
}
function songEditLcd(){
  const steps = song().steps; U.songStep = clamp(U.songStep, 0, steps.length);
  const st = steps[U.songStep];
  return ['SONG ' + songL(U.song) + ' s' + p3(U.songStep + 1) + '/' + p3(steps.length), st ? stepDesc(st) : '-- END -- +P' + p2(digits ? +digits : U.ptn)];
}
function songEditInc(d, sh){
  if (protectedMsg()) return;
  const steps = song().steps, st = steps[U.songStep];
  if (!st){ if (d > 0){ const n = digits ? clamp(+digits, 0, 99) : U.ptn; digits = ''; if (steps.length >= 999) return flash('SONG FULL', '999 parts'); SQ.songInsertStep(S, U.song, steps.length, {type:'pat', n}); U.songStep = steps.length; songChanged(); } return; }
  const x = Object.assign({}, st);
  if (x.type === 'pat') x.n = (x.n + d + 100) % 100;
  else if (x.type === 'rep') x.count = clamp(x.count + d, 1, 99);
  else if (x.type === 'tempo') x.bpm = clamp(+(x.bpm + d * (sh ? 0.1 : 1)).toFixed(1), 40, 250);
  else if (x.type === 'vol') x.level = clamp(x.level + d, 0, 31);
  SQ.songSetStep(S, U.song, U.songStep, x); songChanged();
}
function songEditData(x){
  const st = song().steps[U.songStep]; if (!st || U.protect) return;
  const y = Object.assign({}, st);
  if (y.type === 'pat') y.n = Math.round(x * 99); else if (y.type === 'rep') y.count = 1 + Math.round(x * 98);
  else if (y.type === 'tempo') y.bpm = Math.round(40 + 210 * x); else if (y.type === 'vol') y.level = Math.round(x * 31); else return;
  SQ.songSetStep(S, U.song, U.songStep, y); songChanged();
}
function songEditDigit(d){
  const st = song().steps[U.songStep], s = typed(d, st && st.type === 'rep' ? 3 : 2);
  if (!st || U.protect) return;
  const y = Object.assign({}, st);
  if (y.type === 'pat') y.n = clamp(+s, 0, 99); else if (y.type === 'rep') y.from = clamp(+s - 1, 0, U.songStep); else if (y.type === 'vol') y.level = clamp(+s, 0, 31); else return;
  SQ.songSetStep(S, U.song, U.songStep, y); songChanged();
}
function songEditMove(d){ U.songStep = clamp(U.songStep + d, 0, song().steps.length); digits = ''; }
function songEditPad(i, v){ const st = song().steps[U.songStep]; if (st && st.type === 'vol'){ SQ.songSetStep(S, U.song, U.songStep, Object.assign({}, st, {ch:v % 12})); songChanged(); } }
function songEditYes(){ songEditInc(1); }
function songEditNo(){ songEditInc(-1); }
function inSongEdit(){ return U.page === 'menu' && U.menu === 'esong' && curItem().no === 0; }
function inChainEdit(){ return U.page === 'menu' && U.menu === 'chain' && curItem().no === 2; }
function songInsert(type){
  if (inChainEdit()){ if (type === 'pat'){ SQ.chainInsert(S, U.chain, U.chainStep, U.song); chainChanged(); } return; }
  if (!inSongEdit()){ U.mode = 'song'; openMenu('esong', 0); return; }
  if (protectedMsg()) return;
  const steps = song().steps, i = U.songStep;
  if (steps.length >= 999) return flash('SONG FULL', '999 parts');
  let st;
  if (type === 'pat') st = {type:'pat', n:U.ptn};
  else if (type === 'rep'){ let from = 0; for (let k = i - 1; k >= 0; k--) if (steps[k].type === 'mark'){ from = k + 1; break; } st = {type:'rep', from, count:2}; }
  else if (type === 'tempo') st = {type:'tempo', bpm:S.tempo};
  else st = {type:'vol', ch:U.ch, level:31};
  SQ.songInsertStep(S, U.song, i, st); songChanged();
  flash(type === 'pat' ? 'INSERT' : type === 'rep' ? 'REPEAT' : type === 'tempo' ? 'TEMPO CHANGE' : 'VOLUME CHANGE', 'at s' + p3(i + 1), 900);
}
function songDelete(){
  if (inChainEdit()) return chainDel();
  if (!inSongEdit()){ U.mode = 'song'; openMenu('esong', 0); return; }
  if (protectedMsg()) return;
  if (U.songStep < song().steps.length){ SQ.songDeleteStep(S, U.song, U.songStep); songChanged(); flash('DELETE', 'at s' + p3(U.songStep + 1), 900); }
}
function songTickAt(sn, stepIdx){
  const c = new SQ.SongCursor(S, sn); let abs = 0, n;
  while ((n = c.next()) !== null){ if (c.at >= stepIdx) return abs; abs += SQ.patLen(S.patterns[n]); }
  return null;
}
function searchMark(play){
  const steps = song().steps, from = play ? (pos && pos.song === U.song && pos.step >= 0 ? pos.step : -1) : U.songStep;
  let m = steps.findIndex((s, k) => k > from && s.type === 'mark');
  if (m < 0) m = steps.findIndex(s => s.type === 'mark');
  if (m < 0) return flash('NO MARK', 'SONG ' + songL(U.song));
  if (!play){ U.songStep = m; openMenu('esong', 0); return; }
  const t = songTickAt(U.song, m + 1);
  if (t == null) return flash('MARK AT END', '');
  audio().then(() => { post({t:'transport', cmd:'start', mode:'song', n:U.song}); post({t:'locate', tick:t}); if (!running()) post({t:'transport', cmd:'stop'}); });
  flash('MARK s' + p3(m + 1), running() ? 'playing from it' : 'CONTINUE plays');
}

/* ---------- chain edit ---------- */
function chainL2(){ const c = S.chains[U.chain].songs; U.chainStep = clamp(U.chainStep, 0, c.length); const n = c[U.chainStep]; return n == null ? '-- END -- +' + songL(U.song) : 'SONG ' + songL(n) + ' ' + S.songs[n].name.slice(0, 8); }
function chainInc(d){
  if (protectedMsg()) return;
  const c = S.chains[U.chain].songs;
  if (U.chainStep >= c.length){ if (d > 0){ if (c.length >= (SQ.CHAIN_MAX || 99)) return flash('CHAIN FULL', ''); SQ.chainInsert(S, U.chain, c.length, U.song); U.chainStep = c.length; chainChanged(); } return; }
  c[U.chainStep] = (c[U.chainStep] + d + 20) % 20; chainChanged();
}
function chainSetSong(n){ const c = S.chains[U.chain].songs; if (U.chainStep < c.length){ c[U.chainStep] = n; chainChanged(); } else U.song = n; }
function chainMove(d){ U.chainStep = clamp(U.chainStep + d, 0, S.chains[U.chain].songs.length); }
function chainDel(){ if (protectedMsg()) return; if (U.chainStep < S.chains[U.chain].songs.length){ SQ.chainDelete(S, U.chain, U.chainStep); chainChanged(); } }

/* ---------- the other pages ---------- */
function selectPattern(n){
  n = (n + 100) % 100;
  if (running() && pos.mode === 'pattern'){ U.cue = n; post({t:'cue', n}); }
  else { U.ptn = n; U.cue = -1; post({t:'cue', n}); }
}
function selectSong(n){ U.song = (n + 20) % 20; U.tempoTouched = false; U.chainPlay = false; if (S.midi.clockOut) midiSend([0xF3, U.song]); }
function setTempo(bpm){
  bpm = clamp(Math.round(bpm * 10) / 10, 40, 250); S.tempo = bpm; post({t:'set', k:'tempo', v:bpm});
  if (U.mode === 'song'){ U.tempoTouched = true; post({t:'tempo', bpm}); }
  dirty(); setSlider('TEMPO', (bpm - 40) / 210);
}
const curTempo = () => running() && pos.bpm ? pos.bpm : U.mode === 'song' && !U.tempoTouched ? song().tempo : S.tempo;
function qSet(i){ S.quantize = clamp(i, 0, (SQ.QUANTIZE || QUANT).length - 1); post({t:'set', k:'quantize', v:S.quantize}); dirty(); }
function qDigit(d){ const m = SQ.QUANTIZE_KEYPAD || KEY_Q; qSet(m[d]); const g = QT(S.quantize); U.stepPos = Math.floor(U.stepPos / g) * g; }
const SYNCS = ['int', 'midi', 'tape', 'ext'];
function setSync(s){ S.sync = s; post({t:'set', k:'sync', v:s}); post({t:'ext', on:s === 'midi' || s === 'ext'}); dirty(); paintLeds(); }
function playLine(){
  const p = running() && pos.mode === 'pattern' ? S.patterns[pos.pattern] : pat();
  const bar = running() ? pos.bar : 1;
  return ' ' + p2(p.beats) + ' /' + String(p.unit).padStart(2) + ' :bar' + p2(bar);
}
let nameState = null;
function openName(get, set){ nameState = {back:{page:U.page, menu:U.menu}, buf:(get() || '').padEnd(16).slice(0, 16).split(''), cur:0, set}; U.page = 'name'; paintAll(); }
function nameDone(){ const n = nameState.buf.join('').trimEnd(); nameState.set(n); U.page = nameState.back.page; U.menu = nameState.back.menu; flash('NAME', n || '(blank)', 900); }
function nameType(ch){ const s = nameState; s.buf[s.cur] = ch; s.cur = Math.min(15, s.cur + 1); }

const PAGES = {
  ptn:{
    lcd:() => {
      const now = running() && pos.mode === 'pattern' ? pos.pattern : U.ptn, cue = U.cue >= 0 && running() ? U.cue : -1;
      const nx = digits.length === 1 ? digits + '*' : cue >= 0 ? p2(cue) : '**';
      return ['PLAY PTN' + p2(now) + ':nxt' + nx, playLine()];
    },
    digit:d => { const s = typed(d); if (s.length === 2){ selectPattern(+s); digits = ''; } },
    pm:d => selectPattern((U.cue >= 0 && running() ? U.cue : U.ptn) + d),
    data:x => selectPattern(Math.round(x * 99)),
  },
  songplay:{
    lcd:() => {
      const sn = running() && pos.song >= 0 ? pos.song : U.song, s = S.songs[sn];
      const l1 = (U.chainPlay ? 'CHAIN' + (U.chain + 1) + ' SONG ' : 'PLAY SONG ') + songL(sn) + ':' + s.name;
      if (running() && pos.mode !== 'pattern') return [l1, 's' + p3(pos.step + 1) + ' P' + p2(pos.pattern) + ' b' + p2(pos.bar) + ' ' + Math.round(pos.bpm)];
      return [l1, digits.length === 1 ? 'song ' + digits + '*' : p3(s.steps.length) + ' steps ' + curTempo().toFixed(1)];
    },
    digit:d => { const s = typed(d); if (s.length === 2){ const n = +s - 1; if (n >= 0 && n < 20) selectSong(n); digits = ''; } },
    pm:d => selectSong(U.song + d),
    data:x => selectSong(Math.round(x * 19)),
  },
  rtw:{
    lcd:() => ['RTW PTN' + p2(U.ptn) + ' Q' + QN(S.quantize), running() ? playLine() + ' ' + pos.beat : U.protect ? 'MEMORY PROTECTED' : ' START to record'],
    digit:qDigit, pm:d => qSet(S.quantize + d), data:x => qSet(Math.round(x * 10)),
  },
  step:{
    lcd:() => {
      const p = pat(), g = QT(S.quantize), len = SQ.patLen(p), bt = SQ.barTicks(p);
      U.stepPos = clamp(U.stepPos, 0, len - 1);
      const idx = Math.floor(U.stepPos / g), w0 = Math.floor(idx / 16) * 16, sv = U.stepVoice;
      const bar = Math.floor(U.stepPos / bt) + 1, sib = Math.floor((U.stepPos % bt) / g) + 1;
      let l2 = '';
      for (let k = 0; k < 16; k++){
        const t = (w0 + k) * g;
        if (t >= len){ l2 += ' '; continue; }
        const e = p.events.find(x => x.t === t && x.v === sv);
        l2 += e ? (e.acc === 2 ? '#' : e.acc === 1 ? 'O' : 'o') : (t % SQ.ticksPerBeat(p) === 0 ? ':' : '.');
      }
      return [vname(sv).slice(0, 6).padEnd(6) + p2(bar) + ':' + p2(sib) + ' ' + QN(S.quantize), l2, idx - w0];
    },
    pm:d => { const p = pat(), g = QT(S.quantize), len = SQ.patLen(p); U.stepPos = ((Math.floor(U.stepPos / g) + d) * g % len + len) % len; },
    move:d => PAGES.step.pm(d),
    digit:qDigit,
    data:x => { const p = pat(), g = QT(S.quantize), n = Math.ceil(SQ.patLen(p) / g); U.stepPos = Math.min(n - 1, Math.floor(x * n)) * g; },
    pad:(i, v, vel, acc) => {
      if (protectedMsg()) return;
      U.stepVoice = v;
      const p = pat(), had = p.events.some(e => e.t === U.stepPos && e.v === v);
      if (had && !acc) SQ.stepDelete(S, U.ptn, U.stepPos, v); else SQ.stepInsert(S, U.ptn, U.stepPos, v, U.padVel || vel, acc);
      patChanged();
    },
    silentToggle:true,
  },
  quant:{lcd:() => ['QUANTIZE', '      ' + QN(S.quantize)], digit:qDigit, pm:d => qSet(S.quantize + d), data:x => qSet(Math.round(x * 10))},
  swing:{lcd:() => ['SWING   PTN' + p2(U.ptn), '      ' + pat().swing + ' %'], pm:d => { SQ.setSwing(S, U.ptn, pat().swing + d); patChanged(); },
    digit:d => { const s = typed(d); if (s.length === 2){ SQ.setSwing(S, U.ptn, +s); patChanged(); } }, data:x => { SQ.setSwing(S, U.ptn, Math.round(50 + 25 * x)); patChanged(); }},
  click:{lcd:() => ['CLICK', (S.click.on ? 'ON ' : 'OFF') + '  level ' + Math.round(S.click.level * 100) + '%'], pm:d => { S.click.on = d > 0; post({t:'set', k:'click', v:S.click}); dirty(); }},
  tempo:{lcd:() => ['TEMPO' + (U.mode === 'song' ? '  SONG ' + songL(U.song) : ''), '    ' + curTempo().toFixed(1) + ' BPM'],
    pm:(d, sh) => setTempo(curTempo() + d * (sh ? 0.1 : 1)), digit:d => { const s = typed(d, 3); if (s.length === 3 || +s >= 40) setTempo(+s); }, data:x => setTempo(40 + 210 * x)},
  clear:{lcd:() => [U.mode === 'song' ? 'CLEAR SONG ' + songL(U.song) : 'CLEAR PTN' + p2(U.ptn), ' sure? NO / YES'],
    pm:d => {
      if (d < 0){ backToPlay(); return; }
      if (protectedMsg()) return;
      if (U.mode === 'song'){ S.songs[U.song] = SQ.newSong(U.song); songChanged(); flash('SONG ' + songL(U.song), 'CLEARED'); }
      else { SQ.clearPattern(S, U.ptn); patChanged(); flash('PTN' + p2(U.ptn), 'CLEARED'); }
      backToPlay();
    }},
  copy:{lcd:() => U.mode === 'song' ? ['COPY SONG ' + songL(U.song) + ' TO', '   SONG ' + songL(U.copyDst | 0) + '  YES'] : ['COPY PTN' + p2(U.ptn) + ' TO', '   PTN' + p2(U.copyDst | 0) + '  YES'],
    digit:d => { const s = typed(d); if (s.length === 2) U.copyDst = U.mode === 'song' ? clamp(+s - 1, 0, 19) : +s; },
    data:x => { U.copyDst = Math.round(x * (U.mode === 'song' ? 19 : 99)); },
    pm:d => {
      if (d < 0){ U.copyDst = ((U.copyDst | 0) + 1) % (U.mode === 'song' ? 20 : 100); return; }
      if (protectedMsg()) return;
      const dst = U.copyDst | 0;
      if (U.mode === 'song'){ S.songs[dst] = JSON.parse(JSON.stringify(song())); songChanged(dst); flash('COPIED', 'SONG ' + songL(U.song) + '>' + songL(dst)); }
      else { SQ.copyPattern(S, U.ptn, dst); patChanged(dst); flash('COPIED', 'PTN' + p2(U.ptn) + '>' + p2(dst)); }
    }},
  sync:{lcd:() => ['SYNC MODE', '  ' + ['INT', 'MIDI', 'TAPE (= INT)', 'EXT (MIDI CLK)'][SYNCS.indexOf(S.sync)]], pm:d => setSync(SYNCS[(SYNCS.indexOf(S.sync) + d + 4) % 4]), data:x => setSync(SYNCS[Math.round(x * 3)])},
  reverse:{lcd:() => ['REVERSE  ' + vname(U.voice), vget(U.voice, 'reverse') ? '  ON  (pad: flip)' : '  OFF (pad: flip)'],
    pm:d => vset(U.voice, 'reverse', d > 0 ? 1 : 0), pad:(i, v) => { vset(v, 'reverse', vget(v, 'reverse') ? 0 : 1); }},
  name:{lcd:() => ['NAME  YES = done', nameState.buf.join(''), nameState.cur],
    pad:i => nameType(String.fromCharCode(65 + i)), digit:d => nameType(String(d)), move:d => { nameState.cur = clamp(nameState.cur + d, 0, 15); },
    pm:d => { if (d > 0) nameDone(); else { const s = nameState; s.cur = Math.max(0, s.cur - 1); s.buf[s.cur] = ' '; } }, padOnly:true},
};
function page(){ return U.page === 'menu' ? menuPage : PAGES[U.page] || PAGES.ptn; }
function backToPlay(){ U.page = U.mode === 'song' ? 'songplay' : 'ptn'; U.menu = null; }
function doRandom(){
  if (protectedMsg()) return;
  const seed = (Date.now() ^ Math.floor(Math.random() * 4294967295)) >>> 0, scope = ['all', 'pattern', 'voices', 'fx'][U.rnd];
  const r = SQ.randomize(S, seed, scope, {pattern:U.ptn, start:Math.min(U.ptn, 96), song:U.song});
  if (scope === 'all'){ U.ptn = Math.min(U.ptn, 96); }
  allChanged(); flash('RANDOM ' + scope.toUpperCase(), (r && r.genre ? r.genre.toUpperCase() : '') + (scope === 'all' ? ' ' + Math.round(S.tempo) + 'BPM' : ''), 1800);
}

/* ================= files ================= */
// dir: pick a folder (every file in it and its subfolders; webkitdirectory, where the browser has it)
function pickFile(accept, fn, multi, dir){ const i = $('#fileIn'); i.accept = dir ? '' : accept; i.multiple = !!multi; i.webkitdirectory = !!dir; i.onchange = () => { const all = [...i.files], f = all[0]; i.value = ''; if (multi && all.length) return multi(all).catch(e => { console.error(e); flash('LOAD ERROR', String(e.message || e).slice(0, 16)); }); if (f) fn(f).catch(e => { console.error(e); flash('LOAD ERROR', String(e.message || e).slice(0, 16)); toast(String(e.message || e), 5000); }); }; i.click(); }
const stamp = () => { const d = new Date(); return d.getFullYear() + p2(d.getMonth() + 1) + p2(d.getDate()) + '-' + p2(d.getHours()) + p2(d.getMinutes()); };
async function saveFile(bytes, name, mime){
  const blob = bytes instanceof Blob ? bytes : new Blob([bytes], {type:mime});
  if (window.AndroidFive){
    const b64 = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
    const where = AndroidFive.save(b64, name, mime);
    if (String(where).startsWith('error')) throw new Error(String(where).slice(6));
    flash('SAVED', name, 1600); toast('Saved to ' + where); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
  flash('SAVED', name, 1600);
}
const slotCache = [null, null, null, null];
async function loadSlotInfo(){ for (let i = 0; i < 4; i++) try { const r = await IDB.get('cart' + i); slotCache[i] = r ? {date:r.date} : null; } catch (e) {} }
function slotInfo(i, verb){ const c = slotCache[i]; if (!c) return verb === 'LOAD' ? '(empty)' : 'empty  YES'; const d = new Date(c.date); return p2(d.getMonth() + 1) + '/' + p2(d.getDate()) + ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes()) + '  YES'; }
async function slotSave(i){ flash('SAVING', 'SLOT ' + (i + 1)); const bytes = await SQ.saveCart(S); await IDB.set('cart' + i, {date:Date.now(), bytes}); slotCache[i] = {date:Date.now()}; flash('SAVED', 'SLOT ' + (i + 1)); }
async function slotLoad(i){
  const r = await IDB.get('cart' + i); if (!r) return flash('SLOT ' + (i + 1), 'is empty');
  if (protectedMsg()) return;
  S = await SQ.loadCart(r.bytes); allChanged(); flash('LOADED', 'SLOT ' + (i + 1));
}
async function saveCartFile(){ flash('SAVING', 'cartridge'); await saveFile(await SQ.saveCart(S), 'rx5-' + stamp() + '.o5cart', 'application/octet-stream'); }
async function loadCartFile(f){ if (protectedMsg()) return; S = await SQ.loadCart(await f.arrayBuffer()); allChanged(); flash('CART LOADED', f.name.slice(0, 16)); }
async function decodeAudio(buf){
  const c = ctx || new OfflineAudioContext(1, 1, 44100);
  return await new Promise((res, rej) => { const p = c.decodeAudioData(buf, res, rej); if (p && p.then) p.then(res, rej); });
}
async function loadWav(f){
  const ab = await decodeAudio(await f.arrayBuffer());
  const n = Math.min(ab.length, Math.round(ab.sampleRate * 12)), data = new Float32Array(n);
  for (let c = 0; c < ab.numberOfChannels; c++){ const d = ab.getChannelData(c); for (let i = 0; i < n; i++) data[i] += d[i] / ab.numberOfChannels; }
  S.samples[U.voice] = {name:f.name.replace(/\.[^.]+$/, '').slice(0, 16), sr:ab.sampleRate, data};
  post({t:'sample', v:U.voice, s:S.samples[U.voice]}); dirty();
  flash('WAV > ' + vname(U.voice), (n / ab.sampleRate).toFixed(2) + ' s ' + S.samples[U.voice].name);
}
async function voiceBank(save){
  if (!RXC) return flash('VOICE BANK', 'not available');
  try {
    if (save){
      const fn = RXC.buildBank || RXC.build || RXC.exportBank;
      const bytes = await fn(S); await saveFile(bytes, 'rx5-voices.bin', 'application/octet-stream');
    } else pickFile('.bin,.rx5,application/octet-stream', async f => {
      const fn = RXC.loadBank || RXC.parseBank || RXC.parse || RXC.importBank;
      const r = await fn(new Uint8Array(await f.arrayBuffer()), S, {voice:U.voice});
      if (r && r.state) S = r.state;
      allChanged(); flash('BANK LOADED', r && r.report ? String(r.report).slice(0, 16) : f.name.slice(0, 16)); if (r && r.report) toast(String(r.report), 6000);
    });
  } catch (e) { flash('BANK ERROR', String(e.message || e).slice(0, 16)); }
}
function impStart(){
  if (U.impAt >= 0) return U.impAt;
  for (let i = 0; i < 100; i++) if (S.patterns.slice(i).every(p => !p.events.length)) return i;
  return 0;
}
async function importMidi(f){
  if (protectedMsg()) return;
  const r = SQ.importSMF(await f.arrayBuffer(), {state:S, startPattern:impStart(), songSlot:U.song});
  allChanged();
  U.mode = 'song'; if (r.patterns.length) U.ptn = r.patterns[0]; U.tempoTouched = false;
  const st = S.songs[r.song].steps.length;
  U.lastImport = r.report; console.log('IMPORT MIDI:', r.report);
  flash(r.patterns.length + ' PTN ' + (r.patterns.length ? p2(r.patterns[0]) + '-' + p2(r.patterns[r.patterns.length - 1]) : ''), 'SONG ' + songL(r.song) + ' ' + st + 'st ' + Math.round(r.tempo), 4000);
  toast(r.report, 9000);
}
const expName = () => U.exp ? 'SONG ' + songL(U.song) : 'PTN ' + p2(U.ptn);
async function exportMidi(){
  const what = U.exp ? 'song' : 'pattern', n = U.exp ? U.song : U.ptn;
  const bytes = SQ.exportSMF(S, {what, n});
  await saveFile(bytes, 'rx5-' + (U.exp ? 'song-' + songL(n) : 'ptn' + p2(n)) + '.mid', 'audio/midi');
}
function renderState(){
  // a copy to render offline; a pattern is rendered as a one-step song so it plays once
  const st = Object.assign({}, S, {songs:S.songs.slice()});
  let n = U.song;
  if (!U.exp){ n = 0; st.songs[0] = {name:'RENDER', tempo:S.tempo, steps:[{type:'pat', n:U.ptn}]}; }
  return {st, n};
}
function songSeconds(st, n){
  const f = SQ.flattenSong(st, n); if (!f.length) return 0;
  let secs = 0; const tp = f.tempos.length ? f.tempos : [{t:0, bpm:st.songs[n].tempo}];
  for (let i = 0; i < tp.length; i++){ const t1 = i + 1 < tp.length ? tp[i + 1].t : f.length; secs += (t1 - tp[i].t) * 60 / (tp[i].bpm * SQ.PPQN); }
  return secs;
}
async function renderOffline(seconds){
  const {st, n} = renderState(), sr = 44100;
  const secs = seconds || songSeconds(st, n);
  if (!secs) throw new Error('nothing to render');
  const len = Math.ceil(Math.min(secs + 3, 900) * sr);
  const oc = new OfflineAudioContext(2, len, sr);
  await oc.audioWorklet.addModule('worklet.js');
  const nd = new AudioWorkletNode(oc, 'omega-five', {numberOfInputs:0, outputChannelCount:[2], processorOptions:{state:st, roms:await romsNow(), render:{mode:'song', n}}});
  nd.connect(oc.destination);
  return await oc.startRendering();
}
function encodeWav(buf){
  const nch = buf.numberOfChannels, n = buf.length, sr = buf.sampleRate, out = new DataView(new ArrayBuffer(44 + n * nch * 2));
  const w = (o, s) => { for (let i = 0; i < s.length; i++) out.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); out.setUint32(4, 36 + n * nch * 2, true); w(8, 'WAVEfmt '); out.setUint32(16, 16, true); out.setUint16(20, 1, true); out.setUint16(22, nch, true);
  out.setUint32(24, sr, true); out.setUint32(28, sr * nch * 2, true); out.setUint16(32, nch * 2, true); out.setUint16(34, 16, true); w(36, 'data'); out.setUint32(40, n * nch * 2, true);
  const ch = []; for (let c = 0; c < nch; c++) ch.push(buf.getChannelData(c));
  let o = 44; for (let i = 0; i < n; i++) for (let c = 0; c < nch; c++){ const x = Math.max(-1, Math.min(1, ch[c][i])); out.setInt16(o, x < 0 ? x * 32768 : x * 32767, true); o += 2; }
  return new Uint8Array(out.buffer);
}
async function exportWav(){
  try {
    flash('RENDERING', expName() + '...', 60000);
    const buf = await renderOffline();
    await saveFile(encodeWav(buf), 'rx5-' + (U.exp ? 'song-' + songL(U.song) : 'ptn' + p2(U.ptn)) + '.wav', 'audio/wav');
  } catch (e) { console.error(e); flash('RENDER ERROR', String(e.message || e).slice(0, 16)); }
}
window.__five.renderOffline = renderOffline;

/* ================= MIDI ================= */
const M = {access:null, clocks:[], bpm:0, sx:null};
// the Android app: native MIDI shaped like Web MIDI (byte stream split into messages, SysEx buffered until F7)
if (window.AndroidMidi && AndroidMidi.available && AndroidMidi.available()){
  const access = {inputs:new Map(), outputs:new Map(), onstatechange:null, sysexEnabled:true};
  const scan = () => {
    const seen = new Set();
    for (const d of JSON.parse(AndroidMidi.list())){
      seen.add(d.id);
      if (d.kind === 'input' && !access.inputs.has(d.id)){
        const port = {id:d.id, name:d.name, type:'input', _h:null, _sx:null, _rs:0, _buf:[]};
        Object.defineProperty(port, 'onmidimessage', {get(){ return this._h; }, set(h){ this._h = h; if (h) AndroidMidi.listen(d.id); }});
        access.inputs.set(d.id, port);
      } else if (d.kind === 'output' && !access.outputs.has(d.id)) access.outputs.set(d.id, {id:d.id, name:d.name, type:'output', send(bytes, ts){
        const go = () => AndroidMidi.send(d.id, Array.from(bytes).join(','));
        const dt = ts ? ts - performance.now() : 0; if (dt > 2) setTimeout(go, dt); else go(); }});
    }
    for (const m of [access.inputs, access.outputs]) for (const id of [...m.keys()]) if (!seen.has(id)) m.delete(id);
  };
  const LEN = s => { const h = s & 0xF0; if (h === 0xC0 || h === 0xD0) return 2; if (h < 0xF0) return 3; return s === 0xF2 ? 3 : s === 0xF1 || s === 0xF3 ? 2 : 1; };
  window.__o5Midi = (id, bytes) => {
    const p = access.inputs.get(id); if (!p || !p._h) return;
    const emit = a => p._h({data:Uint8Array.from(a), timeStamp:performance.now(), target:p});
    for (const b of bytes){
      if (b >= 0xF8){ emit([b]); continue; }                       // realtime, may sit inside anything
      if (p._sx){ p._sx.push(b); if (b === 0xF7){ emit(p._sx); p._sx = null; } else if (b & 0x80){ p._sx = null; } else continue; if (!(b & 0x80) || b === 0xF7) continue; }
      if (b === 0xF0){ p._sx = [b]; continue; }
      if (b & 0x80){ p._rs = b < 0xF0 ? b : 0; p._buf = [b]; if (LEN(b) === 1){ emit(p._buf); p._buf = []; } }
      else { if (!p._buf.length && p._rs) p._buf = [p._rs]; if (!p._buf.length) continue; p._buf.push(b); }
      if (p._buf.length && p._buf.length >= LEN(p._buf[0])){ emit(p._buf); p._buf = []; }
    }
  };
  window.__o5MidiChanged = () => { scan(); if (access.onstatechange) access.onstatechange({}); };
  scan();
  navigator.requestMIDIAccess = () => Promise.resolve(access);
}
async function midiInit(){
  if (!navigator.requestMIDIAccess) return;
  try { M.access = await navigator.requestMIDIAccess({sysex:true}); }
  catch (e) { try { M.access = await navigator.requestMIDIAccess(); } catch (e2) { return; } }
  M.access.onstatechange = bindMidi; bindMidi();
}
function bindMidi(){
  if (!M.access) return;
  for (const i of M.access.inputs.values()) i.onmidimessage = U.midiIn === 'all' || U.midiIn === i.id ? ev => onMidi(ev.data, ev.timeStamp) : null;
}
function ports(kind){ if (!M.access) return []; return [...(kind ? M.access.outputs : M.access.inputs).values()]; }
function portLabel(kind){
  const list = ports(kind), cur = kind ? U.midiOut : U.midiIn;
  if (!M.access) return 'NO MIDI';
  if (!kind && cur === 'all') return 'ALL (' + list.length + ')';
  if (kind && !cur) return 'NONE (' + list.length + ')';
  const p = list.find(x => x.id === cur); return p ? p.name.slice(0, 16) : 'gone: ' + cur;
}
function portInc(kind, d){
  const ids = (kind ? [''] : ['all']).concat(ports(kind).map(p => p.id));
  const cur = kind ? U.midiOut : U.midiIn, i = Math.max(0, ids.indexOf(cur));
  const nx = ids[(i + d + ids.length) % ids.length];
  if (kind) U.midiOut = nx; else { U.midiIn = nx; bindMidi(); }
  dirty();
}
function midiOutPort(){ if (!M.access || !U.midiOut) return null; return M.access.outputs.get(U.midiOut) || null; }
function midiSend(bytes, ts){ const o = midiOutPort(); if (!o) return; try { if (ts) o.send(bytes, ts); else o.send(bytes); } catch (e) { try { o.send(bytes); } catch (e2) {} } }
function frameTs(f){
  if (!ctx || !ctx.getOutputTimestamp) return performance.now();
  const o = ctx.getOutputTimestamp(); if (!o || !o.performanceTime) return performance.now();
  return Math.max(performance.now(), o.performanceTime + (f / ctx.sampleRate - o.contextTime) * 1000);
}
function sendEventMidi(ev, ts){
  // note on now, note off (8n v=64) after the gate time; the channel and accent velocity as seq.js decides
  const out = SQ.midiOutForEvent ? SQ.midiOutForEvent(S, ev, {force:true}) : [{bytes:[0x90 | (((S.midi.txCh || 10) - 1) & 15), S.noteMap[ev.v], ev.vel], delayMs:0}, {bytes:[0x80 | (((S.midi.txCh || 10) - 1) & 15), S.noteMap[ev.v], 64], delayMs:S.midi.gateMs || 10}];
  for (const o of out) midiSend(o.bytes, ts + o.delayMs);
}
function midiFromWorklet(evs){
  if (!midiOutPort()) return;
  for (const e of evs){
    const ts = frameTs(e.f);
    if (e.clk){ midiSend([0xF8], ts); continue; }
    if (S.midi.notesOut && S.midi.channelMsg !== false) sendEventMidi(e, ts);
  }
}
function noteName(n){ return ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][n % 12] + (Math.floor(n / 12) - 2); }
function onMidi(d, ts){
  if (!d || !d.length) return;
  const st = d[0];
  if (st === 0xF8){ const t = ts || performance.now(); M.clocks.push(t); if (M.clocks.length > 49) M.clocks.shift(); if (M.clocks.length > 24){ const per = (M.clocks[M.clocks.length - 1] - M.clocks[0]) / (M.clocks.length - 1); M.bpm = 60000 / (per * 24); } }
  // MIDI learn on EDIT NOTE VOICE: the next note on goes to the selected voice
  const it = U.page === 'menu' && U.menu === 'midi' && curItem();
  if (it && it.no === 4.1 && (st & 0xF0) === 0x90 && d[2] > 0){ S.noteMap[U.voice] = d[1]; dirty(); paintLcd(); }
  const ext = S.sync === 'midi' || S.sync === 'ext';
  const acts = SQ.midiIn ? SQ.midiIn(S, {ext}, d) : [];
  for (const a of acts){
    switch (a.type){
      case 'note': playVoice(a.v, a.vel, 0, a.pitch != null ? a.pitch : null, true); break;
      case 'off': if (vget(a.v, 'loop')) post({t:'release', v:a.v}); break;
      case 'clock': post({t:'clock', n:1}); break;
      case 'start': audio().then(() => post({t:'transport', cmd:'start', ...playTarget()})); pos = Object.assign({}, pos || {}, {running:true}); break;
      case 'cont': audio().then(() => post({t:'transport', cmd:'cont', ...playTarget()})); break;
      case 'stop': post({t:'transport', cmd:'stop'}); break;
      case 'spp': post({t:'spp', n:a.n}); break;
      case 'song': selectSong(a.n); paintLcd(); break;
      case 'program': S.midi.tunableVoice = a.n % 24; dirty(); break;
      case 'sysex': onSysex(a.bytes); break;
    }
  }
  // not covered by midiIn: program change selects the pattern / song in VOICE mode, CC7 volume, all notes off
  const type = st & 0xF0, ch = (st & 15) + 1;
  if (st < 0xF0 && ch === (S.midi.rxCh || 10)){
    if (type === 0xC0 && !S.midi.tunable){ if (U.mode === 'song') selectSong(d[1] % 20); else selectPattern(d[1] % 100); paintLcd(); }
    if (type === 0xB0){ if (d[1] === 7){ sset('volume', d[2] / 127); setSlider('VOLUME', S.volume); } else if (d[1] === 120 || d[1] === 123) damp(); }
  }
}
/* SysEx: RX5 dumps and parameter changes, Yamaha-format bulk dumps kept byte-identical (rxsysex.js) */
const BULKS = ['ALL', 'PATTERN', 'SONG', 'VOICES', 'FX', 'RX5 DUMP'];
const bulkName = () => [BULKS[0], 'PATTERN ' + p2(U.ptn), 'SONG ' + songL(U.song), 'VOICES', 'FX', 'RX5 DUMP'][U.bulk % BULKS.length];
function bulkMessages(){
  if (!RXS) return [SQ.sysexDump(S)];
  switch (U.bulk % BULKS.length){
    case 1: return [RXS.omegaPattern(S, U.ptn)];
    case 2: return [RXS.omegaSong(S, U.song)];
    case 3: return [RXS.omegaVoices(S)];
    case 4: return [RXS.omegaFx(S)];
    case 5: return RXS.transmitBulk(S, {deviceNo:S.midi.deviceNo});
  }
  return [RXS.omegaFull(S)];
}
function transmitBulk(){
  if (!midiOutPort()) return flash('NO MIDI OUT', 'MIDI 14: port');
  const msgs = bulkMessages(); if (!msgs.length) return flash('NO RX5 DUMP', 'receive one first');
  let t = performance.now(); for (const m of msgs){ midiSend(m, t); t += m.length / 3.125 + 5; }
  flash('TRANSMIT BULK', bulkName().slice(0, 10) + ' ' + Math.max(1, Math.round(msgs.reduce((a, m) => a + m.length, 0) / 1024)) + 'K', 2000);
}
async function saveSyx(){
  const msgs = bulkMessages(); if (!msgs.length) return flash('NO RX5 DUMP', 'receive one first');
  const out = RXS && RXS.writeSyx ? RXS.writeSyx(msgs) : msgs[0];
  await saveFile(out, 'rx5-' + ['all', 'ptn' + p2(U.ptn), 'song-' + songL(U.song), 'voices', 'fx', 'rx5'][U.bulk % BULKS.length] + '.syx', 'application/octet-stream');
}
async function loadSyx(f){
  const u8 = new Uint8Array(await f.arrayBuffer());
  if (protectedMsg()) return;
  if (RXS && RXS.loadSyx){
    const r = RXS.loadSyx(S, u8); allChanged();
    flash('SYX ' + (r.omega ? r.omega + ' OMEGA ' : '') + (r.yamaha ? r.yamaha + ' RX5' : ''), r.errors.length ? r.errors[0].slice(0, 16) : r.status ? 'dump ' + r.status : 'loaded', 2600);
    return;
  }
  S = SQ.parseSysex(u8); allChanged(); flash('SYX LOADED', f.name.slice(0, 16));
}
function onSysex(bytes, forced){
  const u8 = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
  try {
    if (RXS && RXS.handleSysex){
      // dumps change memory, so they are taken only while RECEIVE BULK waits; requests, identity and parameter changes always
      const dump = (RXS.isYamaha(u8) && (u8[2] >> 4) !== 2) || (RXS.isOmega(u8) && [RXS.OMEGA.FULL, RXS.OMEGA.PATTERN, RXS.OMEGA.SONG, RXS.OMEGA.VOICES, RXS.OMEGA.FX].includes(u8[4]));
      if (dump && !forced && !U.bulkWait) return flash('BULK IGNORED', 'MIDI 09 first');
      if (dump && U.protect) return protectedMsg();
      const r = RXS.handleSysex(S, u8, {deviceNo:S.midi.deviceNo, receiver:M.rx || (M.rx = new RXS.BulkReceiver())});
      for (const m of r.reply || []) midiSend(m);
      if (r.kind === 'omega' && !r.error && r.what){ allChanged(); if (r.cmd !== RXS.OMEGA.PARAM) U.bulkWait = false; }
      if (r.error) flash(String(r.error).slice(0, 16), '', 2600);
      else if (r.message) flash(r.kind === 'yamaha-bulk' ? 'RX5 BULK' : 'BULK', String(r.message).slice(0, 16), 2200);
      if (r.kind === 'yamaha-bulk') dirty();
      return;
    }
    if (u8[1] === 0x7D){
      if (!forced && !U.bulkWait) return flash('BULK IGNORED', 'MIDI 09 first');
      S = SQ.parseSysex(u8); U.bulkWait = false; allChanged(); flash('BULK RECEIVED', Math.round(u8.length / 1024) + 'K', 2000);
    }
  } catch (e) { console.warn(e); flash(/checksum/i.test(e.message) ? 'checksum error!' : 'BULK ERROR', String(e.message || '').slice(0, 16), 2600); }
}

/* ================= playing voices ================= */
const held = new Set();
let kbShift = false;
function playVoice(v, vel, acc, pitch, fromMidi, over){
  audio();
  const rec = U.page === 'rtw' && U.mode === 'pattern' && !U.protect && running();
  if (held.has('CLEAR') && U.page === 'rtw' && running()){
    if (protectedMsg()) return;
    SQ.deleteEvents(S, U.ptn, 0, SQ.patLen(pat()), v); patChanged(); flash('ERASE', vname(v), 700); return;
  }
  over = Object.assign({}, over || {}); if (pitch != null) over.pitch = pitch;
  const ch = over.ch; delete over.ch;
  post({t:'hit', v, vel, acc, rec, over, ch});
  flashPad(v);
  if (!fromMidi && S.midi.notesOut && S.midi.channelMsg !== false) sendEventMidi({v, vel, acc}, performance.now());
}
// key data of the current set-up (S.setups); the key offsets apply to the note played from that key
function keyOf(i){ return (S.setups && S.setups[S.setup | 0] ? S.setups[S.setup | 0].keys[i] : null) || {voice:i, pitch:0, level:0, attack:0, decay:0, acc1:2, acc2:4, out:i % 12}; }
function keySet(i, k, val){ const su = S.setups && S.setups[S.setup | 0]; if (!su) return; su.keys[i] = Object.assign({}, su.keys[i], {[k]:val}); keysChanged(); }
// the worklet needs the set-ups for the OUTPUT CH of sequenced and MIDI notes
function keysChanged(){ post({t:'keys', setups:S.setups, setup:S.setup | 0}); dirty(); }
function keyItem(no, name, k, min, max, opts = {}){ return numItem(no, name, () => keyOf(U.pad)[k], x => keySet(U.pad, k, x), min, max, Object.assign({ctx:() => 'key' + String.fromCharCode(65 + U.pad), pad:i => { U.pad = i; }}, opts)); }
function sgn(x){ return (x > 0 ? '+' : '') + x; }
const multiStep = () => { const k = ['pitch', 'level', 'attack', 'decay'][U.multi.param | 0]; const d = {pitch:100, level:-1, attack:-3, decay:-3}[k]; return U.multi[k + 'Step'] != null ? U.multi[k + 'Step'] : d; };
function keyOver(v, key, multiIdx){
  const o = {}, base = k => vget(v, k);
  let dp = key.pitch || 0, dl = key.level || 0, da = key.attack || 0, dd = key.decay || 0;
  if (multiIdx != null){ const n = multiIdx - 12, st = multiStep(); [() => dp += n * st, () => dl += n * st, () => da += n * st, () => dd += n * st][U.multi.param | 0](); }
  if (dp) o.pitch = clamp(base('pitch') + dp, -3600, 2400);
  if (dl) o.level = clamp(base('level') + dl, 0, 31);
  if (da) o.ar = clamp(base('ar') + da, 1, 99);
  if (dd){ o.d1r = clamp(base('d1r') + dd, 0, 99); o.d2r = clamp(base('d2r') + dd, 0, 99); }
  if (key.acc1 != null && key.acc1 !== base('acc1')) o.acc1 = key.acc1;
  if (key.acc2 != null && key.acc2 !== base('acc2')) o.acc2 = key.acc2;
  if (Number.isInteger(key.out)) o.ch = key.out;   // OUTPUT CH (not a voice parameter: playVoice sends it with the hit)
  return o;
}
function padVoice(i){ return U.multi.on ? U.multi.v : keyOf(i).voice; }
function hitPad(i, vel){
  if (U.page !== 'ptn' && U.page !== 'songplay') unflash();
  const pg = page();
  if (pg.padOnly){ pg.pad(i); paintLcd(); return; }
  const key = keyOf(i), v = padVoice(i), over = keyOver(v, key, U.multi.on ? i : null);
  const acc = held.has('ACC2') ? 2 : held.has('ACC1') ? 1 : 0; if (acc) U.accUsed = true;
  if (U.padVel) vel = U.padVel;
  U.pad = i; U.voice = v; U.ch = v % 12;
  if (!pg.silentToggle || !running()) playVoice(v, vel, acc, null, false, over);
  if (pg.pad) pg.pad(i, v, vel, acc);
  paintLcd();
}
const padEls = [];
function flashPad(v){
  for (let i = 0; i < 24; i++) if (padVoice(i) === v){
    const e = padEls[i]; if (!e) continue; e.classList.add('lit'); clearTimeout(e._t); e._t = setTimeout(() => e.classList.remove('lit'), 90);
    if (!U.multi.on) break;
  }
}

/* ================= keys ================= */
function fnKey(k, sh){
  const songish = U.mode === 'song' || sh;
  switch (k){
    case 'PATTERN': if (sh){ U.mode = 'song'; U.page = 'songplay'; } else { U.mode = U.mode === 'song' ? 'pattern' : 'song'; backToPlay(); } U.menu = null; U.chainPlay = false; break;
    case 'REAL TIME WRITE': if (songish){ U.mode = 'song'; openMenu('esong', 0); } else U.page = 'rtw'; break;
    case 'STEP WRITE': if (songish) songInsert('pat'); else { U.page = 'step'; U.stepVoice = U.voice; } break;
    case 'EDIT VOICE': openMenu('voice'); break;
    case 'KEY ASSIGN': openMenu('key'); break;
    case 'QUANTIZE': if (songish) songDelete(); else U.page = 'quant'; break;
    case 'SWING': if (songish) songInsert('rep'); else U.page = 'swing'; break;
    case 'CLICK': if (songish) songInsert('tempo'); else U.page = 'click'; break;
    case 'CHAIN': openMenu('chain'); break;
    case 'CASSETTE': openMenu('cart'); break;
    case 'EDIT PATTERN': if (songish) songInsert('vol'); else openMenu('eptn'); break;
    case 'CLEAR': if (U.page !== 'rtw') U.page = 'clear'; break;
    case 'COPY': U.page = 'copy'; U.copyDst = U.mode === 'song' ? (U.song + 1) % 20 : (U.ptn + 1) % 100; break;
    case 'SYNC': U.page = 'sync'; break;
    case 'MIDI': openMenu('midi'); break;
    case 'REVERSE': U.page = 'reverse'; vset(U.voice, 'reverse', vget(U.voice, 'reverse') ? 0 : 1); break;
    case 'DAMP': damp(); flash('DAMP', 'all voices off', 700); break;
    case 'TEMPO': U.page = 'tempo'; break;
    case 'UTILITY': openMenu('util'); break;
    case 'JOB': openMenu('job'); break;
  }
}
function modeKey(k){
  switch (k){
    case 'PTN': U.mode = 'pattern'; backToPlay(); U.chainPlay = false; break;
    case 'SONG': U.mode = 'song'; backToPlay(); break;
    case 'EDIT VOICE': openMenu('voice'); break;
    case 'CAS': openMenu('cart', 21); break;
    case 'CART': openMenu('cart', 1); break;
    case 'MIDI': openMenu('midi'); break;
    case 'REV': openMenu('rev'); break;
    case 'DLY': openMenu('dly'); break;
    case 'DAMP': damp(); break;
    case 'MULTI': openMenu('key', 3); break;
    case 'INT SYNC': setSync('int'); break; case 'MIDI SYNC': setSync('midi'); break; case 'TAPE SYNC': setSync('tape'); break; case 'EXT SYNC': setSync('ext'); break;
    case 'RUN': running() ? stop() : start(); break;
  }
}
const MODE_NAMES = new Set(MODE_KEYS.map(m => m[0].replace('\n', ' ')));
function keyDown(k, fromKbd){
  audio(); unflash();
  if (k === 'STOP'){ held.add(k); U.stopWasRun = running(); U.shiftUsed = false; if (U.stopWasRun) stop(); paintAll(); return; }
  const sh = held.has('STOP') || kbShift; if (held.has('STOP')) U.shiftUsed = true;
  held.add(k);
  const pg = page();
  if (k === 'START'){ if (U.page === 'name') nameDone(); else start(); }
  else if (k === 'YES' || k === 'NO'){ const d = k === 'YES' ? 1 : -1; if (pg.pm) pg.pm(d, sh); }
  else if (k[0] === 'N' && k.length === 2){ if (pg.digit) pg.digit(+k[1]); }
  else if (k === 'ACC1' || k === 'ACC2'){ U.accUsed = false; if (U.page === 'name'){ nameType(k === 'ACC1' ? 'Y' : 'Z'); U.accUsed = true; } }
  else if (k === 'RANDOM'){ if (U.page === 'menu' && U.menu === 'job' && curItem().no === 4) doRandom(); else { U.rnd = 0; openMenu('job', 4); } }
  else if (MODE_NAMES.has(k) && fromKbd !== 'fn') modeKey(k);
  else fnKey(k, sh);
  paintAll();
}
function keyUp(k){
  if (!held.has(k)) return;
  held.delete(k);
  if (k === 'STOP' && !U.stopWasRun && !U.shiftUsed) cont();
  if ((k === 'ACC1' || k === 'ACC2') && !U.accUsed){ const pg = page(); if (pg.move) pg.move(k === 'ACC1' ? -1 : 1); }
  paintAll();
}

/* ================= painting ================= */
function paintLeds(){
  const on = (k, v) => { const b = document.querySelector(`.mk[data-k="${k}"]`); if (b) b.classList.toggle('on', !!v); };
  const it = U.page === 'menu' ? curItem() : null;
  on('PTN', U.mode === 'pattern'); on('SONG', U.mode === 'song');
  on('EDIT VOICE', U.menu === 'voice' && U.page === 'menu');
  on('CAS', U.page === 'menu' && U.menu === 'cart' && it && it.no >= 21); on('CART', U.page === 'menu' && U.menu === 'cart' && it && it.no < 21);
  on('MIDI', U.page === 'menu' && U.menu === 'midi'); on('REV', U.page === 'menu' && U.menu === 'rev'); on('DLY', U.page === 'menu' && U.menu === 'dly');
  on('DAMP', U.dampT && performance.now() - U.dampT < 250); on('MULTI', U.multi.on);
  on('INT SYNC', S.sync === 'int'); on('MIDI SYNC', S.sync === 'midi'); on('TAPE SYNC', S.sync === 'tape'); on('EXT SYNC', S.sync === 'ext');
  on('RUN', running() && !(pos.tick < 4 && pos.beat === 1));
  for (const k of ['ACC1', 'ACC2']) { const b = document.querySelector(`[data-k="${k}"]`); if (b) b.classList.toggle('dn', held.has(k)); }
}
function paintSliders(){
  setSlider('VOLUME', S.volume); setSlider('CLICK', S.click.level); setSlider('TEMPO', (S.tempo - 40) / 210);
  for (let c = 0; c < 12; c++) setSlider('CH' + c, cget(c, 'fader'));
}
function paintAll(){ paintLcd(); paintLeds(); if (typeof refreshView === 'function' && document.getElementById('pgBody')){ refreshView(); refreshBigLcd(); } if (loupeAt) paintLoupe(); }

/* ================= wiring ================= */
const KB_PADS = 'qwertyuiop[]asdfghjkl;\'\\';
function wire(){
  const p = P();
  document.querySelectorAll('.pad').forEach(e => padEls[+e.dataset.pad] = e);
  // pads: multitouch, velocity from where the pad is struck (top = loudest)
  p.addEventListener('pointerdown', e => {
    if (zoom && !e.__loupe){ e.preventDefault(); showLoupe(e.clientX, e.clientY); return; }
    const pad = e.target.closest('.pad');
    if (pad){
      e.preventDefault(); cap(pad, e);
      const r = pad.getBoundingClientRect(), y = clamp((e.clientY - r.top) / r.height, 0, 1);
      pad.classList.add('hit'); hitPad(+pad.dataset.pad, Math.round(clamp(127 - y * 96, 16, 127)));
      return;
    }
    const sl = e.target.closest('.slider');
    if (sl){ e.preventDefault(); cap(sl, e); const s = sliders[sl.dataset.s]; s.drag = {y:e.clientY, v:s.v, id:e.pointerId}; return; }
    const key = e.target.closest('[data-k]');
    if (key){ e.preventDefault(); cap(key, e); key.classList.add('dn'); key._pid = e.pointerId; keyDown(key.dataset.k, key.classList.contains('fk') ? 'fn' : false); return; }
    // the printed legend is tiny: a tap on it shows that column magnified; a tap on a line there opens that job, anywhere else closes it
    const mg = $('#mag');
    if (mg.style.display === 'block'){
      const it = e.target.closest('#mag u, #mag b');
      if (it && it.dataset.m){ unflash(); const m = it.dataset.m, no = it.dataset.i != null ? +it.dataset.i : null; if (m === 'song' || m === 'esong') U.mode = 'song'; openMenu(m, no); paintAll(); }
      if (!e.target.closest('#mag') || (it && it.dataset.m)) mg.style.display = 'none';
      e.preventDefault(); return;
    }
    const col = e.target.closest('.legend');
    if (col){ e.preventDefault(); magnify(col); return; }
    if (e.target.closest('#lcd')){ const r = $('#lcd').getBoundingClientRect(), d = e.clientX < r.left + r.width / 2 ? -1 : 1; const pg = page(); if (pg.move) pg.move(d); paintAll(); return; }
  });
  const up = e => {
    const pad = e.target.closest && e.target.closest('.pad'); if (pad) pad.classList.remove('hit');
    const key = e.target.closest && e.target.closest('[data-k]');
    if (key && key._pid === e.pointerId){ key.classList.remove('dn'); key._pid = null; keyUp(key.dataset.k); }
    for (const id in sliders){ const s = sliders[id]; if (s.drag && s.drag.id === e.pointerId) s.drag = null; }
  };
  p.addEventListener('pointerup', up); p.addEventListener('pointercancel', up);
  p.addEventListener('pointermove', e => {
    for (const id in sliders){
      const s = sliders[id]; if (!s.drag || s.drag.id !== e.pointerId) continue;
      sliderSet(id, s.drag.v - (e.clientY - s.drag.y) / (scale * (s.h - 40)));
    }
  });
  p.addEventListener('wheel', e => { const sl = e.target.closest('.slider'); if (!sl) return; e.preventDefault(); sliderSet(sl.dataset.s, sliders[sl.dataset.s].v - e.deltaY * 0.0012); }, {passive:false});
  p.addEventListener('dblclick', e => { const sl = e.target.closest('.slider'); if (!sl) return; const id = sl.dataset.s; sliderSet(id, id === 'TEMPO' ? (120 - 40) / 210 : id === 'VOLUME' ? 0.8 : id === 'CLICK' ? 0.5 : id === 'DATA' ? sliders[id].v : chanDefault(+id.slice(2)).fader); });
  $('#kRandom').dataset.k = 'RANDOM';
  // desktop keys: pads q..] and a..\, keypad digits, - / = for NO / YES, space start / stop, Esc stop, z x accents, arrows
  addEventListener('keydown', e => {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    kbShift = e.shiftKey;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (e.repeat && k !== 'ArrowUp' && k !== 'ArrowDown') return;
    const pi = KB_PADS.indexOf(k);
    let used = true;
    if (pi >= 0 && k !== '') hitPad(pi, e.shiftKey ? 127 : 100);
    else if (/^[0-9]$/.test(k)) keyDown('N' + k);
    else if (k === '-' || k === '_') keyDown('NO');
    else if (k === '=' || k === '+') keyDown('YES');
    else if (k === ' ') running() ? stop() : start();
    else if (k === 'Enter') keyDown('START');
    else if (k === 'Escape'){ stop(); backToPlay(); }
    else if (k === 'z') keyDown('ACC1'); else if (k === 'x') keyDown('ACC2');
    else if (k === 'ArrowLeft' || k === 'ArrowRight'){ const pg = page(); if (pg.move) pg.move(k === 'ArrowLeft' ? -1 : 1); }
    else if (k === 'ArrowUp' || k === 'ArrowDown'){ const d = k === 'ArrowUp' ? 1 : -1, pg = page(); if (U.page === 'menu'){ const it = curItem(); if (inSongEdit()) songEditInc(d, e.shiftKey); else if (it.inc) it.inc(d, e.shiftKey); else if (it.noFn) it.noFn(); } else if (pg.pm) pg.pm(d, e.shiftKey); }
    else used = false;
    if (used){ e.preventDefault(); paintAll(); }
  });
  addEventListener('keyup', e => {
    kbShift = e.shiftKey;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (k === 'z') keyUp('ACC1'); else if (k === 'x') keyUp('ACC2');
    else if (/^[0-9]$/.test(k)) held.delete('N' + k); else if (k === '-' || k === '_') held.delete('NO'); else if (k === '=' || k === '+') held.delete('YES'); else if (k === 'Enter') held.delete('START');
  });
  addEventListener('pointerdown', () => audio(), {once:true, capture:true});
  addEventListener('keydown', () => audio(), {once:true, capture:true});
  addEventListener('resize', fit);
  document.addEventListener('touchstart', e => { if (e.target.closest && e.target.closest('#p')) e.preventDefault(); }, {capture:true, passive:false});
  document.addEventListener('contextmenu', e => e.preventDefault(), {capture:true});
  document.addEventListener('visibilitychange', () => { if (document.hidden) saveNow(); });
  addEventListener('pagehide', saveNow);
  addEventListener('dragover', e => e.preventDefault());
  addEventListener('drop', e => {
    e.preventDefault(); const f = e.dataTransfer.files[0]; if (!f) return;
    const n = f.name.toLowerCase(), run = fn => fn(f).catch(err => flash('LOAD ERROR', String(err.message).slice(0, 16)));
    if (/\.(mid|midi|smf)$/.test(n)) run(importMidi); else if (/\.o5cart$|\.json$/.test(n)) run(loadCartFile); else if (/\.syx$/.test(n)) run(loadSyx); else run(loadWav);
  });
}
function sliderSet(id, v){
  v = clamp(v, 0, 1); setSlider(id, v);
  if (id === 'VOLUME') sset('volume', v);
  else if (id === 'CLICK'){ S.click.level = v; post({t:'set', k:'click', v:S.click}); dirty(); if (U.page === 'click') paintLcd(); }
  else if (id === 'TEMPO'){ setTempo(40 + 210 * v); if (U.page === 'tempo' || U.page === 'ptn' || U.page === 'songplay') paintLcd(); }
  else if (id === 'DATA'){ const pg = page(); if (pg.data){ pg.data(v); paintLcd(); } }
  else cset(+id.slice(2), 'fader', v);
}

/* ================= the rail and its pages =================
   Every LCD command, folded open at a size you can read and tap: a page per group, built from the same MENUS / PAGES
   the LCD uses (nothing is duplicated). A row: job number and name, the value, a slider where the job has one, and keys:
   ◀ ▶ (steps inside the job), − + (the value), NO / YES (actions). What doesn't fit goes on the next page (◀ 1 2 ▶). */
const elp = (tag, cls, parent, html) => { const e = el(tag, cls, null, html); if (parent) parent.appendChild(e); return e; };
const cap = (el, e) => { try { if (el.setPointerCapture && !e.__loupe) el.setPointerCapture(e.pointerId); } catch (err) {} };
let view = 'machine', zoom = false;
const VIEWS = [['machine', 'MACHINE'], ['voice', 'VOICE'], ['keys', 'KEYS'], ['pattern', 'PATTERN'], ['song', 'SONG'], ['mixer', 'MIXER'],
  ['fx', 'FX'], ['midi', 'MIDI'], ['files', 'FILES']];
const wrapPage = (no, name, pg, extra = {}) => Object.assign({no, name, l2:() => pg.lcd()[1].trim() || pg.lcd()[0].trim(), inc:pg.pm ? (d, sh) => pg.pm(d, sh) : null,
  data:pg.data || null}, extra);
function viewItems(v){
  const M = MENUS, I = [];
  const add = (head, list) => { I.push({head}); for (const it of list) if (!it.hidden) I.push(it); };
  if (v === 'voice') add('EDIT VOICE', M.voice);
  else if (v === 'keys') add('KEY ASSIGN', M.key);
  else if (v === 'pattern'){
    add('PATTERN', [{no:0, name:'PATTERN', l2:() => p2(U.ptn) + ' ' + pat().name, inc:d => selectPattern(U.ptn + d), data:x => selectPattern(Math.round(x * 99))}, ...M.eptn]);
    add('WRITE', [wrapPage(0, 'QUANTIZE', PAGES.quant), wrapPage(0, 'SWING', PAGES.swing), wrapPage(0, 'CLICK', PAGES.click), wrapPage(0, 'TEMPO', PAGES.tempo),
      wrapPage(0, 'SYNC', PAGES.sync), {no:0, name:'CLEAR PATTERN', l2:() => 'PTN' + p2(U.ptn) + ' sure?', yes:() => { if (protectedMsg()) return; SQ.clearPattern(S, U.ptn); patChanged(); flash('PTN' + p2(U.ptn), 'CLEARED'); }},
      {no:0, name:'COPY PATTERN', l2:() => 'to PTN' + p2(U.copyDst | 0), noFn:() => { U.copyDst = ((U.copyDst | 0) + 1) % 100; }, inc:d => { U.copyDst = ((U.copyDst | 0) + d + 100) % 100; },
        yes:() => { if (protectedMsg()) return; SQ.copyPattern(S, U.ptn, U.copyDst | 0); patChanged(U.copyDst | 0); flash('COPIED', 'PTN' + p2(U.ptn) + '>' + p2(U.copyDst | 0)); }}]);
  } else if (v === 'song'){
    add('SONG', [{no:0, name:'SONG', l2:() => songL(U.song) + ' ' + song().name, inc:d => selectSong(U.song + d), data:x => selectSong(Math.round(x * 19))},
      {no:0, name:'EDIT SONG', l2:() => songEditLcd().join('  '), move:d => songEditMove(d), inc:(d, sh) => songEditInc(d, sh), data:x => songEditData(x)},
      {no:0, name:'INSERT', l2:() => 'at s' + p3(U.songStep + 1), yes:() => { U.mode = 'song'; openMenu('esong', 0); songInsert('pat'); }},
      {no:0, name:'DELETE', l2:() => 'step s' + p3(U.songStep + 1), yes:() => { U.mode = 'song'; openMenu('esong', 0); songDelete(); }},
      ...M.esong, ...M.song]);
    add('CHAIN', M.chain);
  } else if (v === 'mixer'){
    // the channel picked in the header in full, then every channel's level and mute (a console strip per row)
    const c = () => U.ch, nm = ch => CH_LBL[ch].replace('<br>', '/');
    I.push({head:'CHANNEL'});
    I.push({no:0, name:'LEVEL', l2:() => pct(cget(c(), 'fader')), inc:d => { cset(c(), 'fader', clamp(cget(c(), 'fader') + d * .02, 0, 1)); paintSliders(); }, data:x => { cset(c(), 'fader', x); paintSliders(); }},
      {no:0, name:'PAN', l2:() => fmtPan(cget(c(), 'pan')), inc:d => cset(c(), 'pan', clamp(Math.round((cget(c(), 'pan') + d / 7) * 7) / 7, -1, 1)), data:x => cset(c(), 'pan', x * 2 - 1)},
      {no:0, name:'REV SEND', l2:() => pct(cget(c(), 'sendRev')), inc:d => cset(c(), 'sendRev', clamp(cget(c(), 'sendRev') + d * .05, 0, 1)), data:x => cset(c(), 'sendRev', x)},
      {no:0, name:'DLY SEND', l2:() => pct(cget(c(), 'sendDly')), inc:d => cset(c(), 'sendDly', clamp(cget(c(), 'sendDly') + d * .05, 0, 1)), data:x => cset(c(), 'sendDly', x)},
      {no:0, name:'MUTE', l2:() => cget(c(), 'mute') ? 'MUTED' : 'on', yes:() => cset(c(), 'mute', !cget(c(), 'mute'))});
    I.push({head:'ALL CHANNELS'});
    for (let ch = 0; ch < 12; ch++) I.push({no:0, name:(ch + 1) + ' ' + nm(ch), l2:() => (cget(ch, 'mute') ? 'MUTE ' : '') + pct(cget(ch, 'fader')),
      data:x => { cset(ch, 'fader', x); paintSliders(); }, sv:() => cget(ch, 'fader'), yes:() => cset(ch, 'mute', !cget(ch, 'mute')), yesLabel:'M'});
  } else if (v === 'fx'){ add('REV 7', M.rev); add('TC 2290 DELAY', M.dly); }
  else if (v === 'midi') add('MIDI', M.midi);
  else if (v === 'files'){ add('CASSETTE / CARTRIDGE', M.cart); add('JOB', M.job); add('UTILITY', M.util); }
  return I;
}
const fmtNo = no => { if (!no) return ''; const w = Math.floor(no + 1e-9), sub = Math.round((no - w) * 10); return p2(w) + (sub ? '-' + sub : ''); };
// the page header: what the rows act on (voice, key, channel...), with ◀ ▶
const VIEW_CTX = {voice:['VOICE', () => vname(U.voice), d => { U.voice = (U.voice + d + 24) % 24; }], keys:['KEY', () => String.fromCharCode(65 + U.pad) + ' ' + vname(keyOf(U.pad).voice), d => { U.pad = (U.pad + d + 24) % 24; }],
  pattern:['PTN', () => p2(U.ptn), d => selectPattern(U.ptn + d)], song:['SONG', () => songL(U.song), d => selectSong(U.song + d)],
  fx:['CH', () => STEP_CH[U.ch], d => { U.ch = (U.ch + d + 12) % 12; }], mixer:['CH', () => (U.ch + 1) + ' ' + STEP_CH[U.ch], d => { U.ch = (U.ch + d + 12) % 12; }], midi:['VOICE', () => vname(U.voice), d => { U.voice = (U.voice + d + 24) % 24; }]};
const pageState = {};
function rowEl(it){
  if (it.head) return elp('div', 'pgHead' + (it.small ? ' small' : ''), null, it.head);
  const r = elp('div', 'pgRow');
  r.appendChild(elp('div', 'pgName', null, (fmtNo(it.no) ? '<i>' + fmtNo(it.no) + '</i> ' : '') + it.name));
  const val = elp('div', 'pgVal'); r.appendChild(val); r._val = val; r._it = it;
  const ctl = elp('div', 'pgCtl'); r.appendChild(ctl);
  const k = (t, f, cls) => { const b = elp('button', 'pk' + (cls ? ' ' + cls : ''), null, t); b.onclick = () => { audio(); f(); afterAction(); }; ctl.appendChild(b); };
  // a slider only where it can show where the value is (sv); elsewhere − + set it exactly
  if (it.data && it.sv){ const sl = elp('input', 'pgSl'); sl.type = 'range'; sl.min = 0; sl.max = 1000; sl.oninput = () => { it.data(sl.value / 1000); afterAction(); }; ctl.appendChild(sl); r._sl = sl; }
  if (it.move) k('◀', () => it.move(-1)), k('▶', () => it.move(1));
  if (it.inc) k('−', () => it.inc(-1)), k('+', () => it.inc(1));
  if (it.yes){ if (it.noFn) k('NO', it.noFn, 'red'); k(it.yesLabel || 'YES', it.yes, 'red'); }
  else if (it.noFn && !it.inc) k('NEXT', it.noFn);
  return r;
}
function afterAction(){ refreshView(); paintAll(); paintSliders(); }
function refreshView(){
  for (const r of document.querySelectorAll('#pgBody .pgRow')){ try { const t = r._it.l2 ? r._it.l2() : ''; if (r._val.textContent !== t) r._val.textContent = t; if (r._sl && document.activeElement !== r._sl) r._sl.value = Math.round(r._it.sv() * 1000); } catch (e) { r._val.textContent = ''; } }
  const c = VIEW_CTX[curView()]; if (c) $('#pgCtx b').textContent = c[1]();
  for (const b of document.querySelectorAll('#rail [data-v]')) b.classList.toggle('on', b.dataset.v === view);
  $('#zoomBtn').classList.toggle('on', zoom);
}
const curView = () => innerWidth < innerHeight && view === 'machine' ? 'lcd' : view;
function showView(x, y, w, h){
  const pv = $('#pageView'), v = curView();
  Object.assign(pv.style, {display:'flex', left:x + 'px', top:y + 'px', width:w + 'px', height:h + 'px'});
  pv.innerHTML = '';
  const head = elp('div', 'pgTop', pv), body = elp('div', 'pgBodyWrap', pv); body.id = 'pgBody';
  if (v === 'lcd'){ buildLcdView(head, body); return; }
  head.appendChild(elp('b', 'pgTitle', null, (VIEWS.find(q => q[0] === v) || [, ''])[1]));
  const c = VIEW_CTX[v];
  if (c){ const cx = elp('span', 'pgCtx', head); cx.id = 'pgCtx'; cx.appendChild(elp('i', null, null, c[0]));
    const kb = (t, d) => { const b = elp('button', 'pk', null, t); b.onclick = () => { c[2](d); afterAction(); }; cx.appendChild(b); };
    kb('◀', -1); cx.appendChild(elp('b')); kb('▶', 1); }
  const pg = elp('span', 'pgBtns', head);
  // lay the rows out page by page: a new page whenever the next row doesn't fit. The page keys in the header can make
  // it taller, so once they are in, lay out again in what is left (as Omega 110's paginate does)
  const rows = viewItems(v).map(rowEl); let pages;
  rows.forEach(r => body.appendChild(r));
  const fits = () => { if (body.scrollHeight > body.clientHeight) return false; const vis = rows.filter(r => r.style.display !== 'none'); const last = vis[vis.length - 1];
    return !last || last.getBoundingClientRect().bottom <= body.getBoundingClientRect().bottom - .5; };
  const paginate = () => {
    pages = [[]]; rows.forEach(r => r.style.display = 'none');
    for (const r of rows){
      r.style.display = ''; pages[pages.length - 1].push(r);
      if (!fits() && pages[pages.length - 1].length > 1){ const cur = pages[pages.length - 1]; cur.pop(); cur.forEach(o => o.style.display = 'none');
        // a heading never ends a page
        if (cur.length && cur[cur.length - 1].classList.contains('pgHead')){ const h = cur.pop(); h.style.display = ''; pages.push([h, r]); } else pages.push([r]); }
    }
  };
  paginate();
  const st = pageState[v] = pageState[v] || {page:0}; st.page = Math.min(st.page, pages.length - 1);
  const show = () => {
    rows.forEach(r => r.style.display = 'none'); pages[st.page].forEach(r => r.style.display = '');
    pg.innerHTML = '';
    if (pages.length > 1){
      const b = (t, f, on) => { const k = elp('button', 'pk pg' + (on ? ' on' : ''), pg, t); k.onclick = f; };
      b('◀', () => { st.page = Math.max(0, st.page - 1); show(); });
      pages.forEach((_, i) => b(String(i + 1), () => { st.page = i; show(); }, i === st.page));
      b('▶', () => { st.page = Math.min(pages.length - 1, st.page + 1); show(); });
    }
    refreshView();
  };
  for (let k = 0; k < 3; k++){ const h0 = body.clientHeight; show(); if (body.clientHeight === h0 && fits()) break; paginate(); st.page = Math.min(st.page, pages.length - 1); }
  show();
  let x0 = null;   // swipe to turn the page
  body.onpointerdown = e => { x0 = e.target.closest('button,input') ? null : e.clientX; };
  body.onpointerup = e => { if (x0 === null) return; const dx = e.clientX - x0; x0 = null; if (Math.abs(dx) > 50){ st.page = clamp(st.page + (dx < 0 ? 1 : -1), 0, pages.length - 1); show(); } };
}
// portrait MACHINE: the LCD and its keys at a size you can use, under the machine
function buildLcdView(head, body){
  head.appendChild(elp('b', 'pgTitle', null, 'LCD'));
  const big = elp('div', 'bigLcd', body); big.id = 'bigLcd';
  const keys = elp('div', 'bigKeys', body);
  for (const [t, k, cls] of [['◀ ITEM', 'ACC1'], ['ITEM ▶', 'ACC2'], ['−1 NO', 'NO', 'red'], ['+1 YES', 'YES', 'red'], ['STOP/CONT', 'STOP', 'blue'], ['START', 'START', 'start']]){
    const b = elp('button', 'pk ' + (cls || ''), keys, t);
    b.onpointerdown = e => { e.preventDefault(); audio(); keyDown(k); }; b.onpointerup = () => { keyUp(k); afterAction(); };
  }
  refreshBigLcd();
}
function refreshBigLcd(){ const b = $('#bigLcd'); if (b){ const t = lcdText.join('\n'); if (b.textContent !== t) b.textContent = t; } }
function buildRail(){
  const r = $('#rail'); r.innerHTML = '';
  const fb = elp('button', 'rk fold', r, 'FOLD'); fb.id = 'foldBtn'; fb.onclick = () => setFold(!document.body.classList.contains('folded'));
  for (const [v, t] of VIEWS){ const b = elp('button', 'rk', r, t); b.dataset.v = v; b.onclick = () => { view = v; hideLoupe(); fit(); refreshView(); }; }
  const z = elp('button', 'rk zoom', r, '🔍 ZOOM'); z.id = 'zoomBtn'; z.title = 'Magnifier: tap the panel to see it bigger, then use it there';
  z.onclick = () => { zoom = !zoom; if (!zoom) hideLoupe(); else { if (view !== 'machine'){ view = 'machine'; fit(); } toast('Tap any part of the panel to magnify it', 2500); } refreshView(); };
}
function setFold(f){
  document.body.classList.toggle('folded', f); $('#foldBtn').textContent = f ? 'EXPAND' : 'FOLD';
  try { localStorage.setItem('rx5-folded', f ? '1' : '0'); } catch (e) {}
  fit();
}

/* ================= the loupe (🔍 ZOOM): any part of the panel at 3x; what you tap in it is pressed on the panel ================= */
const LOUPE = 3;
let loupeAt = null;
function showLoupe(cx, cy){
  const L = $('#loupe'), vw = innerWidth, vh = innerHeight;
  const lw = Math.round(Math.min(vw - 16, 460)), lh = Math.round(Math.min(vh * .62, 300)), z = scale * LOUPE;
  const px = (cx - panelBox.x) / scale, py = (cy - panelBox.y) / scale + (H - panelBox.h / scale > 1 ? 135 : 0);   // panel coordinates
  const r = P().getBoundingClientRect(); loupeAt = {px:(cx - r.left) / scale, py:(cy - r.top) / scale, z, lw, lh};
  Object.assign(L.style, {display:'block', width:lw + 'px', height:lh + 'px',
    left:Math.round(clamp(cx - lw / 2, 8, vw - lw - 8)) + 'px', top:Math.round(clamp(cy - lh - 24, 8, vh - lh - 8)) + 'px'});
  if (cy - lh - 24 < 8) L.style.top = Math.round(clamp(cy + 24, 8, vh - lh - 8)) + 'px';
  void px; void py;
  paintLoupe();
}
function paintLoupe(){
  if (!loupeAt) return;
  const inn = $('#loupeIn'); inn.innerHTML = '';
  const c = P().cloneNode(true); c.removeAttribute('id'); c.classList.add('pclone');
  c.style.transform = `translate(${loupeAt.lw / 2 - loupeAt.px * loupeAt.z}px, ${loupeAt.lh / 2 - loupeAt.py * loupeAt.z}px) scale(${loupeAt.z})`;
  inn.appendChild(c);
}
function hideLoupe(){ const L = $('#loupe'); if (L) L.style.display = 'none'; loupeAt = null; }
// a tap inside the loupe: the same pointer events on the real panel, at the point under it
function loupeEvent(e, type){
  const L = $('#loupe'), lr = L.getBoundingClientRect(), r = P().getBoundingClientRect();
  const px = loupeAt.px + (e.clientX - lr.left - loupeAt.lw / 2) / loupeAt.z, py = loupeAt.py + (e.clientY - lr.top - loupeAt.lh / 2) / loupeAt.z;
  const x = r.left + px * scale, y = r.top + py * scale;
  L.style.pointerEvents = 'none'; const t = document.elementFromPoint(x, y); L.style.pointerEvents = '';
  if (!t || !t.closest('#p')) return;
  const ev = new PointerEvent(type, {bubbles:true, cancelable:true, clientX:x, clientY:y, pointerId:e.pointerId}); ev.__loupe = true;
  Object.defineProperty(ev, '__loupe', {value:true});
  t.dispatchEvent(ev);
  if (type === 'pointerup') setTimeout(paintLoupe, 30);
}
function wireRail(){
  buildRail();
  const L = $('#loupe');
  L.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); loupeEvent(e, 'pointerdown'); });
  L.addEventListener('pointerup', e => { e.stopPropagation(); loupeEvent(e, 'pointerup'); });
  // with the loupe open, a tap anywhere else (outside the panel) closes it
  addEventListener('pointerdown', e => { if (loupeAt && !e.target.closest('#loupe') && !e.target.closest('#p') && !e.target.closest('#rail')) hideLoupe(); }, true);
  let f = false; try { f = localStorage.getItem('rx5-folded') === '1'; } catch (e) {}
  setFold(f);
}

/* ================= go ================= */
await loadSaved();
buildPanel(); wire(); wireRail(); fit(); paintSliders(); setSlider('DATA', 0.5);
if (!kitLoaded){ flash('LOADING KIT', 'RX5 waveforms', 60000); kitReady.then(roms => { unflash(); paintAll(); if (roms.some(r => !r)) toast('Some kit waveforms failed to load'); }); } else paintAll();
loadSlotInfo(); midiInit();
setTimeout(fit, 300);
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidFive) navigator.serviceWorker.register('sw.js').catch(() => {});
