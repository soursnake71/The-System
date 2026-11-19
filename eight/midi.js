/* Omega 808: full MIDI. Loaded after app.js; talks to window.Eight (see CONTRACT.md).
   In: drum notes (GM map, learnable per voice; velocity at or over the accent threshold = 808 accent), every parameter on a CC
   (learnable, defaults below, NRPN 0/index), program change = basic rhythm 1-12 / fills 13-16, CC0 bank select = preset bank,
   transport CCs, clock (tempo follow, start / stop / continue, song position). Out: clock and start/stop, a note per voice on
   every step, and the trigger out (the Egyptian Lover trick): a note on steps where the trigger-out source fires. */
'use strict';
(function(){
/* Android app: native MIDI shaped like Web MIDI (same bridge as Omega Twelve's TwelveActivity) */
if (window.AndroidMidi && AndroidMidi.available()){
  const access = {inputs:new Map(), outputs:new Map(), onstatechange:null, sysexEnabled:false, native:true};
  const scan = () => {
    const seen = new Set();
    for (const d of JSON.parse(AndroidMidi.list())){
      seen.add(d.id);
      if (d.kind === 'input' && !access.inputs.has(d.id)){
        const port = {id:d.id, name:d.name, type:'input', _h:null};
        Object.defineProperty(port, 'onmidimessage', {get(){ return this._h; }, set(h){ this._h = h; if (h) AndroidMidi.listen(d.id); }});
        access.inputs.set(d.id, port);
      } else if (d.kind === 'output' && !access.outputs.has(d.id)) access.outputs.set(d.id, {id:d.id, name:d.name, type:'output', send:bytes => AndroidMidi.send(d.id, Array.from(bytes).join(','))});
    }
    for (const m of [access.inputs, access.outputs]) for (const id of [...m.keys()]) if (!seen.has(id)) m.delete(id);
  };
  window.__twMidi = (id, bytes) => { const p = access.inputs.get(id); if (p && p._h) p._h({data:Uint8Array.from(bytes), target:p}); };
  window.__twMidiChanged = () => { scan(); if (access.onstatechange) access.onstatechange({}); };
  scan();
  navigator.requestMIDIAccess = () => Promise.resolve(access);
}

const VOICES = ['BD','SD','LT','MT','HT','LC','MC','HC','RS','CL','CP','MA','CB','CY','OH','CH','TRIG'];
const DEFAULT_NOTES = {BD:[36,35], SD:[38,40], LT:[41,43], MT:[45,47], HT:[48,50], LC:[64], MC:[63], HC:[62], RS:[37], CL:[75], CP:[39], MA:[70], CB:[56], CY:[49,57], OH:[46], CH:[42,44], TRIG:[76]};
// a sensible default CC for each faceplate control and the main mods (all the rest are learnable)
const DEFAULT_CC = {7:'master', 12:'AC.level', 13:'BD.level', 14:'BD.tone', 15:'BD.decay', 16:'SD.level', 17:'SD.tone', 18:'SD.snappy',
  19:'LT.level', 20:'LT.tuning', 21:'MT.level', 22:'MT.tuning', 23:'HT.level', 24:'HT.tuning', 25:'RS.level', 26:'CP.level', 27:'CB.level',
  28:'CY.level', 29:'CY.tone', 30:'CY.decay', 31:'OH.level', 33:'OH.decay', 34:'CH.level', 35:'mod.BD.longDecay', 36:'mod.BD.tune',
  37:'mod.BD.drive', 39:'mod.TRIG.level', 40:'mod.TRIG.width', 41:'swing', 91:'rev.return', 92:'rev.time', 94:'dly.return', 95:'dly.feedback'};
const STORE = 'omega-808-midi';
const DEF = {inId:'all', outId:'', chIn:0, chOut:10, notes:DEFAULT_NOTES, cc:DEFAULT_CC, accThr:100, follow:true, clockOut:false, notesOut:false,
  trigOut:true, trigNote:76, trigCh:10, ccPlay:118, ccTap:119, ccVar:117, ccFill:116};
const M = Object.assign(JSON.parse(JSON.stringify(DEF)), {access:null, learnCc:null, learnNote:null, clock:[], bpm:null, ext:false, bank:0, nrpn:[0,0], last:''});
try { Object.assign(M, JSON.parse(localStorage.getItem(STORE) || '{}')); } catch (e) {}
function save(){ try { const o = {}; for (const k of Object.keys(DEF)) o[k] = M[k]; localStorage.setItem(STORE, JSON.stringify(o)); } catch (e) {} }

const E = () => window.Eight || {};
function params(){
  const e = E();
  if (Array.isArray(e.params) && e.params.length) return e.params;
  return Object.values(M.cc).map(k => ({k, min:0, max:1}));
}
const pByKey = k => params().find(p => p.k === k) || {k, min:0, max:1};
function post(msg){ const e = E(); if (e.node && e.node.port) e.node.port.postMessage(msg); }
function setParam(k, v01){
  const p = pByKey(k);
  let v = p.min + (p.max - p.min) * v01;
  if (p.step) v = Math.round(v / p.step) * p.step;
  if (p.opts || (p.max - p.min <= 3 && p.step !== undefined)) v = Math.round(v);
  const e = E();
  if (e.set) e.set(k, v); else post({t:'set', k, v});
}
function hit(v, vel){
  const acc = vel * 127 >= M.accThr ? 1 : 0, e = E();
  if (e.hit) e.hit(v, acc, vel); else post({t:'hit', v, acc, vel});
}
function play(on){ const e = E(); if (e.play) e.play(on); else post({t:'play', on}); }
function playing(){ const s = E().state; return !!(s && (s.playing || s.play)); }
function select(o){ const e = E(); if (e.selectRhythm) e.selectRhythm(o); else post(Object.assign({t:'select'}, o)); }
function noteVoice(n){ for (const v of VOICES) if ((M.notes[v] || []).includes(n)) return v; return null; }
function activity(s){ M.last = s; if (ui.dlg && ui.dlg.open) ui.last.textContent = s; }

/* ---------------- out ---------------- */
function outPort(){ return M.access && M.outId ? M.access.outputs.get(M.outId) : null; }
function out(bytes, delayMs){
  const o = outPort(); if (!o) return;
  if (!delayMs || delayMs <= 0) return o.send(bytes);
  if (M.access.native) setTimeout(() => o.send(bytes), delayMs); else o.send(bytes, performance.now() + delayMs);
}
function noteOut(ch, n, vel){ const c = (ch - 1) & 15; out([0x90 | c, n & 127, vel]); out([0x80 | c, n & 127, 0], 30); }
let lastStep = 0, stepGap = 0;
function onStep(m){
  if (!m || m.t !== 'step') return;
  const now = performance.now();
  if (lastStep) { const g = now - lastStep; if (g > 5 && g < 2000) stepGap = g; }
  lastStep = now;
  const vel = m.acc ? 127 : 96;
  if (M.notesOut) for (const v of m.hits || []) { const n = (M.notes[v] || [])[0]; if (n != null) noteOut(M.chOut || 10, n, vel); }
  if (M.trigOut && m.trig) noteOut(M.trigCh || 10, M.trigNote, 127);
  if (M.clockOut && !M.ext){
    const s = E().state || {}, bpm = s.tempo || s.bpm || 120, scale = (s.scale != null ? s.scale : m.scale) || 0;
    const per = [6, 3, 4, 8][scale] || 6;                                 // MIDI clocks per step for each pre-scale
    const dur = stepGap || 60000 / bpm / (24 / per);
    for (let i = 0; i < per; i++) out([0xF8], i * dur / per);
  }
}
let wasPlaying = false;
function transportOut(){
  const p = playing();
  if (p !== wasPlaying && M.clockOut && !M.ext) out([p ? 0xFA : 0xFC]);
  if (!p) lastStep = 0;
  wasPlaying = p;
}
setInterval(transportOut, 50);
function panic(){
  for (let c = 0; c < 16; c++){ out([0xB0 | c, 123, 0]); out([0xB0 | c, 120, 0]); }
  const e = E(); if (e.panic) e.panic(); play(false); activity('PANIC');
}

/* ---------------- in ---------------- */
function onMidi(ev){
  const d = ev.data, st = d[0], type = st & 0xf0, ch = (st & 15) + 1;
  if (st === 0xF8){
    if (!M.follow) return;
    const t = performance.now(); M.clock.push(t); if (M.clock.length > 49) M.clock.shift();
    if (M.clock.length >= 25 && (M.clock.length & 7) === 1){
      const per = (M.clock[M.clock.length - 1] - M.clock[0]) / (M.clock.length - 1), bpm = Math.round(60000 / (per * 24) * 10) / 10;
      if (bpm >= 30 && bpm <= 300 && Math.abs(bpm - (M.bpm || 0)) > 0.05){ M.bpm = bpm; M.ext = true; post({t:'clock', bpm}); }
    }
    return;
  }
  if (st === 0xFA || st === 0xFB || st === 0xFC){
    activity(st === 0xFC ? 'STOP' : st === 0xFA ? 'START' : 'CONTINUE');
    if (!M.follow) return;
    if (st === 0xFA) post({t:'pos', step:0});
    play(st !== 0xFC);
    if (st === 0xFC) M.clock = [];
    return;
  }
  if (st === 0xF2){ if (M.follow) post({t:'pos', step:(d[2] << 7) | d[1]}); return; }
  if (st >= 0xF0) return;
  if (M.chIn && ch !== M.chIn) return;
  if (type === 0x90 && d[2] > 0){
    const n = d[1];
    if (M.learnNote){
      for (const v of VOICES) M.notes[v] = (M.notes[v] || []).filter(x => x !== n);
      M.notes[M.learnNote] = [n]; activity(`${M.learnNote} ON NOTE ${n}`); M.learnNote = null; save(); paint(); return;
    }
    const v = noteVoice(n); activity(`NOTE ${n} VEL ${d[2]}${v ? ' ' + v : ''}`);
    if (v) hit(v, d[2] / 127);
  } else if (type === 0xC0){
    const p = d[1]; activity(`PROGRAM ${p + 1}`);
    if (p < 12) select({basic:p}); else if (p < 16) select({fill:p - 12});
  } else if (type === 0xB0){
    const cc = d[1], val = d[2];
    if (cc === 0){
      const banks = window.EIGHT_BANKS || [];
      if (val < banks.length){ M.bank = val; const e = E(); if (e.loadBank) e.loadBank(banks[val].bank); else post({t:'bank', bank:banks[val].bank}); activity('BANK ' + banks[val].name); }
      return;
    }
    if (cc === 120 || cc === 123){ panic(); return; }
    if (M.learnCc){
      for (const k of Object.keys(M.cc)) if (M.cc[k] === M.learnCc) delete M.cc[k];
      M.cc[cc] = M.learnCc; activity(`${M.learnCc} ON CC ${cc}`); M.learnCc = null; save(); paint(); return;
    }
    if (cc === M.ccPlay){ play(val > 63); return; }
    if (cc === M.ccTap){ if (val > 63) post({t:'tap'}); return; }
    if (cc === M.ccVar){ select({variation: val < 43 ? 'A' : val < 86 ? 'AB' : 'B'}); return; }
    if (cc === M.ccFill){ select({ifVar: val < 64 ? 'A' : 'B'}); return; }
    // NRPN: MSB 0, LSB = index into the parameter list; data entry 6 / 38 (14-bit)
    if (cc === 99){ M.nrpn[0] = val; return; } if (cc === 98){ M.nrpn[1] = val; return; }
    if (cc === 6 || cc === 38){
      if (cc === 6) M.nrpnMsb = val; const p = params()[M.nrpn[0] * 128 + M.nrpn[1]];
      if (p) setParam(p.k, cc === 6 ? val / 127 : ((M.nrpnMsb || 0) * 128 + val) / 16383);
      return;
    }
    const k = M.cc[cc];
    activity(`CC ${cc}=${val}${k ? ' ' + k : ''}`);
    if (k) setParam(k, val / 127);
  }
}
function hook(){ if (M.access) M.access.inputs.forEach(i => { i.onmidimessage = (M.inId === 'all' || M.inId === i.id) ? onMidi : null; }); paint(); }
function start(){
  const e = E();
  if (e.onStep && Array.isArray(e.onStep)) e.onStep.push(onStep);
  else if (e.node && e.node.port) e.node.port.addEventListener('message', ev => onStep(ev.data));
  if (!navigator.requestMIDIAccess) return;
  navigator.requestMIDIAccess({sysex:false}).then(a => { M.access = a; a.onstatechange = hook; hook(); }).catch(() => {});
}

/* ---------------- settings dialog ---------------- */
const ui = {};
function el(tag, attrs, kids){ const n = document.createElement(tag); Object.assign(n, attrs || {}); for (const k of kids || []) n.append(k); return n; }
function sel(opts, val, on){ const s = el('select'); for (const [v, t] of opts) s.append(el('option', {value:v, textContent:t, selected:String(v) === String(val)})); s.onchange = () => on(s.value); return s; }
function num(val, min, max, on){ const i = el('input', {type:'number', min, max, value:val}); i.style.width = '4.5em'; i.onchange = () => on(Math.max(min, Math.min(max, +i.value || 0))); return i; }
function chk(val, on){ const i = el('input', {type:'checkbox', checked:!!val}); i.onchange = () => on(i.checked); return i; }
const row = (label, ctl) => el('label', {className:'m8row'}, [el('span', {textContent:label}), ctl]);
function build(){
  const css = el('style', {textContent:`#m8dlg{max-width:680px;width:94vw;max-height:88vh;background:#1e1f21;color:#eee;border:1px solid #555;border-radius:10px;padding:14px;font:14px/1.3 system-ui,sans-serif}
#m8dlg h3{margin:10px 0 6px;color:#ff7a3c;font-size:15px}#m8dlg .m8row{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:3px 0}
#m8dlg select,#m8dlg input,#m8dlg button{font:inherit;background:#333;color:#eee;border:1px solid #666;border-radius:5px;padding:3px 6px}
#m8dlg .m8grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:4px}#m8dlg .m8grid button{text-align:left}
#m8dlg button.arm{background:#d4342c}#m8dlg .m8last{color:#9c9;font-family:monospace;min-height:1.2em}#m8dlg .m8list{max-height:30vh;overflow:auto}`});
  document.head.append(css);
  ui.dlg = el('dialog', {id:'m8dlg'}); document.body.append(ui.dlg);
}
function paint(){
  if (!ui.dlg || !ui.dlg.open) return;
  const d = ui.dlg; d.textContent = '';
  const ins = [['all', 'All inputs'], ...(M.access ? [...M.access.inputs.values()].map(i => [i.id, i.name]) : [])];
  const outs = [['', 'None'], ...(M.access ? [...M.access.outputs.values()].map(o => [o.id, o.name]) : [])];
  const chs = [[0, 'Omni'], ...Array.from({length:16}, (x, i) => [i + 1, 'Ch ' + (i + 1)])];
  const set = (k, f) => v => { M[k] = f ? f(v) : v; save(); if (k === 'inId') hook(); };
  ui.last = el('div', {className:'m8last', textContent:M.last || (M.access ? '' : 'No MIDI access in this browser')});
  d.append(el('h3', {textContent:'MIDI'}), ui.last,
    row('Input', sel(ins, M.inId, set('inId'))), row('Input channel', sel(chs, M.chIn, set('chIn', Number))),
    row('Output', sel(outs, M.outId, set('outId'))), row('Notes out channel', sel(chs.slice(1), M.chOut, set('chOut', Number))),
    row('Follow MIDI clock in', chk(M.follow, set('follow'))), row('Send MIDI clock and start / stop', chk(M.clockOut, set('clockOut'))),
    row('Send a note per voice on every step', chk(M.notesOut, set('notesOut'))), row('Accent at velocity', num(M.accThr, 1, 127, set('accThr'))),
    el('h3', {textContent:'Trigger out (Egyptian Lover trick)'}),
    row('Send a trigger note when the trigger-out source fires', chk(M.trigOut, set('trigOut'))),
    row('Trigger note', num(M.trigNote, 0, 127, set('trigNote'))), row('Trigger channel', sel(chs.slice(1), M.trigCh, set('trigCh', Number))),
    el('h3', {textContent:'Transport CCs'}),
    row('Start / stop', num(M.ccPlay, 1, 127, set('ccPlay'))), row('Fill-in trigger (tap)', num(M.ccTap, 1, 127, set('ccTap'))),
    row('Basic variation A / AB / B', num(M.ccVar, 1, 127, set('ccVar'))), row('Intro/fill variation A / B', num(M.ccFill, 1, 127, set('ccFill'))));
  d.append(el('h3', {textContent:'Notes (tap, then play a note)'}));
  const g = el('div', {className:'m8grid'});
  for (const v of VOICES){ const b = el('button', {textContent:`${v}: ${(M.notes[v] || []).join(', ') || '-'}`, className:M.learnNote === v ? 'arm' : ''}); b.onclick = () => { M.learnNote = M.learnNote === v ? null : v; M.learnCc = null; paint(); }; g.append(b); }
  d.append(g, el('h3', {textContent:'Controls (tap, then move a knob on your controller)'}));
  const byK = {}; for (const [c, k] of Object.entries(M.cc)) byK[k] = c;
  const l = el('div', {className:'m8grid m8list'});
  for (const p of params()){ const b = el('button', {textContent:`${p.label || p.k}: ${byK[p.k] ? 'CC ' + byK[p.k] : '-'}`, className:M.learnCc === p.k ? 'arm' : ''}); b.onclick = () => { M.learnCc = M.learnCc === p.k ? null : p.k; M.learnNote = null; paint(); }; l.append(b); }
  d.append(l);
  const bar = el('div', {className:'m8row'});
  const btn = (t, f) => { const b = el('button', {textContent:t}); b.onclick = f; return b; };
  bar.append(btn('Panic', panic), btn('Default map', () => { M.cc = Object.assign({}, DEFAULT_CC); M.notes = JSON.parse(JSON.stringify(DEFAULT_NOTES)); save(); paint(); }), btn('Close', () => d.close()));
  d.append(bar);
}
function openSettings(){ if (!ui.dlg) build(); if (!ui.dlg.open) ui.dlg.showModal(); paint(); }

window.EightMidi = {openSettings, panic, learn:k => { M.learnCc = k; M.learnNote = null; activity('MOVE A CONTROL FOR ' + k); }, learnNote:v => { M.learnNote = v; },
  state:M, onStep, onMidi, VOICES, DEFAULT_NOTES, DEFAULT_CC};
if (document.readyState === 'loading') addEventListener('DOMContentLoaded', start); else start();
})();
