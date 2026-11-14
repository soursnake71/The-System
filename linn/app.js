/* Omega Linn app: the front panel (faceplate.js) wired to the firmware in the audio thread (worklet.js: engine.js +
   machine.js), the Omega pages (PADS, SOUND, PAD EQ, MIXER, FX, MASTER, PRESETS) and the no-scroll layout.
   io.js adds files, kits and MIDI on top of this (window.LINN). */
'use strict';
(() => {
const D = OLINN, M = OLMACH, PR = OLPRESETS, K = M.K, KP = M.KP;
const {P, PARAMS, NV, CH_KEYS, SOUNDS, vp, vq} = D;
const STORE = 'omega-linn-v1';
const $ = id => document.getElementById(id);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

/* ---------------- state ---------------- */
const S = {
  params:D.defaults(), mem:M.defaultMemory(), pat:0, song:0, mode:0, roms:{}, userPresets:[], preset:0, edited:false, metroAlways:false,
  midi:{in:'all', out:'', ch:9, sync:false, clockOut:false, notesOut:true, pc:true, trigNote:-1},
  disp:{d:{l:'  ', r:'11', song:false, tim:0, ec:2, play:false, ext:false, perc:false, sel:0}, running:false, tk:-1, len:1536, ppat:0, meter:[], perc:false},
  sel:0, power:false, gridPerc:false,
};
function backup(){ return {format:'omega-linn', version:1, mem:S.mem, params:S.params, pat:S.pat, song:S.song, mode:S.mode, roms:S.roms, userPresets:S.userPresets, preset:S.preset, midi:S.midi, metroAlways:S.metroAlways}; }
function validMem(m){ return m && Array.isArray(m.pats) && m.pats.length === M.NP && Array.isArray(m.songs) && m.songs.length === M.NP; }
function applyBackup(j, quiet){
  if (!j || j.format !== 'omega-linn') throw new Error('Not an Omega Linn backup');
  if (validMem(j.mem)){ S.mem = j.mem; if (typeof S.mem.repeat !== 'boolean') S.mem.repeat = true; }
  if (Array.isArray(j.params)) j.params.forEach((v, i) => { if (i < PARAMS.length && typeof v === 'number') S.params[i] = v; });
  for (const k of ['pat', 'song', 'mode', 'preset']) if (typeof j[k] === 'number') S[k] = j[k];
  if (j.roms && typeof j.roms === 'object') S.roms = j.roms;
  if (Array.isArray(j.userPresets)) S.userPresets = j.userPresets.filter(u => u && typeof u.name === 'string' && Array.isArray(u.p));
  if (j.midi) Object.assign(S.midi, j.midi);
  if (typeof j.metroAlways === 'boolean') S.metroAlways = j.metroAlways;
  if (!quiet){ sendAll(); refresh(); }
}
try { const j = JSON.parse(localStorage.getItem(STORE) || 'null'); if (j) applyBackup(j, true); } catch (e){ console.warn(e); }
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify(backup())); } catch (e){ console.warn('save', e); toast('Memory full: your loaded sounds are too big to keep here. Use STORE to save a file.'); } }, 400); }

/* ---------------- audio ---------------- */
let ctx = null, node = null;
const listeners = [];
let factory = null;                             // the ROM, parsed: layers per group
function post(m){ if (node) node.port.postMessage(m); }
const b64 = {enc(u8){ let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return btoa(s); },
  dec(s){ const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }};
// a layer kept in the backup: 16-bit samples as base64
function packLayer(L){ const i16 = new Int16Array(L.data.length); for (let i = 0; i < i16.length; i++) i16[i] = Math.max(-32768, Math.min(32767, Math.round(L.data[i] * 32767))); return {rate:L.rate, clk:L.clk, dyn:L.dyn ?? -1, data:b64.enc(new Uint8Array(i16.buffer))}; }
function unpackLayer(o){ const u = b64.dec(o.data), i16 = new Int16Array(u.buffer, u.byteOffset, u.length >> 1), d = new Float64Array(i16.length); for (let i = 0; i < d.length; i++) d[i] = i16[i] / 32768; return {rate:o.rate, clk:o.clk, kit:0, dyn:o.dyn ?? -1, data:d}; }
const unpacked = {};
function romOf(g){
  const r = S.roms[g];
  if (r && Array.isArray(r.layers)){ if (!unpacked[g] || unpacked[g].src !== r) unpacked[g] = {src:r, layers:r.layers.map(unpackLayer)}; return unpacked[g].layers; }
  return factory ? factory[g] : [];
}
function romName(g){
  const r = S.roms[g]; if (r) return r.name;
  const f = factory && factory[g] || [], kit = S.params[P.kit] >= 0.5 ? 2 : 1, mine = f.filter(L => L.kit === kit), use = mine.length ? mine : f;
  if (!use.length) return 'EMPTY (no recording yet)';
  const dyn = use.filter(L => L.dyn >= 0).length;
  return `LinnDrum, set ${use[0].kit}${mine.length ? '' : ' (this set has none)'}` + (dyn ? `, ${dyn} dynamics` : use.length > 1 ? `, ${use.length} tunings` : '');
}
function sendRoms(){ for (let g = 0; g < D.NG_ROM; g++) post({type:'rom', g, layers:romOf(g)}); }
function sendAll(){ post({type:'params', p:S.params}); post({type:'mem', mem:S.mem}); post({type:'state', pat:S.pat, song:S.song, mode:S.mode}); post({type:'metro', on:S.metroAlways}); sendRoms(); }
function setParam(i, v){ S.params[i] = v; post({type:'params', p:S.params}); if (!S.edited && !PR.KEEP.includes(PARAMS[i][0])){ S.edited = true; showPresetName(); } save(); }
async function loadRom(){ if (factory) return; const r = await fetch('rom/linn.rom'); factory = D.parseRom(new Uint8Array(await r.arrayBuffer())); }
let audioReady = null;
function startAudio(){
  if (audioReady){ if (ctx && ctx.state !== 'running') ctx.resume(); return audioReady; }
  return (audioReady = bootAudio());
}
async function bootAudio(){
  ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
  await loadRom();
  for (const f of ['engine.js', 'machine.js', 'worklet.js']) await ctx.audioWorklet.addModule(f);
  node = new AudioWorkletNode(ctx, 'linn', {numberOfInputs:0, outputChannelCount:[2]});
  node.port.onmessage = ({data}) => onAudio(data);
  node.connect(ctx.destination);
  sendAll();
  S.power = true;
}
function onAudio(m){
  switch (m.type){
    case 'disp': S.disp = m; if (m.d.sel !== S.sel){ S.sel = m.d.sel; refresh(); }
      if (m.pat !== S.pat || m.song !== S.song || m.mode !== S.mode){ S.pat = m.pat; S.song = m.song; S.mode = m.mode; save(); }
      draw(); showMeters(m.meter); drawGrid(); for (const f of padRefresh) f(); break;
    case 'mem': S.mem = m.mem; save(); drawGrid(true); break;
  }
  for (const f of listeners) f(m);
}

