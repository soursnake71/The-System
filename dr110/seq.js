/* Omega 110 sequencer: the DR-110's pattern and song memory, run inside the audio thread (worklet.js) and by the
   WAV export. 32 patterns in four banks (A, B: presets; C, D: yours) of 8 rhythms, 16 or 12 steps (12 = triplets), an
   accent per step; two songs (I and II) of up to 128 measures, with D.C. (repeat from the top) at the end.
   Step bits: 1 BD, 2 SD, 4 OH, 8 CH, 16 CY, 32 HC, 64 KN, 128 ACCENT. */
'use strict';
(function (root) {
const ACC = 128;
const MAX_MEASURES = 128;

// the presets (A1-B8): original patterns in the styles the DR-110's own presets cover. x = hit, A on the accent row
const PRESETS = [
  ['ROCK 1', 16, {BD:'x.......x.x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', AC:'A...A...A...A...'}],
  ['ROCK 2', 16, {BD:'x......xx.x.....', SD:'....x.......x..x', CH:'x.x.x.x.x.x.x.x.', OH:'..............x.', AC:'A.......A.......'}],
  ['DISCO', 16, {BD:'x...x...x...x...', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..x...x...x...x.', AC:'....A.......A...'}],
  ['FUNK', 16, {BD:'x..x..x...x..x..', SD:'....x..x.x..x...', CH:'xxxxxxxxxxxxxxxx', AC:'A...A...A...A...'}],
  ['SHUFFLE', 12, {BD:'x.....x..x..', SD:'...x.....x..', CH:'x.xx.xx.xx.x', AC:'A.....A.....'}],
  ['SLOW ROCK', 12, {BD:'x.....x.x...', SD:'...x.....x..', CH:'xxxxxxxxxxxx', AC:'A..A..A..A..'}],
  ['BOSSA', 16, {BD:'x..xx..xx..xx..x', SD:'x..x..x...x..x..', CH:'x.x.x.x.x.x.x.x.', AC:'A.......A.......'}],
  ['SAMBA', 16, {BD:'x..xx..xx..xx..x', SD:'..x..x...x..x.x.', CH:'xxxxxxxxxxxxxxxx', HC:'....x.......x...', AC:'A...A...A...A...'}],
  ['REGGAE', 16, {BD:'........x.......', SD:'........x.......', CH:'..x...x...x...x.', KN:'....x.......x...', AC:'........A.......'}],
  ['WALTZ', 12, {BD:'x...........', SD:'....x...x...', CH:'x...x...x...', AC:'A...........'}],
  ['SWING', 12, {BD:'x.....x.....', SD:'...x.....x..', CY:'x..x.xx..x.x', CH:'...x.....x..', AC:'A..A..A..A..'}],
  ['ELECTRO', 16, {BD:'x.....x...x.....', SD:'....x.......x...', HC:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'.......x.......x', AC:'A.........A.....'}],
  ['POP', 16, {BD:'x.......x.x.....', SD:'....x.......x...', HC:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', AC:'A...A...A...A...'}],
  ['MARCH', 16, {BD:'x.......x.......', SD:'x.xxx.x.x.xxx.x.', CY:'x...............', AC:'A...A...A...A...'}],
  ['TWIST', 16, {BD:'x..x..x.x..x..x.', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'......x.......x.', AC:'A.......A.......'}],
  ['FILL IN', 16, {BD:'x.......x...x.x.', SD:'..x.xxx.xxxxxxxx', CY:'...............x', KN:'x.......x.......', AC:'A...A...A...AAAA'}],
];
const BIT = {BD:1, SD:2, OH:4, CH:8, CY:16, HC:32, KN:64, AC:ACC};
function presetPattern(i){
  const [, len, rows] = PRESETS[i], st = new Array(16).fill(0);
  for (const [k, s] of Object.entries(rows)) for (let j = 0; j < s.length && j < 16; j++) if (s[j] !== '.') st[j] |= BIT[k];
  return {len, st};
}
function emptyPattern(){ return {len:16, st:new Array(16).fill(0)}; }
function defaultMemory(){
  const pats = [];
  for (let i = 0; i < 16; i++) pats.push(presetPattern(i));
  for (let i = 0; i < 16; i++) pats.push(emptyPattern());
  // C1 starts as a copy of A1 so there's something to edit
  pats[16] = presetPattern(0);
  return {pats, songs:[{m:[0, 0, 0, 15], dc:true}, {m:[], dc:false}]};
}

class Sequencer {
  constructor(engine, sr){
    this.e = engine; this.sr = sr; this.mem = defaultMemory();
    this.running = false; this.mode = 'pattern'; this.pat = 0; this.next = -1; this.song = 0; this.measure = 0;
    this.step = 0; this.countdown = 0; this.bpm = 120; this.ext = false; this.extTicks = 0;
    this.onStep = null; this.onHit = null; this.onStop = null; this.onClock = null; this.clockAcc = 0;
    this.held = 0;
    this.swing = 0;                                 // 0..0.5: delays the even 16ths (the "SHUFFLE" in the mixer)
  }
  patLen(i){ return this.mem.pats[i] ? this.mem.pats[i].len : 16; }
  stepSamples(i){ const len = this.patLen(i); return this.sr * 60 / this.bpm * 4 / len; }
  // which pattern plays at a measure of the song (-1: past the end)
  songPat(m){ const s = this.mem.songs[this.song]; return m < s.m.length ? s.m[m] : -1; }
  start(){
    this.running = true; this.started = false; this.step = 0; this.countdown = 0; this.extTicks = 0; this.clockAcc = 0;
    if (this.mode === 'song'){ this.measure = 0; const p = this.songPat(0); if (p < 0){ this.running = false; return; } this.pat = p; }
    else this.measure = 0;
    if (this.next >= 0){ this.pat = this.next; this.next = -1; }
  }
  stop(){ this.running = false; this.step = 0; for (let v = 0; v < 7; v++) if (this.held & (1 << v)) this.e.release(v); this.held = 0; if (this.onStop) this.onStop(); }
  select(i){ if (this.running && this.mode === 'pattern') this.next = i; else { this.pat = i; this.next = -1; } }
  fire(){
    const pt = this.mem.pats[this.pat], bits = pt ? pt.st[this.step] : 0;
    // each hit's gate lasts one step: the pads' ADSR release starts at the next step
    for (let v = 0; v < 7; v++) if (this.held & (1 << v)) this.e.release(v);
    this.held = bits & 127;
    if (bits & 127) for (let v = 0; v < 7; v++) if (bits & (1 << v)){ this.e.trigger(v, (bits & ACC) !== 0); if (this.onHit) this.onHit(v, (bits & ACC) !== 0); }
    if (this.onStep) this.onStep(this.step, this.pat, this.measure);
  }
  advance(){
    this.step++;
    if (this.step >= this.patLen(this.pat)){
      this.step = 0; this.measure++;
      if (this.mode === 'song'){
        let p = this.songPat(this.measure);
        if (p < 0){ if (this.mem.songs[this.song].dc && this.mem.songs[this.song].m.length){ this.measure = 0; p = this.songPat(0); } else { this.stop(); return false; } }
        this.pat = p;
      } else if (this.next >= 0){ this.pat = this.next; this.next = -1; }
    }
    return true;
  }
  // external MIDI clock: 24 per quarter; a step is 6 ticks (16 steps) or 8 ticks (12 steps)
  clockTick(){
    if (!this.running || !this.ext) return;
    const per = this.patLen(this.pat) === 12 ? 8 : 6;
    if (this.extTicks % per === 0){ if (this.extTicks > 0 && !this.advance()) return; this.fire(); }
    this.extTicks++; if (this.extTicks >= per * 1000) this.extTicks = this.extTicks % per;
  }
  // renders n samples into L, R from off, firing steps on time (internal clock)
  render(L, R, off, n){
    let pos = 0;
    while (pos < n){
      if (this.running && !this.ext && this.countdown <= 0){
        if (this.started) { if (!this.advance()) { this.started = false; continue; } }
        this.started = true;
        this.fire();
        let len = this.stepSamples(this.pat);
        // swing: even steps of a 16-step pattern come late, odd steps early, so pairs keep their length
        if (this.patLen(this.pat) === 16 && this.swing > 0) len *= this.step % 2 === 0 ? 1 + this.swing : 1 - this.swing;
        this.countdown += len;
      }
      let todo = n - pos;
      if (this.running && !this.ext) todo = Math.max(1, Math.min(todo, Math.ceil(this.countdown)));
      this.e.process(L, R, off + pos, todo);
      if (this.running && !this.ext){ this.countdown -= todo; this.clockOut(todo); }
      pos += todo;
    }
    if (!this.running) this.started = false;
  }
  // MIDI clock out: 24 per quarter note, counted in samples
  clockOut(n){
    if (!this.onClock) return;
    const per = this.sr * 60 / this.bpm / 24;
    this.clockAcc += n;
    while (this.clockAcc >= per){ this.clockAcc -= per; this.onClock(); }
  }
  // tap write: the hit lands on the nearest step of the pattern that's playing
  tapWrite(v, accent){
    if (!this.running) return null;
    const pt = this.mem.pats[this.pat], len = this.stepSamples(this.pat);
    let s = this.step; const elapsed = len - this.countdown;
    if (elapsed > len / 2) s = (s + 1) % pt.len;
    pt.st[s] |= 1 << v; if (accent) pt.st[s] |= ACC;
    return {pat:this.pat, step:s, bits:pt.st[s]};
  }
}

const API = {Sequencer, PRESETS, presetPattern, emptyPattern, defaultMemory, ACC, BIT, MAX_MEASURES};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DRSEQ = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
