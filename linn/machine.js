/* Omega Linn firmware: the LinnDrum's front panel, its pattern and song memory and its sequencer, as one piece of code
   that runs on the audio thread (the app's worklet, and line-for-line in C++ in the plugin: plugins/linn/Machine.h).
   The panel sends key presses (key(id, down)) and reads back the displays and LEDs (display()). Written from the
   LinnDrum owner's manual: the keys work as it describes, including the ones you hold while pressing another.

   Memory: 49 patterns and 49 songs, numbered with the 1-7 keys (11-77). Patterns 11-57 come with factory grooves (the
   hardware's were pre-sets too; these are original), 61-77 are empty. A pattern: its length (in beats, 2 bars of 4/4 to
   start), its ERROR CORRECT, its TIMING (AFTER: A-F, straight to full shuffle) and its notes (192 ticks a beat).
   PATTERN mode: type a pattern (two keys), PLAY/STOP. While it plays, a new number waits for the end of the pattern.
   Record: hold RECORD and press PLAY/STOP; play the drum keys; PLAY/STOP ends. Notes land on the ERROR CORRECT grid
   (hold ERROR CORRECT, press the 1-7 key under 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, HI). BEFORE + A-F shuffles the notes
   you record from now on; AFTER + A-F sets the pattern's playback timing. ERASE + a drum key: stopped, it removes that
   sound from the pattern; recording, it wipes the sound as the pattern passes. ERASE + a number empties that pattern.
   COPY + a number: into an empty pattern it copies, onto one with notes it joins (appends), onto itself it doubles.
   RECORD + LENGTH starts the count: PLAY/STOP where the pattern should start over sets its length (in whole beats).
   Holding RECORD shows the free memory (STEP display). Holding BPM/TRIGGER shows the tempo, and with a 1-7 key sets
   the trigger output (1/8 .. 1/32T, or every cowbell).
   SONG mode (the SONG/PATTERN key): SONG# + a number selects a song. ENTER + a number writes a pattern on the step and
   moves on; ENTER + END ends the song there. <- -> move along the steps, INSERT + a number puts a step in, DELETE takes
   one out. PLAY/STOP plays from the first step (INSERT + PLAY/STOP from the step shown). A song starts over at its end;
   SONG/PATTERN + END makes it stop there instead (and back). */
