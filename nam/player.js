// player.js — audition rigs: a NAM model (+ cab) heard on a riff, your own clip or live guitar.
import { namSizes } from './engine.js';
import { makeRiff, encodeWav, makeZip, simpleCab } from './audio.js';

const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const db = x => 10 ** (x / 20);
const SR = 48000;
const TARGET = -18; // NAM players even out loudness to this level

const rigs = [];
let cur = null;          // selected rig
let ctx = null, master, outGain, cabNode = null, simple = null, convolver = null;
let ir = null;           // { name, bytes, buffer }
let srcKind = 'riff', srcData = null, srcName = 'riff', fileData = null, fileName = '';
let playing = null;      // { node, gain, buffer, start, offset }
let bypass = false;
let live = null;         // { stream, src, node }
const cache = new Map(); // render key -> { buffer, loudness }
let worker = null, job = 0;
let lastRendered = null;

function status(msg, err = false) { const el = $('playStatus'); el.textContent = msg; el.classList.toggle('err', err); }
function sanitize(name) { return String(name).replace(/\.nam$/i, '').replace(/[^\w\- .()]+/g, '_').trim() || 'rig'; }
function save(blob, name) { const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 3000); }

// ─── rigs ─────────────────────────────────────────────────────────────────────

export function addRig(name, text, source = 'local file') {
  let json, sizes;
  try { json = JSON.parse(text); sizes = namSizes(json); }
  catch (e) { status(`${name}: ${e.message}`, true); return; }
  const existing = rigs.find(r => r.name === name && r.source === source);
  if (existing) { select(existing); return existing; }
  const rig = { id: Math.random().toString(36).slice(2), name, text, json, sizes, sizeIdx: 0, source };
  rigs.push(rig);
  select(rig);
  document.getElementById('audition').scrollIntoView({ behavior: 'smooth', block: 'start' });
  return rig;
}

function select(rig) {
  cur = rig;
  renderRigs();
  ['playBtn', 'bypassBtn', 'dlNam', 'dlRig'].forEach(id => { $(id).disabled = false; });
  if (playing || live) refresh();
}

function renderRigs() {
  const list = $('rigList');
  if (!rigs.length) { list.innerHTML = '<div class="note">No rigs loaded yet.</div>'; return; }
  list.innerHTML = rigs.map(r => `<div class="rig ${r === cur ? 'on' : ''}" data-id="${r.id}">
    <span class="nm">${esc(r.name)}<small>${esc(r.source)}</small></span>
    ${r.sizes.length > 1 ? `<span class="seg">${r.sizes.map((s, i) => `<button data-size="${i}" class="${i === r.sizeIdx ? 'on' : ''}">${esc(s.label)}</button>`).join('')}</span>` : ''}
    <button class="small" data-remove="1" title="Remove">✕</button>
  </div>`).join('');
}

// ─── audio graph ──────────────────────────────────────────────────────────────

function ensureCtx() {
  if (ctx) return ctx;
  try { ctx = new AudioContext({ sampleRate: SR, latencyHint: 'interactive' }); }
  catch { ctx = new AudioContext({ latencyHint: 'interactive' }); }
  master = ctx.createGain(); master.connect(ctx.destination);
  outGain = ctx.createGain();
  setVolume(); routeCab();
  return ctx;
}
function setVolume() { if (master) master.gain.setTargetAtTime(db(Number($('vol').value)), ctx.currentTime, 0.02); }
function routeCab() {
  if (!ctx) return;
  outGain.disconnect();
  if (cabNode && cabNode.output) cabNode.output.disconnect();
  const mode = bypass ? 'off' : $('cab').value;
  if (mode === 'simple') { simple ||= simpleCab(ctx); cabNode = simple; }
  else if (mode === 'ir' && ir) {
    if (!convolver) { convolver = ctx.createConvolver(); }
    if (convolver.buffer !== ir.buffer) convolver.buffer = ir.buffer;
    cabNode = { input: convolver, output: convolver };
  } else cabNode = null;
  if (cabNode) { outGain.connect(cabNode.input); cabNode.output.connect(master); }
  else outGain.connect(master);
}
function levelFor(loudness) {
  return $('norm').checked && loudness != null && !bypass ? db(TARGET - loudness) : 1;
}

// ─── sources ──────────────────────────────────────────────────────────────────

