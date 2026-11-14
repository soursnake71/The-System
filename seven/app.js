/* Omega 7 app: the faceplate (drawn from the 1983 front panel's layout), the keyboard and wheels, memory and the
 * cartridge (sysex banks), the sequencer editor, MIDI in / out and files. The sound runs in worklet.js; the panel's
 * logic is panel.js. */
'use strict';
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const STORE = 'omega-seven';
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

/* ---------------- state ---------------- */
const S = {
  int:DXV.FACTORY.map(v => Uint8Array.from(v)), cart:null, cartName:'', edit:Uint8Array.from(DXV.FACTORY[0]),
  func:DX.defaultFunc(), seq:DXS.defaultSeqState(), panel:null, view:'auto', octave:0, playerLoop:false, midiIn:'all', midiOut:'', clockOut:true
};
const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); };
const unb64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
function save(){
  try {
    localStorage.setItem(STORE, JSON.stringify({
      int:b64(DX.bankToSysex(S.int, 0)), cart:S.cart ? b64(DX.bankToSysex(S.cart, 0)) : null, cartName:S.cartName, edit:b64(S.edit), func:S.func, seq:S.seq,
      panel:panel.save(), view:S.view, octave:S.octave, midiIn:S.midiIn, midiOut:S.midiOut, clockOut:S.clockOut, playerLoop:S.playerLoop
    }));
  } catch (e) {}
}
let saveT = 0;
function dirty(){ clearTimeout(saveT); saveT = setTimeout(save, 500); }
function load(){
  try {
    const d = JSON.parse(localStorage.getItem(STORE) || 'null'); if (!d) return;
    if (d.int){ const r = DX.parseSysexFile(unb64(d.int)); if (r.bank) S.int = r.bank; }
    if (d.cart){ const r = DX.parseSysexFile(unb64(d.cart)); if (r.bank) S.cart = r.bank; }
    S.cartName = d.cartName || '';
    if (d.edit){ const e = unb64(d.edit); if (e.length === DX.VOICE_SIZE) S.edit = DX.clampVoice(e); }
    if (d.func) S.func = Object.assign(DX.defaultFunc(), d.func);
    if (d.seq){ const q = new DXS.Sequencer({}); q.load(d.seq); S.seq = q.s; }
    for (const k of ['view', 'octave', 'midiIn', 'midiOut', 'clockOut', 'playerLoop']) if (d[k] != null) S[k] = d[k];
    S.panel = d.panel;
  } catch (e) { console.warn(e); }
}
load();

/* ---------------- messages ---------------- */
let toastT = 0;
function toast(msg, ms){ const t = $('#toast'); t.textContent = msg; t.classList.add('show'); clearTimeout(toastT); toastT = setTimeout(() => t.classList.remove('show'), ms || 2600); }

/* ---------------- audio ---------------- */
let ctx = null, node = null, audioReady = null;
const stat = {step:-1, playing:false, rec:false, voices:0, peak:0, player:false, rec2:false, cursor:0};
function post(m){ if (node) node.port.postMessage(m); }
function audio(){
  if (!audioReady) audioReady = (async () => {
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    for (const f of ['engine.js', 'seq.js', 'worklet.js']) await ctx.audioWorklet.addModule(f);
    node = new AudioWorkletNode(ctx, 'omega-seven', {numberOfInputs:0, numberOfOutputs:1, outputChannelCount:[2]});
    node.port.onmessage = e => onWorklet(e.data);
    node.connect(ctx.destination);
    post({t:'funcs', f:S.func}); post({t:'voice', v:S.edit}); post({t:'seq', s:S.seq});
    post({t:'player', cmd:'loop', a:S.playerLoop});
  })().catch(e => { toast('Audio failed: ' + e.message, 6000); audioReady = null; });
  if (ctx && ctx.state === 'suspended') ctx.resume();
  return audioReady;
}
function onWorklet(m){
  if (m.t === 'stat'){
    Object.assign(stat, m);
    if (m.pattern){ S.seq.pats[S.seq.pat] = m.pattern; dirty(); if ($('#dlgSeq').open) drawSeq(); }
    for (const d of m.out) midiSend(d, true);
    return;
  }
  if (m.t === 'func'){ S.func[m.k] = m.v; dirty(); return; }
  if (m.t === 'recData'){
    if (!m.evs.length){ toast('Nothing was recorded'); return; }
    saveFile(DXS.recordingToMidi(m.evs, m.voice, S.func.txCh), 'omega-seven-performance.mid', 'audio/midi');
  }
}
$('#start').addEventListener('pointerdown', () => { audio(); $('#start').classList.add('gone'); });
document.addEventListener('visibilitychange', () => { if (!document.hidden && ctx && ctx.state === 'suspended') ctx.resume(); });

/* ---------------- the panel's host ---------------- */
const host = {
  getVoice:() => S.edit,
  setParam(id, v){ S.edit[id] = v; post({t:'param', id, v}); paramOut(id, v); voiceVer++; dirty(); },
  setVoice(v){ S.edit = DX.clampVoice(Uint8Array.from(v)); post({t:'voice', v:S.edit}); voiceVer++; dirty(); },
  get func(){ return S.func; },
  setFunc(k, v){
    const d = DX.FUNC_DEF.find(x => x[0] === k); if (!d) return;
    S.func[k] = Math.max(0, Math.min(d[2], v)); post({t:'func', k, v:S.func[k]});
    if (d[4] >= 0 && S.func.sysInfo) midiSend(DX.paramChange(S.func.txCh, 2, d[4], S.func[k]));
    dirty();
  },
  bank:src => src === 'int' ? S.int : S.cart,
  store(src, i, v){ (src === 'int' ? S.int : S.cart)[i] = v; dirty(); if ($('#dlgLib').open) drawLib(); },
  setBank(src, arr){ if (src === 'int') S.int = arr; else S.cart = arr; dirty(); if ($('#dlgLib').open) drawLib(); },
  get seq(){ return S.seq; },
  seqCmd(cmd, a){
    if (cmd === 'changed'){ post({t:'seq', s:S.seq}); dirty(); if ($('#dlgSeq').open) drawSeq(); return; }
    if (cmd === 'pattern'){ S.seq.pat = a; post({t:'seq', s:S.seq}); dirty(); if ($('#dlgSeq').open) drawSeq(); return; }
    if (cmd === 'arp'){ S.seq.arp.on = !!a; post({t:'seq', s:S.seq}); post({t:'seqCmd', cmd:'arp', a:!!a}); dirty(); return; }
    if (cmd === 'play') audio().then(() => post({t:'seqCmd', cmd:'play'}));
    else if (cmd === 'stop') post({t:'seqCmd', cmd:'stop'});
    else if (cmd === 'rec'){ post({t:'seqCmd', cmd:'rec', a}); }
    else if (cmd === 'cursor') post({t:'seqCmd', cmd:'cursor', a});
  },
  request:(kind, arg) => request(kind, arg),
  playing:() => stat.playing, recording:() => stat.rec,
  playerState:() => ({playing:stat.player, rec:stat.rec2}), playerLoop:() => S.playerLoop
};
let voiceVer = 0;
const panel = new DXP.Panel(host);
panel.restore(S.panel);

/* ---------------- requests from the panel (files, MIDI dumps) ---------------- */
let pickFor = null;
function pick(kind, accept){ pickFor = kind; const f = $('#fileIn'); f.value = ''; f.accept = accept || ''; f.click(); }
$('#fileIn').addEventListener('change', async e => {
  const f = e.target.files[0]; if (!f) return;
  const bytes = new Uint8Array(await f.arrayBuffer());
  try { openFile(pickFor, bytes, f.name); } catch (err){ toast(err.message, 5000); }
});
function request(kind, arg){
  switch (kind){
    case 'loadFile': pick('cart', '.syx,.SYX,.bin,.mid,.midi,application/octet-stream'); break;
    case 'saveBank': { const src = arg || 'int', b = src === 'int' ? S.int : S.cart; if (!b){ panel.flash('CARTRIDGE', 'NOT READY'); return; }
      saveFile(DX.bankToSysex(b, S.func.txCh), (src === 'int' ? 'omega-seven-internal' : (S.cartName || 'cartridge').replace(/\.\w+$/, '')) + '.syx', 'application/octet-stream'); break; }
    case 'saveVoice': saveFile(DX.voiceToSysex(S.edit, S.func.txCh), DX.voiceName(S.edit).trim().replace(/[^\w.-]+/g, '_') + '.syx', 'application/octet-stream'); break;
    case 'sendVoice': midiSend(DX.voiceToSysex(S.edit, S.func.txCh)); panel.flash('MIDI TRANSMIT', 'VOICE SENT'); break;
    case 'sendBank': midiSend(DX.bankToSysex(S.int, S.func.txCh)); panel.flash('MIDI TRANSMIT', 'BANK SENT'); break;
    case 'requestBank': midiSend([0xF0, 0x43, 0x20 | S.func.txCh, 0x09, 0xF7]); panel.flash('BANK REQUEST', 'SENT'); break;
    case 'requestVoice': midiSend([0xF0, 0x43, 0x20 | S.func.txCh, 0x00, 0xF7]); panel.flash('VOICE REQUEST', 'SENT'); break;
    case 'midiPlay': pick('play', '.mid,.midi,audio/midi'); break;
    case 'midiStop': post({t:'player', cmd:'stop'}); break;
    case 'midiLoop': S.playerLoop = !!arg; post({t:'player', cmd:'loop', a:S.playerLoop}); dirty(); break;
    case 'midiImport': pick('pattern', '.mid,.midi,audio/midi'); break;
    case 'midiExport': exportPattern(); break;
    case 'recStart': audio().then(() => post({t:'rec', on:true})); panel.flash('RECORDING', 'NO=STOP & SAVE'); break;
    case 'recStop': post({t:'rec', on:false}); break;
  }
}
function openFile(kind, bytes, name){
  const isMidi = bytes[0] === 0x4D && bytes[1] === 0x54 && bytes[2] === 0x68 && bytes[3] === 0x64;
  if (kind === 'play' || (kind === 'cart' && isMidi)){
    if (!isMidi) throw new Error('Not a MIDI file');
    const mf = DXS.parseMidiFile(bytes);
    audio().then(() => post({t:'file', mf, name}));
    toast('Playing ' + name); return;
  }
  if (kind === 'pattern'){
    if (!isMidi) throw new Error('Not a MIDI file');
    const r = DXS.patternFromMidi(DXS.parseMidiFile(bytes), S.seq.pats[S.seq.pat].rate);
    S.seq.pats[S.seq.pat] = r.pattern; if (r.tempo) S.seq.tempo = Math.max(30, Math.min(300, Math.round(r.tempo)));
    host.seqCmd('changed'); toast(`${r.notes} notes into pattern ${S.seq.pat + 1} (${r.pattern.len} steps)`); return;
  }
  // voices: a bank goes to the cartridge (or internal), a single voice to the edit buffer
  const r = DX.parseSysexFile(bytes);
  if (r.bad) toast(`${r.bad} checksum error(s): loaded anyway`, 4000);
  if (r.bank){
    if (kind === 'int'){ if (panel.protect.int){ toast('Turn MEMORY PROTECT INTERNAL off first'); return; } S.int = r.bank; toast('Bank loaded into internal memory'); }
    else { S.cart = r.bank; S.cartName = name; panel.src = 'cart'; panel.mode = 'play'; toast('Cartridge: ' + name); }
    dirty(); if ($('#dlgLib').open) drawLib();
  } else if (r.voices.length){
    host.setVoice(r.voices[0]); panel.edited = true; toast('Voice ' + DX.voiceName(r.voices[0]).trim() + ' in the edit buffer (STORE to keep it)', 4000);
  } else if (r.params.length){ for (const [n, v] of r.params) host.setParam(n, v); toast(r.params.length + ' parameter changes applied'); }
  else throw new Error('No voices found in ' + name);
}
async function saveFile(bytes, name, mime){
  const blob = new Blob([bytes], {type:mime});
  if (window.AndroidSeven){
    const data = await new Promise(res => { const r = new FileReader(); r.onload = () => res(String(r.result).split(',')[1]); r.readAsDataURL(blob); });
    const where = AndroidSeven.save(data, name, mime);
    toast(where.startsWith('error') ? where : 'Saved to ' + where, 4000); return;
  }
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
  toast('Saved ' + name);
}
function exportPattern(){
  const p = S.seq.pats[S.seq.pat];
  if (!p.steps.slice(0, p.len).some(s => s.notes.length)){ panel.flash('PATTERN ' + (S.seq.pat + 1), 'IS EMPTY'); return; }
  saveFile(DXS.patternToMidi(p, S.edit, S.seq.tempo, S.func.txCh, 1), `omega-seven-pattern-${S.seq.pat + 1}.mid`, 'audio/midi');
}

