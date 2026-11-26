// Omega Five — sequencer / data model (pure JS, no DOM). ES module.
// Patterns, songs, chains, quantize/swing, step & real-time write, the sample-accurate Player,
// randomize, cartridge files, SysEx bulk dump, SMF import/export.
//
// Timing: 24 PPQN like the hardware (1 tick = 1/96 note = one MIDI clock F8).

export const PPQN = 24;
export const NUM_PATTERNS = 100;
export const NUM_SONGS = 20;
export const NUM_CHAINS = 4;
export const CHAIN_MAX = 99;
export const NUM_VOICES = 24;
export const NUM_CHANNELS = 12;
export const CART_VERSION = 1;
export const SYSEX_HEADER = [0xF0, 0x7D, 0x4F, 0x35];

export const VOICE_NAMES = ['BD 1', 'SD 1', 'RIM 1', 'TOM 1A', 'TOM 2A', 'TOM 3A', 'TOM 4A', 'HH CLS', 'RIDE', 'CRASH', 'TAMB', 'SHAKER',
  'BD 2', 'SD 2', 'RIM 2', 'TOM 1B', 'TOM 2B', 'TOM 3B', 'TOM 4B', 'HH OPN', 'RIDE CP', 'CHINA', 'CLAPS', 'COWBELL'];

// Quantize table in panel order. ticks = grid at 24 PPQN; key = keypad digit (null: no key).
export const QUANTIZE = [
  { name: '1/2', ticks: 48, key: 0 }, { name: '1/4', ticks: 24, key: 1 }, { name: '1/6', ticks: 16, key: 2 },
  { name: '1/8', ticks: 12, key: 3 }, { name: '1/12', ticks: 8, key: 4 }, { name: '1/16', ticks: 6, key: 5 },
  { name: '1/24', ticks: 4, key: 6 }, { name: '1/32', ticks: 3, key: 7 }, { name: '1/48', ticks: 2, key: 8 },
  { name: '1/96', ticks: 1, key: null }, { name: 'OFF', ticks: 1, key: 9 },
];
// keypad digit -> QUANTIZE index
export const QUANTIZE_KEYPAD = { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 10 };
export const Q_1_16 = 5;

// Default note per voice (noteMap). GM-based; voices without a GM drum get a free GM percussion note.
export const MIDI_GM_NOTES = [
  35, 38, 37, 50, 48, 45, 41, 42, 51, 49, 54, 70, //  BD1 SD1 RIM1 TOM1A TOM2A TOM3A TOM4A HHCLS RIDE CRASH TAMB SHAKER
  36, 40, 75, 65, 66, 47, 43, 46, 53, 52, 39, 56, //  BD2 SD2 RIM2 TOM1B TOM2B TOM3B TOM4B HHOPN RIDECP CHINA CLAPS COWBELL
];
// Extra GM notes accepted on import / MIDI in (note -> voice) in addition to the inverted noteMap.
export const GM_EXTRA = { 44: 7, 57: 9, 82: 11 };

const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
const clampInt = (x, lo, hi, d) => (Number.isFinite(+x) ? clamp(Math.round(+x), lo, hi) : d);

// ---------------------------------------------------------------- state model

export function newPattern(i = 0) {
  return { name: 'PTN ' + String(i).padStart(2, '0'), beats: 4, unit: 4, bars: 1, swing: 50, events: [], volume: new Array(NUM_CHANNELS).fill(31) };
}
export function newSong(i = 0) {
  return { name: 'SONG ' + String.fromCharCode(65 + i), tempo: 120, steps: [] };
}
export function newChain(i = 0) {
  return { name: 'CHAIN ' + (i + 1), songs: [] };
}

/** Fresh memory. voice/channel/fx hold opaque engine params (Engine.getState() pieces). */
export function newState() {
  return {
    version: CART_VERSION,
    patterns: Array.from({ length: NUM_PATTERNS }, (_, i) => newPattern(i)),
    songs: Array.from({ length: NUM_SONGS }, (_, i) => newSong(i)),
    chains: Array.from({ length: NUM_CHAINS }, (_, i) => newChain(i)),
    noteMap: MIDI_GM_NOTES.slice(),
    midi: { rxCh: 10, txCh: 10, clockIn: false, clockOut: false, notesOut: false, deviceNo: 1 },
    sync: 'int',            // 'int' | 'midi' | 'tape' | 'ext'
    tempo: 120,             // 40..250
    quantize: Q_1_16,       // index into QUANTIZE
    swing: 50,              // default swing for new/cleared patterns (pattern.swing is authoritative)
    click: { on: false, level: 0.5 },
    volume: 1,              // master
    voice: Array.from({ length: NUM_VOICES }, () => ({})),
    channel: Array.from({ length: NUM_CHANNELS }, () => ({})),
    fx: { rev: {}, dly: {} },
    samples: new Array(NUM_VOICES).fill(null), // null | {name, sr, data: Float32Array}
  };
}

export const ticksPerBeat = (p) => (PPQN * 4) / p.unit;
export const barTicks = (p) => p.beats * ticksPerBeat(p);
export const patLen = (p) => p.bars * barTicks(p);

function sortEvents(p) { p.events.sort((a, b) => a.t - b.t || a.v - b.v); }

// first index with events[i].t >= t (events sorted by t)
function lowerBound(ev, t) {
  let lo = 0, hi = ev.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (ev[m].t < t) lo = m + 1; else hi = m; }
  return lo;
}

/** Seq.tick: events of a pattern with fromTick <= t < toTick. */
export function tickEvents(p, fromTick, toTick) {
  const ev = p.events, out = [];
  for (let i = lowerBound(ev, fromTick); i < ev.length && ev[i].t < toTick; i++) out.push(ev[i]);
  return out;
}

// ---------------------------------------------------------------- editing

/** Quantize (and swing) a raw tick; result is NOT wrapped to the pattern length. */
export function quantizeTick(raw, qIndex, swing = 50) {
  const g = (QUANTIZE[qIndex] || QUANTIZE[Q_1_16]).ticks;
  if (g <= 1) return Math.round(raw);
  const step = Math.round(raw / g);
  let t = step * g;
  if (swing > 50 && (step & 1)) t += Math.round(g * (2 * swing / 100 - 1));
  return t;
}

function putEvent(p, t, v, vel, acc) {
  const ev = p.events;
  let i = lowerBound(ev, t);
  for (let j = i; j < ev.length && ev[j].t === t; j++) {
    if (ev[j].v === v) { ev[j].vel = vel; ev[j].acc = acc; return ev[j]; }
  }
  while (i < ev.length && ev[i].t === t && ev[i].v < v) i++;
  const e = { t, v, vel, acc };
  ev.splice(i, 0, e);
  return e;
}

/** Real-time write: raw (possibly fractional) tick -> quantized+swung, wrapped event. Returns the event. */
export function recordHit(state, pn, rawTick, v, vel = 100, acc = 0, qIndex = state.quantize) {
  const p = state.patterns[pn], len = patLen(p);
  let t = quantizeTick(rawTick, qIndex, p.swing) % len;
  if (t < 0) t += len;
  return putEvent(p, t, v | 0, clampInt(vel, 1, 127, 100), clampInt(acc, 0, 2, 0));
}

