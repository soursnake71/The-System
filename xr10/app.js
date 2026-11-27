// Omega XR10 page: the faceplate photo with live controls on top, and the RX5-style panel underneath.
// All the machine's logic is in the C++ engine (xr10.wasm), run by worklet.js.
'use strict';
const $ = id => document.getElementById(id);
const W = 800, H = 549;   // faceplate photo size: every position below is in its pixels

// button ids (same order as the engine's enum)
const B = { MODE: 0, ERASE: 1, EDIT: 2, ACCENT: 3, PADBANK: 4, TC: 5, INTRO: 6, BREAK: 7, START: 8, LEFT: 9, RIGHT: 10, UP: 11, DOWN: 12,
  MINUS: 13, PLUS: 14, ENTER: 15, D0: 16, PAD1: 26, M_PATTERN: 41, M_SONG: 42, M_SOUND: 43, M_UTILITY: 44 };
const K = { TEMPO: 0, PATTERN: 1, PART: 2, REC: 3, LEN: 4, BEATS: 5, SWING: 6, CLICK: 7, COUNTIN: 8, TC: 9, ACCENT: 10, BANK: 11, FXTYPE: 12,
  FXTIME: 13, FXLEVEL: 14, POLY: 15, MIDICH: 16, MIDIMAP: 17, SYNC: 18, SONG: 19, SOUND: 20, PLAYING: 21, VOLUME: 22, MODE: 23, SUB: 24,
  CLICKLVL: 25, SONGSTEP: 26, PADVEL: 27, ACCENTVEL: 28, USERSAMPLE: 29, MAINVAR: 30, BAR: 31, EFFECTVR: 32 };
const PARTS = ['VAR A', 'VAR B', 'VAR C', 'FILL A', 'FILL B', 'FILL C', 'INTRO', 'ENDING'];
const PART_CODES = ['A', 'B', 'C', 'FA', 'FB', 'FC', 'IN', 'EN'];

// --------------------------------------------------- faceplate hot spots (centre x, y, width, height)
const hot = [];
const addHot = (id, x, y, w, h, kind = 'btn') => hot.push({ id, x, y, w, h, kind });
// cursor row and keypad
[[B.LEFT, 479], [B.RIGHT, 532], [B.UP, 584], [B.DOWN, 636], [B.MINUS, 687], [B.PLUS, 727]].forEach(([id, x]) => addHot(id, x, 292, x > 670 ? 34 : 42, 17));
for (let d = 0; d < 10; d++) addHot(B.D0 + d, [427, 479, 532, 584, 636][d % 5], d < 5 ? 328 : 357, 42, 17);
addHot(B.ENTER, 707, 327, 73, 17);
// big keys: columns left to right, rows top to bottom
const colX = [90, 177, 265, 354, 442, 532, 619, 707], rowY = [405, 454, 503];
const layout = [
  [B.MODE, B.EDIT, B.PADBANK], [B.ERASE, B.ACCENT, B.TC],
  [B.PAD1 + 2, B.PAD1 + 1, B.PAD1], [B.PAD1 + 5, B.PAD1 + 4, B.PAD1 + 3], [B.PAD1 + 8, B.PAD1 + 7, B.PAD1 + 6],
  [B.PAD1 + 11, B.PAD1 + 10, B.PAD1 + 9], [B.PAD1 + 14, B.PAD1 + 13, B.PAD1 + 12], [B.INTRO, B.BREAK, B.START]];
layout.forEach((col, c) => col.forEach((id, r) => addHot(id, colX[c], rowY[r], 73, 37, id >= B.PAD1 && id < B.PAD1 + 15 ? 'pad' : 'btn')));
// the mode lights double as buttons
addHot(B.M_PATTERN, 497, 210, 35, 15); addHot(B.M_SONG, 532, 210, 35, 15); addHot(B.M_SOUND, 497, 231, 35, 15); addHot(B.M_UTILITY, 532, 231, 35, 15);
const LEDS = [[497, 210], [532, 210], [497, 231], [532, 231]];
const LCD = { x: 581, y: 205, w: 136, h: 33 };
const KNOBS = [{ id: 'vol', x: 123, y: 217, r: 38.5 }, { id: 'data', x: 252, y: 217, r: 38.5 }];