/* ---------------- MIDI ---------------- */
if (window.AndroidMidi && AndroidMidi.available()){
  // the Android app: native MIDI shaped like Web MIDI
  const access = {inputs:new Map(), outputs:new Map(), onstatechange:null, sysexEnabled:true};
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
let midiAccess = null;
const sysexBuf = new Map();
function midiInit(){
  if (!navigator.requestMIDIAccess) return;
  navigator.requestMIDIAccess({sysex:true}).catch(() => navigator.requestMIDIAccess({sysex:false})).then(a => {
    midiAccess = a; a.onstatechange = () => { hookMidi(); if ($('#dlgMidi').open) drawMidi(); }; hookMidi();
  }).catch(() => {});
}
function hookMidi(){
  if (!midiAccess) return;
  for (const inp of midiAccess.inputs.values()) inp.onmidimessage = (S.midiIn === 'all' || S.midiIn === inp.id) ? e => midiIn(e.data, inp.id) : null;
}
function midiOutPorts(){
  if (!midiAccess || !S.midiOut) return [];
  return [...midiAccess.outputs.values()].filter(o => S.midiOut === 'all' || o.id === S.midiOut);
}
function midiSend(d, fromSeq){
  if (fromSeq && d.length === 1 && !S.clockOut) return;
  for (const o of midiOutPorts()) try { o.send(d); } catch (e) {}
}
function paramOut(id, v){ if (S.func.sysInfo) midiSend(DX.paramChange(S.func.txCh, 0, id, v)); }
// one or more messages from an input (Web MIDI hands over whole messages; Android may split sysex)
function midiIn(data, src){
  let d = Array.from(data);
  if (sysexBuf.has(src)){ d = sysexBuf.get(src).concat(d); sysexBuf.delete(src); }
  if (d[0] === 0xF0 && d[d.length - 1] !== 0xF7){ sysexBuf.set(src, d); return; }
  const st = d[0];
  if (st === 0xF0){ return midiSysex(d); }
  if (st >= 0xF8 || st === 0xFA){ post({t:'midi', d}); return; }
  const ty = st & 0xF0, rx = S.func.rxCh;
  if (ty >= 0x80 && ty < 0xF0 && rx > 0 && (st & 15) !== rx - 1) return;
  if (ty === 0xC0){ panel.mode = 'play'; panel.selectVoice(d[1] & 31); return; }
  if (ty === 0xB0){
    if (d[1] === 1) wheelShow('mod', d[2] / 127);
    if (d[1] === 0x40) ui.sustain = d[2] >= 64;
  }
  if (ty === 0xE0) wheelShow('pitch', (((d[2] << 7) | d[1]) - 8192) / 8192);
  if ((ty === 0x90 || ty === 0x80) && panel.stepNote(d[1], d[2], ty === 0x90 && d[2] > 0)) return;
  if (ty === 0x90 || ty === 0x80) keyLight(d[1], ty === 0x90 && d[2] > 0);
  audio().then(() => post({t:'midi', d}));
}
function midiSysex(d){
  if (!S.func.sysInfo){ return; }
  const r = DX.parseSysexMessage(Uint8Array.from(d));
  if (!r) return;
  if (r.param){ S.edit[r.param[0]] = r.param[1]; post({t:'param', id:r.param[0], v:r.param[1]}); panel.edited = true; voiceVer++; dirty(); return; }
  if (r.fparam){ const k = DX.FUNC_BY_NUM[r.fparam[0]]; if (k){ S.func[k] = r.fparam[1]; post({t:'func', k, v:r.fparam[1]}); dirty(); } return; }
  if (r.voice){ host.setVoice(r.voice); panel.edited = true; panel.flash('MIDI RECEIVED', 'VOICE'); return; }
  if (r.bank){
    if (!panel.protect.int){ S.int = r.bank; panel.flash('MIDI RECEIVED', 'BANK > INTERNAL'); }
    else { S.cart = r.bank; S.cartName = 'MIDI bank'; panel.flash('MIDI RECEIVED', 'BANK > CARTRIDGE'); }
    dirty(); if ($('#dlgLib').open) drawLib(); return;
  }
  if (r.request === 'bank') request('sendBank');
  if (r.request === 'voice') request('sendVoice');
}

/* ---------------- keyboard notes ---------------- */
const ui = {held:new Map(), sustain:false, velocity:100, fixedVel:false};
function noteDown(n, v){
  if (n < 0 || n > 127) return;
  keyLight(n, true);
  if (panel.stepNote(n, v, true)) return;
  audio().then(() => post({t:'key', on:true, n, v}));
}
function noteUp(n){
  keyLight(n, false);
  if (panel.stepNote(n, 0, false)) return;
  post({t:'key', on:false, n});
}

/* ---------------- the faceplate ---------------- */
const root = $('#synth');
const E = {};      // elements by id
function part(id, cls, html){ const e = el('div', cls, root, html); e.id = 'p_' + id; E[id] = e; return e; }
function btn(id, cls, html){ const e = el('button', 'mb ' + (cls || ''), root, html); e.id = 'p_' + id; E[id] = e; return e; }
function lbl(id, text, cls){ const e = el('div', 'lbl ' + (cls || ''), root, text); E[id] = e; return e; }

part('cheekL', 'cheek'); part('cheekR', 'cheek');
for (let i = 0; i < 4; i++) part('screw' + i, 'screw');
part('rule1', 'rule'); part('rule2', 'rule');
part('logo', 'logo', `<svg viewBox="0 0 520 110">
  <text x="4" y="86" font-family="Helvetica, Arial" font-weight="700" font-size="26" letter-spacing="1" fill="#e9ecec">OMEGA</text>
  <g fill="none" stroke="#e9ecec" stroke-width="2.2" font-family="Helvetica, Arial" font-weight="800" font-size="86" font-style="italic" letter-spacing="-2">
    <text x="132" y="94" font-size="112" stroke-width="2.6">7</text><text x="136" y="90" font-size="112" stroke-width="1" opacity=".6">7</text></g>
  <text x="132" y="106" font-family="Helvetica, Arial" font-weight="700" font-size="9.5" letter-spacing="1.6" fill="#e9ecec">6-OPERATOR FM ALGORITHM SYNTHESIZER</text></svg>`);
part('tools', 'tools', `<button class="tool" data-dlg="dlgLib">Memory</button><button class="tool" data-dlg="dlgSeq">Seq / Arp</button><button class="tool" data-dlg="dlgMidi">MIDI</button>
  <button class="tool" id="diceBtn" title="A random voice"><svg viewBox="0 0 20 20" width="15" height="15" style="vertical-align:-3px;margin-right:4px"><rect x="1.5" y="1.5" width="17" height="17" rx="4" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="6" cy="6" r="1.7" fill="currentColor"/><circle cx="10" cy="10" r="1.7" fill="currentColor"/><circle cx="14" cy="14" r="1.7" fill="currentColor"/><circle cx="14" cy="6" r="1.7" fill="currentColor"/><circle cx="6" cy="14" r="1.7" fill="currentColor"/></svg>Random</button><button class="tool" id="resetBtn" title="Back to the stored voice"><svg viewBox="0 0 20 20" width="15" height="15" style="vertical-align:-3px;margin-right:4px"><path d="M15.5 6.5A7 7 0 1 0 17 11" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"/><path d="M16.8 1.8v5.4h-5.4" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>Reset</button><button class="tool" data-dlg="dlgFx">FX</button><button class="tool" data-dlg="dlgHelp">Help</button><button class="tool" id="viewBtn">View</button><button class="tool" id="octDn">Oct&minus;</button><button class="tool" id="octUp">Oct+</button>
  <button class="tool" id="panic">Panic</button><div class="meter"><b id="meterB"></b></div>`);
part('cart', 'cart', '<div class="ct" id="cartT"></div>');
lbl('cartL', 'CARTRIDGE', 'small');
part('chart', 'chart'); part('eg', 'chart'); part('ol', 'chart');
lbl('volL', 'VOLUME'); part('vol', 'slot', '<div class="cap"></div>'); part('volT', 'ticks');
lbl('dataL', 'DATA ENTRY'); part('data', 'slot', '<div class="cap"></div>'); part('dataT', 'ticks');
lbl('noL', '&minus;1 NO', 'small'); lbl('yesL', '+1 YES', 'small');
btn('no', ''); btn('yes', '');
lbl('storeL', 'STORE'); lbl('protL', 'MEMORY PROTECT'); lbl('protIL', 'INTERNAL', 'small'); lbl('protCL', 'CARTRIDGE', 'small'); lbl('opL', 'OPERATOR<br>SELECT', 'small');
btn('store', 'red'); btn('protI', ''); btn('protC', ''); btn('opSel', 'purple');
lbl('editL', 'EDIT'); lbl('memL', 'MEMORY SELECT'); lbl('memIL', 'INTERNAL', 'small'); lbl('memCL', 'CARTRIDGE', 'small'); lbl('funcL', 'FUNCTION');
btn('edit', 'blue', '<div class="face">COMPARE</div>'); btn('memI', ''); btn('memC', ''); btn('func', 'brown');
part('playBar', 'bar'); lbl('playL', 'PLAY', 'small');
part('led', 'ledbox'); part('lcd', 'lcdbox', '<div class="lcd" id="lcdT"></div>');
for (let i = 1; i <= 32; i++){
  btn('b' + i, '', `<span class="n">${i}</span>`);
  lbl('be' + i, DXP.EDIT_BUTTONS[i] || (i <= 6 ? 'OP ' + i + '<br>ON/OFF' : ''), 'small');
  lbl('bf' + i, DXP.FUNC_BUTTONS[i] || '', 'small tan');
}
lbl('bankL1', 'VOICE / PARAMETER SELECT', 'small'); lbl('bankL2', 'FUNCTION', 'small tan');
part('pwL', 'wheelwell', '<div class="wheel"><div class="mark"></div></div>'); lbl('pwLL', 'PITCH', 'small');
part('mwL', 'wheelwell', '<div class="wheel"><div class="mark"></div></div>'); lbl('mwLL', 'MODULATION', 'small');
part('keys', 'keys');

/* ----- two layouts: the whole instrument, and a larger panel for phones ----- */
const FULL = {W:1880, H:800, kb:[36, 96]};
const COMPACT = {W:1000, H:600, kb:[48, 72]};
const PORTRAIT = {W:600, H:1000, kb:[48, 72]};
function place(id, x, y, w, h, extra){ const e = E[id]; if (!e) return; e.style.display = ''; Object.assign(e.style, {left:x + 'px', top:y + 'px', width:w != null ? w + 'px' : '', height:h != null ? h + 'px' : ''}, extra || {}); }
function hide(...ids){ for (const id of ids) if (E[id]) E[id].style.display = 'none'; }
let L = FULL, compact = false, portrait = false, lastLabels = '';
function layout(){
  // a phone held upright gets the stacked portrait panel (unless the full instrument was asked for)
  portrait = window.innerHeight > window.innerWidth * 1.05 && S.view !== 'full';
  const auto = window.innerWidth < 1150 || window.innerHeight < 430;
  compact = portrait || S.view === 'compact' || (S.view === 'auto' && auto);
  L = portrait ? PORTRAIT : compact ? COMPACT : FULL;
  // tall phones: the portrait panel grows downwards and the keys get the room
  if (portrait) L = {W:600, H:Math.round(Math.max(1000, Math.min(1400, 600 * window.innerHeight / window.innerWidth))), kb:PORTRAIT.kb};
  root.style.width = L.W + 'px'; root.style.height = L.H + 'px';
  for (const k in E) E[k].style.display = '';
  if (!compact){
    place('cheekL', 0, 0, 44, 800); place('cheekR', 1836, 0, 44, 800);
    place('screw0', 64, 18); place('screw1', 1804, 18); place('screw2', 64, 380); place('screw3', 1804, 380);
    place('logo', 104, 52, 520, 110);
    place('tools', 104, 170, 800, 28);
    place('cart', 680, 74, 200, 46); place('cartL', 680, 126, 200, 10);
    place('chart', 930, 30, 640, 160); place('eg', 1580, 30, 120, 160); place('ol', 1710, 30, 112, 160);
    place('rule1', 44, 205, 1792, 6);
    place('volL', 160, 228, 70, 10); place('vol', 189, 252, 12, 122); place('volT', 210, 252, 10, 122);
    place('dataL', 262, 228, 90, 10); place('data', 300, 252, 12, 122); place('dataT', 321, 252, 10, 122);
    place('noL', 352, 320, 56, 10); place('yesL', 414, 320, 56, 10); place('no', 354, 336, 52, 26); place('yes', 416, 336, 52, 26);
    place('storeL', 494, 244, 58, 10); place('protL', 560, 230, 150, 10); place('protIL', 566, 246, 62, 9); place('protCL', 638, 246, 66, 9); place('opL', 712, 236, 72, 20);
    place('store', 496, 262, 56, 26); place('protI', 570, 262, 56, 26); place('protC', 642, 262, 56, 26); place('opSel', 716, 262, 56, 26);
    place('editL', 494, 316, 58, 10); place('memL', 560, 302, 150, 10); place('memIL', 566, 318, 62, 9); place('memCL', 638, 318, 66, 9); place('funcL', 708, 316, 72, 10);
    place('edit', 496, 334, 56, 26); place('memI', 570, 334, 56, 26); place('memC', 642, 334, 56, 26); place('func', 716, 334, 56, 26);
    place('playBar', 570, 368, 128, 3); place('playL', 610, 375, 48, 9);
    place('led', 800, 286, 74, 56); place('lcd', 884, 278, 306, 74);
    for (let i = 1; i <= 32; i++){
      const r = i <= 16 ? 0 : 1, c = (i - 1) % 16, x = 1226 + c * 37, y = r ? 334 : 262;
      place('b' + i, x, y, 32, 26); place('be' + i, x - 1, y - 24, 34, 21, {whiteSpace:'normal', fontSize:'6.5px', lineHeight:'1.05', display:'flex', alignItems:'flex-end', justifyContent:'center'});
      place('bf' + i, x - 1, y + 30, 34, 16, {whiteSpace:'normal', fontSize:'6.5px', lineHeight:'1.05'});
    }
    place('bankL1', 1226, 226, 592, 10); place('bankL2', 1226, 382, 592, 10);
    place('rule2', 44, 400, 1792, 22);
    place('pwL', 112, 500, 40, 230); place('pwLL', 102, 482, 60, 10);
    place('mwL', 196, 500, 40, 230); place('mwLL', 176, 482, 80, 10);
    place('keys', 282, 432, 1548, 352);
  } else if (portrait){
    hide('cheekL', 'cheekR', 'screw0', 'screw1', 'screw2', 'screw3', 'logo', 'rule1', 'rule2', 'ol', 'volT', 'dataT', 'bankL1', 'bankL2', 'playL', 'cartL', 'memL', 'protL');
    for (let i = 1; i <= 32; i++) hide('bf' + i);
    place('tools', 8, 8, 584, 66, {flexWrap:'wrap', alignContent:'flex-start', rowGap:'6px'});
    place('led', 8, 84, 70, 54); place('lcd', 86, 80, 506, 84);
    place('chart', 8, 172, 150, 86); place('eg', 166, 172, 180, 86); place('cart', 354, 172, 238, 86);
    const kh = L.H - 708;
    place('volL', 4, 270, 60, 10); place('vol', 28, 288, 12, 150);
    place('dataL', 60, 270, 80, 10); place('data', 94, 288, 12, 150);
    place('noL', 132, 384, 56, 10); place('yesL', 196, 384, 56, 10); place('no', 134, 398, 56, 40); place('yes', 198, 398, 56, 40);
    place('storeL', 268, 270, 74, 10); place('protIL', 348, 270, 74, 10); place('protCL', 428, 270, 74, 10); place('opL', 508, 266, 80, 18);
    place('store', 272, 286, 70, 40); place('protI', 352, 286, 70, 40); place('protC', 432, 286, 70, 40); place('opSel', 512, 286, 70, 40);
    place('editL', 268, 348, 74, 10); place('memIL', 348, 348, 74, 10); place('memCL', 428, 348, 74, 10); place('funcL', 508, 348, 80, 10);
    place('edit', 272, 364, 70, 40); place('memI', 352, 364, 70, 40); place('memC', 432, 364, 70, 40); place('func', 512, 364, 70, 40);
    place('playBar', 352, 412, 150, 3);
    for (let i = 1; i <= 32; i++){
      const r = Math.floor((i - 1) / 8), c = (i - 1) % 8, x = 8 + c * 73, y = 470 + r * 56;
      place('b' + i, x, y, 66, 38); place('be' + i, x - 3, y - 14, 72, 13, {whiteSpace:'nowrap', overflow:'hidden', fontSize:'8px', display:'block'});
    }
    place('pwL', 14, 718, 36, kh - 22); place('pwLL', 4, 702, 56, 10);
    place('mwL', 64, 718, 36, kh - 22); place('mwLL', 50, 702, 64, 10);
    place('keys', 114, 700, 478, kh);
  } else {
    hide('cheekL', 'cheekR', 'screw0', 'screw1', 'screw2', 'screw3', 'logo', 'rule1', 'rule2', 'ol', 'volT', 'dataT', 'bankL1', 'bankL2', 'playL', 'cartL', 'memL', 'protL');
    for (let i = 1; i <= 32; i++) hide('bf' + i);
    place('tools', 10, 8, 980, 30);
    place('volL', 8, 46, 60, 10); place('vol', 32, 62, 12, 150);
    place('dataL', 64, 46, 80, 10); place('data', 98, 62, 12, 150);
    place('noL', 132, 136, 56, 10); place('yesL', 196, 136, 56, 10); place('no', 134, 150, 56, 40); place('yes', 198, 150, 56, 40);
    place('storeL', 264, 44, 66, 10); place('protIL', 332, 44, 66, 10); place('protCL', 398, 44, 70, 10); place('opL', 466, 40, 70, 18);
    place('store', 268, 60, 60, 40); place('protI', 336, 60, 60, 40); place('protC', 404, 60, 60, 40); place('opSel', 472, 60, 60, 40);
    place('editL', 264, 116, 66, 10); place('memIL', 332, 116, 66, 10); place('memCL', 398, 116, 70, 10); place('funcL', 462, 116, 76, 10);
    place('edit', 268, 132, 60, 40); place('memI', 336, 132, 60, 40); place('memC', 404, 132, 60, 40); place('func', 472, 132, 60, 40);
    place('playBar', 336, 178, 128, 3);
    place('led', 548, 44, 64, 50); place('lcd', 620, 42, 370, 84);
    place('chart', 548, 134, 150, 82); place('eg', 704, 134, 136, 82); place('cart', 846, 134, 144, 82);
    for (let i = 1; i <= 32; i++){
      const r = i <= 16 ? 0 : 1, c = (i - 1) % 16, x = 8 + c * 62, y = r ? 296 : 240;
      place('b' + i, x, y, 56, 36); place('be' + i, x - 3, y - 14, 62, 13, {whiteSpace:'nowrap', overflow:'hidden', fontSize:'8px', display:'block'});
    }
    place('pwL', 14, 368, 36, 220); place('pwLL', 4, 352, 56, 10);
    place('mwL', 64, 368, 36, 220); place('mwLL', 50, 352, 64, 10);
    place('keys', 114, 346, 880, 250);
  }
  // the printed labels in the full view; the compact panels relabel the buttons by mode (paint)
  lastLabels = '';
  if (!compact) for (let i = 1; i <= 32; i++) E['be' + i].innerHTML = DXP.EDIT_BUTTONS[i] || (i <= 6 ? 'OP ' + i + '<br>ON/OFF' : '');
  buildKeys(); drawChart(); fit();
}
function fit(){
  const sc = Math.min(window.innerWidth / L.W, window.innerHeight / L.H);
  root.style.transform = `scale(${sc})`;
  root.style.left = Math.max(0, (window.innerWidth - L.W * sc) / 2) + 'px';
  root.style.top = Math.max(0, (window.innerHeight - L.H * sc) / 2) + 'px';
}
// relayout on rotation (some phones report the new size a moment after the event)
let relay = 0;
const relayout = () => { layout(); clearTimeout(relay); relay = setTimeout(layout, 300); };
window.addEventListener('resize', relayout);
window.addEventListener('orientationchange', relayout);
if (screen.orientation && screen.orientation.addEventListener) screen.orientation.addEventListener('change', relayout);

/* ----- the algorithm chart: the 32 algorithms as on the panel (compact: the current one) ----- */
function algGeometry(ai){
  const a = DX.ALGS[ai], pos = {};
  const mods = k => [0, 1, 2, 3, 4, 5].filter(s => a.mod[k] >> s & 1);
  let col = 0;
  const placeOp = (k, row, c) => {
    if (pos[k]) return 0;
    const ms = mods(k).filter(m => !pos[m]);
    pos[k] = {x:c, y:row};
    let w = 0;
    for (const m of ms) w += placeOp(m, row + 1, c + w);
    pos[k].x = ms.length ? (c + (c + Math.max(1, w) - 1)) / 2 : c;
    return Math.max(1, w);
  };
  for (let k = 0; k < 6; k++) if (a.car[k]) col += placeOp(k, 0, col);
  // shared modulators sit over the middle of the operators they modulate
  for (let k = 0; k < 6; k++){
    const parents = [0, 1, 2, 3, 4, 5].filter(p => a.mod[p] >> k & 1);
    if (parents.length > 1) pos[k].x = parents.reduce((s, p) => s + pos[p].x, 0) / parents.length;
  }
  let rows = 0; for (const k in pos) rows = Math.max(rows, pos[k].y);
  return {pos, cols:col, rows:rows + 1, a, mods};
}
function algSvg(ai, ox, oy, cw, ch, s, big){
  const g = algGeometry(ai), {pos, a} = g;
  const sx = Math.min(s * 1.35, (cw - 4) / Math.max(1, g.cols)), sy = Math.min(s * 1.45, (ch - 12) / g.rows);
  const x0 = ox + (cw - sx * (g.cols - 1)) / 2, yb = oy + ch - 12 - s / 2;
  const P = k => [x0 + pos[k].x * sx, yb - pos[k].y * sy];
  let out = '';
  for (let k = 0; k < 6; k++) for (const m of g.mods(k)){ const [x1, y1] = P(k), [x2, y2] = P(m); out += `<path class="ln" d="M${x2} ${y2 + s / 2} L${x2} ${(y1 + y2) / 2} L${x1} ${(y1 + y2) / 2} L${x1} ${y1 - s / 2}"/>`; }
  const cars = [0, 1, 2, 3, 4, 5].filter(k => a.car[k]).map(P);
  if (cars.length){ const yl = yb + s / 2 + 3, xa = Math.min(...cars.map(c => c[0])), xb = Math.max(...cars.map(c => c[0])); out += `<path class="ln" d="M${xa} ${yl} L${xb} ${yl}"/>`; for (const c of cars) out += `<path class="ln" d="M${c[0]} ${c[1] + s / 2} L${c[0]} ${yl}"/>`; }
  { // feedback loop
    const [fx, fy] = P(a.fb), [sx2, sy2] = P(a.fbs), r = s / 2 + 3;
    out += `<path class="ln" d="M${sx2 + s / 2} ${sy2} L${sx2 + r + 1} ${sy2} L${sx2 + r + 1} ${fy - r} L${fx} ${fy - r} L${fx} ${fy - s / 2}"/>`;
  }
  for (let k = 0; k < 6; k++){ const [x, y] = P(k); out += `<rect class="box" x="${x - s / 2}" y="${y - s / 2}" width="${s}" height="${s}"/>${big ? `<text class="opx" x="${x}" y="${y + 2.2}" text-anchor="middle" style="fill:#e9ecec">${k + 1}</text>` : ''}`; }
  return out;
}
function drawChart(){
  if (compact){
    E.chart.innerHTML = `<svg viewBox="0 0 150 82"><text class="head" x="5" y="11">ALGORITHM</text><text class="num" id="algNum" x="145" y="11" text-anchor="end"></text><g id="algBig"></g></svg>`;
    E.eg.innerHTML = '<svg viewBox="0 0 136 82" id="egSvg"></svg>';
    lastDraw = -1; return;
  }
  let s = `<svg viewBox="0 0 640 160"><text class="head" x="6" y="11">ALGORITHM</text>`;
  for (let i = 0; i < 32; i++){
    const r = i < 16 ? 0 : 1, c = i % 16, x = c * 40, y = 14 + r * 73;
    s += `<g class="alg" data-alg="${i}"><rect class="bg" x="${x + 1}" y="${y}" width="38" height="71" rx="2"/>${algSvg(i, x, y, 40, 70, 6.5)}<text class="num" x="${x + 20}" y="${y + 69}" text-anchor="middle">${i + 1}</text></g>`;
  }
  E.chart.innerHTML = s + '</svg>';
  E.eg.innerHTML = '<svg viewBox="0 0 120 160" id="egSvg"></svg>';
  E.ol.innerHTML = '<svg viewBox="0 0 112 160" id="olSvg"></svg>';
  lastDraw = -1;
}
// a tap on an algorithm picks it (in EDIT mode)
root.addEventListener('pointerdown', e => {
  const g = e.target.closest('.alg'); if (!g) return;
  if (panel.mode !== 'edit'){ panel.flash('ALGORITHM', 'PUSH EDIT FIRST'); return; }
  panel.eBtn = 7; panel.set(134, +g.dataset.alg);
});
let lastDraw = -1, lastOp = -1, lastAlg = -1;
function drawDiagrams(){
  if (lastDraw === voiceVer && lastOp === panel.op) return;
  lastDraw = voiceVer; lastOp = panel.op;
  const v = S.edit, alg = v[134], ob = DX.opBase(panel.op);
  if (compact){
    const g = $('#algBig'); if (g && lastAlg !== alg + 100){ g.innerHTML = algSvg(alg, 0, 12, 150, 70, 11, true); }
    const n = $('#algNum'); if (n) n.textContent = alg + 1;
  } else if (lastAlg !== alg){
    for (const g of $$('.alg')) g.classList.toggle('on', +g.dataset.alg === alg);
  }
  lastAlg = compact ? alg + 100 : alg;
  // envelope of the selected operator
  const eg = $('#egSvg');
  if (eg){
    const W = compact ? 136 : 120, H = compact ? 82 : 160, top = compact ? 16 : 30, bot = H - (compact ? 10 : 30);
    const r = [0, 1, 2, 3].map(i => v[ob + i]), l = [0, 1, 2, 3].map(i => v[ob + 4 + i]);
    const dur = x => 3 + (99 - x) * 0.22;
    const Y = lv => bot - (bot - top) * lv / 99;
    const total = dur(r[0]) + dur(r[1]) + dur(r[2]) + 14 + dur(r[3]);
    const kx = (W - 12) / total;
    let x = 6, pts = [[x, Y(l[3])]];
    x += dur(r[0]) * kx; pts.push([x, Y(l[0])]); x += dur(r[1]) * kx; pts.push([x, Y(l[1])]); x += dur(r[2]) * kx; pts.push([x, Y(l[2])]);
    const kOff = x + 14 * kx; pts.push([kOff, Y(l[2])]); x = kOff + dur(r[3]) * kx; pts.push([x, Y(l[3])]);
    eg.innerHTML = `<text class="head" x="5" y="11">EG  OP${panel.op}</text>` +
      `<path class="ln" d="M6 ${bot} L${W - 6} ${bot}" style="stroke:#777"/><path class="ln" d="M${kOff} ${top - 4} L${kOff} ${bot}" style="stroke:#777;stroke-dasharray:2 2"/>` +
      `<polyline class="ln" style="stroke:var(--teal);stroke-width:1.6" points="${pts.map(p => p.join(',')).join(' ')}"/>` +
      (compact ? '' : `<text x="6" y="${H - 18}">L1 ${l[0]} L2 ${l[1]} L3 ${l[2]} L4 ${l[3]}</text><text x="6" y="${H - 8}">R1 ${r[0]} R2 ${r[1]} R3 ${r[2]} R4 ${r[3]}</text>`);
  }
  // keyboard level scaling of the selected operator
  const ol = $('#olSvg');
  if (ol){
    const bp = v[ob + 8], ld = v[ob + 9], rd = v[ob + 10], lc = v[ob + 11], rc = v[ob + 12], W = 112, mid = 80, cx = 12 + (W - 24) * bp / 99;
    const curve = (side, depth, c) => {
      let d = ''; for (let i = 0; i <= 20; i++){
        const t = i / 20, x = side < 0 ? cx - (cx - 12) * t : cx + (W - 12 - cx) * t;
        let y = (c === 0 || c === 3) ? t : (Math.exp(t * 3) - 1) / (Math.exp(3) - 1);
        y *= depth / 99 * 48; if (c < 2) y = -y;
        d += (i ? 'L' : 'M') + x.toFixed(1) + ' ' + (mid - y).toFixed(1);
      } return d;
    };
    ol.innerHTML = `<text class="head" x="5" y="11">LEVEL SCALING</text><path class="ln" d="M12 ${mid} L${W - 12} ${mid}" style="stroke:#777"/>` +
      `<path class="ln" d="M${cx} 26 L${cx} 134" style="stroke:#777;stroke-dasharray:2 2"/>` +
      `<path class="ln" style="stroke:var(--teal);stroke-width:1.6" d="${curve(-1, ld, lc)}"/><path class="ln" style="stroke:var(--teal);stroke-width:1.6" d="${curve(1, rd, rc)}"/>` +
      `<text x="${cx}" y="146" text-anchor="middle">${DX.bpName(bp)}</text><text x="8" y="156">${DX.CURVES[lc]} ${ld}</text><text x="${W - 8}" y="156" text-anchor="end">${DX.CURVES[rc]} ${rd}</text>`;
  }
}

/* ----- keyboard ----- */
const keyEls = new Map();
const isBlack = n => [1, 3, 6, 8, 10].includes(n % 12);
function kbRange(){ const [a, b] = L.kb; const o = compact ? S.octave * 12 : 0; return [a + o, b + o]; }
function buildKeys(){
  const k = E.keys; k.innerHTML = ''; keyEls.clear();
  const [lo, hi] = kbRange();
  const whites = []; for (let n = lo; n <= hi; n++) if (!isBlack(n)) whites.push(n);
  const W = parseFloat(k.style.width), H = parseFloat(k.style.height), ww = W / whites.length;
  let i = 0;
  for (let n = lo; n <= hi; n++){
    if (isBlack(n)) continue;
    const d = el('div', 'wk', k); d.style.left = (i * ww) + 'px'; d.style.width = ww + 'px'; d.dataset.n = n;
    if (n % 12 === 0) el('div', 'cn', d, DX.noteName(n));
    keyEls.set(n, d); i++;
  }
  for (let n = lo; n <= hi; n++){
    if (!isBlack(n)) continue;
    const wi = whites.indexOf(n - 1); if (wi < 0) continue;
    const d = el('div', 'bk', k); const bw = ww * 0.6; d.style.left = ((wi + 1) * ww - bw / 2) + 'px'; d.style.width = bw + 'px'; d.style.height = (H * 0.62) + 'px'; d.dataset.n = n;
    keyEls.set(n, d);
  }
}
function keyLight(n, on){ const e = keyEls.get(n); if (e) e.classList.toggle('on', on); }
const touches = new Map();     // pointerId -> note
function keyAt(x, y){ const e = document.elementFromPoint(x, y); return e && e.dataset && e.dataset.n != null && e.closest('.keys') ? e : null; }
function velFrom(e, keyEl){
  if (ui.fixedVel) return ui.velocity;
  const r = keyEl.getBoundingClientRect(), f = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height));
  return Math.max(20, Math.min(127, Math.round(30 + f * 97)));
}
E.keys.addEventListener('pointerdown', e => {
  const k = keyAt(e.clientX, e.clientY); if (!k) return;
  e.preventDefault(); E.keys.setPointerCapture(e.pointerId);
  const n = +k.dataset.n; touches.set(e.pointerId, n); noteDown(n, velFrom(e, k));
});
E.keys.addEventListener('pointermove', e => {
  if (!touches.has(e.pointerId)) return;
  const k = keyAt(e.clientX, e.clientY); if (!k) return;
  const n = +k.dataset.n, was = touches.get(e.pointerId);
  if (n !== was){ noteUp(was); touches.set(e.pointerId, n); noteDown(n, velFrom(e, k)); }
});
const endTouch = e => { if (!touches.has(e.pointerId)) return; noteUp(touches.get(e.pointerId)); touches.delete(e.pointerId); };
E.keys.addEventListener('pointerup', endTouch); E.keys.addEventListener('pointercancel', endTouch);

