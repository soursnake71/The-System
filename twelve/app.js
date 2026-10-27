/* Omega Twelve app: UI, playback, file import/export. The sound is rendered by worker.js with engine.js. */
'use strict';
const $ = s => document.querySelector(s);
const PBYID = Object.fromEntries(TW.PARAMS.map(p => [p.id, p]));
const PRESETS = TW.makePresets();
const FAMS = [...new Set(PRESETS.map(p => p.family))];
const STORE = 'omega-twelve';

const S = {
  p: TW.defaults(), match: new Array(10).fill(0), preset: -1, presetName: 'Init',
  sel: [0, 0], loop: false, zoom: false, bypass: false, bpm: null, key: null, ref: null, refName: '',
  user: []   // the user's own presets
};
let src = null;          // {chs:[Float32Array], sr, name}
let undo = [];
let out = null;          // AudioBuffer of the current render
let outStats = null;

/* ---------------- storage ---------------- */
function save(){
  try { localStorage.setItem(STORE, JSON.stringify({p:S.p, match:S.match, preset:S.preset, presetName:S.presetName, loop:S.loop, user:S.user, tgt:$('#tgtBpm').value, fmt:$('#fmt').value})); } catch (e) {}
}
function load(){
  try {
    const d = JSON.parse(localStorage.getItem(STORE) || 'null'); if (!d) return;
    Object.assign(S.p, d.p || {}); S.match = d.match || S.match; S.preset = d.preset ?? -1; S.presetName = d.presetName || 'Init';
    S.loop = !!d.loop; S.user = d.user || [];
    if (d.tgt) $('#tgtBpm').value = d.tgt;
    if (d.fmt) setTimeout(() => { if ([...$('#fmt').options].some(o => o.value === d.fmt)) $('#fmt').value = d.fmt; });
  } catch (e) {}
}

/* ---------------- toast / busy ---------------- */
let toastT = 0;
function toast(msg, ms){ const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('show'), ms || 2600); }
let busyN = 0;
function busy(d){ busyN = Math.max(0, busyN + d); $('#busy').classList.toggle('show', busyN > 0); }

/* ---------------- audio ---------------- */
let ctx = null, bus = null;
function audio(){
  if (!ctx){
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    bus = ctx.createGain(); bus.connect(ctx.destination);
  }
  if (ctx.state === 'suspended') ctx.resume();
  return ctx;
}
const FS = () => (ctx ? ctx.sampleRate : 48000);

/* ---------------- worker ---------------- */
const worker = new Worker('worker.js');
let wid = 0;
const waiting = new Map();
worker.onmessage = e => {
  const d = e.data, cb = waiting.get(d.id);
  if (cb){ waiting.delete(d.id); busy(-1); d.type === 'error' ? cb.reject(new Error(d.msg)) : cb.resolve(d); }
};
function ask(msg, transfer){
  const id = ++wid; busy(1);
  return new Promise((resolve, reject) => { waiting.set(id, {resolve, reject}); worker.postMessage(Object.assign({id}, msg), transfer || []); });
}
function sendSource(){ worker.postMessage({type:'source', chs:src.chs.map(c => c.slice()), sr:src.sr}); padCache.clear(); }

/* ---------------- rendering ---------------- */
let renderT = 0, renderId = 0;
function requestRender(soon){
  clearTimeout(renderT); padCache.clear();
  if (!src) return;
  renderT = setTimeout(doRender, soon ? 0 : 110);
}
async function doRender(){
  const my = ++renderId;
  try {
    const r = await ask({type:'render', p:S.p, match:S.match, sel:S.sel, fs:FS()});
    if (my !== renderId) return;
    const was = playing && !S.bypass ? playPos() : null;
    out = toBuffer(r.out, r.fs); outStats = r;
    $('#iOut').textContent = `${r.lufs} LUFS · ${r.peak} dBFS`;
    $('#mMeter').textContent = `${r.lufs} LUFS · peak ${r.peak} dB`;
    if (was !== null) startMain(was);
    drawWave();
  } catch (e){ toast('Render failed: ' + e.message); }
}
function toBuffer(chs, fs){
  const b = audio().createBuffer(chs.length, chs[0].length, fs);
  chs.forEach((c, i) => b.copyToChannel(c, i));
  return b;
}

/* ---------------- playback ---------------- */
let mainSrc = null, playing = false, playT0 = 0, playOff = 0, playDur = 1;
const voices = new Set();
function srcBuffer(){
  // bypass: the clean selection
  const [a, b] = S.sel, chs = src.chs.map(c => c.slice(a, b));
  return toBuffer(chs, src.sr);
}
function startMain(frac){
  stopMain(true);
  const buf = S.bypass ? srcBuffer() : out; if (!buf) return;
  const c = audio(), s = c.createBufferSource();
  s.buffer = buf; s.loop = S.loop; s.connect(bus);
  playDur = buf.duration; playOff = (frac || 0) * playDur;
  s.start(0, playOff); playT0 = c.currentTime;
  s.onended = () => { if (mainSrc === s){ mainSrc = null; playing = false; $('#playBtn').classList.remove('on'); } };
  mainSrc = s; playing = true; $('#playBtn').classList.add('on');
  requestAnimationFrame(tick);
}
function stopMain(quiet){
  if (mainSrc){ try { mainSrc.onended = null; mainSrc.stop(); } catch (e) {} mainSrc = null; }
  playing = false; if (!quiet) $('#playBtn').classList.remove('on');
}
function playPos(){
  if (!playing) return 0;
  let t = playOff + (ctx.currentTime - playT0);
  if (S.loop) t %= playDur;
  return Math.min(1, t / playDur);
}
function tick(){ drawWave(); if (playing) requestAnimationFrame(tick); }
function stopAll(){
  stopMain();
  for (const v of voices){ try { v.stop(); } catch (e) {} }
  voices.clear();
  if (recorder && recorder.state !== 'inactive'){ recorderCancelled = true; recorder.stop(); }
  if (ctx && bus){
    // cut anything still sounding: swap in a fresh output bus
    bus.disconnect(); bus = ctx.createGain(); bus.connect(ctx.destination);
  }
  document.querySelectorAll('.pad.hit').forEach(p => p.classList.remove('hit'));
  drawWave();
}

