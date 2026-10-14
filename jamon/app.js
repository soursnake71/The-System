/* Jam-On Rig: the gear from Newcleus's Jam On Revenge in one app. Engine, sequencer and UI. */
'use strict';
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const nn = n => NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
const ANA_BASE = {
  juno60:{filter:'ir3109', voices:6, vco:0, w1:0, m1:1, w2:1, m2:0, sync:1, i2:0, sub:.3, chorus:1, hpf:0, ampGate:0, kf:.5},
  sh101:{filter:'ir3109', mono:1, voices:1, vco:1, w1:1, m1:.8, w2:0, m2:.8, sync:1, i2:0, sub:.5, chorus:0, glide:0, kf:.6}
};
const toast = (msg, ms = 2600) => { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('on'), ms); };

/* ================= songs: notation -> editable data ================= */
function noteNum(t){ const m = /^([A-G])([#b]?)(-?\d)$/.exec(t); if (!m) return null; return 12 * (+m[3] + 1) + {C:0, D:2, E:4, F:5, G:7, A:9, B:11}[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0); }
function parseLine(str, steps){
  const tk = String(str || '').trim().split(/\s+/).filter(Boolean); if (!tk.length) return [];
  const out = []; let last = [];
  for (let s = 0; s < steps; s++){
    let t = tk[s % tk.length];
    if (t === '-'){ last.forEach(o => o.l++); continue; }
    last = []; if (t === '.') continue;
    let a = 0, sl = 0; if (t[0] === '!'){ a = 1; t = t.slice(1); } if (t.endsWith('~')){ sl = 1; t = t.slice(0, -1); }
    for (const nm of t.split('+')){ const n = noteNum(nm); if (n != null){ const o = {s, n, l:1, a, sl}; out.push(o); last.push(o); } }
  }
  return out;
}
// notes -> notation (one token per step), for the text editor and for copying
function toLine(notes, steps){
  const by = new Map(); for (const o of notes) (by.get(o.s) || by.set(o.s, []).get(o.s)).push(o);
  const tk = []; let holdTo = -1;
  for (let s = 0; s < steps; s++){
    const g = by.get(s);
    if (g){ g.sort((a, b) => a.n - b.n); tk.push((g[0].a ? '!' : '') + g.map(o => nn(o.n)).join('+') + (g[0].sl ? '~' : '')); holdTo = s + Math.max(...g.map(o => o.l)); }
    else tk.push(s < holdTo ? '-' : '.');
  }
  return tk.join(' ');
}
const fillDrum = (str, steps) => { let o = ''; if (!str) return '.'.repeat(steps); while (o.length < steps) o += str; return o.slice(0, steps); };
function compileSong(def){
  const song = {name:def.name, bpm:def.bpm, swing:def.swing || 0, key:def.key || '', note:def.note || '', presets:Object.assign({}, def.presets || {}), carrier:def.carrier || 'juno', arp:JSON.parse(JSON.stringify(def.arp || {})), sections:[]};
  for (const sd of def.sections){
    const steps = sd.bars * 16, sec = {name:sd.name, bars:sd.bars, rep:sd.rep || 1, drums:{}, notes:{}, say:(sd.say || []).map(([bar, text]) => ({bar, text}))};
    for (const id of SEQ_MACH){
      const M = MACH[id];
      if (M.kind === 'drum'){ sec.drums[id] = {}; for (const [v] of M.voices) sec.drums[id][v] = fillDrum(sd[id] && sd[id][v], steps); }
      else sec.notes[id] = parseLine(sd[id], steps);
    }
    song.sections.push(sec);
  }
  return song;
}
const emptySection = (name, bars) => { const sec = {name, bars, rep:1, drums:{}, notes:{}, say:[]}; for (const id of SEQ_MACH){ const M = MACH[id]; if (M.kind === 'drum'){ sec.drums[id] = {}; for (const [v] of M.voices) sec.drums[id][v] = '.'.repeat(bars * 16); } else sec.notes[id] = []; } return sec; };

/* ================= state ================= */
const STORE = 'jamon-rig-v1';
function defaultState(){
  const mach = {}; for (const id in MACH) mach[id] = {p:MACH[id].def(), preset:'', vol:.8, pan:0, mute:false, solo:false, arp:{mode:'off', oct:1, rate:16}};
  return {v:1, songs:SONGS.map(compileSong), cur:0, sec:0, bar:0, tab:'song', mach, voc:{p:VOC_DEF(), preset:'', tts:{voice:'robot', semi:0, speed:1}, carrier:'juno', direct:false, sayText:'welcome to the rig.', sayRoot:52, sayChord:'min'},
    master:.75, loopSec:false, midiMode:'selected', midiCh:Object.fromEntries(Object.entries(MACH).map(([id, M]) => [id, M.ch])), user:{}, kbOct:4, rollOct:3, len:1, tool:'draw'};
}
let S;
try { S = JSON.parse(localStorage.getItem(STORE)); } catch(e){ S = null; }
if (!S || S.v !== 1) S = defaultState();
{ const D = defaultState(); for (const k in D) if (S[k] === undefined) S[k] = D[k]; for (const id in MACH) if (!S.mach[id]) S.mach[id] = D.mach[id]; }
let saveT = 0; const save = () => { clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify(S)); } catch(e){} }, 400); };
const song = () => S.songs[S.cur] || S.songs[0], section = () => song().sections[clamp(S.sec, 0, song().sections.length - 1)];

/* ================= audio graph ================= */
let ctx = null, R = null, audioReady = null;
const cfgFor = id => { const M = MACH[id], p = S.mach[id].p; return M.proc === 'ana' ? Object.assign({model:M.model}, ANA_BASE[M.model], p) : Object.assign({}, p); };
// builds every machine, the vocoder and the mixer on a context; ev = {id: [events]} for offline renders
function buildRig(c, ev){
  const r = {c, nodes:{}, strip:{}, pan:{}, carrier:{}, drumIn:{}};
  r.masterIn = c.createGain(); r.master = c.createGain(); r.master.gain.value = S.master;
  r.lim = c.createDynamicsCompressor(); Object.assign(r.lim, {}); r.lim.threshold.value = -3; r.lim.knee.value = 0; r.lim.ratio.value = 20; r.lim.attack.value = .002; r.lim.release.value = .12;
  r.masterIn.connect(r.master); r.master.connect(r.lim); r.lim.connect(c.destination);
  r.musicBus = c.createGain(); r.micBus = c.createGain();
  r.voc = new AudioWorkletNode(c, 'ehvocoder', {numberOfInputs:2, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:S.voc.p}});
  r.musicBus.connect(r.voc, 0, 0); r.micBus.connect(r.voc, 0, 1);
  const strip = id => { const g = c.createGain(), p = c.createStereoPanner(); g.connect(p); p.connect(r.masterIn); r.strip[id] = g; r.pan[id] = p; return g; };
  r.voc.connect(strip('voc'));
  for (const id in MACH){
    const M = MACH[id], st = strip(id);
    if (M.kind === 'drum'){ const g = c.createGain(); g.connect(st); r.drumIn[id] = g; continue; }
    const n = new AudioWorkletNode(c, M.proc, {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:cfgFor(id), events:(ev && ev[id]) || []}});
    n.connect(st); const cs = c.createGain(); cs.gain.value = 0; n.connect(cs); cs.connect(r.musicBus); r.carrier[id] = cs; r.nodes[id] = n;
  }
  r.voc.port.onmessage = e => { if (e.data.meter) vocMeter(e.data.meter); };
  applyMix(r);
  return r;
}
function applyMix(r = R){
  if (!r) return;
  const solo = Object.keys(S.mach).some(id => S.mach[id].solo) || (S.vocMix && S.vocMix.solo);
  for (const id in MACH){
    const m = S.mach[id], isCar = S.voc.carrier === id;
    let g = m.vol * m.vol * 1.2; if (m.mute || (solo && !m.solo)) g = 0; if (isCar && !S.voc.direct) g = 0;
    r.strip[id].gain.value = g; r.pan[id].pan.value = m.pan;
    if (r.carrier[id]) r.carrier[id].gain.value = isCar ? 1 : 0;
  }
  const vm = S.vocMix || (S.vocMix = {vol:.85, pan:0, mute:false, solo:false});
  r.strip.voc.gain.value = vm.mute || (solo && !vm.solo) ? 0 : vm.vol * vm.vol * 1.4; r.pan.voc.pan.value = vm.pan;
  r.master.gain.value = S.master;
}
async function ensureAudio(){
  if (audioReady) return audioReady;
  audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    await ctx.audioWorklet.addModule('dsp/worklets.js');
    R = buildRig(ctx);
    R.cap = new AudioWorkletNode(ctx, 'cap', {numberOfInputs:1, numberOfOutputs:0}); R.lim.connect(R.cap);
    R.cap.port.onmessage = e => { if (rec.on || rec.flushing) rec.chunks.push(e.data); };
    schedLoop();
  })();
  return audioReady;
}
document.addEventListener('pointerdown', () => { ensureAudio().then(() => ctx.state !== 'running' && ctx.resume()); }, {capture:true});

