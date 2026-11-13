/* Omega Traks firmware: the Drumtraks' front panel, pattern and song memory and its sequencer, as one piece of code
   that runs on the audio thread (the app's worklet, and line-for-line in C++ in the plugin: plugins/traks/Machine.h).
   The panel sends key presses (key(id, down)) and reads back the displays and LEDs (display()).

   Memory: 100 patterns (00-99) and 100 songs (00-99).
   - A pattern: time signature (beats / note value), 1-99 measures, swing 50-75%, and its notes, recorded in real
     time at 48 ticks per quarter. A note keeps its accent, velocity and, when the VOLUME / TUNING knobs were moved
     while recording, its own volume and tuning (the Drumtraks' real-time dynamic editing).
   - A song: a list of steps; a step plays a pattern and may change the master volume or the tempo.
   SELECT PATTERN FUNCTIONS steps through: PATTERN#, RECORD MODE (ERASE INSTRUMENT, ERROR CORRECT), SWING%,
   TIME SIGNATURE, # OF MEASURES, ERASE PATTERN, COPY / APPEND TO.
   SELECT SONG FUNCTIONS steps through: SONG#, ERASE SONG, COPY / APPEND TO, BUILD MODE (PATTERN#, VOL CHANGE,
   TEMPO CHANGE, INSERT, DELETE). The group selected last decides what RUN / STOP plays: a pattern or a song.
   The keypad types numbers (two digits), ◀ ▶ step a value (in build mode: the song step), ENTER confirms.
   Displays: two 2-digit LED displays, left and right. */