/* ---------------- pads (multi-pitch) ---------------- */
const PAD_SEMIS = [-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7];
const padCache = new Map();
async function padBuffer(semis){
  if (padCache.has(semis)) return padCache.get(semis);
  const pr = ask({type:'pad', p:S.p, match:S.match, sel:S.sel, fs:FS(), semis}).then(r => toBuffer(r.out, r.fs));
  padCache.set(semis, pr);
  return pr;
}
async function hitPad(semis, vel){
  if (!src){ toast('Import a sample first'); return; }
  audio();
  const el = document.querySelector(`.pad[data-s="${semis}"]`);
  if (el){ el.classList.add('hit'); setTimeout(() => el.classList.remove('hit'), 120); }
  try {
    const buf = await padBuffer(semis), s = ctx.createBufferSource(), g = ctx.createGain();
    g.gain.value = vel == null ? 1 : vel;
    s.buffer = buf; s.connect(g).connect(bus); s.start();
    voices.add(s); s.onended = () => voices.delete(s);
  } catch (e){ toast(e.message); }
}
function buildPads(){
  const box = $('#pads');
  // top row highest, like a pad grid
  const order = [12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3];
  for (const i of order){
    const s = PAD_SEMIS[i], b = document.createElement('div');
    b.className = 'pad'; b.dataset.s = s; b.textContent = (s > 0 ? '+' : '') + s;
    b.addEventListener('pointerdown', e => { e.preventDefault(); hitPad(s); });
    box.appendChild(b);
  }
}

