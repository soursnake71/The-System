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
function sendNote(semis, vel){
  const o = midiOut(); if (!o) return;
  const note = M.mode === 'pads' ? 36 + PAD_SEMIS.indexOf(semis) : 60 + semis, ch = Math.max(0, M.ch - 1);
  if (note < 0 || note > 127) return;
  o.send([0x90 | ch, note, Math.round((vel == null ? 1 : vel) * 127)]); setTimeout(() => o.send([0x80 | ch, note, 0]), 150);
}
function midiActivity(text){
  M.last = text; const el = $('#midiLast'); if (el) el.textContent = text;
  const led = $('#midiLed'); if (led){ led.classList.add('on'); clearTimeout(midiActivity.t); midiActivity.t = setTimeout(() => led.classList.remove('on'), 120); }
}
function onMidi(ev){
  const d = ev.data, st = d[0], type = st & 0xf0, ch = (st & 0x0f) + 1;
  // system realtime: clock, start, continue, stop
  if (st === 0xF8){
    const t = performance.now(); M.clock.push(t); if (M.clock.length > 49) M.clock.shift();
    if (M.clock.length >= 25){ const per = (M.clock[M.clock.length - 1] - M.clock[0]) / (M.clock.length - 1); const bpm = Math.round(60000 / (per * 24) * 10) / 10; if (!(bpm >= 20 && bpm <= 400)) return; if (Math.abs((M.bpm || 0) - bpm) >= .1){ M.bpm = bpm; const el = $('#midiClock'); if (el) el.textContent = bpm.toFixed(1) + ' BPM'; } }
    return;
  }
  if (st === 0xFA || st === 0xFB || st === 0xFC){
    midiActivity(st === 0xFC ? 'Stop' : st === 0xFA ? 'Start' : 'Continue');
    if (!M.follow || !src) return;
    if (st === 0xFC) stopMain(); else if (out) startMain(st === 0xFA ? 0 : playPos());
    return;
  }
  if (st >= 0xF0) return;
  if (M.ch && ch !== M.ch) return;
  if (type === 0x90 && d[2] > 0){
    const n = d[1];
    const semis = M.mode === 'pads' ? (n >= 36 && n < 52 ? PAD_SEMIS[n - 36] : null) : TW.clamp(n - 60, -24, 24);
    midiActivity(`Note ${n} vel ${d[2]}`);
    if (semis != null) hitPad(semis, d[2] / 127);
  } else if (type === 0xE0){
    M.bend = ((d[2] << 7 | d[1]) - 8192) / 8192 * 200;   // cents, +-2 semitones
    for (const v of voices) try { v.detune.value = M.bend; } catch (e) {}
    if (mainSrc) try { mainSrc.detune.value = M.bend; } catch (e) {}
    midiActivity(`Bend ${(M.bend / 100).toFixed(2)} st`);
  } else if (type === 0xC0){
    const i = M.bank * 128 + d[1]; midiActivity(`Program ${d[1] + 1}${M.bank ? ' bank ' + M.bank : ''}`);
    if (i < PRESETS.length) loadPreset(i);
  } else if (type === 0xB0){
    const cc = d[1], v = d[2];
    if (cc === 0){ M.bank = v; return; }
    if (cc === 120 || cc === 123){ midiActivity(cc === 120 ? 'All sound off' : 'All notes off'); stopAll(); return; }
    if (M.learn){
      for (const k of Object.keys(M.cc)) if (M.cc[k] === M.learn) delete M.cc[k];
      M.cc[cc] = M.learn; toast(`${PBYID[M.learn].name} is on CC ${cc}`); M.learn = null; M.armed = false; msave(); paintMidi(); return;
    }
    const id = M.cc[cc];
    midiActivity(`CC ${cc} = ${v}${id ? ' → ' + PBYID[id].name : ''}`);
    if (id){
      const p = PBYID[id];
      if (id === 'machine') setMachine(Math.round(v / 127 * p.max)); else setParam(id, p.choices || p.toggle ? Math.round(v / 127 * p.max) : denorm(p, v / 127));
      commit();
    }
  }
}
function hookInputs(){
  if (!M.access) return;
  let n = 0;
  M.access.inputs.forEach(i => { const on = M.inId === 'all' || M.inId === i.id; i.onmidimessage = on ? onMidi : null; if (on) n++; });
  $('#midiInfo').textContent = n ? `MIDI in: ${n}` : '';
  fillDevices();
}
function fillDevices(){
  const si = $('#midiIn'), so = $('#midiOutSel'); if (!si || !M.access) return;
  si.innerHTML = ''; si.add(new Option('All inputs', 'all')); M.access.inputs.forEach(i => si.add(new Option(i.name, i.id))); si.value = M.inId;
  if (si.value !== M.inId) si.value = 'all';
  so.innerHTML = ''; so.add(new Option('None', '')); M.access.outputs.forEach(o => so.add(new Option(o.name, o.id))); so.value = M.outId;
  if (so.value !== M.outId) so.value = '';
}
function paintMidi(){
  const b = $('#midiLearnBtn'); if (b){ b.classList.toggle('on', M.armed); b.textContent = M.learn ? `Learning: ${PBYID[M.learn].name}…` : M.armed ? 'Tap a control…' : 'MIDI learn'; }
  const list = $('#midiMap'); if (list) list.textContent = Object.entries(M.cc).sort((a, b) => a[0] - b[0]).map(([c, id]) => `CC${c} ${PBYID[id] ? PBYID[id].name : id}`).join(' · ');
  document.querySelectorAll('.learnable').forEach(el => el.classList.toggle('arm', M.armed && !M.learn));
}
function setupMidiFull(){
  $('#midiCh').innerHTML = '<option value="0">Omni</option>' + Array.from({length:16}, (_, i) => `<option value="${i + 1}">Channel ${i + 1}</option>`).join('');
  $('#midiCh').value = M.ch; $('#midiMode').value = M.mode; $('#midiFollow').checked = M.follow;
  $('#midiIn').onchange = e => { M.inId = e.target.value; msave(); hookInputs(); };
  $('#midiOutSel').onchange = e => { M.outId = e.target.value; msave(); };
  $('#midiCh').onchange = e => { M.ch = +e.target.value; msave(); };
  $('#midiMode').onchange = e => { M.mode = e.target.value; msave(); };
  $('#midiFollow').onchange = e => { M.follow = e.target.checked; msave(); };
  $('#midiLearnBtn').onclick = () => { M.armed = !M.armed; M.learn = null; paintMidi(); if (M.armed) toast('MIDI learn: tap a knob or menu, then move a control on your MIDI controller'); };
  $('#midiResetBtn').onclick = () => { M.cc = Object.assign({}, DEFAULT_CC); msave(); paintMidi(); toast('MIDI map back to the defaults'); };
  $('#midiClockWarp').onclick = () => { if (!M.bpm){ toast('No MIDI clock coming in'); return; } $('#tgtBpm').value = M.bpm; warp(); };
  // tapping a control while learning arms it
  for (const [id, el] of knobs){
    el.classList.add('learnable');
    el.addEventListener('pointerdown', e => { if (!M.armed || M.learn) return; e.stopPropagation(); e.preventDefault(); M.learn = id; paintMidi(); toast(`Now move a control on your MIDI controller for ${PBYID[id].name}`); }, true);
  }
  paintMidi();
  if (!navigator.requestMIDIAccess){ $('#midiState').textContent = 'This browser has no Web MIDI (use Chrome).'; return; }
  navigator.requestMIDIAccess({sysex:false}).then(a => { M.access = a; a.onstatechange = hookInputs; hookInputs(); $('#midiState').textContent = ''; })
    .catch(() => { $('#midiState').textContent = 'MIDI access was refused.'; });
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
    if (src){ info.FILE = src.name; info.LEN = (src.chs[0].length / src.sr).toFixed(2) + 's'; info.SEL = `${(S.sel[0] / src.sr).toFixed(3)}–${(S.sel[1] / src.sr).toFixed(3)}s`; }
    if (S.bpm) info.BPM = S.bpm.toFixed(1); if (S.key) info.KEY = S.key.name; if (outStats) info.OUT = `${outStats.lufs} LUFS · ${outStats.peak} dBFS`;
    const status = [M.bpm ? `CLOCK <b>${M.bpm.toFixed(1)}</b>` : '', M.last ? `MIDI <b>${M.last}</b>` : '', playing ? '<b>▶ PLAYING</b>' : ''].filter(Boolean).join(' &nbsp; ');
    tvSend({type:'state', p:S.p, preset:S.presetName, match:S.match, sel:S.sel, view:src ? view() : null, info, status, touched:TV.touched});
  }, 40);
}
function tvPeaks(){ if (TV.on && peaks && src) tvSend({type:'peaks', mn:Array.from(peaks.mn, v => Math.round(v * 1000) / 1000), mx:Array.from(peaks.mx, v => Math.round(v * 1000) / 1000), len:src.chs[0].length}); }
function tvFull(){ tvPeaks(); tvState(); }
function tvPosLoop(){
  if (TV.on){ const p = playing ? Math.round(playPos() * 2000) / 2000 : null; if (p !== TV.lastPos){ TV.lastPos = p; tvSend({type:'pos', pos:p}); } }
  requestAnimationFrame(tvPosLoop);
}
async function castTv(){
  if (window.AndroidTv){
    const r = AndroidTv.open();
    if (r.startsWith('display')){ TV.on = true; setTimeout(tvFull, 800); toast('TV screen on ' + r.slice(8) + '. Keep working here.'); }
    else toast('No second screen yet: in Cast settings pick your TV (or plug in HDMI / USB-C), then tap Cast to TV again. Screen mirroring shows the whole app on the TV.', 7000);
    setTimeout(paintTv, 900); return;
  }
  if (!window.PresentationRequest){ toast('Casting needs Chrome. Use "TV window" with a TV plugged into a computer, or your phone\'s Screen cast / Smart View.', 6000); return; }
  try {
    TV.req = TV.req || new PresentationRequest(['tv.html']);
    const c = await TV.req.start();
    TV.conn = c; TV.on = true;
    c.onmessage = e => { try { if (JSON.parse(e.data).type === 'ready') tvFull(); } catch (x) {} };
    c.onconnect = () => { tvSend({type:'hello'}); tvFull(); };
    c.onclose = c.onterminate = () => { if (TV.conn === c) TV.conn = null; paintTv(); };
    paintTv(); toast('Casting to the TV. Everything you do here shows there live.');
  } catch (e){ if (e.name !== 'AbortError' && e.name !== 'NotAllowedError') toast('No TV found to cast to (' + e.name + '). Check the TV is on the same Wi-Fi.', 5000); }
}
window.__twTvClosed = () => paintTv();
function windowTv(){
  if (window.AndroidTv){ castTv(); return; }
  TV.win = window.open('tv.html', 'omega-twelve-tv', 'popup,width=1280,height=720');
  if (!TV.win){ toast('The browser blocked the window: allow pop-ups for this site'); return; }
  TV.on = true; paintTv(); toast('TV window open: drag it to the TV screen and press F11 for full screen.', 5000);
}
function stopTv(){ if (window.AndroidTv) AndroidTv.close(); if (TV.conn) try { TV.conn.terminate(); } catch (e) {} if (TV.win && !TV.win.closed) TV.win.close(); TV.conn = TV.win = null; paintTv(); }
function paintTv(){ const live = (TV.conn && TV.conn.state !== 'closed') || (TV.win && !TV.win.closed) || (window.AndroidTv && AndroidTv.connected()); $('#tvBtn').style.boxShadow = live ? '0 0 0 2px #2bd35c inset' : ''; $('#tvState').textContent = live ? 'On the TV now' : ''; }