/** Step write insert at an exact tick. */
export function stepInsert(state, pn, t, v, vel = 100, acc = 0) {
  const p = state.patterns[pn];
  if (t < 0 || t >= patLen(p)) return null;
  return putEvent(p, t | 0, v | 0, clampInt(vel, 1, 127, 100), clampInt(acc, 0, 2, 0));
}

/** Step write delete at tick t (v < 0: all voices). Returns the number removed. */
export function stepDelete(state, pn, t, v = -1) {
  return deleteEvents(state, pn, t, t + 1, v);
}

/** Delete events with from <= t < to (v < 0: all voices). */
export function deleteEvents(state, pn, from, to, v = -1) {
  const p = state.patterns[pn], n = p.events.length;
  p.events = p.events.filter((e) => !(e.t >= from && e.t < to && (v < 0 || e.v === v)));
  return n - p.events.length;
}

export function clearPattern(state, pn) {
  const p = state.patterns[pn];
  Object.assign(p, newPattern(pn), { name: p.name, swing: state.swing });
}

export function copyPattern(state, src, dst) {
  if (src === dst) return;
  state.patterns[dst] = clonePattern(state.patterns[src]);
}
function clonePattern(p) {
  return { ...p, events: p.events.map((e) => ({ ...e })), volume: p.volume.slice() };
}

/** Time signature. Events beyond the new length are dropped. */
export function setBeats(state, pn, beats, unit = state.patterns[pn].unit) {
  const p = state.patterns[pn];
  p.beats = clampInt(beats, 1, 32, 4);
  p.unit = [4, 8, 16].includes(+unit) ? +unit : 4;
  trimPattern(p);
}
export function setBars(state, pn, bars) {
  const p = state.patterns[pn];
  p.bars = clampInt(bars, 1, 99, 1);
  trimPattern(p);
}
function trimPattern(p) { const len = patLen(p); p.events = p.events.filter((e) => e.t < len); }

export function setSwing(state, pn, swing) { state.patterns[pn].swing = clampInt(swing, 50, 75, 50); }
export function renamePattern(state, pn, name) { state.patterns[pn].name = String(name).slice(0, 16); }
export function renameSong(state, sn, name) { state.songs[sn].name = String(name).slice(0, 16); }

function cleanStep(s) {
  switch (s && s.type) {
    case 'pat': return { type: 'pat', n: clampInt(s.n, 0, NUM_PATTERNS - 1, 0) };
    case 'rep': return { type: 'rep', from: clampInt(s.from, 0, 9999, 0), count: clampInt(s.count, 1, 99, 2) };
    case 'tempo': return { type: 'tempo', bpm: clamp(+s.bpm || 120, 40, 250) };
    case 'vol': return { type: 'vol', ch: clampInt(s.ch, 0, 11, 0), level: clampInt(s.level, 0, 31, 31) };
    case 'mark': return { type: 'mark' };
  }
  throw new Error('bad song step');
}
/** Insert a step before index idx (idx >= length appends). 'rep' steps after idx have their `from` shifted. */
export function songInsertStep(state, sn, idx, step) {
  const steps = state.songs[sn].steps;
  idx = clamp(idx | 0, 0, steps.length);
  for (let i = idx; i < steps.length; i++) if (steps[i].type === 'rep' && steps[i].from >= idx) steps[i].from++;
  steps.splice(idx, 0, cleanStep(step));
}
export function songDeleteStep(state, sn, idx) {
  const steps = state.songs[sn].steps;
  if (idx < 0 || idx >= steps.length) return;
  steps.splice(idx, 1);
  for (let i = idx; i < steps.length; i++) if (steps[i].type === 'rep' && steps[i].from > idx) steps[i].from--;
}
export function songSetStep(state, sn, idx, step) { state.songs[sn].steps[idx] = cleanStep(step); }
export function setSongTempo(state, sn, bpm) { state.songs[sn].tempo = clamp(+bpm || 120, 40, 250); }

/** Replace a chain's song list (max 99 entries). */
export function chainSet(state, cn, songs) {
  state.chains[cn].songs = songs.slice(0, CHAIN_MAX).map((s) => clampInt(s, 0, NUM_SONGS - 1, 0));
}
export function chainInsert(state, cn, idx, song) {
  const c = state.chains[cn].songs;
  if (c.length >= CHAIN_MAX) return;
  c.splice(clamp(idx | 0, 0, c.length), 0, clampInt(song, 0, NUM_SONGS - 1, 0));
}
export function chainDelete(state, cn, idx) { state.chains[cn].songs.splice(idx, 1); }

// ---------------------------------------------------------------- song / chain cursors

/** Walks a song's steps; next() returns the next pattern number or null at the end. */
export class SongCursor {
  constructor(state, sn) { this.state = state; this.sn = sn; this.reset(); }
  reset() {
    const song = this.state.songs[this.sn];
    this.si = 0; this.at = -1; this.cnt = new Map();
    this.tempo = song ? song.tempo : 120;
    this.vol = new Array(NUM_CHANNELS).fill(-1); // -1 = use pattern volume
  }
  next() {
    const song = this.state.songs[this.sn];
    if (!song) return null;
    const steps = song.steps;
    let guard = 0;
    while (this.si < steps.length) {
      if (++guard > 100000) return null;
      const s = steps[this.si];
      if (s.type === 'pat') { this.at = this.si++; return s.n; }
      if (s.type === 'rep') {
        // count = total number of plays of steps [from, this step)
        const c = this.cnt.get(this.si) || 0;
        if (c + 1 < s.count && s.from < this.si) {
          this.cnt.set(this.si, c + 1);
          for (let k = s.from; k < this.si; k++) this.cnt.delete(k);
          this.si = s.from;
        } else { this.cnt.delete(this.si); this.si++; }
        continue;
      }
      if (s.type === 'tempo') this.tempo = s.bpm;
      else if (s.type === 'vol') this.vol[s.ch] = s.level;
      this.si++;
    }
    return null;
  }
}

class ChainCursor {
  constructor(state, cn) { this.state = state; this.cn = cn; this.reset(); }
  reset() { this.ci = 0; this.sc = this._song(); }
  _song() {
    const list = this.state.chains[this.cn].songs;
    return this.ci < list.length ? new SongCursor(this.state, list[this.ci]) : null;
  }
  next() {
    while (this.sc) {
      const r = this.sc.next();
      if (r !== null) return r;
      this.ci++; this.sc = this._song();
    }
    return null;
  }
  get tempo() { return this.sc ? this.sc.tempo : 120; }
  get vol() { return this.sc ? this.sc.vol : new Array(NUM_CHANNELS).fill(-1); }
  get at() { return this.sc ? this.sc.at : -1; }
  get sn() { return this.sc ? this.sc.sn : -1; }
}

/** Flatten a song to absolute events/tempos (raw vel, no volume scaling). */
export function flattenSong(state, sn) {
  const cur = new SongCursor(state, sn), events = [], tempos = [], sigs = [];
  let t0 = 0, n, bpm = null, sig = '', bars = 0;
  while ((n = cur.next()) !== null) {
    const p = state.patterns[n];
    if (cur.tempo !== bpm) { bpm = cur.tempo; tempos.push({ t: t0, bpm }); }
    const s = p.beats + '/' + p.unit;
    if (s !== sig) { sig = s; sigs.push({ t: t0, beats: p.beats, unit: p.unit }); }
    for (const e of p.events) events.push({ t: t0 + e.t, v: e.v, vel: e.vel, acc: e.acc });
    t0 += patLen(p);
    if (++bars > 100000) break;
  }
  return { events, tempos, sigs, length: t0 };
}