/* ---------------- knobs ---------------- */
const knobs = new Map();
function norm(p, v){ return p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min); }
function denorm(p, n){ n = TW.clamp(n, 0, 1); let v = p.log ? p.min * Math.pow(p.max / p.min, n) : p.min + n * (p.max - p.min); if (p.step) v = Math.round(v / p.step) * p.step; return TW.clamp(+v.toFixed(4), p.min, p.max); }
function fmtVal(p, v){
  if (p.choices) return p.choices[Math.round(v)] || '';
  if (p.toggle) return v > .5 ? 'On' : 'Off';
  switch (p.unit){
    case 'Hz': return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k' : Math.round(v) + '';
    case '%': return Math.round(v * 100) + '%';
    case 'dB': return (v > 0 ? '+' : '') + v.toFixed(1);
    case 'st': return (v > 0 ? '+' : '') + Math.round(v) + ' st';
    case 'ct': return (v > 0 ? '+' : '') + Math.round(v) + ' ct';
    case 'x': return v.toFixed(2) + '×';
    case 'ms': return (v < 10 ? v.toFixed(1) : Math.round(v)) + ' ms';
    case 'bit': return Math.round(v) + '';
    case ':1': return v.toFixed(1) + ':1';
    default: return (Math.abs(v) < 10 ? v.toFixed(2) : v.toFixed(1)) + (p.unit || '');
  }
}
function knobSvg(){
  return `<svg viewBox="0 0 52 52"><circle cx="26" cy="26" r="23" fill="#2b2d30" opacity=".18"/><path class="arc" fill="none" stroke="#e2a83a" stroke-width="3" stroke-linecap="round"/>
  <circle cx="26" cy="26" r="17" fill="url(#kg)"/><circle cx="26" cy="26" r="17" fill="none" stroke="#8f9297" stroke-width=".8"/><line class="ind" x1="26" y1="26" x2="26" y2="12" stroke="#d8402f" stroke-width="2.6" stroke-linecap="round"/></svg>`;
}
function makeKnob(id, box, opts){
  const p = PBYID[id], el = document.createElement('div');
  el.className = 'knob'; el.innerHTML = knobSvg() + `<div class="kv"></div><div class="kl">${(opts && opts.label) || p.name}</div>`;
  box.appendChild(el);
  let y0 = 0, n0 = 0, moved = false, lastTap = 0;
  el.addEventListener('pointerdown', e => { el.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = norm(p, S.p[id]); moved = false; });
  el.addEventListener('pointermove', e => {
    if (!el.hasPointerCapture(e.pointerId)) return;
    const dy = y0 - e.clientY; if (Math.abs(dy) > 2) moved = true;
    if (!moved) return;
    setParam(id, denorm(p, n0 + dy / (e.shiftKey ? 800 : 180)));
  });
  el.addEventListener('pointerup', e => {
    el.releasePointerCapture(e.pointerId);
    if (!moved){ const t = Date.now(); if (t - lastTap < 350) setParam(id, p.def); lastTap = t; }
    commit();
  });
  el.addEventListener('wheel', e => { e.preventDefault(); setParam(id, denorm(p, norm(p, S.p[id]) - Math.sign(e.deltaY) * .02)); commit(); }, {passive:false});
  knobs.set(id, el); paintKnob(id);
}
function paintKnob(id){
  const el = knobs.get(id); if (!el) return;
  const p = PBYID[id], n = TW.clamp(norm(p, S.p[id]), 0, 1), a0 = -135, a = a0 + n * 270;
  const pt = deg => { const r = (deg - 90) * Math.PI / 180; return [26 + 23 * Math.cos(r), 26 + 23 * Math.sin(r)]; };
  const [x0, y0] = pt(a0), [x1, y1] = pt(a);
  el.querySelector('.arc').setAttribute('d', n > .002 ? `M${x0} ${y0} A23 23 0 ${a - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}` : '');
  el.querySelector('.ind').setAttribute('transform', `rotate(${a} 26 26)`);
  el.querySelector('.kv').textContent = fmtVal(p, S.p[id]);
}
function makeSelect(id, box, label){
  const p = PBYID[id], f = document.createElement('label');
  f.className = 'field'; f.innerHTML = `${label || p.name}<select></select>`;
  const s = f.querySelector('select'); p.choices.forEach((c, i) => s.add(new Option(c, i)));
  s.addEventListener('change', () => { if (id === 'machine') setMachine(+s.value); else setParam(id, +s.value); commit(); });
  box.appendChild(f); knobs.set(id, f);
}
function makeToggle(id, box, label){
  const b = document.createElement('button'); b.className = 'btn'; b.textContent = label || PBYID[id].name;
  b.addEventListener('click', () => { setParam(id, S.p[id] > .5 ? 0 : 1); commit(); });
  box.appendChild(b); knobs.set(id, b);
}
function paintAll(){
  for (const [id, el] of knobs){
    const p = PBYID[id];
    if (el.classList.contains('knob')) paintKnob(id);
    else if (el.tagName === 'BUTTON') el.classList.toggle('on', S.p[id] > .5);
    else el.querySelector('select').value = Math.round(S.p[id]);
  }
  $('#lcdMachine').value = S.p.machine; $('#lcdFilter').value = S.p.ftype;
  $('#lcdBits').textContent = Math.round(S.p.bits);
  $('#lcdRate').textContent = (S.p.rate / 1000).toFixed(S.p.rate < 10000 ? 2 : 1) + 'k';
  const st = S.p.semis + S.p.cents / 100; $('#lcdTune').textContent = (st > 0 ? '+' : '') + (Math.abs(S.p.cents) > 0 ? st.toFixed(2) : st);
  $('#lcdCut').textContent = S.p.ftype >= 1 && S.p.ftype <= 5 ? 'FIXED' : fmtVal(PBYID.cutoff, S.p.cutoff);
  $('#lcdRes').textContent = fmtVal(PBYID.res, S.p.res);
  $('#lcdMix').textContent = fmtVal(PBYID.mix, S.p.mix);
  $('#lcdPreset').textContent = S.presetName.toUpperCase();
  $('#pName').textContent = S.presetName;
  $('#mLed').classList.toggle('on', S.p.mOn > .5); $('#mOnBtn').classList.toggle('on', S.p.mOn > .5);
  $('#loopBtn').classList.toggle('on', S.loop); $('#bypassBtn').classList.toggle('on', S.bypass); $('#zoomBtn').classList.toggle('on', S.zoom);
  drawEq();
}
function setParam(id, v){ S.p[id] = v; if (!TW.MASTER_IDS.includes(id) && S.preset >= -1){ markEdited(); } paintAll(); }
function markEdited(){ if (!S.presetName.endsWith('*')) S.presetName += '*'; }
function commit(){ save(); requestRender(); }
function setMachine(m){
  const M = TW.MACHINES[m];
  Object.assign(S.p, {machine:m, rate:M.rate, bits:M.bits, ftype:M.ftype, cutoff:M.cutoff, res:M.res});
  markEdited(); paintAll();
}

