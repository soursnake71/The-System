/* Omega Solina: the page. Panel, RX5-style mixer, effects, sequencer, MIDI. The sound is made by engine.js in an
   AudioWorklet (worklet.js); this file only edits state and moves messages. */
'use strict';
const {PARAMS, PID, CH, CH_NAMES, CH_TABS, defaults, randomPatch} = SolinaDSP;
const SQ = SolinaSeq;
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const STORE = 'omega-solina-v1';
const PI = id => PARAMS[PID[id]];

/* ---------------- state ---------------- */
const S = {p:defaults(), song:SQ.newSong(), preset:'Classic · Stock Ensemble', user:{}, page:'Panel', oct:0, len:96, vel:100, view:0, low:48, quant:true, click:false, clip:null,
  midi:{inId:'all', inCh:-1, outId:'', outCh:0, thru:false, clkOut:false, clkIn:true, pcIn:true, pcOut:false}, cc:null};
try { const o = JSON.parse(localStorage.getItem(STORE) || 'null'); if (o){ Object.assign(S.p, o.p || {}); if (o.song && o.song.pats) S.song = o.song; for (const k of ['preset', 'user', 'oct', 'len', 'vel', 'low', 'quant', 'click', 'page']) if (o[k] != null) S[k] = o[k]; Object.assign(S.midi, o.midi || {}); if (o.cc) S.cc = o.cc; } } catch (e) {}
const defaultCC = () => { const m = {}; for (const p of PARAMS) if (p.cc >= 0) m[p.cc] = p.id; return m; };
if (!S.cc) S.cc = defaultCC();
let saveT = 0;
function save(){ clearTimeout(saveT); saveT = setTimeout(() => { try { localStorage.setItem(STORE, JSON.stringify({p:S.p, song:S.song, preset:S.preset, user:S.user, oct:S.oct, len:S.len, vel:S.vel, low:S.low, quant:S.quant, click:S.click, page:S.page, midi:S.midi, cc:S.cc})); } catch (e) {} }, 300); }

/* ---------------- audio ---------------- */
let ctx = null, node = null;
const post = m => { if (node) node.port.postMessage(m); };
async function startAudio(){
  if (ctx){ if (ctx.state !== 'running') ctx.resume(); return; }
  ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
  for (const f of ['engine.js', 'seq.js', 'worklet.js']) await ctx.audioWorklet.addModule(f);
  node = new AudioWorkletNode(ctx, 'omega-solina', {numberOfInputs:0, outputChannelCount:[2]});
  node.connect(ctx.destination);
  node.port.onmessage = e => fromWorklet(e.data);
  post({type:'params', p:S.p}); sendSong(); post({type:'click', on:S.click}); post({type:'clockOut', on:S.midi.clkOut});
}
$('#go').addEventListener('click', async () => { try { await startAudio(); $('#start').remove(); } catch (e) { toast('Audio could not start: ' + e.message); } });
document.addEventListener('pointerdown', () => { if (ctx && ctx.state !== 'running') ctx.resume(); }, true);

let pos = {playing:false, pat:0, pos:0, tick:0};
function fromWorklet(d){
  if (d.type === 'pos'){ pos = d; $('#play').classList.toggle('on', d.playing); if (S.page === 'Seq') drawRoll(); markPlayingPattern(); }
  else if (d.type === 'midi'){
    const st = d.d[0];
    if (st === 0x90 || st === 0x80){ if (d.seq) seqKey(d.d[1], st === 0x90); midiSend([st | S.midi.outCh, d.d[1], d.d[2]]); }
    else if (S.midi.clkOut) midiSend(d.d);
  }
  else if (d.type === 'recNote'){ const p = S.song.pats[d.pat]; p.notes.push(d.note); sendSong(); save(); if (S.page === 'Seq') drawRoll(); renderPatBtns(); }
}
function sendSong(){ post({type:'song', song:S.song}); }

/* ---------------- formatting and the LCD ---------------- */
function fmt(id, v){
  const p = PI(id);
  if (p.choices.length) return p.choices[Math.round(v)] || '';
  if (p.step === 1 && p.max === 1 && !p.unit) return v ? 'ON' : 'OFF';
  if (/Pan$/.test(id)) return Math.abs(v) < .02 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v) * 100);
  switch (p.unit){
    case '%': return Math.round(v * 100) + '%';
    case 'Hz': return v.toFixed(2) + ' Hz';
    case 'ms': return Math.round(v) + ' ms';
    case 's': return v.toFixed(1) + ' s';
    case 'st': return (v > 0 ? '+' : '') + Math.round(v) + ' st';
    case 'ct': return (v > 0 ? '+' : '') + Math.round(v) + ' ct';
    case 'dB': return (v > 0 ? '+' : '') + v.toFixed(1) + ' dB';
  }
  return String(Math.round(v * 100) / 100);
}
let lcdT = 0;
function lcd(s){ $('#lcd').textContent = s; clearTimeout(lcdT); lcdT = setTimeout(() => $('#lcd').textContent = S.preset.replace(' · ', ': '), 2500); }
function toast(s){ const t = $('#toast'); t.textContent = s; t.classList.add('show'); clearTimeout(t._t); t._t = setTimeout(() => t.classList.remove('show'), 2600); }

/* ---------------- parameters and controls ---------------- */
const ctrls = {};       // id -> [render functions]
let learnOn = false, learnTarget = null;
function setParam(id, v, opt){
  const p = PI(id); if (!p) return;
  v = Math.max(p.min, Math.min(p.max, v)); if (p.step === 1) v = Math.round(v);
  if (S.p[id] === v && !(opt && opt.force)) return;
  S.p[id] = v; post({type:'param', id, v});
  (ctrls[id] || []).forEach(f => f());
  if (!(opt && opt.quiet)) lcd(p.name + ': ' + (id === 'dlTime' && S.p.dlSync ? fmt('dlSync', S.p.dlSync) : fmt(id, v)));
  if (!(opt && opt.fromMidi) && S.midi.thru){ const cc = ccOf(id); if (cc >= 0) midiSend([0xB0 | S.midi.outCh, cc, Math.round((v - p.min) / (p.max - p.min) * 127)]); }
  if (/^(violin|viola|trumpet|horn|cello|contra|..Mut|..Sol)$/.test(id)) renderMixerNames();
  save();
}
const ccOf = id => { for (const k in S.cc) if (S.cc[k] === id) return +k; return -1; };
function learnHook(el, id){
  el.addEventListener('pointerdown', e => { if (!learnOn) return; e.stopImmediatePropagation(); e.preventDefault(); learnTarget = id; $$('.learn').forEach(x => x.classList.remove('learn')); el.classList.add('learn'); lcd('Learn: move a control for ' + PI(id).name); }, true);
}
function markMapped(){ for (const id in ctrls) (ctrls[id].els || []).forEach(el => el.classList.toggle('mapped', ccOf(id) >= 0)); }
function reg(id, el, f){ (ctrls[id] = ctrls[id] || []).push(f); (ctrls[id].els = ctrls[id].els || []).push(el); learnHook(el, id); f(); }

