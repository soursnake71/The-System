/* Omega 110: the front panel's behaviour (modes, SHIFT functions, pattern / song writing), the mixer, the effects
   mixer and the audio. The sound itself runs in an AudioWorklet (worklet.js: engine.js + seq.js).
   io.js adds import / export and MIDI on top of this (it uses window.DRAPP). */
'use strict';
const {P, PARAMS, NV, VOICES, VOICE_NAMES, RV_PROGS, vp, defaults} = DR110;
const ACC = DRSEQ.ACC;
const VNAME_SHORT = ['BASS', 'SNARE', 'O.HAT', 'C.HAT', 'CYMBAL', 'CLAP', 'KNUCKLE'];
const STORE = 'omega-dr110-v1';
const keyVoice = {};                              // which voice a held key started (for the ADSR release)

/* ---------------- state ---------------- */
const S = {
  power:false, mode:'pattern', bank:0, rhythm:0, song:0, measure:0, cursor:0, running:false,
  shiftHeld:false, shiftLatch:false, shiftUsed:false, accHeld:false,
  params:defaults(), mem:DRSEQ.defaultMemory(), solo:new Array(NV).fill(0), swing:0,
  play:{step:-1, pat:0, measure:0}, flash:null, clearArm:0, candidate:0, userPresets:[], preset:0,
  midi:{in:'all', out:'', ch:9, sync:false, clockOut:false, notesOut:true, accVel:100, pc:true, notes:[36, 38, 46, 42, 49, 39, 37]},
};
function load(){
  try {
    const j = JSON.parse(localStorage.getItem(STORE) || 'null'); if (!j) return;
    applyBackup(j, true);
  } catch (e){ console.warn(e); }
}
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify(backup())); } catch (e){} }, 300); }
function backup(){ return {format:'omega-dr110', version:1, mem:S.mem, params:S.params, solo:S.solo, swing:S.swing, midi:S.midi, bank:S.bank, rhythm:S.rhythm, song:S.song, userPresets:S.userPresets, preset:S.preset}; }
function applyBackup(j, quiet){
  if (!j || j.format !== 'omega-dr110') throw new Error('Not an Omega 110 backup');
  if (j.mem && Array.isArray(j.mem.pats) && j.mem.pats.length === 32) S.mem = j.mem;
  if (Array.isArray(j.params)) j.params.forEach((v, i) => { if (i < PARAMS.length && typeof v === 'number') S.params[i] = v; });
  if (Array.isArray(j.solo)) S.solo = j.solo.slice(0, NV);
  if (typeof j.swing === 'number') S.swing = j.swing;
  if (j.midi) Object.assign(S.midi, j.midi);
  if (Array.isArray(j.userPresets)) S.userPresets = j.userPresets.filter(u => u && typeof u.name === 'string' && Array.isArray(u.p));
  if (typeof j.preset === 'number') S.preset = j.preset;
  if (typeof j.bank === 'number') S.bank = j.bank; if (typeof j.rhythm === 'number') S.rhythm = j.rhythm; if (typeof j.song === 'number') S.song = j.song;
  if (!quiet){ sendAll(); refreshPanels(); draw(); }
}
const patIndex = () => S.bank * 8 + S.rhythm;
const curPat = () => S.mem.pats[S.running ? S.play.pat : patIndex()];
const userPat = i => i >= 16;