function buildControls(){
  const add = (box, ids) => ids.forEach(id => { const p = PBYID[id]; if (p.choices) makeSelect(id, box); else if (p.toggle) makeToggle(id, box); else makeKnob(id, box); });
  add($('#kSampler'), ['machine', 'calib', 'bits', 'rate', 'alias', 'drive', 'rpm', 'mono']);
  add($('#kFilter'), ['ftype', 'cutoff', 'res', 'fenv', 'mix', 'retrig']);
  add($('#kTune'), ['semis', 'cents', 'a4']);
  add($('#kTime'), ['smode', 'stretch', 'grain']);
  add($('#kLofi'), ['crackle', 'hiss', 'wow', 'flutter', 'tape', 'age', 'hum', 'humHz']);
  add($('#kIo'), ['input', 'output']);
  add($('#kMaster'), ['lsF', 'lsG', 'p1F', 'p1G', 'p1Q', 'p2F', 'p2G', 'p2Q', 'p3F', 'p3G', 'p3Q', 'hsF', 'hsG', 'match', 'cThr', 'cRat', 'cAtk', 'cRel', 'cMake', 'mGain', 'lim']);
  TW.MACHINES.forEach((m, i) => $('#lcdMachine').add(new Option(m.name, i)));
  TW.FILTERS.forEach((f, i) => $('#lcdFilter').add(new Option(f.replace(/ \(.*/, '').replace('6-pole Butterworth', '6-POLE').replace('4-pole resonant', '4-POLE').replace('SSM2044', 'SSM2044').replace('Fixed', 'FIX ' + (i + 2)).replace('None', 'NONE'), i)));
  $('#lcdMachine').addEventListener('change', e => { setMachine(+e.target.value); commit(); });
  $('#lcdFilter').addEventListener('change', e => { setParam('ftype', +e.target.value); commit(); });
  NOTE_KEYS.forEach((k, i) => $('#toKey').add(new Option(k, i)));
}
const NOTE_KEYS = TW.NOTE_NAMES.flatMap(n => [n + ' major', n + ' minor']);

/* ---------------- EQ curve ---------------- */
function drawEq(){
  const c = $('#eqC'), w = c.clientWidth, h = c.clientHeight; if (!w) return;
  const dpr = devicePixelRatio || 1; c.width = w * dpr; c.height = h * dpr;
  const g = c.getContext('2d'); g.scale(dpr, dpr); g.clearRect(0, 0, w, h);
  const fs = 48000, p = S.p, secs = [TW.rbj('ls', fs, p.lsF, .707, p.lsG), TW.rbj('peak', fs, p.p1F, p.p1Q, p.p1G), TW.rbj('peak', fs, p.p2F, p.p2Q, p.p2G), TW.rbj('peak', fs, p.p3F, p.p3Q, p.p3G), TW.rbj('hs', fs, p.hsF, .707, p.hsG)];
  const mt = TW.MATCH_BANDS.map((f, i) => TW.rbj('peak', fs, f, 1.41, S.match[i] * p.match));
  const resp = (list, f) => list.reduce((s, q) => {
    const w0 = 2 * Math.PI * f / fs, cr = Math.cos(w0), ci = -Math.sin(w0), c2r = Math.cos(2 * w0), c2i = -Math.sin(2 * w0);
    const nr = q.b0 + q.b1 * cr + q.b2 * c2r, ni = q.b1 * ci + q.b2 * c2i, dr = 1 + q.a1 * cr + q.a2 * c2r, di = q.a1 * ci + q.a2 * c2i;
    return s + 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
  }, 0);
  const X = f => Math.log(f / 20) / Math.log(1000) * w, Y = db => h / 2 - db / 15 * (h / 2 - 6);
  g.strokeStyle = '#1f2a25'; g.lineWidth = 1;
  for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]){ g.beginPath(); g.moveTo(X(f), 0); g.lineTo(X(f), h); g.stroke(); }
  for (const d of [-12, -6, 0, 6, 12]){ g.beginPath(); g.moveTo(0, Y(d)); g.lineTo(w, Y(d)); g.stroke(); }
  g.fillStyle = '#3d8a62'; g.font = '9px monospace';
  [[100, '100'], [1000, '1k'], [10000, '10k']].forEach(([f, t]) => g.fillText(t, X(f) + 2, h - 3));
  const line = (list, col, wd) => { g.strokeStyle = col; g.lineWidth = wd; g.beginPath(); for (let x = 0; x <= w; x += 2){ const f = 20 * Math.pow(1000, x / w), y = Y(resp(list, f)); x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke(); };
  if (p.match > 0 && S.match.some(v => v)) line(mt, '#e2a83a', 1.2);
  line(secs.concat(p.match > 0 ? mt : []), p.mOn > .5 ? '#7dffb0' : '#3d8a62', 2);
}

