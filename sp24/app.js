'use strict';
/* SP-24: a 24 PPQ sequence editor for E-mu SP-12 / SP-1200 style segments, songs, mixes and sounds.
   Events are kept the way the SP keeps them: tick (24 PPQ), sound 0-31, level 0-31, tune -8..+7. */
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const PPQ = 24, NSND = 32, NSEG = 100, NSONG = 100, NMIX = 8;
const SWINGS = [[12, '50 % (off)'], [13, '54 %'], [14, '58 %'], [15, '63 %'], [16, '67 %'], [17, '71 %']];
// E-mu SP-1200 semitone table (index = semitones + 16), playback increment / 128; from libmd12 (MIT)
const SP_TUNE = [50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255];
const SP_SR = 20e6 / 768;
const DEFAULT_NAMES = ['Kick', 'Snare', 'Hat cl', 'Hat op', 'Clap', 'Rim', 'Tom lo', 'Tom hi',
  'Kick 2', 'Snare 2', 'Shaker', 'Ride', 'Crash', 'Cowbell', 'Conga', 'Bass',
  'Kick 3', 'Snare 3', 'Hat 3', 'Tamb', 'Clave', 'Tom mid', 'Perc', 'Stab',
  'Kick 4', 'Snare 4', 'Hat 4', 'Snap', 'Wood', 'Tom 4', 'Zap', 'Hit'];
const sndLabel = i => `${(i >> 3) + 1}-${(i & 7) + 1}`;
const toast = msg => { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(toast.h); toast.h = setTimeout(() => t.classList.remove('on'), 1800); };

/* ================= project model ================= */
let P;
function newSound(i){ return {name:DEFAULT_NAMES[i], mode:'tune', tune:0, decay:31, sample:null, start:0, end:0, loop:-1}; }
function newSegment(){ return {len:2, num:4, den:4, tempo:90, swing:12, events:[]}; }
function newProject(){
  const p = {v:1, name:'Untitled', model:'SP-1200', sounds:[], segments:Array(NSEG).fill(null), songs:Array(NSONG).fill(null), mixes:[]};
  for (let i = 0; i < NSND; i++) p.sounds.push(newSound(i));
  for (let m = 0; m < NMIX; m++) p.mixes.push(Array(NSND).fill(24));
  // a starter beat so there is something to hear
  const s = {len:2, num:4, den:4, tempo:92, swing:15, events:[]};
  const add = (t, snd, l, u = 0) => s.events.push({t, s:snd, l, u});
  for (let b = 0; b < 2; b++){
    const o = b * 96;
    add(o, 0, 31); add(o + 36, 0, 24); add(o + 60, 0, 28); if (b) add(o + 84, 0, 20);
    add(o + 24, 1, 31); add(o + 72, 1, 31);
    for (let e = 0; e < 8; e++) add(o + e * 12, 2, e % 2 ? 18 : 26);
  }
  add(180, 3, 22);
  s.events.sort((a, b) => a.t - b.t);
  p.segments[1] = s;
  p.songs[1] = {name:'SONG 01', tempo:92, steps:[{k:'rep', v:4}, {k:'seg', v:1}, {k:'rend'}, {k:'end'}]};
  return p;
}
const segTicksPerBar = g => g.num * 96 / g.den;
const segTicks = g => g.len * segTicksPerBar(g);
const curSeg = () => P.segments[ui.seg] || (P.segments[ui.seg] = newSegment());
// where the SP plays an event: segment swing moves notes exactly on the 2nd 8th of a beat (beats restart at each bar)
function playedTick(g, t){
  if (g.swing <= 12) return t;
  const bt = segTicksPerBar(g), rel = t % bt;
  return rel % PPQ === 12 && rel - 12 + g.swing < bt ? t - 12 + g.swing : t;
}
function fmtPos(g, t){
  const bt = segTicksPerBar(g), unit = 96 / g.den, bar = Math.floor(t / bt), rel = t - bar * bt;
  return `${String(bar + 1).padStart(3, '0')}.${Math.floor(rel / unit) + 1}.${String(rel % unit).padStart(2, '0')}`;
}
function parsePos(g, s){
  const m = String(s).trim().match(/^(\d+)(?:\.(\d+))?(?:\.(\d+))?$/); if (!m) return null;
  const unit = 96 / g.den;
  return (Number(m[1]) - 1) * segTicksPerBar(g) + (Number(m[2] || 1) - 1) * unit + Number(m[3] || 0);
}

/* ================= undo ================= */
const undoStack = [], redoStack = [];
const snapshot = () => JSON.stringify({segments:P.segments, songs:P.songs, mixes:P.mixes, sounds:P.sounds.map(s => ({...s, sample:undefined}))});
function restore(json){
  const d = JSON.parse(json);
  P.segments = d.segments; P.songs = d.songs; P.mixes = d.mixes;
  d.sounds.forEach((s, i) => { P.sounds[i] = {...s, sample:P.sounds[i].sample}; });
  ui.sel.clear(); refreshAll();
}
function pushUndo(){ undoStack.push(snapshot()); if (undoStack.length > 150) undoStack.shift(); redoStack.length = 0; }
function undo(){ if (!undoStack.length) return; redoStack.push(snapshot()); restore(undoStack.pop()); changed(); }
function redo(){ if (!redoStack.length) return; undoStack.push(snapshot()); restore(redoStack.pop()); changed(); }

/* ================= storage: autosave + backups (IndexedDB) ================= */
const DB = (() => {
  let p;
  const open = () => p || (p = new Promise((res, rej) => {
    const r = indexedDB.open('sp24', 1);
    r.onupgradeneeded = () => { r.result.createObjectStore('kv'); r.result.createObjectStore('backups', {keyPath:'id', autoIncrement:true}); };
    r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
  }));
  const run = (store, mode, fn) => open().then(db => new Promise((res, rej) => {
    const t = db.transaction(store, mode), req = fn(t.objectStore(store));
    t.oncomplete = () => res(req && req.result); t.onerror = () => rej(t.error);
  }));
  return {
    get:k => run('kv', 'readonly', s => s.get(k)),
    set:(k, v) => run('kv', 'readwrite', s => s.put(v, k)),
    addBackup:b => run('backups', 'readwrite', s => s.add(b)),
    listBackups:() => run('backups', 'readonly', s => s.getAll()),
    getBackup:id => run('backups', 'readonly', s => s.get(id)),
    delBackup:id => run('backups', 'readwrite', s => s.delete(id)),
  };
})();
function f32ToB64(f){
  const i16 = new Int16Array(f.length); for (let i = 0; i < f.length; i++) i16[i] = clamp(Math.round(f[i] * 32767), -32768, 32767);
  const u8 = new Uint8Array(i16.buffer); let s = '';
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}
function b64ToF32(b){
  const s = atob(b), u8 = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
  const i16 = new Int16Array(u8.buffer), f = new Float32Array(i16.length); for (let i = 0; i < f.length; i++) f[i] = i16[i] / 32768;
  return f;
}
const serialize = () => JSON.stringify({...P, sounds:P.sounds.map(s => ({...s, sample:s.sample ? {rate:s.sample.rate, b64:f32ToB64(s.sample.data)} : null}))});
function deserialize(json){
  const d = JSON.parse(json);
  if (!d || !Array.isArray(d.segments) || !Array.isArray(d.sounds)) throw new Error('Not an SP-24 project');
  const p = newProject();
  p.name = d.name || 'Untitled'; p.model = d.model || 'SP-1200';
  for (let i = 0; i < NSEG; i++) p.segments[i] = d.segments[i] || null;
  for (let i = 0; i < NSONG; i++) p.songs[i] = (d.songs || [])[i] || null;
  for (let m = 0; m < NMIX; m++) if (d.mixes && d.mixes[m]) p.mixes[m] = d.mixes[m].slice(0, NSND);
  d.sounds.slice(0, NSND).forEach((s, i) => { p.sounds[i] = {...newSound(i), ...s, sample:s.sample ? {rate:s.sample.rate, data:b64ToF32(s.sample.b64)} : null}; });
  return p;
}
let saveTimer = 0, lastBackup = 0;
function changed(){
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const json = serialize();
    DB.set('current', json).catch(() => {});
    if (Date.now() - lastBackup > 300000) backup('Editing', json);
  }, 600);
}
function backup(reason, json = serialize()){
  lastBackup = Date.now();
  return DB.addBackup({time:Date.now(), reason, name:P.name, json}).then(pruneBackups).then(renderBackups).catch(() => {});
}
async function pruneBackups(){
  const all = await DB.listBackups(); all.sort((a, b) => b.time - a.time);
  for (const b of all.slice(30)) await DB.delBackup(b.id);
}

/* ================= UI state ================= */
const ui = {seg:1, zoom:5, rowH:20, snap:6, tool:'draw', lane:'l', compact:false, sel:new Set(), cursor:0, playTick:-1,
  lastLevel:28, mix:0, mixBank:0, song:1, step:-1, sound:0, clip:null};
const LABEL = 92, RULER = 24, LANE = 84;
const roll = $('#roll'), rctx = roll.getContext('2d'), wrap = $('#rollWrap'), space = $('#rollSpace');
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
let colors = {};
function readColors(){ ['roll', 'rollAlt', 'grid', 'beat', 'bar', 'note', 'sel', 'swing', 'ink', 'dim', 'acc', 'panel', 'panel2', 'line', 'ok'].forEach(k => colors[k] = css('--' + k)); }
const rows = () => {
  if (!ui.compact) return [...Array(NSND).keys()];
  const used = new Set(curSeg().events.map(e => e.s)); ui.sel.forEach(e => used.add(e.s));
  const r = [...used].sort((a, b) => a - b); return r.length ? r : [0];
};

