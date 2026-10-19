/* XL-Desk: the console surface, the rear panel, the tape machine and the print recorder. The audio is all in dsp/console.js. */
'use strict';
const HOST = window.XLHOST;   // set inside the VST3
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const el = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt != null) e.textContent = txt; return e; };
const toast = (msg, ms = 2600) => { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('on'), ms); };

/* ================= console inputs (rear panel) ================= */
const INPUTS = [];
for (let i = 1; i <= 8; i++) INPUTS.push({id:'mic' + i, lb:'Mic in ' + i, st:0, grp:'MIC INPUT 1-8'});
for (let i = 9; i <= 16; i++) INPUTS.push({id:'line' + i, lb:'Line in ' + i, st:0, grp:'LINE INPUT 9-16'});
for (let i = 1; i <= 16; i++) INPUTS.push({id:'daw' + i, lb:'DAW in ' + i, st:0, grp:'DAW INPUT 1-16'});
for (let k = 1; k <= 4; k++) INPUTS.push({id:'st' + k, lb:'Stereo in ' + k, st:1, grp:'STEREO INPUT 1-4'});
INPUTS.push({id:'ret1', lb:'Stereo return 1', st:1, grp:'MISC INPUTS'}, {id:'ret2', lb:'Stereo return 2', st:1, grp:'MISC INPUTS'}, {id:'ext', lb:'External input', st:1, grp:'MISC INPUTS'},
  {id:'ijack', lb:'iJack (front)', st:1, grp:'Front panel'}, {id:'talk', lb:'Talkback mic', st:0, grp:'Front panel'}, {id:'listen', lb:'Listen mic', st:0, grp:'LISTEN IN (XLR)'});
for (let i = 1; i <= 16; i++) INPUTS.push({id:'slotin' + i, lb:'500 slot in ' + i, st:0, grp:'500 SLOT IN 1-16 (straight into the module)'});
const INP = Object.fromEntries(INPUTS.map(x => [x.id, x]));
// console outputs that can be patched to interface channels
const OUTS = [['monsel', 'Monitor: selected speakers (+ sub folded in)', 2], ['main', 'Monitor: Main L/R', 2], ['mini1', 'Monitor: Mini 1 L/R', 2], ['mini2', 'Monitor: Mini 2 L/R', 2], ['sub', 'Monitor: Sub', 1],
  ['hp', 'Headphones', 2], ['fbA', 'Foldback A', 2], ['fbB', 'Foldback B', 2], ['cue', 'Cue stereo', 2], ['aux1', 'Aux 1', 1], ['aux2', 'Aux 2', 1],
  ['mixA', 'Mix A out', 2], ['mixB', 'Mix B out', 2], ['mixC', 'Mix C out', 2], ['mixD', 'Mix D out', 2], ['ret1out', 'Stereo return out 1', 2], ['ret2out', 'Stereo return out 2', 2],
  ['meter', 'Meter L/R', 2], ['lsnout', 'Listen mic out', 1]];
for (let i = 1; i <= 16; i++) OUTS.push(['ch' + i, 'Channel out ' + i, 1]); for (let i = 1; i <= 16; i++) OUTS.push(['slotout' + i, '500 slot out ' + i, 1]); for (let k = 1; k <= 4; k++) OUTS.push(['st' + k + 'out', 'Stereo out ' + k, 2]);
const OUT = Object.fromEntries(OUTS.map(([id, lb, n]) => [id, {lb, n}]));

/* ================= state ================= */
const STORE = 'xldesk-v2';
function defState(){
  return {v:2, P:XL.defaults(), types:XL.defTypes(), names:{}, io:{}, out:[{sig:'monsel', ch:0}], cfg:{micRef:-20, convRef:24}, dev:{in:'', out:'', inOn:false},
    tracks:[], midi:{on:false, inId:'', outId:'', fb:true, pb:true, btn:'toggle', mmcIn:true, mmcOut:false, mmcId:127}, learn:{}, snaps:{},
    rec:{targets:['mixA']}, loop:{on:false, ls:0, le:0}, view:'desk', rackType:'vhdpre'};
}
let S;
try { S = JSON.parse(localStorage.getItem(STORE)); } catch(e){ S = null; }
if (!S){ // carry files, patching and MIDI settings over from version 1 (its module settings used a different layout)
  let o = null; try { o = JSON.parse(localStorage.getItem('xldesk-v1')); } catch(e){}
  S = defState(); if (o) for (const k of ['names', 'io', 'out', 'cfg', 'dev', 'tracks', 'midi', 'loop']) if (o[k]) S[k] = o[k]; }
if (S.v !== 2) S = defState();
{ const D = defState(); for (const k in D) if (S[k] === undefined) S[k] = D[k]; S.P = Object.assign(XL.defaults(), S.P); S.types = Object.assign(XL.defTypes(), S.types); S.midi = Object.assign(D.midi, S.midi); S.cfg = Object.assign(D.cfg, S.cfg); }
// in the plugin, controls live in the plugin's parameters; the page only keeps its own settings (MIDI, names, snapshots) in the session
const uiState = () => ({names:S.names, midi:S.midi, learn:S.learn, snaps:S.snaps, view:S.view, rackType:S.rackType, rec:S.rec});
let saveT = 0; const save = () => { clearTimeout(saveT); saveT = setTimeout(() => { if (HOST){ HOST.send({t:'ui', ui:JSON.stringify(uiState())}); return; } try { localStorage.setItem(STORE, JSON.stringify(S)); } catch(e){} }, 300); };

/* ================= audio ================= */
let ctx = null, node = null, inSrc = null, inStream = null, nIn = 0, nOut = 2;
const post = m => HOST ? HOST.send(m) : node && node.port.postMessage(m);
async function power(){
  if (ctx){ if (ctx.state === 'running'){ await ctx.suspend(); $('#power').classList.remove('on'); toast('Desk off.'); } else { await ctx.resume(); $('#power').classList.add('on'); } return; }
  try {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    await ctx.audioWorklet.addModule('spec.js'); await ctx.audioWorklet.addModule('dsp/console.js');
    nOut = clamp(ctx.destination.maxChannelCount || 2, 2, 32);
    try { ctx.destination.channelCount = nOut; ctx.destination.channelInterpretation = 'discrete'; } catch(e){ nOut = 2; }
    node = new AudioWorkletNode(ctx, 'xl-desk', {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[nOut], channelCount:2, channelCountMode:'explicit', channelInterpretation:'discrete',
      processorOptions:{P:S.P, types:S.types, io:ioMap(), outMap:S.out, cfg:Object.assign({nOut}, S.cfg)}});
    node.connect(ctx.destination); node.port.onmessage = e => onEngine(e.data);
    if (S.dev.out && ctx.setSinkId) ctx.setSinkId(S.dev.out).catch(() => {});
    $('#power').classList.add('on');
    await loadTracks();
    if (S.loop.on) post({t:'tp', cmd:'loop', on:true, ls:S.loop.ls, le:S.loop.le});
    if (S.dev.inOn) enableInput().catch(() => {});
    toast('Desk on: ' + nOut + ' output channels.' + (nOut > 2 ? ' Patch them on the Rear tab.' : ''));
    if (S.view === 'rear') renderRear();
  } catch(e){ console.error(e); toast('Audio failed to start: ' + e.message, 5000); ctx = null; }
}
async function enableInput(){
  if (!ctx) await power();
  if (inSrc){ inSrc.disconnect(); inStream.getTracks().forEach(t => t.stop()); inSrc = null; }
  const c = {channelCount:{ideal:32}, echoCancellation:false, noiseSuppression:false, autoGainControl:false};
  if (S.dev.in) c.deviceId = {exact:S.dev.in};
  inStream = await navigator.mediaDevices.getUserMedia({audio:c});
  nIn = inStream.getAudioTracks()[0].getSettings().channelCount || 2;
  inSrc = ctx.createMediaStreamSource(inStream); node.channelCount = clamp(nIn, 1, 32); inSrc.connect(node);
  S.dev.inOn = true; save(); toast('Interface input on: ' + nIn + ' channel' + (nIn > 1 ? 's' : '') + '.');
  if (S.view === 'rear') renderRear();
}
// what feeds each console input, for the engine
function ioMap(){
  const m = {};
  for (const x of INPUTS){ const s = S.io[x.id]; if (s && s.k !== 'none') m[x.id] = Object.assign({mono:!x.st}, s); }
  for (const t of S.tracks) if (t.dest && !m[t.dest]) m[t.dest] = {k:'file', t:t.id, mono:!INP[t.dest].st};
  return m;
}
const pushIO = () => { post({t:'io', io:ioMap()}); save(); };