const ARC = (a0, a1, r) => { const p = a => [20 + r * Math.sin(a), 20 - r * Math.cos(a)]; const [x0, y0] = p(a0), [x1, y1] = p(a1); return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${Math.abs(a1 - a0) > Math.PI ? 1 : 0} ${a1 > a0 ? 1 : 0} ${x1.toFixed(2)} ${y1.toFixed(2)}`; };
function knob(parent, id, label, opt){
  opt = opt || {};
  const p = PI(id), el = document.createElement('div'); el.className = 'knob';
  el.innerHTML = `<svg viewBox="0 0 40 40"><path d="${ARC(-2.36, 2.36, 16)}" fill="none" stroke="#3a3632" stroke-width="3.2" stroke-linecap="round"/><path class="a" fill="none" stroke="${opt.color || '#ff7a1a'}" stroke-width="3.2" stroke-linecap="round"/>
    <circle cx="20" cy="20" r="11.5" fill="url(#kg)" stroke="#000" stroke-width="1"/><line class="n" x1="20" y1="20" x2="20" y2="10" stroke="#efe6d2" stroke-width="2.2" stroke-linecap="round"/></svg>
    <div class="lb">${label || p.name}</div><div class="vl"></div>`;
  parent.appendChild(el);
  const bip = p.min < 0 && p.max > 0, a = el.querySelector('.a'), nd = el.querySelector('.n'), vl = el.querySelector('.vl');
  const norm = v => opt.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min);
  const denorm = t => opt.log ? p.min * Math.pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
  const render = () => { const v = S.p[id], t = Math.max(0, Math.min(1, norm(v))), ang = -2.36 + t * 4.72, c0 = bip ? 0 : -2.36;
    a.setAttribute('d', Math.abs(ang - c0) < .01 ? '' : ARC(Math.min(c0, ang), Math.max(c0, ang), 16));
    nd.setAttribute('x2', (20 + 9 * Math.sin(ang)).toFixed(2)); nd.setAttribute('y2', (20 - 9 * Math.cos(ang)).toFixed(2));
    vl.textContent = id === 'dlTime' && S.p.dlSync ? fmt('dlSync', S.p.dlSync) : fmt(id, v); };
  let y0 = 0, t0 = 0, last = 0, moved = false;
  el.addEventListener('pointerdown', e => { e.preventDefault(); el.setPointerCapture(e.pointerId); y0 = e.clientY; t0 = norm(S.p[id]); moved = false;
    const now = performance.now(); if (now - last < 320){ setParam(id, p.def); last = 0; return; } last = now; });
  el.addEventListener('pointermove', e => { if (!el.hasPointerCapture(e.pointerId)) return; const dy = y0 - e.clientY; if (Math.abs(dy) > 2) moved = true;
    setParam(id, denorm(Math.max(0, Math.min(1, t0 + dy / (p.step === 1 && p.max - p.min < 8 ? 90 : 180))))); });
  el.addEventListener('contextmenu', e => e.preventDefault());
  reg(id, el, render);
  return el;
}
function sw(el, id, onText){
  const render = () => el.classList.toggle('on', !!S.p[id]);
  el.addEventListener('click', () => { if (learnOn) return; setParam(id, S.p[id] ? 0 : 1); });
  reg(id, el, render);
}
function fader(parent, id){
  const el = document.createElement('div'); el.className = 'fader'; el.innerHTML = '<div class="slot"></div><div class="ticks"></div><div class="cap"></div>'; parent.appendChild(el);
  const cap = el.querySelector('.cap'), p = PI(id);
  const render = () => { const t = (S.p[id] - p.min) / (p.max - p.min); cap.style.top = `calc(6% + ${(1 - t).toFixed(4)} * (88% - 1.5em))`; };
  const at = e => { const r = el.getBoundingClientRect(), h = r.height * .88; return 1 - Math.max(0, Math.min(1, (e.clientY - r.top - r.height * .06 - 12) / Math.max(1, h - 24))); };
  let last = 0;
  el.addEventListener('pointerdown', e => { e.preventDefault(); el.setPointerCapture(e.pointerId); const now = performance.now(); if (now - last < 320){ setParam(id, p.def); return; } last = now; setParam(id, p.min + at(e) * (p.max - p.min)); });
  el.addEventListener('pointermove', e => { if (el.hasPointerCapture(e.pointerId)) setParam(id, p.min + at(e) * (p.max - p.min)); });
  reg(id, el, render);
}
// the knob gradient, once
document.body.insertAdjacentHTML('beforeend', '<svg width="0" height="0" style="position:absolute"><defs><radialGradient id="kg" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#4a4743"/><stop offset="1" stop-color="#151413"/></radialGradient></defs></svg>');

/* ---------------- panel page ---------------- */
const TAB_ORDER = [['viola', 'VIOLA', ''], ['violin', 'VIOLIN', ''], ['trumpet', 'TRUMPET', 'orange'], ['horn', 'HORN', 'orange'], ['cello', 'CELLO', ''], ['contra', 'CONTRA BASS', '']];
for (const [id, lb, cl] of TAB_ORDER){
  const b = document.createElement('button'); b.className = 'tab ' + cl; b.textContent = lb; $('#tabRow').appendChild(b);
  b.addEventListener('click', () => { if (!learnOn) setParam(id, S.p[id] ? 0 : 1); });
  reg(id, b, () => b.classList.toggle('on', !!S.p[id]));
}
knob($('#kPanel'), 'cresc', 'CRESCENDO'); knob($('#kPanel'), 'susLen', 'SUSTAIN LENGTH'); knob($('#kPanel'), 'bassVol', 'BASS VOLUME'); knob($('#kPanel'), 'tune', 'TUNING'); knob($('#kPanel'), 'volume', 'VOLUME');
$$('[data-sw]').forEach(b => sw(b, b.dataset.sw));
knob($('#kEns'), 'chorusHz', 'Chorus'); knob($('#kEns'), 'tremHz', 'Tremolo Hz'); knob($('#kEns'), 'depth', 'Depth'); knob($('#kEns'), 'trem', 'Tremolo');
knob($('#kPlay'), 'velSens', 'Velocity'); knob($('#kPlay'), 'bendRange', 'Bend'); knob($('#kPlay'), 'modDest', 'Mod wheel');

/* ---------------- mixer page (RX5-style strips) ---------------- */
const MIX_ORDER = [1, 0, 2, 3, 4, 5];   // viola, violin, trumpet, horn, cello, contra bass (as on the panel)
for (const c of MIX_ORDER){
  const k = CH[c], st = document.createElement('div'); st.className = 'strip';
  st.innerHTML = `<div class="nm" data-c="${c}">${CH_NAMES[c].toUpperCase()}</div><div class="body"><div class="kg"></div></div><div class="bt"><button class="ens">ENS</button><button class="mute">M</button><button class="solo">S</button></div>`;
  $('#pgMixer').appendChild(st);
  const kg = st.querySelector('.kg');
  knob(kg, k + 'Pan', 'Pan', {color:'#5aa0e0'}); knob(kg, k + 'Pit', 'Pitch', {color:'#5aa0e0'}); knob(kg, k + 'Atk', 'Attack'); knob(kg, k + 'Dec', 'Decay');
  knob(kg, k + 'Ton', 'Tone'); knob(kg, k + 'Ech', 'Echo', {color:'#bfe08a'}); knob(kg, k + 'Rev', 'Reverb', {color:'#bfe08a'});
  const lvl = document.createElement('div'); lvl.className = 'knob'; lvl.innerHTML = '<div class="lb">LEVEL</div><div class="vl"></div>'; kg.appendChild(lvl);
  reg(k + 'Lvl', lvl, () => lvl.querySelector('.vl').textContent = fmt(k + 'Lvl', S.p[k + 'Lvl']));
  fader(st.querySelector('.body'), k + 'Lvl');
  kg.querySelectorAll('.knob').forEach(kn => { const tx = document.createElement('div'); tx.className = 'tx'; kn.querySelectorAll('.lb,.vl').forEach(x => tx.appendChild(x)); kn.appendChild(tx); });
  sw(st.querySelector('.ens'), k + 'Ens'); sw(st.querySelector('.mute'), k + 'Mut'); sw(st.querySelector('.solo'), k + 'Sol');
  st.querySelector('.nm').addEventListener('click', () => { if (!learnOn) setParam(CH_TABS[c], S.p[CH_TABS[c]] ? 0 : 1); });
}
function renderMixerNames(){ $$('.strip .nm').forEach(el => { const c = +el.dataset.c; el.classList.toggle('off', !S.p[CH_TABS[c]]); }); }

/* ---------------- effects page ---------------- */
knob($('#kPh'), 'phRate', 'Rate', {log:true}); knob($('#kPh'), 'phDepth', 'Depth'); knob($('#kPh'), 'phFb', 'Color'); knob($('#kPh'), 'phMix', 'Mix');
knob($('#kDl'), 'dlTime', 'Time', {log:true}); knob($('#kDl'), 'dlSync', 'Sync'); knob($('#kDl'), 'dlFb', 'Feedback'); knob($('#kDl'), 'dlTone', 'Tone'); knob($('#kDl'), 'dlRet', 'Return');
(ctrls.dlSync = ctrls.dlSync || []).push(() => (ctrls.dlTime || []).forEach(f => f()));
knob($('#kRv'), 'rvSize', 'Size'); knob($('#kRv'), 'rvDecay', 'Decay', {log:true}); knob($('#kRv'), 'rvDamp', 'Damping'); knob($('#kRv'), 'rvPre', 'Pre-delay'); knob($('#kRv'), 'rvRet', 'Return');
knob($('#kMs'), 'eqLow', 'Bass'); knob($('#kMs'), 'eqMid', 'Mid'); knob($('#kMs'), 'eqHigh', 'Treble'); knob($('#kMs'), 'drive', 'Drive'); knob($('#kMs'), 'master', 'Master');

/* ---------------- pages ---------------- */
function showPage(pg){
  S.page = pg; $$('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.pg === pg));
  $$('.page').forEach(x => x.classList.toggle('show', x.id === 'pg' + pg));
  if (pg === 'Seq'){ renderSeq(); }
  if (pg === 'Midi') renderMidiPage();
  save();
}
$$('#tabs button').forEach(b => b.addEventListener('click', () => showPage(b.dataset.pg)));

/* ---------------- presets ---------------- */
const FACT = SOLINA_PRESETS;
function presetValues(name){
  if (name.startsWith('User · ')){ const u = S.user[name.slice(7)]; return u ? Object.assign(defaults(), u) : null; }
  const f = FACT.find(x => x.name === name); return f ? Object.assign(defaults(), f.p) : null;
}
function allPresetNames(){ return FACT.map(x => x.name).concat(Object.keys(S.user).sort().map(n => 'User · ' + n)); }
function renderPresetMenu(){
  const groups = new Map();
  for (const n of allPresetNames()){ const g = n.split(' · ')[0]; if (!groups.has(g)) groups.set(g, []); groups.get(g).push(n); }
  let h = S.preset === 'Random' ? '<option value="Random">Random sound</option>' : '';
  for (const [g, ns] of groups) h += `<optgroup label="${g}">` + ns.map(n => `<option value="${n.replace(/"/g, '&quot;')}">${n.split(' · ').slice(1).join(' · ')}</option>`).join('') + '</optgroup>';
  $('#preset').innerHTML = h; $('#preset').value = S.preset;
}
function applyValues(o, name){
  // playing settings belong to the player, not the sound
  const keep = {velSens:S.p.velSens, bendRange:S.p.bendRange, modDest:S.p.modDest};
  Object.assign(S.p, o, keep); post({type:'params', p:S.p});
  for (const id in ctrls) ctrls[id].forEach(f => f());
  renderMixerNames();
  S.preset = name; renderPresetMenu(); lcd(name.replace(' · ', ': ')); save();
}
function loadPreset(name, fromMidi){
  const v = presetValues(name); if (!v) return; applyValues(v, name);
  if (S.midi.pcOut && !fromMidi){ const i = allPresetNames().indexOf(name); if (i >= 0 && i < 128) midiSend([0xC0 | S.midi.outCh, i]); }
}
$('#preset').addEventListener('change', e => loadPreset(e.target.value));
const stepPreset = d => { const a = allPresetNames(); let i = a.indexOf(S.preset); i = (i + d + a.length) % a.length; loadPreset(a[i]); };
$('#prev').addEventListener('click', () => stepPreset(-1)); $('#next').addEventListener('click', () => stepPreset(1));
$('#rand').addEventListener('click', () => { applyValues(randomPatch(), 'Random'); });
$('#savePre').addEventListener('click', () => {
  const nm = S.preset.startsWith('User · ') ? S.preset.slice(7) : (S.preset === 'Random' ? 'Random ' + (Object.keys(S.user).length + 1) : S.preset.split(' · ').pop());
  dialog(`<p>Save the current sound as a user preset.</p><input type="text" id="upName" maxlength="40" value="${nm.replace(/"/g, '&quot;')}">`, [
    ['Cancel'], ...(S.preset.startsWith('User · ') ? [['Delete', () => { delete S.user[S.preset.slice(7)]; S.preset = FACT[0].name; renderPresetMenu(); save(); }]] : []),
    ['Save', () => { const n = ($('#upName').value || '').trim().replace(/ · /g, ' - '); if (!n) return; const o = {}; for (const p of PARAMS) o[p.id] = S.p[p.id]; S.user[n] = o; S.preset = 'User · ' + n; renderPresetMenu(); save(); toast('Saved ' + n); }]]);
});
function dialog(html, btns){
  const d = $('#dlg'); $('#dlgBody').innerHTML = html; const bx = $('#dlgBtns'); bx.innerHTML = '';
  for (const [t, f] of btns){ const b = document.createElement('button'); b.textContent = t; if (t === btns[btns.length - 1][0]) b.className = 'on'; b.addEventListener('click', () => { d.close(); if (f) f(); }); bx.appendChild(b); }
  d.showModal();
}

