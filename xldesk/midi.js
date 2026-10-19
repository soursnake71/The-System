/* XL-Desk MIDI: every control on CC (fixed map in spec.js, MIDI Learn over the top), value feedback to motorised or
   LED controllers, 14-bit pitch-bend faders (Mackie style), program change = desk snapshot, and MIDI Machine Control
   (MMC) both ways: the desk's transport sends MMC to drive your DAW, and MMC from a DAW or controller drives the tape. */
'use strict';
let midiAcc = null, sysexOK = false;
const midiOut = () => { if (!midiAcc || !S.midi.outId) return null; return midiAcc.outputs.get(S.midi.outId) || null; };
const msend = b => { const o = midiOut(); if (o) try { o.send(b); } catch(e){} };
async function midiInit(){
  if (HOST){ HOST.send({t:'midiDevices'}); return; }   // inside the plugin the ports are opened natively
  if (!navigator.requestMIDIAccess){ toast('This browser has no Web MIDI. Use Chrome.'); return; }
  try { midiAcc = await navigator.requestMIDIAccess({sysex:true}); sysexOK = true; }
  catch(e){ try { midiAcc = await navigator.requestMIDIAccess({sysex:false}); sysexOK = false; toast('MIDI on without SysEx, so MMC is off. Allow SysEx in the site settings for MMC.', 5000); } catch(e2){ toast('MIDI was not allowed.'); return; } }
  const hook = () => { let n = 0; midiAcc.inputs.forEach(inp => { inp.onmidimessage = (!S.midi.inId || S.midi.inId === inp.id) ? onMidi : null; n++; });
    $('#midiStat').textContent = 'MIDI ' + n + ' in / ' + midiAcc.outputs.size + ' out' + (sysexOK ? ' · MMC' : ''); if (S.view === 'midi') renderMidi(); };
  midiAcc.onstatechange = hook; hook(); S.midi.on = true; save();
}

