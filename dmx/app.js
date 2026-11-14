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

/* ---------------- audio ---------------- */
const R = {ctx:null, out:null, groups:{}, bufs:{}, src:{}, roms:{}, playing:false, step:0, nextT:0, timer:0, q:[], play:null, queued:-1, songPos:0, open:[], ext:null, sel:0, acc:false, rec:false, mode:'', typed:'', clearArm:0};
function ensureAudio(){
  if (R.ctx){ if (R.ctx.state !== 'running') R.ctx.resume(); return R.ctx; }
  const C = window.AudioContext || window.webkitAudioContext; R.ctx = new C({latencyHint:'interactive'});
  R.out = R.ctx.createGain(); R.out.connect(R.ctx.destination);
  for (const [g] of DMX_FADERS) if (g !== 'vol' && g !== 'met'){ R.groups[g] = R.ctx.createGain(); R.groups[g].connect(R.out); }
  levels(); return R.ctx;
}
// fader to gain: the same curve in the plugin
// faders: the clean engine's curve, or (CIRCUIT) the DMX's audio-taper pots; both sit at the same level at .8
const fgain = v => v * v * 1.25;
const circuitOn = (k = kit()) => S.circuit[k.id] != null ? !!S.circuit[k.id] : k.id !== 'dmx';   // the DMX kit is recorded from a DMX already
const fader = (v, k) => circuitOn(k) ? DMXC.potLaw(v) * DMXC.MIX_K : fgain(v);
const tuneOf = (vi, k = kit()) => ((S.tune[k.id] || {})[vi] ?? 50);
function levels(){ if (!R.ctx) return; for (const g in R.groups) R.groups[g].gain.setTargetAtTime(fader(S.fader[g]), R.ctx.currentTime, .01); R.out.gain.setTargetAtTime(fader(S.fader.vol), R.ctx.currentTime, .01); }
// a kit: the WAVs read at their own rate (as the plugin reads them), each voice's ROM for its card, and what plays:
// the card's output rendered through the circuit model, or (CIRCUIT off) the sound itself
async function loadKit(k){
  if (R.bufs[k.id]) return R.bufs[k.id];
  const c = ensureAudio();
  R.src[k.id] = await Promise.all(k.voices.map(async v => { try { return DMXC.parseWav(await (await fetch('kits/' + k.id + '/' + v[0] + '.wav')).arrayBuffer()); } catch(e){ return null; } }));
  R.roms[k.id] = k.voices.map((v, vi) => { const s = R.src[k.id][vi]; return s ? DMXC.makeRom(s.x, s.sr, DMXC.CARDS[DMXC.cardFor(k, vi)[1]]) : null; });
  R.bufs[k.id] = k.voices.map((v, vi) => voiceBuf(c, k, vi));
  return R.bufs[k.id];
}
function voiceBuf(c, k, vi, rate = c.sampleRate){
  const s = R.src[k.id] && R.src[k.id][vi]; if (!s) return null;
  let d, sr;
  if (circuitOn(k)){ const [, cn, mode] = DMXC.cardFor(k, vi); d = DMXC.render(R.roms[k.id][vi], DMXC.CARDS[cn], mode, tuneOf(vi, k), rate); sr = rate; }
  else { d = Float32Array.from(s.x); sr = s.sr; }
  if (!d.length) return null;
  const b = c.createBuffer(1, d.length, sr); b.copyToChannel(d, 0); return b;
}
// re-render after TUNE or CIRCUIT changes (one voice, or the whole kit)
let retuneT = 0;
function rerender(vi){ const k = kit(); if (!R.bufs[k.id]) return; clearTimeout(retuneT);
  retuneT = setTimeout(() => { const c = ensureAudio(); if (vi == null) R.bufs[k.id] = k.voices.map((v, i) => voiceBuf(c, k, i)); else R.bufs[k.id][vi] = voiceBuf(c, k, vi); levels(); }, 120); }