const face = $('face');
const pct = (v, of) => (v / of * 100) + '%';
function place(el, x, y, w, h) { el.style.left = pct(x - w / 2, W); el.style.top = pct(y - h / 2, H); el.style.width = pct(w, W); el.style.height = pct(h, H); }
const ledEls = LEDS.map(([x, y]) => { const d = document.createElement('div'); d.className = 'led off'; place(d, x, y, 23, 5); face.appendChild(d); return d; });
const hotEls = {};
for (const h of hot) {
  const d = document.createElement('div');
  d.className = 'hot';
  place(d, h.x, h.y, h.w, h.h);
  const down = e => {
    e.preventDefault();
    d.setPointerCapture?.(e.pointerId);
    d.classList.add('down');
    if (h.kind === 'pad') {
      const r = d.getBoundingClientRect();
      const v = Math.round(127 - 70 * Math.min(1, Math.max(0, (e.clientY - r.top) / r.height)));
      send({ t: 'pad', i: h.id - B.PAD1, d: 1, v });
    } else send({ t: 'btn', b: h.id, d: 1 });
  };
  const up = e => {
    if (!d.classList.contains('down')) return;
    d.classList.remove('down');
    if (h.kind === 'pad') send({ t: 'pad', i: h.id - B.PAD1, d: 0 }); else send({ t: 'btn', b: h.id, d: 0 });
  };
  d.addEventListener('pointerdown', down);
  d.addEventListener('pointerup', up);
  d.addEventListener('pointercancel', up);
  d.addEventListener('contextmenu', e => e.preventDefault());
  face.appendChild(d);
  hotEls[h.id] = d;
}
// knobs: a cap over the photo's knob with a pointer that turns
const knobState = { vol: 80, data: 0 };
const knobCaps = {};
for (const k of KNOBS) {
  const cap = document.createElement('div');
  cap.className = 'knobcap';
  place(cap, k.x, k.y, k.r * 2 * 0.86, k.r * 2 * 0.86 * W / H * (H / W));
  cap.style.height = 'auto'; cap.style.aspectRatio = '1';
  cap.style.background = 'radial-gradient(circle at 40% 35%, #ffffff 0%, #eceef6 45%, #d5d7e2 80%, #b9bccb 100%)';
  cap.innerHTML = '<i></i>';
  face.appendChild(cap);
  knobCaps[k.id] = cap;
  const grab = document.createElement('div');
  grab.className = 'knob';
  place(grab, k.x, k.y, k.r * 2.3, k.r * 2.3);
  grab.style.height = 'auto'; grab.style.aspectRatio = '1';
  let lastY = 0, lastX = 0, acc = 0;
  grab.addEventListener('pointerdown', e => { e.preventDefault(); grab.setPointerCapture(e.pointerId); lastY = e.clientY; lastX = e.clientX; acc = 0; });
  grab.addEventListener('pointermove', e => {
    if (!grab.hasPointerCapture(e.pointerId)) return;
    const d = (lastY - e.clientY) + (e.clientX - lastX);
    lastY = e.clientY; lastX = e.clientX;
    if (k.id === 'vol') {
      knobState.vol = Math.max(0, Math.min(100, knobState.vol + d * 0.4));
      send({ t: 'set', k: K.VOLUME, v: Math.round(knobState.vol) });
    } else {
      acc += d;
      while (Math.abs(acc) >= 7) { const s = Math.sign(acc); acc -= s * 7; knobState.data += s; send({ t: 'knob', d: s }); }
    }
    drawKnobs();
  });
  grab.addEventListener('wheel', e => {
    e.preventDefault();
    const s = e.deltaY < 0 ? 1 : -1;
    if (k.id === 'vol') { knobState.vol = Math.max(0, Math.min(100, knobState.vol + s * 3)); send({ t: 'set', k: K.VOLUME, v: Math.round(knobState.vol) }); }
    else { knobState.data += s; send({ t: 'knob', d: s }); }
    drawKnobs();
  }, { passive: false });
  face.appendChild(grab);
}
function drawKnobs() {
  knobCaps.vol.style.transform = `rotate(${-135 + knobState.vol * 2.7}deg)`;
  knobCaps.data.style.transform = `rotate(${knobState.data * 12}deg)`;
}
drawKnobs();

// LCD
const lcd = $('lcd');
place(lcd, LCD.x + LCD.w / 2, LCD.y + LCD.h / 2, LCD.w, LCD.h);
let lcdText = ['', ''];
function drawLcd() {
  const r = lcd.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
  if (lcd.width !== w || lcd.height !== h) { lcd.width = w; lcd.height = h; }
  const c = lcd.getContext('2d');
  const css = getComputedStyle(document.documentElement);
  c.fillStyle = css.getPropertyValue('--lcd') || '#2f3b42';
  c.fillRect(0, 0, w, h);
  const cw = w / 16.4, chh = h / 2.15;
  c.font = `bold ${Math.round(chh * 0.95)}px "Courier New", ui-monospace, monospace`;
  c.textBaseline = 'middle'; c.textAlign = 'center';
  for (let row = 0; row < 2; row++) {
    const s = (lcdText[row] || '').padEnd(16);
    for (let i = 0; i < 16; i++) {
      const x = cw * 0.2 + cw * (i + 0.5), y = h * (row === 0 ? 0.28 : 0.74);
      c.fillStyle = 'rgba(180,200,198,0.07)'; c.fillRect(x - cw * 0.44, y - chh * 0.47, cw * 0.88, chh * 0.94);
      c.fillStyle = '#b4c8c6'; c.fillText(s[i], x, y + 1);
    }
  }
}
const BUILD = 5;   // shown on the panel at start, so you can tell which version is running
// the whole panel stays on screen: as wide as the page, but never taller than the window (a phone on its side)
function fitFace() {
  const avail = Math.max(200, window.innerHeight - 4);
  const w = Math.min(face.parentElement.clientWidth, 1100, avail * W / H);
  face.style.width = Math.floor(w) + 'px';
  face.style.maxWidth = 'none';
  drawLcd();
}
window.addEventListener('resize', fitFace);
window.addEventListener('orientationchange', () => setTimeout(fitFace, 250));
if (window.visualViewport) window.visualViewport.addEventListener('resize', fitFace);
fitFace();
try { $('buildno').textContent = 'Omega XR10, build ' + BUILD; } catch (e) { }

