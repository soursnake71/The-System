/* Omega Twelve app: the panel, live playback (player.js on the audio thread), file import / export, menus.
   Offline jobs (export, resample, tempo / key, EQ match, auto master, timestretch) run in worker.js. */
'use strict';
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const PBYID = Object.fromEntries(TW.PARAMS.map(p => [p.id, p]));
const PRESETS = TW.makePresets();
const FAMS = [...new Set(PRESETS.map(p => p.family))];
const STORE = 'omega-twelve';

// the fader pages: eight settings each ('pad' = the Multi Mode page: each pad's pitch)
const PAGES = [
  {name:'Tune/Decay', ids:['semis', 'cents', 'decay', 'a4', 'rpm', 'stretch', 'grain', 'smode']},
  {name:'Mix', ids:['input', 'drive', 'pre', 'alias', 'mix', 'output', 'link', 'mono']},
  {name:'Multi Mode', ids:'pads'},
  {name:'Sound', ids:['machine', 'bits', 'rate', 'ftype', 'cutoff', 'res', 'fenv', 'retrig']},
  {name:'Lo-Fi', ids:['crackle', 'hiss', 'wow', 'flutter', 'tape', 'age', 'hum', 'humHz']},
  {name:'Master EQ', ids:['lsG', 'p1F', 'p1G', 'p2F', 'p2G', 'p3F', 'p3G', 'hsG']},
  {name:'EQ Shape', ids:['lsF', 'p1Q', 'p2Q', 'p3Q', 'hsF', 'match', 'fOn', 'calib']},
  {name:'Dynamics', ids:['cThr', 'cRat', 'cAtk', 'cRel', 'cMake', 'mGain', 'lim', 'mOn']}
];
const SHORT = {semis:'TUNE', cents:'FINE', decay:'DECAY', a4:'A4', rpm:'ALIAS', stretch:'STRTCH', grain:'GRAIN', smode:'MODE', input:'INPUT', drive:'DRIVE', pre:'PREAMP',
  alias:'UNFILT', mix:'MIX', output:'OUTPUT', link:'LINK', mono:'MONO', machine:'SAMPLR', bits:'BITS', rate:'RATE', ftype:'FILTER', cutoff:'CUTOFF', res:'RESO',
  fenv:'F.ENV', retrig:'RETRIG', crackle:'CRACKL', hiss:'HISS', wow:'WOW', flutter:'FLUTTR', tape:'SATUR', age:'AGE', hum:'HUM', humHz:'MAINS', lsG:'LOW',
  p1F:'B1 FQ', p1G:'BAND 1', p2F:'B2 FQ', p2G:'BAND 2', p3F:'B3 FQ', p3G:'BAND 3', hsG:'HIGH', lsF:'LOW FQ', p1Q:'B1 Q', p2Q:'B2 Q', p3Q:'B3 Q', hsF:'HI FQ',
  match:'MATCH', fOn:'FILT ON', calib:'CALIB', cThr:'THRESH', cRat:'RATIO', cAtk:'ATTACK', cRel:'RELEAS', cMake:'MAKEUP', mGain:'GAIN', lim:'CEIL', mOn:'MASTER'};
// sound settings that change the source the player gets (the rest reach the playing sound directly)
const SOURCE_IDS = new Set(['mono', 'stretch', 'grain', 'smode']);
const BANK_DEF = [[0, 1, 2, 3, 4, 5, 6, 7], [-8, -7, -6, -5, -4, -3, -2, -1], [8, 9, 10, 11, 12, 13, 14, 15], [-16, -15, -14, -13, -12, -11, -10, -9]];
const FORMATS = [['wav:16', 'WAV 16'], ['wav:24', 'WAV 24'], ['wav:f32', 'WAV 32F'], ['aiff:16', 'AIFF 16'], ['aiff:24', 'AIFF 24'], ['flac:16', 'FLAC 16'], ['flac:24', 'FLAC 24'], ['mp3:320', 'MP3 320'], ['mp3:192', 'MP3 192'], ['mp3:128', 'MP3 128']];
const RATES = [44100, 48000, 96000, 32000, 26041];
const LUFS = [-9, -12, -14, -16, -20];
const NOTE_KEYS = TW.NOTE_NAMES.flatMap(n => [n + ' major', n + ' minor']);

const S = {
  p:TW.defaults(), match:new Array(10).fill(0), preset:-1, presetName:'Init',
  sel:[0, 0], cur:null, loop:false, zoom:false, bypass:false, bpm:null, key:null, ref:null, refName:'', user:[],
  page:0, bank:0, pads:BANK_DEF.flat(), shift:0, menu:null, edit:null, entry:null,
  fmt:'wav:16', expSr:48000, expRange:'sel', lufs:-14, warpMode:'stretch', srcBpm:null, tgtBpm:90, toKey:0
};
let src = null;          // {chs:[Float32Array], sr, name}
let undo = [];

/* ---------------- storage ---------------- */
const KEEP = ['p', 'match', 'preset', 'presetName', 'loop', 'user', 'page', 'bank', 'pads', 'fmt', 'expSr', 'expRange', 'lufs', 'warpMode', 'tgtBpm'];
function save(){ try { localStorage.setItem(STORE, JSON.stringify(Object.fromEntries(KEEP.map(k => [k, S[k]])))); } catch (e) {} }
function load(){
  try {
    const d = JSON.parse(localStorage.getItem(STORE) || 'null'); if (!d) return;
    for (const k of KEEP) if (d[k] != null) S[k] = k === 'p' ? Object.assign(TW.defaults(), d.p) : d[k];
    if (!Array.isArray(S.pads) || S.pads.length !== 32) S.pads = BANK_DEF.flat();
  } catch (e) {}
}

/* ---------------- messages ---------------- */
let toastT = 0;
function toast(msg, ms){ const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('show'), ms || 2800); }
let msgT = 0, lcdMsg = null;
// a short message on the display's second line
function say(text, ms){ lcdMsg = text; clearTimeout(msgT); msgT = setTimeout(() => { lcdMsg = null; paintLcd(); }, ms || 1800); paintLcd(); }