/* ---------------- waveform ---------------- */
let peaks = null;
function buildPeaks(){
  if (!src) return;
  const d = src.chs[0], d2 = src.chs[1] || d, N = 2048, n = d.length, mn = new Float32Array(N), mx = new Float32Array(N);
  for (let k = 0; k < N; k++){
    const a = Math.floor(k * n / N), b = Math.max(a + 1, Math.floor((k + 1) * n / N)); let lo = 0, hi = 0;
    for (let i = a; i < b; i += Math.max(1, (b - a) >> 6)){ const v = (d[i] + d2[i]) * .5; if (v < lo) lo = v; if (v > hi) hi = v; }
    mn[k] = lo; mx[k] = hi;
  }
  peaks = {mn, mx};
}
function view(){ if (!src) return [0, 1]; const n = src.chs[0].length; return S.zoom ? [S.sel[0], S.sel[1]] : [0, n]; }
function drawWave(){
  const c = $('#waveC'), w = c.clientWidth, h = c.clientHeight; if (!w) return;
  const dpr = devicePixelRatio || 1; if (c.width !== w * dpr){ c.width = w * dpr; c.height = h * dpr; }
  const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
  $('#dropHint').style.display = src ? 'none' : 'flex';
  if (!src) return;
  const n = src.chs[0].length, [v0, v1] = view(), X = i => (i - v0) / (v1 - v0) * w;
  g.fillStyle = '#12301f'; g.fillRect(X(S.sel[0]), 0, X(S.sel[1]) - X(S.sel[0]), h);
  g.strokeStyle = '#1f2a25'; g.beginPath(); g.moveTo(0, h / 2); g.lineTo(w, h / 2); g.stroke();
  g.fillStyle = '#7dffb0';
  const d = src.chs[0], d2 = src.chs[1] || d;
  for (let x = 0; x < w; x++){
    const a = v0 + x / w * (v1 - v0), b = v0 + (x + 1) / w * (v1 - v0);
    let lo = 0, hi = 0;
    if ((v1 - v0) / w > n / 2048 && peaks){ const k0 = Math.floor(a / n * 2048), k1 = Math.max(k0 + 1, Math.floor(b / n * 2048)); for (let k = k0; k < k1 && k < 2048; k++){ lo = Math.min(lo, peaks.mn[k]); hi = Math.max(hi, peaks.mx[k]); } }
    else { for (let i = Math.floor(a); i < Math.max(Math.floor(a) + 1, Math.floor(b)); i++){ const v = ((d[i] || 0) + (d2[i] || 0)) * .5; lo = Math.min(lo, v); hi = Math.max(hi, v); } }
    const inSel = a >= S.sel[0] && a <= S.sel[1];
    g.globalAlpha = inSel ? 1 : .35;
    g.fillRect(x, h / 2 - hi * h / 2, 1, Math.max(1, (hi - lo) * h / 2));
  }
  g.globalAlpha = 1;
  g.fillStyle = '#e2a83a';
  for (const s of S.sel){ const x = X(s); g.fillRect(x - 1, 0, 2, h); g.fillRect(x - 5, 0, 10, 8); }
  if (playing){
    const f = playPos(), x = X(S.sel[0] + f * (S.sel[1] - S.sel[0]));
    g.fillStyle = '#fff'; g.fillRect(x, 0, 1.5, h);
  }
  const sr = src.sr, secs = i => (i / sr).toFixed(3) + 's';
  $('#iSel').textContent = `${secs(S.sel[0])}–${secs(S.sel[1])}`;
}
function setupWave(){
  const el = $('#wave');
  let mode = null, startX = 0, startI = 0;
  const idx = x => { const r = el.getBoundingClientRect(), [v0, v1] = view(); return Math.round(TW.clamp(v0 + (x - r.left) / r.width * (v1 - v0), 0, src.chs[0].length)); };
  const px = i => { const r = el.getBoundingClientRect(), [v0, v1] = view(); return r.left + (i - v0) / (v1 - v0) * r.width; };
  el.addEventListener('pointerdown', e => {
    if (!src) { $('#fileIn').click(); return; }
    el.setPointerCapture(e.pointerId); startX = e.clientX; startI = idx(e.clientX);
    if (Math.abs(e.clientX - px(S.sel[0])) < 14) mode = 'a';
    else if (Math.abs(e.clientX - px(S.sel[1])) < 14) mode = 'b';
    else mode = 'new';
  });
  el.addEventListener('pointermove', e => {
    if (!mode || !el.hasPointerCapture(e.pointerId)) return;
    const i = idx(e.clientX), n = src.chs[0].length, minLen = Math.round(src.sr * .005);
    if (mode === 'a') S.sel[0] = Math.min(i, S.sel[1] - minLen);
    else if (mode === 'b') S.sel[1] = Math.max(i, S.sel[0] + minLen);
    else if (Math.abs(e.clientX - startX) > 4){ S.sel = [Math.min(startI, i), Math.max(startI, i)]; if (S.sel[1] - S.sel[0] < minLen) S.sel[1] = Math.min(n, S.sel[0] + minLen); }
    drawWave();
  });
  el.addEventListener('pointerup', e => {
    if (!mode) return;
    const moved = Math.abs(e.clientX - startX) > 4; mode = null;
    if (moved){ requestRender(); }
    else if (out){ // tap: play from here
      const f = TW.clamp((idx(e.clientX) - S.sel[0]) / (S.sel[1] - S.sel[0]), 0, .999); startMain(f);
    }
  });
  addEventListener('resize', () => { drawWave(); drawEq(); });
}

/* ---------------- import ---------------- */
async function decodeFile(file){
  const ab = await file.arrayBuffer();
  // our own readers first for AIFF (Chrome can't decode it) and odd WAVs, then the browser's decoder for the rest
  let r = null;
  try { r = TWC.parseAiff(ab) || null; } catch (e) {}
  if (!r){
    try { const b = await audio().decodeAudioData(ab.slice(0)); const chs = []; for (let c = 0; c < Math.min(2, b.numberOfChannels); c++) chs.push(b.getChannelData(c).slice()); r = {chs, sr:b.sampleRate}; }
    catch (e){ try { r = TWC.parseWav(ab); } catch (e2) {} }
  }
  if (!r || !r.chs.length || !r.chs[0].length) throw new Error(`Can't read ${file.name}`);
  if (r.chs.length > 2) r.chs = r.chs.slice(0, 2);
  // keep the original file's rate when the browser resampled it is fine; the converter resamples anyway
  return r;
}
async function importFile(file){
  try {
    busy(1);
    const r = await decodeFile(file);
    pushUndo();
    src = {chs:r.chs, sr:r.sr, name:file.name};
    afterSourceChange(true);
    S.bpm = null; S.key = null; $('#iBpm').textContent = '—'; $('#iKey').textContent = '—'; $('#srcBpm').value = '';
    toast(`Loaded ${file.name} · ${(r.chs[0].length / r.sr).toFixed(2)} s · ${r.sr} Hz · ${r.chs.length === 1 ? 'mono' : 'stereo'}`);
    detect(true);
  } catch (e){ toast(e.message, 4000); }
  finally { busy(-1); }
}
function afterSourceChange(selectAll){
  if (selectAll || S.sel[1] > src.chs[0].length || S.sel[1] <= S.sel[0]) S.sel = [0, src.chs[0].length];
  $('#iName').textContent = src.name; $('#iLen').textContent = (src.chs[0].length / src.sr).toFixed(2) + 's';
  buildPeaks(); sendSource(); drawWave(); requestRender(true);
}
function pushUndo(){ if (src){ undo.push({src, sel:S.sel.slice()}); if (undo.length > 12) undo.shift(); } }