// computer keyboard: A W S E D F T G Y H U J K O L P ; (Z / X octave)
const QWERTY = 'awsedftgyhujkolp;';
let qOct = 0; const qHeld = new Map();
document.addEventListener('keydown', e => {
  if (e.target.closest && e.target.closest('dialog')) return;
  if (e.repeat) return;
  const k = e.key.toLowerCase();
  if (panel.mode === 'edit' && panel.naming && k.length === 1 && /[ -~]/.test(e.key)){ panel.set(145 + panel.nameCur, e.key.charCodeAt(0)); panel.nameCur = Math.min(9, panel.nameCur + 1); e.preventDefault(); return; }
  if (k === 'z'){ qOct = Math.max(-3, qOct - 1); return; } if (k === 'x'){ qOct = Math.min(3, qOct + 1); return; }
  const i = QWERTY.indexOf(k); if (i < 0) return;
  const n = 60 + qOct * 12 + i; qHeld.set(k, n); noteDown(n, ui.velocity);
});
document.addEventListener('keyup', e => { const k = e.key.toLowerCase(); if (qHeld.has(k)){ noteUp(qHeld.get(k)); qHeld.delete(k); } });

/* ----- wheels ----- */
const wheels = {pitch:0, mod:0};
function wheelShow(which, v){
  wheels[which] = v;
  const w = which === 'pitch' ? E.pwL : E.mwL, m = w.querySelector('.mark'), h = parseFloat(w.style.height) || 200;
  const f = which === 'pitch' ? (0.5 - v / 2) : (1 - v);
  m.style.top = (f * (h - 6)) + 'px';
}
function wheelDrag(which, wel){
  let id = null;
  const at = e => { const r = wel.getBoundingClientRect(); const f = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height)); return which === 'pitch' ? (0.5 - f) * 2 : 1 - f; };
  const send = v => {
    wheelShow(which, v);
    if (which === 'pitch'){ const b = Math.max(-8192, Math.min(8191, Math.round(v * 8192))); post({t:'ctl', k:'bend', v:b}); midiSend([0xE0 | S.func.txCh, (b + 8192) & 127, ((b + 8192) >> 7) & 127]); }
    else { const c = Math.round(v * 127); post({t:'ctl', k:'wheel', v:c}); midiSend([0xB0 | S.func.txCh, 1, c]); }
  };
  wel.addEventListener('pointerdown', e => { id = e.pointerId; wel.setPointerCapture(id); audio(); send(at(e)); });
  wel.addEventListener('pointermove', e => { if (e.pointerId === id) send(at(e)); });
  const up = e => { if (e.pointerId !== id) return; id = null; if (which === 'pitch') send(0); };
  wel.addEventListener('pointerup', up); wel.addEventListener('pointercancel', up);
}
wheelDrag('pitch', E.pwL); wheelDrag('mod', E.mwL);