/* ---------------- keyboard and wheels ---------------- */
const held = new Map();   // pointerId -> note
const down = new Set(), seqDown = new Set();
let kbKeys = [], kbLow = 36;
function noteName(n){ return ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][n % 12] + (Math.floor(n / 12) - 1); }
function buildKb(){
  const kb = $('#kb'), w = kb.clientWidth || 600, whites = Math.max(8, Math.min(29, Math.floor(w / 30)));
  // whole octaves plus the top C when they fit
  let oct = Math.max(1, Math.floor((whites - 1) / 7)), nW = oct * 7 + 1; if (nW > whites) { nW = oct * 7; }
  kbLow = 36 + S.oct * 12; if (oct < 4) kbLow = 48 + S.oct * 12;
  kb.innerHTML = ''; kbKeys = [];
  const ww = 100 / nW; let wi = 0;
  for (let n = kbLow; wi < nW; n++){
    const pc = n % 12, black = [1, 3, 6, 8, 10].includes(pc), el = document.createElement('div');
    if (black){ el.className = 'b'; el.style.left = (wi * ww - ww * .3) + '%'; el.style.width = (ww * .6) + '%'; }
    else { el.className = 'w'; el.style.left = (wi * ww) + '%'; el.style.width = ww + '%'; if (pc === 0) el.innerHTML = `<span>${noteName(n)}</span>`; wi++; }
    el.dataset.n = n; kb.appendChild(el); kbKeys[n] = el;
  }
  $('#octLbl').textContent = noteName(kbLow);
  for (const n of down) kbKeys[n] && kbKeys[n].classList.add('dn');
}
function keyAt(x, y){ const el = document.elementFromPoint(x, y); return el && el.dataset && el.dataset.n ? +el.dataset.n : (el && el.parentElement && el.parentElement.dataset.n ? +el.parentElement.dataset.n : null); }
function playNote(n, v, on){
  if (on){ down.add(n); post({type:'note', on:true, n, v}); if (S.midi.thru) midiSend([0x90 | S.midi.outCh, n, Math.max(1, Math.round(v * 127))]); }
  else { down.delete(n); post({type:'note', on:false, n}); if (S.midi.thru) midiSend([0x80 | S.midi.outCh, n, 0]); }
  if (kbKeys[n]) kbKeys[n].classList.toggle('dn', on);
}
function seqKey(n, on){ if (on) seqDown.add(n); else seqDown.delete(n); if (kbKeys[n]) kbKeys[n].classList.toggle('sq', on); }
const kbEl = $('#kb');
kbEl.addEventListener('pointerdown', e => { e.preventDefault(); kbEl.setPointerCapture(e.pointerId); const n = keyAt(e.clientX, e.clientY); if (n == null) return;
  const r = e.target.getBoundingClientRect(), v = Math.max(.3, Math.min(1, (e.clientY - r.top) / r.height * 1.1)); held.set(e.pointerId, n); playNote(n, v, true); });