/* ================= parameters ================= */
const bound = {};   // id -> [render fns]
let SCOPE = 'global';
const bind = (id, fn) => { fn.sc = SCOPE; (bound[id] || (bound[id] = [])).push(fn); };
// panels that re-render drop their old bindings first
function scoped(name, build){ for (const id in bound) bound[id] = bound[id].filter(f => f.sc !== name); const prev = SCOPE; SCOPE = name; try { return build(); } finally { SCOPE = prev; } }
const val = id => S.P[id] !== undefined ? S.P[id] : (XL.P[id] ? XL.P[id].d : 0);
// the spec for any control: console controls from XL.P, module controls from whatever module is loaded at that location
const spec = id => { const sp = XL.P[id]; return sp && sp.k === 'mod' ? XL.modParam(S.types, id) : sp; };
const listeners = [];   // (id, v, from) -> MIDI feedback etc.
function setP(id, v, from){
  const sp = spec(id); if (!sp) return;
  if (sp.k === 'btn') v = v ? 1 : 0; else if (sp.k === 'step'){ const n = sp.v.length - 1; v = clamp(Math.round(v * n), 0, n) / n; } else v = clamp(v, 0, 1);
  if (S.P[id] === v) return;
  S.P[id] = v; post({t:'p', id, v}); (bound[id] || []).forEach(f => f(v)); save();
  for (const f of listeners) f(id, v, from);
  if (id === 'dip.12') $('#prog').classList.toggle('on', !!v);
}
listeners.push(id => { const m = /^(slot\d+)\./.exec(id); if (m) drawSlotHead(m[1]); });
const refreshAll = () => { for (const id in bound) bound[id].forEach(f => f(val(id))); $('#prog').classList.toggle('on', !!val('dip.12')); };
function fmt(id, v = val(id)){
  const sp = spec(id); if (!sp) return '';
  if (sp.k === 'btn') return v ? 'on' : 'off';
  if (sp.k === 'step'){ const x = sp.v[Math.round(v * (sp.v.length - 1))]; return typeof x === 'number' ? x + (sp.u ? (sp.u[0] === ':' ? '' : ' ') + sp.u : '') : x; }
  const x = XL.law(sp.law, v);
  if (sp.u === 'eqdB'){ const w = id.split('.')[0], M = XL.MOD[S.types[w]], bj = M.params.find(q => q.n === 'blk'), d = x * (bj && val(w + '.' + bj.j) ? 18 : 15); return (d > 0 ? '+' : '') + d.toFixed(1) + ' dB'; }
  if (sp.u === 'pan') return Math.abs(x) < .02 ? 'C' : (x < 0 ? 'L' : 'R') + Math.round(Math.abs(x) * 100);
  if (sp.u === 'h') return x < .05 ? '2nd' : x > .95 ? '3rd' : Math.round((1 - x) * 100) + '% 2nd';
  if (sp.u === 'dB' || sp.u === 'dBu') return x === -Infinity ? '-∞' : (x > 0 ? '+' : '') + x.toFixed(1) + ' ' + sp.u;
  if (sp.u === 'Hz') return x >= 1000 ? (x / 1000).toFixed(x >= 10000 ? 1 : 2) + ' kHz' : Math.round(x) + ' Hz';
  if (sp.u === 's') return x < 1 ? Math.round(x * 1000) + ' ms' : x.toFixed(2) + ' s';
  if (sp.u === 'Q') return 'Q ' + x.toFixed(2);
  if (sp.u === ':1') return x === Infinity ? '∞:1 (limit)' : x.toFixed(1) + ':1';
  return x.toFixed(2);
}
const ctlLabel = id => { const sp = spec(id); if (!sp) return id; const m = /^(slot|ins|bins)(\w+)\.\d+$/.exec(id); return m ? XL.MOD[S.types[m[1] + m[2]]].short + ' ' + m[2] + ' ' + sp.lb : sp.lb; };
const ro = $('#readout');
function showRO(e, id){ ro.textContent = ctlLabel(id) + ': ' + fmt(id); ro.style.display = 'block'; const r = e.getBoundingClientRect(); ro.style.left = clamp(r.left - 20, 4, innerWidth - 200) + 'px'; ro.style.top = Math.max(4, r.top - 34) + 'px'; }
const hideRO = () => { ro.style.display = 'none'; };

/* ---------- controls ---------- */
const learn = {on:false, sel:null};
function learnPick(e, id){ if (!learn.on) return false; e.preventDefault(); e.stopPropagation(); $$('.learnsel').forEach(x => x.classList.remove('learnsel')); e.currentTarget.classList.add('learnsel'); learn.sel = id; if (HOST) HOST.send({t:'learn', id}); toast('Now move a knob, fader or button on your MIDI controller.'); return true; }
function knob(id, color = 'black', label = '', size = ''){
  const sp = spec(id), w = el('div', 'kw'), k = el('div', 'kn c-' + color + (size ? ' ' + size : '')), cap = el('div', 'cap'); k.appendChild(cap); k.dataset.id = id; w.appendChild(k); if (label) w.appendChild(el('div', 'kl', label));
  const steps = sp.k === 'step' ? sp.v.length : 0;
  const draw = v => cap.style.setProperty('--r', (-135 + 270 * v) + 'deg');
  draw(val(id)); bind(id, draw);
  let y0 = 0, v0 = 0, lastTap = 0;
  k.addEventListener('pointerdown', e => {
    if (learnPick(e, id)) return;
    k.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = val(id); showRO(k, id);
    const now = Date.now(); if (now - lastTap < 300){ setP(id, sp.dn !== undefined ? sp.dn : sp.d); showRO(k, id); } lastTap = now;
  });
  k.addEventListener('pointermove', e => { if (!k.hasPointerCapture(e.pointerId)) return; const dy = y0 - e.clientY;
    setP(id, steps ? v0 + Math.round(dy / 22) / (steps - 1) : v0 + dy / (e.shiftKey ? 900 : 200)); showRO(k, id); });
  const up = () => hideRO(); k.addEventListener('pointerup', up); k.addEventListener('pointercancel', up);
  k.addEventListener('wheel', e => { e.preventDefault(); setP(id, steps ? val(id) - Math.sign(e.deltaY) / (steps - 1) : val(id) - e.deltaY / 2000); showRO(k, id); clearTimeout(k.t); k.t = setTimeout(hideRO, 700); }, {passive:false});
  return w;
}
function lz(id, label, color = 'yel', extra = ''){
  const b = el('button', 'lz l-' + color + (extra ? ' ' + extra : ''), label); b.dataset.id = id;
  const draw = v => b.classList.toggle('on', !!v); draw(val(id)); bind(id, draw);
  b.addEventListener('pointerdown', e => { learnPick(e, id); });
  b.addEventListener('click', e => { if (learn.on) return; press(id); });
  return b;
}
// buttons with desk behaviour beyond a plain toggle
function press(id){
  if (/^m\d+\.solo$|^s\d+\.solo$/.test(id) && safeMode){ const sid = id.replace('solo', 'safe'); setP(sid, !val(sid)); drawSafe(); return; }
  if (id === 'mon.mini1' || id === 'mon.mini2'){ if (miniSetup){ miniSetup = null; toast('Mini level setup done.'); drawBars(true); return; } const other = id === 'mon.mini1' ? 'mon.mini2' : 'mon.mini1'; if (!val(id)) setP(other, 0); setP(id, !val(id)); drawSub(); return; }
  if (miniSetup && (id === 'mon.dim' || id === 'mon.cut')){ const k = miniSetup === 1 ? 'mon.off1' : 'mon.off2'; setP(k, val(k) + (id === 'mon.cut' ? .5 : -.5) / 40); toast('Mini ' + miniSetup + ' level: ' + fmt(k)); return; }
  if (/^mon\.src/.test(id) && !val('mon.sum')){ for (const s of ['mon.srcA', 'mon.srcExt', 'mon.srcIj']) if (s !== id) setP(s, 0); setP(id, 1); return; }
  if (id === 'hp.fbA' || id === 'hp.fbB'){ const o = id === 'hp.fbA' ? 'hp.fbB' : 'hp.fbA'; if (!val(id)) setP(o, 0); }
  setP(id, !val(id));
}
function fader(id, mix){
  const sp = spec(id), f = el('div', 'fdr' + (mix ? ' mix' : '')), tr = el('div', 'tr'), sc = el('div', 'sc'), cap = el('div', 'fc'); f.dataset.id = id;
  // the scales printed on the desk (manual artwork): channel +10 to -30, Mix A 0 to -60
  const T = mix ? XL.FADA : XL.FAD;
  for (const [p, d] of T){ const s = el('span', '', d === -Infinity ? '∞' : d > 0 ? '+' + d : String(Math.abs(d))); s.style.top = ((1 - p) * 100) + '%'; sc.appendChild(s); }
  f.append(tr, sc, cap);
  const H = 150, draw = v => { cap.style.top = (10 + (1 - v) * H) + 'px'; }; draw(val(id)); bind(id, draw);
  let y0 = 0, v0 = 0, lastTap = 0;
  f.addEventListener('pointerdown', e => { if (learnPick(e, id)) return; f.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = val(id); showRO(f, id);
    const now = Date.now(); if (now - lastTap < 300) setP(id, sp.d); lastTap = now; });
  f.addEventListener('pointermove', e => { if (!f.hasPointerCapture(e.pointerId)) return; setP(id, v0 + (y0 - e.clientY) / (e.shiftKey ? H * 4 : H)); showRO(f, id); });
  f.addEventListener('pointerup', hideRO); f.addEventListener('pointercancel', hideRO);
  f.addEventListener('wheel', e => { e.preventDefault(); setP(id, val(id) - e.deltaY / 3000); showRO(f, id); clearTimeout(f.t); f.t = setTimeout(hideRO, 700); }, {passive:false});
  return f;
}
const kp = (...k) => { const d = el('div', 'pair'); d.append(...k); return d; };
const sec = (...k) => { const d = el('div', 'sec'); d.append(...k); return d; };
const nameBox = key => { const n = el('input', 'name'); n.dataset.key = key; n.value = S.names[key] || ''; n.placeholder = key.replace(/^m/, '').replace(/^s(\d)/, 'ST$1'); n.maxLength = 10; n.onchange = () => { S.names[key] = n.value; save(); }; return n; };