/* ---------------- audio ---------------- */
let ctx = null, node = null, fallback = null;
const listeners = [];
function post(m){ if (node) node.port.postMessage(m); else if (fallback) fallback(m); }
// what the engine gets: the mixer's mutes with the solos folded in
function engineParams(){
  const p = S.params.slice(), anySolo = S.solo.some(Boolean);
  for (let v = 0; v < NV; v++) if (anySolo && !S.solo[v]) p[vp(v, 4)] = 1;
  return p;
}
function sendAll(){ post({type:'params', p:engineParams()}); post({type:'state', mem:S.mem}); post({type:'swing', v:S.swing}); }
function setParam(i, v){ S.params[i] = v; post({type:'params', p:engineParams()}); save(); }
async function startAudio(){
  if (ctx) { if (ctx.state !== 'running') await ctx.resume(); return; }
  ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
  try {
    await ctx.audioWorklet.addModule('engine.js');
    await ctx.audioWorklet.addModule('seq.js');
    await ctx.audioWorklet.addModule('worklet.js');
    node = new AudioWorkletNode(ctx, 'dr110', {numberOfInputs:0, outputChannelCount:[2]});
    node.port.onmessage = ({data}) => onAudio(data);
    node.connect(ctx.destination);
  } catch (e){
    // no AudioWorklet: run the same engine on the main thread
    console.warn('AudioWorklet unavailable, using ScriptProcessor', e);
    const eng = new DR110.Engine(ctx.sampleRate), seq = new DRSEQ.Sequencer(eng, ctx.sampleRate);
    seq.onStep = (step, pat, measure) => onAudio({type:'step', step, pat, measure});
    seq.onHit = (v, acc) => onAudio({type:'hit', v, acc}); seq.onStop = () => onAudio({type:'stop'}); seq.onClock = () => onAudio({type:'clock'});
    const sp = ctx.createScriptProcessor(1024, 0, 2); let L = new Float64Array(1024), R = new Float64Array(1024), mt = 0;
    sp.onaudioprocess = ev => { const n = ev.outputBuffer.length; seq.render(L, R, 0, n); ev.outputBuffer.getChannelData(0).set(L.subarray(0, n)); ev.outputBuffer.getChannelData(1).set(R.subarray(0, n));
      if (++mt % 2 === 0){ onAudio({type:'meter', m:Array.from(eng.meter)}); eng.meter.fill(0); } };
    sp.connect(ctx.destination);
    fallback = m => { switch (m.type){
      case 'state': seq.mem = m.mem; break; case 'params': eng.setAll(m.p); seq.bpm = m.p[P.tempo]; break;
      case 'start': seq.mode = m.mode; if (m.song !== undefined) seq.song = m.song; if (m.pat !== undefined) seq.pat = m.pat; seq.start(); if (!seq.running) onAudio({type:'stop'}); break;
      case 'stop': seq.stop(); break; case 'select': seq.select(m.pat); break; case 'hit': eng.trigger(m.v, m.acc); break;
      case 'tap': { eng.trigger(m.v, m.acc); const w = seq.tapWrite(m.v, m.acc); if (w) onAudio(Object.assign({type:'write'}, w)); break; }
      case 'ext': seq.ext = m.on; break; case 'clock': seq.clockTick(); break; case 'swing': seq.swing = m.v; break; case 'sweep': eng.sweep(); break; case 'release': eng.release(m.v); break;
      case 'sample': eng.setSample(m.v, m.d, m.sr, m.acc); break; } };
  }
  sendAll();
  loadRecordings();
}
function onAudio(m){
  switch (m.type){
    case 'step': S.play = {step:m.step, pat:m.pat, measure:m.measure}; if (S.mode === 'song' && S.running) S.measure = m.measure; draw(); break;
    case 'stop': S.running = false; S.play.step = -1; draw(); break;
    case 'write': S.mem.pats[m.pat].st[m.step] = m.bits; save(); draw(); break;
    case 'meter': showMeters(m.m); break;
  }
  for (const f of listeners) f(m);
}

/* the built-in recordings (dr110/samples): played when SOURCE is Recordings */
const SAMPLE_FILES = [['bd.wav'], ['sd.wav'], ['oh.wav', 'oh-long.wav'], ['ch.wav', 'ch-long.wav'], ['cy.wav'], ['hc.wav'], []];
const samples = new Array(NV).fill(null);       // {d, sr, acc, name}
async function decode(buf){ const a = await ctx.decodeAudioData(buf); const d = new Float32Array(a.length);
  for (let c = 0; c < a.numberOfChannels; c++){ const x = a.getChannelData(c); for (let i = 0; i < a.length; i++) d[i] += x[i] / a.numberOfChannels; } return {d, sr:a.sampleRate}; }
async function loadRecordings(only){
  for (let v = 0; v < NV; v++){
    if (only !== undefined && v !== only) continue;
    const f = SAMPLE_FILES[v]; if (!f.length){ samples[v] = null; post({type:'sample', v, d:null}); continue; }
    try {
      const a = await decode(await (await fetch('samples/' + f[0])).arrayBuffer());
      const b = f[1] ? await decode(await (await fetch('samples/' + f[1])).arrayBuffer()) : null;
      samples[v] = {d:a.d, sr:a.sr, acc:b ? b.d : null, name:'built-in'};
      post({type:'sample', v, d:a.d, sr:a.sr, acc:samples[v].acc});
    } catch (e){ console.warn('sample', f, e); }
  }
  if (window.DRIO) DRIO.sampleRowsRefresh();
}

/* ---------------- the front panel ---------------- */
const plate = document.getElementById('plate');
const UI = FP.build(plate);
let blink = true;
setInterval(() => { blink = !blink; if (S.power && (S.mode === 'step' || S.flash)) draw(); }, 330);

