/* Omega Traks app: the front panel (faceplate.js) wired to the firmware in the audio thread (worklet.js: engine.js +
   machine.js), the Omega pages (PADS, SOUND, PAD EQ, MIXER, FX, MASTER, PRESETS) and the no-scroll layout.
   io.js adds files and MIDI on top of this (window.TRAKS). */
'use strict';
(() => {
const D = OTRAKS, M = OTMACH, PR = OTPRESETS, K = M.K;
const {P, PARAMS, NV, VOICES, KEY_NAMES, vp, vq} = D;
const STORE = 'omega-traks-v1';
const $ = id => document.getElementById(id);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

/* ---------------- state ---------------- */
const S = {
  params:D.defaults(), mem:M.defaultMemory(), pat:0, song:0, quant:16, roms:{}, userPresets:[], preset:0, edited:false, metroAlways:false,
  midi:{in:'all', out:'', ch:9, sync:false, clockOut:false, notesOut:true, accVel:110, pc:true},
  disp:{d:{l:'  ', r:'00', lp:1, ls:0, run:false, sel:0}, running:false, tk:-1, len:192, ppat:0, meter:[]},
  sel:0, pad:0, power:false,
};
function backup(){ return {format:'omega-traks', version:1, mem:S.mem, params:S.params, pat:S.pat, song:S.song, quant:S.quant, roms:S.roms, userPresets:S.userPresets, preset:S.preset, midi:S.midi, metroAlways:S.metroAlways}; }
function applyBackup(j, quiet){
  if (!j || j.format !== 'omega-traks') throw new Error('Not an Omega Traks backup');
  if (j.mem && Array.isArray(j.mem.pats) && j.mem.pats.length === 100 && Array.isArray(j.mem.songs)) S.mem = j.mem;
  if (Array.isArray(j.params)) j.params.forEach((v, i) => { if (i < PARAMS.length && typeof v === 'number') S.params[i] = v; });
  for (const k of ['pat', 'song', 'quant', 'preset']) if (typeof j[k] === 'number') S[k] = j[k];
  if (j.roms && typeof j.roms === 'object') S.roms = j.roms;
  if (Array.isArray(j.userPresets)) S.userPresets = j.userPresets.filter(u => u && typeof u.name === 'string' && Array.isArray(u.p));
  if (j.midi) Object.assign(S.midi, j.midi);
  if (typeof j.metroAlways === 'boolean') S.metroAlways = j.metroAlways;
  if (!quiet){ sendAll(); refresh(); }
}
try { const j = JSON.parse(localStorage.getItem(STORE) || 'null'); if (j) applyBackup(j, true); } catch (e){ console.warn(e); }
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify(backup())); } catch (e){ console.warn('save', e); } }, 400); }

/* ---------------- audio ---------------- */
let ctx = null, node = null;
const listeners = [];
let factory = null;                             // the ROM image, parsed
function post(m){ if (node) node.port.postMessage(m); }
const b64 = {enc(u8){ let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return btoa(s); },
  dec(s){ const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }};