/* ---------------- edit ---------------- */
function needSrc(){ if (!src){ toast('Import a sample first'); return false; } return true; }
function truncate(){
  if (!needSrc()) return;
  const [a, b] = S.sel; if (a === 0 && b === src.chs[0].length){ toast('Select part of the sample first (drag on the waveform)'); return; }
  pushUndo(); src = {...src, chs:src.chs.map(c => c.slice(a, b))}; S.zoom = false;
  afterSourceChange(true); paintAll(); toast(`Truncated to ${((b - a) / src.sr).toFixed(3)} s`);
}
function normalize(){
  if (!needSrc()) return;
  const [a, b] = S.sel; let pk = 1e-9; for (const c of src.chs) for (let i = a; i < b; i++) pk = Math.max(pk, Math.abs(c[i]));
  pushUndo(); const g = .989 / pk; src = {...src, chs:src.chs.map(c => { const d = c.slice(); for (let i = a; i < b; i++) d[i] *= g; return d; })};
  afterSourceChange(false); toast(`Normalized (${(20 * Math.log10(g)).toFixed(1)} dB)`);
}
function reverse(){
  if (!needSrc()) return;
  const [a, b] = S.sel; pushUndo();
  src = {...src, chs:src.chs.map(c => { const d = c.slice(); d.subarray(a, b).reverse(); return d; })};
  afterSourceChange(false); toast('Reversed the selection');
}
function doUndo(){
  const u = undo.pop(); if (!u){ toast('Nothing to undo'); return; }
  src = u.src; S.sel = u.sel; afterSourceChange(false); paintAll(); toast('Undone');
}
function resample(){
  if (!needSrc() || !out) return;
  pushUndo();
  const chs = []; for (let c = 0; c < out.numberOfChannels; c++) chs.push(out.getChannelData(c).slice());
  src = {chs, sr:out.sampleRate, name:src.name.replace(/( · resampled)*$/, '') + ' · resampled'};
  // the sound is now in the sample: reset the effects so nothing is applied twice
  const keep = {}; TW.MASTER_IDS.forEach(id => keep[id] = S.p[id]);
  Object.assign(S.p, TW.defaults(), {machine:6, rate:48000, bits:16, ftype:5, mono:0, fenv:0, retrig:0, drive:0, cutoff:20000, res:0}, keep, {mOn:0});
  S.presetName = 'Resampled'; S.preset = -1;
  afterSourceChange(true); paintAll(); save();
  toast('Resampled. Effects reset to Clean and the master switched off so nothing is applied twice. Undo brings the old sample back.', 4500);
}

/* ---------------- tempo / key / warp ---------------- */
async function detect(quiet){
  if (!needSrc()) return;
  try {
    const r = await ask({type:'analyze', sel:S.sel});
    S.bpm = r.bpm; S.key = r.key;
    $('#iBpm').textContent = r.bpm ? r.bpm.toFixed(1) : 'n/a'; $('#iKey').textContent = r.key ? r.key.name : 'n/a';
    if (r.bpm) $('#srcBpm').value = r.bpm;
    if (r.key) $('#toKey').value = NOTE_KEYS.indexOf(r.key.name);
    if (!quiet) toast(r.bpm ? `${r.bpm.toFixed(1)} BPM · ${r.key ? r.key.name : ''}` : `Too short for a tempo · key ${r.key ? r.key.name : 'n/a'}`);
  } catch (e){ toast(e.message); }
}
function warp(){
  const from = parseFloat($('#srcBpm').value), to = parseFloat($('#tgtBpm').value);
  if (!(from > 0) || !(to > 0)){ toast('Set the sample BPM (Detect) and a target BPM'); return; }
  if ($('#warpMode').value === 'stretch'){ S.p.stretch = TW.clamp(from / to, .25, 4); }
  else {
    // speed: faster plays higher, like the hardware
    const c = 1200 * Math.log2(to / from), st = Math.round(c / 100);
    S.p.semis = TW.clamp(st, -24, 24); S.p.cents = TW.clamp(Math.round(c - st * 100), -100, 100); S.p.stretch = 1;
  }
  markEdited(); paintAll(); commit();
  toast(`Warped ${from} → ${to} BPM (${$('#warpMode').value === 'stretch' ? 'pitch kept' : 'pitch moves with the speed'})`);
}
function toKey(){
  if (!S.key){ toast('Detect the key first'); return; }
  const i = +$('#toKey').value, root = Math.floor(i / 2);
  let d = (root - S.key.root + 12) % 12; if (d > 6) d -= 12;
  S.p.semis = TW.clamp(d, -24, 24); markEdited(); paintAll(); commit();
  toast(`Transposed ${d >= 0 ? '+' : ''}${d} semitones: ${S.key.name} → ${NOTE_KEYS[i].replace(/ (major|minor)/, ' ' + S.key.mode)}`);
}

/* ---------------- master ---------------- */
async function loadRef(file){
  try {
    const r = await decodeFile(file);
    const d = await ask({type:'refspec', chs:r.chs, sr:r.sr, name:file.name});
    S.ref = d.bands; S.refName = file.name; $('#refName').textContent = 'Reference: ' + file.name;
    toast('Reference loaded. Tap Match EQ or Auto master.');
  } catch (e){ toast(e.message); }
}
async function matchEq(){
  if (!needSrc()) return;
  try {
    const d = await ask({type:'match', p:S.p, sel:S.sel, fs:FS(), ref:S.ref || TW.TARGET_CURVE});
    S.match = d.match; if (!S.p.match) S.p.match = .8; S.p.mOn = 1; paintAll(); commit();
    toast(S.ref ? 'EQ matched to ' + S.refName : 'EQ matched to a typical finished mix (load a reference to match a track)');
  } catch (e){ toast(e.message); }
}
async function autoMaster(){
  if (!needSrc()) return;
  try {
    const d = await ask({type:'automaster', p:S.p, sel:S.sel, fs:FS(), ref:S.ref, target:+$('#lufsTgt').value});
    Object.assign(S.p, d.params); S.match = d.match; paintAll(); commit();
    toast(`Auto master: EQ ${S.ref ? 'matched to the reference' : 'balanced'}, compressor at ${d.params.cThr} dB, gain ${d.params.mGain > 0 ? '+' : ''}${d.params.mGain} dB, ceiling ${d.params.lim} dB`);
  } catch (e){ toast(e.message); }
}
function flatMaster(){ const d = TW.defaults(); TW.MASTER_IDS.forEach(id => S.p[id] = d[id]); S.match = new Array(10).fill(0); paintAll(); commit(); toast('Master reset'); }