// --------------------------------------------------- audio and engine
let ctx = null, node = null, info = null, S = null;
const send = m => { if (node) node.port.postMessage(m); };
let ready = false;
async function power() {
  $('start').remove();
  lcdText = ['  OMEGA  XR10', ' BUILD ' + BUILD + ' LOADING']; drawLcd();
  try { await powerOn(); }
  catch (e) { lcdText = ['ERROR:', String(e && e.message || e).toUpperCase().slice(0, 16)]; drawLcd(); alert('Omega XR10 could not start its sound engine: ' + (e && e.message || e)); }
}
async function powerOn() {
  ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  await ctx.audioWorklet.addModule('worklet.js');
  node = new AudioWorkletNode(ctx, 'xr10', { numberOfInputs: 0, outputChannelCount: [2] });
  node.connect(ctx.destination);
  node.port.onmessage = e => onMsg(e.data);
  const bytes = await (await fetch('xr10.wasm')).arrayBuffer();
  let rom = null;
  try { const r = await fetch('rom.bin'); if (r.ok) rom = await r.arrayBuffer(); } catch (e) { }
  node.port.postMessage({ t: 'wasm', bytes, rom }, rom ? [bytes, rom] : [bytes]);
  if (ctx.state !== 'running') await ctx.resume();
}
$('go').addEventListener('click', power);
document.addEventListener('visibilitychange', () => { if (document.hidden) save('auto'); else if (ctx && ctx.state !== 'running') ctx.resume(); });

function onMsg(m) {
  if (m.t === 'ready') { info = m; buildPanel(); restore(); return; }
  if (m.t === 'error') { lcdText = ['ENGINE ERROR', String(m.msg).toUpperCase().slice(0, 16)]; drawLcd(); alert('Omega XR10 sound engine: ' + m.msg); return; }
  if (m.t === 'status') { S = m; update(); return; }
  if (m.t === 'patterns') { info.patterns = m.patterns; fillPatterns(); return; }
  if (m.t === 'saved') { onSaved(m); return; }
  if (m.t === 'loaded') { info.patterns = m.patterns; fillPatterns(); if (m.why === 'file') alert(m.ok ? 'Loaded.' : 'That file is not an XR10 memory file.'); return; }
  if (m.t === 'wave') { drawWave(m.w); return; }
  if (m.t === 'midiFile') { if (m.bytes) download(m.bytes, (m.song ? 'xr10-song-' + (S.song + 1) : 'xr10-' + String(S.pattern + 1).padStart(2, '0') + '-' + PART_CODES[S.part]) + '.mid', 'audio/midi'); return; }
  if (m.t === 'midiImported') {
    info.patterns = m.patterns; fillPatterns();
    alert(m.r > 0 ? `Imported ${m.r} bar${m.r > 1 ? 's' : ''} into pattern ${S.pattern + 1}, ${PARTS[S.part]}.` : m.r < 0 ? 'That is not a MIDI file.' : 'No drum notes found in that file.');
    return;
  }
  if (m.t === 'matched') { const cb = pendingMatch[m.id]; delete pendingMatch[m.id]; if (cb) cb(m.out); return; }
}

// --------------------------------------------------- memory: kept on the device (IndexedDB), and files
const DB = () => new Promise((ok, no) => { const r = indexedDB.open('omega-xr10', 1); r.onupgradeneeded = () => r.result.createObjectStore('s'); r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error); });
async function dbPut(v) { try { const d = await DB(); d.transaction('s', 'readwrite').objectStore('s').put(v, 'main'); } catch (e) { } }
async function dbGet() { try { const d = await DB(); return await new Promise(ok => { const r = d.transaction('s').objectStore('s').get('main'); r.onsuccess = () => ok(r.result); r.onerror = () => ok(null); }); } catch (e) { return null; } }
let lastSaved = '';
function save(why) { send({ t: 'save', why }); }
function onSaved(m) {
  if (!m.bytes) return;
  if (m.why === 'file') { download(m.bytes, 'omega-xr10.xr10'); return; }
  const sig = m.bytes.length + ':' + hash(m.bytes);
  if (sig !== lastSaved) { lastSaved = sig; dbPut(m.bytes); }
}
function hash(b) { let h = 2166136261; for (let i = 0; i < b.length; i += 7) h = Math.imul(h ^ b[i], 16777619); return h >>> 0; }
async function restore() {
  const b = await dbGet();
  if (b && b.byteLength) send({ t: 'load', bytes: b, why: 'restore' });
  setInterval(() => save('auto'), 5000);
}
const pendingMatch = {};
let matchId = 0;
function matchNames(names) { return new Promise(ok => { const id = ++matchId; pendingMatch[id] = ok; send({ t: 'matchNames', id, names }); }); }
// a whole folder of samples (the XR-10 set): each file goes to the sound its name says
async function loadSampleSet(files) {
  files = [...files];
  const idx = await matchNames(files.map(f => f.name));
  const done = [], skipped = [];
  for (let i = 0; i < files.length; i++) {
    if (idx[i] < 0) { skipped.push(files[i].name); continue; }
    try {
      const ab = await ctx.decodeAudioData(await files[i].arrayBuffer());
      const n = Math.min(ab.length, Math.round(ab.sampleRate * 10)), mono = new Float32Array(n);
      for (let c = 0; c < ab.numberOfChannels; c++) { const d = ab.getChannelData(c); for (let k = 0; k < n; k++) mono[k] += d[k] / ab.numberOfChannels; }
      send({ t: 'sample', s: idx[i], data: mono, rate: ab.sampleRate });
      done.push(info.names[idx[i]]);
    } catch (e) { skipped.push(files[i].name); }
  }
  setTimeout(() => { save('auto'); send({ t: 'wave', s: S.sound }); }, 300);
  alert(`Loaded ${done.length} sound${done.length === 1 ? '' : 's'}` + (done.length ? ': ' + done.join(', ') : '') + (skipped.length ? `\n\nNot used (name doesn't match a sound, or unreadable): ${skipped.join(', ')}` : '') +
    '\n\nThey are kept on this device. "Save all to a file" in Setup gives a file that can be built into the app.');
}
function download(bytes, name, mime) {
  if (window.AndroidXR10) {
    let bin = ''; for (let i = 0; i < bytes.length; i += 8192) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
    const where = AndroidXR10.save(btoa(bin), name, mime || 'application/octet-stream');
    alert('Saved to ' + where);
    return;
  }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([bytes])); a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
}