// the plugin's device list, wrapped to look like Web MIDI so the MIDI tab works unchanged
function onHostMidi(d){
  const send = b => HOST.send({t:'midiout', bytes:[...b]});
  midiAcc = {inputs:new Map(d.ins.map(x => [x.id, {id:x.id, name:x.name}])), outputs:new Map(d.outs.map(x => [x.id, {id:x.id, name:x.name, send}])), onstatechange(){}};
  sysexOK = true; S.midi.on = true;
  $('#midiStat').textContent = 'MIDI ' + d.ins.length + ' in / ' + d.outs.length + ' out · MMC';
  if (S.view === 'midi') renderMidi();
}
/* ---------- CC -> parameter ---------- */
const MMAP = XL.MIDIMAP;
const ccTarget = (ch, cc) => S.learn[ch + ':' + cc] || MMAP[ch + ':' + cc] || null;
function applyMidi(id, v7, raw14){
  if (id[0] === '@') return midiAction(id, v7);
  const sp = spec(id); if (!sp) return;
  if (sp.k === 'pot'){ const v = raw14 != null ? raw14 / 16383 : v7 / 127; setP(id, (sp.u === 'pan' && v7 === 64 && raw14 == null) ? .5 : v, 'midi'); }
  else if (sp.k === 'step') setP(id, v7 / 127, 'midi');
  else if (sp.k === 'btn'){
    if (S.midi.btn === 'follow') setP(id, v7 >= 64, 'midi');
    else if (v7 > 0){ if (typeof press === 'function' && !/^(fbA|fbB)\.talk$/.test(id)) press(id); else setP(id, !val(id), 'midi'); }
    else if (/^(fbA|fbB)\.talk$/.test(id) && !val(id === 'fbA.talk' ? 'dip.3' : 'dip.5')) setP(id, 0, 'midi');   // momentary talkback follows the key release
  }
}
function midiAction(id, v){
  if (!v && id !== '@rew' && id !== '@ff') return;
  if (id === '@play') tp.play(); else if (id === '@stop') tp.stop(); else if (id === '@rec') tp.rec(); else if (id === '@rtz') tp.locate(S.loop.on ? S.loop.ls : 0);
  else if (id === '@rew' || id === '@ff'){ if (v) tp.wind(id === '@ff' ? 16 : -16); else { post({t:'tp', cmd:'wind', v:0}); } }
  else if (id === '@soloclear') clearSolos(); else if (id === '@sub') toggleSub();
}
function onMidi(e){
  const d = e.data, st = d[0];
  if (st === 0xF0) return onSysex(d);
  if (st >= 0xF0) return;
  const hi = st & 0xF0, ch = st & 15;
  if (hi === 0xB0){
    const cc = d[1], v = d[2];
    if (learn.on && learn.sel){ learnBind(ch, cc); return; }
    const id = ccTarget(ch, cc); if (id) applyMidi(id, v);
  } else if (hi === 0xE0 && S.midi.pb){ const raw = d[2] << 7 | d[1]; if (learn.on) return; applyMidi('m' + (ch + 1) + '.fader', raw >> 7, raw); }
  else if (hi === 0xC0) recallSnap(d[1]);
  else if (hi === 0x90 && d[2] && learn.on && learn.sel){ toast('Use a knob, fader or button that sends CC, not a note.'); }
}
function learnBind(ch, cc){
  const id = learn.sel, k = ch + ':' + cc;
  for (const key in S.learn) if (S.learn[key] === id) delete S.learn[key];
  S.learn[k] = id; save(); rev = null;
  $$('.learnsel').forEach(x => x.classList.remove('learnsel')); learn.sel = null;
  toast(ctlName(id) + ' → MIDI ch ' + (ch + 1) + ' CC ' + cc + '. Tap another control or Learn to finish.');
  if (S.view === 'midi') renderMidi();
}
/* ---------- feedback to the controller ---------- */
let rev = null;
function revMap(){ if (rev) return rev; rev = {}; for (const k in MMAP) if (!rev[MMAP[k]]) rev[MMAP[k]] = k; for (const k in S.learn) rev[S.learn[k]] = k; return rev; }
function feedback(id, v, from){
  if (HOST) return;   // the plugin sends feedback itself, editor open or not
  if (!S.midi.fb || from === 'midi' || !midiOut()) return;
  const sp = spec(id); if (!sp) return; const key = revMap()[id];
  const v7 = Math.round(v * 127);
  if (key){ const [ch, cc] = key.split(':').map(Number); msend([0xB0 | ch, cc, v7]); }
  const f = /^m(\d+)\.fader$/.exec(id); if (f && S.midi.pb){ const r = Math.round(v * 16383); msend([0xE0 | (f[1] - 1), r & 127, r >> 7]); }
}
listeners.push(feedback);
function sendAll(){ for (const id in XL.P) feedback(id, val(id)); toast('Sent every control to the controller.'); }

/* ---------- MMC ---------- */
const MMC_NAMES = {1:'Stop', 2:'Play', 3:'Deferred play', 4:'Fast forward', 5:'Rewind', 6:'Record strobe', 7:'Record exit', 8:'Record pause', 9:'Pause', 0x0D:'MMC reset', 0x44:'Locate'};
function mmcSend(cmd){ if (!S.midi.mmcOut || !sysexOK) return; msend([0xF0, 0x7F, S.midi.mmcId & 127, 0x06, cmd, 0xF7]); }
function mmcLocate(sec){ if (!S.midi.mmcOut || !sysexOK) return; const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = Math.floor(sec) % 60, f = Math.floor((sec % 1) * 30), sf = Math.floor(((sec * 30) % 1) * 100);
  msend([0xF0, 0x7F, S.midi.mmcId & 127, 0x06, 0x44, 0x06, 0x01, 0x60 | (h & 31), m, s, f, sf, 0xF7]); }   // 0x60 = 30 fps