kbEl.addEventListener('pointermove', e => { if (!held.has(e.pointerId)) return; const n = keyAt(e.clientX, e.clientY), o = held.get(e.pointerId); if (n == null || n === o) return;
  playNote(o, 0, false); held.set(e.pointerId, n); playNote(n, .8, true); });
const kbUp = e => { if (!held.has(e.pointerId)) return; playNote(held.get(e.pointerId), 0, false); held.delete(e.pointerId); };
kbEl.addEventListener('pointerup', kbUp); kbEl.addEventListener('pointercancel', kbUp); kbEl.addEventListener('contextmenu', e => e.preventDefault());
$('#octUp').addEventListener('click', () => { S.oct = Math.min(3, S.oct + 1); buildKb(); save(); });
$('#octDn').addEventListener('click', () => { S.oct = Math.max(-3, S.oct - 1); buildKb(); save(); });
const wheel = (el, spring, init, f) => {
  const i = el.querySelector('i'); let v = init;
  const set = (t, send) => { v = Math.max(0, Math.min(1, t)); i.style.top = (8 + (1 - v) * 84) + '%'; if (send) f(v); };
  set(init, false);
  const at = e => { const r = el.getBoundingClientRect(); return 1 - (e.clientY - r.top - r.height * .08) / (r.height * .84); };
  el.addEventListener('pointerdown', e => { e.preventDefault(); el.setPointerCapture(e.pointerId); set(at(e), true); });
  el.addEventListener('pointermove', e => { if (el.hasPointerCapture(e.pointerId)) set(at(e), true); });
  const up = () => { if (spring) set(.5, true); };
  el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
  return set;
};
const setBendW = wheel($('#wBend'), true, .5, v => { post({type:'bend', v:v * 2 - 1}); const b = Math.round((v * 2 - 1) * 8191) + 8192; midiThru([0xE0 | S.midi.outCh, b & 127, b >> 7]); });
const setModW = wheel($('#wMod'), false, 0, v => { post({type:'mod', v}); midiThru([0xB0 | S.midi.outCh, 1, Math.round(v * 127)]); });
const setExpW = wheel($('#wExp'), false, 1, v => { post({type:'expr', v}); midiThru([0xB0 | S.midi.outCh, 11, Math.round(v * 127)]); });
// a computer keyboard plays too (two rows from Z and Q)
const QW = 'zsxdcvgbhnjmq2w3er5t6y7ui9o0p';
addEventListener('keydown', e => { if (e.repeat || e.target.tagName === 'INPUT' || e.metaKey || e.ctrlKey) return; const i = QW.indexOf(e.key.toLowerCase()); if (i < 0) return; const n = 48 + S.oct * 12 + i; if (!down.has(n)) playNote(n, .85, true); });
addEventListener('keyup', e => { const i = QW.indexOf(e.key.toLowerCase()); if (i < 0) return; const n = 48 + S.oct * 12 + i; if (down.has(n)) playNote(n, 0, false); });
addEventListener('resize', () => { buildKb(); if (S.page === 'Seq') drawRoll(); });

