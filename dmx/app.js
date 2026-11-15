/* Omega DMX: a digital drum machine laid out like the DMX. Kits are real recordings (kits.js); 10 patterns per kit of
   16 steps with accents, song mode, swing, live record, nine faders, MIDI in (notes, clock, start / stop) and WAV export.
   The plugin (plugins/dmx) plays the same kits and patterns with the same timing, levels and choke. */
'use strict';
const $ = s => document.querySelector(s);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const SWINGS = [50, 54, 58, 62, 66, 71];
const VEL = [0, .72, 1];                  // step value 1 = hit, 2 = accent
const KEY = 'omega-dmx-v1';

/* ---------------- state (saved) ---------------- */
const FADER0 = {bass:.8, snare:.8, hat:.8, toms:.8, cym:.8, perc1:.8, perc2:.8, met:0, vol:.85};   // metronome down: no click
const S = Object.assign({kit:'dmx', tempo:112, swing:50, pat:{}, pats:{}, names:{}, song:{}, useSong:{}, edit:false, circuit:{}, tune:{}}, (() => { try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch(e){ return {}; } })());
S.fader = Object.assign({}, FADER0, S.fader); delete S.click; S.circuit = S.circuit || {}; S.tune = S.tune || {};
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(KEY, JSON.stringify(S)); } catch(e){} }, 300); }
const kit = () => DMX_KITS.find(k => k.id === S.kit) || DMX_KITS[0];
function pats(k = kit()){
  if (!S.pats[k.id] || S.pats[k.id].length !== 10) S.pats[k.id] = DMX_PATTERNS.map((_, i) => dmxFactory(k, i));
  for (const p of S.pats[k.id]) while (p.rows.length < k.voices.length) p.rows.push(new Array(16).fill(0));
  return S.pats[k.id];
}
const curPat = () => S.pat[S.kit] || 0;
const song = () => S.song[S.kit] || (S.song[S.kit] = []);
const vname = (i, k = kit()) => ((S.names[k.id] || {})[i]) || k.voices[i][1];

/* ---------------- audio: the engine (engine.js) in an AudioWorklet ---------------- */
const R = {ctx:null, node:null, ready:null, bufs:{}, src:{}, roms:{}, meter:[], playing:false, step:0, nextT:0, timer:0, q:[], play:null, queued:-1, songPos:0, ext:null, sel:0, acc:false, rec:false, mode:'', typed:'', clearArm:0};
const GROUPS = DMX_FADERS.slice(0, 7).map(f => f[0]);
// the sound parameters (engine.js PARAMS): VOICE MOD, the Omega controls, pad EQ / ADSR, effects and master
S.params = Array.isArray(S.params) && S.params.length === DMXE.PARAMS.length ? S.params : DMXE.defaults();
function ensureAudio(){
  if (R.ctx){ if (R.ctx.state !== 'running') R.ctx.resume(); return R.ctx; }
  const C = window.AudioContext || window.webkitAudioContext; R.ctx = new C({latencyHint:'interactive'});
  R.ready = (async () => {
    for (const f of ['fx.js', 'engine.js', 'worklet.js']) await R.ctx.audioWorklet.addModule(f);
    R.node = new AudioWorkletNode(R.ctx, 'dmx', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2]});
    R.node.connect(R.ctx.destination);
    R.node.port.onmessage = ({data:x}) => { if (x.type === 'meter') R.meter = x.m; };
    post({type:'params', p:S.params}); post({type:'tempo', bpm:S.tempo}); levels(); if (R.bufs[kit().id]) sendSounds();
  })();
  return R.ctx;
}
const post = m => { if (R.node) R.node.port.postMessage(m); };
const circuitOn = (k = kit()) => S.circuit[k.id] != null ? !!S.circuit[k.id] : k.id !== 'dmx';   // the DMX kit is recorded from a DMX already
const tuneOf = (vi, k = kit()) => ((S.tune[k.id] || {})[vi] ?? 50);
// where a kit voice sits: its card (index in engine.js CARDS), trigger mode, fader group; its trimmer with the card's TUNE offset
function placeOf(k, vi){ const [g, cn, mode] = DMXC.cardFor(k, vi); return {g:GROUPS.indexOf(g), cn, c:DMXE.CARDS.indexOf(cn), card:DMXC.CARDS[cn], mode}; }
const tuneEff = (k, vi) => clamp(tuneOf(vi, k) + S.params[DMXE.cp(placeOf(k, vi).c, 6)], 0, 100);
function levels(){ post({type:'circuit', on:circuitOn()}); post({type:'faders', f:DMX_FADERS.map(([g]) => S.fader[g])}); }
// a kit: the WAVs read at their own rate (as the plugin reads them), each voice's ROM for its card, and what plays:
// the card's output rendered through the circuit model (with its VOICE MOD set), or (CIRCUIT off) the sound itself
async function loadKit(k){
  ensureAudio(); await R.ready;
  if (R.bufs[k.id]) return R.bufs[k.id];
  R.src[k.id] = await Promise.all(k.voices.map(async v => { try { return DMXC.parseWav(await (await fetch('kits/' + k.id + '/' + v[0] + '.wav')).arrayBuffer()); } catch(e){ return null; } }));
  R.roms[k.id] = k.voices.map((v, vi) => { const s = R.src[k.id][vi]; return s ? DMXC.makeRom(s.x, s.sr, placeOf(k, vi).card) : null; });
  R.bufs[k.id] = k.voices.map((v, vi) => voiceBuf(k, vi));
  if (k === kit()) sendSounds();
  return R.bufs[k.id];
}
function voiceBuf(k, vi, rate = R.ctx.sampleRate){
  const s = R.src[k.id] && R.src[k.id][vi]; if (!s) return null;
  const pl = placeOf(k, vi), m = DMXE.mods(S.params, pl.c);
  if (circuitOn(k)) return {d:DMXC.render(R.roms[k.id][vi], pl.card, pl.mode, tuneEff(k, vi), rate, m), rate};
  const d = Float32Array.from(s.x); if (m.rev) d.reverse(); return {d, rate:s.sr};
}
// the engine's copy of a voice: its sound, card, group, choke, its place in the DMX's stereo mix, the clean trimmer's pitch
function soundMsg(k, vi, b){
  const pl = placeOf(k, vi), circ = circuitOn(k), [pL, pR] = circ ? DMXC.pan(pl.card) : [1, 1];
  const ratio = circ ? 1 : DMXC.rate(pl.card, pl.mode, tuneEff(k, vi)) / DMXC.rate(pl.card, pl.mode, 50);
  return {type:'sound', vi, d:b ? b.d : null, rate:b ? b.rate : 1, card:pl.c, group:pl.g, open:dmxIsOpen(k.voices[vi]), panL:pL, panR:pR, ratio};
}
function sendSounds(){ const k = kit(), B = R.bufs[k.id]; if (!B) return; k.voices.forEach((v, vi) => post(soundMsg(k, vi, B[vi]))); post({type:'nosound', from:k.voices.length}); }
// re-render after TUNE, VOICE MOD or CIRCUIT changes (one voice, a card's voices, or the whole kit)
let retuneT = 0, retuneSet = new Set();
function rerender(which){ const k = kit(); if (!R.bufs[k.id]) return; clearTimeout(retuneT);
  if (which == null) k.voices.forEach((v, i) => retuneSet.add(i)); else if (Array.isArray(which)) which.forEach(i => retuneSet.add(i)); else retuneSet.add(which);
  retuneT = setTimeout(() => { const B = R.bufs[k.id]; for (const i of retuneSet){ B[i] = voiceBuf(k, i); post(soundMsg(k, i, B[i])); } retuneSet.clear(); levels(); }, 120); }
