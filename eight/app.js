/* Omega 808: the UI. The photo is the panel; controls are hit areas over it.
   window.Eight (for midi.js and others):
     node, ctx            the AudioWorkletNode 'eight-808' and its AudioContext (null until the first touch)
     start()              promise: starts audio
     set(k, v)            set a parameter (updates the panel too)   · params: the Eight808.PARAMS list · state: S below
     hit(v, acc, vel)     play a voice now                           · play(on) · tap() · setTempo(bpm) · post(msg)
     onStep: []           callbacks for the worklet's {t:'step'} messages · onChange: [] callbacks (k, v)
     selectRhythm({basic, fill, variation, ifVar}) · loadBank(bank) · applyKit(params) */
'use strict';
const TRACKS = ['AC','BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH'];
const ALT = {LT:'LC', MT:'MC', HT:'HC', RS:'CL', CP:'MA'};
const VOICES = ['BD','SD','LT','MT','HT','LC','MC','HC','RS','CL','CP','MA','CB','CY','OH','CH','TRIG'];
const COLX = [557,668,779,890,1001,1112,1223,1335,1447,1558,1670,1781];
const STEPX = [530,604,678,752,826,900,973,1047,1121,1195,1269,1343,1418,1492,1566,1640];
const MODES = ['1ST PART','2ND PART','MANUAL PLAY','PLAY','COMPOSE'];
const FILLS = [0,16,12,8,4,2];
const $ = (s, r = document) => r.querySelector(s);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

/* ---------------- parameters ---------------- */
function fallbackParams(){
  const P = [], add = (k, label, group, def = 0.5, min = 0, max = 1, extra) => P.push(Object.assign({k, label, group, def, min, max}, extra || {}));
  add('master','Master','face',0.8); add('AC.level','Accent','face',0.7);
  for (const t of TRACKS.slice(1)) add(t + '.level', t + ' level', 'face', 0.75);
  for (const k of ['BD.tone','BD.decay','SD.tone','SD.snappy','LT.tuning','MT.tuning','HT.tuning','CY.tone','CY.decay','OH.decay']) add(k, k, 'face');
  for (const k of ['LT.sel','MT.sel','HT.sel','RS.sel','CP.sel']) add(k, k, 'face', 0, 0, 1, {step:1});
  for (const k of ['BD.tune','BD.sweep','BD.longDecay','BD.click','BD.drive','SD.tune','SD.decay','SD.noiseColor','TOM.decay','TOM.sweep','CONGA.decay','RS.tune','CL.tune','CP.tail','CP.spread','MA.decay','CB.tune','CB.decay','HH.tune','CH.decay','CY.tune','AC.depth','AC.punch','TRIG.level','TRIG.tone'])
    add('mod.' + k, k.split('.')[1], 'mod', /longDecay|click|drive|punch|level/.test(k) ? 0 : 0.5);
  add('mod.TRIG.src','Source','mod',0,0,3,{step:1, opts:['CB','CP','AC','TRIG']}); add('mod.TRIG.width','Width ms','mod',20,1,40);
  add('mod.age','Age','mod',0); add('mod.hiss','Hiss','mod',0); add('swing','Swing','mod',0);
  for (const v of ['BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH','TRIG']){ add('rev.send.' + v, 'REV ' + v, 'send', 0); add('dly.send.' + v, 'DLY ' + v, 'send', 0); }
  add('rev.program','Program','rev',0,0,15,{step:1}); add('rev.time','Time s','rev',2.2,0.3,10); add('rev.predelay','Pre ms','rev',20,0,200);
  for (const k of ['hf','diffusion','density','lpf','hpf','er','return']) add('rev.' + k, k, 'rev', 0.5);
  add('dly.time','Time ms','dly',375,1,2000); add('dly.sync','Sync','dly',0,0,1,{step:1}); add('dly.division','Division','dly',3,0,7,{step:1, opts:['1/32','1/16T','1/16','1/8T','1/8','1/8D','1/4','1/2']});
  for (const k of ['feedback','hicut','lowcut','modDepth','modRate','duck','return']) add('dly.' + k, k, 'dly', 0.3);
  add('dly.pingpong','Ping-pong','dly',0,0,1,{step:1});
  return P;
}
let PARAMS = (globalThis.Eight808 && Eight808.PARAMS) || fallbackParams();
let PMAP = {};
const PROGRAMS = () => ((globalThis.Rev7 && Rev7.PROGRAMS) || ['Hall 1','Hall 2','Room 1','Room 2','Vocal 1','Vocal 2','Plate 1','Plate 2','Early Ref 1','Early Ref 2','Gate Reverb','Reverse Gate','Echo Room','Large Hall','Small Room','Ambience']).map(p => typeof p === 'string' ? p : p.name);
const norm = (p, v) => (v - p.min) / ((p.max - p.min) || 1);
const denorm = (p, n) => { let v = p.min + Math.max(0, Math.min(1, n)) * (p.max - p.min); if (p.step) v = Math.round(v / p.step) * p.step; return v; };
function fmt(p, v){ if (p.opts) return p.opts[Math.round(v - p.min)] ?? v; if (p.step === 1 && p.max - p.min === 1) return v ? 'ON' : 'OFF'; return Math.abs(v) >= 100 ? Math.round(v) + '' : (+v).toFixed(Math.abs(v) >= 10 ? 1 : 2); }

/* ---------------- state ---------------- */
const STORE = 'omega-808-state', USTORE = 'omega-808-user';
const emptyPat = () => ({scale:0, len:16, parts:1, steps:{}});
function normBank(b){
  b = JSON.parse(JSON.stringify(b || {}));
  const r = x => ({A: Object.assign(emptyPat(), x && x.A), B: Object.assign(emptyPat(), x && (x.B || x.A))});
  b.basic = Array.from({length:12}, (_, i) => r(b.basic && b.basic[i]));
  b.fill = Array.from({length:4}, (_, i) => r(b.fill && b.fill[i]));
  if (b.fillEvery == null) b.fillEvery = 0;
  return b;
}
const S = {params:{}, bank:null, basic:0, fill:0, variation:'A', ifVar:'A', which:'basic', mode:3, track:1, trigWrite:false, playing:false, power:true, pads:false,
  tempoK:0.545, fine:0.5, chain:[], chainPos:0, run:-1, curVar:'A', curPart:0, msg:'', clearHeld:false};
function bpm(){ return 40 * Math.pow(7.5, S.tempoK) * (1 + (S.fine - 0.5) * 0.12); }
function loadState(){
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(STORE) || '{}'); } catch (e) {}
  for (const p of PARAMS) S.params[p.k] = saved.params && saved.params[p.k] != null ? saved.params[p.k] : p.def;
  for (const k of ['basic','fill','variation','ifVar','mode','track','tempoK','fine','chain']) if (saved[k] != null) S[k] = saved[k];
  const firstBank = globalThis.EIGHT_BANKS && EIGHT_BANKS[0] && EIGHT_BANKS[0].bank;
  S.bank = normBank(saved.bank || firstBank);
}
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify({params:S.params, bank:S.bank, basic:S.basic, fill:S.fill, variation:S.variation, ifVar:S.ifVar, mode:S.mode, track:S.track, tempoK:S.tempoK, fine:S.fine, chain:S.chain})); } catch (e) {} }, 400); }