/* ================= drum voices ================= */
const drumCache = new Map();
function drumBuf(id, v, acc){
  const p = S.mach[id].p, q = x => Math.round((x == null ? .5 : x) * 20) / 20;
  let key, make;
  if (id === 'd808'){
    const prm = v === 'bd' ? {tone:q(p.bdT), decay:q(p.bdD)} : v === 'sd' ? {tone:q(p.sdT), snappy:q(p.sdS)} : /^(lt|mt|ht|lc|mc|hc)$/.test(v) ? {tune:q(p[v + 'T'])} : v === 'cy' ? {tone:q(p.cyT), decay:q(p.cyD)} : v === 'oh' ? {decay:q(p.ohD)} : {};
    const a = acc ? q(p.acc) : 0; key = 'd808:' + v + ':' + JSON.stringify(prm) + ':' + a; make = () => tr808(v, prm, a);
  } else { key = 'dmx:' + v; make = () => dmx(v); }
  let b = drumCache.get(key);
  if (!b){ const o = make(); const c = ctx || new OfflineAudioContext(1, 1, 44100); b = c.createBuffer(1, o.d.length, o.sr); b.copyToChannel(o.d, 0); drumCache.set(key, b); }
  return b;
}
const chokeOpen = {};
function drumHit(r, id, v, t, acc){
  const p = S.mach[id].p, b = drumBuf(id, v, acc), s = r.c.createBufferSource(), g = r.c.createGain();
  s.buffer = b; if (id === 'dmx') s.playbackRate.value = Math.pow(2, ((p[v + 'P'] == null ? .5 : p[v + 'P']) - .5) * 14 / 12);
  const lv = p[v + 'L'] == null ? .8 : p[v + 'L']; g.gain.value = lv * lv * (acc ? (id === 'dmx' ? 1 + p.acc * .6 : 1.15) : .8);
  s.connect(g); g.connect(r.drumIn[id]); s.start(t);
  // open hat is choked by the closed hat
  const open = id === 'd808' ? 'oh' : 'ohh', closers = id === 'd808' ? ['ch'] : ['chh', 'hha'];
  const ck = r === R ? chokeOpen : (r.choke || (r.choke = {}));
  if (v === open) ck[id] = s; else if (closers.includes(v) && ck[id]){ try { ck[id].stop(t + .005); } catch(e){} ck[id] = null; }
  if (r === R) flashPad(id, v, t);
}

/* ================= speech ================= */
const ttsCache = new Map();
function ttsBuf(text){
  const T = S.voc.tts, key = text + '|' + T.voice + '|' + T.semi + '|' + T.speed;
  let b = ttsCache.get(key);
  if (!b){ const v = TTS_VOICES[T.voice] || TTS_VOICES.robot, d = ttsRender(text, {pitch:v.pitch * Math.pow(2, T.semi / 12), robot:!!v.robot, speed:T.speed, sr:22050});
    const c = ctx || new OfflineAudioContext(1, 1, 22050); b = c.createBuffer(1, Math.max(1, d.length), 22050); b.copyToChannel(d, 0); ttsCache.set(key, b); }
  return b;
}
function speak(r, text, t){ const s = r.c.createBufferSource(); s.buffer = ttsBuf(text); s.connect(r.micBus); s.start(t); return s.buffer.duration; }

/* ================= sequencer ================= */
const stepDur = () => 60 / song().bpm / 4;
const swingOff = (step, sd) => (step % 2) ? song().swing / 100 * sd * .66 : 0;
function arpSeq(chord, A){
  const base = chord.slice().sort((a, b) => a - b), all = [];
  for (let o = 0; o < (A.oct || 1); o++) for (const n of base) all.push(n + 12 * o);
  if (A.mode === 'down') return all.reverse();
  if (A.mode === 'updown') return all.length > 2 ? all.concat(all.slice(1, -1).reverse()) : all;
  return all;
}
// events for one step of a section: calls out.note(id, n, v, t0, t1, slide), out.hit(id, v, t, acc), out.say(text, t)
function stepEvents(sec, step, t, out){
  const sd = stepDur();
  for (const id of SEQ_MACH){
    const M = MACH[id];
    if (M.kind === 'drum'){ const d = sec.drums[id]; for (const v in d){ const ch = d[v][step]; if (ch === 'x' || ch === 'o') out.hit(id, v, t, ch === 'x'); } continue; }
    const ns = sec.notes[id].filter(o => o.s === step); if (!ns.length) continue;
    const A = S.mach[id].arp;
    if (A && A.mode !== 'off'){
      const len = Math.max(...ns.map(o => o.l)), per = 16 / (A.rate || 16), seq = arpSeq(ns.map(o => o.n), A);
      let k = 0;
      for (let x = 0; x < len; x += per, k++){ const n = A.mode === 'random' ? seq[Math.floor(Math.random() * seq.length)] : seq[k % seq.length], t0 = t + x * sd; out.note(id, n, ns[0].a ? 1 : .78, t0, t0 + per * sd * .55, false); }
      continue;
    }
    for (const o of ns){
      const t1 = t + o.l * sd - (o.sl ? -sd * .25 : Math.min(.012, sd * .2));
      out.note(id, o.n, o.a ? 1 : .75, t, t1, !!o.sl);
    }
  }
  if (step % 16 === 0) for (const l of sec.say) if (l.bar === step / 16 && l.text) out.say(l.text, t);
}
let playing = false, pos = {sec:0, rep:0, step:0}, nextT = 0, stepLog = [];
const live = {held:{}, arpK:{}, nextArp:0};
function sendNote(r, id, n, v, t0, t1, sl){
  const node = r.nodes[id]; if (!node) return;
  const sr = r.c.sampleRate, f0 = Math.round(t0 * sr), f1 = Math.round(t1 * sr);
  node.port.postMessage({type:'ev', ev:{f:f0, on:n, v, sl}}); node.port.postMessage({type:'ev', ev:{f:f1, off:n}});
}
const liveOut = {note:(id, n, v, t0, t1, sl) => sendNote(R, id, n, v, t0, t1, sl), hit:(id, v, t, a) => drumHit(R, id, v, t, a), say:(text, t) => speak(R, text, t)};
function schedLoop(){
  setInterval(() => {
    if (!ctx) return;
    const ahead = ctx.currentTime + .12;
    if (playing){
      while (nextT < ahead){
        const sd = stepDur(), sec = song().sections[pos.sec], t = nextT + swingOff(pos.step, sd);
        stepLog.push({t:nextT, sec:pos.sec, step:pos.step}); if (stepLog.length > 64) stepLog.shift();
        stepEvents(sec, pos.step, t, liveOut);
        liveArp(t, sd);
        nextT += sd; pos.step++;
        if (pos.step >= sec.bars * 16){ pos.step = 0;
          if (S.loopSec) {} else if (++pos.rep >= sec.rep){ pos.rep = 0; pos.sec = (pos.sec + 1) % song().sections.length; } }
      }
    } else {
      if (live.nextArp < ctx.currentTime) live.nextArp = ctx.currentTime + .02;
      while (live.nextArp < ahead){ const sd = 60 / song().bpm / 4; liveArp(live.nextArp, sd); live.nextArp += sd; }
    }
  }, 25);
  requestAnimationFrame(drawPlayhead);
}
// keyboard / MIDI notes held on a machine with its arpeggiator on: one arp note per arp step
let arpTick = 0;
function liveArp(t, sd){
  arpTick++;
  for (const id in live.held){
    const A = S.mach[id].arp, held = [...live.held[id]]; if (!held.length || !A || A.mode === 'off') continue;
    const per = 16 / (A.rate || 16), sub = per < 1 ? Math.round(1 / per) : 1;
    if (per >= 1 && arpTick % per) continue;
    const seq = arpSeq(held, A);
    for (let k = 0; k < sub; k++){ const i = live.arpK[id] = (live.arpK[id] || 0) + 1, n = A.mode === 'random' ? seq[Math.floor(Math.random() * seq.length)] : seq[i % seq.length], t0 = t + k * sd / sub;
      if (MACH[id].kind !== 'drum') sendNote(R, id, n, .85, t0, t0 + sd * per * .55 / sub * (per >= 1 ? 1 : 1), false); }
  }
}
async function play(){
  await ensureAudio(); if (ctx.state !== 'running') await ctx.resume();
  if (playing){ stop(); return; }
  playing = true; pos = {sec:S.loopSec ? S.sec : S.sec, rep:0, step:0}; nextT = ctx.currentTime + .06; stepLog = [];
  renderTransport();
}
function stop(){ playing = false; if (R) for (const id in R.nodes) R.nodes[id].port.postMessage({type:'panic'}); for (const id in R ? R.nodes : {}) for (let n = 0; n < 128; n++) R.nodes[id].port.postMessage({type:'ev', ev:{f:0, off:n}}); renderTransport(); }
function nowPos(){ if (!playing || !ctx) return null; let p = null; for (const s of stepLog) if (s.t <= ctx.currentTime) p = s; return p; }