const cardVoices = c => kit().voices.map((v, i) => i).filter(i => placeOf(kit(), i).c === c);
function setParam(i, v){
  S.params[i] = v; post({type:'params', p:S.params}); S.edited = true; save();
  if (i >= DMXE.NG && (i - DMXE.NG) % DMXE.NF < DMXE.RENDER_FIELDS){ const c = Math.floor((i - DMXE.NG) / DMXE.NF); if (circuitOn() || (i - DMXE.NG) % DMXE.NF === 5) rerender(cardVoices(c)); else rerender(cardVoices(c)); }
}
function setAllParams(p){ S.params = p.slice(); post({type:'params', p:S.params}); save(); rerender(); }
// a hit: the engine places it at its sample; gate (s): when the pad's ADSR releases (-1: on release())
const sendHit = (vi, t, vel, gate) => post({type:'hit', vi, vel, t, gate});
const clickOn = () => S.fader.met > .02;
const clickAt = (t, loud) => post({type:'click', t, loud});

/* ---------------- sequencer ---------------- */
const stepDur = () => 60 / S.tempo / 4;
// odd sixteenths are late by the swing amount: 50% straight, 66% a triplet shuffle
const swingOff = s => s % 2 ? (S.swing / 50 - 1) * stepDur() : 0;
function nextPattern(){
  // what plays after the current pattern ends: the song's next entry, a pattern picked on the keypad, or the same again
  const sg = song();
  if (S.useSong[S.kit] && sg.length){ R.songPos = (R.songPos + 1) % sg.length; return sg[R.songPos]; }
  if (R.queued >= 0){ const p = R.queued; R.queued = -1; S.pat[S.kit] = p; save(); return p; }
  return curPat();
}
function scheduler(){
  const c = R.ctx, k = kit();
  while (R.nextT < c.currentTime + .12){
    if (R.step === 16){ R.step = 0; R.play = nextPattern(); paint(); }
    const p = pats()[R.play], t = R.nextT + swingOff(R.step);
    p.rows.forEach((row, vi) => { if (row[R.step] && vi < k.voices.length) sendHit(vi, t, VEL[row[R.step]], stepDur()); });
    if (clickOn() && R.step % 4 === 0) clickAt(t, R.step === 0);
    R.q.push({t, step:R.step, pat:R.play}); if (R.q.length > 64) R.q.shift();
    R.step++; R.nextT += stepDur();
  }
}
async function start(){
  const c = ensureAudio(); await loadKit(kit());
  if (R.playing) return; R.playing = true; R.step = 0; R.q = [];
  const sg = song(); if (S.useSong[S.kit] && sg.length){ R.songPos = 0; R.play = sg[0]; } else { if (R.queued >= 0){ S.pat[S.kit] = R.queued; R.queued = -1; } R.play = curPat(); }
  R.nextT = c.currentTime + .06; if (!R.ext){ scheduler(); R.timer = setInterval(scheduler, 25); }
  paint();
}
function stop(){ R.playing = false; clearInterval(R.timer); R.timer = 0; R.q = []; R.ext = R.ext ? {} : null; paint(); }
// the step sounding now (for the grid's playhead and live recording)
function nowStep(){ if (!R.playing || !R.ctx) return null; const t = R.ctx.currentTime - (R.ctx.outputLatency || 0); let cur = null; for (const e of R.q) if (e.t <= t + stepDur() / 2) cur = e; return cur; }

/* ---------------- playing the keys ---------------- */
async function play(vi, vel, gate = stepDur()){
  const c = ensureAudio(), k = kit(); await loadKit(k);
  sendHit(vi, c.currentTime, vel, gate); flash(vi);
  if (R.rec && R.playing){
    const e = nowStep(); if (!e) return;
    const t = c.currentTime - (c.outputLatency || 0), d = t - e.t, s = d > stepDur() / 2 ? (e.step + 1) % 16 : e.step;
    pats()[e.pat].rows[vi][s] = R.acc ? 2 : 1; save(); paintGrid();
  }
}
function flash(vi){ const b = document.querySelector(`.key[data-v="${vi}"]`); if (!b) return; b.classList.add('hit'); setTimeout(() => b.classList.remove('hit'), 90); }

