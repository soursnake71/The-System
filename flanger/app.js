// MXR Flanger: the faceplate photo is the panel. Knobs are discs cut from the photo and rotated; the footswitch
// toggles bypass. Sound comes from the M-117 circuit model (engine.js) in an AudioWorklet (worklet.js).
(() => {
'use strict';
const M = window.M117;
const $ = id => document.getElementById(id);
const store = {
  get(k, d){ try { const v = localStorage.getItem('mxrfl.' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v){ try { localStorage.setItem('mxrfl.' + k, JSON.stringify(v)); } catch (e) {} },
};

/* ---------------- state ---------------- */
const state = M.defaults();
Object.assign(state, store.get('state', {}));
for (const q of M.PARAMS) if (typeof state[q.id] !== 'number' || !isFinite(state[q.id])) state[q.id] = q.def;

// knob geometry measured on faceplate.jpg (480x370): centre, radius, and where the photo's pointer points
// (clockwise degrees from 12 o'clock). A pot turns from -150 (0) to +150 (10).
const KNOBS = [
  {id:'manual', x:74, y:90, photo:-155},
  {id:'width', x:182, y:92, photo:-18},
  {id:'speed', x:284, y:94, photo:-127},
  {id:'regen', x:392, y:90, photo:77},
];
const R = 40, FSW = {x:234, y:262, r:30}, LED = {x:234, y:214, r:5};

const plate = $('plate'), tip = $('tip'), led = $('led'), fsw = $('fsw');
for (const k of KNOBS){
  const d = document.createElement('div');
  d.className = 'knob'; d.setAttribute('role', 'slider'); d.setAttribute('aria-label', k.id); d.tabIndex = 0;
  plate.insertBefore(d, tip); k.el = d;
}

function layout(){
  const s = plate.clientWidth / 480;
  for (const k of KNOBS){
    const st = k.el.style;
    st.left = (k.x - R) * s + 'px'; st.top = (k.y - R) * s + 'px'; st.width = st.height = 2 * R * s + 'px';
    st.backgroundSize = 480 * s + 'px ' + 370 * s + 'px';
    st.backgroundPosition = -(k.x - R) * s + 'px ' + -(k.y - R) * s + 'px';
  }
  Object.assign(fsw.style, {left:(FSW.x - FSW.r) * s + 'px', top:(FSW.y - FSW.r) * s + 'px', width:2 * FSW.r * s + 'px', height:2 * FSW.r * s + 'px'});
  Object.assign(led.style, {left:(LED.x - LED.r) * s + 'px', top:(LED.y - LED.r) * s + 'px', width:2 * LED.r * s + 'px', height:2 * LED.r * s + 'px'});
}
new ResizeObserver(layout).observe(plate);
layout();

const angleOf = v => -150 + 300 * v / 10;
function drawKnob(k){
  const v = state[k.id];
  k.el.style.transform = `rotate(${angleOf(v) - k.photo}deg)`;
  k.el.setAttribute('aria-valuenow', v.toFixed(1));
}
function showTip(k){
  const s = plate.clientWidth / 480;
  tip.style.left = k.x * s + 'px'; tip.style.top = (k.y - R - 4) * s + 'px';
  tip.textContent = k.id.toUpperCase() + ' ' + state[k.id].toFixed(1);
  tip.style.display = 'block';
}
const hideTip = () => { tip.style.display = 'none'; };

/* ---------------- knob touch ---------------- */
for (const k of KNOBS){
  let startY = 0, startV = 0, lastDown = 0, id = null;
  k.el.addEventListener('pointerdown', e => {
    e.preventDefault();
    const now = performance.now();
    if (now - lastDown < 300){ setParam(k.id, M.PARAMS[M.P[k.id]].def); showTip(k); lastDown = 0; return; }
    lastDown = now;
    id = e.pointerId; k.el.setPointerCapture(id); startY = e.clientY; startV = state[k.id];
    showTip(k);
  });
  k.el.addEventListener('pointermove', e => {
    if (e.pointerId !== id) return;
    const px = Math.max(120, plate.clientWidth * .45);   // a full turn over ~half the panel height of drag
    setParam(k.id, Math.round(Math.min(10, Math.max(0, startV + (startY - e.clientY) / px * 10)) * 10) / 10);
    showTip(k);
  });
  const up = e => { if (e.pointerId === id){ id = null; setTimeout(hideTip, 500); } };
  k.el.addEventListener('pointerup', up); k.el.addEventListener('pointercancel', up);
  k.el.addEventListener('wheel', e => { e.preventDefault(); setParam(k.id, Math.min(10, Math.max(0, Math.round((state[k.id] - Math.sign(e.deltaY) * .2) * 10) / 10))); showTip(k); clearTimeout(k.t); k.t = setTimeout(hideTip, 700); }, {passive:false});
  k.el.addEventListener('keydown', e => {
    const d = {ArrowUp:.1, ArrowRight:.1, ArrowDown:-.1, ArrowLeft:-.1, PageUp:1, PageDown:-1}[e.key];
    if (d){ e.preventDefault(); setParam(k.id, Math.min(10, Math.max(0, Math.round((state[k.id] + d) * 10) / 10))); showTip(k); clearTimeout(k.t); k.t = setTimeout(hideTip, 700); }
  });
}
fsw.addEventListener('click', () => setParam('bypass', state.bypass >= .5 ? 0 : 1));

/* ---------------- sliders ---------------- */
const SLIDERS = {
  mix: v => Math.round(v) + '%',
  spread: v => Math.round(v) + '°',
  level: v => (v > 0 ? '+' : '') + (+v).toFixed(1) + ' dB',
};
for (const id of Object.keys(SLIDERS)) $(id).addEventListener('input', e => setParam(id, +e.target.value));

/* ---------------- params into the UI and the engine ---------------- */
let node = null;
function setParam(id, v, quiet){
  state[id] = v;
  const k = KNOBS.find(x => x.id === id);
  if (k) drawKnob(k);
  if (id === 'bypass') led.classList.toggle('on', v < .5);
  if (SLIDERS[id]){ $(id).value = v; $(id + 'v').textContent = SLIDERS[id](v); }
  if (node) node.port.postMessage({set:{[id]: v}});
  if (!quiet){ store.set('state', state); markEdited(); }
}
function applyAll(values){
  for (const q of M.PARAMS) if (q.id !== 'bypass') setParam(q.id, values[q.id] ?? q.def, true);
  setParam('bypass', state.bypass, true);
  store.set('state', state);
}

/* ---------------- presets ---------------- */
let factory = [], user = store.get('user', []), current = store.get('preset', 0), edited = false;
const all = () => factory.concat(user.map(u => Object.assign({user:true}, u)));
function fillPresets(){
  const sel = $('preset'); sel.innerHTML = '';
  const g1 = document.createElement('optgroup'); g1.label = 'Factory';
  const g2 = document.createElement('optgroup'); g2.label = 'Yours';
  all().forEach((p, i) => { const o = new Option(p.name, i); (p.user ? g2 : g1).appendChild(o); });
  sel.appendChild(g1); if (user.length) sel.appendChild(g2);
  sel.value = current;
}
function loadPreset(i){
  const list = all(); if (!list.length) return;
  current = (i + list.length) % list.length;
  const p = list[current];
  applyAll(Object.assign(M.defaults(), p.values));
  $('preset').value = current; edited = false;
  $('desc').textContent = p.desc || (p.user ? 'Your preset.' : '');
  $('del').disabled = !p.user;
  store.set('preset', current);
}
function markEdited(){
  if (edited) return; edited = true;
  const p = all()[current]; if (p) $('desc').textContent = (p.desc || '') + ' (edited)';
}
$('preset').addEventListener('change', e => loadPreset(+e.target.value));
$('prev').addEventListener('click', () => loadPreset(current - 1));
$('next').addEventListener('click', () => loadPreset(current + 1));
$('save').addEventListener('click', () => {
  const name = (prompt('Name for this preset:', 'My flange') || '').trim(); if (!name) return;
  const values = {}; for (const q of M.PARAMS) if (q.id !== 'bypass') values[q.id] = state[q.id];
  const at = user.findIndex(u => u.name === name);
  if (at >= 0) user[at] = {name, values}; else user.push({name, values});
  store.set('user', user); fillPresets();
  loadPreset(factory.length + (at >= 0 ? at : user.length - 1));
});
$('del').addEventListener('click', () => {
  const p = all()[current]; if (!p || !p.user || !confirm('Delete "' + p.name + '"?')) return;
  user.splice(current - factory.length, 1); store.set('user', user); current = 0; fillPresets(); loadPreset(0);
});

/* ---------------- audio ---------------- */
let ctx = null, srcNode = null, stream = null, analyser = null, running = false, fileBuf = null, demoBuf = null;
const status = t => { $('status').textContent = t; };

async function ensureCtx(){
  if (ctx) return;
  ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
  await ctx.audioWorklet.addModule('engine.js');
  await ctx.audioWorklet.addModule('worklet.js');
  node = new AudioWorkletNode(ctx, 'm117', {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2],
    channelCount:2, channelCountMode:'explicit', channelInterpretation:'speakers'});
  node.port.postMessage({set:Object.assign({}, state)});
  analyser = ctx.createAnalyser(); analyser.fftSize = 1024;
  node.connect(analyser); analyser.connect(ctx.destination);
  window.__flanger = {ctx, node, analyser};
}

function stopSource(){
  if (srcNode){ try { srcNode.stop && srcNode.stop(); } catch (e) {} srcNode.disconnect(); srcNode = null; }
  if (stream){ stream.getTracks().forEach(t => t.stop()); stream = null; }
}

async function start(){
  await ensureCtx();
  await ctx.resume();
  stopSource();
  const kind = $('source').value;
  if (kind === 'live'){
    const dev = $('device').value;
    stream = await navigator.mediaDevices.getUserMedia({audio:{deviceId: dev ? {exact:dev} : undefined, echoCancellation:false, noiseSuppression:false, autoGainControl:false, channelCount:{ideal:2}}});
    srcNode = ctx.createMediaStreamSource(stream);
    listDevices();
    status('Live input. Use headphones to avoid feedback.');
  } else {
    const buf = kind === 'file' ? fileBuf : (demoBuf || (demoBuf = makeDemo(ctx.sampleRate)));
    if (!buf){ status('Pick an audio file first.'); return false; }
    srcNode = ctx.createBufferSource(); srcNode.buffer = buf; srcNode.loop = true; srcNode.start();
    status(kind === 'file' ? 'Playing ' + ($('file').files[0]?.name || 'file') : 'Demo loop');
  }
  srcNode.connect(node);
  return true;
}

$('start').addEventListener('click', async () => {
  try {
    if (running){ stopSource(); running = false; }
    else running = !!(await start());
  } catch (e){ status('Audio error: ' + e.message); running = false; }
  $('start').textContent = running ? 'Stop' : 'Start';
  $('start').classList.toggle('go', !running);
});
$('source').addEventListener('change', async () => {
  const v = $('source').value;
  $('fileRow').classList.toggle('hide', v !== 'file');
  $('liveRow').classList.toggle('hide', v !== 'live');
  store.set('source', v);
  if (running) try { await start(); } catch (e){ status('Audio error: ' + e.message); }
});
$('device').addEventListener('change', async () => { if (running && $('source').value === 'live') try { await start(); } catch (e){ status('Audio error: ' + e.message); } });
$('file').addEventListener('change', async e => {
  const f = e.target.files[0]; if (!f) return;
  status('Loading ' + f.name + '…');
  try {
    await ensureCtx();
    fileBuf = await ctx.decodeAudioData(await f.arrayBuffer());
    status('Loaded ' + f.name + ' (' + fileBuf.duration.toFixed(1) + ' s)');
    if (running) await start();
  } catch (err){ status('Could not read that file: ' + err.message); }
});

async function listDevices(){
  if (!navigator.mediaDevices?.enumerateDevices) return;
  const sel = $('device'), keep = sel.value;
  const devs = (await navigator.mediaDevices.enumerateDevices()).filter(d => d.kind === 'audioinput');
  sel.innerHTML = '<option value="">Default input</option>';
  devs.forEach((d, i) => { if (d.deviceId && d.deviceId !== 'default') sel.appendChild(new Option(d.label || 'Input ' + (i + 1), d.deviceId)); });
  sel.value = keep;
}

/* ---------------- meter ---------------- */
const mc = $('meter'), mg = mc.getContext('2d');
let peakHold = 0, tbuf = null;
function meter(){
  requestAnimationFrame(meter);
  let pk = 0;
  if (analyser && running){
    tbuf = tbuf || new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(tbuf);
    for (let i = 0; i < tbuf.length; i++) pk = Math.max(pk, Math.abs(tbuf[i]));
  }
  window.__meter = pk;
  peakHold = Math.max(pk, peakHold * .96);
  const db = v => Math.max(0, Math.min(1, (20 * Math.log10(v + 1e-9) + 48) / 48));
  const w = mc.width, h = mc.height;
  mg.fillStyle = '#0b0b0b'; mg.fillRect(0, 0, w, h);
  const x = db(peakHold) * w;
  const gr = mg.createLinearGradient(0, 0, w, 0); gr.addColorStop(0, '#6e8f4e'); gr.addColorStop(.75, '#d8c46a'); gr.addColorStop(1, '#e2452f');
  mg.fillStyle = gr; mg.fillRect(0, 0, x, h);
}
requestAnimationFrame(meter);

/* ---------------- demo loop: strummed guitar chords and a drum machine, synthesized ---------------- */
function makeDemo(sr){
  const bpm = 96, beat = 60 / bpm, bars = 2, len = Math.round(sr * beat * 4 * bars);
  const L = new Float32Array(len), Rr = new Float32Array(len);
  let seed = 7; const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296 * 2 - 1;
  const mtof = m => 440 * Math.pow(2, (m - 69) / 12);
  // Karplus-Strong string
  function pluck(t0, midi, amp, dur, pan){
    const f = mtof(midi), n = Math.max(2, Math.round(sr / f)), buf = new Float32Array(n);
    for (let i = 0; i < n; i++) buf[i] = rnd() * amp;
    let idx = 0, last = 0;
    const s0 = Math.round(t0 * sr), s1 = Math.min(len, s0 + Math.round(dur * sr));
    for (let s = s0; s < s1; s++){
      const a = buf[idx], b = buf[(idx + 1) % n], y = .4985 * (a + b) + .002 * last; last = y; buf[idx] = y; idx = (idx + 1) % n;
      const fade = s1 - s < 400 ? (s1 - s) / 400 : 1;
      const k = (s % len); L[k] += a * fade * (1 - pan); Rr[k] += a * fade * pan;
    }
  }
  const chords = [[40, 47, 52, 56, 59, 64], [45, 52, 57, 61, 64], [42, 49, 54, 57, 61, 66], [47, 54, 59, 63, 66]];   // E, A, F#m, B
  const strum = [0, 1.5, 2, 3, 3.5];
  for (let bar = 0; bar < bars; bar++) for (let half = 0; half < 2; half++){
    const ch = chords[bar * 2 + half];
    for (const st of strum){
      const t = (bar * 4 + half * 2 + st * .5) * beat, down = (st * 2) % 2 === 0;
      const notes = down ? ch : ch.slice().reverse();
      notes.forEach((m, i) => pluck(t + i * .011, m, down ? .16 : .1, beat * (st === 3.5 || st === 1.5 ? 1.1 : .55), .35 + i * .05));
    }
  }
  const add = (t, fn, dur) => { const s0 = Math.round(t * sr), n = Math.round(dur * sr); for (let i = 0; i < n && s0 + i < len; i++){ const v = fn(i / sr); L[s0 + i] += v; Rr[s0 + i] += v; } };
  const kick = t => Math.sin(2 * Math.PI * (45 * t + 90 * (1 - Math.exp(-t * 30)) / 30)) * Math.exp(-t * 7) * .7;
  const snare = t => (rnd() * .5 * Math.exp(-t * 18) + Math.sin(2 * Math.PI * 190 * t) * .3 * Math.exp(-t * 25)) * .8;
  let hp = 0, hl = 0;
  const hat = t => { const x = rnd(); hp = x - hl; hl = x; return hp * .22 * Math.exp(-t * 60); };
  for (let b = 0; b < bars * 4; b++){
    const t = b * beat;
    if (b % 4 === 0 || b % 4 === 2) add(t, kick, .4);
    if (b % 4 === 2) add(t + beat * .75, kick, .3);
    if (b % 2 === 1) add(t, snare, .3);
    for (let e = 0; e < 2; e++) add(t + e * beat / 2, hat, .08);
  }
  let pk = 0; for (let i = 0; i < len; i++) pk = Math.max(pk, Math.abs(L[i]), Math.abs(Rr[i]));
  const g = .8 / (pk || 1);
  const buf = new AudioBuffer({numberOfChannels:2, length:len, sampleRate:sr});
  buf.copyToChannel(L.map(v => v * g), 0); buf.copyToChannel(Rr.map(v => v * g), 1);
  return buf;
}

/* ---------------- WAV export: the file (or 4 passes of the demo) through a fresh engine ---------------- */
function wav(L, Rr, sr){
  const n = L.length, b = new ArrayBuffer(44 + n * 4), d = new DataView(b);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) d.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); d.setUint32(4, 36 + n * 4, true); str(8, 'WAVE'); str(12, 'fmt '); d.setUint32(16, 16, true);
  d.setUint16(20, 1, true); d.setUint16(22, 2, true); d.setUint32(24, sr, true); d.setUint32(28, sr * 4, true); d.setUint16(32, 4, true); d.setUint16(34, 16, true);
  str(36, 'data'); d.setUint32(40, n * 4, true);
  for (let i = 0, o = 44; i < n; i++, o += 4){
    d.setInt16(o, Math.max(-1, Math.min(1, L[i])) * 32767, true);
    d.setInt16(o + 2, Math.max(-1, Math.min(1, Rr[i])) * 32767, true);
  }
  return new Uint8Array(b);
}
async function saveBytes(bytes, name, mime){
  if (window.AndroidFlanger && window.AndroidFlanger.save){
    let s = ''; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    const r = window.AndroidFlanger.save(btoa(s), name, mime);
    status(String(r).startsWith('error') ? 'Save failed: ' + r : 'Saved to ' + r);
    return;
  }
  const url = URL.createObjectURL(new Blob([bytes], {type:mime}));
  const a = document.createElement('a'); a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
  status('Exported ' + name);
}
$('export').addEventListener('click', async () => {
  try {
    await ensureCtx();
    const useFile = $('source').value === 'file' && fileBuf;
    const src = useFile ? fileBuf : (demoBuf || (demoBuf = makeDemo(ctx.sampleRate)));
    const reps = useFile ? 1 : 4, sr = src.sampleRate, n = src.length * reps;
    const inL = new Float32Array(n), inR = new Float32Array(n);
    const c0 = src.getChannelData(0), c1 = src.numberOfChannels > 1 ? src.getChannelData(1) : c0;
    for (let r = 0; r < reps; r++){ inL.set(c0, r * src.length); inR.set(c1, r * src.length); }
    const e = new M.Engine(); for (const q of M.PARAMS) e.set(q.id, state[q.id]); e.prepare(sr);
    const oL = new Float32Array(n), oR = new Float32Array(n), step = sr;
    for (let pos = 0; pos < n; pos += step){
      e.process(inL, inR, oL, oR, Math.min(step, n - pos), pos);
      status('Rendering ' + Math.round(pos / n * 100) + '%');
      await new Promise(r => setTimeout(r, 0));
    }
    const base = useFile ? ($('file').files[0]?.name || 'audio').replace(/\.[^.]+$/, '') : 'demo';
    const pname = (all()[current]?.name || 'flanger').replace(/[^\w\- ]+/g, '').trim();
    await saveBytes(wav(oL, oR, sr), `${base} - MXR Flanger - ${pname}.wav`, 'audio/wav');
  } catch (err){ status('Export failed: ' + err.message); }
});

/* ---------------- boot ---------------- */
for (const k of KNOBS) drawKnob(k);
for (const id of Object.keys(SLIDERS)) setParam(id, state[id], true);
setParam('bypass', state.bypass, true);
$('source').value = store.get('source', 'demo'); $('source').dispatchEvent(new Event('change'));
fetch('presets.json').then(r => r.json()).then(list => {
  factory = list; if (current >= all().length) current = 0;
  fillPresets();
  const p = all()[current];
  $('desc').textContent = p ? p.desc || '' : ''; $('del').disabled = !(p && p.user);
  if (!store.get('state', null)) loadPreset(current);
}).catch(e => status('Presets did not load: ' + e.message));
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidFlanger) navigator.serviceWorker.register('sw.js').catch(() => {});
})();
