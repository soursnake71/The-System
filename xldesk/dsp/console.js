/* SSL XL-Desk analogue engine: the whole console in one AudioWorklet, so summing, solo logic and the bus compressor's
   send all happen sample-accurately in one place. Signal flow follows Appendix A (signal flow block diagram, "XL DESK
   Analogue Overview Rev 1.11") of the Owner's Manual. Levels: float 1.0 = +24 dBu, sine-calibrated (0 dBFS = +24 dBu).
   Needs spec.js loaded first (globalThis.XL). */
const XL = globalThis.XL, REF = XL.REF, P_ = XL.P;
const dB = d => d === -Infinity ? 0 : Math.pow(10, d / 20);
const dBu = d => Math.pow(10, (d - REF) / 20);
const toDbu = a => 8.685889638 * Math.log(a + 1e-12) + REF;
let rs = 0x9e3779b9; const rnd = () => { rs ^= rs << 13; rs ^= rs >>> 17; rs ^= rs << 5; return (rs | 0) / 2147483648; };
// SuperAnalogue stages are clean to the rails: linear to +26 dBu, then a short knee to about +27.5 dBu ("headroom > +24 dBu")
const RAIL = dBu(26), KNEE = .25;
const rail = x => { const a = x < 0 ? -x : x; if (a <= RAIL) return x; const y = RAIL + KNEE * Math.tanh((a - RAIL) / KNEE); return x < 0 ? -y : y; };

class BQ {
  constructor(){ this.b0 = 1; this.b1 = this.b2 = this.a1 = this.a2 = 0; this.z1 = this.z2 = 0; }
  set(b0, b1, b2, a0, a1, a2){ this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0; this.a1 = a1 / a0; this.a2 = a2 / a0; return this; }
  run(x){ const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
  reset(){ this.z1 = this.z2 = 0; }
  lp(f, q, fs){ const w = 2 * Math.PI * f / fs, c = Math.cos(w), a = Math.sin(w) / (2 * q); return this.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a); }
  hp(f, q, fs){ const w = 2 * Math.PI * f / fs, c = Math.cos(w), a = Math.sin(w) / (2 * q); return this.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a); }
  lp1(f, fs){ const K = Math.tan(Math.PI * f / fs); return this.set(K, K, 0, K + 1, K - 1, 0); }
  hp1(f, fs){ const K = Math.tan(Math.PI * f / fs); return this.set(1, -1, 0, K + 1, K - 1, 0); }
  peak(f, q, g, fs){ const A = Math.pow(10, g / 40), w = 2 * Math.PI * f / fs, c = Math.cos(w), a = Math.sin(w) / (2 * q); return this.set(1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A); }
  // first-order (6 dB/octave) shelves, as the E Series EQ guide describes; cut mirrors boost
  lshelf1(f, g, fs){ const K = Math.tan(Math.PI * f / fs), V = Math.pow(10, g / 20), a = g >= 0 ? V : 1, b = g >= 0 ? 1 : 1 / V; return this.set(1 + a * K, a * K - 1, 0, 1 + b * K, b * K - 1, 0); }
  hshelf1(f, g, fs){ const K = Math.tan(Math.PI * f / fs), V = Math.pow(10, g / 20), a = g >= 0 ? V : 1, b = g >= 0 ? 1 : 1 / V; return this.set(a + K, K - a, 0, b + K, K - b, 0); }
}

/* ---------- VHD preamp (channels 1-8) ----------
   +15 to +75 dB gain, 20 dB pad, HI-Z 1.2 kOhm -> 10 kOhm (manual p.9). Clean SuperAnalogue path with VHD out; with VHD in,
   the black pot blends 2nd-harmonic (asymmetric) and 3rd-harmonic (symmetric) drive, and the distortion grows with the
   level hitting the stage, which is why the manual says to drive the red pot and pull back with Trim. 2x oversampled. */
const OS = 2;
const th = x => x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x);   // rational tanh
class VHD {
  constructor(fs){ const f4 = fs * OS, qs = [.5176, .7071, 1.9319]; this.up = qs.map(q => new BQ().lp(20000, q, f4)); this.dn = qs.map(q => new BQ().lp(20000, q, f4)); this.dc = 0; this.dcx = 0; this.dcA = 1 / (1 + 2 * Math.PI * 8 / fs); this.th1 = 0; this.th2 = 0; this.thA = Math.exp(-1 / (.004 * fs)); this.thB = Math.exp(-1 / (.09 * fs)); }
  thump(){ this.th1 += dBu(4); this.th2 += dBu(4); }   // +48 V switching transient
  run(b, N, gain, vhd, drive, noise){
    const up = this.up, dn = this.dn, k2 = .5, k3 = 2.4, bl = drive;
    for (let i = 0; i < N; i++){
      let x = b[i] + noise * rnd();
      x *= gain;
      if (vhd){
        let y = 0;
        for (let o = 0; o < OS; o++){
          let u = up[2].run(up[1].run(up[0].run(o === 0 ? x * OS : 0)));
          const t = th(u), e = t + k2 * t * t, od = th(k3 * u) / k3;
          u = (1 - bl) * e + bl * od;
          const v = dn[2].run(dn[1].run(dn[0].run(u)));
          if (o === 0) y = v;
        }
        const h = this.dcA * (this.dc + y - this.dcx); this.dcx = y; this.dc = h; x = h;   // the even harmonics' DC goes in the coupling cap
      }
      this.th1 *= this.thA; this.th2 *= this.thB;
      b[i] = rail(x + this.th2 - this.th1);
    }
  }
}