/* ---------------- presets ---------------- */
function applyValues(vals, name, idx){
  Object.assign(S.p, vals); S.presetName = name; S.preset = idx; paintAll(); commit();
}
function loadPreset(i){
  const n = PRESETS.length; i = (i + n) % n; const p = PRESETS[i];
  applyValues(p.values, p.name, i);
}
function stepPreset(d){
  if (S.preset >= 0) loadPreset(S.preset + d);
  else loadPreset(d > 0 ? 0 : PRESETS.length - 1);
}
function randomize(){
  const r = TW.randomPatch((Math.random() * 2 ** 32) >>> 0);
  applyValues(r.values, r.name, -1); toast('Random: ' + r.name.replace('Random · ', '') + ' family, everything nudged');
}
function savePreset(){
  const name = prompt('Name this preset', S.presetName.replace(/\*$/, '') + ' (mine)'); if (!name) return;
  const values = {}; TW.SOUND_IDS.forEach(id => values[id] = S.p[id]);
  S.user = S.user.filter(u => u.name !== name); S.user.push({name, values, family:'My presets'});
  S.presetName = name; S.preset = -1; paintAll(); save(); toast('Saved "' + name + '"');
}
let famSel = 'All';
function openPresets(){
  const fams = ['All', ...(S.user.length ? ['My presets'] : []), ...FAMS], box = $('#pFams');
  box.innerHTML = '';
  for (const f of fams){ const c = document.createElement('button'); c.className = 'chip' + (f === famSel ? ' on' : ''); c.textContent = f; c.onclick = () => { famSel = f; openPresets(); }; box.appendChild(c); }
  listPresets(); $('#presetDlg').open || $('#presetDlg').showModal();
}
function listPresets(){
  const q = $('#pSearch').value.trim().toLowerCase(), list = $('#pList'); list.innerHTML = '';
  const all = [...S.user.map(u => ({...u, user:true})), ...PRESETS.map((p, i) => ({...p, i}))];
  const hits = all.filter(p => (famSel === 'All' || p.family === famSel) && (!q || p.name.toLowerCase().includes(q)));
  $('#pCount').textContent = hits.length + ' shown';
  for (const p of hits.slice(0, 600)){
    const b = document.createElement('button'); b.textContent = p.name; if (p.i === S.preset) b.className = 'cur';
    b.onclick = () => { if (p.user) applyValues(p.values, p.name, -1); else loadPreset(p.i); listPresets(); };
    list.appendChild(b);
  }
}