/* ---------------- the front panel ---------------- */
const plate = $('plate');
const UI = FP.build(plate);
let flash = null, casLed = -1, casUntil = 0;
function showFlash(l, r, ms){ flash = {l, r, until:Date.now() + ms}; draw(); setTimeout(draw, ms + 20); }
const KNOB_P = [P.tempo, P.tSN, P.tHT, P.tMT, P.tLT, P.tHC, P.tLC, P.hatDecay, P.volume];
function knobNorm(k){ const i = KNOB_P[k]; return i === P.tempo ? D.knobOfTempo(S.params[i]) / 100 : S.params[i] / 100; }
const lvlParam = i => i < NV ? vp(i, 0) : P.clickLv, panParam = i => i < NV ? vp(i, 1) : P.clickPan;
function draw(){
  const d = S.disp.d;
  const f = flash && flash.until > Date.now() ? flash : null;
  FP.setDigits(UI.dispStep, f ? f.l : d.l); FP.setDigits(UI.dispPat, f ? f.r : d.r);
  FP.setLed(UI.leds.song, d.song); FP.setLed(UI.leds.pat, !d.song);
  UI.leds.tim.forEach((g, i) => FP.setLed(g, d.tim === i)); UI.leds.ec.forEach((g, i) => FP.setLed(g, d.ec === i));
  FP.setLed(UI.leds.play, d.play || d.rec); FP.setLed(UI.leds.ext, d.ext); FP.setLed(UI.leds.perc, d.perc);
  FP.setLed(UI.leds.store, casLed === 0 && Date.now() < casUntil); FP.setLed(UI.leds.load, casLed === 1 && Date.now() < casUntil);
  UI.knobs.forEach((k, i) => FP.setKnob(k, knobNorm(i)));
  UI.lvl.forEach((s, i) => FP.setSlider(s, S.params[lvlParam(i)] / 100));
  UI.pan.forEach((s, i) => FP.setSlider(s, 1 - S.params[panParam(i)] / 100));
}
function showTempo(){ const s = String(Math.round(S.params[P.tempo])).padStart(4, ' '); showFlash(s.slice(0, 2), s.slice(2), 1200); }
// keys: down and up go to the firmware (holding one and pressing another works, as on the hardware)
async function keyDown(id){
  if (!S.power) await startAudio();
  if (id === K.STORE){ casLed = 0; casUntil = Date.now() + 1500; window.LINNIO && LINNIO.saveTape(); draw(); return; }
  if (id === K.LOAD){ casLed = 1; casUntil = Date.now() + 1500; window.LINNIO && LINNIO.loadTape(); draw(); return; }
  post({type:'key', id, down:true});
}
function keyUp(id){ if (id !== K.STORE && id !== K.LOAD) post({type:'key', id, down:false}); }
for (const [id, g] of Object.entries(UI.keys)){
  const kid = +id;
  g.addEventListener('pointerdown', e => { e.preventDefault(); g.setPointerCapture && g.setPointerCapture(e.pointerId); g.classList.add('down'); keyDown(kid); });
  const up = () => { if (!g.classList.contains('down')) return; g.classList.remove('down'); keyUp(kid); };
  g.addEventListener('pointerup', up); g.addEventListener('pointercancel', up); g.addEventListener('lostpointercapture', up);
}
// knobs and sliders: drag up / down, double-tap for the default
function dragKnob(elm, getN, setN, dflt, scale = 3){
  let y0 = 0, n0 = 0, last = 0;
  elm.addEventListener('pointerdown', e => {
    e.preventDefault(); e.stopPropagation(); elm.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = getN();
    const now = Date.now(); if (now - last < 300 && dflt !== undefined) setN(dflt); last = now;
  });
  elm.addEventListener('pointermove', e => {
    if (!elm.hasPointerCapture(e.pointerId)) return;
    const h = elm.getBoundingClientRect().height || 60;
    setN(Math.max(0, Math.min(1, n0 + (y0 - e.clientY) / (h * scale))));
  });
  elm.addEventListener('wheel', e => { e.preventDefault(); setN(Math.max(0, Math.min(1, getN() - Math.sign(e.deltaY) * 0.02))); }, {passive:false});
}
const dflt = i => (PARAMS[i][4] - PARAMS[i][2]) / (PARAMS[i][3] - PARAMS[i][2]);
UI.knobs.forEach((k, i) => dragKnob(k.g, () => knobNorm(i), n => {
  const pi = KNOB_P[i];
  if (pi === P.tempo){ setParam(pi, Math.round(D.tempoOfKnob(n * 100))); showTempo(); }
  else { setParam(pi, Math.round(n * 1000) / 10); showFlash('  ', String(Math.min(99, Math.round(n * 99))).padStart(2, '0'), 900); }
  refresh();
}, i === 0 ? D.knobOfTempo(120) / 100 : dflt(KNOB_P[i])));
// a slider follows the finger (its hit area is the slot's height)
function dragSlider(s, get, set, dfl){
  let last = 0;
  const at = e => { const r = s.g.getBoundingClientRect(), y0 = r.top + r.height * 18 / (s.y1 - s.y0 + 36), y1 = r.bottom - r.height * 18 / (s.y1 - s.y0 + 36); return Math.max(0, Math.min(1, (y1 - e.clientY) / Math.max(1, y1 - y0))); };
  s.g.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); s.g.setPointerCapture(e.pointerId); const now = Date.now(); if (now - last < 300){ set(dfl); last = 0; return; } last = now; set(at(e)); });
  s.g.addEventListener('pointermove', e => { if (s.g.hasPointerCapture(e.pointerId)) set(at(e)); });
}
UI.lvl.forEach((s, i) => dragSlider(s, () => S.params[lvlParam(i)] / 100, n => { setParam(lvlParam(i), Math.round(n * 100)); draw(); refresh(); }, PARAMS[lvlParam(i)][4] / 100));
UI.pan.forEach((s, i) => dragSlider(s, () => 1 - S.params[panParam(i)] / 100, n => { setParam(panParam(i), Math.round((1 - n) * 100)); draw(); refresh(); }, 0.5));
setInterval(() => { if (flash || casUntil) draw(); }, 250);