/* ---------------- display ---------------- */
let toastT = 0;
function toast(s, ms = 1800){ const t = $('#toast'); t.textContent = s; t.classList.add('on'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('on'), ms); }
function led(){
  const pn = String(curPat() + 1).padStart(2, '0');
  if (R.mode === 'tempo') return ('t' + (R.typed || String(S.tempo)).padStart(4)).padStart(6);
  if (R.mode === 'swing') return 'SU ' + String(S.swing).padStart(3);
  if (R.mode === 'tune') return 'tu ' + (R.typed ? R.typed.padEnd(2, '_') : String(tuneOf(R.sel))).padStart(3);
  if (R.mode === 'copy') return 'CP' + pn + ' -';
  if (R.mode === 'song'){ const sg = song(); return 'S' + String(sg.length).padStart(2, '0') + ' ' + (sg.length ? String(sg[sg.length - 1] + 1).padStart(2, '0') : '--'); }
  if (S.useSong[S.kit] && song().length && R.playing) return 'S' + String(Math.max(0, R.songPos) + 1).padStart(2, '0') + ' ' + String(R.play + 1).padStart(2, '0');
  return pn + ' ' + String(S.tempo).padStart(3);
}
function msg(){
  const p = pats()[curPat()];
  if (R.mode === 'tempo') return 'TEMPO: TYPE IT ON THE KEYPAD, < > NUDGES';
  if (R.mode === 'swing') return 'SWING ' + S.swing + '%: PRESS SWING TO STEP';
  if (R.mode === 'tune') return 'TUNE ' + vname(R.sel) + ': 00-99 ON THE KEYPAD, < > STEPS 5, A VOICE KEY PICKS';
  if (R.mode === 'copy') return 'COPY PATTERN ' + (curPat() + 1) + ' TO… PRESS A NUMBER';
  if (R.mode === 'song') return 'SONG: NUMBERS ADD PATTERNS, ERASE REMOVES (' + song().map(n => n + 1).join(' ') + ')';
  const q = R.queued >= 0 ? ' → NEXT ' + (R.queued + 1) : '';
  return kit().name + ' · ' + (S.useSong[S.kit] && song().length ? 'SONG ' + song().map(n => n + 1).join('-') : 'PATTERN ' + (curPat() + 1) + ' · ' + p.name) + q;
}
function paint(){
  $('#digits').textContent = led(); $('#msg').textContent = msg();
  $('#play').classList.toggle('on', R.playing);
  $('#dmx').classList.toggle('edit', !!S.edit); paintCircuit();
  document.querySelectorAll('#pad button[data-n]').forEach(b => { const n = +b.dataset.n; b.classList.toggle('on', n === curPat() && !R.mode); b.classList.toggle('used', pats()[n].rows.some(r => r.some(x => x))); });
  const fk = {swing:R.mode === 'swing', song:R.mode === 'song' || !!(S.useSong[S.kit] && song().length), copy:R.mode === 'copy', tempo:R.mode === 'tempo', tune:R.mode === 'tune', rec:R.rec, clear:!!R.clearArm, edit:!!S.edit, acc:R.acc};
  document.querySelectorAll('#fn .key, #small .key').forEach(b => b.classList.toggle('on', !!fk[b.dataset.f]));
  document.querySelectorAll('#mat .key[data-v]').forEach(b => b.classList.toggle('on', +b.dataset.v === R.sel));
  paintGrid();
}
function buildGrid(){
  const k = kit(), g = $('#grid');
  g.innerHTML = `<div class="steps"><span></span>${Array.from({length:16}, (_, s) => `<em data-s="${s}">${s + 1}</em>`).join('')}</div>` +
    k.voices.map((v, vi) => `<div class="row" data-v="${vi}"><b title="${vname(vi)}">${vname(vi).toUpperCase()}</b>${Array.from({length:16}, (_, s) => `<i data-s="${s}" class="${s % 4 === 0 ? 'beat' : ''}"></i>`).join('')}</div>`).join('');
  let paintV = null;
  g.onpointerdown = e => {
    const i = e.target.closest('.row i'), r = e.target.closest('.row'); if (!r) return;
    const vi = +r.dataset.v; R.sel = vi;
    if (!i){ play(vi, VEL[R.acc ? 2 : 1]); holdRename(e, vi); paint(); return; }
    const p = pats()[R.playing ? R.play : curPat()], s = +i.dataset.s, want = R.acc ? 2 : 1;
    paintV = p.rows[vi][s] === want ? 0 : want; p.rows[vi][s] = paintV; g._row = vi; save(); paint();
    if (paintV && !R.playing) play(vi, VEL[paintV]);
  };
  g.onpointermove = e => {
    if (paintV == null || !e.buttons && e.pointerType === 'mouse') return;
    const el = document.elementFromPoint(e.clientX, e.clientY), i = el && el.closest && el.closest('.row i'); if (!i) return;
    const vi = +i.parentElement.dataset.v; if (vi !== g._row) return;
    const p = pats()[R.playing ? R.play : curPat()], s = +i.dataset.s; if (p.rows[vi][s] !== paintV){ p.rows[vi][s] = paintV; save(); paintGrid(); }
  };
  const end = () => { paintV = null; }; g.onpointerup = end; g.onpointercancel = end; g.onpointerleave = end;
}
function paintGrid(){
  const p = pats()[R.playing ? R.play : curPat()], e = nowStep(), now = e ? e.step : -1;
  document.querySelectorAll('#grid .row').forEach(r => { const vi = +r.dataset.v; r.classList.toggle('sel', vi === R.sel);
    r.querySelectorAll('i').forEach((c, s) => { const v = p.rows[vi][s]; c.classList.toggle('on', v > 0); c.classList.toggle('acc', v === 2); c.classList.toggle('now', s === now); }); });
  document.querySelectorAll('#grid .steps em').forEach((m, s) => m.classList.toggle('now', s === now));
}
let lastNow = -2;
function frame(){ const e = nowStep(), n = e ? e.step * 100 + e.pat : -1; if (n !== lastNow){ lastNow = n; paintGrid(); if (e && R.mode === '' ) { $('#digits').textContent = led(); } } requestAnimationFrame(frame); }

