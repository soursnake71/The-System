/* Sound engines (Web Audio), MIDI out (Web MIDI or the Android app's USB MIDI) and MIDI file export. */
const Sound = (() => {
  let ctx = null, master = null, verbIn = null, delayIn = null;
  const lanes = {};   // lane id -> {gain}
  const mtof = n => 440 * Math.pow(2, (n - 69) / 12);

  function init(){
    if (ctx) return ctx;
    ctx = new (window.AudioContext || window.webkitAudioContext)({latencyHint:'interactive'});
    const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 4; comp.attack.value = 0.004; comp.release.value = 0.2;
    master = ctx.createGain(); master.gain.value = 0.8; master.connect(comp); comp.connect(ctx.destination);
    // reverb: generated decaying-noise impulse
    const len = ctx.sampleRate * 3.2, ir = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++){ const d = ir.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3.2); }
    const verb = ctx.createConvolver(); verb.buffer = ir; verbIn = ctx.createGain(); verbIn.gain.value = 0.5; verbIn.connect(verb); verb.connect(master);
    delayIn = ctx.createGain(); delayIn.gain.value = 0.5;
    // Simple: one dotted-eighth delay, darker each repeat
    const dl = ctx.createDelay(2), fb = ctx.createGain(), dlf = ctx.createBiquadFilter(), simpleOut = ctx.createGain(); dl.delayTime.value = 0.375; fb.gain.value = 0.38; dlf.type = 'lowpass'; dlf.frequency.value = 2800;
    delayIn.connect(dl); dl.connect(dlf); dlf.connect(fb); fb.connect(dl); dlf.connect(simpleOut); simpleOut.connect(master); simpleOut.connect(verbIn);
    // Edge (U2 style): driven input into a modulated dotted eighth (right) with feedback, plus a quarter-note tap (left)
    const drive = ctx.createWaveShaper(), curve = new Float32Array(1024); for (let i = 0; i < 1024; i++){ const x = i / 511.5 - 1; curve[i] = Math.tanh(1.6 * x) / Math.tanh(1.6); } drive.curve = curve;
    const eA = ctx.createDelay(2), eB = ctx.createDelay(2), eFb = ctx.createGain(), eFbB = ctx.createGain(), eHp = ctx.createBiquadFilter(), eLp = ctx.createBiquadFilter(), eLpB = ctx.createBiquadFilter();
    eHp.type = 'highpass'; eHp.frequency.value = 140; eLp.type = 'lowpass'; eLp.frequency.value = 4500; eLpB.type = 'lowpass'; eLpB.frequency.value = 4000;
    const lfo = ctx.createOscillator(), lfoG = ctx.createGain(); lfo.frequency.value = 0.7; lfoG.gain.value = 0.0018; lfo.connect(lfoG); lfoG.connect(eA.delayTime); lfoG.connect(eB.delayTime); lfo.start();
    const panA = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain(), panB = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain(), tapB = ctx.createGain(), edgeOut = ctx.createGain();
    if (panA.pan){ panA.pan.value = 0.65; panB.pan.value = -0.65; } tapB.gain.value = 0.6;
    delayIn.connect(drive); drive.connect(eA); drive.connect(eB);
    eA.connect(eHp); eHp.connect(eLp); eLp.connect(eFb); eFb.connect(eA); eLp.connect(panA); panA.connect(edgeOut);
    eB.connect(eLpB); eLpB.connect(eFbB); eFbB.connect(eB); eLpB.connect(tapB); tapB.connect(panB); panB.connect(edgeOut);
    edgeOut.connect(master); edgeOut.connect(verbIn);
    fx = {dl, fb, dlf, simpleOut, eA, eB, eFb, eFbB, eLp, eLpB, edgeOut};
    // gated reverb (80s drums): a dense, flat-topped impulse that stops dead, so the room cuts off after the gate time
    gatedIn = ctx.createGain(); gatedConv = ctx.createConvolver(); gatedOut = ctx.createGain();
    gatedIn.connect(gatedConv); gatedConv.connect(gatedOut); gatedOut.connect(master);
    // random echoes: three tempo-locked echo lines that individual notes get thrown into
    echoOut = ctx.createGain(); echoOut.connect(master); const eRev = ctx.createGain(); eRev.gain.value = 0.25; echoOut.connect(eRev); eRev.connect(verbIn);
    echoes = [-0.7, 0.7, 0].map(pan => { const input = ctx.createGain(), d = ctx.createDelay(3), f = ctx.createGain(), lp = ctx.createBiquadFilter(), hp = ctx.createBiquadFilter();
      lp.type = 'lowpass'; lp.frequency.value = 3800; hp.type = 'highpass'; hp.frequency.value = 220; f.gain.value = 0.48;
      const pn = ctx.createStereoPanner ? ctx.createStereoPanner() : ctx.createGain(); if (pn.pan) pn.pan.value = pan;
      input.connect(d); d.connect(lp); lp.connect(hp); hp.connect(f); f.connect(d); hp.connect(pn); pn.connect(echoOut); return {input, d}; });
    setFx(fxState); setSpace(spaceState);
    return ctx;
  }
  let gatedIn = null, gatedConv = null, gatedOut = null, echoOut = null, echoes = [], spaceState = {gtime:0.3, gmix:0.7, emix:0.6}, gatedLen = 0;
  function gatedImpulse(len){
    const n = Math.floor(ctx.sampleRate * (len + 0.02)), ir = ctx.createBuffer(2, n, ctx.sampleRate), cut = Math.floor(ctx.sampleRate * len);
    for (let c = 0; c < 2; c++){ const d = ir.getChannelData(c); for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * 0.5 * (i < cut ? 0.85 + 0.15 * i / cut : Math.max(0, 1 - (i - cut) / (n - cut))); }
    return ir;
  }
  function setMaster(v){ if (master) master.gain.setTargetAtTime(v, ctx.currentTime, 0.02); }
  // gated reverb time and level, echo level
  function setSpace(s){
    spaceState = Object.assign({}, spaceState, s); if (!gatedIn) return;
    if (Math.abs(spaceState.gtime - gatedLen) > 0.005){ gatedLen = spaceState.gtime; gatedConv.buffer = gatedImpulse(gatedLen); }
    gatedOut.gain.setTargetAtTime(spaceState.gmix * 0.9, ctx.currentTime, 0.05); echoOut.gain.setTargetAtTime(spaceState.emix, ctx.currentTime, 0.05);
    setDelayTime(fxBpm);
  }
  let fx = null, fxState = {mode:0, mix:0.5, fb:0.38, tone:2800}, fxBpm = 96;
  // the delay unit: mode 0 = simple dotted eighth, 1 = Edge (dotted eighth + quarter, modulated, driven)
  function setFx(s){
    fxState = Object.assign({}, fxState, s); if (!fx) return;
    const t = ctx.currentTime, edge = fxState.mode == 1, m = fxState.mix * 1.6;
    fx.simpleOut.gain.setTargetAtTime(edge ? 0 : m, t, 0.05); fx.edgeOut.gain.setTargetAtTime(edge ? m : 0, t, 0.05);
    fx.fb.gain.setTargetAtTime(fxState.fb, t, 0.05); fx.eFb.gain.setTargetAtTime(Math.min(0.92, fxState.fb * 1.1), t, 0.05); fx.eFbB.gain.setTargetAtTime(fxState.fb * 0.55, t, 0.05);
    fx.dlf.frequency.setTargetAtTime(fxState.tone, t, 0.05); fx.eLp.frequency.setTargetAtTime(fxState.tone * 1.4, t, 0.05); fx.eLpB.frequency.setTargetAtTime(fxState.tone * 1.2, t, 0.05);
    setDelayTime(fxBpm);
  }
  // delay times follow the tempo: a dotted eighth, and a quarter for the Edge's second tap
  function setDelayTime(bpm){
    fxBpm = bpm; if (!fx) return; const t = ctx.currentTime, beat = 60 / bpm;
    fx.dl.delayTime.setTargetAtTime(Math.min(1.9, beat * 0.75), t, 0.05); fx.eA.delayTime.setTargetAtTime(Math.min(1.9, beat * 0.75), t, 0.05); fx.eB.delayTime.setTargetAtTime(Math.min(1.9, beat), t, 0.05);
    [1 / 3, 1, 1.5].forEach((b, i) => echoes[i] && echoes[i].d.delayTime.setTargetAtTime(Math.min(2.9, beat * b), t, 0.05));   // 8th triplet, quarter, dotted quarter
  }
  /* mixer: one bus per lane and one for the drums: fader, sends to reverb / delay / gated reverb, and a meter */
  function bus(id){
    let l = lanes[id];
    if (!l){ l = lanes[id] = {gain:ctx.createGain(), rev:ctx.createGain(), dly:ctx.createGain(), gtd:ctx.createGain(), meter:ctx.createAnalyser()};
      l.gain.connect(master); l.gain.connect(l.rev); l.gain.connect(l.dly); l.gain.connect(l.gtd); l.rev.connect(verbIn); l.dly.connect(delayIn); l.gtd.connect(gatedIn);
      l.meter.fftSize = 512; l.gain.connect(l.meter); l.data = new Float32Array(512); }
    return l;
  }
  // m = {vol (already including mute / solo), rev, dly, gtd}
  function setBus(id, m){ if (!ctx) return; const l = bus(id), t = ctx.currentTime;
    l.gain.gain.setTargetAtTime(m.vol, t, 0.02); l.rev.gain.setTargetAtTime(m.rev, t, 0.02); l.dly.gain.setTargetAtTime(m.dly, t, 0.02); l.gtd.gain.setTargetAtTime(m.gtd || 0, t, 0.02); }
  // peak level of a bus, 0..1
  function level(id){ const l = lanes[id]; if (!l) return 0; l.meter.getFloatTimeDomainData(l.data); let p = 0; for (const x of l.data) p = Math.max(p, Math.abs(x)); return p; }
  // send a copy of one note into an echo line
  function throwEcho(src, amt, line){ if (!amt || !echoes.length) return; const g = ctx.createGain(); g.gain.value = amt; src.connect(g); g.connect(echoes[line % echoes.length].input); }

  function env(g, t, a, peak, d, sus, rel, end){
    g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(peak, t + a);
    g.gain.exponentialRampToValueAtTime(Math.max(0.0001, peak * sus), t + a + d);
    g.gain.setValueAtTime(Math.max(0.0001, peak * sus), end); g.gain.exponentialRampToValueAtTime(0.0001, end + rel);
    return end + rel + 0.05;
  }
  function osc(type, f, t, stop){ const o = ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(f, t); o.start(t); o.stop(stop); return o; }
  let noiseBuf = null;
  function noise(t, stop){ if (!noiseBuf){ noiseBuf = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate); const d = noiseBuf.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1; }
    const s = ctx.createBufferSource(); s.buffer = noiseBuf; s.loop = true; s.start(t); s.stop(stop); return s; }

  // each engine: (note, velocity 0-1, start, duration, out node) — builds its own little voice
  const ENGINES = {
    pluck:{name:'Pluck', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.Q.value = 6;
      lp.frequency.setValueAtTime(f * 10 * (0.5 + v), t); lp.frequency.exponentialRampToValueAtTime(f * 1.2, t + 0.25);
      const end = env(g, t, 0.003, 0.35 * v, 0.25, 0.25, 0.18, t + dur); const a = osc('sawtooth', f, t, end), b = osc('square', f * 1.003, t, end);
      a.connect(lp); b.connect(lp); lp.connect(g); g.connect(out); }},
    pad:{name:'Pad', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.setValueAtTime(f * 3, t); lp.frequency.linearRampToValueAtTime(f * 6, t + dur);
      const end = env(g, t, Math.min(1.2, dur * 0.4), 0.16 * v, 0.5, 0.8, 1.6, t + dur);
      for (const d of [-7, 0, 7]){ const o = osc('sawtooth', f, t, end); o.detune.value = d; o.connect(lp); }
      lp.connect(g); g.connect(out); }},
    bell:{name:'FM Bell', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), mg = ctx.createGain();
      const end = env(g, t, 0.002, 0.3 * v, 1.4, 0.05, 0.6, t + Math.max(dur, 0.6)); const c = osc('sine', f, t, end), m = osc('sine', f * 3.5, t, end);
      mg.gain.setValueAtTime(f * 4 * v, t); mg.gain.exponentialRampToValueAtTime(f * 0.2, t + 1.2); m.connect(mg); mg.connect(c.frequency); c.connect(g); g.connect(out); }},
    bass:{name:'Bass', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.Q.value = 8;
      lp.frequency.setValueAtTime(f * 8, t); lp.frequency.exponentialRampToValueAtTime(f * 1.5, t + 0.18);
      const end = env(g, t, 0.004, 0.45 * v, 0.2, 0.6, 0.08, t + dur); osc('sawtooth', f, t, end).connect(lp); const s = osc('sine', f / 2, t, end), sg = ctx.createGain(); sg.gain.value = 0.6; s.connect(sg); sg.connect(g);
      lp.connect(g); g.connect(out); }},
    glass:{name:'Glass', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(); const end = env(g, t, 0.02, 0.22 * v, 0.6, 0.3, 0.9, t + dur);
      osc('triangle', f, t, end).connect(g); const h = osc('sine', f * 2.01, t, end), hg = ctx.createGain(); hg.gain.value = 0.3; h.connect(hg); hg.connect(g); g.connect(out); }},
    lead:{name:'Lead', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = f * 5; lp.Q.value = 2;
      const end = env(g, t, 0.01, 0.2 * v, 0.1, 0.7, 0.15, t + dur); const o = osc('sawtooth', f, t, end), lfo = osc('sine', 5.5, t, end), lg = ctx.createGain();
      lg.gain.setValueAtTime(0, t); lg.gain.linearRampToValueAtTime(f * 0.008, t + 0.3); lfo.connect(lg); lg.connect(o.frequency); o.connect(lp); lp.connect(g); g.connect(out); }},
    perc:{name:'Perc', play(n, v, t, dur, out){ const f = mtof(n), g = ctx.createGain(), end = t + 0.5; g.gain.setValueAtTime(0.6 * v, t); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.35);
      const o = osc('sine', f * 2, t, end); o.frequency.exponentialRampToValueAtTime(f / 2, t + 0.12); o.connect(g);
      const nz = noise(t, t + 0.1), ng = ctx.createGain(), hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 3000; ng.gain.setValueAtTime(0.25 * v, t); ng.gain.exponentialRampToValueAtTime(0.0001, t + 0.06);
      nz.connect(hp); hp.connect(ng); ng.connect(g); g.connect(out); }},
    obxa:{name:'OB-Xa', play(n, v, t, dur, out, P){ obxa(n, v, t, dur, out, P || SkyPresets.defaults(SkyPresets.PATCH)); }},
    none:{name:'MIDI only', play(){}},
  };

  /* OB-Xa style voice: two oscillators (saw or pulse) with detune and interval, a 12 or 24 dB resonant lowpass
     with its own ADSR and key tracking, amp ADSR, and an LFO for vibrato and pulse-width movement */
  const pulseCache = {};
  function pulseWave(pw){
    const k = Math.round(pw * 50); if (pulseCache[k]) return pulseCache[k];
    const N = 64, re = new Float32Array(N), im = new Float32Array(N), w = k / 50;
    for (let i = 1; i < N; i++){ const a = 2 / (i * Math.PI) * Math.sin(i * Math.PI * w); re[i] = a * Math.cos(i * Math.PI * w); im[i] = a * Math.sin(i * Math.PI * w); }
    return pulseCache[k] = ctx.createPeriodicWave(re, im);
  }
  function obxa(n, v, t, dur, out, P){
    const f = mtof(n), end = t + dur, stop = end + Math.max(P.ar, P.fr) * 4 + 0.05;
    const mk = (w, freq, detune, pwOff) => { const o = ctx.createOscillator(); if (w) o.setPeriodicWave(pulseWave(Math.max(0.05, Math.min(0.5, P.pw + pwOff)))); else o.type = 'sawtooth';
      o.frequency.setValueAtTime(freq, t); o.detune.value = detune; o.start(t); o.stop(stop); return o; };
    const iv = SkyPresets.INTERVALS[P.int2 | 0] || 0, f2 = f * Math.pow(2, iv / 12);
    // pulse-width movement is approximated with a second, slightly wider pulse copy
    const o1 = mk(P.w1, f, -P.det / 2, 0), o2 = mk(P.w2, f2, P.det / 2, 0), mix = ctx.createGain(), g2 = ctx.createGain();
    g2.gain.value = P.mix2; o1.connect(mix); o2.connect(g2); g2.connect(mix);
    if (P.pwm > 0 && (P.w1 || P.w2)){ const o3 = mk(1, f, P.det, P.pwm), g3 = ctx.createGain(); g3.gain.value = 0.5; o3.connect(g3); g3.connect(mix); }
    if (P.vib > 0){ const l = ctx.createOscillator(), lg = ctx.createGain(); l.frequency.value = P.lfo; lg.gain.value = P.vib; l.connect(lg); lg.connect(o1.detune); lg.connect(o2.detune); l.start(t); l.stop(stop); }
    // filter: key-tracked cutoff, envelope in octaves
    const base = Math.min(16000, P.cut * Math.pow(2, (n - 60) / 24)), top = Math.min(18000, base * Math.pow(2, P.env * (0.6 + 0.4 * v))), sus = Math.min(18000, base * Math.pow(2, P.env * P.fs));
    const stages = P.poles ? [0.7, 0.6 + P.res * 14] : [0.6 + P.res * 12];
    let node = mix;
    for (const q of stages){ const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.Q.value = q;
      lp.frequency.setValueAtTime(base, t); lp.frequency.exponentialRampToValueAtTime(top, t + P.fa); lp.frequency.exponentialRampToValueAtTime(Math.max(20, sus), t + P.fa + P.fd);
      lp.frequency.setValueAtTime(Math.max(20, sus), end); lp.frequency.exponentialRampToValueAtTime(base, end + P.fr); node.connect(lp); node = lp; }
    const g = ctx.createGain(); env(g, t, P.aa, 0.2 * v, P.ad, Math.max(0.0001, P.as), P.ar, end);
    node.connect(g); g.connect(out);
  }

  /* ---- drum kit: twelve synthesized voices; the kit is a set of numbers (see SkyPresets.KIT) ---- */
  function decayGain(t, peak, d){ const g = ctx.createGain(); g.gain.setValueAtTime(peak, t); g.gain.exponentialRampToValueAtTime(0.0001, t + d); return g; }
  function filtered(src, type, f, q = 0.7){ const b = ctx.createBiquadFilter(); b.type = type; b.frequency.value = f; b.Q.value = q; src.connect(b); return b; }
  function drum(inst, v, t, pan, K, vol = 1, echoAmt = 0, echoLine = 0){
    if (!ctx) return;
    const b = bus('drums');
    let out = b.gain; if (ctx.createStereoPanner){ const p = ctx.createStereoPanner(); p.pan.value = Math.max(-1, Math.min(1, pan || 0)); p.connect(b.gain); out = p; }
    throwEcho(out, echoAmt, echoLine);
    const a = v * vol * K.lvl;
    if (inst === 'k'){ const o = osc('sine', K.kp, t, t + K.kd + 0.1); o.frequency.exponentialRampToValueAtTime(K.kf, t + 0.045); const g = decayGain(t, 0.9 * a, K.kd); o.connect(g); g.connect(out);
      if (K.kc > 0){ const c = filtered(noise(t, t + 0.02), 'highpass', 2500), cg = decayGain(t, 0.35 * K.kc * a, 0.012); c.connect(cg); cg.connect(out); } }
    else if (inst === 's'){ for (const m of [1, 1.47]){ const o = osc('triangle', K.sf * m, t, t + 0.25), g = decayGain(t, 0.32 * a / m, 0.11); o.connect(g); g.connect(out); }
      const nz = filtered(noise(t, t + K.sd + 0.05), 'highpass', 1500), ng = decayGain(t, 0.45 * K.ss * a, K.sd); nz.connect(ng); ng.connect(out); }
    else if (inst === 'ch' || inst === 'oh'){ const d = inst === 'ch' ? K.hd : K.ho, nz = filtered(filtered(noise(t, t + d + 0.05), 'highpass', K.ht), 'peaking', K.ht * 1.3, 2), g = decayGain(t, 0.28 * a, d); nz.connect(g); g.connect(out); }
    else if (inst === 'cl'){ const nz = filtered(noise(t, t + K.cd + 0.1), 'bandpass', 1200, 1.4), g = ctx.createGain();
      g.gain.setValueAtTime(0.0001, t); for (const k of [0, 0.011, 0.022]){ g.gain.setValueAtTime(0.5 * a, t + k); g.gain.exponentialRampToValueAtTime(0.05 * a, t + k + 0.009); }
      g.gain.setValueAtTime(0.4 * a, t + 0.033); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.033 + K.cd); nz.connect(g); g.connect(out); }
    else if (inst === 't'){ const o = osc('sine', K.tf * 1.3, t, t + K.td + 0.1); o.frequency.exponentialRampToValueAtTime(K.tf, t + 0.06); const g = decayGain(t, 0.6 * a, K.td); o.connect(g); g.connect(out); }
    else if (inst === 'r'){ const o = osc('square', K.rf, t, t + 0.06), g = decayGain(t, 0.22 * a, 0.03), h = filtered(o, 'highpass', K.rf * 0.8); h.connect(g); g.connect(out); }
    else if (inst === 'p'){ for (const m of [1, K.pr]){ const o = osc('square', K.pf * m, t, t + K.pd + 0.05), g = decayGain(t, 0.14 * a, K.pd), bp = filtered(o, 'bandpass', K.pf * m, 2); bp.connect(g); g.connect(out); } }
    // cymbal: six detuned squares (the 808 trick) plus bright noise
    else if (inst === 'cy'){ const g = decayGain(t, 0.16 * a, K.cy), hp = ctx.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = K.ct;
      for (const m of [1, 1.342, 1.2312, 1.6532, 1.9523, 2.1523]){ const o = osc('square', K.ct / 13 * m, t, t + K.cy + 0.05); o.connect(hp); }
      const nz = filtered(noise(t, t + K.cy + 0.05), 'highpass', K.ct * 1.2), ng = ctx.createGain(); ng.gain.value = 0.6; nz.connect(ng); ng.connect(hp); hp.connect(g); g.connect(out); }
    else if (inst === 'sh'){ const nz = filtered(noise(t, t + K.shd + 0.05), 'highpass', 6000), g = ctx.createGain();
      g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(0.22 * a, t + 0.012); g.gain.exponentialRampToValueAtTime(0.0001, t + K.shd); nz.connect(g); g.connect(out); }
    else if (inst === 'cg'){ const o = osc('sine', K.cgf * 1.12, t, t + K.cgd + 0.1); o.frequency.exponentialRampToValueAtTime(K.cgf, t + 0.03); const g = decayGain(t, 0.55 * a, K.cgd); o.connect(g); g.connect(out);
      const h = osc('sine', K.cgf * 2.7, t, t + 0.05), hg = decayGain(t, 0.1 * a, 0.03); h.connect(hg); hg.connect(out); }
    else if (inst === 'tb'){ const nz = filtered(filtered(noise(t, t + K.tbd + 0.05), 'highpass', 7000), 'peaking', 9500, 4), g = ctx.createGain();
      g.gain.setValueAtTime(0.0001, t); for (const k of [0, 0.006, 0.013]){ g.gain.setValueAtTime(0.22 * a, t + k); g.gain.exponentialRampToValueAtTime(0.06 * a, t + k + 0.005); }
      g.gain.setValueAtTime(0.18 * a, t + 0.018); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.018 + K.tbd); nz.connect(g); g.connect(out); }
  }

  // pan: -1 (west) .. 1 (east)
  // l = {patch} plus, for buses the app hasn't set up, {vol, rev, dly}; echoAmt > 0 throws this note into echo line echoLine
  function note(laneId, engine, n, v, t, dur, pan, l, echoAmt = 0, echoLine = 0){
    const e = ENGINES[engine]; if (!e || engine === 'none' || !ctx) return;
    n = Math.max(12, Math.min(115, n));
    const b = lanes[laneId] ? lanes[laneId] : (setBus(laneId, {vol:l.vol, rev:l.rev, dly:l.dly, gtd:0}), lanes[laneId]); let dest = b.gain;
    if (ctx.createStereoPanner){ const p = ctx.createStereoPanner(); p.pan.value = Math.max(-1, Math.min(1, pan || 0)); p.connect(b.gain); dest = p; }
    throwEcho(dest, echoAmt, echoLine);
    e.play(n, v, t, dur, dest, l.patch);
  }

  /* ---- MIDI out ---- */
  const midi = {access:null, out:null, nativeId:null, list:[]};
  const native = window.SkyNative || null;
  async function midiInit(onChange){
    if (native){
      window.__skyMidi = () => { midiRefresh(); onChange && onChange(); };
      midiRefresh(); return true;
    }
    if (!navigator.requestMIDIAccess) return false;
    try { midi.access = await navigator.requestMIDIAccess({sysex:false}); } catch (e) { return false; }
    midi.access.onstatechange = () => { midiRefresh(); onChange && onChange(); };
    midiRefresh(); return true;
  }
  function midiRefresh(){
    if (native){ try { midi.list = JSON.parse(native.midiOutputs()); } catch (e) { midi.list = []; } return; }
    midi.list = midi.access ? [...midi.access.outputs.values()].map(o => ({id:o.id, name:o.name})) : [];
  }
  function midiSelect(id){
    midi.out = null; midi.nativeId = null;
    if (!id) { if (native) native.midiOpen(''); return; }
    if (native){ midi.nativeId = id; native.midiOpen(id); return; }
    if (midi.access) midi.out = midi.access.outputs.get(id) || null;
  }
  // t is an AudioContext time; converted to the clock the output wants
  function midiSend(bytes, t){
    const delayMs = ctx ? Math.max(0, (t - ctx.currentTime) * 1000) : 0;
    if (native && midi.nativeId){ native.midiSend(bytes.map(b => b.toString(16).padStart(2, '0')).join(''), delayMs); return; }
    if (midi.out) try { midi.out.send(bytes, performance.now() + delayMs); } catch (e) {}
  }
  function midiNote(ch, n, v, t, dur){
    const c = (ch - 1) & 15, nn = Math.max(0, Math.min(127, Math.round(n)));
    midiSend([0x90 | c, nn, Math.max(1, Math.min(127, Math.round(v * 127)))], t); midiSend([0x80 | c, nn, 0], t + dur);
  }
  const midiActive = () => !!(midi.out || midi.nativeId);

  /* ---- Standard MIDI File writer (format 1, one track per lane) ---- */
  function smf(tracks, bpm, ppq = 480){
    const vlq = n => { const b = [n & 127]; while ((n >>= 7)) b.unshift((n & 127) | 128); return b; };
    const u32 = n => [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255], str = s => [...s].map(c => c.charCodeAt(0) & 255);
    const chunk = (id, data) => [...str(id), ...u32(data.length), ...data];
    const us = Math.round(60e6 / bpm), head = chunk('MThd', [0, 1, 0, tracks.length + 1, ppq >> 8, ppq & 255]);
    const out = [...head, ...chunk('MTrk', [0, 0xFF, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255, 0, 0xFF, 0x2F, 0])];
    for (const tr of tracks){
      const ev = [];
      for (const n of tr.notes){ const on = Math.round(n.t * bpm / 60 * ppq), off = on + Math.max(1, Math.round(n.d * bpm / 60 * ppq)), c = (n.ch - 1) & 15;
        ev.push([on, 1, [0x90 | c, n.n, Math.max(1, Math.round(n.v * 127))]], [off, 0, [0x80 | c, n.n, 0]]); }
      ev.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
      const d = [0, 0xFF, 0x03, ...vlq(tr.name.length), ...str(tr.name)]; let last = 0;
      for (const [t, , b] of ev){ d.push(...vlq(t - last), ...b); last = t; }
      d.push(0, 0xFF, 0x2F, 0); out.push(...chunk('MTrk', d));
    }
    return new Uint8Array(out);
  }

  return {init, get ctx(){ return ctx; }, ENGINES, note, drum, setFx, setSpace, setBus, setMaster, level, midi, midiInit, midiRefresh, midiSelect, midiSend, midiNote, midiActive, smf, setDelayTime};
})();