/* ---------------- page widgets ---------------- */
const refreshers = [], padRefresh = [];
function refresh(){ for (const f of refreshers) f(); drawCurves(); draw(); }
function miniKnob(parent, label, get, set, fmt, dfl){
  const w = el('div', 'knobRow', parent);
  w.innerHTML = `<svg class="mk" viewBox="-26 -26 52 52"><g class="tk"></g><circle r="20" fill="#1a1a1a"/><circle r="15.5" fill="url(#gMk)"/><line class="ptr" x1="0" y1="-3" x2="0" y2="-15" stroke="#111" stroke-width="3.2" stroke-linecap="round"/></svg><span>${label}</span><span class="val"></span>`;
  const svg = w.querySelector('svg'), tk = svg.querySelector('.tk'), ptr = svg.querySelector('.ptr'), val = w.querySelector('.val');
  for (let i = 0; i <= 10; i++){ const a = (-150 + 30 * i) * Math.PI / 180; const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle'); c.setAttribute('cx', Math.sin(a) * 23.5); c.setAttribute('cy', -Math.cos(a) * 23.5); c.setAttribute('r', 1.4); c.setAttribute('fill', '#f25a2c'); tk.appendChild(c); }
  w.refresh = () => { ptr.setAttribute('transform', `rotate(${-150 + 300 * Math.max(0, Math.min(1, get()))})`); val.textContent = fmt(); };
  dragKnob(svg, get, n => { set(n); w.refresh(); drawCurves(); draw(); }, dfl);
  refreshers.push(w.refresh); w.refresh();
  return w;
}
const hz = v => v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k' : Math.round(v) + '';
const pct = v => Math.round(v * 100) + '%';
const db = v => (v > 0 ? '+' : '') + v.toFixed(1) + 'dB';
const ms = v => v >= 1 ? v.toFixed(2) + 's' : Math.round(v * 1000) + 'ms';
const panFmt = v => Math.abs(v - 50) < 0.5 ? 'C' : (v < 50 ? 'L' : 'R') + Math.round(Math.abs(v - 50) * 2);
function pknob(parent, label, idx, kind = 'lin', fmt){
  const I = typeof idx === 'function' ? idx : () => idx;
  const q = () => PARAMS[I()];
  const get = () => { const [, , lo, hi] = q(), v = S.params[I()]; return kind === 'log' ? Math.log(v / lo) / Math.log(hi / lo) : (v - lo) / (hi - lo); };
  const put = n => { const [, , lo, hi] = q(); let v = kind === 'log' ? lo * Math.pow(hi / lo, n) : lo + n * (hi - lo);
    v = kind === 'int' ? Math.round(v) : kind === 'log' ? +v.toPrecision(3) : Math.round(v * 1000) / 1000; setParam(I(), v); };
  const d = (() => { const [, , lo, hi, df] = q(); return kind === 'log' ? Math.log(df / lo) / Math.log(hi / lo) : (df - lo) / (hi - lo); })();
  return miniKnob(parent, label, get, put, () => (fmt || (v => Math.abs(v) >= 100 ? Math.round(v) : v.toFixed(2)))(S.params[I()]), d);
}
function toggleBtn(parent, label, idx){
  const I = typeof idx === 'function' ? idx : () => idx;
  const b = el('button', 'small', parent, label);
  b.onclick = () => { setParam(I(), S.params[I()] >= 0.5 ? 0 : 1); show(); };
  const show = () => b.classList.toggle('on', S.params[I()] >= 0.5);
  refreshers.push(show); show(); return b;
}
function choice(parent, names, idx){
  const g = el('div', 'revProgs', parent);
  names.forEach((n, i) => { const b = el('button', 'small', g, n); b.onclick = () => { setParam(idx, i); refresh(); }; });
  refreshers.push(() => [...g.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[idx]))));
  return g;
}
function selector(parent){
  const g = el('div', 'sel13', parent);
  const bs = CH_KEYS.map((n, v) => { const b = el('button', 'small', g, n); b.onclick = () => { S.sel = v; post({type:'sel', v}); refresh(); }; return b; });
  refreshers.push(() => bs.forEach((b, v) => b.classList.toggle('on', v === S.sel)));
  return g;
}
const curves = [];
function eqCurve(parent, bands){ const c = el('canvas', 'eqc', parent); c.width = 640; c.height = 170; curves.push({c, bands}); return c; }
function drawCurves(){
  for (const {c, bands, draw:dr} of curves){
    if (!c.offsetParent) continue;
    if (dr){ dr(c); continue; }
    const g = c.getContext('2d'), W = c.width, H = c.height, sr = 48000;
    g.fillStyle = '#1d0507'; g.fillRect(0, 0, W, H);
    g.strokeStyle = 'rgba(255,36,64,.18)'; g.lineWidth = 1;
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]){ const x = Math.log(f / 20) / Math.log(1000) * W; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); }
    for (const d of [-12, -6, 0, 6, 12]){ const y = H / 2 - d / 18 * H / 2; g.beginPath(); g.moveTo(0, y); g.lineTo(W, y); g.stroke(); }
    const bq = bands().map(([ty, f, gn, q]) => { const b = new D.Biquad(); b.set(ty, f, gn, q, sr); return b; });
    g.strokeStyle = '#ff2440'; g.lineWidth = 3; g.beginPath();
    for (let x = 0; x <= W; x += 2){
      const f = 20 * Math.pow(1000, x / W), w = 2 * Math.PI * f / sr; let mag = 0;
      for (const b of bq){ if (!b.on) continue;
        const cr = Math.cos(w), ci = -Math.sin(w), c2r = Math.cos(2 * w), c2i = -Math.sin(2 * w);
        const nr = b.b0 + b.b1 * cr + b.b2 * c2r, ni = b.b1 * ci + b.b2 * c2i, dr2 = 1 + b.a1 * cr + b.a2 * c2r, di = b.a1 * ci + b.a2 * c2i;
        mag += 10 * Math.log10((nr * nr + ni * ni) / (dr2 * dr2 + di * di)); }
      const y = H / 2 - Math.max(-18, Math.min(18, mag)) / 18 * H / 2;
      if (x === 0) g.moveTo(x, y); else g.lineTo(x, y);
    }
    g.stroke();
  }
}
function toast(s, t = 2600){ const e = $('toast'); e.textContent = s; e.classList.add('on'); clearTimeout(toast.t); toast.t = setTimeout(() => e.classList.remove('on'), t); }