/* ---------------- transport ---------------- */
$('#play').addEventListener('click', async () => { await startAudio(); if (pos.playing){ post({type:'stop'}); } else { sendSong(); post({type:'play'}); } });
$('#stop').addEventListener('click', () => { post({type:'stop'}); if (S.recOn){ S.recOn = false; $('#rec').classList.remove('on'); post({type:'rec', on:false}); } });
$('#rec').addEventListener('click', () => { S.recOn = !S.recOn; $('#rec').classList.toggle('on', S.recOn); post({type:'rec', on:S.recOn, quant:S.quant}); if (S.recOn) toast('Recording into pattern ' + (S.song.cur + 1) + ' while it plays'); });
$('#bpm').value = S.song.bpm;
$('#bpm').addEventListener('change', e => { const v = Math.max(30, Math.min(300, +e.target.value || 120)); S.song.bpm = v; e.target.value = v; post({type:'bpm', v}); save(); });

/* ---------------- sequencer page ---------------- */
for (let i = 0; i < SQ.NPAT; i++){ const b = document.createElement('button'); b.textContent = i + 1; $('#patBtns').appendChild(b);
  b.addEventListener('click', () => { if (S.chainEdit){ S.song.chain.push(i); } else { S.song.cur = i; S.view = 0; } sendSong(); save(); renderSeq(); }); }
function renderPatBtns(){ [...$('#patBtns').children].forEach((b, i) => { b.classList.toggle('cur', i === S.song.cur); b.classList.toggle('has', S.song.pats[i].notes.length > 0); }); }
function markPlayingPattern(){ [...$('#patBtns').children].forEach((b, i) => b.classList.toggle('playing', pos.playing && i === pos.pat)); }
function renderSeq(){
  const p = S.song.pats[S.song.cur];
  renderPatBtns(); $('#bars').value = p.bars;
  $('#barView').innerHTML = Array.from({length:p.bars}, (_, i) => `<option value="${i}">bar ${i + 1} / ${p.bars}</option>`).join('');
  if (S.view >= p.bars) S.view = 0; $('#barView').value = S.view;
  $('#songMode').textContent = S.song.mode ? 'SONG' : 'PATTERN'; $('#songMode').classList.toggle('on', !!S.song.mode);
  $('#chainEd').classList.toggle('on', !!S.chainEdit);
  $('#chain').textContent = 'CHAIN: ' + (S.song.chain.length ? S.song.chain.map(x => x + 1).join(' ') : '(empty)');
  $('#quant').classList.toggle('on', S.quant); $('#click').classList.toggle('on', S.click);
  $('#noteLen').value = S.len; $('#noteVel').value = S.vel;
  drawRoll();
}
$('#bars').addEventListener('change', e => { S.song.pats[S.song.cur].bars = +e.target.value; sendSong(); save(); renderSeq(); });
$('#barView').addEventListener('change', e => { S.view = +e.target.value; drawRoll(); });
$('#songMode').addEventListener('click', () => { S.song.mode = S.song.mode ? 0 : 1; if (S.song.mode && !S.song.chain.length) S.song.chain = [S.song.cur]; sendSong(); save(); renderSeq(); });
$('#chainEd').addEventListener('click', () => { S.chainEdit = !S.chainEdit; if (S.chainEdit) toast('Tap pattern numbers to add them to the chain'); renderSeq(); });
$('#chainDel').addEventListener('click', () => { S.song.chain.pop(); sendSong(); save(); renderSeq(); });
$('#chainClr').addEventListener('click', () => { S.song.chain = []; sendSong(); save(); renderSeq(); });
$('#chainAdd').addEventListener('click', () => { S.song.chain.push(S.song.cur); sendSong(); save(); renderSeq(); });
$('#noteLen').addEventListener('change', e => { S.len = +e.target.value; save(); });
$('#noteVel').addEventListener('change', e => { S.vel = +e.target.value; save(); });
$('#upR').addEventListener('click', () => { S.low = Math.min(96, S.low + 6); drawRoll(); save(); });
$('#dnR').addEventListener('click', () => { S.low = Math.max(12, S.low - 6); drawRoll(); save(); });
$('#quant').addEventListener('click', () => { S.quant = !S.quant; post({type:'rec', on:!!S.recOn, quant:S.quant}); renderSeq(); save(); });
$('#click').addEventListener('click', () => { S.click = !S.click; post({type:'click', on:S.click}); renderSeq(); save(); });
$('#randPat').addEventListener('click', () => { S.song.pats[S.song.cur] = SQ.randomPattern(); S.view = 0; sendSong(); save(); renderSeq(); toast('Random progression in pattern ' + (S.song.cur + 1)); });
$('#copyPat').addEventListener('click', () => { S.clip = JSON.parse(JSON.stringify(S.song.pats[S.song.cur])); toast('Pattern ' + (S.song.cur + 1) + ' copied'); });
$('#pastePat').addEventListener('click', () => { if (!S.clip) return toast('Copy a pattern first'); S.song.pats[S.song.cur] = JSON.parse(JSON.stringify(S.clip)); sendSong(); save(); renderSeq(); });
$('#clrPat').addEventListener('click', () => dialog(`<p>Clear pattern ${S.song.cur + 1}?</p>`, [['Cancel'], ['Clear', () => { S.song.pats[S.song.cur] = SQ.newPattern(); sendSong(); save(); renderSeq(); }]]));
// swing lives in the song; a knob needs a parameter, so it gets a small stand-in
PARAMS.push({id:'swing', name:'Swing', min:0, max:1, def:0, step:0, unit:'', group:'seq', cc:-1, choices:[]}); PID.swing = PARAMS.length - 1;
S.p.swing = S.song.swing || 0;
knob($('#kSeq'), 'swing', 'Swing');
ctrls.swing.unshift(() => { if (S.song.swing !== S.p.swing){ S.song.swing = S.p.swing; sendSong(); } });
const fmt0 = fmt; fmt = (id, v) => id === 'swing' ? Math.round(50 + v * 25) + '%' : fmt0(id, v);