function modeLines(){
  if (S.clearArm > Date.now()) return ['CLEAR?', 'PRESS 7'];
  if (S.flash && S.flash.until > Date.now()) return S.flash.lines;
  switch (S.mode){
    case 'pattern': return ['PLAY', 'PATTERN'];
    case 'song': return ['PLAY', 'SONG'];
    case 'wsong': return ['WRITE', 'SONG'];
    case 'step': return ['WRITE', 'STEP'];
    case 'tap': return ['WRITE', 'TAP'];
  }
  return [];
}
function flash(lines, ms = 1200){ S.flash = {lines, until:Date.now() + ms}; setTimeout(draw, ms + 20); draw(); }
let tempoShow = 0;
function draw(){
  UI.powerKnob.setAttribute('x', S.power ? 1702 : 1664);
  const pi = S.running ? S.play.pat : (S.mode === 'wsong' ? songShownPat() : patIndex());
  const pt = S.mem.pats[pi] || DRSEQ.emptyPattern();
  let mt = '   ';
  if (tempoShow > Date.now()) mt = String(Math.round(S.params[P.tempo])).padStart(3, ' ');
  else if (S.mode === 'song' || S.mode === 'wsong'){ mt = String(S.measure + 1).padStart(3, ' '); if (S.mode === 'wsong' && S.measure >= S.mem.songs[S.song].m.length) mt = S.mem.songs[S.song].dc && S.measure === S.mem.songs[S.song].m.length && S.measure > 0 ? ' dC' : String(S.measure + 1).padStart(3, ' '); }
  FP.drawLcd(UI.lcd, {
    power:S.power, len:pt.len, steps:pt.st, playStep:S.running ? S.play.step : -1,
    cursor:S.mode === 'step' ? S.cursor : -1, blink, song:(S.mode === 'song' || S.mode === 'wsong') ? S.song : -1,
    measureText:mt, bank:'AbCd'[Math.floor(pi / 8)], rhythm:String(pi % 8 + 1),
    modeLines:modeLines(), shift:S.shiftHeld || S.shiftLatch, running:S.running,
  });
  for (const [id, k] of Object.entries(UI.knobs)){
    const n = knobNorm(id);
    k.ptr.setAttribute('transform', `rotate(${-135 + 270 * n} ${k.cx} ${k.cy})`);
  }
}
// in song write the display shows the pattern stored at this measure, or the one you're about to enter
function songShownPat(){ const s = S.mem.songs[S.song]; return S.measure < s.m.length && !S.pickedSinceMove ? s.m[S.measure] : patIndex(); }

/* knobs: TEMPO 40-250 BPM (log), BALANCE -5..5, ACCENT 0..10, VOLUME 0..10 */
const KN = {tempo:P.tempo, balance:P.balance, accent:P.accent, volume:P.volume};
function knobNorm(id){
  const v = S.params[KN[id]], q = PARAMS[KN[id]];
  if (id === 'tempo') return Math.log(v / q[2]) / Math.log(q[3] / q[2]);
  return (v - q[2]) / (q[3] - q[2]);
}
function setKnobNorm(id, n){
  n = Math.max(0, Math.min(1, n)); const q = PARAMS[KN[id]];
  let v = id === 'tempo' ? q[2] * Math.pow(q[3] / q[2], n) : q[2] + n * (q[3] - q[2]);
  if (id === 'tempo') v = Math.round(v); else v = Math.round(v * 10) / 10;
  setParam(KN[id], v);
  if (id === 'tempo'){ tempoShow = Date.now() + 1500; setTimeout(draw, 1550); }
  draw();
}
function dragKnob(elm, getN, setN, dflt){
  let y0 = 0, n0 = 0, last = 0;
  elm.addEventListener('pointerdown', e => {
    e.preventDefault(); elm.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = getN();
    const now = Date.now(); if (now - last < 300 && dflt !== undefined) setN(dflt); last = now;
  });
  elm.addEventListener('pointermove', e => {
    if (!elm.hasPointerCapture(e.pointerId)) return;
    const h = elm.getBoundingClientRect().height || 60;
    setN(n0 + (y0 - e.clientY) / (h * 2.6));
  });
  elm.addEventListener('wheel', e => { e.preventDefault(); setN(getN() - Math.sign(e.deltaY) * 0.02); }, {passive:false});
}
for (const [id, k] of Object.entries(UI.knobs)){
  const q = PARAMS[KN[id]], dn = id === 'tempo' ? Math.log(q[4] / q[2]) / Math.log(q[3] / q[2]) : (q[4] - q[2]) / (q[3] - q[2]);
  dragKnob(k.g, () => knobNorm(id), n => { if (S.power) setKnobNorm(id, n); }, dn);
}

UI.power.addEventListener('pointerdown', async e => {
  e.preventDefault();
  S.power = !S.power;
  if (S.power){ await startAudio(); flash(['OMEGA', '110'], 900); }
  else { post({type:'stop'}); S.running = false; S.shiftLatch = false; }
  draw();
});