/* ----- sliders ----- */
const sliders = {vol:S.func.volume / 99, data:0.5};
function sliderShow(id){ const s = E[id], cap = s.querySelector('.cap'), h = parseFloat(s.style.height) || 120; cap.style.top = ((1 - sliders[id]) * (h - 26)) + 'px'; }
function sliderDrag(id, onMove){
  const s = E[id]; let pid = null;
  const at = e => { const r = s.getBoundingClientRect(); return Math.max(0, Math.min(1, 1 - (e.clientY - r.top) / r.height)); };
  s.addEventListener('pointerdown', e => { pid = e.pointerId; s.setPointerCapture(pid); sliders[id] = at(e); sliderShow(id); onMove(sliders[id]); });
  s.addEventListener('pointermove', e => { if (e.pointerId !== pid) return; sliders[id] = at(e); sliderShow(id); onMove(sliders[id]); });
  const up = e => { if (e.pointerId === pid) pid = null; };
  s.addEventListener('pointerup', up); s.addEventListener('pointercancel', up);
}
sliderDrag('vol', x => host.setFunc('volume', Math.round(x * 99)));
sliderDrag('data', x => panel.data(x));

/* ----- buttons ----- */
function press(id, fn, repeat){
  const b = E[id]; let t1 = 0, t2 = 0;
  b.addEventListener('pointerdown', e => {
    e.preventDefault(); b.classList.add('dn'); audio(); fn(); paint();
    if (repeat){ t1 = setTimeout(() => { t2 = setInterval(() => { fn(); paint(); }, 70); }, 420); }
  });
  const up = () => { b.classList.remove('dn'); clearTimeout(t1); clearInterval(t2); dirty(); };
  b.addEventListener('pointerup', up); b.addEventListener('pointerleave', up); b.addEventListener('pointercancel', up);
}
press('no', () => panel.no(), true); press('yes', () => panel.yes(), true);
press('store', () => panel.storeBtn()); press('protI', () => panel.protectBtn('int')); press('protC', () => panel.protectBtn('cart')); press('opSel', () => panel.opSelect());
press('edit', () => panel.editBtn()); press('memI', () => panel.memSelect('int')); press('memC', () => panel.memSelect('cart')); press('func', () => panel.funcBtn());
for (let i = 1; i <= 32; i++) press('b' + i, () => panel.button(i));
E.cart.addEventListener('click', () => openDlg('dlgLib'));
$$('[data-dlg]').forEach(b => b.addEventListener('click', () => openDlg(b.dataset.dlg)));
// View: the whole instrument, or the larger panel (upright phones use the portrait panel unless the whole instrument is chosen)
$('#viewBtn').addEventListener('click', () => { S.view = compact ? 'full' : (window.innerHeight > window.innerWidth * 1.05 ? 'auto' : 'compact'); layout(); dirty(); });
$('#octDn').addEventListener('click', () => { S.octave = Math.max(-3, S.octave - 1); buildKeys(); dirty(); });
$('#octUp').addEventListener('click', () => { S.octave = Math.min(3, S.octave + 1); buildKeys(); dirty(); });
$('#diceBtn').addEventListener('click', () => { audio(); panel.random(); paint(); dirty(); });
$('#resetBtn').addEventListener('click', () => { panel.resetVoice(); paint(); dirty(); });
$('#panic').addEventListener('click', () => { post({t:'panic'}); S.seq.arp.on = false; for (const n of keyEls.keys()) keyLight(n, false); midiSend([0xB0 | S.func.txCh, 123, 0]); toast('All notes off'); });