/* ================= piano roll ================= */
function layoutRoll(){
  if (!P) return;
  const g = curSeg(), R = rows();
  space.style.width = (LABEL + segTicks(g) * ui.zoom + 60) + 'px';
  space.style.height = (RULER + R.length * ui.rowH + LANE) + 'px';
  const dpr = devicePixelRatio || 1, w = wrap.clientWidth, h = wrap.clientHeight;
  roll.width = w * dpr; roll.height = h * dpr; roll.style.width = w + 'px'; roll.style.height = h + 'px';
  rctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawRoll();
}
const xOf = t => LABEL + t * ui.zoom - wrap.scrollLeft;
const yOf = r => RULER + r * ui.rowH - wrap.scrollTop;
const noteW = () => Math.max(6, Math.min(ui.zoom * 5, 18));
function drawRoll(){
  if (!P) return;
  const c = rctx, g = curSeg(), R = rows(), W = roll.clientWidth, H = roll.clientHeight, total = segTicks(g), bt = segTicksPerBar(g), unit = 96 / g.den;
  const laneTop = H - LANE;
  c.fillStyle = colors.roll; c.fillRect(0, 0, W, H);
  // rows
  c.save(); c.beginPath(); c.rect(0, RULER, W, laneTop - RULER); c.clip();
  R.forEach((s, r) => { const y = yOf(r); if (y > laneTop || y + ui.rowH < RULER) return; if ((s >> 3) % 2) { c.fillStyle = colors.rollAlt; c.fillRect(LABEL, y, W, ui.rowH); } });
  // grid lines
  const t0 = Math.max(0, Math.floor((wrap.scrollLeft) / ui.zoom) - 2), t1 = Math.min(total, Math.ceil((wrap.scrollLeft + W) / ui.zoom));
  const step = ui.zoom * ui.snap >= 5 ? ui.snap : unit;
  for (let t = t0 - (t0 % step); t <= t1; t += step){
    const x = Math.round(xOf(t)) + .5; if (x < LABEL) continue;
    c.strokeStyle = t % bt === 0 ? colors.bar : (t % bt) % unit === 0 ? colors.beat : colors.grid;
    c.beginPath(); c.moveTo(x, RULER); c.lineTo(x, laneTop); c.stroke();
  }
  const endX = xOf(total); c.fillStyle = 'rgba(0,0,0,.35)'; c.fillRect(Math.max(LABEL, endX), RULER, W, laneTop);
  R.forEach((s, r) => { const y = Math.round(yOf(r + 1)) + .5; c.strokeStyle = colors.grid; c.beginPath(); c.moveTo(LABEL, y); c.lineTo(W, y); c.stroke(); });
  // notes
  const rowOf = new Map(R.map((s, i) => [s, i])), nw = noteW();
  for (const e of g.events){
    const r = rowOf.get(e.s); if (r === undefined) continue;
    const pt = playedTick(g, e.t), x = xOf(pt), y = yOf(r);
    if (x + nw < LABEL || x > W) continue;
    const sel = ui.sel.has(e);
    if (pt !== e.t){ // swung by the segment: outline where it is stored
      const xs = xOf(e.t); c.strokeStyle = colors.swing; c.setLineDash([2, 2]); c.strokeRect(xs + .5, y + 3.5, nw - 1, ui.rowH - 7); c.setLineDash([]);
      c.beginPath(); c.moveTo(xs + nw, y + ui.rowH / 2); c.lineTo(x, y + ui.rowH / 2); c.stroke();
    }
    c.globalAlpha = .35 + .65 * e.l / 31;
    c.fillStyle = sel ? colors.sel : pt !== e.t ? colors.swing : colors.note;
    c.fillRect(x, y + 2, nw, ui.rowH - 4); c.globalAlpha = 1;
    if (sel){ c.strokeStyle = colors.ink; c.strokeRect(x + .5, y + 2.5, nw - 1, ui.rowH - 5); }
    if (e.u && ui.zoom >= 3){ c.fillStyle = '#000'; c.font = '9px ui-monospace,monospace'; c.fillText(e.u > 0 ? '+' + e.u : e.u, x + 1, y + ui.rowH - 5); }
  }
  // marquee
  if (drag && drag.kind === 'marquee'){ const m = drag; c.fillStyle = 'rgba(255,255,255,.08)'; c.strokeStyle = colors.sel; const x = Math.min(m.x0, m.x1), y = Math.min(m.y0, m.y1); c.fillRect(x, y, Math.abs(m.x1 - m.x0), Math.abs(m.y1 - m.y0)); c.strokeRect(x + .5, y + .5, Math.abs(m.x1 - m.x0), Math.abs(m.y1 - m.y0)); }
  c.restore();
  // labels
  c.fillStyle = colors.panel; c.fillRect(0, RULER, LABEL, laneTop - RULER);
  c.save(); c.beginPath(); c.rect(0, RULER, LABEL, laneTop - RULER); c.clip(); c.font = '11.5px system-ui'; c.textBaseline = 'middle';
  R.forEach((s, r) => {
    const y = yOf(r); if (y > laneTop || y + ui.rowH < RULER) return;
    c.fillStyle = s === ui.sound ? colors.acc : colors.dim; c.fillText(sndLabel(s), 6, y + ui.rowH / 2);
    c.fillStyle = colors.ink; c.fillText(P.sounds[s].name.slice(0, 9) + (P.sounds[s].mode === 'decay' ? ' ᴰ' : ''), 32, y + ui.rowH / 2);
    c.strokeStyle = colors.line; c.beginPath(); c.moveTo(0, Math.round(y + ui.rowH) + .5); c.lineTo(LABEL, Math.round(y + ui.rowH) + .5); c.stroke();
  });
  c.restore();
  // ruler
  c.fillStyle = colors.panel2; c.fillRect(0, 0, W, RULER);
  c.font = '11px ui-monospace,monospace'; c.textBaseline = 'middle';
  for (let t = t0 - (t0 % unit); t <= t1; t += unit){
    const x = Math.round(xOf(t)) + .5; if (x < LABEL) continue;
    const isBar = t % bt === 0; c.strokeStyle = isBar ? colors.ink : colors.dim;
    c.beginPath(); c.moveTo(x, isBar ? 4 : 14); c.lineTo(x, RULER); c.stroke();
    if (isBar && t < total){ c.fillStyle = colors.ink; c.fillText(String(t / bt + 1), x + 3, 9); }
  }
  // cursor + playhead
  const cx = xOf(ui.cursor); if (cx >= LABEL){ c.fillStyle = colors.acc; c.beginPath(); c.moveTo(cx - 5, 14); c.lineTo(cx + 5, 14); c.lineTo(cx, RULER); c.fill(); c.strokeStyle = colors.acc; c.globalAlpha = .4; c.beginPath(); c.moveTo(cx + .5, RULER); c.lineTo(cx + .5, laneTop); c.stroke(); c.globalAlpha = 1; }
  if (ui.playTick >= 0){ const px = Math.round(xOf(ui.playTick)) + .5; if (px >= LABEL){ c.strokeStyle = colors.ok; c.lineWidth = 2; c.beginPath(); c.moveTo(px, 0); c.lineTo(px, H); c.stroke(); c.lineWidth = 1; } }
  c.fillStyle = colors.panel2; c.fillRect(0, 0, LABEL, RULER); c.fillStyle = colors.dim; c.fillText(`${g.num}/${g.den}`, 8, 12);
  // lane
  c.fillStyle = colors.panel; c.fillRect(0, laneTop, W, LANE); c.strokeStyle = colors.line; c.beginPath(); c.moveTo(0, laneTop + .5); c.lineTo(W, laneTop + .5); c.stroke();
  c.fillStyle = colors.dim; c.font = '11px system-ui'; c.fillText(ui.lane === 'l' ? 'Level 0–31' : 'Tune / Decay', 6, laneTop + 12);
  const lh = LANE - 22, lb = laneTop + LANE - 6;
  if (ui.lane === 'u'){ const zy = lb - lh * 8 / 15; c.strokeStyle = colors.line; c.beginPath(); c.moveTo(LABEL, zy + .5); c.lineTo(W, zy + .5); c.stroke(); }
  c.save(); c.beginPath(); c.rect(LABEL, laneTop, W - LABEL, LANE); c.clip();
  for (const e of g.events){
    if (!rowOf.has(e.s)) continue;
    const x = xOf(playedTick(g, e.t)) + 1; if (x < LABEL - 4 || x > W) continue;
    c.fillStyle = ui.sel.has(e) ? colors.sel : colors.note;
    if (ui.lane === 'l'){ const h = Math.max(1, lh * e.l / 31); c.fillRect(x, lb - h, 3, h); }
    else { const zy = lb - lh * 8 / 15, y = lb - lh * (e.u + 8) / 15; c.fillRect(x, Math.min(y, zy), 3, Math.max(2, Math.abs(zy - y))); }
  }
  c.restore();
}

/* ---- hit testing ---- */
function hitEvent(x, y){
  const g = curSeg(), R = rows(), r = Math.floor((y - RULER + wrap.scrollTop) / ui.rowH), s = R[r]; if (s === undefined) return null;
  const nw = noteW(); let best = null;
  for (const e of g.events){ if (e.s !== s) continue; const ex = xOf(playedTick(g, e.t)); if (x >= ex - 2 && x <= ex + nw + 1) best = e; }
  return best;
}
const tickAt = x => (x - LABEL + wrap.scrollLeft) / ui.zoom;
const rowAt = y => Math.floor((y - RULER + wrap.scrollTop) / ui.rowH);
function snapFloor(g, t){ const bt = segTicksPerBar(g), b = Math.floor(t / bt) * bt; return b + Math.floor((t - b) / ui.snap) * ui.snap; }
function snapRound(g, t){ const bt = segTicksPerBar(g), b = Math.floor(t / bt) * bt; return Math.min(b + Math.round((t - b) / ui.snap) * ui.snap, b + bt); }
function dedupe(g){
  const seen = new Map();
  for (const e of g.events) seen.set(e.t + ':' + e.s, e);
  g.events = [...seen.values()].sort((a, b) => a.t - b.t || a.s - b.s);
  for (const e of [...ui.sel]) if (!g.events.includes(e)) ui.sel.delete(e);
}

