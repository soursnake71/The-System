/* Jam-On rig audio worklets.
   TB-303, the analog engine (Juno-60 / SH-101) and the capture node are copied from The Omega System (../index.html);
   the Pro-One, RS-09 and Electro-Harmonix vocoder models are new (see the notes on each class). */
/* Roland TB-303, following Robin Whittle's Devil Fish documentation of the stock circuit and published diode-ladder analysis:
   saw VCO (square derived from the saw by a transistor shaper), 4-pole diode-ladder low-pass whose stages load each other,
   with a high-pass inside the resonance loop (so it rings but does not self-oscillate), no keyboard tracking,
   MEG (filter env) decay 200 ms - 2 s, fixed 200 ms on accents; VEG (amp env) 4 ms delay, 3 ms attack, ~3.5 s decay,
   8 ms hold + 8 ms linear fade at note end; slide = 60 ms RC on the pitch CV with no retrigger;
   accent = MEG through the ACCENT pot into the accent sweep circuit, whose lag (C13) is ganged to RESONANCE and keeps charge
   between accents, adding to both cutoff and volume. Mono, last-note priority. */
// measured self-oscillation threshold of this ladder (feedback gain) at each cutoff in Hz
const THR = [[150, 59.9], [300, 42.13], [600, 30.51], [1200, 24.65], [2400, 21.04], [4800, 17.71], [6500, 16.15]];
class TB303 extends AudioWorkletProcessor{
  constructor(o){
    super();
    const opt = (o && o.processorOptions) || {}; this.fs = sampleRate;
    this.c = Object.assign({tune:.5, wave:0, cut:.35, res:.75, env:.55, dec:.45, acc:.7, vol:.8}, opt.cfg || {});
    this.q = (opt.events || []).slice().sort((a, b) => a.f - b.f);
    this.held = []; this.note = -1; this.cv = null; this.cvT = null; this.gate = false; this.acc = false;
    this.ph = 0; this.meg = 0; this.megTau = .2; this.veg = 0; this.vegState = 0; this.vegT = 0; this.c13 = 0; this.accPulse = 0;
    this.s = [0, 0, 0, 0]; this.hp = 0; this.hpx = 0; this.dc = 0; this.dcx = 0; this.offT = 0;
    this.port.onmessage = e => { const d = e.data; if (d.type === 'ev'){ this.q.push(d.ev); this.q.sort((a, b) => a.f - b.f); } else if (d.type === 'cfg') Object.assign(this.c, d.cfg); };
  }
  on(ev){
    const legato = this.gate || ev.sl;          // a tie or an overlapping note slides instead of retriggering
    this.held = this.held.filter(n => n !== ev.on); this.held.push(ev.on);
    this.note = ev.on; this.cvT = ev.on;
    this.acc = (ev.v || 0) >= .85;
    if (!legato || this.cv == null){
      this.cv = ev.on;
      this.meg = 1; this.megTau = this.acc ? .2 : .2 * Math.pow(10, this.c.dec);   // 200 ms .. 2 s, accent fixed at 200 ms
      this.vegState = 1; this.vegT = 0;
      if (this.acc) this.accPulse = 1;
    } else if (this.acc){ this.meg = 1; this.megTau = .2; this.accPulse = 1; }
    this.gate = true;
  }
  off(ev){
    this.held = this.held.filter(n => n !== ev.off);
    if (this.held.length){ this.note = this.cvT = this.held[this.held.length - 1]; return; }
    if (ev.off === this.note || !this.held.length){ this.gate = false; this.vegState = 3; this.vegT = 0; }
  }
  t(x){ if (x > 3) return 1; if (x < -3) return -1; const x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, fs = this.fs, C = this.c, OS = 4, fo = fs * OS;
    const slide = 1 - Math.exp(-1 / (.026 * fs));               // 60 ms slide
    const megD = Math.exp(-1 / (this.megTau / 3 * fs));
    const vegA = 1 - Math.exp(-1 / (.001 * fs)), vegD = Math.exp(-1 / (3.5 / 3 * fs));
    const resAmt = Math.pow(C.res, .5);
    const lag = .02 + C.res * .09;                               // C13 lag grows with RESONANCE (ganged pot)
    const c13up = 1 - Math.exp(-1 / (lag * fs)), c13dn = Math.exp(-1 / ((lag * 6) * fs));
    const hpA = 1 / (1 + 2 * Math.PI * 150 / fo), dcA = 1 / (1 + 2 * Math.PI * 25 / fs);
    // silent between notes and no note due in this block: skip the ladder
    if ((!this.q.length || this.q[0].f >= currentFrame + N) && (this.cv == null || (this.vegState === 0 && this.veg === 0 && this.c13 < 1e-4))){ L.fill(0); if (R !== L) R.fill(0); this.dc = this.dcx = 0; return true; }
    for (let i = 0; i < N; i++){
      const frame = currentFrame + i;
      while (this.q.length && this.q[0].f <= frame){ const ev = this.q.shift(); if (ev.on != null) this.on(ev); else this.off(ev); this.ok = false; }
      if (this.cv == null){ L[i] = R[i] = 0; continue; }
      this.cv += (this.cvT - this.cv) * slide;
      // VEG: 4 ms delay, 3 ms attack, long decay; release = 8 ms hold then 8 ms linear
      this.vegT += 1 / fs;
      if (this.vegState === 1){ if (this.vegT > .004){ this.veg += (1.05 - this.veg) * vegA; if (this.veg >= 1){ this.veg = 1; this.vegState = 2; } } }
      else if (this.vegState === 2) this.veg *= vegD;
      else if (this.vegState === 3){ if (this.vegT > .008){ this.veg -= 1 / (.008 * fs); if (this.veg <= 0){ this.veg = 0; this.vegState = 0; } } }
      this.meg *= megD;
      // accent sweep circuit: MEG x ACCENT, direct at low resonance, through C13 lag (keeping charge) at high resonance
      const a = this.accPulse ? this.meg * C.acc : 0; if (this.meg < .01) this.accPulse = 0;
      this.c13 = a > this.c13 ? this.c13 + (a - this.c13) * c13up : this.c13 * c13dn;
      const sweep = (1 - C.res) * a + C.res * this.c13 * 1.6;
      // oscillator: saw, or the transistor-shaped square
      if ((i & 7) === 0 || !this.ok){ this.ok = true; this.dt = 440 * Math.pow(2, (this.cv - 69) / 12 + (C.tune - .5)) / fs; }
      const dt = this.dt;
      this.ph += dt; if (this.ph >= 1) this.ph -= 1;
      let saw = 1 - 2 * this.ph;
      if (this.ph < dt){ const x = this.ph / dt; saw += x + x - x * x - 1; } else if (this.ph > 1 - dt){ const x = (this.ph - 1) / dt; saw += x * x + x + x + 1; }
      const osc = C.probe === 2 ? (Math.random() * 2 - 1) * 1e-4 : C.probe ? Math.random() * 2 - 1 : C.wave ? this.t(6 * (saw + .1)) * .9 + saw * .1 : saw;
      // cutoff: knob ~250 Hz-2.5 kHz, env mod, accent sweep; no keyboard tracking
      const oct = Math.log2(250) + C.cut * 3.3 + this.meg * (.6 + C.env * 2.6) * (this.acc ? 1.15 : 1) + sweep * 1.8;
      // cutoff and resonance gain update every 8 samples
      if ((i & 7) === 0 || this.w == null){
        const fc = C.fcFix || Math.min(6500, Math.pow(2, oct));
        this.w = 2 * Math.PI * fc * 1.32 / fo;   // the loaded ladder peaks ~25% low; trimmed so the peak sits on the cutoff
        // full resonance stays at 88% of the measured self-oscillation threshold for this cutoff: a stock 303 rings but never sings
        let thr = THR[THR.length - 1][1];
        for (let j = 1; j < THR.length; j++) if (fc <= THR[j][0]){ const [f0, k0] = THR[j - 1], [f1, k1] = THR[j], x = Math.max(0, Math.log2(fc / f0) / Math.log2(f1 / f0)); thr = k0 + (k1 - k0) * x; break; }
        this.resK = C.kFix != null ? C.kFix : .95 * thr * resAmt;
      }
      const w = this.w, resK = this.resK, s = this.s;
      let y = 0;
      for (let k = 0; k < OS; k++){
        // resonance feedback through a 150 Hz high-pass, then the diode ladder: each stage's diodes see the next capacitor
        const fbIn = s[3], hp = hpA * (this.hp + fbIn - this.hpx); this.hp = hp; this.hpx = fbIn;
        const u = osc * .9 - resK * hp;
        const d0 = this.t(u - s[0]), d1 = this.t(s[0] - s[1]), d2 = this.t(s[1] - s[2]), d3 = this.t(s[2] - s[3]);
        s[0] += w * (d0 - d1); s[1] += w * .5 * (d1 - d2); s[2] += w * .5 * (d2 - d3); s[3] += w * .5 * d3;
        y += s[3];
      }
      y = y / OS * (1 + resK * .06) * 2.2;
      // VCA: VEG plus the accent sweep; output coupling rolls off the deep bass (32 Hz about -5 dB)
      let o = y * this.veg * (.55 + sweep * .9 + (this.acc ? .25 : 0)) * C.vol;
      const dc = dcA * (this.dc + o - this.dcx); this.dc = dc; this.dcx = o; o = this.t(dc * 1.2) * .8;
      L[i] = o; R[i] = o;
    }
    return true;
  }
}
registerProcessor('tb303', TB303);
/* Analog poly/mono engine, circuit-modelled per instrument. The voice path runs at twice the sample rate and is
   decimated through a half-band filter, so the nonlinear stages behave like the hardware instead of aliasing.
   Oscillators: DCO models (Juno-60, JX-3P, Six-Trak) are counter-reset integrators locked to a master clock: exact
   pitch, no drift; the pulse is a comparator on the ramp and the Juno sub a flip-flop. VCO models (SH-101, Minimoog,
   CS-80) are exponential-converter ramp cores with thermal drift (a slow random walk of a few cents per oscillator) and
   a little cycle jitter. Every output is AC-coupled and slew-limited like the op-amps that buffer it.
   Filters, all solved per oversampled step with their saturating elements:
   - Roland IR3109 (Juno, JX-3P, SH-101): four OTA integrators, each stage dv = g*tanh(v_in - v_out); resonance through
     a saturating feedback OTA, unity-gain buffers, so the bass drops as resonance rises, as on the real synths.
   - Moog transistor ladder (Minimoog): Huovilainen's model, tanh of each stage's input and output (the differential
     pairs), feedback taken half a step back; the mixer before it overdrives when the sources are loud.
   - Curtis CEM3394 (Six-Trak): four cleaner OTA-style poles with internal resonance compensation.
   - Yamaha IG00156 (CS-80): a 12 dB high-pass into a 12 dB low-pass, each a saturating state-variable stage; the two
     layers (osc 1 / osc 2+3) have separate filters per voice.
   Voice cards: each voice slot has fixed part tolerances (cutoff, envelope times, VCA gain), wider on the VCO synths.
   Envelopes are RC: the attack charges toward ~1.3 V and stops at 1, decay and release discharge exponentially.
   VCA: OTA (Roland, Curtis) or transistor (Moog, Yamaha) soft saturation. Chorus: the Juno/JX bucket brigade (MN3009,
   256 stages) clocked by its LFO, sampled at the BBD clock between 9 kHz anti-alias and reconstruction filters, with the
   compander hiss the Juno chorus is known for. */