/* ----- painting: display, LED, lights ----- */
let lastL1 = '', lastL2 = '', lastLed = '';
function paint(){
  const [l1, l2] = panel.lines();
  if (l1 !== lastL1 || l2 !== lastL2){ $('#lcdT').textContent = l1 + '\n' + l2; lastL1 = l1; lastL2 = l2; }
  const Lt = panel.lights();
  const led = panel.mode === 'play' ? String(panel.num + 1) : panel.mode === 'edit' ? String(panel.naming ? 31 : panel.eBtn) : String(panel.fBtn);
  if (led !== lastLed){ E.led.textContent = led.padStart(2, ' '); lastLed = led; }
  E.store.classList.toggle('lit', Lt.store); E.protI.classList.toggle('lit', Lt.protInt); E.protC.classList.toggle('lit', Lt.protCart);
  E.edit.classList.toggle('lit', Lt.edit); E.memI.classList.toggle('lit', Lt.memInt); E.memC.classList.toggle('lit', Lt.memCart); E.func.classList.toggle('lit', Lt.func);
  E.edit.querySelector('.face').style.opacity = Lt.compare ? 1 : .55;
  for (let i = 1; i <= 32; i++){
    let on = i === Lt.btn;
    if (Lt.steps && i <= 16) on = Lt.steps[i - 1] > 0 || (stat.playing && stat.step === (panel.stepPage * 16 + i - 1));
    if (panel.mode === 'edit' && i <= 6) on = on || !!((S.edit[155] >> (6 - i)) & 1);
    E['b' + i].classList.toggle('lit', on);
  }
  // compact: the labels follow the mode
  if (compact){
    const key = panel.mode + (panel.stepEdit ? 's' : '');
    if (key !== lastLabels){
      lastLabels = key;
      for (let i = 1; i <= 32; i++){
        let t = panel.mode === 'edit' ? (DXP.EDIT_BUTTONS[i] || 'OP' + i + ' ON/OFF') : panel.mode === 'func' ? DXP.FUNC_BUTTONS[i] : (DXV.GROUPS[i - 1] || '');
        if (panel.stepEdit) t = i <= 16 ? 'STEP ' + i : i <= 20 ? 'PAGE ' + (i - 16) : i === 21 ? 'CLR STEP' : i === 22 ? 'CLR PAT' : DXP.FUNC_BUTTONS[i];
        E['be' + i].textContent = t;
      }
    }
  } else lastLabels = '';
  E.cart.classList.toggle('in', !!S.cart); $('#cartT').textContent = S.cart ? (S.cartName || 'cartridge').replace(/\.\w+$/, '') : (compact ? 'CARTRIDGE (TAP)' : '');
  drawDiagrams();
}
function frame(){
  paint();
  const pk = Math.min(1, stat.peak || 0); $('#meterB').style.width = (pk * 100) + '%';
  if ($('#dlgSeq').open) seqPlayhead();
  requestAnimationFrame(frame);
}

