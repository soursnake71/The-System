/* Omega Linn files and MIDI: STORE / LOAD (the whole machine as a .json, the same file the plugin saves), your own
   sounds into a ROM, LOAD FOLDER (a whole kit, sorted onto the sounds by name: kit.js), WAV export (offline render
   through the same engine and firmware), MIDI files out and in, and Web MIDI: notes in (General MIDI drums, velocity
   picks the dynamic), clock in / out, notes out, the trigger output, program change selects patterns. */
'use strict';
(() => {
const T = window.LINN, S = T.S, D = OLINN, M = OLMACH, KIT = OLKIT;
const $ = id => document.getElementById(id);
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };
/* General MIDI drum notes -> sounds; velocity picks the dynamic where the LinnDrum has more than one */
const lvl = (vel, a, b, c) => c === undefined ? (vel < 80 ? a : b) : vel < 50 ? a : vel < 100 ? b : c;
function noteToSound(n, vel){
  switch (n){
    case 35: case 36: return lvl(vel, 0, 1);
    case 38: case 40: return lvl(vel, 2, 3, 4);
    case 37: case 31: return 5;
    case 42: return lvl(vel, 6, 7); case 44: return 6; case 46: return 8;
    case 48: case 50: return 9; case 45: case 47: return 10; case 41: case 43: return 11;
    case 51: case 53: case 59: return lvl(vel, 12, 13);
    case 49: case 52: case 55: case 57: return 14;
    case 69: return lvl(vel, 15, 16); case 70: return 15;
    case 54: return lvl(vel, 17, 18);
    case 60: case 62: case 63: return 19; case 61: case 64: return 20;
    case 56: return 21; case 39: return 22;
    default: return -1;
  }
}
const SOUND_NOTE = [35, 36, 38, 38, 38, 37, 42, 42, 46, 50, 47, 43, 51, 51, 49, 69, 69, 54, 54, 63, 64, 56, 39];
const SOUND_VEL = [70, 120, 40, 90, 127, 120, 70, 120, 120, 120, 120, 120, 70, 120, 120, 70, 120, 70, 120, 120, 120, 120, 120];
// in the Android app (LinnActivity) Web MIDI and downloads are native: shape them like the browser's
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
  if (window.AndroidLinn){
    const blob = new Blob([data], {type});
    const b64s = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
    const where = AndroidLinn.save(b64s, name, type || 'application/octet-stream');
    T.toast(where.startsWith('error') ? "Couldn't save: " + where.slice(7) : 'Saved to ' + where, 4000); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([data], {type})); a.download = name; document.body.appendChild(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000); }
function pick(accept){ return new Promise(res => { const f = $('filePick'); f.value = ''; f.accept = accept; f.onchange = () => res(f.files[0] || null); f.click(); }); }
function pickMany(folder){ return new Promise(res => { const f = $(folder ? 'folderPick' : 'multiPick'); f.value = ''; if (folder && window.AndroidLinn && AndroidLinn.folderNext) AndroidLinn.folderNext(); f.onchange = () => res([...f.files]); f.click(); }); }