/* ---------------- audio ---------------- */
const Eight = window.Eight = {node:null, ctx:null, params:PARAMS, state:S, onStep:[], onChange:[]};
let starting = null;
// messages sent before the worklet is up (the first START, pad or step tap starts the audio) wait here, not lost
const pending = [];
Eight.post = m => { if (Eight.node) Eight.node.port.postMessage(m); else if (pending.length < 500) pending.push(m); };
Eight.start = () => starting || (starting = (async () => {
  const ctx = Eight.ctx = new AudioContext({latencyHint:'interactive'});
  try {
    for (const f of ['fx.js','seq.js','engine.js','worklet.js']) await ctx.audioWorklet.addModule(f);
    const node = new AudioWorkletNode(ctx, 'eight-808', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{sr:ctx.sampleRate}});
    node.connect(ctx.destination);
    node.port.onmessage = onMsg;
    Eight.node = node;
    syncAll();
    for (const m of pending.splice(0)) node.port.postMessage(m);
    restoreSamples();
  } catch (e) { toast('Audio engine failed: ' + e.message); }
  if (ctx.state !== 'running') ctx.resume();
})());
function syncAll(){
  Eight.post({t:'setMany', p:S.params});
  Eight.post({t:'bank', bank:S.bank});
  Eight.post({t:'tempo', bpm:bpm()});
  sendSelect();
}
function onMsg(e){
  const m = e.data;
  if (m.t === 'ready'){
    if (!(globalThis.Eight808 && Eight808.PARAMS) && m.params){ PARAMS = Eight.params = m.params; for (const p of PARAMS) if (S.params[p.k] == null) S.params[p.k] = p.def; buildBoard(); }
    if (m.programs && !globalThis.Rev7) globalThis.Rev7 = {PROGRAMS:m.programs};
  } else if (m.t === 'step'){
    let st = m.step, part = m.part;
    if (part == null) part = st >= 16 ? 1 : 0;
    S.run = st % 16; S.curPart = part; if (m.var) S.curVar = m.var;
    const now = performance.now(); if (S.lastStep && now - S.lastStep.at < 2000) S.stepMs = now - S.lastStep.at;
    S.lastStep = {at:now, idx:part * 16 + st % 16, which:m.which || S.which, n:m.which === 'fill' ? S.fill : (m.rhythm != null ? m.rhythm : S.basic), var:m.var || editVar()};
    if (m.which === 'basic' && m.rhythm != null) S.playingBasic = m.rhythm;
    chainStep(m);
    renderLeds();
    for (const f of Eight.onStep) try { f(m); } catch (err) {}
  }
}
Eight.set = (k, v, quiet) => {
  S.params[k] = v; Eight.post({t:'set', k, v});
  const c = controls[k]; if (c) c.render();
  for (const f of Eight.onChange) try { f(k, v); } catch (e) {}
  if (!quiet){ const p = PMAP[k]; if (p) flash((p.label || k) + ' ' + fmt(p, v)); }
  save();
};
Eight.hit = (v, acc = 0, vel = 1) => { Eight.start(); Eight.post({t:'hit', v, acc, vel}); };
Eight.play = on => { S.playing = on == null ? !S.playing : !!on; if (S.playing) Eight.start(); Eight.post({t:'play', on:S.playing}); if (!S.playing){ S.run = -1; S.chainPos = 0; } renderAll(); };
Eight.tap = () => Eight.post({t:'tap'});
Eight.setTempo = b => { S.fine = 0.5; S.tempoK = Math.max(0, Math.min(1, Math.log(b / 40) / Math.log(7.5))); Eight.post({t:'tempo', bpm:bpm()}); renderAll(); save(); };
Eight.selectRhythm = o => { Object.assign(S, o || {}); sendSelect(); renderAll(); save(); };
Eight.loadBank = (b, tempo) => { S.bank = normBank(b); Eight.post({t:'bank', bank:S.bank}); const t = tempo || b.tempo; if (t) Eight.setTempo(t); if (b.swing != null && PMAP.swing) Eight.set('swing', b.swing, true); renderAll(); save(); };
Eight.applyKit = (params, keys = k => /^(mod\.|[A-Z]+\.(level|tone|decay|snappy|tuning|sel)$|rev\.send|dly\.send)/.test(k) && k !== 'master') => {
  const p = {};
  for (const q of PARAMS) if (keys(q.k)) p[q.k] = q.def;
  Object.assign(p, params);
  Object.assign(S.params, p); Eight.post({t:'setMany', p}); renderAll(); save();
};
function sendSelect(){ Eight.post({t:'select', basic:S.basic, fill:S.fill, variation:S.variation, ifVar:S.ifVar}); }

/* ---------------- toast and readout ---------------- */
let toastT = 0;
function toast(s){ const t = $('#toast'); t.textContent = s; t.classList.add('on'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('on'), 2600); }
let flashT = 0;
function flash(s){ S.msg = s; clearTimeout(flashT); flashT = setTimeout(() => { S.msg = ''; renderLcd(); }, 1400); renderLcd(); }
function renderLcd(){
  const lcd = $('#lcd'); if (!lcd) return;
  const tr = S.trigWrite ? 'TRIG' : trackVoice(S.track);
  let l1;
  if (S.rec) l1 = (S.playing ? '● REC ' : 'REC: START to record ') + (S.lastStep && S.playing ? (S.lastStep.which === 'fill' ? 'I/F' + (S.lastStep.n + 1) : 'R' + String(S.lastStep.n + 1).padStart(2, '0')) + S.lastStep.var : '');
  else if (S.pads) l1 = 'PADS  tap the step keys';
  else if (S.mode <= 1) l1 = `WRITE ${S.mode ? '2ND' : '1ST'} ${S.which === 'basic' ? 'R' + String(S.basic + 1).padStart(2, '0') : 'I/F' + (S.fill + 1)}${editVar()} ${tr}`;
  else if (S.mode === 4) l1 = 'COMPOSE ' + (S.chain.length ? S.chain.map(n => n + 1).join('-') : '(tap 1-12)');
  else l1 = `${S.mode === 2 ? 'MANUAL' : 'PLAY'} R${String(S.basic + 1).padStart(2, '0')} ${S.variation}  I/F${S.fill + 1}${S.ifVar}`;
  const l2 = S.msg || `${bpm().toFixed(1)} BPM  ${S.playing ? '▶' : '■'}  FILL ${FILLS[fillIdx()] || 'MAN'}`;
  lcd.innerHTML = '';
  el('div', '', lcd).textContent = l1.slice(0, 26);
  el('div', 'l2', lcd).textContent = l2.slice(0, 30);
}
const fillIdx = () => Math.max(0, FILLS.indexOf(S.bank.fillEvery || 0));
const editVar = () => S.which === 'basic' ? (S.variation === 'B' ? 'B' : 'A') : S.ifVar;
function trackVoice(i){ const t = TRACKS[i]; return ALT[t] && S.params[t + '.sel'] ? ALT[t] : t; }

/* ---------------- patterns ---------------- */
function curPat(){ const r = S.which === 'basic' ? S.bank.basic[S.basic] : S.bank.fill[S.fill]; return r[editVar()]; }
function padSteps(p){ const n = p.len * p.parts; for (const k in p.steps){ let s = p.steps[k] || ''; p.steps[k] = (s + '.'.repeat(n)).slice(0, Math.max(n, s.length)); } }
function stepOn(p, tr, i){ const s = p.steps[tr]; return !!s && s[i] === 'x'; }
function toggleStep(i){
  const p = curPat(), tr = S.trigWrite ? 'TRIG' : TRACKS[S.track];
  const idx = (S.mode === 1 ? 16 : 0) + i;
  if (S.mode === 1 && p.parts < 2){ p.parts = 2; }
  if (i >= p.len){ flash('LAST STEP ' + p.len + ' (hold CLEAR + step)'); return; }
  padSteps(p);
  let s = (p.steps[tr] || '').padEnd(p.len * p.parts, '.');
  const on = s[idx] !== 'x';
  s = s.slice(0, idx) + (on ? 'x' : '.') + s.slice(idx + 1);
  p.steps[tr] = s;
  if (S.mode === 1 && p.parts === 2) Eight.post({t:'bank', bank:S.bank});
  else Eight.post({t:'edit', which:S.which, n:S.which === 'basic' ? S.basic : S.fill, var:editVar(), track:tr, step:idx, on});
  if (on && !S.playing && tr !== 'AC') Eight.hit(tr === 'TRIG' ? 'TRIG' : trackVoice(S.track), 0, 1);
  save(); renderLeds();
}
function chainStep(m){
  if (!(S.mode === 2 || S.mode === 4) || !S.chain.length || m.which !== 'basic') return;
  const p = S.bank.basic[m.rhythm != null ? m.rhythm : S.basic][m.var || 'A'];
  if (m.step === p.len - 1 && (m.part || 0) === p.parts - 1){ S.chainPos = (S.chainPos + 1) % S.chain.length; S.basic = S.chain[S.chainPos]; sendSelect(); renderLcd(); }
}