/* ---------------- dialogs ---------------- */
function openDlg(id){ const d = $('#' + id); if (id === 'dlgFx') drawFx(); if (id === 'dlgLib') drawLib(); if (id === 'dlgSeq') drawSeq(); if (id === 'dlgMidi') drawMidi(); if (id === 'dlgHelp') drawHelp(); if (!d.open) d.showModal(); }
$$('dialog').forEach(d => { d.querySelector('[data-close]').addEventListener('click', () => d.close()); d.addEventListener('click', e => { if (e.target === d) d.close(); }); });

/* ----- memory and cartridge ----- */
let libSel = null;   // {src, i} picked for copying
function drawLib(){
  const b = $('#libBody');
  const list = (src, bank) => bank ? bank.map((v, i) => `<button data-src="${src}" data-i="${i}" class="${libSel && libSel.src === src && libSel.i === i ? 'sel' : ''} ${panel.src === src && panel.num === i ? 'cur' : ''}"><i>${String(i + 1).padStart(2, '0')}</i>${DX.voiceName(v)}</button>`).join('') : '<p class="hint">No cartridge. Load a .syx bank (any 32-voice sysex dump, 4104 bytes) or receive one over MIDI.</p>';
  b.innerHTML = `
    <p class="hint">Tap a voice to play it. To copy, tap a voice, then <b>Copy here</b> on the slot it goes to (memory protect must be off). The edit buffer is stored with the panel's STORE button.</p>
    <div class="row">
      <button class="btn pri" data-a="loadCart">Load .syx into cartridge</button><button class="btn" data-a="loadInt">Load .syx into internal</button>
      <button class="btn" data-a="loadVoice">Load single voice</button><button class="btn" data-a="saveInt">Save internal bank</button><button class="btn" data-a="saveCart">Save cartridge bank</button>
      <button class="btn" data-a="saveVoice">Save edit voice</button><button class="btn" data-a="ejectCart">Eject cartridge</button><button class="btn warn" data-a="factory">Factory voices &gt; internal</button>
    </div>
    <div class="row"><label><input type="checkbox" id="pInt" ${panel.protect.int ? 'checked' : ''}> Memory protect internal</label>
      <label><input type="checkbox" id="pCart" ${panel.protect.cart ? 'checked' : ''}> Memory protect cartridge</label>
      ${libSel ? `<span class="hint">Copying ${libSel.src === 'int' ? 'INT' : 'CRT'} ${libSel.i + 1}: tap <b>Copy here</b>…</span>` : ''}</div>
    <div class="banks">
      <div class="bank"><h4>Internal memory</h4><div class="vlist">${list('int', S.int)}</div></div>
      <div class="bank"><h4>Cartridge ${S.cart ? '&middot; ' + (S.cartName || '') : ''}</h4><div class="vlist">${list('cart', S.cart)}</div></div>
    </div>
    <div class="row" id="libCopy" style="display:${libSel ? 'flex' : 'none'}"><span class="hint">Copy here:</span><select id="cpDst"><option value="int">Internal</option><option value="cart">Cartridge</option></select>
      <select id="cpNum">${Array.from({length:32}, (_, i) => `<option value="${i}">${i + 1}</option>`).join('')}</select><button class="btn pri" data-a="copy">Copy here</button><button class="btn" data-a="cancel">Cancel</button></div>`;
  b.querySelectorAll('.vlist button').forEach(x => x.addEventListener('click', () => {
    const src = x.dataset.src, i = +x.dataset.i;
    audio(); panel.src = src; panel.mode = 'play'; panel.selectVoice(i);
    libSel = {src, i}; drawLib();
  }));
  b.querySelector('#pInt').onchange = e => { panel.protect.int = e.target.checked; dirty(); };
  b.querySelector('#pCart').onchange = e => { panel.protect.cart = e.target.checked; dirty(); };
  b.querySelectorAll('[data-a]').forEach(x => x.addEventListener('click', () => {
    const a = x.dataset.a;
    if (a === 'loadCart') pick('cart', '.syx,.SYX,.bin');
    if (a === 'loadInt') pick('int', '.syx,.SYX,.bin');
    if (a === 'loadVoice') pick('voice', '.syx,.SYX');
    if (a === 'saveInt') request('saveBank', 'int');
    if (a === 'saveCart') request('saveBank', 'cart');
    if (a === 'saveVoice') request('saveVoice');
    if (a === 'ejectCart'){ S.cart = null; S.cartName = ''; if (panel.src === 'cart') panel.src = 'int'; dirty(); drawLib(); }
    if (a === 'factory'){ if (panel.protect.int){ toast('Turn memory protect internal off first'); return; } if (confirm('Replace the internal memory with the 32 factory voices?')){ S.int = DXV.FACTORY.map(v => Uint8Array.from(v)); dirty(); drawLib(); } }
    if (a === 'cancel'){ libSel = null; drawLib(); }
    if (a === 'copy' && libSel){
      const dst = $('#cpDst').value, n = +$('#cpNum').value;
      if (panel.protect[dst]){ toast('Memory protect is on for the ' + (dst === 'int' ? 'internal memory' : 'cartridge')); return; }
      if (dst === 'cart' && !S.cart){ toast('No cartridge'); return; }
      const v = Uint8Array.from((libSel.src === 'int' ? S.int : S.cart)[libSel.i]);
      (dst === 'int' ? S.int : S.cart)[n] = v; libSel = null; dirty(); drawLib(); toast('Copied');
    }
  }));
}

