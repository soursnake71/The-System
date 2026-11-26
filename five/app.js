/* Omega Five: faceplate, LCD menus, AudioWorklet host, MIDI, files. ES module. */
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
  [200, [['EDIT VOICE', 'voice', [['01 SELECT VOICE', 1], ['02 PITCH', 2], ['03 EG', 3.1], [[['( 1 AR', 3.1], [' 2 D1R', 3.2], [' 3 D1L', 3.3]]], [[['  4 D2R', 3.4], [' 5 RR', 3.5], [' 6 GT)', 3.6]]], ['04 BEND', 4.1], [[['( 1 RATE', 4.1], [' 2 RANGE)', 4.2]]]]]]],
  [292, [[' ', 'voice', [['05 VOICE LEVEL', 5], ['06 LOOP', 6], ['07 STORE VOICE', 7], ['08 INITIALIZE VOICE', 8], ['09 RECALL EDIT', 9], ['10 REVERSE', 10], ['11 LOAD WAV', 11]]]]],
  [372, [['CHAIN', 'chain', [['01 PLAY', 1], ['02 EDIT', 2], ['03 CHAIN NAME', 3], ['04 CLEAR', 4]]]]],
  [430, [['UTILITY', 'util', [['01 REMAINING PATTERN', 1], ['02 REMAINING SONG', 2], ['03 MEMORY PROTECT', 3], ['04 CHANGE VOICE', 4], ['05 CLEAR ALL PATTERNS', 5], ['06 CLEAR ALL SONGS', 6]]]]],
  [521, [['KEY ASSIGN', 'key', [['01 VOICE ASSIGN', 1], ['02 PAN', 2], ['03 MULTI VOICE', 3], ['04 MULTI STEP', 4], ['05 ACCENT LEVEL', 5], ['06 OUTPUT CH ASSIGN', 6]]]]],
  [604, [[' ', 'key', [['07 SAVE KEY DATA', 7], ['08 LOAD KEY DATA', 8], ['09 COPY VOICE', 9], ['10 REV SEND', 10], ['11 DLY SEND', 11]]]]],
  [688, [['CASSETTE/CARTRIDGE', 'cart', [['01-04 SAVE SLOT', 1], ['11-14 LOAD SLOT', 11], ['21 SAVE FILE', 21], ['22 LOAD FILE', 22], ['23 LOAD WAV VOICE', 23], ['24 VOICE BANK', 24], ['30 FORMAT SLOT', 30]]]]],
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
function makeSlider(id, left, top, h){
  const s = el('div', 'slider', {left:left + 'px', top:top + 'px', height:h + 'px'});
  const cap = el('div', 'cap'); s.appendChild(cap);
  s.dataset.s = id; $('#sliders').appendChild(s);
  sliders[id] = {el:s, cap, h, v:0};
}
function setSlider(id, v){ const s = sliders[id]; if (!s) return; s.v = Math.max(0, Math.min(1, v)); s.cap.style.top = (22 + (s.h - 40) * (1 - s.v)) + 'px'; }

/* ================= fit to the screen ================= */
let scale = 1;
function fit(){
  const vw = innerWidth, vh = innerHeight; scale = Math.min(vw / W, vh / H);
  P().style.transform = `translate(${Math.round((vw - W * scale) / 2)}px, ${Math.round((vh - H * scale) / 2)}px) scale(${scale})`;
  $('#hint').style.display = vh > vw * 1.2 ? 'block' : 'none';
}