/* ================= live playing: on-screen keys, pads, MIDI ================= */
function liveOn(id, n, v = .85){
  ensureAudio().then(() => {
    const M = MACH[id];
    if (M.kind === 'drum'){ const vv = M.voices.find(([k]) => M.gm[k] === n) || M.voices[((n - 36) % 16 + 16) % 16]; drumHit(R, id, vv[0], ctx.currentTime + .005, v > .9); recordHit(id, vv[0], v > .9); return; }
    const A = S.mach[id].arp;
    if (A && A.mode !== 'off'){ (live.held[id] || (live.held[id] = new Set())).add(n); return; }
    R.nodes[id].port.postMessage({type:'ev', ev:{f:0, on:n, v}}); recordNote(id, n, v, true);
  });
}
function liveOff(id, n){
  if (!R) return; const M = MACH[id]; if (M.kind === 'drum') return;
  if (live.held[id] && live.held[id].has(n)){ if (!keyLatch) live.held[id].delete(n); return; }
  R.nodes[id].port.postMessage({type:'ev', ev:{f:0, off:n}}); recordNote(id, n, 0, false);
}
// realtime record: while playing with REC NOTES on, notes land on the nearest 16th of the current section
let recNotes = false; const recOpen = new Map();
function recordNote(id, n, v, on){
  if (!recNotes || !playing) return; const p = nowPos(); if (!p) return;
  const sec = song().sections[p.sec], sd = stepDur(), off = (ctx.currentTime - p.t) / sd, st = (p.step + Math.round(off)) % (sec.bars * 16);
  if (on){ const o = {s:st, n, l:1, a:v > .95 ? 1 : 0, sl:0}; sec.notes[id] = sec.notes[id].filter(x => !(x.s === st && x.n === n)); sec.notes[id].push(o); recOpen.set(id + ':' + n, {o, t:ctx.currentTime}); }
  else { const r = recOpen.get(id + ':' + n); if (r){ r.o.l = Math.max(1, Math.round((ctx.currentTime - r.t) / sd)); recOpen.delete(id + ':' + n); } }
  save(); if (curTab() === id) renderEditor();
}
function recordHit(id, v, acc){
  if (!recNotes || !playing) return; const p = nowPos(); if (!p) return;
  const sec = song().sections[p.sec], off = (ctx.currentTime - p.t) / stepDur(), st = (p.step + Math.round(off)) % (sec.bars * 16), s = sec.drums[id][v];
  sec.drums[id][v] = s.slice(0, st) + (acc ? 'x' : 'o') + s.slice(st + 1); save(); if (curTab() === id) renderEditor();
}
let keyLatch = false;
async function midiInit(){
  if (!navigator.requestMIDIAccess){ toast('This browser has no Web MIDI. On the S23 use Chrome.'); return; }
  try {
    const acc = await navigator.requestMIDIAccess();
    const hook = () => { let n = 0; acc.inputs.forEach(inp => { inp.onmidimessage = onMidi; n++; }); $('#midiStat').textContent = n ? 'MIDI: ' + n + ' in' : 'MIDI: none'; };
    acc.onstatechange = hook; hook(); toast('MIDI on. Notes play ' + (S.midiMode === 'selected' ? 'the machine on screen' : 'by channel') + '.');
  } catch(e){ toast('MIDI was not allowed.'); }
}
function onMidi(e){
  const [st, d1, d2] = e.data, hi = st & 0xF0, ch = st & 15;
  let id = curTab();
  if (S.midiMode === 'channel' || !MACH[id]){ id = Object.keys(S.midiCh).find(k => S.midiCh[k] === ch); if (!id) return; }
  if (hi === 0x90 && d2) liveOn(id, d1, d2 / 127); else if (hi === 0x80 || (hi === 0x90 && !d2)) liveOff(id, d1);
  else if (hi === 0xE0 && R && R.nodes[id]) R.nodes[id].port.postMessage({type:'bend', v:((d2 << 7 | d1) - 8192) / 8192 * 2});
  else if (hi === 0xB0 && d1 === 1 && R && R.nodes[id]) R.nodes[id].port.postMessage({type:'wheel', v:d2 / 127});
}

/* ================= WAV: live recording and offline export ================= */
const rec = {on:false, chunks:[], flushing:false};
function wavBlob(chs, sr){
  const n = chs[0].length, buf = new ArrayBuffer(44 + n * 4), v = new DataView(buf), w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
  w(0, 'RIFF'); v.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true); v.setUint32(24, sr, true); v.setUint32(28, sr * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, n * 4, true);
  let o = 44; for (let i = 0; i < n; i++) for (let c = 0; c < 2; c++){ v.setInt16(o, clamp(chs[c][i], -1, 1) * 32767, true); o += 2; }
  return new Blob([buf], {type:'audio/wav'});
}
const download = (blob, name) => { const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000); };
const fname = (ext) => song().name.replace(/[^\w]+/g, '-').replace(/^-|-$/g, '') + '.' + ext;
async function toggleRec(){
  await ensureAudio();
  if (!rec.on){ rec.on = true; rec.chunks = []; R.cap.port.postMessage(true); $('#recBtn').classList.add('on'); toast('Recording the output. Tap REC again to stop and save a WAV.'); return; }
  rec.on = false; rec.flushing = true; R.cap.port.postMessage(false); $('#recBtn').classList.remove('on');
  setTimeout(() => { rec.flushing = false; const n = rec.chunks.reduce((a, c) => a + c.l.length, 0); if (!n){ toast('Nothing recorded.'); return; }
    const L = new Float32Array(n), Rr = new Float32Array(n); let o = 0; for (const c of rec.chunks){ L.set(c.l, o); Rr.set(c.r, o); o += c.l.length; }
    download(wavBlob([L, Rr], ctx.sampleRate), 'jamon-live-' + Date.now() + '.wav'); }, 300);
}
// the whole song (every section and repeat once through), rendered faster than real time
async function exportWav(){
  await ensureAudio();
  const sg = song(), sd = stepDur(); let total = 0; for (const s of sg.sections) total += s.bars * 16 * s.rep;
  const dur = total * sd + 3, sr = 44100;
  if (dur > 900){ toast('That song is over 15 minutes; shorten it first.'); return; }
  $('#busy').textContent = 'Rendering ' + Math.round(dur) + ' s of audio…'; $('#busy').classList.add('on');
  try {
    const ev = {}, hits = [], says = [];
    const out = {note:(id, n, v, t0, t1, sl) => { (ev[id] || (ev[id] = [])).push({f:Math.round(t0 * sr), on:n, v, sl}, {f:Math.round(t1 * sr), off:n}); }, hit:(id, v, t, a) => hits.push([id, v, t, a]), say:(text, t) => says.push([text, t])};
    let t = .05; for (const sec of sg.sections) for (let r = 0; r < sec.rep; r++) for (let st = 0; st < sec.bars * 16; st++){ stepEvents(sec, st, t + swingOff(st, sd), out); t += sd; }
    const oc = new OfflineAudioContext(2, Math.ceil(dur * sr), sr); await oc.audioWorklet.addModule('dsp/worklets.js');
    const r = buildRig(oc, ev); r.voc.port.onmessage = null;
    for (const h of hits) drumHit(r, ...h);
    for (const [text, tt] of says) speak(r, text, tt);
    const b = await oc.startRendering();
    download(wavBlob([b.getChannelData(0), b.getChannelData(1)], sr), fname('wav')); toast('WAV saved.');
  } catch(e){ console.error(e); toast('Export failed: ' + e.message); }
  $('#busy').classList.remove('on');
}