function romOf(v){ const r = S.roms[v]; if (r && r.codes) return {codes:b64.dec(r.codes), rate:r.rate}; return factory ? factory[v] : null; }
function sendRoms(){ for (let v = 0; v < NV; v++){ const r = romOf(v); if (r) post({type:'rom', v, codes:r.codes, rate:r.rate}); } }
function sendAll(){ post({type:'params', p:S.params}); post({type:'mem', mem:S.mem}); post({type:'state', pat:S.pat, song:S.song, quant:S.quant}); post({type:'metro', on:S.metroAlways}); sendRoms(); }
function setParam(i, v){ S.params[i] = v; post({type:'params', p:S.params}); if (!S.edited && !PR.KEEP.includes(PARAMS[i][0])){ S.edited = true; showPresetName(); } save(); }
async function loadRom(){ if (factory) return; const r = await fetch('rom/traks.rom'); factory = D.parseRom(new Uint8Array(await r.arrayBuffer())); }
let audioReady = null;
function startAudio(){
  if (audioReady){ if (ctx && ctx.state !== 'running') ctx.resume(); return audioReady; }
  return (audioReady = bootAudio());
}
async function bootAudio(){
  ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
  await loadRom();
  for (const f of ['engine.js', 'machine.js', 'worklet.js']) await ctx.audioWorklet.addModule(f);
  node = new AudioWorkletNode(ctx, 'traks', {numberOfInputs:0, outputChannelCount:[2]});
  node.port.onmessage = ({data}) => onAudio(data);
  node.connect(ctx.destination);
  sendAll();
  S.power = true;
}
function onAudio(m){
  switch (m.type){
    case 'disp': S.disp = m; if (m.d.sel !== S.sel){ S.sel = m.d.sel; selectPad(S.sel); }
      if (m.pat !== S.pat || m.song !== S.song || m.quant !== S.quant){ S.pat = m.pat; S.song = m.song; S.quant = m.quant; save(); }
      draw(); showMeters(m.meter); drawGrid(); if (view === 'pads' || view === 'machine') for (const f of padRefresh) f(); break;
    case 'mem': S.mem = m.mem; save(); drawGrid(true); break;
  }
  for (const f of listeners) f(m);
}

/* ---------------- the front panel ---------------- */
const plate = $('plate');
const UI = FP.build(plate);
let flash = null, casLed = -1, casUntil = 0;
function showFlash(l, r, ms){ flash = {l, r, until:Date.now() + ms}; draw(); setTimeout(draw, ms + 20); }
function knobParam(k){ return k === 0 ? vp(S.sel, 0) : k === 1 ? vp(S.sel, 2) : k === 2 ? P.metro : P.volume; }
function knobNorm(k){ const i = knobParam(k), q = PARAMS[i]; return (S.params[i] - q[2]) / (q[3] - q[2]); }
function draw(){
  const d = S.disp.d;
  const f = flash && flash.until > Date.now() ? flash : null;
  FP.setDigits(UI.dispL, f ? f.l : d.l); FP.setDigits(UI.dispR, f ? f.r : d.r);
  UI.leds.song.forEach((g, i) => FP.setLed(g, (d.ls >> i) & 1)); UI.leds.pat.forEach((g, i) => FP.setLed(g, (d.lp >> i) & 1));
  FP.setLed(UI.leds.run, d.run || d.rec);
  FP.setLed(UI.leds.save, casLed === 0 && Date.now() < casUntil); FP.setLed(UI.leds.load, casLed === 1 && Date.now() < casUntil);
  UI.sel.forEach((r, v) => r.setAttribute('opacity', v === S.sel ? 1 : 0));
  UI.knobs.forEach((k, i) => FP.setKnob(k, knobNorm(i)));
}
function showTempo(){ const s = String(Math.round(S.params[P.tempo])).padStart(4, ' '); showFlash(s.slice(0, 2), s.slice(2), 1500); }
function tempoStep(d){ setParam(P.tempo, Math.max(40, Math.min(250, S.params[P.tempo] + d))); showTempo(); }
let repeatT = 0;
for (const [id, g] of Object.entries(UI.keys)){
  const kid = +id;
  const down = async e => {
    e.preventDefault(); g.setPointerCapture && g.setPointerCapture(e.pointerId);
    if (!S.power){ await startAudio(); }
    g.classList.add('down');
    if (kid === K.SLOWER || kid === K.FASTER){ const d = kid === K.FASTER ? 1 : -1; tempoStep(d); clearInterval(repeatT); const t0 = Date.now(); repeatT = setInterval(() => { if (Date.now() - t0 > 420) tempoStep(d); }, 70); return; }
    if (kid === K.SAVE){ casLed = 0; casUntil = Date.now() + 1500; window.TRAKSIO && TRAKSIO.saveTape(); draw(); return; }
    if (kid === K.LOAD){ casLed = 1; casUntil = Date.now() + 1500; window.TRAKSIO && TRAKSIO.loadTape(); draw(); return; }
    post({type:'key', id:kid, down:true});
  };
  const up = () => {
    if (!g.classList.contains('down')) return;
    g.classList.remove('down'); clearInterval(repeatT);
    if (kid !== K.SLOWER && kid !== K.FASTER && kid !== K.SAVE && kid !== K.LOAD) post({type:'key', id:kid, down:false});
  };
  g.addEventListener('pointerdown', down);
  g.addEventListener('pointerup', up); g.addEventListener('pointercancel', up); g.addEventListener('lostpointercapture', up);
}
// knobs: drag up / down, double-tap for the default
function dragKnob(elm, getN, setN, dflt){
  let y0 = 0, n0 = 0, last = 0;
  elm.addEventListener('pointerdown', e => {
    e.preventDefault(); e.stopPropagation(); elm.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = getN();
    const now = Date.now(); if (now - last < 300 && dflt !== undefined) setN(dflt); last = now;
  });
  elm.addEventListener('pointermove', e => {
    if (!elm.hasPointerCapture(e.pointerId)) return;
    const h = elm.getBoundingClientRect().height || 60;
    setN(Math.max(0, Math.min(1, n0 + (y0 - e.clientY) / (h * 3))));
  });
  elm.addEventListener('wheel', e => { e.preventDefault(); setN(Math.max(0, Math.min(1, getN() - Math.sign(e.deltaY) * 0.02))); }, {passive:false});
}
UI.knobs.forEach((k, i) => dragKnob(k.g, () => knobNorm(i), n => {
  const pi = knobParam(i), q = PARAMS[pi];
  let v = q[2] + n * (q[3] - q[2]);
  v = i === 1 ? Math.round(v * 2) / 2 : Math.round(v * 100) / 100;
  setParam(pi, v); refresh();
  if (i === 0) showFlash('u ', String(Math.min(99, Math.round(v * 99))).padStart(2, '0'), 1200);
  else if (i === 1) showFlash(v < 0 ? ' -' : '  ', String(Math.abs(Math.round(v))).padStart(2, ' '), 1200);
  else showFlash('  ', String(Math.round(v * 10)).padStart(2, ' '), 900);
  if (i <= 1) post({type:'knob'});
}, (() => { const q = PARAMS[knobParam(i)]; return (q[4] - q[2]) / (q[3] - q[2]); })()));
setInterval(() => { if (flash || casUntil) draw(); }, 250);

