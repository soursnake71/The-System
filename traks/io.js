/* Omega Traks files and MIDI: the CASSETTE (backup .json, the same file the plugin saves), your own sounds into a
   sound's ROM, WAV export (offline render through the same engine and firmware), MIDI file export, and Web MIDI:
   notes in (General MIDI drums, velocity), clock in / out, notes out, program change selects patterns 00-99. */
'use strict';
(() => {
const T = window.TRAKS, S = T.S, D = OTRAKS, M = OTMACH;
const $ = id => document.getElementById(id);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
const NOTE_MAP = [36, 38, 37, 48, 45, 49, 51, 42, 46, 39, 54, 56, 69];
function noteToVoice(n){
  const i = NOTE_MAP.indexOf(n); if (i >= 0) return i;
  return {35:0, 40:1, 50:3, 47:3, 43:4, 41:4, 57:5, 52:5, 55:5, 59:6, 53:6, 44:7, 70:12}[n] ?? -1;
}
// in the Android app (TraksActivity) Web MIDI and downloads are native: shape them like the browser's
if (window.AndroidMidi && AndroidMidi.available()){
  const access = {inputs:new Map(), outputs:new Map(), onstatechange:null, sysexEnabled:false};
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
  window.__drMidi = (id, bytes) => { const p = access.inputs.get(id); if (p && p._h) p._h({data:Uint8Array.from(bytes), target:p}); };
  window.__drMidiChanged = () => { scan(); if (access.onstatechange) access.onstatechange({}); };
  scan();
  navigator.requestMIDIAccess = () => Promise.resolve(access);
}
async function download(name, data, type){
  if (window.AndroidTraks){
    const blob = new Blob([data], {type});
    const b64s = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
    const where = AndroidTraks.save(b64s, name, type || 'application/octet-stream');
    T.toast(where.startsWith('error') ? "Couldn't save: " + where.slice(7) : 'Saved to ' + where, 4000); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([data], {type})); a.download = name; document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000); }
function pick(accept){ return new Promise(res => { const f = $('filePick'); f.value = ''; f.accept = accept; f.onchange = () => res(f.files[0] || null); f.click(); }); }

/* ---- the cassette */
async function saveTape(){ download('omega-traks.json', JSON.stringify(T.backup()), 'application/json'); T.toast('Saved the machine (patterns, songs, sounds, settings)'); }
async function loadTape(){
  const f = await pick('.json,application/json'); if (!f) return;
  try { T.applyBackup(JSON.parse(await f.text())); T.fillPresets(); T.save(); T.toast('Loaded ' + f.name); } catch (e){ T.toast('Not an Omega Traks backup'); }
}
/* ---- a sound into a ROM: decode, mono, resample to 25 kHz, normalise, compand to 8 bits */
async function loadSound(v){
  const f = await pick('audio/*,video/*,.wav,.mp3,.aif,.aiff,.flac,.ogg,.m4a'); if (!f) return;
  try {
    const ac = new (window.OfflineAudioContext || window.webkitOfflineAudioContext)(1, 1, 25000);
    const buf = await ac.decodeAudioData(await f.arrayBuffer());
    const len = Math.min(buf.length, Math.round(buf.sampleRate * 8)), mono = new Float32Array(len);
    for (let c = 0; c < buf.numberOfChannels; c++){ const x = buf.getChannelData(c); for (let i = 0; i < len; i++) mono[i] += x[i] / buf.numberOfChannels; }
    const step = buf.sampleRate / 25000, n = Math.floor((len - 1) / step), rs = new Float32Array(n); let pk = 1e-9;
    for (let i = 0; i < n; i++){ const p = i * step, k = Math.floor(p), fr = p - k; rs[i] = mono[k] + (mono[Math.min(k + 1, len - 1)] - mono[k]) * fr; pk = Math.max(pk, Math.abs(rs[i])); }
    const codes = new Uint8Array(n); for (let i = 0; i < n; i++) codes[i] = D.muEncode(rs[i] / pk * 0.97);
    S.roms[v] = {name:f.name.replace(/\.[^.]+$/, '').slice(0, 30), rate:25000, codes:T.b64.enc(codes)};
    T.sendRoms(); T.save(); T.refresh(); T.toast(`${f.name} burned into ${D.KEY_NAMES[v]}`);
  } catch (e){ console.warn(e); T.toast("Couldn't read that file"); }
}

/* ---- WAV export: the same engine and firmware, offline */
async function render(song, bars, rate){
  await T.loadRom();
  const e = new D.Engine(rate); for (let v = 0; v < D.NV; v++){ const r = T.romOf(v); if (r) e.setRom(v, r.codes, r.rate); } e.setAll(S.params);
  const m = new M.Machine(e, rate); m.mem = JSON.parse(JSON.stringify(S.mem)); m.pat = m.ppat = S.pat; m.song = S.song; m.bpm = S.params[D.P.tempo];
  m.group = song ? 1 : 0; m.start();
  if (!m.running) throw new Error(song ? 'The song is empty' : 'Nothing to play');
  let total;
  if (song){ total = 0; let bpm = m.bpm; for (const st of S.mem.songs[S.song].steps){ if (st.tempo > 0) bpm = st.tempo; total += M.patLen(S.mem.pats[st.p]) * rate * 60 / bpm / M.PPQ; } }
  else { const pt = S.mem.pats[S.pat]; total = bars * pt.num * (4 / pt.den) * rate * 60 / m.bpm; }
  const n = Math.round(total + rate * 2.5), L = new Float64Array(n), R = new Float64Array(n);
  for (let o = 0; o < n; o += 512){ if (!song && o >= total) m.running && m.stop(); m.render(L, R, o, Math.min(512, n - o)); }
  const b = new DataView(new ArrayBuffer(44 + n * 4)), w = (o, s) => { for (let i = 0; i < s.length; i++) b.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); b.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); b.setUint32(16, 16, true); b.setUint16(20, 1, true); b.setUint16(22, 2, true);
  b.setUint32(24, rate, true); b.setUint32(28, rate * 4, true); b.setUint16(32, 4, true); b.setUint16(34, 16, true); w(36, 'data'); b.setUint32(40, n * 4, true);
  for (let i = 0; i < n; i++){ b.setInt16(44 + i * 4, Math.max(-1, Math.min(1, L[i])) * 32767, true); b.setInt16(46 + i * 4, Math.max(-1, Math.min(1, R[i])) * 32767, true); }
  return b.buffer;
}
/* ---- MIDI file: the pattern (or the song) as General MIDI drums on channel 10 */
function midiFile(song){
  const ev = []; let t0 = 0;
  const steps = song ? S.mem.songs[S.song].steps.map(s => s.p) : [S.pat];
  for (const p of steps){ const pt = S.mem.pats[p]; for (const x of pt.ev) ev.push([t0 + x.t * 2, NOTE_MAP[x.v], x.a ? 127 : Math.round(100 * (x.vel || 1))]); t0 += M.patLen(pt) * 2; }
  const on = []; for (const [t, n, vel] of ev){ on.push([t, 0x99, n, vel]); on.push([t + 24, 0x89, n, 0]); }
  on.sort((a, b) => a[0] - b[0]);
  const bytes = [], vlq = x => { const s = [x & 0x7f]; while ((x >>= 7)) s.unshift((x & 0x7f) | 0x80); bytes.push(...s); };
  const us = Math.round(60000000 / S.params[D.P.tempo]);
  vlq(0); bytes.push(0xff, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255);
  let last = 0; for (const [t, st, n, v] of on){ vlq(t - last); last = t; bytes.push(st, n, v); }
  vlq(0); bytes.push(0xff, 0x2f, 0);
  const head = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 0x4d, 0x54, 0x72, 0x6b, bytes.length >> 24 & 255, bytes.length >> 16 & 255, bytes.length >> 8 & 255, bytes.length & 255];
  return new Uint8Array(head.concat(bytes));
}
{
  const pg = $('file');
  const b1 = el('div', 'box', pg, '<h3>CASSETTE (BACKUP)</h3><p>All 100 patterns, 100 songs, your sounds and every setting, in one file. The plugin\'s SAVE / LOAD keys read and write the same file.</p>');
  el('button', 'small', b1, 'SAVE (.json)').onclick = saveTape; el('button', 'small', b1, 'LOAD').onclick = loadTape;
  const b2 = el('div', 'box', pg, '<h3>AUDIO</h3><p>Renders with everything you hear: sounds, mixer, effects.</p>');
  const lb = el('label', '', b2, 'Bars of the pattern <input id="wavBars" type="number" min="1" max="64" value="4">');
  const lr = el('label', '', b2, 'Rate <select id="wavRate"><option>44100</option><option selected>48000</option></select>');
  const go = async song => { try { T.toast('Rendering...'); const buf = await render(song, Math.max(1, Math.min(64, +$('wavBars').value || 4)), +$('wavRate').value);
    download(song ? `omega-traks-song-${String(S.song).padStart(2, '0')}.wav` : `omega-traks-pattern-${String(S.pat).padStart(2, '0')}.wav`, buf, 'audio/wav'); T.toast('Exported'); } catch (e){ T.toast(e.message); } };
  el('button', 'small', b2, 'EXPORT PATTERN (.wav)').onclick = () => go(false); el('button', 'small', b2, 'EXPORT SONG (.wav)').onclick = () => go(true);
  void lb; void lr;
  const b3 = el('div', 'box', pg, '<h3>MIDI FILES</h3><p>General MIDI drum notes on channel 10, for your DAW.</p>');
  el('button', 'small', b3, 'EXPORT PATTERN (.mid)').onclick = () => download(`omega-traks-pattern-${String(S.pat).padStart(2, '0')}.mid`, midiFile(false), 'audio/midi');
  el('button', 'small', b3, 'EXPORT SONG (.mid)').onclick = () => download(`omega-traks-song-${String(S.song).padStart(2, '0')}.mid`, midiFile(true), 'audio/midi');
  const b4 = el('div', 'box', pg, '<h3>RESET</h3><p>Back to the factory memory (patterns 00-15, the demo song 00); your sounds and presets stay.</p>');
  el('button', 'small', b4, 'FACTORY MEMORY').onclick = () => { if (!confirm('Erase all patterns and songs?')) return; S.mem = M.defaultMemory(); T.post({type:'mem', mem:S.mem}); T.save(); T.toast('Factory memory'); };
}