let drag = null;
roll.addEventListener('contextmenu', e => e.preventDefault());
roll.addEventListener('pointerdown', ev => {
  const rect = roll.getBoundingClientRect(), x = ev.clientX - rect.left, y = ev.clientY - rect.top, H = roll.clientHeight, g = curSeg();
  roll.setPointerCapture(ev.pointerId); roll.focus();
  if (y < RULER){ ui.cursor = clamp(snapRound(g, tickAt(x)), 0, segTicks(g) - 1); drawRoll(); updateLcd(); return; }
  if (y > H - LANE){ pushUndo(); drag = {kind:'lane'}; laneEdit(x, y); return; }
  if (x < LABEL){ const s = rows()[rowAt(y)]; if (s !== undefined){ ui.sound = s; audition(s, ui.lastLevel, 0); drawRoll(); } return; }
  const hit = hitEvent(x, y);
  if (ev.button === 2 || ui.tool === 'erase'){ pushUndo(); drag = {kind:'erase'}; if (hit) removeEvents([hit]); return; }
  if (hit){
    if (ev.shiftKey){ ui.sel.has(hit) ? ui.sel.delete(hit) : ui.sel.add(hit); refreshSel(); drawRoll(); return; }
    if (!ui.sel.has(hit)){ ui.sel.clear(); ui.sel.add(hit); }
    ui.sound = hit.s; audition(hit.s, hit.l, hit.u);
    startMove(hit, x, y); refreshSel(); return;
  }
  if (ui.tool === 'select' || ev.ctrlKey || ev.metaKey || ev.shiftKey){
    if (!ev.shiftKey) ui.sel.clear();
    drag = {kind:'marquee', x0:x, y0:y, x1:x, y1:y, base:new Set(ui.sel)}; refreshSel(); return;
  }
  // draw a new event
  const s = rows()[rowAt(y)]; if (s === undefined) return;
  const t = snapFloor(g, tickAt(x)); if (t < 0 || t >= segTicks(g)) return;
  pushUndo();
  const e = {t, s, l:ui.lastLevel, u:0}; g.events.push(e); dedupe(g);
  const ne = g.events.find(o => o.t === t && o.s === s);
  ui.sel.clear(); ui.sel.add(ne); ui.sound = s; audition(s, ne.l, 0);
  startMove(ne, x, y, true); refreshSel(); changed();
});
function startMove(anchor, x, y, undone){
  drag = {kind:'move', anchor, x0:x, y0:y, orig:[...ui.sel].map(e => [e, e.t, e.s]), at:anchor.t, row:rows().indexOf(anchor.s), moved:false, undone:!!undone};
}
roll.addEventListener('pointermove', ev => {
  if (!drag) return;
  const rect = roll.getBoundingClientRect(), x = ev.clientX - rect.left, y = ev.clientY - rect.top, g = curSeg();
  if (drag.kind === 'lane'){ laneEdit(x, y); return; }
  if (drag.kind === 'erase'){ const h = hitEvent(x, y); if (h) removeEvents([h]); return; }
  if (drag.kind === 'marquee'){
    drag.x1 = x; drag.y1 = y;
    const ta = tickAt(Math.min(drag.x0, x)), tb = tickAt(Math.max(drag.x0, x)), ra = rowAt(Math.min(drag.y0, y)), rb = rowAt(Math.max(drag.y0, y)), R = rows();
    ui.sel = new Set(drag.base);
    for (const e of g.events){ const r = R.indexOf(e.s), pt = playedTick(g, e.t); if (r >= ra && r <= rb && pt + noteW() / ui.zoom >= ta && pt <= tb) ui.sel.add(e); }
    refreshSel(); drawRoll(); return;
  }
  if (drag.kind === 'move'){
    const raw = drag.at + (x - drag.x0) / ui.zoom, nt = ui.snap === 1 ? Math.round(raw) : snapRound(g, raw);
    let dt = nt - drag.at, dr = Math.round((y - drag.y0) / ui.rowH);
    if (!drag.moved && Math.abs(x - drag.x0) < 3 && Math.abs(y - drag.y0) < 3) return;
    if (!drag.moved && !drag.undone) pushUndo();
    drag.moved = true;
    const total = segTicks(g), R = rows();
    const minT = Math.min(...drag.orig.map(o => o[1])), maxT = Math.max(...drag.orig.map(o => o[1]));
    dt = clamp(dt, -minT, total - 1 - maxT);
    const ri = drag.orig.map(o => R.indexOf(o[2])); dr = clamp(dr, -Math.min(...ri), R.length - 1 - Math.max(...ri));
    const prevS = drag.anchor.s;
    drag.orig.forEach(([e, t, s], i) => { e.t = t + dt; e.s = R[ri[i] + dr]; });
    if (drag.anchor.s !== prevS){ ui.sound = drag.anchor.s; audition(drag.anchor.s, drag.anchor.l, drag.anchor.u); }
    refreshSel(); drawRoll();
  }
});
roll.addEventListener('pointerup', () => {
  if (!drag) return;
  if (drag.kind === 'move' && drag.moved) dedupe(curSeg());
  if (drag.kind !== 'marquee') changed();
  drag = null; refreshSel(); if (ui.compact) layoutRoll(); else drawRoll();
});
function laneEdit(x, y){
  const g = curSeg(), H = roll.clientHeight, laneTop = H - LANE, lh = LANE - 22, lb = laneTop + LANE - 6;
  const f = clamp((lb - y) / lh, 0, 1), R = new Set(rows()), tol = Math.max(3, ui.zoom * 1.5);
  const pool = ui.sel.size ? [...ui.sel] : g.events;
  for (const e of pool){
    if (!R.has(e.s)) continue;
    if (Math.abs(xOf(playedTick(g, e.t)) + 2 - x) > tol) continue;
    if (ui.lane === 'l') e.l = Math.round(f * 31); else e.u = clamp(Math.round(f * 15) - 8, -8, 7);
  }
  refreshSel(); drawRoll();
}
function removeEvents(list){
  const g = curSeg(), del = new Set(list);
  g.events = g.events.filter(e => !del.has(e)); list.forEach(e => ui.sel.delete(e));
  refreshSel(); drawRoll(); changed();
}
wrap.addEventListener('scroll', () => drawRoll(), {passive:true});
wrap.addEventListener('wheel', ev => {
  if (!(ev.ctrlKey || ev.metaKey)) return;
  ev.preventDefault();
  const rect = roll.getBoundingClientRect(), x = ev.clientX - rect.left, t = tickAt(x);
  ui.zoom = clamp(ui.zoom * (ev.deltaY < 0 ? 1.15 : 1 / 1.15), 1, 16); $('#zoom').value = ui.zoom;
  layoutRoll(); wrap.scrollLeft = LABEL + t * ui.zoom - x; drawRoll();
}, {passive:false});
new ResizeObserver(() => layoutRoll()).observe(wrap);

/* ---- selection inspector ---- */
function refreshSel(){
  const sel = [...ui.sel], g = curSeg(), n = sel.length;
  $('#selCount').textContent = n ? `(${n})` : '';
  const same = f => n && sel.every(e => f(e) === f(sel[0])) ? f(sel[0]) : '';
  ['#iPos', '#iSnd', '#iLvl', '#iTune', '#qSel', '#delSel', '#ssApply', '#ssUndo'].forEach(id => $(id).disabled = !n);
  $('#iPos').disabled = n !== 1;
  $('#iPos').value = n === 1 ? fmtPos(g, sel[0].t) : '';
  $('#iSnd').value = same(e => e.s);
  $('#iLvl').value = same(e => e.l);
  $('#iTune').value = same(e => e.u);
  const decay = n && sel.every(e => P.sounds[e.s].mode === 'decay');
  $('#iTuneLbl').textContent = decay ? 'Decay offset' : n && sel.some(e => P.sounds[e.s].mode === 'decay') ? 'Tune / Decay' : 'Tune';
  updateLcd();
}
function editSel(fn){ if (!ui.sel.size) return; pushUndo(); ui.sel.forEach(fn); dedupe(curSeg()); refreshSel(); drawRoll(); changed(); }
$('#iPos').addEventListener('change', e => { const g = curSeg(), t = parsePos(g, e.target.value); if (t === null || t < 0 || t >= segTicks(g)) return refreshSel(); editSel(ev => ev.t = t); });
$('#iSnd').addEventListener('change', e => { const s = Number(e.target.value); editSel(ev => ev.s = s); });
$('#iLvl').addEventListener('change', e => { const v = clamp(Math.round(Number(e.target.value)), 0, 31); ui.lastLevel = v; editSel(ev => ev.l = v); });
$('#iTune').addEventListener('change', e => { const v = clamp(Math.round(Number(e.target.value)), -8, 7); editSel(ev => ev.u = v); });
$('#qSel').onclick = () => { const g = curSeg(); editSel(ev => ev.t = clamp(snapRound(g, ev.t), 0, segTicks(g) - 1)); };
$('#delSel').onclick = () => { pushUndo(); removeEvents([...ui.sel]); };

/* ---- selective swing ---- */
// 8ths: a note exactly on the 2nd 8th of a beat (12 ticks in) moves to `amt` ticks; 16ths: a note 6 ticks into a half beat moves to `amt`.
function swingTick(g, t, unit, amt){
  const bt = segTicksPerBar(g), b = Math.floor(t / bt) * bt, rel = t - b, half = unit === 8 ? 12 : 6, span = half * 2;
  if (rel % span !== half) return t;
  const nt = b + rel - half + amt; return nt < b + bt ? nt : t;
}
function straightTick(g, t, unit){
  const bt = segTicksPerBar(g), b = Math.floor(t / bt) * bt, rel = t - b, half = unit === 8 ? 12 : 6, span = half * 2, off = rel % span;
  const swung = unit === 8 ? off > 12 && off <= 17 : off > 6 && off <= 9;
  return swung ? b + rel - off + half : t;
}
$('#ssApply').onclick = () => {
  const [u, a] = $('#ssAmt').value.split(':').map(Number), g = curSeg(); let n = 0;
  editSel(e => { const nt = swingTick(g, e.t, u, a); if (nt !== e.t){ e.t = nt; n++; } });
  toast(n ? `${n} note${n > 1 ? 's' : ''} swung` : 'No selected note sits on an off-beat ' + (u === 8 ? '8th' : '16th'));
};
$('#ssUndo').onclick = () => {
  const [u] = $('#ssAmt').value.split(':').map(Number), g = curSeg(); let n = 0;
  editSel(e => { const nt = straightTick(g, e.t, u); if (nt !== e.t){ e.t = nt; n++; } });
  toast(`${n} note${n === 1 ? '' : 's'} straightened`);
};