/* ---------------- audio: the live player ---------------- */
// inside the Omega System: it goes quiet when you leave and picks up when you come back
window.twelveSuspend = () => { if (ctx) ctx.suspend(); };
window.twelveResume = () => { if (ctx) ctx.resume(); };
let ctx = null, node = null, local = null, audioReady = null, meterPk = 0, playing = false, playFrac = -1;
function audio(){
  if (!audioReady) audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    try {
      for (const f of ['sp1200.js', 'engine.js', 'player.js']) await ctx.audioWorklet.addModule(f);
      node = new AudioWorkletNode(ctx, 'twelve-player', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2]});
      node.port.onmessage = e => onPlayer(e.data);
      node.connect(ctx.destination);
    } catch (e){
      // no AudioWorklet: the same player on this thread
      local = new TW.Player(ctx.sampleRate);
      const sp = ctx.createScriptProcessor(1024, 0, 2); let n = 0;
      sp.onaudioprocess = ev => { const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1); local.process(L, R, L.length);
        let pk = 0; for (let i = 0; i < L.length; i++) pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i]));
        if ((n += L.length) > ctx.sampleRate / 30){ n = 0; onPlayer({type:'tick', pos:local.pos(), active:local.active, peak:pk}); } };
      sp.connect(ctx.destination); node = {port:{postMessage:d => localMsg(d)}};
    }
    pushParams(); pushSource();
  })();
  if (ctx && ctx.state === 'suspended') ctx.resume();
  return audioReady;
}
function localMsg(d){
  const pl = local;
  if (d.type === 'source') pl.setSource(d.chs, d.sr); else if (d.type === 'params') pl.setParams(d.p, d.match); else if (d.type === 'play') pl.play(d);
  else if (d.type === 'stop') pl.stop(d.main); else if (d.type === 'loop') pl.setLoop(d.on); else if (d.type === 'bend') pl.setBend(d.st);
}
function send(msg, transfer){ if (node) node.port.postMessage(msg, transfer || []); }
function onPlayer(d){
  if (d.type === 'tick'){
    meterPk = Math.max(d.peak, meterPk * .85);
    const was = playing; playing = d.pos >= 0; playFrac = d.pos;
    if (was !== playing) paintLeds();
    paintVu(); if (playing || was) drawWave();
  } else if (d.type === 'error') toast('Audio: ' + d.msg, 4000);
}
const FS = () => (ctx ? ctx.sampleRate : 48000);
function pushParams(){ send({type:'params', p:S.p, match:S.match}); }
// the selection, mono-mixed and timestretched as set, to the player
let srcGen = 0, srcT = 0;
function pushSource(){
  clearTimeout(srcT);
  if (!src || !node) return;
  const my = ++srcGen;
  if (S.p.stretch === 1){
    const [a, b] = S.sel; let chs = src.chs.map(c => c.slice(a, b));
    if (S.p.mono > .5 && chs.length > 1){ const m = new Float32Array(chs[0].length); for (let i = 0; i < m.length; i++) m[i] = (chs[0][i] + chs[1][i]) * .5; chs = [m]; }
    send({type:'source', chs, sr:src.sr}, chs.map(c => c.buffer));
    return;
  }
  srcT = setTimeout(() => ask({type:'prep', p:S.p, sel:S.sel}).then(r => { if (my === srcGen) send({type:'source', chs:r.chs, sr:src.sr}, r.chs.map(c => c.buffer)); }).catch(e => toast(e.message)), 60);
}
function paintVu(){ const db = 20 * Math.log10(meterPk + 1e-9), el = $('#vu'); el.style.width = Math.max(0, Math.min(100, (db + 48) / 48 * 100)) + '%'; }

/* ---------------- worker (offline jobs) ---------------- */
const worker = new Worker('worker.js');
let wid = 0; const waiting = new Map();
worker.onmessage = e => { const d = e.data, cb = waiting.get(d.id); if (cb){ waiting.delete(d.id); d.type === 'error' ? cb.reject(new Error(d.msg)) : cb.resolve(d); } };
function ask(msg, transfer){ const id = ++wid; return new Promise((resolve, reject) => { waiting.set(id, {resolve, reject}); worker.postMessage(Object.assign({id}, msg), transfer || []); }); }
function sendSourceToWorker(){ worker.postMessage({type:'source', chs:src.chs.map(c => c.slice()), sr:src.sr}); }

/* ---------------- playing ---------------- */
function startMain(frac){
  if (!needSrc()) return;
  audio().then(() => send({type:'play', main:true, loop:S.loop, raw:S.bypass, from:frac || 0}));
}
function stopMain(){ send({type:'stop', main:true}); }
function playPos(){ return playing ? Math.max(0, playFrac) : 0; }
function stopAll(){
  send({type:'stop'});
  if (recorder && recorder.state !== 'inactive'){ recorderCancelled = true; recorder.stop(); }
  $$('.pad.hit').forEach(p => p.classList.remove('hit'));
  say('STOP ALL');
}
function run(){
  if (playing){ stopMain(); return; }
  const [a, b] = S.sel, from = S.cur != null && S.cur > a && S.cur < b ? (S.cur - a) / (b - a) : 0;
  startMain(from);
}
// a pad: plays at once (the player makes the sound as it goes); a new hit cuts the last, like the hardware
function hitPad(i, vel){
  if (!src){ say('LOAD A SAMPLE'); return Promise.resolve(); }
  const semis = typeof i === 'number' && i >= 0 && i < 32 ? S.pads[i] : 0;
  const el = $(`.pad[data-i="${i % 8}"]`);
  if (el && Math.floor(i / 8) === S.bank){ el.classList.add('hit'); setTimeout(() => el.classList.remove('hit'), 110); }
  lastPad = i; if (!S.menu && !S.edit && !S.entry) say(`PAD ${'ABCD'[Math.floor(i / 8)]}${i % 8 + 1}  ${semis > 0 ? '+' : ''}${semis} ST`, 900);
  return audio().then(() => send({type:'play', semis, vel:vel == null ? 1 : vel, raw:S.bypass}));
}
let lastPad = 0;
// MIDI notes play any pitch (chromatic mode)
function playSemis(semis, vel){ if (!src) return Promise.resolve(); return audio().then(() => send({type:'play', semis, vel:vel == null ? 1 : vel, raw:S.bypass})); }

/* ---------------- values ---------------- */
function norm(p, v){ return p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min); }
function denorm(p, n){ n = TW.clamp(n, 0, 1); let v = p.log ? p.min * Math.pow(p.max / p.min, n) : p.min + n * (p.max - p.min); if (p.step) v = Math.round(v / p.step) * p.step; return TW.clamp(+v.toFixed(4), p.min, p.max); }
function fmtVal(p, v){
  if (p.choices) return p.choices[Math.round(v)] || '';
  if (p.toggle) return v > .5 ? 'ON' : 'OFF';
  if (p.id === 'decay') return v >= 31 ? 'OFF' : Math.round(v) + '';
  switch (p.unit){
    case 'Hz': return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + 'k' : Math.round(v) + '';
    case '%': return Math.round(v * 100) + '%';
    case 'dB': return (v > 0 ? '+' : '') + v.toFixed(1);
    case 'st': return (v > 0 ? '+' : '') + (p.step < 1 ? v.toFixed(1) : Math.round(v)) + 'st';
    case 'ct': return (v > 0 ? '+' : '') + Math.round(v) + 'ct';
    case 'x': return v.toFixed(2) + 'x';
    case 'ms': return (v < 10 ? v.toFixed(1) : Math.round(v)) + 'ms';
    case 'bit': return Math.round(v) + '';
    case ':1': return v.toFixed(1) + ':1';
    default: return (Math.abs(v) < 10 ? v.toFixed(2) : v.toFixed(1)) + (p.unit || '');
  }
}
let touched = null;
function setParam(id, v){
  const was = S.p[id];
  S.p[id] = v; touched = id;
  if (!TW.MASTER_IDS.includes(id)) markEdited();
  pushParams();
  if (SOURCE_IDS.has(id) && was !== v) pushSource();
  paintAll();
}
function markEdited(){ if (!S.presetName.endsWith('*')) S.presetName += '*'; }
function commit(){ save(); }
function setMachine(m){
  const M = TW.MACHINES[m];
  Object.assign(S.p, {machine:m, rate:M.rate, bits:M.bits, ftype:M.ftype, cutoff:M.cutoff, res:M.res});
  touched = 'machine'; markEdited(); pushParams(); paintAll();
}
function setAny(id, v){ if (id === 'machine') setMachine(Math.round(v)); else setParam(id, v); }