/* ---- Web MIDI */
let access = null;
const status = t => { const s = $('midiStatus'); if (s) s.textContent = t; };
function outPort(){ return access && S.midi.out ? access.outputs.get(S.midi.out) : null; }
function onMidi(ev){
  const [st, d1, d2] = ev.data, type = st & 0xf0, ch = st & 15;
  if (st === 0xf8){ if (S.midi.sync) T.post({type:'clock'}); return; }
  if (st === 0xfa || st === 0xfb){ if (S.midi.sync) T.post({type:'midistart'}); return; }
  if (st === 0xfc){ if (S.midi.sync) T.post({type:'midistop'}); return; }
  if (S.midi.ch !== 16 && ch !== S.midi.ch) return;
  if (type === 0x90 && d2 > 0){ const v = noteToVoice(d1); if (v >= 0){ if (!S.power) T.startAudio(); T.post({type:'note', v, vel:d2 / 127, acc:d2 >= S.midi.accVel}); } }
  else if (type === 0x80 || (type === 0x90 && d2 === 0)){ const v = noteToVoice(d1); if (v >= 0) T.post({type:'off', v}); }
  else if (type === 0xc0 && S.midi.pc){ const n = d1 % 100, s = String(n).padStart(2, '0'); T.post({type:'key', id:M.K.D0 + +s[0], down:true}); T.post({type:'key', id:M.K.D0 + +s[1], down:true}); }
}
function bindInputs(){ if (!access) return; for (const i of access.inputs.values()) i.onmidimessage = S.midi.in === 'none' || (S.midi.in !== 'all' && S.midi.in !== i.id) ? null : onMidi; }
T.listeners.push(m => {
  const o = outPort(); if (!o) return;
  if (m.type === 'hit' && S.midi.notesOut){ const n = NOTE_MAP[m.v], ch = S.midi.ch === 16 ? 9 : S.midi.ch; o.send([0x90 | ch, n, m.acc ? 127 : Math.max(1, Math.round(m.vel * 100))]); o.send([0x80 | ch, n, 0], performance.now() + 40); }
  else if (m.type === 'clock' && S.midi.clockOut) o.send([0xf8]);
  else if (m.type === 'start' && S.midi.clockOut) o.send([0xfa]);
  else if (m.type === 'stop' && S.midi.clockOut) o.send([0xfc]);
});
{
  const pg = $('midi');
  const b1 = el('div', 'box', pg, '<h3>PORTS</h3>');
  el('label', '', b1, 'Input <select id="midiIn"><option value="all">All inputs</option><option value="none">None</option></select>');
  el('label', '', b1, 'Output <select id="midiOut"><option value="">None</option></select>');
  const chs = Array.from({length:16}, (_, i) => `<option value="${i}">${i + 1}</option>`).join('') + '<option value="16">Omni</option>';
  el('label', '', b1, `Channel <select id="midiCh">${chs}</select>`);
  el('p', '', b1, '<span id="midiStatus">Web MIDI not started.</span>');
  el('button', 'small', b1, 'START MIDI').onclick = startMidi;
  const b2 = el('div', 'box', pg, '<h3>SYNC &amp; NOTES</h3>');
  const cb = (id, text) => { const l = el('label', '', b2, `<input type="checkbox" id="${id}"> ${text}`); return l.querySelector('input'); };
  const sync = cb('midiSync', 'Follow MIDI clock in (start / stop)'), clk = cb('midiClockOut', 'Send MIDI clock, start / stop'), nts = cb('midiNotesOut', 'Send the notes the machine plays'), pc = cb('midiPc', 'Program change 0-99 selects the pattern');
  el('label', '', b2, 'Accent from velocity &ge; <input id="midiAccVel" type="number" min="1" max="127">');
  const b3 = el('div', 'box', pg, '<h3>NOTE MAP (GENERAL MIDI)</h3><p>' + D.KEY_NAMES.map((n, v) => `${n} ${NOTE_MAP[v]}`).join(' · ') + '</p>');
  void b3;
  const sync2ui = () => { $('midiCh').value = String(S.midi.ch); sync.checked = S.midi.sync; clk.checked = S.midi.clockOut; nts.checked = S.midi.notesOut; pc.checked = S.midi.pc; $('midiAccVel').value = S.midi.accVel; $('midiIn').value = S.midi.in; };
  sync2ui();
  const upd = () => { S.midi.ch = +$('midiCh').value; S.midi.sync = sync.checked; S.midi.clockOut = clk.checked; S.midi.notesOut = nts.checked; S.midi.pc = pc.checked;
    S.midi.accVel = Math.max(1, Math.min(127, +$('midiAccVel').value || 110)); S.midi.in = $('midiIn').value; S.midi.out = $('midiOut').value;
    T.post({type:'ext', on:S.midi.sync}); T.post({type:'hitsOut', on:!!S.midi.out && S.midi.notesOut}); bindInputs(); T.save(); };
  for (const id of ['midiCh', 'midiSync', 'midiClockOut', 'midiNotesOut', 'midiPc', 'midiAccVel', 'midiIn', 'midiOut']) $(id).addEventListener('change', upd);
  T.listeners.push(m => { if (m.type === 'disp' && !T.__midiSent){ T.__midiSent = true; T.post({type:'ext', on:S.midi.sync}); T.post({type:'hitsOut', on:!!S.midi.out && S.midi.notesOut}); } });
}
async function startMidi(){
  if (!navigator.requestMIDIAccess){ status('This browser has no Web MIDI.'); return; }
  try {
    access = await navigator.requestMIDIAccess({sysex:false});
    const fill = () => {
      const ins = $('midiIn'), outs = $('midiOut');
      ins.innerHTML = '<option value="all">All inputs</option><option value="none">None</option>' + [...access.inputs.values()].map(i => `<option value="${i.id}">${i.name}</option>`).join('');
      outs.innerHTML = '<option value="">None</option>' + [...access.outputs.values()].map(o => `<option value="${o.id}">${o.name}</option>`).join('');
      ins.value = S.midi.in; outs.value = access.outputs.has(S.midi.out) ? S.midi.out : '';
      status(`${access.inputs.size} in, ${access.outputs.size} out`); bindInputs();
    };
    access.onstatechange = fill; fill();
  } catch (e){ status('MIDI was refused: ' + e.message); }
}
if (window.AndroidMidi) startMidi();
else if (navigator.requestMIDIAccess && navigator.permissions) navigator.permissions.query({name:'midi'}).then(p => { if (p.state === 'granted') startMidi(); }).catch(() => {});
window.TRAKSIO = {saveTape, loadTape, loadSound, render, midiFile};
})();