// --------------------------------------------------- RX5 panel
const tabs = [...document.querySelectorAll('#tabs button')];
tabs.forEach(b => b.addEventListener('click', () => {
  tabs.forEach(x => x.classList.toggle('on', x === b));
  for (const t of ['pads', 'voice', 'grid', 'song', 'setup', 'help']) $('tab-' + t).hidden = t !== b.dataset.tab;
}));
let gridBar = 0, waveFor = -1;
function opt(sel, items) { sel.innerHTML = ''; items.forEach(([v, t]) => { const o = document.createElement('option'); o.value = v; o.textContent = t; sel.appendChild(o); }); }
function fillPatterns() {
  opt($('patsel'), info.patterns.map((n, i) => [i, String(i + 1).padStart(2, '0') + ' ' + n + (i >= 50 ? ' (user)' : '')]));
  document.querySelectorAll('#songlist select.pp').forEach(s => { const v = s.value; opt(s, info.patterns.map((n, i) => [i, String(i + 1).padStart(2, '0') + ' ' + n])); s.value = v; });
}
function buildPanel() {
  fillPatterns();
  $('patsel').onchange = e => { send({ t: 'set', k: K.PATTERN, v: +e.target.value }); };
  $('play').onclick = () => send({ t: 'set', k: K.PLAYING, v: S && S.playing ? 0 : 1 });
  $('recbtn').onclick = () => { send({ t: 'set', k: K.REC, v: S && S.recording ? 0 : 1 }); setTimeout(() => send({ t: 'patterns' }), 100); };
  $('tempo').onchange = e => send({ t: 'set', k: K.TEMPO, v: Math.round(+e.target.value * 10) });
  let taps = [];
  $('tap').onclick = () => {
    const now = performance.now(); taps = taps.filter(t => now - t < 2500); taps.push(now);
    if (taps.length >= 2) { const iv = (taps[taps.length - 1] - taps[0]) / (taps.length - 1); send({ t: 'set', k: K.TEMPO, v: Math.round(600000 / iv) }); }
  };
  // big pads (easier than the photo on a phone)
  const bp = $('bigpads');
  for (let i = 0; i < 15; i++) {
    const b = document.createElement('button'); b.className = 'bigpad'; b.dataset.i = i;
    b.innerHTML = `<small>PAD ${i + 1}</small><span></span>`;
    b.addEventListener('pointerdown', e => { e.preventDefault(); const r = b.getBoundingClientRect(); send({ t: 'pad', i, d: 1, v: Math.round(127 - 70 * Math.min(1, Math.max(0, (e.clientY - r.top) / r.height))) }); b.classList.add('down'); });
    const up = () => { if (b.classList.contains('down')) { b.classList.remove('down'); send({ t: 'pad', i, d: 0 }); } };
    b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); b.addEventListener('pointerleave', up);
    bp.appendChild(b);
  }
  const pf = $('partpads');
  [['VAR A', B.PAD1 + 11], ['VAR B', B.PAD1 + 10], ['VAR C', B.PAD1 + 9], ['FILL A', B.PAD1 + 14], ['FILL B', B.PAD1 + 13], ['FILL C', B.PAD1 + 12],
   ['INTRO / END', B.INTRO], ['BREAK', B.BREAK], ['START / STOP', B.START], ['ACCENT', B.ACCENT], ['PAD BANK', B.PADBANK], ['TIMING', B.TC]].forEach(([n, id]) => {
    const b = document.createElement('button'); b.textContent = n; b.dataset.b = id;
    b.addEventListener('pointerdown', e => { e.preventDefault(); if (id >= B.PAD1 && id < B.PAD1 + 15) send({ t: 'pad', i: id - B.PAD1, d: 1 }); else send({ t: 'btn', b: id, d: 1 }); });
    b.addEventListener('pointerup', () => { if (id >= B.PAD1 && id < B.PAD1 + 15) send({ t: 'pad', i: id - B.PAD1, d: 0 }); else send({ t: 'btn', b: id, d: 0 }); });
    pf.appendChild(b);
  });
  // instruments
  const inst = $('inst');
  info.names.forEach((n, i) => {
    const b = document.createElement('button');
    b.textContent = n; b.title = (i + 1) + ' ' + n;
    b.addEventListener('pointerdown', e => { e.preventDefault(); send({ t: 'set', k: K.SOUND, v: i }); send({ t: 'hit', s: i, v: 110 }); });
    inst.appendChild(b);
  });
  // voice params
  const pr = $('params');
  info.pnames.forEach((n, p) => {
    if (p === info.nvpXr10) { const h = document.createElement('div'); h.className = 'help'; h.style.gridColumn = '1 / -1'; h.textContent = 'RX5-style extras (not on the XR10):'; pr.appendChild(h); }
    const row = document.createElement('div'); row.className = 'param';
    const toggle = info.plo[p] === 0 && info.phi[p] === 1;
    row.innerHTML = `<span>${n.replace('*', '')}</span>` + (toggle ? `<input type="checkbox" data-p="${p}" style="justify-self:start">` : `<input type="range" data-p="${p}" min="${info.plo[p]}" max="${info.phi[p]}">`) + `<span id="pv${p}"></span>`;
    pr.appendChild(row);
    const inp = row.querySelector('input');
    inp.addEventListener('input', () => { send({ t: 'param', s: S.sound, p, v: toggle ? (inp.checked ? 1 : 0) : +inp.value }); if (toggle) setTimeout(() => send({ t: 'wave', s: S.sound }), 60); });
    inp.addEventListener('change', () => send({ t: 'hit', s: S.sound, v: 110 }));
  });
  opt($('assignpad'), [['', '…']].concat(Array.from({ length: 15 }, (_, i) => [i, 'Pad ' + (i + 1)])));
  $('assignpad').onchange = e => { if (e.target.value !== '') send({ t: 'setPad', bank: S.bank, pad: +e.target.value, s: S.sound }); e.target.value = ''; };
  $('loadsmp').onclick = () => $('smpfile').click();
  $('smpfile').onchange = async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) return;
    try {
      const ab = await ctx.decodeAudioData(await f.arrayBuffer());
      const n = Math.min(ab.length, Math.round(ab.sampleRate * 10));
      const mono = new Float32Array(n);
      for (let c = 0; c < ab.numberOfChannels; c++) { const d = ab.getChannelData(c); for (let i = 0; i < n; i++) mono[i] += d[i] / ab.numberOfChannels; }
      // trim leading silence
      let s0 = 0; while (s0 < n - 1 && Math.abs(mono[s0]) < 0.003) s0++;
      send({ t: 'sample', s: S.sound, data: mono.subarray(s0), rate: ab.sampleRate });
      setTimeout(() => { send({ t: 'wave', s: S.sound }); send({ t: 'hit', s: S.sound, v: 110 }); }, 150);
    } catch (err) { alert('Could not read that file: ' + err.message); }
  };
  $('loadset').onclick = () => $('setfile').click();
  $('setfile').onchange = e => { const f = e.target.files; if (f && f.length) loadSampleSet(f); e.target.value = ''; };
  $('romsmp').onclick = () => { send({ t: 'clearSample', s: S.sound }); setTimeout(() => send({ t: 'wave', s: S.sound }), 100); };
  $('resetv').onclick = () => { send({ t: 'resetVoice', s: S.sound }); send({ t: 'hit', s: S.sound, v: 110 }); };
  // grid
  const pb = $('partbtns');
  PARTS.forEach((n, i) => { const b = document.createElement('button'); b.textContent = n; b.dataset.part = i; b.onclick = () => send({ t: 'set', k: K.PART, v: i }); pb.appendChild(b); });
  opt($('len'), Array.from({ length: 4 }, (_, i) => [i + 1, (i + 1) + (i ? ' bars' : ' bar')]));
  opt($('beats'), Array.from({ length: 7 }, (_, i) => [i + 2, (i + 2) + '/4']));
  $('len').onchange = e => { send({ t: 'set', k: K.LEN, v: +e.target.value }); later(); };
  $('beats').onchange = e => { send({ t: 'set', k: K.BEATS, v: +e.target.value }); later(); };
  $('sub').onchange = e => { send({ t: 'set', k: K.SUB, v: +e.target.value }); later(); };
  $('swing').onchange = e => { send({ t: 'set', k: K.SWING, v: +e.target.value }); later(); };
  $('bank').onchange = e => send({ t: 'set', k: K.BANK, v: +e.target.value });
  $('clearpart').onclick = () => { if (confirm('Clear ' + PARTS[S.part] + ' of this pattern?')) { send({ t: 'clearPart' }); later(); } };
  $('midiexp').onclick = () => send({ t: 'exportMidi', song: 0 });
  $('midiimp').onclick = () => $('midifile').click();
  $('midifile').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (f) send({ t: 'importMidi', bytes: await f.arrayBuffer() }); };
  // song
  $('songmidi').onclick = () => { if (!S.songLen) { alert('The song is empty.'); return; } send({ t: 'exportMidi', song: 1 }); };
  opt($('songsel'), Array.from({ length: 20 }, (_, i) => [i, 'Song ' + (i + 1)]));
  $('songsel').onchange = e => send({ t: 'set', k: K.SONG, v: +e.target.value });
  $('songtempo').onchange = e => send({ t: 'songTempo', song: S.song, bpm: Math.max(0, Math.min(300, +e.target.value || 0)) });
  $('songadd').onclick = () => send({ t: 'songSet', song: S.song, step: 999, p: S.pattern, part: S.keys[K.MAINVAR], rep: 1 });
  $('songplay').onclick = () => { send({ t: 'set', k: K.MODE, v: 1 }); send({ t: 'set', k: K.PLAYING, v: S.playing ? 0 : 1 }); };
  // setup
  opt($('fxtype'), ['Room (extra)', 'Hall (extra)', 'Plate (extra)', 'Gated (extra)', 'Delay (extra)', 'External: EFFECT jack only (the XR10)', 'Off'].map((n, i) => [i, n]));
  opt($('tc'), ['1/4', '1/8', '1/8 triplet', '1/16', '1/16 triplet', '1/32', 'Off'].map((n, i) => [i, n]));
  opt($('poly'), [4, 6, 8, 12, 16, 24, 32].map(v => [v, v + (v === 8 ? ' (XR10)' : v === 12 ? ' (RX5)' : '')]));
  opt($('midich'), [[0, 'Omni']].concat(Array.from({ length: 16 }, (_, i) => [i + 1, i + 1])));
  const bindSel = (id, k) => $(id).onchange = e => send({ t: 'set', k, v: +e.target.value });
  bindSel('fxtype', K.FXTYPE); bindSel('tc', K.TC); bindSel('poly', K.POLY); bindSel('midich', K.MIDICH); bindSel('midimap', K.MIDIMAP); bindSel('click', K.CLICK);
  const bindRange = (id, k) => $(id).oninput = e => send({ t: 'set', k, v: +e.target.value });
  bindRange('effectvr', K.EFFECTVR); bindRange('fxtime', K.FXTIME); bindRange('fxlevel', K.FXLEVEL); bindRange('clicklvl', K.CLICKLVL);
  $('accvel').onchange = e => send({ t: 'set', k: K.ACCENTVEL, v: +e.target.value });
  $('padvel').onchange = e => send({ t: 'set', k: K.PADVEL, v: e.target.checked ? 1 : 0 });
  $('countin').onchange = e => send({ t: 'set', k: K.COUNTIN, v: e.target.checked ? 1 : 0 });
  $('export').onclick = () => save('file');
  $('import').onclick = () => $('importfile').click();
  $('importfile').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (f) send({ t: 'load', bytes: await f.arrayBuffer(), why: 'file' }); };
  $('init').onclick = () => { if (confirm('Erase all your patterns, songs, voice edits and samples?')) send({ t: 'reset' }); };
  $('theme').onclick = () => { const r = document.documentElement; const dark = r.dataset.theme ? r.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches; r.dataset.theme = dark ? 'light' : 'dark'; try { localStorage.xr10theme = r.dataset.theme; } catch (e) { } drawLcd(); };
  $('midion').onclick = midiOn;
  document.addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (e.code === 'Space') { e.preventDefault(); send({ t: 'btn', b: B.START, d: 1 }); send({ t: 'btn', b: B.START, d: 0 }); }
    const keys = 'zxcasdqwe123', i = keys.indexOf(e.key);
    if (i >= 0 && !e.repeat) send({ t: 'pad', i, d: 1, v: 110 });
  });
}
function later() { setTimeout(() => send({ t: 'patterns' }), 120); }
function drawWave(w) {
  const c = $('wave'), g = c.getContext('2d'), wd = c.width, ht = c.height;
  g.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--lcd'); g.fillRect(0, 0, wd, ht);
  g.strokeStyle = '#b4c8c6'; g.beginPath();
  const per = Math.max(1, Math.floor(w.length / wd));
  for (let x = 0; x < wd; x++) {
    let mn = 0, mx = 0;
    for (let i = x * per; i < (x + 1) * per && i < w.length; i++) { const v = w[i] / 32768; if (v < mn) mn = v; if (v > mx) mx = v; }
    g.moveTo(x + 0.5, ht / 2 - mx * ht / 2); g.lineTo(x + 0.5, ht / 2 - mn * ht / 2 + 1);
  }
  g.stroke();
  g.fillStyle = '#b4c8c6'; g.font = '11px system-ui'; g.fillText((w.length / 31250).toFixed(2) + ' s, 16-bit, 31.25 kHz', 6, 13);
}