const ana_blep = (t, d) => t < d ? (t / d) * 2 - (t / d) * (t / d) - 1 : t > 1 - d ? ((t - 1) / d) * ((t - 1) / d) + 2 * ((t - 1) / d) + 1 : 0;
const tnh = x => x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x);
const ANA_HB = [-.000183, 0, .00235, 0, -.010062, 0, .030562, 0, -.082065, 0, .309433, .499932, .309433, 0, -.082065, 0, .030562, 0, -.010062, 0, .00235, 0, -.000183];  // 23-tap half-band (Blackman), 2:1
class ANA extends AudioWorkletProcessor{
  constructor(o){
    super(); const opt = (o && o.processorOptions) || {}; this.fs = sampleRate;
    this.cfg = Object.assign({model:'juno60', voices:6}, opt.cfg || {}); this.q = (opt.events || []).slice().sort((a, b) => a.f - b.f);
    this.vs = []; this.held = []; this.lfo = 0; this.lfoSH = 0; this.idle = 0; this.card = 0;
    this.ch = {b:new Float32Array(8192), w:0, p1:0, hL:0, hR:0, cL:1, cR:1, aa:[0, 0, 0], rl:[0, 0, 0], rr:[0, 0, 0]};
    this.hb = new Float32Array(ANA_HB.length * 2); this.hbw = 0;
    // eight voice cards, each with its own part tolerances and thermal state
    const R = () => Math.random() - .5; this.cards = Array.from({length:8}, () => ({fc:R(), env:R(), amp:R(), dr:[R() * .002, R() * .002, R() * .002], walk:[0, 0, 0]}));
    this.port.onmessage = e => { const d = e.data; if (d.type === 'ev'){ this.q.push(d.ev); this.q.sort((a, b) => a.f - b.f); } else if (d.type === 'cfg') Object.assign(this.cfg, d.cfg); };
  }
  voiceNew(n, v){ const k = this.card++ % ((this.cfg.mono ? 1 : this.cfg.voices) || 6) % 8;
    return {n, v, down:true, cd:this.cards[k], ph:[Math.random(), Math.random(), Math.random()], sub:1, sub2:1, e1:0, s1:0, e2:0, s2:0, z:[0, 0, 0, 0], y:[0, 0, 0, 0], sv:[0, 0, 0, 0], sv2:[0, 0, 0, 0], ac:[0, 0], sl:0, hp:0, hz:0, age:0, lfoD:0, cc:0, pw:.5, d1:1, h2b:0, h3:0, g:.1, gh:0, vca:0}; }
  noteOn(n, v){
    const C = this.cfg; this.held = this.held.filter(x => x !== n); this.held.push(n);
    const max = C.mono ? 1 : (C.voices || 6);
    if (C.mono){ let V = this.vs[0]; const legato = V && V.down; if (!V){ V = this.voiceNew(n, v); this.vs.push(V); } V.n = n; V.v = v; V.down = true; if (!legato || C.retrig){ V.s1 = 0; V.s2 = 0; } return; }
    if (this.vs.length >= max){ let k = this.vs.findIndex(x => !x.down); if (k < 0) k = 0; this.vs.splice(k, 1); }
    this.vs.push(this.voiceNew(n, v));
  }
  noteOff(n){
    const C = this.cfg; this.held = this.held.filter(x => x !== n);
    if (C.mono){ const V = this.vs[0]; if (!V) return; if (this.held.length){ V.n = this.held[this.held.length - 1]; } else V.down = false; return; }
    for (const V of this.vs) if (V.n === n && V.down) V.down = false;
  }
  // RC envelope: attack charges toward 1.3 and switches to decay at 1; card tolerance scales the times
  adsr(V, k, a, d, s, r, dt, tol){
    const t = x => (.0008 + x * x * 10) * tol; let e = k ? V.e2 : V.e1, st = k ? V.s2 : V.s1;
    if (!V.down) st = 3;
    if (st === 0){ e += (1.3 - e) * (1 - Math.exp(-dt / (t(a) * .55))); if (e >= 1){ e = 1; st = 1; } }
    else if (st === 1) e += (s - e) * (1 - Math.exp(-dt / (t(d) * .3))); else e += -e * (1 - Math.exp(-dt / (t(r) * .3)));
    if (k){ V.e2 = e; V.s2 = st; } else { V.e1 = e; V.s1 = st; } return e;
  }
  osc(V, i, hz, wave, pw, dt2){
    const dt = hz * dt2; let ph = V.ph[i] + dt; if (ph >= 1) ph -= 1; V.ph[i] = ph; if (dt >= .5) return 0;
    switch (wave){
      case 1: return (ph < pw ? 1 : -1) + ana_blep(ph, dt) - ana_blep((ph + 1 - pw) % 1, dt);      // pulse: comparator on the ramp
      case 2: return 1 - 4 * Math.abs(ph - .5);                                                      // triangle (shaper)
      case 3: { const tr = 1 - 4 * Math.abs(ph - .5); return tr * (1.5 - .5 * tr * tr); }            // sine: triangle through a diode shaper
      case 4: return (ph < .5 ? 1 : -1) + ana_blep(ph, dt) - ana_blep((ph + .5) % 1, dt);           // square
      case 5: return ph < .2 ? 10 * ph - 1 : 1 - 2.5 * (ph - .2);                                    // Minimoog "shark" (tri-saw)
      default: return 2 * ph - 1 - ana_blep(ph, dt);                                                 // ramp: integrator reset
    }
  }
  // one oversampled step of the low-pass: four TPT (zero-delay) integrator stages with the circuit's nonlinearity;
  // g = tan(pi fc / fs2); the resonance loop reads the last output (half a base-rate sample back)
  lp(V, z, x, g, res, type){
    const G = g / (1 + g), y = V.y;
    if (type === 'ladder'){                                         // Moog: tanh of each differential pair
      let inp = x - res * 4.1 * y[3];
      for (let k = 0; k < 4; k++){ const v = G * (tnh(inp) - tnh(z[k])), o = z[k] + v; z[k] = o + v; y[k] = o; inp = o; }
      return y[3] * (1 + res * 1.1);
    }
    if (type === 'cem'){                                            // CEM3394: cleaner poles, compensated resonance
      let inp = tnh((x * (1 + res * .6) - res * 3.9 * y[3]) * .6) / .6;
      for (let k = 0; k < 4; k++){ const v = G * (inp - z[k]), o = z[k] + v; z[k] = o + v; y[k] = o; inp = o; }
      return y[3];
    }
    // IR3109: OTA integrators saturate on their input difference; saturating resonance OTA
    let inp = x - tnh(res * 4.25 * y[3] * .9) / .9;
    for (let k = 0; k < 4; k++){ const v = G * tnh(inp - z[k]), o = z[k] + v; z[k] = o + v; y[k] = o; inp = o; }
    return y[3] * (1 + res * .35);
  }
  // CS-80 IG00156 pair: saturating state-variable high-pass then low-pass (12 dB each)
  csf(s, x, gh, gl, rh, rl){
    let k = 2 - rh * 1.9, a1 = 1 / (1 + gh * (gh + k)), v1 = a1 * (s[0] + gh * (x - s[1])), v2 = s[1] + gh * v1; s[0] = tnh(2 * v1 - s[0]); s[1] = 2 * v2 - s[1];
    const hp = x - k * v1 - v2;
    k = 2 - rl * 1.95; a1 = 1 / (1 + gl * (gl + k)); v1 = a1 * (s[2] + gl * (hp - s[3])); v2 = s[3] + gl * v1; s[2] = tnh(2 * v1 - s[2]); s[3] = 2 * v2 - s[3];
    return v2;
  }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, C = this.cfg, fs = this.fs, fs2 = fs * 2, dt = 1 / fs, dt2 = 1 / fs2;
    if (this.vs.length || (this.q.length && this.q[0].f < currentFrame + N)) this.idle = 0;
    else if ((this.idle += N) > fs * 3){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const ftype = C.filter || 'ir3109', vco = !!C.vco, lfoHz = .1 + (C.lfoRate || .3) * (C.lfoRate || .3) * 25, glide = C.glide ? Math.exp(-1 / (fs * (.005 + C.glide * C.glide * 1.5))) : 0, glide4 = glide * glide * glide * glide;
    const moog = ftype === 'ladder', cs = ftype === 'cs', roland = ftype === 'ir3109', tolW = vco ? 1 : .45;
    const acA = 1 - Math.exp(-2 * Math.PI * 6 / fs2), slA = 1 - Math.exp(-2 * Math.PI * 19000 / fs2), hb = this.hb, HB = ANA_HB, HN = HB.length;
    const mixDrive = moog ? .7 + ((C.m1 == null ? 1 : C.m1) + (C.m2 || 0) + (C.m3 || 0) + (C.noise || 0)) * .45 : 1;
    for (let i = 0; i < N; i++){
      const fr = currentFrame + i;
      while (this.q.length && this.q[0].f <= fr){ const ev = this.q.shift(); if (ev.on != null) this.noteOn(ev.on, ev.v == null ? .8 : ev.v); else this.noteOff(ev.off); }
      this.lfo += lfoHz / fs; if (this.lfo >= 1){ this.lfo -= 1; this.lfoSH = Math.random() * 2 - 1; }
      if (C.lfoWave === 4) this.lfoN = (this.lfoN || 0) + ((Math.random() * 2 - 1) * 1.7 - (this.lfoN || 0)) * .02;   // SH-101 noise modulator
      const lw = C.lfoWave || 0, lv = lw === 1 ? (this.lfo < .5 ? 1 : -1) : lw === 2 ? 1 - 2 * this.lfo : lw === 3 ? this.lfoSH : lw === 4 ? this.lfoN : 1 - 4 * Math.abs(this.lfo - .5);
      // thermal drift: slow random walk per VCO, bounded to a few cents
      if (vco && (fr & 63) === 0) for (const cd of this.cards) for (let k = 0; k < 3; k++){ cd.walk[k] = Math.max(-.003, Math.min(.003, cd.walk[k] + (Math.random() - .5) * 6e-5)); }
      let mixA = 0, mixB = 0;
      for (let vi = this.vs.length - 1; vi >= 0; vi--){
        const V = this.vs[vi], cd = V.cd;
        // control voltages (envelopes, pitch, cutoff) settle at a quarter of the audio rate, like the CV buses
        if (--V.cc < 0){ V.cc = 3; const dc = dt * 4; V.age += dc; V.lfoD = Math.min(1, V.lfoD + dc / (.001 + (C.lfoDelay || 0) * 3));
          const et = 1 + cd.env * .12 * tolW;
          const e1 = this.adsr(V, 0, C.a || 0, C.d == null ? .4 : C.d, C.s == null ? .6 : C.s, C.r == null ? .3 : C.r, dc, et);
          const e2 = C.ampGate ? (V.down ? (V.e2 = 1) : (V.e2 *= .9972)) : this.adsr(V, 1, C.a2 == null ? (C.a || 0) : C.a2, C.d2 == null ? (C.d == null ? .4 : C.d) : C.d2, C.s2 == null ? (C.s == null ? .6 : C.s) : C.s2, C.r2 == null ? (C.r == null ? .3 : C.r) : C.r2, dc, et);
          if (!V.down && e2 < 1e-4 && !C.ampGate || (C.ampGate && !V.down && V.e2 < 1e-3)){ this.vs.splice(vi, 1); continue; }
          const lfoAmt = lv * V.lfoD, target = 440 * Math.pow(2, (V.n - 69 + (C.transpose || 0) + lfoAmt * (C.lfoPitch || 0) * 1.5 + (C.bend || 0)) / 12);
          V.hz = glide && V.hz ? target + (V.hz - target) * glide4 : target;
          V.pw = Math.min(.95, Math.max(.05, (C.pw == null ? .5 : C.pw) + lfoAmt * (C.pwm || 0) * .45 + (C.pwEnv || 0) * e1 * .4));
          V.d1 = vco ? 1 + cd.dr[0] + cd.walk[0] : 1; const d2 = vco ? 1 + cd.dr[1] + cd.walk[1] : 1, d3 = vco ? 1 + cd.dr[2] + cd.walk[2] : 1;
          V.h2b = (C.m2 || C.ring) ? V.hz * Math.pow(2, ((C.i2 || 0) + (C.d2t || 0) * .01 + (C.env2Pitch || 0) * e1 * 12) / 12) * d2 : 0;
          V.h3 = C.m3 ? V.hz * Math.pow(2, ((C.i3 || 0) + (C.d3t || 0) * .01) / 12) * d3 : 0;
          const kt = C.kf == null ? .5 : C.kf, oct = (C.cut == null ? .5 : C.cut) * 10 + (C.fenv || 0) * e1 * 7 * (C.fenvInv ? -1 : 1) + lfoAmt * (C.lfoCut || 0) * 2 + (V.n - 60) / 12 * kt + (C.velCut || 0) * V.v * 2;
          const fc = Math.min(fs2 * .42, 25 * Math.pow(2, oct) * (1 + cd.fc * .06 * tolW)); V.g = Math.tan(Math.PI * fc / fs2);
          if (cs){ const fh = Math.min(fs2 * .4, 20 * Math.pow(2, (C.hpCut || 0) * 9)); V.gh = Math.tan(Math.PI * fh / fs2); }
          V.vca = e2 * (C.velAmp ? .4 + .6 * V.v : .85) * (1 + cd.amp * .05 * tolW);
        }
        const pw = V.pw, g = V.g, gh = V.gh, vca = V.vca, h2b = V.h2b, h3 = V.h3, res = C.res || 0, h1 = V.hz * V.d1 * (vco ? 1 + (Math.random() - .5) * 2e-5 : 1);
        for (let s2 = 0; s2 < 2; s2++){
          const ph1 = V.ph[0], o1 = this.osc(V, 0, h1, C.w1 || 0, pw, dt2), wrap1 = V.ph[0] < ph1;
          if (wrap1){ V.sub = -V.sub; if (V.sub > 0) V.sub2 = -V.sub2; }                                     // flip-flop dividers
          let o2 = 0, o3 = 0;
          if (h2b){ if (C.sync && wrap1) V.ph[1] = 0; o2 = this.osc(V, 1, h2b * (C.xmod ? 1 + o1 * C.xmod * .8 : 1), C.w2 || 0, pw, dt2); }
          if (h3) o3 = this.osc(V, 2, h3, C.w3 || 0, .5, dt2);
          const sub = C.subType === 2 ? V.sub2 : C.subType === 1 ? (V.sub2 > 0 && V.sub > 0 ? 1 : -.33) : V.sub;
          const ring = C.ring ? o1 * o2 * C.ring : 0, nz = (Math.random() * 2 - 1) * (C.noise || 0);
          let x = o1 * (C.m1 == null ? 1 : C.m1) + (cs ? 0 : o2 * (C.m2 || 0) + o3 * (C.m3 || 0)) + sub * (C.sub || 0) + nz + ring;
          // AC coupling and op-amp slew
          V.ac[0] += acA * (x - V.ac[0]); x -= V.ac[0]; V.sl += slA * (x - V.sl); x = V.sl * .5;
          if (moog) x = tnh(x * mixDrive) / mixDrive * 1.2;                                                     // Minimoog mixer overdrive
          if (C.hpf){ const fh = [0, 120, 250, 520][C.hpf | 0] || 0; if (fh){ V.hp += (1 - Math.exp(-2 * Math.PI * fh / fs2)) * (x - V.hp); x -= V.hp; } }
          let y;
          if (cs){ const xb = (o2 * (C.m2 || 0) + o3 * (C.m3 || 0) + nz * .5) * .5;
            y = (this.csf(V.sv, x, gh, g, C.hpRes || .1, res) + this.csf(V.sv2, xb, gh, g * 1.015, C.hpRes || .1, res)) * .6; }
          else y = this.lp(V, V.z, x, g, res, ftype);
          // VCA: OTA (Roland/Curtis) or transistor (Moog/Yamaha) soft saturation
          const out = roland || ftype === 'cem' ? tnh(y * vca * 1.4) / 1.4 : tnh(y * vca * 1.1) / 1.1;
          if (s2) mixB += out; else mixA += out;
        }
      }
      // decimate 2:1 through the half-band
      let w = this.hbw; hb[w] = hb[w + HN] = mixA; w = (w + 1) % HN; hb[w] = hb[w + HN] = mixB; w = (w + 1) % HN; this.hbw = w;
      let mix = hb[w + 11] * HB[11]; for (let k = 0; k < 11; k += 2) mix += HB[k] * (hb[w + k] + hb[w + 22 - k]);
      mix += (Math.random() - .5) * 2e-6 * this.vs.length;                                                     // voice-card noise floor
      if (!(Math.abs(mix) < 1e3)){ mix = 0; hb.fill(0); for (const V of this.vs){ V.z.fill(0); V.y.fill(0); V.sv.fill(0); V.sv2.fill(0); } }
      mix *= .5 * (C.vol == null ? .8 : C.vol);
      // chorus: bucket brigade clocked by the chorus LFO, sampled at its clock, with hiss
      let oL = mix, oR = mix; const cm = C.chorus | 0;
      if (cm){
        const ch = this.ch, rate = cm === 1 ? .513 : cm === 2 ? .863 : cm === 3 ? 9.75 : .6, depth = cm === 3 ? .3 : cm === 4 ? .9 : 1;
        ch.p1 += rate / fs; if (ch.p1 >= 1) ch.p1 -= 1; const tri = 1 - 4 * Math.abs(ch.p1 - .5);
        // 9 kHz anti-alias (3 poles) and a touch of compander hiss into the BBD
        const aa = ch.aa, fa = this.chFa || (this.chFa = 1 - Math.exp(-2 * Math.PI * 9000 / fs)); let u = mix + (Math.random() - .5) * 4e-4; for (let k = 0; k < 3; k++){ aa[k] += fa * (u - aa[k]); u = aa[k]; }
        ch.b[ch.w & 8191] = u;
        const base = cm === 3 ? 3.2 : 3.5 + 1.8 * depth, dLm = base + tri * 1.5 * depth, dRm = base - tri * 1.5 * depth;
        const rd = ms => { const r = ch.w - ms * fs / 1000, i0 = Math.floor(r), f = r - i0, a = ch.b[i0 & 8191], b = ch.b[(i0 + 1) & 8191]; return a + (b - a) * f; };
        // the BBD output is held at its clock (256 stages: f = 256 / (2 d)); reconstruction low-pass after
        ch.cL += 256 / (2 * dLm / 1000) / fs; if (ch.cL >= 1){ ch.cL -= Math.floor(ch.cL); ch.hL = rd(dLm); }
        ch.cR += 256 / (2 * dRm / 1000) / fs; if (ch.cR >= 1){ ch.cR -= Math.floor(ch.cR); ch.hR = rd(dRm); }
        let wl = ch.hL, wr = ch.hR; for (let k = 0; k < 3; k++){ ch.rl[k] += fa * (wl - ch.rl[k]); wl = ch.rl[k]; ch.rr[k] += fa * (wr - ch.rr[k]); wr = ch.rr[k]; }
        ch.w++;
        oL = mix * .7 + wl * .7; oR = mix * .7 + wr * .7;
      }
      L[i] = oL; R[i] = oR;
    }
    return true;
  }
}
registerProcessor('ana', ANA);
  class Cap extends AudioWorkletProcessor{
  constructor(){super();this.on=false;this.bl=[];this.br=[];this.n=0;this.f0=0;
    this.port.onmessage=e=>{this.on=e.data;if(!this.on)this.flush();};}
  flush(){if(!this.n)return;const l=new Float32Array(this.n),r=new Float32Array(this.n);let o=0;
    for(let k=0;k<this.bl.length;k++){l.set(this.bl[k],o);r.set(this.br[k],o);o+=this.bl[k].length;}
    this.port.postMessage({f:this.f0,l,r},[l.buffer,r.buffer]);this.bl=[];this.br=[];this.n=0;}
  process(inputs){const inp=inputs[0];
    if(this.on&&inp&&inp.length){if(!this.n)this.f0=currentFrame;this.bl.push(inp[0].slice());this.br.push((inp[1]||inp[0]).slice());this.n+=inp[0].length;if(this.n>=4096)this.flush();}
    return true;}}