/* ---------------- export ---------------- */
let recorder = null, recorderCancelled = false;
function deviceTypes(){
  if (!window.MediaRecorder) return [];
  return [['audio/ogg;codecs=opus', 'OGG Opus', 'ogg'], ['audio/webm;codecs=opus', 'WebM Opus', 'webm'], ['audio/mp4;codecs=mp4a.40.2', 'M4A AAC', 'm4a'], ['audio/mp4', 'M4A', 'm4a'], ['audio/mp4;codecs=opus', 'MP4 Opus', 'mp4']]
    .filter(([t]) => MediaRecorder.isTypeSupported(t));
}
function buildFormats(){
  const s = $('#fmt');
  [['wav:16', 'WAV 16-bit'], ['wav:24', 'WAV 24-bit'], ['wav:f32', 'WAV 32-bit float'], ['aiff:16', 'AIFF 16-bit'], ['aiff:24', 'AIFF 24-bit'], ['flac:16', 'FLAC 16-bit'], ['flac:24', 'FLAC 24-bit'], ['mp3:320', 'MP3 320 kbps'], ['mp3:192', 'MP3 192 kbps'], ['mp3:128', 'MP3 128 kbps']]
    .forEach(([v, t]) => s.add(new Option(t, v)));
  const seen = new Set();
  deviceTypes().forEach(([t, name, ext]) => { if (seen.has(name)) return; seen.add(name); s.add(new Option(name + ' (device codec, real time)', 'dev:' + t + '|' + ext)); });
}
let lastFile = null;
async function doExport(){
  if (!needSrc()) return;
  const fmt = $('#fmt').value, fs = +$('#expSr').value, sel = $('#expRange').value === 'all' ? [0, src.chs[0].length] : S.sel;
  const base = src.name.replace(/\.[^.]+$/, '').replace(/( · resampled)+/, '-resampled') + ' - ' + S.presetName.replace(/\*$/, '').replace(/[\\/:*?"<>|]/g, '');
  try {
    let blob, name;
    if (fmt.startsWith('dev:')){
      const [type, ext] = fmt.slice(4).split('|');
      const r = await ask({type:'export', p:S.p, match:S.match, sel, fs:FS(), fmt:'wav:f32'});
      const pcm = TWC.parseWav(r.bytes.buffer);
      blob = await recordDevice(pcm.chs, pcm.sr, type); if (!blob) return; name = base + '.' + ext;
    } else {
      if (fmt.startsWith('mp3') && ![32000, 44100, 48000].includes(fs)){ toast('MP3 needs 32, 44.1 or 48 kHz'); return; }
      toast('Exporting…', 20000);
      const r = await ask({type:'export', p:S.p, match:S.match, sel, fs, fmt});
      blob = new Blob([r.bytes], {type:r.mime}); name = base + '.' + r.ext;
    }
    lastFile = new File([blob], name, {type:blob.type});
    const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
    $('#shareBtn').hidden = !(navigator.canShare && navigator.canShare({files:[lastFile]}));
    toast(`Saved ${name} (${(blob.size / 1048576).toFixed(2)} MB)`);
  } catch (e){ toast('Export failed: ' + e.message, 4000); }
}
function recordDevice(chs, sr, type){
  return new Promise((resolve, reject) => {
    const c = audio(), dest = c.createMediaStreamDestination(), b = c.createBuffer(chs.length, chs[0].length, sr);
    chs.forEach((d, i) => b.copyToChannel(d, i));
    const s = c.createBufferSource(); s.buffer = b; s.connect(dest);
    const parts = []; recorderCancelled = false;
    recorder = new MediaRecorder(dest.stream, {mimeType:type, audioBitsPerSecond:256000});
    recorder.ondataavailable = e => e.data.size && parts.push(e.data);
    recorder.onstop = () => { s.disconnect(); if (recorderCancelled){ toast('Export stopped'); resolve(null); } else resolve(new Blob(parts, {type:type.split(';')[0]})); };
    recorder.onerror = e => reject(e.error || new Error('Recorder failed'));
    s.onended = () => setTimeout(() => recorder.state !== 'inactive' && recorder.stop(), 250);
    recorder.start(); s.start(c.currentTime + .05);
    toast(`Recording ${b.duration.toFixed(1)} s with the device codec… (STOP ALL cancels)`, b.duration * 1000 + 1500);
  });
}

/* ---------------- wiring ---------------- */
function wire(){
  $('#importBtn').onclick = () => $('#fileIn').click();
  $('#fileIn').onchange = e => { const f = e.target.files[0]; if (f) importFile(f); e.target.value = ''; };
  $('#refBtn').onclick = () => $('#refIn').click();
  $('#refIn').onchange = e => { const f = e.target.files[0]; if (f) loadRef(f); e.target.value = ''; };
  $('#playBtn').onclick = () => { if (!needSrc()) return; if (playing) stopMain(); else if (out || S.bypass) startMain(0); else toast('Still rendering…'); };
  $('#loopBtn').onclick = () => { S.loop = !S.loop; if (mainSrc) mainSrc.loop = S.loop; paintAll(); save(); };
  $('#stopBtn').onclick = stopAll;
  $('#bypassBtn').onclick = () => { S.bypass = !S.bypass; paintAll(); if (playing) startMain(playPos()); };
  $('#allBtn').onclick = () => { if (!needSrc()) return; S.sel = [0, src.chs[0].length]; S.zoom = false; paintAll(); drawWave(); requestRender(); };
  $('#zoomBtn').onclick = () => { S.zoom = !S.zoom; paintAll(); drawWave(); };
  $('#truncBtn').onclick = truncate; $('#normBtn').onclick = normalize; $('#revBtn').onclick = reverse; $('#undoBtn').onclick = doUndo; $('#resampleBtn').onclick = resample;
  $('#detectBtn').onclick = () => detect(false);
  $('#halfBtn').onclick = () => { const v = parseFloat($('#srcBpm').value); if (v) $('#srcBpm').value = (v / 2).toFixed(1); };
  $('#dblBtn').onclick = () => { const v = parseFloat($('#srcBpm').value); if (v) $('#srcBpm').value = (v * 2).toFixed(1); };
  $('#tgtBpm').onchange = save;
  $('#warpBtn').onclick = warp; $('#toKeyBtn').onclick = toKey;
  $('#mOnBtn').onclick = () => { S.p.mOn = S.p.mOn > .5 ? 0 : 1; paintAll(); commit(); };
  $('#matchBtn').onclick = matchEq; $('#clearMatchBtn').onclick = () => { S.match = new Array(10).fill(0); S.p.match = 0; paintAll(); commit(); };
  $('#autoBtn').onclick = autoMaster; $('#flatBtn').onclick = flatMaster;
  $('#pPrev').onclick = () => stepPreset(-1); $('#pNext').onclick = () => stepPreset(1); $('#pRand').onclick = randomize; $('#pSave').onclick = savePreset;
  $('#pName').onclick = openPresets; $('#pClose').onclick = () => $('#presetDlg').close(); $('#pSearch').oninput = listPresets;
  $('#helpBtn').onclick = () => $('#helpDlg').showModal(); $('#helpClose').onclick = () => $('#helpDlg').close();
  $('#exportBtn').onclick = doExport; $('#fmt').onchange = save;
  $('#shareBtn').onclick = () => lastFile && navigator.share({files:[lastFile], title:lastFile.name}).catch(() => {});
  // drag and drop
  addEventListener('dragover', e => e.preventDefault());
  addEventListener('drop', e => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) importFile(f); });
  // keys: space play/stop, Escape stops everything
  addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    if (e.key === 'Escape'){ stopAll(); }
    else if (e.key === ' '){ e.preventDefault(); $('#playBtn').click(); }
  });
  addEventListener('pointerdown', () => audio(), {once:true});
}

buildControls(); buildPads(); buildFormats(); load(); wire(); setupWave(); paintAll(); drawWave();   // MIDI and TV: io.js
if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