/* ---------------- the faceplate ---------------- */
const controls = {};
let scale = 1;
// knob: the photo's own knob cut out and turned; photo = the angle the photo shows it at
function knob(o){
  const ctl = $('#ctl');
  const k = el('div', 'kn' + (o.r > 40 ? ' big' : ''), ctl);
  Object.assign(k.style, {left:o.x - o.r + 'px', top:o.y - o.r + 'px', width:o.r * 2 + 'px', height:o.r * 2 + 'px', backgroundPosition:`${-(o.x - o.r)}px ${-(o.y - o.r)}px`});
  const ind = el('i', '', k);
  const R = o.r + 16, h = el('div', 'hit', ctl);
  Object.assign(h.style, {left:o.x - R + 'px', top:o.y - R + 'px', width:R * 2 + 'px', height:R * 2 + 'px'});
  const angle = () => o.angles ? o.angles[o.get()] : -150 + 300 * o.get();
  const c = {render(){ const a = angle(); k.style.transform = `rotate(${a - o.photo}deg)`; ind.style.transform = `rotate(${o.photo}deg)`; }};
  let sy = 0, sv = 0, last = 0, id = null;
  h.addEventListener('pointerdown', e => {
    e.preventDefault(); h.setPointerCapture(e.pointerId); id = e.pointerId; sy = e.clientY; sv = o.get();
    const now = performance.now(); if (now - last < 300 && o.reset){ o.reset(); c.render(); } last = now;
    if (o.show) o.show(); knobBubble(e.clientX, e.clientY);
  });
  h.addEventListener('pointermove', e => {
    if (e.pointerId !== id) return;
    // screen pixels, not panel pixels, so the feel is the same at any size: 240 px for the full turn, 30 px per click
    const d = sy - e.clientY;
    if (o.angles){ const n = Math.max(0, Math.min(o.angles.length - 1, sv + Math.round(d / 30))); if (n !== o.get()){ o.set(n); c.render(); } }
    else { o.set(Math.max(0, Math.min(1, sv + d / 240))); c.render(); }
    knobBubble(e.clientX, e.clientY);
  });
  const up = () => { id = null; knobBubble(); };
  h.addEventListener('pointerup', up); h.addEventListener('pointercancel', up);
  h.addEventListener('wheel', e => { e.preventDefault(); if (o.angles) o.set(Math.max(0, Math.min(o.angles.length - 1, o.get() + (e.deltaY < 0 ? 1 : -1)))); else o.set(Math.max(0, Math.min(1, o.get() - e.deltaY / 2000))); c.render(); }, {passive:false});
  c.render();
  return c;
}
// the big value bubble over the finger while a knob turns (it shows the readout's message); no args hides it
function knobBubble(x, y){
  let b = $('#kbubble'); if (!b){ b = el('div', '', document.body); b.id = 'kbubble'; }
  if (x == null){ b.classList.remove('on'); return; }
  b.textContent = S.msg || '';
  b.style.left = Math.max(70, Math.min(innerWidth - 70, x)) + 'px'; b.style.top = (y - 130 < 8 ? y + 70 : y - 130) + 'px';
  b.classList.toggle('on', !!S.msg);
}
function paramKnob(k, x, y, r, photo){
  const p = () => PMAP[k] || {min:0, max:1, def:0.5};
  controls[k] = knob({x, y, r, photo, get:() => norm(p(), S.params[k] ?? p().def), set:n => Eight.set(k, denorm(p(), n)), reset:() => Eight.set(k, p().def),
    show:() => flash((p().label || k) + ' ' + fmt(p(), S.params[k] ?? p().def))});
}
function button(x, y, w, h, fn, cls){
  const b = el('div', 'hit sq' + (cls ? ' ' + cls : ''), $('#ctl'));
  Object.assign(b.style, {left:x + 'px', top:y + 'px', width:w + 'px', height:h + 'px'});
  b.addEventListener('pointerdown', e => { e.preventDefault(); b.setPointerCapture(e.pointerId); fn(e, true, b); });
  b.addEventListener('pointerup', e => fn(e, false, b));
  b.addEventListener('pointercancel', e => fn(e, false, b));
  return b;
}
const at = (cls, x, y, parent = $('#ctl')) => { const e = el('div', cls, parent); e.style.left = x + 'px'; e.style.top = y + 'px'; return e; };
function lever(slot, lw, lh, positions, get, horizontal){
  const s = el('div', 'slot', $('#ctl')); Object.assign(s.style, {left:slot[0] + 'px', top:slot[1] + 'px', width:slot[2] + 'px', height:slot[3] + 'px'});
  const l = el('div', 'lever', $('#ctl')); l.style.width = lw + 'px'; l.style.height = lh + 'px';
  return {render(){ const p = positions[get()]; if (horizontal){ l.style.left = p - lw / 2 + 'px'; l.style.top = slot[1] + (slot[3] - lh) / 2 + 'px'; } else { l.style.top = p - lh / 2 + 'px'; l.style.left = slot[0] + (slot[2] - lw) / 2 + 'px'; } }};
}
const leds = [], vLeds = [], pLeds = [], padLbls = [];
let trigPad = null;
let presses = [];
function buildFace(){
  const ctl = $('#ctl'); ctl.innerHTML = '';
  // the twelve LEVEL knobs, then TONE / TUNING and DECAY / SNAPPY
  TRACKS.forEach((t, i) => paramKnob(t === 'AC' ? 'AC.level' : t + '.level', COLX[i], 268, 27, 110));
  [['BD.tone',668,388,70],['SD.tone',779,388,110],['LT.tuning',890,388,95],['MT.tuning',1001,388,110],['HT.tuning',1112,388,135],['CY.tone',1558,386,130],
   ['BD.decay',668,508,110],['SD.snappy',779,508,75],['CY.decay',1558,507,140],['OH.decay',1670,507,150]].forEach(a => paramKnob(a[0], a[1], a[2], 24, a[3]));
  paramKnob('master', 1775, 690, 44, 150);
  // tom / conga, rim / claves, clap / maracas switches: tap to flip; the chosen label glows
  [['LT',893],['MT',1002],['HT',1112],['RS',1223],['CP',1334]].forEach(([t, x]) => {
    const k = t + '.sel';
    const up = at('sel', x - 52, 485), dn = at('sel', x - 52, 581);
    for (const s of [up, dn]){ s.style.width = '104px'; s.style.height = '32px'; }
    const sl = lever([x - 11, 524, 22, 44], 18, 18, [532, 556], () => S.params[k] ? 0 : 1);
    controls[k] = {render(){ const on = !!S.params[k]; up.classList.toggle('on', on); dn.classList.toggle('on', !on); sl.render(); }};
    button(x - 55, 482, 110, 134, (e, down) => { if (down) Eight.set(k, S.params[k] ? 0 : 1); });
    controls[k].render();
  });
  // left section: pattern write mode, instrument select, auto fill in, tempo, fine
  controls._mode = knob({x:228, y:320, r:44, photo:-15, angles:[-70,-30,15,60,100], get:() => S.mode, show:() => flash(MODES[S.mode]), set:n => { S.mode = n; S.pads = false; flash(MODES[n]); renderAll(); save(); }});
  controls._inst = knob({x:410, y:318, r:40, photo:-70, angles:TRACKS.map((_, i) => -165 + 30 * i), get:() => S.track, show:() => flash('TRACK ' + TRACKS[S.track]), set:n => { S.track = n; S.trigWrite = false; flash('TRACK ' + TRACKS[n]); renderAll(); save(); }});
  controls._fill = knob({x:412, y:478, r:40, photo:45, angles:[-100,-45,-15,15,45,75], get:fillIdx, show:() => flash('AUTO FILL ' + (FILLS[fillIdx()] || 'MANUAL')), set:n => { S.bank.fillEvery = FILLS[n]; Eight.post({t:'bank', bank:S.bank}); flash('AUTO FILL ' + (FILLS[n] || 'MANUAL')); save(); }});
  controls._tempo = knob({x:250, y:635, r:78, photo:-80, get:() => S.tempoK, show:() => flash(bpm().toFixed(1) + ' BPM'), set:n => { S.tempoK = n; Eight.post({t:'tempo', bpm:bpm()}); flash(bpm().toFixed(1) + ' BPM'); save(); }, reset:() => Eight.setTempo(120)});
  controls._fine = knob({x:420, y:688, r:34, photo:-60, get:() => S.fine, show:() => flash(bpm().toFixed(1) + ' BPM'), set:n => { S.fine = n; Eight.post({t:'tempo', bpm:bpm()}); flash(bpm().toFixed(1) + ' BPM'); save(); }, reset:() => { S.fine = 0.5; Eight.post({t:'tempo', bpm:bpm()}); }});
  // CLEAR: hold to clear the track (write modes); hold and press a step to set the last step
  let clearT = 0;
  button(212, 408, 42, 42, (e, down) => {
    S.clearHeld = down;
    if (down){ clearT = setTimeout(() => { if (S.mode <= 1 && S.clearHeld){ const p = curPat(); const tr = S.trigWrite ? 'TRIG' : TRACKS[S.track]; delete p.steps[tr]; Eight.post({t:'bank', bank:S.bank}); flash('TRACK CLEAR ' + tr); save(); renderLeds(); } }, 700); }
    else clearTimeout(clearT);
  });
  // POWER
  const pw = button(190, 120, 72, 52, (e, down) => { if (!down) return; S.power = !S.power; $('#panel').classList.toggle('off', !S.power);
    if (!S.power){ Eight.play(false); if (Eight.ctx) Eight.ctx.suspend(); } else { Eight.start().then(() => Eight.ctx && Eight.ctx.resume()); } }, 'power');
  pw.title = 'Power';
  // switches: BASIC-VARIATION A / AB / B, PRE-SCALE 1-4, I/F-VARIATION A / B
  const bv = lever([235, 816, 78, 24], 30, 18, [248, 273, 299], () => ['A','AB','B'].indexOf(S.variation), true);
  controls._bv = bv;
  button(225, 800, 100, 60, (e, down) => { if (!down) return; S.variation = S.variation === 'A' ? 'AB' : S.variation === 'AB' ? 'B' : 'A'; sendSelect(); flash('VARIATION ' + S.variation); renderAll(); save(); });
  const ps = lever([422, 808, 24, 76], 20, 18, [818, 837, 856, 875], () => curPat().scale || 0);
  controls._ps = ps;
  button(405, 800, 60, 92, (e, down) => { if (!down) return; const p = curPat(); p.scale = ((p.scale || 0) + 1) % 4; Eight.post({t:'bank', bank:S.bank}); flash('PRE-SCALE ' + (p.scale + 1)); renderAll(); save(); });
  const iv = lever([1738, 816, 64, 24], 26, 18, [1752, 1788], () => S.ifVar === 'B' ? 1 : 0, true);
  controls._iv = iv;
  button(1725, 800, 92, 60, (e, down) => { if (!down) return; S.ifVar = S.ifVar === 'A' ? 'B' : 'A'; sendSelect(); flash('I/F VARIATION ' + S.ifVar); renderAll(); save(); });
  vLeds[0] = at('ledsm', 243, 887); vLeds[1] = at('ledsm', 305, 887);
  pLeds[0] = at('ledsm', 436, 953); pLeds[1] = at('ledsm', 436, 1007);
  // START / STOP and TAP
  button(195, 940, 156, 82, (e, down) => { if (down) Eight.play(); });
  button(1725, 942, 80, 86, (e, down) => { if (down) Eight.tap(); });
  // the 16 step keys
  presses = [];
  STEPX.forEach((x, i) => {
    leds[i] = at('led', x, 948);
    const pr = el('div', 'press', ctl); Object.assign(pr.style, {left:x - 30 + 'px', top:932 + 'px', width:'60px', height:'98px'}); presses[i] = pr;
    button(x - 34, 928, 68, 106, (e, down, b) => stepKey(i, down, e, b));
    padLbls[i] = at('padlbl', x, 992);
  });
  // REC: a big trigger-out pad in the blank space beside the buttons
  trigPad = el('button', 'trigpad', ctl, 'TRIG OUT<small>tap to record the click</small>');
  trigPad.addEventListener('pointerdown', e => { e.preventDefault(); e.stopPropagation(); Eight.start(); S.lastTrig = performance.now(); recHit('TRIG', 1); renderLeds(); setTimeout(renderLeds, 140); });
  // readout and buttons in the blank space
  el('div', '', ctl).id = 'lcd';
  const fb = el('div', 'fbtns', ctl);
  for (const [lab, fn, id] of [['PRESETS', openPresets], ['PADS', () => { S.pads = !S.pads; renderAll(); }, 'bpads'], ['MIDI', () => window.EightMidi && EightMidi.openSettings ? EightMidi.openSettings() : toast('MIDI is not available here')],
    ['EXPORT', () => $('#export').showModal()], ['CIRCUIT ▲', () => fold(true)], ['● REC', () => setRec(!S.rec), 'brec'], ['🔍 ZOOM', () => {}, 'bzoom']]){
    const b = el('button', 'fb', fb, lab); if (id) b.id = id;
    b.addEventListener('pointerdown', e => e.stopPropagation()); b.addEventListener('click', fn);
  }
  // ZOOM (loupe.js): magnify any part of the panel (face or board) and use it there
  if (window.OmegaLoupe) OmegaLoupe.attach({root:$('#panel'), button:$('#bzoom')});
  const hg = el('button', 'hinge', ctl, 'OPEN ▲'); hg.addEventListener('click', () => fold(true));
  swipe(hg, -1, () => fold(true));
  tileFace();
}
// portrait: the face is cut into four tiles of the same photo, stacked down the screen (voices left / right, then the
// sequencer left / right). Every control moves into the tile under its centre, so it keeps its photo coordinates;
// fit() places the tiles. In landscape the tiles sit untouched on top of each other and the panel looks as before.
const TILES = [[88, 58, 969, 712], [1057, 58, 855, 712], [88, 770, 996, 325], [1084, 770, 828, 325]];
function tileFace(){
  const lid = $('#lid'), ctl = $('#ctl');
  let tiles = [...lid.querySelectorAll('.tile')];
  if (!tiles.length) tiles = TILES.map(() => {
    const t = el('div', 'tile', lid), im = el('img', 'tph', t);
    im.src = 'faceplate.jpg'; im.alt = ''; im.draggable = false; el('div', 'ctl', t); return t;
  });
  tiles.forEach(t => { t.querySelector('.ctl').innerHTML = ''; });
  for (const c of [...ctl.children]){
    const cx = c.offsetLeft + c.offsetWidth / 2, cy = c.offsetTop + c.offsetHeight / 2;
    const i = cy < 770 ? (cx < 1057 ? 0 : 1) : (cx < 1084 ? 2 : 3);
    tiles[i].querySelector('.ctl').appendChild(c);
  }
}
function stepKey(i, down, e, b){
  presses[i].classList.toggle('on', down);
  if (!down) return;
  Eight.start();
  if (S.pads || S.rec){
    const r = b.getBoundingClientRect(), vel = Math.max(0.3, Math.min(1, 1 - (e.clientY - r.top) / r.height + 0.3));
    const v = padVoice(i);
    if (v){ if (S.rec) recHit(v, vel); else Eight.hit(v, S.padAcc ? 1 : 0, vel); } else { S.padAcc = !S.padAcc; flash('ACCENT ' + (S.padAcc ? 'ON' : 'OFF')); renderLeds(); }
    return;
  }
  if (S.clearHeld && S.mode <= 1){ const p = curPat(); p.len = i + 1; padSteps(p); Eight.post({t:'bank', bank:S.bank}); flash('LAST STEP ' + (i + 1)); save(); renderLeds(); return; }
  if (S.mode <= 1) return toggleStep(i);
  if (S.mode === 4){ if (i < 12){ S.chain.push(i); flash('ADD R' + (i + 1)); } else if (i === 12){ S.chain.pop(); flash('UNDO'); } else if (i === 13){ S.chain = []; flash('CHAIN CLEARED'); } save(); renderAll(); return; }
  if (i < 12){ S.basic = i; S.which = 'basic'; } else { S.fill = i - 12; S.which = 'fill'; }
  sendSelect(); renderAll(); save();
}
// pads (PADS and REC): key 1 = ACCENT, 2-12 the instrument tracks, 13-16 maracas, claves, hi conga, trigger out
const padVoice = i => i === 0 ? null : i < 12 ? trackVoice(i) : ['MA','CL','HC','TRIG'][i - 12];
const trackOf = v => { if (v === 'TRIG') return 'TRIG'; for (let i = 1; i < 12; i++) if (trackVoice(i) === v || TRACKS[i] === v || ALT[TRACKS[i]] === v) return TRACKS[i]; return null; };
/* real-time recording: while the rhythm plays, a hit lands on the nearest step of the playing pattern (with the
   accent when ACCENT is on). Early hits wait for that step (the sequencer plays them on time, so nothing flams);
   late ones sound now. The trigger out has its own track: recording into it points the trigger out at it. */