const roll = $('#rollC');
function rollGeom(){
  const r = roll.getBoundingClientRect(), dpr = devicePixelRatio || 1;
  if (roll.width !== Math.round(r.width * dpr) || roll.height !== Math.round(r.height * dpr)){ roll.width = Math.round(r.width * dpr); roll.height = Math.round(r.height * dpr); }
  const rows = Math.max(12, Math.min(37, Math.floor(r.height / 13))), kw = Math.min(42, r.width * .09);
  return {w:r.width, h:r.height, dpr, rows, kw, rh:r.height / rows, cw:(r.width - kw) / 16, low:S.low, x0:r.left, y0:r.top};
}
function drawRoll(){
  if (!roll.isConnected) return;
  const g = rollGeom(), c = roll.getContext('2d'), p = S.song.pats[S.song.cur], b0 = S.view * SQ.BAR;
  c.setTransform(g.dpr, 0, 0, g.dpr, 0, 0); c.clearRect(0, 0, g.w, g.h);
  for (let r = 0; r < g.rows; r++){
    const n = g.low + g.rows - 1 - r, pc = n % 12, black = [1, 3, 6, 8, 10].includes(pc), y = r * g.rh;
    c.fillStyle = black ? '#121110' : '#191816'; c.fillRect(g.kw, y, g.w - g.kw, g.rh);
    c.fillStyle = black ? '#2a2826' : '#d9d1bf'; c.fillRect(0, y + .5, g.kw - 2, g.rh - 1);
    if (pc === 0){ c.fillStyle = '#5a5246'; c.font = `${Math.min(11, g.rh * .8)}px system-ui`; c.textBaseline = 'middle'; c.fillText(noteName(n), 3, y + g.rh / 2); c.fillStyle = '#3a3632'; c.fillRect(g.kw, y + g.rh - 1, g.w - g.kw, 1); }
  }
  for (let s = 0; s <= 16; s++){ c.fillStyle = s % 4 ? '#262422' : '#4a4540'; c.fillRect(g.kw + s * g.cw, 0, 1, g.h); }
  for (const nt of p.notes){
    const r = g.low + g.rows - 1 - nt.n; if (r < 0 || r >= g.rows) continue;
    const x = g.kw + (nt.t - b0) / SQ.STEP * g.cw, wdt = nt.d / SQ.STEP * g.cw; if (x + wdt < g.kw || x > g.w) continue;
    const a = .45 + .55 * nt.v / 127;
    c.fillStyle = `rgba(255,122,26,${a})`; c.fillRect(Math.max(g.kw, x) + 1, r * g.rh + 1, Math.max(3, Math.min(x + wdt, g.w) - Math.max(g.kw, x) - 2), g.rh - 2);
    c.fillStyle = '#ffd2a8'; if (x >= g.kw) c.fillRect(x + 1, r * g.rh + 1, 2, g.rh - 2);
  }
  // off-screen notes above or below
  let above = 0, below = 0; for (const nt of p.notes){ if (nt.t >= b0 && nt.t < b0 + SQ.BAR){ if (nt.n >= g.low + g.rows) above++; else if (nt.n < g.low) below++; } }
  c.fillStyle = '#ff7a1a'; c.font = '11px system-ui'; c.textBaseline = 'top'; if (above) c.fillText('▲ ' + above, g.w - 34, 3); c.textBaseline = 'bottom'; if (below) c.fillText('▼ ' + below, g.w - 34, g.h - 3);
  if (pos.playing && pos.pat === S.song.cur){ const t = pos.tick - b0; if (t >= 0 && t < SQ.BAR){ c.fillStyle = '#4caf50'; c.fillRect(g.kw + t / SQ.STEP * g.cw, 0, 2, g.h); } }
}
let drag = null;
roll.addEventListener('pointerdown', e => {
  e.preventDefault(); roll.setPointerCapture(e.pointerId);
  const g = rollGeom(), x = e.clientX - g.x0, y = e.clientY - g.y0, p = S.song.pats[S.song.cur], b0 = S.view * SQ.BAR;
  const n = g.low + g.rows - 1 - Math.floor(y / g.rh);
  if (x < g.kw){ startAudio().then(() => { playNote(n, .8, true); drag = {audition:n}; }); return; }
  const tk = b0 + (x - g.kw) / g.cw * SQ.STEP;
  const hit = p.notes.findIndex(nt => nt.n === n && tk >= nt.t && tk < nt.t + Math.max(nt.d, SQ.STEP * .5));
  if (hit >= 0){ drag = {del:hit, x}; return; }
  const t = Math.floor((tk - b0) / SQ.STEP) * SQ.STEP + b0, nt = {t, n, d:S.len, v:S.vel};
  if (t >= SQ.patLen(p)) return;
  p.notes.push(nt); drag = {nt, x, d0:S.len}; startAudio().then(() => { playNote(n, S.vel / 127, true); setTimeout(() => playNote(n, 0, false), 180); });
  drawRoll(); renderPatBtns();
});
roll.addEventListener('pointermove', e => {
  if (!drag || !drag.nt) return; const g = rollGeom(), dx = e.clientX - g.x0 - drag.x;
  drag.nt.d = Math.max(SQ.STEP, Math.round((drag.d0 + dx / g.cw * SQ.STEP) / SQ.STEP) * SQ.STEP); drawRoll();
});
const rollUp = () => { if (!drag) return; if (drag.audition != null) playNote(drag.audition, 0, false);
  if (drag.del != null) S.song.pats[S.song.cur].notes.splice(drag.del, 1);
  drag = null; sendSong(); save(); drawRoll(); renderPatBtns(); };
roll.addEventListener('pointerup', rollUp); roll.addEventListener('pointercancel', rollUp);