'use strict';
(function (root) {
const PPQ = 192;
const MAX_EV = 3000;                 // per pattern
const MEM_CAP = 12000;               // notes in all patterns (the FREE display is out of this)
const NP = 49;
const DEF_LEN = 8 * PPQ;             // two bars of 4/4
// keys
const K = {SONGPAT:0, SONGNUM:1, END:2, DELETE:3, INSERT:4, LEFT:5, RIGHT:6, ENTER:7, D1:8, STORE:15, LOAD:16, EXTSYNC:17, BPM:18, PLAY:19, DRUM:20};
// the same keys in pattern mode
const KP = {ERRCOR:1, BEFORE:2, AFTER:3, COPY:4, LENGTH:5, ERASE:6, RECORD:7};
const NDRUM = 16, PERC_KEY = 7;
// the drum keys: [drum set sound, percussion sound] (-1: the PERCUSSION key)
const DRUM_KEYS = [[5, 5], [2, 2], [3, 3], [4, 4], [0, 0], [1, 1], [14, 14], [-1, -1],
  [6, 15], [7, 16], [8, 17], [9, 18], [10, 19], [11, 20], [12, 21], [13, 22]];
const EC_GRID = [96, 64, 48, 32, 24, 16, 1];
const EC_NAMES = ['1/8', '1/8T', '1/16', '1/16T', '1/32', '1/32T', 'HI'];
const TIMING = ['A', 'B', 'C', 'D', 'E', 'F'];
const SHUF16 = [0, 4, 8, 12, 16, 20];          // 50, 54, 58, 62, 66.7, 71 % of an 1/8 (shuffled 1/16s)
const SHUF8 = [0, 8, 15, 23, 32, 40];          // the same on 1/8s (shuffled 1/8s when ERROR CORRECT is 1/8 or 1/8T)
const TRIG_DIV = [96, 64, 48, 32, 24, 16, 0];  // the trigger output (0: every cowbell)
const pnum = i => (Math.floor(i / 7) + 1) * 10 + (i % 7) + 1;
const pidx = n => (Math.floor(n / 10) - 1) * 7 + (n % 10) - 1;

function emptyPattern(){ return {len:DEF_LEN, ec:2, tim:0, bef:0, ev:[]}; }
function emptySong(){ return {steps:[]}; }
// the factory grooves 11-57: [name, timing, error correct, beats, rows]; rows: one bar of 1/16s (or of the error
// correct grid), repeated to fill the pattern. Keys: sound names below; x = hit.
const SN = {bd:0, BD:1, sn1:2, sn2:3, SN:4, SS:5, hh:6, HH:7, OH:8, HT:9, MT:10, LT:11, rd:12, RD:13, CR:14, cb:15, CB:16, tb:17, TB:18, HC:19, LC:20, CW:21, CP:22};
const FACTORY = [
  ['ROCK', 0, 2, 8, {BD:'x.......x.x.....', SN:'....x.......x...', HH:'x.x.x.x.x.x.x.x.'}],
  ['ROCK 2', 0, 2, 8, {BD:'x.x.....x.x..x..', SN:'....x.......x...', HH:'x.x.x.x.x.x.x.x.', OH:'..............x.'}],
  ['POP 16', 0, 2, 8, {BD:'x.....x.x.......', SN:'....x.......x...', HH:'x.xxx.xxx.xxx.xx', hh:'.x...x...x...x..'}],
  ['FUNK', 1, 2, 8, {BD:'x..x..x...x..x..', SN:'....x..x.x..x...', sn1:'.x.....x.....x..', HH:'xxxxxxxxxxxxxx.x', OH:'..............x.'}],
  ['DISCO', 0, 2, 8, {BD:'x...x...x...x...', SN:'....x.......x...', hh:'x...x...x...x...', OH:'..x...x...x...x.', CP:'....x.......x...'}],
  ['NEW WAVE', 0, 2, 8, {BD:'x...x...x...x...', SN:'....x.......x...', HH:'xxxxxxxxxxxxxxxx', TB:'....x.......x...'}],
  ['HALF TIME', 0, 2, 8, {BD:'x.....x...x.....', SN:'........x.......', HH:'x.x.x.x.x.x.x.x.', RD:'................'}],
  ['SHUFFLE', 4, 2, 8, {BD:'x.....x.x.......', SN:'....x.......x...', HH:'x.xxx.xxx.xxx.xx'}],
  ['BALLAD', 0, 2, 8, {BD:'x.........x.....', SS:'....x.......x...', HH:'x.x.x.x.x.x.x.x.', CB:'..x...x...x...x.'}],
  ['MOTOWN', 1, 2, 8, {BD:'x..x....x..x....', SN:'....x.......x...', TB:'....x.......x...', HH:'x.x.x.x.x.x.x.x.'}],
  ['REGGAE', 2, 2, 8, {BD:'........x.......', SS:'........x.......', HH:'..x...x...x...x.', CB:'x.x.x.x.x.x.x.x.'}],
  ['LATIN', 0, 2, 8, {BD:'x..x....x..x....', HC:'..x..x....x.xx..', LC:'x.....x.......x.', CW:'x...x.x...x.x...', CB:'x.xxx.xxx.xxx.xx'}],
  ['BOSSA', 0, 2, 8, {BD:'x..xx..xx..xx..x', SS:'x..x..x...x..x..', HH:'x.x.x.x.x.x.x.x.'}],
  ['SAMBA', 0, 2, 8, {BD:'x..xx..xx..xx..x', SN:'..x..x....x..x..', TB:'xxxxxxxxxxxxxxxx', HC:'....x..x....x..x'}],
  ['ELECTRO', 0, 2, 8, {BD:'x.....x...x.....', SN:'....x.......x...', CP:'....x.......x...', HH:'x.x.x.x.x.x.x.x.', CW:'..x.....x...x...'}],
  ['HIP HOP', 2, 2, 8, {BD:'x......x..x.....', SN:'....x.......x..x', HH:'x.x.x.x.x.x.x.x.', TB:'....x.......x...'}],
  ['FREESTYLE', 0, 2, 8, {BD:'x..x..x...x..x..', SN:'....x.......x...', CP:'....x.......x...', HH:'xxxxxxxxxxxxxxxx', OH:'..x...x...x...x.'}],
  ['SYNTH POP', 0, 2, 8, {BD:'x...x...x...x...', SN:'....x.......x...', CP:'....x.......x...', HH:'..x...x...x...x.', CB:'x.x.x.x.x.x.x.x.'}],
  ['TOM FILL', 0, 2, 4, {SN:'x.xx............', HT:'....x.xx........', MT:'........x.xx....', LT:'............x.xx', BD:'x...........x...'}],
  ['CRASH IN', 0, 2, 8, {BD:'x.......x.x.....', SN:'....x.......x...', CR:'x...............', HH:'..x.x.x.x.x.x.x.'}],
  ['BREAK', 0, 2, 8, {BD:'x.x.......xx....', SN:'....x..x.x..x..x', OH:'x...............', HH:'..x.x.x.x.x.x.x.'}],
  ['GHOST FUNK', 0, 2, 8, {BD:'x..x...x..x..x..', SN:'....x.......x...', sn1:'.x.xx.x..x.x.x.x', HH:'x.x.x.x.x.x.x.x.', hh:'.x.x.x.x.x.x.x.x'}],
  ['SHUFFLE 2', 3, 2, 8, {BD:'x.....x...x.....', SN:'....x.......x...', RD:'x.xxx.xxx.xxx.xx', HH:'....x.......x...'}],
  ['12/8', 0, 1, 6, {BD:'x.....x.....', SN:'......x.....', RD:'xxxxxxxxxxxx', HH:'...x.....x..'}],
  ['WALTZ', 0, 2, 6, {BD:'x...........', SS:'....x...x...', HH:'x.x.x.x.x.x.', CB:'..x...x...x.'}],
  ['CONGAS', 0, 2, 8, {BD:'x.......x.......', HC:'x.xx..x.x.xx..x.', LC:'....x.......x..x', CB:'x.x.x.x.x.x.x.x.', CW:'x.......x.......'}],
  ['COWBELL', 0, 2, 8, {BD:'x..x..x...x..x..', SN:'....x.......x...', CW:'x.x.x.x.x.x.x.x.', HH:'xxxxxxxxxxxxxxxx'}],
  ['SIDESTICK', 0, 2, 8, {BD:'x.....x.x.......', SS:'....x.......x...', RD:'x.x.x.x.x.x.x.x.'}],
  ['RIDE', 0, 2, 8, {BD:'x.......x.......', SN:'....x.......x...', RD:'x.x.x.x.x.x.x.x.', rd:'.x.x.x.x.x.x.x.x', hh:'....x.......x...'}],
  ['TRIPLETS', 0, 3, 8, {BD:'x.....x.....', SN:'...x.....x..', HH:'xxxxxxxxxxxx'}],
  ['32NDS', 0, 4, 4, {BD:'x.......x.......x.......x.......', SN:'........x...............x.......', HH:'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'}],
  ['DOUBLE', 0, 2, 8, {BD:'x.x.x.x.x.x.x.x.', SN:'....x.......x...', HH:'xxxxxxxxxxxxxxxx', CR:'x...............'}],
  ['MARCH', 0, 2, 8, {BD:'x.......x.......', SN:'x.xxx.x.x.xxx.xx', sn2:'.x...x...x...x..'}],
  ['TAMBOURINE', 1, 2, 8, {BD:'x...x...x...x...', SN:'....x.......x...', TB:'..x...x...x...x.', tb:'x...x...x...x...', HH:'x.x.x.x.x.x.x.x.'}],
  ['CLAPS', 0, 2, 8, {BD:'x..x..x.x..x..x.', CP:'....x.......x...', HH:'x.x.x.x.x.x.x.x.', OH:'.......x.......x'}],
];
function factoryPattern(i){
  const [, tim, ec, beats, rows] = FACTORY[i], p = emptyPattern(); p.tim = tim; p.ec = ec; p.len = beats * PPQ;
  const g = EC_GRID[ec];
  for (const [k, s] of Object.entries(rows)){
    const n = s.length; if (!n) continue;
    for (let t = 0, j = 0; t < p.len; t += g, j++) if (s[j % n] === 'x') p.ev.push({t, s:SN[k]});
  }
  p.ev.sort((a, b) => a.t - b.t || a.s - b.s);
  return p;
}
function defaultMemory(){
  const pats = [], songs = [];
  for (let i = 0; i < NP; i++) pats.push(i < FACTORY.length ? factoryPattern(i) : emptyPattern());
  for (let i = 0; i < NP; i++) songs.push(emptySong());
  // song 11: a short demo arrangement
  songs[0].steps = [19, 0, 0, 1, 1, 18, 3, 3, 4, 4, 20, 19].map(n => n);
  return {pats, songs, repeat:true};
}
const used = mem => mem.pats.reduce((a, p) => a + p.ev.length, 0);

class Machine {
  constructor(engine, sr){
    this.e = engine; this.sr = sr; this.mem = defaultMemory();
    this.mode = 0; this.pat = 0; this.song = 0; this.cursor = 0; this.next = -1;
    this.held = 0; this.used = 0; this.entry = ''; this.trig = 2; this.perc = false; this.ext = false;
    this.running = false; this.recording = false; this.ppat = 0; this.sstep = 0; this.tk = 0; this.countdown = 0; this.lenArm = -1;
    this.bpm = 120; this.eraseHeld = 0; this.sel = 0;
    this.now = 0; this.flashL = ''; this.flashR = ''; this.flashUntil = 0;
    this.metroAlways = false; this.version = 0; this.clockAcc = 0;
    this.onHit = null; this.onClock = null; this.onStop = null; this.onStart = null; this.onTrig = null;
  }
  changed(){ this.version++; }
  curPat(){ return this.mem.pats[this.running ? this.ppat : this.pat]; }
  flash(l, r, ms){ this.flashL = l; this.flashR = r; this.flashUntil = this.now + Math.round(ms * this.sr / 1000); }
  isHeld(k){ return ((this.held >> k) & 1) === 1; }
  // ---- keys
  key(id, down){
    if (id < 0 || id >= K.DRUM + NDRUM) return;
    if (down) this.used |= this.held;                 // the keys held now got used with this one (SONG/PATTERN + END)
    if (id < K.DRUM){ if (down){ this.held |= 1 << id; this.used &= ~(1 << id); } else this.held &= ~(1 << id); }
    if (id >= K.DRUM){ this.drum(id - K.DRUM, down); return; }
    if (!down){
      if (id === K.SONGPAT && !((this.used >> id) & 1)){ if (this.running) this.stop(); this.mode = this.mode ? 0 : 1; this.cursor = 0; this.changed(); }
      if (id < K.D1 || id >= K.D1 + 7) this.entry = '';        // letting go of the held key drops a half-typed number
      return;
    }
    this.entry = id >= K.D1 && id < K.D1 + 7 ? this.entry : '';
    if (id >= K.D1 && id < K.D1 + 7){ this.digit(id - K.D1 + 1); return; }
    switch (id){
      case K.PLAY: this.play(); break;
      case K.EXTSYNC: this.ext = !this.ext; this.changed(); break;
      case K.END:
        if (this.mode === 1 && this.isHeld(K.SONGPAT)){ this.mem.repeat = !this.mem.repeat; this.e.beep(); this.flash(this.mem.repeat ? 'rE' : 'St', 'P ', 900); this.changed(); }
        else if (this.mode === 1 && this.isHeld(K.ENTER)){ const s = this.mem.songs[this.song]; s.steps.length = Math.min(s.steps.length, this.cursor); this.changed(); }
        break;
      case K.DELETE: if (this.mode === 1){ const s = this.mem.songs[this.song]; if (this.cursor < s.steps.length){ s.steps.splice(this.cursor, 1); this.changed(); } } break;
      case K.LEFT:                  // LENGTH in pattern mode
        if (this.mode === 1){ this.cursor = Math.max(0, this.cursor - 1); this.changed(); }
        else if (this.isHeld(KP.RECORD)) this.armLength();
        break;
      case K.RIGHT: if (this.mode === 1){ this.cursor = Math.min(this.mem.songs[this.song].steps.length, this.cursor + 1); this.changed(); } break;
    }
  }
  drum(k, down){
    if (k === PERC_KEY){ if (down){ this.perc = !this.perc; this.changed(); } return; }
    const s = DRUM_KEYS[k][this.perc ? 1 : 0];
    if (!down){ this.eraseHeld &= ~(1 << s); this.e.release(soundCh(s)); return; }
    if (this.mode === 0 && this.isHeld(KP.ERASE)){
      if (this.recording){ this.eraseHeld |= 1 << s; return; }
      const pt = this.mem.pats[this.pat], n = pt.ev.length; pt.ev = pt.ev.filter(x => x.s !== s);
      if (pt.ev.length !== n){ this.e.beep(); this.changed(); }
      return;
    }
    this.inst(s);
  }
  // a drum key or a MIDI note: plays it and records it while recording
  inst(s){
    this.sel = soundCh(s);
    this.e.trigger(s);
    if (this.onHit) this.onHit(s);
    if (this.trig === 6 && s === 21 && this.onTrig) this.onTrig();
    if (this.running && this.recording && this.mode === 0) this.record(s);
  }
  record(s){
    const pt = this.mem.pats[this.ppat], len = pt.len, spt = this.spt();
    if (used(this.mem) >= MEM_CAP || pt.ev.length >= MAX_EV){ this.flash('Fu', 'LL', 900); this.e.beep(); return; }
    const f = this.tk - this.countdown / spt, q = EC_GRID[pt.ec], base = pt.ec <= 1 ? 96 : 48;
    let tq = Math.floor(f / q + 0.5) * q;
    if (q > 1 && pt.bef > 0 && tq % (base * 2) === base) tq += (base === 96 ? SHUF8 : SHUF16)[pt.bef];
    while (tq >= len) tq -= len; if (tq < 0) tq += len;
    pt.ev = pt.ev.filter(x => !(x.t === tq && x.s === s));
    const ev = {t:tq, s};
    // already heard: don't play it again when the sequencer reaches it this pass
    const ahead = ((tq - f) % len + len) % len;
    if (ahead > 0 && ahead <= q + 20) ev.skip = 1;
    pt.ev.push(ev); pt.ev.sort((a, b) => a.t - b.t || a.s - b.s);
    this.changed();
  }
  // the step grid (the app's PADS page): a note on or off at tick t of pattern p
  edit(p, t, s, on){
    const pt = this.mem.pats[p]; if (!pt || t < 0 || t >= pt.len || s < 0 || s > 22) return;
    pt.ev = pt.ev.filter(x => !(x.t === t && x.s === s));
    if (on && pt.ev.length < MAX_EV){ pt.ev.push({t, s}); pt.ev.sort((a, b) => a.t - b.t || a.s - b.s); }
    this.changed();
  }
  setLen(p, beats){ const pt = this.mem.pats[p]; if (!pt) return; pt.len = Math.max(1, Math.min(99, beats)) * PPQ; pt.ev = pt.ev.filter(x => x.t < pt.len); if (this.running && this.tk >= this.curPat().len) this.tk = 0; this.changed(); }
  digit(d){
    const m = this.mode;
    if (this.isHeld(K.BPM)){ this.trig = d - 1; this.changed(); return; }
    if (m === 0){
      const pt = this.mem.pats[this.pat];
      if (this.isHeld(KP.ERRCOR)){ pt.ec = d - 1; this.changed(); return; }
      if (this.isHeld(KP.BEFORE)){ if (d <= 6){ pt.bef = d - 1; this.changed(); } return; }
      if (this.isHeld(KP.AFTER)){ if (d <= 6){ (this.running ? this.mem.pats[this.ppat] : pt).tim = d - 1; this.changed(); } return; }
    } else if (!this.isHeld(K.SONGNUM) && !this.isHeld(K.ENTER) && !this.isHeld(K.INSERT)) return;
    this.entry += d;
    if (this.entry.length < 2) return;
    const n = pidx(parseInt(this.entry, 10)); this.entry = '';
    if (m === 0){
      if (this.isHeld(KP.ERASE)){ this.mem.pats[n] = emptyPattern(); this.e.beep(); this.flash('  ', String(pnum(n)), 700); if (this.running && this.ppat === n) this.tk = 0; this.changed(); return; }
      if (this.isHeld(KP.COPY)){ this.copy(this.pat, n); return; }
      this.selectPattern(n);
    } else {
      const s = this.mem.songs[this.song];
      if (this.isHeld(K.SONGNUM)){ if (this.running) this.stop(); this.song = n; this.cursor = 0; }
      else if (this.isHeld(K.ENTER)){ if (this.cursor < s.steps.length) s.steps[this.cursor] = n; else if (s.steps.length < 99) s.steps.push(n); else { this.flash('Fu', 'LL', 700); this.e.beep(); return; } this.cursor = Math.min(s.steps.length, this.cursor + 1); }
      else if (this.isHeld(K.INSERT)){ if (s.steps.length < 99){ s.steps.splice(this.cursor, 0, n); this.cursor++; } else { this.flash('Fu', 'LL', 700); this.e.beep(); return; } }
      this.changed();
    }
  }
  selectPattern(n){ if (this.running && this.mode === 0) this.next = n; else { this.pat = n; this.ppat = n; } this.pat = n; this.changed(); }
  copy(src, dst){
    const a = this.mem.pats[src], b = this.mem.pats[dst];
    if (dst === src){            // doubles it
      if (a.len * 2 > 99 * PPQ || a.ev.length * 2 > MAX_EV) return this.err();
      a.ev = a.ev.concat(a.ev.map(x => ({t:x.t + a.len, s:x.s}))); a.len *= 2;
    } else if (!b.ev.length){ this.mem.pats[dst] = JSON.parse(JSON.stringify(a)); for (const x of this.mem.pats[dst].ev) delete x.skip; }
    else {                        // joins: the source after the destination
      if (a.len + b.len > 99 * PPQ || a.ev.length + b.ev.length > MAX_EV) return this.err();
      b.ev = b.ev.concat(a.ev.map(x => ({t:x.t + b.len, s:x.s}))); b.len += a.len;
    }
    if (used(this.mem) > MEM_CAP) return this.err();
    this.e.beep(); this.flash('  ', String(pnum(dst)), 700); this.changed();
  }
  err(){ this.flash('Er', 'r ', 700); this.e.beep(); }
  armLength(){
    if (!this.running){ this.ppat = this.pat; this.running = true; this.tk = 0; this.countdown = 0; this.clockAcc = 0; this.recording = false; if (this.onStart) this.onStart(); }
    this.lenArm = 0; this.changed();
  }
  play(){
    if (this.lenArm >= 0){         // RECORD + LENGTH: the length is up to here, in whole beats
      const beats = Math.max(1, Math.min(99, Math.round(this.lenArm / PPQ)));
      const pt = this.mem.pats[this.ppat]; pt.len = beats * PPQ; pt.ev = pt.ev.filter(x => x.t < pt.len);
      this.lenArm = -1; this.tk = 0; this.countdown = 0; this.flash('  ', String(beats).padStart(2, ' '), 900); this.changed(); return;
    }
    if (this.running){ this.stop(); return; }
    if (this.mode === 0){
      const rec = this.isHeld(KP.RECORD);
      if (!rec && !this.mem.pats[this.pat].ev.length){ this.e.beep(); this.flash('  ', '--', 500); return; }
      this.start(rec, 0);
    } else {
      const s = this.mem.songs[this.song];
      if (!s.steps.length){ this.e.beep(); this.flash('--', '--', 500); return; }
      this.start(false, this.isHeld(K.INSERT) ? Math.min(this.cursor, s.steps.length - 1) : 0);
    }
  }
  // ---- transport
  spt(){ return this.sr * 60 / this.bpm / PPQ; }
  start(rec, step){
    this.running = true; this.tk = 0; this.countdown = 0; this.clockAcc = 0; this.lenArm = -1; this.eraseHeld = 0;
    for (const pt of this.mem.pats) for (const x of pt.ev) x.skip = 0;
    if (this.mode === 1){ this.sstep = step || 0; this.ppat = this.mem.songs[this.song].steps[this.sstep]; this.recording = false; }
    else { this.ppat = this.pat; this.next = -1; this.recording = !!rec; }
    if (this.onStart) this.onStart();
    this.changed();
  }
  stop(){ this.running = false; this.recording = false; this.lenArm = -1; this.next = -1; if (this.mode === 0) this.pat = this.ppat; if (this.onStop) this.onStop(); this.changed(); }
  // the plugin following the DAW: a position in ticks from the start
  locate(abs){
    if (!this.running) return;
    if (this.mode === 0){ const len = this.mem.pats[this.ppat].len; this.tk = ((Math.floor(abs) % len) + len) % len; }
    else {
      const s = this.mem.songs[this.song]; let a = Math.max(0, Math.floor(abs)), i = 0;
      let total = 0; for (const p of s.steps) total += this.mem.pats[p].len;
      if (this.mem.repeat && total > 0) a %= total;
      while (i < s.steps.length){ const len = this.mem.pats[s.steps[i]].len; if (a < len) break; a -= len; i++; }
      if (i >= s.steps.length){ this.stop(); return; }
      this.sstep = i; this.ppat = s.steps[i]; this.tk = a;
    }
    this.countdown = 0;
  }
  position(){
    if (this.mode === 0) return this.tk;
    let a = 0; const s = this.mem.songs[this.song]; for (let i = 0; i < this.sstep && i < s.steps.length; i++) a += this.mem.pats[s.steps[i]].len; return a + this.tk;
  }
  // one tick of the sequencer
  tick(){
    const pt = this.mem.pats[this.ppat], len = pt.len, t = this.tk;
    const base = pt.ec <= 1 ? 96 : 48, sw = (base === 96 ? SHUF8 : SHUF16)[pt.tim];
    let removed = false;
    for (const x of pt.ev){
      const et = sw && x.t % (base * 2) === base ? x.t + sw : x.t;
      if (et !== t) continue;
      if (this.recording && ((this.eraseHeld >> x.s) & 1)){ x.del = 1; removed = true; continue; }
      if (x.skip){ x.skip = 0; continue; }
      this.e.trigger(x.s);
      if (this.onHit) this.onHit(x.s);
      if (this.trig === 6 && x.s === 21 && this.onTrig) this.onTrig();
    }
    if (removed){ pt.ev = pt.ev.filter(x => !x.del); this.changed(); }
    if ((this.recording || this.metroAlways || this.lenArm >= 0) && t % PPQ === 0) this.e.click(t === 0 || (this.lenArm >= 0 && this.lenArm % (8 * PPQ) === 0));
    if (this.trig < 6 && t % TRIG_DIV[this.trig] === 0 && this.onTrig) this.onTrig();
    this.tk++;
    if (this.lenArm >= 0){ this.lenArm++; if (this.lenArm > 99 * PPQ){ this.lenArm = -1; this.err(); } return; }
    if (this.tk >= len){
      this.tk = 0;
      if (this.mode === 1){
        const s = this.mem.songs[this.song];
        this.sstep++;
        if (this.sstep >= s.steps.length){ if (this.mem.repeat && s.steps.length){ this.sstep = 0; } else { this.stop(); return; } }
        this.ppat = s.steps[this.sstep];
      } else if (this.next >= 0){ this.ppat = this.next; this.next = -1; }
    }
  }
  // external MIDI clock: 24 per quarter = 8 ticks each
  clockTick(){ if (this.running && this.ext) for (let i = 0; i < 8 && this.running; i++) this.tick(); }
  render(L, R, off, n, outs){
    let pos = 0;
    while (pos < n){
      if (this.running && !this.ext && this.countdown <= 0){ this.tick(); this.countdown += this.spt(); continue; }
      let todo = n - pos;
      if (this.running && !this.ext) todo = Math.max(1, Math.min(todo, Math.ceil(this.countdown)));
      this.e.process(L, R, off + pos, todo, outs);
      if (this.running && !this.ext){ this.countdown -= todo; this.clockOut(todo); }
      pos += todo; this.now += todo;
    }
  }
  clockOut(n){
    if (!this.onClock) return;
    const per = this.spt() * 8; this.clockAcc += n;
    while (this.clockAcc >= per){ this.clockAcc -= per; this.onClock(); }
  }
  // ---- the displays (STEP#/BEAT and PATTERN, two digits each) and the LEDs
  display(){
    const two = n => String(Math.max(0, Math.min(99, n))).padStart(2, '0');
    let l = '  ', r = '  ';
    const pt = this.curPat();
    if (this.isHeld(K.BPM)){ const b = String(Math.round(this.bpm)); if (b.length > 2){ l = ' ' + b[0]; r = b.slice(1); } else r = b.padStart(2, ' '); }
    else if (this.mode === 0){
      r = this.entry ? this.entry + '-' : String(pnum(this.running ? this.ppat : this.pat));
      if (this.isHeld(KP.RECORD) && !this.running) l = two(Math.floor(99 * (1 - used(this.mem) / MEM_CAP)));
      else if (this.lenArm >= 0) l = two(Math.floor(this.lenArm / PPQ) + 1);
      else if (this.running) l = two(Math.floor(this.tk / PPQ) + 1);
    } else {
      const s = this.mem.songs[this.song];
      if (this.isHeld(K.SONGNUM)){ l = '  '; r = this.entry ? this.entry + '-' : String(pnum(this.song)); }
      else {
        const st = this.running ? this.sstep : this.cursor;
        l = two(st + 1);
        r = this.entry ? this.entry + '-' : this.running ? String(pnum(this.ppat)) : st < s.steps.length ? String(pnum(s.steps[st])) : 'En';
      }
    }
    if (this.now < this.flashUntil){ l = this.flashL; r = this.flashR; }
    const tim = this.mode === 0 && this.isHeld(KP.BEFORE) ? pt.bef : pt.tim;
    const ecl = this.isHeld(K.BPM) ? this.trig : pt.ec;
    const beat = this.running ? Math.floor(this.tk / PPQ) : -1;
    const playLed = this.running && (this.tk % PPQ) < PPQ / 2;
    return {l, r, song:this.mode === 1, tim, ec:ecl, play:playLed, rec:this.recording, ext:this.ext, perc:this.perc, beat, sel:this.sel, repeat:this.mem.repeat};
  }
}
// the mixer channel of a sound (the engine's table)
const OL = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.OLINN;
function soundCh(s){ return OL.SOUNDS[s][1]; }

const API = {Machine, K, KP, PPQ, FACTORY, DRUM_KEYS, EC_GRID, EC_NAMES, TIMING, SHUF16, SHUF8, NP, emptyPattern, emptySong, defaultMemory, pnum, pidx, used, MEM_CAP, soundCh, PERC_KEY};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OLMACH = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