/* ---------------- page widgets ---------------- */
const refreshers = [], padRefresh = [];
function refresh(){ for (const f of refreshers) f(); drawCurves(); draw(); }
function miniKnob(parent, label, get, set, fmt, dflt){
  const w = el('div', 'knobRow', parent);
  w.innerHTML = `<svg class="mk" viewBox="-26 -26 52 52"><g class="tk"></g><circle r="19" fill="#e8e8e8"/><circle r="16.5" fill="url(#gMk)"/><line class="ptr" x1="0" y1="-4" x2="0" y2="-16" stroke="#f0f0ee" stroke-width="3.2" stroke-linecap="round"/></svg><span>${label}</span><span class="val"></span>`;
  const svg = w.querySelector('svg'), tk = svg.querySelector('.tk'), ptr = svg.querySelector('.ptr'), val = w.querySelector('.val');
  for (let i = 0; i <= 10; i++){ const a = (-135 + 27 * i) * Math.PI / 180; const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle'); c.setAttribute('cx', Math.sin(a) * 23); c.setAttribute('cy', -Math.cos(a) * 23); c.setAttribute('r', 1.4); c.setAttribute('fill', '#f0f0ee'); tk.appendChild(c); }
  w.refresh = () => { ptr.setAttribute('transform', `rotate(${-135 + 270 * Math.max(0, Math.min(1, get()))})`); val.textContent = fmt(); };
  dragKnob(svg, get, n => { set(n); w.refresh(); afterTurn(); }, dflt);
  refreshers.push(w.refresh); w.refresh();
  return w;
}
function afterTurn(){ drawCurves(); }
// a knob on a parameter (or, with pad === true, on the picked sound's field f)
const hz = v => v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k' : Math.round(v) + '';
const pct = v => Math.round(v * 100) + '%';
const db = v => (v > 0 ? '+' : '') + v.toFixed(1) + 'dB';
const ms = v => v >= 1 ? v.toFixed(2) + 's' : Math.round(v * 1000) + 'ms';
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
function selector(parent, onPick){
  const g = el('div', 'sel13', parent);
  const bs = KEY_NAMES.map((n, v) => { const b = el('button', 'small', g, n); b.onclick = () => { S.sel = v; post({type:'sel', v}); onPick(v); }; return b; });
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
function selectPad(v){ S.pad = v; refresh(); }

/* ---------------- PADS: big pads and the step grid ---------------- */
{
  const pg = $('pads');
  const bx = el('div', 'box padsBox pads1', pg);
  // the transport on its own row at the top, like the hardware's RUN/STOP and ACCENT box, then the 13 pads by channel
  const tr = el('div', 'transport', bx);
  const ctl = (label, f) => { const b = el('button', '', tr, label); b.addEventListener('pointerdown', async e => { e.preventDefault(); if (!S.power) await startAudio(); f(true, b); });
    const up = () => f(false, b); b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); return b; };
  const runB = ctl('RUN / STOP', d => d && post({type:'key', id:K.RUN, down:true}));
  const recB = ctl('RECORD', d => { if (d) post({type:'recmode', on:!S.disp.rec && !(S.disp.group === 0 && S.disp.patFn >= 1 && S.disp.patFn <= 3)}); });
  ctl('ACCENT', (d, b) => { post({type:'key', id:K.ACCENT, down:d}); b.classList.toggle('on', d); });
  padRefresh.push(() => { const recFn = S.disp.group === 0 && S.disp.patFn >= 1 && S.disp.patFn <= 3; recB.classList.toggle('on', recFn); runB.classList.toggle('on', !!S.disp.running); });
  const padEls = [];
  for (const row of [[0, 1, 2, 9], [3, 4, 7, 8], [5, 6, 10, 11, 12]]){ const grid = el('div', 'padRow', bx); for (const v of row){
    const b = el('button', '', grid, KEY_NAMES[v]); padEls[v] = b;
    b.addEventListener('pointerdown', async e => { e.preventDefault(); if (!S.power) await startAudio();
      const r = b.getBoundingClientRect(), vel = Math.max(0.35, Math.min(1, 1.15 - (e.clientY - r.top) / r.height * 0.8));
      post({type:'note', v, vel});         // where it's hit sets the velocity: the top is loudest
      b.classList.add('hit'); });
    const up = () => { if (b.classList.contains('hit')){ b.classList.remove('hit'); post({type:'off', v}); } };
    b.addEventListener('pointerup', up); b.addEventListener('pointerleave', up); b.addEventListener('pointercancel', up);
  } }
  // step grid: the pattern on screen, one measure at a time, in steps of ERROR CORRECT (1/16 if off)
  const sb = el('div', 'box padsBox', pg, '<h3>STEP GRID</h3>');
  const head = el('div', 'kRow', sb);
  const info = el('span', 'revLcd', head);
  let meas = 0;
  const prevM = el('button', 'small', head, '◀ MEASURE'), nextM = el('button', 'small', head, 'MEASURE ▶'), accB = el('button', 'small', head, 'ACCENT');
  let accMode = false; accB.onclick = () => { accMode = !accMode; accB.classList.toggle('on', accMode); };
  const g2 = el('div', 'stepGrid', sb);
  prevM.onclick = () => { meas = Math.max(0, meas - 1); drawGrid(true); }; nextM.onclick = () => { meas++; drawGrid(true); };
  let cells = [], lastKey = '';
  window.drawGridImpl = force => {
    if (!sb.offsetParent && !force) return;
    const pi = S.disp.running ? S.disp.ppat : S.pat, pt = S.mem.pats[pi]; if (!pt) return;
    const q = S.quant ? 192 / S.quant : 12, ml = pt.num * 192 / pt.den, steps = Math.max(1, Math.min(32, Math.round(ml / q)));
    meas = Math.min(meas, pt.meas - 1);
    const key = pi + ':' + q + ':' + steps + ':' + meas;
    if (key !== lastKey || force){
      lastKey = key; g2.innerHTML = ''; cells = [];
      g2.style.gridTemplateColumns = `52px repeat(${steps}, 1fr)`;
      for (let v = 0; v < NV; v++){
        el('b', '', g2, KEY_NAMES[v]);
        const row = [];
        for (let s = 0; s < steps; s++){
          const c = el('i', (s * q) % (192 / pt.den) === 0 ? 'beat' : '', g2), t = meas * ml + s * q;
          c.onclick = () => { const on = pt.ev.find(x => x.v === v && x.t === t); post({type:'edit', p:pi, t, v, on:!on || (accMode && !on.a), acc:accMode}); if (!S.power) startAudio(); };
          row.push({c, t});
        }
        cells.push(row);
      }
    }
    const has = {}; for (const x of pt.ev) has[x.v + ':' + x.t] = x;
    const now = S.disp.running && S.disp.tk >= 0 ? S.disp.tk : -1;
    cells.forEach((row, v) => row.forEach(({c, t}) => { const x = has[v + ':' + t]; c.classList.toggle('on', !!x); c.classList.toggle('acc', !!(x && x.a)); c.classList.toggle('now', now >= t && now < t + q); }));
    info.textContent = `PAT ${String(pi).padStart(2, '0')}  ${pt.num}/${pt.den}  M ${meas + 1}/${pt.meas}  ${S.quant ? '1/' + S.quant : '1/16'}`;
  };
}
function drawGrid(force){ if (window.drawGridImpl) window.drawGridImpl(force); }

/* ---------------- SOUND ---------------- */
{
  const pg = $('sound');
  const b1 = el('div', 'box', pg, '<h3>SOUND</h3>');
  selector(b1, selectPad);
  const k = el('div', 'kRow', b1), f = n => () => vp(S.sel, n);
  pknob(k, 'VOLUME', f(0), 'lin', v => String(Math.min(99, Math.round(v * 99)))); pknob(k, 'TUNING', f(2), 'lin', v => (v > 0 ? '+' : '') + v.toFixed(1));
  pknob(k, 'DECAY', f(3), 'log', v => v >= 2.999 ? 'FULL' : ms(v)); pknob(k, 'PAN', f(1), 'lin', v => Math.abs(v) < 0.005 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v) * 100));
  pknob(k, 'CUTOFF', f(7), 'log', v => v >= 19999 ? 'OPEN' : hz(v)); pknob(k, 'RESO', f(8), 'lin', pct); pknob(k, 'START', f(9), 'lin', pct);
  pknob(k, 'OUTPUT', f(10), 'int', v => v ? 'CH' + v : 'OWN');
  const b2 = el('div', 'box', pg, '<h3>SENDS · DRIVE · ROM</h3>');
  const k2 = el('div', 'kRow', b2);
  pknob(k2, 'REVERB', f(5), 'lin', pct); pknob(k2, 'DELAY', () => vq(S.sel, 12), 'lin', pct); pknob(k2, 'DRIVE', () => vq(S.sel, 11), 'lin', pct);
  const t = el('div', 'kRow', b2);
  toggleBtn(t, 'REVERSE', f(6)); toggleBtn(t, 'MUTE', f(4));
  const rn = el('p', '', b2);
  refreshers.push(() => { const r = S.roms[S.sel]; rn.innerHTML = `ROM: <b>${r ? r.name : factory && factory[S.sel] && factory[S.sel].codes.length <= 2 ? 'EMPTY (no recording yet)' : 'Drumtraks'}</b>. Load your own sound onto this key: it's burned into the ROM (25 kHz, 8-bit companded) and plays through the machine like the original.`; });
  const t2 = el('div', 'kRow', b2);
  el('button', 'small', t2, 'LOAD SOUND').onclick = () => window.TRAKSIO && TRAKSIO.loadSound(S.sel);
  el('button', 'small', t2, 'BUILT-IN SOUND').onclick = () => { delete S.roms[S.sel]; sendRoms(); save(); refresh(); toast('Built-in ROM back on ' + KEY_NAMES[S.sel]); };
  const b3 = el('div', 'box', pg, '<h3>ACCENT · METRONOME</h3>');
  const k3 = el('div', 'kRow', b3);
  pknob(k3, 'ACCENT', P.accent, 'lin', v => '+' + v.toFixed(1) + 'dB'); pknob(k3, 'METRONOME', P.metro, 'lin', v => v.toFixed(1)); pknob(k3, 'TEMPO', P.tempo, 'int', v => v + ' BPM');
  const mb = el('button', 'small', k3, 'CLICK ALWAYS'); mb.onclick = () => { S.metroAlways = !S.metroAlways; post({type:'metro', on:S.metroAlways}); save(); refresh(); };
  refreshers.push(() => mb.classList.toggle('on', S.metroAlways));
}

