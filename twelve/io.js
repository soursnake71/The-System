/* Omega Twelve: full MIDI and the TV screen. Loaded after app.js and hooks into it.
   MIDI in: notes (chromatic around middle C, or 16 pads from note 36), velocity, pitch bend (+-2 st), program change with
   bank select, every control on a CC (learned, or the General MIDI sound-controller defaults), all notes off / all sound
   off as STOP ALL, MIDI clock (tempo shown, start / stop / continue drive playback). MIDI out: the pads you tap.
   TV: Cast (Presentation API, Chromecast / Android TV / smart TVs), a TV window for a TV plugged into a computer, or the
   phone's own screen mirroring; tv.html draws the app's live state big. */
'use strict';

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
  window.__twMidi = (id, bytes) => { const p = access.inputs.get(id); if (p && p._h) p._h({data:Uint8Array.from(bytes), target:p}); };
  window.__twMidiChanged = () => { scan(); if (access.onstatechange) access.onstatechange({}); };
  scan();
  navigator.requestMIDIAccess = () => Promise.resolve(access);
}

/* ---------------- MIDI ---------------- */
const MSTORE = 'omega-twelve-midi';
const DEFAULT_CC = {1:'cutoff', 74:'cutoff', 71:'res', 73:'fenv', 72:'drive', 7:'output', 91:'crackle', 93:'wow', 94:'hiss', 95:'bits'};
const M = {access:null, inId:'all', outId:'', ch:0, mode:'chromatic', cc:Object.assign({}, DEFAULT_CC), follow:true, learn:null, armed:false, bend:0, bank:0, clock:[], bpm:null, last:''};
try { Object.assign(M, JSON.parse(localStorage.getItem(MSTORE) || '{}'), {learn:null, armed:false, bend:0, clock:[], bpm:null}); } catch (e) {}
function msave(){ try { localStorage.setItem(MSTORE, JSON.stringify({inId:M.inId, outId:M.outId, ch:M.ch, mode:M.mode, cc:M.cc, follow:M.follow})); } catch (e) {} }

function midiOut(){ return M.access && M.outId ? M.access.outputs.get(M.outId) : null; }
// pads mode: note 36 is pad A1, up to 67 for D8; chromatic: middle C is the sample at its own pitch
function sendNote(note, vel){
  const o = midiOut(); if (!o || note < 0 || note > 127) return;
  const ch = Math.max(0, M.ch - 1);
  o.send([0x90 | ch, note, Math.round((vel == null ? 1 : vel) * 127)]); setTimeout(() => o.send([0x80 | ch, note, 0]), 150);
}
function midiActivity(text){ M.last = text; if (S.menu === 'sync') paintLcd(); }
function onMidi(ev){
  const d = ev.data, st = d[0], type = st & 0xf0, ch = (st & 0x0f) + 1;
  // system realtime: clock, start, continue, stop
  if (st === 0xF8){
    const t = performance.now(); M.clock.push(t); if (M.clock.length > 49) M.clock.shift();
    if (M.clock.length >= 25){ const per = (M.clock[M.clock.length - 1] - M.clock[0]) / (M.clock.length - 1); const bpm = Math.round(60000 / (per * 24) * 10) / 10; if (bpm >= 20 && bpm <= 400) M.bpm = bpm; }
    return;
  }
  if (st === 0xFA || st === 0xFB || st === 0xFC){
    midiActivity(st === 0xFC ? 'STOP' : st === 0xFA ? 'START' : 'CONTINUE');
    if (!M.follow || !src) return;
    if (st === 0xFC) stopMain(); else startMain(st === 0xFA ? 0 : playPos());
    return;
  }
  if (st >= 0xF0) return;
  if (M.ch && ch !== M.ch) return;
  if (type === 0x90 && d[2] > 0){
    const n = d[1];
    midiActivity(`NOTE ${n} VEL ${d[2]}`);
    if (M.mode === 'pads'){ if (n >= 36 && n < 68) hitPad(n - 36, d[2] / 127); }
    else playSemis(TW.clamp(n - 60, -24, 24), d[2] / 127);
  } else if (type === 0xE0){
    M.bend = ((d[2] << 7 | d[1]) - 8192) / 8192 * 2;   // semitones, +-2
    send({type:'bend', st:M.bend});
  } else if (type === 0xC0){
    const i = M.bank * 128 + d[1]; midiActivity(`PROGRAM ${d[1] + 1}`);
    if (i < PRESETS.length) loadPreset(i);
  } else if (type === 0xB0){
    const cc = d[1], v = d[2];
    if (cc === 0){ M.bank = v; return; }
    if (cc === 120 || cc === 123){ midiActivity(cc === 120 ? 'ALL SOUND OFF' : 'ALL NOTES OFF'); stopAll(); return; }
    if (M.learn){
      for (const k of Object.keys(M.cc)) if (M.cc[k] === M.learn) delete M.cc[k];
      M.cc[cc] = M.learn; say(`${SHORT[M.learn] || M.learn} ON CC ${cc}`, 2500); M.learn = null; M.armed = false; msave(); return;
    }
    const id = M.cc[cc];
    midiActivity(`CC ${cc}=${v}${id ? ' ' + (SHORT[id] || id) : ''}`);
    if (id){ const p = PBYID[id]; setAny(id, p.choices || p.toggle ? Math.round(v / 127 * p.max) : denorm(p, v / 127)); commit(); }
  }
}
function hookInputs(){
  if (!M.access) return;
  M.access.inputs.forEach(i => { const on = M.inId === 'all' || M.inId === i.id; i.onmidimessage = on ? onMidi : null; });
}
const ins = () => M.access ? [['all', 'ALL'], ...[...M.access.inputs.values()].map(i => [i.id, i.name.toUpperCase()])] : [['all', 'ALL']];
const outs = () => M.access ? [['', 'NONE'], ...[...M.access.outputs.values()].map(o => [o.id, o.name.toUpperCase()])] : [['', 'NONE']];
const nameOf = (list, id) => (list.find(x => x[0] === id) || list[0])[1];
// tapping a fader or knob while learning picks what the next controller move drives
learnHook = id => {
  if (!M.armed || M.learn || !id) return false;
  M.learn = id; say(`MOVE A CONTROL FOR ${SHORT[id] || id}`, 6000); return true;
};
function setupMidi(){
  for (const [id, el] of knobs) el.addEventListener('pointerdown', e => { if (learnHook(id)){ e.stopPropagation(); e.preventDefault(); } }, true);
  if (!navigator.requestMIDIAccess) return;
  navigator.requestMIDIAccess({sysex:false}).then(a => { M.access = a; a.onstatechange = hookInputs; hookInputs(); }).catch(() => {});
}