/* ---------------- panel: knobs ---------------- */
const knobs = new Map();   // param id -> element (MIDI learn uses this)
function makeKnob(id, box, label){
  const p = PBYID[id], el = document.createElement('div');
  el.className = 'knob'; el.innerHTML = `<div class="cap"><i class="ptr"></i></div><span class="lbl">${label}</span>`;
  box.appendChild(el);
  let y0 = 0, n0 = 0, moved = false, lastTap = 0;
  el.addEventListener('pointerdown', e => { el.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = norm(p, S.p[id]); moved = false; });
  el.addEventListener('pointermove', e => {
    if (!el.hasPointerCapture(e.pointerId)) return;
    const dy = (y0 - e.clientY) / panelScale; if (Math.abs(dy) > 2) moved = true;
    if (moved) setParam(id, denorm(p, n0 + dy / 160));
  });
  el.addEventListener('pointerup', e => { if (!moved){ const t = Date.now(); if (t - lastTap < 350) setParam(id, p.def); lastTap = t; } commit(); });
  knobs.set(id, el);
}
function paintKnobs(){
  for (const [id, el] of knobs){ if (!el.classList.contains('knob')) continue; const n = TW.clamp(norm(PBYID[id], S.p[id]), 0, 1); el.querySelector('.ptr').style.transform = `rotate(${-140 + n * 280}deg)`; }
}

/* ---------------- panel: faders ---------------- */
const faders = [];
function faderTarget(i){
  const pg = PAGES[S.page];
  if (pg.ids === 'pads') return {pad:S.bank * 8 + i, p:{id:'pad', name:`Pad ${'ABCD'[S.bank]}${i + 1}`, min:-24, max:24, def:BANK_DEF[S.bank][i], step:1, unit:'st'}};
  const id = pg.ids[i]; return {id, p:PBYID[id]};
}
function targetVal(t){ return t.pad != null ? S.pads[t.pad] : S.p[t.id]; }
function setTarget(t, v){
  if (t.pad != null){ S.pads[t.pad] = v; touched = null; lcdParam = {name:t.p.name, val:fmtVal(t.p, v)}; paintFaders(); paintLcd(); return; }
  lcdParam = null; setAny(t.id, v);
}
let lcdParam = null;
function buildFaders(){
  const box = $('#faders');
  for (let i = 0; i < 8; i++){
    const el = document.createElement('div'); el.className = 'fader learnable';
    el.innerHTML = '<span class="val"></span><div class="track"><i class="ticks"></i><i class="slot"></i><i class="fcap"></i></div><span class="nm"></span><span class="num">' + (i + 1) + '</span>';
    box.appendChild(el); faders.push(el);
    const track = el.querySelector('.track');
    let lastTap = 0, down = false;
    const at = e => { const r = track.getBoundingClientRect(), y = (e.clientY - r.top) / r.height; return TW.clamp((1 - y - .04) / .92, 0, 1); };
    el.addEventListener('pointerdown', e => {
      const t = faderTarget(i);
      if (learnHook(t.id)) return;
      el.setPointerCapture(e.pointerId); down = true;
      const now = Date.now(); if (now - lastTap < 320){ setTarget(t, t.p.def); commit(); lastTap = 0; return; } lastTap = now;
      setTarget(t, denorm(t.p, at(e)));
    });
    el.addEventListener('pointermove', e => { if (down && el.hasPointerCapture(e.pointerId)){ const t = faderTarget(i); setTarget(t, denorm(t.p, at(e))); } });
    el.addEventListener('pointerup', () => { down = false; commit(); });
  }
}
function paintFaders(){
  faders.forEach((el, i) => {
    const t = faderTarget(i), v = targetVal(t), n = TW.clamp(norm(t.p, v), 0, 1);
    el.querySelector('.fcap').style.top = (4 + (1 - n) * 92) + '%';
    el.querySelector('.nm').textContent = t.pad != null ? `${'ABCD'[S.bank]}${i + 1}` : SHORT[t.id] || t.p.name;
    el.querySelector('.val').textContent = fmtVal(t.p, v).replace(/^(.{7}).*$/, '$1');
    el.dataset.id = t.id || '';
  });
  $$('#pageList button').forEach((b, i) => b.querySelector('.led').classList.toggle('on', i === S.page));
}
function setPage(i){ S.page = (i + PAGES.length) % PAGES.length; lcdParam = null; touched = null; paintFaders(); say(PAGES[S.page].name.toUpperCase() + ' FADERS', 1200); save(); }

/* ---------------- panel: pads, banks, keys ---------------- */
function buildPanel(){
  const pads = $('#pads');
  for (let i = 0; i < 8; i++){
    const b = document.createElement('div'); b.className = 'pad'; b.dataset.i = i;
    b.innerHTML = '<div class="pk"></div><span class="pn">' + (i + 1) + '</span>';
    b.addEventListener('pointerdown', e => { e.preventDefault(); hitPad(S.bank * 8 + i); });
    pads.appendChild(b);
  }
  $('#bankLeds').innerHTML = 'ABCD'.split('').map(c => `<div><i class="led"></i>${c}</div>`).join('');
  $('#pageList').innerHTML = PAGES.map((pg, i) => `<button data-i="${i}"><i class="led"></i>${pg.name}</button>`).join('');
  $$('#pageList button').forEach(b => b.addEventListener('click', () => setPage(+b.dataset.i)));
  const kp = $('#keypad');
  for (const k of ['1', '2', '3', '4', '5', '6', '7', '8', '9', 'No', '0', 'Yes']){
    const c = document.createElement('div'); c.className = 'cell';
    c.innerHTML = `<span class="lbl">${k}</span><button class="key" aria-label="${k}"></button>`;
    c.querySelector('button').addEventListener('click', () => keypad(k));
    kp.appendChild(c);
  }
  makeKnob('input', $('#gainKnob'), 'Gain');
  makeKnob('output', $('#knobs'), 'Mix Volume');
  makeKnob('mGain', $('#knobs'), 'Master Volume');
  // programming keys: Edit functions, or Master functions with the Master light on
  const pb = $('#pbtns');
  PROG.forEach((f, i) => {
    const c = document.createElement('div'); c.className = 'cell';
    c.innerHTML = `<span class="lbl t">${f[0]}</span><button class="key" aria-label="${f[0]} / ${f[2]}"></button><span class="lbl b">${f[2]}</span>`;
    c.querySelector('button').addEventListener('click', () => (S.shift ? f[3] : f[1])());
    pb.appendChild(c);
  });
  fillLists();
  buildFaders();
  const tap = (id, fn) => $(id).addEventListener('click', fn);
  tap('#kSetup', () => openMenu('setup')); tap('#kDisk', () => openMenu('disk')); tap('#kSync', () => openMenu('sync')); tap('#kSample', () => openMenu('sample'));
  tap('#kTempo', () => openMenu('tempo')); tap('#kLeft', () => arrow(-1)); tap('#kRight', () => arrow(1)); tap('#kEnter', enter);
  tap('#kShift', () => { S.shift = S.shift ? 0 : 1; paintLeds(); say(S.shift ? 'MASTER KEYS' : 'EDIT KEYS', 1000); });
  tap('#kPage', () => setPage(S.page + 1));
  tap('#kBank', () => { S.bank = (S.bank + 1) % 4; paintLeds(); paintFaders(); say(`BANK ${'ABCD'[S.bank]}  ${fmtSt(S.pads[S.bank * 8])}..${fmtSt(S.pads[S.bank * 8 + 7])}`, 1200); save(); });
  tap('#kLoop', () => { S.loop = !S.loop; send({type:'loop', on:S.loop}); paintLeds(); save(); say(S.loop ? 'REPEAT ON' : 'REPEAT OFF', 900); });
  tap('#kRun', run); tap('#kStop', stopAll);
  tap('#kDice', randomize); tap('#kReset', resetAll);
}
// the menus printed beside their keys
function fillLists(){
  if (window.IO) MENUS.sync.items = IO.items();
  for (const [k, m] of Object.entries(MENUS)){
    const ol = $('#l' + k[0].toUpperCase() + k.slice(1)); if (!ol) continue;
    ol.innerHTML = m.items.map(it => `<li data-n="${it[0]}">${it[0]} ${it[1]}</li>`).join('');
    ol.querySelectorAll('li').forEach(li => li.addEventListener('click', () => { openMenu(k, true); runItem(+li.dataset.n); }));
  }
}
const fmtSt = v => (v > 0 ? '+' : '') + v;
function paintLeds(){
  for (const k of ['setup', 'disk', 'sync', 'sample']) $('#led' + k[0].toUpperCase() + k.slice(1)).classList.toggle('on', S.menu === k);
  $('#ledEdit').classList.toggle('on', !S.shift); $('#ledMaster').classList.toggle('on', !!S.shift);
  $$('#pbtns .lbl.t').forEach(l => l.classList.toggle('off', !!S.shift)); $$('#pbtns .lbl.b').forEach(l => l.classList.toggle('off', !S.shift));
  $$('#bankLeds .led').forEach((l, i) => l.classList.toggle('on', i === S.bank));
  $('#ledLoop').classList.toggle('on', S.loop); $('#ledRun').classList.toggle('on', playing); $('#ledStop').classList.toggle('on', S.bypass);
}