/* ----- sequencer / arpeggiator ----- */
let seqPage = 0, seqSel = 0;
function drawSeq(){
  const b = $('#seqBody'), q = S.seq, p = q.pats[q.pat], a = q.arp;
  const opt = (arr, cur) => arr.map((t, i) => `<option value="${i}" ${i === cur ? 'selected' : ''}>${t}</option>`).join('');
  const st = p.steps[seqSel] || p.steps[0];
  b.innerHTML = `
    <div class="row">
      <button class="btn ${stat.playing ? 'on' : 'pri'}" id="sqPlay">${stat.playing ? 'Stop' : 'Play'}</button>
      <button class="btn ${stat.rec ? 'warn' : ''}" id="sqRec">${stat.rec ? 'Recording' : 'Record'}</button>
      <label>Pattern <select id="sqPat">${opt(Array.from({length:16}, (_, i) => String(i + 1)), q.pat)}</select></label>
      <label>Tempo <input type="number" id="sqTempo" min="30" max="300" value="${q.tempo}" style="width:64px"></label>
      <label><input type="checkbox" id="sqExt" ${q.ext ? 'checked' : ''}> MIDI clock in</label>
      <label>Length <input type="number" id="sqLen" min="1" max="64" value="${p.len}" style="width:56px"></label>
      <label>Step <select id="sqRate">${opt(DXS.RATES.map(r => r[1]), p.rate)}</select></label>
      <label>Swing <input type="range" id="sqSwing" min="50" max="75" value="${p.swing}"></label>
      <label><input type="checkbox" id="sqTrans" ${q.keyTrans ? 'checked' : ''}> Keys transpose</label>
      <label>Chain <input type="text" id="sqChain" value="${q.chain.map(x => x + 1).join(' ')}" placeholder="e.g. 1 1 2 3" style="width:110px"></label>
    </div>
    <div class="row"><span class="hint">Steps ${seqPage * 16 + 1}–${seqPage * 16 + 16}</span>${[0, 1, 2, 3].map(i => `<button class="btn ${i === seqPage ? 'on' : ''}" data-page="${i}">${i * 16 + 1}–${i * 16 + 16}</button>`).join('')}
      <button class="btn" id="sqClear">Clear pattern</button><button class="btn" id="sqCopy">Copy to next</button><button class="btn" id="sqImp">Import MIDI</button><button class="btn" id="sqExp">Export MIDI</button></div>
    <div class="grid" id="sqGrid">${Array.from({length:16}, (_, i) => { const k = seqPage * 16 + i, s = p.steps[k];
      return `<button class="st ${k >= p.len ? 'cut' : ''} ${s.notes.length ? 'has' : ''} ${s.on ? '' : 'off'} ${k === seqSel ? 'sel' : ''}" data-k="${k}">${k + 1}<br>${s.notes.map(x => DX.noteName(x[0])).join(' ')}<span class="g" style="width:${Math.min(100, s.gate)}%"></span></button>`; }).join('')}</div>
    <div class="row"><b style="font-size:13px">Step ${seqSel + 1}</b>
      <label><input type="checkbox" id="stOn" ${st.on ? 'checked' : ''}> On</label>
      <label>Gate <input type="range" id="stGate" min="5" max="${DXS.TIE}" value="${st.gate}"> <span id="stGateT">${st.gate >= DXS.TIE ? 'TIE' : st.gate + '%'}</span></label>
      <span class="hint">${st.notes.map(x => DX.noteName(x[0]) + '/' + x[1]).join('  ') || 'rest'}</span>
      <button class="btn" id="stWrite">Write notes from keys</button><button class="btn" id="stClr">Clear step</button></div>
    <p class="hint">Write: turn it on and play a note or chord on the keyboard; it goes into the selected step and moves on (the same as FUNCTION 28 on the panel). Real-time recording: Play, then Record and play along; moving a parameter while recording writes it into a motion lane.</p>
    <h4 style="margin:12px 0 4px;color:var(--tan);font:700 12px var(--sans);letter-spacing:.6px">MOTION LANES (any voice or function parameter, per step)</h4>
    ${p.lanes.map((l, li) => `<div class="row"><label>Lane ${li + 1} <select data-lane="${li}" class="lnP">${DXP.MOTION_IDS.map(id => `<option value="${id}" ${id === l.param ? 'selected' : ''}>${DXP.motionName(id)}</option>`).join('')}</select></label>
      <label><input type="checkbox" class="lnS" data-lane="${li}" ${l.smooth ? 'checked' : ''}> Smooth</label><button class="btn lnC" data-lane="${li}">Clear</button></div>
      <div class="lane" data-lane="${li}">${Array.from({length:16}, (_, i) => { const k = seqPage * 16 + i, val = l.vals[k], mx = laneMax(l.param);
        return `<div class="lv ${val < 0 ? 'none' : ''} ${k >= p.len ? 'st cut' : ''}" data-k="${k}"><b style="height:${val < 0 ? 0 : (val / mx * 100)}%"></b></div>`; }).join('')}</div>`).join('')}
    <h4 style="margin:12px 0 4px;color:var(--tan);font:700 12px var(--sans);letter-spacing:.6px">ARPEGGIATOR</h4>
    <div class="row"><button class="btn ${a.on ? 'on' : ''}" id="arOn">${a.on ? 'Arp on' : 'Arp off'}</button>
      <label>Mode <select id="arMode">${opt(DXS.ARP_MODES, a.mode)}</select></label>
      <label>Octaves <select id="arOct">${opt(['1', '2', '3', '4'], a.oct - 1)}</select></label>
      <label>Rate <select id="arRate">${opt(DXS.RATES.map(r => r[1]), a.rate)}</select></label>
      <label>Gate <input type="range" id="arGate" min="5" max="100" value="${a.gate}"></label>
      <label>Swing <input type="range" id="arSwing" min="50" max="75" value="${a.swing}"></label>
      <label><input type="checkbox" id="arLatch" ${a.latch ? 'checked' : ''}> Latch</label></div>
    <p class="hint">The arpeggiator runs on the same clock as the sequencer (the tempo above, or MIDI clock in). Keys go to the arpeggiator while it is on; with the sequencer playing and "Keys transpose" on, a key transposes the pattern (C3 = as written).</p>`;
  const ch = () => host.seqCmd('changed');
  $('#sqPlay').onclick = () => { host.seqCmd(stat.playing ? 'stop' : 'play'); stat.playing = !stat.playing; setTimeout(drawSeq, 80); };
  $('#sqRec').onclick = () => { host.seqCmd('rec', !stat.rec); stat.rec = !stat.rec; setTimeout(drawSeq, 80); };
  $('#sqPat').onchange = e => { host.seqCmd('pattern', +e.target.value); };
  $('#sqTempo').onchange = e => { q.tempo = Math.max(30, Math.min(300, +e.target.value || 120)); ch(); };
  $('#sqExt').onchange = e => { q.ext = e.target.checked; ch(); };
  $('#sqLen').onchange = e => { p.len = Math.max(1, Math.min(64, +e.target.value || 16)); ch(); };
  $('#sqRate').onchange = e => { p.rate = +e.target.value; ch(); };
  $('#sqSwing').oninput = e => { p.swing = +e.target.value; post({t:'seq', s:q}); dirty(); };
  $('#sqTrans').onchange = e => { q.keyTrans = e.target.checked; ch(); };
  $('#sqChain').onchange = e => { q.chain = e.target.value.split(/[\s,]+/).map(x => parseInt(x, 10) - 1).filter(x => x >= 0 && x < 16); ch(); };
  b.querySelectorAll('[data-page]').forEach(x => x.onclick = () => { seqPage = +x.dataset.page; drawSeq(); });
  $('#sqClear').onclick = () => { if (confirm('Clear pattern ' + (q.pat + 1) + '?')){ q.pats[q.pat] = DXS.emptyPattern(); ch(); } };
  $('#sqCopy').onclick = () => { const to = (q.pat + 1) % 16; q.pats[to] = DXS.copyPattern(p); ch(); toast('Copied to pattern ' + (to + 1)); };
  $('#sqImp').onclick = () => request('midiImport');
  $('#sqExp').onclick = () => request('midiExport');
  b.querySelectorAll('#sqGrid .st').forEach(x => x.onclick = () => { const k = +x.dataset.k; if (k === seqSel) p.steps[k].on = !p.steps[k].on, ch(); seqSel = k; panel.stepSel = k; host.seqCmd('cursor', k); drawSeq(); });
  $('#stOn').onchange = e => { st.on = e.target.checked; ch(); };
  $('#stGate').oninput = e => { st.gate = +e.target.value; $('#stGateT').textContent = st.gate >= DXS.TIE ? 'TIE' : st.gate + '%'; post({t:'seq', s:q}); dirty(); };
  $('#stGate').onchange = () => drawSeq();
  $('#stWrite').onclick = () => { panel.mode = 'func'; panel.fBtn = 28; panel.stepEdit = true; panel.stepSel = seqSel; panel.stepPage = Math.floor(seqSel / 16); $('#dlgSeq').close(); toast('Play the notes for step ' + (seqSel + 1) + ' (FUNCTION 28 is on)'); };
  $('#stClr').onclick = () => { st.notes = []; ch(); };
  b.querySelectorAll('.lnP').forEach(x => x.onchange = () => { const l = p.lanes[+x.dataset.lane]; l.param = +x.value; l.vals.fill(-1); ch(); });
  b.querySelectorAll('.lnS').forEach(x => x.onchange = () => { p.lanes[+x.dataset.lane].smooth = x.checked; ch(); });
  b.querySelectorAll('.lnC').forEach(x => x.onclick = () => { p.lanes[+x.dataset.lane].vals.fill(-1); ch(); });
  // draw lane values with a finger: up is more; a tap at the very bottom clears the step
  b.querySelectorAll('.lane').forEach(lane => {
    const l = p.lanes[+lane.dataset.lane]; let down = false;
    const setAt = e => {
      const t = document.elementFromPoint(e.clientX, e.clientY); const cell = t && t.closest('.lv'); if (!cell || !lane.contains(cell)) return;
      if (l.param < 0){ toast('Pick a parameter for the lane first'); down = false; return; }
      const r = cell.getBoundingClientRect(), f = 1 - (e.clientY - r.top) / r.height, k = +cell.dataset.k, mx = laneMax(l.param);
      l.vals[k] = f < 0.04 ? -1 : Math.round(Math.max(0, Math.min(1, f)) * mx);
      cell.classList.toggle('none', l.vals[k] < 0); cell.firstChild.style.height = (l.vals[k] < 0 ? 0 : l.vals[k] / mx * 100) + '%';
    };
    lane.addEventListener('pointerdown', e => { down = true; lane.setPointerCapture(e.pointerId); setAt(e); });
    lane.addEventListener('pointermove', e => { if (down) setAt(e); });
    lane.addEventListener('pointerup', () => { if (down){ down = false; ch(); } });
  });
  $('#arOn').onclick = () => { host.seqCmd('arp', !a.on); drawSeq(); };
  $('#arMode').onchange = e => { a.mode = +e.target.value; ch(); };
  $('#arOct').onchange = e => { a.oct = +e.target.value + 1; ch(); };
  $('#arRate').onchange = e => { a.rate = +e.target.value; ch(); };
  $('#arGate').oninput = e => { a.gate = +e.target.value; post({t:'seq', s:q}); dirty(); };
  $('#arSwing').oninput = e => { a.swing = +e.target.value; post({t:'seq', s:q}); dirty(); };
  $('#arLatch').onchange = e => { a.latch = e.target.checked; ch(); };
}
function laneMax(id){ if (id < 0) return 99; if (id >= 200){ const d = DX.FUNC_DEF[id - 200]; return d ? d[2] : 99; } return DX.VPARAMS[id].max; }
let lastHead = -2;
function seqPlayhead(){
  const k = stat.playing ? stat.step : -1; if (k === lastHead) return; lastHead = k;
  $$('#sqGrid .st').forEach(x => x.classList.toggle('play', +x.dataset.k === k));
}