/* ================= MIDI files ================= */
function exportMidi(){
  const sg = song(), ppq = 96, tps = ppq / 4, tracks = {};
  const out = {note:(id, n, v, t0, t1) => { const tr = tracks[id] || (tracks[id] = []); tr.push({t:Math.round(t0 / stepDur() * tps), d:Math.max(1, Math.round((t1 - t0) / stepDur() * tps)), n, v}); },
    hit:(id, v, t, a) => { const tr = tracks[id] || (tracks[id] = []); tr.push({t:Math.round(t / stepDur() * tps), d:tps / 2, n:MACH[id].gm[v], v:a ? 1 : .7}); }, say:() => {}};
  let t = 0; for (const sec of sg.sections) for (let r = 0; r < sec.rep; r++) for (let st = 0; st < sec.bars * 16; st++){ stepEvents(sec, st, t, out); t += stepDur(); }
  const list = Object.keys(tracks).map(id => ({name:MACH[id].name, ch:S.midiCh[id], notes:tracks[id]}));
  download(new Blob([SMF.write(list, sg.bpm, ppq, sg.name)], {type:'audio/midi'}), fname('mid')); toast('MIDI saved: ' + list.length + ' tracks.');
}
let imp = null;
async function importMidi(file){
  try { imp = SMF.read(await file.arrayBuffer()); } catch(e){ toast(e.message); return; }
  imp.file = file.name.replace(/\.midi?$/i, '');
  // one row per track + channel; guess the machine from the channel, the track name, or drums on channel 10
  imp.groups = [];
  for (const tr of imp.tracks){ const byCh = {}; for (const n of tr.notes) (byCh[n.ch] || (byCh[n.ch] = [])).push(n);
    for (const ch in byCh){ const name = (tr.name || '').toLowerCase(); let id = Object.keys(MACH).find(k => name.includes(MACH[k].name.toLowerCase()) || name.includes(k));
      if (!id) id = +ch === 9 ? 'd808' : Object.keys(S.midiCh).find(k => S.midiCh[k] === +ch && MACH[k].kind !== 'drum') || '';
      imp.groups.push({label:(tr.name || 'Track') + ' · ch ' + (+ch + 1) + ' · ' + byCh[ch].length + ' notes', notes:byCh[ch], id}); } }
  if (!imp.groups.length){ toast('No notes in that file.'); return; }
  const d = $('#impDlg'), opts = '<option value="">(skip)</option>' + Object.keys(MACH).map(k => `<option value="${k}">${MACH[k].name}</option>`).join('');
  $('#impRows').innerHTML = imp.groups.map((g, i) => `<label class="improw"><span>${esc(g.label)}</span><select data-i="${i}">${opts}</select></label>`).join('');
  $$('#impRows select').forEach(s => { s.value = imp.groups[+s.dataset.i].id; s.onchange = () => imp.groups[+s.dataset.i].id = s.value; });
  $('#impInfo').textContent = imp.file + ' · ' + Math.round(imp.tempo) + ' BPM';
  d.showModal();
}
function importApply(){
  const tps = imp.ppq / 4; let last = 0; for (const g of imp.groups) if (g.id) for (const n of g.notes) last = Math.max(last, n.t + n.d);
  const bars = clamp(Math.ceil(last / tps / 16) || 1, 1, 128), sec = emptySection('Imported', bars), steps = bars * 16;
  for (const g of imp.groups){
    if (!g.id) continue; const M = MACH[g.id];
    for (const n of g.notes){ const s = Math.round(n.t / tps); if (s >= steps) continue;
      if (M.kind === 'drum'){ let v = Object.keys(M.gm).find(k => M.gm[k] === n.n); if (!v){ const near = Object.keys(M.gm).sort((a, b) => Math.abs(M.gm[a] - n.n) - Math.abs(M.gm[b] - n.n)); v = near[0]; }
        const str = sec.drums[g.id][v]; sec.drums[g.id][v] = str.slice(0, s) + (n.v > .85 ? 'x' : 'o') + str.slice(s + 1); }
      else sec.notes[g.id].push({s, n:n.n, l:Math.max(1, Math.round(n.d / tps)), a:n.v > .85 ? 1 : 0, sl:0}); }
  }
  S.songs.push({name:imp.file, bpm:Math.round(clamp(imp.tempo, 40, 240)), swing:0, key:'', note:'Imported from a MIDI file', presets:{}, carrier:S.voc.carrier, arp:{}, sections:[sec]});
  loadSong(S.songs.length - 1); $('#impDlg').close(); toast('Imported as a new song: ' + bars + ' bars.');
}
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;'}[c]));

/* ================= presets ================= */
function applyPreset(id, name){
  if (id === 'voc'){ const P = PRESETS.voc[name] || (S.user.voc || {})[name]; if (!P) return; S.voc.p = Object.assign(VOC_DEF(), P.voc); S.voc.tts = Object.assign({}, P.tts); S.voc.preset = name; if (R) R.voc.port.postMessage(S.voc.p); save(); return; }
  const P = (PRESETS[id] || {})[name] || ((S.user[id] || {})[name]); if (!P) return;
  const m = S.mach[id]; m.p = Object.assign(MACH[id].def(), P); m.preset = name; pushCfg(id); save();
}
function pushCfg(id){ if (!R) return; if (id === 'voc') R.voc.port.postMessage(S.voc.p); else if (R.nodes[id]) R.nodes[id].port.postMessage({type:'cfg', cfg:cfgFor(id)}); }
function loadSong(i){
  if (playing) stop();
  S.cur = i; S.sec = 0; S.bar = 0; const sg = song();
  for (const id in MACH) S.mach[id].arp = Object.assign({mode:'off', oct:1, rate:16}, (sg.arp || {})[id] || {});
  for (const [id, name] of Object.entries(sg.presets || {})) applyPreset(id, name);
  if (sg.carrier) S.voc.carrier = sg.carrier;
  applyMix(); save(); renderAll();
}