// ---------------------------------------------------------------- Player

/**
 * Sample-accurate sequencer clock. advance() returns note events
 * {offset, v, vel, acc} and metronome events {offset, v:-1, vel:0, acc:0, click:1|2} (2 = bar downbeat).
 * vel already includes pattern volume / song VOL steps (level 0..31 scales vel; 0 mutes).
 */
export class Player {
  constructor(state) {
    this.state = state;
    this.mode = 'pattern'; this.n = 0;
    this.running = false; this.ended = false; this.ext = false;
    this.cursor = null;
    this.pat = 0; this.tick = 0; this.cur = -1; this.pass = 0;
    this.wait = 0; this.tempo = state.tempo; this.tempoOverride = null;
    this.skip = new Map(); // event -> pass in which it must not sound (just played live)
  }
  setMode(mode, n = 0) {
    this.mode = mode === 'song' || mode === 'chain' ? mode : 'pattern';
    this.n = n | 0;
    this._rewind();
  }
  setExternal(on) { this.ext = !!on; }
  _rewind() {
    this.tick = 0; this.cur = -1; this.pass = 0; this.wait = 0; this.ended = false; this.skip.clear();
    if (this.mode === 'pattern') { this.cursor = null; this.pat = this.n; this.tempo = this.state.tempo; return true; }
    this.cursor = this.mode === 'song' ? new SongCursor(this.state, this.n) : new ChainCursor(this.state, this.n);
    const first = this.cursor.next();
    if (first === null) { this.ended = true; return false; }
    this.pat = first; this.tempo = this.cursor.tempo;
    return true;
  }
  start() { this.running = this._rewind(); }
  stop() { this.running = false; }
  cont() {
    if (this.ended) return this.start();
    this.wait = 0; this.running = true;
  }
  /** Fixed tempo override (e.g. tempo slider while a song plays); null = follow state/song. */
  setTempo(bpm) { this.tempoOverride = bpm ? clamp(+bpm, 40, 250) : null; }
  get bpm() { return this.tempoOverride || (this.mode === 'pattern' ? this.state.tempo : this.tempo); }

  advance(nFrames, sr, bpm) {
    const out = [];
    if (bpm) this.tempoOverride = bpm;
    if (!this.running || this.ext) return out;
    while (this.running && this.wait < nFrames) {
      const off = Math.min(nFrames - 1, Math.max(0, Math.ceil(this.wait - 1e-6)));
      this._fire(off, out);
      this.wait += (sr * 60) / (this.bpm * PPQN);
    }
    if (this.running) this.wait -= nFrames;
    this._spt = (sr * 60) / (this.bpm * PPQN);
    return out;
  }

  /** MIDI clock (F8) while synced externally: one tick per clock. */
  clock24(offset = 0) {
    const out = [];
    if (this.running) this._fire(offset, out);
    return out;
  }

  /** Song Position Pointer (MIDI beats = 16ths = 6 ticks). */
  spp(n) { this.locate((n | 0) * 6); }

  /** Move to absolute tick from the start of the pattern/song/chain. */
  locate(abs) {
    const wasRunning = this.running;
    if (!this._rewind()) { this.running = false; return; }
    if (this.mode === 'pattern') {
      this.tick = abs % patLen(this.state.patterns[this.pat]);
    } else {
      let guard = 0;
      for (;;) {
        const L = patLen(this.state.patterns[this.pat]);
        if (abs < L) { this.tick = abs; break; }
        abs -= L;
        const r = this.cursor.next();
        if (r === null || ++guard > 100000) { this.ended = true; this.running = false; return; }
        this.pat = r; this.tempo = this.cursor.tempo;
      }
    }
    this.cur = this.tick - 1;
    this.running = wasRunning;
  }

  _fire(off, out) {
    const st = this.state;
    let p = st.patterns[this.pat];
    if (this.tick >= patLen(p)) {
      this.pass++; this.tick = 0;
      if (this.cursor) {
        const r = this.cursor.next();
        if (r === null) { this.running = false; this.ended = true; return; }
        this.pat = r; this.tempo = this.cursor.tempo;
        p = st.patterns[r];
      }
    }
    const t = this.tick, ev = p.events, ovr = this.cursor ? this.cursor.vol : null;
    if (st.click && st.click.on && t % ticksPerBeat(p) === 0) {
      out.push({ offset: off, v: -1, vel: 0, acc: 0, click: t % barTicks(p) === 0 ? 2 : 1 });
    }
    for (let i = lowerBound(ev, t); i < ev.length && ev[i].t === t; i++) {
      const e = ev[i];
      if (this.skip.size && this.skip.has(e)) {
        const sp = this.skip.get(e);
        if (sp <= this.pass) this.skip.delete(e);
        if (sp === this.pass) continue;
      }
      const ch = e.v % NUM_CHANNELS;
      const lvl = ovr && ovr[ch] >= 0 ? ovr[ch] : p.volume[ch];
      if (lvl <= 0) continue;
      const vel = lvl >= 31 ? e.vel : Math.max(1, Math.round((e.vel * lvl) / 31));
      out.push({ offset: off, v: e.v, vel, acc: e.acc });
    }
    this.cur = t; this.tick = t + 1;
  }

  /** Fractional tick position at the end of the last processed block. */
  _rawPos() {
    const spt = this._spt || 1;
    return this.tick - Math.max(0, this.wait) / spt;
  }

  /**
   * Real-time write (overdub) while looping in pattern mode: records at the current play position
   * into the playing pattern and makes sure the new event doesn't double-trigger right after the live hit.
   */
  record(v, vel = 100, acc = 0) {
    if (!this.running || this.mode !== 'pattern') return null;
    const st = this.state, p = st.patterns[this.pat], len = patLen(p);
    const q = quantizeTick(this._rawPos(), st.quantize, p.swing);
    const e = recordHit(st, this.pat, q, v, vel, acc, 9 /* already quantized: 1/96 */);
    if (q >= len) this.skip.set(e, this.pass + 1);
    else if (e.t >= this.tick) this.skip.set(e, this.pass);
    return e;
  }

  pos() {
    const p = this.state.patterns[this.pat];
    const t = Math.max(0, this.cur), tpb = ticksPerBeat(p), bt = barTicks(p);
    return {
      mode: this.mode, running: this.running, ended: this.ended,
      song: this.cursor ? this.cursor.sn : -1, chain: this.mode === 'chain' ? this.n : -1,
      chainPos: this.mode === 'chain' ? this.cursor.ci : -1,
      step: this.cursor ? this.cursor.at : -1,
      pattern: this.pat, bar: Math.floor(t / bt) + 1, beat: Math.floor((t % bt) / tpb) + 1, tick: t % tpb,
      bpm: this.bpm,
    };
  }
}

// ---------------------------------------------------------------- randomize