/* ---------------- building the panel ---------------- */
function buildFaders(){
  $('#faders').innerHTML = DMX_FADERS.map(([g, n]) => `<div class="fader"><div class="slot" data-g="${g}" role="slider" aria-label="${n}" tabindex="0"><div class="cap"></div></div><span>${n}</span></div>`).join('');
  document.querySelectorAll('.slot').forEach(sl => {
    const g = sl.dataset.g, set = v => { S.fader[g] = Math.round(clamp(v, 0, 1) * 100) / 100; posFader(sl); levels(); save(); };
    const at = e => { const r = sl.getBoundingClientRect(); set(1 - (e.clientY - r.top - 8) / (r.height - 16)); };
    sl.addEventListener('pointerdown', e => { ensureAudio(); sl.setPointerCapture(e.pointerId); at(e); sl.onpointermove = at; });
    sl.addEventListener('pointerup', () => { sl.onpointermove = null; }); sl.addEventListener('pointercancel', () => { sl.onpointermove = null; });
    sl.addEventListener('keydown', e => { if (e.key === 'ArrowUp' || e.key === 'ArrowRight') set(S.fader[g] + .05); if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') set(S.fader[g] - .05); });
    posFader(sl);
  });
}
function posFader(sl){ const v = S.fader[sl.dataset.g], cap = sl.firstElementChild; cap.style.top = `calc(8px + (100% - 16px) * ${1 - v})`; sl.setAttribute('aria-valuenow', Math.round(v * 100)); }
// the voice keys in columns under their fader groups, three to a column, as on the hardware
function buildMat(){
  const k = kit(), groups = DMX_FADERS.filter(([g]) => g !== 'met' && g !== 'vol');
  $('#mat').innerHTML = groups.map(([g, n]) => { const vs = k.voices.map((v, vi) => [v, vi]).filter(([v]) => v[2] === g); if (!vs.length) return '';
    return `<div class="grp" style="flex:${Math.ceil(vs.length / 3)}"><b>${n}</b><div class="cols">${vs.map(([v, vi]) => `<button class="key" data-v="${vi}"><i></i><span>${vname(vi)}</span></button>`).join('')}</div></div>`; }).join('');
  document.querySelectorAll('#mat .key[data-v]').forEach(b => {
    const vi = +b.dataset.v;
    b.addEventListener('pointerdown', e => { e.preventDefault(); R.sel = vi; play(vi, VEL[R.acc ? 2 : 1]); holdRename(e, vi); paint(); });
  });
}
let holdT = 0;
function holdRename(e, vi){
  clearTimeout(holdT); const x = e.clientX, y = e.clientY, el = e.target;
  holdT = setTimeout(() => rename(vi), 650);
  const cancel = ev => { if (ev.type !== 'pointermove' || Math.hypot(ev.clientX - x, ev.clientY - y) > 12){ clearTimeout(holdT); off(); } };
  const off = () => ['pointerup', 'pointercancel', 'pointermove'].forEach(t => el.removeEventListener(t, cancel));
  ['pointerup', 'pointercancel', 'pointermove'].forEach(t => el.addEventListener(t, cancel));
}
function rename(vi){
  const d = $('#nameDlg'), k = kit(); $('#nameTtl').textContent = 'Rename ' + k.voices[vi][1]; $('#nameIn').value = vname(vi);
  $('#nameOk').onclick = () => { const n = $('#nameIn').value.trim(); (S.names[k.id] = S.names[k.id] || {})[vi] = n || undefined; save(); d.close(); rebuild(); };
  $('#nameReset').onclick = () => { if (S.names[k.id]) delete S.names[k.id][vi]; save(); d.close(); rebuild(); };
  d.showModal(); $('#nameIn').focus();
}
function buildPad(){
  $('#pad').innerHTML = ['7', '8', '9', '4', '5', '6', '1', '2', '3', '<', '0', '>'].map(k => /\d/.test(k) ? `<button data-k="${k}" data-n="${k === '0' ? 9 : +k - 1}" aria-label="${k}">${k}</button>` : `<button data-k="${k}" aria-label="${k === '<' ? 'Down' : 'Up'}">${k}</button>`).join('');
  $('#pad').addEventListener('pointerdown', e => { const b = e.target.closest('button'); if (!b) return; e.preventDefault(); keypad(b.dataset.k); });
}
function keypad(k){
  const ps = pats(), n = k === '0' ? 9 : +k - 1;
  if (R.mode === 'tempo'){
    if (k === '<' || k === '>'){ S.tempo = clamp(S.tempo + (k === '>' ? 1 : -1), 40, 250); R.typed = ''; }
    else { R.typed = (R.typed + k).slice(-3); if (R.typed.length === 3 || (R.typed.length === 2 && +R.typed >= 40 && R.typed[0] > '2')){ S.tempo = clamp(+R.typed, 40, 250); R.typed = ''; } }
    save(); paint(); return;
  }
  if (R.mode === 'tune'){   // the voice card's pitch trimmer: 50 is the centre
    const k0 = kit();
    if (k === '<' || k === '>'){ R.typed = ''; setTune(k0, R.sel, tuneOf(R.sel) + (k === '>' ? 5 : -5)); }
    else { R.typed += k; if (R.typed.length === 2){ setTune(k0, R.sel, +R.typed); R.typed = ''; } }
    paint(); return;
  }
  if (R.mode === 'song'){
    if (k === '<' || k === '>') return;
    song().push(n); save(); paint(); return;
  }
  if (R.mode === 'copy'){
    if (k === '<' || k === '>') return;
    ps[n] = JSON.parse(JSON.stringify(ps[curPat()])); R.mode = ''; save(); toast('Pattern ' + (curPat() + 1) + ' copied to ' + (n + 1)); paint(); return;
  }
  if (R.mode === 'swing') R.mode = '';
  const to = k === '<' ? (curPat() + 9) % 10 : k === '>' ? (curPat() + 1) % 10 : n;
  S.useSong[S.kit] = false;
  if (R.playing){ R.queued = to; } else S.pat[S.kit] = to;
  save(); paint();
}
// bottom right as on the hardware (SWING SONG EDIT … / RECORD ERASE COPY …); TEMPO, ACCENT and FACTORY sit by the keypad
const FN = [['swing', 'SWING'], ['song', 'SONG'], ['edit', 'EDIT'], ['kit', 'KIT'], ['rec', 'RECORD'], ['erase', 'ERASE'], ['copy', 'COPY'], ['clear', 'CLEAR']];
const SMALL = [['tempo', 'TEMPO'], ['tune', 'TUNE'], ['acc', 'ACCENT'], ['factory', 'FACTORY']];
function buildFn(){
  const k = ([f, n]) => `<button class="key${f === 'rec' ? ' red' : ''}" data-f="${f}"><i></i><span>${n}</span></button>`;
  $('#fn').innerHTML = `<div class="hdr">CHECK · PLAY · RECORD</div>` + FN.map(k).join('') + `<div class="brand">Omega</div>`;
  $('#small').innerHTML = SMALL.map(k).join('');
  for (const box of [$('#fn'), $('#small')]) box.addEventListener('pointerdown', e => { const b = e.target.closest('.key'); if (!b) return; e.preventDefault(); fn(b.dataset.f); });
}
function fn(f){
  const ps = pats(), p = ps[curPat()];
  if (f !== 'clear') R.clearArm = 0;
  if (f === 'acc'){ R.acc = !R.acc; paint(); return; }
  if (f === 'tune'){ R.mode = R.mode === 'tune' ? '' : 'tune'; R.typed = ''; paint(); return; }
  if (f === 'kit'){ kitDialog(); return; }
  if (f === 'edit'){ S.edit = !S.edit; save(); paint(); return; }
  if (f === 'swing'){ if (R.mode === 'swing') S.swing = SWINGS[(SWINGS.indexOf(S.swing) + 1) % SWINGS.length]; R.mode = 'swing'; save(); }
  else if (f === 'tempo'){ R.mode = R.mode === 'tempo' ? '' : 'tempo'; R.typed = ''; }
  else if (f === 'copy') R.mode = R.mode === 'copy' ? '' : 'copy';
  else if (f === 'song'){ if (R.mode === 'song'){ R.mode = ''; S.useSong[S.kit] = song().length > 0; R.songPos = -1; } else { R.mode = 'song'; } save(); }
  else if (f === 'rec'){ R.rec = !R.rec; if (R.rec && !R.playing) toast('RECORD: press PLAY and play the keys'); }
  else if (f === 'erase'){
    if (R.mode === 'song'){ song().pop(); }
    else { const pp = ps[R.playing ? R.play : curPat()]; pp.rows[R.sel] = new Array(16).fill(0); toast('Erased ' + vname(R.sel)); }
    save();
  } else if (f === 'clear'){
    if (!R.clearArm){ R.clearArm = 1; toast('Press CLEAR again to clear pattern ' + (curPat() + 1)); setTimeout(() => { R.clearArm = 0; paint(); }, 2500); }
    else { R.clearArm = 0; p.rows = p.rows.map(() => new Array(16).fill(0)); p.name = 'Pattern ' + (curPat() + 1); save(); toast('Pattern ' + (curPat() + 1) + ' cleared'); }
  } else if (f === 'factory'){ ps[curPat()] = dmxFactory(kit(), curPat()); save(); toast('Factory pattern: ' + ps[curPat()].name); }
  paint();
}
function rebuild(){ buildGrid(); buildMat(); paint(); }
function setTune(k, vi, v){ (S.tune[k.id] = S.tune[k.id] || {})[vi] = clamp(Math.round(v), 0, 100); save(); rerender(vi); setTimeout(() => play(vi, VEL[2]), 150); }
function paintCircuit(){ const b = $('#circBtn'); b.textContent = circuitOn() ? 'CIRCUIT DMX' : 'CLEAN'; b.classList.toggle('on', circuitOn()); }

/* ---------------- kits ---------------- */
function kitDialog(){
  const d = $('#kitDlg');
  $('#kitList').innerHTML = DMX_KITS.map(k => `<button data-k="${k.id}" class="${k.id === S.kit ? 'on' : ''}"><b>${k.name}</b> · ${k.voices.length} sounds<small>${k.about}</small></button>`).join('');
  $('#kitList').onclick = async e => { const b = e.target.closest('button'); if (!b) return; d.close(); await setKit(b.dataset.k); };
  d.showModal();
}
async function setKit(id){
  const was = R.playing; if (was) stop();
  S.kit = id; R.sel = 0; R.queued = -1; R.mode = ''; save(); rebuild(); toast('Loading ' + kit().name + '…', 900);
  await loadKit(kit()); if (was) start();
}

/* ---------------- WAV export: the pattern four times, or the song, rendered offline ---------------- */
async function exportWav(){
  // the same engine as the audio thread, run here: every hit at its sample, the effects, the master
  const k = kit(); await loadKit(k); const sr = R.ctx.sampleRate, sg = song(), useSong = S.useSong[S.kit] && sg.length;
  const order = useSong ? sg : [curPat(), curPat(), curPat(), curPat()];
  const E = new DMXE.Engine(sr); E.setAll(S.params); E.setTempo(S.tempo); E.setCircuit(circuitOn()); E.setFaders(DMX_FADERS.map(([g]) => S.fader[g]));
  const B = R.bufs[k.id]; k.voices.forEach((v, vi) => { const m = soundMsg(k, vi, B[vi]); E.setSound(vi, m.d, m.rate, m.card, m.group, m.open, m.panL, m.panR, m.ratio); });
  const ev = []; let t = .02;
  for (const pi of order) for (let st = 0; st < 16; st++, t += stepDur()){ pats()[pi].rows.forEach((r, vi) => { if (r[st]) ev.push([Math.round((t + swingOff(st)) * sr), vi, VEL[r[st]]]); }); }
  ev.sort((x, y) => x[0] - y[0]);
  toast('Rendering…', 900); await new Promise(r => setTimeout(r, 30));
  const n = Math.ceil((t + 2.5) * sr), L = new Float32Array(n), Rr = new Float32Array(n), bl = new Float64Array(128), br = new Float64Array(128), gate = Math.round(stepDur() * sr);
  for (let f0 = 0, e = 0; f0 < n; f0 += 128){
    const m = Math.min(128, n - f0);
    while (e < ev.length && ev[e][0] < f0 + m){ E.trigger(ev[e][1], ev[e][2], Math.max(0, ev[e][0] - f0), gate); e++; }
    E.process(bl, br, m); L.set(bl.subarray(0, m), f0); Rr.set(br.subarray(0, m), f0);
  }
  const data = new DataView(new ArrayBuffer(44 + n * 4));
  let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(L[i]), Math.abs(Rr[i]));
  if (pk > .98){ const k = .98 / pk; for (let i = 0; i < n; i++){ L[i] *= k; Rr[i] *= k; } }   // no clipping in the file: turned down to fit
  const w = (o, s) => [...s].forEach((ch, i) => data.setUint8(o + i, ch.charCodeAt(0)));
  w(0, 'RIFF'); data.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); data.setUint32(16, 16, true); data.setUint16(20, 1, true); data.setUint16(22, 2, true);
  data.setUint32(24, sr, true); data.setUint32(28, sr * 4, true); data.setUint16(32, 4, true); data.setUint16(34, 16, true); w(36, 'data'); data.setUint32(40, n * 4, true);
  for (let i = 0, o = 44; i < n; i++, o += 4){ data.setInt16(o, clamp(L[i], -1, 1) * 32767, true); data.setInt16(o + 2, clamp(Rr[i], -1, 1) * 32767, true); }
  const name = `Omega DMX ${k.name} ${useSong ? 'song' : 'pattern ' + (curPat() + 1)} ${S.tempo}bpm.wav`;
  try { const where = await dmxSaveFile(new Blob([data], {type:'audio/wav'}), name); toast('Saved ' + where, 4000); } catch(e){ toast('Export failed: ' + e.message, 4000); }
}

