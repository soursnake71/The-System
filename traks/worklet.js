/* Omega Traks audio thread: the engine (engine.js) and the firmware (machine.js), loaded before this file into the same
   AudioWorkletGlobalScope. In: {type:'key', id, down}, 'note' (v, vel), 'params', 'rom' (v, codes, rate), 'mem',
   'edit', 'knob', 'bpm', 'ext', 'clock', 'metro', 'state' (pat, song, quant). Out: 'disp' (30 a second: the displays,
   LEDs, position, meters), 'mem' (when the memory changed), 'hit' (for MIDI out), 'clock', 'start', 'stop'. */
'use strict';
class TraksProcessor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.e = new OTRAKS.Engine(sampleRate);
    this.m = new OTMACH.Machine(this.e, sampleRate);
    this.L = new Float64Array(128); this.R = new Float64Array(128);
    const post = x => this.port.postMessage(x);
    this.hitsOut = false;
    this.m.onHit = (v, acc, vel) => { if (this.hitsOut) post({type:'hit', v, acc, vel}); };
    this.m.onClock = () => post({type:'clock'});
    this.m.onStart = () => post({type:'start'});
    this.m.onStop = () => post({type:'stop'});
    this.n = 0; this.ver = -1; this.memAt = 0;
    this.port.onmessage = ({data:x}) => {
      const m = this.m, e = this.e;
      switch (x.type){
        case 'key': m.key(x.id, x.down); break;
        case 'note': { const prev = m.accHeld; m.accHeld = prev || !!x.acc; m.inst(x.v, x.vel); m.held &= ~(1 << x.v); m.accHeld = prev; break; }
        case 'off': e.release(x.v); break;
        case 'params': e.setAll(x.p); m.bpm = x.p[OTRAKS.P.tempo]; break;
        case 'rom': e.setRom(x.v, x.codes, x.rate); break;
        case 'mem': m.mem = x.mem; this.ver = m.version; break;
        case 'state': m.pat = m.ppat = x.pat; m.song = x.song; m.quant = x.quant; break;
        case 'edit': m.edit(x.p, x.t, x.v, x.on, x.acc); break;
        case 'knob': m.knobMoved(); break;
        case 'recmode': m.group = 0; m.patFn = x.on ? 1 : 0; m.entry = ''; m.recording = m.running && m.recActive(); m.changed(); break;
        case 'sel': m.sel = x.v; break;
        case 'ext': m.ext = x.on; break;
        case 'clock': m.clockTick(); break;
        case 'midistart': if (!m.running) m.start(); break;
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
      this.port.postMessage({type:'disp', d:m.display(), running:m.running, rec:m.recording, ppat:m.running && m.group === 0 ? m.ppat : m.pat, pat:m.pat, song:m.song, quant:m.quant,
        group:m.group, patFn:m.patFn, songFn:m.songFn, tk:m.running ? m.tk : -1, len:OTMACH.patLen(pt), meter:Array.from(this.e.meter)});
      this.e.meter.fill(0);
      if (m.version !== this.ver && currentTime - this.memAt > 0.4){ this.ver = m.version; this.memAt = currentTime; this.port.postMessage({type:'mem', mem:m.mem}); }
    }
    return true;
  }
}
registerProcessor('traks', TraksProcessor);