/** xorshift32; returns u32 generator with helpers. */
export function rng(seed) {
  let x = (seed >>> 0) || 0x9E3779B9;
  const next = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x; };
  next.f = () => next() / 4294967296;
  next.r = (lo, hi) => lo + (hi - lo) * next.f();
  next.i = (lo, hi) => lo + Math.floor((hi - lo + 1) * next.f());
  next.p = (prob) => next.f() < prob;
  return next;
}

// 16-step probability tables (one 4/4 bar of 16ths). v: BD1 0, SD1 1, RIM1 2, HHCLS 7, RIDE 8, TAMB 10,
// SHAKER 11, HHOPN 19, CLAPS 22, COWBELL 23.
const GENRES = {
  rock: { tempo: [96, 140], swing: [50, 50], parts: {
    0: [.95, 0, .1, 0, 0, 0, .25, 0, .85, 0, .4, 0, 0, 0, .15, 0],
    1: [0, 0, 0, 0, .98, 0, 0, .05, 0, 0, 0, 0, .98, 0, 0, .2],
    7: [.95, .05, .9, .05, .95, .05, .9, .05, .95, .05, .9, .05, .95, .05, .9, .05],
    19: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .25, 0] } },
  funk: { tempo: [92, 115], swing: [50, 58], parts: {
    0: [.95, 0, .2, .3, 0, 0, .3, 0, .4, .2, .6, 0, 0, .3, 0, .1],
    1: [0, 0, 0, .1, .98, 0, .15, .3, 0, .25, 0, .1, .98, 0, .2, .3],
    7: [.95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7],
    19: [0, 0, 0, 0, 0, 0, 0, .3, 0, 0, 0, 0, 0, 0, 0, .3],
    10: [0, 0, .2, 0, 0, 0, .2, 0, 0, 0, .2, 0, 0, 0, .2, 0] } },
  hiphop: { tempo: [80, 98], swing: [56, 66], parts: {
    0: [.98, 0, 0, .2, 0, 0, .3, .2, .1, .4, .6, 0, 0, 0, .2, 0],
    1: [0, 0, 0, 0, .98, 0, 0, 0, 0, 0, 0, 0, .98, 0, 0, .1],
    7: [.9, .2, .9, .3, .9, .2, .9, .3, .9, .2, .9, .3, .9, .2, .9, .4],
    2: [0, 0, 0, 0, 0, 0, 0, .15, 0, 0, 0, 0, 0, .15, 0, 0] } },
  house: { tempo: [118, 128], swing: [50, 56], parts: {
    0: [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, .1],
    22: [0, 0, 0, 0, .95, 0, 0, 0, 0, 0, 0, 0, .95, 0, 0, 0],
    7: [0, .2, 0, .5, 0, .2, 0, .5, 0, .2, 0, .5, 0, .2, 0, .5],
    19: [0, 0, .95, 0, 0, 0, .95, 0, 0, 0, .95, 0, 0, 0, .95, 0],
    11: [.6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6] } },
  electro: { tempo: [108, 130], swing: [50, 50], parts: {
    0: [.95, 0, 0, .3, 0, 0, .6, 0, 0, .3, .5, 0, 0, 0, 0, .2],
    1: [0, 0, 0, 0, .95, 0, 0, 0, 0, 0, 0, 0, .95, 0, 0, .1],
    22: [0, 0, 0, 0, .5, 0, 0, 0, 0, 0, 0, 0, .5, 0, 0, 0],
    7: [.9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4],
    23: [0, 0, .15, 0, 0, 0, 0, .15, 0, 0, .15, 0, 0, 0, 0, 0] } },
  breakbeat: { tempo: [130, 170], swing: [50, 54], parts: {
    0: [.98, 0, .3, 0, 0, 0, 0, 0, 0, 0, .9, .2, 0, 0, .2, 0],
    1: [0, 0, 0, 0, .98, 0, 0, .25, 0, .3, 0, 0, .9, 0, .1, .35],
    8: [.8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0],
    7: [0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3] } },
};
export const GENRE_NAMES = Object.keys(GENRES);

/** Generate a 2-bar 4/4 pattern; fill on the last bar's last beat(s); crash on 1 if `crash`. */
function genPattern(R, g, name, { fill = false, crash = false, ride = false, swing = 50 } = {}) {
  const p = newPattern();
  p.name = name; p.bars = 2; p.swing = swing;
  const sixteenth = 6, bar = 96;
  for (let b = 0; b < p.bars; b++) {
    const fillFrom = fill && b === p.bars - 1 ? (R.p(0.5) ? 8 : 12) : 16;
    for (const [vs, probs] of Object.entries(g.parts)) {
      let v = +vs;
      if (ride && v === 7) v = 8;
      for (let s = 0; s < 16; s++) {
        if (s >= fillFrom && v !== 0) continue;
        if (!R.p(probs[s] * (b === 1 ? R.r(0.85, 1.1) : 1))) continue;
        const acc = s % 4 === 0 && R.p(0.25) ? 1 : 0;
        const vel = Math.round(clamp((s % 4 === 0 ? 100 : s % 2 === 0 ? 88 : 70) + R.r(-14, 14), 1, 127));
        putEvent(p, b * bar + quantizeTick(s * sixteenth, Q_1_16, swing), v, vel, acc);
      }
    }
    if (fillFrom < 16) {
      const toms = [3, 4, 5, 6, 15, 16, 17, 18];
      let ti = R.i(0, 1);
      for (let s = fillFrom; s < 16; s++) {
        if (!R.p(0.8)) continue;
        const v = R.p(0.35) ? 1 : toms[Math.min(toms.length - 1, ti)];
        if (v !== 1 && R.p(0.6)) ti++;
        putEvent(p, b * bar + quantizeTick(s * sixteenth, Q_1_16, swing), v, Math.round(R.r(80, 120)), s === 15 && R.p(0.5) ? 2 : 0);
      }
    }
  }
  if (crash) putEvent(p, 0, 9, 110, 1);
  return p;
}

