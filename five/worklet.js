/* Omega Five: the AudioWorkletProcessor. Runs engine.js (voices, 12-bit path, effects) and the seq.js Player
   sample-accurately inside the audio thread. The main thread owns the state and posts changes; this posts back the
   play position, the pads that fired, meters (about 30 times a second) and MIDI out events with their frame times.
   Also used by the WAV export (OfflineAudioContext + processorOptions.render). */
import { Engine } from './engine.js';
import { Player, patLen, NUM_VOICES, NUM_CHANNELS } from './seq.js';

const sr = sampleRate;

class FiveProcessor extends AudioWorkletProcessor {
  constructor(opts){
    super();
    const o = (opts && opts.processorOptions) || {};
    this.e = new Engine(sr);
    this.state = null; this.player = null;
    this.cue = -1; this.ext = false; this.clockOut = false; this.notesOut = false;
    this.L = new Float32Array(128); this.R = new Float32Array(128);
    this.peak = [0, 0]; this.chPeak = new Float32Array(NUM_CHANNELS); this.fired = []; this.midi = []; this.since = 0;
    this.pendingClocks = 0; this.restore = []; this.lastBpm = 0; this.wasRunning = false; this.render = o.render || null;
    this.port.onmessage = ev => this.msg(ev.data);
    this.roms = o.roms || null;   // the factory kit: [{data, sr}] per voice (the RX5 recordings, 25 kHz)
    if (o.state) this.setState(o.state);
    if (this.render && this.player){ this.player.setMode(this.render.mode, this.render.n); this.player.start(); }
    this.port.postMessage({t:'ready'});
  }
  setState(s){
    this.state = s;
    const e = this.e;
    for (let v = 0; v < NUM_VOICES; v++){
      const vp = s.voice[v] || {};
      for (const k in vp) e.setVoiceParam(v, k, vp[k]);
      const smp = s.samples && s.samples[v];
      this.sample(v, smp);
    }
    for (let ch = 0; ch < NUM_CHANNELS; ch++) if (s.channel[ch]) e.setChannel(ch, s.channel[ch]);
    // the reverb program first: it loads that program's parameters, which the saved values then override
    for (const kind of ['rev', 'dly']) { const f = (s.fx && s.fx[kind]) || {}; if ('program' in f) e.setFx(kind, 'program', f.program); for (const k in f) if (k !== 'program') e.setFx(kind, k, f[k]); }
    e.setMaster({volume:s.volume, click:s.click ? s.click.level : 0.5});
    const old = this.player;
    this.player = new Player(s);
    if (old && old.running && !this.render){ this.player.setMode(old.mode, old.n); this.player.locate(Math.max(0, old.cur + 1)); this.player.running = true; }
    this.player.setExternal(this.ext);
  }
  // a voice plays the user's sample when it has one, else its factory kit waveform
  sample(v, smp){
    const e = this.e;
    if (smp && smp.data && smp.data.length > 1){ e.setUserSample(v, smp.data, smp.sr, smp.raw ? {rec:true, raw:true} : undefined); return; }
    const r = this.roms && this.roms[v];
    if (!r || !r.data) return;
    if (e.loadRom) e.loadRom(v, r.data, r.sr); else e.setUserSample(v, r.data, r.sr, {rec:true, raw:true});
  }
  msg(m){
    const s = this.state, e = this.e, p = this.player;
    switch (m.t){
      case 'state': this.setState(m.state); break;
      case 'roms': this.roms = m.roms; for (let v = 0; v < NUM_VOICES; v++) this.sample(v, this.state && this.state.samples && this.state.samples[v]); break;
      case 'pat': s.patterns[m.n] = m.p; break;
      case 'song': s.songs[m.n] = m.s; break;
      case 'chain': s.chains[m.n] = m.c; break;
      case 'set': s[m.k] = m.v; if (m.k === 'volume' || m.k === 'click') e.setMaster({volume:s.volume, click:s.click.level}); break;
      case 'voice': s.voice[m.v][m.k] = m.val; e.setVoiceParam(m.v, m.k, m.val); break;
      case 'voiceAll': s.voice[m.v] = m.p; for (const k in m.p) e.setVoiceParam(m.v, k, m.p[k]); break;
      case 'channel': s.channel[m.ch] = m.o; e.setChannel(m.ch, m.o); break;
      case 'fx': (s.fx[m.kind] = s.fx[m.kind] || {})[m.k] = m.val; e.setFx(m.kind, m.k, m.val); break;
      case 'sample': s.samples[m.v] = m.s; this.sample(m.v, m.s); break;
      case 'hit': {
        // key offsets / multi voice / tunable notes: parameters set for this note only. The engine reads a voice's
        // parameters when the note starts (inside the next process()), so they are restored after it.
        if (m.over) for (const k in m.over){ const base = e.voices ? e.voices[m.v][k] : undefined; if (base === undefined) continue; this.restore.push([m.v, k, base]); e.setVoiceParam(m.v, k, m.over[k]); }
        e.trigger(m.v, m.vel, m.acc, 0);
        this.fired.push(m.v);
        if (m.rec && p && p.running){
          const ev = p.record(m.v, m.vel, m.acc);
          if (ev) this.port.postMessage({t:'rec', pn:p.pat, e:{t:ev.t, v:ev.v, vel:ev.vel, acc:ev.acc}});
        }
        break;
      }
      case 'release': e.release(m.v); break;
      case 'damp':
        if (e.damp) e.damp(); else if (e.cut) for (let v = 0; v < NUM_VOICES; v++) e.cut(v); else for (let v = 0; v < NUM_VOICES; v++) e.release(v);
        break;
      case 'transport':
        if (!p) break;
        if (m.cmd === 'start'){ p.setMode(m.mode, m.n); p.start(); this.cue = -1; }
        else if (m.cmd === 'stop') p.stop();
        else if (m.cmd === 'cont'){ if (p.mode !== m.mode || p.n !== m.n) { p.setMode(m.mode, m.n); p.start(); } else p.cont(); }
        break;
      case 'locate': if (p) p.locate(m.tick); break;
      case 'cue': this.cue = m.n; if (p && !p.running && p.mode === 'pattern'){ p.n = p.pat = m.n; this.cue = -1; } break;
      case 'tempo': if (p) p.setTempo(m.bpm); break;
      case 'ext': this.ext = !!m.on; if (p) p.setExternal(this.ext); break;
      case 'clock': this.pendingClocks += m.n || 1; break;
      case 'spp': if (p) p.spp(m.n); break;
      case 'midiOut': this.clockOut = !!m.clock; this.notesOut = !!m.notes; break;
    }
  }
  run(out, n, off0){
    // apply sequencer events to the engine; collect pads to flash and MIDI out
    for (const ev of out){
      if (ev.v < 0){ this.e.clickTick(ev.click === 2, ev.offset + off0); continue; }
      this.e.trigger(ev.v, ev.vel, ev.acc, ev.offset + off0);
      this.fired.push(ev.v);
      if (this.notesOut) this.midi.push({f:currentFrame + ev.offset + off0, v:ev.v, vel:ev.vel, acc:ev.acc});
    }
  }
  process(inputs, outputs){
    const o = outputs[0], n = o[0].length;
    if (this.L.length !== n){ this.L = new Float32Array(n); this.R = new Float32Array(n); }
    const p = this.player;
    if (p){
      if (this.ext){
        // external clock: every F8 that arrived since the last block fires one tick at the start of this block
        while (this.pendingClocks > 0){ this.pendingClocks--; this.run(p.clock24(0), n, 0); }
      } else if (p.running){
        const spt = sr * 60 / (p.bpm * 24);
        if (this.clockOut) for (let w = p.wait; w < n; w += spt) this.midi.push({f:currentFrame + Math.max(0, Math.ceil(w - 1e-6)), clk:1});
        this.run(p.advance(n, sr), n, 0);
      }
      // a cued pattern takes over at the end of the playing one
      if (this.cue >= 0 && p.mode === 'pattern' && p.running && p.tick >= patLen(this.state.patterns[p.pat])){
        p.pat = p.n = this.cue; p.tick = 0; p.pass++; this.cue = -1;
      }
      if (this.wasRunning && !p.running && p.ended) this.port.postMessage({t:'ended', mode:p.mode, n:p.n});
      this.wasRunning = p.running;
    }
    // the delay's tempo sync follows the sequencer
    if (p && p.bpm !== this.lastBpm && this.e.setTempo){ this.lastBpm = p.bpm; this.e.setTempo(p.bpm); }
    const L = this.L, R = this.R;
    this.e.process(L, R, n);
    if (this.restore.length){ for (let i = this.restore.length - 1; i >= 0; i--){ const [v, k, x] = this.restore[i]; this.e.setVoiceParam(v, k, x); } this.restore.length = 0; }
    let pl = this.peak[0], pr = this.peak[1];
    for (let i = 0; i < n; i++){ const a = Math.abs(L[i]), b = Math.abs(R[i]); if (a > pl) pl = a; if (b > pr) pr = b; }
    this.peak[0] = pl; this.peak[1] = pr;
    o[0].set(L); if (o[1]) o[1].set(R);
    const cp = this.e.peak; if (cp) for (let c = 0; c < NUM_CHANNELS; c++) if (cp[c] > this.chPeak[c]) this.chPeak[c] = cp[c];
    if (this.render) return true;
    this.since += n;
    if (this.since >= sr / 30 || this.midi.length){
      if (this.since >= sr / 30){
        this.port.postMessage({t:'pos', pos:p ? p.pos() : null, cue:this.cue, peak:this.peak.slice(), ch:Array.from(this.chPeak), fired:this.fired, frame:currentFrame});
        this.peak[0] = this.peak[1] = 0; this.chPeak.fill(0); this.fired = []; this.since = 0;
      }
      if (this.midi.length){ this.port.postMessage({t:'midi', ev:this.midi}); this.midi = []; }
    }
    return true;
  }
}
registerProcessor('omega-five', FiveProcessor);