/* ---------------- PAD EQ / ADSR ---------------- */
{
  const pg = $('padeq');
  const b1 = el('div', 'box', pg, '<h3>PARAMETRIC EQ</h3>');
  selector(b1, selectPad);
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

/* ---------------- MIXER ---------------- */
const meterEls = [];
{
  const pg = $('mixer');
  for (let v = 0; v < NV; v++){
    const s = el('div', 'strip', pg, `<h4>${KEY_NAMES[v]}</h4>`);
    pknob(s, 'VOL', vp(v, 0), 'lin', x => String(Math.min(99, Math.round(x * 99))));
    pknob(s, 'PAN', vp(v, 1), 'lin', x => Math.abs(x) < 0.005 ? 'C' : (x < 0 ? 'L' : 'R') + Math.round(Math.abs(x) * 100));
    pknob(s, 'REV', vp(v, 5), 'lin', pct);
    const m = el('div', 'meter', s); meterEls.push(el('i', '', m));
    toggleBtn(s, 'MUTE', vp(v, 4));
  }
}
function showMeters(m){ if (!m) return; for (let v = 0; v < NV && v < meterEls.length; v++){ const d = 20 * Math.log10(Math.max(1e-4, m[v] * 1.4)); meterEls[v].style.width = Math.max(0, Math.min(100, (d + 48) / 48 * 100)) + '%'; } }

/* ---------------- FX: REV 7 and the TC delay ---------------- */
{
  const pg = $('fx');
  const r = el('div', 'box', pg, '<h3>REV 7 · DIGITAL REVERBERATOR</h3>');
  const progs = el('div', 'revProgs', r);
  D.RV_PROGS.forEach((n, i) => { const b = el('button', 'small', progs, n); b.onclick = () => { setParam(P.rvProg, i); refresh(); }; });
  refreshers.push(() => [...progs.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[P.rvProg]))));
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
  for (let v = 0; v < NV; v++) pknob(k3, KEY_NAMES[v], vq(v, 12), 'lin', pct);
}