/* ---- keyboard ---- */
document.addEventListener('keydown', ev => {
  if (ev.target.matches('input,select,textarea')) return;
  const k = ev.key, mod = ev.ctrlKey || ev.metaKey, g = curSeg();
  if (k === ' '){ ev.preventDefault(); play.on ? stop() : start('seg'); return; }
  if (!$('#v-seq').classList.contains('on')) return;
  if (mod && k.toLowerCase() === 'z'){ ev.preventDefault(); ev.shiftKey ? redo() : undo(); return; }
  if (mod && k.toLowerCase() === 'y'){ ev.preventDefault(); redo(); return; }
  if (mod && k.toLowerCase() === 'a'){ ev.preventDefault(); ui.sel = new Set(g.events); refreshSel(); drawRoll(); return; }
  if (mod && k.toLowerCase() === 'c'){ copySel(); return; }
  if (mod && k.toLowerCase() === 'x'){ copySel(); pushUndo(); removeEvents([...ui.sel]); return; }
  if (mod && k.toLowerCase() === 'v'){ paste(); return; }
  if (k === 'Delete' || k === 'Backspace'){ if (ui.sel.size){ ev.preventDefault(); pushUndo(); removeEvents([...ui.sel]); } return; }
  if (k === 'Escape'){ ui.sel.clear(); refreshSel(); drawRoll(); return; }
  if (!mod && 'dse'.includes(k.toLowerCase()) && k.length === 1){ setTool({d:'draw', s:'select', e:'erase'}[k.toLowerCase()]); return; }
  if ((k === 'ArrowLeft' || k === 'ArrowRight') && ui.sel.size){
    ev.preventDefault(); const d = (k === 'ArrowLeft' ? -1 : 1) * (ev.altKey ? 1 : ui.snap), total = segTicks(g);
    const sel = [...ui.sel]; if (sel.some(e => e.t + d < 0 || e.t + d >= total)) return;
    editSel(e => e.t += d); return;
  }
  if ((k === 'ArrowUp' || k === 'ArrowDown') && ui.sel.size){
    ev.preventDefault(); const R = rows(), d = k === 'ArrowUp' ? -1 : 1, sel = [...ui.sel];
    if (sel.some(e => { const r = R.indexOf(e.s) + d; return r < 0 || r >= R.length; })) return;
    editSel(e => e.s = R[R.indexOf(e.s) + d]); const a = sel[0]; ui.sound = a.s; audition(a.s, a.l, a.u); return;
  }
});
function copySel(){
  if (!ui.sel.size) return; const sel = [...ui.sel], t0 = Math.min(...sel.map(e => e.t));
  ui.clip = sel.map(e => ({t:e.t - t0, s:e.s, l:e.l, u:e.u})); toast(`${sel.length} copied`);
}
function paste(){
  if (!ui.clip) return; const g = curSeg(), total = segTicks(g); pushUndo();
  const add = ui.clip.map(e => ({...e, t:e.t + ui.cursor})).filter(e => e.t < total);
  g.events.push(...add); ui.sel = new Set(add); dedupe(g); refreshSel(); drawRoll(); changed();
}

/* ================= audio preview ================= */
let ac = null, noiseBuf = null, master = null;
const bufCache = new Map();
function ensureAudio(){
  if (ac) { if (ac.state === 'suspended') ac.resume(); return ac; }
  ac = new (window.AudioContext || window.webkitAudioContext)();
  master = ac.createGain(); master.gain.value = .8;
  const comp = ac.createDynamicsCompressor(); comp.threshold.value = -6; comp.ratio.value = 4;
  master.connect(comp).connect(ac.destination);
  noiseBuf = ac.createBuffer(1, ac.sampleRate, ac.sampleRate);
  const d = noiseBuf.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  return ac;
}
const tuneRate = u => (SP_TUNE[clamp(u, -16, 15) + 16] + 1) / 128;
function sampleBuffer(i){
  const s = P.sounds[i]; if (!s.sample) return null;
  let b = bufCache.get(i); if (b && b._src === s.sample.data) return b;
  b = ac.createBuffer(1, s.sample.data.length, s.sample.rate); b.copyToChannel(s.sample.data, 0); b._src = s.sample.data;
  bufCache.set(i, b); return b;
}
// one hit: level 0-31, value -8..+7 is tune (Tune mode) or a decay offset (Decay mode)
function voice(i, level, val, t, mixIdx = ui.mix){
  ensureAudio();
  const s = P.sounds[i], mixL = P.mixes[mixIdx][i] / 31, gain = Math.pow(level / 31, 1.5) * mixL;
  if (gain <= 0.0005) return;
  const tune = s.mode === 'tune' ? s.tune + val : s.tune, decay = clamp(s.mode === 'decay' ? s.decay + val * 2 : s.decay, 0, 31) / 31;
  const rate = tuneRate(tune), out = ac.createGain(); out.connect(master);
  const buf = sampleBuffer(i);
  if (buf){
    const src = ac.createBufferSource(); src.buffer = buf; src.playbackRate.value = rate;
    const st = clamp(s.start, 0, buf.length - 1), en = clamp(s.end || buf.length, st + 1, buf.length);
    let dur = (en - st) / buf.sampleRate / rate;
    if (s.loop >= st && s.loop < en - 1){ src.loop = true; src.loopStart = s.loop / buf.sampleRate; src.loopEnd = en / buf.sampleRate; dur = Math.max(dur, 2); }
    const len = dur * (0.06 + 0.94 * decay * decay);
    out.gain.setValueAtTime(gain, t);
    if (decay < 1 || src.loop){ out.gain.setValueAtTime(gain, t + len * .55); out.gain.exponentialRampToValueAtTime(.0005, t + len); }
    src.connect(out); src.start(t, st / buf.sampleRate); src.stop(t + len + .01);
    return;
  }
  synthVoice(s.name.toLowerCase(), out, t, gain, rate, 0.15 + 0.85 * decay);
}
function env(g, t, peak, len){ g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(peak, t + .002); g.gain.exponentialRampToValueAtTime(.0005, t + len); }
function noise(out, t, len, type, freq, q, peak){
  const n = ac.createBufferSource(); n.buffer = noiseBuf; n.loop = true;
  const f = ac.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q || .7;
  const g = ac.createGain(); env(g, t, peak, len); n.connect(f).connect(g).connect(out); n.start(t, Math.random() * .5); n.stop(t + len + .02);
}
function tone(out, t, len, f0, f1, peak, type = 'sine'){
  const o = ac.createOscillator(); o.type = type; o.frequency.setValueAtTime(f0, t); if (f1) o.frequency.exponentialRampToValueAtTime(f1, t + Math.min(len, .25));
  const g = ac.createGain(); env(g, t, peak, len); o.connect(g).connect(out); o.start(t); o.stop(t + len + .02);
}
function synthVoice(name, out, t, gain, rate, dec){
  out.gain.value = gain;
  const has = w => name.includes(w);
  if (has('kick')) tone(out, t, .45 * dec, 140 * rate, 45 * rate, 1);
  else if (has('snare')){ tone(out, t, .12 * dec, 220 * rate, 160 * rate, .6, 'triangle'); noise(out, t, .2 * dec, 'highpass', 1500 * rate, .7, .7); }
  else if (has('clap') || has('snap')){ [0, .011, .022].forEach(o => noise(out, t + o, .025, 'bandpass', 1200 * rate, 1.2, .8)); noise(out, t + .03, .18 * dec, 'bandpass', 1100 * rate, 1, .6); }
  else if (has('hat op') || has('ride') || has('crash')){ noise(out, t, (has('crash') ? 1.2 : has('ride') ? .9 : .4) * dec, 'highpass', (has('ride') ? 5000 : 7000) * rate, .8, .5); }
  else if (has('hat') || has('shaker') || has('tamb')) noise(out, t, (has('shaker') ? .09 : .06) * dec, 'highpass', 8000 * Math.min(rate, 2), .8, .55);
  else if (has('tom') || has('conga')){ const f = has('lo') ? 95 : has('hi') ? 180 : has('conga') ? 260 : 130; tone(out, t, .35 * dec, f * 1.4 * rate, f * rate, .9); }
  else if (has('rim') || has('clave') || has('wood')){ tone(out, t, .05 * dec, (has('clave') ? 2500 : has('wood') ? 900 : 1700) * rate, 0, .7, 'triangle'); }
  else if (has('cowbell')){ tone(out, t, .3 * dec, 560 * rate, 0, .35, 'square'); tone(out, t, .3 * dec, 845 * rate, 0, .3, 'square'); }
  else if (has('bass')) tone(out, t, .6 * dec, 55 * rate, 0, 1, 'triangle');
  else if (has('stab') || has('hit')){ [1, 1.26, 1.5].forEach(m => tone(out, t, .3 * dec, 220 * m * rate, 0, .25, 'sawtooth')); }
  else if (has('zap')) tone(out, t, .2 * dec, 2000 * rate, 80 * rate, .5, 'square');
  else { tone(out, t, .15 * dec, 400 * rate, 300 * rate, .5, 'triangle'); noise(out, t, .05, 'bandpass', 3000, 1, .3); }
}
function audition(i, level, val){
  if (!ac) ensureAudio();
  voice(i, level, val, ac.currentTime + .005);
  midiNote(i, level, performance.now());
}