/* ---------------- MIDI: notes play the kit (GM drum notes, and each voice from C3 up), clock and start / stop ---------------- */
const noteVoice = n => dmxNoteVoice(kit(), n);
function midiIn(e){
  const [st, d1, d2] = e.data, ty = st & 0xf0;
  $('#midiLed').classList.add('on'); clearTimeout(midiIn.t); midiIn.t = setTimeout(() => $('#midiLed').classList.remove('on'), 120);
  if (ty === 0x90 && d2 > 0){ const vi = noteVoice(d1); if (vi >= 0){ R.sel = vi; play(vi, d2 >= 100 || R.acc ? 1 : Math.max(.25, d2 / 127 * .9), -1); paint(); } return; }
  if (ty === 0x80 || (ty === 0x90 && d2 === 0)){ const vi = noteVoice(d1); if (vi >= 0) post({type:'release', vi}); return; }   // note off: the pad's ADSR releases
  if (st === 0xFA || st === 0xFB){ if (R.playing) stop(); R.ext = {n:0, last:0, bpm:0}; start(); return; }
  if (st === 0xFC){ stop(); R.ext = null; return; }
  if (st === 0xF8 && R.ext && R.playing && R.ctx){
    const now = performance.now();
    if (R.ext.last){ const bpm = 60000 / ((now - R.ext.last) * 24); R.ext.bpm = R.ext.bpm ? R.ext.bpm * .92 + bpm * .08 : bpm; if (R.ext.n % 24 === 0) S.tempo = Math.round(clamp(R.ext.bpm, 40, 250)); }
    R.ext.last = now;
    if (R.ext.n % 6 === 0){ R.nextT = R.ctx.currentTime + .005; scheduler1(); }
    R.ext.n++;
  }
}
// one step now (MIDI clock drives the sequencer step by step)
function scheduler1(){
  if (R.step === 16){ R.step = 0; R.play = nextPattern(); paint(); }
  const p = pats()[R.play], t = R.nextT;
  p.rows.forEach((row, vi) => { if (row[R.step]) sendHit(vi, t, VEL[row[R.step]], stepDur()); });
  if (clickOn() && R.step % 4 === 0) clickAt(t, R.step === 0);
  R.q.push({t, step:R.step, pat:R.play}); if (R.q.length > 64) R.q.shift(); R.step++;
}
function hookMidi(){
  if (!navigator.requestMIDIAccess) return;
  navigator.requestMIDIAccess({sysex:false}).then(a => { const hook = () => a.inputs.forEach(p => { p.onmidimessage = midiIn; }); a.onstatechange = hook; hook(); }).catch(() => {});
}