/* ---------------- MASTER / MACHINE ---------------- */
{
  const pg = $('master');
  const e = el('div', 'box', pg, '<h3>MASTER PARAMETRIC EQ</h3>');
  eqCurve(e, () => [[0, S.params[P.mLsF], S.params[P.mLsG], .707], [1, S.params[P.mP1F], S.params[P.mP1G], S.params[P.mP1Q]], [1, S.params[P.mP2F], S.params[P.mP2G], S.params[P.mP2Q]], [2, S.params[P.mHsF], S.params[P.mHsG], .707]]);
  const k = el('div', 'kRow', e);
  pknob(k, 'LOW F', P.mLsF, 'log', hz); pknob(k, 'LOW', P.mLsG, 'lin', db); pknob(k, 'MID1 F', P.mP1F, 'log', hz); pknob(k, 'MID1', P.mP1G, 'lin', db); pknob(k, 'MID1 Q', P.mP1Q, 'log');
  pknob(k, 'MID2 F', P.mP2F, 'log', hz); pknob(k, 'MID2', P.mP2G, 'lin', db); pknob(k, 'MID2 Q', P.mP2Q, 'log'); pknob(k, 'HIGH F', P.mHsF, 'log', hz); pknob(k, 'HIGH', P.mHsG, 'lin', db);
  const o = el('div', 'box', pg, '<h3>OVERDRIVE · CLIPPER</h3>'), k2 = el('div', 'kRow', o);
  pknob(k2, 'DRIVE', P.odDrive, 'lin', pct); pknob(k2, 'MIX', P.odMix, 'lin', pct); pknob(k2, 'OUTPUT', P.odOut, 'lin', db);
  const modes = el('div', 'revProgs', o);
  D.OD_MODES.forEach((n, i) => { const b = el('button', 'small', modes, n); b.onclick = () => { setParam(P.odMode, i); refresh(); }; });
  refreshers.push(() => [...modes.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[P.odMode]))));
  const m = el('div', 'box', pg, '<h3>MACHINE</h3><p>The DAC: the stock 8-bit companded codes, or re-quantized linear 8 / 6 bits. SMOOTH interpolates instead of holding each sample (the hardware holds). POLY lets every sound ring on (the hardware has one sound per channel). FILTER BYPASS opens the output stage.</p>');
  const dm = el('div', 'revProgs', m);
  D.DAC_MODES.forEach((n, i) => { const b = el('button', 'small', dm, n); b.onclick = () => { setParam(P.dac, i); refresh(); }; });
  refreshers.push(() => [...dm.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[P.dac]))));
  const t = el('div', 'kRow', m);
  toggleBtn(t, 'SMOOTH', P.interp); toggleBtn(t, 'POLY', P.poly); toggleBtn(t, 'FILTER BYPASS', P.filt);
  const k3 = el('div', 'kRow', m); pknob(k3, 'MASTER', P.volume, 'lin', v => v.toFixed(1)); pknob(k3, 'ACCENT', P.accent, 'lin', v => '+' + v.toFixed(1) + 'dB');
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
  S.params = PR.load(S.params, src.p); S.preset = k; S.edited = false;
  post({type:'params', p:S.params}); save(); refresh(); showPresetName();
}
const allPresets = () => PR.PRESETS.map((p, i) => i).concat(S.userPresets.map((u, k) => 1000 + k));
const stepPreset = d => { const all = allPresets(); let i = all.indexOf(S.preset); i = (i + d + all.length) % all.length; loadPreset(all[i]); };
$('pPrev').onclick = () => stepPreset(-1); $('pNext').onclick = () => stepPreset(1);
$('pPart').onclick = () => { diceIdx = (diceIdx + 1) % PR.DICE_PARTS.length; $('pPart').textContent = PART_NAMES[PR.DICE_PARTS[diceIdx]]; };
$('pDice').onclick = () => { S.params = PR.load(S.params, PR.dice(S.params, PR.DICE_PARTS[diceIdx])); S.edited = true; post({type:'params', p:S.params}); save(); refresh(); showPresetName(); };
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
  el('button', 'small', acts, 'INIT').onclick = () => { S.params = PR.load(S.params, D.defaults()); S.edited = true; post({type:'params', p:S.params}); save(); refresh(); showPresetName(); };
  pp.__page = keep; showPresetName();
}
fillPresets();