/* ---- transport ---- */
const play = {on:false, list:[], li:0, tick:0, next:0, timer:0, marks:[], mode:'seg'};
function songSequence(n, depth = 0, state = null){
  const song = P.songs[n]; const out = [];
  if (!song) return out;
  const st = state || {tempo:song.tempo || 90, mix:ui.mix};
  const steps = song.steps;
  const matchEnd = i => { let d = 0; for (let j = i + 1; j < steps.length; j++){ if (steps[j].k === 'rep') d++; else if (steps[j].k === 'rend'){ if (!d) return j; d--; } } return steps.length; };
  let ended = false;
  const run = (a, b) => {
    for (let i = a; i < b && !ended && out.length < 4000; i++){
      const s = steps[i];
      if (s.k === 'seg'){ if (P.segments[s.v]) out.push({seg:s.v, tempo:st.tempo, mix:st.mix, idx:i, song:n}); }
      else if (s.k === 'tempo') st.tempo = clamp(Number(s.v) || 90, 30, 250);
      else if (s.k === 'mix') st.mix = clamp((s.v | 0) - 1, 0, NMIX - 1);
      else if (s.k === 'rep'){ const e = matchEnd(i), c = clamp(s.v | 0, 1, 99); for (let r = 0; r < c && !ended; r++) run(i + 1, e); i = e; }
      else if (s.k === 'sub'){ if (depth < 6 && s.v !== n) out.push(...songSequence(s.v | 0, depth + 1, st).map(o => ({...o, idx:i, song:n}))); }
      else if (s.k === 'link'){ if (depth < 6 && s.v !== n) out.push(...songSequence(s.v | 0, depth + 1, st).map(o => ({...o, idx:i, song:n}))); ended = true; }
      else if (s.k === 'end') ended = true;
    }
  };
  run(0, steps.length);
  return out;
}
function start(mode){
  ensureAudio(); stop(true);
  if (mode === 'song'){
    play.list = songSequence(ui.song);
    if (!play.list.length){ toast('This song has no segments to play'); return; }
  } else { const g = curSeg(); play.list = [{seg:ui.seg, tempo:g.tempo, mix:ui.mix}]; }
  play.mode = mode; play.on = true; play.li = 0; play.tick = mode === 'seg' ? ui.cursor : 0; play.next = ac.currentTime + .08; play.marks = [];
  if ($('#clkOut').checked) midiSend([0xFA], performance.now() + 80);
  play.timer = setInterval(pump, 20); pump();
  $(mode === 'song' ? '#playSong' : '#playSeg').classList.add('on');
  requestAnimationFrame(frame);
}
function stop(quiet){
  if (play.on && $('#clkOut').checked) midiSend([0xFC]);
  clearInterval(play.timer); play.on = false; ui.playTick = -1; ui.step = -1;
  $('#playSeg').classList.remove('on'); $('#playSong').classList.remove('on');
  if (!quiet){ drawRoll(); updateLcd(); if ($('#v-song').classList.contains('on')) renderSong(); }
}
function pump(){
  while (play.on && play.next < ac.currentTime + .12){
    const e = play.list[play.li], g = P.segments[e.seg];
    if (!g){ advance(); continue; }
    for (const ev of g.events) if (playedTick(g, ev.t) === play.tick){
      voice(ev.s, ev.l, ev.u, play.next, e.mix);
      if ($('#noteOut').checked) midiNote(ev.s, ev.l, performance.now() + (play.next - ac.currentTime) * 1000);
    }
    if ($('#clkOut').checked) midiSend([0xF8], performance.now() + (play.next - ac.currentTime) * 1000);
    play.marks.push([play.next, play.li, play.tick]);
    play.next += 60 / (e.tempo * PPQ);
    if (++play.tick >= segTicks(g)) advance();
  }
}
function advance(){
  play.tick = 0;
  if (play.mode === 'song'){ if (++play.li >= play.list.length){ const end = play.next; setTimeout(() => stop(), Math.max(0, (end - ac.currentTime) * 1000)); play.on = false; clearInterval(play.timer); } }
}
function frame(){
  if (!ac || (!play.on && !play.marks.length)) return;
  const now = ac.currentTime; let m = null;
  while (play.marks.length && play.marks[0][0] <= now) m = play.marks.shift();
  if (m){
    const e = play.list[m[1]];
    if (e && e.seg !== ui.seg && play.mode === 'song'){ selectSegment(e.seg); }
    ui.playTick = e && e.seg === ui.seg ? m[2] : -1;
    if (play.mode === 'song' && e && ui.step !== e.idx){ ui.step = e.idx; if ($('#v-song').classList.contains('on')) renderSong(); }
    drawRoll(); updateLcd();
  }
  if (play.on || play.marks.length) requestAnimationFrame(frame);
}
function updateLcd(){
  const g = curSeg(), t = ui.playTick >= 0 ? ui.playTick : ui.cursor;
  $('#lcd').textContent = `SEG ${String(ui.seg).padStart(2, '0')} ${fmtPos(g, t)}  ${g.tempo} BPM`;
}
$('#playSeg').onclick = () => start('seg');
$('#playSong').onclick = () => start('song');
$('#stopBtn').onclick = () => stop();

/* ================= segment bar ================= */
function fillSelect(sel, n, from, label, used){
  sel.innerHTML = ''; for (let i = from; i < n; i++){ const o = document.createElement('option'); o.value = i; o.textContent = label(i) + (used && used(i) ? ' •' : ''); sel.appendChild(o); }
}
function renderSegBar(){
  fillSelect($('#segSel'), NSEG, 0, i => String(i).padStart(2, '0'), i => P.segments[i] && P.segments[i].events.length);
  $('#segSel').value = ui.seg;
  const g = curSeg();
  $('#segLen').value = g.len; $('#segNum').value = g.num; $('#segDen').value = g.den; $('#segTempo').value = g.tempo; $('#segSwing').value = g.swing;
}
function selectSegment(n){ ui.seg = n; ui.sel.clear(); ui.cursor = 0; renderSegBar(); refreshSel(); layoutRoll(); updateLcd(); }
$('#segSwing').innerHTML = SWINGS.map(([v, l]) => `<option value="${v}">${l}</option>`).join('');
$('#segSel').onchange = e => { if (play.on && play.mode === 'seg') stop(); selectSegment(Number(e.target.value)); };
function segField(id, fn){ $(id).addEventListener('change', e => { pushUndo(); fn(curSeg(), e.target.value); const g = curSeg(); g.events = g.events.filter(ev => ev.t < segTicks(g)); dedupe(g); renderSegBar(); refreshSel(); layoutRoll(); updateLcd(); changed(); }); }
segField('#segLen', (g, v) => g.len = clamp(Math.round(v) || 1, 1, 99));
segField('#segNum', (g, v) => g.num = clamp(Math.round(v) || 4, 1, 16));
segField('#segDen', (g, v) => g.den = Number(v));
segField('#segTempo', (g, v) => g.tempo = clamp(Math.round(Number(v) * 10) / 10 || 90, 30, 250));
segField('#segSwing', (g, v) => g.swing = Number(v));
$('#segClear').onclick = () => { if (!curSeg().events.length) return; pushUndo(); curSeg().events = []; ui.sel.clear(); renderSegBar(); refreshSel(); layoutRoll(); changed(); };
$('#segCopy').onclick = () => {
  const v = prompt('Copy segment ' + String(ui.seg).padStart(2, '0') + ' to segment number (0–99):'); if (v === null) return;
  const n = Math.round(Number(v)); if (!(n >= 0 && n < NSEG) || n === ui.seg) return toast('Pick another segment number from 0 to 99');
  if (P.segments[n] && P.segments[n].events.length && !confirm('Segment ' + n + ' has notes. Replace it?')) return;
  pushUndo(); P.segments[n] = JSON.parse(JSON.stringify(curSeg())); selectSegment(n); changed(); toast('Copied to segment ' + String(n).padStart(2, '0'));
};
function setTool(t){ ui.tool = t; $$('#tools button').forEach(b => b.classList.toggle('on', b.dataset.t === t)); }
$$('#tools button').forEach(b => b.onclick = () => setTool(b.dataset.t));
$('#snap').onchange = e => { ui.snap = Number(e.target.value); drawRoll(); };
$('#zoom').oninput = e => { ui.zoom = Number(e.target.value); layoutRoll(); };
$('#lane').onchange = e => { ui.lane = e.target.value; drawRoll(); };
$('#compact').onchange = e => { ui.compact = e.target.checked; layoutRoll(); };
$('#undo').onclick = undo; $('#redo').onclick = redo;

