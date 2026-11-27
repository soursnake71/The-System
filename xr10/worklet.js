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
      const { instance } = await WebAssembly.instantiate(m.bytes, {});
      const x = this.x = instance.exports;
      x.__wasm_call_ctors();
      x.xr_init(sampleRate);
      const names = [], pnames = [], plo = [], phi = [];
      for (let i = 0; i < 64; i++) names.push(this.str(x.xr_sound_name(i), 12));
      for (let i = 0; i < 13; i++) { pnames.push(this.str(x.xr_param_name(i), 12)); plo.push(x.xr_param_lo(i)); phi.push(x.xr_param_hi(i)); }
      this.port.postMessage({ t: 'ready', names, pnames, plo, phi, patterns: this.patternNames() });
      return;
    }
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
      case 'reset': x.xr_factory_reset(); this.port.postMessage({ t: 'patterns', patterns: this.patternNames() }); break;
      case 'clearPart': x.xr_clear_part(); break;
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
        break;
      }
      case 'sample': {
        const d = m.data, max = (x.xr_scratch_size() >> 2);
        const n = Math.min(d.length, max);
        new Float32Array(x.memory.buffer, x.xr_scratch(), n).set(d.subarray(0, n));
        x.xr_load_sample(m.s, n, m.rate);
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
    s.keys = []; for (let k = 0; k < 32; k++) s.keys.push(x.xr_get(k));
    s.params = []; for (let q = 0; q < 13; q++) s.params.push(x.xr_param(s.sound, q));
    s.user = []; for (let i = 0; i < 64; i++) s.user.push(x.xr_has_user(i));
    s.steps = x.xr_grid_steps();
    s.grid = new Uint8Array(b, x.xr_grid(this.gridBar), 16 * 33).slice();
    const sp = x.xr_song(s.song), sb = new Uint8Array(b, sp, 10 + 2 + 2 + 99 * 4);
    const len = sb[10]; s.songLen = len; s.songTempo = sb[12] | (sb[13] << 8); s.songSteps = [];
    for (let i = 0; i < len; i++) s.songSteps.push([sb[14 + i * 4], sb[15 + i * 4], sb[16 + i * 4]]);
    s.patName = this.str(x.xr_pattern_name(s.pattern), 10);
    s.tick = this.tick;
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