function recHit(v, vel){
  const acc = S.padAcc ? 1 : 0;
  if (!S.playing || !S.lastStep){ Eight.hit(v, acc, vel); return; }
  const L = S.lastStep, r = L.which === 'fill' ? S.bank.fill[L.n] : S.bank.basic[L.n], p = r[L.var] || r.A;
  const dur = S.stepMs || 15000 / bpm(), early = performance.now() - L.at > dur / 2;
  let idx = L.idx;
  if (early){ const st = idx % 16, part = idx >= 16 ? 1 : 0; idx = st + 1 < p.len ? idx + 1 : part + 1 < p.parts ? 16 : 0; }
  else Eight.hit(v, acc, vel);
  const tr = trackOf(v); if (!tr) return;
  if (tr !== 'TRIG' && ALT[tr] && v !== tr && !S.params[tr + '.sel']) Eight.set(tr + '.sel', 1, true);
  padSteps(p);
  const put = (t) => { let s = (p.steps[t] || '').padEnd(p.len * p.parts, '.'); if (s[idx] === 'x') return; p.steps[t] = s.slice(0, idx) + 'x' + s.slice(idx + 1);
    Eight.post({t:'edit', which:L.which, n:L.n, var:r[L.var] ? L.var : 'A', track:t, step:idx, on:true}); };
  put(tr); if (acc) put('AC');
  if (tr === 'TRIG' && Math.round(S.params['mod.TRIG.src'] || 0) !== 3){
    Eight.set('mod.TRIG.src', 3, true); if (!(S.params['mod.TRIG.level'] > 0.05)) Eight.set('mod.TRIG.level', 0.6, true);
    flash('TRIG OUT: its own track');
  } else flash('REC ' + v + ' step ' + ((idx % 16) + 1) + (idx >= 16 ? ' (2nd)' : ''));
  save();
}
function setRec(on){
  S.rec = on; if (on){ S.pads = false; Eight.start(); }
  document.body.classList.toggle('rec', on); renderAll();
}
function renderLeds(){
  if (!leds.length) return;
  const lit = new Array(16).fill(false);
  if (S.pads || S.rec){ lit[0] = !!S.padAcc; }
  else if (S.mode <= 1){ const p = curPat(), tr = S.trigWrite ? 'TRIG' : TRACKS[S.track], off = S.mode === 1 ? 16 : 0; for (let i = 0; i < 16; i++) lit[i] = stepOn(p, tr, off + i); }
  else if (S.mode === 4){ for (const n of S.chain) lit[n] = true; }
  else { lit[S.basic] = true; lit[12 + S.fill] = true; }
  for (let i = 0; i < 16; i++){
    const run = S.playing && S.run === i;
    leds[i].classList.toggle('on', run ? !lit[i] || S.mode > 1 : lit[i]); leds[i].classList.toggle('run', run);
    // the key itself glows when it is lit, so it is easy to see which keys are on
    presses[i].classList.toggle('lit', lit[i]); presses[i].classList.toggle('now', run);
    if (padLbls[i]){ const v = S.pads || S.rec ? (i === 0 ? 'ACC' : padVoice(i)) : ''; if (padLbls[i].textContent !== v) padLbls[i].textContent = v; }
  }
  if (trigPad) trigPad.classList.toggle('hot', S.lastTrig && performance.now() - S.lastTrig < 120);
  const v = S.playing ? S.curVar : S.variation;
  vLeds[0].classList.toggle('on', v === 'A' || v === 'AB'); vLeds[1].classList.toggle('on', v === 'B' || v === 'AB');
  const part = S.mode <= 1 && !S.playing ? S.mode : S.curPart;
  pLeds[0].classList.toggle('on', part === 0); pLeds[1].classList.toggle('on', part === 1);
}
function renderAll(){
  for (const k in controls) controls[k].render();
  const pb = $('#bpads'); if (pb) pb.classList.toggle('on', S.pads);
  const rb = $('#brec'); if (rb) rb.classList.toggle('on', !!S.rec);
  renderLeds(); renderLcd(); renderBoard();
}

