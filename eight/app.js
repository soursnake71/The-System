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
Eight.post = m => { if (Eight.node) Eight.node.port.postMessage(m); };
Eight.start = () => starting || (starting = (async () => {
  const ctx = Eight.ctx = new AudioContext({latencyHint:'interactive'});
  try {
    for (const f of ['fx.js','seq.js','engine.js','worklet.js']) await ctx.audioWorklet.addModule(f);
    const node = new AudioWorkletNode(ctx, 'eight-808', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2], processorOptions:{sr:ctx.sampleRate}});
    node.connect(ctx.destination);
    node.port.onmessage = onMsg;
    Eight.node = node;
    syncAll();
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
  if (S.pads) l1 = 'PADS  tap the step keys';
  else if (S.mode <= 1) l1 = `WRITE ${S.mode ? '2ND' : '1ST'} ${S.which === 'basic' ? 'R' + String(S.basic + 1).padStart(2, '0') : 'I/F' + (S.fill + 1)}${editVar()} ${tr}`;
  else if (S.mode === 4) l1 = 'COMPOSE ' + (S.chain.length ? S.chain.map(n => n + 1).join('-') : '(tap 1-12)');
  else l1 = `${S.mode === 2 ? 'MANUAL' : 'PLAY'} R${String(S.basic + 1).padStart(2, '0')} ${S.variation}  I/F${S.fill + 1}${S.ifVar}`;
  const l2 = S.msg || `${bpm().toFixed(1)} BPM  ${S.playing ? '▶' : '■'}  FILL ${FILLS[fillIdx()] || 'MAN'}`;
  lcd.innerHTML = '';
  el('div', '', lcd).textContent = l1.slice(0, 32);
  el('div', 'l2', lcd).textContent = l2.slice(0, 36);
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
  });
  h.addEventListener('pointermove', e => {
    if (e.pointerId !== id) return;
    const d = (sy - e.clientY) / scale;
    if (o.angles){ const n = Math.max(0, Math.min(o.angles.length - 1, sv + Math.round(d / 60))); if (n !== o.get()){ o.set(n); c.render(); } }
    else { o.set(Math.max(0, Math.min(1, sv + d / 320))); c.render(); }
  });
  h.addEventListener('pointerup', () => { id = null; });
  h.addEventListener('wheel', e => { e.preventDefault(); if (o.angles) o.set(Math.max(0, Math.min(o.angles.length - 1, o.get() + (e.deltaY < 0 ? 1 : -1)))); else o.set(Math.max(0, Math.min(1, o.get() - e.deltaY / 2000))); c.render(); }, {passive:false});
  c.render();
  return c;
}
function paramKnob(k, x, y, r, photo){
  const p = () => PMAP[k] || {min:0, max:1, def:0.5};
  controls[k] = knob({x, y, r, photo, get:() => norm(p(), S.params[k] ?? p().def), set:n => Eight.set(k, denorm(p(), n)), reset:() => Eight.set(k, p().def)});
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
const leds = [], vLeds = [], pLeds = [];
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
  controls._mode = knob({x:228, y:320, r:44, photo:-15, angles:[-70,-30,15,60,100], get:() => S.mode, set:n => { S.mode = n; S.pads = false; flash(MODES[n]); renderAll(); save(); }});
  controls._inst = knob({x:410, y:318, r:40, photo:-70, angles:TRACKS.map((_, i) => -165 + 30 * i), get:() => S.track, set:n => { S.track = n; S.trigWrite = false; flash('TRACK ' + TRACKS[n]); renderAll(); save(); }});
  controls._fill = knob({x:412, y:478, r:40, photo:45, angles:[-100,-45,-15,15,45,75], get:fillIdx, set:n => { S.bank.fillEvery = FILLS[n]; Eight.post({t:'bank', bank:S.bank}); flash('AUTO FILL ' + (FILLS[n] || 'MANUAL')); save(); }});
  controls._tempo = knob({x:250, y:635, r:78, photo:-80, get:() => S.tempoK, set:n => { S.tempoK = n; Eight.post({t:'tempo', bpm:bpm()}); flash(bpm().toFixed(1) + ' BPM'); save(); }, reset:() => Eight.setTempo(120)});
  controls._fine = knob({x:420, y:688, r:34, photo:-60, get:() => S.fine, set:n => { S.fine = n; Eight.post({t:'tempo', bpm:bpm()}); flash(bpm().toFixed(1) + ' BPM'); save(); }, reset:() => { S.fine = 0.5; Eight.post({t:'tempo', bpm:bpm()}); }});
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
  });
  // readout and buttons in the blank space
  el('div', '', ctl).id = 'lcd';
  const fb = el('div', 'fbtns', ctl);
  for (const [lab, fn, id] of [['PRESETS', openPresets], ['PADS', () => { S.pads = !S.pads; renderAll(); }, 'bpads'], ['MIDI', () => window.EightMidi && EightMidi.openSettings ? EightMidi.openSettings() : toast('MIDI is not available here')],
    ['EXPORT', () => $('#export').showModal()], ['CIRCUIT ▲', () => fold(true)]]){
    const b = el('button', 'fb', fb, lab); if (id) b.id = id;
    b.addEventListener('pointerdown', e => e.stopPropagation()); b.addEventListener('click', fn);
  }
  const hg = el('button', 'hinge', ctl, 'OPEN ▲'); hg.addEventListener('click', () => fold(true));
  swipe(hg, -1, () => fold(true));
}
function stepKey(i, down, e, b){
  presses[i].classList.toggle('on', down);
  if (!down) return;
  Eight.start();
  if (S.pads){
    const r = b.getBoundingClientRect(), vel = Math.max(0.3, Math.min(1, 1 - (e.clientY - r.top) / r.height + 0.3));
    const v = i === 0 ? null : i < 12 ? trackVoice(i) : ['MA','CL','HC','TRIG'][i - 12];
    if (v) Eight.hit(v, S.padAcc ? 1 : 0, vel); else { S.padAcc = !S.padAcc; flash('ACCENT ' + (S.padAcc ? 'ON' : 'OFF')); }
    return;
  }
  if (S.clearHeld && S.mode <= 1){ const p = curPat(); p.len = i + 1; padSteps(p); Eight.post({t:'bank', bank:S.bank}); flash('LAST STEP ' + (i + 1)); save(); renderLeds(); return; }
  if (S.mode <= 1) return toggleStep(i);
  if (S.mode === 4){ if (i < 12){ S.chain.push(i); flash('ADD R' + (i + 1)); } else if (i === 12){ S.chain.pop(); flash('UNDO'); } else if (i === 13){ S.chain = []; flash('CHAIN CLEARED'); } save(); renderAll(); return; }
  if (i < 12){ S.basic = i; S.which = 'basic'; } else { S.fill = i - 12; S.which = 'fill'; }
  sendSelect(); renderAll(); save();
}
function renderLeds(){
  if (!leds.length) return;
  const lit = new Array(16).fill(false);
  if (S.pads){}
  else if (S.mode <= 1){ const p = curPat(), tr = S.trigWrite ? 'TRIG' : TRACKS[S.track], off = S.mode === 1 ? 16 : 0; for (let i = 0; i < 16; i++) lit[i] = stepOn(p, tr, off + i); }
  else if (S.mode === 4){ for (const n of S.chain) lit[n] = true; }
  else { lit[S.basic] = true; lit[12 + S.fill] = true; }
  for (let i = 0; i < 16; i++){ const run = S.playing && S.run === i; leds[i].classList.toggle('on', run ? !lit[i] || S.mode > 1 : lit[i]); leds[i].classList.toggle('run', run); }
  const v = S.playing ? S.curVar : S.variation;
  vLeds[0].classList.toggle('on', v === 'A' || v === 'AB'); vLeds[1].classList.toggle('on', v === 'B' || v === 'AB');
  const part = S.mode <= 1 && !S.playing ? S.mode : S.curPart;
  pLeds[0].classList.toggle('on', part === 0); pLeds[1].classList.toggle('on', part === 1);
}
function renderAll(){
  for (const k in controls) controls[k].render();
  const pb = $('#bpads'); if (pb) pb.classList.toggle('on', S.pads);
  renderLeds(); renderLcd(); renderBoard();
}