// --------------------------------------------------- per-frame update from the engine's status
let lastSound = -1, lastGridKey = '', lastSongKey = '';
function setVal(el, v) { if (document.activeElement !== el && String(el.value) !== String(v)) el.value = v; }
function update() {
  if (!S) return;
  lcdText = [S.l1, S.l2];
  drawLcd();
  ledEls.forEach((d, i) => d.className = 'led ' + (S.mode === i ? 'on' : 'off'));
  for (let i = 0; i < 15; i++) hotEls[B.PAD1 + i].title = info.names[S.padSound[i]];
  hotEls[B.ACCENT].classList.toggle('lit', !!S.accent);
  hotEls[B.EDIT].classList.toggle('lit', !!S.recording);
  hotEls[B.START].classList.toggle('lit', !!S.playing);
  hotEls[B.ERASE].classList.toggle('lit', !!S.erase);
  const vp = (i) => [B.PAD1 + 11, B.PAD1 + 10, B.PAD1 + 9, B.PAD1 + 14, B.PAD1 + 13, B.PAD1 + 12, B.INTRO, B.INTRO][i];
  for (let i = 0; i < 6; i++) hotEls[vp(i)].classList.toggle('lit', !S.recording && S.mode < 2 && S.part === i);
  $('play').classList.toggle('on', !!S.playing);
  $('recbtn').classList.toggle('on', !!S.recording);
  setVal($('tempo'), (S.tempo10 / 10).toFixed(S.tempo10 % 10 ? 1 : 0));
  setVal($('patsel'), S.pattern);
  // big pads
  document.querySelectorAll('#bigpads .bigpad').forEach((b, i) => { b.querySelector('span').textContent = (i >= 9 && !S.recording && S.mode < 2) ? ['VAR C', 'VAR B', 'VAR A', 'FILL C', 'FILL B', 'FILL A'][i - 9] : info.names[S.padSound[i]]; });
  document.querySelectorAll('#partpads button').forEach(b => {
    const id = +b.dataset.b, idx = [B.PAD1 + 11, B.PAD1 + 10, B.PAD1 + 9, B.PAD1 + 14, B.PAD1 + 13, B.PAD1 + 12].indexOf(id);
    b.classList.toggle('on', (idx >= 0 && S.part === idx) || (id === B.START && !!S.playing) || (id === B.ACCENT && !!S.accent) || (id === B.INTRO && S.part >= 6));
  });
  $('padlcd').textContent = S.l1 + '\n' + S.l2;
  // instruments and voice
  const ib = $('inst').children;
  for (let i = 0; i < 64; i++) { ib[i].classList.toggle('on', i === S.sound); ib[i].classList.toggle('user', !!S.user[i]); }
  if (S.sound !== lastSound) { lastSound = S.sound; send({ t: 'wave', s: S.sound }); }
  $('vtitle').textContent = `Voice ${S.sound + 1}: ${info.names[S.sound]}${S.user[S.sound] ? ' (your sample)' : info.packed && info.packed[S.sound] ? ' (XR-10 sample)' : ' (synthesized)'}`;
  document.querySelectorAll('#params input').forEach(inp => {
    const p = +inp.dataset.p, v = S.params[p];
    if (inp.type === 'checkbox') inp.checked = !!v; else setVal(inp, v);
    $('pv' + p).textContent = fmtParam(p, v);
  });
  // grid
  $('gtitle').textContent = `Step grid: pattern ${S.pattern + 1} ${S.patName}, ${PARTS[S.part]}` + (S.pattern < 50 ? ' (preset: editing makes a user copy)' : '');
  document.querySelectorAll('#partbtns button').forEach(b => b.classList.toggle('on', +b.dataset.part === S.part));
  if (gridBar >= S.bars) { gridBar = 0; send({ t: 'gridBar', bar: 0 }); }
  const bb = $('barbtns');
  if (bb.children.length !== S.bars) {
    bb.innerHTML = '';
    for (let i = 0; i < S.bars; i++) { const b = document.createElement('button'); b.textContent = i + 1; b.onclick = () => { gridBar = i; send({ t: 'gridBar', bar: i }); }; bb.appendChild(b); }
  }
  [...bb.children].forEach((b, i) => b.classList.toggle('on', i === gridBar));
  setVal($('len'), S.keys[K.LEN]); setVal($('beats'), S.keys[K.BEATS]); setVal($('sub'), S.keys[K.SUB]); setVal($('swing'), S.keys[K.SWING]); setVal($('bank'), S.bank);
  drawGrid();
  // song
  setVal($('songsel'), S.song);
  setVal($('songtempo'), S.songTempo || '');
  drawSong();
  // setup
  setVal($('effectvr'), S.keys[K.EFFECTVR]); setVal($('fxtype'), S.keys[K.FXTYPE]); setVal($('fxtime'), S.keys[K.FXTIME]); setVal($('fxlevel'), S.keys[K.FXLEVEL]);
  setVal($('tc'), S.keys[K.TC]); setVal($('poly'), S.keys[K.POLY]); setVal($('midich'), S.keys[K.MIDICH]); setVal($('midimap'), S.keys[K.MIDIMAP]);
  setVal($('click'), S.keys[K.CLICK]); setVal($('clicklvl'), S.keys[K.CLICKLVL]); setVal($('accvel'), S.keys[K.ACCENTVEL]);
  $('padvel').checked = !!S.keys[K.PADVEL]; $('countin').checked = !!S.keys[K.COUNTIN];
}
function fmtParam(p, v) {
  const n = info.pnames[p];
  if (n === 'PAN') return v === 0 ? 'center' : (v < 0 ? 'L' + -v : 'R' + v);
  if (n === 'TUNE') return (v > 0 ? '+' : '') + v + ' st';
  if (n === 'FINE') return (v > 0 ? '+' : '') + (v * 6.25).toFixed(2).replace(/\.?0+$/, '') + 'c';
  if (n === 'SWEEP') return v ? 'down' : 'up';
  if (n === 'TONE*' && v >= 99) return 'open';
  if (n === 'CHOKE GRP*') return v ? v : 'off';
  if (info.plo[p] === 0 && info.phi[p] === 1) return v ? 'on' : 'off';
  return v;
}
function drawGrid() {
  const g = $('grid'), steps = S.steps;
  const rows = []; for (let r = 0; r < 16; r++) { const s = S.grid[16 * 32 + r]; if (s !== 255) rows.push(s); }
  const key = [steps, S.sub, rows.join(',')].join('|');
  if (key !== lastGridKey) {
    lastGridKey = key;
    g.innerHTML = '';
    g.style.gridTemplateColumns = `minmax(84px, 110px) repeat(${steps}, minmax(16px, 1fr))`;
    rows.forEach((snd, r) => {
      const sel = document.createElement('select');
      opt(sel, info.names.map((n, i) => [i, (i + 1) + ' ' + n]));
      sel.value = snd;
      sel.title = 'Change this row\'s sound (its notes move with it)';
      sel.onchange = () => { send({ t: 'gridAssign', row: r, s: +sel.value }); later(); };
      const nm = document.createElement('div'); nm.className = 'name'; nm.appendChild(sel);
      g.appendChild(nm);
      for (let s = 0; s < steps; s++) {
        const c = document.createElement('div');
        c.className = 'cell';
        c.dataset.r = r; c.dataset.s = s;
        c.addEventListener('pointerdown', e => { e.preventDefault(); send({ t: 'gridToggle', row: r, bar: gridBar, step: s }); later(); });
        g.appendChild(c);
      }
    });
  }
  const live = S.playing && S.bar === gridBar;
  g.querySelectorAll('.cell').forEach(c => {
    const r = +c.dataset.r, s = +c.dataset.s, v = S.grid[r * 32 + s];
    const cls = 'cell' + (s % S.sub === 0 ? ' beat' : '') + (v ? ' v' + v : '') + (live && Math.floor(s / S.sub) === S.beat ? ' now' : '');
    if (c.className !== cls) c.className = cls;
  });
}
function drawSong() {
  const key = JSON.stringify([S.song, S.songSteps, S.songStep, S.playing, info.patterns.length]);
  if (key === lastSongKey) return;
  lastSongKey = key;
  const l = $('songlist');
  l.innerHTML = '';
  if (!S.songSteps.length) { l.innerHTML = '<p class="help">Empty. Pick a pattern above and press "Add current pattern", or use SONG mode on the panel.</p>'; return; }
  S.songSteps.forEach(([p, part, rep], i) => {
    const row = document.createElement('div'); row.className = 'srow' + (i === S.songStep && S.playing ? ' now' : '');
    row.innerHTML = `<span>${i + 1}</span>`;
    const ps = document.createElement('select'); ps.className = 'pp'; opt(ps, info.patterns.map((n, k) => [k, String(k + 1).padStart(2, '0') + ' ' + n])); ps.value = p;
    const pt = document.createElement('select'); opt(pt, PARTS.map((n, k) => [k, n])); pt.value = part;
    const rp = document.createElement('input'); rp.type = 'number'; rp.min = 1; rp.max = 99; rp.value = rep;
    const set = () => send({ t: 'songSet', song: S.song, step: i, p: +ps.value, part: +pt.value, rep: +rp.value || 1 });
    ps.onchange = pt.onchange = rp.onchange = set;
    const ins = document.createElement('button'); ins.textContent = '+'; ins.title = 'Duplicate'; ins.onclick = () => send({ t: 'songInsert', song: S.song, step: i });
    const del = document.createElement('button'); del.textContent = '×'; del.title = 'Delete'; del.onclick = () => send({ t: 'songDelete', song: S.song, step: i });
    row.append(ps, pt, rp, ins, del);
    l.appendChild(row);
  });
}