/* ---------------- HOW TO ---------------- */
$('help').innerHTML = `<div class="help box"><h3>HOW TO</h3>
<p><b>PAGES</b>: nothing scrolls. The buttons on the side pick a page; ◀ ▶ and the numbers at a page's top (or a swipe) turn it. FOLD shows the bare machine as big as it gets.</p>
<p><b>Play a pattern</b>: SELECT PATTERN FUNCTIONS lights PATTERN#. Type two digits on the keypad (or ◀ ▶) and press RUN/STOP. While it runs, a new number waits for the end of the pattern.</p>
<p><b>Record</b>: press SELECT PATTERN FUNCTIONS again: RECORD MODE. RUN/STOP starts it with the metronome; play the instrument keys (hold ACCENT for accents). Notes land on the ERROR CORRECT grid (the next LED: type 04, 08, 12, 16, 24, 32, 48, 64, or 00 for off). Move VOLUME / TUNING while recording and the notes keep those values.</p>
<p><b>ERASE INSTRUMENT</b>: while it runs, hold an instrument key to wipe that sound as the pattern passes.</p>
<p><b>SWING%</b> (50-75), <b>TIME SIGNATURE</b> (left: beats, typed; ◀ ▶: the note value), <b># OF MEASURES</b> (1-99). <b>ERASE PATTERN</b>: type the number, ENTER. <b>COPY / APPEND TO</b>: type where, ENTER (into an empty pattern it copies, onto one with notes it appends).</p>
<p><b>Songs</b>: SELECT SONG FUNCTIONS: SONG# (two digits), ERASE SONG, COPY / APPEND TO, BUILD MODE. In BUILD MODE the left display is the step, the right the pattern: type a pattern, ENTER writes it and moves on; ◀ ▶ move. VOL CHANGE (00-99) and TEMPO CHANGE (three digits) put a change on the step with ENTER (ENTER alone clears it); INSERT and DELETE work on the step. With the song functions lit, RUN/STOP plays the song.</p>
<p><b>VOLUME / TUNING per instrument</b>: they act on the last instrument you pressed (it's marked under its key). <b>TEMPO</b>: SLOWER / FASTER (hold to run). <b>CASSETTE</b>: SAVE writes the whole machine to a file, LOAD reads one back (the plugin reads the same files).</p>
<p><b>PADS</b>: big pads, RUN / STOP, ACCENT, RECORD, and a step grid for the pattern on screen. <b>SOUND</b>: per-sound decay, pan, filter, reverse, start, output, sends; LOAD SOUND burns your own sample into the sound's ROM.</p>
<p><b>MIDI</b>: General MIDI drum notes play the sounds (velocity-sensitive); clock in and out. Knobs: drag up / down, double-tap for the default.</p></div>`;