/* ---------------- fold open: the board under the lid ---------------- */
function fold(open){ $('#panel').classList.toggle('open', open); $('#board').setAttribute('aria-hidden', !open); if (open) renderBoard(); }
function swipe(elm, dir, fn){
  let y0 = null;
  elm.addEventListener('pointerdown', e => { y0 = e.clientY; });
  elm.addEventListener('pointerup', e => { if (y0 != null && (e.clientY - y0) * dir > 40) fn(); y0 = null; });
}
const SECTS = [
  {id:'BD', name:'BASS DRUM', v:['BD'], at:[24,90,236,545]}, {id:'SD', name:'SNARE DRUM', v:['SD'], at:[268,90,236,545]},
  {id:'TOMS', name:'TOMS / CONGAS', v:['LT','MT','HT','LC','MC','HC'], at:[512,90,236,545]}, {id:'RSCL', name:'RIM / CLAVES', v:['RS','CL'], at:[756,90,236,545]},
  {id:'CPMA', name:'CLAP / MARACAS', v:['CP','MA'], at:[1000,90,236,545]}, {id:'CB', name:'COWBELL', v:['CB'], at:[1244,90,236,545]},
  {id:'CY', name:'CYMBAL', v:['CY'], at:[1488,90,236,545]}, {id:'HH', name:'HI-HATS', v:['OH','CH'], at:[1732,90,244,545]},
  {id:'ACC', name:'ACCENT', v:[], at:[24,668,236,455]}, {id:'TRIG', name:'TRIGGER OUT', v:['TRIG'], at:[268,668,380,455]},
  {id:'MACH', name:'MACHINE', v:[], at:[656,668,236,455]},
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
function trim(parent, p, cls){
  if (p.step === 1 && p.max - p.min === 1 && !p.opts){
    const d = el('div', 'dip', parent, `<div class="sw"></div><div class="lab"></div><div class="val"></div>`);
    d.querySelector('.lab').textContent = p.label || p.k;
    d.addEventListener('click', () => Eight.set(p.k, S.params[p.k] ? p.min : p.max));
    boardCtl[p.k] = () => { d.classList.toggle('on', S.params[p.k] === p.max); d.querySelector('.val').textContent = fmt(p, S.params[p.k]); };
    return;
  }
  const t = el('div', 'tp' + (cls ? ' ' + cls : ''), parent, `<div class="pot"><b></b></div><div class="lab"></div><div class="val"></div>`);
  t.querySelector('.lab').textContent = p.label || p.k; t.title = p.k;
  const b = t.querySelector('b'), val = t.querySelector('.val');
  boardCtl[p.k] = () => { const v = S.params[p.k] ?? p.def; b.style.transform = `rotate(${-135 + 270 * norm(p, v)}deg)`; val.textContent = fmt(p, v); };
  let sy = 0, sv = 0, id = null, last = 0;
  t.addEventListener('pointerdown', e => { e.preventDefault(); t.setPointerCapture(e.pointerId); id = e.pointerId; sy = e.clientY; sv = norm(p, S.params[p.k] ?? p.def);
    const now = performance.now(); if (now - last < 300) Eight.set(p.k, p.def); last = now; });
  t.addEventListener('pointermove', e => { if (e.pointerId !== id) return; const n = sv + (sy - e.clientY) / scale / (p.opts ? 60 * (p.opts.length) : 300); Eight.set(p.k, denorm(p, n)); });
  t.addEventListener('pointerup', () => { id = null; });
}
function buildBoard(){
  PMAP = {}; for (const p of PARAMS) PMAP[p.k] = p;
  const B = $('#board'); B.innerHTML = '';
  for (const k in boardCtl) delete boardCtl[k];
  // copper traces
  let svg = '<svg class="traces" viewBox="0 0 2000 1190" preserveAspectRatio="none"><g fill="none" stroke="#3f9a5c" stroke-width="5" stroke-linecap="round">';
  for (let i = 0; i < 26; i++){ const y = 80 + i * 42, x = 30 + (i * 137) % 1800; svg += `<path d="M${x} ${y} h${180 + (i * 53) % 260} l40 40 h${120 + (i * 31) % 200}"/>`; }
  svg += '</g><g fill="#c9a64a">'; for (let i = 0; i < 60; i++) svg += `<circle cx="${(i * 331) % 1960 + 20}" cy="${(i * 173) % 1150 + 20}" r="6"/>`;
  B.innerHTML = svg + '</g></svg>';
  const head = el('div', 'bhead', B, `<span>TR-808 MAIN BOARD</span><small>CIRCUIT MODS · double tap a trimpot for stock · drag to turn</small><span class="sp"></span>`);
  const rnd = el('button', 'bbtn', head, 'STOCK ALL MODS'); rnd.addEventListener('click', () => { const p = {}; for (const q of PARAMS) if (/^mod\./.test(q.k)) p[q.k] = q.def; Object.assign(S.params, p); Eight.post({t:'setMany', p}); renderAll(); save(); flash('STOCK CIRCUIT'); });
  const bins = {};
  for (const s of SECTS){
    const d = el('div', 'sect', B); Object.assign(d.style, {left:s.at[0] + 'px', top:s.at[1] + 'px', width:s.at[2] + 'px', height:s.at[3] + 'px'});
    el('h3', '', d, s.name);
    if (s.id === 'TRIG'){
      el('h3', '', d).remove();
      d.querySelector('h3').innerHTML = 'TRIGGER OUT<span class="tipq" title="The 808\'s rear TRIGGER OUT jacks put out 20 ms pulses following the CB, CP or AC track. Egyptian Lover recorded that click as a drum sound in his records">?</span>';
      el('div', 'egypt', d).textContent = "The 808's rear TRIGGER OUT jacks put out 20 ms pulses following the CB, CP or AC track. Egyptian Lover recorded that click as a drum sound in his records. Turn up LEVEL to put it in the mix.";
      const w = el('button', 'bbtn', d, 'WRITE TRIG TRACK'); w.style.margin = '0 4px 8px';
      w.addEventListener('click', () => { S.trigWrite = !S.trigWrite; if (S.trigWrite && S.mode > 1) S.mode = 0; fold(false); flash(S.trigWrite ? 'WRITE TRIG TRACK' : 'TRACK ' + TRACKS[S.track]); renderAll(); });
    }
    bins[s.id] = {pots:el('div', 'pots', d), slots:s.v.length ? el('div', 'slots', d) : null, v:s.v};
  }
  for (const p of PARAMS){ const s = sectionOf(p.k); if (!s || s === 'REV' || s === 'DLY') continue; trim((bins[s] || bins.MACH).pots, p, /send/.test(p.k) ? '' : 'mod'); }
  for (const id in bins) if (bins[id].slots) for (const v of bins[id].v) sampleSlot(bins[id].slots, v);
  // REV7
  const rv = el('div', 'unit rev7', B); Object.assign(rv.style, {left:'900px', top:'668px', width:'520px', height:'455px'});
  el('div', 'uname', rv, 'REV 7 <small>DIGITAL REVERBERATOR</small>');
  const lcd = el('div', 'lcd', rv, '<button>◀</button><span class="num"></span><span class="pn"></span><button>▶</button>');
  const pp = PMAP['rev.program'];
  if (pp){
    const [l, r] = lcd.querySelectorAll('button'), n = PROGRAMS().length;
    const step = d => Eight.set('rev.program', (Math.round(S.params['rev.program'] || 0) + d + n) % n, true);
    l.addEventListener('click', () => step(-1)); r.addEventListener('click', () => step(1));
    boardCtl['rev.program'] = () => { const i = Math.round(S.params['rev.program'] || 0); lcd.querySelector('.num').textContent = String(i + 1).padStart(2, '0'); lcd.querySelector('.pn').textContent = (PROGRAMS()[i] || '').toUpperCase(); };
  }
  const rp = el('div', 'pots', rv);
  for (const p of PARAMS) if (sectionOf(p.k) === 'REV' && p.k !== 'rev.program') trim(rp, p);
  // TC 2290
  const dl = el('div', 'unit tc2290', B); Object.assign(dl.style, {left:'1428px', top:'668px', width:'548px', height:'455px'});
  el('div', 'uname', dl, '2290 <small>DYNAMIC DIGITAL DELAY</small>');
  const disp = el('div', 'disp', dl, '<span class="unitlab"></span><span class="seg"></span>');
  boardCtl._disp = () => { const sync = S.params['dly.sync'], dv = PMAP['dly.division'];
    disp.querySelector('.seg').textContent = sync && dv ? fmt(dv, S.params['dly.division']) : String(Math.round(S.params['dly.time'] || 0)).padStart(4, ' ');
    disp.querySelector('.unitlab').textContent = sync ? 'TEMPO' : 'ms'; };
  const dp = el('div', 'pots', dl);
  for (const p of PARAMS) if (sectionOf(p.k) === 'DLY') trim(dp, p);
  const cb = el('button', 'closeb', B, 'CLOSE ▼'); cb.addEventListener('click', () => fold(false));
  swipe(cb, 1, () => fold(false));
  renderBoard();
}
function renderBoard(){ for (const k in boardCtl) boardCtl[k](); for (const v in slotEls) slotEls[v](); }

/* ---------------- samples ---------------- */
const slotEls = {}, sampleNames = {};
function sampleSlot(parent, v){
  const s = el('div', 'slotx', parent, `<b>${v}</b><span class="nm">circuit</span><button>LOAD</button><button class="clr">✕</button>`);
  const [ld, cl] = s.querySelectorAll('button');
  ld.addEventListener('click', () => { const f = el('input'); f.type = 'file'; f.accept = 'audio/*,video/*'; f.onchange = () => f.files[0] && loadSample(v, f.files[0]); f.click(); });
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
function fit(){
  const vw = innerWidth, vh = innerHeight;
  scale = Math.min(vw / 2000, vh / 1190);
  $('#panel').style.transform = `translate(${(vw - 2000 * scale) / 2}px, ${(vh - 1190 * scale) / 2}px) scale(${scale})`;
}
addEventListener('resize', fit);
document.addEventListener('pointerdown', () => { if (S.power) Eight.start(); }, {capture:true, once:true});
document.querySelectorAll('[data-close]').forEach(b => b.addEventListener('click', () => b.closest('dialog').close()));
$('#psearch').addEventListener('input', renderPresetList);
$('#prandom').addEventListener('click', () => { Eight.start(); randomPreset(); });
$('#psave').addEventListener('click', () => { const name = prompt('Name for this preset'); if (!name) return; const u = userPresets(); u.push({name, cat:'Mine', params:Object.assign({}, S.params), bank:S.bank, tempo:bpm()}); localStorage.setItem(USTORE, JSON.stringify(u)); ptab = 'Mine'; openPresets(); });
$('#xgo').addEventListener('click', exportWav);
if ('serviceWorker' in navigator && location.protocol === 'https:' && location.hostname !== 'appassets.androidplatform.net') navigator.serviceWorker.register('sw.js').catch(() => {});

loadState();
buildBoard();
buildFace();
fit();
renderAll();
