/* Omega Twelve live player: runs on the audio thread (AudioWorklet), after sp1200.js and engine.js. The sound is made
   here as it plays, so a pad hit sounds at once and every knob is heard while you turn it. The page sends:
   {type:'source', chs, sr}: the selection (mono-mixed and timestretched as set); {type:'params', p, match};
   {type:'play', semis, vel, from, loop, main, raw}; {type:'stop', main}; {type:'loop', on}; {type:'bend', st}.
   It answers about 30 times a second with {type:'tick', pos, active, peak} for the playhead and the meter. */
'use strict';
class TwelvePlayer extends AudioWorkletProcessor {
  constructor(){
    super();
    this.pl = new TW.Player(sampleRate); this.peak = 0; this.n = 0; this.every = Math.round(sampleRate / 30); this.err = '';
    this.port.onmessage = e => {
      const d = e.data, pl = this.pl;
      try {
        if (d.type === 'source') pl.setSource(d.chs, d.sr);
        else if (d.type === 'params') pl.setParams(d.p, d.match);
        else if (d.type === 'play') pl.play(d);
        else if (d.type === 'stop') pl.stop(d.main);
        else if (d.type === 'loop') pl.setLoop(d.on);
        else if (d.type === 'bend') pl.setBend(d.st);
      } catch (x){ this.port.postMessage({type:'error', msg:String(x && x.message || x)}); }
    };
  }
  process(inputs, outputs){
    const o = outputs[0], L = o[0], R = o[1] || o[0], n = L.length;
    try { this.pl.process(L, R, n); }
    catch (x){ L.fill(0); R.fill(0); this.pl.voices.length = 0; this.port.postMessage({type:'error', msg:String(x && x.message || x)}); }
    let pk = 0; for (let i = 0; i < n; i++){ const a = Math.abs(L[i]), b = Math.abs(R[i]); if (a > pk) pk = a; if (b > pk) pk = b; }
    if (pk > this.peak) this.peak = pk;
    if ((this.n += n) >= this.every){ this.n = 0; this.port.postMessage({type:'tick', pos:this.pl.pos(), active:this.pl.active, peak:this.peak}); this.peak = 0; }
    return true;
  }
}
registerProcessor('twelve-player', TwelvePlayer);