/* ---------------- TV ---------------- */
const TV = {conn:null, win:null, bc:null, req:null, touched:null, timer:0, lastPos:-1, on:false};
try { TV.bc = new BroadcastChannel('omega-twelve-tv'); TV.bc.onmessage = e => { if (e.data && e.data.type === 'ready'){ TV.on = true; tvFull(); } }; } catch (e) {}
addEventListener('message', e => { if (e.origin === location.origin && e.data && e.data.type === 'ready'){ TV.on = true; tvFull(); } });
function tvSend(msg){
  if (!TV.on) return;
  const s = JSON.stringify(msg);
  if (TV.conn && TV.conn.state === 'connected') try { TV.conn.send(s); } catch (e) {}
  if (TV.win && !TV.win.closed) TV.win.postMessage(msg, location.origin);
  if (TV.bc) TV.bc.postMessage(msg);
  if (window.AndroidTv && AndroidTv.connected()) AndroidTv.post(s);
}
function tvState(){
  if (!TV.on) return;
  clearTimeout(TV.timer);
  TV.timer = setTimeout(() => {
    const info = {};
    if (src){ info.FILE = src.name; info.LEN = (src.chs[0].length / src.sr).toFixed(2) + 's'; info.IN = (S.sel[0] / src.sr).toFixed(3) + 's'; info.OUT = (S.sel[1] / src.sr).toFixed(3) + 's'; }
    if (S.bpm) info.BPM = S.bpm.toFixed(1); if (S.key) info.KEY = S.key.name; info.PAGE = PAGES[S.page].name; info.BANK = 'ABCD'[S.bank];
    const status = [M.bpm ? `CLOCK <b>${M.bpm.toFixed(1)}</b>` : '', M.last ? `MIDI <b>${M.last}</b>` : '', playing ? '<b>▶ PLAYING</b>' : ''].filter(Boolean).join(' &nbsp; ');
    tvSend({type:'state', p:S.p, preset:S.presetName, match:S.match, sel:S.sel, view:src ? view() : null, info, status, touched:TV.touched});
  }, 40);
}
function tvPeaks(){ if (TV.on && peaks && src) tvSend({type:'peaks', mn:Array.from(peaks.mn, v => Math.round(v * 1000) / 1000), mx:Array.from(peaks.mx, v => Math.round(v * 1000) / 1000), len:src.chs[0].length}); }
function tvFull(){ tvPeaks(); tvState(); }
function tvPosLoop(){
  if (TV.on){ const p = playing ? Math.round(playPos() * 2000) / 2000 : null; if (p !== TV.lastPos){ TV.lastPos = p; tvSend({type:'pos', pos:p}); } }
  setTimeout(tvPosLoop, 50);
}
async function castTv(){
  if (window.AndroidTv){
    const r = AndroidTv.open();
    if (r.startsWith('display')){ TV.on = true; setTimeout(tvFull, 800); say('TV ON ' + r.slice(8).toUpperCase()); }
    else toast('No second screen yet: in Cast settings pick your TV (or plug in HDMI / USB-C), then tap Cast again. Screen mirroring shows the whole app on the TV.', 7000);
    return;
  }
  if (!window.PresentationRequest){ toast('Casting needs Chrome. Use TV Window with a TV plugged into a computer, or your phone\'s screen cast.', 6000); return; }
  try {
    TV.req = TV.req || new PresentationRequest(['tv.html']);
    const c = await TV.req.start();
    TV.conn = c; TV.on = true;
    c.onmessage = e => { try { if (JSON.parse(e.data).type === 'ready') tvFull(); } catch (x) {} };
    c.onconnect = () => { tvSend({type:'hello'}); tvFull(); };
    c.onclose = c.onterminate = () => { if (TV.conn === c) TV.conn = null; };
    say('CASTING TO THE TV');
  } catch (e){ if (e.name !== 'AbortError' && e.name !== 'NotAllowedError') toast('No TV found to cast to (' + e.name + '). Check the TV is on the same Wi-Fi.', 5000); }
}
window.__twTvClosed = () => {};
function windowTv(){
  if (window.AndroidTv){ castTv(); return; }
  TV.win = window.open('tv.html', 'omega-twelve-tv', 'popup,width=1280,height=720');
  if (!TV.win){ toast('The browser blocked the window: allow pop-ups for this site'); return; }
  TV.on = true; toast('TV window open: drag it to the TV screen and press F11 for full screen.', 5000);
}
function stopTv(){ if (window.AndroidTv) AndroidTv.close(); if (TV.conn) try { TV.conn.terminate(); } catch (e) {} if (TV.win && !TV.win.closed) TV.win.close(); TV.conn = TV.win = null; say('TV OFF'); }
const tvLive = () => !!((TV.conn && TV.conn.state !== 'closed') || (TV.win && !TV.win.closed) || (window.AndroidTv && AndroidTv.connected()));