/* ---------------- MIDI files ---------------- */
$('#impMidi').addEventListener('click', () => $('#file').click());
$('#file').addEventListener('change', async e => {
  const f = e.target.files[0]; e.target.value = ''; if (!f) return;
  try {
    const m = SQ.parseMidi(await f.arrayBuffer());
    const chans = [...new Set(m.notes.map(x => x.ch))].sort((a, b) => a - b);
    dialog(`<p><b>${f.name.replace(/</g, '&lt;')}</b>: ${m.notes.length} notes, ${Math.round(m.tempo)} BPM, channels ${chans.map(c => c + 1).join(', ')}.</p>
      <p>Replace the song with it? It goes into patterns of 4 bars (up to 64 bars) chained in order.</p>
      <p><label><input type="checkbox" id="impQ"> Quantize to 1/16</label><br><label><input type="checkbox" id="impD" checked> Leave out drums (channel 10)</label></p>
      <p><label>Channel <select id="impCh"><option value="-1">All</option>${chans.map(c => `<option value="${c}">${c + 1}</option>`).join('')}</select></label></p>`,
      [['Cancel'], ['Import', () => { const r = SQ.importToSong(m, {quantize:$('#impQ').checked, skipDrums:$('#impD').checked, channel:+$('#impCh').value});
        if (!r.count) return toast('No notes to import');
        S.song = r.song; S.p.swing = 0; (ctrls.swing || []).forEach(f => f()); $('#bpm').value = S.song.bpm; post({type:'bpm', v:S.song.bpm}); sendSong(); save(); S.view = 0;
        const lo = Math.min(...S.song.pats[0].notes.map(x => x.n).concat([60])); S.low = Math.max(12, lo - 2); renderSeq(); showPage('Seq');
        toast(`Imported ${r.count} notes, ${r.bars} bars, song mode`); }]]);
  } catch (err) { toast('Could not read the file: ' + err.message); }
});
$('#expMidi').addEventListener('click', () => dialog('<p>Export as a Standard MIDI File (type 1, 96 PPQ).</p>', [['Cancel'], ['Current pattern', () => exportMid('pattern')], ['Song', () => exportMid('song')]]));
function exportMid(which){
  if (which === 'song' && !S.song.mode) which = 'pattern';
  const bytes = SQ.exportMidi(S.song, which, S.midi.outCh), name = 'Omega Solina ' + (which === 'pattern' ? 'pattern ' + (S.song.cur + 1) : 'song') + '.mid';
  saveFile(bytes, name, 'audio/midi');
}
function saveFile(bytes, name, mime){
  if (window.AndroidSolina){
    let s = ''; for (let i = 0; i < bytes.length; i += 32768) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 32768));
    const where = AndroidSolina.save(btoa(s), name, mime); toast(where.startsWith('error') ? 'Could not save: ' + where : 'Saved to ' + where); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([bytes], {type:mime})); a.download = name; document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000); toast('Saved ' + name);
}

