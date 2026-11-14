// Yamaha REV7 (1985) digital reverberator: a line-for-line JavaScript port of the Omega REV7 engine
// (plugins/dsp/Rev7.h, Rack.h, Common.h and the parameter / factory-preset tables of plugins/src/Rev7.cpp at the
// omega-rev7 tag). Same math in the same order of operations, so it renders the same samples as the C++.
//
// Fixed panel: input knob at 8 (unity), MONO on, mix 100 % wet, output 1.0, no mute / bypass / phase; 120 BPM.
// Loads as a classic script (also inside an AudioWorklet) and in Node: exports {P, PARAMS, PROG_NAMES, Rev7Full}
// on globalThis.DXR / module.exports. A C++ twin with the same interface lives in plugins/seven/Rev7Full.h.
(function(root){ 'use strict';

  const PI = Math.PI;                 // 3.14159265358979323846
  const FS = 31250;                   // the REV7's sampling rate
  const jround = x => Math.floor(x + .5);   // JavaScript's Math.round, as in Common.h

  // ---- parameters ------------------------------------------------------------------------------------------
  const KEYS = ['prog', 'rt', 'hi', 'lo', 'diff', 'idly', 'fdly', 'flev', 'live', 'mode', 'size', 'dlyL', 'dlyR', 'dlev',
    'fb', 'hdamp', 'mfreq', 'mdep', 'mdly', 'amdep', 'eq', 'eqlf', 'eqlg', 'eqmf', 'eqmg', 'eqhf', 'eqhg', 'circ'];
  const P = {};
  KEYS.forEach((k, i) => { P[k.toUpperCase()] = i; });
  P.PCOUNT = KEYS.length;

  const PROG_NAMES = [
    'LARGE HALL', 'SMALL HALL', 'VOCAL PLATE', 'PERCUSSION PLATE', 'EARLY REF. 1', 'EARLY REF. 2', 'DELAY L,R', 'STEREO ECHO',
    'STEREO FLANGE', 'REVERB FLANGE', 'CHORUS A', 'CHORUS B', 'STEREO PHASING', 'TREMOLO', 'SYMPHONIC', 'REVERB ROOM',
    'ECHO ROOM', 'STRINGS', 'ELECTRIC BASS A', 'ELECTRIC BASS B', 'KICK', 'SNARE', 'GATE REVERB', 'REVERSE GATE',
    'PIANO', 'ORGAN', 'BRASS', 'GUITAR', 'HANDCLAPS', 'LIVE REFERENCE'];

  const PARAMS = [
    {key: 'prog', name: 'Program', min: 1, max: 30, def: 1},
    {key: 'rt', name: 'Rev time', min: .3, max: 10, def: 2.6},
    {key: 'hi', name: 'High RT ratio', min: .1, max: 1, def: .3},
    {key: 'lo', name: 'Low RT ratio', min: .1, max: 2.4, def: 1.2},
    {key: 'diff', name: 'Diffusion', min: 0, max: 10, def: 5},
    {key: 'idly', name: 'Initial delay', min: .1, max: 100, def: 30},
    {key: 'fdly', name: '1st ref delay', min: .1, max: 100, def: 10},
    {key: 'flev', name: '1st ref level', min: 0, max: 1, def: 0},
    {key: 'live', name: 'Liveness', min: 0, max: 10, def: 5},
    {key: 'mode', name: 'E/R mode', min: 1, max: 6, def: 1},
    {key: 'size', name: 'Room size', min: .1, max: 20, def: 2},
    {key: 'dlyL', name: 'L delay', min: .1, max: 900, def: 100},
    {key: 'dlyR', name: 'R delay', min: .1, max: 900, def: 200},
    {key: 'dlev', name: 'Delay level', min: 0, max: 1, def: 1},
    {key: 'fb', name: 'Feedback gain', min: 0, max: .99, def: .6},
    {key: 'hdamp', name: 'High damp', min: 0, max: 10, def: 5},
    {key: 'mfreq', name: 'Mod freq', min: .1, max: 20, def: 1.1},
    {key: 'mdep', name: 'Mod depth', min: 0, max: 1, def: .5},
    {key: 'mdly', name: 'Mod delay', min: .1, max: 100, def: 1.2},
    {key: 'amdep', name: 'AM depth', min: 0, max: 1, def: .4},
    {key: 'eq', name: 'EQ on', min: 0, max: 1, def: 0},
    {key: 'eqlf', name: 'EQ: Lo freq', min: 50, max: 700, def: 100},
    {key: 'eqlg', name: 'EQ: Lo level', min: -15, max: 15, def: 0},
    {key: 'eqmf', name: 'EQ: Mid freq', min: 350, max: 7000, def: 1000},
    {key: 'eqmg', name: 'EQ: Mid level', min: -15, max: 15, def: 0},
    {key: 'eqhf', name: 'EQ: Hi freq', min: 2000, max: 20000, def: 8000},
    {key: 'eqhg', name: 'EQ: Hi level', min: -15, max: 15, def: 0},
    {key: 'circ', name: 'Vintage converters', min: 0, max: 1, def: 1},
  ];

  // factory presets 1-30 (the service manual's ROM chart, as in Rev7.cpp)
  const rev = (rt, id, hi, lo, df, fd = .1, fl = 0) => ({rt, idly: id, fdly: fd, flev: fl, hi, lo, diff: df});
  const er = (live, id, fl, mode, size, df) => ({live, idly: id, fdly: .1, flev: fl, mode, size, diff: df});
  const FACTORY = [null,
    rev(2.6, 30, .3, 1.2, 5, 10),
    rev(2.0, 20, .4, 1.0, 5, 10),
    rev(2.4, 45, .3, 1.0, 5, 10),
    rev(2.0, 10, .5, 1.2, 5, 10),
    er(5, 10, 0, 1, 2.0, 5),
    er(5, 10, 0, 1, 2.0, 5),
    {idly: .1, fdly: .1, dlyL: 100, dlyR: 200, dlev: 1},
    {fb: .6, idly: .1, dlyL: 170, dlyR: 178, hdamp: 9},
    {fb: .35, idly: .1, mfreq: 2.5, mdep: .5, mdly: 1.2},
    {rt: 2.5, idly: .1, mfreq: 1.1, mdep: .8, mdly: 1.2},
    {idly: .1, mfreq: .2, mdep: .5, amdep: .4},
    {idly: .1, mfreq: .6, mdep: .5, amdep: .1},
    {idly: .1, mfreq: 1.1, mdep: .5, mdly: 1.0},
    {idly: .1, mfreq: 5.0, mdep: .5},
    {idly: .1, mfreq: .6, mdep: .5},
    rev(1.6, 25, .2, 1.0, 5),
    rev(1.8, 16, .3, 1.0, 5),
    rev(2.2, 13, .3, 1.0, 5),
    er(5, 12, .65, 5, 2.0, 5),
    er(3, 12, .68, 5, 2.0, 5),
    er(1, 12, .87, 5, 2.0, 5),
    rev(1.2, 10, .8, 1.0, 5),
    er(5, 20, 0, 3, 1.6, 5),
    er(5, 25, 0, 4, 2.4, 5),
    er(3, 12, .72, 6, 2.0, 5),
    er(4, 10, .61, 1, 2.0, 7),
    er(4, 12, .61, 3, .9, 5),
    er(5, 5, .69, 6, 1.5, 5),
    rev(.4, .1, .1, 2.0, 5),
    er(5, 20, 0, 2, 2.5, 5),
  ];

  // ---- building blocks (Common.h) ------------------------------------------------------------------------------
  class DLine {
    constructor() { this.b = new Float32Array(4); this.mask = 3; this.w = 0; }
    init(n) { let m = 1; while (m < n + 4) m <<= 1; this.b = new Float32Array(m); this.mask = m - 1; this.w = 0; }
    push(x) { this.w = (this.w + 1) & this.mask; this.b[this.w] = x; }
    at(d) { return this.b[(this.w - d) & this.mask]; }
    lerp(d) { const i = Math.floor(d); const f = d - i; const a = this.b[(this.w - i) & this.mask], c = this.b[(this.w - i - 1) & this.mask]; return a + (c - a) * f; }
    clear() { this.b.fill(0); }
  }

  class BQ {
    constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
    run(x) { const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
    setCoefs(o) { this.b0 = o.b0; this.b1 = o.b1; this.b2 = o.b2; this.a1 = o.a1; this.a2 = o.a2; }   // n.keepState(*this); *this = n
    static lp(fs, f, q) {
      const w = 2 * PI * Math.min(f, fs * .45) / fs, al = Math.sin(w) / (2 * q), cw = Math.cos(w), a0 = 1 + al;
      const r = new BQ(); r.b0 = (1 - cw) / 2 / a0; r.b1 = (1 - cw) / a0; r.b2 = (1 - cw) / 2 / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al) / a0; return r;
    }
  }

  // LPN with an even order: n/2 biquads. Like the C++, init() keeps the biquads' state (it is never cleared).
  class LPN {
    constructor() { this.b = []; this.cur = -1; this.fs = 48000; }
    init(fs, f, n, qs) {
      this.fs = fs; this.n = n; this.qs = qs; this.cur = -1;
      if (f === this.cur) return; this.cur = f;
      const nb = [];
      for (let k = 0; k < (n >> 1); ++k) {
        const q = BQ.lp(fs, f, k < qs.length ? qs[k] : .707);
        if (k < this.b.length) { q.z1 = this.b[k].z1; q.z2 = this.b[k].z2; }
        nb.push(q);
      }
      this.b = nb; this.q0 = nb[0]; this.q1 = nb[1]; this.q2 = nb[2];
    }
    run(x) { return this.q2.run(this.q1.run(this.q0.run(x))); }   // order 6 only (all this unit uses)
  }

  class Shelf {   // first-order shelf by the bilinear transform: zero at fz, pole at fp
    constructor() { this.b0 = 1; this.b1 = 0; this.a1 = 0; this.x1 = 0; this.y1 = 0; }
    set(fs, fz, fp) {
      const k = 2 * fs, wz = 2 * PI * fz, wp = 2 * PI * fp;
      const n0 = 1 + k / wz, n1 = 1 - k / wz, d0 = 1 + k / wp, d1 = 1 - k / wp;
      this.b0 = n0 / d0; this.b1 = n1 / d0; this.a1 = d1 / d0;
    }
    run(x) { const y = this.b0 * x + this.b1 * this.x1 - this.a1 * this.y1; this.x1 = x; this.y1 = y; return y; }
    clear() { this.x1 = this.y1 = 0; }
  }

  function peak(fs, f, q, db) {
    const A = Math.pow(10, db / 40), w = 2 * PI * Math.min(f, fs * .45) / fs, al = Math.sin(w) / (2 * q), cw = Math.cos(w), a0 = 1 + al / A;
    const r = new BQ(); r.b0 = (1 + al * A) / a0; r.b1 = -2 * cw / a0; r.b2 = (1 - al * A) / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al / A) / a0; return r;
  }

  function tailOf(ms, fb) {
    return Math.min(20.0, ms / 1000 * (fb > .01 ? Math.log(1e-4) / Math.log(Math.min(.98, Math.abs(fb))) : 1) + .3);
  }

  // ---- the programs' hidden part ----------------------------------------------------------------------------
  const REV = 0, ER = 1, DLY = 2, ECHO = 3, FLANGE = 4, RFLANGE = 5, CHORUS = 6, PHASE = 7, TREM = 8, SYMPH = 9;
  const PROGS = [
    [REV, 1, 9000, 0],
    [REV, 1.0, 9000, 0], [REV, .62, 9500, 0], [REV, .5, 11000, 1], [REV, .42, 11500, 1],
    [ER, 1, 12000, 0], [ER, 1, 12000, 1],
    [DLY, 1, 12000, 0], [ECHO, 1, 12000, 0],
    [FLANGE, 1, 12000, 0], [RFLANGE, .8, 10000, 0], [CHORUS, 1, 12000, 0], [CHORUS, 1, 12000, 1],
    [PHASE, 1, 12000, 0], [TREM, 1, 12000, 0], [SYMPH, 1, 12000, 0],
    [REV, .55, 10000, 0], [REV, .6, 8500, 0], [REV, .9, 10500, 0],
    [ER, 1, 9000, 2], [ER, 1, 8000, 3], [ER, 1, 7000, 4],
    [REV, .45, 11500, 1],
    [ER, 1, 12000, 5], [ER, 1, 12000, 6], [ER, 1, 11000, 7], [ER, 1, 10000, 8], [ER, 1, 11000, 9], [ER, 1, 11500, 10],
    [REV, .4, 12000, 1], [ER, 1, 12000, 11],
  ];
  const progOf = n => PROGS[n < 1 || n > 30 ? 0 : n];

  const TANK_BASE = [1061, 1277, 1489, 1667, 1847, 2011, 2203, 2399];
  const IN_AP = [89, 67, 233, 173];
  const LPN_QS = [.518, .707, 1.932];

  // the six E/R modes' reflection patterns: [mode][channel] -> {t, g} (18 each), generated once
  const PAT = (() => {
    const pat = [];
    for (let m = 0; m < 6; ++m) {
      pat.push([]);
      for (let c = 0; c < 2; ++c) {
        let s = (0x5eed + (m * 977 + c * 131)) >>> 0;
        const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s >>> 8) / 16777216.0; };
        const t = new Float64Array(18), g = new Float64Array(18);
        for (let k = 0; k < 18; ++k) {
          const x = (k + .25 + .5 * rnd()) / 18; let tt = 0;
          switch (m) {
            case 0: tt = 1.5 + 28 * Math.pow(x, 1.3); break;
            case 1: tt = 3 + 58 * Math.pow(x, 1.15); break;
            case 2: tt = 1 + 44 * rnd(); break;
            case 3: tt = 2 + 48 * x; break;
            case 4: tt = .7 + 17 * x; break;
            case 5: tt = 4.1 * (((k / 3) | 0) + 1) * (1 + .035 * c) + (k % 3) * .37 + .15 * rnd(); break;
          }
          t[k] = tt;
          const sg = rnd() < .5 ? -1 : 1; g[k] = sg * (.75 + .25 * rnd());
        }
        for (let i = 0; i < 18; ++i) for (let j = i + 1; j < 18; ++j) if (t[j] < t[i]) {
          let q = t[i]; t[i] = t[j]; t[j] = q; q = g[i]; g[i] = g[j]; g[j] = q;
        }
        pat[m].push({t, g});
      }
    }
    return pat;
  })();

  // ---- the unit ------------------------------------------------------------------------------------------------
  class Rev7Full {
    constructor() {
      this.vals = new Float64Array(P.PCOUNT);
      for (let i = 0; i < P.PCOUNT; ++i) this.vals[i] = PARAMS[i].def;
      this.fs = 48000; this.quiet = 1e12; this.asleep = true; this.ready = false;
      // Rev7 members
      this.kind = REV; this.scale = 1; this.bw = 9000; this.aux = 0;
      this.num = 1; this.mode = 1;
      this.rt = 2.6; this.hiR = .3; this.loR = 1.2; this.diff = 5; this.live = 5; this.size = 2;
      this.circuit = true; this.eqOn = false;
      this.inGain = 1; this.bwA = .8; this.apG = .55; this.lo500 = .1; this.lo4k = .5; this.dampA = .5;
      this.len = new Int32Array(8); this.gM = new Float64Array(8); this.gH = new Float64Array(8); this.gL = new Float64Array(8);
      this.line = new DLine(); this.loop = [new DLine(), new DLine()]; this.mod = [new DLine(), new DLine()];
      this.fdn = []; for (let k = 0; k < 8; ++k) this.fdn.push(new DLine());
      this.inAp = []; for (let k = 0; k < 4; ++k) this.inAp.push(new DLine());
      this.erAp = [[new DLine(), new DLine()], [new DLine(), new DLine()]]; this.outAp = [new DLine(), new DLine()];
      this.aaf = new LPN(); this.rcf = [new LPN(), new LPN()];
      this.pre = new Shelf(); this.de = [new Shelf(), new Shelf()];
      this.eqb = [new BQ(), new BQ(), new BQ()]; this.aperture = [new BQ(), new BQ()];
      this.acc = 0; this.xPrev = 0; this.y00 = 0; this.y01 = 0; this.y10 = 0; this.y11 = 0;
      this.nTap = 0; this.tapD0 = new Int32Array(18); this.tapD1 = new Int32Array(18); this.tapG0 = new Float64Array(18); this.tapG1 = new Float64Array(18);
      this.bandL = new Float64Array(8); this.bandH = new Float64Array(8); this.damp = new Float64Array(2);
      this.bwz = 0; this.lfo = 0; this.lfo2 = 0; this.lfo3 = 0; this.tank = 0;
      this.r = new Float64Array(8);
      this.out = new Float64Array(2);   // tick's / reverb's outputs
      // integer values derived from the parameters (what tick() would compute from them each sample)
      this.inN = new Int32Array(4); this.oa = 0; this.ob = 0; this.revPre = 0;
      this.idN = 1; this.fdN = 1; this.dlyN = new Int32Array(2); this.echoN = new Int32Array(2);
    }
    v(i) { return this.vals[i]; }

    // RackUnit::prepare
    prepare(sr) { this.fs = sr; this.build(); this.cfg(); this.update(); this.reset(); this.quiet = 1e12; this.asleep = true; this.ready = true; }
    changed() { if (!this.ready) return; this.cfg(); this.update(); }
    set(p, v) {
      const d = PARAMS[p]; if (!d) return;
      v = +v; if (!(v === v)) return;
      this.vals[p] = v < d.min ? d.min : v > d.max ? d.max : v;
      this.changed();
    }
    get(p) { return this.vals[p]; }
    loadFactory(prog) {
      prog = Math.max(1, Math.min(30, Math.trunc(prog) || 1));
      for (let i = 0; i < P.PCOUNT; ++i) this.vals[i] = PARAMS[i].def;
      this.vals[P.PROG] = prog;
      const f = FACTORY[prog];
      for (const k in f) this.vals[KEYS.indexOf(k)] = f[k];
      this.changed();
    }

    build() {
      const fs = this.fs;
      this.line.init(Math.trunc(FS * 2.6));
      for (let c = 0; c < 2; ++c) { this.loop[c].init(Math.trunc(FS * .5)); this.mod[c].init(Math.trunc(FS * .06)); this.erAp[c][0].init(160); this.erAp[c][1].init(160); this.outAp[c].init(400); }
      for (let k = 0; k < 4; ++k) this.inAp[k].init(400);
      for (let k = 0; k < 8; ++k) this.fdn[k].init(4096);
      for (const f of [this.aaf, this.rcf[0], this.rcf[1]]) f.init(fs, 12000, 6, LPN_QS);
      this.pre.set(fs, 3200, 12000); this.de[0].set(fs, 12000, 3200); this.de[1].set(fs, 12000, 3200);
      this.aperture[0] = peak(fs, 11000, .7, 2.2); this.aperture[1] = peak(fs, 11000, .7, 2.2);
    }
    reset() {
      const fs = this.fs;
      this.line.clear(); for (const d of this.loop) d.clear(); for (const d of this.mod) d.clear(); for (const d of this.fdn) d.clear(); for (const d of this.inAp) d.clear();
      for (const c of this.erAp) for (const d of c) d.clear();
      for (const d of this.outAp) d.clear();
      this.aaf.init(fs, 12000, 6, LPN_QS); for (const f of this.rcf) f.init(fs, 12000, 6, LPN_QS);
      this.pre.clear(); for (const s of this.de) s.clear(); for (const b of this.eqb) b.z1 = b.z2 = 0; for (const b of this.aperture) b.z1 = b.z2 = 0;
      this.bandL.fill(0); this.bandH.fill(0);
      this.damp.fill(0);
      this.bwz = 0; this.acc = 0; this.xPrev = 0; this.y00 = this.y01 = this.y10 = this.y11 = 0;
    }
    tail() {
      const v = this.vals;
      let t = .3 + v[P.IDLY] / 1000;
      switch (this.kind) {
        case REV: case RFLANGE: t += v[P.RT] * Math.max(1.0, v[P.LO]) * 1.15; break;
        case ER: t += .07 * v[P.SIZE] + .3; break;
        case DLY: t += Math.max(v[P.DLYL], v[P.DLYR]) / 1000; break;
        case ECHO: t += tailOf(Math.max(v[P.DLYL], v[P.DLYR]), v[P.FB]); break;
        case FLANGE: t += tailOf(v[P.MDLY] + 4, v[P.FB]); break;
        default: t += .2; break;
      }
      return Math.min(30.0, t + v[P.FDLY] / 1000);
    }
    cfg() {
      const v = this.vals;
      this.num = Math.trunc(v[P.PROG]); const pr = progOf(this.num);
      this.kind = pr[0]; this.scale = pr[1]; this.bw = pr[2]; this.aux = pr[3];
      this.rt = v[P.RT]; this.hiR = v[P.HI]; this.loR = v[P.LO]; this.diff = v[P.DIFF]; this.live = v[P.LIVE];
      this.mode = Math.max(1, Math.min(6, Math.trunc(v[P.MODE]))); this.size = v[P.SIZE];
      this.circuit = v[P.CIRC] >= .5;
    }
    update() {
      const v = this.vals, fs = this.fs;
      this.eqOn = v[P.EQ] >= .5;
      const fk = [P.EQLF, P.EQMF, P.EQHF], gk = [P.EQLG, P.EQMG, P.EQHG];
      for (let k = 0; k < 3; ++k) this.eqb[k].setCoefs(peak(fs, v[fk[k]], k === 1 ? 1.0 : .8, v[gk[k]]));
      const inPos = 8; this.inGain = inPos <= 0 ? 0 : Math.pow(10, (inPos - 8) * 6 / 20);   // panel "in" knob at 8
      this.bwA = 1 - Math.exp(-2 * PI * this.bw / FS);
      for (let k = 0; k < 8; ++k) {
        const len = this.len[k] = Math.max(40, Math.trunc(jround(TANK_BASE[k] * this.scale)));
        const g = t => Math.pow(10, -3 * len / (Math.max(.05, t) * FS));
        this.gM[k] = g(this.rt); this.gH[k] = g(this.rt * this.hiR); this.gL[k] = Math.min(.9995, g(this.rt * this.loR));
      }
      this.lo500 = 1 - Math.exp(-2 * PI * 500 / FS); this.lo4k = 1 - Math.exp(-2 * PI * 4000 / FS);
      const plate = this.aux > .5 && this.kind === REV ? .08 : 0;
      this.apG = this.diff <= 0 ? .1 : .34 + .041 * this.diff + plate;
      if (this.kind === REV) this.setRevER(); else if (this.kind === ER) this.setER();
      const hd = v[P.HDAMP]; this.dampA = 1 - Math.exp(-2 * PI * (12000 / (1 + .35 * hd)) / FS);
      // integer delays tick() / reverb() compute from the parameters (cached here, same values)
      const sc = .6 + .4 * this.scale;
      for (let k = 0; k < 4; ++k) this.inN[k] = Math.trunc(jround(IN_AP[k] * sc));
      this.oa = Math.trunc(jround(311 * sc)); this.ob = Math.trunc(jround(257 * sc));
      this.revPre = Math.trunc(jround(.012 * this.scale * FS));
      this.idN = Math.max(1, Math.trunc(jround(v[P.IDLY] / 1000 * FS)));
      this.fdN = Math.max(1, Math.trunc(jround(v[P.FDLY] / 1000 * FS)));
      this.dlyN[0] = Math.max(1, Math.trunc(jround(v[P.DLYL] / 1000 * FS)));
      this.dlyN[1] = Math.max(1, Math.trunc(jround(v[P.DLYR] / 1000 * FS)));
      this.echoN[0] = Math.max(1, Math.trunc(jround(Math.min(450.0, v[P.DLYL]) / 1000 * FS)));
      this.echoN[1] = Math.max(1, Math.trunc(jround(Math.min(450.0, v[P.DLYR]) / 1000 * FS)));
    }
    setER() {
      const m = this.mode - 1, live = this.live, a = (10 - live) * .55 + .3;
      const vr = 1 + .06 * Math.sin(this.aux * 1.7);
      for (let c = 0; c < 2; ++c) {
        const tapD = c ? this.tapD1 : this.tapD0, tapG = c ? this.tapG1 : this.tapG0, p = PAT[m][c];
        let ss = 0;
        for (let k = 0; k < 18; ++k) {
          const x = k / 17.0; let env;
          if (m === 3) env = Math.pow(x + 1.0 / 18, .5 + (10 - live) * .2);
          else if (m === 2) env = Math.exp(-x * a * .35);
          else env = Math.exp(-x * a);
          tapD[k] = Math.max(1, Math.trunc(jround(p.t[k] * vr * this.size * FS / 1000)));
          tapG[k] = p.g[k] * env; ss += tapG[k] * tapG[k];
        }
        const nrm = .75 / Math.sqrt(Math.max(1e-9, ss)); for (let k = 0; k < 18; ++k) tapG[k] *= nrm;
      }
      this.nTap = 18;
    }
    setRevER() {
      const tL = [7.1, 13.9, 22.7], tR = [9.3, 17.1, 26.3], gg = [.42, -.33, .26];
      for (let k = 0; k < 3; ++k) {
        this.tapD0[k] = Math.trunc(jround(tL[k] * this.scale * FS / 1000)); this.tapD1[k] = Math.trunc(jround(tR[k] * this.scale * FS / 1000));
        this.tapG0[k] = gg[k]; this.tapG1[k] = -gg[k] * .9;
      }
      this.nTap = 3;
    }

    // ---- the DSP, one sample at 31.25 kHz ----
    reverb(x) {   // -> this.out[0], this.out[1]
      this.bwz += this.bwA * (x - this.bwz); let d = this.bwz;
      const apG = this.apG, inAp = this.inAp, inN = this.inN;
      for (let k = 0; k < 4; ++k) {
        const dl = inAp[k], vv = dl.at(inN[k]); const w = d + apG * vv; dl.push(w); d = vv - apG * w;
      }
      this.tank += 2 * PI * .73 / FS; if (this.tank > 2 * PI) this.tank -= 2 * PI;
      const r = this.r, fdn = this.fdn, len = this.len, tank = this.tank;
      for (let k = 0; k < 8; ++k) r[k] = (k === 0 || k === 5) ? fdn[k].lerp(len[k] - 1 + 5 * Math.sin(tank + k)) : fdn[k].at(len[k]);
      let sum = 0; for (let k = 0; k < 8; ++k) sum += r[k]; sum *= .25;
      const lo500 = this.lo500, lo4k = this.lo4k, bL = this.bandL, bH = this.bandH, gL = this.gL, gM = this.gM, gH = this.gH;
      for (let k = 0; k < 8; ++k) {
        const y = r[k] - sum + (k & 1 ? -d : d) * .5;
        const l = bL[k] += lo500 * (y - bL[k]); const h = bH[k] += lo4k * (y - bH[k]);
        const lo = l, mid = h - l, hi = y - h;
        fdn[k].push(gL[k] * lo + gM[k] * mid + gH[k] * hi);
      }
      let oL = .42 * (r[0] - r[2] + r[4] + r[6] * .8);
      let oR = .42 * (r[1] + r[3] - r[5] + r[7] * .8);
      const g = apG * .7;
      { const dl = this.outAp[0], vv = dl.at(this.oa); const w = oL + g * vv; dl.push(w); oL = vv - g * w; }
      { const dl = this.outAp[1], vv = dl.at(this.ob); const w = oR + g * vv; dl.push(w); oR = vv - g * w; }
      this.out[0] = oL; this.out[1] = oR;
    }
    lfoStep(ph, hz) { ph += 2 * PI * hz / FS; if (ph > 2 * PI) ph -= 2 * PI; return ph; }
    tick(x) {   // -> this.out[0], this.out[1]
      const v = this.vals, line = this.line, out = this.out;
      line.push(x);
      const id = this.idN;
      const xi = line.at(id);
      let oL = 0, oR = 0;
      const mf = v[P.MFREQ], md = v[P.MDEP] * .004 * FS;
      switch (this.kind) {
        case REV: {
          const n = this.nTap, d0 = this.tapD0, d1 = this.tapD1, g0 = this.tapG0, g1 = this.tapG1;
          for (let k = 0; k < n; ++k) { oL += line.at(id + d0[k]) * g0[k]; oR += line.at(id + d1[k]) * g1[k]; }
          this.reverb(line.at(id + this.revPre)); oL += out[0]; oR += out[1]; break;
        }
        case ER: {
          const n = this.nTap, d0 = this.tapD0, d1 = this.tapD1, g0 = this.tapG0, g1 = this.tapG1;
          for (let k = 0; k < n; ++k) { oL += line.at(id + d0[k]) * g0[k]; oR += line.at(id + d1[k]) * g1[k]; }
          const g = this.diff * .068;
          oL = ap(this.erAp[0][1], 79, g, ap(this.erAp[0][0], 113, g, oL)); oR = ap(this.erAp[1][1], 67, g, ap(this.erAp[1][0], 97, g, oR));
          break;
        }
        case DLY: {
          const lev = v[P.DLEV];
          oL = line.at(id + this.dlyN[0]) * lev;
          oR = line.at(id + this.dlyN[1]) * lev;
          break;
        }
        case ECHO: {
          const fb = v[P.FB], damp = this.damp, dampA = this.dampA;
          for (let c = 0; c < 2; ++c) {
            const lp = this.loop[c];
            const y = lp.at(this.echoN[c]); damp[c] += dampA * (y - damp[c]);
            lp.push(xi + fb * damp[c]); if (c) oR = y; else oL = y;
          }
          break;
        }
        case FLANGE: case RFLANGE: {
          let sL = xi, sR = xi;
          if (this.kind === RFLANGE) { this.reverb(xi); sL = out[0]; sR = out[1]; }
          const ph = this.lfo = this.lfoStep(this.lfo, mf), fb = this.kind === RFLANGE ? .3 : v[P.FB];
          const base = v[P.MDLY] / 1000 * FS;
          const dL = Math.max(1.0, base + md * Math.sin(ph)), dR = Math.max(1.0, base - md * Math.sin(ph));
          const yL = this.mod[0].lerp(dL), yR = this.mod[1].lerp(dR);
          this.mod[0].push(sL + fb * yL); this.mod[1].push(sR + fb * yR);
          oL = .7 * (sL + yL); oR = .7 * (sR + yR); break;
        }
        case CHORUS: {
          const ph = this.lfo = this.lfoStep(this.lfo, mf), am = v[P.AMDEP], base = (this.aux > .5 ? 9.0 : 12.5) / 1000 * FS;
          const m0 = this.mod[0]; m0.push(xi);
          const yL = m0.lerp(base + md * Math.sin(ph)), yR = m0.lerp(base + md * Math.sin(ph + PI));
          const gL2 = 1 - am * .5 * (1 + Math.sin(ph + PI / 2)), gR2 = 1 - am * .5 * (1 + Math.sin(ph - PI / 2));
          oL = .7 * (xi + yL * gL2); oR = .7 * (xi + yR * gR2); break;
        }
        case PHASE: {
          const ph = this.lfo = this.lfoStep(this.lfo, mf), base = Math.min(8.0, v[P.MDLY]) / 1000 * FS;
          const m0 = this.mod[0]; m0.push(xi);
          const fixed = m0.lerp(Math.max(.5, base)), centre = m0.lerp(Math.max(.5, base + md * Math.sin(ph)));
          oL = .5 * (fixed + centre) + .35 * xi; oR = .5 * (centre - fixed) + .35 * xi; break;
        }
        case TREM: {
          const ph = this.lfo = this.lfoStep(this.lfo, mf); const m0 = this.mod[0]; m0.push(xi);
          oL = oR = m0.lerp(.0055 * FS + md * Math.sin(ph)); break;
        }
        case SYMPH: {
          const p1 = this.lfo = this.lfoStep(this.lfo, mf), p2 = this.lfo2 = this.lfoStep(this.lfo2, mf * 1.37), p3 = this.lfo3 = this.lfoStep(this.lfo3, mf * .71);
          const m0 = this.mod[0]; m0.push(xi);
          const a = m0.lerp(.007 * FS + md * Math.sin(p1)), b = m0.lerp(.011 * FS + md * Math.sin(p2 + 2)), c = m0.lerp(.015 * FS + md * Math.sin(p3 + 4));
          oL = .55 * (xi + a - .6 * b + .7 * c); oR = .55 * (xi - .6 * a + b + .7 * c); break;
        }
      }
      const fl = v[P.FLEV];
      if (fl > 0) { const r = line.at(this.fdN) * fl; oL += r; oR += r; }
      out[0] = oL; out[1] = oR;
    }

    // ---- host-rate wrapper: the converters (Rev7::run) ----
    run(input, outL, outR, off, n) {
      const FSr = FS, ratio = FSr / this.fs, ret = 1, adcScale = .7;
      const circuit = this.circuit, eqOn = this.eqOn, inGain = this.inGain, out = this.out;
      const e0 = this.eqb[0], e1 = this.eqb[1], e2 = this.eqb[2];
      const aaf = this.aaf, pre = this.pre, rcf0 = this.rcf[0], rcf1 = this.rcf[1], de0 = this.de[0], de1 = this.de[1], ap0 = this.aperture[0], ap1 = this.aperture[1];
      let acc = this.acc, xPrev = this.xPrev, y00 = this.y00, y01 = this.y01, y10 = this.y10, y11 = this.y11;
      for (let i = 0; i < n; ++i) {
        const a = input[off + i];
        let x = (a + a) * .5 * inGain;
        if (eqOn) { x = e0.run(x); x = e1.run(x); x = e2.run(x); }
        if (circuit) x = pre.run(aaf.run(x));
        const before = acc; acc += ratio;
        for (let k = Math.floor(before) + 1; k <= acc; k += 1) {
          const f = (k - before) / ratio;
          let s = (xPrev + (x - xPrev) * f) * adcScale;
          if (circuit) s = q16(s);
          this.tick(s);
          let l = out[0], r = out[1];
          if (circuit) { l = q16(l); r = q16(r); }
          y00 = y10; y01 = y11; y10 = l / adcScale; y11 = r / adcScale;
        }
        if (acc >= 1048576) acc -= 1048576;
        xPrev = x;
        const fr = acc - Math.floor(acc);
        let oL = y00 + (y10 - y00) * fr, oR = y01 + (y11 - y01) * fr;
        if (circuit) { oL = ap0.run(de0.run(rcf0.run(oL))); oR = ap1.run(de1.run(rcf1.run(oR))); }
        outL[off + i] = oL * ret; outR[off + i] = oR * ret;
      }
      this.acc = acc; this.xPrev = xPrev; this.y00 = y00; this.y01 = y01; this.y10 = y10; this.y11 = y11;
    }

    // RackUnit::process with Rev7's panel fixed (mono, 100 % wet): sleeps once silent longer than tail()
    process(input, outL, outR, off, n) {
      off = off | 0;
      let any = false;
      for (let i = 0; i < n; ++i) if (input[off + i] !== 0) { any = true; break; }
      if (any) { this.quiet = 0; if (this.asleep) { this.asleep = false; this.update(); } } else this.quiet += n;
      if (this.quiet > this.tail() * this.fs) {
        if (!this.asleep) { this.asleep = true; this.reset(); }
        for (let i = 0; i < n; ++i) { outL[off + i] = 0; outR[off + i] = 0; }
        return;
      }
      this.run(input, outL, outR, off, n);
    }
  }

  function ap(d, n, g, x) { const vv = d.at(n); const w = x + g * vv; d.push(w); return vv - g * w; }
  function q16(x) { x = x > 1 ? 1 : x < -1 ? -1 : x; return jround(x * 32767) / 32767; }

  // a factory program's values without an engine (for the display and the controls)
  function factoryValues(prog){
    prog = Math.max(1, Math.min(30, Math.trunc(prog) || 1));
    const v = PARAMS.map(d => d.def); v[P.PROG] = prog;
    const f = FACTORY[prog]; for (const k in f) v[KEYS.indexOf(k)] = f[k];
    return v;
  }
  const api = {P, PARAMS, PROG_NAMES, Rev7Full, factoryValues};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.DXR = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
