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
{ const D = defaultState(); for (const k in D) if (S[k] === undefined) S[k] = D[k]; for (const id in MACH){ if (!S.mach[id]) S.mach[id] = D.mach[id]; S.mach[id].p = Object.assign(MACH[id].def(), S.mach[id].p); } S.voc.p = Object.assign(VOC_DEF(), S.voc.p); if (!S.learn) S.learn = {}; if (!S.midi) S.midi = {sync:'int', clockOut:false, notesOut:false, thru:false, out:''}; if (!S.cymOut) S.cymOut = JSON.parse(JSON.stringify(CYM_DEF)); }
let saveT = 0; const save = () => { clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify(S)); } catch(e){} }, 400); };
const song = () => S.songs[S.cur] || S.songs[0], section = () => song().sections[clamp(S.sec, 0, song().sections.length - 1)];

/* ================= audio graph ================= */
let ctx = null, R = null, audioReady = null;
// panel settings -> engine settings (pulse width source switches, Juno chorus buttons, SH-101 envelope trigger)
const cfgFor = id => { const M = MACH[id], p = S.mach[id].p; if (M.proc !== 'ana') return Object.assign({}, p);
  const c = Object.assign({model:M.model}, ANA_BASE[M.model], p), k = p.pwk || 0, src = p.pwmSrc | 0;
  c.pw = src === 1 ? .5 - k * .45 : .5; c.pwm = src === 0 ? k : 0; c.pwEnv = src === 2 ? k : 0;
  if (id === 'juno'){ c.chorus = p.chI && p.chII ? 3 : p.chI ? 1 : p.chII ? 2 : 0; c.fenvInv = !!p.fenvInv; }
  if (id === 'sh101') c.retrig = (p.envTrig | 0) === 0;
  return c; };