/* ---------------- MIDI in and out ---------------- */
// the Android app has no Web MIDI: AndroidMidi (SolinaActivity) is shaped like it here
if (window.AndroidMidi && AndroidMidi.available()){
  const access = {inputs:new Map(), outputs:new Map(), onstatechange:null};
  const scan = () => { const seen = new Set();
    for (const d of JSON.parse(AndroidMidi.list())){ seen.add(d.id);
      if (d.kind === 'input' && !access.inputs.has(d.id)){ const port = {id:d.id, name:d.name, type:'input', _h:null};
        Object.defineProperty(port, 'onmidimessage', {get(){ return this._h; }, set(h){ this._h = h; if (h) AndroidMidi.listen(d.id); }}); access.inputs.set(d.id, port); }
      else if (d.kind === 'output' && !access.outputs.has(d.id)) access.outputs.set(d.id, {id:d.id, name:d.name, type:'output', send:b => AndroidMidi.send(d.id, Array.from(b).join(','))}); }
    for (const m of [access.inputs, access.outputs]) for (const id of [...m.keys()]) if (!seen.has(id)) m.delete(id); };
  window.__soMidi = (id, bytes) => { const p = access.inputs.get(id); if (p && p._h) p._h({data:Uint8Array.from(bytes), target:p}); };
  window.__soMidiChanged = () => { scan(); if (access.onstatechange) access.onstatechange({}); };
  scan(); navigator.requestMIDIAccess = () => Promise.resolve(access);
}
let MA = null;
const midiLog = [];
function logMidi(s){ midiLog.push(s); if (midiLog.length > 40) midiLog.shift(); if (S.page === 'Midi') $('#midiLog').textContent = midiLog.slice(-Math.max(4, Math.floor($('#midiLog').clientHeight / 13))).join('\n'); }
function midiSend(bytes){ if (!MA || !S.midi.outId) return; const o = MA.outputs.get(S.midi.outId); if (o) try { o.send(bytes); } catch (e) {} }
function midiThru(bytes){ if (S.midi.thru) midiSend(bytes); }
async function initMidi(){
  if (!navigator.requestMIDIAccess) return;
  try { MA = await navigator.requestMIDIAccess({sysex:false}); } catch (e) { return; }
  const hook = () => { for (const i of MA.inputs.values()) i.onmidimessage = e => onMidi(e.data, i.id); renderMidiPage(); };
  MA.onstatechange = hook; hook();
}
const clk = {times:[], tick:0};
function onMidi(d, src){
  if (S.midi.inId !== 'all' && src !== S.midi.inId) return;
  const st = d[0];
  if (st >= 0xF8){
    if (!S.midi.clkIn) return;
    if (st === 0xF8){ const t = performance.now(); clk.times.push(t); if (clk.times.length > 25) clk.times.shift(); clk.tick += 4;
      if (clk.times.length >= 13 && clk.times.length % 6 === 1){ const n = clk.times.length - 1, iv = (clk.times[n] - clk.times[0]) / n, bpm = Math.round(60000 / (iv * 24) * 10) / 10;
        if (bpm > 30 && bpm < 300 && Math.abs(bpm - S.song.bpm) > .15){ S.song.bpm = bpm; $('#bpm').value = Math.round(bpm); post({type:'bpm', v:bpm}); } } }
    else if (st === 0xFA){ clk.tick = 0; sendSong(); post({type:'play'}); }
    else if (st === 0xFB){ sendSong(); post({type:'play', tick:clk.tick}); }
    else if (st === 0xFC){ post({type:'stop'}); }
    return;
  }
  if (st === 0xF2 && d.length > 2){ clk.tick = (d[1] | d[2] << 7) * SQ.STEP; return; }
  if (st >= 0xF0) return;
  const hi = st & 0xF0, ch = st & 15; if (S.midi.inCh >= 0 && ch !== S.midi.inCh) return;
  logMidi(`ch${ch + 1} ${({0x80:'off', 0x90:'on', 0xA0:'poly AT', 0xB0:'CC', 0xC0:'program', 0xD0:'AT', 0xE0:'bend'})[hi] || hi.toString(16)} ${d[1]}${d.length > 2 ? ' ' + d[2] : ''}`);
  if (hi === 0x90 && d[2] > 0){ post({type:'note', on:true, n:d[1], v:d[2] / 127}); down.add(d[1]); kbKeys[d[1]] && kbKeys[d[1]].classList.add('dn'); midiThru([0x90 | S.midi.outCh, d[1], d[2]]); }
  else if (hi === 0x80 || hi === 0x90){ post({type:'note', on:false, n:d[1]}); down.delete(d[1]); kbKeys[d[1]] && kbKeys[d[1]].classList.remove('dn'); midiThru([0x80 | S.midi.outCh, d[1], 0]); }
  else if (hi === 0xE0){ const v = ((d[2] << 7 | d[1]) - 8192) / 8192; post({type:'bend', v}); setBendW(v / 2 + .5, false); midiThru([0xE0 | S.midi.outCh, d[1], d[2]]); }
  else if (hi === 0xD0){ post({type:'mod', v:d[1] / 127}); setModW(d[1] / 127, false); }
  else if (hi === 0xC0){ if (S.midi.pcIn){ const a = allPresetNames(); if (d[1] < a.length) loadPreset(a[d[1]], true); } }
  else if (hi === 0xB0){
    const cc = d[1], v = d[2];
    if (learnOn && learnTarget && ![0, 32, 64, 120, 121, 123].includes(cc)){
      for (const k in S.cc) if (S.cc[k] === learnTarget) delete S.cc[k];
      S.cc[cc] = learnTarget; toast(PI(learnTarget).name + ' on CC ' + cc); learnTarget = null; $$('.learn').forEach(x => x.classList.remove('learn')); markMapped(); renderCcList(); save(); return;
    }
    if (cc === 1){ post({type:'mod', v:v / 127}); setModW(v / 127, false); }
    else if (cc === 11){ post({type:'expr', v:v / 127}); setExpW(v / 127, false); }
    else if (cc === 64) post({type:'pedal', on:v >= 64});
    else if (cc === 120 || cc === 123){ post({type:'panic'}); down.clear(); $$('#kb .dn').forEach(x => x.classList.remove('dn')); }
    else if (cc === 121){ post({type:'bend', v:0}); post({type:'mod', v:0}); post({type:'expr', v:1}); post({type:'pedal', on:false}); setBendW(.5); setModW(0); setExpW(1); }
    else if (S.cc[cc]){ const p = PI(S.cc[cc]); if (p) setParam(p.id, p.step === 1 && p.max - p.min === 1 ? (v >= 64 ? 1 : 0) : p.min + v / 127 * (p.max - p.min), {fromMidi:true}); }
    midiThru([0xB0 | S.midi.outCh, cc, v]);
  }
}
function renderMidiPage(){
  const ins = MA ? [...MA.inputs.values()] : [], outs = MA ? [...MA.outputs.values()] : [];
  $('#midiIn').innerHTML = '<option value="all">All inputs</option>' + ins.map(i => `<option value="${i.id}">${i.name}</option>`).join('') + (MA ? '' : '<option value="" disabled>No MIDI in this browser</option>');
  $('#midiIn').value = S.midi.inId; if ($('#midiIn').value !== S.midi.inId) $('#midiIn').value = 'all';
  $('#midiOut').innerHTML = '<option value="">None</option>' + outs.map(o => `<option value="${o.id}">${o.name}</option>`).join(''); $('#midiOut').value = S.midi.outId;
  $('#midiInCh').innerHTML = '<option value="-1">Omni</option>' + Array.from({length:16}, (_, i) => `<option value="${i}">${i + 1}</option>`).join(''); $('#midiInCh').value = S.midi.inCh;
  $('#midiOutCh').innerHTML = Array.from({length:16}, (_, i) => `<option value="${i}">${i + 1}</option>`).join(''); $('#midiOutCh').value = S.midi.outCh;
  for (const k of ['thru', 'clkOut', 'clkIn', 'pcIn', 'pcOut']) $('#' + k).classList.toggle('on', !!S.midi[k]);
  renderCcList(); $('#midiLog').textContent = midiLog.slice(-20).join('\n') || 'Incoming MIDI shows here.';
}
function renderCcList(){ $('#ccList').innerHTML = Object.keys(S.cc).map(Number).sort((a, b) => a - b).map(cc => `<div>CC ${cc} <b>${(PI(S.cc[cc]) || {name:'?'}).name}</b></div>`).join('') +
  '<div>CC 1 <b>Mod wheel</b></div><div>CC 11 <b>Expression pedal</b></div><div>CC 64 <b>Sustain pedal</b></div><div>CC 120/123 <b>All notes off</b></div>'; }
$('#midiIn').addEventListener('change', e => { S.midi.inId = e.target.value; save(); });
$('#midiOut').addEventListener('change', e => { S.midi.outId = e.target.value; save(); });
$('#midiInCh').addEventListener('change', e => { S.midi.inCh = +e.target.value; save(); });
$('#midiOutCh').addEventListener('change', e => { S.midi.outCh = +e.target.value; save(); });
for (const k of ['thru', 'clkOut', 'clkIn', 'pcIn', 'pcOut']) $('#' + k).addEventListener('click', () => { S.midi[k] = !S.midi[k]; $('#' + k).classList.toggle('on', S.midi[k]); if (k === 'clkOut') post({type:'clockOut', on:S.midi.clkOut}); save(); });
$('#panic').addEventListener('click', () => { post({type:'panic'}); for (let c = 0; c < 16; c++) midiSend([0xB0 | c, 123, 0]); down.clear(); $$('#kb .dn').forEach(x => x.classList.remove('dn')); });
$('#learn').addEventListener('click', () => { learnOn = !learnOn; learnTarget = null; $('#learn').classList.toggle('on', learnOn); $$('.learn').forEach(x => x.classList.remove('learn'));
  toast(learnOn ? 'MIDI learn: tap a control on any page, then move a knob on your controller' : 'MIDI learn off'); });
$('#ccReset').addEventListener('click', () => { S.cc = defaultCC(); markMapped(); renderCcList(); save(); toast('Default CC map'); });

/* ---------------- go ---------------- */
renderPresetMenu(); renderMixerNames(); markMapped(); buildKb(); showPage(S.page || 'Panel');
lcd(S.preset.replace(' · ', ': '));
initMidi();
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidSolina) navigator.serviceWorker.register('sw.js').catch(() => {});