/* ---------------- fold open: the board under the lid ---------------- */
function fold(open){ $('#panel').classList.toggle('open', open); $('#board').setAttribute('aria-hidden', !open); fit(); if (open) renderBoard(); }
function swipe(elm, dir, fn){
  let y0 = null;
  elm.addEventListener('pointerdown', e => { y0 = e.clientY; });
  elm.addEventListener('pointerup', e => { if (y0 != null && (e.clientY - y0) * dir > 40) fn(); y0 = null; });
}
// The board is laid out in real screen pixels (not the panel's 2000-wide units) so its text stays readable on a
// phone: #board holds .bview, counter-scaled by 1/scale and sized to the panel's on-screen size, and it scrolls.
const SECTS = [
  {id:'BD', name:'BASS DRUM', v:['BD']}, {id:'SD', name:'SNARE DRUM', v:['SD']},
  {id:'TOMS', name:'TOMS / CONGAS', v:['LT','MT','HT','LC','MC','HC'], wide:true}, {id:'RSCL', name:'RIM / CLAVES', v:['RS','CL']},
  {id:'CPMA', name:'CLAP / MARACAS', v:['CP','MA']}, {id:'CB', name:'COWBELL', v:['CB']},
  {id:'CY', name:'CYMBAL', v:['CY']}, {id:'HH', name:'HI-HATS', v:['OH','CH']},
  {id:'ACC', name:'ACCENT', v:[]}, {id:'TRIG', name:'TRIGGER OUT', v:['TRIG']},
  {id:'MACH', name:'MACHINE', v:[]},
];
function sectionOf(k){
  let m = /^mod\.([A-Z]+)\./.exec(k);
  if (m) return {BD:'BD', SD:'SD', TOM:'TOMS', CONGA:'TOMS', RS:'RSCL', CL:'RSCL', CP:'CPMA', MA:'CPMA', CB:'CB', CY:'CY', HH:'HH', CH:'HH', OH:'HH', AC:'ACC', TRIG:'TRIG'}[m[1]] || 'MACH';
  m = /^(rev|dly)\.send\.([A-Z]+)$/.exec(k);
  if (m) return {BD:'BD', SD:'SD', LT:'TOMS', MT:'TOMS', HT:'TOMS', RS:'RSCL', CP:'CPMA', CB:'CB', CY:'CY', OH:'HH', CH:'HH', TRIG:'TRIG'}[m[2]] || 'MACH';
  if (/^rev\./.test(k)) return 'REV';
  if (/^dly\./.test(k)) return 'DLY';
  if (/^(master|tempo|fine)$|^[A-Z]+\.(level|tone|decay|snappy|tuning|sel)$/.test(k)) return null;
  return 'MACH';
}
const boardCtl = {};
// value with its unit, for the readouts and the drag bubble
function fmtU(p, v){
  if (p.opts) return String(p.opts[Math.round(v - p.min)] ?? v);
  const u = /\((s|ms|Hz|kHz|dB|%)\)\s*$/.exec(p.label || ''), t = fmt(p, v);
  return u ? t + ' ' + u[1] : t;
}
const plainLabel = p => (p.label || p.k).replace(/\s*\([^)]*\)\s*$/, '');
let bubble = null;
function showBubble(p, on){
  if (!bubble) return;
  bubble.classList.toggle('on', on);
  if (on) bubble.innerHTML = `<small>${plainLabel(p)}</small>${fmtU(p, S.params[p.k] ?? p.def)}`;
}
// one control per parameter: on/off switch, segmented choice (≤4 options), stepper (more options) or fader
function trim(parent, p, cls){
  const v = () => S.params[p.k] ?? p.def;
  const n = p.step === 1 ? Math.round(p.max - p.min) + 1 : 0;
  if (n === 2){
    const d = el('button', 'dip', parent, `<span class="sw"></span><span class="lab"></span><span class="val"></span>`);
    d.querySelector('.lab').textContent = plainLabel(p);
    d.addEventListener('click', () => Eight.set(p.k, v() >= p.max ? p.min : p.max));
    boardCtl[p.k] = () => { d.classList.toggle('on', v() >= p.max); d.querySelector('.val').textContent = p.opts ? fmtU(p, v()) : (v() >= p.max ? 'ON' : 'OFF'); };
    return;
  }
  if (n > 2 && n <= 4 && p.opts){
    const d = el('div', 'seg', parent, `<span class="lab"></span><span class="opts"></span>`);
    d.querySelector('.lab').textContent = plainLabel(p);
    const bs = p.opts.map((o, i) => { const b = el('button', '', d.querySelector('.opts'), o); b.addEventListener('click', () => Eight.set(p.k, p.min + i)); return b; });
    boardCtl[p.k] = () => bs.forEach((b, i) => b.classList.toggle('on', Math.round(v() - p.min) === i));
    return;
  }
  if (n > 4){
    const d = el('div', 'stepper', parent, `<span class="lab"></span><span class="row"><button>◀</button><span class="val"></span><button>▶</button></span>`);
    d.querySelector('.lab').textContent = plainLabel(p);
    const [l, r] = d.querySelectorAll('button');
    const go = dd => Eight.set(p.k, Math.max(p.min, Math.min(p.max, Math.round(v()) + dd)));
    l.addEventListener('click', () => go(-1)); r.addEventListener('click', () => go(1));
    boardCtl[p.k] = () => { d.querySelector('.val').textContent = fmtU(p, v()); };
    return;
  }
  const t = el('div', 'tp' + (cls ? ' ' + cls : ''), parent, `<div class="fd"><i></i><b></b></div><div class="lab"></div><div class="val"></div>`);
  t.querySelector('.lab').textContent = plainLabel(p); t.title = p.k;
  const b = t.querySelector('b'), val = t.querySelector('.val');
  boardCtl[p.k] = () => { t.style.setProperty('--n', norm(p, v())); val.textContent = fmtU(p, v()); };
  let sy = 0, sv = 0, id = null, last = 0;
  t.addEventListener('pointerdown', e => {
    e.preventDefault(); t.setPointerCapture(e.pointerId); id = e.pointerId; sy = e.clientY; sv = norm(p, v());
    const now = performance.now(); if (now - last < 300){ Eight.set(p.k, p.def, true); sv = norm(p, p.def); } last = now;
    t.classList.add('drag'); showBubble(p, true);
  });
  // faders: the cap follows the finger, 106 px of travel covers the whole range
  t.addEventListener('pointermove', e => { if (e.pointerId !== id) return; Eight.set(p.k, denorm(p, sv + (sy - e.clientY) / 106), true); showBubble(p, true); });
  const up = () => { id = null; t.classList.remove('drag'); showBubble(p, false); };
  t.addEventListener('pointerup', up); t.addEventListener('pointercancel', up);
}
let boardHooked = false;
function boardScale(){ const b = $('#board'); if (b) b.style.setProperty('--s', scale); fitPage(); }
// the board shows one page at a time, picked with the buttons down the left and right: no scrolling.
// fitPage sizes the open page to the space: it tries narrower scales until the page's height fits too
const PAGES = [['BD','BD'],['SD','SD'],['TOMS','TOMS'],['RSCL','RIM'],['CPMA','CLAP'],['CB','BELL'],['CY','CYM'],
  ['HH','HATS'],['ACC','ACCENT'],['TRIG','TRIG OUT'],['MACH','MACHINE'],['REV','REV 7'],['DLY','2290']];