/* ---------------- the display ---------------- */
function paintLcd(){
  const m = S.menu ? MENUS[S.menu] : null;
  $('#l1a').textContent = m ? m.title : PAGES[S.page].name.toUpperCase();
  $('#l1b').textContent = (S.preset >= 0 ? 'P' + (S.preset + 1) + ' ' : '') + S.presetName.toUpperCase();
  let l2 = '', l2b = '';
  if (S.entry) l2 = `${S.entry.label}: ${S.entry.text}_`;
  else if (S.edit){ l2 = S.edit.label + ':'; l2b = '◀ ' + S.edit.show() + ' ▶'; }
  else if (lcdMsg) l2 = lcdMsg;
  else if (lcdParam){ l2 = lcdParam.name.toUpperCase(); l2b = lcdParam.val; }
  else if (touched){ const p = PBYID[touched]; l2 = p.name.toUpperCase(); l2b = fmtVal(p, S.p[touched]); }
  else if (!src) l2 = 'LOAD A SAMPLE';
  else l2 = TW.MACHINES[S.p.machine | 0].name.toUpperCase() + ' ' + fmtVal(PBYID.ftype, S.p.ftype).toUpperCase();
  $('#l2').textContent = l2; $('#l2b').textContent = l2b;
  const grid = $('#menuGrid');
  grid.classList.toggle('on', !!m && !S.entry);
  if (m && !S.entry){
    grid.innerHTML = m.items.map(it => { const v = it[3] ? it[3]() : ''; return `<button data-n="${it[0]}" class="${S.edit && S.edit.n === it[0] ? 'sel' : ''}">${it[0]} ${it[1].toUpperCase()}${v ? ' <i>' + v + '</i>' : ''}</button>`; }).join('');
    grid.querySelectorAll('button').forEach(b => b.addEventListener('click', () => runItem(+b.dataset.n)));
  }
  const sr = src ? src.sr : 1, t = i => (i / sr).toFixed(3);
  $('#info').innerHTML = src ? `<b>${S.bpm ? S.bpm.toFixed(1) : '--'}</b>BPM <b>${S.key ? S.key.name.replace(' major', '').replace(' minor', 'm') : '--'}</b> IN<b>${t(S.sel[0])}</b> OUT<b>${t(S.sel[1])}</b>${S.cur != null ? ' CUR<b>' + t(S.cur) + '</b>' : ''}` : 'BANK ' + 'ABCD'[S.bank];
}
function paintAll(){ paintKnobs(); paintFaders(); paintLeds(); paintLcd(); }