async function sourceData() {
  if (srcKind === 'riff') { srcData ||= makeRiff(SR); srcName = 'riff'; return srcData; }
  if (srcKind === 'file' && fileData) { srcName = fileName; return fileData; }
  return null;
}
async function decodeFile(file) {
  ensureCtx();
  const buf = await ctx.decodeAudioData(await file.arrayBuffer());
  // mono, at 48 kHz, up to 30 s
  const len = Math.min(buf.length, buf.sampleRate * 30);
  const off = new OfflineAudioContext(1, Math.round(len * SR / buf.sampleRate), SR);
  const s = off.createBufferSource(); s.buffer = buf; s.connect(off.destination); s.start();
  const r = await off.startRendering();
  return r.getChannelData(0).slice();
}
async function resample(data, from, to) {
  if (from === to) return data;
  const b = new AudioBuffer({ length: data.length, numberOfChannels: 1, sampleRate: from });
  b.copyToChannel(data, 0);
  const off = new OfflineAudioContext(1, Math.round(data.length * to / from), to);
  const s = off.createBufferSource(); s.buffer = b; s.connect(off.destination); s.start();
  return (await off.startRendering()).getChannelData(0).slice();
}

// ─── rendering ────────────────────────────────────────────────────────────────

function render(rig, sizeIdx, input) {
  const net = rig.sizes[sizeIdx].json;
  const gain = db(Number($('inGain').value));
  const key = `${rig.id}|${sizeIdx}|${$('inGain').value}|${srcName}`;
  if (cache.has(key)) return Promise.resolve(cache.get(key));
  if (worker) worker.terminate();
  worker = new Worker('render.js', { type: 'module' });
  const id = ++job;
  return new Promise(async (resolve, reject) => {
    const msr = net.sample_rate || SR;
    const inp = await resample(input, SR, msr);
    worker.onmessage = e => {
      const d = e.data; if (d.id !== id) return;
      if (d.progress != null) status(`Rendering ${rig.sizes.length > 1 ? rig.sizes[sizeIdx].label + ' ' : ''}… ${Math.round(d.progress * 100)}%`);
      else if (d.error) reject(new Error(d.error));
      else if (d.done) {
        const buffer = ctx.createBuffer(1, d.output.length, d.sampleRate);
        buffer.copyToChannel(d.output, 0);
        const r = { buffer, loudness: d.loudness };
        if (cache.size > 16) cache.delete(cache.keys().next().value);
        cache.set(key, r); resolve(r);
      }
    };
    worker.onerror = e => reject(new Error(e.message || 'Render failed'));
    worker.postMessage({ id, json: net, input: inp, gain });
  });
}

function position() {
  if (!playing) return 0;
  const d = playing.buffer.duration;
  return ((ctx.currentTime - playing.start) + playing.offset) % d;
}
function startBuffer(buffer, level, offset = 0) {
  const g = ctx.createGain(); g.gain.value = 0;
  const node = ctx.createBufferSource(); node.buffer = buffer; node.loop = true;
  node.connect(g); g.connect(outGain);
  outGain.gain.setTargetAtTime(level, ctx.currentTime, 0.02);
  const t = ctx.currentTime + 0.01;
  node.start(t, offset % buffer.duration);
  g.gain.setTargetAtTime(1, t, 0.015);
  const old = playing;
  if (old) { old.gain.gain.setTargetAtTime(0, t, 0.015); old.node.stop(t + 0.12); }
  playing = { node, gain: g, buffer, start: t, offset: offset % buffer.duration };
}
function stopAll() {
  if (playing) { const p = playing; p.gain.gain.setTargetAtTime(0, ctx.currentTime, 0.015); p.node.stop(ctx.currentTime + 0.1); playing = null; }
  stopLive();
  $('playBtn').textContent = '▶ Play'; $('playBtn').classList.remove('playing');
  $('dlClip').disabled = !lastRendered;
}

// (re)start sound for the current rig, size, source and gain
async function refresh() {
  if (!cur) return;
  ensureCtx(); if (ctx.state !== 'running') await ctx.resume();
  routeCab();
  if (srcKind === 'live') return startLive();
  stopLive();
  const input = await sourceData();
  if (!input) { status('Choose an audio file first.', true); return; }
  try {
    const want = { rig: cur, size: cur.sizeIdx };
    let r;
    if (bypass) { const b = ctx.createBuffer(1, input.length, SR); b.copyToChannel(Float32Array.from(input, x => x * db(Number($('inGain').value))), 0); r = { buffer: b, loudness: null }; }
    else r = await render(cur, cur.sizeIdx, input);
    if (cur !== want.rig || cur.sizeIdx !== want.size) return; // the user moved on
    startBuffer(r.buffer, levelFor(r.loudness), position());
    lastRendered = { rig: cur, size: cur.sizeIdx, ...r };
    $('dlClip').disabled = bypass;
    $('playBtn').textContent = '■ Stop'; $('playBtn').classList.add('playing');
    const l = r.loudness;
    status(bypass ? 'Dry DI (no amp, no cab).' : `Playing ${cur.name}${cur.sizes.length > 1 ? ' · ' + cur.sizes[cur.sizeIdx].label : ''}${l != null ? ` · capture loudness ${l.toFixed(1)} dB` : ''}`);
  } catch (e) { if (!/terminated/.test(e.message)) status(`Can't play this model: ${e.message}`, true); }
}