function fitPage(){
  const G = $('#board .bgrid'), pg = G && G.querySelector('.pg.on'); if (!pg) return;
  const W = G.clientWidth, H = G.clientHeight; if (!W || !H) return;
  const fit = () => {
    let sc = 1.5;
    for (let i = 0; i < 24; i++){
      pg.style.width = (W / sc) + 'px'; pg.style.transform = `scale(${sc})`;
      if (Math.max(pg.scrollHeight, pg.offsetHeight) * sc + 22 <= H && Math.max(pg.scrollWidth, pg.offsetWidth) * sc <= W + 1) break;
      sc *= 0.92;
    }
    return sc;
  };
  // try the page wrapped and in one row, keep whichever gives bigger controls
  pg.classList.remove('row'); const a = fit();
  pg.classList.add('row'); const b = fit();
  if (a > b){ pg.classList.remove('row'); fit(); }
}
function showPage(id){
  S.boardPage = id;
  document.querySelectorAll('#board .pg').forEach(e => e.classList.toggle('on', e.dataset.pg === id));
  document.querySelectorAll('#board .bnav button').forEach(e => e.classList.toggle('on', e.dataset.pg === id));
  fitPage();
}
function buildBoard(){
  PMAP = {}; for (const p of PARAMS) PMAP[p.k] = p;
  const B = $('#board'); B.innerHTML = '';
  for (const k in boardCtl) delete boardCtl[k];
  if (!boardHooked){
    boardHooked = true;
    // keep the board in step with every change (drags, presets, MIDI). A REV7 program loads its settings, as on the unit
    Eight.onChange.push(k => {
      if (k === 'rev.program' && globalThis.Rev7 && Rev7.PROGRAMS && typeof Rev7.PROGRAMS[0] === 'object'){
        const pr = Rev7.PROGRAMS[Math.round(S.params[k])];
        if (pr) for (const kk of ['time','predelay','hf','diffusion','density','lpf','hpf','er']) if (pr[kk] != null) S.params['rev.' + kk] = pr[kk];
        renderBoard(); return;
      }
      if (boardCtl[k]) boardCtl[k]();
      if (/^dly\./.test(k) && boardCtl._disp) boardCtl._disp();
    });
    addEventListener('resize', boardScale);
  }
  boardScale();
  const V = el('div', 'bview', B);
  // copper traces behind everything
  let svg = '<svg class="traces" viewBox="0 0 2000 1190" preserveAspectRatio="none"><g fill="none" stroke="#3f9a5c" stroke-width="5" stroke-linecap="round">';
  for (let i = 0; i < 26; i++){ const y = 80 + i * 42, x = 30 + (i * 137) % 1800; svg += `<path d="M${x} ${y} h${180 + (i * 53) % 260} l40 40 h${120 + (i * 31) % 200}"/>`; }
  svg += '</g></svg>';
  V.insertAdjacentHTML('beforeend', svg);
  const head = el('div', 'bhead', V, `<span>TR-808 MAIN BOARD</span><small>slide a fader · double tap for stock</small><span class="sp"></span>`);
  const rnd = el('button', 'bbtn', head, 'STOCK ALL MODS');
  rnd.addEventListener('click', () => { const p = {}; for (const q of PARAMS) if (/^mod\./.test(q.k)) p[q.k] = q.def; Object.assign(S.params, p); Eight.post({t:'setMany', p}); renderAll(); save(); flash('STOCK CIRCUIT'); });
  const cb0 = el('button', 'bbtn closetop', head, 'CLOSE ▲'); cb0.addEventListener('click', () => fold(false));
  const WR = el('div', 'bwrap', V), NL = el('div', 'bnav', WR), G = el('div', 'bgrid', WR), NR = el('div', 'bnav', WR);
  PAGES.forEach(([id, name], i) => { const b = el('button', '', i < 7 ? NL : NR, name); b.dataset.pg = id; b.addEventListener('click', () => showPage(id)); });
  const bins = {};
  for (const s of SECTS){
    const d = el('div', 'sect pg' + (s.wide ? ' wide' : ''), G); d.dataset.pg = s.id;
    const h = el('h3', '', d, s.name);
    if (s.id === 'TRIG'){
      const q = el('button', 'tipq', h, '?');
      const ex = el('div', 'egypt', d);
      ex.textContent = "The 808's rear TRIGGER OUT jacks put out 20 ms pulses following the CB, CP or AC track. Egyptian Lover recorded that click as a drum sound in his records. Turn up Click Into Mix to hear it, and pick its source below.";
      q.addEventListener('click', () => { ex.classList.toggle('on'); fitPage(); });
      const w = el('button', 'bbtn', d, 'WRITE TRIG TRACK'); w.style.margin = '0 0 10px';
      w.addEventListener('click', () => { S.trigWrite = !S.trigWrite; if (S.trigWrite && S.mode > 1) S.mode = 0; fold(false); flash(S.trigWrite ? 'WRITE TRIG TRACK' : 'TRACK ' + TRACKS[S.track]); renderAll(); });
    }
    bins[s.id] = {pots:el('div', 'pots', d), sends:el('div', 'pots sends', d), slots:s.v.length ? el('div', 'slots', d) : null, v:s.v};
  }
  for (const p of PARAMS){
    const s = sectionOf(p.k); if (!s || s === 'REV' || s === 'DLY') continue;
    const bin = bins[s] || bins.MACH, send = /send/.test(p.k);
    trim(send ? bin.sends : bin.pots, p, send ? 'send' : 'mod');
  }
  for (const id in bins){ if (!bins[id].sends.children.length) bins[id].sends.remove(); else bins[id].sends.insertAdjacentHTML('afterbegin', '<div class="subh">FX SENDS</div>'); }
  for (const id in bins) if (bins[id].slots){ bins[id].slots.insertAdjacentHTML('afterbegin', '<div class="subh">SAMPLES</div>'); for (const v of bins[id].v) sampleSlot(bins[id].slots, v); }
  // the FX rack under the sections
  const R = G;
  // REV7
  const rv = el('div', 'unit rev7 pg', R); rv.dataset.pg = 'REV';
  el('div', 'uname', rv, 'REV 7 <small>DIGITAL REVERBERATOR</small>');
  const lcd = el('div', 'lcd', rv, '<button>◀</button><span class="num"></span><span class="pn"></span><button>▶</button>');
  if (PMAP['rev.program']){
    const [l, r] = lcd.querySelectorAll('button'), n = PROGRAMS().length;
    const step = d => Eight.set('rev.program', (Math.round(S.params['rev.program'] || 0) + d + n) % n);
    l.addEventListener('click', () => step(-1)); r.addEventListener('click', () => step(1));
    boardCtl['rev.program'] = () => { const i = Math.round(S.params['rev.program'] || 0); lcd.querySelector('.num').textContent = String(i + 1).padStart(2, '0'); lcd.querySelector('.pn').textContent = (PROGRAMS()[i] || '').toUpperCase(); };
  }
  const rp = el('div', 'pots', rv);
  for (const p of PARAMS) if (sectionOf(p.k) === 'REV' && p.k !== 'rev.program') trim(rp, p);
  // TC 2290
  const dl = el('div', 'unit tc2290 pg', R); dl.dataset.pg = 'DLY';
  el('div', 'uname', dl, '2290 <small>DYNAMIC DIGITAL DELAY</small>');
  const disp = el('div', 'disp', dl, '<span class="unitlab"></span><span class="seg7"></span>');
  boardCtl._disp = () => { const sync = S.params['dly.sync'] >= 0.5, dv = PMAP['dly.division'];
    disp.querySelector('.seg7').textContent = sync && dv ? fmtU(dv, S.params['dly.division'] ?? dv.def) : String(Math.round(S.params['dly.time'] ?? 0));
    disp.querySelector('.unitlab').textContent = sync ? 'TEMPO SYNC' : 'DELAY ms'; };
  const dp = el('div', 'pots', dl);
  for (const p of PARAMS) if (sectionOf(p.k) === 'DLY') trim(dp, p);
  bubble = el('div', 'vbubble', B);
  showPage(PAGES.some(p => p[0] === S.boardPage) ? S.boardPage : 'BD');
  renderBoard();
}
function renderBoard(){ boardScale(); for (const k in boardCtl) boardCtl[k](); for (const v in slotEls) slotEls[v](); }