/* ---------------- PADS: the 16 drum keys, the transport and the step grid ---------------- */
const PAD_ORDER = [4, 5, 2, 3, 8, 9, 10, 0, 11, 12, 13, 6, 14, 15, 1, 7];      // drum keys, laid out to play with thumbs
const keyLabel = (k, perc) => { if (k === M.PERC_KEY) return ['PERCUSSION', perc ? 'ON' : 'OFF']; const d = M.DRUM_KEYS[k]; return [SOUNDS[d[perc ? 1 : 0]][0], d[0] !== d[1] ? SOUNDS[d[perc ? 0 : 1]][0] : '']; };
{
  const pg = $('pads');
  const bx = el('div', 'box padsBox pads1', pg);
  const tr = el('div', 'transport', bx);
  const ctl = (label, f) => { const b = el('button', '', tr, label); b.addEventListener('pointerdown', async e => { e.preventDefault(); if (!S.power) await startAudio(); f(true, b); });
    const up = () => f(false, b); b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); return b; };
  const playB = ctl('PLAY / STOP', d => { if (d){ post({type:'key', id:K.PLAY, down:true}); post({type:'key', id:K.PLAY, down:false}); } });
  // RECORD: the hardware's hold RECORD + PLAY/STOP in one press (stops when it's recording)
  const recB = ctl('RECORD', d => { if (!d) return; if (S.disp.running){ post({type:'key', id:K.PLAY, down:true}); post({type:'key', id:K.PLAY, down:false}); return; }
    if (S.mode !== 0){ post({type:'key', id:K.SONGPAT, down:true}); post({type:'key', id:K.SONGPAT, down:false}); }
    post({type:'key', id:KP.RECORD, down:true}); post({type:'key', id:K.PLAY, down:true}); post({type:'key', id:K.PLAY, down:false}); post({type:'key', id:KP.RECORD, down:false}); });
  const erB = ctl('ERASE', (d, b) => { post({type:'key', id:KP.ERASE, down:d}); b.classList.toggle('on', d); });
  const pcB = ctl('PERC', d => { if (d){ post({type:'key', id:K.DRUM + M.PERC_KEY, down:true}); post({type:'key', id:K.DRUM + M.PERC_KEY, down:false}); } });
  void erB;
  padRefresh.push(() => { recB.classList.toggle('on', !!S.disp.rec); playB.classList.toggle('on', !!S.disp.running); pcB.classList.toggle('on', !!S.disp.d.perc); });
  const grid = el('div', 'padGrid', bx), pads = [];
  for (const k of PAD_ORDER){
    const b = el('button', '', grid); pads.push([k, b]);
    b.addEventListener('pointerdown', async e => { e.preventDefault(); b.setPointerCapture && b.setPointerCapture(e.pointerId); if (!S.power) await startAudio(); post({type:'key', id:K.DRUM + k, down:true}); b.classList.add('hit'); });
    const up = () => { if (b.classList.contains('hit')){ b.classList.remove('hit'); post({type:'key', id:K.DRUM + k, down:false}); } };
    b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); b.addEventListener('lostpointercapture', up);
  }
  let lastPerc = null;
  padRefresh.push(() => { const perc = !!S.disp.d.perc; if (perc === lastPerc) return; lastPerc = perc;
    for (const [k, b] of pads){ const [a, c] = keyLabel(k, perc); b.innerHTML = `${a}${c ? `<small>${c}</small>` : ''}`; b.classList.toggle('perc', perc && M.DRUM_KEYS[k][0] !== M.DRUM_KEYS[k][1]); } });
  // the step grid: the pattern on screen, a bar (4 beats) at a time, in steps of its ERROR CORRECT (1/16 when HI)
  const sb = el('div', 'box padsBox', pg, '<h3>STEP GRID</h3>');
  const head = el('div', 'kRow', sb);
  const info = el('span', 'revLcd', head);
  let bar = 0;
  const prevB = el('button', 'small', head, '◀ BAR'), nextB = el('button', 'small', head, 'BAR ▶');
  const lenM = el('button', 'small', head, 'LEN −'), lenP = el('button', 'small', head, 'LEN +'), kitB = el('button', 'small', head, 'DRUMS');
  const g2 = el('div', 'stepGrid', sb);
  const curP = () => S.disp.running ? S.disp.ppat : S.pat;
  prevB.onclick = () => { bar = Math.max(0, bar - 1); drawGrid(true); }; nextB.onclick = () => { bar++; drawGrid(true); };
  lenM.onclick = () => { const pt = S.mem.pats[curP()]; post({type:'len', p:curP(), beats:Math.round(pt.len / M.PPQ) - 1}); };
  lenP.onclick = () => { const pt = S.mem.pats[curP()]; post({type:'len', p:curP(), beats:Math.round(pt.len / M.PPQ) + 1}); };
  kitB.onclick = () => { S.gridPerc = !S.gridPerc; kitB.textContent = S.gridPerc ? 'PERCUSSION' : 'DRUMS'; drawGrid(true); };
  const ROWS = [[1, 0, 4, 3, 2, 5, 7, 6, 8, 9, 10, 11, 13, 12, 14], [16, 15, 18, 17, 19, 20, 21, 22]];
  let cells = [], lastKey = '';
  window.drawGridImpl = force => {
    if (!sb.offsetParent && !force) return;
    const pi = curP(), pt = S.mem.pats[pi]; if (!pt) return;
    const q = pt.ec === 6 ? 48 : M.EC_GRID[pt.ec], barLen = 4 * M.PPQ, bars = Math.max(1, Math.ceil(pt.len / barLen));
    bar = Math.min(bar, bars - 1);
    const steps = Math.max(1, Math.min(32, Math.round(Math.min(barLen, pt.len - bar * barLen) / q)));
    const rows = ROWS[S.gridPerc ? 1 : 0];
    const key = pi + ':' + q + ':' + steps + ':' + bar + ':' + S.gridPerc;
    if (key !== lastKey || force){
      lastKey = key; g2.innerHTML = ''; cells = [];
      g2.style.gridTemplateColumns = `62px repeat(${steps}, 1fr)`;
      for (const s of rows){
        el('b', '', g2, SOUNDS[s][0]);
        const row = [];
        for (let k = 0; k < steps; k++){
          const t = bar * barLen + k * q, c = el('i', t % M.PPQ === 0 ? 'beat' : '', g2);
          c.onclick = () => { const on = pt.ev.filter(x => x.s === s && x.t >= t && x.t < t + q);
            if (on.length) for (const x of on) post({type:'edit', p:pi, t:x.t, s, on:false}); else post({type:'edit', p:pi, t, s, on:true});
            if (!S.power) startAudio(); };
          row.push({c, t});
        }
        cells.push([s, row]);
      }
    }
    const now = S.disp.running && S.disp.tk >= 0 ? S.disp.tk : -1;
    for (const [s, row] of cells) for (const {c, t} of row){ const on = pt.ev.some(x => x.s === s && x.t >= t && x.t < t + q); c.classList.toggle('on', on); c.classList.toggle('now', now >= t && now < t + q); }
    info.textContent = `PAT ${M.pnum(pi)}  ${Math.round(pt.len / M.PPQ)} BEATS  BAR ${bar + 1}/${bars}  ${M.TIMING[pt.tim]}  ${M.EC_NAMES[pt.ec]}`;
  };
}
function drawGrid(force){ if (window.drawGridImpl) window.drawGridImpl(force); }