/* ================= songs ================= */
const STEP_NAMES = {seg:'Segment', rep:'Repeat start', rend:'Repeat end', tempo:'Tempo', mix:'Mix', sub:'Sub-song', link:'Link to song', end:'End'};
const curSong = () => P.songs[ui.song] || (P.songs[ui.song] = {name:'SONG ' + String(ui.song).padStart(2, '0'), tempo:90, steps:[]});
const fmtTime = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
function renderSong(){
  fillSelect($('#songSel'), NSONG, 0, i => String(i).padStart(2, '0') + (P.songs[i] ? ' ' + P.songs[i].name : ''), null);
  $('#songSel').value = ui.song;
  const song = curSong(); $('#songName').value = song.name; $('#songTempo').value = song.tempo;
  // bars and time after each step, following repeats, sub-songs and tempo changes
  const seq = songSequence(ui.song), at = {}; let bars = 0, secs = 0;
  for (const e of seq){ const g = P.segments[e.seg]; bars += g.len; secs += segTicks(g) * 60 / (e.tempo * PPQ); at[e.idx] = [bars, secs]; }
  let last = [0, 0];
  $('#steps').innerHTML = '';
  song.steps.forEach((s, i) => {
    if (at[i]) last = at[i];
    const tr = document.createElement('tr'); if (i === ui.step) tr.className = 'cur';
    const hasVal = !['rend', 'end'].includes(s.k);
    const seg = s.k === 'seg' ? P.segments[s.v] : null;
    tr.innerHTML = `<td class="n">${String(i + 1).padStart(2, '0')}</td><td>${STEP_NAMES[s.k]}${seg ? ` <span class="badge">${seg.len} bar${seg.len > 1 ? 's' : ''} ${seg.num}/${seg.den}</span>` : s.k === 'seg' ? ' <span class="badge">empty</span>' : ''}</td>
      <td>${hasVal ? `<input type="number" value="${s.v}" step="${s.k === 'tempo' ? .1 : 1}" style="width:76px">` : ''}</td>
      <td class="n">${last[0]}</td><td class="n">${fmtTime(last[1])}</td>
      <td style="white-space:nowrap"><button data-a="up" title="Move up">↑</button> <button data-a="dn" title="Move down">↓</button> <button data-a="sel" title="Insert new steps after this one">${ui.stepIns === i ? '◉' : '○'}</button> <button data-a="del" title="Remove">✕</button></td>`;
    const inp = tr.querySelector('input');
    if (inp) inp.onchange = () => { pushUndo(); s.v = s.k === 'tempo' ? clamp(Number(inp.value), 30, 250) : clamp(Math.round(Number(inp.value)), s.k === 'mix' ? 1 : s.k === 'rep' ? 1 : 0, s.k === 'mix' ? 8 : 99); renderSong(); changed(); };
    tr.querySelectorAll('button').forEach(b => b.onclick = () => {
      const a = b.dataset.a;
      if (a === 'sel'){ ui.stepIns = ui.stepIns === i ? undefined : i; return renderSong(); }
      pushUndo();
      if (a === 'del') song.steps.splice(i, 1);
      if (a === 'up' && i > 0) [song.steps[i - 1], song.steps[i]] = [song.steps[i], song.steps[i - 1]];
      if (a === 'dn' && i < song.steps.length - 1) [song.steps[i + 1], song.steps[i]] = [song.steps[i], song.steps[i + 1]];
      renderSong(); changed();
    });
    $('#steps').appendChild(tr);
  });
  $('#songTotal').textContent = `${bars} bars · ${fmtTime(secs)}`;
}
$('#songSel').onchange = e => { ui.song = Number(e.target.value); ui.stepIns = undefined; renderSong(); };
$('#songName').onchange = e => { pushUndo(); curSong().name = e.target.value.slice(0, 16); renderSong(); changed(); };
$('#songTempo').onchange = e => { pushUndo(); curSong().tempo = clamp(Number(e.target.value) || 90, 30, 250); renderSong(); changed(); };
$('#stepKind').onchange = e => { const k = e.target.value; $('#stepVal').value = k === 'seg' ? ui.seg : k === 'tempo' ? curSong().tempo : k === 'rep' ? 2 : 1; $('#stepVal').disabled = k === 'rend' || k === 'end'; };
$('#stepAdd').onclick = () => {
  const k = $('#stepKind').value, song = curSong(); let v = Number($('#stepVal').value);
  v = k === 'tempo' ? clamp(v, 30, 250) : k === 'mix' ? clamp(Math.round(v), 1, 8) : clamp(Math.round(v), k === 'rep' ? 1 : 0, 99);
  pushUndo();
  const step = ['rend', 'end'].includes(k) ? {k} : {k, v};
  const at = ui.stepIns !== undefined ? ui.stepIns + 1 : song.steps.length;
  song.steps.splice(at, 0, step); if (ui.stepIns !== undefined) ui.stepIns = at;
  renderSong(); changed();
};
$('#songFromSegs').onclick = () => {
  const used = P.segments.map((g, i) => g && g.events.length ? i : -1).filter(i => i >= 0); if (!used.length) return toast('No segments with notes yet');
  let n = P.songs.findIndex((s, i) => i > 0 && !s); if (n < 0) return toast('All 99 songs are in use');
  pushUndo(); P.songs[n] = {name:'SONG ' + String(n).padStart(2, '0'), tempo:P.segments[used[0]].tempo, steps:used.map(v => ({k:'seg', v}))};
  ui.song = n; renderSong(); changed(); toast('Song ' + String(n).padStart(2, '0') + ' made from ' + used.length + ' segments');
};
$('#songClear').onclick = () => { pushUndo(); curSong().steps = []; renderSong(); changed(); };

/* ================= mixes ================= */
function renderMix(){
  fillSelect($('#mixSel'), NMIX, 0, i => 'Mix ' + (i + 1)); $('#mixSel').value = ui.mix;
  fillSelect($('#mixTo'), NMIX, 0, i => 'Mix ' + (i + 1));
  fillSelect($('#mixSndTo'), NMIX, 0, i => 'Mix ' + (i + 1));
  fillSelect($('#mixSnd'), NSND, 0, i => sndLabel(i) + ' ' + P.sounds[i].name);
  $('#mixBank').value = ui.mixBank;
  const grid = $('#mixGrid'); grid.innerHTML = '';
  for (let p = 0; p < 8; p++){
    const i = ui.mixBank * 8 + p, d = document.createElement('div'); d.className = 'ch';
    d.innerHTML = `<b>${P.mixes[ui.mix][i]}</b><input type="range" min="0" max="31" value="${P.mixes[ui.mix][i]}"><span>${sndLabel(i)}</span><span>${P.sounds[i].name}</span>`;
    const r = d.querySelector('input');
    r.onpointerdown = () => pushUndo();
    r.oninput = () => { P.mixes[ui.mix][i] = Number(r.value); d.querySelector('b').textContent = r.value; changed(); };
    r.onchange = () => audition(i, 28, 0);
    grid.appendChild(d);
  }
}
$('#mixSel').onchange = e => { ui.mix = Number(e.target.value); renderMix(); };
$('#mixBank').onchange = e => { ui.mixBank = Number(e.target.value); renderMix(); };
$('#mixCopy').onclick = () => { const to = Number($('#mixTo').value); if (to === ui.mix) return; pushUndo(); P.mixes[to] = P.mixes[ui.mix].slice(); changed(); toast(`Mix ${ui.mix + 1} copied to mix ${to + 1}`); };
$('#mixSndCopy').onclick = () => { const s = Number($('#mixSnd').value), to = Number($('#mixSndTo').value); pushUndo(); P.mixes[to][s] = P.mixes[ui.mix][s]; changed(); toast(`${sndLabel(s)} level copied to mix ${to + 1}`); };