/* ---------------- samples ---------------- */
const slotEls = {}, sampleNames = {};
function sampleSlot(parent, v){
  const s = el('div', 'slotx', parent, `<b>${v}</b><span class="nm">circuit</span><button>LOAD</button><button class="clr">✕</button>`);
  const [ld, cl] = s.querySelectorAll('button');
  ld.addEventListener('click', () => { const f = el('input'); f.type = 'file'; f.accept = 'audio/*,video/*'; f.style.display = 'none'; document.body.appendChild(f); /* some Android WebViews only open the chooser for an input in the page */ f.onchange = () => { if (f.files[0]) loadSample(v, f.files[0]); f.remove(); }; f.click(); });
  cl.addEventListener('click', () => { delete sampleNames[v]; Eight.post({t:'sample', v, clear:true}); idb('delete', v); slotEls[v](); });
  slotEls[v] = () => { s.classList.toggle('has', !!sampleNames[v]); s.querySelector('.nm').textContent = sampleNames[v] || 'circuit'; };
}
async function loadSample(v, file){
  await Eight.start();
  try {
    const buf = await Eight.ctx.decodeAudioData(await file.arrayBuffer());
    const n = buf.length, data = new Float32Array(n);
    for (let c = 0; c < buf.numberOfChannels; c++){ const d = buf.getChannelData(c); for (let i = 0; i < n; i++) data[i] += d[i] / buf.numberOfChannels; }
    sampleNames[v] = file.name;
    idb('put', v, {data, sr:buf.sampleRate, name:file.name});
    Eight.post({t:'sample', v, data, sr:buf.sampleRate});
    slotEls[v] && slotEls[v](); flash(v + ' SAMPLE ' + file.name);
  } catch (e) { toast("Couldn't read that file: " + e.message); }
}
function idb(op, key, val){
  return new Promise(res => {
    try {
      const rq = indexedDB.open('omega-808', 1);
      rq.onupgradeneeded = () => rq.result.createObjectStore('samples');
      rq.onerror = () => res(null);
      rq.onsuccess = () => { try { const tx = rq.result.transaction('samples', op === 'get' || op === 'all' ? 'readonly' : 'readwrite'), st = tx.objectStore('samples');
        const r = op === 'put' ? st.put(val, key) : op === 'delete' ? st.delete(key) : op === 'all' ? st.getAll() : st.get(key);
        if (op === 'all'){ const ks = st.getAllKeys(); tx.oncomplete = () => res({keys:ks.result, vals:r.result}); } else r.onsuccess = () => res(r.result); r.onerror = () => res(null);
      } catch (e) { res(null); } };
    } catch (e) { res(null); }
  });
}
async function restoreSamples(){
  const all = await idb('all'); if (!all) return;
  all.keys.forEach((k, i) => { const s = all.vals[i]; if (!s) return; sampleNames[k] = s.name; Eight.post({t:'sample', v:k, data:s.data, sr:s.sr}); });
  renderBoard();
}

/* ---------------- presets ---------------- */
let ptab = 'Kits';
const userPresets = () => { try { return JSON.parse(localStorage.getItem(USTORE) || '[]'); } catch (e) { return []; } };
function lists(){ return {Kits:globalThis.EIGHT_KITS || [], Rhythms:globalThis.EIGHT_RHYTHMS || [], Banks:globalThis.EIGHT_BANKS || [], FX:globalThis.EIGHT_FX || [], Mine:userPresets()}; }
function applyPreset(tab, p){
  if (p.bank) Eight.loadBank(p.bank, p.tempo);
  if (p.rhythm){ const r = p.rhythm.A ? p.rhythm : {A:p.rhythm, B:p.rhythm}; S.bank.basic[S.basic] = {A:Object.assign(emptyPat(), JSON.parse(JSON.stringify(r.A))), B:Object.assign(emptyPat(), JSON.parse(JSON.stringify(r.B || r.A)))};
    Eight.post({t:'bank', bank:S.bank}); if (p.tempo) Eight.setTempo(p.tempo); if (p.swing != null && PMAP.swing) Eight.set('swing', p.swing, true); }
  if (p.params){
    if (tab === 'FX') Eight.applyKit(p.params, k => /^(rev|dly)\./.test(k) && !/send/.test(k));
    else if (tab === 'Mine'){ Object.assign(S.params, p.params); Eight.post({t:'setMany', p:S.params}); }
    else Eight.applyKit(p.params);
  }
  renderAll(); save(); flash(p.name.toUpperCase());
}
function openPresets(){
  const d = $('#presets'), tabs = $('#ptabs'); tabs.innerHTML = '';
  for (const t of Object.keys(lists())){ const b = el('button', t === ptab ? 'on' : '', tabs, t + ' ' + lists()[t].length); b.onclick = () => { ptab = t; openPresets(); }; }
  renderPresetList(); if (!d.open) d.showModal();
}
function renderPresetList(){
  const L = lists()[ptab], q = $('#psearch').value.toLowerCase(), box = $('#plist'); box.innerHTML = '';
  let cat = null;
  L.forEach((p, i) => {
    if (q && !(p.name + ' ' + (p.cat || '')).toLowerCase().includes(q)) return;
    if ((p.cat || '') !== cat){ cat = p.cat || ''; if (cat) el('div', 'cat', box).textContent = cat.toUpperCase(); }
    const b = el('button', 'pi', box); el('span', '', b).textContent = p.name;
    if (ptab === 'Mine'){ const x = el('span', 'del', b, '✕'); x.onclick = e => { e.stopPropagation(); const u = userPresets(); u.splice(i, 1); localStorage.setItem(USTORE, JSON.stringify(u)); renderPresetList(); }; }
    b.onclick = () => { Eight.start(); applyPreset(ptab, p); $('#pinfo').textContent = p.name; };
  });
  if (!L.length) el('div', 'cat', box).textContent = ptab === 'Mine' ? 'NOTHING SAVED YET' : 'NO PRESETS LOADED';
}
function randomPreset(){
  const L = lists(), pick = a => a[Math.floor(Math.random() * a.length)];
  if (L.Kits.length) applyPreset('Kits', pick(L.Kits));
  else { const p = {}; for (const q of PARAMS) if (/^mod\./.test(q.k) && !q.step) p[q.k] = denorm(q, norm(q, q.def) + (Math.random() - 0.5) * 0.5); Eight.applyKit(p); }
  if (L.Rhythms.length) applyPreset('Rhythms', pick(L.Rhythms));
  if (L.FX.length && Math.random() < 0.5) applyPreset('FX', pick(L.FX));
}