/* keys */
const isShift = () => S.shiftHeld || S.shiftLatch;
function useShift(){ S.shiftUsed = true; if (S.shiftLatch){ S.shiftLatch = false; } }
function press(id){
  if (!S.power) return;
  if (id === 'shift'){ S.shiftHeld = true; S.shiftUsed = false; draw(); return; }
  if (id === 'acc'){ S.accHeld = true; if (S.mode === 'step' && !isShift()) toggleBit(S.cursor, ACC); return; }
  const sh = isShift();
  if (sh && id !== 'start' && id !== 'stop') useShift();
  if (id === 'bank'){
    if (sh){ S.song = 1 - S.song; S.measure = 0; flash(['SONG', S.song ? 'II' : 'I']); }
    else { S.bank = (S.bank + 1) % 4; picked(); }
  } else if (/^n\d$/.test(id)){
    const n = +id[1];
    if (sh) shiftNumber(n);
    else { S.rhythm = n - 1; picked(); }
  } else if (id === 'start') startKey();
  else if (id === 'stop') stopKey();
  else if (/^v\d$/.test(id)){
    let v = +id[1];
    if (sh){
      if (v === 0) return measureMove(-1);
      if (v === 1) return measureMove(1);
      if (v === 2) return enterKey();
      if (v === 3) return dcKey();
      if (v === 5) v = 6;                          // SHIFT + HAND CLAP: KNUCKLE
      else if (v !== 4) return;
    }
    keyVoice[id] = v;
    voiceKey(v);
  }
  draw();
}
function release(id){
  if (id === 'shift'){
    S.shiftHeld = false;
    // the "split" SHIFT: tapped on its own it latches for the next key, tapped again it lets go
    if (!S.shiftUsed) S.shiftLatch = !S.shiftLatch;
    draw();
  }
  if (id === 'acc') S.accHeld = false;
  if (/^v\d$/.test(id) && keyVoice[id] !== undefined){ post({type:'release', v:keyVoice[id]}); delete keyVoice[id]; }
}
function picked(){
  const i = patIndex();
  if (S.mode === 'wsong') S.pickedSinceMove = true;
  if ((S.mode === 'step' || S.mode === 'tap') && !userPat(i)){ S.bank = 2 + (S.bank & 1); flash(['WRITE', 'C / D ONLY']); }
  post({type:'select', pat:patIndex()});
  if (S.running && S.mode !== 'pattern' && S.mode !== 'tap'){}       // song playback ignores picks
}
function shiftNumber(n){
  const wasRunning = S.running;
  switch (n){
    case 1: setMode('song'); break;
    case 2: setMode('pattern'); break;
    case 3: setMode('wsong'); break;
    case 4: setMode('step'); break;
    case 5: setMode('tap'); break;
    case 6: { const i = patIndex(); if (!userPat(i)) return flash(['12/16', 'C / D ONLY']);
      const pt = S.mem.pats[i]; pt.len = pt.len === 16 ? 12 : 16; if (S.cursor >= pt.len) S.cursor = 0; memChanged(); flash(['STEPS', String(pt.len)]); break; }
    case 7: { const i = patIndex(); if (!userPat(i)) return flash(['CLEAR', 'C / D ONLY']);
      if (S.clearArm > Date.now()){ S.mem.pats[i] = DRSEQ.emptyPattern(); S.clearArm = 0; memChanged(); flash(['PATTERN', 'CLEARED']); }
      else { S.clearArm = Date.now() + 2500; setTimeout(draw, 2550); }
      break; }
    case 8: S.measure = 0; S.pickedSinceMove = false; flash(['MEASURE', 'RESET']); break;
  }
  void wasRunning;
}
function setMode(m){
  if (S.running) stopKey();
  S.mode = m; S.measure = 0; S.cursor = 0; S.pickedSinceMove = false;
  if ((m === 'step' || m === 'tap') && !userPat(patIndex())){ S.bank = 2 + (S.bank & 1); post({type:'select', pat:patIndex()}); }
  post({type:'mode', mode:m === 'song' ? 'song' : 'pattern'});
}
function startKey(){
  if (S.mode === 'step'){ const len = S.mem.pats[patIndex()].len; S.cursor = (S.cursor + 1) % len; return; }
  if (S.mode === 'wsong') return;
  if (S.running) return;
  if (S.midi.sync && window.DRIO && DRIO.syncing()) return flash(['WAITING', 'MIDI CLOCK']);
  S.running = true;
  S.play = {step:-1, pat:patIndex(), measure:0};
  post({type:'start', mode:S.mode === 'song' ? 'song' : 'pattern', song:S.song, pat:patIndex()});
  if (window.DRIO) DRIO.transport(true);
}
function stopKey(){
  if (S.mode === 'step'){ const len = S.mem.pats[patIndex()].len; S.cursor = (S.cursor - 1 + len) % len; return; }
  post({type:'stop'}); S.running = false; S.play.step = -1;
  if (window.DRIO) DRIO.transport(false);
}
function measureMove(d){
  if (S.mode !== 'wsong' && S.mode !== 'song') return;
  const s = S.mem.songs[S.song];
  S.measure = Math.max(0, Math.min(Math.min(s.m.length, DRSEQ.MAX_MEASURES - 1), S.measure + d));
  S.pickedSinceMove = false; draw();
}
function enterKey(){
  if (S.mode !== 'wsong') return;
  const s = S.mem.songs[S.song];
  if (S.measure >= DRSEQ.MAX_MEASURES) return flash(['SONG', 'FULL']);
  const p = patIndex();
  if (S.measure < s.m.length) s.m[S.measure] = p; else s.m.push(p);
  S.measure++; S.pickedSinceMove = false; memChanged();
}
function dcKey(){
  if (S.mode !== 'wsong') return;
  const s = S.mem.songs[S.song];
  if (s.m.length === S.measure && s.dc){ s.dc = false; flash(['SONG END', 'NO REPEAT']); }
  else { s.m.length = Math.min(s.m.length, S.measure); s.dc = true; flash(['D.C.', 'REPEAT']); }
  memChanged();
}
function voiceKey(v){
  const acc = S.accHeld;
  if (S.mode === 'step'){ toggleBit(S.cursor, 1 << v); post({type:'hit', v, acc:!!(S.mem.pats[patIndex()].st[S.cursor] & ACC)}); }
  else if (S.mode === 'tap' && S.running) post({type:'tap', v, acc});
  else post({type:'hit', v, acc});
  if (window.DRIO) DRIO.noteOut(v, acc);
}
function toggleBit(step, bit){
  const i = patIndex(); if (!userPat(i)) return flash(['WRITE', 'C / D ONLY']);
  S.mem.pats[i].st[step] ^= bit; memChanged();
}
function memChanged(){ post({type:'state', mem:S.mem}); save(); draw(); }

