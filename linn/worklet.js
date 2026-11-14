/* Omega Linn audio thread: the engine (engine.js) and the firmware (machine.js), loaded before this file into the same
   AudioWorkletGlobalScope. In: 'key' (id, down), 'note' (s: a sound 0-22), 'off' (channel), 'params', 'rom' (group,
   layers), 'mem', 'state', 'edit', 'len', 'sel', 'ext', 'clock', 'midistart', 'midistop', 'metro', 'hitsOut'.
   Out: 'disp' (30 a second: displays, LEDs, position, meters), 'mem' (when the memory changed), 'hit' (for MIDI out),
   'clock', 'trig', 'start', 'stop'. */
'use strict';
class LinnProcessor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.e = new OLINN.Engine(sampleRate);
    this.m = new OLMACH.Machine(this.e, sampleRate);
    this.L = new Float64Array(128); this.R = new Float64Array(128);
    const post = x => this.port.postMessage(x);
    this.hitsOut = false;
    this.m.onHit = s => { if (this.hitsOut) post({type:'hit', s}); };
    this.m.onClock = () => post({type:'clock'});
    this.m.onTrig = () => post({type:'trig'});
    this.m.onStart = () => post({type:'start'});
    this.m.onStop = () => post({type:'stop'});
    this.n = 0; this.ver = -1; this.memAt = 0;
    this.port.onmessage = ({data:x}) => {
      const m = this.m, e = this.e;
      switch (x.type){
        case 'key': m.key(x.id, x.down); break;
        case 'note': m.inst(x.s); break;
        case 'off': e.release(x.v); break;
        case 'params': e.setAll(x.p); m.bpm = x.p[OLINN.P.tempo]; break;
        case 'rom': e.setGroup(x.g, x.layers); break;
        case 'mem': m.mem = x.mem; this.ver = m.version; break;
        case 'state': m.pat = m.ppat = x.pat; m.song = x.song; if (typeof x.mode === 'number' && !m.running) m.mode = x.mode; break;
        case 'edit': m.edit(x.p, x.t, x.s, x.on); break;
        case 'len': m.setLen(x.p, x.beats); break;
        case 'sel': m.sel = x.v; break;
        case 'ext': m.ext = x.on; m.changed(); break;
        case 'clock': m.clockTick(); break;
        case 'midistart': if (!m.running && m.ext) m.play(); break;
        case 'midistop': if (m.running) m.stop(); break;
        case 'metro': m.metroAlways = x.on; break;
        case 'hitsOut': this.hitsOut = x.on; break;
      }
    };
  }
  process(inputs, outputs){
    const out = outputs[0], n = out[0].length;
    if (this.L.length < n){ this.L = new Float64Array(n); this.R = new Float64Array(n); }
    this.m.render(this.L, this.R, 0, n);
    const l = out[0], r = out[1] || out[0];
    for (let i = 0; i < n; i++){ l[i] = this.L[i]; if (out[1]) r[i] = this.R[i]; }
    this.n += n;
    if (this.n > sampleRate / 30){
      this.n = 0; const m = this.m, pt = m.curPat();
      this.port.postMessage({type:'disp', d:m.display(), running:m.running, rec:m.recording, ppat:m.running ? m.ppat : m.pat, pat:m.pat, song:m.song, mode:m.mode,
        tk:m.running ? m.tk : -1, len:pt.len, cursor:m.cursor, perc:m.perc, meter:Array.from(this.e.meter)});
      this.e.meter.fill(0);
      if (m.version !== this.ver && currentTime - this.memAt > 0.3){ this.ver = m.version; this.memAt = currentTime; this.port.postMessage({type:'mem', mem:m.mem, ext:m.ext}); }
    }
    return true;
  }
}
registerProcessor('linn', LinnProcessor);
