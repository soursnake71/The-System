/* Omega 7 on the audio thread (AudioWorklet), after engine.js and seq.js. Runs the synth, the arpeggiator and
 * sequencer on their clock (internal tempo or incoming MIDI clock), the MIDI file player and the recorder.
 * The page owns the voice and settings and sends them here; this sends back the sequencer position, notes for MIDI
 * out and what was recorded. */
class SevenProcessor extends AudioWorkletProcessor {
  constructor(){
    super();
    this.syn = new DX.Synth(sampleRate);
    this.base = this.syn.voice.slice();               // the voice without motion-sequence moves
    this.frame = 0;
    this.out = [];                                    // MIDI to send out, flushed with the status
    const self = this;
    this.seq = new DXS.Sequencer({
      noteOn(n, v){ self.syn.noteOn(n, v); self.midiOut([0x90 | self.syn.func.txCh, n, v]); self.record([0x90, n, v]); },
      noteOff(n){ self.syn.noteOff(n); self.midiOut([0x80 | self.syn.func.txCh, n, 0]); self.record([0x80, n, 0]); },
      param(id, v){ if (id >= 200){ const d = DX.FUNC_DEF[id - 200]; if (d) self.syn.setFunc(d[0], v); } else self.syn.setParam(id, v); }
    });
    this.player = new DXS.FilePlayer();
    this.spt = 0; this.toTick = 0;                    // samples per tick, samples to the next tick
    this.clockDiv = 0;
    this.rec = null;                                  // [{sec, data}] while recording
    this.dirty = false; this.lastStat = 0;
    this.port.onmessage = e => this.msg(e.data);
  }
  midiOut(d){ this.out.push(d); }
  spare(n){ if (!this._spare || this._spare.length !== n) this._spare = new Float32Array(n); return this._spare; }
  record(d){ if (this.rec) this.rec.push({sec:this.frame / sampleRate, data:d.slice()}); }
  // a key: to the arpeggiator / sequencer when they take it, else to the synth (and MIDI out when it came from the screen)
  key(on, n, v, screen){
    const tx = this.syn.func.txCh;
    if (screen) this.record(on ? [0x90, n, v] : [0x80, n, 0]);
    if (on){ if (!this.seq.keyOn(n, v)){ this.syn.noteOn(n, v); if (screen) this.midiOut([0x90 | tx, n, v]); } }
    else if (!this.seq.keyOff(n)){ this.syn.noteOff(n); if (screen) this.midiOut([0x80 | tx, n, 0]); }
    if (this.seq.rec) this.dirty = true;
  }
  // a MIDI message from the keyboard on screen or a MIDI input (the page already applied voice / function changes)
  midi(d){
    const st = d[0];
    if (st === 0xF8){ if (this.seq.s.ext) this.extClock(); return; }
    if (st === 0xFA){ if (this.seq.s.ext){ this.seq.start(); } return; }
    if (st === 0xFB){ if (this.seq.s.ext) this.seq.playing = true; return; }
    if (st === 0xFC){ if (this.seq.s.ext) this.stopSeq(); return; }
    if (st >= 0xF0) { if (st === 0xF0) this.syn.midi(d); return; }
    const ty = st & 0xF0, rx = this.syn.func.rxCh;
    if (rx > 0 && (st & 15) !== rx - 1) return;
    this.record(d);
    if (ty === 0x90 && d[2] > 0) return this.key(true, d[1], d[2]);
    if (ty === 0x80 || ty === 0x90) return this.key(false, d[1], 0);
    const r = this.syn.midi(d);
    if (r && r.func) this.port.postMessage({t:'func', k:r.func, v:this.syn.func[r.func]});
  }
  extClock(){ for (let i = 0; i < 4; i++) this.seq.doTick(); }
  stopSeq(){ this.seq.stop(); this.syn.loadVoice(this.base); }
  msg(m){
    switch (m.t){
      case 'voice': this.base = Uint8Array.from(m.v); this.syn.loadVoice(this.base); break;
      case 'param': this.base[m.id] = m.v; this.syn.setParam(m.id, m.v); if (this.seq.rec && this.seq.recordParam(m.id, m.v)) this.dirty = true; break;
      case 'func': this.syn.setFunc(m.k, m.v); break;
      case 'funcs': for (const k in m.f) this.syn.func[k] = m.f[k]; this.syn.recalcCtl(); break;
      case 'midi': this.midi(m.d); break;
      case 'key': this.key(m.on, m.n, m.v || 100, true); break;
      case 'ctl': if (m.k === 'bend') this.syn.setBend(m.v); else if (m.k === 'sustain') this.syn.sustain(m.v); else this.syn.setCtl(m.k, m.v); break;
      case 'panic': this.seq.stop(); this.seq.setArp(false); this.syn.allNotesOff(true); break;
      case 'seq': { const playing = this.seq.playing; this.seq.load(m.s); this.seq.playing = playing; break; }
      case 'seqCmd':
        if (m.cmd === 'play'){ this.seq.start(); this.toTick = 0; if (!this.seq.s.ext) this.midiOut([0xFA]); }
        else if (m.cmd === 'stop'){ this.stopSeq(); this.seq.rec = false; if (!this.seq.s.ext) this.midiOut([0xFC]); }
        else if (m.cmd === 'rec') this.seq.rec = !!m.a;
        else if (m.cmd === 'arp'){ this.seq.setArp(!!m.a); }
        else if (m.cmd === 'cursor') this.seq.stepCursor = m.a;
        else if (m.cmd === 'rest') this.seq.stepRest();
        else if (m.cmd === 'tie') this.seq.stepTie();
        break;
      case 'file': this.player.load(m.mf, m.name); this.player.start(); break;
      case 'player': if (m.cmd === 'stop'){ this.player.stop(); this.syn.allNotesOff(false); } else if (m.cmd === 'loop') this.player.loop = !!m.a; else this.player.start(); break;
      case 'rec':
        if (m.on) this.rec = [];
        else if (this.rec){ this.port.postMessage({t:'recData', evs:this.rec, voice:this.syn.voice.slice()}); this.rec = null; }
        break;
    }
  }
  process(inputs, outputs){
    const o = outputs[0], L = o[0], R = o[1] || this.spare(L.length), n = L.length, sq = this.seq;
    const clocked = !sq.s.ext && (sq.playing || (sq.s.arp.on && sq.arpRun));
    this.spt = sampleRate * 60 / (Math.max(20, sq.s.tempo) * DXS.PPQ);
    // MIDI clock while the sequencer or arpeggiator runs on the internal tempo
    let pos = 0;
    while (pos < n){
      let chunk = n - pos;
      if (clocked){ if (this.toTick <= 0){ sq.doTick(); if (sq.playing && (this.clockDiv++ & 3) === 0) this.midiOut([0xF8]); this.toTick += this.spt; } chunk = Math.min(chunk, Math.max(1, Math.ceil(this.toTick))); }
      if (this.player.playing) this.player.run(chunk / sampleRate, d => this.midi(d));
      this.syn.renderStereo(L, R, pos, chunk);
      if (clocked) this.toTick -= chunk;
      pos += chunk;
    }
    if (!clocked) this.toTick = 0;
    this.frame += n;
    if (this.frame - this.lastStat > sampleRate / 30 || this.out.length > 32){
      this.lastStat = this.frame;
      const st = {t:'stat', step:sq.playing ? sq.step : -1, pat:sq.s.pat, playing:sq.playing, rec:sq.rec, voices:this.syn.activeVoices(), peak:this.syn.peak,
        player:this.player.playing, playerT:this.player.t, rec2:!!this.rec, out:this.out, cursor:sq.stepCursor};
      if (this.dirty){ st.pattern = sq.pat; this.dirty = false; }
      this.port.postMessage(st);
      this.out = []; this.syn.peak = 0;
    }
    return true;
  }
}
registerProcessor('omega-seven', SevenProcessor);