// pointer handling for keys (multi-touch: SHIFT + key, ACCENT + voice)
const downKeys = new Map();
plate.addEventListener('pointerdown', e => {
  const key = e.target.closest('.key'), cell = e.target.closest('.cell');
  if (key){ e.preventDefault(); const id = key.dataset.b; downKeys.set(e.pointerId, {id, key}); key.classList.add('down'); press(id); }
  else if (cell && S.power){
    e.preventDefault();
    const step = +cell.dataset.step, row = +cell.dataset.row, i = S.running ? S.play.pat : patIndex();
    if (!userPat(i) || step >= S.mem.pats[i].len) return flash(['WRITE', 'C / D ONLY']);
    // tapping the grid: main voice, then the row's second voice, then both off
    const map = [[8, 0], [4, 16], [2, 32], [1, 64], [ACC, 0]][row], st = S.mem.pats[i].st;
    const a = st[step] & map[0], b = map[1] && (st[step] & map[1]);
    st[step] &= ~(map[0] | map[1]);
    if (!a && !b) st[step] |= map[0]; else if (a && !b && map[1]) st[step] |= map[1];
    if (S.mode === 'step') S.cursor = step;
    memChanged();
  }
});
const up = e => { const k = downKeys.get(e.pointerId); if (!k) return; downKeys.delete(e.pointerId); k.key.classList.remove('down'); release(k.id); };
plate.addEventListener('pointerup', up); plate.addEventListener('pointercancel', up);
plate.addEventListener('contextmenu', e => e.preventDefault());

// computer keyboard: Shift, A (accent), Z X C V B N M the voices, , knuckle, 1-8, Q bank, space start/stop
const KEYMAP = {KeyZ:'v0', KeyX:'v1', KeyC:'v2', KeyV:'v3', KeyB:'v4', KeyN:'v5', KeyA:'acc', KeyQ:'bank',
  Digit1:'n1', Digit2:'n2', Digit3:'n3', Digit4:'n4', Digit5:'n5', Digit6:'n6', Digit7:'n7', Digit8:'n8', ShiftLeft:'shift', ShiftRight:'shift', Enter:'start', Backspace:'stop'};
addEventListener('keydown', e => {
  if (e.target.closest('input,select,textarea') || e.repeat) return;
  if (e.code === 'Space'){ e.preventDefault(); press(S.running ? 'stop' : 'start'); return; }
  if (e.code === 'KeyM'){ press('shift'); press('v5'); release('shift'); S.shiftLatch = false; S.shiftUsed = true; draw(); return; }
  const id = KEYMAP[e.code]; if (!id) return;
  e.preventDefault(); const k = UI.buttons[id]; if (k) k.g.classList.add('down'); press(id);
});
addEventListener('keyup', e => { const id = KEYMAP[e.code]; if (!id) return; const k = UI.buttons[id]; if (k) k.g.classList.remove('down'); release(id); });

