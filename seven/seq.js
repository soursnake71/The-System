/* Omega Seven: arpeggiator, function sequencer (16 patterns of up to 64 steps, notes plus motion lanes that move any
 * voice or function parameter step by step, in the manner of the 2016 FM groovebox's v1.10 firmware), the MIDI file
 * player, MIDI file import / export and the performance recorder. Clocked at 96 ticks per quarter note by the
 * internal tempo, MIDI clock, or (in the plugin) the host's transport. Ported line for line to plugins/seven/Seven.h. */
(function(root){
'use strict';

const PPQ = 96;
const RATES = [[96, '1/4'], [48, '1/8'], [32, '1/8T'], [24, '1/16'], [16, '1/16T'], [12, '1/32']];
const ARP_MODES = ['UP', 'DOWN', 'UP/DOWN', 'DOWN/UP', 'RANDOM', 'AS PLAYED', 'CHORD', 'UP+DOWN'];
const MAX_STEPS = 64, LANES = 4, PATTERNS = 16, STEP_NOTES = 8;
const TIE = 101;      // a gate of 101% ties into the next step

function emptyPattern(){
  const steps = [];
  for (let i = 0; i < MAX_STEPS; i++) steps.push({on:true, notes:[], gate:75});
  const lanes = [];
  for (let i = 0; i < LANES; i++) lanes.push({param:-1, smooth:false, vals:new Array(MAX_STEPS).fill(-1)});
  return {len:16, rate:3, swing:50, steps, lanes};
}
function defaultSeqState(){
  const pats = []; for (let i = 0; i < PATTERNS; i++) pats.push(emptyPattern());
  return {tempo:120, ext:false, pat:0, chain:[], keyTrans:true, transBase:60,
    arp:{on:false, mode:0, oct:1, rate:3, gate:50, latch:false, swing:50}, pats};
}
function copyPattern(p){ return JSON.parse(JSON.stringify(p)); }

/* host: {noteOn(n, v), noteOff(n), param(id, v)} ; param ids: 0-155 voice, 200+ function (index in FUNC_DEF) */
class Sequencer {
  constructor(host){
    this.h = host;
    this.s = defaultSeqState();
    this.playing = false; this.rec = false;
    this.tick = 0;                 // position in the pattern
    this.abs = 0;                  // ticks since start (never wraps): note ends are timed on it
    this.step = -1; this.chainIx = 0;
    this.sounding = [];            // [note, end] from the sequencer
    this.arpHeld = []; this.arpLatched = []; this.arpIx = 0; this.arpTick = 0; this.arpSounding = []; this.arpRun = false;
    this.trans = 0; this.rand = 12345;
    this.recHeld = new Map();      // note -> [step, abs] while recording
    this.stepCursor = 0; this.stepHeld = 0;
    this.smoothLast = [-1, -1, -1, -1];
  }
  load(s){
    const d = defaultSeqState(); this.s = Object.assign(d, s || {});
    this.s.arp = Object.assign(defaultSeqState().arp, (s && s.arp) || {});
    if (!Array.isArray(this.s.pats) || this.s.pats.length !== PATTERNS) this.s.pats = d.pats;
  }
  get pat(){ return this.s.pats[this.s.pat]; }
  stepTicks(p){ return RATES[(p || this.pat).rate][0]; }

  /* ----- transport ----- */
  start(){ this.flush(); this.playing = true; this.tick = 0; this.step = -1; this.chainIx = 0; this.trans = 0; if (this.s.chain.length) this.s.pat = this.s.chain[0]; }
  stop(){ this.playing = false; this.flush(); this.recHeld.clear(); }
  flush(){ for (const x of this.sounding) this.h.noteOff(x[0]); this.sounding = []; }
  flushArp(){ for (const x of this.arpSounding) this.h.noteOff(x[0]); this.arpSounding = []; }
  setArp(on){ this.s.arp.on = on; if (!on){ this.arpRun = false; this.arpHeld = []; this.arpLatched = []; this.flushArp(); } }

  /* ----- keyboard input: returns true when the arpeggiator / transpose took the note (the synth doesn't play it) ----- */
  keyOn(n, v){
    if (this.s.arp.on){
      if (this.s.arp.latch && this.arpHeld.length === 0) this.arpLatched = [];
      this.arpHeld.push([n, v]);
      if (this.s.arp.latch) this.arpLatched.push([n, v]);
      if (!this.arpRun){ this.arpRun = true; this.arpIx = 0; this.arpTick = 0; }
      if (this.rec) this.recordOn(n, v);
      return true;
    }
    if (this.rec){ this.recordOn(n, v); return false; }
    if (this.playing && this.s.keyTrans && this.patHasNotes()){ this.trans = n - this.s.transBase; return true; }
    return false;
  }
  keyOff(n){
    if (this.s.arp.on){
      this.arpHeld = this.arpHeld.filter(x => x[0] !== n);
      if (!this.arpHeld.length && !this.s.arp.latch){ this.arpRun = false; this.flushArp(); }
      if (this.rec) this.recordOff(n);
      return true;
    }
    if (this.rec){ this.recordOff(n); return false; }
    return this.playing && this.s.keyTrans && this.patHasNotes();
  }
  patHasNotes(){ const p = this.pat; for (let i = 0; i < p.len; i++) if (p.steps[i].notes.length) return true; return false; }

  /* ----- recording ----- */
  recordOn(n, v){
    const p = this.pat;
    if (this.playing){
      // real time: to the nearest step, added to what is there (overdub)
      const st = this.stepTicks(p), s = Math.round(this.tick / st) % p.len, step = p.steps[s];
      if (!step.notes.some(x => x[0] === n) && step.notes.length < STEP_NOTES) step.notes.push([n, v]);
      step.on = true;
      this.recHeld.set(n, [s, this.abs]);
    } else {
      // step recording: notes held together make a chord; the cursor moves on when all are released
      const step = p.steps[this.stepCursor];
      if (this.stepHeld === 0) step.notes = [];
      if (step.notes.length < STEP_NOTES) step.notes.push([n, v]);
      step.on = true; step.gate = 75;
      this.stepHeld++;
    }
  }
  recordOff(n){
    const p = this.pat;
    if (this.playing){
      const r = this.recHeld.get(n); if (!r) return;
      this.recHeld.delete(n);
      p.steps[r[0]].gate = Math.max(5, Math.min(TIE, Math.round((this.abs - r[1]) / this.stepTicks(p) * 100)));
    } else {
      this.stepHeld = Math.max(0, this.stepHeld - 1);
      if (this.stepHeld === 0) this.stepCursor = (this.stepCursor + 1) % p.len;
    }
  }
  stepRest(){ const p = this.pat; p.steps[this.stepCursor].notes = []; this.stepCursor = (this.stepCursor + 1) % p.len; }
  stepTie(){ const p = this.pat, prev = p.steps[(this.stepCursor + p.len - 1) % p.len]; prev.gate = TIE; p.steps[this.stepCursor].notes = prev.notes.map(x => x.slice()); this.stepCursor = (this.stepCursor + 1) % p.len; }
  // a parameter moved while recording: written to the step now playing, on its lane (a free lane is taken if needed)
  recordParam(id, v){
    if (!this.rec || !this.playing) return false;
    const p = this.pat;
    let lane = p.lanes.find(l => l.param === id);
    if (!lane){ lane = p.lanes.find(l => l.param < 0); if (!lane) return false; lane.param = id; lane.vals.fill(-1); }
    lane.vals[Math.max(0, this.step) % p.len] = v;
    return true;
  }

  /* ----- one tick (1/96 quarter note) ----- */
  doTick(){
    if (this.sounding.length) this.sounding = this.sounding.filter(x => { if (x[1] <= this.abs){ this.h.noteOff(x[0]); return false; } return true; });
    if (this.arpSounding.length) this.arpSounding = this.arpSounding.filter(x => { if (x[1] <= this.abs){ this.h.noteOff(x[0]); return false; } return true; });
    if (this.playing){
      let p = this.pat, st = this.stepTicks(p);
      if (this.tick >= p.len * st){
        this.tick = 0;
        if (this.s.chain.length > 1){ this.chainIx = (this.chainIx + 1) % this.s.chain.length; this.s.pat = this.s.chain[this.chainIx]; p = this.pat; st = this.stepTicks(p); }
      }
      const si = Math.floor(this.tick / st), pos = this.tick - si * st, sw = Math.round((p.swing - 50) / 50 * st);
      if (pos === (si & 1 ? sw : 0)){ this.step = si; this.playStep(p, si); }
      else if (pos > 0) this.motionSmooth(p, st, pos);
      this.tick++;
    }
    if (this.s.arp.on && this.arpRun) this.arpStep();
    this.abs++;
  }
  playStep(p, s){
    const step = p.steps[s], st = this.stepTicks(p);
    for (let i = 0; i < LANES; i++){ const l = p.lanes[i]; if (l.param >= 0 && l.vals[s] >= 0){ this.h.param(l.param, l.vals[s]); this.smoothLast[i] = l.vals[s]; } }
    if (!step.on || !step.notes.length) return;
    let len = Math.max(1, Math.round(st * step.gate / 100));
    if (step.gate >= TIE){ let k = 1; while (k < p.len && p.steps[(s + k) % p.len].gate >= TIE) k++; len = st * k + 1; }
    const prev = p.steps[(s + p.len - 1) % p.len], tied = prev.on && prev.gate >= TIE;
    for (const nv of step.notes){
      const nn = Math.max(0, Math.min(127, nv[0] + this.trans));
      const was = this.sounding.findIndex(x => x[0] === nn);
      // tied from the step before: the note carries on
      if (was >= 0 && tied && prev.notes.some(x => x[0] === nv[0])){ this.sounding[was][1] = Math.max(this.sounding[was][1], this.abs + len); continue; }
      if (was >= 0){ this.h.noteOff(nn); this.sounding.splice(was, 1); }
      this.h.noteOn(nn, nv[1]);
      this.sounding.push([nn, this.abs + len]);
    }
  }
  motionSmooth(p, st, pos){
    if (this.step < 0) return;
    for (let i = 0; i < LANES; i++){
      const l = p.lanes[i];
      if (!l.smooth || l.param < 0) continue;
      const a = l.vals[this.step], b = l.vals[(this.step + 1) % p.len];
      if (a < 0 || b < 0 || a === b) continue;
      const v = Math.round(a + (b - a) * pos / st);
      if (v !== this.smoothLast[i]){ this.smoothLast[i] = v; this.h.param(l.param, v); }
    }
  }
  arpNotes(){
    const a = this.s.arp;
    const src = a.latch ? this.arpLatched : this.arpHeld;
    const base = a.mode === 5 ? src.slice() : src.slice().sort((x, y) => x[0] - y[0]);
    const up = [];
    for (let o = 0; o < a.oct; o++) for (const nv of base) if (nv[0] + 12 * o < 128) up.push([nv[0] + 12 * o, nv[1]]);
    switch (a.mode){
      case 1: return up.reverse();
      case 2: return up.length > 2 ? up.concat(up.slice(1, -1).reverse()) : up;
      case 3: { const d = up.slice().reverse(); return d.length > 2 ? d.concat(d.slice(1, -1).reverse()) : d; }
      case 7: return up.concat(up.slice().reverse());
      default: return up;
    }
  }
  arpStep(){
    const a = this.s.arp, st = RATES[a.rate][0], si = Math.floor(this.arpTick / st), pos = this.arpTick - si * st, sw = Math.round((a.swing - 50) / 50 * st);
    if (pos === (si & 1 ? sw : 0)){
      const list = this.arpNotes();
      if (list.length){
        const len = Math.max(1, Math.round(st * a.gate / 100));
        let pick;
        if (a.mode === 6) pick = list;
        else if (a.mode === 4){ let r = this.rand; r ^= r << 13; r ^= r >>> 17; r ^= r << 5; this.rand = r >>> 0; pick = [list[this.rand % list.length]]; }
        else pick = [list[this.arpIx % list.length]];
        this.arpIx++;
        for (const nv of pick){
          const was = this.arpSounding.findIndex(x => x[0] === nv[0]);
          if (was >= 0){ this.h.noteOff(nv[0]); this.arpSounding.splice(was, 1); }
          this.h.noteOn(nv[0], nv[1]);
          this.arpSounding.push([nv[0], this.abs + len]);
        }
      }
    }
    this.arpTick++;
  }
}

/* ---------------- standard MIDI files ---------------- */
function readVar(d, p){ let v = 0, b; do { b = d[p.i++]; v = (v << 7) | (b & 127); } while (b & 128 && p.i < d.length); return v; }
// -> {format, ppq, events:[{tick, track, data:[bytes], meta, tempo}]} merged in time order
function parseMidiFile(bytes){
  const d = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const str = (o, n) => String.fromCharCode(...d.subarray(o, o + n));
  if (str(0, 4) !== 'MThd') throw new Error('not a MIDI file');
  const hl = (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7];
  const format = (d[8] << 8) | d[9], ntr = (d[10] << 8) | d[11], div = (d[12] << 8) | d[13];
  if (div & 0x8000) throw new Error('SMPTE-timed MIDI files are not supported');
  let o = 8 + hl;
  const events = [];
  for (let t = 0; t < ntr && o + 8 <= d.length; t++){
    if (str(o, 4) !== 'MTrk'){ const l = (d[o + 4] << 24) | (d[o + 5] << 16) | (d[o + 6] << 8) | d[o + 7]; o += 8 + l; t--; if (o >= d.length) break; continue; }
    const len = (d[o + 4] << 24) | (d[o + 5] << 16) | (d[o + 6] << 8) | d[o + 7];
    const end = Math.min(d.length, o + 8 + len);
    const p = {i:o + 8};
    let tick = 0, run = 0, seq = 0;
    while (p.i < end){
      tick += readVar(d, p);
      let st = d[p.i];
      if (st < 0x80){ st = run; } else { p.i++; if (st < 0xF0) run = st; }
      if (st === 0xFF){
        const ty = d[p.i++], l = readVar(d, p), data = d.subarray(p.i, p.i + l); p.i += l;
        if (ty === 0x51 && l === 3) events.push({tick, track:t, seq:seq++, tempo:(data[0] << 16) | (data[1] << 8) | data[2]});
        if (ty === 0x2F) break;
        continue;
      }
      if (st === 0xF0 || st === 0xF7){
        const l = readVar(d, p), data = Array.from(d.subarray(p.i, p.i + l)); p.i += l;
        events.push({tick, track:t, seq:seq++, data:st === 0xF0 ? [0xF0].concat(data) : data});
        continue;
      }
      const ty = st & 0xF0, n = (ty === 0xC0 || ty === 0xD0) ? 1 : 2;
      const data = [st]; for (let k = 0; k < n; k++) data.push(d[p.i++] & 127);
      events.push({tick, track:t, seq:seq++, data});
    }
    o = end;
  }
  events.sort((a, b) => a.tick - b.tick || a.track - b.track || a.seq - b.seq);
  return {format, ppq:div || 96, events};
}
// seconds for each event (tempo map applied)
function timeMidiFile(mf){
  let tempo = 500000, lastTick = 0, sec = 0;
  const out = [];
  for (const e of mf.events){
    sec += (e.tick - lastTick) * tempo / 1e6 / mf.ppq; lastTick = e.tick;
    if (e.tempo){ tempo = e.tempo; continue; }
    out.push({t:sec, data:e.data});
  }
  return out;
}
function writeVar(a, v){ const s = [v & 127]; while ((v >>= 7) > 0) s.unshift((v & 127) | 128); a.push(...s); }
// events: [{tick, data:[...]}] (data may be a sysex starting with F0) -> SMF format 0
function writeMidiFile(events, ppq, tempoBpm, name){
  const tr = [];
  const ev = events.slice().sort((a, b) => a.tick - b.tick);
  if (name){ const nb = Array.from(new TextEncoder().encode(name)); tr.push(0, 0xFF, 0x03); writeVar(tr, nb.length); tr.push(...nb); }
  const us = Math.round(60e6 / (tempoBpm || 120));
  tr.push(0, 0xFF, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255);
  let last = 0;
  for (const e of ev){
    writeVar(tr, Math.max(0, e.tick - last)); last = Math.max(last, e.tick);
    if (e.data[0] === 0xF0){ tr.push(0xF0); writeVar(tr, e.data.length - 1); tr.push(...e.data.slice(1)); }
    else tr.push(...e.data);
  }
  tr.push(0, 0xFF, 0x2F, 0);
  const h = [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, (ppq >> 8) & 255, ppq & 255];
  const L = tr.length;
  return new Uint8Array(h.concat([0x4D, 0x54, 0x72, 0x6B, (L >>> 24) & 255, (L >> 16) & 255, (L >> 8) & 255, L & 255], tr));
}
/* a pattern from a MIDI file: notes quantized to the pattern's step, from the start, up to 64 steps; DX parameter
 * changes (sysex) become motion lanes. Returns {pattern, tempo, notes:count} */
function patternFromMidi(mf, rate){
  const DX = root.DX || require('./engine.js');
  const p = emptyPattern(); p.rate = rate != null ? rate : 3;
  const stepTk = mf.ppq * RATES[p.rate][0] / PPQ;
  let tempo = 120, count = 0, maxStep = 0;
  const on = new Map();
  for (const e of mf.events){
    if (e.tempo){ if (e.tick === 0 || tempo === 120) tempo = Math.round(60e6 / e.tempo * 10) / 10; continue; }
    const m = e.data; if (!m) continue;
    const s = Math.round(e.tick / stepTk);
    if (m[0] === 0xF0){
      const r = DX.parseSysexMessage(m);
      if (r && r.param && s < MAX_STEPS){
        let lane = p.lanes.find(l => l.param === r.param[0]) || p.lanes.find(l => l.param < 0);
        if (lane){ lane.param = r.param[0]; lane.vals[s] = r.param[1]; maxStep = Math.max(maxStep, s); }
      }
      continue;
    }
    const ty = m[0] & 0xF0;
    if (ty === 0x90 && m[2] > 0){
      if (s >= MAX_STEPS) continue;
      const st = p.steps[s];
      if (st.notes.length < STEP_NOTES && !st.notes.some(x => x[0] === m[1])){ st.notes.push([m[1], m[2]]); count++; }
      on.set(m[1], [s, e.tick]); maxStep = Math.max(maxStep, s);
    } else if (ty === 0x80 || (ty === 0x90 && m[2] === 0)){
      const r = on.get(m[1]); if (!r) continue; on.delete(m[1]);
      p.steps[r[0]].gate = Math.max(5, Math.min(TIE, Math.round((e.tick - r[1]) / stepTk * 100)));
    }
  }
  p.len = Math.min(MAX_STEPS, Math.max(16, Math.ceil((maxStep + 1) / 16) * 16));
  return {pattern:p, tempo, notes:count};
}
// a pattern (looped `loops` times) as a MIDI file; the voice goes first as a single-voice dump, motion as parameter changes
function patternToMidi(p, voice, tempo, ch, loops){
  const DX = root.DX || require('./engine.js');
  const ppq = 96, ev = [], st = RATES[p.rate][0], c = (ch || 0) & 15;
  if (voice) ev.push({tick:0, data:Array.from(DX.voiceToSysex(voice, c))});
  for (let L = 0; L < (loops || 1); L++){
    for (let s = 0; s < p.len; s++){
      const sw = s % 2 ? Math.round((p.swing - 50) / 50 * st) : 0;
      const t = (L * p.len + s) * st + sw;
      for (const l of p.lanes) if (l.param >= 0 && l.param <= 155 && l.vals[s] >= 0) ev.push({tick:t, data:DX.paramChange(c, 0, l.param, l.vals[s])});
      const step = p.steps[s];
      if (!step.on) continue;
      const len = Math.max(1, Math.round(st * Math.min(step.gate, TIE) / 100));
      for (const [n, v] of step.notes){ ev.push({tick:t, data:[0x90 | c, n, v]}); ev.push({tick:t + len, data:[0x80 | c, n, 0]}); }
    }
  }
  // note-offs before note-ons at the same tick
  ev.sort((a, b) => a.tick - b.tick || ((a.data[0] & 0xF0) === 0x80 ? -1 : 0) - ((b.data[0] & 0xF0) === 0x80 ? -1 : 0));
  return writeMidiFile(ev, ppq, tempo, 'Omega Seven pattern');
}
// the recorder's events ({sec, data}) as a MIDI file at 120 BPM, 480 ticks per quarter
function recordingToMidi(evs, voice, ch){
  const DX = root.DX || require('./engine.js');
  const ppq = 480, ev = [];
  if (voice) ev.push({tick:0, data:Array.from(DX.voiceToSysex(voice, ch || 0))});
  for (const e of evs) ev.push({tick:Math.round(e.sec * 2 * ppq), data:e.data});
  return writeMidiFile(ev, ppq, 120, 'Omega Seven performance');
}

/* ---------------- MIDI file player ---------------- */
class FilePlayer {
  constructor(){ this.evs = []; this.ix = 0; this.t = 0; this.playing = false; this.loop = false; this.len = 0; this.name = ''; }
  load(mf, name){ this.evs = timeMidiFile(mf); this.len = this.evs.length ? this.evs[this.evs.length - 1].t : 0; this.ix = 0; this.t = 0; this.name = name || ''; }
  start(){ this.ix = 0; this.t = 0; this.playing = this.evs.length > 0; }
  stop(){ this.playing = false; }
  // advance by dt seconds, sending due events to send(data)
  run(dt, send){
    if (!this.playing) return;
    this.t += dt;
    while (this.ix < this.evs.length && this.evs[this.ix].t <= this.t){ send(this.evs[this.ix].data); this.ix++; }
    if (this.ix >= this.evs.length){ if (this.loop){ this.ix = 0; this.t = 0; } else this.playing = false; }
  }
}

const api = {PPQ, RATES, ARP_MODES, MAX_STEPS, LANES, PATTERNS, STEP_NOTES, TIE, emptyPattern, defaultSeqState, copyPattern, Sequencer, parseMidiFile, timeMidiFile,
  writeMidiFile, patternFromMidi, patternToMidi, recordingToMidi, FilePlayer};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
root.DXS = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