function randomVoice(R, v) {
  const metal = [7, 8, 9, 19, 20, 21].includes(v);
  return {
    pitch: Math.round(R.p(0.6) ? R.r(-150, 150) : R.r(-500, 500)),
    level: R.i(22, 31), ar: R.i(90, 99), d1r: R.i(0, 70), d1l: R.i(60, 99), d2r: R.i(metal ? 20 : 30, 80), rr: R.i(30, 90),
    gate: Math.round(R.r(50, metal ? 2000 : 800)), // ms
    bendRate: R.p(0.25) ? R.i(10, 80) : 0, bendRange: R.p(0.25) ? Math.round(R.r(-1200, 600)) : 0,
    reverse: R.p(0.06) ? 1 : 0, loop: R.p(0.04) ? 1 : 0,
  };
}
function randomRev(R) {
  const program = R.i(0, 10);
  return {
    program, time: +R.r(0.5, program >= 9 ? 1.5 : 5).toFixed(2), hiRatio: +R.r(0.3, 1).toFixed(2), delay: Math.round(R.r(0, 80)),
    diffusion: R.i(3, 10), density: R.i(40, 100), erLevel: +R.r(0.2, 1).toFixed(2), revLevel: +R.r(0.4, 1).toFixed(2),
    hpf: Math.round(R.r(20, 300)), lpf: Math.round(R.r(4000, 16000)), gateTime: Math.round(R.r(100, 450)),
  };
}
const DLY_SYNC = ['off', '1/4', '1/8', '1/8.', '1/16', '1/4T'];
export { DLY_SYNC };
function randomDly(R) {
  const tl = Math.round(R.r(60, 700));
  return {
    timeL: tl, timeR: R.p(0.5) ? tl : Math.round(tl * R.r(0.5, 1.5)), sync: R.p(0.5) ? R.i(1, 5) : 0, feedback: Math.round(R.r(10, 70)),
    pingpong: R.p(0.5) ? 1 : 0, hiCut: Math.round(R.r(3000, 14000)), loCut: Math.round(R.r(40, 400)), modRate: +R.r(0.1, 3).toFixed(2),
    modDepth: +R.r(0, 0.5).toFixed(2), duckThresh: Math.round(R.r(-40, -6)), duckRelease: Math.round(R.r(50, 500)),
    spread: +R.r(0.3, 1).toFixed(2), mix: +R.r(0.15, 0.5).toFixed(2),
  };
}
function randomChannel(R, ch) {
  const centre = ch === 0 || ch === 1 || ch === 10;
  return {
    fader: +R.r(0.6, 0.95).toFixed(2), pan: centre ? 0 : +R.r(-0.7, 0.7).toFixed(2),
    sendRev: +(ch === 1 || ch === 10 ? R.r(0.2, 0.6) : R.r(0, 0.3)).toFixed(2), sendDly: +(R.p(0.3) ? R.r(0.1, 0.4) : 0).toFixed(2), mute: false,
  };
}

/**
 * Deterministic randomize. scope: 'all' | 'pattern' | 'voices' | 'fx'.
 * opts.pattern: target pattern for 'pattern' (default 0); opts.start: first pattern for 'all' (default 0, uses 4);
 * opts.song: song slot for 'all' (default 0); opts.genre: force a genre name. Returns {genre}.
 */
export function randomize(state, seed, scope = 'all', opts = {}) {
  const R = rng(seed);
  const genre = GENRES[opts.genre] ? opts.genre : GENRE_NAMES[R.i(0, GENRE_NAMES.length - 1)];
  const g = GENRES[genre];
  const swing = Math.round(R.r(g.swing[0], g.swing[1]));
  if (scope === 'voices' || scope === 'all') {
    for (let v = 0; v < NUM_VOICES; v++) state.voice[v] = Object.assign({}, state.voice[v], randomVoice(R, v));
  }
  if (scope === 'fx' || scope === 'all') {
    state.fx = state.fx || {};
    state.fx.rev = Object.assign({}, state.fx.rev, randomRev(R));
    state.fx.dly = Object.assign({}, state.fx.dly, randomDly(R));
  }
  if (scope === 'all') {
    for (let ch = 0; ch < NUM_CHANNELS; ch++) state.channel[ch] = Object.assign({}, state.channel[ch], randomChannel(R, ch));
  }
  if (scope === 'pattern') {
    const pn = opts.pattern | 0;
    state.patterns[pn] = genPattern(R, g, genre.toUpperCase().slice(0, 8), { fill: R.p(0.7), swing });
  }
  if (scope === 'all') {
    const s0 = clampInt(opts.start, 0, NUM_PATTERNS - 4, 0), sn = clampInt(opts.song, 0, NUM_SONGS - 1, 0);
    const tag = genre.slice(0, 4).toUpperCase();
    state.patterns[s0] = genPattern(R, g, tag + ' A', { swing });
    state.patterns[s0 + 1] = genPattern(R, g, tag + ' A2', { swing });
    state.patterns[s0 + 2] = genPattern(R, g, tag + ' FIL', { fill: true, swing });
    state.patterns[s0 + 3] = genPattern(R, g, tag + ' B', { crash: true, ride: R.p(0.5), swing });
    const tempo = Math.round(R.r(g.tempo[0], g.tempo[1]));
    state.tempo = tempo;
    state.songs[sn] = { name: genre.toUpperCase().slice(0, 8), tempo, steps: [
      { type: 'pat', n: s0 }, { type: 'pat', n: s0 + 1 }, { type: 'rep', from: 0, count: 2 },
      { type: 'pat', n: s0 + 2 }, { type: 'mark' }, { type: 'pat', n: s0 + 3 }, { type: 'rep', from: 5, count: 3 },
      { type: 'pat', n: s0 + 2 }, { type: 'pat', n: s0 }] };
  }
  return { genre };
}

// ---------------------------------------------------------------- bytes helpers

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
export function b64encode(u8) {
  let s = '';
  for (let i = 0; i < u8.length; i += 3) {
    const a = u8[i], b = u8[i + 1], c = u8[i + 2], n = (a << 16) | ((b || 0) << 8) | (c || 0);
    s += B64[n >> 18] + B64[(n >> 12) & 63] + (b === undefined ? '=' : B64[(n >> 6) & 63]) + (c === undefined ? '=' : B64[n & 63]);
  }
  return s;
}
export function b64decode(s) {
  s = s.replace(/[^A-Za-z0-9+/]/g, '');
  const out = new Uint8Array(Math.floor((s.length * 3) / 4));
  let o = 0;
  for (let i = 0; i < s.length; i += 4) {
    const n = (B64.indexOf(s[i]) << 18) | (B64.indexOf(s[i + 1]) << 12) | ((B64.indexOf(s[i + 2]) & 63) << 6) | (B64.indexOf(s[i + 3]) & 63);
    out[o++] = n >> 16;
    if (i + 2 < s.length) out[o++] = (n >> 8) & 255;
    if (i + 3 < s.length) out[o++] = n & 255;
  }
  return out.subarray(0, o);
}
function utf8encode(str) {
  if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(str);
  const bin = unescape(encodeURIComponent(str));
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}
function utf8decode(u8) {
  if (typeof TextDecoder !== 'undefined') return new TextDecoder().decode(u8);
  let bin = '';
  for (let i = 0; i < u8.length; i++) bin += String.fromCharCode(u8[i]);
  return decodeURIComponent(escape(bin));
}
async function pipeBytes(u8, ts) {
  const w = ts.writable.getWriter();
  w.write(u8).catch(() => {}); w.close().catch(() => {});
  const r = ts.readable.getReader(), chunks = [];
  let n = 0;
  for (;;) { const { value, done } = await r.read(); if (done) break; chunks.push(value); n += value.length; }
  const out = new Uint8Array(n);
  let o = 0;
  for (const c of chunks) { out.set(c, o); o += c.length; }
  return out;
}

// ---------------------------------------------------------------- cartridge

function samplesToCart(samples) {
  return (samples || []).map((s) => {
    if (!s || !s.data) return null;
    const pcm = new Int16Array(s.data.length);
    for (let i = 0; i < pcm.length; i++) pcm[i] = Math.round(clamp(s.data[i], -1, 1) * 32767);
    return { name: s.name || '', sr: s.sr, length: pcm.length, pcm16: b64encode(new Uint8Array(pcm.buffer)) };
  });
}
function samplesFromCart(list) {
  const out = new Array(NUM_VOICES).fill(null);
  (list || []).forEach((s, v) => {
    if (!s || !s.pcm16 || v >= NUM_VOICES) return;
    const b = b64decode(s.pcm16), n = b.length >> 1, dv = new DataView(b.buffer, b.byteOffset, b.length);
    const data = new Float32Array(n);
    for (let i = 0; i < n; i++) data[i] = dv.getInt16(i * 2, true) / 32767;
    out[v] = { name: s.name || '', sr: +s.sr || 44100, data };
  });
  return out;
}