/* ---------------- mixer and effects mixer ---------------- */
function miniKnob(parent, label, get, set, fmt, dflt){
  const w = document.createElement('div'); w.className = 'knobRow';
  w.innerHTML = `<svg class="mk" viewBox="0 0 60 60"><circle cx="30" cy="30" r="25" fill="#151515" stroke="#000" stroke-width="2"/><line x1="30" y1="30" x2="30" y2="9" stroke="#ff7a1a" stroke-width="5" stroke-linecap="round"/></svg><span>${label}</span><span class="val"></span>`;
  parent.appendChild(w);
  const line = w.querySelector('line'), val = w.querySelector('.val'), svg = w.querySelector('svg');
  const show = () => { const n = get(); line.setAttribute('transform', `rotate(${-135 + 270 * n} 30 30)`); val.textContent = fmt(); };
  dragKnob(svg, get, n => { set(Math.max(0, Math.min(1, n))); show(); }, dflt);
  show(); w.refresh = show; return w;
}
const norm = i => (S.params[i] - PARAMS[i][2]) / (PARAMS[i][3] - PARAMS[i][2]);
const denorm = (i, n) => PARAMS[i][2] + n * (PARAMS[i][3] - PARAMS[i][2]);
const dnorm = i => (PARAMS[i][4] - PARAMS[i][2]) / (PARAMS[i][3] - PARAMS[i][2]);
const refreshers = [];
let meterEls = [];
function buildMixer(){
  const strips = document.getElementById('strips'); strips.innerHTML = ''; meterEls = [];
  for (let v = 0; v < NV; v++){
    const s = document.createElement('div'); s.className = 'strip';
    s.innerHTML = `<h4>${VNAME_SHORT[v]}</h4>`;
    const pan = vp(v, 1), tune = vp(v, 2), dec = vp(v, 3), lv = vp(v, 0), mute = vp(v, 4), snd = vp(v, 5);
    refreshers.push(miniKnob(s, 'TUNE', () => norm(tune), n => setParam(tune, Math.round(denorm(tune, n) * 2) / 2), () => (S.params[tune] > 0 ? '+' : '') + S.params[tune] + ' st', dnorm(tune)).refresh);
    refreshers.push(miniKnob(s, 'DECAY', () => Math.log(S.params[dec] / .25) / Math.log(16), n => setParam(dec, Math.round(.25 * Math.pow(16, n) * 100) / 100), () => '×' + S.params[dec].toFixed(2), .5).refresh);
    refreshers.push(miniKnob(s, 'PAN', () => norm(pan), n => setParam(pan, Math.round(denorm(pan, n) * 20) / 20), () => S.params[pan] === 0 ? 'C' : (S.params[pan] < 0 ? 'L' : 'R') + Math.round(Math.abs(S.params[pan]) * 100), .5).refresh);
    const f = document.createElement('div'); f.className = 'fader';
    f.innerHTML = `<input type="range" min="0" max="1.5" step="0.01" aria-label="${VOICE_NAMES[v]} level"><div class="meter"><i></i></div>`;
    const r = f.querySelector('input'); r.value = S.params[lv];
    r.addEventListener('input', () => setParam(lv, +r.value)); r.addEventListener('dblclick', () => { r.value = 1; setParam(lv, 1); });
    refreshers.push(() => { r.value = S.params[lv]; });
    s.appendChild(f); meterEls.push(f.querySelector('.meter i'));
    const b = document.createElement('div'); b.className = 'btns';
    b.innerHTML = `<button class="ch m">M</button><button class="ch s">S</button>`;
    const [mb, sb] = b.querySelectorAll('button');
    mb.onclick = () => { setParam(mute, S.params[mute] >= .5 ? 0 : 1); mb.classList.toggle('on', S.params[mute] >= .5); };
    sb.onclick = () => { S.solo[v] = S.solo[v] ? 0 : 1; sb.classList.toggle('on', !!S.solo[v]); post({type:'params', p:engineParams()}); save(); };
    refreshers.push(() => { mb.classList.toggle('on', S.params[mute] >= .5); sb.classList.toggle('on', !!S.solo[v]); });
    s.appendChild(b);
    const hit = document.createElement('button'); hit.className = 'small'; hit.textContent = 'PLAY';
    hit.onclick = async () => { if (!S.power){ S.power = true; await startAudio(); draw(); } post({type:'hit', v, acc:false}); };
    s.appendChild(hit);
    strips.appendChild(s);
    void snd;
  }
  // the effects mixer: one send per voice
  const sends = document.getElementById('sends'); sends.innerHTML = '';
  for (let v = 0; v < NV; v++){
    const s = document.createElement('div'); s.className = 'strip'; s.innerHTML = `<h4>${VNAME_SHORT[v]}</h4>`;
    const snd = vp(v, 5);
    refreshers.push(miniKnob(s, 'REV SEND', () => norm(snd), n => setParam(snd, Math.round(n * 100) / 100), () => Math.round(S.params[snd] * 100) + '%', dnorm(snd)).refresh);
    sends.appendChild(s);
  }
  // REV 7
  const progs = document.getElementById('revProgs'); progs.innerHTML = '';
  RV_PROGS.forEach((name, i) => {
    const b = document.createElement('button'); b.textContent = String(i + 1).padStart(2, '0') + ' ' + name;
    b.onclick = () => { setParam(P.rvProg, i); refreshPanels(); };
    progs.appendChild(b);
  });
  refreshers.push(() => { [...progs.children].forEach((b, i) => b.classList.toggle('on', i === Math.round(S.params[P.rvProg])));
    document.getElementById('revProgName').textContent = String(Math.round(S.params[P.rvProg]) + 1).padStart(2, '0') + ' ' + RV_PROGS[Math.round(S.params[P.rvProg])] + '  RT ' + S.params[P.rvTime].toFixed(1) + 's'; });
  const rk = document.getElementById('revKnobs'); rk.innerHTML = '';
  const lin = (i, digits, unit, scale = 1) => refreshers.push(miniKnob(rk, PARAMS[i][1].replace('Reverb ', '').toUpperCase(), () => norm(i), n => { setParam(i, Math.round(denorm(i, n) * Math.pow(10, digits)) / Math.pow(10, digits)); refreshPanels(); },
    () => (S.params[i] * scale).toFixed(digits) + unit, dnorm(i)).refresh);
  refreshers.push(miniKnob(rk, 'REV TIME', () => Math.log(S.params[P.rvTime] / .3) / Math.log(10 / .3), n => { setParam(P.rvTime, Math.round(.3 * Math.pow(10 / .3, n) * 10) / 10); refreshPanels(); }, () => S.params[P.rvTime].toFixed(1) + ' s', Math.log(2.6 / .3) / Math.log(10 / .3)).refresh);
  lin(P.rvHigh, 2, '');
  lin(P.rvDelay, 0, ' ms');
  lin(P.rvDiff, 0, '');
  lin(P.rvEr, 2, '');
  refreshers.push(miniKnob(rk, 'HIGH CUT', () => Math.log(S.params[P.rvCut] / 1000) / Math.log(12), n => setParam(P.rvCut, Math.round(1000 * Math.pow(12, n) / 100) * 100), () => (S.params[P.rvCut] / 1000).toFixed(1) + ' k', Math.log(9) / Math.log(12)).refresh);
  lin(P.rvLevel, 2, '');
  // source and shuffle
  const src = document.getElementById('src');
  src.onchange = () => setParam(P.src, +src.value);
  refreshers.push(() => { src.value = String(Math.round(S.params[P.src])); });
  const sw = document.getElementById('swing'), so = document.getElementById('swingOut');
  sw.oninput = () => { S.swing = +sw.value; so.textContent = Math.round(S.swing * 100) + '%'; post({type:'swing', v:S.swing}); save(); };
  refreshers.push(() => { sw.value = S.swing; so.textContent = Math.round(S.swing * 100) + '%'; });
  document.getElementById('mixReset').onclick = () => {
    const d = defaults(); for (let i = DR110.NG; i < PARAMS.length; i++) S.params[i] = d[i];
    S.solo.fill(0); post({type:'params', p:engineParams()}); save(); refreshPanels();
  };
}
/* the voice-mod panel */
function toggle(parent, label, sub, get, set){
  const b = document.createElement('button'); b.className = 'tog';
  b.innerHTML = `<span>${sub ? 'ON' : ''}</span><i></i><span>${label}</span>`;
  b.onclick = () => { set(!get()); show(); };
  const show = () => b.classList.toggle('on', !!get());
  parent.appendChild(b); refreshers.push(show); show(); return b;
}
function buildMod(){
  const root = document.getElementById('modPanel'); root.innerHTML = '';
  const sec = title => { const d = document.createElement('div'); d.className = 'modSec'; d.innerHTML = `<h4>${title}</h4>`; root.appendChild(d); return d; };
  const k = (parent, label, i, kind) => {
    let get, set, fmt, d;
    if (kind === 'tune'){ get = () => norm(i[0]); set = n => { const v = Math.round(denorm(i[0], n) * 2) / 2; for (const j of i) S.params[j] = v; setParam(i[0], v); }; fmt = () => (S.params[i[0]] > 0 ? '+' : '') + S.params[i[0]] + ' st'; d = .5; }
    else if (kind === 'dec'){ get = () => Math.log(S.params[i] / .25) / Math.log(16); set = n => setParam(i, Math.round(.25 * Math.pow(16, n) * 100) / 100); fmt = () => '×' + S.params[i].toFixed(2); d = .5; }
    else { get = () => norm(i); set = n => setParam(i, Math.round(n * 100) / 100); fmt = () => Math.round(S.params[i] * 10 * 10) / 10 + ''; d = dnorm(i); }
    const w = miniKnob(parent, label, get, n => { set(n); refreshPanels(); }, fmt, d); w.querySelector('svg').classList.add('white'); refreshers.push(w.refresh);
  };
  const sw = (parent, label, i) => toggle(parent, label, true, () => S.params[i] >= .5, on => setParam(i, on ? 1 : 0));
  let s = sec('Kick'); k(s, 'PITCH', [vp(0, 2)], 'tune'); k(s, 'SNAP', P.bdSnap); k(s, 'DECAY', vp(0, 3), 'dec'); sw(s, 'DECAY BOOST', P.bdBoost); sw(s, 'BASS BOOST', P.bdBass);
  s = sec('Snare'); k(s, 'PITCH', [vp(1, 2)], 'tune'); k(s, 'SNAP', P.sdSnap); k(s, 'DECAY', vp(1, 3), 'dec'); sw(s, 'DECAY BOOST', P.sdBoost); k(s, 'MIX', P.sdMix);
  s = sec('HiHat'); k(s, 'PITCH', [vp(2, 2), vp(3, 2)], 'tune'); k(s, 'FILTER', P.hhFilter); k(s, 'CH DECAY', vp(3, 3), 'dec'); k(s, 'OH DECAY', vp(2, 3), 'dec'); k(s, 'MIX', P.hhMix);
  s = sec('Cymbal'); k(s, 'FILTER', P.cyFilter); k(s, 'DECAY', vp(4, 3), 'dec');
  const t = document.createElement('div'); t.className = 'modSec'; t.style.marginTop = '4px'; t.innerHTML = '<h4 style="font-size:13px">TRIG SWEEP</h4><small style="font-size:9px;font-weight:800">(EXT / ACCENT)</small>'; s.appendChild(t);
  toggle(t, 'BD PITCH · CH DECAY', false, () => S.params[P.swTarget] >= .5, on => setParam(P.swTarget, on ? 1 : 0));
  k(t, 'DECAY', P.swDecay); k(t, 'AMOUNT', P.swAmount);
  const fire = document.createElement('button'); fire.className = 'small'; fire.textContent = 'SWEEP'; fire.onclick = () => post({type:'sweep'}); t.appendChild(fire);
  s = sec('Clap'); k(s, 'PITCH', [vp(5, 2)], 'tune'); k(s, 'FILTER', P.hcFilter); k(s, 'DECAY', vp(5, 3), 'dec'); sw(s, 'DECAY BOOST', P.hcBoost);
  const br = document.createElement('div'); br.className = 'modBrand';
  br.innerHTML = '<span>POWER SUPPLY</span>'; root.appendChild(br);
  toggle(br, 'BATTERIES 5.4 V', false, () => S.params[P.supply] >= .5, on => { setParam(P.supply, on ? 1 : 0); toast(on ? 'Four AA cells: the rail sags to 5.4 V, less headroom' : 'DC 9V adapter: regulated 6 V rail'); });
  br.insertAdjacentHTML('beforeend', '<b>VOICE MOD<br>110</b>');
}
function refreshPanels(){ for (const f of refreshers) f(); }
function showMeters(m){ for (let v = 0; v < NV && v < meterEls.length; v++){ const db = 20 * Math.log10(Math.max(1e-4, m[v] * 1.4)); meterEls[v].style.height = Math.max(0, Math.min(100, (db + 48) / 48 * 100)) + '%'; } }

// tabs
document.querySelectorAll('#tabs button').forEach(b => b.onclick = () => {
  document.querySelectorAll('#tabs button').forEach(x => x.classList.toggle('on', x === b));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('on', p.id === b.dataset.tab));
});

let toastT = 0;
function toast(s, ms = 2600){ const t = document.getElementById('toast'); t.textContent = s; t.classList.add('on'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('on'), ms); }

load();
buildMod();
buildMixer(); refreshPanels(); draw();
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidDR110) navigator.serviceWorker.register('sw.js').catch(() => {});

window.DRAPP = {setParam, miniKnob, toggle, refreshers, dragKnob, norm, denorm, dnorm, sendAll, S, post, press, release, draw, flash, toast, save, backup, applyBackup, memChanged, startAudio, samples, loadRecordings, decode, engineParams,
  listeners, patIndex, userPat, startKey, stopKey, voiceKey, refreshPanels, get ctx(){ return ctx; }};