/* ---------- SSL E Series EQ ---------- */
class EEQ {
  constructor(fs){ this.fs = fs; this.f = [new BQ(), new BQ(), new BQ(), new BQ()]; this.k = ''; }
  upd(g){
    const blk = g('blk'), rng = blk ? 18 : 15, gn = n => (g(n) - .5) * 2 * rng;
    const hfG = gn('hfG'), hmfG = gn('hmfG'), lmfG = gn('lmfG'), lfG = gn('lfG');
    const hfF = 1500 * Math.pow(16000 / 1500, g('hfF')), hmfF = 600 * Math.pow(7000 / 600, g('hmfF')), lmfF = 200 * Math.pow(2500 / 200, g('lmfF'));
    // Black: 'different control law for tighter control of low frequencies' -> more of the LF pot's travel spent at the bottom
    const lfF = 30 * Math.pow(450 / 30, blk ? Math.pow(g('lfF'), 1.4) : g('lfF'));
    // Brown: constant-Q mids (the +/-3 dB points keep their interval at any gain); Black: Q tightens as gain rises (est.)
    let hq = 2.5 * Math.pow(.2, g('hmfQ')), lq = 2.5 * Math.pow(.2, g('lmfQ'));
    if (blk){ hq *= .6 + .8 * Math.abs(hmfG) / 18; lq *= .6 + .8 * Math.abs(lmfG) / 18; }
    const v = [hfG, hmfG, lmfG, lfG, hfF, hmfF, lmfF, lfF, hq, lq, g('hfBell'), g('lfBell')];
    this.on = !!g('in'); if (this.k && v.every((x, j) => x === this.k[j])) return; this.k = v;
    const fs = this.fs, F = this.f, BELLQ = .7;
    if (g('hfBell')) F[0].peak(Math.min(hfF, fs * .45), BELLQ, hfG, fs); else F[0].hshelf1(Math.min(hfF, fs * .45), hfG, fs);
    F[1].peak(Math.min(hmfF, fs * .45), hq, hmfG, fs); F[2].peak(lmfF, lq, lmfG, fs);
    if (g('lfBell')) F[3].peak(lfF, BELLQ, lfG, fs); else F[3].lshelf1(lfF, lfG, fs);
  }
  run(b, N){ if (!this.on) return; const [a, c, d, e] = this.f; for (let i = 0; i < N; i++) b[i] = rail(e.run(d.run(c.run(a.run(b[i]))))); }
}

/* ---------- SSL E Series Dynamics (611 DYN) ----------
   Compressor: true RMS sidechain, RATIO 1:1 (off) to limit, THRESHOLD +4 to -26 dBu, over-easy knee or hard knee,
   attack 30 ms per 20 dB (3 ms with FAST ATT), log release 0.1-4 s or linear (LIN REL, threshold +6 dB),
   automatic make-up so the output holds steady. Gate/expander: 20:1 gate or 2:1 expander (EXP), RANGE 0-40 dB,
   threshold -30 to +10 dBu with hysteresis that grows as the threshold drops, attack 1.5 ms per 40 dB (100 us with
   FAST ATT), release 0.1-4 s. */
class EDYN {
  constructor(fs){ this.fs = fs; this.ms = 0; this.gr = 0; this.att = 0; this.pk = 0; this.grMax = 0; this.gMax = 0; this.msA = 1 - Math.exp(-1 / (.008 * fs)); this.pkR = Math.exp(-1 / (.01 * fs)); }
  upd(g){
    const p = g('ratio'); this.R = p >= .999 ? Infinity : 1 / (1 - p); this.T = 4 - 30 * g('cthr'); this.hard = !!g('hard'); this.lin = !!g('linrel');
    this.crel = .1 * Math.pow(40, g('crel')); this.catt = (g('cfast') ? 20 / .003 : 20 / .03) / this.fs; this.on = !!g('in');
    this.range = 40 * g('range'); this.GT = -30 + 40 * g('gthr'); this.exp = !!g('exp'); this.grel = .1 * Math.pow(40, g('grel')); this.gatt = (g('gfast') ? 40 / .0001 : 40 / .0015) / this.fs;
    this.hyst = 2 + (10 - this.GT) * .15;
    const slope = this.R === Infinity ? 1 : 1 - 1 / this.R; this.slope = slope; this.mk = Math.max(0, 4 - this.T) * slope;   // auto make-up referenced to +4 dBu
  }
  run(b, N){
    if (!this.on){ this.grMax = this.gMax = 0; return; }
    const fs = this.fs, T = this.T + (this.lin ? 6 : 0), W = this.hard ? 0 : 10, sl = this.slope, compOn = this.R > 1.001, gateOn = this.range > .05;
    const relC = 1 - Math.exp(-1 / (this.crel * fs)), relLin = 20 / this.crel / fs, gRel = 1 - Math.exp(-1 / (this.grel * fs));
    let grMax = 0, gMax = 0;
    for (let i = 0; i < N; i++){
      const x = b[i]; let gdb = 0;
      if (compOn){
        this.ms += (x * x - this.ms) * this.msA;
        const L = 4.342944819 * Math.log(2 * this.ms + 1e-24) + REF, o = L - T;
        const tgt = W && o > -W / 2 && o < W / 2 ? sl * (o + W / 2) * (o + W / 2) / (2 * W) : o > 0 ? sl * o : 0;
        if (tgt > this.gr) this.gr = Math.min(tgt, this.gr + this.catt);
        else this.gr = this.lin ? Math.max(tgt, this.gr - relLin) : this.gr + (tgt - this.gr) * relC;
        gdb = this.mk - this.gr; if (this.gr > grMax) grMax = this.gr;
      }
      if (gateOn){
        const a = x < 0 ? -x : x; this.pk = a > this.pk ? a : this.pk * this.pkR;
        const L = toDbu(this.pk), th = this.att < 1 ? this.GT - this.hyst : this.GT;
        const tgt = L >= th ? 0 : Math.min(this.range, (th - L) * (this.exp ? 1 : 19));
        if (tgt < this.att) this.att = Math.max(tgt, this.att - this.gatt); else this.att += (tgt - this.att) * gRel;
        gdb -= this.att; if (this.att > gMax) gMax = this.att;
      }
      b[i] = rail(x * Math.exp(gdb * .1151292546));
    }
    this.grMax = grMax; this.gMax = gMax;
  }
}

/* ---------- SSL G Series Bus Compressor ----------
   VCA always in circuit, IN enables the sidechain. ATTACK .1 .3 1 3 10 30 ms, RELEASE .1 .2 .4 .8 1.6 s / AUTO,
   RATIO 1.5 2 3 4 5 10, THRESHOLD +/-20, MAKE UP, sidechain HPF OFF 30 60 105 125 185 Hz (faceplate, 2020 guide).
   'Dominant' sidechain: L and R rectified separately by true-peak full-wave detectors, the louder one sets the gain
   for both. The knee point moves with RATIO: lower ratios lower the effective threshold. */