/* ---------------- WAV export ---------------- */
async function exportWav(){
  const bars = +$('#xbars').value, sr = 44100, msg = $('#xmsg');
  const p = S.bank.basic[S.basic][S.variation === 'B' ? 'B' : 'A'];
  const secs = bars * p.parts * p.len * 15 / bpm() + 2.5;
  msg.textContent = 'Rendering…';
  try {
    const oc = new OfflineAudioContext(2, Math.ceil(secs * sr), sr);
    for (const f of ['fx.js','seq.js','engine.js','worklet.js']) await oc.audioWorklet.addModule(f);
    const init = [{t:'setMany', p:S.params}, {t:'bank', bank:S.bank}, {t:'tempo', bpm:bpm()}, {t:'select', basic:S.basic, fill:S.fill, variation:S.variation, ifVar:S.ifVar}];
    const all = await idb('all'); if (all) all.keys.forEach((k, i) => all.vals[i] && init.push({t:'sample', v:k, data:all.vals[i].data, sr:all.vals[i].sr}));
    init.push({t:'play', on:true});
    const n = new AudioWorkletNode(oc, 'eight-808', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{sr, init}});
    n.connect(oc.destination);
    for (const m of init) n.port.postMessage(m);
    await new Promise(r => setTimeout(r, 300));
    const buf = await oc.startRendering();
    const wav = encodeWav(buf), name = `Omega-808-R${S.basic + 1}-${Math.round(bpm())}bpm.wav`;
    if (window.AndroidEight && AndroidEight.save){
      let bin = ''; const u = new Uint8Array(wav); for (let i = 0; i < u.length; i += 0x8000) bin += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
      msg.textContent = 'Saved: ' + AndroidEight.save(btoa(bin), name, 'audio/wav');
    } else {
      const a = el('a'); a.href = URL.createObjectURL(new Blob([wav], {type:'audio/wav'})); a.download = name; a.click(); msg.textContent = 'Saved ' + name;
    }
  } catch (e) { msg.textContent = 'Export failed: ' + e.message; }
}
function encodeWav(buf){
  const n = buf.length, ch = 2, out = new ArrayBuffer(44 + n * ch * 2), v = new DataView(out);
  const w = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); v.setUint32(4, 36 + n * ch * 2, true); w(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, ch, true);
  v.setUint32(24, buf.sampleRate, true); v.setUint32(28, buf.sampleRate * ch * 2, true); v.setUint16(32, ch * 2, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, n * ch * 2, true);
  const L = buf.getChannelData(0), R = buf.getChannelData(1);
  for (let i = 0, o = 44; i < n; i++){ for (const d of [L, R]){ v.setInt16(o, Math.max(-1, Math.min(1, d[i])) * 32767, true); o += 2; } }
  return out;
}

/* ---------------- layout ---------------- */
// closed, the photo is cropped to the panel face (no jacks, end cheeks or feet) and scaled as big as the screen allows;
// open, the whole 2000 x 1190 board shows. Everything sits in photo coordinates, so one transform keeps it all aligned
const CROP = [88, 58, 1824, 1037];
function fit(){
  const vw = innerWidth, vh = innerHeight, open = $('#panel').classList.contains('open'), portrait = vh > vw * 1.1;
  const panel = $('#panel'), tiles = [...document.querySelectorAll('#lid .tile')], board = $('#board');
  document.body.classList.toggle('portrait', portrait);
  panel.style.height = '';
  if (board) board.style.removeProperty('--ph');
  tiles.forEach(t => { t.style.transform = ''; t.querySelector('.tph').style.clipPath = ''; });
  if (portrait){
    // closed: the four tiles stacked, as big as fits; open: the board fills the whole screen
    const tw = Math.max(...TILES.map(r => r[2])), th = TILES.reduce((a, r) => a + r[3], 0);
    scale = open ? vw / 2000 : Math.min(vw / (tw + 40), vh / (th + 40));
    panel.style.transform = `scale(${scale})`;
    const W = vw / scale, H = vh / scale;
    if (open){ panel.style.height = H + 'px'; if (board) board.style.setProperty('--ph', H + 'px'); }
    const gap = Math.max(0, (H - th) / (TILES.length + 1));
    let y = gap;
    TILES.forEach(([rx, ry, rw, rh], i) => {
      const t = tiles[i]; if (!t) return;
      t.style.transform = `translate(${(W - rw) / 2 - rx}px, ${y - ry}px)`;
      t.querySelector('.tph').style.clipPath = `inset(${ry}px ${2000 - rx - rw}px ${1190 - ry - rh}px ${rx}px round 10px)`;
      y += rh + gap;
    });
    return;
  }
  const [cx, cy, cw, ch] = open ? [0, 0, 2000, 1190] : CROP;
  scale = Math.min(vw / cw, vh / ch);
  panel.style.transform = `translate(${(vw - cw * scale) / 2 - cx * scale}px, ${(vh - ch * scale) / 2 - cy * scale}px) scale(${scale})`;
}
addEventListener('resize', fit);
document.addEventListener('pointerdown', () => { if (S.power) Eight.start(); }, {capture:true, once:true});
document.querySelectorAll('[data-close]').forEach(b => b.addEventListener('click', () => b.closest('dialog').close()));
$('#psearch').addEventListener('input', renderPresetList);
$('#prandom').addEventListener('click', () => { Eight.start(); randomPreset(); });
$('#psave').addEventListener('click', () => { const name = prompt('Name for this preset'); if (!name) return; const u = userPresets(); u.push({name, cat:'Mine', params:Object.assign({}, S.params), bank:S.bank, tempo:bpm()}); localStorage.setItem(USTORE, JSON.stringify(u)); ptab = 'Mine'; openPresets(); });
$('#xgo').addEventListener('click', exportWav);
/* ---------------- MIDI files: import to the drum tracks, export the rhythms (midifile.js) ---------------- */
function saveBytes(u8, name, mime){
  if (window.AndroidEight && AndroidEight.save){
    let bin = ''; for (let i = 0; i < u8.length; i += 0x8000) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return AndroidEight.save(btoa(bin), name, mime);
  }
  const a = el('a'); a.href = URL.createObjectURL(new Blob([u8], {type:mime})); a.download = name; document.body.appendChild(a); a.click(); a.remove();
  return name;
}
async function importMidi(file){
  try {
    const mf = EightMidiFile.parse(await file.arrayBuffer());
    const thr = (window.EightMidi && EightMidi.state && EightMidi.state.accThr) || 100;
    const r = EightMidiFile.toPatterns(mf, {accThr:thr});
    if (!r.patterns.length || !r.used) { toast('No drum notes found in ' + file.name); return; }
    const b = JSON.parse(JSON.stringify(S.bank));
    r.patterns.forEach((p, i) => { b.basic[i] = {A:p, B:JSON.parse(JSON.stringify(p))}; });
    Eight.loadBank(b, r.tempo ? Math.round(r.tempo) : null);
    if (Eight.selectRhythm) Eight.selectRhythm({basic:0, variation:'A'});
    $('#presets').close && $('#presets').close();
    toast(`${file.name}: ${r.patterns.length} bar${r.patterns.length > 1 ? 's' : ''} into rhythms 1-${r.patterns.length}` +
      (r.tempo ? `, ${Math.round(r.tempo)} BPM` : '') + `, channel ${r.ch}` + (r.skipped ? `, ${r.skipped} notes with no 808 voice skipped` : '') +
      (r.dropped ? `, ${r.dropped} bars past 12 left out` : ''));
  } catch (e) { toast("Couldn't read that MIDI file: " + e.message); }
}
$('#pmidiin').addEventListener('click', () => {
  const f = el('input'); f.type = 'file'; f.accept = '.mid,.midi,audio/midi,audio/x-midi,audio/mid'; f.style.display = 'none'; document.body.appendChild(f);
  f.onchange = () => { if (f.files[0]) importMidi(f.files[0]); f.remove(); }; f.click();
});
$('#pmidiout').addEventListener('click', () => {
  const pats = [], has = p => p && Object.keys(p.steps || {}).some(k => k !== 'AC' && /x/.test(p.steps[k]));
  for (const r of S.bank.basic){ if (has(r.A)) pats.push(r.A); if (has(r.B) && JSON.stringify(r.B.steps) !== JSON.stringify(r.A.steps)) pats.push(r.B); }
  if (!pats.length) { toast('No rhythms with notes to export'); return; }
  const name = `Omega-808-${Math.round(bpm())}bpm.mid`;
  toast('Saved: ' + saveBytes(EightMidiFile.build(pats, bpm()), name, 'audio/midi'));
});
if ('serviceWorker' in navigator && location.protocol === 'https:' && location.hostname !== 'appassets.androidplatform.net') navigator.serviceWorker.register('sw.js').catch(() => {});

loadState();
buildBoard();
buildFace();
fit();
renderAll();