/* ---------------- SOUND ---------------- */
const groupOfCh = v => v === 0 && S.params[P.bassChip] >= 0.5 ? D.G.BASS2 : D.CH_GROUP[v];
{
  const pg = $('sound');
  const b1 = el('div', 'box', pg, '<h3>SOUND</h3>');
  selector(b1);
  const k = el('div', 'kRow', b1), f = n => () => vp(S.sel, n);
  pknob(k, 'TUNE', f(2), 'lin', v => (v > 0 ? '+' : '') + v.toFixed(1)); pknob(k, 'DECAY', f(3), 'log', v => v >= 2.999 ? 'FULL' : ms(v));
  pknob(k, 'CUTOFF', f(7), 'log', v => v >= 19999 ? 'OPEN' : hz(v)); pknob(k, 'RESO', f(8), 'lin', pct); pknob(k, 'START', f(9), 'lin', pct);
  pknob(k, 'OUTPUT', f(10), 'int', v => v ? 'OUT ' + v : 'OWN');
  const b2 = el('div', 'box', pg, '<h3>SENDS · DRIVE · ROM</h3>');
  const k2 = el('div', 'kRow', b2);
  pknob(k2, 'REVERB', f(5), 'lin', pct); pknob(k2, 'DELAY', () => vq(S.sel, 12), 'lin', pct); pknob(k2, 'DRIVE', () => vq(S.sel, 11), 'lin', pct);
  const t = el('div', 'kRow', b2);
  toggleBtn(t, 'REVERSE', f(6)); toggleBtn(t, 'MUTE', f(4));
  const rn = el('p', '', b2);
  refreshers.push(() => { const g = groupOfCh(S.sel); rn.innerHTML = `${D.GROUPS[g]} ROM: <b>${romName(g)}</b>. LOAD SOUND puts your own recording in this ROM (a LinnDrum recording's length tells its tuning; others play at the knob's factory setting). FILES has LOAD FOLDER for a whole kit.`; });
  const t2 = el('div', 'kRow', b2);
  el('button', 'small', t2, 'LOAD SOUND').onclick = () => window.LINNIO && LINNIO.loadSound(groupOfCh(S.sel));
  el('button', 'small', t2, 'BUILT-IN').onclick = () => { const g = groupOfCh(S.sel); delete S.roms[g]; sendRoms(); save(); refresh(); toast('Built-in ROM back on ' + D.GROUPS[g]); };
  const b3 = el('div', 'box', pg, '<h3>HI-HAT · MACHINE</h3><p>HAT MODE: LINN keeps the hat ROM counting through quick hits (each closed hat is a later slice of the cymbal), RESTART plays every hit from the top, LM-1 LOOP lets the ROM run round all the time and a hit only opens the VCA.</p>');
  choice(b3, D.HAT_MODES, P.hatMode);
  el('p', '', b3, 'SOUND SET: your two sets of LinnDrum recordings (each one fills the other\'s gaps).');
  choice(b3, ['SOUND SET 1', 'SOUND SET 2'], P.kit);
  const k3 = el('div', 'kRow', b3);
  pknob(k3, 'HAT DECAY', P.hatDecay, 'lin', v => Math.round(v) + '%'); pknob(k3, 'MUX TRIM', P.mux, 'lin', v => Math.round(v) + '%'); pknob(k3, 'TEMPO', P.tempo, 'int', v => v + ' BPM');
  pknob(k3, 'CLICK', P.clickLv, 'lin', v => Math.round(v) + '');
  const t3 = el('div', 'kRow', b3);
  const chip = el('button', 'small', t3, 'BASS CHIP'); chip.onclick = () => { setParam(P.bassChip, S.params[P.bassChip] >= 0.5 ? 0 : 1); sendRoms(); refresh(); };
  refreshers.push(() => { chip.textContent = 'BASS CHIP: ' + (S.params[P.bassChip] >= 0.5 ? 'ALT' : 'STOCK'); });
  toggleBtn(t3, 'POLY', P.poly);
  const mb = el('button', 'small', t3, 'CLICK ALWAYS'); mb.onclick = () => { S.metroAlways = !S.metroAlways; post({type:'metro', on:S.metroAlways}); save(); refresh(); };
  refreshers.push(() => mb.classList.toggle('on', S.metroAlways));
}

/* ---------------- PAD EQ / ADSR ---------------- */
{
  const pg = $('padeq');
  const b1 = el('div', 'box', pg, '<h3>PARAMETRIC EQ</h3>');
  selector(b1);
  const q = f => () => vq(S.sel, f), Q = f => S.params[vq(S.sel, f)];
  eqCurve(b1, () => [[0, Q(0), Q(1), .707], [1, Q(2), Q(3), Q(4)], [2, Q(5), Q(6), .707]]);
  const k = el('div', 'kRow', b1);
  pknob(k, 'LOW F', q(0), 'log', hz); pknob(k, 'LOW', q(1), 'lin', db); pknob(k, 'MID F', q(2), 'log', hz); pknob(k, 'MID', q(3), 'lin', db); pknob(k, 'MID Q', q(4), 'log');
  pknob(k, 'HIGH F', q(5), 'log', hz); pknob(k, 'HIGH', q(6), 'lin', db);
  const b2 = el('div', 'box', pg, '<h3>ADSR · DRIVE</h3><p>Shapes the hit on top of the ROM sound; the gate is the key press or the MIDI note. Sustain 100% with release at maximum leaves the sound as it is.</p>');
  const c = el('canvas', 'eqc', b2); c.width = 640; c.height = 150;
  curves.push({c, draw:cv => { const g = cv.getContext('2d'), W = cv.width, H = cv.height; g.fillStyle = '#1d0507'; g.fillRect(0, 0, W, H);
    const a = Q(7), d = Q(8), s = Q(9), r = Q(10), hold = r >= 3.999, dd = Math.min(d, 1.5), total = a + dd + 0.4 + (hold ? 0.6 : Math.min(r, 2));
    const X = t => 10 + t / total * (W - 20), Y = v => H - 10 - v * (H - 20);
    g.strokeStyle = '#ff2440'; g.lineWidth = 3; g.beginPath(); g.moveTo(X(0), Y(a > 0 ? 0 : 1)); g.lineTo(X(a), Y(1));
    for (let t = 0; t <= dd; t += dd / 30) g.lineTo(X(a + t), Y(s + (1 - s) * Math.exp(-t / (d / 3))));
    const sv = s + (1 - s) * Math.exp(-dd / (d / 3)), t0 = a + dd + 0.4; g.lineTo(X(t0), Y(sv));
    if (hold) g.lineTo(X(total), Y(sv)); else { const rr = Math.min(r, 2); for (let t = 0; t <= rr; t += rr / 30) g.lineTo(X(t0 + t), Y(sv * Math.exp(-t / (r / 3)))); }
    g.stroke(); }});
  const k2 = el('div', 'kRow', b2);
  pknob(k2, 'ATTACK', q(7), 'lin', ms); pknob(k2, 'DECAY', q(8), 'log', ms); pknob(k2, 'SUSTAIN', q(9), 'lin', pct); pknob(k2, 'RELEASE', q(10), 'log', v => v >= 3.999 ? 'HOLD' : ms(v));
  pknob(k2, 'DRIVE', q(11), 'lin', pct);
  el('button', 'small', k2, 'RESET PAD').onclick = () => { const d = D.defaults(); for (let f = 0; f < 13; f++) S.params[vq(S.sel, f)] = d[vq(S.sel, f)]; post({type:'params', p:S.params}); save(); refresh(); };
}

/* ---------------- MIXER: the panel's 15 channels and the click, big ---------------- */
const meterEls = [];
{
  const pg = $('mixer');
  for (let v = 0; v <= NV; v++){
    const s = el('div', 'strip', pg, `<h4>${v < NV ? CH_KEYS[v] : 'CLICK'}</h4>`);
    pknob(s, 'PAN', panParam(v), 'lin', panFmt);
    const fd = el('div', 'fader', s), inp = el('input', '', fd); inp.type = 'range'; inp.min = 0; inp.max = 100; inp.step = 1;
    const val = el('span', '', fd);
    inp.oninput = () => { setParam(lvlParam(v), +inp.value); val.textContent = inp.value; draw(); };
    refreshers.push(() => { inp.value = S.params[lvlParam(v)]; val.textContent = Math.round(S.params[lvlParam(v)]); });
    if (v < NV){
      const m = el('div', 'meter', s); meterEls.push(el('i', '', m));
      toggleBtn(s, 'MUTE', vp(v, 4));
    }
  }
}
function showMeters(m){ if (!m) return; for (let v = 0; v < NV && v < meterEls.length; v++){ const d = 20 * Math.log10(Math.max(1e-4, m[v] * 1.4)); meterEls[v].style.width = Math.max(0, Math.min(100, (d + 48) / 48 * 100)) + '%'; } }