// ─── live input ───────────────────────────────────────────────────────────────

async function startLive() {
  if (playing) { const p = playing; p.gain.gain.setTargetAtTime(0, ctx.currentTime, 0.015); p.node.stop(ctx.currentTime + 0.1); playing = null; }
  try {
    if (!live) {
      await ctx.audioWorklet.addModule('worklet.js');
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 1 } });
      const src = ctx.createMediaStreamSource(stream);
      const node = new AudioWorkletNode(ctx, 'nam-processor', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
      src.connect(node); node.connect(outGain);
      live = { stream, src, node };
      node.port.onmessage = e => {
        if (e.data.error) status(`Can't play this model: ${e.data.error}`, true);
        else if (e.data.ready) { outGain.gain.setTargetAtTime(bypass ? 1 : levelFor(e.data.loudness), ctx.currentTime, 0.02); const sz = cur.sizes[cur.sizeIdx].label; status(`Live: ${cur.name}${cur.sizes.length > 1 ? ' · ' + sz : ''}.${cur.sizes.length > 1 && sz !== 'Lite' ? ' If it crackles, try Lite.' : ' If it crackles, raise the buffer in your OS audio settings.'}`); }
      };
    }
    const net = cur.sizes[cur.sizeIdx].json;
    if ((net.sample_rate || SR) !== ctx.sampleRate) status(`Note: this model is for ${net.sample_rate} Hz, audio is running at ${ctx.sampleRate} Hz.`);
    live.node.port.postMessage({ gain: db(Number($('inGain').value)) });
    if (bypass) { live.node.port.postMessage({ clear: true }); live.src.disconnect(); live.src.connect(outGain); outGain.gain.setTargetAtTime(1, ctx.currentTime, 0.02); status('Live: dry DI.'); }
    else { try { live.src.disconnect(); } catch {} live.src.connect(live.node); live.node.port.postMessage({ json: net }); status('Loading model…'); }
    $('playBtn').textContent = '■ Stop'; $('playBtn').classList.add('playing');
  } catch (e) { status(`Live input failed: ${e.message}`, true); stopLive(); }
}
function stopLive() {
  if (!live) return;
  live.stream.getTracks().forEach(t => t.stop());
  try { live.src.disconnect(); live.node.disconnect(); } catch {}
  live = null;
}

// ─── downloads ────────────────────────────────────────────────────────────────

function rigSettings() {
  return {
    model: sanitize(cur.name) + '.nam',
    size: cur.sizes[cur.sizeIdx].label,
    input_gain_db: Number($('inGain').value),
    volume_db: Number($('vol').value),
    normalize_to_db: $('norm').checked ? TARGET : null,
    cab: $('cab').value === 'ir' && ir ? ir.name : $('cab').value === 'simple' ? 'simple cab (not included: use any guitar cab IR)' : 'none',
    source: cur.source,
  };
}
function downloadRig() {
  if (!cur) return;
  const enc = new TextEncoder(), base = sanitize(cur.name), s = rigSettings();
  const files = [{ name: `${base}/${base}.nam`, data: enc.encode(cur.text) }];
  if ($('cab').value === 'ir' && ir) files.push({ name: `${base}/${ir.name}`, data: new Uint8Array(ir.bytes) });
  files.push({ name: `${base}/rig.json`, data: enc.encode(JSON.stringify(s, null, 2)) });
  files.push({ name: `${base}/README.txt`, data: enc.encode(
    `${cur.name}\n\nModel: ${s.model}${cur.sizes.length > 1 ? ` (A2 file: holds ${cur.sizes.map(x => x.label).join(' and ')}; this rig used ${s.size})` : ''}\n` +
    `Cab: ${s.cab}\nInput gain: ${s.input_gain_db} dB\nOutput: normalized to ${s.normalize_to_db ?? 'off'} dB, volume ${s.volume_db} dB\nFrom: ${s.source}\n\n` +
    `Load the .nam in a NAM player (NAM plugin 0.7.14+ for A2) and the IR in its cab/IR slot.\n`) });
  save(makeZip(files), `${base}.zip`);
}
async function downloadClip() {
  if (!lastRendered) return;
  const { buffer, loudness, rig, size } = lastRendered;
  const off = new OfflineAudioContext(1, Math.round(buffer.duration * SR), SR);
  const s = off.createBufferSource(); s.buffer = buffer;
  const g = off.createGain(); g.gain.value = levelFor(loudness) * db(Number($('vol').value));
  s.connect(g);
  const mode = $('cab').value;
  let last = g;
  if (mode === 'simple') { const c = simpleCab(off); g.connect(c.input); last = c.output; }
  else if (mode === 'ir' && ir) { const c = off.createConvolver(); c.buffer = ir.buffer; g.connect(c); last = c; }
  last.connect(off.destination); s.start();
  const out = await off.startRendering();
  save(encodeWav(out.getChannelData(0), SR), `${sanitize(rig.name)}${rig.sizes.length > 1 ? '-' + rig.sizes[size].label : ''}.wav`);
}