/* ================= the desk ================= */
const LEDS = {};   // meter key -> element(s)
function monoStrip(i){
  const m = 'm' + i + '.', s = el('div', 'strip');
  s.append(el('div', 'num', String(i)));
  if (i <= 8){ const v = el('div', 'sec vhdblk');
    v.append(knob(m + 'gain', 'red', 'GAIN'), knob(m + 'drive', 'black', 'DRIVE', 'sm'), kp(lz(m + 'vhd', 'VHD', 'red'), lz(m + 'hiz', 'HI-Z')), kp(lz(m + 'p48', '+48V', 'red'), lz(m + 'pad', 'PAD')));
    s.append(v);
  } else { const v = el('div', 'sec vhdblk'); v.append(el('div', 'kl', 'LINE')); s.append(v); }
  const led = el('span', 'led'); LEDS['m' + i] = led;
  s.append(sec(knob(m + 'trim', 'black', 'TRIM'), led), sec(kp(lz(m + 'daw', 'DAW', 'grn'), lz(m + 'phase', 'Ø')), kp(lz(m + 's500', '500', 'blu'), lz(m + 'ins', 'INS', 'blu')), lz(m + 'chop', 'CHOP POST', 'yel', 'wide')));
  s.append(sec(el('div', 'kl', 'ST CUE'), knob(m + 'cue', 'green', 'LEVEL', 'sm'), knob(m + 'cuepan', 'blue', 'PAN', 'sm'), kp(lz(m + 'cuepost', 'POST'), lz(m + 'alt', 'ALT'))));
  s.append(sec(knob(m + 'aux1', 'green', 'AUX 1', 'sm'), knob(m + 'aux2', 'green', 'AUX 2', 'sm')));
  s.append(sec(knob(m + 'pan', 'blue', 'PAN'), kp(lz(m + 'A', 'A', 'wht'), lz(m + 'B', 'B', 'wht')), kp(lz(m + 'C', 'C', 'wht'), lz(m + 'D', 'D', 'wht'))));
  s.append(sec(lz(m + 'cut', 'CUT', 'red', 'wide'), lz(m + 'solo', 'SOLO', 'yel', 'wide'), nameBox('m' + i)), fader(m + 'fader'));
  return s;
}
function stereoStrip(k){
  const p = 's' + k + '.', s = el('div', 'strip st');
  s.append(el('div', 'num', 'ST ' + k));
  const v = el('div', 'sec vhdblk'); v.append(el('div', 'kl', 'STEREO'), el('div', 'kl', 'CHANNEL')); s.append(v);
  const l1 = el('span', 'led'), l2 = el('span', 'led'); LEDS['s' + k + 'L'] = l1; LEDS['s' + k + 'R'] = l2;
  s.append(sec(knob(p + 'trim', 'black', 'TRIM'), kp(l1, l2)), sec(kp(lz(p + 'phL', 'ØL'), lz(p + 'phR', 'ØR')), lz(p + 'mono', 'MONO', 'yel', 'wide'), lz(p + 'chop', 'CHOP POST', 'yel', 'wide')));
  s.append(sec(el('div', 'kl', 'ST CUE'), knob(p + 'cue', 'green', 'LEVEL', 'sm'), knob(p + 'cuepan', 'blue', 'BAL', 'sm'), lz(p + 'cuepost', 'POST')));
  s.append(sec(knob(p + 'aux1', 'green', 'AUX 1', 'sm'), knob(p + 'aux2', 'green', 'AUX 2', 'sm')));
  s.append(sec(knob(p + 'pan', 'blue', 'BAL/PAN'), kp(lz(p + 'A', 'A', 'wht'), lz(p + 'B', 'B', 'wht')), kp(lz(p + 'C', 'C', 'wht'), lz(p + 'D', 'D', 'wht'))));
  s.append(sec(lz(p + 'cut', 'CUT', 'red', 'wide'), lz(p + 'solo', 'SOLO', 'yel', 'wide'), nameBox('s' + k)), fader(p + 'fader'));
  return s;
}
const blk = (title, ...k) => { const b = el('div', 'blk'); if (title) b.append(el('h4', '', title)); b.append(...k); return b; };
const g2 = (...k) => { const d = el('div', 'grid2'); d.append(...k); return d; };
const g4 = (...k) => { const d = el('div', 'grid4'); d.append(...k); return d; };
let safeMode = false, miniSetup = null;
function centre(){
  const c = el('div', 'centre');
  // stereo returns
  const c1 = el('div', 'cc');
  for (const k of [1, 2]){ const r = 'r' + k + '.';
    c1.append(blk('Stereo return ' + k, g2(knob(r + 'level', 'black', 'LEVEL'), knob(r + 'bal', 'blue', 'BAL')), g2(lz(r + 'cue', 'ST CUE'), lz(r + 'mono', 'MONO')), g4(lz(r + 'A', 'A', 'wht'), lz(r + 'B', 'B', 'wht'), lz(r + 'C', 'C', 'wht'), lz(r + 'D', 'D', 'wht')), lz(r + 'afl', 'AFL', 'grn', 'wide'))); }
  c1.append(blk('Master trims', g2(knob('cue.level', 'green', 'ST CUE'), kp(lz('cue.afl', 'AFL', 'grn'), lz('cue.toA', 'TO A'))),
    g2(knob('aux1.level', 'green', 'AUX 1'), kp(lz('aux1.pre', 'PRE'), lz('aux1.afl', 'AFL', 'grn'))), g2(knob('aux2.level', 'green', 'AUX 2'), kp(lz('aux2.pre', 'PRE'), lz('aux2.afl', 'AFL', 'grn')))));
  // mix B C D
  const c2 = el('div', 'cc');
  for (const X of 'BCD') c2.append(blk('Mix ' + X, g2(lz(X + '.s500', '500', 'blu'), lz(X + '.ins', 'INS', 'blu')), g2(lz(X + '.sum', 'SUM', 'blu'), lz(X + '.toA', 'TO A')), g2(knob(X + '.level', 'black', 'LEVEL'), lz(X + '.afl', 'AFL', 'grn'))));
  const solo = blk('Solo', g2(btnAct('CLEAR', 'red', clearSolos, '@soloclear'), btnAct('SAFE', 'yel', toggleSafe, '', 'safeBtn')), lz('solo.afl', 'AFL', 'grn', 'wide'));
  c2.append(solo);
  // monitoring
  const c3 = el('div', 'cc w');
  const miniBtn = n => { const b = lz('mon.mini' + n, 'MINI ' + n); let t = 0;
    b.addEventListener('pointerdown', () => { if (learn.on) return; t = setTimeout(() => { miniSetup = n; b.dataset.held = 1; toast('Mini ' + n + ' level setup: DIM -0.5 dB, CUT +0.5 dB, MINI ' + n + ' to finish.', 5000); drawBars(true); }, 3000); });
    b.addEventListener('pointerup', () => clearTimeout(t)); b.addEventListener('pointerleave', () => clearTimeout(t));
    b.addEventListener('click', e => { if (b.dataset.held){ delete b.dataset.held; e.stopImmediatePropagation(); } }, true);
    return b; };
  const sub = el('button', 'lz l-yel', 'SUB'); sub.id = 'subBtn'; sub.dataset.id = '@sub'; sub.addEventListener('pointerdown', e => learnPick(e, '@sub')); sub.onclick = () => { if (!learn.on) toggleSub(); };
  c3.append(blk('Monitor', knob('mon.level', 'black', 'MONITOR LEVEL', 'big'), g2(lz('mon.dim', 'DIM'), lz('mon.cut', 'CUT', 'red')), g2(knob('mon.dimamt', 'black', 'DIM'), knob('mon.afl', 'black', 'AFL')),
    el('h4', '', 'Mon select'), g2(lz('mon.mono', 'MONO'), miniBtn(2)), g2(miniBtn(1), sub),
    el('h4', '', 'Mon source'), g2(lz('mon.srcA', 'MIX A', 'grn'), lz('mon.srcExt', 'EXT', 'grn')), g2(lz('mon.srcIj', 'iJack', 'grn'), lz('mon.sum', 'SUM'))));
  c3.append(blk('H/P', knob('hp.level', 'black', 'LEVEL'), g2(lz('hp.fbA', 'F/B A'), lz('hp.fbB', 'F/B B'))));
  const c4 = el('div', 'cc');
  c4.append(blk('Foldback A (st cue)', knob('fbA.level', 'black', 'LEVEL'), g2(lz('fbA.mon', 'MON'), talkBtn('fbA.talk', 'TALK A', 3))), blk('Foldback B (aux 1)', knob('fbB.level', 'black', 'LEVEL'), g2(lz('fbB.mon', 'MON'), talkBtn('fbB.talk', 'TALK B', 5))),
    blk('Communications', g2(knob('talk.level', 'black', 'TALK'), knob('listen.level', 'black', 'LISTEN')), lz('listen.on', 'LISTEN', 'yel', 'wide')));
  // Mix A master
  const c5 = el('div', 'strip'); c5.style.width = '84px';
  c5.append(el('div', 'num', 'MIX A'), sec(lz('A.s500', '500', 'blu'), lz('A.ins', 'INS', 'blu'), lz('A.sum', 'SUM', 'blu'), lz('A.comp', 'COMP', 'red', 'wide')), fader('A.fader', true));
  c5.querySelector('.fdr').style.marginTop = 'auto';
  c.append(c1, c2, c3, c4, c5);
  return c;
}
function btnAct(label, color, fn, learnId, idAttr){ const b = el('button', 'lz l-' + color, label); if (idAttr) b.id = idAttr; if (learnId){ b.dataset.id = learnId; b.addEventListener('pointerdown', e => learnPick(e, learnId)); } b.onclick = () => { if (!learn.on) fn(); }; return b; }
// talkback: momentary or latching per DIP 3 / 5
function talkBtn(id, label, dip){
  const nb = el('button', 'lz l-red', label); nb.dataset.id = id; const draw = v => nb.classList.toggle('on', !!v); bind(id, draw);
  nb.addEventListener('pointerdown', e => { if (learnPick(e, id)) return; if (val('dip.' + dip)) setP(id, !val(id)); else { setP(id, 1); nb.setPointerCapture(e.pointerId); } });
  const rel = () => { if (!val('dip.' + dip)) setP(id, 0); }; nb.addEventListener('pointerup', rel); nb.addEventListener('pointercancel', rel);
  return nb;
}
function clearSolos(){ for (let i = 1; i <= 16; i++) setP('m' + i + '.solo', 0); for (let k = 1; k <= 4; k++) setP('s' + k + '.solo', 0); }
function toggleSafe(){ safeMode = !safeMode; $('#safeBtn').classList.toggle('on', safeMode); drawSafe(); toast(safeMode ? 'Solo Safe setup: press SOLO on the channels to protect, then SAFE.' : 'Solo Safe set.'); }
function drawSafe(){ for (let i = 1; i <= 16; i++) $$(`[data-id="m${i}.solo"]`).forEach(b => b.classList.toggle('flash', safeMode && !!val('m' + i + '.safe')));
  for (let k = 1; k <= 4; k++) $$(`[data-id="s${k}.solo"]`).forEach(b => b.classList.toggle('flash', safeMode && !!val('s' + k + '.safe'))); }