/* ---------------- the Sync menu ---------------- */
window.IO = {items: () => [
  [1, 'MIDI In', choice('MIDI IN', () => nameOf(ins(), M.inId), d => { M.inId = cyc(ins().map(x => x[0]), M.inId, d); msave(); hookInputs(); }), () => nameOf(ins(), M.inId).slice(0, 8)],
  [2, 'Channel', choice('CHANNEL', () => M.ch ? 'CH ' + M.ch : 'OMNI', d => { M.ch = (M.ch + d + 17) % 17; msave(); }), () => M.ch ? M.ch + '' : 'OMNI'],
  [3, 'Notes', choice('NOTES', () => M.mode === 'pads' ? 'PADS FROM 36' : 'CHROMATIC C3', () => { M.mode = M.mode === 'pads' ? 'chromatic' : 'pads'; msave(); }), () => M.mode === 'pads' ? 'PADS' : 'CHROM'],
  [4, 'MIDI Out', choice('MIDI OUT', () => nameOf(outs(), M.outId), d => { M.outId = cyc(outs().map(x => x[0]), M.outId, d); msave(); }), () => nameOf(outs(), M.outId).slice(0, 8)],
  [5, 'MIDI Learn', () => { M.armed = !M.armed; M.learn = null; say(M.armed ? 'TOUCH A FADER OR KNOB' : 'LEARN OFF', 3000); }, () => M.armed ? 'ARMED' : ''],
  [6, 'Default Map', () => { M.cc = Object.assign({}, DEFAULT_CC); msave(); say('DEFAULT MIDI MAP'); }],
  [7, 'Clock Follow', () => { M.follow = !M.follow; msave(); }, () => M.follow ? 'ON' : 'OFF'],
  [8, 'Cast to TV', () => castTv(), () => tvLive() ? 'ON' : ''],
  [9, 'TV Window', () => windowTv()],
  [0, 'Stop TV · Clock Warp', () => { if (M.bpm && !tvLive()){ S.tgtBpm = M.bpm; warp(); } else stopTv(); }, () => M.bpm ? M.bpm.toFixed(1) : '']
]};

/* ---------------- hooks into app.js ---------------- */
const _setParam = setParam;
setParam = function (id, v){ TV.touched = id; _setParam(id, v); };
const _paintAll = paintAll;
paintAll = function (){ _paintAll(); tvState(); };
const _buildPeaks = buildPeaks;
buildPeaks = function (){ _buildPeaks(); tvPeaks(); };
const _hitPad = hitPad;
hitPad = function (i, vel){ tvSend({type:'pad', semis:S.pads[i]}); sendNote(M.mode === 'pads' ? 36 + i : 60 + S.pads[i], vel); return _hitPad(i, vel); };

setupMidi(); tvPosLoop(); fillLists();