/* ================= modules ================= */
const SQ = await import('./seq.js');
let ENG = {}; try { ENG = await import('./engine.js'); } catch (e) { console.warn('engine.js', e.message); }
const opt = async f => { try { return await import(f); } catch (e) { return null; } };
const RXS = await opt('./rxsysex.js'), RXC = await opt('./rxcart.js');
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
const FALLBACK_VOICE = {pitch:0, level:31, ar:99, d1r:40, d1l:60, d2r:40, rr:60, gate:400, bendRate:0, bendRange:0, reverse:0, loop:0};
function voiceDefault(v){
  const D = ENG.VOICE_DEFAULTS;
  const d = D ? (Array.isArray(D) ? D[v] : typeof D === 'function' ? D(v) : D[v] || D) : null;
  return Object.assign({}, FALLBACK_VOICE, d || {});
}
const STEREO = [8, 9, 9, 12, 10, 6, 4, 13, 3, 11, 5, 8]; // hardware stereo positions, 15 = left .. 1 = right
function chanDefault(ch){
  const D = ENG.CHANNEL_DEFAULTS;
  const d = D ? (Array.isArray(D) ? D[ch] : D) : null;
  return Object.assign({fader:0.8, pan:(8 - STEREO[ch]) / 7, sendRev:0.12, sendDly:0, mute:false}, d || {});
}
const FX_FALLBACK = {
  rev:{program:0, time:2.2, hiRatio:0.6, delay:20, diffusion:7, density:80, erLevel:0.6, revLevel:0.8, hpf:60, lpf:9000, gateTime:250},
  dly:{timeL:375, timeR:375, sync:0, feedback:35, pingpong:0, hiCut:8000, loCut:120, modRate:0.5, modDepth:0.1, duckThresh:-24, duckRelease:200, spread:0.8, mix:0.3}};
function fxDefault(kind){ const D = ENG.FX_DEFAULTS && ENG.FX_DEFAULTS[kind]; return Object.assign({}, FX_FALLBACK[kind], D || {}); }
const REV_PROGRAMS = ENG.REV_PROGRAMS || ['HALL1', 'HALL2', 'ROOM1', 'ROOM2', 'VOCAL', 'PLATE1', 'PLATE2', 'ER1', 'ER2', 'GATE', 'REVERSE GATE'];
const DLY_SYNC = SQ.DLY_SYNC || ['off', '1/4', '1/8', '1/8.', '1/16', '1/4T'];
const RX5_NOTES = SQ.RX5_NOTES || (SQ.NOTE_PRESETS && SQ.NOTE_PRESETS.rx5) || null;
const NOTE_PRESETS = SQ.NOTE_PRESETS || {gm:SQ.MIDI_GM_NOTES, ...(RX5_NOTES ? {rx5:RX5_NOTES} : {})};

/* ================= state ================= */
let S = SQ.newState();
// panel settings that are not part of the cartridge
const U = {mode:'pattern', ptn:0, song:0, chain:0, voice:0, ch:0, pad:0, page:'ptn', menu:null, item:{}, songStep:0, chainStep:0, stepPos:0, stepVoice:0,
  padMap:Array.from({length:24}, (_, i) => i), multi:{on:false, v:0, step:100}, padVel:0, protect:false, stored:{},
  midiIn:'all', midiOut:'', chMsg:true, egVel:true, gate:100, bulk:0, syx:0, rnd:0, exp:0, impAt:-1, slot:0, tunable:false};
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
function allChanged(){ post({t:'state', state:S}); dirty(); paintSliders(); }
function protectedMsg(){ if (U.protect){ flash('MEMORY PROTECTED', 'UTILITY 03: OFF'); return true; } return false; }