class GCOMP {
  constructor(fs){ this.fs = fs; this.hL = [new BQ(), new BQ()]; this.hR = [new BQ(), new BQ()]; this.gr = 0; this.grS = 0; this.meter = 0; this.k = ''; }
  upd(g){
    const S = XL.GSTEP; this.att = S.att[g('att') | 0]; this.rel = S.rel[g('rel') | 0]; this.R = S.ratio[g('ratio') | 0]; this.hpf = S.hpf[g('hpf') | 0];
    this.T = -20 + 40 * g('thr') - 8 * (1 - Math.log10(this.R)); this.mk = -5 + 20 * g('mk'); this.on = !!g('in');
    // threshold 0 sits at +4 dBu program; the knob moves it +/-20 dB from there
    this.T += 4;
    const key = String(this.hpf);
    if (key !== this.k){ this.k = key; if (this.hpf !== 'OFF') for (const f of [...this.hL, ...this.hR]) f.hp(this.hpf, .7071, this.fs); }
  }
  run(L, R, N){
    const fs = this.fs, sl = 1 - 1 / this.R, aC = 1 - Math.exp(-1 / (this.att / 1000 * fs)), auto = this.rel === 'AUTO';
    const rC = 1 - Math.exp(-1 / ((auto ? .1 : this.rel) * fs)), sC = 1 - Math.exp(-1 / (1.2 * fs)), sA = 1 - Math.exp(-1 / (.3 * fs));
    const mkG = dB(this.mk), hp = this.hpf !== 'OFF', W = 4, T = this.T, mC = 1 - Math.exp(-1 / (.12 * fs));
    for (let i = 0; i < N; i++){
      let l = L[i], r = R[i];
      if (this.on){
        if (hp){ l = this.hL[1].run(this.hL[0].run(l)); r = this.hR[1].run(this.hR[0].run(r)); }
        const d = Math.max(l < 0 ? -l : l, r < 0 ? -r : r), o = toDbu(d) - T;
        const tgt = o <= -W / 2 ? 0 : o < W / 2 ? sl * (o + W / 2) * (o + W / 2) / (2 * W) : sl * o;
        if (tgt > this.gr) this.gr += (tgt - this.gr) * aC; else this.gr += (tgt - this.gr) * rC;
        if (auto){ if (tgt > this.grS) this.grS += (tgt - this.grS) * sA; else this.grS += (tgt - this.grS) * sC; }   // AUTO: fast release on transients, slow on sustained compression
      } else { this.gr *= 1 - rC; this.grS *= 1 - sC; }
      const grd = auto ? Math.max(this.gr, this.grS) : this.gr, gn = Math.exp(-grd * .1151292546) * mkG;
      L[i] = rail(L[i] * gn); R[i] = rail(R[i] * gn);
      this.meter += (grd - this.meter) * mC;
    }
  }
}

/* ---------- SSL VHD Pre (500) ----------
   Block diagram from the module guide: +48V -> HI-Z -> -20 dB pad -> VHD pre -> +/-20 dB trim -> polarity -> HPF -> out,
   signal LED at the output. Gain +20 to +75 dB; HPF 18 dB/octave, 15-500 Hz; same VHD stage as the desk's own preamps. */
class VHDPRE {
  constructor(fs){ this.fs = fs; this.pre = new VHD(fs); this.h = [new BQ(), new BQ()]; this.hk = 0; this.pk = 0; }
  upd(g){ this.gain = dB(20 + 55 * g('gain')); this.vhd = !!g('vhd'); this.drive = g('drive'); this.trim = dB((g('trim') - .5) * 40) * (g('phase') ? -1 : 1);
    this.pad = g('pad') ? .1 : 1; this.hiz = g('hiz') ? 10000 / 10150 : 1200 / 1350; this.hpfIn = !!g('hpfIn');
    const f = 15 * Math.pow(500 / 15, g('hpf')); if (f !== this.hk){ this.hk = f; this.h[0].hp1(f, this.fs); this.h[1].hp(f, 1, this.fs); }   // 3rd-order Butterworth = 1st order + 2nd order Q 1
    if (g('p48') !== this.p48){ if (this.p48 !== undefined) this.pre.thump(); this.p48 = g('p48'); } }
  run(b, N){
    const k = this.pad * this.hiz; for (let i = 0; i < N; i++) b[i] *= k;
    this.pre.run(b, N, this.gain, this.vhd, this.drive, this.noise || 0);
    let pk = this.pk; const t = this.trim, [h1, h2] = this.h, hp = this.hpfIn;
    for (let i = 0; i < N; i++){ let x = b[i] * t; if (hp) x = h2.run(h1.run(x)); x = rail(x); b[i] = x; const a = x < 0 ? -x : x; if (a > pk) pk = a; }
    this.pk = pk;
  }
}
// the 500 module registry: type id -> DSP class. New modules and revisions register here (and in spec.js).
const MODULES = {vhdpre:VHDPRE, eeq:EEQ, edyn:EDYN, gcomp:GCOMP};
/* ---------- true-peak detector for the bus bargraphs ----------
   The desk meters the analogue signal, whose peaks can fall between samples; 4x oversampling with a 32-tap
   windowed-sinc interpolator (ITU-R BS.1770 style) recovers them so the bargraph reads what the analogue bus does. */
const TP_TAPS = 8, TP_PH = 4, TP_H = (() => { const h = []; for (let p = 0; p < TP_PH; p++){ const row = []; for (let k = 0; k < TP_TAPS; k++){ const t = k - (TP_TAPS / 2 - 1) - p / TP_PH, w = .5 + .5 * Math.cos(Math.PI * t / (TP_TAPS / 2)); row.push((Math.abs(t) < 1e-9 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t)) * w); }
  const s = row.reduce((a, b) => a + b, 0); h.push(row.map(v => v / s)); } return h; })();
class TruePeak {
  constructor(){ this.z = new Float32Array(TP_TAPS); }
  run(b, N){ let pk = 0; const z = this.z, c0 = TP_TAPS / 2 - 1;
    for (let n = 0; n < N; n++){ z.copyWithin(0, 1); z[TP_TAPS - 1] = b[n];
      const a = b[n] < 0 ? -b[n] : b[n]; if (a > pk) pk = a;
      // only interpolate where the neighbouring samples are near the running peak; elsewhere the true peak can't win
      const l = Math.max(Math.abs(z[c0]), Math.abs(z[c0 + 1])); if (l < pk * .7) continue;
      for (let p = 1; p < TP_PH; p++){ const h = TP_H[p]; let y = 0; for (let k = 0; k < TP_TAPS; k++) y += h[k] * z[k]; if (y < 0) y = -y; if (y > pk) pk = y; } }
    return pk; }
}
const mkMod = (type, fs) => MODULES[type] ? new MODULES[type](fs) : null;

