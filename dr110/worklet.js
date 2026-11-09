/* Omega DR-110 audio thread: the engine (engine.js) and the sequencer (seq.js), loaded before this file into the same
   AudioWorkletGlobalScope. Messages in: {type:'state', mem}, 'param', 'params', 'start', 'stop', 'select', 'mode',
   'hit', 'tap', 'bpm', 'ext', 'clock', 'sample', 'swing'. Out: 'step', 'hit' (for MIDI out), 'write', 'stop', 'meter', 'clock'. */
'use strict';
class DR110Processor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.e = new DR110.Engine(sampleRate);
    this.s = new DRSEQ.Sequencer(this.e, sampleRate);
    this.L = new Float64Array(128); this.R = new Float64Array(128);
    const post = m => this.port.postMessage(m);
    this.s.onStep = (step, pat, measure) => post({type:'step', step, pat, measure});
    this.s.onHit = (v, acc) => post({type:'hit', v, acc});
    this.s.onStop = () => post({type:'stop'});
    this.s.onClock = () => post({type:'clock'});
    this.meterN = 0;
    this.port.onmessage = ({data:m}) => {
      const s = this.s, e = this.e;
      switch (m.type){
        case 'state': s.mem = m.mem; break;
        case 'param': e.set(m.i, m.v); if (m.i === DR110.P.tempo) s.bpm = m.v; break;
        case 'params': e.setAll(m.p); s.bpm = m.p[DR110.P.tempo]; break;
        case 'start': s.mode = m.mode || s.mode; if (m.song !== undefined) s.song = m.song; if (m.pat !== undefined) s.pat = m.pat; s.start(); if (!s.running) post({type:'stop'}); break;
        case 'stop': s.stop(); break;
        case 'select': s.select(m.pat); break;
        case 'mode': s.mode = m.mode; break;
        case 'hit': e.trigger(m.v, m.acc); break;
        case 'tap': { e.trigger(m.v, m.acc); const w = s.tapWrite(m.v, m.acc); if (w) post(Object.assign({type:'write'}, w)); break; }
        case 'bpm': s.bpm = m.v; break;
        case 'ext': s.ext = m.on; break;
        case 'clock': s.clockTick(); break;
        case 'swing': s.swing = m.v; break;
        case 'sweep': e.sweep(); break;
        case 'sample': e.setSample(m.v, m.d, m.sr, m.acc); break;
      }
    };
  }
  process(inputs, outputs){
    const out = outputs[0], n = out[0].length;
    if (this.L.length < n){ this.L = new Float64Array(n); this.R = new Float64Array(n); }
    this.s.render(this.L, this.R, 0, n);
    const l = out[0], r = out[1] || out[0];
    for (let i = 0; i < n; i++){ l[i] = this.L[i]; if (out[1]) r[i] = this.R[i]; }
    // meters for the mixer, about 30 times a second
    this.meterN += n;
    if (this.meterN > sampleRate / 30){ this.meterN = 0; this.port.postMessage({type:'meter', m:this.e.meter.slice()}); this.e.meter.fill(0); }
    return true;
  }
}
registerProcessor('dr110', DR110Processor);