/* ---------------- FX: REV 7 and the TC delay ---------------- */
{
  const pg = $('fx');
  const r = el('div', 'box', pg, '<h3>REV 7 · DIGITAL REVERBERATOR</h3>');
  choice(r, D.RV_PROGS, P.rvProg);
  const k = el('div', 'kRow', r);
  pknob(k, 'REV TIME', P.rvTime, 'log', v => v.toFixed(1) + 's'); pknob(k, 'HIGH', P.rvHigh, 'lin'); pknob(k, 'DELAY', P.rvDelay, 'lin', v => Math.round(v) + 'ms');
  pknob(k, 'DIFFUSION', P.rvDiff, 'lin', v => v.toFixed(1)); pknob(k, 'ER LEVEL', P.rvEr, 'lin', pct); pknob(k, 'HIGH CUT', P.rvCut, 'log', hz); pknob(k, 'RETURN', P.rvLevel, 'lin', pct);
  const t = el('div', 'box', pg, '<h3>TC 2290 · DYNAMIC DELAY · STYLE</h3>');
  const k2 = el('div', 'kRow', t);
  pknob(k2, 'TIME', P.dlTime, 'log', v => Math.round(v) + 'ms'); pknob(k2, 'SYNC', P.dlSync, 'int', v => D.DL_DIV_NAMES[Math.round(v)]); pknob(k2, 'FEEDBACK', P.dlFb, 'lin', pct);
  pknob(k2, 'HIGH CUT', P.dlHiCut, 'log', hz); pknob(k2, 'LOW CUT', P.dlLoCut, 'log', hz); pknob(k2, 'MOD RATE', P.dlModRate, 'log', v => v.toFixed(2) + 'Hz');
  pknob(k2, 'MOD DEPTH', P.dlModDepth, 'lin', pct); pknob(k2, 'DUCKING', P.dlDuck, 'lin', pct); pknob(k2, 'RETURN', P.dlLevel, 'lin', pct);
  toggleBtn(k2, 'PING-PONG', P.dlPing);
  const s = el('div', 'box', pg, '<h3>DELAY SENDS</h3>'), k3 = el('div', 'kRow', s);
  for (let v = 0; v < NV; v++) pknob(k3, CH_KEYS[v], vq(v, 12), 'lin', pct);
}

/* ---------------- MASTER ---------------- */
{
  const pg = $('master');
  const e = el('div', 'box', pg, '<h3>MASTER PARAMETRIC EQ</h3>');
  eqCurve(e, () => [[0, S.params[P.mLsF], S.params[P.mLsG], .707], [1, S.params[P.mP1F], S.params[P.mP1G], S.params[P.mP1Q]], [1, S.params[P.mP2F], S.params[P.mP2G], S.params[P.mP2Q]], [2, S.params[P.mHsF], S.params[P.mHsG], .707]]);
  const k = el('div', 'kRow', e);
  pknob(k, 'LOW F', P.mLsF, 'log', hz); pknob(k, 'LOW', P.mLsG, 'lin', db); pknob(k, 'MID1 F', P.mP1F, 'log', hz); pknob(k, 'MID1', P.mP1G, 'lin', db); pknob(k, 'MID1 Q', P.mP1Q, 'log');
  pknob(k, 'MID2 F', P.mP2F, 'log', hz); pknob(k, 'MID2', P.mP2G, 'lin', db); pknob(k, 'MID2 Q', P.mP2Q, 'log'); pknob(k, 'HIGH F', P.mHsF, 'log', hz); pknob(k, 'HIGH', P.mHsG, 'lin', db);
  const o = el('div', 'box', pg, '<h3>OVERDRIVE · CLIPPER</h3>'), k2 = el('div', 'kRow', o);
  pknob(k2, 'DRIVE', P.odDrive, 'lin', pct); pknob(k2, 'MIX', P.odMix, 'lin', pct); pknob(k2, 'OUTPUT', P.odOut, 'lin', db);
  choice(o, D.OD_MODES, P.odMode);
  const m = el('div', 'box', pg, '<h3>OUTPUT</h3>'), k3 = el('div', 'kRow', m);
  pknob(k3, 'VOLUME', P.volume, 'lin', v => Math.round(v) + '');
}

/* ---------------- presets: the module beside the machine and the PRESETS page ---------------- */
const PART_NAMES = {all:'ALL', voices:'SOUNDS', eq:'PAD EQ', env:'ADSR', fx:'FX', master:'MASTER'};
let diceIdx = 0;
const bar = $('presetBar');
bar.innerHTML = `<div class="lcdBox"><b id="pNum">001</b><span id="pFam"></span><span id="pTitle"></span><em id="pName"></em></div>
  <div class="mRow"><button class="key" id="pPrev" aria-label="Previous preset">◀</button><button class="key" id="pNext" aria-label="Next preset">▶</button></div>
  <button class="key dice" id="pDice">⚄ DICE</button><button class="key small" id="pPart">ALL</button>`;
const presetName = k => k >= 1000 ? (S.userPresets[k - 1000] || {}).name || '' : PR.PRESETS[k].name.split(' · ')[1];
const presetFam = k => k >= 1000 ? 'YOURS' : PR.PRESETS[k].family;
function showPresetName(){
  $('pNum').textContent = S.preset >= 1000 ? 'U' + (S.preset - 999) : String(S.preset + 1).padStart(3, '0');
  $('pFam').textContent = presetFam(S.preset).toUpperCase(); $('pTitle').textContent = presetName(S.preset);
  $('pName').textContent = S.edited ? 'EDITED' : '';
  document.querySelectorAll('#presets .pBtnGrid button[data-k]').forEach(b => b.classList.toggle('on', +b.dataset.k === S.preset));
}
function loadPreset(k){
  const src = k >= 1000 ? S.userPresets[k - 1000] : PR.PRESETS[k]; if (!src) return;
  const chip = S.params[P.bassChip];
  S.params = PR.load(S.params, src.p); S.preset = k; S.edited = false;
  post({type:'params', p:S.params}); if (chip !== S.params[P.bassChip]) sendRoms(); save(); refresh(); showPresetName();
}
const allPresets = () => PR.PRESETS.map((p, i) => i).concat(S.userPresets.map((u, k) => 1000 + k));
const stepPreset = d => { const all = allPresets(); let i = all.indexOf(S.preset); i = (i + d + all.length) % all.length; loadPreset(all[i]); };
$('pPrev').onclick = () => stepPreset(-1); $('pNext').onclick = () => stepPreset(1);
$('pPart').onclick = () => { diceIdx = (diceIdx + 1) % PR.DICE_PARTS.length; $('pPart').textContent = PART_NAMES[PR.DICE_PARTS[diceIdx]]; };
$('pDice').onclick = () => { S.params = PR.load(S.params, PR.dice(S.params, PR.DICE_PARTS[diceIdx])); S.edited = true; post({type:'params', p:S.params}); sendRoms(); save(); refresh(); showPresetName(); };
const pp = $('presets');
let fam = 0;
function fillPresets(){
  const keep = pp.__page || 0; pp.__blocks = null; pp.innerHTML = '';
  const fb = el('div', 'box', pp, '<h3>FAMILY</h3>'), fg = el('div', 'pBtnGrid', fb);
  PR.FAMILIES.forEach((f, fi) => { const b = el('button', 'small' + (fi === fam ? ' on' : ''), fg, f.name); b.onclick = () => { fam = fi; fillPresets(); layout(); }; });
  const f = PR.FAMILIES[fam], box = el('div', 'box', pp, `<h3>${f.name.toUpperCase()}</h3>`), g0 = el('div', 'pBtnGrid', box);
  f.names.forEach((n, i) => { const k = fam * 10 + i, b = el('button', 'small', g0, `<i>${String(k + 1).padStart(3, '0')}</i>${n}`); b.dataset.k = k; b.onclick = () => loadPreset(k); });
  const mine = el('div', 'box', pp, '<h3>YOURS</h3>'), g = el('div', 'pBtnGrid', mine);
  S.userPresets.forEach((u, k) => { const b = el('button', 'small', g, `<i>U${k + 1}</i>${u.name}`); b.dataset.k = 1000 + k; b.onclick = () => loadPreset(1000 + k); });
  const acts = el('div', 'kRow', mine);
  el('button', 'small', acts, 'SAVE').onclick = () => { const name = (prompt('Name for this preset', 'My kit ' + (S.userPresets.length + 1)) || '').trim(); if (!name) return;
    S.userPresets.push({name:name.slice(0, 24), p:S.params.slice()}); S.preset = 1000 + S.userPresets.length - 1; S.edited = false; fillPresets(); save(); layout(); };
  el('button', 'small', acts, 'DELETE').onclick = () => { if (S.preset < 1000) return toast('Factory presets stay; pick one of yours to delete');
    const u = S.userPresets[S.preset - 1000]; if (!u || !confirm('Delete "' + u.name + '"?')) return; S.userPresets.splice(S.preset - 1000, 1); S.preset = 0; fillPresets(); save(); layout(); };
  el('button', 'small', acts, 'INIT').onclick = () => { S.params = PR.load(S.params, D.defaults()); S.edited = true; post({type:'params', p:S.params}); sendRoms(); save(); refresh(); showPresetName(); };
  pp.__page = keep; showPresetName();
}
fillPresets();

