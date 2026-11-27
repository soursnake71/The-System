/* Omega Solina AudioWorklet: the engine (engine.js) and the sequencer (seq.js), sample-accurate. Loaded after both. */
'use strict';
const {SolinaEngine} = globalThis.SolinaDSP, {SeqPlayer, BAR} = globalThis.SolinaSeq;

class SolinaProcessor extends AudioWorkletProcessor{
  constructor(){
    super();
    this.eng = new SolinaEngine(sampleRate); this.seq = new SeqPlayer(); this.ev = []; this.click = false; this.clickPh = 0; this.clickAmp = 0; this.lastBeat = -1;
    this.metT = 0; this.pk = [0, 0]; this.sendClock = true; this.extClock = false;
    this.port.onmessage = e => this.msg(e.data);
  }
  msg(d){
    const g = this.eng;
    switch (d.type){
      case 'param': g.set(d.id, d.v); break;
      case 'params': g.setAll(d.p); break;
      case 'note': if (d.on){ g.noteOn(d.n, d.v); this.seq.recOn(d.n, d.v); } else { g.noteOff(d.n); const r = this.seq.recOff(d.n); if (r) this.port.postMessage({type:'recNote', pat:r.pat, note:r.note}); } break;
      case 'pedal': g.pedal(d.on); break;
      case 'bend': g.bend(d.v); break;
      case 'mod': g.modWheel(d.v); break;
      case 'expr': g.expression(d.v); break;
      case 'panic': g.allOff(); break;
      case 'song': this.seq.setSong(d.song); g.bpm = d.song.bpm; break;
      case 'bpm': this.seq.song.bpm = d.v; g.bpm = d.v; break;
      case 'play': this.seq.start(d.tick || 0); if (this.sendClock) this.port.postMessage({type:'midi', d:[d.tick ? 0xFB : 0xFA]}); break;
      case 'stop': this.seq.stop((o, s, n) => { g.noteOff(n); this.port.postMessage({type:'midi', d:[0x80, n, 0]}); }); if (this.sendClock) this.port.postMessage({type:'midi', d:[0xFC]}); break;
      case 'rec': this.seq.rec = d.on; this.seq.quant = d.quant ? 1 : 0; break;
      case 'click': this.click = d.on; break;
      case 'clockOut': this.sendClock = d.on; break;
    }
  }
  process(ins, outs){
    const L = outs[0][0], R = outs[0][1] || outs[0][0], n = L.length, g = this.eng, ev = this.ev;
    ev.length = 0;
    this.seq.run(n, sampleRate, (o, s, nn, v) => ev.push(o, s, nn, v));
    let at = 0;
    for (let i = 0; i < ev.length; i += 4){
      const o = ev[i], s = ev[i + 1];
      if (o > at){ g.process(L, R, at, o - at); at = o; }
      if (s === 0x90){ g.noteOn(ev[i + 2], ev[i + 3] / 127); this.port.postMessage({type:'midi', d:[0x90, ev[i + 2], ev[i + 3]], seq:1}); }
      else if (s === 0x80){ g.noteOff(ev[i + 2]); this.port.postMessage({type:'midi', d:[0x80, ev[i + 2], 0], seq:1}); }
      else if (s === 0xF8 && this.sendClock) this.port.postMessage({type:'midi', d:[0xF8]});
    }
    if (at < n) g.process(L, R, at, n - at);
    // metronome while recording (or when switched on)
    if (this.click && this.seq.playing){
      const beat = Math.floor(this.seq.tick / 96);
      if (beat !== this.lastBeat){ this.lastBeat = beat; this.clickAmp = beat % 4 ? .18 : .3; this.clickF = beat % 4 ? 1500 : 2400; }
      for (let i = 0; i < n; i++){ if (this.clickAmp < 1e-4) break; this.clickPh += this.clickF / sampleRate; const c = Math.sin(this.clickPh * 2 * Math.PI) * this.clickAmp; this.clickAmp *= .9985; L[i] += c; if (R !== L) R[i] += c; }
    } else this.lastBeat = -1;
    for (let i = 0; i < n; i++){ const a = Math.abs(L[i]), b = Math.abs(R[i]); if (a > this.pk[0]) this.pk[0] = a; if (b > this.pk[1]) this.pk[1] = b; }
    if ((this.metT += n) >= sampleRate / 30){ this.metT = 0;
      this.port.postMessage({type:'pos', playing:this.seq.playing, pat:this.seq.pat, pos:this.seq.pos, tick:this.seq.tick, l:this.pk[0], r:this.pk[1]}); this.pk[0] = this.pk[1] = 0; }
    return true;
  }
}
registerProcessor('omega-solina', SolinaProcessor);