/* ---------------- waveform: cursor and manual in / out ---------------- */
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
function view(){ if (!src) return [0, 1]; const n = src.chs[0].length; if (!S.zoom) return [0, n]; const [a, b] = S.sel, pad = (b - a) * .04; return [Math.max(0, a - pad), Math.min(n, b + pad)]; }
function drawWave(){
  const c = $('#waveC'), r = c.getBoundingClientRect(), w = c.clientWidth, h = c.clientHeight; if (!w) return;
  const dpr = (devicePixelRatio || 1) * r.width / w;
  if (c.width !== Math.round(w * dpr)){ c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
  const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
  const ink = '#1b2310', dim = 'rgba(27,35,16,.32)';
  if (!src){ g.fillStyle = dim; g.font = '700 11px monospace'; g.textAlign = 'center'; g.fillText('DISK 1 OR SAMPLE 1 LOADS A SAMPLE', w / 2, h / 2 + 4); return; }
  const n = src.chs[0].length, [v0, v1] = view(), X = i => (i - v0) / (v1 - v0) * w, d = src.chs[0], d2 = src.chs[1] || d;
  const top = 9, H = h - top, mid = top + H / 2;
  for (let x = 0; x < w; x++){
    const a = v0 + x / w * (v1 - v0), b = v0 + (x + 1) / w * (v1 - v0); let lo = 0, hi = 0;
    if ((v1 - v0) / w > n / 2048 && peaks){ const k0 = Math.floor(a / n * 2048), k1 = Math.max(k0 + 1, Math.floor(b / n * 2048)); for (let k = k0; k < k1 && k < 2048; k++){ lo = Math.min(lo, peaks.mn[k]); hi = Math.max(hi, peaks.mx[k]); } }
    else for (let i = Math.floor(a); i < Math.max(Math.floor(a) + 1, Math.floor(b)); i++){ const v = ((d[i] || 0) + (d2[i] || 0)) * .5; lo = Math.min(lo, v); hi = Math.max(hi, v); }
    g.fillStyle = a >= S.sel[0] && a < S.sel[1] ? ink : dim;
    g.fillRect(x, mid - hi * H / 2, 1, Math.max(1, (hi - lo) * H / 2));
  }
  // in / out flags
  g.fillStyle = ink; g.font = '700 8px Arial, sans-serif'; g.textBaseline = 'alphabetic'; g.textAlign = 'left';
  [[S.sel[0], 'IN'], [S.sel[1], 'OUT']].forEach(([s, t], k) => {
    const x = X(s), fw = Math.ceil(g.measureText(t).width) + 6, fx = TW.clamp(k ? x - fw : x, 0, w - fw);
    g.fillRect(x - .75, 0, 1.5, h); g.fillRect(fx, 0, fw, 9); g.fillStyle = '#b7c38b'; g.fillText(t, fx + 3, 7.5); g.fillStyle = ink;
  });
  if (S.cur != null){ const x = X(S.cur); for (let y = top; y < h; y += 4) g.fillRect(x - .5, y, 1, 2); }
  if (playing && playFrac >= 0){ const x = X(S.sel[0] + playFrac * (S.sel[1] - S.sel[0])); g.fillRect(x - 1, top, 2, H); }
}
function setupWave(){
  const el = $('#wave');
  let mode = null, startX = 0;
  const idx = x => { const r = el.getBoundingClientRect(), [v0, v1] = view(); return Math.round(TW.clamp(v0 + (x - r.left) / r.width * (v1 - v0), 0, src.chs[0].length)); };
  const px = i => { const r = el.getBoundingClientRect(), [v0, v1] = view(); return r.left + (i - v0) / (v1 - v0) * r.width; };
  el.addEventListener('pointerdown', e => {
    if (S.menu) return;
    if (!src){ mode = 'import'; return; }
    el.setPointerCapture(e.pointerId); startX = e.clientX;
    const r = el.getBoundingClientRect(), grab = 16 * r.height / el.clientHeight, dA = Math.abs(e.clientX - px(S.sel[0])), dB = Math.abs(e.clientX - px(S.sel[1]));
    // the flags: grab the nearer one; anywhere else moves the cursor (nothing is selected for you)
    if (Math.min(dA, dB) < grab) mode = dA <= dB ? 'in' : 'out';
    else { mode = 'cur'; S.cur = idx(e.clientX); drawWave(); paintLcd(); }
  });
  el.addEventListener('pointermove', e => {
    if (!mode || mode === 'import' || !el.hasPointerCapture(e.pointerId)) return;
    const i = idx(e.clientX), minLen = Math.round(src.sr * .005);
    if (mode === 'in') S.sel[0] = Math.min(i, S.sel[1] - minLen);
    else if (mode === 'out') S.sel[1] = Math.max(i, S.sel[0] + minLen);
    else S.cur = i;
    drawWave(); paintLcd();
  });
  el.addEventListener('pointerup', () => {
    if (mode === 'import'){ mode = null; $('#fileIn').click(); return; }
    if (mode === 'in' || mode === 'out'){ pushSource(); say(`${mode.toUpperCase()} ${(S.sel[mode === 'in' ? 0 : 1] / src.sr).toFixed(3)}S`); }
    mode = null;
  });
}
function setIn(){ if (!needSrc()) return; const c = S.cur == null ? 0 : S.cur; if (c >= S.sel[1] - 32){ say('CURSOR IS PAST OUT'); return; } S.sel[0] = c; pushSource(); drawWave(); say(`IN ${(c / src.sr).toFixed(3)}S`); }
function setOut(){ if (!needSrc()) return; const c = S.cur == null ? src.chs[0].length : S.cur; if (c <= S.sel[0] + 32){ say('CURSOR IS BEFORE IN'); return; } S.sel[1] = c; pushSource(); drawWave(); say(`OUT ${(c / src.sr).toFixed(3)}S`); }

/* ---------------- import ---------------- */
async function decodeFile(file){
  const ab = await file.arrayBuffer();
  // our own readers first for AIFF (Chrome can't decode it) and odd WAVs, then the browser's decoder for the rest
  let r = null;
  try { r = TWC.parseAiff(ab) || null; } catch (e) {}
  if (!r){
    await audio();
    try { const b = await ctx.decodeAudioData(ab.slice(0)); const chs = []; for (let c = 0; c < Math.min(2, b.numberOfChannels); c++) chs.push(b.getChannelData(c).slice()); r = {chs, sr:b.sampleRate}; }
    catch (e){ try { r = TWC.parseWav(ab); } catch (e2) {} }
  }
  if (!r || !r.chs.length || !r.chs[0].length) throw new Error(`Can't read ${file.name}`);
  if (r.chs.length > 2) r.chs = r.chs.slice(0, 2);
  return r;
}
async function importFile(file){
  try {
    say('LOADING…', 20000);
    const r = await decodeFile(file);
    pushUndo(); src = {chs:r.chs, sr:r.sr, name:file.name};
    S.cur = null; afterSourceChange(true);
    S.bpm = null; S.key = null; S.srcBpm = null;
    say(`LOADED ${(r.chs[0].length / r.sr).toFixed(2)}S`, 1500);
    toast(`${file.name} · ${(r.chs[0].length / r.sr).toFixed(2)} s · ${r.sr} Hz · ${r.chs.length === 1 ? 'mono' : 'stereo'}`);
    detect(true);
  } catch (e){ say('LOAD FAILED'); toast(e.message, 4000); }
}
function afterSourceChange(whole){
  if (whole || S.sel[1] > src.chs[0].length || S.sel[1] <= S.sel[0]) S.sel = [0, src.chs[0].length];
  if (S.cur != null && S.cur > src.chs[0].length) S.cur = null;
  buildPeaks(); sendSourceToWorker(); pushSource(); drawWave(); paintLcd();
}
function pushUndo(){ if (src){ undo.push({src, sel:S.sel.slice()}); if (undo.length > 12) undo.shift(); } }
function needSrc(){ if (!src){ say('LOAD A SAMPLE'); return false; } return true; }

/* ---------------- edit ---------------- */
function truncate(){
  if (!needSrc()) return;
  const [a, b] = S.sel; if (a === 0 && b === src.chs[0].length){ say('SET IN / OUT FIRST'); return; }
  pushUndo(); src = {...src, chs:src.chs.map(c => c.slice(a, b))}; S.zoom = false; S.cur = null;
  afterSourceChange(true); say(`TRUNCATED ${((b - a) / src.sr).toFixed(3)}S`);
}
function normalize(){
  if (!needSrc()) return;
  const [a, b] = S.sel; let pk = 1e-9; for (const c of src.chs) for (let i = a; i < b; i++) pk = Math.max(pk, Math.abs(c[i]));
  pushUndo(); const g = .989 / pk; src = {...src, chs:src.chs.map(c => { const d = c.slice(); for (let i = a; i < b; i++) d[i] *= g; return d; })};
  afterSourceChange(false); say(`NORMALIZED ${(20 * Math.log10(g)).toFixed(1)}DB`);
}
function reverse(){
  if (!needSrc()) return;
  const [a, b] = S.sel; pushUndo();
  src = {...src, chs:src.chs.map(c => { const d = c.slice(); d.subarray(a, b).reverse(); return d; })};
  afterSourceChange(false); say('REVERSED');
}
function doUndo(){ const u = undo.pop(); if (!u){ say('NOTHING TO UNDO'); return; } src = u.src; S.sel = u.sel; afterSourceChange(false); say('UNDONE'); }
async function resample(){
  if (!needSrc()) return;
  try {
    say('RESAMPLING…', 30000);
    const r = await ask({type:'render', p:S.p, match:S.match, sel:S.sel, fs:FS()});
    pushUndo();
    src = {chs:r.out, sr:r.fs, name:src.name.replace(/( · resampled)*$/, '') + ' · resampled'};
    // the sound is now in the sample: reset the effects so nothing is applied twice
    const keep = {}; TW.MASTER_IDS.forEach(id => keep[id] = S.p[id]);
    Object.assign(S.p, TW.defaults(), {machine:6, rate:48000, bits:16, ftype:5, mono:0, fenv:0, retrig:0, drive:0, cutoff:20000, res:0}, keep, {mOn:0});
    S.presetName = 'Resampled'; S.preset = -1; S.cur = null;
    pushParams(); afterSourceChange(true); paintAll(); save();
    say('RESAMPLED'); toast('Resampled: effects reset to Clean and the master off, so nothing is applied twice. Undo brings the old sample back.', 4500);
  } catch (e){ say('RESAMPLE FAILED'); toast(e.message); }
}
function selectAll(){ if (!needSrc()) return; S.sel = [0, src.chs[0].length]; S.zoom = false; pushSource(); drawWave(); say('ALL SELECTED'); }
function toggleZoom(){ S.zoom = !S.zoom; drawWave(); say(S.zoom ? 'ZOOM IN' : 'ZOOM OUT', 900); }
function toggleBypass(){ S.bypass = !S.bypass; paintLeds(); say(S.bypass ? 'BYPASS: CLEAN SAMPLE' : 'BYPASS OFF'); if (playing) startMain(playPos()); }

/* ---------------- tempo / key / warp ---------------- */
async function detect(quiet){
  if (!needSrc()) return;
  try {
    if (!quiet) say('DETECTING…', 20000);
    const r = await ask({type:'analyze', sel:S.sel});
    S.bpm = r.bpm; S.key = r.key; S.srcBpm = r.bpm || null;
    if (r.key) S.toKey = NOTE_KEYS.indexOf(r.key.name);
    say(r.bpm ? `${r.bpm.toFixed(1)} BPM ${r.key ? r.key.name.toUpperCase() : ''}` : `KEY ${r.key ? r.key.name.toUpperCase() : 'N/A'}`, 2500);
  } catch (e){ say('DETECT FAILED'); }
}
function warp(){
  const from = S.srcBpm, to = S.tgtBpm;
  if (!(from > 0) || !(to > 0)){ say('DETECT OR SET BPM'); return; }
  if (S.warpMode === 'stretch') setParam('stretch', TW.clamp(from / to, .25, 5));
  else { const c = 1200 * Math.log2(to / from), st = Math.round(c / 100); S.p.cents = TW.clamp(Math.round(c - st * 100), -100, 100); S.p.stretch = 1; setParam('semis', TW.clamp(st, -24, 24)); pushSource(); }
  commit(); say(`WARPED ${from.toFixed(1)}>${to.toFixed(1)}`, 2200);
}
function transpose(){
  if (!S.key){ say('DETECT THE KEY FIRST'); return; }
  const root = Math.floor(S.toKey / 2); let d = (root - S.key.root + 12) % 12; if (d > 6) d -= 12;
  setParam('semis', TW.clamp(d, -24, 24)); commit(); say(`TRANSPOSED ${fmtSt(d)} ST`);
}

/* ---------------- master ---------------- */
async function loadRef(file){
  try { const r = await decodeFile(file); const d = await ask({type:'refspec', chs:r.chs, sr:r.sr, name:file.name}); S.ref = d.bands; S.refName = file.name; say('REFERENCE LOADED'); toast('Reference: ' + file.name); }
  catch (e){ toast(e.message); }
}
async function matchEq(){
  if (!needSrc()) return;
  try {
    say('MATCHING…', 20000);
    const d = await ask({type:'match', p:S.p, sel:S.sel, fs:FS(), ref:S.ref || TW.TARGET_CURVE});
    S.match = d.match; if (!S.p.match) S.p.match = .8; S.p.mOn = 1; pushParams(); paintAll(); commit();
    say(S.ref ? 'EQ MATCHED TO REF' : 'EQ MATCHED');
  } catch (e){ toast(e.message); }
}
async function autoMaster(){
  if (!needSrc()) return;
  try {
    say('AUTO MASTER…', 30000);
    const d = await ask({type:'automaster', p:S.p, sel:S.sel, fs:FS(), ref:S.ref, target:S.lufs});
    Object.assign(S.p, d.params); S.match = d.match; pushParams(); paintAll(); commit();
    say(`MASTERED ${S.lufs} LUFS`); toast(`Auto master: compressor ${d.params.cThr} dB, gain ${d.params.mGain > 0 ? '+' : ''}${d.params.mGain} dB, ceiling ${d.params.lim} dB`);
  } catch (e){ toast(e.message); }
}
function clearMatch(){ S.match = new Array(10).fill(0); S.p.match = 0; pushParams(); paintAll(); commit(); say('MATCH CLEARED'); }
function flatMaster(){ const d = TW.defaults(); TW.MASTER_IDS.forEach(id => S.p[id] = d[id]); S.match = new Array(10).fill(0); pushParams(); paintAll(); commit(); say('MASTER RESET'); }
function toggleMaster(){ setParam('mOn', S.p.mOn > .5 ? 0 : 1); commit(); say(S.p.mOn > .5 ? 'MASTER ON' : 'MASTER OFF'); }

/* ---------------- presets ---------------- */
function applyValues(vals, name, idx){ Object.assign(S.p, vals); S.presetName = name; S.preset = idx; touched = null; lcdParam = null; pushParams(); pushSource(); paintAll(); commit(); }
function loadPreset(i){ const n = PRESETS.length; i = (i + n) % n; const p = PRESETS[i]; applyValues(p.values, p.name, i); say('P' + (i + 1) + ' LOADED', 900); }
function stepPreset(d){ if (S.preset >= 0) loadPreset(S.preset + d); else loadPreset(d > 0 ? 0 : PRESETS.length - 1); }
function randomize(){ const r = TW.randomPatch((Math.random() * 2 ** 32) >>> 0); applyValues(r.values, r.name, -1); say('RANDOM ' + r.name.replace('Random · ', '').toUpperCase()); }
// everything back to how it starts: all settings, the EQ match and the pad pitches (the sample stays). The first
// press asks, the second within two seconds does it.
let resetArm = 0;
function resetAll(){
  const now = Date.now();
  if (now - resetArm > 2000){ resetArm = now; $('#kReset').classList.add('arm'); setTimeout(() => $('#kReset').classList.remove('arm'), 2000); say('PRESS RESET AGAIN', 2000); return; }
  resetArm = 0; $('#kReset').classList.remove('arm');
  S.match = new Array(10).fill(0); S.pads = BANK_DEF.flat(); S.bypass = false;
  applyValues(TW.defaults(), 'Init', -1); say('RESET TO DEFAULT');
}
function initSound(){ const keep = {}; TW.MASTER_IDS.forEach(id => keep[id] = S.p[id]); applyValues(Object.assign(TW.defaults(), keep), 'Init', -1); }
function savePreset(){
  const name = prompt('Name this preset', S.presetName.replace(/\*$/, '') + ' (mine)'); if (!name) return;
  const values = {}; TW.SOUND_IDS.forEach(id => values[id] = S.p[id]);
  S.user = S.user.filter(u => u.name !== name); S.user.push({name, values, family:'My presets'});
  S.presetName = name; S.preset = -1; paintAll(); save(); say('SAVED');
}
let famSel = 'All', pPage = 0;
const PER_PAGE = 16;
function openPresets(){
  const fams = ['All', ...(S.user.length ? ['My presets'] : []), ...FAMS], box = $('#pFams');
  box.innerHTML = '';
  for (const f of fams){ const c = document.createElement('button'); c.className = 'chip' + (f === famSel ? ' on' : ''); c.textContent = f; c.onclick = () => { famSel = f; pPage = 0; openPresets(); }; box.appendChild(c); }
  listPresets(); $('#presetDlg').open || $('#presetDlg').showModal();
}
function presetHits(){
  const q = $('#pSearch').value.trim().toLowerCase();
  const all = [...S.user.map(u => ({...u, user:true})), ...PRESETS.map((p, i) => ({...p, i}))];
  return all.filter(p => (famSel === 'All' || p.family === famSel) && (!q || p.name.toLowerCase().includes(q)));
}
function listPresets(){
  const hits = presetHits(), list = $('#pList'), pages = Math.max(1, Math.ceil(hits.length / PER_PAGE)); pPage = TW.clamp(pPage, 0, pages - 1);
  list.innerHTML = ''; $('#pCount').textContent = hits.length + ' presets'; $('#pPage').textContent = `${pPage + 1} / ${pages}`;
  for (const p of hits.slice(pPage * PER_PAGE, pPage * PER_PAGE + PER_PAGE)){
    const b = document.createElement('button'); b.textContent = (p.user ? '' : (p.i + 1) + ' ') + p.name; if (p.i === S.preset) b.className = 'cur';
    b.onclick = () => { if (p.user) applyValues(p.values, p.name, -1); else loadPreset(p.i); listPresets(); };
    list.appendChild(b);
  }
}

/* ---------------- export ---------------- */
let recorder = null, recorderCancelled = false, lastFile = null;
function deviceTypes(){
  if (!window.MediaRecorder) return [];
  return [['audio/ogg;codecs=opus', 'OGG OPUS', 'ogg'], ['audio/webm;codecs=opus', 'WEBM OPUS', 'webm'], ['audio/mp4;codecs=mp4a.40.2', 'M4A AAC', 'm4a'], ['audio/mp4', 'M4A', 'm4a']]
    .filter(([t]) => MediaRecorder.isTypeSupported(t));
}
function allFormats(){ const seen = new Set(); return FORMATS.concat(deviceTypes().filter(([, n]) => !seen.has(n) && seen.add(n)).map(([t, n, ext]) => ['dev:' + t + '|' + ext, n])); }
async function doExport(){
  if (!needSrc()) return;
  const fmt = S.fmt, fs = S.expSr, sel = S.expRange === 'all' ? [0, src.chs[0].length] : S.sel;
  const base = src.name.replace(/\.[^.]+$/, '').replace(/( · resampled)+/, '-resampled') + ' - ' + S.presetName.replace(/\*$/, '').replace(/[\\/:*?"<>|]/g, '');
  try {
    let blob, name;
    say('EXPORTING…', 60000);
    if (fmt.startsWith('dev:')){
      const [type, ext] = fmt.slice(4).split('|');
      await audio();
      const r = await ask({type:'export', p:S.p, match:S.match, sel, fs:FS(), fmt:'wav:f32'});
      const pcm = TWC.parseWav(r.bytes.buffer);
      blob = await recordDevice(pcm.chs, pcm.sr, type); if (!blob) return; name = base + '.' + ext;
    } else {
      if (fmt.startsWith('mp3') && ![32000, 44100, 48000].includes(fs)){ say('MP3: 32 / 44.1 / 48K'); return; }
      const r = await ask({type:'export', p:S.p, match:S.match, sel, fs, fmt});
      blob = new Blob([r.bytes], {type:r.mime}); name = base + '.' + r.ext;
    }
    lastFile = new File([blob], name, {type:blob.type});
    if (window.AndroidTwelve){
      // the Android app: no browser downloads, so hand the file to the app, which puts it in Downloads
      const b64 = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
      const where = AndroidTwelve.save(b64, name, blob.type || 'application/octet-stream');
      if (where.startsWith('error')) throw new Error(where.slice(7));
      say('SAVED'); toast(`Saved to ${where} (${(blob.size / 1048576).toFixed(2)} MB)`, 5000); return;
    }
    const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
    say('SAVED'); toast(`Saved ${name} (${(blob.size / 1048576).toFixed(2)} MB)`);
  } catch (e){ say('EXPORT FAILED'); toast(e.message, 4000); }
}
function share(){ if (lastFile && navigator.canShare && navigator.canShare({files:[lastFile]})) navigator.share({files:[lastFile], title:lastFile.name}).catch(() => {}); else say('EXPORT FIRST'); }
function recordDevice(chs, sr, type){
  return new Promise((resolve, reject) => {
    const c = ctx, dest = c.createMediaStreamDestination(), b = c.createBuffer(chs.length, chs[0].length, sr);
    chs.forEach((d, i) => b.copyToChannel(d, i));
    const s = c.createBufferSource(); s.buffer = b; s.connect(dest);
    const parts = []; recorderCancelled = false;
    recorder = new MediaRecorder(dest.stream, {mimeType:type, audioBitsPerSecond:256000});
    recorder.ondataavailable = e => e.data.size && parts.push(e.data);
    recorder.onstop = () => { s.disconnect(); if (recorderCancelled){ say('EXPORT STOPPED'); resolve(null); } else resolve(new Blob(parts, {type:type.split(';')[0]})); };
    recorder.onerror = e => reject(e.error || new Error('Recorder failed'));
    s.onended = () => setTimeout(() => recorder.state !== 'inactive' && recorder.stop(), 250);
    recorder.start(); s.start(c.currentTime + .05);
    say(`RECORDING ${b.duration.toFixed(1)}S`, b.duration * 1000 + 1500);
  });
}

/* ---------------- menus on the display ---------------- */
// [number, name, action, current value (shown beside it)]
const cyc = (list, cur, d) => list[(list.indexOf(cur) + d + list.length) % list.length];
const choice = (label, get, set) => () => { S.edit = {label, show:get, step:d => { set(d); save(); }}; };
const MENUS = {
  setup:{title:'SET-UP', items:[
    ...PAGES.map((pg, i) => [i + 1, pg.name, () => { setPage(i); closeMenu(); }]),
    [9, 'Calibration', () => { setParam('calib', S.p.calib > .5 ? 0 : 1); commit(); }, () => S.p.calib > .5 ? 'MEASURED' : 'MANUAL'],
    [0, 'Help', () => { closeMenu(); $('#helpDlg').showModal(); }]]},
  disk:{title:'DISK', items:[
    [1, 'Load Sample', () => { closeMenu(); $('#fileIn').click(); }],
    [2, 'Export Audio', () => { closeMenu(); doExport(); }],
    [3, 'Format', choice('FORMAT', () => (allFormats().find(f => f[0] === S.fmt) || FORMATS[0])[1], d => { S.fmt = cyc(allFormats().map(f => f[0]), S.fmt, d); }), () => (allFormats().find(f => f[0] === S.fmt) || FORMATS[0])[1]],
    [4, 'Rate', choice('RATE', () => (S.expSr / 1000).toFixed(S.expSr % 1000 ? 2 : 0) + 'K', d => { S.expSr = cyc(RATES, S.expSr, d); }), () => (S.expSr / 1000).toFixed(S.expSr % 1000 ? 1 : 0) + 'K'],
    [5, 'Range', choice('RANGE', () => S.expRange === 'all' ? 'WHOLE' : 'IN-OUT', () => { S.expRange = S.expRange === 'all' ? 'sel' : 'all'; }), () => S.expRange === 'all' ? 'WHOLE' : 'IN-OUT'],
    [6, 'Presets', () => { closeMenu(); openPresets(); }],
    [7, 'Save Preset', () => { closeMenu(); savePreset(); }],
    [8, 'Random', () => randomize()],
    [9, 'Share', () => share()],
    [0, 'Init Sound', () => { initSound(); say('INIT'); }]]},
  sync:{title:'SYNC · MIDI · TV', items:[]},   // filled by io.js
  sample:{title:'SAMPLE', items:[
    [1, 'Load Sample', () => { closeMenu(); $('#fileIn').click(); }],
    [2, 'Set In', setIn], [3, 'Set Out', setOut], [4, 'Select All', selectAll], [5, 'Truncate', truncate],
    [6, 'Normalize', normalize], [7, 'Reverse', reverse], [8, 'Re-Sample', resample], [9, 'Zoom', toggleZoom, () => S.zoom ? 'ON' : ''],
    [0, 'Undo', doUndo]]},
  tempo:{title:'TEMPO · KEY', items:[
    [1, 'Detect', () => detect(false), () => S.bpm ? S.bpm.toFixed(1) : ''],
    [2, 'Sample BPM', () => entry('SAMPLE BPM', v => { S.srcBpm = v; }), () => S.srcBpm ? S.srcBpm.toFixed(1) : '--'],
    [3, 'Half', () => { if (S.srcBpm) S.srcBpm /= 2; say(`SAMPLE ${S.srcBpm ? S.srcBpm.toFixed(1) : '--'} BPM`); }],
    [4, 'Double', () => { if (S.srcBpm) S.srcBpm *= 2; say(`SAMPLE ${S.srcBpm ? S.srcBpm.toFixed(1) : '--'} BPM`); }],
    [5, 'Target BPM', () => entry('TARGET BPM', v => { S.tgtBpm = v; save(); }), () => S.tgtBpm.toFixed(1)],
    [6, 'Warp Mode', choice('WARP', () => S.warpMode === 'stretch' ? 'STRETCH' : 'SPEED', () => { S.warpMode = S.warpMode === 'stretch' ? 'speed' : 'stretch'; }), () => S.warpMode === 'stretch' ? 'STRETCH' : 'SPEED'],
    [7, 'Warp', () => warp()],
    [8, 'To Key', choice('KEY', () => NOTE_KEYS[S.toKey].toUpperCase(), d => { S.toKey = (S.toKey + d + 24) % 24; }), () => NOTE_KEYS[S.toKey].replace(' major', '').replace(' minor', 'm')],
    [9, 'Transpose', () => transpose()],
    [0, 'Loudness', choice('LOUDNESS', () => S.lufs + ' LUFS', d => { S.lufs = cyc(LUFS, S.lufs, d); }), () => S.lufs + '']]}
};
// programming keys: [Edit label, action, Master label, action]
const PROG = [
  ['Set In', setIn, 'Auto Master', autoMaster], ['Set Out', setOut, 'Match EQ', matchEq], ['Truncate', truncate, 'Reference', () => $('#refIn').click()],
  ['Normalize', normalize, 'Clear Match', clearMatch], ['Reverse', reverse, 'Master On/Off', toggleMaster], ['Undo', doUndo, 'Master Reset', flatMaster],
  ['Re-Sample', resample, 'Random Preset', randomize], ['Bypass', toggleBypass, 'Help', () => $('#helpDlg').showModal()]
];
function openMenu(k, keep){
  S.edit = null; S.entry = null;
  if (S.menu === k && !keep) S.menu = null; else S.menu = k;
  if (k === 'sync' && window.IO) MENUS.sync.items = IO.items();
  lcdParam = null; touched = null; paintLeds(); paintLcd(); drawWave();
}
function closeMenu(){ S.menu = null; S.edit = null; S.entry = null; paintLeds(); paintLcd(); drawWave(); }
function runItem(n){
  const m = S.menu && MENUS[S.menu]; if (!m) return;
  const it = m.items.find(x => x[0] === n); if (!it) return;
  S.edit = null; it[2](); if (S.menu === 'sync' && window.IO) MENUS.sync.items = IO.items();
  paintLcd();
}
// typing a number on the keypad
function entry(label, done){ S.entry = {label, text:'', done}; paintLcd(); }
function keypad(k){
  if (S.entry){
    if (k === 'Yes'){ const v = parseFloat(S.entry.text); const e = S.entry; S.entry = null; if (v > 0) e.done(v); paintLcd(); }
    else if (k === 'No'){ if (S.entry.text) S.entry.text = S.entry.text.slice(0, -1); else S.entry = null; paintLcd(); }
    else if (S.entry.text.length < 6){ S.entry.text += k; paintLcd(); }
    return;
  }
  if (S.edit){ if (k === 'Yes' || k === 'No'){ S.edit = null; paintLcd(); } return; }
  if (S.menu){ if (k === 'No') closeMenu(); else if (k !== 'Yes') runItem(+k); return; }
  // no menu: a preset number
  if (k === 'No' || k === 'Yes') return;
  entry('PRESET #', v => { if (v >= 1 && v <= PRESETS.length) loadPreset(Math.round(v) - 1); else say('NO SUCH PRESET'); });
  S.entry.text = k; paintLcd();
}
function arrow(d){
  if (S.edit){ S.edit.step(d); paintLcd(); return; }
  if (S.entry || S.menu) return;
  stepPreset(d);
}
function enter(){
  if (S.entry){ keypad('Yes'); return; }
  if (S.edit){ S.edit = null; paintLcd(); return; }
  if (S.menu){ closeMenu(); return; }
  openPresets();
}

/* ---------------- fit the panel to the screen ---------------- */
let panelScale = 1;
function fit(){
  const sp = $('#sp'), vw = innerWidth, vh = innerHeight, tall = vw / vh < .9;
  sp.classList.toggle('tall', tall); sp.classList.toggle('wide', !tall);
  const W = tall ? 400 : 1200, H = tall ? 842 : 1000, k = Math.min(vw / W, vh / H);
  panelScale = k;
  sp.style.transform = `translate(${(vw - W * k) / 2}px, ${(vh - H * k) / 2}px) scale(${k})`;
  drawWave();
}

/* ---------------- wiring ---------------- */
function wire(){
  $('#fileIn').onchange = e => { const f = e.target.files[0]; if (f) importFile(f); e.target.value = ''; };
  $('#refIn').onchange = e => { const f = e.target.files[0]; if (f) loadRef(f); e.target.value = ''; };
  $('#pClose').onclick = () => $('#presetDlg').close(); $('#pSearch').oninput = () => { pPage = 0; listPresets(); };
  $('#pPrevPg').onclick = () => { pPage--; listPresets(); }; $('#pNextPg').onclick = () => { pPage++; listPresets(); };
  $('#helpClose').onclick = () => $('#helpDlg').close();
  addEventListener('dragover', e => e.preventDefault());
  addEventListener('drop', e => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) importFile(f); });
  // keys: space run / stop, Escape stops everything, arrows change preset, 1-8 play the pads
  addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' || $('#presetDlg').open || $('#helpDlg').open) return;
    if (e.key === 'Escape') stopAll();
    else if (e.key === ' '){ e.preventDefault(); run(); }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') arrow(e.key === 'ArrowLeft' ? -1 : 1);
    else if (/^[1-8]$/.test(e.key) && !e.repeat) hitPad(S.bank * 8 + +e.key - 1);
  });
  addEventListener('pointerdown', () => audio(), {once:true, capture:true});
  addEventListener('resize', fit);
  // long presses: Android buzzes when a long press starts its gesture, so cancel the touch on everything played by
  // pointer events. Nothing is selectable, and only the mouse's right-click menu is blocked.
  document.addEventListener('touchstart', e => { const t = e.target; if (t.closest && t.closest('.pad, .knob, .fader, #waveC')) e.preventDefault(); }, {capture:true, passive:false});
  document.addEventListener('contextmenu', e => { if (!(e.target.closest && e.target.closest('input'))) e.preventDefault(); }, {capture:true});
}
// MIDI learn hook (io.js replaces it): true when the touch was taken to arm learning
let learnHook = () => false;

load(); buildPanel(); wire(); setupWave(); fit(); paintAll();
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidTwelve) navigator.serviceWorker.register('sw.js').catch(() => {});