/* ---------------- HOW TO ---------------- */
$('help').innerHTML = `<div class="help box"><h3>PLAYING</h3>
<p><b>PAGES</b>: nothing scrolls. The buttons on the side pick a page; ◀ ▶ and the numbers at a page's top (or a swipe) turn it. FOLD shows the bare machine as big as it gets. Knobs and sliders: drag; double-tap for the default.</p>
<p><b>Patterns</b> are numbered with the 1-7 keys: 11-57 hold factory grooves, 61-77 are yours. Type two keys and press PLAY/STOP; a number typed while it plays waits for the end of the pattern. An empty pattern beeps.</p>
<p><b>Record</b>: hold RECORD and press PLAY/STOP (the PADS page's RECORD does both). Play the drum keys with the click; notes land on the ERROR CORRECT grid. Hold ERROR CORRECT and press the 1-7 key under 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T or HI (no correction). PLAY/STOP ends.</p>
<p><b>Timing</b>: hold AFTER and press 1-6 for A-F (straight to full shuffle) to change how the pattern plays; BEFORE + 1-6 shuffles what you record from then on.</p>
<p><b>Erase</b>: ERASE + a drum key (stopped) takes that sound out of the pattern; while recording, hold ERASE and the drum key as it passes. ERASE + a number empties that pattern.</p></div>
<div class="help box"><h3>EDITING · SONGS</h3>
<p><b>Copy</b>: COPY + a number: into an empty pattern it copies, onto one with notes it joins on the end, onto the same number it doubles. <b>Length</b>: hold RECORD, press LENGTH, count in with the click and press PLAY/STOP where the pattern should start over (or use LEN on the step grid).</p>
<p><b>Songs</b>: SONG/PATTERN to song mode. SONG# + a number picks the song. Hold ENTER and type patterns: each goes on a step and moves on; ENTER + END ends the song there. ← → move, INSERT + a number puts a step in, DELETE takes one out. PLAY/STOP plays from the top, INSERT + PLAY/STOP from the step shown. SONG/PATTERN + END: stop at the end instead of starting over.</p>
<p><b>BPM/TRIG.</b>: hold it to see the tempo; with a 1-7 key it sets the trigger output (MIDI page). <b>PERCUSSION</b> swaps the upper drum keys to cabasa, tambourine, congas, cowbell and claps. <b>STORE / LOAD</b>: the whole machine to a file and back.</p></div>
<div class="help box"><h3>THE SOUND</h3>
<p>The sounds are LinnDrum recordings played through a model of its circuits: TUNING knobs set the snare and tom / conga sample clocks (8-80 kHz), HIHAT DECAY the closed hat. The closed hat is the open hat through the hat VCA (HAT MODE on the SOUND page). Sounds with no recording are silent until you load one (FILES: LOAD FOLDER).</p>
<p><b>MIDI</b>: General MIDI drum notes play the sounds; velocity picks the dynamic (SNARE 1/2/3, BASS 1/2...). MIDI files: FILES, IMPORT .mid.</p></div>`;