/* ================= sounds ================= */
const wave = $('#wave'), wctx = wave.getContext('2d');
function renderPads(){
  const box = $('#sndPads'); box.innerHTML = '';
  for (let i = 0; i < NSND; i++){
    const s = P.sounds[i], b = document.createElement('button'); b.className = 'pad' + (i === ui.sound ? ' on' : '');
    b.innerHTML = `<small>${sndLabel(i)}${s.mode === 'decay' ? ' · decay' : ''}</small><span>${s.name}</span>${s.sample ? '<i>sample</i>' : ''}`;
    b.onclick = () => { ui.sound = i; audition(i, 28, 0); renderSound(); };
    box.appendChild(b);
  }
}
function renderSound(){
  renderPads();
  const s = P.sounds[ui.sound];
  $('#sndTitle').textContent = sndLabel(ui.sound);
  $('#sndName').value = s.name; $('#sndMode').value = s.mode; $('#sndTune').value = s.tune; $('#sndDecay').value = s.decay;
  const n = s.sample ? s.sample.data.length : 0;
  ['#sStart', '#sEnd', '#sLoop', '#sndWav', '#sndClear'].forEach(id => $(id).disabled = !n);
  $('#sStart').value = n ? s.start : ''; $('#sEnd').value = n ? s.end : ''; $('#sLoop').value = n ? s.loop : '';
  $('#sInfo').textContent = n ? `${n} samples · ${(n / s.sample.rate).toFixed(3)} s · ${Math.round(s.sample.rate)} Hz` : 'built-in voice';
  drawWave();
}
function drawWave(){
  if (!P) return;
  const dpr = devicePixelRatio || 1, W = wave.clientWidth, H = wave.clientHeight; if (!W) return;
  wave.width = W * dpr; wave.height = H * dpr; wctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  wctx.fillStyle = colors.roll; wctx.fillRect(0, 0, W, H);
  const s = P.sounds[ui.sound];
  if (!s.sample){ wctx.fillStyle = colors.dim; wctx.font = '13px system-ui'; wctx.fillText('No sample loaded: this pad plays its built-in voice', 12, H / 2); return; }
  const d = s.sample.data, n = d.length, per = n / W;
  wctx.fillStyle = 'rgba(0,0,0,.35)';
  wctx.fillRect(0, 0, s.start / per, H); wctx.fillRect(s.end / per, 0, W, H);
  wctx.strokeStyle = colors.note; wctx.beginPath();
  for (let x = 0; x < W; x++){
    let lo = 1, hi = -1; const a = Math.floor(x * per), b = Math.max(a + 1, Math.floor((x + 1) * per));
    for (let i = a; i < b && i < n; i++){ const v = d[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
    wctx.moveTo(x + .5, H / 2 - hi * H / 2 * .95); wctx.lineTo(x + .5, H / 2 - lo * H / 2 * .95 + 1);
  }
  wctx.stroke();
  const mark = (pos, col, lbl) => { const x = Math.round(pos / per) + .5; wctx.strokeStyle = col; wctx.fillStyle = col; wctx.beginPath(); wctx.moveTo(x, 0); wctx.lineTo(x, H); wctx.stroke(); wctx.fillRect(x - 8, 0, 16, 16); wctx.fillStyle = '#000'; wctx.font = 'bold 11px system-ui'; wctx.fillText(lbl, x - 4, 12); };
  mark(s.start, colors.ok, 'S'); mark(s.end, colors.acc, 'E'); if (s.loop >= 0) mark(s.loop, colors.swing, 'L');
}
let wdrag = null;
wave.addEventListener('pointerdown', ev => {
  const s = P.sounds[ui.sound]; if (!s.sample) return;
  const per = s.sample.data.length / wave.clientWidth, x = ev.offsetX, pos = Math.round(x * per);
  const cand = [['start', s.start], ['end', s.end]].concat(s.loop >= 0 ? [['loop', s.loop]] : []);
  cand.sort((a, b) => Math.abs(a[1] / per - x) - Math.abs(b[1] / per - x));
  wdrag = Math.abs(cand[0][1] / per - x) < 14 ? cand[0][0] : ev.shiftKey ? 'loop' : (Math.abs(pos - s.start) < Math.abs(pos - s.end) ? 'start' : 'end');
  pushUndo(); wave.setPointerCapture(ev.pointerId); moveMarker(x);
});
wave.addEventListener('pointermove', ev => { if (wdrag) moveMarker(ev.offsetX); });
wave.addEventListener('pointerup', () => { if (wdrag){ wdrag = null; changed(); } });
function moveMarker(x){
  const s = P.sounds[ui.sound], n = s.sample.data.length, pos = clamp(Math.round(x * n / wave.clientWidth), 0, n);
  if (wdrag === 'start') s.start = clamp(pos, 0, s.end - 1);
  if (wdrag === 'end') s.end = clamp(pos, s.start + 1, n);
  if (wdrag === 'loop') s.loop = clamp(pos, s.start, s.end - 1);
  $('#sStart').value = s.start; $('#sEnd').value = s.end; $('#sLoop').value = s.loop; drawWave();
}
function sndField(id, fn){ $(id).addEventListener('change', e => { pushUndo(); fn(P.sounds[ui.sound], e.target.value); renderSound(); refreshSel(); drawRoll(); changed(); }); }
sndField('#sndName', (s, v) => s.name = v.slice(0, 12) || DEFAULT_NAMES[ui.sound]);
sndField('#sndMode', (s, v) => s.mode = v);
sndField('#sndTune', (s, v) => s.tune = clamp(Math.round(v), -8, 7));
sndField('#sndDecay', (s, v) => s.decay = clamp(Math.round(v), 0, 31));
sndField('#sStart', (s, v) => s.start = clamp(Math.round(v), 0, s.end - 1));
sndField('#sEnd', (s, v) => s.end = clamp(Math.round(v), s.start + 1, s.sample.data.length));
sndField('#sLoop', (s, v) => s.loop = Number(v) < 0 ? -1 : clamp(Math.round(v), s.start, s.end - 1));
$('#sndPlay').onclick = () => audition(ui.sound, 31, 0);
$('#sndFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  ensureAudio();
  try {
    const buf = await ac.decodeAudioData(await f.arrayBuffer());
    let mono = new Float32Array(buf.length);
    for (let c = 0; c < buf.numberOfChannels; c++){ const d = buf.getChannelData(c); for (let i = 0; i < d.length; i++) mono[i] += d[i] / buf.numberOfChannels; }
    let rate = buf.sampleRate;
    if ($('#spConv').checked){ mono = spConvert(mono, rate); rate = SP_SR; }
    pushUndo();
    const s = P.sounds[ui.sound];
    s.sample = {rate, data:mono}; s.start = 0; s.end = mono.length; s.loop = -1;
    s.name = f.name.replace(/\.[^.]+$/, '').slice(0, 12);
    renderSound(); drawRoll(); changed(); audition(ui.sound, 31, 0);
  } catch (err){ toast('Could not read that audio file'); }
};
// resample to the SP rate (linear, after a gentle low-pass) and truncate to 12 bits
function spConvert(d, sr){
  const out = new Float32Array(Math.floor(d.length * SP_SR / sr)), a = Math.exp(-2 * Math.PI * 11000 / sr);
  const f = new Float32Array(d.length); let y = 0, y2 = 0; for (let i = 0; i < d.length; i++){ y = (1 - a) * d[i] + a * y; y2 = (1 - a) * y + a * y2; f[i] = y2; }
  let pk = 1e-6; for (const v of f) pk = Math.max(pk, Math.abs(v));
  for (let k = 0; k < out.length; k++){ const x = k * sr / SP_SR, i = Math.floor(x), fr = x - i; const v = ((f[i] || 0) * (1 - fr) + (f[i + 1] || 0) * fr) / pk * .98; out[k] = clamp(Math.floor(v * 2048), -2048, 2047) / 2048; }
  return out;
}
function wavBytes(data, rate){
  const n = data.length, b = new DataView(new ArrayBuffer(44 + n * 2)), w = (o, s) => [...s].forEach((c, i) => b.setUint8(o + i, c.charCodeAt(0)));
  w(0, 'RIFF'); b.setUint32(4, 36 + n * 2, true); w(8, 'WAVEfmt '); b.setUint32(16, 16, true); b.setUint16(20, 1, true); b.setUint16(22, 1, true);
  const r = Math.round(rate); b.setUint32(24, r, true); b.setUint32(28, r * 2, true); b.setUint16(32, 2, true); b.setUint16(34, 16, true); w(36, 'data'); b.setUint32(40, n * 2, true);
  for (let i = 0; i < n; i++) b.setInt16(44 + i * 2, clamp(Math.round(data[i] * 32767), -32768, 32767), true);
  return b.buffer;
}
function download(name, data, type){ const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([data], {type})); a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000); }
$('#sndWav').onclick = () => { const s = P.sounds[ui.sound]; if (!s.sample) return; download(`${sndLabel(ui.sound)} ${s.name}.wav`, wavBytes(s.sample.data.subarray(s.start, s.end), s.sample.rate), 'audio/wav'); };
$('#sndClear').onclick = () => { pushUndo(); const s = P.sounds[ui.sound]; s.sample = null; s.start = s.end = 0; s.loop = -1; renderSound(); changed(); };

/* ================= MIDI ================= */
const midi = {access:null, inp:null, out:null, clk:0, running:false, lastClock:0, sysex:[], cur:null};
function log(msg){ const l = $('#log'); l.textContent = (msg + '\n' + l.textContent).slice(0, 8000); }
async function midiEnable(){
  if (!navigator.requestMIDIAccess){ $('#midiState').textContent = 'Web MIDI not available in this browser'; return; }
  try { midi.access = await navigator.requestMIDIAccess({sysex:true}); }
  catch { try { midi.access = await navigator.requestMIDIAccess(); toast('SysEx not allowed: notes and clock only'); } catch { $('#midiState').textContent = 'permission denied'; return; } }
  midi.access.onstatechange = midiPorts; midiPorts();
  $('#midiState').textContent = 'on'; $('#midiState').classList.add('ok'); $('#midiOn').disabled = true;
}
function midiPorts(){
  const fill = (sel, ports, keep) => { sel.innerHTML = '<option value="">—</option>' + [...ports.values()].map(p => `<option value="${p.id}">${p.name}</option>`).join(''); if (keep) sel.value = keep; };
  fill($('#midiIn'), midi.access.inputs, midi.inp && midi.inp.id); fill($('#midiOut'), midi.access.outputs, midi.out && midi.out.id);
  if (!midi.inp && midi.access.inputs.size){ $('#midiIn').selectedIndex = 1; pickIn(); }
  if (!midi.out && midi.access.outputs.size){ $('#midiOut').selectedIndex = 1; pickOut(); }
}
function pickIn(){ if (midi.inp) midi.inp.onmidimessage = null; midi.inp = midi.access.inputs.get($('#midiIn').value) || null; if (midi.inp) midi.inp.onmidimessage = onMidi; }
function pickOut(){ midi.out = midi.access.outputs.get($('#midiOut').value) || null; }
$('#midiOn').onclick = midiEnable; $('#midiIn').onchange = pickIn; $('#midiOut').onchange = pickOut;
function midiSend(bytes, at){ if (midi.out) try { midi.out.send(bytes, at); } catch {} }
function midiNote(i, level, at){
  if (!midi.out || !$('#noteOut').checked) return;
  const ch = clamp(Number($('#midiCh').value) - 1, 0, 15), note = clamp(Number($('#midiBase').value) + i, 0, 127), vel = clamp(Math.round(level / 31 * 126) + 1, 1, 127);
  midiSend([0x90 | ch, note, vel], at); midiSend([0x80 | ch, note, 0], at + 60);
}
function onMidi(m){
  const d = m.data, st = d[0];
  if (st === 0xF0){ if ($('#sxCap').checked){ midi.sysex.push(new Uint8Array(d)); $('#sxInfo').textContent = `${midi.sysex.length} messages · ${midi.sysex.reduce((a, b) => a + b.length, 0)} bytes`; } log(`SysEx ${d.length} bytes: ${[...d.slice(0, 12)].map(x => x.toString(16).padStart(2, '0')).join(' ')}${d.length > 12 ? ' …' : ''}`); return; }
  if (st >= 0xF8){
    if (!$('#followClk').checked) return;
    if (st === 0xF8){ midi.lastClock = performance.now(); if (midi.running){ midi.clk++; extPlayhead(); } }
    else if (st === 0xFA){ midi.clk = 0; midi.running = true; extPlayhead(); log('Start'); }
    else if (st === 0xFB){ midi.running = true; log('Continue'); }
    else if (st === 0xFC){ midi.running = false; log('Stop'); ui.playTick = -1; drawRoll(); updateLcd(); }
    return;
  }
  if (st === 0xF2 && $('#followClk').checked){ midi.clk = (d[1] | (d[2] << 7)) * 6; extPlayhead(); log('Song position ' + (d[1] | (d[2] << 7))); return; }
  const k = st & 0xF0;
  if (k === 0x90 && d[2] > 0){
    const i = d[1] - Number($('#midiBase').value);
    log(`Note ${d[1]} vel ${d[2]} ch ${(st & 15) + 1}${i >= 0 && i < NSND ? ' → ' + sndLabel(i) : ''}`);
  }
}
let extDraw = 0;
function extPlayhead(){
  if (play.on) return;
  const g = curSeg(); ui.playTick = midi.clk % segTicks(g);
  if (!extDraw) extDraw = requestAnimationFrame(() => { extDraw = 0; drawRoll(); updateLcd(); });
}
setInterval(() => { const live = performance.now() - midi.lastClock < 600; $('#clkBadge').textContent = live ? 'CLOCK ' + (midi.running ? 'RUN' : 'IDLE') : 'CLOCK —'; $('#clkBadge').classList.toggle('ok', live); }, 300);
$('#sxSave').onclick = () => { if (!midi.sysex.length) return toast('Nothing captured yet'); const n = midi.sysex.reduce((a, b) => a + b.length, 0), all = new Uint8Array(n); let o = 0; midi.sysex.forEach(b => { all.set(b, o); o += b.length; }); download('sp-capture.syx', all, 'application/octet-stream'); };
$('#sxClear').onclick = () => { midi.sysex = []; $('#sxInfo').textContent = '0 messages'; };
$('#sxFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  if (!midi.out) return toast('Pick a MIDI output first');
  const b = new Uint8Array(await f.arrayBuffer()), msgs = []; let s = -1;
  for (let i = 0; i < b.length; i++){ if (b[i] === 0xF0) s = i; else if (b[i] === 0xF7 && s >= 0){ msgs.push(b.slice(s, i + 1)); s = -1; } }
  if (!msgs.length) return toast('No SysEx messages in that file');
  // pace the messages so a 31.25 kbaud DIN link keeps up (320 µs per byte, plus a gap)
  let t = performance.now();
  for (const m of msgs){ midiSend(m, t); t += m.length * .32 + 20; }
  toast(`Sending ${msgs.length} SysEx message${msgs.length > 1 ? 's' : ''}`);
};