/* ---------------- layout: one panel scaled to the screen ---------------- */
function fit(){
  const W = innerWidth, H = innerHeight, el = $('#dmx'), land = W > H * 1.05;
  el.classList.toggle('land', land); el.classList.toggle('port', !land);
  const w = land ? 1280 : 720, h = land ? 720 : 1380, s = Math.min(W / w, H / h);
  el.style.transform = `translate(${(W - w * s) / 2}px, ${(H - h * s) / 2}px) scale(${s})`;
}

/* ---------------- the Omega pages (as on the Omega 110 and Omega Traks) ----------------
   VOICE MOD (each card's circuit mods), SOUND (level, pan, decay, filter, start, mute, sends), PAD EQ (EQ, ADSR, drive),
   FX (the REV7-style reverb and the TC 2290-style delay), MASTER (EQ, overdrive) and PRESETS (the 100, the dice, yours). */
const E_ = DMXE, PNAME = DMXE.PARAMS.map(q => q[0]);
const ENUMS = {rvProg:E_.RV_PROGS, dlSync:E_.DL_DIV_NAMES, odMode:E_.OD_MODES, dmod:E_.DECAY_MODS, early:E_.START_MODS, filt:E_.FILTER_MODS, dac:E_.DAC_MODS};
const TOGGLES = new Set(['rev', 'mute', 'dlPing']);
const PAGES = [['mods', 'VOICE MOD'], ['sound', 'SOUND'], ['padeq', 'PAD EQ'], ['fx', 'FX'], ['master', 'MASTER'], ['presets', 'PRESETS']];
R.page = ''; R.card = 0; R.family = 0;
S.preset = S.preset ?? 0; S.userPresets = S.userPresets || [];
const fieldOf = id => { const i = PNAME.indexOf(id); return i >= E_.NG ? id.slice(2) : id; };
const isLog = q => q[2] > 0 && q[3] / q[2] >= 20;
const toNorm = (q, v) => isLog(q) ? Math.log(v / q[2]) / Math.log(q[3] / q[2]) : (v - q[2]) / (q[3] - q[2]);
const fromNorm = (q, n) => isLog(q) ? q[2] * Math.pow(q[3] / q[2], n) : q[2] + (q[3] - q[2]) * n;
function fmtParam(id, v){
  const f = fieldOf(id);
  if (ENUMS[f]) return ENUMS[f][Math.round(v)];
  if (f === 'cut' && v >= 19999) return 'OFF';
  if (/F$/.test(f) || f === 'cut' || f === 'rvCut' || f === 'dlHiCut' || f === 'dlLoCut') return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 0 : 1) + 'k' : Math.round(v) + '';
  if (/G$/.test(f) || f === 'odOut') return (v > 0 ? '+' : '') + v.toFixed(1);
  if (f === 'dec') return v >= 2.999 ? 'OFF' : v.toFixed(2) + 's';
  if (f === 'rel') return v >= 3.999 ? 'HOLD' : v.toFixed(2) + 's';
  if (f === 'tune') return (v > 0 ? '+' : '') + Math.round(v);
  if (f === 'pan') return Math.abs(v) < .01 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v) * 100);
  if (f === 'cap') return '×' + v.toFixed(2);
  if (f === 'dlTime' || f === 'rvDelay') return Math.round(v) + 'ms';
  if (f === 'rvTime' || f === 'atk' || f === 'dcy') return v.toFixed(2) + 's';
  return Math.abs(v) >= 100 ? Math.round(v) + '' : v.toFixed(2);
}
function knobSvg(n){ const a0 = -135, a = a0 + 270 * n, rad = d => (d - 90) * Math.PI / 180, pt = (d, r) => [20 + r * Math.cos(rad(d)), 20 + r * Math.sin(rad(d))];
  const arc = (from, to, r) => { const [x0, y0] = pt(from, r), [x1, y1] = pt(to, r); return `M${x0.toFixed(1)} ${y0.toFixed(1)}A${r} ${r} 0 ${to - from > 180 ? 1 : 0} 1 ${x1.toFixed(1)} ${y1.toFixed(1)}`; };
  const [lx, ly] = pt(a, 11);
  return `<svg viewBox="0 0 40 40"><path d="${arc(a0, 135, 16)}" stroke="#2a1010" stroke-width="3.5" fill="none" stroke-linecap="round"/>${n > .001 ? `<path d="${arc(a0, a, 16)}" stroke="#ff3a2a" stroke-width="3.5" fill="none" stroke-linecap="round"/>` : ''}<circle cx="20" cy="20" r="12" fill="url(#kg)" stroke="#000"/><line x1="20" y1="20" x2="${lx.toFixed(1)}" y2="${ly.toFixed(1)}" stroke="#eee" stroke-width="2.4" stroke-linecap="round"/><defs><radialGradient id="kg" cx=".4" cy=".35"><stop offset="0" stop-color="#5a5b62"/><stop offset="1" stop-color="#16171a"/></radialGradient></defs></svg>`; }