/* ================= UI ================= */
const TABS = [['song', 'Song'], ['d808', 'TR-808'], ['dmx', 'DMX'], ['tb303', 'TB-303'], ['proone', 'Pro-One'], ['sh101', 'SH-101'], ['juno', 'Juno-60'], ['rs09', 'RS-09'], ['pbass', 'P-Bass'], ['voc', 'Vocoder'], ['mix', 'Mixer']];
const curTab = () => S.tab;
function renderTabs(){ $('#tabs').innerHTML = TABS.map(([k, l]) => `<button data-k="${k}" class="${S.tab === k ? 'on' : ''}" style="--c:${MACH[k] ? MACH[k].color : '#e8742b'}">${l}</button>`).join(''); }
$('#tabs').addEventListener('click', e => { const b = e.target.closest('button'); if (!b) return; S.tab = b.dataset.k; save(); renderAll(); $('#main').scrollTop = 0; });
function renderTransport(){
  $('#playBtn').textContent = playing ? '■' : '▶'; $('#playBtn').classList.toggle('on', playing);
  $('#bpm').value = song().bpm; $('#swing').value = song().swing; $('#loopBtn').classList.toggle('on', S.loopSec); $('#recNotesBtn').classList.toggle('on', recNotes);
  $('#songSel').innerHTML = S.songs.map((s, i) => `<option value="${i}">${esc(s.name)}</option>`).join(''); $('#songSel').value = S.cur;
}
function sectionBar(){
  const sg = song();
  return `<div class="secbar">${sg.sections.map((s, i) => `<button class="sec ${i === S.sec ? 'on' : ''}" data-sec="${i}">${esc(s.name)}<small>${s.bars} bar${s.bars > 1 ? 's' : ''}${s.rep > 1 ? ' ×' + s.rep : ''}</small></button>`).join('')}</div>
  <div class="barsel">${section().bars > 1 ? Array.from({length:section().bars}, (_, b) => `<button data-bar="${b}" class="${b === S.bar ? 'on' : ''}">Bar ${b + 1}</button>`).join('') : ''}</div>`;
}
function onSectionBar(root){
  root.querySelectorAll('[data-sec]').forEach(b => b.onclick = () => { S.sec = +b.dataset.sec; S.bar = 0; save(); renderAll(); });
  root.querySelectorAll('[data-bar]').forEach(b => b.onclick = () => { S.bar = +b.dataset.bar; save(); renderAll(); });
}
function panelHTML(id, panel, vals){
  const groups = []; for (const c of panel){ let g = groups.find(x => x.g === c.g); if (!g) groups.push(g = {g:c.g, cs:[]}); g.cs.push(c); }
  return `<div class="panel">${groups.map(g => `<fieldset><legend>${esc(g.g)}</legend>${g.cs.map(c => {
    const v = vals[c.k];
    if (c.t === 's') return `<button class="sw ${v ? 'on' : ''}" data-k="${c.k}">${esc(c.l)}</button>`;
    if (c.t === 'sel') return `<label class="ctl"><span>${esc(c.l)}</span><select data-k="${c.k}">${c.opts.map(([ov, ol]) => `<option value="${ov}" ${+v === ov ? 'selected' : ''}>${esc(ol)}</option>`).join('')}</select></label>`;
    return `<label class="ctl"><span>${esc(c.l)}</span><input type="range" min="0" max="1" step="0.01" value="${v == null ? .5 : v}" data-k="${c.k}"><b>${Math.round((v == null ? .5 : v) * 10 * 10) / 10}</b></label>`;
  }).join('')}</fieldset>`).join('')}</div>`;
}
function bindPanel(root, vals, onChange){
  root.querySelectorAll('.panel input[type=range]').forEach(el => el.oninput = () => { vals[el.dataset.k] = +el.value; el.nextElementSibling.textContent = Math.round(el.value * 100) / 10; onChange(); });
  root.querySelectorAll('.panel select').forEach(el => el.onchange = () => { vals[el.dataset.k] = +el.value; onChange(); });
  root.querySelectorAll('.panel .sw').forEach(el => el.onclick = () => { vals[el.dataset.k] = vals[el.dataset.k] ? 0 : 1; el.classList.toggle('on', !!vals[el.dataset.k]); onChange(); });
  root.querySelectorAll('.panel input[type=range]').forEach(el => el.ondblclick = () => {});
}
function presetRow(id){
  const names = Object.keys(PRESETS[id] || {}), user = Object.keys(S.user[id] || {}), cur = id === 'voc' ? S.voc.preset : S.mach[id].preset;
  return `<div class="row"><select id="preSel"><option value="">Preset…</option>${names.map(n => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('')}${user.length ? '<optgroup label="Yours">' + user.map(n => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('') + '</optgroup>' : ''}</select>
    <button id="preSave">Save as…</button></div>`;
}
function bindPresetRow(id){
  $('#preSel').onchange = e => { if (e.target.value){ applyPreset(id, e.target.value); renderAll(); } };
  $('#preSave').onclick = () => { const n = prompt('Name for this sound:'); if (!n) return; (S.user[id] || (S.user[id] = {}))[n] = id === 'voc' ? {voc:Object.assign({}, S.voc.p), tts:Object.assign({}, S.voc.tts)} : Object.assign({}, S.mach[id].p); if (id === 'voc') S.voc.preset = n; else S.mach[id].preset = n; save(); renderAll(); toast('Saved.'); };
}
function renderAll(){ renderTabs(); renderTransport(); renderMain(); }
function renderMain(){
  const m = $('#main'), k = S.tab;
  if (k === 'song') return renderSong(m);
  if (k === 'mix') return renderMixer(m);
  if (k === 'voc') return renderVoc(m);
  renderMachine(m, k);
}
function renderSong(m){
  const sg = song();
  m.innerHTML = `<section class="card"><h2>${esc(sg.name)}</h2><p class="hint">${esc(sg.note || '')}${sg.key ? ' · ' + esc(sg.key) : ''}</p>
    <div class="row"><button id="songNew">New song</button><button id="songDup">Copy</button><button id="songRen">Rename</button><button id="songDel">Delete</button><button id="songReset">Reset the 7 styles</button></div></section>
    <section class="card"><h3>Sections <small>play in order; tap one to edit it on every machine</small></h3><div id="secList">${sg.sections.map((s, i) => `<div class="secrow ${i === S.sec ? 'on' : ''}" data-i="${i}">
      <input value="${esc(s.name)}" data-f="name"><label>Bars<select data-f="bars">${[1, 2, 4, 8, 16].concat(s.bars > 16 ? [s.bars] : []).map(b => `<option ${b === s.bars ? 'selected' : ''}>${b}</option>`).join('')}</select></label>
      <label>×<select data-f="rep">${[1, 2, 3, 4, 6, 8].map(b => `<option ${b === s.rep ? 'selected' : ''}>${b}</option>`).join('')}</select></label>
      <span class="parts">${partsSummary(s)}</span><span class="acts"><button data-a="edit">Edit</button><button data-a="up">↑</button><button data-a="dup">Copy</button><button data-a="del">✕</button></span></div>`).join('')}</div>
      <div class="row"><button id="secAdd">Add section</button></div></section>
    <section class="card"><h3>Files</h3><div class="row"><button id="wavBtn">Export WAV</button><button id="midBtn">Export MIDI</button><button id="midImp">Import MIDI</button><button id="projSave">Save project</button><button id="projLoad">Open project</button></div>
    <p class="hint">MIDI export has one track per machine: drums on their channels with General MIDI notes, arpeggios written out as they play. Import puts every track on a machine you pick.</p></section>
    <section class="card"><h3>MIDI input</h3><div class="row"><button id="midiOn">Connect MIDI</button><span id="midiStat2" class="hint"></span>
      <select id="midiMode"><option value="selected" ${S.midiMode === 'selected' ? 'selected' : ''}>Plays the machine on screen</option><option value="channel" ${S.midiMode === 'channel' ? 'selected' : ''}>Plays by channel</option></select></div>
      <div class="chmap">${Object.keys(MACH).map(id => `<label>${MACH[id].name}<select data-ch="${id}">${Array.from({length:16}, (_, c) => `<option value="${c}" ${S.midiCh[id] === c ? 'selected' : ''}>ch ${c + 1}</option>`).join('')}</select></label>`).join('')}</div></section>`;
  $('#songNew').onclick = () => { S.songs.push({name:'New song', bpm:112, swing:0, key:'', note:'', presets:{}, carrier:S.voc.carrier, arp:{}, sections:[emptySection('A', 4)]}); loadSong(S.songs.length - 1); };
  $('#songDup').onclick = () => { const c = JSON.parse(JSON.stringify(sg)); c.name += ' (copy)'; S.songs.push(c); loadSong(S.songs.length - 1); };
  $('#songRen').onclick = () => { const n = prompt('Song name:', sg.name); if (n){ sg.name = n; save(); renderAll(); } };
  $('#songDel').onclick = () => { if (S.songs.length < 2 || !confirm('Delete "' + sg.name + '"?')) return; S.songs.splice(S.cur, 1); loadSong(0); };
  $('#songReset').onclick = () => { if (!confirm('Put the seven style songs back as they came (your other songs stay)?')) return; const names = SONGS.map(s => s.name); S.songs = SONGS.map(compileSong).concat(S.songs.filter(s => !names.includes(s.name))); loadSong(0); };
  $('#secList').onclick = e => { const row = e.target.closest('.secrow'), a = e.target.dataset.a; if (!row || !a) return; const i = +row.dataset.i, secs = sg.sections;
    if (a === 'edit'){ S.sec = i; S.bar = 0; S.tab = secs[i].notes.tb303.length ? 'tb303' : 'dmx'; }
    if (a === 'up' && i){ [secs[i - 1], secs[i]] = [secs[i], secs[i - 1]]; S.sec = i - 1; }
    if (a === 'dup'){ secs.splice(i + 1, 0, JSON.parse(JSON.stringify(secs[i]))); }
    if (a === 'del' && secs.length > 1 && confirm('Delete section "' + secs[i].name + '"?')){ secs.splice(i, 1); S.sec = 0; }
    save(); renderAll(); };
  $('#secList').onchange = e => { const row = e.target.closest('.secrow'), f = e.target.dataset.f; if (!row || !f) return; const s = sg.sections[+row.dataset.i];
    if (f === 'name') s.name = e.target.value; if (f === 'rep') s.rep = +e.target.value; if (f === 'bars') resizeSection(s, +e.target.value); save(); renderAll(); };
  $('#secList').querySelectorAll('.secrow').forEach(r => r.addEventListener('click', e => { if (e.target.tagName === 'DIV' || e.target.classList.contains('parts')){ S.sec = +r.dataset.i; save(); renderAll(); } }));
  $('#secAdd').onclick = () => { sg.sections.push(emptySection('Section ' + (sg.sections.length + 1), 4)); S.sec = sg.sections.length - 1; save(); renderAll(); };
  $('#wavBtn').onclick = exportWav; $('#midBtn').onclick = exportMidi; $('#midImp').onclick = () => $('#midFile').click();
  $('#projSave').onclick = () => download(new Blob([JSON.stringify(S)], {type:'application/json'}), 'jamon-project.json');
  $('#projLoad').onclick = () => $('#projFile').click();
  $('#midiOn').onclick = midiInit; $('#midiMode').onchange = e => { S.midiMode = e.target.value; save(); };
  m.querySelectorAll('[data-ch]').forEach(s => s.onchange = () => { S.midiCh[s.dataset.ch] = +s.value; save(); });
}
function partsSummary(s){
  const out = [];
  for (const id of SEQ_MACH){ const M = MACH[id], has = M.kind === 'drum' ? Object.values(s.drums[id]).some(x => /[xo]/.test(x)) : s.notes[id].length; if (has) out.push(`<i style="--c:${M.color}">${M.name}</i>`); }
  if (s.say.length) out.push('<i style="--c:#7fd">Vocoder</i>');
  return out.join('');
}
function resizeSection(s, bars){
  const steps = bars * 16;
  for (const id in s.drums) for (const v in s.drums[id]){ const o = s.drums[id][v]; s.drums[id][v] = steps > o.length ? o + fillDrum(o, steps).slice(o.length) : o.slice(0, steps); }
  for (const id in s.notes){ const old = s.bars * 16, ns = s.notes[id].filter(o => o.s < steps); if (steps > old) for (const o of s.notes[id]) for (let k = old; k < steps; k += old) if (o.s + k < steps) ns.push(Object.assign({}, o, {s:o.s + k})); s.notes[id] = ns; }
  s.say = s.say.filter(l => l.bar < bars); s.bars = bars; S.bar = Math.min(S.bar, bars - 1);
}
function renderMachine(m, id){
  const M = MACH[id], ms = S.mach[id];
  m.innerHTML = `<section class="card mach" style="--c:${M.color}"><div class="mhead"><h2>${M.name}</h2><span class="maker">${M.maker}</span></div>${presetRow(id)}
    ${M.kind !== 'drum' ? `<div class="row arp"><label>Arpeggiator<select id="arpMode">${['off', 'up', 'down', 'updown', 'random'].map(x => `<option ${ms.arp.mode === x ? 'selected' : ''}>${x}</option>`).join('')}</select></label>
      <label>Range<select id="arpOct">${[1, 2, 3].map(x => `<option ${ms.arp.oct === x ? 'selected' : ''}>${x}</option>`).join('')}</select></label>
      <label>Rate<select id="arpRate">${[[8, '1/8'], [16, '1/16'], [32, '1/32']].map(([v, l]) => `<option value="${v}" ${ms.arp.rate === v ? 'selected' : ''}>${l}</option>`).join('')}</select></label></div>` : ''}
    <details ${S.openPanel !== false ? 'open' : ''} id="panelDet"><summary>Panel</summary>${panelHTML(id, M.panel, ms.p)}</details></section>
    <section class="card">${sectionBar()}<div id="editor"></div></section>
    <section class="card">${M.kind === 'drum' ? '<div id="pads" class="pads"></div>' : `<div class="row"><button id="kbDn">◀ Oct</button><span id="kbLbl"></span><button id="kbUp">Oct ▶</button><button id="latch" class="${keyLatch ? 'on' : ''}">Hold</button></div><div id="kb" class="kb"></div>`}</section>`;
  bindPresetRow(id);
  bindPanel(m, ms.p, () => { ms.preset = ''; pushCfg(id); save(); });
  $('#panelDet').ontoggle = e => { S.openPanel = e.target.open; save(); };
  onSectionBar(m);
  if (M.kind !== 'drum'){
    $('#arpMode').onchange = e => { ms.arp.mode = e.target.value; live.held[id] = new Set(); save(); };
    $('#arpOct').onchange = e => { ms.arp.oct = +e.target.value; save(); };
    $('#arpRate').onchange = e => { ms.arp.rate = +e.target.value; save(); };
    renderKeys(id);
  } else renderPads(id);
  renderEditor();
}
function renderEditor(){
  const el = $('#editor'); if (!el) return; const id = S.tab, M = MACH[id], sec = section(), b0 = S.bar * 16;
  if (M.kind === 'drum'){
    el.innerHTML = `<div class="grid drum">${M.voices.map(([v, name]) => `<div class="gl"><span>${name}</span>${Array.from({length:16}, (_, s) => { const c = sec.drums[id][v][b0 + s]; return `<button data-v="${v}" data-s="${b0 + s}" class="st ${c === 'x' ? 'x' : c === 'o' ? 'o' : ''} ${s % 4 === 0 ? 'beat' : ''}"></button>`; }).join('')}</div>`).join('')}</div>
      <p class="hint">Tap a step: soft hit, then accent, then off.</p>`;
    el.querySelector('.grid').onclick = e => { const b = e.target.closest('.st'); if (!b) return; const v = b.dataset.v, s = +b.dataset.s, str = sec.drums[id][v], c = str[s], nc = c === '.' ? 'o' : c === 'o' ? 'x' : '.';
      sec.drums[id][v] = str.slice(0, s) + nc + str.slice(s + 1); b.className = 'st ' + (nc === 'x' ? 'x' : nc === 'o' ? 'o' : '') + (((s - b0) % 4 === 0) ? ' beat' : ''); save(); };
    return;
  }
  const notes = sec.notes[id];
  // open the roll where this part's notes are
  if (notes.length){ const ns = notes.map(o => o.n), lo0 = S.rollOct * 12 + 12; if (!ns.some(n => n >= lo0 && n < lo0 + 25)) S.rollOct = clamp(Math.floor(Math.min(...ns) / 12) - 1, 0, 6); }
  const lo = S.rollOct * 12 + 12, rows = 25;
  const cell = (n, s) => { const st = b0 + s, o = notes.find(x => x.n === n && x.s <= st && st < x.s + x.l); if (!o) return ''; return o.s === st ? 'nh' + (o.a ? ' acc' : '') + (o.sl ? ' sl' : '') : 'nt'; };
  el.innerHTML = `<div class="row tools">${[['draw', 'Draw'], ['acc', 'Accent'], ['slide', 'Slide'], ['erase', 'Erase']].map(([k, l]) => `<button data-tool="${k}" class="${S.tool === k ? 'on' : ''}">${l}</button>`).join('')}
      <label>Length<select id="lenSel">${[1, 2, 3, 4, 6, 8, 16, 32].map(x => `<option ${S.len === x ? 'selected' : ''}>${x}</option>`).join('')}</select></label>
      <button id="rollDn">▼</button><button id="rollUp">▲</button><button id="txtBtn">Text</button><button id="clrBtn">Clear</button></div>
    <div class="grid roll">${Array.from({length:rows}, (_, r) => { const n = lo + rows - 1 - r, blk = [1, 3, 6, 8, 10].includes(n % 12); return `<div class="gl ${blk ? 'blk' : ''}"><span>${nn(n)}</span>${Array.from({length:16}, (_, s) => `<button data-n="${n}" data-s="${b0 + s}" class="st ${cell(n, s)} ${s % 4 === 0 ? 'beat' : ''}"></button>`).join('')}</div>`; }).join('')}</div>
    <p class="hint">Draw adds a note of the chosen length (tap a note to remove it). Accent and Slide mark notes (slide glides into the next note on the 303, Pro-One and SH-101). Text shows the whole section as notation you can edit or paste.</p>`;
  el.querySelectorAll('[data-tool]').forEach(b => b.onclick = () => { S.tool = b.dataset.tool; save(); renderEditor(); });
  $('#lenSel').onchange = e => { S.len = +e.target.value; save(); };
  $('#rollDn').onclick = () => { S.rollOct = Math.max(0, S.rollOct - 1); save(); renderEditor(); };
  $('#rollUp').onclick = () => { S.rollOct = Math.min(6, S.rollOct + 1); save(); renderEditor(); };
  $('#clrBtn').onclick = () => { if (confirm('Clear ' + M.name + ' in "' + sec.name + '"?')){ sec.notes[id] = []; save(); renderEditor(); } };
  $('#txtBtn').onclick = () => { const t = prompt('Notation for ' + M.name + ' in "' + sec.name + '" (one token per 16th: E2  E3+G3  .  -  !accent  slide~):', toLine(notes, sec.bars * 16)); if (t != null){ sec.notes[id] = parseLine(t, sec.bars * 16); save(); renderEditor(); } };
  el.querySelector('.roll').onclick = e => {
    const b = e.target.closest('.st'); if (!b) return; const n = +b.dataset.n, s = +b.dataset.s;
    const hit = notes.find(x => x.n === n && x.s <= s && s < x.s + x.l);
    if (S.tool === 'erase' || (S.tool === 'draw' && hit)){ if (hit) sec.notes[id] = notes.filter(x => x !== hit); }
    else if (S.tool === 'draw'){ notes.push({s, n, l:Math.min(S.len, sec.bars * 16 - s), a:0, sl:0}); ensureAudio().then(() => { const t = ctx.currentTime; sendNote(R, id, n, .8, t, t + .18, false); }); }
    else if (hit){ if (S.tool === 'acc') hit.a = hit.a ? 0 : 1; else hit.sl = hit.sl ? 0 : 1; }
    save(); renderEditor();
  };
}
function renderPads(id){
  const M = MACH[id], el = $('#pads');
  el.innerHTML = M.voices.map(([v, name]) => `<button class="pad" data-v="${v}" data-pv="${id}:${v}">${name}</button>`).join('');
  el.onpointerdown = e => { const b = e.target.closest('.pad'); if (!b) return; e.preventDefault(); ensureAudio().then(() => { const acc = e.pressure > .7; drumHit(R, id, b.dataset.v, ctx.currentTime + .003, acc); recordHit(id, b.dataset.v, acc); }); };
}
function flashPad(id, v, t){ const d = Math.max(0, (t - ctx.currentTime) * 1000); setTimeout(() => { const p = document.querySelector(`[data-pv="${id}:${v}"]`); if (p){ p.classList.add('hit'); setTimeout(() => p.classList.remove('hit'), 90); } }, d); }
function renderKeys(id){
  const el = $('#kb'), lo = S.kbOct * 12 + 12, keys = [];
  for (let n = lo; n <= lo + 24; n++) keys.push(n);
  const white = keys.filter(n => ![1, 3, 6, 8, 10].includes(n % 12)), W = 100 / white.length;
  el.innerHTML = keys.map(n => { const blk = [1, 3, 6, 8, 10].includes(n % 12), wi = white.filter(w => w < n).length; return `<div class="key ${blk ? 'b' : 'w'}" data-n="${n}" style="left:${blk ? (wi * W - W * .32) : wi * W}%;width:${blk ? W * .64 : W}%">${n % 12 === 0 ? nn(n) : ''}</div>`; }).join('');
  $('#kbLbl').textContent = nn(lo) + ' – ' + nn(lo + 24);
  const down = new Map();
  el.onpointerdown = e => { const k = e.target.closest('.key'); if (!k) return; e.preventDefault(); el.setPointerCapture(e.pointerId); const n = +k.dataset.n; down.set(e.pointerId, n); k.classList.add('on'); liveOn(id, n, .85); };
  el.onpointermove = e => { if (!down.has(e.pointerId)) return; const k = document.elementFromPoint(e.clientX, e.clientY)?.closest('.key'); if (!k) return; const n = +k.dataset.n, o = down.get(e.pointerId); if (n !== o){ liveOff(id, o); el.querySelector(`[data-n="${o}"]`)?.classList.remove('on'); down.set(e.pointerId, n); k.classList.add('on'); liveOn(id, n, .85); } };
  const up = e => { if (!down.has(e.pointerId)) return; const n = down.get(e.pointerId); down.delete(e.pointerId); liveOff(id, n); el.querySelector(`[data-n="${n}"]`)?.classList.remove('on'); };
  el.onpointerup = up; el.onpointercancel = up;
  $('#kbDn').onclick = () => { S.kbOct = Math.max(0, S.kbOct - 1); save(); renderKeys(id); };
  $('#kbUp').onclick = () => { S.kbOct = Math.min(7, S.kbOct + 1); save(); renderKeys(id); };
  $('#latch').onclick = e => { keyLatch = !keyLatch; e.target.classList.toggle('on', keyLatch); if (!keyLatch){ for (const k in live.held) live.held[k] = new Set(); } };
}
function renderVoc(m){
  const sec = section(), V = S.voc;
  m.innerHTML = `<section class="card mach" style="--c:#7fd"><div class="mhead"><h2>Vocoder</h2><span class="maker">Electro-Harmonix · 1978 (EH-0300 model)</span></div>${presetRow('voc')}
    ${panelHTML('voc', VOC_PANEL, V.p)}<div id="bands" class="bands">${EH_BANDS_UI.map(f => `<div><i></i><span>${f < 1000 ? f : (f / 1000).toFixed(1) + 'k'}</span></div>`).join('')}</div>
    <div class="row"><label>Music input (carrier)<select id="carSel">${Object.keys(MACH).filter(k => MACH[k].kind !== 'drum').map(k => `<option value="${k}" ${V.carrier === k ? 'selected' : ''}>${MACH[k].name}</option>`).join('')}</select></label>
      <button id="direct" class="sw ${V.direct ? 'on' : ''}">Carrier also dry</button><button id="micBtn" class="sw ${micOn ? 'on' : ''}">Live mic</button></div></section>
    <section class="card"><h3>Text to speech (the mic input)</h3>
      <div class="row"><label>Voice<select id="ttsVoice">${Object.keys(TTS_VOICES).map(v => `<option ${V.tts.voice === v ? 'selected' : ''}>${v}</option>`).join('')}</select></label>
      <label>Pitch<input id="ttsSemi" type="number" min="-12" max="12" value="${V.tts.semi}"></label><label>Speed<input id="ttsSpeed" type="number" min=".5" max="2" step=".05" value="${V.tts.speed}"></label></div>
      <textarea id="sayText" rows="2">${esc(V.sayText)}</textarea>
      <div class="row"><button id="sayBtn" class="big">Say it</button><label>with chord<select id="sayRoot">${Array.from({length:25}, (_, i) => 40 + i).map(n => `<option value="${n}" ${V.sayRoot === n ? 'selected' : ''}>${nn(n)}</option>`).join('')}</select></label>
      <select id="sayChord">${[['min', 'minor'], ['maj', 'major'], ['one', 'one note'], ['oct', 'octaves'], ['m7', 'minor 7']].map(([k, l]) => `<option value="${k}" ${V.sayChord === k ? 'selected' : ''}>${l}</option>`).join('')}</select></div>
      <p class="hint">Type /phonemes/ between slashes to spell a word exactly (e.g. /K AX M P Y UW T ER/). Say it plays the chord on the carrier machine while the speech runs.</p></section>
    <section class="card">${sectionBar()}<h3>Speech in "${esc(sec.name)}"</h3><div id="sayList">${sec.say.map((l, i) => `<div class="sayrow"><select data-i="${i}" data-f="bar">${Array.from({length:sec.bars}, (_, b) => `<option value="${b}" ${l.bar === b ? 'selected' : ''}>Bar ${b + 1}</option>`).join('')}</select><input data-i="${i}" data-f="text" value="${esc(l.text)}"><button data-i="${i}" data-a="del">✕</button></div>`).join('')}</div>
      <div class="row"><button id="sayAdd">Add a line</button></div><p class="hint">Each line is spoken at the start of its bar. The carrier machine needs notes in that section (the songs hold chords there).</p></section>`;
  bindPresetRow('voc'); bindPanel(m, V.p, () => { V.preset = ''; pushCfg('voc'); save(); }); onSectionBar(m);
  $('#carSel').onchange = e => { V.carrier = e.target.value; song().carrier = V.carrier; applyMix(); save(); };
  $('#direct').onclick = e => { V.direct = !V.direct; e.target.classList.toggle('on', V.direct); applyMix(); save(); };
  $('#micBtn').onclick = toggleMic;
  $('#ttsVoice').onchange = e => { V.tts.voice = e.target.value; save(); };
  $('#ttsSemi').onchange = e => { V.tts.semi = +e.target.value; save(); };
  $('#ttsSpeed').onchange = e => { V.tts.speed = clamp(+e.target.value || 1, .5, 2); save(); };
  $('#sayText').oninput = e => { V.sayText = e.target.value; save(); };
  $('#sayRoot').onchange = e => { V.sayRoot = +e.target.value; save(); };
  $('#sayChord').onchange = e => { V.sayChord = e.target.value; save(); };
  $('#sayBtn').onclick = sayNow;
  $('#sayList').onchange = e => { const i = +e.target.dataset.i, f = e.target.dataset.f; if (f === 'bar') sec.say[i].bar = +e.target.value; else sec.say[i].text = e.target.value; save(); };
  $('#sayList').onclick = e => { if (e.target.dataset.a === 'del'){ sec.say.splice(+e.target.dataset.i, 1); save(); renderMain(); } };
  $('#sayAdd').onclick = () => { sec.say.push({bar:0, text:'new line.'}); save(); renderMain(); };
}
const EH_BANDS_UI = [216, 298, 361, 478, 493, 614, 797, 1013, 1142, 1381, 1677, 2031, 2461, 2982];
function vocMeter(a){ const bars = $$('#bands i'); if (!bars.length) return; a.forEach((v, i) => { if (bars[i]) bars[i].style.height = Math.min(100, Math.sqrt(v) * 160) + '%'; }); }
async function sayNow(){
  await ensureAudio(); if (ctx.state !== 'running') await ctx.resume();
  const V = S.voc, t = ctx.currentTime + .05, dur = speak(R, V.sayText || '...', t), r = V.sayRoot;
  const chord = {min:[0, 3, 7], maj:[0, 4, 7], one:[0], oct:[-12, 0, 12], m7:[0, 3, 7, 10]}[V.sayChord] || [0];
  for (const iv of chord) sendNote(R, V.carrier, r + iv, .9, t, t + dur + .1, false);
}
let micOn = false, micSrc = null;
async function toggleMic(){
  await ensureAudio();
  if (micOn){ micSrc && micSrc.disconnect(); micSrc && micSrc.mediaStream.getTracks().forEach(t => t.stop()); micOn = false; renderMain(); return; }
  try { const st = await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:false, noiseSuppression:false, autoGainControl:false}}); micSrc = ctx.createMediaStreamSource(st); micSrc.connect(R.micBus); micOn = true; toast('Mic is the vocoder\'s voice now. Use headphones.'); }
  catch(e){ toast('No microphone: ' + e.message); }
  renderMain();
}
function renderMixer(m){
  const ch = Object.keys(MACH).map(id => ({id, name:MACH[id].name, c:MACH[id].color, s:S.mach[id]})).concat([{id:'voc', name:'Vocoder', c:'#7fd', s:S.vocMix || (S.vocMix = {vol:.85, pan:0, mute:false, solo:false})}]);
  m.innerHTML = `<section class="card"><h2>Mixer</h2><div class="mixer">${ch.map(x => `<div class="strip" style="--c:${x.c}" data-id="${x.id}"><b>${x.name}</b>
    <input type="range" class="fader" min="0" max="1" step="0.01" value="${x.s.vol}" data-f="vol"><label>Pan<input type="range" min="-1" max="1" step="0.05" value="${x.s.pan}" data-f="pan"></label>
    <div class="ms"><button data-f="mute" class="${x.s.mute ? 'on' : ''}">M</button><button data-f="solo" class="${x.s.solo ? 'on' : ''}">S</button></div>${S.voc.carrier === x.id && !S.voc.direct ? '<small>into vocoder</small>' : ''}</div>`).join('')}</div>
    <label class="ctl master"><span>Master</span><input id="masterVol" type="range" min="0" max="1" step="0.01" value="${S.master}"></label></section>`;
  m.querySelector('.mixer').oninput = e => { const st = e.target.closest('.strip'), f = e.target.dataset.f; if (!st || !f) return; const o = st.dataset.id === 'voc' ? S.vocMix : S.mach[st.dataset.id]; o[f] = +e.target.value; applyMix(); save(); };
  m.querySelector('.mixer').onclick = e => { const st = e.target.closest('.strip'), f = e.target.dataset.f; if (!st || (f !== 'mute' && f !== 'solo')) return; const o = st.dataset.id === 'voc' ? S.vocMix : S.mach[st.dataset.id]; o[f] = !o[f]; e.target.classList.toggle('on', o[f]); applyMix(); save(); };
  $('#masterVol').oninput = e => { S.master = +e.target.value; applyMix(); save(); };
}
function drawPlayhead(){
  requestAnimationFrame(drawPlayhead);
  const p = nowPos(); $$('.st.ph').forEach(e => e.classList.remove('ph'));
  if (!p){ $('#posLbl').textContent = ''; return; }
  const sec = song().sections[p.sec]; $('#posLbl').textContent = sec.name + ' · ' + (Math.floor(p.step / 16) + 1) + '.' + (Math.floor(p.step % 16 / 4) + 1);
  if (p.sec === S.sec && Math.floor(p.step / 16) === S.bar) $$(`#editor .st[data-s="${p.step}"]`).forEach(e => e.classList.add('ph'));
}