const curSet = () => val('mon.mini1') ? 'Mini1' : val('mon.mini2') ? 'Mini2' : 'Main';
// SUB remembers its state per speaker set (manual p.18)
function toggleSub(){ const id = 'mon.sub' + curSet(); setP(id, !val(id)); drawSub(); }
function drawSub(){ const b = $('#subBtn'); if (b) b.classList.toggle('on', !!val('mon.sub' + curSet())); }
['mon.subMain', 'mon.subMini1', 'mon.subMini2', 'mon.mini1', 'mon.mini2'].forEach(id => bind(id, drawSub));

/* ---------- penthouse: 500 slots above the channels, the bus compressor and the meter bridge ---------- */
const SLOTHD = {};
function slotHead(w, label){
  const d = el('div', 'slothd'); d.onclick = () => openModule(w); SLOTHD[w] = d; drawSlotHead(w); return d;
}
function drawSlotHead(w){
  const d = SLOTHD[w]; if (!d) return; const t = S.types[w], M = XL.MOD[t], n = w.replace('slot', '');
  const stolenBy = {9:'A', 10:'A', 11:'B', 12:'B', 13:'C', 14:'C', 15:'D', 16:'D'}[n];
  const st = stolenBy && val(stolenBy + '.s500');
  d.className = 'slothd' + (t === 'empty' ? ' empty' : '') + (st ? ' stolen' : '');
  d.innerHTML = ''; d.append(el('small', '', 'SLOT ' + n), el('div', '', M ? M.short : 'EMPTY'));
  if (M && (M.cat === 'dyn' || M.cat === 'pre')){ const gr = el('div', 'gr'); for (let j = 0; j < (M.cat === 'pre' ? 1 : 5); j++) gr.appendChild(el('i')); d.appendChild(gr); }
  const inq = M && M.params.find(q => q.n === 'in'); if (inq) d.appendChild(el('small', '', val(w + '.' + inq.j) ? 'IN' : 'out'));
  if (M && M.cat === 'pre') d.appendChild(el('small', '', val(w + '.ext') ? '◂ 500 SLOT IN' : '◂ ch send'));
  if (st) d.appendChild(el('small', '', '→ MIX ' + stolenBy));
}
for (let i = 1; i <= 16; i++) bind('slot' + i + '.ext', () => drawSlotHead('slot' + i));
['A', 'B', 'C', 'D'].forEach(X => bind(X + '.s500', () => { for (let i = 9; i <= 16; i++) drawSlotHead('slot' + i); }));
const BARS = [];   // [bus][side] -> segments
const SEG_DB = XL.SEG_DB;
function bridge(){
  const b = el('div', 'bridge'), bars = el('div', 'bars');
  ['A', 'B', 'C', 'D'].forEach((X, j) => { const bar = el('div', 'bar'), col = el('div', 'bcol'); BARS[j] = [];
    for (let s = 0; s < 2; s++){ const sg = el('div', 'seg'); BARS[j][s] = [];
      for (let q = SEG_DB.length - 1; q >= 0; q--){ const i = el('i'); sg.appendChild(i); BARS[j][s][q] = i; } col.appendChild(sg); }
    bar.append(el('b', '', 'MIX ' + X), col); bars.appendChild(bar); });
  const badge = el('div', 'badge'); const img = el('img'); img.src = 'art/afs-logo.jpg'; img.alt = 'Absolute Future Studios';
  badge.append(img, el('div', 'xl', 'XL-Desk'), el('div', 'mk', 'Absolute Future Studios'));
  const gc = el('div', 'fp w2'); gc.style.cursor = 'pointer'; gc.onclick = () => openModule('slot17');
  gc.append(el('h5', '', 'G-COMP · slots 17-18'), needle('gcNeedle'), el('div', 'kl', 'tap to open'));
  b.append(gc, bars, badge);
  return b;
}
function needle(id){
  const ns = 'http://www.w3.org/2000/svg', s = document.createElementNS(ns, 'svg'); s.setAttribute('viewBox', '0 0 120 66'); s.setAttribute('class', 'needle');
  s.innerHTML = '<rect x="2" y="2" width="116" height="62" rx="5" fill="#e9e3cf" stroke="#333"/>' + [0, 4, 8, 12, 16, 20].map(d => { const a = (-50 + d / 20 * 100) * Math.PI / 180, x = 60 + Math.sin(a) * 46, y = 60 - Math.cos(a) * 46; return `<text x="${x}" y="${y}" font-size="8" text-anchor="middle" fill="#222">${d}</text>`; }).join('') +
    '<text x="60" y="44" font-size="7" text-anchor="middle" fill="#444">dB COMPRESSION</text><line id="' + id + '" x1="60" y1="62" x2="60" y2="16" stroke="#111" stroke-width="1.5"/>';
  return s;
}
function buildDesk(){
  const d = $('#desk'), p = $('#pent'); d.innerHTML = ''; p.innerHTML = '';
  for (let i = 1; i <= 16; i++){ const ps = el('div', 'ps'); ps.appendChild(slotHead('slot' + i)); p.appendChild(ps); d.appendChild(monoStrip(i)); }
  for (let k = 1; k <= 4; k++) d.appendChild(stereoStrip(k));
  p.appendChild(bridge()); segColours();
  d.appendChild(centre());
  const j = $('#jump'); j.innerHTML = '';
  [['1-8', 0], ['9-16', 8], ['Stereo', 16], ['Returns', 20], ['Mix B-D', 21], ['Monitor', 22], ['Mix A', 24]].forEach(([lb, idx]) => {
    const b = el('button', '', lb); b.onclick = () => { const kids = d.children; const t = kids[Math.min(idx, kids.length - 1)];
      const tgt = idx >= 20 ? d.querySelector('.centre').children[idx - 20] : t; $('#deskWrap').scrollLeft = tgt.offsetLeft - 16; }; j.appendChild(b); });
  drawSub();
}