// builds every machine, the vocoder and the mixer on a context; ev = {id: [events]} for offline renders
/* every channel: source -> insert slot -> drop (dub cut) -> fader -> pan -> master; sends post-fader, throws pre-fader into the PrimeTime */
const CH = () => [...Object.keys(MACH), 'cym', 'mic', 'voc'];
const chName = id => id === 'cym' ? 'Cymbal bus' : id === 'mic' ? 'Voice' : id === 'voc' ? 'Vocoder' : MACH[id].name;
function chState(id){
  const o = id === 'voc' ? (S.vocMix || (S.vocMix = {vol:.85, pan:0, mute:false, solo:false})) : id === 'mic' ? (S.micMix || (S.micMix = {vol:.8, pan:0, mute:false, solo:false, ins:'chipmunk'})) : id === 'cym' ? (S.cymMix || (S.cymMix = {vol:.8, pan:0, mute:false, solo:false})) : S.mach[id];
  if (!o.snd) o.snd = {}; for (const u of RET_U) if (o.snd[u] == null) o.snd[u] = 0; if (!o.insP) o.insP = {}; if (!o.ins) o.ins = 'none'; return o;
}
// a return keeps its level and mute, and its unit's values under the unit's own key
function retState(u){ S.ret = S.ret || {}; return S.ret[u] || (S.ret[u] = {vol:.8, mute:false}); }
const retVals = u => fxVals(retState(u), u);
const insVals = (id, u) => fxVals(chState(id).insP, u);
function buildRig(c, ev){
  const r = {c, nodes:{}, strip:{}, pan:{}, carrier:{}, drumIn:{}, pre:{}, post:{}, drop:{}, ins:{}, snd:{}, thr:{}, ret:{}, retG:{}, sendBus:{}};
  r.masterIn = c.createGain(); r.master = c.createGain(); r.master.gain.value = S.master;
  r.lim = c.createDynamicsCompressor(); r.lim.threshold.value = -3; r.lim.knee.value = 0; r.lim.ratio.value = 20; r.lim.attack.value = .002; r.lim.release.value = .12;
  r.masterIn.connect(r.master); r.master.connect(r.lim); r.lim.connect(c.destination);
  // returns
  for (const u of RET_U){
    const bus = c.createGain(), node = new AudioWorkletNode(c, FXU[u].proc, {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:fxCfg(u, retVals(u), song().bpm)}}), g = c.createGain();
    bus.connect(node); node.connect(g); g.connect(r.masterIn); r.sendBus[u] = bus; r.ret[u] = node; r.retG[u] = g;
  }
  r.musicBus = c.createGain(); r.micBus = c.createGain();
  r.voc = new AudioWorkletNode(c, 'ehvocoder', {numberOfInputs:2, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:S.voc.p}});
  r.musicBus.connect(r.voc, 0, 0); r.micBus.connect(r.voc, 0, 1);
  for (const id of CH()){
    const pre = c.createGain(), post = c.createGain(), drop = c.createGain(), g = c.createGain(), p = c.createStereoPanner();
    post.connect(drop); drop.connect(g); g.connect(p); p.connect(r.masterIn);
    Object.assign(r, {}); r.pre[id] = pre; r.post[id] = post; r.drop[id] = drop; r.strip[id] = g; r.pan[id] = p; r.snd[id] = {};
    for (const u of RET_U){ const sg = c.createGain(); sg.gain.value = 0; g.connect(sg); sg.connect(r.sendBus[u]); r.snd[id][u] = sg; }
    const th = c.createGain(); th.gain.value = 0; post.connect(th); th.connect(r.sendBus.pt); r.thr[id] = th;
    setInsert(r, id);
  }
  r.voc.connect(r.pre.voc);
  // the Voice channel: the same mic and speech the vocoder hears, dry (with the chipmunk insert by default)
  r.vf = c.createGain(); r.vf.gain.value = 0; r.micBus.connect(r.vf); r.vf.connect(r.pre.mic);
  for (const id in MACH){
    const M = MACH[id];
    if (M.kind === 'drum'){ r.drumIn[id] = r.pre[id]; continue; }
    const n = new AudioWorkletNode(c, M.proc, {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:cfgFor(id), events:(ev && ev[id]) || []}});
    n.connect(r.pre[id]); const cs = c.createGain(); cs.gain.value = 0; n.connect(cs); cs.connect(r.musicBus); r.carrier[id] = cs; r.nodes[id] = n;
  }
  r.voc.port.onmessage = e => { if (e.data.meter) vocMeter(e.data.meter); };
  applyMix(r);
  return r;
}
function setInsert(r, id){
  const st = chState(id), u = st.ins, pre = r.pre[id], post = r.post[id];
  pre.disconnect(); if (r.ins[id]){ r.ins[id].disconnect(); r.ins[id] = null; }
  if (!FXU[u]){ pre.connect(post); return; }
  const n = new AudioWorkletNode(r.c, FXU[u].proc, {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{cfg:fxCfg(u, insVals(id, u), song().bpm)}});
  pre.connect(n); n.connect(post); r.ins[id] = n;
}
function pushFx(kind, id){
  if (!R) return;
  if (kind === 'ret') R.ret[id].port.postMessage({type:'cfg', cfg:fxCfg(id, retVals(id), song().bpm)});
  else { const u = chState(id).ins; if (R.ins[id] && FXU[u]) R.ins[id].port.postMessage({type:'cfg', cfg:fxCfg(u, insVals(id, u), song().bpm)}); }
}
function applyMix(r = R){
  if (!r) return;
  const ids = CH(), solo = ids.some(id => chState(id).solo);
  for (const id of ids){
    const m = chState(id), isCar = S.voc.carrier === id;
    let g = m.vol * m.vol * (id === 'voc' ? 1.4 : 1.2); if (m.mute || (solo && !m.solo)) g = 0; if (isCar && !S.voc.direct) g = 0;
    r.strip[id].gain.value = g; r.pan[id].pan.value = m.pan || 0;
    if (r.carrier[id]) r.carrier[id].gain.value = isCar ? 1 : 0;
    for (const u of RET_U) r.snd[id][u].gain.value = (m.snd[u] || 0) * (m.snd[u] || 0) * 1.2;
  }
  for (const u of RET_U){ const o = retState(u); r.retG[u].gain.value = o.mute ? 0 : o.vol * o.vol * 1.3; }
  r.vf.gain.value = S.voiceDry ? 1 : 0;
  r.master.gain.value = S.master;
}
// the dry Voice channel, with its chipmunk insert (used by the Chipmunk vocoder preset and the button on the Vocoder screen)
function setVoice(on, v){
  S.voiceDry = on; const st = chState('mic');
  if (on){ st.ins = (v && v.ins) || 'chipmunk'; if (FXU[st.ins]) fxPreset(st.insP, st.ins, (v && v.p) || Object.keys(FXU[st.ins].presets)[0]); if (R) setInsert(R, 'mic'); }
  applyMix(); save();
}
// dub moves: THROW sends the channel (before its fader) into the PrimeTime while held; DROP cuts it while held
function dubThrow(id, on){ if (!R) return; R.thr[id].gain.setTargetAtTime(on ? 1.1 : 0, ctx.currentTime, on ? .004 : .02); $$(`[data-learn="thr:${id}"]`).forEach(e => e.classList.toggle('on', on)); }
function dubDrop(id, on){ if (!R) return; R.drop[id].gain.setTargetAtTime(on ? 0 : 1, ctx.currentTime, .004); $$(`[data-learn="drp:${id}"]`).forEach(e => e.classList.toggle('on', on)); }
function applyMixPreset(name){
  const P = MIX_PRESETS[name]; if (!P) return;
  for (const id of CH()){ const st = chState(id); st.snd = Object.assign({}, (P.snd || {})[id] || {}); }
  const cy = chState('cym'); cy.ins = P.cym ? P.cym.ins : 'none'; if (P.cym && P.cym.p && FXU[cy.ins]) fxPreset(cy.insP, cy.ins, P.cym.p);
  for (const u of RET_U){ const pn = (P.ret || {})[u]; if (pn) fxPreset(retState(u), u, pn); }
  S.mixPreset = name; save();
  if (R){ setInsert(R, 'cym'); for (const u of RET_U) pushFx('ret', u); applyMix(); }
}
async function ensureAudio(){
  if (audioReady) return audioReady;
  audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    await ctx.audioWorklet.addModule('dsp/worklets.js'); await ctx.audioWorklet.addModule('dsp/rack.js');
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
    const prm = v === 'bd' ? {tone:q(p.bdT), decay:q(p.bdD)} : v === 'sd' ? {tone:q(p.sdT), snappy:q(p.sdS)} : /^(lt|mt|ht|lc|mc|hc)$/.test(v) ? {tune:q(p[k808(v) + 'T'])} : v === 'cy' ? {tone:q(p.cyT), decay:q(p.cyD)} : v === 'oh' ? {decay:q(p.ohD)} : {};
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
  const lk = id === 'd808' ? k808(v) + 'L' : v + 'L', lv = p[lk] == null ? .8 : p[lk]; g.gain.value = lv * lv * (acc ? (id === 'dmx' ? 1 + p.acc * .6 : 1.15) : .8);
  s.connect(g); g.connect((S.cymOut[id] || []).includes(v) ? r.pre.cym : r.drumIn[id]); s.start(t);
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
const midiTs = t => performance.now() + (t - ctx.currentTime + (ctx.outputLatency || ctx.baseLatency || 0)) * 1000;
function midiOutDev(){ if (!midiAccess || !S.midi.out) return null; return midiAccess.outputs.get(S.midi.out) || null; }
function midiSend(bytes, t){ const o = midiOutDev(); if (!o) return; try { t == null ? o.send(bytes) : o.send(bytes, Math.max(performance.now(), midiTs(t))); } catch(e){} }
function midiNoteOut(id, n, v, t0, t1){ if (!S.midi.notesOut) return; const ch = S.midiCh[id] & 15; midiSend([0x90 | ch, n & 127, Math.max(1, Math.round(v * 127))], t0); midiSend([0x80 | ch, n & 127, 0], t1); }
const liveOut = {note:(id, n, v, t0, t1, sl) => { sendNote(R, id, n, v, t0, t1, sl); midiNoteOut(id, n, v, t0, t1); },
  hit:(id, v, t, a) => { drumHit(R, id, v, t, a); midiNoteOut(id, MACH[id].gm[v], a ? 1 : .7, t, t + .05); }, say:(text, t) => speak(R, text, t)};
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
        if (S.midi.clockOut) for (let k = 0; k < 6; k++) midiSend([0xF8], nextT + k * sd / 6);
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
async function play(){ if (playing){ stop(); return; } return startAt(null); }
// start the song at a 16th-note position (MIDI song position), or at the section being edited
function posFromSteps(n){ const secs = song().sections; let k = 0; for (let i = 0; i < secs.length; i++) for (let r = 0; r < secs[i].rep; r++){ const len = secs[i].bars * 16; if (n < k + len) return {sec:i, rep:r, step:n - k}; k += len; } return {sec:0, rep:0, step:0}; }
function stepsFromPos(p){ const secs = song().sections; let k = 0; for (let i = 0; i < p.sec; i++) k += secs[i].bars * 16 * secs[i].rep; return k + p.rep * secs[p.sec].bars * 16 + p.step; }
async function startAt(steps, fromMidi){
  await ensureAudio(); if (ctx.state !== 'running') await ctx.resume();
  playing = true; pos = steps == null ? {sec:S.sec, rep:0, step:0} : posFromSteps(steps); nextT = ctx.currentTime + .06; stepLog = [];
  if (!fromMidi && S.midi.clockOut){ const spp = stepsFromPos(pos); midiSend([0xF2, spp & 127, (spp >> 7) & 127]); midiSend(spp ? [0xFB] : [0xFA], nextT); }
  renderTransport();
}
function stop(fromMidi){ if (playing && !fromMidi && S.midi.clockOut) midiSend([0xFC]); playing = false; if (R) for (const id in R.nodes) R.nodes[id].port.postMessage({type:'panic'}); for (const id in R ? R.nodes : {}) for (let n = 0; n < 128; n++) R.nodes[id].port.postMessage({type:'ev', ev:{f:0, off:n}}); renderTransport(); }
function nowPos(){ if (!playing || !ctx) return null; let p = null; for (const s of stepLog) if (s.t <= ctx.currentTime) p = s; return p; }

/* ================= live playing: on-screen keys, pads, MIDI ================= */
function liveOn(id, n, v = .85){
  ensureAudio().then(() => {
    const M = MACH[id];
    if (M.kind === 'drum'){ const vv = M.voices.find(([k]) => M.gm[k] === n) || M.voices[((n - 36) % 16 + 16) % 16]; drumHit(R, id, vv[0], ctx.currentTime + .005, v > .9); recordHit(id, vv[0], v > .9); return; }
    const A = S.mach[id].arp;
    if (A && A.mode !== 'off'){ (live.held[id] || (live.held[id] = new Set())).add(n); return; }
    R.nodes[id].port.postMessage({type:'ev', ev:{f:0, on:n, v}}); recordNote(id, n, v, true);
    if (S.midi.notesOut && !S.midi.thru) midiSend([0x90 | (S.midiCh[id] & 15), n & 127, Math.max(1, Math.round(v * 127))]);
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
let midiAccess = null;
async function midiInit(){
  if (!navigator.requestMIDIAccess){ toast('This browser has no Web MIDI. On the S23 use Chrome.'); return; }
  try {
    const acc = midiAccess = await navigator.requestMIDIAccess({sysex:false});
    const hook = () => { let n = 0; acc.inputs.forEach(inp => { inp.onmidimessage = onMidi; n++; }); $('#midiStat').textContent = 'MIDI: ' + n + ' in, ' + acc.outputs.size + ' out'; if (S.tab === 'song') renderMain(); };
    acc.onstatechange = hook; hook(); toast('MIDI on. Notes play ' + (S.midiMode === 'selected' ? 'the machine on screen' : 'by channel') + '.');
  } catch(e){ toast('MIDI was not allowed.'); }
}
/* MIDI in: notes with velocity, sustain (CC64), pitch bend, mod wheel (CC1), volume (CC7), pan (CC10), program change
   (picks the machine's preset), all sound off / reset / all notes off (CC120, 121, 123), and clock, start, stop, continue
   and song position when the tempo follows an external clock. */
const sus = {}, susHeld = {}, ext = {last:0, iv:[], rt:0};
function midiPanic(){ for (let c = 0; c < 16; c++){ midiSend([0xB0 | c, 123, 0]); midiSend([0xB0 | c, 120, 0]); } }
function machineFor(ch){ let id = curTab(); if (S.midiMode === 'channel' || !MACH[id]) id = Object.keys(S.midiCh).find(k => S.midiCh[k] === ch); return id; }
function bendTo(id, v){ if (!R || !R.nodes[id]) return; const M = MACH[id];
  if (M.proc === 'ana') R.nodes[id].port.postMessage({type:'cfg', cfg:{bend:v * 2}}); else R.nodes[id].port.postMessage({type:'bend', v:v * 2}); }
function allOff(id){ if (!R || !R.nodes[id]) return; R.nodes[id].port.postMessage({type:'panic'}); for (let n = 0; n < 128; n++) R.nodes[id].port.postMessage({type:'ev', ev:{f:0, off:n}}); if (live.held[id]) live.held[id] = new Set(); susHeld[id] = new Set(); }
function midiRealtime(st, ts){
  if (S.midi.sync !== 'ext') return;
  if (st === 0xF8){ if (ext.last){ const dt = ts - ext.last; if (dt > 4 && dt < 250){ ext.iv.push(dt); if (ext.iv.length > 48) ext.iv.shift();
      const avg = ext.iv.reduce((a, b) => a + b, 0) / ext.iv.length, bpm = Math.round(60000 / (avg * 24) * 10) / 10;
      if (Math.abs(bpm - song().bpm) >= .2 && ext.iv.length >= 12){ song().bpm = clamp(bpm, 40, 240); clearTimeout(ext.rt); ext.rt = setTimeout(renderTransport, 200); } } }
    ext.last = ts; }
  else if (st === 0xFA){ ext.iv = []; startAt(0, true); }
  else if (st === 0xFB){ startAt(ext.spp || 0, true); }
  else if (st === 0xFC) stop(true);
}
function onMidi(e){
  const d = e.data, st = d[0];
  if (st >= 0xF8) return midiRealtime(st, e.timeStamp || performance.now());
  if (st === 0xF2){ ext.spp = d[1] | d[2] << 7; if (S.midi.sync === 'ext' && playing) startAt(ext.spp, true); return; }
  if (st >= 0xF0) return;
  const hi = st & 0xF0, ch = st & 15, d1 = d[1], d2 = d[2];
  if (hi === 0xB0 || (hi === 0x90 && d2)){ const src = (hi === 0xB0 ? 'cc:' : 'note:') + ch + ':' + d1;
    if (learn.on && learn.sel){ learnBind(src); return; }
    const key = S.learn[src]; if (key){ learnApply(key, d2 / 127, hi === 0x90 || d2 >= 64, hi === 0x90); return; } }
  if (hi === 0x80 || (hi === 0x90 && !d2)){ const key = S.learn['note:' + ch + ':' + d1]; if (key){ learnApply(key, 0, false, true); return; } }
  const id = machineFor(ch); if (!id) return;
  if (hi === 0x90 && d2){ if (susHeld[id]) susHeld[id].delete(d1); liveOn(id, d1, d2 / 127); if (S.midi.thru) midiSend([0x90 | (S.midiCh[id] & 15), d1, d2]); }
  else if (hi === 0x80 || hi === 0x90){ if (sus[id]) (susHeld[id] || (susHeld[id] = new Set())).add(d1); else liveOff(id, d1); if (S.midi.thru) midiSend([0x80 | (S.midiCh[id] & 15), d1, 0]); }
  else if (hi === 0xE0) bendTo(id, ((d2 << 7 | d1) - 8192) / 8192);
  else if (hi === 0xC0){ const names = Object.keys(PRESETS[id] || {}); if (names[d1]){ applyPreset(id, names[d1]); if (curTab() === id) renderMain(); toast(MACH[id].name + ': ' + names[d1]); } }
  else if (hi === 0xB0){
    if (d1 === 1 && R && R.nodes[id]) R.nodes[id].port.postMessage({type:'wheel', v:d2 / 127});
    else if (d1 === 64){ sus[id] = d2 >= 64; if (!sus[id] && susHeld[id]){ for (const n of susHeld[id]) liveOff(id, n); susHeld[id].clear(); } }
    else if (d1 === 7){ chState(id).vol = d2 / 127; applyMix(); }
    else if (d1 === 10){ chState(id).pan = (d2 - 64) / 64; applyMix(); }
    else if (d1 === 120 || d1 === 123) allOff(id);
    else if (d1 === 121){ bendTo(id, 0); sus[id] = false; if (R && R.nodes[id]) R.nodes[id].port.postMessage({type:'wheel', v:0}); }
  }
}

/* ================= MIDI learn: every knob, slider, switch and selector, the mixer and the transport ================= */
const learn = {on:false, sel:null};
function learnSpec(key){
  const [kind, id, k] = key.split(':');
  if (kind === 'p'){ const c = MACH[id] && MACH[id].panel.find(x => x.k === k); return c && {c, vals:S.mach[id].p, name:MACH[id].name + ' · ' + c.g + ' · ' + c.l, after:() => { S.mach[id].preset = ''; pushCfg(id); }}; }
  if (kind === 'v'){ const c = VOC_PANEL.find(x => x.k === k); return c && {c, vals:S.voc.p, name:'Vocoder · ' + c.g + ' · ' + c.l, after:() => { S.voc.preset = ''; pushCfg('voc'); }}; }
  if (kind === 'f'){ const c = FXU[id].ctl.find(x => x.k === k); return c && {c, vals:retVals(id), name:FXU[id].short + ' · ' + c.l, after:() => { retVals(id)._preset = ''; pushFx('ret', id); }}; }
  if (kind === 'i'){ const u = chState(id).ins, c = FXU[u] && FXU[u].ctl.find(x => x.k === k); return c && {c, vals:insVals(id, u), name:chName(id) + ' · ' + FXU[u].short + ' · ' + c.l, after:() => pushFx('ins', id)}; }
  if (kind === 'snd') return {c:{t:'k', k}, vals:chState(id).snd, name:chName(id) + ' · Send · ' + FXU[k].short, after:() => applyMix()};
  if (kind === 'rv') return {c:{t:'k', k:'vol'}, vals:retState(id), name:FXU[id].short + ' · Return level', after:() => applyMix()};
  if (kind === 'thr') return {c:{t:'mom'}, name:chName(id) + ' · Echo throw', set:on => dubThrow(id, on)};
  if (kind === 'drp') return {c:{t:'mom'}, name:chName(id) + ' · Drop', set:on => dubDrop(id, on)};
  if (kind === 'mix'){ const o = chState(id), nm = chName(id) + ' · Mixer · ';
    return {c:{t:k === 'mute' || k === 'solo' ? 's' : 'k', k, l:k}, vals:o, name:nm + k, after:() => applyMix(), pan:k === 'pan'}; }
  if (kind === 'master') return {c:{t:'k', k:'master'}, vals:S, name:'Master level', after:() => applyMix()};
  if (kind === 'play') return {c:{t:'btn'}, name:'Play / Stop', press:() => play()};
  if (kind === 'loop') return {c:{t:'btn'}, name:'Loop section', press:() => $('#loopBtn').click()};
  if (kind === 'say') return {c:{t:'btn'}, name:'Vocoder · Say it', press:() => sayNow()};
  if (kind === 'chip') return {c:{t:'btn'}, name:'Chipmunk voice on / off', press:() => { setVoice(!S.voiceDry, {ins:'chipmunk'}); if (S.tab === 'voc') renderMain(); }};
  return null;
}
function learnApply(key, v, press, isNote){
  const L = learnSpec(key); if (!L) return; const c = L.c;
  if (c.t === 'btn'){ if (press) L.press(); return; }
  if (c.t === 'mom'){ L.set(!!press); return; }
  if (c.t === 's'){ if (isNote){ if (press) L.vals[c.k] = L.vals[c.k] ? 0 : 1; } else L.vals[c.k] = v >= .5 ? 1 : 0; }
  else if (c.t === 'sel'){ const o = c.opts[Math.min(c.opts.length - 1, Math.floor(v * c.opts.length))]; L.vals[c.k] = o[0]; }
  else L.vals[c.k] = L.pan ? Math.round((v * 2 - 1) * 20) / 20 : Math.round(v * 100) / 100;
  L.after(); save();
  // move the control on screen if it is showing
  const el = document.querySelector(`[data-learn="${key}"]`); if (!el) return; const val = L.vals[c.k];
  if (el.classList.contains('dial')) dialSet(el, val);
  else if (el.classList.contains('seg')) el.querySelectorAll('button').forEach(b => b.classList.toggle('on', +b.dataset.v === val));
  else if (el.tagName === 'INPUT') el.value = val; else el.classList.toggle('on', !!val);
}
function learnBind(src){
  for (const k in S.learn) if (S.learn[k] === learn.sel) delete S.learn[k];
  S.learn[src] = learn.sel; save();
  const [t, ch, n] = src.split(':'), L = learnSpec(learn.sel);
  toast((t === 'cc' ? 'CC ' + n : 'Note ' + nn(+n)) + ' (ch ' + (+ch + 1) + ') → ' + (L ? L.name : learn.sel));
  learn.sel = null; $$('.learn-sel').forEach(e => e.classList.remove('learn-sel')); markLearned();
}
function markLearned(){ const on = new Set(Object.values(S.learn || {})); $$('[data-learn]').forEach(e => e.classList.toggle('mapped', on.has(e.dataset.learn))); }
async function learnToggle(){
  learn.on = !learn.on; learn.sel = null; document.body.classList.toggle('learning', learn.on); $('#learnBtn').classList.toggle('on', learn.on);
  if (learn.on){ if (!midiAccess) await midiInit(); toast('MIDI Learn: tap a control, then move a knob or press a key on your MIDI device. Tap Learn again to finish.', 4500); }
  $$('.learn-sel').forEach(e => e.classList.remove('learn-sel'));
}
// while learning, a tap picks the control instead of changing it
['pointerdown', 'click'].forEach(ev => document.addEventListener(ev, e => {
  if (!learn.on) return; const el = e.target.closest('[data-learn]'); if (!el) return;
  e.preventDefault(); e.stopImmediatePropagation();
  if (ev === 'click') return;
  $$('.learn-sel').forEach(x => x.classList.remove('learn-sel')); el.classList.add('learn-sel'); learn.sel = el.dataset.learn;
  const L = learnSpec(learn.sel); toast('Now move a control on your MIDI device for: ' + (L ? L.name : learn.sel));
}, true));

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
    const oc = new OfflineAudioContext(2, Math.ceil(dur * sr), sr); await oc.audioWorklet.addModule('dsp/worklets.js'); await oc.audioWorklet.addModule('dsp/rack.js');
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
  if (id === 'voc'){ const P = PRESETS.voc[name] || (S.user.voc || {})[name]; if (!P) return; S.voc.p = Object.assign(VOC_DEF(), P.voc); S.voc.tts = Object.assign({}, P.tts); S.voc.preset = name; if (R) R.voc.port.postMessage(S.voc.p);
    setVoice(!!P.voice, P.voice); save(); return; }
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
const TABS = [['song', 'Song'], ['d808', 'TR-808'], ['dmx', 'DMX'], ['tb303', 'TB-303'], ['proone', 'Pro-One'], ['sh101', 'SH-101'], ['juno', 'Juno-60'], ['rs09', 'RS-09'], ['pbass', 'Chilly Bass'], ['voc', 'Vocoder'], ['fx', 'Mix FX'], ['mix', 'Mixer'], ['about', 'About']];
const curTab = () => S.tab;
function renderTabs(){ $('#tabs').innerHTML = TABS.map(([k, l]) => `<button data-k="${k}" class="${S.tab === k ? 'on' : ''}" style="--c:${MACH[k] ? MACH[k].color : k === 'voc' ? '#7fd' : '#e8742b'}">${gearSVG(k)}<span>${l}</span></button>`).join(''); }
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
// rotary knob: drag up / down (or left / right), double-tap for the middle; arrow keys work too
const knobArc = v => { const a0 = -225 * Math.PI / 180, a = a0 + v * 270 * Math.PI / 180, r = 15, p = t => (20 + r * Math.cos(t)).toFixed(2) + ' ' + (20 + r * Math.sin(t)).toFixed(2);
  return {track:`M${p(a0)} A${r} ${r} 0 1 1 ${p(a0 + 270 * Math.PI / 180)}`, val:v > .002 ? `M${p(a0)} A${r} ${r} 0 ${v * 270 > 180 ? 1 : 0} 1 ${p(a)}` : '', x:(20 + 10 * Math.cos(a)).toFixed(2), y:(20 + 10 * Math.sin(a)).toFixed(2)}; };
const dialText = (el, v) => { const f = el.dataset.fmt; return f != null && f !== '' ? FMTS[+f](toX({min:+el.dataset.min, max:+el.dataset.max, curve:el.dataset.curve}, v)) : (v * 10).toFixed(1); };
const ctlAttrs = c => c && c.fmt != null ? `data-fmt="${c.fmt}" data-min="${c.min}" data-max="${c.max}" data-curve="${c.curve}"` : '';
const ctlText = (c, v) => c && c.fmt != null ? FMTS[c.fmt](toX(c, v)) : (v * 10).toFixed(1);
const knobHTML = (k, l, v, L = '', c = null) => { const A = knobArc(v); return `<div class="knob dial" data-k="${k}" ${L} ${ctlAttrs(c)} tabindex="0" role="slider" aria-label="${esc(l)}" aria-valuemin="0" aria-valuemax="10" aria-valuenow="${(v * 10).toFixed(1)}"><svg viewBox="0 0 40 40"><path class="kt" d="${A.track}"/><circle class="body" cx="20" cy="20" r="11.5"/><path class="kv" d="${A.val}"/><line x1="20" y1="20" x2="${A.x}" y2="${A.y}"/></svg><b>${ctlText(c, v)}</b><span>${esc(l)}</span></div>`; };
function knobSet(el, v){ const A = knobArc(v); el.querySelector('.kv').setAttribute('d', A.val); const ln = el.querySelector('line'); ln.setAttribute('x2', A.x); ln.setAttribute('y2', A.y); el.querySelector('b').textContent = dialText(el, v); el.setAttribute('aria-valuenow', (v * 10).toFixed(1)); }
const lookVars = L => L ? `--pbg:${L.bg};--pink:${L.ink};--kb:${L.knob || '#333'};--kp:${L.ptr || '#eee'};--kr:${L.ring || '#555'};--cap:${L.cap || '#eee'};--track:${L.track || '#000'}` : '';
function panelHTML(id, panel, vals, look, preOver){
  const groups = []; for (const c of panel){ let g = groups.find(x => x.g === c.g); if (!g) groups.push(g = {g:c.g, cs:[]}); g.cs.push(c); }
  const sel = (S.grp || (S.grp = {}))[id], cur = groups.some(g => g.g === sel) ? sel : groups[0].g, pre = preOver || (id === 'voc' ? 'v:' : 'p:' + id + ':'), swk = (look && look.sw) || 'btn';
  return `<div class="panel" data-pid="${id}" style="${lookVars(look)}">${groups.length > 1 ? `<div class="chips">${groups.map(g => `<button data-g="${esc(g.g)}" class="${g.g === cur ? 'on' : ''}">${esc(g.g)}</button>`).join('')}</div>` : ''}
    ${groups.map(g => `<fieldset class="grp ${g.g === cur ? 'on' : ''}" data-g="${esc(g.g)}"><legend>${esc(g.g)}</legend><div class="knobs">${g.cs.map(c => {
    const v = vals[c.k], L = `data-learn="${pre}${c.k}"`;
    if (c.t === 's') return `<button class="sw sw-${swk} ${v ? 'on' : ''}" data-k="${c.k}" ${L}><i></i><span>${esc(c.l)}</span></button>`;
    if (c.t === 'sel') return `<div class="seg" data-k="${c.k}" ${L}><div>${c.opts.map(([ov, ol]) => `<button data-v="${ov}" class="${+v === ov ? 'on' : ''}">${esc(ol)}</button>`).join('')}</div><span>${esc(c.l)}</span></div>`;
    if (c.t === 'f'){ const cap = (look && look.caps && look.caps[c.g]) || ''; return `<div class="fader dial" data-k="${c.k}" ${L} tabindex="0" role="slider" aria-label="${esc(c.l)}"><div class="ftrack"><i style="bottom:calc(${((v == null ? .5 : v) * 100).toFixed(1)}% * .84 + 2px);${cap ? 'background:' + cap : ''}"></i></div><b>${((v == null ? .5 : v) * 10).toFixed(1)}</b><span>${esc(c.l)}</span></div>`; }
    return knobHTML(c.k, c.l, v == null ? .5 : v, L, c);
  }).join('')}</div></fieldset>`).join('')}</div>`;
}
function dialSet(el, v){ if (el.classList.contains('knob')) return knobSet(el, v); el.querySelector('.ftrack i').style.bottom = `calc(${(v * 100).toFixed(1)}% * .84 + 2px)`; el.querySelector('b').textContent = (v * 10).toFixed(1); }
function bindPanel(root, vals, onChange){
  const P = root.querySelector('.panel'); if (!P) return;
  P.querySelectorAll('.chips button').forEach(b => b.onclick = () => { S.grp[P.dataset.pid] = b.dataset.g; save();
    P.querySelectorAll('.chips button').forEach(x => x.classList.toggle('on', x === b)); P.querySelectorAll('.grp').forEach(f => f.classList.toggle('on', f.dataset.g === b.dataset.g)); });
  P.querySelectorAll('.seg').forEach(el => el.querySelectorAll('button').forEach(b => b.onclick = () => { vals[el.dataset.k] = +b.dataset.v; el.querySelectorAll('button').forEach(x => x.classList.toggle('on', x === b)); onChange(); }));
  P.querySelectorAll('.sw').forEach(el => el.onclick = () => { vals[el.dataset.k] = vals[el.dataset.k] ? 0 : 1; el.classList.toggle('on', !!vals[el.dataset.k]); onChange(); });
  P.querySelectorAll('.dial').forEach(el => {
    const k = el.dataset.k, set = v => { v = clamp(Math.round(v * 100) / 100, 0, 1); if (v === vals[k]) return; vals[k] = v; dialSet(el, v); onChange(); };
    const span = el.classList.contains('fader') ? 110 : 160;
    let drag = null, lastTap = 0;
    el.onpointerdown = e => { e.preventDefault(); el.setPointerCapture(e.pointerId); drag = {x:e.clientX, y:e.clientY, v:vals[k] == null ? .5 : vals[k]}; el.classList.add('drag');
      const now = Date.now(); if (now - lastTap < 300) set(.5); lastTap = now; };
    el.onpointermove = e => { if (!drag) return; set(drag.v + ((drag.y - e.clientY) + (el.classList.contains('fader') ? 0 : e.clientX - drag.x)) / span); };
    el.onpointerup = el.onpointercancel = () => { drag = null; el.classList.remove('drag'); };
    el.onkeydown = e => { const d = {ArrowUp:.05, ArrowRight:.05, ArrowDown:-.05, ArrowLeft:-.05}[e.key]; if (d){ e.preventDefault(); set((vals[k] == null ? .5 : vals[k]) + d); } };
  });
  markLearned();
}
// a machine screen is split into views so each one fits on a phone; wide screens show them all
function viewTabs(id, list){ const cur = (S.view || (S.view = {}))[id] || list[0][0]; return `<div class="vtabs">${list.map(([k, l]) => `<button data-view="${k}" class="${k === cur ? 'on' : ''}">${l}</button>`).join('')}</div>`; }
const vw = (id, k, list) => (((S.view || {})[id]) || list[0][0]) === k ? 'vw on' : 'vw';
function bindViews(m, id){ m.querySelectorAll('[data-view]').forEach(b => b.onclick = () => { S.view[id] = b.dataset.view; save();
  m.querySelectorAll('[data-view]').forEach(x => x.classList.toggle('on', x === b)); m.querySelectorAll('.vw').forEach(v => v.classList.toggle('on', v.dataset.v === b.dataset.view)); }); }
function presetRow(id){
  const names = Object.keys(PRESETS[id] || {}), user = Object.keys(S.user[id] || {}), cur = id === 'voc' ? S.voc.preset : S.mach[id].preset;
  return `<div class="row"><select id="preSel"><option value="">Preset…</option>${names.map(n => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('')}${user.length ? '<optgroup label="Yours">' + user.map(n => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('') + '</optgroup>' : ''}</select>
    <button id="preSave">Save as…</button></div>`;
}
function bindPresetRow(id){
  $('#preSel').onchange = e => { if (e.target.value){ applyPreset(id, e.target.value); renderAll(); } };
  $('#preSave').onclick = () => { const n = prompt('Name for this sound:'); if (!n) return; (S.user[id] || (S.user[id] = {}))[n] = id === 'voc' ? {voc:Object.assign({}, S.voc.p), tts:Object.assign({}, S.voc.tts)} : Object.assign({}, S.mach[id].p); if (id === 'voc') S.voc.preset = n; else S.mach[id].preset = n; save(); renderAll(); toast('Saved.'); };
}
function renderAll(){ renderTabs(); renderTransport(); renderMain(); markLearned(); }
function renderMain(){
  const m = $('#main'), k = S.tab;
  if (k === 'song') return renderSong(m);
  if (k === 'mix') return renderMixer(m);
  if (k === 'fx') return renderFx(m);
  if (k === 'about') return renderAbout(m);
  if (k === 'voc') return renderVoc(m);
  renderMachine(m, k);
}
function renderSong(m){
  const sg = song();
  m.innerHTML = `<section class="card"><h2>${esc(sg.name)}</h2><p class="hint">${esc(sg.note || '')}${sg.key ? ' · ' + esc(sg.key) : ''}</p>
    <div class="row"><button id="songNew">New song</button><button id="songDup">Copy</button><button id="songRen">Rename</button><button id="songDel">Delete</button><button id="songReset">Reset the 7 styles</button></div></section>
    <section class="card"><h3>Sections <small>play in order; tap one to edit it</small></h3><div id="secList">${sg.sections.map((s, i) => `<div class="secrow ${i === S.sec ? 'on' : ''}" data-i="${i}">
      <input value="${esc(s.name)}" data-f="name"><label><select data-f="bars" aria-label="Bars">${[1, 2, 4, 8, 16].concat(s.bars > 16 ? [s.bars] : []).map(b => `<option ${b === s.bars ? 'selected' : ''}>${b}</option>`).join('')}</select>bars</label>
      <label>×<select data-f="rep">${[1, 2, 3, 4, 6, 8].map(b => `<option ${b === s.rep ? 'selected' : ''}>${b}</option>`).join('')}</select></label>
      <span class="acts"><button data-a="up" title="Move up" aria-label="Move up">↑</button><button data-a="dup" title="Copy" aria-label="Copy">⧉</button><button data-a="del" title="Delete" aria-label="Delete">✕</button></span><span class="parts">${partsSummary(s)}</span></div>`).join('')}</div>
      <div class="row"><button id="secAdd">Add section</button></div></section>
    <details class="card fold" ${S.foldFiles ? "open" : ""} data-fold="foldFiles"><summary><h3>Files: WAV, MIDI, project</h3></summary><div class="row"><button id="wavBtn">Export WAV</button><button id="midBtn">Export MIDI</button><button id="midImp">Import MIDI</button><button id="projSave">Save project</button><button id="projLoad">Open project</button></div>
    <p class="hint">MIDI export has one track per machine: drums on their channels with General MIDI notes, arpeggios written out as they play. Import puts every track on a machine you pick.</p></details>
    <details class="card fold" ${S.foldMidi ? "open" : ""} data-fold="foldMidi"><summary><h3>MIDI input</h3></summary><div class="row"><button id="midiOn">Connect MIDI</button><span id="midiStat2" class="hint"></span>
      <select id="midiMode"><option value="selected" ${S.midiMode === 'selected' ? 'selected' : ''}>Plays the machine on screen</option><option value="channel" ${S.midiMode === 'channel' ? 'selected' : ''}>Plays by channel</option></select></div>
      <h3>Sync and output</h3><div class="row"><label>Tempo<select id="mSync"><option value="int" ${S.midi.sync === 'int' ? 'selected' : ''}>Internal</option><option value="ext" ${S.midi.sync === 'ext' ? 'selected' : ''}>Follow MIDI clock in</option></select></label>
        <label>Out<select id="mOut"><option value="">None</option>${midiAccess ? [...midiAccess.outputs.values()].map(o => `<option value="${esc(o.id)}" ${S.midi.out === o.id ? 'selected' : ''}>${esc(o.name)}</option>`).join('') : ''}</select></label></div>
      <div class="row"><label><input type="checkbox" id="mClock" ${S.midi.clockOut ? 'checked' : ''}> Send clock, start / stop and song position</label><label><input type="checkbox" id="mNotes" ${S.midi.notesOut ? 'checked' : ''}> Send every machine's notes on its channel</label><label><input type="checkbox" id="mThru" ${S.midi.thru ? 'checked' : ''}> MIDI thru</label></div>
      <p class="hint">In: notes and velocity, sustain, pitch bend, mod wheel, volume (CC7), pan (CC10), program change (picks a preset), all notes off. With "Follow MIDI clock in" the tempo, start, stop, continue and song position come from your MPC or DAW.</p>
      <h3>MIDI Learn</h3><p class="hint">Tap <b>Learn</b> (top bar, under ⋯), tap any knob, slider, switch, mixer control or Play, then move a control on your MIDI device.</p>
      <div id="learnList">${Object.entries(S.learn).map(([src, key]) => { const [t, ch, n] = src.split(':'), L = learnSpec(key); return `<div class="sayrow"><span class="grow">${t === 'cc' ? 'CC ' + n : 'Note ' + nn(+n)} · ch ${+ch + 1} → ${esc(L ? L.name : key)}</span><button data-src="${src}">✕</button></div>`; }).join('') || '<p class="hint">Nothing learned yet.</p>'}</div>
      <h3>Channels</h3><div class="chmap">${Object.keys(MACH).map(id => `<label>${MACH[id].name}<select data-ch="${id}">${Array.from({length:16}, (_, c) => `<option value="${c}" ${S.midiCh[id] === c ? 'selected' : ''}>ch ${c + 1}</option>`).join('')}</select></label>`).join('')}</div></details>`;
  m.querySelectorAll('[data-fold]').forEach(d => d.ontoggle = () => { S[d.dataset.fold] = d.open; save(); });
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
  $('#mSync').onchange = e => { S.midi.sync = e.target.value; save(); };
  $('#mOut').onchange = e => { S.midi.out = e.target.value; save(); };
  $('#mClock').onchange = e => { S.midi.clockOut = e.target.checked; save(); };
  $('#mNotes').onchange = e => { S.midi.notesOut = e.target.checked; save(); };
  $('#mThru').onchange = e => { S.midi.thru = e.target.checked; save(); };
  $('#learnList').onclick = e => { const src = e.target.dataset.src; if (!src) return; delete S.learn[src]; save(); renderMain(); };
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
const MV = [['pat', 'Pattern'], ['snd', 'Sound'], ['play', 'Play']];
function renderMachine(m, id){
  const M = MACH[id], ms = S.mach[id];
  m.innerHTML = `<section class="card mach" style="--c:${M.color}"><div class="mhead">${gearSVG(id, 'hero')}<div class="mtitle"><h2>${M.name}</h2><span class="maker">${M.maker}</span></div>${viewTabs(id, MV)}</div>
    <div class="${vw(id, 'pat', MV)}" data-v="pat">${sectionBar()}<div id="editor"></div></div>
    <div class="${vw(id, 'snd', MV)}" data-v="snd">${presetRow(id)}
    ${M.kind !== 'drum' ? `<div class="row arp"><label>Arp<select id="arpMode">${['off', 'up', 'down', 'updown', 'random'].map(x => `<option ${ms.arp.mode === x ? 'selected' : ''}>${x}</option>`).join('')}</select></label>
      <label>Oct<select id="arpOct">${[1, 2, 3].map(x => `<option ${ms.arp.oct === x ? 'selected' : ''}>${x}</option>`).join('')}</select></label>
      <label>Rate<select id="arpRate">${[[8, '1/8'], [16, '1/16'], [32, '1/32']].map(([v, l]) => `<option value="${v}" ${ms.arp.rate === v ? 'selected' : ''}>${l}</option>`).join('')}</select></label></div>` : ''}
    ${panelHTML(id, M.panel, ms.p, M.look)}</div>
    <div class="${vw(id, 'play', MV)}" data-v="play">${M.kind === 'drum' ? '<div id="pads" class="pads"></div>' : `<div class="row"><button id="kbDn">◀ Oct</button><span id="kbLbl"></span><button id="kbUp">Oct ▶</button><button id="latch" class="${keyLatch ? 'on' : ''}">Hold</button></div><div id="kb" class="kb"></div>`}</div></section>`;
  bindViews(m, id);
  bindPresetRow(id);
  bindPanel(m, ms.p, () => { ms.preset = ''; pushCfg(id); save(); });
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
  const VV = [['say', 'Speech'], ['snd', 'Sound']];
  m.innerHTML = `<section class="card mach" style="--c:#7fd"><div class="mhead">${gearSVG('voc', 'hero')}<div class="mtitle"><h2>Vocoder</h2><span class="maker">Electro-Harmonix · 1978 (EH-0300 model)</span></div>${viewTabs('voc', VV)}</div>
    <div class="${vw('voc', 'snd', VV)}" data-v="snd">${presetRow('voc')}
    ${panelHTML('voc', VOC_PANEL, V.p, VOC_LOOK)}<div id="bands" class="bands">${EH_BANDS_UI.map(f => `<div><i></i><span>${f < 1000 ? f : (f / 1000).toFixed(1) + 'k'}</span></div>`).join('')}</div>
    <div class="row"><label>Music input (carrier)<select id="carSel">${Object.keys(MACH).filter(k => MACH[k].kind !== 'drum').map(k => `<option value="${k}" ${V.carrier === k ? 'selected' : ''}>${MACH[k].name}</option>`).join('')}</select></label>
      <button id="direct" class="sw ${V.direct ? 'on' : ''}">Carrier also dry</button><button id="micBtn" class="sw ${micOn ? 'on' : ''}">Live mic</button></div>
    <div class="row"><button id="chipBtn" class="sw ${S.voiceDry && chState('mic').ins === 'chipmunk' ? 'on' : ''}" data-learn="chip">Chipmunk voice</button>
      <select id="chipSpeed">${FXU.chipmunk.ctl[0].opts.map(([v, l]) => `<option value="${v}" ${insVals('mic', 'chipmunk').speed === v ? 'selected' : ''}>Tape ${l}</option>`).join('')}</select>
      <span class="hint">The mic and the speech, sped up like tape, on the Voice channel (beside the vocoder). Use headphones with the live mic.</span></div></div>
    <div class="${vw('voc', 'say', VV)}" data-v="say">
      <div class="row"><label>Voice<select id="ttsVoice">${Object.keys(TTS_VOICES).map(v => `<option ${V.tts.voice === v ? 'selected' : ''}>${v}</option>`).join('')}</select></label>
      <label>Pitch<input id="ttsSemi" type="number" min="-12" max="12" value="${V.tts.semi}"></label><label>Speed<input id="ttsSpeed" type="number" min=".5" max="2" step=".05" value="${V.tts.speed}"></label></div>
      <textarea id="sayText" rows="2">${esc(V.sayText)}</textarea>
      <div class="row"><button id="sayBtn" class="big" data-learn="say">Say it</button><label>with chord<select id="sayRoot">${Array.from({length:25}, (_, i) => 40 + i).map(n => `<option value="${n}" ${V.sayRoot === n ? 'selected' : ''}>${nn(n)}</option>`).join('')}</select></label>
      <select id="sayChord">${[['min', 'minor'], ['maj', 'major'], ['one', 'one note'], ['oct', 'octaves'], ['m7', 'minor 7']].map(([k, l]) => `<option value="${k}" ${V.sayChord === k ? 'selected' : ''}>${l}</option>`).join('')}</select></div>
      <p class="hint">Type /phonemes/ between slashes to spell a word exactly (e.g. /K AX M P Y UW T ER/). Say it plays the chord on the carrier machine while the speech runs.</p>
    <h3>Spoken in a section</h3>${sectionBar()}<div id="sayList">${sec.say.map((l, i) => `<div class="sayrow"><select data-i="${i}" data-f="bar">${Array.from({length:sec.bars}, (_, b) => `<option value="${b}" ${l.bar === b ? 'selected' : ''}>Bar ${b + 1}</option>`).join('')}</select><input data-i="${i}" data-f="text" value="${esc(l.text)}"><button data-i="${i}" data-a="del">✕</button></div>`).join('')}</div>
      <div class="row"><button id="sayAdd">Add a line</button></div><p class="hint">Each line is spoken at the start of its bar. The carrier machine needs notes in that section (the songs hold chords there).</p></div></section>`;
  bindViews(m, 'voc'); bindPresetRow('voc'); bindPanel(m, V.p, () => { V.preset = ''; pushCfg('voc'); save(); }); onSectionBar(m);
  $('#carSel').onchange = e => { V.carrier = e.target.value; song().carrier = V.carrier; applyMix(); save(); };
  $('#direct').onclick = e => { V.direct = !V.direct; e.target.classList.toggle('on', V.direct); applyMix(); save(); };
  $('#micBtn').onclick = toggleMic;
  $('#chipBtn').onclick = async () => { await ensureAudio(); const on = !(S.voiceDry && chState('mic').ins === 'chipmunk'); setVoice(on, {ins:'chipmunk', p:on ? 'Tape ×2 (classic chipmunk)' : null}); if (on) insVals('mic', 'chipmunk').speed = +$('#chipSpeed').value; pushFx('ins', 'mic'); renderMain(); };
  $('#chipSpeed').onchange = e => { insVals('mic', 'chipmunk').speed = +e.target.value; pushFx('ins', 'mic'); save(); };
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
  const ch = CH().map(id => ({id, name:chName(id), c:MACH[id] ? MACH[id].color : id === 'voc' ? '#7fd' : '#e6b23a', s:chState(id)}));
  m.innerHTML = `<section class="card"><h2>Mixer</h2><div class="mixer">${ch.map(x => `<div class="strip" style="--c:${x.c}" data-id="${x.id}"><b>${x.name}</b>
    <input type="range" class="fader" min="0" max="1" step="0.01" value="${x.s.vol}" data-f="vol" data-learn="mix:${x.id}:vol"><label>Pan<input type="range" min="-1" max="1" step="0.05" value="${x.s.pan || 0}" data-f="pan" data-learn="mix:${x.id}:pan"></label>
    <div class="ms"><button data-f="mute" class="${x.s.mute ? 'on' : ''}" data-learn="mix:${x.id}:mute">M</button><button data-f="solo" class="${x.s.solo ? 'on' : ''}" data-learn="mix:${x.id}:solo">S</button></div>${S.voc.carrier === x.id && !S.voc.direct ? '<small>into vocoder</small>' : ''}</div>`).join('')}</div>
    <div class="mixer rets"><b class="retlbl">Returns</b>${RET_U.map(u => `<div class="strip" style="--c:#9ad" data-ret="${u}"><b>${FXU[u].short}</b><input type="range" class="fader" min="0" max="1" step="0.01" value="${retState(u).vol}" data-f="vol" data-learn="rv:${u}:vol">
      <div class="ms"><button data-f="mute" class="${retState(u).mute ? 'on' : ''}">M</button></div></div>`).join('')}</div>
    <label class="ctl master"><span>Master</span><input id="masterVol" type="range" min="0" max="1" step="0.01" value="${S.master}" data-learn="master"></label></section>`;
  markLearned();
  const obj = e => { const st = e.target.closest('.strip'); if (!st) return null; return st.dataset.ret ? retState(st.dataset.ret) : chState(st.dataset.id); };
  m.querySelectorAll('.mixer').forEach(mx => {
    mx.oninput = e => { const o = obj(e), f = e.target.dataset.f; if (!o || !f) return; o[f] = +e.target.value; applyMix(); save(); };
    mx.onclick = e => { const o = obj(e), f = e.target.dataset.f; if (!o || (f !== 'mute' && f !== 'solo')) return; o[f] = !o[f]; e.target.classList.toggle('on', o[f]); applyMix(); save(); };
  });
  $('#masterVol').oninput = e => { S.master = +e.target.value; applyMix(); save(); };
}
/* ---- Mix FX: dub pads, channel insert + sends, the effect rack ---- */
const FV = [['dub', 'Dub'], ['ch', 'Channel'], ['rack', 'Rack']];
function renderFx(m){
  S.fxCh = CH().includes(S.fxCh) ? S.fxCh : 'cym'; S.fxRet = RET_U.includes(S.fxRet) ? S.fxRet : 'pt';
  const id = S.fxCh, st = chState(id), u = st.ins, ru = S.fxRet;
  const presetSel = (sid, names, cur) => `<select id="${sid}"><option value="">Preset…</option>${names.map(n => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('')}</select>`;
  m.innerHTML = `<section class="card mach" style="--c:#e6b23a"><div class="mhead">${gearSVG('fx', 'hero')}<div class="mtitle"><h2>Mix FX</h2><span class="maker">1984 studio rack · sends, inserts, dub</span></div>${viewTabs('fx', FV)}</div>
    <div class="row"><label>Whole mix ${presetSel('mixPre', Object.keys(MIX_PRESETS), S.mixPreset)}</label></div>
    <div class="${vw('fx', 'dub', FV)}" data-v="dub"><p class="hint">Hold <b>Throw</b> to fire a channel into the PrimeTime echo; hold <b>Drop</b> to cut it. Both can be MIDI-learned (notes work best).</p>
      <div class="dubgrid">${CH().map(c => `<div class="dubcell" style="--c:${MACH[c] ? MACH[c].color : c === 'voc' ? '#7fd' : '#e6b23a'}"><b>${chName(c)}</b><button class="dub thr" data-dub="thr" data-ch="${c}" data-learn="thr:${c}">Throw</button><button class="dub drp" data-dub="drp" data-ch="${c}" data-learn="drp:${c}">Drop</button></div>`).join('')}</div></div>
    <div class="${vw('fx', 'ch', FV)}" data-v="ch"><div class="chips chlist">${CH().map(c => `<button data-fxch="${c}" class="${c === id ? 'on' : ''}">${chName(c)}</button>`).join('')}</div>
      <h3>Insert</h3><div class="seg inssel"><div>${['none'].concat(INS_U).map(x => `<button data-ins="${x}" class="${x === u ? 'on' : ''}">${x === 'none' ? 'None' : FXU[x].short}</button>`).join('')}</div></div>
      ${FXU[u] ? `<div class="row"><span class="maker">${FXU[u].name} · ${FXU[u].maker}</span>${presetSel('insPre', Object.keys(FXU[u].presets), insVals(id, u)._preset)}</div><div id="insPanel">${panelHTML('ins_' + id, FXU[u].ctl, insVals(id, u), FX_LOOK[u], 'i:' + id + ':')}</div>` : ''}
      <h3>Sends</h3><div id="sndPanel">${panelHTML('snd_' + id, RET_U.map(r => K(r, FXU[r].short, 'Sends')), st.snd, null, 'snd:' + id + ':')}</div>
      ${id === 'cym' ? `<details class="fold"><summary><h3>Voices on the cymbal bus</h3></summary><div class="row">${['d808', 'dmx'].map(d => MACH[d].voices.map(([v, nm]) => `<button class="sw ${(S.cymOut[d] || []).includes(v) ? 'on' : ''}" data-cymv="${d}:${v}">${MACH[d].name} ${nm}</button>`).join('')).join('')}</div></details>` : ''}</div>
    <div class="${vw('fx', 'rack', FV)}" data-v="rack"><div class="chips">${RET_U.map(r => `<button data-fxret="${r}" class="${r === ru ? 'on' : ''}">${FXU[r].short}</button>`).join('')}</div>
      <div class="row"><span class="maker">${FXU[ru].name} · ${FXU[ru].maker}</span>${presetSel('retPre', Object.keys(FXU[ru].presets), retVals(ru)._preset)}</div>
      <div id="retPanel">${panelHTML('ret_' + ru, FXU[ru].ctl, retVals(ru), FX_LOOK[ru], 'f:' + ru + ':')}</div>
      <div id="retLvl">${panelHTML('rl_' + ru, [K('vol', 'Return level', 'Return')], retState(ru), null, 'rv:' + ru + ':')}</div></div></section>`;
  bindViews(m, 'fx');
  $('#mixPre').onchange = e => { if (e.target.value){ applyMixPreset(e.target.value); renderMain(); } };
  m.querySelectorAll('[data-dub]').forEach(b => {
    const f = b.dataset.dub === 'thr' ? dubThrow : dubDrop, on = v => ensureAudio().then(() => f(b.dataset.ch, v));
    b.onpointerdown = e => { e.preventDefault(); b.setPointerCapture(e.pointerId); on(true); }; b.onpointerup = b.onpointercancel = () => on(false);
  });
  m.querySelectorAll('[data-fxch]').forEach(b => b.onclick = () => { S.fxCh = b.dataset.fxch; save(); renderMain(); });
  m.querySelectorAll('[data-fxret]').forEach(b => b.onclick = () => { S.fxRet = b.dataset.fxret; save(); renderMain(); });
  m.querySelectorAll('[data-ins]').forEach(b => b.onclick = () => { st.ins = b.dataset.ins; if (R) setInsert(R, id); save(); renderMain(); });
  if ($('#insPre')) $('#insPre').onchange = e => { if (!e.target.value) return; fxPreset(st.insP, u, e.target.value); pushFx('ins', id); save(); renderMain(); };
  $('#retPre').onchange = e => { if (!e.target.value) return; fxPreset(retState(ru), ru, e.target.value); pushFx('ret', ru); save(); renderMain(); };
  if ($('#insPanel')) bindPanel($('#insPanel'), insVals(id, u), () => { insVals(id, u)._preset = ''; pushFx('ins', id); save(); });
  bindPanel($('#sndPanel'), st.snd, () => { applyMix(); save(); });
  bindPanel($('#retPanel'), retVals(ru), () => { retVals(ru)._preset = ''; pushFx('ret', ru); save(); });
  bindPanel($('#retLvl'), retState(ru), () => { applyMix(); save(); });
  m.querySelectorAll('[data-cymv]').forEach(b => b.onclick = () => { const [d, v] = b.dataset.cymv.split(':'), a = S.cymOut[d] || (S.cymOut[d] = []), i = a.indexOf(v); if (i < 0) a.push(v); else a.splice(i, 1); b.classList.toggle('on', i < 0); save(); });
  markLearned();
}
function drawPlayhead(){
  requestAnimationFrame(drawPlayhead);
  const p = nowPos(); $$('.st.ph').forEach(e => e.classList.remove('ph'));
  if (!p){ $('#posLbl').textContent = ''; return; }
  const sec = song().sections[p.sec]; $('#posLbl').textContent = sec.name + ' · ' + (Math.floor(p.step / 16) + 1) + '.' + (Math.floor(p.step % 16 / 4) + 1);
  if (p.sec === S.sec && Math.floor(p.step / 16) === S.bar) $$(`#editor .st[data-s="${p.step}"]`).forEach(e => e.classList.add('ph'));
}

/* ---- About: credits, and pictures that stay on this device (never uploaded, never part of the app) ---- */
const ART = (() => {
  let dbp = null; const db = () => dbp || (dbp = new Promise((ok, no) => { const q = indexedDB.open('jamon-art', 1); q.onupgradeneeded = () => q.result.createObjectStore('img'); q.onsuccess = () => ok(q.result); q.onerror = () => no(q.error); }));
  const tx = async (mode, f) => { const d = await db(); return new Promise((ok, no) => { const t = d.transaction('img', mode), r = f(t.objectStore('img')); t.oncomplete = () => ok(r && r.result); t.onerror = () => no(t.error); }); };
  const urls = {};
  return {
    async get(k){ if (urls[k]) return urls[k]; try { const b = await tx('readonly', s => s.get(k)); if (b) return urls[k] = URL.createObjectURL(b); } catch(e){}
      for (const ext of ['jpg', 'webp', 'png']){ const u = 'art/' + k + '.' + ext; try { const r = await fetch(u, {method:'HEAD'}); if (r.ok) return urls[k] = u; } catch(e){} } return null; },
    async put(k, blob){ await tx('readwrite', s => s.put(blob, k)); if (urls[k]) URL.revokeObjectURL(urls[k]); urls[k] = URL.createObjectURL(blob); },
    async del(k){ await tx('readwrite', s => s.delete(k)); if (urls[k]){ if (urls[k].startsWith('blob:')) URL.revokeObjectURL(urls[k]); delete urls[k]; } }
  };
})();
async function applyBackdrop(){ const u = S.artBack !== false ? await ART.get('cover') : null; document.body.style.setProperty('--backdrop', u ? `url("${u}")` : 'none'); document.body.classList.toggle('has-art', !!u); }
function renderAbout(m){
  const slot = (k, title, cap) => `<figure class="artslot" data-art="${k}"><div class="artimg"><span class="hint">No picture yet</span></div><figcaption>${cap}</figcaption>
    <div class="row"><button data-pick="${k}">Choose picture</button><button data-clear="${k}">Remove</button></div></figure>`;
  m.innerHTML = `<section class="card about"><h2 class="comic">Newcleus · Jam On Revenge</h2>
    <p>Newcleus, from Brooklyn: <b>Cozmo D</b> on keys, <b>Chilly B</b> on bass, and <b>Lady E</b>. The bass here is the <b>Chilly B Bass</b>, a Fender Precision simulator; the Pro-One, SH-101, Juno-60 and RS-09 are Cozmo D's keys. <i>Jam On Revenge</i> came out on Sunnyview Records in 1984; the mixing sessions were handled by <b>Jonathan Fearing</b>.</p>
    <p class="hint">This rig recreates the gear and the 1984 studio around it. The songs here are new arrangements in the album's style, not the records.</p></section>
    <section class="card"><h3>Your pictures</h3><p class="hint">Pictures in the app's <code>art/</code> folder (cover.jpg, band.jpg) show here for everyone. You can also pick your own from your phone; those stay on this device only.</p>
    <div class="artgrid">${slot('cover', 'Album cover', 'Album cover')}${slot('band', 'Band', 'Chilly B (left), Lady E (middle), Cozmo D (right)')}</div>
    <label class="row"><input type="checkbox" id="artBack" ${S.artBack !== false ? 'checked' : ''}> Use the cover as the app's backdrop</label></section>`;
  for (const k of ['cover', 'band']) ART.get(k).then(u => { const el = m.querySelector(`[data-art="${k}"] .artimg`); if (u && el) el.innerHTML = `<img src="${u}" alt="${k === 'cover' ? 'Album cover' : 'Chilly B, Lady E and Cozmo D'}">`; });
  m.querySelectorAll('[data-pick]').forEach(b => b.onclick = () => { const f = $('#artFile'); f.dataset.k = b.dataset.pick; f.click(); });
  m.querySelectorAll('[data-clear]').forEach(b => b.onclick = async () => { await ART.del(b.dataset.clear); applyBackdrop(); renderMain(); });
  $('#artBack').onchange = e => { S.artBack = e.target.checked; save(); applyBackdrop(); };
}
$('#artFile').onchange = async e => { const f = e.target.files[0], k = e.target.dataset.k; e.target.value = ''; if (!f || !k) return;
  try { await ART.put(k, f); if (k === 'cover' && S.artBack == null){ S.artBack = true; save(); } applyBackdrop(); renderMain(); toast('Saved on this device only.'); } catch(err){ toast('Could not keep that picture: ' + err.message); } };
applyBackdrop();

/* ================= transport bindings ================= */
$('#playBtn').onclick = play;
$('#bpm').onchange = e => { song().bpm = clamp(+e.target.value || 112, 40, 240); save(); };
$('#swing').onchange = e => { song().swing = clamp(+e.target.value || 0, 0, 60); save(); };
$('#songSel').onchange = e => loadSong(+e.target.value);
$('#loopBtn').onclick = () => { S.loopSec = !S.loopSec; if (playing && S.loopSec){ pos.sec = S.sec; } save(); renderTransport(); toast(S.loopSec ? 'Looping the section you are editing.' : 'Playing the whole song.'); };
$('#recBtn').onclick = toggleRec;
$('#moreBtn').onclick = () => $('#more').classList.toggle('on');
$('#learnBtn').onclick = learnToggle;
$('#recNotesBtn').onclick = () => { recNotes = !recNotes; renderTransport(); toast(recNotes ? 'Notes you play now are written into the playing section.' : 'Note recording off.'); };
$('#panicBtn').onclick = () => { stop(); midiPanic(); for (const k in live.held) live.held[k] = new Set(); keyLatch = false; };
$('#midFile').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) importMidi(f); };
$('#impOk').onclick = importApply; $('#impCancel').onclick = () => $('#impDlg').close();
$('#projFile').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (!f) return; try { const o = JSON.parse(await f.text()); if (o.v !== 1 || !o.songs) throw 0; S = o; const D = defaultState(); for (const k in D) if (S[k] === undefined) S[k] = D[k]; save(); loadSong(S.cur || 0); toast('Project opened.'); } catch(_){ toast('That is not a Jam-On Rig project.'); } };
document.addEventListener('keydown', e => { if (e.code === 'Space' && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)){ e.preventDefault(); play(); } });
if (!S.mixPreset) applyMixPreset(Object.keys(MIX_PRESETS)[0]);
if (!localStorage.getItem(STORE)) loadSong(0); else renderAll();
if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