/* ---- STORE / LOAD */
async function saveTape(){ download('omega-linn.json', JSON.stringify(T.backup()), 'application/json'); T.toast('Stored the machine (patterns, songs, sounds, settings)'); }
async function loadTape(){
  const f = await pick('.json,application/json'); if (!f) return;
  try { T.applyBackup(JSON.parse(await f.text())); T.fillPresets(); T.save(); T.toast('Loaded ' + f.name); } catch (e){ T.toast('Not an Omega Linn backup'); }
}
/* ---- sounds: decode at 44.1 kHz, mono */
async function decode(file){
  const ac = new (window.OfflineAudioContext || window.webkitOfflineAudioContext)(1, 1, 44100);
  const buf = await ac.decodeAudioData(await file.arrayBuffer());
  const len = Math.min(buf.length, Math.round(buf.sampleRate * 6)), mono = new Float64Array(len);
  for (let c = 0; c < buf.numberOfChannels; c++){ const x = buf.getChannelData(c); for (let i = 0; i < len; i++) mono[i] += x[i] / buf.numberOfChannels; }
  return {mono, rate:buf.sampleRate};
}
async function loadSound(g){
  const f = await pick('audio/*,video/*,.wav,.mp3,.aif,.aiff,.flac,.ogg,.m4a'); if (!f) return;
  try {
    const {mono, rate} = await decode(f);
    const L = KIT.makeLayer(g, mono, rate, g === D.G.HAT && KIT.isOpen(f.name));
    S.roms[g] = {name:f.name.replace(/\.[^.]+$/, '').slice(0, 30), layers:[T.packLayer(L)]};
    T.sendRoms(); T.save(); T.refresh(); T.toast(`${f.name} burned into the ${D.GROUPS[g]} ROM`);
  } catch (e){ console.warn(e); T.toast("Couldn't read that file"); }
}
// a whole kit: every file sorted onto its sound by name (several snares / toms / congas become tuning layers)
async function loadKit(folder){
  const files = (await pickMany(folder)).filter(f => /\.(wav|aif|aiff|mp3|flac|ogg|m4a)$/i.test(f.name)); if (!files.length) return;
  const names = files.map(f => f.name), {groups, left} = KIT.classify(names);
  T.toast('Loading ' + files.length + ' files...');
  const use = new Set(groups.flat()), recs = [];
  for (let i = 0; i < files.length; i++){
    if (!use.has(i)){ recs.push({mono:new Float64Array(2), rate:44100}); continue; }
    try { recs.push(await decode(files[i])); } catch (e){ console.warn(names[i], e); recs.push({mono:new Float64Array(2), rate:44100}); }
  }
  const kit = KIT.buildKit(names, recs), done = [];
  for (let g = 0; g < D.NG_ROM; g++){
    const ls = kit.layers.filter(L => L.g === g && L.data.length > 16); if (!ls.length) continue;
    S.roms[g] = {name:ls.map(L => names[L.i].replace(/\.[^.]+$/, '')).join(', ').slice(0, 60), layers:ls.map(T.packLayer)};
    done.push(D.GROUPS[g]);
  }
  T.sendRoms(); T.save(); T.refresh();
  T.toast(`Loaded: ${done.join(', ')}` + (left.length ? `. Not used: ${left.map(i => names[i]).slice(0, 6).join(', ')}${left.length > 6 ? '...' : ''}` : ''), 6000);
}
function factoryKit(){ if (!confirm('Put every sound back to the built-in LinnDrum ROM?')) return; S.roms = {}; T.sendRoms(); T.save(); T.refresh(); T.toast('Built-in sounds back'); }