/* ----- MIDI settings ----- */
function drawMidi(){
  const b = $('#midiBody'), f = S.func;
  const ins = midiAccess ? [...midiAccess.inputs.values()] : [], outs = midiAccess ? [...midiAccess.outputs.values()] : [];
  const chOpt = (cur, omni) => (omni ? '<option value="0">Omni</option>' : '') + Array.from({length:16}, (_, i) => `<option value="${omni ? i + 1 : i}" ${(omni ? i + 1 : i) === cur ? 'selected' : ''}>${i + 1}</option>`).join('');
  b.innerHTML = `
    ${navigator.requestMIDIAccess ? '' : '<p class="hint">This browser has no Web MIDI. Use Chrome or Edge, or the Android app.</p>'}
    <div class="row"><label>MIDI in <select id="miIn"><option value="all">All inputs</option><option value="none" ${S.midiIn === 'none' ? 'selected' : ''}>None</option>${ins.map(i => `<option value="${i.id}" ${S.midiIn === i.id ? 'selected' : ''}>${i.name}</option>`).join('')}</select></label>
      <label>Receive channel <select id="miRx">${chOpt(f.rxCh, true)}</select></label></div>
    <div class="row"><label>MIDI out <select id="miOut"><option value="">None</option><option value="all" ${S.midiOut === 'all' ? 'selected' : ''}>All outputs</option>${outs.map(o => `<option value="${o.id}" ${S.midiOut === o.id ? 'selected' : ''}>${o.name}</option>`).join('')}</select></label>
      <label>Transmit channel <select id="miTx">${chOpt(f.txCh, false)}</select></label>
      <label><input type="checkbox" id="miSys" ${f.sysInfo ? 'checked' : ''}> Sys info (sysex in / out, parameter changes)</label>
      <label><input type="checkbox" id="miClk" ${S.clockOut ? 'checked' : ''}> Send clock</label></div>
    <div class="row"><button class="btn" data-r="sendVoice">Send voice</button><button class="btn" data-r="sendBank">Send internal bank</button>
      <button class="btn" data-r="requestVoice">Request voice</button><button class="btn" data-r="requestBank">Request bank</button></div>
    <h4 style="margin:12px 0 4px;color:var(--tan);font:700 12px var(--sans)">MIDI FILES</h4>
    <div class="row"><button class="btn pri" data-r="midiPlay">Play a MIDI file</button><button class="btn" data-r="midiStop">Stop</button>
      <label><input type="checkbox" id="miLoop" ${S.playerLoop ? 'checked' : ''}> Loop</label>
      <button class="btn ${stat.rec2 ? 'warn' : ''}" id="miRec">${stat.rec2 ? 'Stop & save recording' : 'Record performance'}</button></div>
    <p class="hint">Received: notes, velocity, pitch bend, mod wheel (1), breath (2), foot (4), portamento time (5), volume (7), expression (11), sustain (64), portamento on/off (65),
      channel and key aftertouch, program change (1–32), all notes off / reset controllers / mono / poly, voice and 32-voice bank dumps, parameter changes and dump requests, clock, start, stop, continue.
      Sent: the keys you play, the wheels, arpeggiator and sequencer notes, clock, parameter changes while you edit (sys info on), voice and bank dumps.
      MIDI files play through the synth (sysex in them loads voices); the recorder saves what you play as a .mid with the voice at the start.</p>`;
  $('#miIn').onchange = e => { S.midiIn = e.target.value; hookMidi(); dirty(); };
  $('#miOut').onchange = e => { S.midiOut = e.target.value; dirty(); };
  $('#miRx').onchange = e => host.setFunc('rxCh', +e.target.value);
  $('#miTx').onchange = e => host.setFunc('txCh', +e.target.value);
  $('#miSys').onchange = e => host.setFunc('sysInfo', e.target.checked ? 1 : 0);
  $('#miClk').onchange = e => { S.clockOut = e.target.checked; dirty(); };
  $('#miLoop').onchange = e => request('midiLoop', e.target.checked);
  $('#miRec').onclick = () => { request(stat.rec2 ? 'recStop' : 'recStart'); setTimeout(drawMidi, 150); };
  b.querySelectorAll('[data-r]').forEach(x => x.onclick = () => request(x.dataset.r));
}

/* ----- effects ----- */
const FX_UI = [['chorus', 'Chorus depth'], ['chorusRate', 'Chorus rate'], ['delayMix', 'Delay mix'], ['delayTime', 'Delay time'], ['delayFb', 'Delay feedback'], ['revMix', 'Reverb mix'], ['revSize', 'Reverb size']];
const FX_PRESETS = {'Dry':[0, 30, 0, 60, 35, 0, 60], 'Chorus':[55, 30, 0, 60, 35, 0, 60], 'Studio':[40, 25, 18, 62, 30, 22, 55], 'Big hall':[30, 20, 0, 60, 35, 45, 90], 'Echo':[0, 30, 40, 66, 55, 15, 50], 'Dream':[70, 18, 30, 74, 60, 50, 95]};
function drawFx(){
  const b = $('#fxBody'), f = S.func;
  b.innerHTML = `<p class="hint">After the converter and output filter (the hardware's own sound stays as it is): a stereo chorus, a ping-pong delay and a reverb. Also on the panel: FUNCTION 30, pushed again.</p>
    <div class="row">${Object.keys(FX_PRESETS).map(k => `<button class="btn" data-fxp="${k}">${k}</button>`).join('')}</div>
    <div class="row"><label><input type="checkbox" id="fxDac" ${f.dac ? '' : 'checked'}> Mark I 12-bit converter (off = clean 16-bit)</label></div>
    <div class="row"><label style="width:130px">DAC noise &amp; grit</label><input type="range" min="0" max="99" value="${f.hiss}" data-fx="hiss" style="flex:1"><span style="width:28px;text-align:right">${f.hiss}</span></div>
    <p class="hint">The Mark I converter: each voice converted on its own (12-bit DAC, range divider with 1 % resistors), hiss from the DAC and the output stage. 50 = a typical unit, 0 = perfect parts, 99 = a tired one. The volume slider moves in 8 steps, as on the original (it picks the DAC's reference voltage).</p>
    ${FX_UI.map(([k, n]) => `<div class="row"><label style="width:130px">${n}</label><input type="range" min="0" max="99" value="${f[k]}" data-fx="${k}" style="flex:1"><span style="width:28px;text-align:right">${f[k]}</span></div>`).join('')}`;
  b.querySelectorAll('[data-fx]').forEach(x => x.oninput = () => { host.setFunc(x.dataset.fx, +x.value); x.nextElementSibling.textContent = x.value; });
  b.querySelectorAll('[data-fxp]').forEach(x => x.onclick = () => { FX_PRESETS[x.dataset.fxp].forEach((v, i) => host.setFunc(FX_UI[i][0], v)); drawFx(); });
  $('#fxDac').onchange = e => host.setFunc('dac', e.target.checked ? 0 : 1);
}

/* ----- help ----- */
function drawHelp(){
  $('#helpBody').innerHTML = `
  <h4>Playing</h4><p>Push <b>MEMORY SELECT INTERNAL</b> or <b>CARTRIDGE</b>, then 1–32 to pick a voice (YES / NO step through them). Play the keys (higher on a key = softer), the wheels, a MIDI keyboard, or the computer keys A W S E D F T G Y H U J K (Z / X octave).</p>
  <h4>Editing (EDIT)</h4><p>Push <b>EDIT</b>, then a numbered button for the parameter (printed above it). Change it with the <b>DATA ENTRY</b> slider or <b>YES</b> (+1) / <b>NO</b> (−1).
  Buttons that hold several values step through them when pushed again: 20 / 21 EG rate / level 1–4, 28 / 29 pitch EG, 23 / 24 left / right curve and depth, 15 pitch then amplitude mod sensitivity, 16 oscillator mode then key sync.
  <b>OPERATOR SELECT</b> picks the operator (shown on the display). Buttons 1–6 switch operators on and off. 31 is the name: the buttons type letters (1–26 A–Z, 27 space, 28 −, 29 ., 30 digits, 32 case), YES / NO move the cursor, the slider picks any character; push 31 again to finish.
  32 copies the selected operator to another (slider picks which, YES copies). Push <b>EDIT</b> again to <b>COMPARE</b> with the stored voice. Tapping an algorithm in the chart picks it.</p>
  <h4>Storing</h4><p>Turn <b>MEMORY PROTECT</b> off (push it, then NO), push <b>STORE</b>, then the number to write the edited voice to. The display's * means the voice has been edited and not stored. FUNCTION 9 (edit recall) brings back edits you left for another voice.</p>
  <h4>Function (FUNCTION)</h4><ul>
  <li>1 master tune · 2 poly / mono · 3 / 4 pitch bend range and step · 5–7 portamento mode, glissando and time · 8 MIDI: receive channel, sys info, transmit channel, send voice, send bank</li>
  <li>9 edit recall · 10 voice init (pushed again: random voice) · 11 load a .syx into the cartridge (or a .mid to play) · 12 save the selected bank · 13 save the edit voice · 14 / 15 copy cartridge to internal and back · 16 battery</li>
  <li>17–24 mod wheel, foot, breath and aftertouch: range, then (pushed again) pitch / amplitude / EG bias on or off</li>
  <li>25 arpeggiator: on, mode, octaves, rate, gate, latch, swing · 26 tempo and clock source · 27 sequencer: pattern, play / stop, record, length, step, swing, key transpose</li>
  <li>28 step edit: buttons 1–16 pick a step (again: on / off), 17–20 the page, 21 clears the step, 22 the pattern; play notes to write them; the slider sets the gate (up to TIE); YES / NO move</li>
  <li>29 motion lanes 1–4: which parameter each one moves, then smooth on / off · 30 converter (12-bit Mark I or 16-bit clean), then the effects: chorus depth and rate, delay mix, time and feedback, reverb mix and size · 31 MIDI files: play, import into the pattern, export the pattern, record, loop · 32 sysex: send bank, request bank or voice</li></ul>
  <h4>Random and reset</h4><p><b>Random</b> (the die) makes a new playable voice in the edit buffer: a random algorithm with musical ratios and envelopes. Keep it with STORE. <b>Reset</b> goes back to the voice as stored in memory.</p><h4>Effects</h4><p><b>FX</b> (or FUNCTION 30 pushed again) sets a stereo chorus, a ping-pong delay and a reverb after the converter, with a few starting points.</p>
  <h4>Files</h4><p>Banks are standard 32-voice sysex dumps (4104 bytes) and single voices are 163-byte dumps, so banks from the original instrument, librarians and other emulators load here and what you save loads there. MIDI files: type 0 and 1.</p>
  <h4>The sound</h4><p>Six operators per voice, 16 voices, computed at the original 49.096 kHz with log-sine and exponential tables, the original envelope, LFO and scaling behaviour, then the Mark I converter: 12-bit samples whose range switches in 6 dB steps, held between samples and smoothed by the output filter. FUNCTION 30 swaps in a clean 16-bit converter for comparison.</p>`;
}

/* ---------------- the Android app ---------------- */
// a .syx or .mid opened from a file manager
window.__sevenOpen = (data, name) => { try { const bytes = unb64(data); audio(); $('#start').classList.add('gone'); openFile('cart', bytes, name); } catch (e) { toast(e.message, 5000); } };
window.__sevenPause = () => save();

/* ---------------- start ---------------- */
layout();
sliderShow('vol'); sliderShow('data'); wheelShow('pitch', 0); wheelShow('mod', 0);
midiInit();
requestAnimationFrame(frame);
window.addEventListener('beforeunload', save);
if ('serviceWorker' in navigator && location.protocol === 'https:' && !window.AndroidSeven) navigator.serviceWorker.register('sw.js').catch(() => {});
