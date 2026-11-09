/* Omega 110: import / export and MIDI. Loaded after app.js (uses window.DRAPP).
   Files: backup (.json: patterns, songs, mixer, reverb, MIDI settings), MIDI files (GM drum notes, channel 10) out of a
   pattern or a song and into a pattern, WAV renders, and your own sounds on the voices.
   MIDI (Web MIDI, or the Android app's native MIDI): notes in play the voices (velocity >= the accent setting is an
   accent; in WRITE TAP they record), the ACC and SWEEP notes work like the rear trigger inputs, program change picks
   A1-D8, clock in drives the sequencer (start / stop / continue), clock and notes out. */
'use strict';
(() => {
const A = window.DRAPP, S = A.S, {NV, VOICE_NAMES, P} = DR110;

/* ---------------- Android app: native MIDI shaped like Web MIDI ---------------- */
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

/* ---------------- MIDI ---------------- */
if (!S.midi.notes || S.midi.notes.length < 7) S.midi.notes = [36, 38, 46, 42, 49, 39, 37];
if (S.midi.accNote === undefined) S.midi.accNote = 33;
if (S.midi.sweepNote === undefined) S.midi.sweepNote = 34;
let access = null, lastClock = 0, clockTimes = [], accHeldMidi = false;
const $ = id => document.getElementById(id);
const status = s => { $('midiStatus').textContent = s; };
function outPort(){ return access && S.midi.out ? access.outputs.get(S.midi.out) : null; }
function send(bytes){ const o = outPort(); if (o) try { o.send(bytes); } catch (e){} }
function noteOut(v, acc){
  if (!S.midi.notesOut || !outPort()) return;
  const ch = S.midi.ch, n = S.midi.notes[v];
  send([0x90 | ch, n, acc ? 127 : 90]);
  setTimeout(() => send([0x80 | ch, n, 0]), 30);
}
function transport(on){ if (S.midi.clockOut) send([on ? 0xFA : 0xFC]); }
A.listeners.push(m => {
  if (m.type === 'hit') noteOut(m.v, m.acc);
  else if (m.type === 'clock' && S.midi.clockOut && !S.midi.sync) send([0xF8]);
});
function onMessage(e){
  if (S.midi.in === 'none' || (S.midi.in !== 'all' && e.target && e.target.id !== S.midi.in)) return;
  const d = e.data, st = d[0];
  if (st === 0xF8){                      // clock
    const now = performance.now(); lastClock = now;
    clockTimes.push(now); if (clockTimes.length > 48) clockTimes.shift();
    if (clockTimes.length >= 24){ const per = (clockTimes[clockTimes.length - 1] - clockTimes[0]) / (clockTimes.length - 1); const bpm = 60000 / (per * 24);
      if (bpm > 30 && bpm < 300 && S.midi.sync && Math.abs(bpm - S.params[P.tempo]) > 0.5){ S.params[P.tempo] = Math.round(bpm * 10) / 10; } }
    if (S.midi.sync){ A.post({type:'clock'}); if (S.midi.clockOut) send([0xF8]); }
    return;
  }
  if (S.midi.sync && (st === 0xFA || st === 0xFB)){ if (!S.power) return; S.running = true; S.play = {step:-1, pat:A.patIndex(), measure:0};
    A.post({type:'ext', on:true}); A.post({type:'start', mode:S.mode === 'song' ? 'song' : 'pattern', song:S.song, pat:A.patIndex()}); A.draw(); return; }
  if (S.midi.sync && st === 0xFC){ A.post({type:'stop'}); S.running = false; A.draw(); return; }
  const type = st & 0xF0, ch = st & 15;
  if (S.midi.ch !== 16 && ch !== S.midi.ch && type !== 0xF0) return;     // 16 = omni
  if (!S.power) return;
  if (type === 0x90 && d[2] > 0){
    const n = d[1];
    if (n === S.midi.accNote){ accHeldMidi = true; A.post({type:'sweep'}); return; }
    if (n === S.midi.sweepNote){ A.post({type:'sweep'}); return; }
    const v = S.midi.notes.indexOf(n); if (v < 0) return;
    const acc = d[2] >= S.midi.accVel || accHeldMidi;
    if (S.mode === 'tap' && S.running) A.post({type:'tap', v, acc});
    else if (S.mode === 'step'){ const pi = A.patIndex(); if (A.userPat(pi)){ S.mem.pats[pi].st[S.cursor] |= (1 << v) | (acc ? DRSEQ.ACC : 0); A.memChanged(); } A.post({type:'hit', v, acc}); }
    else A.post({type:'hit', v, acc});
  } else if (type === 0x80 || (type === 0x90 && d[2] === 0)){ if (d[1] === S.midi.accNote) accHeldMidi = false; const v = S.midi.notes.indexOf(d[1]); if (v >= 0) A.post({type:'release', v}); }
  else if (type === 0xC0 && S.midi.pc && d[1] < 32){ S.bank = d[1] >> 3; S.rhythm = d[1] & 7; A.post({type:'select', pat:d[1]}); A.draw(); }
}
function hook(){
  if (!access) return;
  for (const i of access.inputs.values()) i.onmidimessage = onMessage;
  const fill = (sel, ports, first) => { const cur = sel.value; sel.innerHTML = first; for (const p of ports.values()){ const o = document.createElement('option'); o.value = p.id; o.textContent = p.name; sel.appendChild(o); } sel.value = cur; if (sel.value !== cur) sel.value = sel.options[0].value; };
  fill($('midiIn'), access.inputs, '<option value="all">All inputs</option><option value="none">None</option>');
  fill($('midiOut'), access.outputs, '<option value="">None</option>');
  $('midiIn').value = S.midi.in; if ($('midiIn').value !== S.midi.in) $('midiIn').value = 'all';
  $('midiOut').value = S.midi.out; if ($('midiOut').value !== S.midi.out) $('midiOut').value = '';
  status(`${access.inputs.size} input(s), ${access.outputs.size} output(s).`);
}
function startMidi(){
  if (access || !navigator.requestMIDIAccess) { if (!navigator.requestMIDIAccess) status('This browser has no Web MIDI (use Chrome, Edge or the Android app).'); return; }
  navigator.requestMIDIAccess({sysex:false}).then(a => { access = a; a.onstatechange = hook; hook(); }).catch(e => status('MIDI refused: ' + e.message));
}
function midiUi(){
  const ch = $('midiCh'); ch.innerHTML = '';
  for (let i = 0; i < 17; i++){ const o = document.createElement('option'); o.value = i; o.textContent = i === 16 ? 'Omni (in)' : String(i + 1) + (i === 9 ? ' (drums)' : ''); ch.appendChild(o); }
  ch.value = S.midi.ch; ch.onchange = () => { S.midi.ch = +ch.value; A.save(); };
  $('midiIn').onchange = e => { S.midi.in = e.target.value; A.save(); };
  $('midiOut').onchange = e => { S.midi.out = e.target.value; A.save(); };
  const cb = (id, key, after) => { const c = $(id); c.checked = !!S.midi[key]; c.onchange = () => { S.midi[key] = c.checked; A.save(); if (after) after(); }; };
  cb('midiSync', 'sync', () => A.post({type:'ext', on:S.midi.sync}));
  cb('midiClockOut', 'clockOut'); cb('midiNotesOut', 'notesOut'); cb('midiPc', 'pc');
  const av = $('midiAccVel'); av.value = S.midi.accVel; av.onchange = () => { S.midi.accVel = Math.max(1, Math.min(127, +av.value || 100)); A.save(); };
  const map = $('noteMap'); map.innerHTML = '';
  const row = (label, get, set) => { const l = document.createElement('label'); l.innerHTML = `<span>${label}</span><input type="number" min="0" max="127">`; const i = l.querySelector('input'); i.value = get(); i.onchange = () => { set(Math.max(0, Math.min(127, +i.value || 0))); A.save(); }; map.appendChild(l); };
  for (let v = 0; v < NV; v++) row(VOICE_NAMES[v], () => S.midi.notes[v], n => { S.midi.notes[v] = n; });
  row('ACC trigger', () => S.midi.accNote, n => { S.midi.accNote = n; });
  row('SWEEP trigger', () => S.midi.sweepNote, n => { S.midi.sweepNote = n; });
  A.post({type:'ext', on:!!S.midi.sync});
}
document.querySelector('[data-tab=midi]').addEventListener('click', startMidi);
// start MIDI with the machine (a key press counts as the gesture some browsers want)
document.getElementById('plate').addEventListener('pointerdown', () => { if (S.power) startMidi(); });

/* ---------------- files ---------------- */
async function saveFile(blob, name){
  if (window.AndroidDR110){
    const b64 = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
    const where = AndroidDR110.save(b64, name, blob.type || 'application/octet-stream');
    if (where.startsWith('error')) throw new Error(where.slice(7));
    A.toast('Saved to ' + where, 4000); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
  A.toast('Saved ' + name);
}
function pick(accept){
  return new Promise(res => { const f = $('filePick'); f.accept = accept; f.value = ''; f.onchange = () => res(f.files[0] || null); f.click(); });
}
const patName = i => 'ABCD'[i >> 3] + ((i & 7) + 1);
const stamp = () => new Date().toISOString().slice(0, 16).replace(/[-:T]/g, '');

$('expJson').onclick = () => saveFile(new Blob([JSON.stringify(A.backup(), null, 1)], {type:'application/json'}), `omega-110-${stamp()}.json`).catch(e => A.toast(e.message));
$('impJson').onclick = async () => { const f = await pick('.json,application/json'); if (!f) return;
  try { A.applyBackup(JSON.parse(await f.text())); A.save(); midiUi(); A.toast('Backup loaded'); } catch (e){ A.toast(e.message, 4000); } };

/* MIDI files: SMF type 0, 96 ticks per quarter */
const PPQ = 96;
function vlq(n){ const b = [n & 0x7F]; while ((n >>= 7)) b.unshift((n & 0x7F) | 0x80); return b; }
function smf(measures, name){
  const ev = [];   // [tick, bytes]
  let t = 0;
  for (const pi of measures){
    const pt = S.mem.pats[pi], per = PPQ * 4 / pt.len;
    for (let s = 0; s < pt.len; s++){
      const bits = pt.st[s], at = Math.round(t + s * per);
      for (let v = 0; v < NV; v++) if (bits & (1 << v)){
        ev.push([at, [0x99, S.midi.notes[v], bits & DRSEQ.ACC ? 127 : 90]]); ev.push([at + PPQ / 8, [0x89, S.midi.notes[v], 0]]);
      }
    }
    t += PPQ * 4;
  }
  ev.push([t, null]);
  ev.sort((a, b) => a[0] - b[0] || (a[1] ? (a[1][0] & 0xF0) === 0x80 ? -1 : 1 : 1));
  const tempo = Math.round(60000000 / S.params[P.tempo]);
  const nm = Array.from(new TextEncoder().encode(name));
  const trk = [0, 0xFF, 0x03, ...vlq(nm.length), ...nm, 0, 0xFF, 0x51, 3, tempo >> 16 & 255, tempo >> 8 & 255, tempo & 255, 0, 0xFF, 0x58, 4, 4, 2, 24, 8];
  let last = 0;
  for (const [at, b] of ev){ trk.push(...vlq(at - last)); last = at; if (b) trk.push(...b); else trk.push(0xFF, 0x2F, 0); }
  const hdr = [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, PPQ >> 8, PPQ & 255];
  const len = trk.length;
  return new Blob([new Uint8Array([...hdr, 0x4D, 0x54, 0x72, 0x6B, len >>> 24, len >> 16 & 255, len >> 8 & 255, len & 255, ...trk])], {type:'audio/midi'});
}
const songMeasures = () => S.mem.songs[S.song].m.slice();
$('expMidPat').onclick = () => saveFile(smf([A.patIndex()], 'Pattern ' + patName(A.patIndex())), `dr110-pattern-${patName(A.patIndex())}.mid`).catch(e => A.toast(e.message));
$('expMidSong').onclick = () => { const m = songMeasures(); if (!m.length) return A.toast('Song ' + (S.song ? 'II' : 'I') + ' is empty');
  saveFile(smf(m, 'Song ' + (S.song ? 'II' : 'I')), `dr110-song-${S.song ? 'II' : 'I'}.mid`).catch(e => A.toast(e.message)); };
function parseSmf(buf){
  const d = new DataView(buf); let o = 0;
  const str = () => String.fromCharCode(d.getUint8(o), d.getUint8(o + 1), d.getUint8(o + 2), d.getUint8(o + 3));
  if (str() !== 'MThd') throw new Error('Not a MIDI file');
  const ntr = d.getUint16(o + 10), div = d.getUint16(o + 12); if (div & 0x8000) throw new Error('SMPTE timing is not supported');
  o += 8 + d.getUint32(o + 4);
  const notes = [];
  for (let tr = 0; tr < ntr && o < buf.byteLength; tr++){
    if (str() !== 'MTrk'){ o += 8 + d.getUint32(o + 4); continue; }
    const end = o + 8 + d.getUint32(o + 4); o += 8; let t = 0, run = 0;
    const rv = () => { let v = 0, b; do { b = d.getUint8(o++); v = (v << 7) | (b & 0x7F); } while (b & 0x80); return v; };
    while (o < end){
      t += rv(); let st = d.getUint8(o);
      if (st & 0x80) o++; else st = run;
      if (st === 0xFF){ o++; const l = rv(); o += l; continue; }
      if (st === 0xF0 || st === 0xF7){ const l = rv(); o += l; continue; }
      run = st; const ty = st & 0xF0, a = d.getUint8(o++), b = ty === 0xC0 || ty === 0xD0 ? 0 : d.getUint8(o++);
      if (ty === 0x90 && b > 0) notes.push({t, n:a, v:b});
    }
    o = end;
  }
  return {ppq:div, notes};
}
// notes from other drum maps that should still land on a voice
const ALIAS = {35:0, 36:0, 37:6, 38:1, 40:1, 39:5, 42:3, 44:3, 46:2, 49:4, 51:4, 52:4, 55:4, 57:4, 75:6, 76:6, 77:6};
$('impMid').onclick = async () => {
  const f = await pick('.mid,.midi,audio/midi'); if (!f) return;
  try {
    const {ppq, notes} = parseSmf(await f.arrayBuffer());
    let pi = A.patIndex(); if (!A.userPat(pi)){ S.bank = 2 + (S.bank & 1); pi = A.patIndex(); }
    const first = notes.length ? Math.floor(Math.min(...notes.map(x => x.t)) / (ppq * 4)) * ppq * 4 : 0;
    // 12 or 16 steps: whichever grid the notes fit better
    const fit = len => notes.reduce((e, x) => { const q = (x.t - first) / (ppq * 4 / len); return e + Math.abs(q - Math.round(q)); }, 0);
    const len = notes.length && fit(12) < fit(16) * 0.6 ? 12 : 16;
    const pt = {len, st:new Array(16).fill(0)}; let used = 0;
    for (const x of notes){
      const st = Math.round((x.t - first) / (ppq * 4 / len)); if (st < 0 || st >= len) continue;
      let v = S.midi.notes.indexOf(x.n); if (v < 0) v = ALIAS[x.n] ?? -1; if (v < 0) continue;
      pt.st[st] |= 1 << v; if (x.v >= S.midi.accVel) pt.st[st] |= DRSEQ.ACC; used++;
    }
    S.mem.pats[pi] = pt; A.memChanged(); A.toast(`${used} notes into ${patName(pi)} (${len} steps)`);
  } catch (e){ A.toast(e.message, 4000); }
};

/* WAV: the engine and sequencer run offline with what you hear */
function wav(L, R, sr){
  const n = L.length, b = new ArrayBuffer(44 + n * 4), d = new DataView(b);
  const w = (o, s) => { for (let i = 0; i < 4; i++) d.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); d.setUint32(4, 36 + n * 4, true); w(8, 'WAVE'); w(12, 'fmt '); d.setUint32(16, 16, true); d.setUint16(20, 1, true); d.setUint16(22, 2, true);
  d.setUint32(24, sr, true); d.setUint32(28, sr * 4, true); d.setUint16(32, 4, true); d.setUint16(34, 16, true); w(36, 'data'); d.setUint32(40, n * 4, true);
  for (let i = 0, o = 44; i < n; i++, o += 4){ d.setInt16(o, Math.max(-1, Math.min(1, L[i])) * 32767, true); d.setInt16(o + 2, Math.max(-1, Math.min(1, R[i])) * 32767, true); }
  return new Blob([b], {type:'audio/wav'});
}
async function render(song){
  const sr = +$('wavRate').value, e = new DR110.Engine(sr), q = new DRSEQ.Sequencer(e, sr);
  e.setAll(A.engineParams());
  A.samples.forEach((s, v) => { if (s) e.setSample(v, s.d, s.sr, s.acc); });
  q.mem = JSON.parse(JSON.stringify(S.mem)); q.bpm = S.params[P.tempo]; q.swing = S.swing; q.song = S.song; q.pat = A.patIndex();
  q.mode = song ? 'song' : 'pattern';
  // a song plays once through (a D.C. repeat would wrap the measure back to 1: that's the end)
  let last = 0, done = false;
  const target = song ? S.mem.songs[S.song].m.length : Math.max(1, Math.min(64, +$('wavBars').value || 4));
  q.onStep = (step, pat, measure) => { if (measure >= target || measure < last) done = true; last = measure; };
  q.onStop = () => { done = true; };
  q.start();
  const chunks = [], blk = 4096, tail = sr * 2; let tailLeft = -1, total = 0;
  while (true){
    const L = new Float64Array(blk), R = new Float64Array(blk);
    if (done && q.running) q.stop();
    if (done && tailLeft < 0) tailLeft = tail;
    q.render(L, R, 0, blk); chunks.push([L, R]); total += blk;
    if (tailLeft >= 0 && (tailLeft -= blk) <= 0) break;
    if (total > sr * 60 * 20) break;
    if (chunks.length % 64 === 0) await new Promise(r => setTimeout(r));
  }
  const L = new Float32Array(total), R = new Float32Array(total);
  chunks.forEach(([l, r], k) => { L.set(l, k * blk); R.set(r, k * blk); });
  return wav(L, R, sr);
}
$('expWavPat').onclick = async () => { A.toast('Rendering…'); try { await saveFile(await render(false), `dr110-${patName(A.patIndex())}.wav`); } catch (e){ A.toast(e.message); } };
$('expWavSong').onclick = async () => { if (!songMeasures().length) return A.toast('Song ' + (S.song ? 'II' : 'I') + ' is empty');
  A.toast('Rendering the song…'); try { await saveFile(await render(true), `dr110-song-${S.song ? 'II' : 'I'}.wav`); } catch (e){ A.toast(e.message); } };

/* your sounds on the voices */
function sampleRowsRefresh(){
  const box = $('sampleRows'); box.innerHTML = '';
  for (let v = 0; v < NV; v++){
    const r = document.createElement('div'); r.className = 'sampleRow';
    const s = A.samples[v];
    r.innerHTML = `<b>${VOICE_NAMES[v]}</b><span>${s ? s.name : 'circuit only'}</span><button class="small">LOAD</button><button class="small">RESET</button>`;
    const [ld, rs] = r.querySelectorAll('button');
    ld.onclick = async () => {
      const f = await pick('audio/*,.wav,.mp3,.flac,.ogg,.aif,.aiff'); if (!f) return;
      if (!S.power){ S.power = true; await A.startAudio(); A.draw(); }
      try { const a = await A.decode(await f.arrayBuffer()); A.samples[v] = {d:a.d, sr:a.sr, acc:null, name:f.name};
        A.post({type:'sample', v, d:a.d, sr:a.sr, acc:null}); if (S.params[P.src] < .5){ S.params[P.src] = 1; A.post({type:'params', p:A.engineParams()}); A.refreshPanels(); A.save(); }
        A.toast(`${f.name} on ${VOICE_NAMES[v]} (SOURCE: Recordings)`); sampleRowsRefresh(); }
      catch (e){ A.toast('Could not read that file: ' + e.message, 4000); }
    };
    rs.onclick = async () => { if (!A.ctx){ S.power = true; await A.startAudio(); A.draw(); } A.loadRecordings(v); };
    box.appendChild(r);
  }
}
window.DRIO = {noteOut, transport, syncing:() => false, sampleRowsRefresh};
midiUi(); sampleRowsRefresh();
})();