/** Plain JSON object of the cartridge (samples as 16-bit LE PCM base64). */
export function cartToJSON(state) {
  const { samples, ...rest } = state;
  return { format: 'omega-five-cart', version: CART_VERSION, state: { ...rest, version: CART_VERSION }, samples: samplesToCart(samples) };
}
/** Migrate older cart objects to CART_VERSION (stub: v0 = bare state object). */
export function migrateCart(obj) {
  if (!obj || typeof obj !== 'object') throw new Error('not an Omega Five cartridge');
  if (obj.format !== 'omega-five-cart') obj = { format: 'omega-five-cart', version: 0, state: obj, samples: obj.samples || [] };
  if (obj.version > CART_VERSION) throw new Error('cartridge version ' + obj.version + ' is newer than this app');
  // future: if (obj.version < 2) { ...upgrade... }
  obj.version = CART_VERSION;
  return obj;
}
export function cartFromJSON(obj) {
  obj = migrateCart(obj);
  const s = normalizeState(obj.state);
  s.samples = samplesFromCart(obj.samples);
  return s;
}

/** Fill defaults / validate a loaded state object. */
export function normalizeState(src = {}) {
  const s = newState();
  (src.patterns || []).slice(0, NUM_PATTERNS).forEach((p, i) => {
    if (!p) return;
    const q = s.patterns[i];
    q.name = String(p.name ?? q.name); q.beats = clampInt(p.beats, 1, 32, 4); q.unit = [4, 8, 16].includes(+p.unit) ? +p.unit : 4;
    q.bars = clampInt(p.bars, 1, 99, 1); q.swing = clampInt(p.swing, 50, 75, 50);
    if (Array.isArray(p.volume)) q.volume = q.volume.map((d, ch) => clampInt(p.volume[ch], 0, 31, d));
    const len = patLen(q);
    q.events = (p.events || []).filter((e) => e && e.t >= 0 && e.t < len && e.v >= 0 && e.v < NUM_VOICES)
      .map((e) => ({ t: e.t | 0, v: e.v | 0, vel: clampInt(e.vel, 1, 127, 100), acc: clampInt(e.acc, 0, 2, 0) }));
    sortEvents(q);
  });
  (src.songs || []).slice(0, NUM_SONGS).forEach((g, i) => {
    if (!g) return;
    const d = s.songs[i];
    d.name = String(g.name ?? d.name); d.tempo = clamp(+g.tempo || 120, 40, 250);
    d.steps = (g.steps || []).flatMap((st) => { try { return [cleanStep(st)]; } catch { return []; } });
  });
  (src.chains || []).slice(0, NUM_CHAINS).forEach((c, i) => {
    if (!c) return;
    s.chains[i].name = String(c.name ?? s.chains[i].name);
    chainSet(s, i, c.songs || []);
  });
  if (Array.isArray(src.noteMap)) s.noteMap = s.noteMap.map((d, v) => clampInt(src.noteMap[v], 0, 127, d));
  if (src.midi) Object.assign(s.midi, src.midi);
  if (['int', 'midi', 'tape', 'ext'].includes(src.sync)) s.sync = src.sync;
  if (src.tempo) s.tempo = clamp(+src.tempo, 40, 250);
  if (src.quantize !== undefined) s.quantize = clampInt(src.quantize, 0, QUANTIZE.length - 1, Q_1_16);
  if (src.swing !== undefined) s.swing = clampInt(src.swing, 50, 75, 50);
  if (src.click) Object.assign(s.click, src.click);
  if (src.volume !== undefined) s.volume = clamp(+src.volume || 0, 0, 1);
  if (Array.isArray(src.voice)) src.voice.slice(0, NUM_VOICES).forEach((o, v) => { if (o) s.voice[v] = o; });
  if (Array.isArray(src.channel)) src.channel.slice(0, NUM_CHANNELS).forEach((o, c) => { if (o) s.channel[c] = o; });
  if (src.fx && typeof src.fx === 'object') s.fx = src.fx;
  if (Array.isArray(src.samples)) src.samples.forEach((x, v) => { if (x && x.data instanceof Float32Array && v < NUM_VOICES) s.samples[v] = x; });
  return s;
}

/** Cartridge file bytes (*.o5cart). gzip (default true) only if CompressionStream exists. */
export async function saveCart(state, { gzip = true } = {}) {
  const raw = utf8encode(JSON.stringify(cartToJSON(state)));
  if (gzip && typeof CompressionStream !== 'undefined') return pipeBytes(raw, new CompressionStream('gzip'));
  return raw;
}
/** Load cartridge bytes (gzip or plain JSON). */
export async function loadCart(bytes) {
  let u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (u8[0] === 0x1f && u8[1] === 0x8b) {
    if (typeof DecompressionStream === 'undefined') throw new Error('gzip cartridge needs DecompressionStream');
    u8 = await pipeBytes(u8, new DecompressionStream('gzip'));
  }
  return cartFromJSON(JSON.parse(utf8decode(u8)));
}

// ---------------------------------------------------------------- SysEx bulk dump

/** 7-bit pack: each group of up to 7 bytes -> 1 MSB byte (bit i = MSB of byte i) + the 7 low parts. */
export function pack7(u8) {
  const out = [];
  for (let i = 0; i < u8.length; i += 7) {
    const n = Math.min(7, u8.length - i);
    let msb = 0;
    for (let k = 0; k < n; k++) msb |= (u8[i + k] >> 7) << k;
    out.push(msb);
    for (let k = 0; k < n; k++) out.push(u8[i + k] & 0x7f);
  }
  return out;
}
export function unpack7(d) {
  const out = [];
  for (let i = 0; i < d.length; i += 8) {
    const msb = d[i], n = Math.min(7, d.length - i - 1);
    for (let k = 0; k < n; k++) out.push(d[i + 1 + k] | (((msb >> k) & 1) << 7));
  }
  return Uint8Array.from(out);
}
/** Whole cartridge (plain JSON, samples included) as F0 7D 4F 35 <packed> F7. */
export function sysexDump(state) {
  const packed = pack7(utf8encode(JSON.stringify(cartToJSON(state))));
  const out = new Uint8Array(SYSEX_HEADER.length + packed.length + 1);
  out.set(SYSEX_HEADER, 0); out.set(packed, SYSEX_HEADER.length); out[out.length - 1] = 0xF7;
  return out;
}
export function parseSysex(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  for (let i = 0; i < SYSEX_HEADER.length; i++) if (u8[i] !== SYSEX_HEADER[i]) throw new Error('not an Omega Five SysEx dump');
  if (u8[u8.length - 1] !== 0xF7) throw new Error('SysEx dump truncated (no F7)');
  return cartFromJSON(JSON.parse(utf8decode(unpack7(u8.subarray(SYSEX_HEADER.length, u8.length - 1)))));
}

// ---------------------------------------------------------------- SMF