/* ---- WAV export: the same engine and firmware, offline */
async function render(song, bars, rate){
  await T.loadRom();
  const e = new D.Engine(rate); for (let g = 0; g < D.NG_ROM; g++) e.setGroup(g, T.romOf(g)); e.setAll(S.params);
  const m = new M.Machine(e, rate); m.mem = JSON.parse(JSON.stringify(S.mem)); m.pat = m.ppat = S.pat; m.song = S.song; m.bpm = S.params[D.P.tempo]; m.mode = song ? 1 : 0;
  const s = S.mem.songs[S.song];
  if (song ? !s.steps.length : !S.mem.pats[S.pat].ev.length) throw new Error(song ? 'The song is empty' : 'The pattern is empty');
  m.mem.repeat = false; m.start(false, 0);
  let total;
  if (song){ total = 0; for (const p of s.steps) total += S.mem.pats[p].len * rate * 60 / m.bpm / M.PPQ; }
  else total = bars * 4 * M.PPQ * rate * 60 / m.bpm / M.PPQ;
  const n = Math.round(total + rate * 2.5), L = new Float64Array(n), R = new Float64Array(n);
  for (let o = 0; o < n; o += 512){ if (!song && o >= total && m.running) m.stop(); m.render(L, R, o, Math.min(512, n - o)); }
  const b = new DataView(new ArrayBuffer(44 + n * 4)), w = (o, s) => { for (let i = 0; i < s.length; i++) b.setUint8(o + i, s.charCodeAt(i)); };
  w(0, 'RIFF'); b.setUint32(4, 36 + n * 4, true); w(8, 'WAVEfmt '); b.setUint32(16, 16, true); b.setUint16(20, 1, true); b.setUint16(22, 2, true);
  b.setUint32(24, rate, true); b.setUint32(28, rate * 4, true); b.setUint16(32, 4, true); b.setUint16(34, 16, true); w(36, 'data'); b.setUint32(40, n * 4, true);
  for (let i = 0; i < n; i++){ b.setInt16(44 + i * 4, Math.max(-1, Math.min(1, L[i])) * 32767, true); b.setInt16(46 + i * 4, Math.max(-1, Math.min(1, R[i])) * 32767, true); }
  return b.buffer;
}
/* ---- MIDI file out: the pattern (or the song) as General MIDI drums on channel 10, 96 ticks a beat */
function midiFile(song){
  const ev = []; let t0 = 0;
  const steps = song ? S.mem.songs[S.song].steps : [S.pat];
  for (const p of steps){ const pt = S.mem.pats[p]; for (const x of pt.ev) ev.push([t0 + x.t / 2, SOUND_NOTE[x.s], SOUND_VEL[x.s]]); t0 += pt.len / 2; }
  const on = []; for (const [t, n, vel] of ev){ on.push([Math.round(t), 0x99, n, vel]); on.push([Math.round(t) + 12, 0x89, n, 0]); }
  on.sort((a, b) => a[0] - b[0] || (a[1] === 0x89 ? -1 : 1));
  const bytes = [], vlq = x => { const s = [x & 0x7f]; while ((x >>= 7)) s.unshift((x & 0x7f) | 0x80); bytes.push(...s); };
  const us = Math.round(60000000 / S.params[D.P.tempo]);
  vlq(0); bytes.push(0xff, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255);
  let last = 0; for (const [t, st, n, v] of on){ vlq(t - last); last = t; bytes.push(st, n, v); }
  vlq(0); bytes.push(0xff, 0x2f, 0);
  const head = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 0x4d, 0x54, 0x72, 0x6b, bytes.length >> 24 & 255, bytes.length >> 16 & 255, bytes.length >> 8 & 255, bytes.length & 255];
  return new Uint8Array(head.concat(bytes));
}
/* ---- MIDI file in: the drum notes (channel 10, or every channel if there's nothing on 10), at 192 ticks a beat.
   Into a pattern: the notes of the first bars that fit (up to 99 beats). As a song: cut into patterns of BARS bars
   (empty patterns 61-77 first, then any empty one; repeats share a pattern) and the song lists them in order. */