class XLDesk extends AudioWorkletProcessor {
  constructor(o){
    super();
    const opt = (o && o.processorOptions) || {}, fs = this.fs = sampleRate, N = 128;
    this.P = Object.assign(XL.defaults(), opt.P || {}); this.types = Object.assign(XL.defTypes(), opt.types || {});
    this.io = opt.io || {}; this.outMap = opt.outMap || [{sig:'monsel', ch:0}]; this.cfg = Object.assign({micRef:-20, convRef:24, nOut:2}, opt.cfg || {});
    this.mods = {}; for (const w in XL.WHERE) this.setType(w, this.types[w]);
    this.pre = []; for (let i = 0; i < 8; i++) this.pre.push(new VHD(fs));
    this.sm = {}; this.kc = {}; this.gc = {}; this.slotExt = []; this.tpk = [0, 1, 2, 3, 4, 5, 6, 7].map(() => new TruePeak());                                   // smoothed gains: key -> last value
    this.tracks = new Map(); this.tp = {play:false, pos:0, wind:0, rec:false, loop:false, ls:0, le:0};
    this.rec = {targets:[], buf:{}, n:0};
    this.tone = 0; this.toneBuf = new Float32Array(N); this.toneAt = -1; this.pink = [0, 0, 0, 0, 0, 0, 0];
    const buf = () => new Float32Array(N); this.B = buf;
    this.sig = {}; this.sigs = {};
    const ST = ['monsel', 'main', 'mini1', 'mini2', 'hp', 'fbA', 'fbB', 'cue', 'mixA', 'mixB', 'mixC', 'mixD', 'ret1out', 'ret2out', 'meter', 'st1out', 'st2out', 'st3out', 'st4out'];
    const MO = ['sub', 'aux1', 'aux2', 'lsnout']; for (let i = 1; i <= 16; i++) MO.push('ch' + i, 'slotout' + i);
    for (const s of ST) this.sig[s] = [buf(), buf()]; for (const s of MO) this.sig[s] = [buf()];
    this.src = {}; this.tmp = [buf(), buf(), buf(), buf()];
    this.bus = {A:[buf(), buf()], B:[buf(), buf()], C:[buf(), buf()], D:[buf(), buf()], cue:[buf(), buf()], aux1:[buf()], aux2:[buf()], afl:[buf(), buf()]};
    this.ch = []; for (let i = 0; i < 16; i++) this.ch.push(buf());
    this.alt = []; for (let i = 0; i < 16; i++) this.alt.push(buf());
    this.ins = [buf(), buf()];
    this.mon = {sub:new BQ().lp(80, 1, fs), sub1:new BQ().lp1(80, fs), hpL:new BQ().hp(80, .7071, fs), hpR:new BQ().hp(80, .7071, fs)};
    this.lsn = {gr:0, env:0};
    this.meters = {m:new Float32Array(16), s:new Float32Array(8), b:new Float32Array(8), mod:{}, gc:0, n:0};
    this.port.onmessage = e => this.msg(e.data);
  }
  setType(w, type){
    this.types[w] = type; if (this.gc) for (const k in this.gc) if (k.startsWith(w + '.')) delete this.gc[k];
    if (XL.MOD[type] && XL.MOD[type].st) this.mods[w] = [mkMod(type, this.fs)];
    else if (w.startsWith('bins')) this.mods[w] = [mkMod(type, this.fs), mkMod(type, this.fs)];
    else this.mods[w] = [mkMod(type, this.fs)];
  }
  msg(d){
    if (d.t === 'p'){ const prev = this.P[d.id]; this.P[d.id] = d.v; const m = /^m([1-8])\.p48$/.exec(d.id); if (m && prev !== d.v) this.pre[m[1] - 1].thump(); }
    else if (d.t === 'pp') Object.assign(this.P, d.P);
    else if (d.t === 'type') this.setType(d.w, d.type);
    else if (d.t === 'io') this.io = d.io;
    else if (d.t === 'out') this.outMap = d.map;
    else if (d.t === 'cfg') Object.assign(this.cfg, d.cfg);
    else if (d.t === 'track') this.tracks.set(d.id, {ch:d.ch, len:d.ch[0].length});
    else if (d.t === 'untrack') this.tracks.delete(d.id);
    else if (d.t === 'tp'){ const tp = this.tp;
      if (d.cmd === 'play'){ tp.play = true; tp.wind = 0; }
      else if (d.cmd === 'stop'){ const was = tp.rec && tp.play; tp.play = false; tp.wind = 0; if (tp.rec){ tp.rec = false; } if (was) this.flushRec(true); }
      else if (d.cmd === 'pause'){ tp.play = false; tp.wind = 0; }
      else if (d.cmd === 'locate'){ tp.pos = Math.max(0, Math.round(d.v * this.fs)); }
      else if (d.cmd === 'wind') tp.wind = d.v;
      else if (d.cmd === 'rec'){ if (!tp.rec){ tp.rec = true; this.rec.targets = d.targets || this.rec.targets; this.rec.buf = {}; this.rec.n = 0; } tp.play = true; }
      else if (d.cmd === 'recx'){ if (tp.rec){ tp.rec = false; this.flushRec(true); } }
      else if (d.cmd === 'loop'){ tp.loop = d.on; tp.ls = d.ls; tp.le = d.le; }
    }
  }
  flushRec(done){
    const r = this.rec; if (r.n){ this.port.postMessage({t:'rec', buf:r.buf}); r.buf = {}; r.n = 0; }
    if (done) this.port.postMessage({t:'recdone'});
  }
  // ramp a gain over the block: returns [start, step]
  rp(key, v, N){ const s = this.sm[key]; this.sm[key] = v; if (s === undefined || s === v) return [v, 0]; return [s, (v - s) / N]; }
  g(id){ const v = this.P[id]; return v === undefined ? (P_[id] ? P_[id].d : 0) : v; }
  law(id){ return XL.law(P_[id].law, this.g(id)); }
  getSrc(name, N, ins){
    // returns [L, R] in console units, or null when nothing is plugged in
    const s = this.io[name]; if (!s || s.k === 'none') return null;
    const out = this.src[name] || (this.src[name] = [new Float32Array(N), new Float32Array(N)]), [L, R] = out;
    const mic = /^(mic|listen|talk|slotin)/.test(name), sc = mic ? dBu(this.cfg.micRef) : dBu(this.cfg.convRef);
    if (s.k === 'iface'){
      const inp = ins[0] || [], a = inp[s.ch], b = inp[s.ch + 1] || a; if (!a){ L.fill(0); R.fill(0); return out; }
      for (let i = 0; i < N; i++){ L[i] = a[i] * sc; R[i] = (s.mono ? a[i] : b[i]) * sc; }
    } else if (s.k === 'file'){
      const t = this.tracks.get(s.t), tp = this.tp;
      if (!t || !tp.play || tp.wind){ L.fill(0); R.fill(0); return out; }
      const a = t.ch[0], b = t.ch[1] || a;
      const sum = s.mono && b !== a, k = sum ? sc * .5 : sc;   // a stereo file on a mono input is summed
      for (let i = 0; i < N; i++){ const p = tp.pos + i; if (p >= t.len){ L[i] = R[i] = 0; continue; } L[i] = (sum ? a[p] + b[p] : a[p]) * k; R[i] = sum ? L[i] : b[p] * k; }
    } else if (s.k === 'tone'){
      const tb = this.toneBuf, a = dB(-20) * sc;   // 1 kHz at -20 dBFS: +4 dBu line-up on a +24 converter
      if (this.toneAt !== this.blk){ this.toneAt = this.blk; const w = 2 * Math.PI * 1000 / this.fs; for (let i = 0; i < N; i++) tb[i] = Math.sin(this.tone + w * i); }
      for (let i = 0; i < N; i++){ L[i] = R[i] = tb[i] * a; }
    } else if (s.k === 'pink'){
      const k = this.pink, a = dB(-20) * sc * .11;
      for (let i = 0; i < N; i++){ const w = rnd();   // Paul Kellet's pink filter
        k[0] = .99886 * k[0] + w * .0555179; k[1] = .99332 * k[1] + w * .0750759; k[2] = .969 * k[2] + w * .153852; k[3] = .8665 * k[3] + w * .3104856; k[4] = .55 * k[4] + w * .5329522; k[5] = -.7616 * k[5] - w * .016898;
        L[i] = R[i] = (k[0] + k[1] + k[2] + k[3] + k[4] + k[5] + k[6] + w * .5362) * a; k[6] = w * .115926; }
    }
    return out;
  }
  // cached id lookups so the audio thread doesn't build strings every block
  K(pre){ let o = this.kc[pre]; if (o) return o; o = this.kc[pre] = {}; for (const n of ['fd', 'lv', 'A', 'B', 'C', 'D']) o[n] = pre + '.' + n; for (const id in P_) if (id.startsWith(pre + '.')) o[id.slice(pre.length + 1)] = id; return o; }
  // a module reads its controls by name; they live in the location's generic slots <where>.<j>
  G(w, t){ const key = w + '.' + t; let f = this.gc[key]; if (f) return f; const M = XL.MOD[t], P = this.P, ids = {}, st = {};
    for (const q of M.params){ ids[q.n] = w + '.' + q.j; if (q.k === 'step') st[q.n] = q.v.length - 1; }
    f = this.gc[key] = n => { const v = P[ids[n]]; return st[n] ? Math.round((v || 0) * st[n]) : v || 0; }; return f; }
  modRun(w, b, N){ const m = this.mods[w] && this.mods[w][0]; if (!m){ b.fill(0); return; } const t = this.types[w]; m.upd(this.G(w, t)); m.run(b, N); this.modMeter(w, m); }
  modMeter(w, m){ const o = this.meters.mod[w] || (this.meters.mod[w] = [0, 0]); if (m.grMax !== undefined){ o[0] = Math.max(o[0], m.grMax); o[1] = Math.max(o[1], m.gMax); } else if (m.pk !== undefined){ o[0] = Math.max(o[0], m.pk); m.pk = 0; } }
  process(ins, outs){
    const out = outs[0], N = out[0].length, fs = this.fs, P = this.P, g = id => this.g(id); this.blk = (this.blk | 0) + 1;
    const noiseOn = !!g('sys.noise'), nCh = noiseOn ? dBu(-86) * 1.732 : 1e-9, nPre = noiseOn ? dBu(-128) * 1.732 : 0;
    const B = this.bus; for (const k in B) for (const a of B[k]) a.fill(0);
    for (const k in this.sig) for (const a of this.sig[k]) a.fill(0);
    const tp = this.tp;
    // solo logic: solo-in-place cuts everything that isn't soloed or solo-safe; AFL mode feeds the AFL bus instead
    const aflMode = !!g('solo.afl'); let soloAny = false;
    for (let i = 1; i <= 16 && !soloAny; i++) if (P['m' + i + '.solo']) soloAny = true;
    for (let k = 1; k <= 4 && !soloAny; k++) if (P['s' + k + '.solo']) soloAny = true;
    const sip = soloAny && !aflMode;
    const stolen = {}; [['A', 9], ['B', 11], ['C', 13], ['D', 15]].forEach(([b, s]) => { if (P[b + '.s500']){ stolen[s] = b; stolen[s + 1] = b; } });
    const links = [!g('rear.unlink1'), !g('rear.unlink2')];   // the four short D-sub cables (ships fitted)
    const aux1Pre = !!g('aux1.pre'), aux2Pre = !!g('aux2.pre');
    const M = this.meters, idle = {A:0, B:0, C:0, D:0};
    /* ---------- 500 slots fed from the rear 500 SLOT IN connector (a mic or line straight into the module) ---------- */
    for (let i = 1; i <= 16; i++){
      const w = 'slot' + i, so = this.sig['slotout' + i][0]; this.slotExt[i] = false;
      if (stolen[i] || !P[w + '.ext'] || this.types[w] === 'empty') continue;
      const s = this.getSrc('slotin' + i, N, ins); if (s) so.set(s[0]); else so.fill(0);
      const m = this.mods[w][0]; if (m.pre) m.noise = noiseOn ? dBu(-128) * 1.732 : 0; this.modRun(w, so, N); this.slotExt[i] = true;
    }
    /* ---------- mono channels ---------- */
    for (let i = 1; i <= 16; i++){
      const m = this.K('m' + i), x = this.ch[i - 1], alt = this.alt[i - 1];
      const daw = !!P[m.daw];
      let main = null;
      if (i <= 8){
        const s = this.getSrc('mic' + i, N, ins);
        if (s){ main = this.tmp[0]; main.set(s[0]);
          const pad = P[m.pad] ? .1 : 1, hiz = P[m.hiz] ? 10000 / 10150 : 1200 / 1350;   // 150 ohm mic into 1.2k or 10k
          if (pad !== 1 || hiz !== 1) for (let n = 0; n < N; n++) main[n] *= pad * hiz;
          this.pre[i - 1].run(main, N, dB(this.law(m.gain)), !!P[m.vhd], g(m.drive), nPre); }
      } else { const s = this.getSrc('line' + i, N, ins); if (s) main = s[0]; }
      const ds = this.getSrc('daw' + i, N, ins), dw = ds ? ds[0] : null;
      const sel = daw ? dw : main, oth = daw ? main : dw;
      if (!sel && !oth && !(P[m.s500] && this.slotExt[i])){   // nothing plugged in: skip the strip but keep its line-amp noise on the buses it feeds
        const cutI = P[m.cut] || (sip && !P[m.solo] && !P[m.safe]), f = cutI ? 0 : dB(this.law(m.fader)), e = f * f * .5;
        for (const b of 'ABCD') if (P[m[b]]) idle[b] += e; this.sm[m.fd] = undefined; continue;
      }
      const [t0, tS] = this.rp(m.trim, dB(this.law(m.trim)) * (P[m.phase] ? -1 : 1), N);
      if (sel) for (let n = 0; n < N; n++) x[n] = sel[n] * (t0 + tS * n) + nCh * rnd(); else for (let n = 0; n < N; n++) x[n] = nCh * rnd();
      if (oth) alt.set(oth); else alt.fill(0);
      // 500: the second insert, wired to the slot above the channel through the rear D-sub links
      // 500: the second insert. The send reaches the slot through the link cable; the slot's output comes back to the 500 return.
      if (P[m.s500]){ if (stolen[i] || this.types['slot' + i] === 'empty' || (!this.slotExt[i] && !links[i <= 8 ? 0 : 1])) x.fill(0);
        else if (this.slotExt[i]) x.set(this.sig['slotout' + i][0]);
        else { this.modRun('slot' + i, x, N); this.sig['slotout' + i][0].set(x); } }
      if (P[m.ins]){ const t = this.types['ins' + i]; if (t === 'none') x.fill(0); else if (t !== 'loop') this.modRun('ins' + i, x, N); }
      let pk = 0; for (let n = 0; n < N; n++){ x[n] = rail(x[n]); const a = x[n] < 0 ? -x[n] : x[n]; if (a > pk) pk = a; }
      if (pk > M.m[i - 1]) M.m[i - 1] = pk;
      const cutNow = P[m.cut] || (sip && !P[m.solo] && !P[m.safe]);
      const [f0, fS] = this.rp(m.fd, dB(this.law(m.fader)) * (cutNow ? 0 : 1), N);
      const pan = this.law(m.pan), pa = (pan + 1) * Math.PI / 4, pL = Math.cos(pa), pR = Math.sin(pa);   // -3 dB constant-power pan (est.)
      const cp = this.law(m.cuepan), ca = (cp + 1) * Math.PI / 4, [c0, cS] = this.rp(m.cue, dB(this.law(m.cue)), N), cL = Math.cos(ca), cR = Math.sin(ca);
      const cuePost = !!P[m.cuepost], cueAlt = !!P[m.alt];
      const [a10, a1S] = this.rp(m.aux1, dB(this.law(m.aux1)), N), [a20, a2S] = this.rp(m.aux2, dB(this.law(m.aux2)), N);
      const bA = P[m.A], bB = P[m.B], bC = P[m.C], bD = P[m.D], chop = P[m.chop], afl = aflMode && P[m.solo];
      const D = this.sig['ch' + i][0], cue = B.cue, a1 = B.aux1[0], a2 = B.aux2[0];
      for (let n = 0; n < N; n++){
        const v = x[n], post = v * (f0 + fS * n), l = post * pL, r = post * pR;
        if (bA){ B.A[0][n] += l; B.A[1][n] += r; } if (bB){ B.B[0][n] += l; B.B[1][n] += r; }
        if (bC){ B.C[0][n] += l; B.C[1][n] += r; } if (bD){ B.D[0][n] += l; B.D[1][n] += r; }
        const cs = (cueAlt ? alt[n] : cuePost ? post : v) * (c0 + cS * n); cue[0][n] += cs * cL; cue[1][n] += cs * cR;
        a1[n] += (aux1Pre ? v : post) * (a10 + a1S * n); a2[n] += (aux2Pre ? v : post) * (a20 + a2S * n);
        D[n] = chop ? post : v;
        if (afl){ B.afl[0][n] += l; B.afl[1][n] += r; }
      }
    }
    /* ---------- stereo channels ---------- */
    for (let k = 1; k <= 4; k++){
      const s = this.K('s' + k), src = this.getSrc('st' + k, N, ins), L = this.tmp[0], R = this.tmp[1];
      if (!src){ const cutI = P[s.cut] || (sip && !P[s.solo] && !P[s.safe]), f = cutI ? 0 : dB(this.law(s.fader)); for (const b of 'ABCD') if (P[s[b]]) idle[b] += f * f; this.sm[s.fd] = undefined; continue; }
      const tr = dB(this.law(s.trim)), mono = !!P[s.mono], sL = P[s.phL] ? -tr : tr, sR = P[s.phR] ? -tr : tr;
      for (let n = 0; n < N; n++){ L[n] = rail((src ? src[0][n] : 0) * sL + nCh * rnd()); R[n] = rail(mono ? L[n] : (src ? src[1][n] : 0) * sR + nCh * rnd()); }   // MONO takes the left input (L-MONO)
      let pl = 0, pr = 0; for (let n = 0; n < N; n++){ const a = Math.abs(L[n]), b = Math.abs(R[n]); if (a > pl) pl = a; if (b > pr) pr = b; }
      M.s[2 * k - 2] = Math.max(M.s[2 * k - 2], pl); M.s[2 * k - 1] = Math.max(M.s[2 * k - 1], pr);
      const cutNow = P[s.cut] || (sip && !P[s.solo] && !P[s.safe]);
      const [f0, fS] = this.rp(s.fd, dB(this.law(s.fader)) * (cutNow ? 0 : 1), N);
      const pan = this.law(s.pan);
      let gL, gR;
      if (mono){ const a = (pan + 1) * Math.PI / 4; gL = Math.cos(a); gR = Math.sin(a); L.set(R); }   // mono: pan
      else { gL = pan > 0 ? Math.cos(pan * Math.PI / 2) : 1; gR = pan < 0 ? Math.cos(-pan * Math.PI / 2) : 1; }   // stereo: balance
      const cb = this.law(s.cuepan), cgL = cb > 0 ? Math.cos(cb * Math.PI / 2) : 1, cgR = cb < 0 ? Math.cos(-cb * Math.PI / 2) : 1, cl = dB(this.law(s.cue)), cuePost = !!P[s.cuepost];
      const a1g = dB(this.law(s.aux1)), a2g = dB(this.law(s.aux2)), chop = P[s.chop], afl = aflMode && P[s.solo];
      const bs = 'ABCD'.split('').filter(b => P[s[b]]), D = this.sig['st' + k + 'out'];
      for (let n = 0; n < N; n++){
        const f = f0 + fS * n, l = L[n] * f * gL, r = R[n] * f * gR;
        for (const b of bs){ B[b][0][n] += l; B[b][1][n] += r; }
        const cs = cuePost ? f : 1; B.cue[0][n] += L[n] * cs * cl * cgL; B.cue[1][n] += R[n] * cs * cl * cgR;
        const mn = (L[n] + R[n]) * .5; B.aux1[0][n] += mn * (aux1Pre ? 1 : f) * a1g; B.aux2[0][n] += mn * (aux2Pre ? 1 : f) * a2g;
        D[0][n] = chop ? l : L[n]; D[1][n] = chop ? r : R[n];
        if (afl){ B.afl[0][n] += l; B.afl[1][n] += r; }
      }
    }
    /* ---------- stereo returns ---------- */
    for (let k = 1; k <= 2; k++){
      const r = this.K('r' + k), src = this.getSrc('ret' + k, N, ins); if (!src && !P[r.afl]) continue;
      const lv = sip ? 0 : dB(this.law(r.level)), bal = this.law(r.bal), mono = !!P[r.mono], D = this.sig['ret' + k + 'out'];
      let gL, gR; if (mono){ const a = (bal + 1) * Math.PI / 4; gL = Math.cos(a); gR = Math.sin(a); } else { gL = bal > 0 ? Math.cos(bal * Math.PI / 2) : 1; gR = bal < 0 ? Math.cos(-bal * Math.PI / 2) : 1; }
      const [l0, lS] = this.rp(r.lv, lv, N), bs = 'ABCD'.split('').filter(b => P[r[b]]), cue = !!P[r.cue], afl = !!P[r.afl];
      for (let n = 0; n < N; n++){
        let a = src ? src[0][n] : 0, b = src ? src[1][n] : 0; if (mono) a = b = (a + b) * .5;
        const v = l0 + lS * n, l = a * v * gL, rr = b * v * gR;
        for (const x of bs){ B[x][0][n] += l; B[x][1][n] += rr; }
        if (cue){ B.cue[0][n] += l; B.cue[1][n] += rr; }
        if (afl){ B.afl[0][n] += l; B.afl[1][n] += rr; }
        D[0][n] = l; D[1][n] = rr;
      }
    }
    /* ---------- mix buses B, C, D, then A ---------- */
    const bamp = (b, nz) => { for (const a of b) for (let n = 0; n < N; n++) a[n] = rail(a[n] + nz * rnd()); };
    const busChain = (X, s1) => {
      const b = B[X];
      if (P[X + '.s500']){ for (const [side, slot] of [[0, s1], [1, s1 + 1]]){ if (this.types['slot' + slot] === 'empty') b[side].fill(0); else this.modRun('slot' + slot, b[side], N); } }
      const ins = !!P[X + '.ins'], sum = !!P[X + '.sum'];
      if (ins || sum){
        const t = this.types['bins' + X], [rl, rr] = this.ins; rl.set(b[0]); rr.set(b[1]);
        if (t === 'none'){ rl.fill(0); rr.fill(0); }
        else if (XL.MOD[t] && XL.MOD[t].st){ const m = this.mods['bins' + X][0]; m.upd(this.G('bins' + X, t)); m.run(rl, rr, N); this.meters.mod['bins' + X] = m.meter; }
        else if (t !== 'loop'){ const ms = this.mods['bins' + X]; for (const [j, a] of [[0, rl], [1, rr]]){ ms[j].upd(this.G('bins' + X, t)); ms[j].run(a, N); this.modMeter('bins' + X, ms[j]); } }
        for (let n = 0; n < N; n++){ b[0][n] = sum ? b[0][n] + rl[n] : rl[n]; b[1][n] = sum ? b[1][n] + rr[n] : rr[n]; }   // SUM: return added to the bus feed
      }
    };
    const nBus = noiseOn ? dBu(-86) * 1.732 : 0;
    for (const [X, s1] of [['B', 11], ['C', 13], ['D', 15]]){
      const b = B[X]; bamp(b, Math.sqrt(nBus * nBus + nCh * nCh * idle[X])); busChain(X, s1);
      const [v0, vS] = this.rp(X + '.lv', dB(this.law(X + '.level')), N), o = this.sig['mix' + X], toA = P[X + '.toA'], afl = P[X + '.afl'];
      for (let n = 0; n < N; n++){ const v = v0 + vS * n, l = rail(b[0][n] * v), r = rail(b[1][n] * v); o[0][n] = l; o[1][n] = r; if (toA){ B.A[0][n] += l; B.A[1][n] += r; } if (afl){ B.afl[0][n] += l; B.afl[1][n] += r; } }
    }
    { // cue and aux masters (+10 dB of trim, centre-detent unity)
      const cv = dB(this.law('cue.level')), o = this.sig.cue, toA = P['cue.toA'], afl = P['cue.afl'];
      for (let n = 0; n < N; n++){ const l = rail(B.cue[0][n] * cv), r = rail(B.cue[1][n] * cv); o[0][n] = l; o[1][n] = r; if (toA){ B.A[0][n] += l; B.A[1][n] += r; } if (afl){ B.afl[0][n] += l; B.afl[1][n] += r; } }
      for (const a of ['aux1', 'aux2']){ const v = dB(this.law(a + '.level')), o = this.sig[a][0], af = P[a + '.afl']; for (let n = 0; n < N; n++){ o[n] = rail(B[a][0][n] * v); if (af){ B.afl[0][n] += o[n]; B.afl[1][n] += o[n]; } } }
    }
    { const b = B.A; bamp(b, Math.sqrt(nBus * nBus + nCh * nCh * idle.A)); busChain('A', 9);
      // the bus compressor's send is always live, so its needle moves even with COMP out (manual p.17)
      const t17 = this.types.slot17, gc = this.mods.slot17[0], [cl, cr] = this.ins;
      cl.set(b[0]); cr.set(b[1]);
      if (gc){ gc.upd(this.G('slot17', 'gcomp')); gc.run(cl, cr, N); M.gc = gc.meter; } else { cl.fill(0); cr.fill(0); M.gc = 0; }
      if (P['A.comp']){ b[0].set(cl); b[1].set(cr); }
      const [f0, fS] = this.rp('A.fd', dB(this.law('A.fader')), N), o = this.sig.mixA;
      for (let n = 0; n < N; n++){ const f = f0 + fS * n; o[0][n] = rail(b[0][n] * f); o[1][n] = rail(b[1][n] * f); }
    }
    for (const [j, X] of ['A', 'B', 'C', 'D'].entries()){ const o = this.sig['mix' + X]; M.b[2 * j] = Math.max(M.b[2 * j], this.tpk[2 * j].run(o[0], N)); M.b[2 * j + 1] = Math.max(M.b[2 * j + 1], this.tpk[2 * j + 1].run(o[1], N)); }
    /* ---------- listen mic: preamp and the classic listen-mic compressor (fast, heavy, fixed: est.) ---------- */
    let lsn = null;
    { const s = this.getSrc('listen', N, ins);
      if (s){ lsn = this.sig.lsnout[0]; const gn = dB(this.law('listen.level')), aC = 1 - Math.exp(-1 / (.0002 * fs)), rC = 1 - Math.exp(-1 / (.12 * fs)), T = -16, mk = dB(14);
        for (let n = 0; n < N; n++){ const x = s[0][n] * gn, a = Math.abs(x); this.lsn.env += (a - this.lsn.env) * (a > this.lsn.env ? aC : rC);
          const o = toDbu(this.lsn.env) - T, gr = o > 0 ? o * (1 - 1 / 20) : 0; lsn[n] = rail(x * Math.pow(10, -gr / 20) * mk); } } }
    /* ---------- monitoring ---------- */
    const mon = this.tmp[2], mon2 = this.tmp[3], meterO = this.sig.meter;
    const aflAny = (aflMode && soloAny) || P['r1.afl'] || P['r2.afl'] || P['B.afl'] || P['C.afl'] || P['D.afl'] || P['cue.afl'] || P['aux1.afl'] || P['aux2.afl'];
    if (aflAny){ const v = dB(this.law('mon.afl')); for (let n = 0; n < N; n++){ mon[n] = B.afl[0][n] * v; mon2[n] = B.afl[1][n] * v; } }
    else { mon.fill(0); mon2.fill(0);
      const add = s => { if (s) for (let n = 0; n < N; n++){ mon[n] += s[0][n]; mon2[n] += s[1][n]; } };
      if (P['mon.srcA']) add(this.sig.mixA); if (P['mon.srcExt']) add(this.getSrc('ext', N, ins)); if (P['mon.srcIj']) add(this.getSrc('ijack', N, ins)); }
    if (P['listen.on'] && lsn) for (let n = 0; n < N; n++){ mon[n] += lsn[n]; mon2[n] += lsn[n]; }
    meterO[0].set(mon); meterO[1].set(mon2);
    { // headphones: the foldback picked with F/B A or B, else the monitor source (talkback is not heard here, manual p.18)
      const hp = this.sig.hp, v = dB(this.law('hp.level'));
      const fbSrc = which => which === 'A' ? (P['fbA.mon'] ? [mon, mon2] : this.sig.cue) : (P['fbB.mon'] ? [mon, mon] : [this.sig.aux1[0], this.sig.aux1[0]]);
      const s = P['hp.fbA'] ? fbSrc('A') : P['hp.fbB'] ? fbSrc('B') : [mon, mon2];
      const fv = P['hp.fbA'] ? dB(this.law('fbA.level')) : P['hp.fbB'] ? dB(this.law('fbB.level')) : 1;
      for (let n = 0; n < N; n++){ hp[0][n] = s[0][n] * fv * v; hp[1][n] = s[1][n] * fv * v; }
      // foldback A (st cue) and B (aux 1) with talkback, sum or replace per DIP 2 / 4
      const tk = this.getSrc('talk', N, ins), tg = dB(this.law('talk.level'));
      for (const [f, dip, a, b] of [['fbA', 2, ...(P['fbA.mon'] ? [mon, mon2] : this.sig.cue)], ['fbB', 4, ...(P['fbB.mon'] ? [mon, mon2] : [this.sig.aux1[0], this.sig.aux1[0]])]]){
        const o = this.sig[f], lv = dB(this.law(f + '.level')), talk = P[f + '.talk'] && tk, rep = talk && P['dip.' + dip];
        for (let n = 0; n < N; n++){ const t = talk ? tk[0][n] * tg : 0; o[0][n] = rail(((rep ? 0 : a[n]) + t) * lv); o[1][n] = rail(((rep ? 0 : b[n]) + t) * lv); }
      }
    }
    { const mono = P['mon.mono'], cut = P['mon.cut'], set = P['mon.mini1'] ? 'mini1' : P['mon.mini2'] ? 'mini2' : 'main';
      let v = cut ? 0 : dB(this.law('mon.level')) * (P['mon.dim'] ? dB(this.law('mon.dimamt')) : 1);
      if (set === 'mini1') v *= dB(this.law('mon.off1')); if (set === 'mini2') v *= dB(this.law('mon.off2'));
      const [v0, vS] = this.rp('mon.v', v, N), subOn = !!P[set === 'main' ? 'mon.subMain' : set === 'mini1' ? 'mon.subMini1' : 'mon.subMini2'];
      const mono2 = set === 'mini2' && P['dip.7'], o = this.sig[set], sub = this.sig.sub[0], sel = this.sig.monsel, F = this.mon;
      for (let n = 0; n < N; n++){
        let l = mon[n] * (v0 + vS * n), r = mon2[n] * (v0 + vS * n);
        if (mono || mono2){ l = r = (l + r) * .5; }
        let s = 0;
        if (subOn){ s = F.sub.run(F.sub1.run(l + r)); l = F.hpL.run(l); r = F.hpR.run(r); }   // bass management: 3rd-order LP at 80 Hz to the sub, 2nd-order HP on the speakers
        o[0][n] = rail(l); o[1][n] = rail(r); sub[n] = rail(s);
        sel[0][n] = rail(l + s * .5); sel[1][n] = rail(r + s * .5);
      }
    }
    /* ---------- outputs: each console output signal patched to an interface channel ---------- */
    for (const c of out) c.fill(0);
    for (const m of this.outMap){ const s = this.sig[m.sig]; if (!s) continue; for (let j = 0; j < s.length; j++){ const c = out[m.ch + j]; if (!c) continue; const a = s[j]; for (let n = 0; n < N; n++) c[n] += a[n]; } }
    /* ---------- transport and print ---------- */
    if (tp.play && tp.rec){ const r = this.rec; for (const t of r.targets){ const s = this.sig[t]; if (!s) continue; (r.buf[t] || (r.buf[t] = s.map(() => []))).forEach((arr, j) => arr.push(s[j].slice())); } r.n += N; if (r.n >= 8192) this.flushRec(false); }
    if (tp.wind) tp.pos = Math.max(0, tp.pos + Math.round(N * tp.wind));
    else if (tp.play){ tp.pos += N; if (tp.loop && tp.le > tp.ls && tp.pos >= tp.le * fs) tp.pos = Math.round(tp.ls * fs); }
    this.tone = (this.tone + 2 * Math.PI * 1000 / fs * N) % (2 * Math.PI);
    M.n += N;
    if (M.n >= 1600){
      const db = a => Array.from(a, v => toDbu(v));
      this.port.postMessage({t:'m', m:db(M.m), s:db(M.s), b:db(M.b), gc:M.gc, mod:M.mod, pos:tp.pos / fs, play:tp.play, rec:tp.rec, wind:tp.wind});
      M.m.fill(0); M.s.fill(0); M.b.fill(0); M.mod = {}; M.n = 0;
    }
    return true;
  }
}
registerProcessor('xl-desk', XLDesk);