'use strict';
(function (root) {
const PPQ = 48;
const MAX_EV = 4000;
const NV = 13;
// keys
const K = {SAVE:0, LOAD:1, SLOWER:2, FASTER:3, SONGFN:4, ENTER:5, D0:6, LEFT:16, RIGHT:17, PATFN:18, RUN:19, ACCENT:20, INST:21};
const PAT_FNS = ['PATTERN#', 'RECORD MODE', 'ERASE INSTRUMENT', 'ERROR CORRECT', 'SWING%', 'TIME SIGNATURE', '# OF MEASURES', 'ERASE PATTERN', 'COPY / APPEND TO'];
const SONG_FNS = ['SONG#', 'ERASE SONG', 'COPY / APPEND TO', 'BUILD MODE', 'PATTERN#', 'VOL CHANGE', 'TEMPO CHANGE', 'INSERT', 'DELETE'];
const QUANTS = [0, 4, 8, 12, 16, 24, 32, 48, 64];      // ERROR CORRECT: 1/n notes (12 = 1/8 triplets, 24 = 1/16T, 48 = 1/32T); 0 = off
const SWINGS = [50, 54, 58, 63, 67, 71, 75];
const DENS = [2, 4, 8, 16];

function emptyPattern(){ return {num:4, den:4, meas:1, swing:50, ev:[]}; }
function emptySong(){ return {steps:[]}; }
// the factory patterns 00-15: original grooves. rows: 16ths of one 4/4 measure; x = hit, A = accented hit
const FACTORY = [
  ['ROCK', 50, {BS:'x.......x.x.....', SN:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..............A.'}],
  ['FUNK', 54, {BS:'x..x..x...x..x..', SN:'....A..x.x..A...', CH:'xxxxxxxxxxxxxx.x', OH:'..............x.', RM:'.......x........'}],
  ['ELECTRO', 50, {BS:'x.....x...x.....', SN:'....A.......A...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'..x.....x...x...'}],
  ['HIP HOP', 58, {BS:'x......x..x.....', SN:'....A.......A..x', CH:'x.x.x.x.x.x.x.x.', TB:'....x.......x...'}],
  ['DISCO', 50, {BS:'x...x...x...x...', SN:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..x...x...x...x.', CP:'....x.......x...'}],
  ['NEW WAVE', 50, {BS:'x...x...x...x...', SN:'....A.......A...', CH:'xxxxxxxxxxxxxxxx', T1:'..............xx'}],
  ['SHUFFLE', 67, {BS:'x.....x.x.......', SN:'....x.......x...', RD:'x.x.x.x.x.x.x.x.', CH:'....x.......x...'}],
  ['LATIN', 50, {BS:'x..x....x..x....', RM:'..x..x....x..x..', CB:'x...x.x...x.x...', CA:'x.xxx.xxx.xxx.xx', T2:'......x.......x.'}],
  ['REGGAE', 54, {BS:'........x.......', RM:'........x.......', CH:'..x...x...x...x.', CA:'x.x.x.x.x.x.x.x.'}],
  ['BALLAD', 50, {BS:'x.........x.....', SN:'........A.......', RD:'x...x...x...x...', RM:'....x.......x...'}],
  ['POP', 50, {BS:'x.......x.x.....', SN:'....x.......x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', TB:'x.x.x.x.x.x.x.x.'}],
  ['BOOGIE', 63, {BS:'x...x...x...x...', SN:'....x.......x...', CH:'x.xxx.xxx.xxx.xx', CB:'x...............'}],
  ['MOTOWN', 54, {BS:'x..x....x..x....', SN:'....A.......A...', TB:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['TOM FILL', 50, {SN:'x.xx............', T1:'....x.xx........', T2:'........x.xx.x.x', CR:'................', BS:'x...............'}],
  ['CRASH IN', 50, {BS:'x.......x.x.....', SN:'....x.......x...', CR:'A...............', CH:'..x.x.x.x.x.x.x.'}],
  ['BREAKDOWN', 50, {BS:'x..x...........x', CB:'x..x..x...x.x...', CP:'....x.......x...', CA:'..x...x...x...x.'}],
];
const VID = {BS:0, SN:1, RM:2, T1:3, T2:4, CR:5, RD:6, CH:7, OH:8, CP:9, TB:10, CB:11, CA:12};
function factoryPattern(i){
  const [, swing, rows] = FACTORY[i], p = emptyPattern(); p.swing = swing;
  for (const [k, s] of Object.entries(rows)) for (let j = 0; j < 16 && j < s.length; j++) if (s[j] !== '.') p.ev.push({t:j * 12, v:VID[k], a:s[j] === 'A' ? 1 : 0, vel:1, lv:-1, tu:-99});
  p.ev.sort((a, b) => a.t - b.t || a.v - b.v);
  return p;
}
function defaultMemory(){
  const pats = [], songs = [];
  for (let i = 0; i < 100; i++) pats.push(i < FACTORY.length ? factoryPattern(i) : emptyPattern());
  for (let i = 0; i < 100; i++) songs.push(emptySong());
  // song 00: a short demo arrangement
  songs[0].steps = [14, 0, 0, 1, 1, 13, 2, 2, 3, 3, 15, 14].map(p => ({p, vol:-1, tempo:-1}));
  return {pats, songs};
}
const patLen = p => p.meas * p.num * (PPQ * 4 / p.den);
const beatLen = p => PPQ * 4 / p.den;

class Machine {
  constructor(engine, sr){
    this.e = engine; this.sr = sr; this.mem = defaultMemory();
    this.group = 0;                  // 0 pattern functions, 1 song functions
    this.patFn = 0; this.songFn = 0;
    this.pat = 0; this.song = 0; this.bstep = 0; this.quant = 16;
    this.entry = ''; this.target = -1;          // typed digits; ERASE / COPY targets
    this.running = false; this.recording = false; this.ppat = 0; this.next = -1; this.sstep = 0; this.tk = 0; this.countdown = 0; this.pmeas = 0;
    this.bpm = 120; this.songBpm = 0; this.ext = false;
    this.held = 0; this.accHeld = false; this.sel = 0; this.touched = 0;
    this.now = 0; this.flashL = ''; this.flashR = ''; this.flashUntil = 0;
    this.metroAlways = false; this.version = 0; this.clockAcc = 0;
    this.onHit = null; this.onClock = null; this.onStop = null; this.onStart = null;
  }
  changed(){ this.version++; }
  curPat(){ return this.mem.pats[this.running && this.group === 0 ? this.ppat : this.pat]; }
  flash(l, r, ms){ this.flashL = l; this.flashR = r; this.flashUntil = this.now + Math.round(ms * this.sr / 1000); }
  recActive(){ return this.group === 0 && this.patFn >= 1 && this.patFn <= 3; }
  // ---- keys
  key(id, down){
    if (id >= K.INST && id < K.INST + NV){ const v = id - K.INST; if (down) this.inst(v, 1); else { this.held &= ~(1 << v); this.e.release(v); } return; }
    if (id === K.ACCENT){ this.accHeld = down; return; }
    if (!down) return;
    if (id >= K.D0 && id <= K.D0 + 9){ this.digit(id - K.D0); return; }
    switch (id){
      case K.PATFN: if (this.group !== 0){ this.group = 0; this.patFn = 0; } else this.patFn = (this.patFn + 1) % PAT_FNS.length; this.entry = ''; this.target = -1; this.recording = this.running && this.recActive(); break;
      case K.SONGFN: if (this.group !== 1){ this.group = 1; this.songFn = 0; if (this.running) this.stop(); } else this.songFn = (this.songFn + 1) % SONG_FNS.length; this.entry = ''; this.target = -1; this.recording = false; break;
      case K.RUN: if (this.running) this.stop(); else this.start(); break;
      case K.LEFT: this.arrow(-1); break;
      case K.RIGHT: this.arrow(1); break;
      case K.ENTER: this.enter(); break;
    }
  }
  // an instrument key or a MIDI note: plays it, selects it for VOLUME / TUNING, and records it in record mode
  inst(v, vel){
    const acc = this.accHeld;
    this.held |= 1 << v; this.sel = v;
    this.e.trigger(v, acc, vel);
    if (this.onHit) this.onHit(v, acc, vel);
    if (this.running && this.recording && this.group === 0 && this.patFn !== 2) this.record(v, acc, vel);
  }
  record(v, acc, vel){
    const pt = this.mem.pats[this.ppat], len = patLen(pt), spt = this.spt();
    const f = this.tk - this.countdown / spt;
    const q = this.quant > 0 ? PPQ * 4 / this.quant : 1;
    let tq = Math.floor(f / q + 0.5) * q; while (tq >= len) tq -= len; if (tq < 0) tq += len;
    pt.ev = pt.ev.filter(x => !(x.t === tq && x.v === v));
    if (pt.ev.length >= MAX_EV) return;
    const dyn = (this.touched >> v) & 1, p = this.e.p, O = OTRAKS_REF || root.OTRAKS;
    const ev = {t:tq, v, a:acc ? 1 : 0, vel, lv:dyn ? p[O.vp(v, 0)] : -1, tu:dyn ? p[O.vp(v, 2)] : -99};
    // already heard: don't play it again when the sequencer reaches it this pass
    const ahead = ((tq - f) % len + len) % len;
    if (ahead > 0 && ahead <= q) ev.skip = 1;
    pt.ev.push(ev); pt.ev.sort((a, b) => a.t - b.t || a.v - b.v);
    this.changed();
  }
  // the step grid (the app's PADS page): a note on or off at tick t of pattern p
  edit(p, t, v, on, acc){
    const pt = this.mem.pats[p]; if (!pt || t < 0 || t >= patLen(pt)) return;
    pt.ev = pt.ev.filter(x => !(x.t === t && x.v === v));
    if (on && pt.ev.length < MAX_EV){ pt.ev.push({t, v, a:acc ? 1 : 0, vel:1, lv:-1, tu:-99}); pt.ev.sort((a, b) => a.t - b.t || a.v - b.v); }
    this.changed();
  }
  // the VOLUME / TUNING knobs moved for the selected sound: while recording, notes carry the new values
  knobMoved(){ if (this.recording) this.touched |= 1 << this.sel; }
  digit(d){
    const fnKey = this.group === 0 ? 'p' + this.patFn : 's' + this.songFn;
    const width = fnKey === 's6' ? 3 : 2;
    this.entry = (this.entry + d).slice(-width);
    if (this.entry.length < width) return;
    const n = parseInt(this.entry, 10);
    switch (fnKey){
      case 'p0': case 'p1': case 'p2': this.selectPattern(n); this.entry = ''; break;
      case 'p3': if (QUANTS.indexOf(n) >= 0){ this.quant = n; this.changed(); } else this.flash('Er', 'r ', 600); this.entry = ''; break;
      case 'p4': if (n >= 50 && n <= 75){ this.curPat().swing = n; this.changed(); } else this.flash('Er', 'r ', 600); this.entry = ''; break;
      case 'p5': if (n >= 1 && n <= 16) this.setSig(n, this.curPat().den); else this.flash('Er', 'r ', 600); this.entry = ''; break;
      case 'p6': if (n >= 1 && n <= 99) this.setMeas(n); else this.flash('Er', 'r ', 600); this.entry = ''; break;
      case 's0': this.song = n; this.bstep = 0; this.entry = ''; this.changed(); break;
      default: break;               // ERASE / COPY targets and build entries wait for ENTER
    }
  }
  selectPattern(n){ if (this.running && this.group === 0) this.next = n; else { this.pat = n; this.ppat = n; } this.pat = n; this.changed(); }
  setSig(num, den){
    const pt = this.curPat(); pt.num = num; pt.den = den;
    const len = patLen(pt); pt.ev = pt.ev.filter(x => x.t < len); if (this.running && this.tk >= len) this.tk = 0; this.changed();
  }
  setMeas(m){ const pt = this.curPat(); pt.meas = m; const len = patLen(pt); pt.ev = pt.ev.filter(x => x.t < len); if (this.running && this.tk >= len) this.tk = 0; this.changed(); }
  arrow(d){
    this.entry = '';
    const step = (v, lo, hi) => Math.max(lo, Math.min(hi, v + d));
    if (this.group === 0){
      const pt = this.curPat();
      switch (this.patFn){
        case 0: case 1: case 2: this.selectPattern((this.pat + d + 100) % 100); break;
        case 3: { const i = QUANTS.indexOf(this.quant); this.quant = QUANTS[step(i < 0 ? 4 : i, 0, QUANTS.length - 1)]; break; }
        case 4: { let i = 0; while (i < SWINGS.length - 1 && SWINGS[i] < pt.swing) i++; if (SWINGS[i] !== pt.swing && d < 0) i++; pt.swing = SWINGS[step(i, 0, SWINGS.length - 1)]; break; }
        case 5: { const i = DENS.indexOf(pt.den); this.setSig(pt.num, DENS[step(i < 0 ? 1 : i, 0, DENS.length - 1)]); break; }
        case 6: this.setMeas(step(pt.meas, 1, 99)); break;
        case 7: case 8: this.target = (this.tgt() + d + 100) % 100; break;
      }
    } else {
      const s = this.mem.songs[this.song];
      if (this.songFn === 0){ this.song = (this.song + d + 100) % 100; this.bstep = 0; }
      else if (this.songFn <= 2) this.target = (this.tgt() + d + 100) % 100;
      else this.bstep = step(this.bstep, 0, s.steps.length);
    }
    this.changed();
  }
  tgt(){ return this.target >= 0 ? this.target : this.group === 0 ? this.pat : this.song; }
  enter(){
    const n = this.entry.length ? parseInt(this.entry, 10) : -1;
    this.entry = '';
    if (this.group === 0){
      if (this.patFn === 7){ const t = n >= 0 ? n : this.tgt(); this.mem.pats[t] = emptyPattern(); this.target = -1; this.flash('EP', String(t).padStart(2, '0'), 900); this.changed(); }
      else if (this.patFn === 8){
        const t = n >= 0 ? n : this.tgt(), src = this.mem.pats[this.pat], dst = this.mem.pats[t];
        if (t === this.pat) return this.flash('Er', 'r ', 600);
        if (!dst.ev.length) this.mem.pats[t] = JSON.parse(JSON.stringify(src));
        else if (dst.meas + src.meas <= 99){
          // append: src's measures after dst's (in dst's time signature)
          const off = patLen(dst); dst.meas += src.meas;
          const sc = (PPQ * 4 / src.den) * src.num / ((PPQ * 4 / dst.den) * dst.num);
          for (const x of src.ev){ const t2 = off + Math.floor(x.t / sc); if (t2 < patLen(dst) && dst.ev.length < MAX_EV) dst.ev.push(Object.assign({}, x, {t:t2})); }
          dst.ev.sort((a, b) => a.t - b.t || a.v - b.v);
        } else return this.flash('Er', 'r ', 600);
        this.target = -1; this.flash('CP', String(t).padStart(2, '0'), 900); this.changed();
      }
      else if (n >= 0 && this.patFn <= 2) this.selectPattern(n);
      return;
    }
    const s = this.mem.songs[this.song];
    switch (this.songFn){
      case 0: if (n >= 0){ this.song = n; this.bstep = 0; } break;
      case 1: { const t = n >= 0 ? n : this.tgt(); this.mem.songs[t] = emptySong(); this.target = -1; this.flash('ES', String(t).padStart(2, '0'), 900); if (t === this.song) this.bstep = 0; break; }
      case 2: { const t = n >= 0 ? n : this.tgt(); if (t === this.song) return this.flash('Er', 'r ', 600);
        const d = this.mem.songs[t]; d.steps = d.steps.concat(s.steps.map(x => Object.assign({}, x))).slice(0, 255); this.target = -1; this.flash('CP', String(t).padStart(2, '0'), 900); break; }
      case 3: case 4: { const p = n >= 0 ? Math.min(99, n) : this.pat;
        if (this.bstep < s.steps.length) s.steps[this.bstep].p = p; else if (s.steps.length < 255) s.steps.push({p, vol:-1, tempo:-1}); else return this.flash('Fu', 'LL', 600);
        this.bstep = Math.min(s.steps.length, this.bstep + 1); break; }
      case 5: if (this.bstep < s.steps.length) s.steps[this.bstep].vol = n >= 0 ? Math.min(99, n) : -1; break;
      case 6: if (this.bstep < s.steps.length){ s.steps[this.bstep].tempo = n >= 0 ? Math.max(40, Math.min(250, n)) : -1; } break;
      case 7: if (s.steps.length < 255){ const p = n >= 0 ? Math.min(99, n) : (this.bstep < s.steps.length ? s.steps[this.bstep].p : this.pat); s.steps.splice(this.bstep, 0, {p, vol:-1, tempo:-1}); } break;
      case 8: if (this.bstep < s.steps.length) s.steps.splice(this.bstep, 1); break;
    }
    this.changed();
  }
  // ---- transport
  spt(){ const b = this.songBpm > 0 ? this.songBpm : this.bpm; return this.sr * 60 / b / PPQ; }
  start(){
    this.running = true; this.tk = 0; this.countdown = 0; this.pmeas = 0; this.clockAcc = 0; this.touched = 0; this.songBpm = 0; this.e.songGain = 1;
    for (const pt of this.mem.pats) for (const x of pt.ev) x.skip = 0;
    if (this.group === 1){
      const s = this.mem.songs[this.song];
      if (!s.steps.length){ this.running = false; this.flash('--', '--', 700); return; }
      this.sstep = 0; this.enterStep();
    } else { this.ppat = this.pat; this.next = -1; }
    this.recording = this.recActive();
    if (this.onStart) this.onStart();
  }
  stop(){ this.running = false; this.recording = false; this.songBpm = 0; this.e.songGain = 1; this.next = -1; if (this.onStop) this.onStop(); }
  enterStep(){
    const st = this.mem.songs[this.song].steps[this.sstep];
    this.ppat = st.p;
    if (st.vol >= 0) this.e.songGain = st.vol / 99;
    if (st.tempo > 0) this.songBpm = st.tempo;
  }
  // locate (the plugin following the DAW): a position in ticks from the start
  locate(abs){
    if (!this.running) return;
    if (this.group === 0){ const len = patLen(this.mem.pats[this.ppat]); this.tk = ((Math.floor(abs) % len) + len) % len; this.pmeas = 0; }
    else {
      const s = this.mem.songs[this.song]; let a = Math.max(0, Math.floor(abs)), i = 0;
      while (i < s.steps.length){ const len = patLen(this.mem.pats[s.steps[i].p]); if (a < len) break; a -= len; i++; }
      if (i >= s.steps.length){ this.stop(); return; }
      this.sstep = i; this.enterStep(); this.tk = a;
    }
    this.countdown = 0;
  }
  // one tick of the sequencer: the notes on it, the metronome, erasing held sounds
  tick(){
    const pt = this.mem.pats[this.ppat], len = patLen(pt), sw = pt.swing > 50 ? Math.floor((pt.swing - 50) / 50 * 12 + 0.5) : 0;
    const t = this.tk;
    const erase = this.recording && this.patFn === 2 && this.held;
    let removed = false;
    for (const x of pt.ev){
      const et = sw && x.t % 24 === 12 ? x.t + sw : x.t;
      if (et !== t) continue;
      if (erase && (this.held >> x.v) & 1){ x.del = 1; removed = true; continue; }
      if (x.skip){ x.skip = 0; continue; }
      this.e.trigger(x.v, x.a !== 0, x.vel, x.lv, x.tu);
      if (this.onHit) this.onHit(x.v, x.a !== 0, x.vel);
    }
    if (removed){ pt.ev = pt.ev.filter(x => !x.del); this.changed(); }
    const bl = beatLen(pt);
    if ((this.recording || this.metroAlways) && t % bl === 0) this.e.click(t % (bl * pt.num) === 0);
    this.tk++;
    if (this.tk >= len){
      this.tk = 0; this.pmeas = 0;
      if (this.group === 1){
        this.sstep++;
        if (this.sstep >= this.mem.songs[this.song].steps.length){ this.stop(); return; }
        this.enterStep();
      } else if (this.next >= 0){ this.ppat = this.next; this.next = -1; }
    }
  }
  // external MIDI clock: 24 per quarter = 2 ticks each
  clockTick(){ if (this.running && this.ext){ this.tick(); if (this.running) this.tick(); } }
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
    const per = this.spt() * 2; this.clockAcc += n;
    while (this.clockAcc >= per){ this.clockAcc -= per; this.onClock(); }
  }
  // ---- the displays: two 2-digit LED displays, the function LEDs, the RUN LED
  display(){
    const two = n => String(Math.max(0, Math.min(99, n))).padStart(2, '0');
    const ent = w => this.entry.padStart(w, '-');
    let l = '  ', r = '  ';
    const pt = this.curPat(), meas = this.running ? Math.floor(this.tk / (beatLen(pt) * pt.num)) + 1 : 0;
    if (this.group === 0){
      switch (this.patFn){
        case 0: case 1: case 2: l = this.running ? two(meas) : '  '; r = this.entry ? ent(2) : two(this.running ? this.ppat : this.pat); break;
        case 3: l = 'EC'; r = this.entry ? ent(2) : (this.quant ? two(this.quant) : 'oF'); break;
        case 4: l = 'Sh'; r = this.entry ? ent(2) : two(pt.swing); break;
        case 5: l = this.entry ? ent(2) : two(pt.num); r = two(pt.den); break;
        case 6: l = 'n '; r = this.entry ? ent(2) : two(pt.meas); break;
        case 7: l = 'EP'; r = this.entry ? ent(2) : two(this.tgt()); break;
        case 8: l = 'CP'; r = this.entry ? ent(2) : two(this.tgt()); break;
      }
    } else {
      const s = this.mem.songs[this.song], st = s.steps[this.bstep], end = this.bstep >= s.steps.length;
      switch (this.songFn){
        case 0: l = this.running ? two(this.sstep + 1) : '  '; r = this.entry ? ent(2) : two(this.song); break;
        case 1: l = 'ES'; r = this.entry ? ent(2) : two(this.tgt()); break;
        case 2: l = 'CP'; r = this.entry ? ent(2) : two(this.tgt()); break;
        case 3: case 4: case 7: case 8: l = this.running ? two(this.sstep + 1) : end ? 'En' : two(this.bstep + 1); r = this.entry ? ent(2) : this.running ? two(this.ppat) : end ? '--' : two(st.p); break;
        case 5: l = end ? 'En' : two(this.bstep + 1); r = this.entry ? ent(2) : end || st.vol < 0 ? '--' : two(st.vol); break;
        case 6: { const t = this.entry ? ent(3) : end || st.tempo < 0 ? '---' : String(st.tempo).padStart(3, ' '); l = ' ' + t[0]; r = t.slice(1); break; }
      }
    }
    if (this.now < this.flashUntil){ l = this.flashL; r = this.flashR; }
    let lp = 0, ls = 0;
    if (this.group === 0){ lp = 1 << this.patFn; if (this.patFn === 2 || this.patFn === 3) lp |= 2; }
    else { ls = 1 << this.songFn; if (this.songFn >= 4) ls |= 8; }
    const beat = this.running ? Math.floor(this.tk / beatLen(pt)) : -1;
    const runLed = this.running && (this.tk % beatLen(pt)) < beatLen(pt) / 2;
    return {l, r, lp, ls, run:runLed, rec:this.recording, beat, sel:this.sel};
  }
}
const OTRAKS_REF = typeof module !== 'undefined' && module.exports ? require('./engine.js') : null;

const API = {Machine, K, PAT_FNS, SONG_FNS, QUANTS, SWINGS, PPQ, FACTORY, emptyPattern, emptySong, defaultMemory, patLen};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OTMACH = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
