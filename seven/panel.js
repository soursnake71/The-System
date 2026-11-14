/* Omega Seven front panel: the state machine behind the buttons, the data entry slider and the 2 x 16 display, so the
 * synth is programmed the way the 1983 hardware is: MEMORY SELECT (play), EDIT / COMPARE, FUNCTION, STORE, MEMORY
 * PROTECT, OPERATOR SELECT, the 32 numbered buttons, YES / NO (+1 / -1) and DATA ENTRY. No drawing here: app.js (web)
 * and SevenEditor.cpp (plugin, through Panel.h, a line-for-line port) draw it and pass presses in.
 *
 * host: getVoice() -> edit buffer (Uint8Array 156), setParam(id, v), setVoice(v), func -> object, setFunc(k, v),
 *       bank('int' | 'cart') -> array of 32 voices or null, store(src, i, v), setBank(src, voices), seq -> sequencer
 *       state (DXS.defaultSeqState shape), seqCmd(cmd, a, b) -> any, request(kind, arg) (files, MIDI dumps), playing(),
 *       recording(), playerState() -> {playing, rec, name} */
(function(root){
'use strict';
const DX = root.DX || require('./engine.js');
const DXS = root.DXS || require('./seq.js');

function pad(s, n){ s = String(s); return s.length >= n ? s.slice(0, n) : s + ' '.repeat(n - s.length); }
function rpad(s, n){ s = String(s); return s.length >= n ? s.slice(-n) : ' '.repeat(n - s.length) + s; }
function line(l, r){ l = String(l); r = String(r == null ? '' : r); const room = 16 - r.length; return pad(l, Math.max(0, room)) + r; }
const ONOFF = v => v ? 'ON' : 'OFF';
const ASSIGN = ['PITCH', 'AMP', 'EG BIAS'];

// what each numbered button edits in EDIT mode: [label, sub-pages]
const EDIT_BUTTONS = {
  7:'ALGORITHM', 8:'FEEDBACK', 9:'LFO WAVE', 10:'LFO SPEED', 11:'LFO DELAY', 12:'LFO PM DEPTH', 13:'LFO AM DEPTH', 14:'LFO KEY SYNC',
  15:'MOD SENSITIVITY', 16:'OSC MODE/SYNC', 17:'FREQ COARSE', 18:'FREQ FINE', 19:'OSC DETUNE', 20:'EG RATE', 21:'EG LEVEL', 22:'BREAK POINT',
  23:'CURVE', 24:'DEPTH', 25:'RATE SCALING', 26:'OUTPUT LEVEL', 27:'KEY VELOCITY', 28:'P EG RATE', 29:'P EG LEVEL', 30:'KEY TRANSPOSE',
  31:'VOICE NAME', 32:'OP COPY'
};
const EDIT_SUBS = {15:2, 16:2, 20:4, 21:4, 23:2, 24:2, 28:4, 29:4};
const FUNC_BUTTONS = {
  1:'MASTER TUNE', 2:'POLY/MONO', 3:'P BEND RANGE', 4:'P BEND STEP', 5:'PORTA MODE', 6:'PORTA GLISS', 7:'PORTA TIME', 8:'MIDI',
  9:'EDIT RECALL', 10:'VOICE INIT', 11:'LOAD FILE', 12:'SAVE BANK', 13:'SAVE VOICE', 14:'CART > INT', 15:'INT > CART', 16:'BATTERY',
  17:'MW RANGE', 18:'MW ASSIGN', 19:'FC RANGE', 20:'FC ASSIGN', 21:'BC RANGE', 22:'BC ASSIGN', 23:'AT RANGE', 24:'AT ASSIGN',
  25:'ARPEGGIATOR', 26:'TEMPO/CLOCK', 27:'SEQUENCER', 28:'STEP EDIT', 29:'MOTION', 30:'DAC/EFFECTS', 31:'MIDI FILE', 32:'SYSEX'
};
const FUNC_SUBS = {8:5, 10:2, 18:3, 20:3, 22:3, 24:3, 25:7, 26:2, 27:7, 29:8, 30:8, 31:5, 32:3};
// characters typed by the 32 buttons while naming a voice
const NAME_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ -./0';

// the motion-lane parameter list: voice parameters 0-155 then functions (200 + index)
function motionName(id){
  if (id < 0) return 'OFF';
  if (id >= 200){ const d = DX.FUNC_DEF[id - 200]; return d ? d[1] : '?'; }
  const p = DX.VPARAMS[id];
  return p.op ? 'OP' + p.op + ' ' + p.short : p.short;
}
const MOTION_IDS = (() => { const a = [-1]; for (let i = 0; i <= 144; i++) a.push(i); for (let i = 0; i < 14; i++) a.push(200 + i); for (let i = 20; i < DX.FUNC_DEF.length; i++) a.push(200 + i); return a; })();
const FX_KEYS = ['dac', 'chorus', 'chorusRate', 'delayMix', 'delayTime', 'delayFb', 'revMix', 'revSize'];

class Panel {
  constructor(host){
    this.h = host;
    this.mode = 'play';            // play | edit | func
    this.src = 'int';              // memory select: int | cart
    this.num = 0;                  // voice 0..31 last selected
    this.edited = false;           // edit buffer differs from memory
    this.compare = false; this.compareBuf = null;
    this.protect = {int:true, cart:true};
    this.protectSel = null;        // 'int' | 'cart' while the protect switch is shown
    this.op = 1;
    this.eBtn = 7; this.eSub = 0;  // edit button and its page
    this.fBtn = 1; this.fSub = 0;
    this.storing = false;
    this.naming = false; this.nameCur = 0;
    this.recall = null;            // edit buffer kept for EDIT RECALL
    this.copyTo = 2;
    this.stepSel = 0; this.stepPage = 0; this.stepEdit = false;
    this.msg = null; this.msgUntil = 0;
    this.slider = -1;              // last data entry position (0..1)
    this.lastSent = null;
  }
  get v(){ return this.h.getVoice(); }
  now(){ return Date.now(); }
  flash(a, b, ms){ this.msg = [line(a), line(b || '')]; this.msgUntil = this.now() + (ms || 1400); }

  /* ======================= mode buttons ======================= */
  memSelect(src){
    this.protectSel = null; this.storing = false; this.naming = false; this.stepEdit = false;
    if (this.compare) this.endCompare();
    if (src === 'cart' && !this.h.bank('cart')){ this.flash('CARTRIDGE', 'NOT READY  (11)'); }
    this.src = src; this.mode = 'play';
  }
  editBtn(){
    this.protectSel = null; this.storing = false;
    if (this.mode === 'edit'){
      // EDIT again: compare with the stored voice
      if (this.compare) this.endCompare();
      else if (this.edited){
        const b = this.h.bank(this.src);
        if (b){ this.compareBuf = Uint8Array.from(this.v); this.compare = true; this.h.setVoice(b[this.num]); }
      }
      return;
    }
    this.mode = 'edit'; this.naming = false; this.stepEdit = false;
  }
  endCompare(){ if (!this.compare) return; this.compare = false; this.h.setVoice(this.compareBuf); this.compareBuf = null; }
  funcBtn(){ this.protectSel = null; this.storing = false; this.naming = false; if (this.compare) this.endCompare(); this.mode = 'func'; }
  storeBtn(){
    if (this.compare) this.endCompare();
    this.storing = !this.storing; this.protectSel = null;
    if (this.storing) this.flash('MEMORY STORE', 'PUSH 1-32 (' + (this.src === 'int' ? 'INT' : 'CRT') + ')', 4000);
  }
  protectBtn(which){ this.storing = false; this.protectSel = this.protectSel === which ? null : which; }
  opSelect(){
    if (this.mode !== 'edit') { this.flash('OPERATOR SELECT', 'IN EDIT MODE'); return; }
    this.op = this.op % 6 + 1;
  }

  /* ======================= the 32 numbered buttons ======================= */
  button(n){
    if (this.storing){ this.doStore(n - 1); return; }
    if (this.protectSel) this.protectSel = null;
    if (this.mode === 'play') return this.selectVoice(n - 1);
    if (this.mode === 'edit'){
      if (this.compare) this.endCompare();
      if (this.naming){
        if (n === 31){ this.naming = false; return; }
        if (n === 32){ const v = this.v, c = v[145 + this.nameCur]; const ch = String.fromCharCode(c); const sw = ch === ch.toLowerCase() ? ch.toUpperCase() : ch.toLowerCase(); this.set(145 + this.nameCur, sw.charCodeAt(0)); return; }
        let ch = NAME_CHARS[n - 1];
        if (n === 30){ const c = this.v[145 + this.nameCur]; ch = c >= 48 && c < 57 ? String.fromCharCode(c + 1) : '0'; this.set(145 + this.nameCur, ch.charCodeAt(0)); return; }
        this.set(145 + this.nameCur, ch.charCodeAt(0)); this.nameCur = Math.min(9, this.nameCur + 1); return;
      }
      if (n <= 6){ const bit = 1 << (6 - n); this.set(155, this.v[155] ^ bit); return; }
      if (n === 31){ this.naming = true; this.nameCur = 0; this.eBtn = 31; return; }
      if (this.eBtn === n && EDIT_SUBS[n]) this.eSub = (this.eSub + 1) % EDIT_SUBS[n];
      else { this.eBtn = n; this.eSub = 0; }
      this.slider = -1;
      return;
    }
    // function mode
    if (this.stepEdit && n <= 22) return this.stepButton(n);
    if (this.fBtn === n && FUNC_SUBS[n]) this.fSub = (this.fSub + 1) % FUNC_SUBS[n];
    else { this.fBtn = n; this.fSub = 0; }
    if (n === 28){ this.stepEdit = true; this.stepSel = 0; this.stepPage = 0; }
    else this.stepEdit = false;
    if (n === 16) this.flash('BATTERY VOLT', '= 3.0V', 1500);
    this.slider = -1;
  }
  selectVoice(i){
    const b = this.h.bank(this.src);
    if (!b){ this.flash('CARTRIDGE', 'NOT READY  (11)'); return; }
    if (this.edited) this.recall = {v:Uint8Array.from(this.v), src:this.src, num:this.num};
    this.num = i; this.edited = false;
    this.h.setVoice(b[i]);
  }
  doStore(i){
    this.storing = false;
    if (this.protect[this.src]){ this.flash('MEMORY PROTECTED', this.src === 'int' ? 'INTERNAL' : 'CARTRIDGE'); return; }
    if (this.src === 'cart' && !this.h.bank('cart')){ this.flash('CARTRIDGE', 'NOT READY  (11)'); return; }
    this.h.store(this.src, i, Uint8Array.from(this.v));
    this.num = i; this.edited = false;
    this.flash('STORED TO', (this.src === 'int' ? 'INT ' : 'CRT ') + (i + 1));
  }

  /* ======================= YES / NO / DATA ENTRY ======================= */
  yes(){ this.inc(1); }
  no(){ this.inc(-1); }
  inc(d){
    if (this.protectSel){ this.protect[this.protectSel] = d > 0; return; }
    if (this.mode === 'edit'){
      if (this.compare) return;
      if (this.naming){ this.nameCur = Math.max(0, Math.min(9, this.nameCur + d)); return; }
      const t = this.editTarget(); if (!t) return;
      if (t.action){ if (d > 0) t.action(); return; }
      this.set(t.id, Math.max(t.min || 0, Math.min(t.max, this.v[t.id] + d)));
      return;
    }
    if (this.mode === 'func'){
      if (this.stepEdit) return this.stepInc(d);
      const t = this.funcTarget(); if (!t) return;
      if (t.action){ (d > 0 ? t.action : (t.noAction || (() => {})))(); return; }
      t.set(Math.max(t.min || 0, Math.min(t.max, t.get() + d)));
      return;
    }
    // play: YES / NO step through the voices
    this.selectVoice((this.num + d + 32) % 32);
  }
  // the slider, 0..1: jumps the value to its position
  data(x){
    x = Math.max(0, Math.min(1, x)); this.slider = x;
    if (this.protectSel) return;
    if (this.mode === 'edit'){
      if (this.compare) return;
      if (this.naming){ this.set(145 + this.nameCur, 32 + Math.round(x * 94)); return; }
      const t = this.editTarget(); if (!t || t.action) { if (t && t.slide) t.slide(x); return; }
      const lo = t.min || 0; this.set(t.id, lo + Math.round(x * (t.max - lo)));
      return;
    }
    if (this.mode === 'func'){
      if (this.stepEdit){ const st = this.curStep(); if (st){ st.gate = Math.max(5, Math.round(5 + x * (DXS.TIE - 5))); this.h.seqCmd('changed'); } return; }
      const t = this.funcTarget(); if (!t) return;
      if (t.slide){ t.slide(x); return; }
      if (t.action) return;
      const lo = t.min || 0; t.set(lo + Math.round(x * (t.max - lo)));
    }
  }
  set(id, val){ this.h.setParam(id, val); this.edited = true; }

  /* ======================= EDIT targets ======================= */
  editTarget(){
    const n = this.eBtn, ob = DX.opBase(this.op), s = this.eSub;
    switch (n){
      case 7: return {id:134, max:31}; case 8: return {id:135, max:7}; case 9: return {id:142, max:5}; case 10: return {id:137, max:99};
      case 11: return {id:138, max:99}; case 12: return {id:139, max:99}; case 13: return {id:140, max:99}; case 14: return {id:141, max:1};
      case 15: return s ? {id:ob + 14, max:3} : {id:143, max:7};
      case 16: return s ? {id:136, max:1} : {id:ob + 17, max:1};
      case 17: return {id:ob + 18, max:31}; case 18: return {id:ob + 19, max:99}; case 19: return {id:ob + 20, max:14};
      case 20: return {id:ob + s, max:99}; case 21: return {id:ob + 4 + s, max:99}; case 22: return {id:ob + 8, max:99};
      case 23: return {id:ob + 11 + s, max:3}; case 24: return {id:ob + 9 + s, max:99}; case 25: return {id:ob + 13, max:7};
      case 26: return {id:ob + 16, max:99}; case 27: return {id:ob + 15, max:7}; case 28: return {id:126 + s, max:99}; case 29: return {id:130 + s, max:99};
      case 30: return {id:144, max:48};
      case 32: return {action:() => this.copyOp(), slide:x => { this.copyTo = 1 + Math.round(x * 5); }};
    }
    return null;
  }
  copyOp(){
    if (this.copyTo === this.op){ this.flash('OP COPY', 'PICK ANOTHER OP'); return; }
    const a = DX.opBase(this.op), b = DX.opBase(this.copyTo), v = this.v;
    for (let i = 0; i < 21; i++) this.set(b + i, v[a + i]);
    this.flash('OP' + this.op + ' COPIED', 'TO OP' + this.copyTo);
  }
  editLines(){
    const v = this.v, op = this.op, ob = DX.opBase(op), s = this.eSub;
    let bits = ''; for (let o = 1; o <= 6; o++) bits += (v[155] >> (6 - o)) & 1 ? '1' : '0';
    const l1 = 'ALG' + rpad(v[134] + 1, 2) + ' ' + bits + ' OP' + op;
    if (this.compare) return ['COMPARE   I' + rpad(this.num + 1, 2) + '  ', DX.voiceName(v) + '      '];
    if (this.naming){
      const nm = DX.voiceName(v);
      return ['VOICE NAME   ' + rpad(this.nameCur + 1, 2) + ' ', nm.slice(0, this.nameCur) + '[' + nm[this.nameCur] + ']' + nm.slice(this.nameCur + 1)];
    }
    const n = this.eBtn;
    let l2;
    switch (n){
      case 7: l2 = line('ALGORITHM', '=' + rpad(v[134] + 1, 3)); break;
      case 8: l2 = line('FEEDBACK', '=' + rpad(v[135], 3)); break;
      case 9: l2 = 'LFO WAVE=' + DX.WAVES[v[142]]; break;
      case 10: l2 = line('LFO SPEED', '=' + rpad(v[137], 3)); break;
      case 11: l2 = line('LFO DELAY', '=' + rpad(v[138], 3)); break;
      case 12: l2 = line('LFO PM DEPTH', '=' + rpad(v[139], 3)); break;
      case 13: l2 = line('LFO AM DEPTH', '=' + rpad(v[140], 3)); break;
      case 14: l2 = line('LFO KEY SYNC', '=' + ONOFF(v[141])); break;
      case 15: l2 = s ? line('A MOD SENS', '=' + rpad(v[ob + 14], 3)) : line('P MOD SENS', '=' + rpad(v[143], 3)); break;
      case 16: l2 = s ? line('OSC KEY SYNC', '=' + ONOFF(v[136])) : 'OSC MODE=' + (v[ob + 17] ? 'FIXED' : 'RATIO'); break;
      case 17: l2 = line(v[ob + 17] ? 'F COARSE' : 'F COARSE', '=' + rpad(DX.freqText(v, op), 7)); break;
      case 18: l2 = line('F FINE', '=' + rpad(DX.freqText(v, op), 7)); break;
      case 19: { const d = v[ob + 20] - 7; l2 = line('OSC DETUNE', '=' + rpad((d > 0 ? '+' : '') + d, 3)); break; }
      case 20: l2 = line('EG RATE  ' + (s + 1), '=' + rpad(v[ob + s], 3)); break;
      case 21: l2 = line('EG LEVEL ' + (s + 1), '=' + rpad(v[ob + 4 + s], 3)); break;
      case 22: l2 = line('BREAK POINT', '=' + rpad(DX.bpName(v[ob + 8]), 4)); break;
      case 23: l2 = line((s ? 'R' : 'L') + ' CURVE', '=' + DX.CURVES[v[ob + 11 + s]]); break;
      case 24: l2 = line((s ? 'R' : 'L') + ' DEPTH', '=' + rpad(v[ob + 9 + s], 3)); break;
      case 25: l2 = line('RATE SCALING', '=' + rpad(v[ob + 13], 3)); break;
      case 26: l2 = line('OUTPUT LEVEL', '=' + rpad(v[ob + 16], 3)); break;
      case 27: l2 = line('KEY VELOCITY', '=' + rpad(v[ob + 15], 3)); break;
      case 28: l2 = line('P EG RATE  ' + (s + 1), '=' + rpad(v[126 + s], 3)); break;
      case 29: l2 = line('P EG LEVEL ' + (s + 1), '=' + rpad(v[130 + s], 3)); break;
      case 30: l2 = line('MIDDLE C', '=' + rpad(DX.noteName(60 + v[144] - 24 + 0), 4)); break;
      case 32: l2 = line('COPY TO OP' + this.copyTo, '?YES'); break;
      default: l2 = line('OP' + n + ' ON/OFF', '');
    }
    return [l1, l2];
  }

  /* ======================= FUNCTION targets ======================= */
  fn(k){ return {get:() => this.h.func[k], set:v => this.h.setFunc(k, v), max:DX.FUNC_DEF.find(d => d[0] === k)[2]}; }
  seqS(){ return this.h.seq; }
  funcTarget(){
    const n = this.fBtn, s = this.fSub, q = this.seqS(), a = q.arp, ch = () => this.h.seqCmd('changed');
    const sq = (obj, k, max, min) => ({get:() => obj[k], set:v => { obj[k] = v; ch(); }, max, min:min || 0});
    switch (n){
      case 1: return this.fn('tune'); case 2: return this.fn('mono'); case 3: return this.fn('pbRange'); case 4: return this.fn('pbStep');
      case 5: return this.fn('portaMode'); case 6: return this.fn('gliss'); case 7: return this.fn('portaTime');
      case 8: return [this.fn('rxCh'), this.fn('sysInfo'), this.fn('txCh'), {action:() => this.h.request('sendVoice')}, {action:() => this.h.request('sendBank')}][s];
      case 9: return {action:() => this.editRecall()};
      case 10: return s ? {action:() => this.random()} : {action:() => { this.h.setVoice(DX.initVoice()); this.edited = true; this.flash('VOICE INIT', 'DONE'); }};
      case 11: return {action:() => this.h.request('loadFile')};
      case 12: return {action:() => this.h.request('saveBank', this.src)};
      case 13: return {action:() => this.h.request('saveVoice')};
      case 14: return {action:() => this.copyBank('cart', 'int')};
      case 15: return {action:() => this.copyBank('int', 'cart')};
      case 16: return null;
      case 17: return this.fn('mwRange'); case 19: return this.fn('fcRange'); case 21: return this.fn('bcRange'); case 23: return this.fn('atRange');
      case 18: case 20: case 22: case 24: {
        const k = ['mwAssign', 'fcAssign', 'bcAssign', 'atAssign'][(n - 18) / 2], bit = 1 << s;
        return {get:() => (this.h.func[k] & bit) ? 1 : 0, set:v => this.h.setFunc(k, v ? this.h.func[k] | bit : this.h.func[k] & ~bit), max:1};
      }
      case 25: return [{get:() => a.on ? 1 : 0, set:v => this.h.seqCmd('arp', !!v), max:1}, sq(a, 'mode', 7), sq(a, 'oct', 4, 1), sq(a, 'rate', 5), sq(a, 'gate', 100, 5),
        {get:() => a.latch ? 1 : 0, set:v => { a.latch = !!v; ch(); }, max:1}, sq(a, 'swing', 75, 50)][s];
      case 26: return [sq(q, 'tempo', 300, 30), {get:() => q.ext ? 1 : 0, set:v => { q.ext = !!v; ch(); }, max:1}][s];
      case 27: {
        const p = q.pats[q.pat];
        return [{get:() => q.pat, set:v => this.h.seqCmd('pattern', v), max:15},
          {action:() => this.h.seqCmd('play'), noAction:() => this.h.seqCmd('stop')},
          {action:() => this.h.seqCmd('rec', true), noAction:() => this.h.seqCmd('rec', false)},
          sq(p, 'len', 64, 1), sq(p, 'rate', 5), sq(p, 'swing', 75, 50),
          {get:() => q.keyTrans ? 1 : 0, set:v => { q.keyTrans = !!v; ch(); }, max:1}][s];
      }
      case 29: {
        const l = q.pats[q.pat].lanes[s & 3];
        if (s >= 4) return {get:() => l.smooth ? 1 : 0, set:v => { l.smooth = !!v; ch(); }, max:1};
        return {get:() => Math.max(0, MOTION_IDS.indexOf(l.param)), set:v => { const id = MOTION_IDS[v]; if (id !== l.param){ l.param = id; l.vals.fill(-1); } ch(); }, max:MOTION_IDS.length - 1};
      }
      case 30: return this.fn(FX_KEYS[s]);
      case 31: return [{action:() => this.h.request('midiPlay'), noAction:() => this.h.request('midiStop')},
        {action:() => this.h.request('midiImport')}, {action:() => this.h.request('midiExport')},
        {action:() => this.h.request('recStart'), noAction:() => this.h.request('recStop')},
        {get:() => this.h.playerLoop() ? 1 : 0, set:v => this.h.request('midiLoop', !!v), max:1}][s];
      case 32: return [{action:() => this.h.request('sendBank')}, {action:() => this.h.request('requestBank')}, {action:() => this.h.request('requestVoice')}][s];
    }
    return null;
  }
  // the dice: a random voice into the edit buffer (STORE keeps it); reset: back to the voice as stored
  random(seed){
    const v = DX.randomVoice(seed != null ? seed : Math.floor(Math.random() * 4294967295));
    this.h.setVoice(v); this.edited = true;
    this.flash('RANDOM VOICE', DX.voiceName(v));
  }
  resetVoice(){
    const b = this.h.bank(this.src);
    if (!b){ this.flash('CARTRIDGE', 'NOT READY  (11)'); return; }
    if (this.compare) this.endCompare();
    this.h.setVoice(b[this.num]); this.edited = false;
    this.flash('VOICE RESET', (this.src === 'int' ? 'INT ' : 'CRT ') + (this.num + 1));
  }
  editRecall(){
    if (!this.recall){ this.flash('EDIT RECALL', 'NOTHING TO RECALL'); return; }
    this.src = this.recall.src; this.num = this.recall.num; this.h.setVoice(this.recall.v); this.edited = true; this.recall = null;
    this.flash('EDIT RECALL', 'DONE'); this.mode = 'edit';
  }
  copyBank(from, to){
    const b = this.h.bank(from);
    if (!b){ this.flash('CARTRIDGE', 'NOT READY  (11)'); return; }
    if (this.protect[to]){ this.flash('MEMORY PROTECTED', to === 'int' ? 'INTERNAL' : 'CARTRIDGE'); return; }
    this.h.setBank(to, b.map(v => Uint8Array.from(v)));
    this.flash(from === 'cart' ? 'CART > INT' : 'INT > CART', 'COMPLETED');
  }
  funcLines(){
    const n = this.fBtn, s = this.fSub, f = this.h.func, q = this.seqS(), a = q.arp;
    if (this.stepEdit) return this.stepLines();
    const title = 'F' + rpad(n, 2) + ' ' + FUNC_BUTTONS[n];
    let l2 = '';
    const asg = k => { const v = f[k]; return line(ASSIGN[s], '=' + ONOFF(v >> s & 1)); };
    switch (n){
      case 1: { const c = Math.round((f.tune - 64) * 75 / 64); l2 = line('MASTER TUNE', (c > 0 ? '+' : '') + c + 'c'); break; }
      case 2: l2 = f.mono ? 'MONO MODE' : 'POLY MODE'; break;
      case 3: l2 = line('P BEND RANGE', '=' + rpad(f.pbRange, 3)); break;
      case 4: l2 = line('P BEND STEP', '=' + rpad(f.pbStep, 3)); break;
      case 5: l2 = f.mono ? (f.portaMode ? 'FULL TIME PORTA' : 'FINGERED PORTA') : (f.portaMode ? 'SUS-KEY P FOLLOW' : 'SUS-KEY P RETAIN'); break;
      case 6: l2 = line('GLISSANDO', '=' + ONOFF(f.gliss)); break;
      case 7: l2 = line('PORTA TIME', '=' + rpad(f.portaTime, 3)); break;
      case 8: l2 = [line('MIDI CH', '=' + (f.rxCh ? rpad(f.rxCh, 3) : 'OMNI')), 'SYS INFO ' + (f.sysInfo ? 'AVAIL' : 'UNAVAIL'), line('MIDI TX CH', '=' + rpad(f.txCh + 1, 3)),
        'SEND VOICE?  YES', 'SEND BANK ?  YES'][s]; break;
      case 9: l2 = 'EDIT RECALL? YES'; break;
      case 10: l2 = s ? 'RANDOM VOICE?YES' : 'VOICE INIT ? YES'; break;
      case 11: l2 = 'LOAD .SYX ?  YES'; break;
      case 12: l2 = 'SAVE ' + (this.src === 'int' ? 'INT' : 'CRT') + ' BANK?YES'; break;
      case 13: l2 = 'SAVE VOICE ? YES'; break;
      case 14: l2 = 'CART>INT ?   YES'; break;
      case 15: l2 = 'INT>CART ?   YES'; break;
      case 16: l2 = 'BATTERY  = 3.0V'; break;
      case 17: l2 = line('MW RANGE', '=' + rpad(f.mwRange, 3)); break; case 18: l2 = asg('mwAssign'); break;
      case 19: l2 = line('FC RANGE', '=' + rpad(f.fcRange, 3)); break; case 20: l2 = asg('fcAssign'); break;
      case 21: l2 = line('BC RANGE', '=' + rpad(f.bcRange, 3)); break; case 22: l2 = asg('bcAssign'); break;
      case 23: l2 = line('AT RANGE', '=' + rpad(f.atRange, 3)); break; case 24: l2 = asg('atAssign'); break;
      case 25: l2 = [line('ARP', '=' + ONOFF(a.on)), 'MODE=' + DXS.ARP_MODES[a.mode], line('OCTAVES', '=' + rpad(a.oct, 3)), line('RATE', '=' + DXS.RATES[a.rate][1]),
        line('GATE', '=' + rpad(a.gate, 3) + '%'), line('LATCH', '=' + ONOFF(a.latch)), line('SWING', '=' + rpad(a.swing, 3) + '%')][s]; break;
      case 26: l2 = [line('TEMPO', '=' + rpad(q.tempo, 3) + 'BPM'), 'CLOCK=' + (q.ext ? 'MIDI/HOST' : 'INTERNAL')][s]; break;
      case 27: {
        const p = q.pats[q.pat];
        l2 = [line('PATTERN', '=' + rpad(q.pat + 1, 3)), this.h.playing() ? 'PLAYING   NO=STP' : 'STOPPED  YES=PLY', 'REC ' + ONOFF(this.h.recording()) + ' YES/NO',
          line('LENGTH', '=' + rpad(p.len, 3)), line('STEP', '=' + DXS.RATES[p.rate][1]), line('SWING', '=' + rpad(p.swing, 3) + '%'), line('KEY TRANSP', '=' + ONOFF(q.keyTrans))][s];
        break;
      }
      case 29: { const l = q.pats[q.pat].lanes[s & 3]; l2 = s < 4 ? 'L' + (s + 1) + '=' + motionName(l.param) : line('L' + ((s & 3) + 1) + ' SMOOTH', '=' + ONOFF(l.smooth)); break; }
      case 30: l2 = s === 0 ? (f.dac ? 'DAC=16BIT CLEAN' : 'DAC=12BIT MARK I') : line(['', 'CHORUS DEPTH', 'CHORUS RATE', 'DELAY MIX', 'DELAY TIME', 'DELAY FDBACK', 'REVERB MIX', 'REVERB SIZE'][s], '=' + rpad(f[FX_KEYS[s]], 3)); break;
      case 31: { const ps = this.h.playerState(); l2 = ['PLAY ' + (ps.playing ? 'ON  NO=STOP' : 'OFF YES=FILE'), 'IMPORT>PAT ? YES', 'EXPORT PAT ? YES', 'RECORD ' + (ps.rec ? 'ON  NO=SAVE' : 'OFF YES=GO'), line('LOOP', '=' + ONOFF(this.h.playerLoop()))][s]; break; }
      case 32: l2 = ['SEND BANK  ? YES', 'REQUEST BANK?YES', 'REQUEST VOICE?YS'][s]; break;
    }
    return [title, l2];
  }

  /* ======================= step edit (function 28) ======================= */
  curStep(){ const q = this.seqS(), p = q.pats[q.pat]; return p.steps[this.stepSel]; }
  stepButton(n){
    const q = this.seqS(), p = q.pats[q.pat];
    if (n <= 16){
      const i = this.stepPage * 16 + n - 1;
      if (i >= p.len){ this.flash('STEP ' + (i + 1), 'PAST LENGTH ' + p.len); return; }
      if (this.stepSel === i) p.steps[i].on = !p.steps[i].on;
      this.stepSel = i; this.h.seqCmd('cursor', i); this.h.seqCmd('changed');
      return;
    }
    if (n <= 20){ this.stepPage = n - 17; return; }
    if (n === 21){ const st = p.steps[this.stepSel]; st.notes = []; st.gate = 75; this.h.seqCmd('changed'); this.flash('STEP ' + (this.stepSel + 1), 'CLEARED'); return; }
    if (n === 22){ q.pats[q.pat] = DXS.emptyPattern(); this.h.seqCmd('changed'); this.flash('PATTERN ' + (q.pat + 1), 'CLEARED'); return; }
  }
  stepInc(d){
    const q = this.seqS(), p = q.pats[q.pat];
    this.stepSel = (this.stepSel + d + p.len) % p.len; this.stepPage = Math.floor(this.stepSel / 16);
    this.h.seqCmd('cursor', this.stepSel);
  }
  stepLines(){
    const q = this.seqS(), p = q.pats[q.pat], st = p.steps[this.stepSel];
    const notes = st.notes.map(x => DX.noteName(x[0])).join(' ') || (st.on ? 'REST' : '');
    return ['P' + rpad(q.pat + 1, 2) + ' STEP ' + rpad(this.stepSel + 1, 2) + (st.on ? ' ' : ' OFF').slice(0, 4) + rpad(st.gate >= DXS.TIE ? 'TIE' : st.gate + '%', 4).slice(-4),
      pad(st.on ? notes : 'STEP OFF', 16)];
  }
  // notes played while step editing are written into the selected step (a chord if held together)
  stepNote(n, v, down){
    if (!(this.mode === 'func' && this.stepEdit)) return false;
    const st = this.curStep();
    if (down){
      if (!this._held) { st.notes = []; this._held = 0; }
      if (st.notes.length < DXS.STEP_NOTES && !st.notes.some(x => x[0] === n)) st.notes.push([n, v]);
      st.on = true; this._held = (this._held || 0) + 1;
    } else {
      this._held = Math.max(0, (this._held || 0) - 1);
      if (!this._held){ const q = this.seqS(), p = q.pats[q.pat]; this.stepSel = (this.stepSel + 1) % p.len; this.stepPage = Math.floor(this.stepSel / 16); this.h.seqCmd('cursor', this.stepSel); }
    }
    this.h.seqCmd('changed');
    return true;
  }

  /* ======================= display and lights ======================= */
  lines(){
    if (this.msg && this.now() < this.msgUntil) return this.msg;
    this.msg = null;
    if (this.protectSel) return ['MEMORY PROTECT', line(this.protectSel === 'int' ? 'INTERNAL' : 'CARTRIDGE', ONOFF(this.protect[this.protectSel]))];
    if (this.mode === 'edit') return this.editLines().map(x => pad(x, 16));
    if (this.mode === 'func') return this.funcLines().map(x => pad(x, 16));
    const q = this.seqS(), f = this.h.func;
    const flags = (q.arp.on ? 'A' : ' ') + (this.h.playing() ? (this.h.recording() ? 'R' : '>') : ' ') + (f.mono ? 'M' : 'P');
    const l1 = (this.src === 'int' ? 'I' : 'C') + rpad(this.num + 1, 2) + (this.edited ? '*' : ' ') + rpad(flags, 12);
    return [pad(l1, 16), pad(DX.voiceName(this.v), 16)];
  }
  // lit: mode buttons, the selected voice / parameter / operator / step
  lights(){
    const L = {store:this.storing, protInt:this.protectSel === 'int', protCart:this.protectSel === 'cart', edit:this.mode === 'edit', compare:this.compare,
      memInt:this.mode === 'play' && this.src === 'int', memCart:this.mode === 'play' && this.src === 'cart', func:this.mode === 'func', op:this.op, btn:-1, steps:null};
    if (this.mode === 'play') L.btn = this.num + 1;
    else if (this.mode === 'edit') L.btn = this.naming ? 31 : this.eBtn;
    else if (this.stepEdit){
      const q = this.seqS(), p = q.pats[q.pat]; L.steps = [];
      for (let i = 0; i < 16; i++){ const k = this.stepPage * 16 + i; L.steps.push(k < p.len && p.steps[k].on && p.steps[k].notes.length ? (k === this.stepSel ? 2 : 1) : (k === this.stepSel ? 2 : 0)); }
      L.btn = 17 + this.stepPage;
    } else L.btn = this.fBtn;
    return L;
  }
  // the parameter shown, for the slider's position and for MIDI parameter-change out
  shownParam(){ if (this.mode === 'edit' && !this.naming){ const t = this.editTarget(); return t && t.id != null ? t : null; } return null; }
  save(){ return {src:this.src, num:this.num, protect:this.protect, op:this.op, eBtn:this.eBtn, eSub:this.eSub, fBtn:this.fBtn, edited:this.edited}; }
  restore(o){ if (!o) return; for (const k of ['src', 'num', 'op', 'eBtn', 'eSub', 'fBtn', 'edited']) if (o[k] != null) this[k] = o[k]; if (o.protect) this.protect = Object.assign({int:true, cart:true}, o.protect); }
}

const api = {Panel, EDIT_BUTTONS, FUNC_BUTTONS, NAME_CHARS, MOTION_IDS, motionName};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
root.DXP = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
