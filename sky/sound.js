/* Sound engines (Web Audio), MIDI out (Web MIDI or the Android app's USB MIDI) and MIDI file export. */
const Sound = (() => {
  let ctx = null, master = null, verbIn = null, delayIn = null, delayNode = null;
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
    // ping-pong-ish dotted-eighth delay
    const dl = ctx.createDelay(2), fb = ctx.createGain(), dlf = ctx.createBiquadFilter(); dl.delayTime.value = 0.375; fb.gain.value = 0.38; dlf.type = 'lowpass'; dlf.frequency.value = 2800;
    delayNode = dl; delayIn = ctx.createGain(); delayIn.gain.value = 0.5; delayIn.connect(dl); dl.connect(dlf); dlf.connect(fb); fb.connect(dl); dlf.connect(master); dlf.connect(verbIn);
    return ctx;
  }
  // delay follows the tempo: a dotted eighth
  function setDelayTime(bpm){ if (delayNode) delayNode.delayTime.setTargetAtTime(Math.min(1.9, 45 / bpm), ctx.currentTime, 0.05); }
  function lane(id, vol, rev, dly){
    let l = lanes[id];
    if (!l){ l = lanes[id] = {gain:ctx.createGain(), rev:ctx.createGain(), dly:ctx.createGain()}; l.gain.connect(master); l.gain.connect(l.rev); l.gain.connect(l.dly); l.rev.connect(verbIn); l.dly.connect(delayIn); }
    l.gain.gain.value = vol; l.rev.gain.value = rev; l.dly.gain.value = dly; return l;
  }

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
    none:{name:'MIDI only', play(){}},
  };

  // pan: -1 (west) .. 1 (east)
  function note(laneId, engine, n, v, t, dur, pan, l){
    const e = ENGINES[engine]; if (!e || engine === 'none' || !ctx) return;
    n = Math.max(12, Math.min(115, n));
    const out = lane(laneId, l.vol, l.rev, l.dly); let dest = out.gain;
    if (ctx.createStereoPanner){ const p = ctx.createStereoPanner(); p.pan.value = Math.max(-1, Math.min(1, pan || 0)); p.connect(out.gain); dest = p; }
    e.play(n, v, t, dur, dest);
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

  return {init, get ctx(){ return ctx; }, ENGINES, note, midi, midiInit, midiRefresh, midiSelect, midiSend, midiNote, midiActive, smf, setDelayTime};
})();