registerProcessor('cap',Cap);

/* ================= Sequential Circuits Pro-One (1981) =================
   Built on the same Curtis chips as the Prophet-5, laid out as on the Pro-One panel:
   OSC A (CEM3340): saw, pulse, pulse width, SYNC to B.  OSC B (CEM3340): saw, triangle, pulse, pulse width, FINE,
   LO FREQ (B becomes a ~0.4-30 Hz modulation source) and KBD on / off.  MIXER: A, B, noise.
   FILTER (CEM3320 4-pole OTA low-pass): cutoff, emphasis (self-oscillates near full), env amount, keyboard amount off / half / full.
   Two CEM3310 ADSRs (filter, amplifier).  MODULATION: sources FILTER ENV and OSC B (direct or through the wheel) and the LFO
   (saw / triangle / square, own amount, direct or through the wheel), each routable to FREQ A, PW A, FREQ B, PW B and FILTER.
   GLIDE; key modes NORMAL (legato, no retrigger), RETRIG (every key retriggers), DRONE (VCA held open).
   Mono, last-note priority, per-unit VCO drift.  Filter and oscillators run at twice the sample rate. */
class ProOne extends AudioWorkletProcessor{
  constructor(o){
    super(); const opt = (o && o.processorOptions) || {}; this.fs = sampleRate;
    this.c = Object.assign({oAf:.5, oAsaw:1, oApul:0, oApw:.5, sync:0, oBf:.5, oBfine:.5, oBsaw:1, oBtri:0, oBpul:0, oBpw:.5, oBlo:0, oBkbd:1,
      mA:.8, mB:.6, mN:0, cut:.45, res:.2, fenv:.4, kbd:.5, fa:0, fd:.35, fs:.3, fr:.3, aa:0, ad:.4, as:.8, ar:.3,
      mFE:0, mOB:0, mLFO:0, pmWheel:0, lfoWheel:0, lfoF:.4, lfoSaw:0, lfoTri:1, lfoSq:0, dFA:0, dPWA:0, dFB:0, dPWB:0, dF:0, glide:0, mode:0, vol:.8}, opt.cfg || {});
    this.q = (opt.events || []).slice().sort((a, b) => a.f - b.f); this.held = []; this.gate = false; this.note = -1; this.pitch = null; this.wheel = 0; this.bend = 0;
    this.pa = Math.random(); this.pb = Math.random(); this.fe = 0; this.fst = 0; this.ae = 0; this.ast = 0; this.lad = [0, 0, 0, 0];
    this.lfoP = 0; this.dA = 0; this.dB = 0; this.idle = 1e9; this.offA = (Math.random() - .5) * 6; this.offB = (Math.random() - .5) * 6;
    this.port.onmessage = e => { const d = e.data;
      if (d.type === 'ev'){ this.q.push(d.ev); this.q.sort((a, b) => a.f - b.f); }
      else if (d.type === 'cfg') Object.assign(this.c, d.cfg);
      else if (d.type === 'wheel') this.wheel = d.v; else if (d.type === 'bend') this.bend = d.v;
      else if (d.type === 'panic'){ this.q = []; this.held = []; this.gate = false; this.fst = this.fst ? 4 : 0; this.ast = this.ast ? 4 : 0; } };
  }
  envT(k){ const x = Math.round(Math.max(0, Math.min(1, k || 0)) * 127) / 127; return .001 * Math.pow(10000, Math.pow(x, .8)); }   // 1 ms .. 10 s
  on(n){
    const legato = this.gate && this.held.length;
    this.held = this.held.filter(x => x !== n); this.held.push(n); this.note = n; this.gate = true;
    if (this.pitch == null) this.pitch = n;
    if (!legato || this.c.mode === 1){ this.fst = 1; this.ast = 1; }
  }
  off(n){
    this.held = this.held.filter(x => x !== n);
    if (this.held.length){ this.note = this.held[this.held.length - 1]; if (this.c.mode === 1){ this.fst = 1; this.ast = 1; } return; }
    this.gate = false;
  }
  env(e, st, a, d, s, r){   // CEM3310: attack toward 1.3 stops at 1, exponential decay / release
    if (!this.gate && st && st < 4) st = 4;
    if (st === 1){ e = 1.3 - (1.3 - e) * a; if (e >= 1){ e = 1; st = 2; } }
    else if (st === 2){ e = s + (e - s) * d; }
    else if (st === 4){ e *= r; if (e < 1e-5){ e = 0; st = 0; } }
    return [e, st];
  }
  blep(t, dt){ if (t < dt){ const x = t / dt; return x + x - x * x - 1; } if (t > 1 - dt){ const x = (t - 1) / dt; return x * x + x + x + 1; } return 0; }
  tanh(x){ if (x > 3) return 1; if (x < -3) return -1; const x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, fs = this.fs, C = this.c, fos = fs * 2;
    if (this.q.length && this.q[0].f < currentFrame + N) this.idle = 0;
    if (this.gate || this.ast || C.mode === 2) this.idle = 0; else this.idle += N;
    if (this.idle > fs){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const fa = Math.exp(-1.466 / (this.envT(C.fa) * fs)), fd = Math.exp(-3 / (this.envT(C.fd) * fs)), fr = Math.exp(-3 / (this.envT(C.fr) * fs));
    const aa = Math.exp(-1.466 / (this.envT(C.aa) * fs)), ad = Math.exp(-3 / (this.envT(C.ad) * fs)), ar = Math.exp(-3 / (this.envT(C.ar) * fs));
    const stA = Math.round(C.oAf * 48) - 24, stB = Math.round(C.oBf * 48) - 24 + (C.oBfine - .5) * 2;
    const lfoHz = .1 * Math.pow(300, C.lfoF), loHz = .4 * Math.pow(75, C.oBf);
    const glideK = C.glide > .005 ? 1 - Math.exp(-1 / ((.002 + C.glide * C.glide * 2.5) * fs)) : 1;
    const k = C.res <= .75 ? 3.9 * C.res / .75 : 3.9 + (C.res - .75) * 2.4, fenv = C.fenv * 9;
    const pmAmt = (C.pmWheel ? this.wheel : 1), lfoAmt = C.mLFO * (C.lfoWheel ? this.wheel : 1);
    const gk = -2 * Math.PI * 1.073 / fos, lg = Math.log2(261.63), LN2 = Math.LN2, vol = C.vol == null ? .8 : C.vol;
    let nsh = (C.lfoSaw ? 1 : 0) + (C.lfoTri ? 1 : 0) + (C.lfoSq ? 1 : 0) || 1;
    for (let i = 0; i < N; i++){
      const frame = currentFrame + i;
      while (this.q.length && this.q[0].f <= frame){ const ev = this.q.shift(); if (ev.on != null) this.on(ev.on); else this.off(ev.off); }
      if (this.pitch == null){ L[i] = R[i] = 0; continue; }
      this.pitch += (this.note + this.bend - this.pitch) * glideK;
      let r = this.env(this.fe, this.fst, fa, fd, C.fs, fr); this.fe = r[0]; this.fst = r[1];
      r = this.env(this.ae, this.ast, aa, ad, C.as, ar); this.ae = r[0]; this.ast = r[1];
      this.lfoP = (this.lfoP + lfoHz / fs) % 1; const p = this.lfoP;
      const lfo = ((C.lfoSaw ? 2 * p - 1 : 0) + (C.lfoTri ? 1 - 4 * Math.abs(p - .5) : 0) + (C.lfoSq ? (p < .5 ? 1 : -1) : 0)) / nsh;
      const kb = (this.pitch - 60) / 12;
      // OSC B (also the modulation source); LO FREQ drops it to sub-audio and frees it from the keyboard unless KBD is on
      const eB = (C.oBlo ? Math.log2(loHz) : lg + stB / 12) + (C.oBkbd ? kb : 0) + this.offB / 1200;
      const fB = Math.exp(eB * LN2);
      let ya = 0, yb = 0, outS = 0;
      for (let s = 0; s < 2; s++){
        const pbPrev = this.pb;
        // modulation sum (uses OSC B's current value)
        const ob = (C.oBsaw ? 2 * this.pb - 1 : 0) + (C.oBtri ? 1 - 4 * Math.abs(this.pb - .5) : 0) + (C.oBpul ? (this.pb < C.oBpw ? 1 : -1) : 0);
        const mod = (C.mFE * this.fe + C.mOB * ob) * pmAmt + lfoAmt * lfo;
        const dB = Math.min(.45, fB * Math.pow(2, C.dFB ? mod * 2 : 0) / fos);
        let pB = this.pb + dB, wrap = false; if (pB >= 1){ pB -= 1; wrap = true; } this.pb = pB;
        const pwB = Math.max(.02, Math.min(.98, C.oBpw + (C.dPWB ? mod * .45 : 0)));
        let oscB = 0;
        if (C.oBsaw) oscB += 2 * pB - 1 - this.blep(pB, dB);
        if (C.oBtri) oscB += 1 - 4 * Math.abs(pB - .5);
        if (C.oBpul) oscB += (pB < pwB ? 1 : -1) + this.blep(pB, dB) - this.blep((pB - pwB + 1) % 1, dB);
        // OSC A, hard-synced to B
        const eA = lg + kb + stA / 12 + this.offA / 1200 + (C.dFA ? mod * 2 : 0);
        const dA = Math.min(.45, Math.exp(eA * LN2) / fos);
        const pwA = Math.max(.02, Math.min(.98, C.oApw + (C.dPWA ? mod * .45 : 0)));
        let pA = this.pa + dA;
        if (C.sync && wrap && !C.oBlo){ pA = (pB / dB) * dA; } else if (pA >= 1) pA -= 1;
        this.pa = pA;
        let oscA = 0;
        if (C.oAsaw) oscA += 2 * pA - 1 - this.blep(pA, dA);
        if (C.oApul) oscA += (pA < pwA ? 1 : -1) + this.blep(pA, dA) - this.blep((pA - pwA + 1) % 1, dA);
        // MIXER -> CEM3320. Keyboard amount: off, half, full
        const x = C.mA * .55 * oscA + (C.oBlo ? 0 : C.mB * .55 * oscB) + C.mN * .45 * (Math.random() * 2 - 1) + (Math.random() - .5) * 4e-5;
        const fO = Math.round(C.cut * 127) / 12 + C.kbd * kb + fenv * this.fe + (C.dF ? mod * 5 : 0);
        const g = 1 - Math.exp(gk * Math.min(.21 * fos, 16.35 * Math.exp(fO * LN2)));
        const l = this.lad, u = this.tanh(x - k * l[3]), t0 = this.tanh(l[0]), t1 = this.tanh(l[1]), t2 = this.tanh(l[2]);
        l[0] += g * (u - t0); l[1] += g * (t0 - t1); l[2] += g * (t1 - t2); l[3] += g * (t2 - this.tanh(l[3]));
        outS += l[3];
      }
      const amp = C.mode === 2 ? 1 : this.ae;
      let o = this.tanh(outS * .5 * (1 + k * .45) * amp * 1.4) * .5 * vol;
      if (!(Math.abs(o) < 10)){ o = 0; this.lad.fill(0); }
      L[i] = o; R[i] = o;
    }
    return true;
  }
}
registerProcessor('proone', ProOne);

/* ================= Roland RS-09 Organ / Strings (1979) =================
   Divide-down and fully polyphonic: a top-octave synthesizer (twelve dividers of a ~2 MHz master clock) drives octave
   dividers, so every footage of a key is a phase-locked square wave.
   ORGAN: 16', 8', 4' and 2' squares keyed on and off (a few ms keying), voice I bright / II mellow (tone low-pass).
   STRINGS: 8' and 4' footages, each a stack of phase-locked squares at falling levels summed to a stepped sawtooth,
   with the ATTACK and SUSTAIN (release) envelope and a TONE low-pass.
   ENSEMBLE: three BBD delay lines swept by a slow chorus LFO and a faster vibrato LFO, 120 degrees apart; mode I faster,
   mode II slower. BALANCE mixes the two sections. LFO rates and BBD delays are estimates, not measured. */
const RS_TOS = [478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253], RS_CLK = 2.0009e6;
class RS09 extends AudioWorkletProcessor{
  constructor(o){
    super(); const opt = (o && o.processorOptions) || {}; this.fs = sampleRate;
    this.c = Object.assign({o16:1, o8:1, o4:0, o2:0, orgTone:0, orgVol:.7, s8:1, s4:1, sAtk:.3, sRel:.5, sTone:.6, strVol:.8, bal:.5, ens:1, vol:.8}, opt.cfg || {});
    this.q = (opt.events || []).slice().sort((a, b) => a.f - b.f); this.vs = new Map(); this.buf = new Float32Array(4096); this.w = 0; this.lc = 0; this.lv = 0; this.idle = 1e9;
    this.ol = [0, 0]; this.sl = [0, 0]; this.bb = [0, 0, 0];
    this.port.onmessage = e => { const d = e.data;
      if (d.type === 'ev'){ this.q.push(d.ev); this.q.sort((a, b) => a.f - b.f); }
      else if (d.type === 'cfg') Object.assign(this.c, d.cfg);
      else if (d.type === 'panic'){ this.q = []; for (const v of this.vs.values()) v.down = false; } };
  }
  hz(n){ const pc = ((n % 12) + 12) % 12; return RS_CLK / RS_TOS[pc] / Math.pow(2, (108 + pc - n) / 12); }
  sq(ph, dt){ if (dt >= .5) return 0; let y = ph < .5 ? 1 : -1;
    const b = (t) => { if (t < dt){ const x = t / dt; return x + x - x * x - 1; } if (t > 1 - dt){ const x = (t - 1) / dt; return x * x + x + x + 1; } return 0; };
    return y + b(ph) - b((ph + .5) % 1); }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, fs = this.fs, C = this.c;
    if (this.vs.size || (this.q.length && this.q[0].f < currentFrame + N)) this.idle = 0; else this.idle += N;
    if (this.idle > fs * 2){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const kA = 1 - Math.exp(-1 / (.003 * fs)), kR = 1 - Math.exp(-1 / (.008 * fs));
    const sA = 1 - Math.exp(-1 / ((.01 + C.sAtk * C.sAtk * 1.5) * fs)), sR = 1 - Math.exp(-1 / ((.05 + C.sRel * C.sRel * 4) * fs));
    const oF = 1 - Math.exp(-2 * Math.PI * (C.orgTone ? 1800 : 5200) / fs), sF = 1 - Math.exp(-2 * Math.PI * (700 * Math.pow(12, C.sTone)) / fs);
    const ft = [C.o16, C.o8, C.o4, C.o2], bal = C.bal, ens = C.ens | 0;
    const cHz = ens === 1 ? .9 : .5, vHz = ens === 1 ? 6.6 : 5.2, buf = this.buf, BN = buf.length, bbA = 1 - Math.exp(-2 * Math.PI * 8000 / fs);
    for (let i = 0; i < N; i++){
      const frame = currentFrame + i;
      while (this.q.length && this.q[0].f <= frame){ const ev = this.q.shift();
        if (ev.on != null){ let v = this.vs.get(ev.on); if (!v){ v = {ph:0, oe:0, se:0}; this.vs.set(ev.on, v); } v.down = true; v.dt = this.hz(ev.on - 12) / fs; }
        else { const v = this.vs.get(ev.off); if (v) v.down = false; } }
      let org = 0, str = 0;
      for (const [n, v] of this.vs){
        v.oe += ((v.down ? 1 : 0) - v.oe) * (v.down ? kA : kR);
        v.se += ((v.down ? 1 : 0) - v.se) * (v.down ? sA : sR);
        if (!v.down && v.oe < 1e-4 && v.se < 1e-4){ this.vs.delete(n); continue; }
        v.ph += v.dt; if (v.ph >= 1) v.ph -= 1;
        // divider outputs: octave k above the 16' is frac(ph * 2^k)
        const s = [], dt = v.dt;
        for (let k = 0, m = 1; k < 7; k++, m *= 2) s.push(this.sq((v.ph * m) % 1, dt * m));
        if (v.oe > 1e-4) org += (ft[0] * s[0] + ft[1] * s[1] + ft[2] * s[2] + ft[3] * s[3]) * v.oe;
        if (v.se > 1e-4){
          const saw = b => s[b] + .5 * s[b + 1] + .25 * s[b + 2] + .125 * s[b + 3];
          str += ((C.s8 ? saw(1) : 0) + (C.s4 ? saw(2) * .8 : 0)) * v.se;
        }
      }
      this.ol[0] += oF * (org - this.ol[0]); this.ol[1] += oF * (this.ol[0] - this.ol[1]);
      this.sl[0] += sF * (str - this.sl[0]); this.sl[1] += sF * (this.sl[0] - this.sl[1]);
      const mix = (this.ol[1] * C.orgVol * (1 - bal) * 2 * .12 + this.sl[1] * C.strVol * bal * 2 * .1);
      let oL = mix, oR = mix;
      if (ens){
        this.bb[0] += bbA * (mix - this.bb[0]); buf[this.w % BN] = this.bb[0];
        this.lc = (this.lc + cHz / fs) % 1; this.lv = (this.lv + vHz / fs) % 1;
        const tap = k => { const ph = k / 3, d = (.006 + .0022 * Math.sin(2 * Math.PI * (this.lc + ph)) + .0003 * Math.sin(2 * Math.PI * (this.lv + ph))) * fs;
          const r = this.w - d, i0 = Math.floor(r), f = r - i0; return buf[((i0 % BN) + BN) % BN] * (1 - f) + buf[(((i0 + 1) % BN) + BN) % BN] * f; };
        const a = tap(0), b = tap(1), c = tap(2); this.w++;
        oL = mix * .45 + (a + b * .5) * .55; oR = mix * .45 + (c + b * .5) * .55;
      }
      const g = C.vol == null ? .8 : C.vol;
      L[i] = Math.tanh(oL * g * 1.2) / 1.2; R[i] = Math.tanh(oR * g * 1.2) / 1.2;
    }
    return true;
  }
}
registerProcessor('rs09', RS09);

/* ================= Electro-Harmonix Vocoder (EH-0300, 1978 rack) =================
   14 channels at the published band centres 216, 298, 361, 478, 493, 614, 797, 1013, 1142, 1381, 1677, 2031, 2461 and
   2982 Hz (taken as listed; 478 and 493 Hz sit unusually close). Analysis and synthesis filters: two cascaded 2-pole
   band-passes per channel, bandwidth set by the neighbouring centres. Envelope followers: full-wave rectifier into
   attack / release smoothing. Built-in mic compressor (up to 20 dB), MIC and MUSIC sensitivity, HI BOOST (the mic's top
   end above the last band passed straight through so S and T stay clear), BLEND (vocoder to dry mic).
   Input 0 = MUSIC (carrier), input 1 = MIC (modulator). Band levels are posted back for the meters. */
const EH_BANDS = [216, 298, 361, 478, 493, 614, 797, 1013, 1142, 1381, 1677, 2031, 2461, 2982];
class EHVocoder extends AudioWorkletProcessor{
  constructor(o){
    super(); const fs = sampleRate; this.fs = fs;
    this.p = Object.assign({mic:.6, music:.6, hi:.5, comp:.6, blend:0, resp:.4, vol:.9, bypass:0}, (o && o.processorOptions && o.processorOptions.cfg) || {});
    const bp = (f, Q) => { const w = 2 * Math.PI * f / fs, al = Math.sin(w) / (2 * Q), a0 = 1 + al;
      return {b0:al / a0, b2:-al / a0, a1:-2 * Math.cos(w) / a0, a2:(1 - al) / a0, z1:0, z2:0}; };
    this.A = []; this.S = []; this.env = new Float64Array(14); this.env2 = new Float64Array(14); this.meter = new Float32Array(14); this.mt = 0;
    EH_BANDS.forEach((f, k) => {
      const lo = k ? EH_BANDS[k - 1] : f / 1.38, hi = k < 13 ? EH_BANDS[k + 1] : f * 1.21, bw = Math.max(25, (hi - lo) / 2), Q = Math.max(2, Math.min(9, f / bw));
      this.A.push([bp(f, Q), bp(f, Q)]); this.S.push([bp(f, Q), bp(f, Q)]);
    });
    const hp = f => { const w = 2 * Math.PI * f / fs, al = Math.sin(w) / (2 * .7071), a0 = 1 + al, c = Math.cos(w);
      return {b0:(1 + c) / 2 / a0, b1:-(1 + c) / a0, b2:(1 + c) / 2 / a0, a1:-2 * c / a0, a2:(1 - al) / a0, z1:0, z2:0}; };
    this.H = [hp(3300), hp(3300)]; this.cPk = 0; this.gC = 1;
    this.port.onmessage = e => Object.assign(this.p, e.data);
  }
  bq(q, x){ const y = q.b0 * x + q.z1; q.z1 = (q.b1 || 0) * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
  process(ins, outs){
    const mus = ins[0], mic = ins[1], L = outs[0][0], R = outs[0][1] || outs[0][0], N = L.length, P = this.p, fs = this.fs;
    const atk = 1 - Math.exp(-1 / ((.003 + P.resp * .012) * fs)), rel = 1 - Math.exp(-1 / ((.015 + P.resp * .12) * fs)), sm = 1 - Math.exp(-1 / (.006 * fs));
    const cA = 1 - Math.exp(-1 / (.002 * fs)), cR = Math.exp(-1 / (.25 * fs)), maxG = Math.pow(10, 20 * P.comp / 20);
    const gMic = Math.pow(10, (P.mic - .5) * 40 / 20) * 2, gMus = Math.pow(10, (P.music - .5) * 40 / 20) * 2;
    for (let i = 0; i < N; i++){
      const c = (mus && mus.length ? (mus.length > 1 ? (mus[0][i] + mus[1][i]) * .5 : mus[0][i]) : 0) * gMus;
      let m = (mic && mic.length ? (mic.length > 1 ? (mic[0][i] + mic[1][i]) * .5 : mic[0][i]) : 0) * gMic;
      if (P.bypass){ L[i] = R[i] = c; continue; }
      // compressor: peak follower, gain up to 20 dB toward a fixed reference
      const am = Math.abs(m); if (am > this.cPk) this.cPk += (am - this.cPk) * cA; else this.cPk *= cR;
      this.gC = Math.min(maxG, Math.max(1, .25 / Math.max(this.cPk, 1e-5))); m *= this.gC;
      let sum = 0;
      for (let k = 0; k < 14; k++){
        const A = this.A[k], S = this.S[k];
        const a = this.bq(A[1], this.bq(A[0], m)), r = Math.abs(a);
        let e = this.env[k]; e += (r - e) * (r > e ? atk : rel); this.env[k] = e;
        const e2 = this.env2[k] += (e - this.env2[k]) * sm;
        sum += this.bq(S[1], this.bq(S[0], c)) * e2;
        if (e2 > this.meter[k]) this.meter[k] = e2;
      }
      const hi = this.bq(this.H[1], this.bq(this.H[0], m)) * P.hi * .8;
      const voc = Math.tanh((sum * 9 + hi) * .9);
      const o = (voc * (1 - P.blend) + m * .5 * P.blend) * P.vol;
      L[i] = o; R[i] = o;
    }
    if ((this.mt += N) > fs / 30){ this.mt = 0; this.port.postMessage({meter:Array.from(this.meter)}); this.meter.fill(0); }
    return true;
  }
}
registerProcessor('ehvocoder', EHVocoder);

/* ================= Fender Precision Bass (a physical model, not a circuit) =================
   Four-voice digital waveguide string: a delay line one period long with a fractional (all-pass) tuning tap, a loop
   filter for string damping (brighter for roundwounds), pluck position as a comb on the excitation, the split-coil
   pickup position as a comb on the output, then the passive TONE control (250k pot into a 0.047uF cap, a low-pass that
   opens fully at 10). Styles: FINGER (soft, wide excitation), PICK (narrow, bright), SLAP (thumb on the last fret,
   a sharp transient, string slapping the frets as a short buzz), MUTE (palm: fast decay). Notes below E1 fold up an octave. */
class PBass extends AudioWorkletProcessor{
  constructor(o){
    super(); const opt = (o && o.processorOptions) || {}; this.fs = sampleRate;
    this.c = Object.assign({style:0, tone:.7, decay:.6, pos:.25, buzz:.2, vol:.8}, opt.cfg || {});
    this.q = (opt.events || []).slice().sort((a, b) => a.f - b.f); this.vs = []; this.idle = 1e9; this.tz = 0;
    this.port.onmessage = e => { const d = e.data; if (d.type === 'ev'){ this.q.push(d.ev); this.q.sort((a, b) => a.f - b.f); } else if (d.type === 'cfg') Object.assign(this.c, d.cfg); else if (d.type === 'panic'){ this.q = []; for (const v of this.vs) v.down = false; } };
  }
  pluck(n, vel){
    while (n < 28) n += 12;
    const C = this.c, fs = this.fs, f = 440 * Math.pow(2, (n - 69) / 12), P = fs / f, N = Math.floor(P - .5), frac = P - .5 - N;
    let v = this.vs.find(x => x.n === n) || (this.vs.length >= 4 ? this.vs.shift() : null);
    if (v) this.vs = this.vs.filter(x => x !== v);
    const buf = new Float32Array(N + 2), style = C.style | 0;
    // excitation: shaped noise / impulse, pluck position comb
    const width = style === 1 ? .08 : style === 2 ? .03 : style === 3 ? .2 : .22, pos = Math.max(.05, Math.min(.45, style === 2 ? .48 : C.pos));
    for (let i = 0; i < N; i++){ const x = i / N, tri = x < pos ? x / pos : (1 - x) / (1 - pos); buf[i] = tri * (1 - width) + (Math.random() * 2 - 1) * width; }
    const pk = Math.floor(N * pos), tmp = buf.slice(); for (let i = 0; i < N; i++) buf[i] = tmp[i] - .6 * tmp[(i + pk) % N];
    let lp = 0; const sm = style === 1 || style === 2 ? .2 : .55; for (let i = 0; i < N; i++){ lp += (buf[i] - lp) * (1 - sm); buf[i] = lp; }
    let m = 0; for (let i = 0; i < N; i++) m = Math.max(m, Math.abs(buf[i])); for (let i = 0; i < N; i++) buf[i] *= vel / (m || 1);
    const ap = (1 - frac) / (1 + frac);
    const t60 = (style === 3 ? .25 : .9 + C.decay * C.decay * 5) * (style === 2 ? .8 : 1) * Math.pow(55 / f, .3);
    const g = Math.pow(10, -3 / (t60 * f));
    const bright = style === 1 || style === 2 ? .82 : style === 3 ? .45 : .62;
    this.vs.push({n, buf, N, w:0, ap, apx:0, apy:0, lp:0, g, bright, down:true, rel:1, age:0, slap:style === 2 ? 1 : 0, pu:new Float32Array(64), pw:0, puD:Math.max(1, Math.min(63, Math.round(N * .21)))});
  }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], Nn = L.length, C = this.c, fs = this.fs;
    if (this.vs.length || (this.q.length && this.q[0].f < currentFrame + Nn)) this.idle = 0; else this.idle += Nn;
    if (this.idle > fs){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const tA = 1 - Math.exp(-2 * Math.PI * (300 + C.tone * C.tone * 7000) / fs), relK = Math.exp(-1 / (.03 * fs));
    for (let i = 0; i < Nn; i++){
      const fr = currentFrame + i;
      while (this.q.length && this.q[0].f <= fr){ const ev = this.q.shift(); if (ev.on != null) this.pluck(ev.on, .45 + .55 * (ev.v == null ? .8 : ev.v)); else for (const v of this.vs) if (v.n === ev.off || v.n === ev.off + 12 || v.n === ev.off + 24) v.down = false; }
      let out = 0;
      for (let k = this.vs.length - 1; k >= 0; k--){
        const v = this.vs[k];
        // read with all-pass fractional delay, loop low-pass, write back
        const x = v.buf[v.w], y = v.ap * x + v.apx - v.ap * v.apy; v.apx = x; v.apy = y;
        v.lp += (y - v.lp) * v.bright; let s = v.lp * v.g;
        if (!v.down){ v.rel *= relK; s *= relK; }
        // slap: the string hits the frets for the first moments
        if (v.slap && v.age < fs * .04) s = Math.max(-.7, Math.min(.7, s * 1.3));
        if (C.buzz && v.age < fs * .08 && (C.style | 0) !== 3) s += (Math.abs(s) > .6 ? (Math.random() - .5) * .05 * C.buzz : 0);
        v.buf[v.w] = s; v.w = (v.w + 1) % v.N; v.age++;
        // pickup position comb
        v.pu[v.pw] = s; const pp = v.pu[(v.pw - v.puD + 64) % 64]; v.pw = (v.pw + 1) % 64;
        out += s - .45 * pp;
        if (v.rel < 1e-4 || (v.age > fs * 12)) this.vs.splice(k, 1);
      }
      this.tz += (out - this.tz) * tA;
      const o = Math.tanh(this.tz * 1.6 * (C.vol == null ? .8 : C.vol)) * .8;
      L[i] = o; R[i] = o;
    }
    return true;
  }
}
registerProcessor('pbass', PBass);
