/* Omega 808 sequencer: 12 basic rhythms and 4 intro/fill rhythms, each with variation A and B; basic variation
   A / AB (alternate per measure) / B; I/F variation A / B; TAP = fill in at the next measure (while stopped: play the
   intro first); auto fill-in every N measures; 1st/2nd part (up to 32 steps); swing; sample-accurate.
   Pre-scale (steps per beat): 0 = 16th notes (4), 1 = 32nd notes (8), 2 = 16th triplets (6), 3 = 8th triplets (3). */
(function (g) {
  'use strict';
  const TRACKS = ['AC','BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH','TRIG'];
  const NT = TRACKS.length, SPB = [4, 8, 6, 3], NSLOT = 32, MAXS = 32;
  function slotOf(which, n, v) { return ((which === 'fill' ? 12 : 0) + n) * 2 + (v === 'B' ? 1 : 0); }
  class Seq808 {
    constructor(sr) {
      this.sr = sr; this.bpm = 120; this.swing = 0; this.playing = false;
      this.grid = new Uint8Array(NSLOT * NT * MAXS);
      this.scale = new Uint8Array(NSLOT); this.len = new Uint8Array(NSLOT).fill(16); this.parts = new Uint8Array(NSLOT).fill(1);
      this.basic = 0; this.fill = 0; this.variation = 'A'; this.ifVar = 'A'; this.fillEvery = 0;
      this.cur = 0; this.step = 0; this.phase = 0; this.fired = false; this.measure = 0; this.ab = 0;
      this.pendingFill = false; this.intro = false; this.inFill = false;
      this.info = {step:0, part:0, rhythm:0, which:'basic', var:'A', acc:0, n:0, tracks:new Int32Array(NT)};
      this.setBank(Seq808.DEFAULT_BANK);
    }
    setBank(b) {
      this.grid.fill(0);
      const put = (which, i, r) => { if (!r) return; for (const v of ['A','B']) if (r[v]) this.setPattern(slotOf(which, i, v), r[v]); };
      (b.basic || []).forEach((r, i) => put('basic', i, r)); (b.fill || []).forEach((r, i) => put('fill', i, r));
      if (b.tempo) this.setTempo(b.tempo);
      if (b.swing != null) this.swing = b.swing;
      if (b.fillEvery != null) this.fillEvery = b.fillEvery;
    }
    setPattern(s, p) {
      this.scale[s] = p.scale || 0; this.len[s] = Math.max(1, Math.min(16, p.len || 16)); this.parts[s] = p.parts === 2 ? 2 : 1;
      for (let t = 0; t < NT; t++) { const str = (p.steps && p.steps[TRACKS[t]]) || ''; for (let k = 0; k < MAXS; k++) this.grid[(s * NT + t) * MAXS + k] = str.charAt(k) === 'x' ? 1 : 0; }
    }
    getBank() {
      const pat = s => { const steps = {}; const L = this.len[s] * this.parts[s];
        for (let t = 0; t < NT; t++) { let str = ''; for (let k = 0; k < L; k++) str += this.grid[(s * NT + t) * MAXS + k] ? 'x' : '.'; steps[TRACKS[t]] = str; }
        return {scale:this.scale[s], len:this.len[s], parts:this.parts[s], steps}; };
      const rh = (w, i) => ({A:pat(slotOf(w, i, 'A')), B:pat(slotOf(w, i, 'B'))});
      return {basic:[...Array(12).keys()].map(i => rh('basic', i)), fill:[...Array(4).keys()].map(i => rh('fill', i)), tempo:this.bpm, swing:this.swing, fillEvery:this.fillEvery};
    }
    select(o) { if (o.basic != null) this.basic = o.basic; if (o.fill != null) this.fill = o.fill; if (o.variation) this.variation = o.variation; if (o.ifVar) this.ifVar = o.ifVar; if (o.fillEvery != null) this.fillEvery = o.fillEvery; }
    setTempo(b) { this.bpm = Math.max(20, Math.min(400, b)); }
    edit(which, n, v, track, step, on) { const t = TRACKS.indexOf(track); if (t < 0 || step < 0 || step >= MAXS) return; this.grid[(slotOf(which, n, v) * NT + t) * MAXS + step] = on ? 1 : 0; }
    tap() { if (this.playing) this.pendingFill = true; else this.intro = true; }
    basicSlot() { let v = this.variation; if (v === 'AB') { v = this.ab ? 'B' : 'A'; this.ab ^= 1; } return slotOf('basic', this.basic, v); }
    play(on) {
      if (on && !this.playing) {
        this.step = 0; this.phase = 0; this.fired = false; this.measure = 0; this.ab = 0;
        if (this.intro) { this.cur = slotOf('fill', this.fill, this.ifVar); this.inFill = true; this.intro = false; } else { this.cur = this.basicSlot(); this.inFill = false; }
      }
      this.playing = !!on;
    }
    stepLen() { return this.sr * 60 / (this.bpm * SPB[this.scale[this.cur]]); }
    nextMeasure() {
      this.measure++;
      const wasFill = this.inFill; this.inFill = false;
      if (this.pendingFill) { this.pendingFill = false; this.cur = slotOf('fill', this.fill, this.ifVar); this.inFill = true; }
      else if (!wasFill && this.fillEvery > 0 && (this.measure + 1) % this.fillEvery === 0) { this.cur = slotOf('fill', this.fill, this.ifVar); this.inFill = true; }
      else this.cur = this.basicSlot();
    }
    fire(off, cb) {
      const s = this.cur, inf = this.info; inf.step = this.step; inf.part = this.step >= this.len[s] ? 1 : 0;
      inf.which = s >= 24 ? 'fill' : 'basic'; inf.rhythm = (s >> 1) - (s >= 24 ? 12 : 0); inf.var = (s & 1) ? 'B' : 'A';
      let c = 0; inf.acc = 0;
      for (let t = 0; t < NT; t++) if (this.grid[(s * NT + t) * MAXS + this.step]) { if (t === 0) inf.acc = 1; else inf.tracks[c++] = t; }
      inf.n = c;
      cb(off, inf);
    }
    // advance n samples; cb(offset, info) at each step (info is reused: copy what you keep)
    tick(n, cb) {
      if (!this.playing) return;
      let off = 0;
      for (let guard = 0; guard < 1000; guard++) {
        const L = this.stepLen();
        const sw = (this.step & 1) ? this.swing * 0.5 * L : 0;
        const target = this.fired ? L : sw;
        const need = target - this.phase;
        if (need > n - off) { this.phase += n - off; return; }
        if (need > 0) { off += need; this.phase = target; }
        if (!this.fired) { this.fired = true; this.fire(Math.min(n - 1, Math.floor(off)), cb); }
        else {
          this.phase -= L; if (this.phase < 0) this.phase = 0; this.fired = false; this.step++;
          if (this.step >= this.len[this.cur] * this.parts[this.cur]) { this.step = 0; this.nextMeasure(); }
        }
      }
    }
  }
  const P = (o) => ({scale:0, len:16, parts:1, steps:o});
  Seq808.DEFAULT_BANK = {tempo:118, swing:0, fillEvery:0, basic:[
    {A:P({AC:'x...x...x...x...', BD:'x......x..x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}),
     B:P({AC:'x...x...x...x...', BD:'x......x..x...x.', SD:'....x.......x...', CH:'x.x.x.x.x.x.x...', OH:'..............x.', CP:'....x.......x...'})},
    {A:P({BD:'x..x..x...x..x..', SD:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', CB:'..x.....x.....x.', AC:'x.......x.......'}),
     B:P({BD:'x..x..x...x..xx.', SD:'....x.......x.x.', CH:'xxxxxxxxxxxxxx..', OH:'..............x.', CB:'..x.....x...x...'})},
    {A:P({BD:'x.......x.......', CP:'....x.......x...', CH:'..x...x...x...x.', RS:'...x..x....x..x.'}),
     B:P({BD:'x.......x..x....', CP:'....x.......x..x', CH:'..x...x...x...x.', RS:'...x..x....x....', CY:'x...............'})}],
    fill:[{A:P({AC:'....x...x...xxxx', BD:'x.......x.......', SD:'....x.x.x.xxxxxx', HT:'........x.x.....', MT:'..........x.x...', LT:'............x.xx'}),
           B:P({BD:'x...x...x...x...', CP:'x.x.x.x.x.x.xxxx', CY:'x...............'})}]};
  Seq808.TRACKS = TRACKS; Seq808.SPB = SPB;
  g.Seq808 = Seq808;
  if (typeof module !== 'undefined' && module.exports) module.exports = {Seq808};
})(typeof globalThis !== 'undefined' ? globalThis : this);
