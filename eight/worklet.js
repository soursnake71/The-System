/* Omega 808 AudioWorkletProcessor 'eight-808': one Eight808 and one Seq808, sample-accurate steps.
   Load order: fx.js, seq.js, engine.js, worklet.js (they share the worklet global scope). */
'use strict';
class Eight808Processor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = new Eight808(sampleRate); this.s = new Seq808(sampleRate);
    this.e.set('tempo', this.s.bpm);
    this.pend = [];
    this.port.onmessage = ev => this.msg(ev.data);
    this.port.postMessage({t:'ready', params:Eight808.PARAMS, programs:Rev7.PROGRAMS.map(p => p.name), divisions:TC2290.DIVISIONS.map(d => d.name), bank:this.s.getBank()});
  }
  msg(m) {
    const e = this.e, s = this.s;
    switch (m.t) {
      case 'set': e.set(m.k, m.v); if (m.k === 'swing') s.swing = m.v; if (m.k === 'rev.program') this.echoRev(); break;
      case 'setMany': for (const k in m.p) { e.set(k, m.p[k]); if (k === 'swing') s.swing = m.p[k]; } if ('rev.program' in m.p) this.echoRev(); break;
      case 'hit': this.pend.push(m); break;
      case 'play': s.play(m.on); break;
      case 'tempo': case 'clock': s.setTempo(m.bpm); e.set('tempo', s.bpm); break;
      case 'bank': s.setBank(m.bank); e.set('tempo', s.bpm); break;
      case 'select': s.select(m); break;
      case 'tap': s.tap(); break;
      case 'edit': s.edit(m.which, m.n, m.var, m.track, m.step, m.on); break;
      case 'sample': if (m.clear) e.clearSample(m.v); else e.loadSample(m.v, m.data, m.sr); break;
      case 'pos': s.step = Math.max(0, m.step | 0); s.phase = 0; s.fired = false; break;
      case 'getBank': this.port.postMessage({t:'bank', bank:s.getBank()}); break;
    }
  }
  // a REV7 program also sets time, predelay and the rest: tell the UI their new values
  echoRev() { const p = {}; for (const k of ['rev.time','rev.predelay','rev.hf','rev.diffusion','rev.density','rev.lpf','rev.hpf','rev.er']) p[k] = this.e.get(k); this.port.postMessage({t:'params', p}); }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length, e = this.e;
    for (const h of this.pend) e.trigger(h.v, h.acc ? 1 : 0, h.vel == null ? 1 : h.vel);
    this.pend.length = 0;
    let done = 0;
    this.s.tick(n, (off, inf) => {
      if (off > done) { e.process(L, R, off - done, done); done = off; }
      e.trigFired = 0;
      const hits = [];
      if (inf.acc) e.trigger('AC', 1, 1);
      for (let i = 0; i < inf.n; i++) {
        const tr = Seq808.TRACKS[inf.tracks[i]], v = tr === 'TRIG' ? 'TRIG' : e.trackVoice(tr);
        e.trigger(v, inf.acc, 1); hits.push(v);
      }
      this.port.postMessage({t:'step', step:inf.step, part:inf.part, rhythm:inf.rhythm, which:inf.which, var:inf.var, hits, acc:inf.acc, trig:e.trigFired, time:currentTime + off / sampleRate});
    });
    if (n > done) e.process(L, R, n - done, done);
    if (R !== L && out.length < 2) {}
    return true;
  }
}
registerProcessor('eight-808', Eight808Processor);