// ─── wiring ───────────────────────────────────────────────────────────────────

export function initPlayer() {
  $('rigFiles').addEventListener('change', async e => { const fs = [...e.target.files]; e.target.value = ''; for (const f of fs) addRig(f.name, await f.text()); });
  $('rigList').addEventListener('click', e => {
    const el = e.target.closest('.rig'); if (!el) return;
    const rig = rigs.find(r => r.id === el.dataset.id);
    if (e.target.closest('[data-remove]')) {
      rigs.splice(rigs.indexOf(rig), 1);
      if (cur === rig) { cur = rigs[0] || null; if (!cur) { stopAll(); ['playBtn', 'bypassBtn', 'dlNam', 'dlRig', 'dlClip'].forEach(id => { $(id).disabled = true; }); } }
      renderRigs(); if (cur && (playing || live)) refresh(); return;
    }
    const sb = e.target.closest('[data-size]');
    if (sb) rig.sizeIdx = Number(sb.dataset.size);
    select(rig);
  });
  $('playBtn').addEventListener('click', () => { if (playing || live) stopAll(); else refresh(); });
  $('bypassBtn').addEventListener('click', () => { bypass = !bypass; $('bypassBtn').classList.toggle('on', bypass); if (playing || live) refresh(); });
  $('src').addEventListener('change', async () => {
    const v = $('src').value;
    if (v === 'file') { $('srcFile').click(); return; }
    srcKind = v; if (playing || live) { stopAll(); refresh(); }
  });
  $('srcFile').addEventListener('change', async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) { $('src').value = srcKind; return; }
    try {
      status('Reading ' + f.name + '…');
      fileData = await decodeFile(f); fileName = f.name; srcKind = 'file';
      $('src').options[1].textContent = 'Your file: ' + f.name;
      status('');
      if (playing || live) { stopAll(); } refresh();
    } catch (err) { status(`Couldn't read that audio: ${err.message}`, true); $('src').value = srcKind; }
  });
  $('cab').addEventListener('change', () => { if ($('cab').value === 'ir' && !ir) $('irFile').click(); else routeCab(); });
  $('irFile').addEventListener('change', async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) { $('cab').value = 'simple'; routeCab(); return; }
    try {
      ensureCtx();
      const bytes = await f.arrayBuffer();
      const buffer = await ctx.decodeAudioData(bytes.slice(0));
      ir = { name: f.name, bytes, buffer };
      $('cab').options[2].textContent = 'Your IR: ' + f.name;
      $('cab').value = 'ir'; routeCab();
    } catch (err) { status(`Couldn't read that IR: ${err.message}`, true); $('cab').value = 'simple'; routeCab(); }
  });
  let t;
  $('inGain').addEventListener('input', () => {
    $('inGainV').textContent = `${$('inGain').value > 0 ? '+' : ''}${$('inGain').value} dB`;
    if (live) live.node.port.postMessage({ gain: db(Number($('inGain').value)) });
    else if (playing) { clearTimeout(t); t = setTimeout(refresh, 350); }
  });
  $('vol').addEventListener('input', () => { $('volV').textContent = `${$('vol').value} dB`; setVolume(); });
  $('norm').addEventListener('change', () => { if (playing || live) refresh(); });
  $('dlNam').addEventListener('click', () => cur && save(new Blob([cur.text], { type: 'application/json' }), sanitize(cur.name) + '.nam'));
  $('dlRig').addEventListener('click', downloadRig);
  $('dlClip').addEventListener('click', downloadClip);
}