// a control for the parameter `id`: a knob (drag up / down, double-tap for its default) or a switch
function ctl(parent, id, label){
  const i = PNAME.indexOf(id), q = E_.PARAMS[i], f = fieldOf(id);
  if (TOGGLES.has(f)){
    const b = document.createElement('button'); b.className = 'tg'; b.innerHTML = `<i></i>${label}`;
    const show = () => b.classList.toggle('on', S.params[i] >= .5); show();
    b.onclick = () => { setParam(i, S.params[i] >= .5 ? 0 : 1); show(); }; parent.appendChild(b); return;
  }
  const w = document.createElement('div'); w.className = 'kn'; w.title = q[1];
  const draw = () => { w.innerHTML = knobSvg(toNorm(q, S.params[i])) + `<b>${label}</b><span>${fmtParam(id, S.params[i])}</span>`; }; draw();
  const set = n => { let v = fromNorm(q, clamp(n, 0, 1)); if (ENUMS[f]) v = Math.round(v); else v = Math.round(v * 1000) / 1000; if (v !== S.params[i]){ setParam(i, v); draw(); } };
  let y0 = 0, n0 = 0, lastTap = 0;
  w.addEventListener('pointerdown', e => { e.preventDefault(); w.setPointerCapture(e.pointerId); y0 = e.clientY; n0 = toNorm(q, S.params[i]);
    const now = Date.now(); if (now - lastTap < 320){ setParam(i, q[4]); draw(); } lastTap = now;
    w.onpointermove = ev => set(n0 + (y0 - ev.clientY) / (ENUMS[f] ? 60 : 160) * (ev.shiftKey ? .2 : 1)); });
  const up = () => { w.onpointermove = null; }; w.addEventListener('pointerup', up); w.addEventListener('pointercancel', up);
  w.addEventListener('wheel', e => { e.preventDefault(); set(toNorm(q, S.params[i]) - Math.sign(e.deltaY) * (ENUMS[f] ? 1 / Math.max(1, q[3] - q[2]) : .02)); }, {passive:false});
  parent.appendChild(w);
}
function sec(parent, title){ const d = document.createElement('div'); d.className = 'sec'; d.innerHTML = `<h5>${title}</h5><div></div>`; parent.appendChild(d); return d.lastChild; }
function cardBar(head){
  const k = kit(), has = new Set(k.voices.map((v, vi) => placeOf(k, vi).c));
  const d = document.createElement('div'); d.className = 'cards';
  E_.CARD_NAMES.forEach((n, c) => { const b = document.createElement('button'); b.textContent = n.toUpperCase(); b.className = (c === R.card ? 'on' : '') + (has.has(c) ? '' : ' none'); b.onclick = () => { R.card = c; buildPage(); }; d.appendChild(b); });
  head.appendChild(d);
  const vs = k.voices.map((v, vi) => [v, vi]).filter(([, vi]) => placeOf(k, vi).c === R.card).map(([, vi]) => vname(vi));
  head.insertAdjacentHTML('beforeend', `<i>${vs.length ? vs.join(' · ') : 'no voice of this kit on this card'}</i>`);
}
function buildPage(){
  const pg = $('#page'), id = R.page; pg.innerHTML = '';
  $('#dmx').classList.toggle('paged', !!id);
  document.querySelectorAll('#pageBtns button').forEach(b => b.classList.toggle('on', b.dataset.p === id));
  if (!id) return;
  const head = document.createElement('div'); head.className = 'phead'; head.innerHTML = `<b>${PAGES.find(p => p[0] === id)[1]}</b>`; pg.appendChild(head);
  const body = document.createElement('div'); body.className = 'pbody'; pg.appendChild(body);
  const C = E_.CARD_IDS[R.card];
  if (id === 'mods'){
    cardBar(head);
    let s1 = sec(body, 'CLOCK');
    ctl(s1, C + 'tune', 'TUNE');
    s1 = sec(body, 'DECAY (C3, EG)'); ctl(s1, C + 'cap', 'CAP'); ctl(s1, C + 'dmod', 'DECAY'); ctl(s1, C + 'early', 'START');
    s1 = sec(body, 'OUTPUT FILTER'); ctl(s1, C + 'filt', 'FILTER');
    s1 = sec(body, 'DAC AND ROM'); ctl(s1, C + 'dac', 'DAC'); ctl(s1, C + 'rev', 'REVERSE');
    body.insertAdjacentHTML('beforeend', `<p style="flex-basis:100%;margin:0;font:700 11px var(--cond);color:var(--dim);letter-spacing:.04em">${circuitOn() ? 'Mods to the voice card: TUNE moves its 555 trimmer for every voice on the card, CAP changes the decay capacitor C3, DECAY fits or removes the decay envelope, START moves where the decay begins (counter bit 6 or 10), FILTER swaps the card\'s Sallen-Key set (LOW ~10 kHz, HIGH ~16 kHz) or bypasses it, DAC swaps the AM6070 for a linear 8- or 6-bit DAC, REVERSE reads the ROM backwards.' : 'CIRCUIT is CLEAN: only TUNE and REVERSE act. Switch CIRCUIT to DMX for the card mods.'}</p>`);
  } else if (id === 'sound'){
    cardBar(head);
    let s1 = sec(body, 'LEVEL'); ctl(s1, C + 'lv', 'LEVEL'); ctl(s1, C + 'pan', 'PAN'); ctl(s1, C + 'mute', 'MUTE');
    s1 = sec(body, 'SHAPE'); ctl(s1, C + 'dec', 'DECAY'); ctl(s1, C + 'start', 'START');
    s1 = sec(body, 'FILTER'); ctl(s1, C + 'cut', 'CUTOFF'); ctl(s1, C + 'res', 'RESO');
    s1 = sec(body, 'SENDS'); ctl(s1, C + 'snd', 'REV7'); ctl(s1, C + 'dls', 'DELAY');
  } else if (id === 'padeq'){
    cardBar(head);
    let s1 = sec(body, 'EQ'); ctl(s1, C + 'lsF', 'LOW F'); ctl(s1, C + 'lsG', 'LOW'); ctl(s1, C + 'pkF', 'MID F'); ctl(s1, C + 'pkG', 'MID'); ctl(s1, C + 'pkQ', 'MID Q'); ctl(s1, C + 'hsF', 'HIGH F'); ctl(s1, C + 'hsG', 'HIGH');
    s1 = sec(body, 'ADSR'); ctl(s1, C + 'atk', 'ATTACK'); ctl(s1, C + 'dcy', 'DECAY'); ctl(s1, C + 'sus', 'SUSTAIN'); ctl(s1, C + 'rel', 'RELEASE');
    s1 = sec(body, 'DRIVE'); ctl(s1, C + 'drv', 'DRIVE');
  } else if (id === 'fx'){
    head.insertAdjacentHTML('beforeend', '<i>sends: SOUND page, per card</i>');
    let s1 = sec(body, 'REV7 REVERB');
    for (const [p, l] of [['rvProg', 'PROGRAM'], ['rvTime', 'RT'], ['rvHigh', 'HIGH'], ['rvDelay', 'DELAY'], ['rvDiff', 'DIFFUSION'], ['rvEr', 'ER LEVEL'], ['rvCut', 'HIGH CUT'], ['rvLevel', 'RETURN']]) ctl(s1, p, l);
    s1 = sec(body, 'TC 2290 DELAY');
    for (const [p, l] of [['dlTime', 'TIME'], ['dlSync', 'SYNC'], ['dlFb', 'FEEDBACK'], ['dlHiCut', 'HIGH CUT'], ['dlLoCut', 'LOW CUT'], ['dlModRate', 'MOD RATE'], ['dlModDepth', 'MOD DEPTH'], ['dlDuck', 'DUCKING'], ['dlPing', 'PING-PONG'], ['dlLevel', 'RETURN']]) ctl(s1, p, l);
  } else if (id === 'master'){
    let s1 = sec(body, 'MASTER EQ');
    for (const [p, l] of [['mLsF', 'LOW F'], ['mLsG', 'LOW'], ['mP1F', 'MID1 F'], ['mP1G', 'MID 1'], ['mP1Q', 'MID1 Q'], ['mP2F', 'MID2 F'], ['mP2G', 'MID 2'], ['mP2Q', 'MID2 Q'], ['mHsF', 'HIGH F'], ['mHsG', 'HIGH']]) ctl(s1, p, l);
    s1 = sec(body, 'OVERDRIVE'); for (const [p, l] of [['odDrive', 'DRIVE'], ['odMode', 'MODE'], ['odMix', 'MIX'], ['odOut', 'OUTPUT']]) ctl(s1, p, l);
  } else if (id === 'presets'){
    const P_ = DMXPRESETS, cur = S.preset;
    head.insertAdjacentHTML('beforeend', `<i>${typeof cur === 'number' ? P_.PRESETS[cur].name : cur}${S.edited ? ' (edited)' : ''}</i>`);
    const btn = (parent, t, fn, on) => { const b = document.createElement('button'); b.className = 'pbtn' + (on ? ' on' : ''); b.textContent = t; b.onclick = fn; parent.appendChild(b); return b; };
    const fam = document.createElement('div'); fam.className = 'cards'; body.appendChild(fam);
    P_.FAMILIES.forEach((f, fi) => btn(fam, f.name.toUpperCase(), () => { R.family = fi; buildPage(); }, fi === R.family));
    if (S.userPresets.length) btn(fam, 'YOURS', () => { R.family = -1; buildPage(); }, R.family === -1);
    const list = document.createElement('div'); list.className = 'plist'; body.appendChild(list);
    if (R.family >= 0) P_.PRESETS.forEach((pr, pi) => { if (pr.family !== P_.FAMILIES[R.family].name) return; const b = btn(list, pr.name.split(' · ')[1], () => loadPreset(pi), pi === cur); b.className = pi === cur ? 'on' : ''; });
    else S.userPresets.forEach((u, ui) => { const b = btn(list, u.name, () => loadPreset(u.name), u.name === cur); b.className = u.name === cur ? 'on' : ''; });
    const row = document.createElement('div'); row.className = 'cards'; body.appendChild(row);
    btn(row, '◀', () => stepPreset(-1)); btn(row, '▶', () => stepPreset(1));
    row.insertAdjacentHTML('beforeend', '<span style="width:10px"></span>');
    for (const part of P_.DICE_PARTS) btn(row, '🎲 ' + part.toUpperCase(), () => { setAllParams(P_.dice(S.params, part)); S.preset = 'Dice'; S.edited = false; save(); toast('Dice: ' + part); buildPage(); });
    row.insertAdjacentHTML('beforeend', '<span style="width:10px"></span>');
    btn(row, 'SAVE AS…', () => { const n = (prompt('Name for your preset', typeof cur === 'string' ? cur : '') || '').trim(); if (!n) return; S.userPresets = S.userPresets.filter(u => u.name !== n).concat([{name:n, p:S.params.slice()}]); S.preset = n; S.edited = false; save(); R.family = -1; buildPage(); toast('Saved ' + n); });
    if (typeof cur === 'string' && S.userPresets.some(u => u.name === cur)) btn(row, 'DELETE', () => { S.userPresets = S.userPresets.filter(u => u.name !== cur); S.preset = 0; save(); buildPage(); });
    btn(row, 'RESET', () => { setAllParams(DMXE.defaults()); S.preset = 0; S.edited = false; save(); buildPage(); });
  }
}
function loadPreset(which){
  const P_ = DMXPRESETS, u = typeof which === 'string' ? S.userPresets.find(x => x.name === which) : null;
  setAllParams(u ? u.p : P_.PRESETS[which].p); S.preset = which; S.edited = false; save(); buildPage(); toast(u ? u.name : P_.PRESETS[which].name);
}
function stepPreset(d){ const n = DMXPRESETS.PRESETS.length, cur = typeof S.preset === 'number' ? S.preset : 0, nx = (cur + d + n) % n; R.family = DMXPRESETS.FAMILIES.findIndex(f => f.name === DMXPRESETS.PRESETS[nx].family); loadPreset(nx); }
function buildPageBtns(){
  $('#pageBtns').innerHTML = PAGES.map(([id, n]) => `<button data-p="${id}">${n}</button>`).join('');
  $('#pageBtns').onclick = e => { const b = e.target.closest('button'); if (!b) return; R.page = R.page === b.dataset.p ? '' : b.dataset.p; if (R.page && R.page !== 'presets' && R.page !== 'fx' && R.page !== 'master'){ const k = kit(); R.card = placeOf(k, R.sel).c; } buildPage(); };
}