/* ================= transport bindings ================= */
$('#playBtn').onclick = play;
$('#bpm').onchange = e => { song().bpm = clamp(+e.target.value || 112, 40, 240); save(); };
$('#swing').onchange = e => { song().swing = clamp(+e.target.value || 0, 0, 60); save(); };
$('#songSel').onchange = e => loadSong(+e.target.value);
$('#loopBtn').onclick = () => { S.loopSec = !S.loopSec; if (playing && S.loopSec){ pos.sec = S.sec; } save(); renderTransport(); toast(S.loopSec ? 'Looping the section you are editing.' : 'Playing the whole song.'); };
$('#recBtn').onclick = toggleRec;
$('#recNotesBtn').onclick = () => { recNotes = !recNotes; renderTransport(); toast(recNotes ? 'Notes you play now are written into the playing section.' : 'Note recording off.'); };
$('#panicBtn').onclick = () => { stop(); for (const k in live.held) live.held[k] = new Set(); keyLatch = false; };
$('#midFile').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) importMidi(f); };
$('#impOk').onclick = importApply; $('#impCancel').onclick = () => $('#impDlg').close();
$('#projFile').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (!f) return; try { const o = JSON.parse(await f.text()); if (o.v !== 1 || !o.songs) throw 0; S = o; const D = defaultState(); for (const k in D) if (S[k] === undefined) S[k] = D[k]; save(); loadSong(S.cur || 0); toast('Project opened.'); } catch(_){ toast('That is not a Jam-On Rig project.'); } };
document.addEventListener('keydown', e => { if (e.code === 'Space' && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)){ e.preventDefault(); play(); } });
if (!localStorage.getItem(STORE)) loadSong(0); else renderAll();
if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
