/* Omega Solina: sequencer (16 patterns of 1-4 bars, song chain, swing, real-time and step recording) and Standard MIDI
   File import / export. Shared by the page (editing) and the AudioWorklet (playing). Times are ticks at 96 per quarter:
   a 16th step is 24 ticks, a bar of 4/4 is 384. plugins/solina/Seq.h is the same sequencer in C++. */
'use strict';
(function(G){
const PPQ = 96, STEP = 24, BAR = 384, NPAT = 16;

const newPattern = () => ({bars:1, notes:[]});
const newSong = () => ({bpm:120, swing:0, mode:0, cur:0, chain:[0], pats:Array.from({length:NPAT}, newPattern)});
const patLen = p => Math.max(1, Math.min(4, p.bars | 0)) * BAR;

/* ---------------- the player (runs in the worklet / plugin) ---------------- */
class SeqPlayer{
  constructor(){ this.song = newSong(); this.playing = false; this.tick = 0; this.pos = 0; this.pat = 0; this.held = []; this.rec = false; this.quant = 1; this.recHeld = new Map(); this.clockAcc = 0; }
  setSong(s){ this.song = s; if (!this.playing) this.pat = s.mode ? (s.chain[0] | 0) : s.cur; }
  start(fromTick){ const s = this.song; this.playing = true; this.pos = 0; this.pat = s.mode ? (s.chain[0] | 0) : s.cur; this.tick = fromTick || 0; this.clockAcc = 0;
    if (s.mode && this.tick){ let t = this.tick; this.pos = 0; while (s.chain.length && t >= patLen(s.pats[s.chain[this.pos] | 0])){ t -= patLen(s.pats[s.chain[this.pos] | 0]); this.pos = (this.pos + 1) % s.chain.length; } this.pat = s.chain[this.pos] | 0; this.tick = t; } }
  stop(emit){ this.playing = false; for (const h of this.held) emit(0, 0x80, h.n, 0); this.held = []; this.recHeld.clear(); }
  swingT(t){ return (Math.floor(t / STEP) & 1) ? t + this.song.swing * 12 : t; }
  // advance by n samples; emit(sampleOffset, status, note, vel) for notes, emit(offset, 0xF8) for MIDI clock
  run(n, fs, emit){
    if (!this.playing) return;
    const s = this.song, tps = s.bpm * PPQ / (60 * fs);
    let done = 0;
    while (done < n){
      const p = s.pats[this.pat], len = patLen(p), t0 = this.tick, left = n - done;
      let t1 = t0 + left * tps, wrap = false;
      if (t1 >= len){ t1 = len; wrap = true; }
      const off = t => done + Math.max(0, Math.min(left - 1, Math.floor((t - t0) / tps)));
      // MIDI clock: 24 per quarter = every 4 ticks
      let c = Math.ceil(this.clockAcc / 4) * 4; if (c < t0) c = Math.ceil(t0 / 4) * 4;
      for (; c < t1; c += 4) emit(off(c), 0xF8, 0, 0);
      this.clockAcc = t1;
      // note offs
      for (let i = 0; i < this.held.length; i++){ const h = this.held[i]; if (h.off < t1){ emit(off(h.off), 0x80, h.n, 0); this.held.splice(i--, 1); } }
      // note ons
      for (const nt of p.notes){ const ts = this.swingT(nt.t); if (ts >= t0 && ts < t1 && nt.t < len){
        for (let i = 0; i < this.held.length; i++) if (this.held[i].n === nt.n){ emit(off(ts), 0x80, nt.n, 0); this.held.splice(i--, 1); }
        emit(off(ts), 0x90, nt.n, Math.max(1, Math.min(127, nt.v | 0)));
        let e = ts + Math.max(1, nt.d); this.held.push({n:nt.n, off:e}); } }
      done += wrap ? Math.max(1, Math.ceil((t1 - t0) / tps)) : left;
      if (wrap){
        for (const h of this.held) h.off -= len;
        this.tick = 0; this.clockAcc = 0;
        if (s.mode && s.chain.length){ this.pos = (this.pos + 1) % s.chain.length; this.pat = s.chain[this.pos] | 0; } else this.pat = s.cur;
      } else this.tick = t1;
    }
  }
  // real-time recording: returns a note {t, n, d, v} when a key is released (or null)
  recOn(n, v){ if (!this.playing || !this.rec) return; this.recHeld.set(n, {t:this.tick, v, pat:this.pat}); }
  recOff(n){
    const h = this.recHeld.get(n); if (!h) return null; this.recHeld.delete(n);
    const len = patLen(this.song.pats[h.pat]);
    let t = h.t, d = this.pat === h.pat ? this.tick - h.t : len - h.t + this.tick;
    if (this.quant){ t = Math.round(t / STEP) * STEP; d = Math.max(STEP, Math.round(d / STEP) * STEP); if (t >= len) t -= len; }
    return {pat:h.pat, note:{t:Math.max(0, Math.round(t)), n, d:Math.max(1, Math.round(Math.min(d, BAR * 4))), v:Math.round(v * 127)}};
  }
}

/* ---------------- Standard MIDI Files ---------------- */
function parseMidi(buf){
  const b = new Uint8Array(buf); let p = 0;
  const u32 = () => (b[p++] << 24 | b[p++] << 16 | b[p++] << 8 | b[p++]) >>> 0, u16 = () => b[p++] << 8 | b[p++];
  const str = n => { let s = ''; for (let i = 0; i < n; i++) s += String.fromCharCode(b[p++]); return s; };
  if (str(4) !== 'MThd') throw new Error('Not a MIDI file');
  const hl = u32(), fmt = u16(), ntr = u16(), div = u16(); p += hl - 6;
  if (div & 0x8000) throw new Error('SMPTE-timed MIDI files are not supported');
  const notes = [], tempos = [], names = [];
  for (let tr = 0; tr < ntr && p < b.length; tr++){
    while (p < b.length && str(4) !== 'MTrk'){ const l = u32(); p += l; }
    const len = u32(), end = p + len; let t = 0, run = 0; const open = new Map();
    while (p < end){
      let d = 0, c; do { c = b[p++]; d = d << 7 | c & 127; } while (c & 128); t += d;
      let st = b[p];
      if (st & 128) p++; else st = run;
      if (st === 0xFF){ const ty = b[p++]; let l = 0; do { c = b[p++]; l = l << 7 | c & 127; } while (c & 128);
        if (ty === 0x51 && l === 3) tempos.push({t, us:b[p] << 16 | b[p + 1] << 8 | b[p + 2]});
        if (ty === 0x03){ let s = ''; for (let i = 0; i < l; i++) s += String.fromCharCode(b[p + i]); names[tr] = s; }
        p += l; continue; }
      if (st === 0xF0 || st === 0xF7){ let l = 0; do { c = b[p++]; l = l << 7 | c & 127; } while (c & 128); p += l; continue; }
      run = st; const hi = st & 0xF0, ch = st & 15;
      const a = b[p++], v = (hi === 0xC0 || hi === 0xD0) ? 0 : b[p++];
      if (hi === 0x90 && v > 0){ const k = ch * 128 + a; if (open.has(k)){ const o = open.get(k); o.d = t - o.t; } const nn = {t, n:a, v, d:0, ch, tr}; open.set(k, nn); notes.push(nn); }
      else if (hi === 0x80 || hi === 0x90){ const k = ch * 128 + a, o = open.get(k); if (o){ o.d = t - o.t; open.delete(k); } }
    }
    for (const o of open.values()) o.d = Math.max(1, t - o.t);
    p = end;
  }
  notes.sort((x, y) => x.t - y.t || x.n - y.n);
  return {format:fmt, ppq:div, tempo:tempos.length ? 60e6 / tempos[0].us : 120, notes, names};
}

// a parsed file into the song: rescaled to 96 per quarter, split into 4-bar patterns (up to 16), chained in order
function importToSong(m, opt){
  opt = opt || {}; const song = newSong(); song.bpm = Math.round(Math.max(30, Math.min(300, m.tempo)) * 10) / 10; song.mode = 1;
  const sc = PPQ / m.ppq, notes = m.notes.filter(x => !(opt.skipDrums && x.ch === 9) && (opt.channel == null || opt.channel < 0 || x.ch === opt.channel));
  if (!notes.length) return {song, count:0, bars:0};
  const first = opt.keepStart ? 0 : Math.floor(notes[0].t * sc / BAR) * BAR;
  let last = 0; for (const x of notes) last = Math.max(last, x.t * sc - first + 1);
  const bars = Math.min(NPAT * 4, Math.ceil(last / BAR)), npat = Math.ceil(bars / 4);
  song.chain = [];
  for (let i = 0; i < npat; i++){ song.pats[i].bars = Math.min(4, bars - i * 4); song.chain.push(i); }
  let count = 0;
  for (const x of notes){
    let t = x.t * sc - first, d = Math.max(1, x.d * sc);
    if (opt.quantize){ t = Math.round(t / STEP) * STEP; d = Math.max(STEP, Math.round(d / STEP) * STEP); }
    t = Math.round(t); d = Math.round(d);
    const pi = Math.floor(t / (BAR * 4)); if (pi >= npat || pi >= NPAT) continue;
    song.pats[pi].notes.push({t:t - pi * BAR * 4, n:x.n, d:Math.min(d, BAR * 4 * NPAT), v:x.v}); count++;
  }
  return {song, count, bars};
}

function vlq(a, n){ const s = [n & 127]; while (n >>= 7) s.unshift(n & 127 | 128); a.push(...s); }
// the song (chain) or the current pattern as a type-1 file: tempo track + one note track
function exportMidi(song, which, ch){
  ch = ch | 0; const ev = []; let base = 0;
  const list = which === 'pattern' ? [song.cur] : (song.mode ? song.chain : [song.cur]);
  for (const pi of list){ const p = song.pats[pi | 0], len = patLen(p);
    for (const nt of p.notes){ if (nt.t >= len) continue; const t = base + Math.round(nt.t + ((Math.floor(nt.t / STEP) & 1) ? song.swing * 12 : 0));
      ev.push({t, s:0x90 | ch, a:nt.n, b:Math.max(1, Math.min(127, nt.v | 0))}, {t:t + Math.max(1, nt.d), s:0x80 | ch, a:nt.n, b:0}); }
    base += len; }
  ev.sort((x, y) => x.t - y.t || ((x.s & 0xF0) === 0x80 ? -1 : 1));
  const tr0 = [], us = Math.round(60e6 / song.bpm);
  vlq(tr0, 0); tr0.push(0xFF, 0x03, 12, ...Array.from('Omega Solina', c => c.charCodeAt(0)));
  vlq(tr0, 0); tr0.push(0xFF, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255);
  vlq(tr0, 0); tr0.push(0xFF, 0x58, 4, 4, 2, 24, 8);
  vlq(tr0, base); tr0.push(0xFF, 0x2F, 0);
  const tr1 = []; let last = 0;
  vlq(tr1, 0); tr1.push(0xFF, 0x03, 7, ...Array.from('Strings', c => c.charCodeAt(0)));
  for (const e of ev){ vlq(tr1, e.t - last); last = e.t; tr1.push(e.s, e.a, e.b); }
  vlq(tr1, Math.max(0, base - last)); tr1.push(0xFF, 0x2F, 0);
  const out = [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, 2, PPQ >> 8, PPQ & 255];
  for (const t of [tr0, tr1]) out.push(0x4D, 0x54, 0x72, 0x6B, t.length >>> 24 & 255, t.length >> 16 & 255, t.length >> 8 & 255, t.length & 255, ...t);
  return new Uint8Array(out);
}

/* ---------------- random patterns: chord progressions voiced for a string ensemble ---------------- */
const SCALES = {Major:[0, 2, 4, 5, 7, 9, 11], Minor:[0, 2, 3, 5, 7, 8, 10], Dorian:[0, 2, 3, 5, 7, 9, 10]};
function randomPattern(rnd, opt){
  rnd = rnd || Math.random; opt = opt || {};
  const root = opt.root != null ? opt.root : Math.floor(rnd() * 12), names = Object.keys(SCALES), sc = SCALES[opt.scale || names[Math.floor(rnd() * names.length)]];
  const progs = [[0, 5, 3, 4], [0, 3, 4, 0], [0, 5, 1, 4], [0, 6, 5, 4], [5, 3, 0, 4], [0, 4, 5, 3], [0, 2, 3, 4]];
  const prog = progs[Math.floor(rnd() * progs.length)], style = Math.floor(rnd() * 4), bars = 4, notes = [];
  const deg = (d, o) => { const i = ((d % 7) + 7) % 7; return 48 + root + sc[i] + 12 * (Math.floor(d / 7) + o); };
  for (let b = 0; b < bars; b++){
    const d = prog[b % prog.length], chord = [deg(d, 0), deg(d + 2, 0), deg(d + 4, 0)];
    if (rnd() < .5) chord.push(deg(d + 6, 0));
    const t0 = b * BAR, bass = deg(d, -1);
    if (style === 0){ for (const n of chord) notes.push({t:t0, n:n + 12, d:BAR - 6, v:100}); notes.push({t:t0, n:bass - 12 < 36 ? bass : bass - 12, d:BAR - 6, v:100}); }
    else if (style === 1){ for (const h of [0, 192]) for (const n of chord) notes.push({t:t0 + h, n:n + 12, d:186, v:96}); notes.push({t:t0, n:bass, d:BAR - 6, v:100}); }
    else if (style === 2){ const ar = chord.concat([chord[0] + 12]); for (let s = 0; s < 16; s++) notes.push({t:t0 + s * STEP, n:ar[s % ar.length] + 12, d:STEP - 2, v:s % 4 ? 80 : 110}); notes.push({t:t0, n:bass, d:BAR - 6, v:100}); }
    else { for (const n of chord) notes.push({t:t0, n:n + 12, d:BAR + (b < bars - 1 ? -6 : -6), v:100}); for (let s = 0; s < 4; s++) notes.push({t:t0 + s * 96 + (s % 2 ? 48 : 0), n:bass, d:40, v:110}); }
  }
  return {bars, notes};
}

const api = {PPQ, STEP, BAR, NPAT, newSong, newPattern, patLen, SeqPlayer, parseMidi, importToSong, exportMidi, randomPattern, SCALES};
G.SolinaSeq = api;
if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