// MMC in: F0 7F <device> 06 <command> ... F7. Device 7F is "all call".
const MMC_FPS = [24, 25, 29.97, 30];
function onSysex(d){
  if (!S.midi.mmcIn || d[1] !== 0x7F || d[3] !== 0x06) return;
  const dev = d[2]; if (dev !== 0x7F && S.midi.mmcId !== 0x7F && dev !== S.midi.mmcId) return;
  const cmd = d[4];
  if (cmd === 0x01 || cmd === 0x0D) tp.stop('mmc');
  else if (cmd === 0x02 || cmd === 0x03) tp.play('mmc');
  else if (cmd === 0x09 || cmd === 0x08) tp.pause('mmc');
  else if (cmd === 0x04) tp.wind(16, 'mmc');
  else if (cmd === 0x05) tp.wind(-16, 'mmc');
  else if (cmd === 0x06){ if (!tpState.rec) tp.rec('mmc'); }
  else if (cmd === 0x07){ if (tpState.rec) tp.rec('mmc'); }
  else if (cmd === 0x44 && d[5] === 0x06 && d[6] === 0x01){ const hr = d[7], fps = MMC_FPS[(hr >> 5) & 3]; tp.locate((hr & 31) * 3600 + d[8] * 60 + d[9] + ((d[10] & 31) + (d[11] || 0) / 100) / fps, 'mmc'); }
  const n = MMC_NAMES[cmd]; if (n) $('#midiStat').textContent = 'MMC: ' + n;
}

/* ---------- snapshots (program change 0-127) ---------- */
function saveSnap(n){ S.snaps[n] = {P:Object.assign({}, S.P), types:Object.assign({}, S.types), names:Object.assign({}, S.names), at:Date.now()}; save(); toast('Saved the desk to program ' + (n + 1) + '.'); }
function recallSnap(n){
  const s = S.snaps[n]; if (!s){ toast('Program ' + (n + 1) + ' is empty.'); return; }
  S.P = Object.assign(XL.defaults(), s.P); for (const w in s.types) if (S.types[w] !== s.types[w]){ S.types[w] = s.types[w]; post({t:'type', w, type:s.types[w]}); }
  S.names = Object.assign({}, s.names); post({t:'pp', P:S.P}); save(); refreshAll(); $$('.name').forEach(n => n.value = S.names[n.dataset.key] || ''); for (const w in SLOTHD) drawSlotHead(w); drawSafe(); if (S.view !== 'desk') show(S.view);
  if (S.midi.fb) for (const id in XL.P) feedback(id, val(id));
  if (typeof n === 'number') toast('Recalled program ' + (n + 1) + '.'); else toast('Desk loaded.');
}