/* ---------- module faceplates ---------- */
// faceplates are drawn from each module's layout in spec.js, so a new module or revision needs no UI code
function faceplate(w){
  const t = S.types[w], M = XL.MOD[t], f = el('div', 'fp' + (M && M.w === 2 ? ' w2' : '') + (M ? '' : ' empty'));
  const n = w.replace(/^slot|^ins|^bins/, ''), kind = w.startsWith('slot') ? (w === 'slot17' ? 'SLOTS 17-18' : 'SLOT ' + n) : w.startsWith('ins') ? 'INS ' + n : 'MIX ' + n + ' INS';
  const h = el('h5'); h.append(el('span', '', M ? M.short : (t === 'loop' ? 'CABLE' : '—')), el('span', 'slotno', kind)); f.appendChild(h);
  const sel = el('select'); for (const o of XL.WHERE[w]) sel.appendChild(new Option(XL.TYPE_NAME[o] + (XL.MOD[o] ? ' (rev ' + XL.MOD[o].rev + ')' : ''), o)); sel.value = t;
  sel.onchange = () => setType(w, sel.value); f.appendChild(sel);
  if (!M){ f.appendChild(el('div', 'hint', t === 'empty' ? 'Nothing in this slot: the 500 insert returns silence, as on the desk.' : t === 'none' ? 'Nothing patched: pressing INS mutes, as on the desk.' : 'Send wired straight to return.')); return f; }
  const ps = Object.fromEntries(M.params.map(q => [q.n, q])), big = M.w === 2;
  for (const row of M.layout){
    const r = el('div', 'pair');
    for (const n of row){
      if (n === '#LED'){ r.appendChild(modLed(w, M)); continue; }
      if (n === '#NEEDLE'){ r.appendChild(needle('nd-' + w)); continue; }
      const q = ps[n], id = w + '.' + q.j;
      r.appendChild(q.k === 'btn' ? lz(id, q.lb, n === 'in' ? 'grn' : 'wht') : knob(id, q.c || 'grey', q.lb, big ? '' : 'sm'));
    }
    f.appendChild(r);
  }
  return f;
}
// module meters: a signal LED for preamps, gain-reduction / gate LEDs for dynamics
function modLed(w, M){
  if (M.cat === 'pre'){ const l = el('span', 'led'); MODLED[w] = {pre:l}; const d = el('div', 'ledcol'); d.append(l, el('span', '', 'SIGNAL')); return d; }
  const leds = el('div', 'ledcol'), L = [];
  [20, 14, 10, 6, 3].forEach(d => { const s = el('span'); const a = el('span', 'led'), b = el('span', 'led'); s.append(a, String(d), b); leds.appendChild(s); L.push([a, b, d]); });
  MODLED[w] = {dyn:L}; return leds;
}
const MODLED = {};
function setType(w, type){
  S.types[w] = type; post({t:'type', w, type});
  const d = XL.modDefaults(type); for (let j = 0; j < XL.MODMAX; j++) setP(w + '.' + j, d[j]);   // a freshly fitted module starts at its own defaults
  save(); drawSlotHead(w);
  if ($('#modDlg').open) openModule(w); if (S.view === 'rack') renderRack(); if (S.view === 'rear') renderRear();
}
function openModule(w){
  const b = $('#modBody'); b.innerHTML = '';
  const where = w.startsWith('slot') ? (w === 'slot17' ? '500 slots 17-18 (bus compressor space, over Mix A with COMP)' : '500 slot ' + w.slice(4) + (+w.slice(4) <= 16 ? ' (channel ' + w.slice(4) + "'s 500 insert" + (+w.slice(4) >= 9 ? ', or half of a Mix bus pair' : '') + ')' : ''))
    : w.startsWith('ins') ? 'Channel ' + w.slice(3) + ' INS (external insert)' : 'Mix ' + w.slice(4) + ' INS (stereo external insert)';
  b.append(el('h3', '', where));
  const wrap = el('div', ''); wrap.style.display = 'flex'; wrap.style.justifyContent = 'center'; wrap.appendChild(scoped('mod', () => faceplate(w))); b.appendChild(wrap);
  b.appendChild(el('p', 'hint', 'Drag knobs up and down (shift for fine), double-tap to reset. Switch knobs click between positions.'));
  if (!$('#modDlg').open) $('#modDlg').showModal();
}
$('#modClose').onclick = () => $('#modDlg').close();

/* ---------- meters ---------- */
let lastM = null, peakHold = {};
function onEngine(d){
  if (d.t === 'm'){ lastM = d; return; }
  if (d.t === 'rec') return recChunk(d.buf);
  if (d.t === 'recdone') return recDone();
}
const scale24 = () => !val('dip.1');   // DIP 1 up = +24 dBu, down = +18 dBu
function ledClass(dbu){ const s = scale24(); return dbu >= (s ? 21 : 16) ? 'r' : dbu >= (s ? 4 : 0) ? 'y' : dbu >= -24 ? 'g' : ''; }
/* Bargraph: 20-segment true-peak meter, 0 dB at the top = the selected peak level (+24 or +18 dBu, DIP 1), -48 at the
   bottom. Colours follow the desk (SOS): green below -24 dBu... up to nominal, yellow above nominal (+4 / 0 dBu), red within
   2 dB of the peak level. Instant attack; fall-back 20 dB in 1.7 s (IEC 60268-18 peak programme meter, est. for this desk). */
let barT = 0;
function segColours(){ const ref = scale24() ? 24 : 18, nom = scale24() ? 4 : 0;
  for (const bus of BARS) for (const side of bus) side.forEach((seg, q) => { const d = SEG_DB[q]; seg.className = d >= -2 ? 'r' : ref + d >= nom ? 'y' : 'g'; }); }
bind('dip.1', () => segColours());
function drawBars(force){
  const test = !!val('dip.11'), ref = scale24() ? 24 : 18, now = performance.now(), dt = Math.min(.1, (now - (barT || now)) / 1000); barT = now;
  for (let j = 0; j < 4; j++) for (let s = 0; s < 2; s++){
    const k = j + ':' + s, inst = lastM ? lastM.b[2 * j + s] - ref : -99, held = (peakHold[k] === undefined ? -99 : peakHold[k]) - dt * 20 / 1.7;
    const v = Math.max(inst, held); peakHold[k] = v;
    BARS[j][s].forEach((seg, q) => seg.classList.toggle('on', test || v >= SEG_DB[q] - .001));
  }
  if (miniSetup){ const off = XL.law(XL.P['mon.off1'].law, val(miniSetup === 1 ? 'mon.off1' : 'mon.off2')); BARS[0].forEach(side => side.forEach((seg, q) => seg.classList.toggle('on', q === clamp(Math.round(-off * 2) + 6, 0, 19)))); }
}
// channel LEDs hold for 100 ms so short peaks stay visible
const ledHold = {};
function setLed(key, e, dbu){ const now = performance.now(), c = ledClass(dbu), h = ledHold[key]; const rank = {'':0, g:1, y:2, r:3};
  if (!h || rank[c] >= rank[h.c] || now - h.t > 100){ ledHold[key] = {c, t:now}; e.className = 'led ' + c; } }
function frame(){
  requestAnimationFrame(frame);
  if (!lastM) return;
  const m = lastM;
  if (S.view === 'desk'){
    for (let i = 1; i <= 16; i++){ const e = LEDS['m' + i]; if (e) setLed('m' + i, e, m.m[i - 1]); }
    for (let k = 1; k <= 4; k++){ setLed('s' + k + 'L', LEDS['s' + k + 'L'], m.s[2 * k - 2]); setLed('s' + k + 'R', LEDS['s' + k + 'R'], m.s[2 * k - 1]); }
    drawBars();
    for (let i = 1; i <= 16; i++){ const w = 'slot' + i, d = SLOTHD[w], M = XL.MOD[S.types[w]]; if (!d || !M) continue; const g = m.mod[w] || [0, 0], dots = d.querySelectorAll('.gr i');
      if (M.cat === 'dyn'){ [3, 6, 10, 14, 20].forEach((t, j) => dots[j].className = g[0] >= t ? (j > 2 ? 'r' : 'y') : ''); if (g[1] > 3) dots[0].className = 'g'; }
      else if (M.cat === 'pre' && dots[0]) dots[0].className = ledClass(20 * Math.log10(g[0] + 1e-12) + 24); }
  }
  const setNeedle = (id, gr) => { const n = document.getElementById(id); if (!n) return; const a = (-50 + clamp(gr, 0, 20) / 20 * 100) * Math.PI / 180; n.setAttribute('x2', 60 + Math.sin(a) * 44); n.setAttribute('y2', 62 - Math.cos(a) * 44); };
  setNeedle('gcNeedle', m.gc); setNeedle('nd-slot17', m.gc);
  for (const w of ['binsA', 'binsB', 'binsC', 'binsD']) if (typeof m.mod[w] === 'number') setNeedle('nd-' + w, m.mod[w]);
  for (const w in MODLED){ const g = m.mod[w] || [0, 0], L = MODLED[w], first = L.pre || L.dyn[0][0]; if (!document.body.contains(first)){ delete MODLED[w]; continue; }
    if (L.pre) L.pre.className = 'led ' + ledClass(20 * Math.log10(g[0] + 1e-12) + 24);
    else L.dyn.forEach(([a, b, d]) => { a.className = 'led' + (g[1] >= d ? ' g' : ''); b.className = 'led' + (g[0] >= d ? (d >= 14 ? ' r' : ' y') : ''); }); }
  $('#tc').textContent = tcStr(m.pos);
  $('#tPlay').classList.toggle('on', m.play && !m.wind); $('#tRec').classList.toggle('on', m.rec);
  tpState.play = m.play; tpState.pos = m.pos; tpState.rec = m.rec;
}
requestAnimationFrame(frame);