const isOpen = dmxIsOpen;
// one hit: the voice through its fader group; a closed or medium hat chokes a ringing open hat
function hit(c, dest, open, k, bufs, vi, when, vel){
  const b = bufs && bufs[vi]; if (!b) return;
  const v = k.voices[vi], to = dest[v[2]] || dest.vol;
  if (v[2] === 'hat' && !isOpen(v)) for (const o of open.splice(0)){ try { o.g.gain.setTargetAtTime(0, when, .004); o.s.stop(when + .05); } catch(e){} }
  const s = c.createBufferSource(), g = c.createGain(); s.buffer = b; g.gain.value = vel; s.connect(g);
  if (circuitOn(k)){   // the card's place in the DMX's stereo mix
    const [pl, pr] = DMXC.pan(DMXC.CARDS[DMXC.cardFor(k, vi)[1]]), m = c.createChannelMerger(2), l = c.createGain(), r = c.createGain();
    l.gain.value = pl; r.gain.value = pr; g.connect(l); g.connect(r); l.connect(m, 0, 0); r.connect(m, 0, 1); m.connect(to);
  } else {   // clean: the trimmer moves the pitch as on the hardware (the clock's ratio to its centre)
    const [, cn, mode] = DMXC.cardFor(k, vi), card = DMXC.CARDS[cn]; s.playbackRate.value = DMXC.rate(card, mode, tuneOf(vi, k)) / DMXC.rate(card, mode, 50); g.connect(to);
  }
  s.start(when);
  if (isOpen(v)){ const o = {s, g}; open.push(o); s.onended = () => { const i = open.indexOf(o); if (i >= 0) open.splice(i, 1); }; }
}
// the metronome: its own fader, straight to the output (the plugin does the same)
const clickOn = () => S.fader.met > .02;
// the metronome: the DMX's 1 ms pulse, loud on the bar and quiet on the beats, through its own fader
function clickAt(c, dest, when, down){
  const b = c.createBuffer(1, Math.max(1, Math.round(c.sampleRate * .001)), c.sampleRate); b.getChannelData(0).fill(1);
  const s = c.createBufferSource(), g = c.createGain(); s.buffer = b; g.gain.value = DMXC.MET_V[down ? 0 : 1] * fader(S.fader.met); s.connect(g); g.connect(dest); s.start(when);
}

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
  const c = R.ctx, k = kit(), bufs = R.bufs[k.id];
  while (R.nextT < c.currentTime + .12){
    if (R.step === 16){ R.step = 0; R.play = nextPattern(); paint(); }
    const p = pats()[R.play], t = R.nextT + swingOff(R.step);
    p.rows.forEach((row, vi) => { if (row[R.step] && vi < k.voices.length) hit(c, R.groups, R.open, k, bufs, vi, t, VEL[row[R.step]]); });
    if (clickOn() && R.step % 4 === 0) clickAt(c, c.destination, t, R.step === 0);
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
async function play(vi, vel){
  const c = ensureAudio(), k = kit(), bufs = await loadKit(k);
  hit(c, R.groups, R.open, k, bufs, vi, c.currentTime, vel); flash(vi);
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
function setTune(k, vi, v){ (S.tune[k.id] = S.tune[k.id] || {})[vi] = clamp(Math.round(v), 0, 100); save(); if (circuitOn(k)) rerender(vi); play(vi, VEL[2]); }
function paintCircuit(){ const b = $('#circBtn'); b.textContent = 'CIRCUIT: ' + (circuitOn() ? 'DMX' : 'CLEAN'); b.classList.toggle('on', circuitOn()); }

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
  const k = kit(), bufs = await loadKit(k), sr = 48000, sg = song(), useSong = S.useSong[S.kit] && sg.length;
  const order = useSong ? sg : [curPat(), curPat(), curPat(), curPat()], dur = order.length * 16 * stepDur() + 2;
  const c = new OfflineAudioContext(2, Math.ceil(dur * sr), sr), out = c.createGain(), groups = {}, open = [];
  out.gain.value = fader(S.fader.vol); out.connect(c.destination);
  for (const [g] of DMX_FADERS) if (g !== 'vol' && g !== 'met'){ groups[g] = c.createGain(); groups[g].gain.value = fader(S.fader[g]); groups[g].connect(out); }
  let t = .02;
  for (const pi of order) for (let s = 0; s < 16; s++, t += stepDur()){ const row = pats()[pi].rows; row.forEach((r, vi) => { if (r[s]) hit(c, groups, open, k, bufs, vi, t + swingOff(s), VEL[r[s]]); }); }
  toast('Rendering…', 900);
  const b = await c.startRendering(), n = Math.min(b.length, Math.ceil((t + 1.5) * sr));
  const L = b.getChannelData(0), Rr = b.getChannelData(1), data = new DataView(new ArrayBuffer(44 + n * 4));
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
  if (ty === 0x90 && d2 > 0){ const vi = noteVoice(d1); if (vi >= 0){ R.sel = vi; play(vi, d2 >= 100 || R.acc ? 1 : Math.max(.25, d2 / 127 * .9)); paint(); } return; }
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
  const c = R.ctx, k = kit(), bufs = R.bufs[k.id];
  if (R.step === 16){ R.step = 0; R.play = nextPattern(); paint(); }
  const p = pats()[R.play], t = R.nextT;
  p.rows.forEach((row, vi) => { if (row[R.step]) hit(c, R.groups, R.open, k, bufs, vi, t, VEL[row[R.step]]); });
  if (clickOn() && R.step % 4 === 0) clickAt(c, c.destination, t, R.step === 0);
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

/* ---------------- start ---------------- */
buildFaders(); buildPad(); buildFn(); rebuild(); fit();
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