/* ---------------- start ---------------- */
buildFaders(); buildPad(); buildFn(); buildPageBtns(); rebuild(); fit();
addEventListener('resize', fit); addEventListener('orientationchange', () => setTimeout(fit, 200));
$('#play').addEventListener('pointerdown', e => { e.preventDefault(); if (R.playing) stop(); else start(); });
$('#exportBtn').addEventListener('click', exportWav);
$('#circBtn').addEventListener('click', () => { const k = kit(); S.circuit[k.id] = !circuitOn(k); save(); rerender(); levels(); paint(); toast(circuitOn(k) ? 'Circuit: the DMX voice cards (ROM, 555 clock, AM6070, filters, mixer)' : 'Circuit off: the sounds as recorded'); });
$('#helpBtn').addEventListener('click', () => $('#helpDlg').showModal());
document.querySelectorAll('dialog [data-close]').forEach(b => b.addEventListener('click', () => b.closest('dialog').close()));
addEventListener('keydown', e => {
  if (document.querySelector('dialog[open]')) return;
  if (e.code === 'Space'){ e.preventDefault(); R.playing ? stop() : start(); }
  else if (/^\d$/.test(e.key)) keypad(e.key);
});
document.addEventListener('pointerdown', () => ensureAudio(), {once:true});
hookMidi(); requestAnimationFrame(frame);
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidDmx) navigator.serviceWorker.register('sw.js').catch(() => {});