const SMF_DIV = 96; // export division (DAW friendly); internal ticks * 4
const ACC_GAIN = [1, 1.413, 1.995]; // +0 / +3 / +6 dB
export const accentVel = (vel, acc) => Math.min(127, Math.round(vel * ACC_GAIN[acc | 0]));

/** note -> voice map for import / MIDI in: inverted noteMap plus GM_EXTRA. */
export function noteToVoiceMap(state) {
  const m = new Map(Object.entries(GM_EXTRA).map(([n, v]) => [+n, v]));
  for (let v = NUM_VOICES - 1; v >= 0; v--) m.set(state.noteMap[v], v); // noteMap wins; lowest voice wins on duplicates
  return m;
}

function vlq(n, out) {
  const st = [n & 0x7f];
  while ((n >>= 7)) st.unshift((n & 0x7f) | 0x80);
  out.push(...st);
}
function chunk(id, data) {
  const out = [...id].map((c) => c.charCodeAt(0));
  out.push((data.length >>> 24) & 255, (data.length >>> 16) & 255, (data.length >>> 8) & 255, data.length & 255);
  return out.concat(data);
}
function trackBytes(evs) { // evs: [{t, b:[bytes]}] sorted
  const out = [];
  let last = 0;
  for (const e of evs) { vlq(e.t - last, out); out.push(...e.b); last = e.t; }
  return out;
}

/**
 * SMF export, format 1, division 96: track 0 = name/tempo/time signature, track 1 = drums on ch 10 (GM notes from noteMap).
 * Note-on velocity includes the accent; a sequencer-specific meta FF 7F [7D 4F 35 note acc vel] before each
 * accented note keeps the raw vel/accent lossless for re-import.
 */
export function exportSMF(state, { what = 'pattern', n = 0 } = {}) {
  let flat;
  if (what === 'song') flat = flattenSong(state, n);
  else {
    const p = state.patterns[n];
    flat = { events: p.events.map((e) => ({ ...e })), tempos: [{ t: 0, bpm: state.tempo }], sigs: [{ t: 0, beats: p.beats, unit: p.unit }], length: patLen(p) };
  }
  const name = what === 'song' ? state.songs[n].name : state.patterns[n].name;
  const K = SMF_DIV / PPQN, end = flat.length * K;
  const t0 = [{ t: 0, b: [0xFF, 0x03, ...vlqLen(utf8encode(name))] }];
  for (const tp of flat.tempos) { const us = Math.round(60e6 / tp.bpm); t0.push({ t: tp.t * K, b: [0xFF, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255] }); }
  for (const sg of flat.sigs) t0.push({ t: sg.t * K, b: [0xFF, 0x58, 4, sg.beats, Math.log2(sg.unit), 24, 8] });
  t0.sort((a, b) => a.t - b.t);
  t0.push({ t: end, b: [0xFF, 0x2F, 0] });

  const notes = [], seen = new Set();
  const evs = flat.events.slice().sort((a, b) => a.t - b.t || a.v - b.v);
  for (const e of evs) {
    const note = state.noteMap[e.v], key = e.t + ':' + note;
    if (seen.has(key)) continue;
    seen.add(key);
    notes.push({ t: e.t, note, e });
  }
  const tr = [];
  const nextSame = new Map();
  for (let i = notes.length - 1; i >= 0; i--) { notes[i].next = nextSame.get(notes[i].note); nextSame.set(notes[i].note, notes[i].t); }
  for (const x of notes) {
    const on = x.t * K;
    const dur = Math.max(1, Math.min(6 * K, (x.next !== undefined ? x.next * K : end) - on));
    if (x.e.acc) tr.push({ t: on, o: 1, b: [0xFF, 0x7F, 6, 0x7D, 0x4F, 0x35, x.note, x.e.acc, x.e.vel] });
    tr.push({ t: on, o: 2, b: [0x99, x.note, accentVel(x.e.vel, x.e.acc)] });
    tr.push({ t: on + dur, o: 0, b: [0x89, x.note, 0x40] });
  }
  tr.sort((a, b) => a.t - b.t || a.o - b.o);
  const trEnd = Math.max(end, tr.length ? tr[tr.length - 1].t : 0);
  const t1 = [{ t: 0, b: [0xFF, 0x03, ...vlqLen(utf8encode('DRUMS'))] }, ...tr, { t: trEnd, b: [0xFF, 0x2F, 0] }];
  const head = chunk('MThd', [0, 1, 0, 2, (SMF_DIV >> 8) & 255, SMF_DIV & 255]);
  return Uint8Array.from(head.concat(chunk('MTrk', trackBytes(t0)), chunk('MTrk', trackBytes(t1))));
}
function vlqLen(bytes) { const o = []; vlq(bytes.length, o); return o.concat(Array.from(bytes)); }