/* ---------- MIDI tab ---------- */
function renderMidi(){
  const v = $('#v-midi'); v.innerHTML = '';
  const c = el('div', 'card'); c.append(el('h2', '', 'MIDI'));
  if (!midiAcc){ const b = el('button', 'on', 'Turn on MIDI'); b.onclick = () => midiInit(); c.append(kp(b), el('p', 'hint', 'Chrome asks for MIDI with SysEx; allow it so MMC works.')); v.appendChild(c); renderMap(v); return; }
  const si = el('select'); si.appendChild(new Option(HOST ? 'No hardware input (track MIDI only)' : 'All inputs', '')); midiAcc.inputs.forEach(i => si.appendChild(new Option(i.name, i.id))); si.value = S.midi.inId; si.onchange = () => { S.midi.inId = si.value; save(); midiAcc.onstatechange(); if (HOST) toast('The plugin also takes MIDI from its track.'); };
  const so = el('select'); so.appendChild(new Option('No output', '')); midiAcc.outputs.forEach(o => so.appendChild(new Option(o.name, o.id))); so.value = S.midi.outId; so.onchange = () => { S.midi.outId = so.value; save(); };
  c.append(el('div', 'row'), kp(el('span', 'hint', 'In'), si, el('span', 'hint', 'Out'), so));
  const chk = (k, lb) => { const l = el('label'), i = el('input'); i.type = 'checkbox'; i.checked = !!S.midi[k]; i.onchange = () => { S.midi[k] = i.checked; save(); }; l.append(i, lb); return l; };
  const bm = el('select'); bm.append(new Option('Buttons toggle on each press (momentary pads)', 'toggle'), new Option('Buttons follow the value (≥64 on)', 'follow')); bm.value = S.midi.btn; bm.onchange = () => { S.midi.btn = bm.value; save(); };
  const r1 = el('div', 'row'); r1.append(chk('fb', 'Send control changes back out (feedback)'), chk('pb', 'Pitch bend on MIDI ch 1-16 = faders 1-16 (14-bit)'), bm); c.appendChild(r1);
  const sa = el('button', '', 'Send all values now'); sa.onclick = sendAll; c.appendChild(kp(sa));
  const m = el('div', 'card'); m.append(el('h2', '', 'MMC (MIDI Machine Control)'), el('p', 'hint', sysexOK ? 'SysEx is allowed.' : 'SysEx was not allowed, so MMC cannot be sent or received.'));
  const id = el('input'); id.type = 'number'; id.min = 0; id.max = 127; id.value = S.midi.mmcId; id.style.width = '70px'; id.onchange = () => { S.midi.mmcId = clamp(+id.value | 0, 0, 127); save(); };
  const r2 = el('div', 'row'); r2.append(chk('mmcIn', 'Receive MMC (stop, play, deferred play, FF, rewind, record strobe/exit, pause, locate)'), chk('mmcOut', 'Send MMC from the transport'), el('span', 'hint', 'Device ID (127 = all call)'), id); m.appendChild(r2);
  m.appendChild(el('p', 'hint', 'Send MMC to drive Pro Tools, Logic, Cubase, Reaper and others from the desk; set the DAW to receive MMC on the same port and ID. Locate uses 30 fps timecode.'));
  const sn = el('div', 'card'); sn.append(el('h2', '', 'Snapshots'), el('p', 'hint', 'Program change n recalls the desk saved as program n: every knob, switch, fader, module and scribble strip. (The real XL-Desk has no recall; this is the emulator\'s.)'));
  const pn = el('input'); pn.type = 'number'; pn.min = 1; pn.max = 128; pn.value = 1; pn.style.width = '70px';
  const sv = el('button', '', 'Save'), rc = el('button', '', 'Recall'); sv.onclick = () => saveSnap(clamp(+pn.value - 1, 0, 127)); rc.onclick = () => recallSnap(clamp(+pn.value - 1, 0, 127));
  const ex = el('button', '', 'Export desk file'), im = el('button', '', 'Import desk file'), rs = el('button', '', 'Reset desk to defaults');
  ex.onclick = () => { const a = el('a'); a.href = URL.createObjectURL(new Blob([JSON.stringify({xldesk:1, P:S.P, types:S.types, names:S.names, learn:S.learn, snaps:S.snaps}, null, 1)], {type:'application/json'})); a.download = 'xl-desk.json'; a.click(); };
  im.onclick = () => { const f = el('input'); f.type = 'file'; f.accept = '.json'; f.onchange = async () => { try { const j = JSON.parse(await f.files[0].text()); S.snaps = Object.assign(S.snaps, j.snaps || {}); S.learn = Object.assign(S.learn, j.learn || {}); rev = null; S.snaps.__imp = {P:j.P, types:j.types, names:j.names}; recallSnap('__imp'); delete S.snaps.__imp; save(); } catch(e){ toast('Not a desk file.'); } }; f.click(); };
  rs.onclick = () => { if (!confirm('Reset every control to its default?')) return; S.snaps.__d = {P:XL.defaults(), types:XL.defTypes(), names:{}}; recallSnap('__d'); delete S.snaps.__d; save(); };
  sn.append(kp(el('span', 'hint', 'Program'), pn, sv, rc), kp(ex, im, rs));
  const used = Object.keys(S.snaps).filter(k => /^\d+$/.test(k)).map(Number).sort((a, b) => a - b); if (used.length) sn.appendChild(el('p', 'hint', 'Saved: ' + used.map(n => n + 1).join(', ')));
  const lc = el('div', 'card'); lc.append(el('h2', '', 'Learned controls'));
  const ks = Object.keys(S.learn); if (!ks.length) lc.appendChild(el('p', 'hint', 'None yet. Press Learn in the header, tap a control, move a control on your MIDI controller.'));
  else { const t = el('table'); t.innerHTML = '<tr><th>MIDI</th><th>Control</th><th></th></tr>'; for (const k of ks){ const [ch, cc] = k.split(':'); const tr = el('tr'); tr.append(el('td', '', 'ch ' + (+ch + 1) + ' CC ' + cc), el('td', '', ctlName(S.learn[k]))); const x = el('button', '', '✕'); x.onclick = () => { delete S.learn[k]; rev = null; save(); renderMidi(); }; const td = el('td'); td.appendChild(x); tr.appendChild(td); t.appendChild(tr); } lc.appendChild(t);
    const cl = el('button', '', 'Clear all learned'); cl.onclick = () => { S.learn = {}; rev = null; save(); renderMidi(); }; lc.appendChild(kp(cl)); }
  v.append(c, m, sn, lc); renderMap(v);
}
function ctlName(id){
  if (id[0] === '@') return {'@play':'Transport play', '@stop':'Transport stop', '@rec':'Transport record', '@rtz':'Return to zero', '@rew':'Rewind (hold)', '@ff':'Fast forward (hold)', '@soloclear':'Solo clear', '@sub':'SUB (current speakers)'}[id] || id;
  const sp = spec(id), m = /^(m|s|r)(\d+)\./.exec(id), pre = m ? ({m:'Ch ', s:'Stereo ', r:'Return '}[m[1]] + m[2] + ' · ') : /^slot(\d+)\.\d/.test(id) ? 'Slot ' + id.match(/^slot(\d+)/)[1] + ' · ' : /^ins(\d+)\.\d/.test(id) ? 'Ch ' + id.match(/^ins(\d+)/)[1] + ' INS · ' : '';
  return pre + (sp ? sp.lb : id);
}
function renderMap(v){
  const c = el('div', 'card'), d = el('details'); d.appendChild(el('summary', '', 'Default MIDI map (works with no setup)'));
  const strip = Object.entries(XL.STRIP_CC).map(([k, cc]) => cc + ' ' + (XL.P['m1.' + k] || XL.P['m9.' + k]).lb).join(' · ');
  d.append(el('p', 'hint', 'MIDI channel n controls mono channel n (1-16): ' + strip + '. CC 102-117: that channel\'s 500 slot module, in the order of its controls. Pitch bend on channel n moves fader n.'));
  const t = el('table'); t.innerHTML = '<tr><th>MIDI</th><th>Control</th></tr>';
  const rows = Object.entries(XL.MIDIMAP).filter(([k]) => +k.split(':')[1] >= 40 && +k.split(':')[1] < 64).sort((a, b) => { const [c1, n1] = a[0].split(':').map(Number), [c2, n2] = b[0].split(':').map(Number); return c1 - c2 || n1 - n2; });
  for (const [k, id] of rows){ const [ch, cc] = k.split(':'); const tr = el('tr'); tr.append(el('td', '', 'ch ' + (+ch + 1) + ' CC ' + cc), el('td', '', ctlName(id))); t.appendChild(tr); }
  d.appendChild(t); c.appendChild(d); v.appendChild(c);
}
window.renderMidi = renderMidi;
if (S.midi.on) midiInit();