/* ---------------- hooks into app.js ---------------- */
const _setParam = setParam;
setParam = function (id, v){ TV.touched = id; _setParam(id, v); };
const _paintAll = paintAll;
paintAll = function (){ _paintAll(); tvState(); };
const _buildPeaks = buildPeaks;
buildPeaks = function (){ _buildPeaks(); tvPeaks(); };
const _hitPad = hitPad;
hitPad = function (semis, vel){ tvSend({type:'pad', semis}); sendNote(semis, vel); return _hitPad(semis, vel).then(() => { if (M.bend) for (const v of voices) try { v.detune.value = M.bend; } catch (e) {} }); };
const _doRender = doRender;
doRender = async function (){ await _doRender(); tvState(); };

// a TV remote or a keyboard: knobs take focus, arrows turn them, Enter plays a pad
function remoteKeys(){
  for (const [id, el] of knobs){
    if (!el.classList.contains('knob')) continue;
    el.tabIndex = 0;
    el.addEventListener('keydown', e => {
      const p = PBYID[id], dir = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : e.key === 'ArrowDown' || e.key === 'ArrowLeft' ? -1 : 0;
      if (!dir) return;
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ if (!e.shiftKey) return; }   // left/right move between controls unless Shift
      e.preventDefault(); setParam(id, denorm(p, norm(p, S.p[id]) + dir * (p.step >= 1 ? 1 / (p.max - p.min) : .02))); commit();
    });
  }
  document.querySelectorAll('.pad').forEach(el => { el.tabIndex = 0; el.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' '){ e.preventDefault(); hitPad(+el.dataset.s); } }); });
}

$('#castBtn').onclick = castTv; $('#tvWinBtn').onclick = windowTv; $('#tvStopBtn').onclick = stopTv;
if (window.AndroidTv){ $('#castState').textContent = 'Uses a second screen: HDMI / USB-C or a wireless display'; $('#tvWinBtn').hidden = true; }
else if (window.PresentationRequest){
  try { new PresentationRequest(['tv.html']).getAvailability().then(a => { const f = () => { $('#castBtn').disabled = false; $('#castState').textContent = a.value ? 'TV found on your network' : 'No cast TV found yet (same Wi-Fi?)'; }; f(); a.onchange = f; }).catch(() => {}); } catch (e) {}
}
setupMidiFull(); remoteKeys(); requestAnimationFrame(tvPosLoop);