/* ---------------- the screen: nothing ever scrolls (see CLAUDE.md; the owner's phone is a Galaxy S23) ---------------- */
const app = $('app'), stage = $('stage'), machineView = $('machineView'), plateWrap = $('plateWrap'), rail = $('rail');
const TITLES = {pads:'PADS · STEP GRID', sound:'SOUND', padeq:'PAD EQ · ADSR', mixer:'MIXER', fx:'REV 7 · TC DELAY', master:'MASTER · MACHINE', presets:'PRESETS', file:'FILES', midi:'MIDI', help:'HOW TO'};
const PLATE_AR = 2120 / 916;
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
    // the pads fill the page: the box takes the page's width (up to 560) and the rows share its height
    const bw = Math.min(760, w - 26), avail = h - 36 - 12 - 46 - 3 * 7 - 12;
    panel.style.setProperty('--padW', bw + 'px');
    panel.style.setProperty('--padH', Math.max(60, Math.min(150, Math.floor(avail / 3))) + 'px');
    panel.style.setProperty('--stepH', Math.max(14, Math.min(34, Math.floor((h - 36 - 14 - 26 - (w < 420 ? 84 : 44) - 30) / 13) - 2)) + 'px');
  }
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
      // the machine as big as it goes: full width, the preset module as a strip under it (or beside it on tall screens)
      machineView.style.display = ''; place(machineView, x0, 0, w, H);
      const pb = $('presetBar'); pb.style.display = '';
      const sh = 44, under = w / PLATE_AR + sh + 6 <= H || (H - sh - 6) * PLATE_AR > (w - 150) ;
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
  try { localStorage.setItem('omega-traks-basic', basic ? '1' : '0'); } catch (e){}
  requestAnimationFrame(layout);
};
foldBtn.onclick = () => setFold(!document.body.classList.contains('basic'));
let startBasic = false; try { startBasic = localStorage.getItem('omega-traks-basic') === '1'; } catch (e){}
// the first touch anywhere starts the audio (browsers need a gesture)
document.addEventListener('pointerdown', () => { if (!S.power) startAudio(); }, {once:true, capture:true});
window.addEventListener('resize', () => drawCurves());
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidTraks) navigator.serviceWorker.register('sw.js').catch(() => {});

window.TRAKS = {S, post, save, refresh, toast, backup, applyBackup, startAudio, sendAll, sendRoms, setParam, listeners, layout, fillPresets, b64, loadRom, romOf, showFlash, get factory(){ return factory; }};
loadRom().catch(() => {});
setFold(startBasic);
refresh(); showPresetName();
})();