/* ---------------- the screen: nothing ever scrolls (see CLAUDE.md; the owner's phone is a Galaxy S23) ---------------- */
const app = $('app'), stage = $('stage'), machineView = $('machineView'), plateWrap = $('plateWrap'), rail = $('rail');
const TITLES = {pads:'PADS · STEP GRID', sound:'SOUND · HI-HAT', padeq:'PAD EQ · ADSR', mixer:'MIXER', fx:'REV 7 · TC DELAY', master:'MASTER', presets:'PRESETS', file:'FILES', midi:'MIDI', help:'HOW TO'};
const PLATE_AR = FP.W / FP.H;
let view = 'machine';
function blocksOf(panel){
  if (panel.__blocks) return panel.__blocks;
  const blocks = [...panel.children].filter(e => !e.classList.contains('pageHead') && !e.classList.contains('pageBody'));
  panel.innerHTML = '';
  const head = el('div', 'pageHead', panel, `<b>${TITLES[panel.id] || ''}</b><span class="pgBtns"></span>`);
  const body = el('div', 'pageBody', panel);
  blocks.forEach(b => body.appendChild(b));
  panel.__blocks = blocks; panel.__head = head; panel.__body = body; panel.__page = panel.__page || 0;
  let x0 = null;
  body.addEventListener('pointerdown', e => { x0 = e.target.closest('.mk,input,select,button,canvas,i') ? null : e.clientX; });
  body.addEventListener('pointerup', e => { if (x0 === null) return; const dx = e.clientX - x0; x0 = null; if (Math.abs(dx) > 50) turn(panel, dx < 0 ? 1 : -1); });
  return blocks;
}
function turn(panel, d){ const n = (panel.__pages || [[]]).length; panel.__page = Math.max(0, Math.min(n - 1, panel.__page + d)); showPage(panel); }
function showPage(panel){
  const pages = panel.__pages; panel.__blocks.forEach(b => b.style.display = 'none');
  (pages[panel.__page] || []).forEach(b => b.style.display = '');
  const pg = panel.__head.querySelector('.pgBtns'); pg.innerHTML = '';
  if (pages.length > 1){
    const mk = (t, f, on) => { const b = el('button', 'key pg' + (on ? ' on' : ''), pg, t); b.onclick = f; };
    mk('◀', () => turn(panel, -1));
    pages.forEach((p, i) => mk(String(i + 1), () => { panel.__page = i; showPage(panel); }, i === panel.__page));
    mk('▶', () => turn(panel, 1));
  }
  drawCurves(); drawGrid(true);
}
function paginate(panel, x, y, w, h, again){
  if (panel.id === 'pads'){
    const bw = Math.min(760, w - 26), port = document.body.classList.contains('port'), rows = port ? 4 : 2;
    const avail = h - 36 - 12 - 48 - (rows - 1) * 7 - 14;
    panel.style.setProperty('--padW', bw + 'px');
    panel.style.setProperty('--padH', Math.max(52, Math.min(150, Math.floor(avail / rows))) + 'px');
    panel.style.setProperty('--stepH', Math.max(12, Math.min(30, Math.floor((h - 36 - 14 - 26 - (w < 420 ? 84 : 44) - 30) / 15) - 2)) + 'px');
  }
  if (panel.id === 'mixer') panel.style.setProperty('--faderH', Math.max(60, Math.min(220, h - 36 - 190)) + 'px');
  const blocks = blocksOf(panel), body = panel.__body;
  Object.assign(panel.style, {left:x + 'px', top:y + 'px', width:w + 'px', height:h + 'px'});
  blocks.forEach(b => { b.style.display = 'none'; b.style.zoom = ''; });
  const fits = () => body.scrollHeight <= body.clientHeight + 1 && body.scrollWidth <= body.clientWidth + 1;
  const pages = [[]];
  for (const b of blocks){
    b.style.display = '';
    pages[pages.length - 1].push(b);
    if (!fits()){
      const cur = pages[pages.length - 1];
      if (cur.length > 1){ cur.pop(); cur.forEach(o => o.style.display = 'none'); pages.push([b]); }
      if (!fits()){ let z = Math.min(body.clientHeight / b.offsetHeight, body.clientWidth / b.offsetWidth, 1);
        for (let k = 0; k < 10 && !fits(); k++){ z *= 0.96; b.style.zoom = String(Math.max(0.3, z)); } }
    }
  }
  panel.__pages = pages; panel.__page = Math.min(panel.__page, pages.length - 1);
  const h0 = body.clientHeight;
  showPage(panel);
  if (!again && body.clientHeight !== h0) paginate(panel, x, y, w, h, true);
}
function layout(){
  const W = app.clientWidth, H = app.clientHeight; if (!W || !H) return;
  const basic = document.body.classList.contains('basic'), land = W > H;
  document.body.classList.toggle('land', land); document.body.classList.toggle('port', !land);
  const panels = [...document.querySelectorAll('.panel')];
  const pageId = basic ? null : view === 'machine' ? (land ? null : 'pads') : view;
  panels.forEach(p => p.classList.toggle('on', p.id === pageId));
  const place = (e, x, y, w, h) => Object.assign(e.style, {left:x + 'px', top:y + 'px', width:w + 'px', height:h + 'px'});
  const fitPlate = (x, y, w, h, align) => { let pw = w, ph = w / PLATE_AR; if (ph > h){ ph = h; pw = h * PLATE_AR; }
    place(plateWrap, x + (w - pw) / 2, align === 'top' ? y : y + (h - ph) / 2, pw, ph); return {pw, ph}; };
  place(stage, 0, 0, W, H);
  if (basic){
    place(rail, 6, 6, 80, 34);
    machineView.style.display = ''; place(machineView, 0, 0, W, H); $('presetBar').style.display = 'none';
    fitPlate(0, 0, W, H);
  } else if (land){
    const rw = Math.max(70, Math.min(100, W * 0.1));
    place(rail, 0, 0, rw, H);
    const x0 = rw + 4, w = W - x0;
    if (!pageId){
      machineView.style.display = ''; place(machineView, x0, 0, w, H);
      const pb = $('presetBar'); pb.style.display = '';
      const sh = 44, under = w / PLATE_AR + sh + 6 <= H || (H - sh - 6) * PLATE_AR > (w - 150);
      pb.classList.toggle('strip', under);
      if (under){
        const {pw, ph} = fitPlate(0, 0, w, H - sh - 6, 'top');
        const py = Math.max(0, (H - ph - sh - 6) / 2); plateWrap.style.top = py + 'px';
        place(pb, parseFloat(plateWrap.style.left), py + ph + 6, pw, sh);
      } else {
        const mw = Math.max(92, Math.min(140, w * 0.15));
        const {pw, ph} = fitPlate(0, 0, w - mw - 6, H);
        const px = parseFloat(plateWrap.style.left);
        place(pb, Math.min(px + pw + 6, w - mw), (H - ph) / 2, mw, ph);
      }
    } else { machineView.style.display = 'none'; paginate($(pageId), x0, 0, w, H); }
  } else {
    machineView.style.display = ''; place(machineView, 0, 0, W, W / PLATE_AR + 52);
    const {ph} = fitPlate(0, 0, W, W / PLATE_AR, 'top');
    const pb = $('presetBar'); pb.style.display = ''; pb.classList.add('strip'); place(pb, 0, ph + 4, W, 46);
    const ry = ph + 54, rh = Math.min(84, Math.max(62, H * 0.1));
    place(rail, 0, ry, W, rh);
    paginate($(pageId), 0, ry + rh + 4, W, H - ry - rh - 4);
  }
  document.querySelectorAll('#rail [data-tab]').forEach(b => b.classList.toggle('on', b.dataset.tab === view));
}
document.querySelectorAll('#rail [data-tab]').forEach(b => b.addEventListener('click', () => { view = b.dataset.tab; layout(); refresh(); }));
new ResizeObserver(() => layout()).observe(app);
addEventListener('orientationchange', () => setTimeout(layout, 250));
const foldBtn = $('foldBtn');
const setFold = basic => {
  document.body.classList.toggle('basic', basic);
  foldBtn.textContent = basic ? 'EXPAND' : 'FOLD'; foldBtn.setAttribute('aria-pressed', String(basic));
  try { localStorage.setItem('omega-linn-basic', basic ? '1' : '0'); } catch (e){}
  requestAnimationFrame(layout);
};
foldBtn.onclick = () => setFold(!document.body.classList.contains('basic'));
let startBasic = false; try { startBasic = localStorage.getItem('omega-linn-basic') === '1'; } catch (e){}
document.addEventListener('pointerdown', () => { if (!S.power) startAudio(); }, {once:true, capture:true});
window.addEventListener('resize', () => drawCurves());
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidLinn) navigator.serviceWorker.register('sw.js').catch(() => {});

window.LINN = {S, post, save, refresh, toast, backup, applyBackup, startAudio, sendAll, sendRoms, setParam, listeners, layout, fillPresets, b64, loadRom, romOf, romName, packLayer, showFlash, drawGrid, get factory(){ return factory; }};
loadRom().then(() => refresh()).catch(() => {});
setFold(startBasic);
refresh(); showPresetName();
})();