/* ================= Standard MIDI files ================= */
function vlq(n){ const b = [n & 0x7F]; while ((n >>= 7)) b.unshift((n & 0x7F) | 0x80); return b; }
function exportMidi(){
  const g = curSeg(), ch = clamp(Number($('#midiCh').value) - 1, 0, 15), base = Number($('#midiBase').value), us = Math.round(60e6 / g.tempo);
  const evs = [];
  for (const e of g.events){ const t = playedTick(g, e.t), n = clamp(base + e.s, 0, 127), v = clamp(Math.round(e.l / 31 * 126) + 1, 1, 127); evs.push([t, 1, [0x90 | ch, n, v]], [t + 6, 0, [0x80 | ch, n, 0]]); }
  evs.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const trk = [0, 0xFF, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255, 0, 0xFF, 0x58, 4, g.num, Math.log2(g.den), 24, 8];
  const name = [...`SEG ${String(ui.seg).padStart(2, '0')}`].map(c => c.charCodeAt(0)); trk.push(0, 0xFF, 0x03, name.length, ...name);
  let last = 0; for (const [t, , m] of evs){ trk.push(...vlq(t - last), ...m); last = t; }
  trk.push(...vlq(Math.max(0, segTicks(g) - last)), 0xFF, 0x2F, 0);
  const hdr = [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, PPQ], th = [0x4D, 0x54, 0x72, 0x6B, trk.length >>> 24, trk.length >> 16 & 255, trk.length >> 8 & 255, trk.length & 255];
  download(`${P.name} seg${String(ui.seg).padStart(2, '0')}.mid`, new Uint8Array([...hdr, ...th, ...trk]), 'audio/midi');
}
function parseMidi(buf){
  const b = new Uint8Array(buf), dv = new DataView(buf); let p = 0;
  const str = n => String.fromCharCode(...b.subarray(p, p + n));
  if (str(4) !== 'MThd') throw new Error('Not a MIDI file');
  const ntr = dv.getUint16(10), div = dv.getUint16(12); if (div & 0x8000) throw new Error('SMPTE time not supported');
  p = 8 + dv.getUint32(4);
  const notes = []; let tempo = 0, num = 4, den = 4;
  const rd = () => { let v = 0, c; do { c = b[p++]; v = (v << 7) | (c & 0x7F); } while (c & 0x80); return v; };
  for (let tr = 0; tr < ntr && p < b.length; tr++){
    if (str(4) !== 'MTrk') break; const end = p + 8 + dv.getUint32(p + 4); p += 8; let t = 0, run = 0;
    while (p < end){
      t += rd(); let st = b[p];
      if (st & 0x80) p++; else st = run;
      if (st === 0xFF){ const ty = b[p++], len = rd(); if (ty === 0x51 && !tempo) tempo = 60e6 / ((b[p] << 16) | (b[p + 1] << 8) | b[p + 2]); if (ty === 0x58){ num = b[p]; den = 1 << b[p + 1]; } p += len; continue; }
      if (st === 0xF0 || st === 0xF7){ p += rd(); continue; }
      run = st; const k = st & 0xF0, a = b[p++], c = (k === 0xC0 || k === 0xD0) ? 0 : b[p++];
      if (k === 0x90 && c > 0) notes.push({t:t * PPQ / div, n:a, v:c});
    }
    p = end;
  }
  return {notes, tempo, num, den};
}
$('#midExp').onclick = exportMidi;
$('#midImp').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try {
    const m = parseMidi(await f.arrayBuffer()), base = Number($('#midiBase').value), g = curSeg();
    const evs = m.notes.map(o => ({t:Math.round(o.t), s:o.n - base, l:clamp(Math.round(o.v / 127 * 31), 0, 31), u:0})).filter(o => o.s >= 0 && o.s < NSND);
    if (!evs.length) return toast(`No notes in the range ${base}–${base + 31}; set First note in MIDI / Hardware`);
    pushUndo();
    if ([4, 8, 16].includes(m.den)){ g.num = clamp(m.num, 1, 16); g.den = m.den; }
    if (m.tempo) g.tempo = clamp(Math.round(m.tempo * 10) / 10, 30, 250);
    g.swing = 12; g.len = clamp(Math.ceil((Math.max(...evs.map(o => o.t)) + 1) / segTicksPerBar(g)), 1, 99);
    g.events = evs.filter(o => o.t < segTicks(g)); ui.sel.clear(); dedupe(g);
    renderSegBar(); refreshSel(); layoutRoll(); changed(); backup('MIDI import');
    toast(`${g.events.length} notes imported into segment ${String(ui.seg).padStart(2, '0')}`);
  } catch (err){ toast(err.message || 'Could not read that MIDI file'); }
};

/* ================= project ================= */
function refreshAll(){
  readColors(); fillSelect($('#iSnd'), NSND, 0, i => sndLabel(i) + ' ' + P.sounds[i].name);
  renderSegBar(); refreshSel(); layoutRoll(); updateLcd();
  renderSong(); renderMix(); renderSound();
  $('#projName').value = P.name; $('#projModel').value = P.model;
  bufCache.clear();
}
function loadProject(p, reason){
  stop(true); P = p; ui.sel.clear(); undoStack.length = redoStack.length = 0;
  ui.seg = P.segments.findIndex(g => g && g.events.length); if (ui.seg < 0) ui.seg = 1;
  refreshAll(); changed(); if (reason) backup(reason);
}
$('#projName').onchange = e => { P.name = e.target.value || 'Untitled'; changed(); };
$('#projModel').onchange = e => { P.model = e.target.value; changed(); };
$('#pNew').onclick = () => { if (confirm('Start a new project? The current one stays in the backups.')){ backup('Before new project').then(() => loadProject(newProject(), null)); } };
$('#pSave').onclick = () => { const json = serialize(); download((P.name || 'project') + '.sp24', json, 'application/json'); backup('Saved', json); };
$('#pOpen').onchange = async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try { loadProject(deserialize(await f.text()), 'Opened ' + f.name); toast('Opened ' + f.name); } catch (err){ toast(err.message || 'Could not open that file'); }
};
async function renderBackups(){
  let all = []; try { all = await DB.listBackups(); } catch {}
  all.sort((a, b) => b.time - a.time);
  const tb = $('#backups'); tb.innerHTML = all.length ? '' : '<tr><td class="hint">No backups yet</td></tr>';
  for (const b of all){
    const tr = document.createElement('tr');
    tr.innerHTML = `<td class="n">${new Date(b.time).toLocaleString()}</td><td>${b.name}</td><td>${b.reason}</td><td><button>Restore</button></td>`;
    tr.querySelector('button').onclick = async () => {
      if (!confirm('Restore this backup? The current project is backed up first.')) return;
      await backup('Before restore');
      const full = await DB.getBackup(b.id); loadProject(deserialize(full.json), null); toast('Backup restored');
    };
    tb.appendChild(tr);
  }
}

/* ================= tabs, theme, start ================= */
$$('#tabs button').forEach(b => b.onclick = () => {
  $$('#tabs button').forEach(x => x.classList.toggle('on', x === b));
  $$('.view').forEach(v => v.classList.toggle('on', v.id === 'v-' + b.dataset.v));
  if (b.dataset.v === 'seq'){ fillSelect($('#iSnd'), NSND, 0, i => sndLabel(i) + ' ' + P.sounds[i].name); refreshSel(); layoutRoll(); }
  if (b.dataset.v === 'song') renderSong();
  if (b.dataset.v === 'mix') renderMix();
  if (b.dataset.v === 'snd') renderSound();
  if (b.dataset.v === 'proj') renderBackups();
});
function setTheme(t){ document.documentElement.dataset.theme = t; $('#theme').value = t; try { localStorage.setItem('sp24-theme', t); } catch {} readColors(); drawRoll(); drawWave(); }
$('#theme').onchange = e => setTheme(e.target.value);

(async function init(){
  let theme = 'classic'; try { theme = localStorage.getItem('sp24-theme') || theme; } catch {}
  document.documentElement.dataset.theme = theme; $('#theme').value = theme;
  let p = null;
  try { const j = await DB.get('current'); if (j) p = deserialize(j); } catch {}
  P = p || newProject();
  ui.seg = P.segments.findIndex(g => g && g.events.length); if (ui.seg < 0) ui.seg = 1;
  refreshAll(); renderBackups();
  if ('serviceWorker' in navigator && location.protocol !== 'file:') navigator.serviceWorker.register('sw.js').catch(() => {});
})();
// for tests and the console
window.SP24 = {get project(){ return P; }, ui, playedTick, swingTick, straightTick, songSequence, segTicks, parseMidi, fmtPos, parsePos};