/* ================= tape machine (the DAW returns) and print recorder ================= */
const tpState = {play:false, pos:0, rec:false};
const FPS = 30;
const tcStr = t => { t = Math.max(0, t); const h = Math.floor(t / 3600), mi = Math.floor(t / 60) % 60, s = Math.floor(t) % 60, f = Math.floor((t % 1) * FPS); return [h, mi, s, f].map(x => String(x).padStart(2, '0')).join(':'); };
const tp = {
  play(src){ post({t:'tp', cmd:'play'}); if (src !== 'mmc') mmcSend(0x02); },
  stop(src){ post({t:'tp', cmd:'stop'}); if (src !== 'mmc') mmcSend(0x01); },
  pause(src){ post({t:'tp', cmd:'pause'}); if (src !== 'mmc') mmcSend(0x09); },
  rec(src){ if (tpState.rec){ post({t:'tp', cmd:'recx'}); if (src !== 'mmc') mmcSend(0x07); return; } if (!S.rec.targets.length){ toast('Pick something to print on the Tape tab.'); return; } recStart(); post({t:'tp', cmd:'rec', targets:S.rec.targets}); if (src !== 'mmc') mmcSend(0x06); },
  locate(sec, src){ post({t:'tp', cmd:'locate', v:sec}); tpState.pos = sec; $('#tc').textContent = tcStr(sec); if (src !== 'mmc') mmcLocate(sec); },
  wind(speed, src){ post({t:'tp', cmd:'wind', v:speed}); if (src !== 'mmc') mmcSend(speed > 0 ? 0x04 : speed < 0 ? 0x05 : 0x01); }
};
$('#tPlay').onclick = () => { if (learn.on) return; tp.play(); };
$('#tStop').onclick = () => { if (learn.on) return; tp.stop(); };
$('#tRec').onclick = () => { if (learn.on) return; tp.rec(); };
$('#tRtz').onclick = () => { if (learn.on) return; tp.locate(S.loop.on ? S.loop.ls : 0); };
for (const [id, sp] of [['#tRew', -16], ['#tFf', 16]]){ const b = $(id);
  b.addEventListener('pointerdown', e => { if (learn.on) return; b.setPointerCapture(e.pointerId); tp.wind(sp); });
  const up = () => { if (learn.on) return; post({t:'tp', cmd:'wind', v:0}); if (tpState.play) mmcSend(0x02); else mmcSend(0x01); }; b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); }
$$('.tp [data-learn]').forEach(b => b.addEventListener('pointerdown', e => learnPick(e, b.dataset.learn), true));

