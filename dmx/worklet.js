/* Omega DMX audio thread: the engine (engine.js, after fx.js) loaded into the same AudioWorkletGlobalScope.
   In: {type:'params', p}, 'faders' (f: nine positions), 'circuit' (on), 'tempo' (bpm), 'sound' (vi, d, rate, card, group,
   open, panL, panR, ratio), 'hit' (vi, vel, t: context time, gate: seconds or -1), 'release' (vi), 'click' (t, loud).
   Out: 'meter' (15 a second: each voice's peak). Hits are placed at their sample inside the block. */
'use strict';
class DmxProcessor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.e = new DMXE.Engine(sampleRate); this.q = []; this.L = new Float64Array(128); this.R = new Float64Array(128); this.n = 0;
    this.port.onmessage = ({data:x}) => {
      const e = this.e;
      switch (x.type){
        case 'params': e.setAll(x.p); break;
        case 'faders': e.setFaders(x.f); break;
        case 'circuit': e.setCircuit(x.on); break;
        case 'tempo': e.setTempo(x.bpm); break;
        case 'sound': e.setSound(x.vi, x.d, x.rate, x.card, x.group, x.open, x.panL, x.panR, x.ratio); break;
        case 'nosound': for (let i = x.from; i < 20; i++) e.setSound(i, null); break;
        case 'hit': case 'click': this.q.push(Object.assign({f:Math.round(x.t * sampleRate)}, x)); this.q.sort((a, b) => a.f - b.f); break;
        case 'release': e.release(x.vi); break;
      }
    };
  }
  process(inputs, outputs){
    const out = outputs[0], n = out[0].length, f0 = currentFrame;
    if (this.L.length < n){ this.L = new Float64Array(n); this.R = new Float64Array(n); }
    while (this.q.length && this.q[0].f < f0 + n){
      const h = this.q.shift(), off = Math.max(0, h.f - f0);
      if (h.type === 'hit') this.e.trigger(h.vi, h.vel, off, h.gate < 0 ? -1 : Math.round(h.gate * sampleRate)); else this.e.click(h.loud, off);
    }
    this.e.process(this.L, this.R, n);
    const l = out[0], r = out[1] || out[0];
    for (let i = 0; i < n; i++){ l[i] = this.L[i]; if (out[1]) r[i] = this.R[i]; }
    this.n += n;
    if (this.n > sampleRate / 15){ this.n = 0; this.port.postMessage({type:'meter', m:Array.from(this.e.meter)}); this.e.meter.fill(0); }
    return true;
  }
}
registerProcessor('dmx', DmxProcessor);