function parseMidi(u8){
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength); let o = 0;
  const str = (p, n) => String.fromCharCode(...u8.subarray(p, p + n));
  if (str(0, 4) !== 'MThd') throw new Error('Not a MIDI file');
  const hl = dv.getUint32(4), ntr = dv.getUint16(10), div = dv.getUint16(12);
  if (div & 0x8000) throw new Error('SMPTE-timed MIDI files are not supported');
  o = 8 + hl; const notes = []; let tempo = 0, num = 4, den = 4;
  for (let t = 0; t < ntr && o + 8 <= u8.length; t++){
    if (str(o, 4) !== 'MTrk'){ o += 8 + dv.getUint32(o + 4); t--; continue; }
    const end = o + 8 + dv.getUint32(o + 4); let p = o + 8, tick = 0, run = 0;
    const vlq = () => { let v = 0, b; do { b = u8[p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80 && p < end); return v; };
    while (p < end){
      tick += vlq(); let st = u8[p];
      if (st & 0x80) p++; else st = run;
      if (st === 0xff){ const ty = u8[p++], len = vlq();
        if (ty === 0x51 && !tempo && len === 3) tempo = 60000000 / ((u8[p] << 16) | (u8[p + 1] << 8) | u8[p + 2]);
        if (ty === 0x58 && len >= 2 && tick === 0){ num = u8[p]; den = 1 << u8[p + 1]; }
        p += len; continue; }
      if (st === 0xf0 || st === 0xf7){ p += vlq(); continue; }
      run = st; const ty = st & 0xf0, ch = st & 15;
      if (ty === 0xc0 || ty === 0xd0){ p += 1; continue; }
      const d1 = u8[p], d2 = u8[p + 1]; p += 2;
      if (ty === 0x90 && d2 > 0) notes.push({tick, ch, n:d1, vel:d2});
    }
    o = end;
  }
  const on10 = notes.filter(x => x.ch === 9), use = on10.length ? on10 : notes;
  return {div, tempo, num, den, notes:use.map(x => ({t:Math.round(x.tick * M.PPQ / div), s:noteToSound(x.n, x.vel)})).filter(x => x.s >= 0)};
}
function quantize(t, ec){ const q = M.EC_GRID[ec]; return Math.round(t / q) * q; }
async function importMidi(asSong){
  const f = await pick('.mid,.midi,audio/midi,audio/x-midi'); if (!f) return;
  let mf; try { mf = parseMidi(new Uint8Array(await f.arrayBuffer())); } catch (e){ T.toast(e.message); return; }
  if (!mf.notes.length){ T.toast('No drum notes in that file'); return; }
  const ec = +$('midiQ').value, barTicks = Math.round(mf.num * 4 / mf.den * M.PPQ);
  const notes = mf.notes.map(x => ({t:ec >= 0 ? quantize(x.t, ec) : x.t, s:x.s}));
  const last = Math.max(...notes.map(x => x.t)), totalBars = Math.max(1, Math.ceil((last + 1) / barTicks));
  const cut = (from, len) => { const ev = [], seen = new Set(); for (const x of notes) if (x.t >= from && x.t < from + len){ const k = (x.t - from) + ':' + x.s; if (!seen.has(k)){ seen.add(k); ev.push({t:x.t - from, s:x.s}); } } ev.sort((a, b) => a.t - b.t || a.s - b.s); return ev; };
  const mem = JSON.parse(JSON.stringify(S.mem));
  if (!asSong){
    const beats = Math.min(99, Math.max(1, Math.round(totalBars * barTicks / M.PPQ)));
    const pt = mem.pats[S.pat]; pt.len = beats * M.PPQ; pt.ev = cut(0, pt.len).slice(0, 3000); if (ec >= 0) pt.ec = ec;
    if (totalBars * barTicks > pt.len) T.toast(`The file is longer than a pattern: took the first ${beats} beats into ${M.pnum(S.pat)}`, 5000);
    else T.toast(`${f.name} is pattern ${M.pnum(S.pat)} (${beats} beats, ${pt.ev.length} notes)`, 4000);
  } else {
    const bars = Math.max(1, +$('midiBars').value || 2), len = bars * barTicks;
    const free = []; for (let i = 35; i < M.NP; i++) if (!mem.pats[i].ev.length) free.push(i); for (let i = 0; i < 35; i++) if (!mem.pats[i].ev.length) free.push(i);
    const made = new Map(), steps = [];
    for (let b = 0; b < totalBars; b += bars){
      const ev = cut(b * barTicks, len), key = JSON.stringify(ev);
      if (!made.has(key)){
        if (!free.length){ T.toast('Not enough empty patterns: the song stops at step ' + steps.length, 5000); break; }
        const i = free.shift(); mem.pats[i] = {len:Math.min(99 * M.PPQ, len), ec:ec >= 0 ? ec : 6, tim:0, bef:0, ev}; made.set(key, i);
      }
      if (steps.length < 99) steps.push(made.get(key));
    }
    mem.songs[S.song].steps = steps;
    T.toast(`${f.name} is song ${M.pnum(S.song)}: ${steps.length} steps on ${made.size} patterns`, 5000);
  }
  if (mf.tempo && $('midiTempo').checked) T.setParam(D.P.tempo, Math.max(40, Math.min(250, Math.round(mf.tempo))));
  S.mem = mem; T.post({type:'mem', mem}); T.save(); T.drawGrid(true); T.refresh();
}
{
  const pg = $('file');
  const b1 = el('div', 'box', pg, '<h3>STORE · LOAD</h3><p>All 49 patterns, 49 songs, your sounds and every setting, in one file. The plugin reads and writes the same file.</p>');
  el('button', 'small', b1, 'STORE (.json)').onclick = saveTape; el('button', 'small', b1, 'LOAD').onclick = loadTape;
  const b0 = el('div', 'box', pg, '<h3>SOUNDS</h3><p>LOAD FOLDER takes a kit: each file goes on its sound by name (kick, snare, sidestick / sst, open hat / ohh, tom, conga, ride, crash, cabasa, tamb, cowbell, clap). Several snares, toms or congas become tunings of one ROM. Sounds with no file keep the built-in one.</p>');
  el('button', 'small', b0, 'LOAD FOLDER').onclick = () => loadKit(true); el('button', 'small', b0, 'LOAD FILES').onclick = () => loadKit(false);
  el('button', 'small', b0, 'BUILT-IN SOUNDS').onclick = factoryKit;
  const b4 = el('div', 'box', pg, '<h3>MIDI FILE IN</h3><p>Drum notes (General MIDI; velocity picks the dynamic) into the pattern shown, or as the song shown cut into patterns.</p>');
  el('label', '', b4, 'Correct to <select id="midiQ"><option value="-1">as played</option>' + M.EC_NAMES.map((n, i) => `<option value="${i}"${i === 2 ? ' selected' : ''}>${n}</option>`).join('') + '</select>');
  el('label', '', b4, 'Bars a pattern (song) <input id="midiBars" type="number" min="1" max="24" value="2">');
  el('label', '', b4, '<input type="checkbox" id="midiTempo" checked> Take the file\'s tempo');
  el('button', 'small', b4, 'INTO PATTERN').onclick = () => importMidi(false); el('button', 'small', b4, 'AS A SONG').onclick = () => importMidi(true);
  const b3 = el('div', 'box', pg, '<h3>MIDI FILE OUT · AUDIO</h3><p>General MIDI drums on channel 10, or audio with everything you hear.</p>');
  el('button', 'small', b3, 'PATTERN (.mid)').onclick = () => download(`omega-linn-pattern-${M.pnum(S.pat)}.mid`, midiFile(false), 'audio/midi');
  el('button', 'small', b3, 'SONG (.mid)').onclick = () => download(`omega-linn-song-${M.pnum(S.song)}.mid`, midiFile(true), 'audio/midi');
  el('label', '', b3, 'Bars of the pattern <input id="wavBars" type="number" min="1" max="64" value="4">');
  const go = async song => { try { T.toast('Rendering...'); const buf = await render(song, Math.max(1, Math.min(64, +$('wavBars').value || 4)), 48000);
    download(song ? `omega-linn-song-${M.pnum(S.song)}.wav` : `omega-linn-pattern-${M.pnum(S.pat)}.wav`, buf, 'audio/wav'); T.toast('Exported'); } catch (e){ T.toast(e.message); } };
  el('button', 'small', b3, 'PATTERN (.wav)').onclick = () => go(false); el('button', 'small', b3, 'SONG (.wav)').onclick = () => go(true);
  const b5 = el('div', 'box', pg, '<h3>RESET</h3><p>Back to the factory memory (grooves 11-57, the demo song 11); your sounds and presets stay.</p>');
  el('button', 'small', b5, 'FACTORY MEMORY').onclick = () => { if (!confirm('Erase all patterns and songs?')) return; S.mem = M.defaultMemory(); T.post({type:'mem', mem:S.mem}); T.save(); T.toast('Factory memory'); };
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
  if (type === 0x90 && d2 > 0){ const s = noteToSound(d1, d2); if (s >= 0){ if (!S.power) T.startAudio(); T.post({type:'note', s}); } }
  else if (type === 0x80 || (type === 0x90 && d2 === 0)){ const s = noteToSound(d1, 100); if (s >= 0) T.post({type:'off', v:D.SOUNDS[s][1]}); }
  else if (type === 0xc0 && S.midi.pc && d1 < M.NP){ const n = String(M.pnum(d1)); T.post({type:'key', id:M.K.D1 + +n[0] - 1, down:true}); T.post({type:'key', id:M.K.D1 + +n[0] - 1, down:false}); T.post({type:'key', id:M.K.D1 + +n[1] - 1, down:true}); T.post({type:'key', id:M.K.D1 + +n[1] - 1, down:false}); }
}
function bindInputs(){ if (!access) return; for (const i of access.inputs.values()) i.onmidimessage = S.midi.in === 'none' || (S.midi.in !== 'all' && S.midi.in !== i.id) ? null : onMidi; }
T.listeners.push(m => {
  const o = outPort(); if (!o) return;
  const ch = S.midi.ch === 16 ? 9 : S.midi.ch;
  if (m.type === 'hit' && S.midi.notesOut){ const n = SOUND_NOTE[m.s]; o.send([0x90 | ch, n, SOUND_VEL[m.s]]); o.send([0x80 | ch, n, 0], performance.now() + 40); }
  else if (m.type === 'trig' && S.midi.trigNote >= 0){ o.send([0x90 | ch, S.midi.trigNote, 100]); o.send([0x80 | ch, S.midi.trigNote, 0], performance.now() + 15); }
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
  const b2 = el('div', 'box', pg, '<h3>SYNC · NOTES · TRIGGER</h3><p>EXT. SYNC on the panel follows the MIDI clock (with this ticked).</p>');
  const cb = (id, text) => { const l = el('label', '', b2, `<input type="checkbox" id="${id}"> ${text}`); return l.querySelector('input'); };
  const sync = cb('midiSync', 'Follow MIDI clock in (start / stop)'), clk = cb('midiClockOut', 'Send MIDI clock, start / stop'), nts = cb('midiNotesOut', 'Send the notes the machine plays'), pc = cb('midiPc', 'Program change 0-48 selects pattern 11-77');
  el('label', '', b2, 'Trigger output as note <input id="midiTrig" type="number" min="-1" max="127"> (-1 off; BPM/TRIG. + 1-7 sets when)');
  const b3 = el('div', 'box', pg, '<h3>NOTE MAP (GENERAL MIDI)</h3><p>BASS 35/36 · SNARE 38/40 (velocity: 1 under 50, 2, 3 from 100) · SIDESTICK 37 · HIHAT 42 (1 / 2 by velocity), 44, OPEN 46 · TOMS 50 48 / 47 45 / 43 41 · RIDE 51 59 · CRASH 49 57 · CABASA 69 · TAMB 54 · CONGAS 63 62 / 64 · COWBELL 56 · CLAPS 39</p>');
  void b3;
  const sync2ui = () => { $('midiCh').value = String(S.midi.ch); sync.checked = S.midi.sync; clk.checked = S.midi.clockOut; nts.checked = S.midi.notesOut; pc.checked = S.midi.pc; $('midiTrig').value = S.midi.trigNote; $('midiIn').value = S.midi.in; };
  sync2ui();
  const upd = () => { S.midi.ch = +$('midiCh').value; S.midi.sync = sync.checked; S.midi.clockOut = clk.checked; S.midi.notesOut = nts.checked; S.midi.pc = pc.checked;
    S.midi.trigNote = Math.max(-1, Math.min(127, Math.round(+$('midiTrig').value))); S.midi.in = $('midiIn').value; S.midi.out = $('midiOut').value;
    T.post({type:'hitsOut', on:!!S.midi.out && S.midi.notesOut}); bindInputs(); T.save(); };
  for (const id of ['midiCh', 'midiSync', 'midiClockOut', 'midiNotesOut', 'midiPc', 'midiTrig', 'midiIn', 'midiOut']) $(id).addEventListener('change', upd);
  T.listeners.push(m => { if (m.type === 'disp' && !T.__midiSent){ T.__midiSent = true; T.post({type:'hitsOut', on:!!S.midi.out && S.midi.notesOut}); } });
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
window.LINNIO = {saveTape, loadTape, loadSound, loadKit, render, midiFile, parseMidi, noteToSound};
})();