// audio files live in IndexedDB so the DAW returns come back next time
const idb = () => new Promise((res, rej) => { const r = indexedDB.open('xldesk', 1); r.onupgradeneeded = () => r.result.createObjectStore('files'); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
async function idbDo(mode, fn){ const db = await idb(); return new Promise((res, rej) => { const tx = db.transaction('files', mode), st = tx.objectStore('files'), r = fn(st); tx.oncomplete = () => res(r && r.result); tx.onerror = () => rej(tx.error); }); }
async function addFiles(files){
  if (!ctx) await power(); if (!ctx) return;
  const used = new Set(S.tracks.map(t => t.dest));
  for (const f of files){
    try {
      const buf = await ctx.decodeAudioData(await f.arrayBuffer());
      const id = 't' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
      // mono files to the next free DAW input, stereo files to the next free stereo channel, then anywhere free
      const order = buf.numberOfChannels > 1 ? ['st1', 'st2', 'st3', 'st4', ...INPUTS.filter(x => /^daw/.test(x.id)).map(x => x.id)] : INPUTS.filter(x => /^daw/.test(x.id)).map(x => x.id);
      const dest = order.find(d => !used.has(d)) || ''; if (dest) used.add(dest);
      await idbDo('readwrite', st => st.put(f, id));
      S.tracks.push({id, name:f.name, dest, dur:buf.duration, chs:buf.numberOfChannels});
      sendTrack(id, buf);
    } catch(e){ toast('Could not read ' + f.name + ': ' + e.message, 4000); }
  }
  pushIO(); renderTape();
}
function sendTrack(id, buf){ const ch = []; for (let c = 0; c < Math.min(2, buf.numberOfChannels); c++) ch.push(buf.getChannelData(c).slice()); post({t:'track', id, ch}); }
async function loadTracks(){
  for (const t of S.tracks){ try { const f = await idbDo('readonly', st => st.get(t.id)); if (!f) continue; sendTrack(t.id, await ctx.decodeAudioData(await f.arrayBuffer())); } catch(e){ console.warn(e); } }
  pushIO();
}
async function removeTrack(id){ S.tracks = S.tracks.filter(t => t.id !== id); post({t:'untrack', id}); try { await idbDo('readwrite', st => st.delete(id)); } catch(e){} pushIO(); renderTape(); }

// print: the engine streams the chosen outputs while recording; each take becomes 24-bit WAVs
let take = null; const takes = [];
function recStart(){ take = {buf:{}, t0:tpState.pos, at:new Date()}; }
function recChunk(buf){ if (!take) recStart(); for (const k in buf){ const dst = take.buf[k] || (take.buf[k] = buf[k].map(() => [])); buf[k].forEach((blocks, j) => dst[j].push(...blocks)); } }
function recDone(){
  if (!take) return; const t = take; take = null; const files = [];
  for (const k in t.buf){ const chans = t.buf[k].map(bl => { const n = bl.reduce((a, b) => a + b.length, 0), o = new Float32Array(n); let p = 0; for (const b of bl){ o.set(b, p); p += b.length; } return o; });
    if (!chans[0].length) continue; files.push({name:'XL ' + OUT[k].lb + ' ' + t.at.toTimeString().slice(0, 8).replace(/:/g, '') + '.wav', blob:wav(chans, ctx.sampleRate), sec:chans[0].length / ctx.sampleRate}); }
  if (files.length){ takes.unshift({at:t.at, files}); toast('Printed ' + files.length + ' file' + (files.length > 1 ? 's' : '') + '. They are on the Tape tab.'); if (S.view === 'tape') renderTape(); }
}
function wav(chans, sr){
  const n = chans[0].length, nc = chans.length, ba = nc * 3, buf = new ArrayBuffer(44 + n * ba), v = new DataView(buf), w = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); v.setUint32(4, 36 + n * ba, true); w(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, nc, true); v.setUint32(24, sr, true); v.setUint32(28, sr * ba, true); v.setUint16(32, ba, true); v.setUint16(34, 24, true); w(36, 'data'); v.setUint32(40, n * ba, true);
  let o = 44; for (let i = 0; i < n; i++) for (let c = 0; c < nc; c++){ let x = Math.round(clamp(chans[c][i], -1, 1) * 8388607); if (x < 0) x += 16777216; v.setUint8(o, x & 255); v.setUint8(o + 1, x >> 8 & 255); v.setUint8(o + 2, x >> 16 & 255); o += 3; }
  return new Blob([buf], {type:'audio/wav'});
}
function renderTape(){
  const v = $('#v-tape'); v.innerHTML = '';
  const c1 = el('div', 'card'); c1.append(el('h2', '', 'Tape (DAW returns)'), el('p', 'hint', 'Load stems or mixes. Each file plays into a console input while the transport runs: mono files go to the DAW inputs (press DAW on the channel), stereo files to the stereo channels. Change any of them below. MMC from your DAW or controller drives this transport, and these buttons send MMC out when it is on (MIDI tab).'));
  const fi = el('input'); fi.type = 'file'; fi.accept = 'audio/*'; fi.multiple = true; fi.onchange = () => addFiles([...fi.files]);
  const lb = el('button', 'on', 'Load audio files…'); lb.onclick = () => fi.click(); c1.append(kp(lb));
  const list = el('div');
  for (const t of S.tracks){ const r = el('div', 'trk'); r.append(el('b', '', t.name + ' · ' + (t.chs > 1 ? 'stereo' : 'mono') + ' · ' + tcStr(t.dur || 0).slice(3, 8)));
    const s = el('select'); s.appendChild(new Option('(not patched)', '')); for (const x of INPUTS) s.appendChild(new Option(x.lb, x.id)); s.value = t.dest || '';
    s.onchange = () => { t.dest = s.value; pushIO(); }; const x = el('button', '', '✕'); x.onclick = () => removeTrack(t.id); r.append(s, x); list.appendChild(r); }
  if (!S.tracks.length) list.appendChild(el('p', 'hint', 'No files loaded.'));
  c1.appendChild(list);
  const lp = el('div', 'row'); const lc = el('label'); const cb = el('input'); cb.type = 'checkbox'; cb.checked = S.loop.on; lc.append(cb, 'Loop');
  const tin = (k) => { const i = el('input'); i.type = 'number'; i.step = '0.1'; i.min = 0; i.style.width = '80px'; i.value = S.loop[k]; i.onchange = () => { S.loop[k] = +i.value || 0; sendLoop(); }; return i; };
  const ls = tin('ls'), le = tin('le'), setIn = el('button', '', 'Set start here'), setOut = el('button', '', 'Set end here');
  setIn.onclick = () => { S.loop.ls = +tpState.pos.toFixed(2); ls.value = S.loop.ls; sendLoop(); }; setOut.onclick = () => { S.loop.le = +tpState.pos.toFixed(2); le.value = S.loop.le; sendLoop(); };
  cb.onchange = () => { S.loop.on = cb.checked; sendLoop(); };
  lp.append(lc, 'from', ls, 's to', le, 's', setIn, setOut); c1.appendChild(lp);
  const loc = el('div', 'row'); const li = el('input'); li.placeholder = '00:01:30:00'; li.style.width = '120px'; const lg = el('button', '', 'Locate');
  lg.onclick = () => { const p = li.value.split(':').map(Number); while (p.length < 4) p.unshift(0); tp.locate(p[0] * 3600 + p[1] * 60 + p[2] + (p[3] || 0) / FPS); }; loc.append(li, lg); c1.appendChild(loc);
  const c2 = el('div', 'card'); c2.append(el('h2', '', 'Print'), el('p', 'hint', '● records the outputs ticked here while the transport runs (like printing stems from the direct outs into your DAW). Mix A is your final mix. Files are 24-bit WAV at ' + (ctx ? ctx.sampleRate : 48000) + ' Hz.'));
  const tg = el('div', 'row');
  for (const [id, o] of Object.entries(OUT)){ if (id === 'monsel') continue; const l = el('label'); const c = el('input'); c.type = 'checkbox'; c.checked = S.rec.targets.includes(id);
    c.onchange = () => { S.rec.targets = c.checked ? [...new Set([...S.rec.targets, id])] : S.rec.targets.filter(x => x !== id); save(); }; l.append(c, o.lb); tg.appendChild(l); }
  c2.appendChild(tg);
  for (const tk of takes){ const d = el('div'); d.appendChild(el('h3', '', 'Take ' + tk.at.toLocaleTimeString()));
    for (const f of tk.files){ const a = el('a', '', f.name + ' (' + f.sec.toFixed(1) + ' s)'); a.href = URL.createObjectURL(f.blob); a.download = f.name; a.style.display = 'block'; a.style.color = '#7fe0a0'; d.appendChild(a); }
    c2.appendChild(d); }
  v.append(c1, c2);
}
function sendLoop(){ save(); post({t:'tp', cmd:'loop', on:S.loop.on, ls:S.loop.ls, le:S.loop.le}); }

/* ================= rear panel ================= */
async function renderRear(){
  let devs = []; try { devs = await navigator.mediaDevices.enumerateDevices(); } catch(e){}
  scoped('rear', () => buildRear(devs));
}
const PLUGIN_BUSES = {in:[['DAW 1-16 (main input)', 'the DAW inputs; on a stereo track you get DAW 1-2'], ['Mic 1-8 (VHD)', 'into the desk\'s VHD preamps, mic level'], ['Line 9-16', 'main inputs of channels 9-16'], ['Stereo In 1-4', 'the four stereo channels'], ['Returns 1-2 + Ext', 'stereo returns and the External monitor source'], ['iJack + Talk + Listen', 'iJack L/R, talkback mic, listen mic'], ['500 Slot In 1-16', 'straight into each 500 module, mic level']],
  out:[['Mix A (main output)', 'the mix'], ['Mix B, C, D', 'stems'], ['Monitor Main, Mini 1, Mini 2, Sub', 'speaker sets after the monitor section'], ['Headphones', ''], ['Foldback A, B', 'cue mixes with talkback'], ['Cue, Aux 1-2', 'send buses'], ['Channel outs 1-16', 'CHOP direct outs'], ['Stereo outs 1-4', ''], ['Return outs 1-2', ''], ['500 Slot outs 1-16', 'each module\'s output, for recording preamps directly'], ['Meter L/R + Listen out', '']]};
function buildRear(devs){
  const v = $('#v-rear'); v.innerHTML = '';
  if (HOST) return buildRearPlugin(v);
  const c0 = el('div', 'card'); c0.append(el('h2', '', 'Audio interface'));
  const si = el('select'); si.appendChild(new Option('Default input', '')); devs.filter(d => d.kind === 'audioinput').forEach(d => si.appendChild(new Option(d.label || 'Input ' + d.deviceId.slice(0, 6), d.deviceId))); si.value = S.dev.in;
  si.onchange = () => { S.dev.in = si.value; save(); if (S.dev.inOn) enableInput().catch(e => toast(e.message)); };
  const en = el('button', S.dev.inOn && inSrc ? 'on' : '', inSrc ? 'Input on (' + nIn + ' ch)' : 'Enable input'); en.onclick = () => enableInput().catch(e => toast('Input: ' + e.message, 4000));
  const so = el('select'); so.appendChild(new Option('Default output', '')); devs.filter(d => d.kind === 'audiooutput').forEach(d => so.appendChild(new Option(d.label || 'Output ' + d.deviceId.slice(0, 6), d.deviceId))); so.value = S.dev.out;
  so.onchange = () => { S.dev.out = so.value; save(); if (ctx && ctx.setSinkId) ctx.setSinkId(so.value).then(() => toast('Output set. Turn the desk off and on to pick up its channel count.')).catch(e => toast(e.message)); };
  c0.append(el('div', 'row'), kp(si, en), kp(so), el('p', 'hint', (ctx ? nOut + ' output channels open. ' : 'Press ⏻ first. ') + 'Chrome opens up to 32 output channels on a multichannel interface; phones usually give 2 in, 2 out.'));
  const cal = el('div', 'row');
  const cr = el('select'); [[24, '0 dBFS = +24 dBu (SSL default)'], [18, '0 dBFS = +18 dBu (EBU)'], [20, '0 dBFS = +20 dBu']].forEach(([x, l]) => cr.appendChild(new Option(l, x))); cr.value = S.cfg.convRef; cr.onchange = () => { S.cfg.convRef = +cr.value; post({t:'cfg', cfg:S.cfg}); save(); };
  const mr = el('input'); mr.type = 'number'; mr.value = S.cfg.micRef; mr.style.width = '70px'; mr.onchange = () => { S.cfg.micRef = +mr.value; post({t:'cfg', cfg:S.cfg}); save(); };
  cal.append(el('span', 'hint', 'Converters'), cr, el('span', 'hint', 'Mic inputs: 0 dBFS ='), mr, el('span', 'hint', 'dBu'));
  c0.append(cal, el('p', 'hint', 'Mic inputs treat your file or interface as the microphone itself: at -20 dBu per 0 dBFS, a -20 dBFS signal is a -40 dBu mic, so the VHD gain behaves like the real preamp.'));
  // inputs
  const c1 = el('div', 'card'); c1.append(el('h2', '', 'Inputs (rear D-subs)'));
  const grid = el('div', 'patch'); let grp = '';
  for (const x of INPUTS){
    if (x.grp !== grp){ grp = x.grp; const h = el('h3', '', grp); h.style.gridColumn = '1/-1'; grid.appendChild(h); }
    const s = el('select'); s.appendChild(new Option('—', 'none')); s.appendChild(new Option('1 kHz tone (-20 dBFS)', 'tone')); s.appendChild(new Option('Pink noise', 'pink'));
    for (let c = 0; c < Math.max(nIn, 2); c++) s.appendChild(new Option('Interface in ' + (c + 1) + (x.st ? '-' + (c + 2) : ''), 'i' + c));
    const t = S.tracks.find(t => t.dest === x.id);
    const cur = S.io[x.id]; s.value = cur ? (cur.k === 'iface' ? 'i' + cur.ch : cur.k) : 'none';
    s.onchange = () => { const q = s.value; S.io[x.id] = q[0] === 'i' && q !== 'none' ? {k:'iface', ch:+q.slice(1)} : {k:q}; pushIO(); };
    const d = el('div', 'row'); d.style.margin = 0; d.append(s); if (t) d.append(el('span', 'hint', 'file: ' + t.name + (cur && cur.k !== 'none' ? ' (this source wins)' : '')));
    grid.append(el('span', '', x.lb), d);
  }
  c1.appendChild(grid);
  // outputs
  const c2 = el('div', 'card'); c2.append(el('h2', '', 'Outputs'), el('p', 'hint', 'Patch console outputs to your interface channels. Stereo outputs use the channel and the one after it.'));
  const og = el('div', 'patch');
  for (let c = 0; c < nOut; c++){ const s = el('select'); s.appendChild(new Option('—', ''));
    for (const [id, o] of Object.entries(OUT)) s.appendChild(new Option(o.lb, id));
    const m = S.out.find(o => o.ch === c); s.value = m ? m.sig : '';
    s.onchange = () => { S.out = S.out.filter(o => o.ch !== c); if (s.value) S.out.push({sig:s.value, ch:c}); post({t:'out', map:S.out}); save(); };
    og.append(el('span', '', 'Out ' + (c + 1)), s); }
  c2.appendChild(og);
  // outboard (INS) and rear links, DIP switches
  buildRearCommon(v, [c0, c1, c2]);
}
function buildRearCommon(v, first = []){
  const cs = el('div', 'card'); cs.append(el('h2', '', '500 SLOT IN'), el('p', 'hint', "Lit: the slot's input comes from the rear 500 SLOT IN connector (a mic or line plugged straight into the module, the manual's way of using 500 preamps). Unlit: it comes from the channel's 500 send through the link cable, so the module is an insert. Either way the module's output returns to the channel when you press 500, and also appears on 500 SLOT OUT."));
  const sg = el('div', 'pair'); for (let i = 1; i <= 16; i++){ const b = lz('slot' + i + '.ext', 'SLOT ' + i, 'red'); b.style.height = 'auto'; b.style.padding = '4px 8px'; sg.appendChild(b); } cs.appendChild(sg);
  const c3 = el('div', 'card'); c3.append(el('h2', '', 'Outboard on the INS points'), el('p', 'hint', "Each mono channel's INS and each Mix bus INS is a real insert: pick what is patched there. 'Nothing patched' goes silent when INS is pressed, exactly like the desk with an empty return."));
  const ig = el('div', 'patch');
  for (const w of [...Array.from({length:16}, (_, i) => 'ins' + (i + 1)), 'binsA', 'binsB', 'binsC', 'binsD']){
    const s = el('select'); for (const o of XL.WHERE[w]) s.appendChild(new Option(XL.TYPE_NAME[o], o)); s.value = S.types[w]; s.onchange = () => setType(w, s.value);
    const e = el('button', '', 'Edit'); e.onclick = () => openModule(w);
    ig.append(el('span', '', w.startsWith('ins') ? 'Channel ' + w.slice(3) + ' INS' : 'Mix ' + w.slice(4) + ' INS'), kp(s, e)); }
  c3.appendChild(ig);
  const c4 = el('div', 'card'); c4.append(el('h2', '', '500 rack links'), el('p', 'hint', 'The four short D-sub cables SSL ships link 500 SEND to 500 SLOT IN and 500 SLOT OUT to 500 RETURN. Take them out (as you would to patch through a bay) and the channel 500 buttons return silence.'));
  c4.append(kp(lz('rear.unlink1', 'Links 1-2 removed (slots 1-8)', 'red', 'wide'), lz('rear.unlink2', 'Links 3-4 removed (slots 9-16)', 'red', 'wide')));
  c4.querySelectorAll('.lz').forEach(b => { b.style.height = 'auto'; b.style.padding = '4px 10px'; });
  const c5 = el('div', 'card'); c5.append(el('h2', '', 'Option DIP switches'), el('p', 'hint', 'As on the rear panel: left block 1-6, right block 7-12. Up is the first option.'));
  const dp = el('div', 'dip');
  XL.DIP.forEach(([fn, up, dn], j) => { const l = el('label'), b = el('button'); const id = 'dip.' + (j + 1); const draw = v => { b.classList.toggle('on', !!v); cap.textContent = (j + 1) + ': ' + fn + ' — ' + (v ? dn : up); };
    const cap = el('span'); bind(id, draw); b.onclick = () => setP(id, !val(id)); draw(val(id)); l.append(b, cap); dp.appendChild(l); });
  c5.append(dp, el('div', 'row'), lz('sys.noise', 'Analogue noise floor (spec: < -86 dBu channel, < -83 dBu mix)', 'grn', 'wide'));
  c5.querySelectorAll('.lz').forEach(b => { b.style.height = 'auto'; b.style.padding = '4px 10px'; });
  v.append(...first, cs, c3, c4, c5);
}

function buildRearPlugin(v){
  const c = el('div', 'card'); c.append(el('h2', '', 'Plugin buses (the rear panel)'), el('p', 'hint', 'Each D-sub on the desk is a bus on the plugin. Turn on the buses you need in your DAW (sidechain / extra inputs and outputs) and route tracks to them; the main input and output work on any stereo track.'));
  for (const [k, title] of [['in', 'Inputs'], ['out', 'Outputs']]){ const t = el('table'); t.innerHTML = '<tr><th>' + title + '</th><th></th></tr>'; for (const [a, b] of PLUGIN_BUSES[k]){ const tr = el('tr'); tr.append(el('td', '', a), el('td', '', b)); t.appendChild(tr); } c.appendChild(t); }
  const cal = el('div', 'row'), mr = el('input'); mr.type = 'number'; mr.value = S.cfg.micRef; mr.style.width = '70px'; mr.onchange = () => { S.cfg.micRef = +mr.value; post({t:'cfg', cfg:S.cfg}); };
  cal.append(el('span', 'hint', 'Mic-level inputs: 0 dBFS ='), mr, el('span', 'hint', 'dBu. Line inputs: 0 dBFS = +24 dBu, the desk\'s headroom.')); c.appendChild(cal);
  v.appendChild(c);
  buildRearCommon(v);
}
/* ================= rack view ================= */
function renderRack(){ scoped('rack', buildRack); }
function buildRack(){
  const v = $('#v-rack'); v.innerHTML = '';
  const c = el('div', 'card'); c.append(el('h2', '', '500-series rack (16 + 2 slots)'), el('p', 'hint', "Slots 1-16 sit above channels 1-16 and come in with the channel's 500 button. Mix A steals slots 9-10, Mix B 11-12, Mix C 13-14, Mix D 15-16 with their 500 buttons (odd slot = left, even = right). Slots 17-18 hold the bus compressor, inserted over Mix A with COMP."));
  const fill = el('div', 'row'); const ft = el('select'); ['vhdpre', 'eeq', 'edyn', 'empty'].forEach(t => ft.appendChild(new Option(XL.TYPE_NAME[t], t))); ft.value = S.rackType;
  const fb = el('button', '', 'Fill slots 1-16'); fb.onclick = () => { S.rackType = ft.value; for (let i = 1; i <= 16; i++) setType('slot' + i, ft.value); }; fill.append(ft, fb); c.appendChild(fill);
  const r = el('div', 'rack'); for (let i = 1; i <= 17; i++) r.appendChild(faceplate('slot' + i));
  v.append(c, r);
}

/* ================= views ================= */
const VIEWS = [['desk', 'Console'], ['rack', '500 Rack'], ['tape', 'Tape'], ['rear', 'Rear panel'], ['midi', 'MIDI'], ['help', 'Help']];
function show(vw){
  S.view = vw; save(); $$('.view').forEach(x => x.classList.toggle('on', x.id === 'v-' + vw)); $$('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.v === vw));
  if (vw === 'rack') renderRack(); if (vw === 'tape') renderTape(); if (vw === 'rear') renderRear(); if (vw === 'midi' && window.renderMidi) renderMidi(); if (vw === 'help') renderHelp();
}
for (const [id, lb] of VIEWS){ const b = el('button', '', lb); b.dataset.v = id; b.onclick = () => show(id); $('#tabs').appendChild(b); }
$('#power').onclick = () => power();
$('#learnBtn').onclick = () => { learn.on = !learn.on; learn.sel = null; $('#learnBtn').classList.toggle('on', learn.on); document.body.classList.toggle('learning', learn.on); $$('.learnsel').forEach(x => x.classList.remove('learnsel'));
  toast(learn.on ? 'MIDI Learn: tap any control on the desk, then move a control on your MIDI controller.' : 'Learn off.'); };
document.addEventListener('keydown', e => { if (e.target.tagName === 'INPUT') return; if (e.code === 'Space'){ e.preventDefault(); tpState.play ? tp.stop() : tp.play(); } });
scoped('desk', buildDesk); refreshAll(); show(S.view);
/* ---------- inside the VST3 ---------- */
function applyRemote(id, v){ if (!XL.P[id]) return; S.P[id] = v; (bound[id] || []).forEach(f => f(v)); for (const f of listeners) f(id, v, 'host'); if (id === 'dip.12') $('#prog').classList.toggle('on', !!v); }
if (HOST){
  document.body.classList.add('host'); $('#tabs button[data-v="tape"]').style.display = 'none'; if (S.view === 'tape') S.view = 'desk';
  // the transport drives your DAW over MMC
  tp.play = () => mmcSend(0x02); tp.stop = () => mmcSend(0x01); tp.pause = () => mmcSend(0x09); tp.rec = () => mmcSend(0x06);
  tp.locate = sec => mmcLocate(sec); tp.wind = sp => mmcSend(sp > 0 ? 0x04 : sp < 0 ? 0x05 : 0x01);
  HOST.on(d => {
    if (d.t === 'm') lastM = d;
    else if (d.t === 'p') applyRemote(d.id, d.v);
    else if (d.t === 'pp') for (const id in d.P) applyRemote(id, d.P[id]);
    else if (d.t === 'init'){
      S.P = Object.assign(XL.defaults(), d.P); S.types = Object.assign(XL.defTypes(), d.types); S.cfg = {micRef:d.micRef, convRef:d.convRef};
      try { Object.assign(S, JSON.parse(d.ui || '{}')); } catch(e){}
      scoped('desk', buildDesk); refreshAll(); show(S.view); if (window.midiInit) midiInit();
    }
    else if (d.t === 'type'){ S.types[d.w] = d.type; drawSlotHead(d.w); if (S.view === 'rack') renderRack(); }
    else if (d.t === 'learned'){ for (const k in S.learn) if (S.learn[k] === d.id) delete S.learn[k]; S.learn[d.key] = d.id; save(); $$('.learnsel').forEach(x => x.classList.remove('learnsel')); learn.sel = null; const [ch, cc] = d.key.split(':'); toast(ctlName(d.id) + ' → MIDI ch ' + (+ch + 1) + ' CC ' + cc + '.'); if (S.view === 'midi') renderMidi(); }
    else if (d.t === 'mmc') $('#midiStat').textContent = 'MMC ' + (typeof MMC_NAMES !== 'undefined' && MMC_NAMES[d.cmd] || d.cmd) + ' → DAW';
    else if (d.t === 'recalled') toast('Recalled program ' + (+d.n + 1) + '.');
    else if (d.t === 'midiDevices' && window.onHostMidi) onHostMidi(d);
  });
  HOST.send({t:'hello'});
}
if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(() => {});
