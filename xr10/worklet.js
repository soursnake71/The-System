// Omega XR10 audio: runs the C++ engine (xr10.wasm) inside an AudioWorklet and talks to the page by messages.
class XR10Processor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;
    this.tick = 0;
    this.gridBar = 0;
    this.port.onmessage = e => this.msg(e.data);
  }
  async msg(m) {
    if (m.t === 'wasm') {
      try { await this.start(m); } catch (e) { this.x = null; this.port.postMessage({ t: 'error', msg: String(e && e.message || e) }); }
      return;
    }
    this.handle(m);
  }
  async start(m) {
    {
      // the module only asks the system for a few things it never really uses here (it's built with the C++ library)
      const mod = await WebAssembly.compile(m.bytes);
      const imports = {};
      for (const im of WebAssembly.Module.imports(mod)) (imports[im.module] ??= {})[im.name] = im.kind === 'function' ? () => 0 : undefined;
      const instance = await WebAssembly.instantiate(mod, imports);
      const x = this.x = instance.exports;
      if (x._initialize) x._initialize(); else if (x.__wasm_call_ctors) x.__wasm_call_ctors();
      if (m.rom && m.rom.byteLength > 8 && m.rom.byteLength <= x.xr_scratch_size()) {
        new Uint8Array(x.memory.buffer, x.xr_scratch(), m.rom.byteLength).set(new Uint8Array(m.rom));
        x.xr_init_pack(sampleRate, m.rom.byteLength);
      } else x.xr_init(sampleRate);
      const names = [], pnames = [], plo = [], phi = [];
      for (let i = 0; i < 64; i++) names.push(this.str(x.xr_sound_name(i), 12));
      const nvp = x.xr_nvp(); this.nvp = nvp; this.nkeys = x.xr_nkeys();
      for (let i = 0; i < nvp; i++) { pnames.push(this.str(x.xr_param_name(i), 12)); plo.push(x.xr_param_lo(i)); phi.push(x.xr_param_hi(i)); }
      const packed = []; for (let i = 0; i < 64; i++) packed.push(x.xr_from_pack(i));
      const rackUnits = []; for (let i = 0; i < x.xr_rack_units(); i++) rackUnits.push(this.str(x.xr_rack_unit_name(i), 120));
      this.port.postMessage({ t: 'ready', names, pnames, plo, phi, packed, nvpXr10: x.xr_nvp_xr10(), rackUnits, patterns: this.patternNames() });
    }
  }
  handle(m) {
    const x = this.x;
    if (!x) return;
    switch (m.t) {
      case 'btn': x.xr_button(m.b, m.d); break;
      case 'pad': x.xr_pad(m.i, m.d, m.v | 0); break;
      case 'knob': x.xr_knob(m.d); break;
      case 'set': x.xr_set(m.k, m.v); break;
      case 'param': x.xr_set_param(m.s, m.p, m.v); break;
      case 'hit': x.xr_trigger(m.s, m.v); break;
      case 'midi': x.xr_midi(m.a, m.b, m.c); break;
      case 'gridBar': this.gridBar = m.bar; break;
      case 'gridToggle': x.xr_grid_toggle(m.row, m.bar, m.step); break;
      case 'gridAssign': x.xr_grid_assign(m.row, m.s); break;
      case 'setPad': x.xr_set_pad(m.bank, m.pad, m.s); break;
      case 'songSet': x.xr_song_set(m.song, m.step, m.p, m.part, m.rep); break;
      case 'songInsert': x.xr_song_insert(m.song, m.step); break;
      case 'songDelete': x.xr_song_delete(m.song, m.step); break;
      case 'songTempo': x.xr_song_tempo(m.song, m.bpm); break;
      case 'reset': x.xr_factory_reset(); for (let i = 0; i < 4; i++) this.describe(i); this.port.postMessage({ t: 'patterns', patterns: this.patternNames() }); break;
      case 'clearPart': x.xr_clear_part(); break;
      case 'randomBeat': x.xr_random_beat(m.seed); this.port.postMessage({ t: 'patterns', patterns: this.patternNames() }); break;
      case 'randomSound': x.xr_random_sound(m.seed); break;
      case 'mute': x.xr_mute(m.s, m.on); break;
      case 'solo': x.xr_solo(m.s, m.on); break;
      case 'rackType': x.xr_rack_set_type(m.slot, m.type); this.describe(m.slot); break;
      case 'rackParam': x.xr_rack_set_param(m.slot, m.p, m.v); break;
      case 'rackPreset': x.xr_rack_preset(m.slot, m.k); this.describe(m.slot); break;
      case 'rackBypass': x.xr_rack_bypass(m.slot, m.on); break;
      case 'rackParallel': x.xr_rack_parallel(m.slot, m.on); break;
      case 'rackReturn': x.xr_rack_set_return(m.v); break;
      case 'rackDescribe': this.describe(m.slot); break;
      case 'resetVoice': x.xr_default_voice(m.s); break;
      case 'clearSample': x.xr_clear_sample(m.s); break;
      case 'patterns': this.port.postMessage({ t: 'patterns', patterns: this.patternNames() }); break;
      case 'save': {
        const n = x.xr_save();
        const bytes = n > 0 ? new Uint8Array(x.memory.buffer, x.xr_scratch(), n).slice() : null;
        this.port.postMessage({ t: 'saved', bytes, why: m.why }, bytes ? [bytes.buffer] : []);
        break;
      }
      case 'load': {
        const b = new Uint8Array(m.bytes);
        if (b.length > x.xr_scratch_size()) break;
        new Uint8Array(x.memory.buffer, x.xr_scratch(), b.length).set(b);
        const ok = x.xr_load(b.length);
        this.port.postMessage({ t: 'loaded', ok, why: m.why, patterns: this.patternNames() });
        for (let i = 0; i < 4; i++) this.describe(i);
        break;
      }
      case 'sample': {
        const d = m.data, max = (x.xr_scratch_size() >> 2);
        const n = Math.min(d.length, max);
        new Float32Array(x.memory.buffer, x.xr_scratch(), n).set(d.subarray(0, n));
        x.xr_load_sample(m.s, n, m.rate);
        break;
      }
      case 'exportMidi': {
        const n = x.xr_export_midi(m.song ? 1 : 0);
        const bytes = n > 0 ? new Uint8Array(x.memory.buffer, x.xr_scratch(), n).slice() : null;
        this.port.postMessage({ t: 'midiFile', bytes, song: m.song }, bytes ? [bytes.buffer] : []);
        break;
      }
      case 'importMidi': {
        const b = new Uint8Array(m.bytes);
        let r = -1;
        if (b.length <= x.xr_scratch_size()) { new Uint8Array(x.memory.buffer, x.xr_scratch(), b.length).set(b); r = x.xr_import_midi(b.length); }
        this.port.postMessage({ t: 'midiImported', r, patterns: this.patternNames() });
        break;
      }
      case 'matchNames': {
        const out = m.names.map(nm => { const e = Uint8Array.from(nm.slice(0, 200), ch => ch.charCodeAt(0) & 0xff); new Uint8Array(x.memory.buffer, x.xr_scratch(), e.length).set(e); return x.xr_match_name(e.length); });
        this.port.postMessage({ t: 'matched', id: m.id, out });
        break;
      }
      case 'wave': {
        const n = x.xr_render_sound(m.s);
        const w = new Int16Array(x.memory.buffer, x.xr_scratch(), n).slice();
        this.port.postMessage({ t: 'wave', s: m.s, w }, [w.buffer]);
        break;
      }
    }
  }
  describe(slot) {
    const x = this.x, n = x.xr_rack_describe(slot);
    this.port.postMessage({ t: 'rackDescribed', slot, text: this.str(x.xr_rack_text(), n) });
  }
  str(p, n) { const m = new Uint8Array(this.x.memory.buffer); let s = ''; for (let i = 0; i < n && m[p + i]; i++) s += String.fromCharCode(m[p + i]); return s; }
  patternNames() { const a = []; for (let i = 0; i < 99; i++) a.push(this.str(this.x.xr_pattern_name(i), 10)); return a; }
  snapshot() {
    const x = this.x, p = x.xr_status(), b = x.memory.buffer;
    const i32 = new Int32Array(b, p + 36, 53);
    const s = { t: 'status', l1: this.str(p, 16), l2: this.str(p + 17, 16) };
    const f = ['mode', 'playing', 'recording', 'accent', 'bank', 'pattern', 'part', 'bar', 'beat', 'tempo10', 'sound'];
    f.forEach((k, i) => s[k] = i32[i]);
    s.padHit = Array.from(i32.subarray(11, 26));
    const g = ['erase', 'editing', 'fxType', 'tc', 'song', 'songStep', 'cursor', 'blink', 'version', 'beats', 'sub', 'bars'];
    g.forEach((k, i) => s[k] = i32[26 + i]);
    s.padSound = Array.from(i32.subarray(38, 53));
    s.keys = []; for (let k = 0; k < this.nkeys; k++) s.keys.push(x.xr_get(k));
    s.params = []; for (let q = 0; q < this.nvp; q++) s.params.push(x.xr_param(s.sound, q));
    s.user = []; for (let i = 0; i < 64; i++) s.user.push(x.xr_has_user(i));
    s.steps = x.xr_grid_steps();
    s.grid = new Uint8Array(b, x.xr_grid(this.gridBar), 16 * 33).slice();
    const sp = x.xr_song(s.song), sb = new Uint8Array(b, sp, 10 + 2 + 2 + 99 * 4);
    const len = sb[10]; s.songLen = len; s.songTempo = sb[12] | (sb[13] << 8); s.songSteps = [];
    for (let i = 0; i < len; i++) s.songSteps.push([sb[14 + i * 4], sb[15 + i * 4], sb[16 + i * 4]]);
    s.patName = this.str(x.xr_pattern_name(s.pattern), 10);
    s.tick = this.tick;
    // the fold-out
    const mu = new Uint8Array(b, x.xr_mutes(), 64), so = new Uint8Array(b, x.xr_solos(), 64), me = new Float32Array(b, x.xr_meters(), 64);
    s.padParams = s.padSound.map(snd => [x.xr_param(snd, 0), x.xr_param(snd, 1), x.xr_param(snd, 4)]);   // LEVEL, PAN, FX SEND
    s.mute = Array.from(mu); s.solo = Array.from(so); s.meter = Array.from(me);
    const op = new Float32Array(b, x.xr_out_peaks(), 2); s.outPeak = [op[0], op[1]];
    s.rack = { ret: x.xr_rack_return(), peakIn: x.xr_rack_peak(0), peakOut: x.xr_rack_peak(1), slots: [] };
    for (let i = 0; i < 4; i++) s.rack.slots.push({ type: x.xr_rack_type(i), flags: x.xr_rack_flags(i), peak: x.xr_rack_peak(2 + i) });
    return s;
  }
  process(_in, outputs) {
    const out = outputs[0], x = this.x;
    if (!x) return true;
    const n = out[0].length;
    x.xr_process(n);
    const b = x.memory.buffer;
    out[0].set(new Float32Array(b, x.xr_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(b, x.xr_out_r(), n));
    if (++this.tick % 12 === 0) this.port.postMessage(this.snapshot());
    return true;
  }
}
registerProcessor('xr10', XR10Processor);