/* ================= audio: the worklet ================= */
let ctx = null, node = null, audioReady = null, pos = null, meter = {peak:[0, 0], ch:[], max:0};
const queue = [];
function post(m){ if (node) node.port.postMessage(m); else if (m.t !== 'state') queue.push(m); }
function audio(){
  if (audioReady) { if (ctx && ctx.state !== 'running') ctx.resume().catch(() => {}); return audioReady; }
  audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    await ctx.audioWorklet.addModule('worklet.js');
    node = new AudioWorkletNode(ctx, 'omega-five', {numberOfInputs:0, outputChannelCount:[2], processorOptions:{state:S}});
    node.connect(ctx.destination);
    node.port.onmessage = ev => fromWorklet(ev.data);
    queue.length = 0;
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
let lcdText = [L16('OMEGA FIVE'), L16('')], lcdCur = -1, flashT = 0, flashTx = null;
function setLcd(a, b, cur = -1){
  a = L16(a); b = L16(b); lcdText = [a, b]; lcdCur = cur;
  for (const r of [0, 1]){ const cells = $('#lcd' + r).children, s = r ? b : a; for (let i = 0; i < 16; i++){ const c = cells[i]; if (c.textContent !== s[i]) c.textContent = s[i]; c.classList.toggle('cur', r === 1 && i === cur); } }
}
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
  return {no, name, ctx:opts.ctx, l2:opts.l2 || (() => ' sure? NO / YES'), yes:run, inc:opts.inc, data:opts.data, pad:opts.pad};
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
    vItem(2, 'PITCH', 'pitch', -3600, 2400, {step:100, fine:10, fmt:fmtCents}),
    vItem(3.1, 'EG AR', 'ar', 0, 99), vItem(3.2, 'EG D1R', 'd1r', 0, 99), vItem(3.3, 'EG D1L', 'd1l', 0, 99), vItem(3.4, 'EG D2R', 'd2r', 0, 99), vItem(3.5, 'EG RR', 'rr', 0, 99),
    vItem(3.6, 'EG GATE', 'gate', 10, 6500, {step:10, fine:1, fmt:x => p3(Math.round(x)).padStart(4, '0') + ' ms', curve:2}),
    vItem(4.1, 'BEND RATE', 'bendRate', 0, 99), vItem(4.2, 'BEND RANGE', 'bendRange', -6000, 6000, {step:100, fine:10, fmt:fmtCents}),
    vItem(5, 'VOICE LEVEL', 'level', 0, 31), vBool(6, 'LOOP', 'loop'),
    actItem(7, 'STORE VOICE', () => { U.stored[U.voice] = Object.assign(voiceDefault(U.voice), S.voice[U.voice]); dirty(); flash('STORED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    actItem(8, 'INIT VOICE', () => { S.voice[U.voice] = voiceDefault(U.voice); post({t:'voiceAll', v:U.voice, p:S.voice[U.voice]}); dirty(); flash('INITIALIZED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    actItem(9, 'RECALL EDIT', () => { const s = U.stored[U.voice]; if (!s){ flash('NOTHING STORED', vname(U.voice)); return; } S.voice[U.voice] = Object.assign({}, s); post({t:'voiceAll', v:U.voice, p:S.voice[U.voice]}); dirty(); flash('RECALLED', vname(U.voice)); }, {ctx:vctx, pad:padSelVoice}),
    vBool(10, 'REVERSE', 'reverse'),
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
    {no:1, name:'VOICE ASSIGN', ctx:() => 'key' + String.fromCharCode(65 + U.pad), l2:() => ':' + (S.samples[U.padMap[U.pad]] ? 'Usr-' : 'Int-') + vname(U.padMap[U.pad]), pad:i => { U.pad = i; },
      inc:d => { U.padMap[U.pad] = (U.padMap[U.pad] + d + 24) % 24; dirty(); }, data:x => { U.padMap[U.pad] = Math.round(x * 23); dirty(); }},
    numItem(2, 'PAN', () => cget(U.ch, 'pan'), x => cset(U.ch, 'pan', x), -1, 1, {step:1 / 7, fmt:fmtPan, ctx:cctx, pad:padSelCh}),
    {no:3, name:'MULTI VOICE', l2:() => U.multi.on ? vname(U.multi.v) + ' A-X' : 'OFF', inc:d => { const k = (U.multi.on ? U.multi.v + 1 : 0) + d; U.multi.on = k > 0 && k <= 24; if (U.multi.on) U.multi.v = (k - 1 + 24) % 24; dirty(); paintLeds(); },
      pad:(i, v) => { if (U.multi.on) return; }},
    numItem(4, 'MULTI STEP', () => U.multi.step, x => { U.multi.step = x; dirty(); }, 10, 1200, {step:10, fmt:x => x + ' cent'}),
    numItem(5, 'ACCENT LEVEL', () => U.padVel, x => { U.padVel = x; dirty(); }, 0, 127, {fmt:x => x ? 'pad vel ' + x : 'pad vel: TOUCH'}),
    listItem(6, 'OUTPUT CH', () => cget(U.ch, 'mute') ? 1 : 0, x => cset(U.ch, 'mute', !!x), ['ON', 'MUTE'], {ctx:cctx, pad:padSelCh}),
    {no:7, name:'SAVE KEY DATA', l2:() => 'set ' + (U.slot % 3 + 1) + '  YES', noFn:() => { const d = 1; U.slot = (U.slot + d + 3) % 3; }, yes:() => { try { localStorage.setItem('o5key' + U.slot % 3, JSON.stringify({padMap:U.padMap, multi:U.multi})); flash('KEY DATA SAVED', 'set ' + (U.slot % 3 + 1)); } catch (e) {} }},
    {no:8, name:'LOAD KEY DATA', l2:() => 'set ' + (U.slot % 3 + 1) + '  YES', noFn:() => { const d = 1; U.slot = (U.slot + d + 3) % 3; }, yes:() => { try { const k = JSON.parse(localStorage.getItem('o5key' + U.slot % 3)); if (!k) throw 0; U.padMap = k.padMap; U.multi = k.multi; dirty(); paintLeds(); flash('KEY DATA LOADED', 'set ' + (U.slot % 3 + 1)); } catch (e) { flash('NO KEY DATA', 'set ' + (U.slot % 3 + 1)); } }},
    {no:9, name:'COPY VOICE', ctx:vctx, pad:padSelVoice, l2:() => '>' + vname(U.copyTo || 0) + ' YES', noFn:() => { const d = 1; U.copyTo = ((U.copyTo || 0) + d + 24) % 24; },
      yes:() => { const to = U.copyTo || 0; S.voice[to] = Object.assign(voiceDefault(U.voice), S.voice[U.voice]); post({t:'voiceAll', v:to, p:S.voice[to]}); dirty(); flash('COPIED', vname(U.voice) + '>' + vname(to)); }},
    numItem(10, 'REV SEND', () => cget(U.ch, 'sendRev'), x => cset(U.ch, 'sendRev', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
    numItem(11, 'DLY SEND', () => cget(U.ch, 'sendDly'), x => cset(U.ch, 'sendDly', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
  ],
  cart:[
    ...[0, 1, 2, 3].map(i => ({no:1 + i, name:'SAVE SLOT ' + (i + 1), l2:() => slotInfo(i, 'SAVE'), yes:() => slotSave(i)})),
    ...[0, 1, 2, 3].map(i => ({no:11 + i, name:'LOAD SLOT ' + (i + 1), l2:() => slotInfo(i, 'LOAD'), yes:() => slotLoad(i)})),
    actItem(21, 'SAVE FILE', () => saveCartFile(), {l2:() => 'YES: .o5cart'}),
    actItem(22, 'LOAD FILE', () => pickFile('.o5cart,.json,application/json,application/gzip', loadCartFile), {l2:() => 'YES: pick a file'}),
    actItem(23, 'LOAD WAV', () => pickFile('audio/*,.wav,.aif,.aiff', loadWav), {ctx:vctx, pad:padSelVoice, l2:() => 'YES: into voice'}),
    {no:24, name:'VOICE BANK', l2:() => ['LOAD .bin', 'SAVE .bin'][U.bankDir | 0] + ' YES', noFn:() => { const d = 1; U.bankDir = (U.bankDir | 0) ? 0 : 1; }, yes:() => voiceBank(U.bankDir | 0)},
    {no:30, name:'FORMAT SLOT', l2:() => 'slot ' + (U.slot % 4 + 1) + ' NO/YES', noFn:() => { const d = 1; U.slot = (U.slot + d + 4) % 4; }, yes:async () => { await IDB.del('cart' + U.slot % 4); slotCache[U.slot % 4] = null; flash('SLOT ' + (U.slot % 4 + 1), 'FORMATTED'); }},
  ],
  midi:[
    listItem(1, 'CH MESSAGE', () => U.chMsg ? 1 : 0, x => { U.chMsg = !!x; dirty(); }, onoff),
    {no:2, name:'RECEIVE CH', l2:() => U.tunable ? 'TUNABLE ' + (S.midi.rxCh || 1) : S.midi.rxCh ? 'ch ' + p2(S.midi.rxCh) : 'OMNI', inc:d => { S.midi.rxCh = clamp((S.midi.rxCh | 0) + d, 0, 16); dirty(); }, data:x => { S.midi.rxCh = Math.round(x * 16); dirty(); }},
    {no:3, name:'TRANSMIT CH', l2:() => 'ch ' + p2(S.midi.txCh || 10), inc:d => { S.midi.txCh = clamp((S.midi.txCh || 10) + d, 1, 16); dirty(); }, data:x => { S.midi.txCh = 1 + Math.round(x * 15); dirty(); }},
    {no:4.1, name:'NOTE VOICE', ctx:vctx, pad:padSelVoice, l2:() => 'note ' + noteName(S.noteMap[U.voice]) + ' ' + p3(S.noteMap[U.voice]) + ' lrn',
      inc:(d, sh) => { S.noteMap[U.voice] = clamp(S.noteMap[U.voice] + d * (sh ? 12 : 1), 0, 127); dirty(); }, data:x => { S.noteMap[U.voice] = Math.round(x * 127); dirty(); }},
    {no:4.2, name:'NOTE PITCH', l2:() => U.tunable ? 'ON ' + vname(U.tuneV | 0) + ' 36-96' : 'OFF (voice mode)', inc:d => { U.tunable = d > 0; dirty(); }, ctx:null,
      pad:(i, v) => { U.tuneV = v; dirty(); }},
    {no:5, name:'INIT NOTE', l2:() => 'preset ' + (U.notePreset ? 'GM' : 'RX5') + ' YES', noFn:() => { U.notePreset = U.notePreset ? 0 : 1; },
      yes:() => { const src = U.notePreset ? NOTE_PRESETS.gm : (NOTE_PRESETS.rx5 || NOTE_PRESETS.gm); S.noteMap = src.slice(); dirty(); flash('NOTES INITIALIZED', U.notePreset ? 'GM' : 'RX5'); }},
    numItem(6, 'EG VELOCITY', () => U.egVel | 0, x => { U.egVel = x; dirty(); }, 0, 4, {fmt:x => x ? p2(x) : 'OFF'}),
    numItem(7, 'GATE TIME', () => U.gate, x => { U.gate = x; dirty(); }, 10, 1000, {step:10, fmt:x => (x / 1000).toFixed(2) + ' s'}),
    numItem(8, 'DEVICE NUMBER', () => S.midi.deviceNo | 0, x => { S.midi.deviceNo = x; dirty(); }, 0, 16, {fmt:x => x ? p2(x) : 'OFF'}),
    {no:9, name:'RECEIVE BULK', l2:() => U.bulkWait ? 'waiting...  NO' : 'YES: wait', yes:() => { U.bulkWait = true; flash('RECEIVE BULK', 'waiting for data'); }, noFn:() => { U.bulkWait = false; }},
    {no:10, name:'TRANSMIT BULK', l2:() => bulkName() + ' YES', noFn:() => { const d = 1; U.bulk = (U.bulk + d + 4) % 4; }, yes:() => transmitBulk()},
    listItem(11, 'CLOCK OUT', () => S.midi.clockOut ? 1 : 0, x => { S.midi.clockOut = !!x; post({t:'midiOut', clock:!!S.midi.clockOut, notes:!!S.midi.notesOut}); dirty(); }, onoff),
    listItem(12, 'NOTES OUT', () => S.midi.notesOut ? 1 : 0, x => { S.midi.notesOut = !!x; post({t:'midiOut', clock:!!S.midi.clockOut, notes:!!S.midi.notesOut}); dirty(); }, onoff),
    {no:13, name:'INPUT PORT', l2:() => portLabel(0), inc:d => portInc(0, d)},
    {no:14, name:'OUTPUT PORT', l2:() => portLabel(1), inc:d => portInc(1, d)},
    {no:15, name:'SYX FILE', l2:() => (U.syx ? 'SAVE ' + bulkName() : 'LOAD .syx') + ' YES', noFn:() => { U.syx = U.syx ? 0 : 1; if (U.syx) U.bulk = (U.bulk + 1) % 4; }, yes:() => U.syx ? saveSyx() : pickFile('.syx,application/octet-stream', loadSyx)},
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
    listItem(1, 'PROGRAM', () => fget('rev', 'program'), x => fset('rev', 'program', x), REV_PROGRAMS),
    numItem(2, 'REV TIME', () => fget('rev', 'time'), x => fset('rev', 'time', x), 0.3, 30, {step:0.1, fmt:x => x.toFixed(1) + ' s', curve:2}),
    numItem(3, 'HIGH RATIO', () => fget('rev', 'hiRatio'), x => fset('rev', 'hiRatio', x), 0.1, 1, {step:0.1, fmt:x => x.toFixed(1)}),
    numItem(4, 'DELAY', () => fget('rev', 'delay'), x => fset('rev', 'delay', x), 0, 200, {fmt:x => x.toFixed(0) + ' ms'}),
    numItem(5, 'DIFFUSION', () => fget('rev', 'diffusion'), x => fset('rev', 'diffusion', x), 0, 10),
    numItem(6, 'DENSITY', () => fget('rev', 'density'), x => fset('rev', 'density', x), 0, 100, {fmt:x => x + ' %'}),
    numItem(7, 'ER LEVEL', () => fget('rev', 'erLevel'), x => fset('rev', 'erLevel', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(8, 'REV LEVEL', () => fget('rev', 'revLevel'), x => fset('rev', 'revLevel', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(9, 'HPF', () => fget('rev', 'hpf'), x => fset('rev', 'hpf', x), 20, 1000, {step:10, fmt:x => x < 25 ? 'THRU' : x + ' Hz', curve:2}),
    numItem(10, 'LPF', () => fget('rev', 'lpf'), x => fset('rev', 'lpf', x), 1000, 18000, {step:500, fmt:x => x >= 18000 ? 'THRU' : (x / 1000).toFixed(1) + ' kHz', curve:2}),
    numItem(11, 'GATE TIME', () => fget('rev', 'gateTime'), x => fset('rev', 'gateTime', x), 10, 500, {step:10, fmt:x => x + ' ms'}),
    numItem(12, 'REV SEND', () => cget(U.ch, 'sendRev'), x => cset(U.ch, 'sendRev', x), 0, 1, {step:0.01, fmt:pct, ctx:cctx, pad:padSelCh}),
  ],
  dly:[
    numItem(1, 'TIME L', () => fget('dly', 'timeL'), x => fset('dly', 'timeL', x), 1, 2000, {step:1, fmt:x => x + ' ms', curve:2}),
    numItem(2, 'TIME R', () => fget('dly', 'timeR'), x => fset('dly', 'timeR', x), 1, 2000, {step:1, fmt:x => x + ' ms', curve:2}),
    listItem(3, 'TEMPO SYNC', () => fget('dly', 'sync'), x => fset('dly', 'sync', x), DLY_SYNC.map(s => s === 'off' ? 'OFF' : s)),
    numItem(4, 'FEEDBACK', () => fget('dly', 'feedback'), x => fset('dly', 'feedback', x), 0, 120, {fmt:x => x + ' %'}),
    listItem(5, 'PING PONG', () => fget('dly', 'pingpong') ? 1 : 0, x => fset('dly', 'pingpong', x), onoff),
    numItem(6, 'HI CUT', () => fget('dly', 'hiCut'), x => fset('dly', 'hiCut', x), 1000, 20000, {step:500, fmt:x => (x / 1000).toFixed(1) + ' kHz', curve:2}),
    numItem(7, 'LO CUT', () => fget('dly', 'loCut'), x => fset('dly', 'loCut', x), 20, 2000, {step:10, fmt:x => x + ' Hz', curve:2}),
    numItem(8, 'MOD RATE', () => fget('dly', 'modRate'), x => fset('dly', 'modRate', x), 0, 10, {step:0.05, fmt:x => x.toFixed(2) + ' Hz'}),
    numItem(9, 'MOD DEPTH', () => fget('dly', 'modDepth'), x => fset('dly', 'modDepth', x), 0, 1, {step:0.01, fmt:pct}),
    numItem(10, 'DUCK THRESH', () => fget('dly', 'duckThresh'), x => fset('dly', 'duckThresh', x), -60, 0, {fmt:x => x <= -60 ? 'OFF' : x + ' dB'}),
    numItem(11, 'DUCK RELEASE', () => fget('dly', 'duckRelease'), x => fset('dly', 'duckRelease', x), 10, 2000, {step:10, fmt:x => x + ' ms', curve:2}),
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