/** Parse an SMF into raw tracks of events (absolute ticks in the file's division). */
export function parseSMF(buf) {
  const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  let p = 0;
  const str = (n) => { let s = ''; for (let i = 0; i < n; i++) s += String.fromCharCode(u8[p + i]); return s; };
  const u32 = () => { const v = ((u8[p] << 24) | (u8[p + 1] << 16) | (u8[p + 2] << 8) | u8[p + 3]) >>> 0; p += 4; return v; };
  const u16 = () => { const v = (u8[p] << 8) | u8[p + 1]; p += 2; return v; };
  if (u8.length < 14 || str(4) !== 'MThd') throw new Error('not a Standard MIDI File (no MThd)');
  p = 4;
  const hlen = u32(), hs = p;
  const format = u16(), ntrks = u16(), division = u16();
  if (division & 0x8000) throw new Error('SMPTE time division is not supported (only ticks-per-quarter-note files)');
  if (!division) throw new Error('bad SMF division 0');
  p = hs + hlen;
  const tracks = [];
  while (p + 8 <= u8.length && tracks.length < ntrks) {
    const id = str(4); p += 4;
    const len = u32(), end = Math.min(u8.length, p + len);
    if (id !== 'MTrk') { p = end; continue; }
    const ev = [];
    let t = 0, status = 0;
    const rd = () => { let v = 0, b; do { b = u8[p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80 && p < end); return v; };
    while (p < end) {
      t += rd();
      let b = u8[p];
      if (b === 0xFF) {
        p++; const type = u8[p++], l = rd();
        ev.push({ t, meta: type, data: u8.subarray(p, p + l) }); p += l;
        if (type === 0x2F) break;
        continue;
      }
      if (b === 0xF0 || b === 0xF7) { p++; const l = rd(); p += l; continue; }
      if (b & 0x80) { status = b; p++; } else if (!status) throw new Error('SMF: data byte without running status');
      const hi = status & 0xF0, ch = (status & 15) + 1;
      const d1 = u8[p++], d2 = hi === 0xC0 || hi === 0xD0 ? 0 : u8[p++];
      ev.push({ t, type: hi, ch, d1, d2 });
    }
    p = end;
    tracks.push(ev);
  }
  return { format, ntrks, division, tracks };
}

/**
 * Import an SMF into state. opts: {channel: 'auto'|'any'|1..16 (auto = 10 if it has notes, else any),
 * startPattern = 0, songSlot = 0, quantize: QUANTIZE index (default none)}.
 * Splits into 1-bar patterns (identical bars share a pattern), builds a song with repeats, tempo steps, time signature.
 * Returns {patterns:[indices], song, tempo, report}.
 */
export function importSMF(buf, opts = {}, maybeOpts) {
  if (buf && buf.patterns) [buf, opts] = [opts, { ...(maybeOpts || {}), state: buf }]; // importSMF(state, buf, opts) also works
  const state = opts.state;
  if (!state) throw new Error('importSMF: opts.state is required');
  const smf = parseSMF(buf);
  const R = (t) => Math.round((t * PPQN) / smf.division);
  const notes = [], tempos = [], sigs = [], markers = new Map();
  let trackName = '', eot = 0;
  for (const tr of smf.tracks) {
    for (const e of tr) {
      if (e.meta === 0x2F) eot = Math.max(eot, R(e.t));
      else if (e.meta === 0x51 && e.data.length >= 3) tempos.push({ t: R(e.t), bpm: 60e6 / ((e.data[0] << 16) | (e.data[1] << 8) | e.data[2]) });
      else if (e.meta === 0x58 && e.data.length >= 2) sigs.push({ t: R(e.t), num: e.data[0], den: 2 ** e.data[1] });
      else if (e.meta === 0x03 && !trackName) trackName = utf8decode(e.data).trim();
      else if (e.meta === 0x7F && e.data.length >= 6 && e.data[0] === 0x7D && e.data[1] === 0x4F && e.data[2] === 0x35)
        markers.set(e.t + ':' + e.data[3], { acc: e.data[4], vel: e.data[5] });
      else if (e.type === 0x90 && e.d2 > 0) notes.push({ raw: e.t, ch: e.ch, note: e.d1, vel: e.d2 });
    }
  }
  let chSel = opts.channel ?? 'auto';
  if (chSel === 'auto') chSel = notes.some((n) => n.ch === 10) ? 10 : 'any';
  const map = noteToVoiceMap(state);
  let otherCh = 0, unmapped = 0;
  const q = opts.quantize !== undefined && opts.quantize !== null ? opts.quantize : null;
  const evs = [];
  for (const n of notes) {
    if (chSel !== 'any' && n.ch !== +chSel) { otherCh++; continue; }
    const v = map.get(n.note);
    if (v === undefined) { unmapped++; continue; }
    const mk = markers.get(n.raw + ':' + n.note);
    let t = R(n.raw);
    if (q !== null) t = Math.max(0, quantizeTick(t, q, 50));
    evs.push({ t, v, vel: mk ? mk.vel : n.vel, acc: mk ? mk.acc : 0 });
  }
  tempos.sort((a, b) => a.t - b.t); sigs.sort((a, b) => a.t - b.t);
  const lastT = Math.max(eot - 1, evs.reduce((m, e) => Math.max(m, e.t), -1));
  // bar grid following time signature changes
  const sigAt = (t) => { let s = { num: 4, den: 4 }; for (const x of sigs) if (x.t <= t) s = x; return s; };
  const toPU = (s) => {
    let beats = s.num, unit = s.den;
    if (unit < 4) { beats = (beats * 4) / unit; unit = 4; } else if (unit > 16) { beats = Math.max(1, Math.round((beats * 16) / unit)); unit = 16; }
    return { beats: clamp(Math.round(beats), 1, 32), unit };
  };
  const bars = [];
  for (let t = 0; t <= lastT || bars.length === 0;) {
    const pu = toPU(sigAt(t)), L = (pu.beats * PPQN * 4) / pu.unit;
    bars.push({ t, len: L, ...pu, ev: [] });
    t += L;
    if (bars.length > 10000) break;
  }
  let bi = 0;
  for (const e of evs.sort((a, b) => a.t - b.t || a.v - b.v)) {
    while (bi + 1 < bars.length && e.t >= bars[bi + 1].t) bi++;
    const b = bars[bi], rel = e.t - b.t;
    if (rel >= b.len) continue;
    const dup = b.ev.find((x) => x.t === rel && x.v === e.v);
    if (dup) { if (e.vel > dup.vel) Object.assign(dup, { vel: e.vel, acc: e.acc }); } else b.ev.push({ t: rel, v: e.v, vel: e.vel, acc: e.acc });
  }
  // dedupe bars -> patterns
  const start = clampInt(opts.startPattern, 0, NUM_PATTERNS - 1, 0), songSlot = clampInt(opts.songSlot, 0, NUM_SONGS - 1, 0);
  const sigMap = new Map(), used = [];
  let dropped = 0;
  for (const b of bars) {
    b.ev.sort((x, y) => x.t - y.t || x.v - y.v);
    const key = b.beats + '/' + b.unit + '|' + b.ev.map((e) => `${e.t},${e.v},${e.vel},${e.acc}`).join(';');
    if (!sigMap.has(key)) {
      if (start + used.length >= NUM_PATTERNS) { b.pn = -1; dropped++; continue; }
      const pn = start + used.length, p = newPattern(pn);
      p.name = 'MID ' + String(used.length + 1).padStart(3, '0');
      p.beats = b.beats; p.unit = b.unit; p.events = b.ev;
      state.patterns[pn] = p; sigMap.set(key, pn); used.push(pn);
    }
    b.pn = sigMap.get(key);
  }
  const tempo0 = clamp(tempos.length && tempos[0].t === 0 ? tempos[0].bpm : 120, 40, 250);
  const steps = [];
  let curBpm = tempo0, ti = 0, i = 0;
  const validBars = bars.filter((b) => b.pn >= 0);
  while (i < validBars.length) {
    const b = validBars[i];
    let bpm = curBpm;
    while (ti < tempos.length && tempos[ti].t < b.t + b.len) { bpm = clamp(tempos[ti].bpm, 40, 250); ti++; }
    if (Math.abs(bpm - curBpm) > 1e-6) { steps.push({ type: 'tempo', bpm: Math.round(bpm * 100) / 100 }); curBpm = bpm; }
    let run = 1;
    while (i + run < validBars.length && validBars[i + run].pn === b.pn && run < 99 &&
      !tempos.some((x) => x.t >= validBars[i + run].t && x.t < validBars[i + run].t + validBars[i + run].len && Math.abs(x.bpm - curBpm) > 1e-6)) run++;
    const at = steps.length;
    steps.push({ type: 'pat', n: b.pn });
    if (run > 1) steps.push({ type: 'rep', from: at, count: run });
    i += run;
  }
  state.songs[songSlot] = { name: (trackName || 'MIDI IMP').slice(0, 8), tempo: Math.round(tempo0 * 100) / 100, steps };
  const s0 = sigs[0] ? `${sigs[0].num}/${sigs[0].den}` : '4/4';
  const report = `SMF format ${smf.format}, ${smf.tracks.length} track(s), ${smf.division} tpq; ` +
    `${evs.length} notes imported (channel ${chSel}), ${unmapped} unmapped, ${otherCh} on other channels; ` +
    `${bars.length} bars -> ${used.length} patterns (${String(start).padStart(2, '0')}..), song ${songSlot}; ` +
    `tempo ${tempo0.toFixed(1)}${tempos.length > 1 ? ` (+${tempos.length - 1} changes)` : ''}, time sig ${s0}` +
    (dropped ? `; ${dropped} bars dropped (out of pattern memory)` : '');
  return { patterns: used, song: songSlot, tempo: tempo0, report };
}