// --------------------------------------------------- MIDI (Web MIDI, or Android's through AndroidMidi)
async function midiAccess() {
  if (window.AndroidMidi && AndroidMidi.available()) {
    const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
    const scan = () => {
      for (const d of JSON.parse(AndroidMidi.list())) {
        if (d.kind === 'input' && !access.inputs.has(d.id)) {
          const port = { id: d.id, name: d.name, type: 'input', _h: null };
          Object.defineProperty(port, 'onmidimessage', { get() { return this._h; }, set(h) { this._h = h; if (h) AndroidMidi.listen(d.id); } });
          access.inputs.set(d.id, port);
        }
      }
    };
    window.__twMidi = (id, bytes) => { const p = access.inputs.get(id); if (p && p._h) p._h({ data: new Uint8Array(bytes) }); };
    window.__twMidiChanged = () => { scan(); access.onstatechange && access.onstatechange(); };
    scan();
    return access;
  }
  if (!navigator.requestMIDIAccess) throw new Error('No MIDI on this browser');
  return navigator.requestMIDIAccess();
}
async function midiOn() {
  try {
    const a = await midiAccess();
    const hook = () => {
      const names = [];
      for (const i of a.inputs.values()) {
        names.push(i.name);
        i.onmidimessage = e => { const d = e.data; if (d.length >= 1) send({ t: 'midi', a: d[0], b: d[1] | 0, c: d[2] | 0 }); };
      }
      $('midistat').textContent = names.length ? 'Listening: ' + names.join(', ') : 'No MIDI inputs found yet';
    };
    a.onstatechange = hook;
    hook();
    $('midion').classList.add('on');
  } catch (e) { $('midistat').textContent = e.message; }
}
document.addEventListener('dragover', e => e.preventDefault());
document.addEventListener('drop', async e => {
  e.preventDefault();
  if (!node || !S) return;
  const files = [...e.dataTransfer.files];
  const mid = files.find(f => /\.midi?$/i.test(f.name));
  if (mid) { send({ t: 'importMidi', bytes: await mid.arrayBuffer() }); return; }
  const mem = files.find(f => /\.xr10$/i.test(f.name));
  if (mem) { send({ t: 'load', bytes: await mem.arrayBuffer(), why: 'file' }); return; }
  if (files.length) loadSampleSet(files);
});
try { if (localStorage.xr10theme) document.documentElement.dataset.theme = localStorage.xr10theme; } catch (e) { }
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidXR10) navigator.serviceWorker.register('sw.js').catch(() => { });
