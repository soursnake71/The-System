/* Omega 808: Standard MIDI File import to the drum tracks, and export of the rhythms.
   Import: format 0 or 1, every track merged. Drum notes are mapped to the 808 tracks with the General MIDI drum map
   (the same notes as live MIDI in). Velocity at or over the accent threshold writes the accent track. Notes snap to the
   nearest 16th step, and each 4/4 bar becomes one basic rhythm (up to 12), with the file's first tempo.
   Export: the basic rhythms that have notes, one bar each in order, on channel 10, as a format 0 file. */
(function (g) {
  'use strict';
  // GM drum note -> 808 track. Congas play on the tom tracks, claves on RS, maracas on CP (the 808's switches pick them)
  const NOTE_TRACK = {35:'BD', 36:'BD', 37:'RS', 38:'SD', 40:'SD', 39:'CP', 41:'LT', 43:'LT', 45:'MT', 47:'MT', 48:'HT', 50:'HT',
    64:'LT', 63:'MT', 62:'HT', 75:'RS', 70:'CP', 56:'CB', 49:'CY', 57:'CY', 51:'CY', 46:'OH', 42:'CH', 44:'CH', 76:'TRIG'};
  // export: one note per track
  const TRACK_NOTE = {BD:36, SD:38, LT:41, MT:45, HT:48, RS:37, CP:39, CB:56, CY:49, OH:46, CH:42, TRIG:76};
  const TRACKS = ['AC','BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH','TRIG'];
  const SPB = [4, 8, 6, 3];   // steps per beat for pre-scale 0..3 (see seq.js)

  function vlq(d, p) { let v = 0, b; do { b = d[p.i++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80 && p.i < d.length); return v; }

  /** parse an SMF into {ppq, tempo (bpm or null), notes:[{tick, ch (1-16), note, vel}]} */
  function parse(buf) {
    const d = new Uint8Array(buf), str = (o, n) => String.fromCharCode.apply(null, d.subarray(o, o + n));
    const u32 = o => (d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) >>> 0, u16 = o => d[o] << 8 | d[o + 1];
    if (d.length < 14 || str(0, 4) !== 'MThd') throw new Error('not a MIDI file');
    const ntrk = u16(10), div = u16(12);
    if (div & 0x8000) throw new Error('SMPTE-timed MIDI files are not supported');
    let o = 8 + u32(4), tempo = null;
    const notes = [];
    for (let t = 0; t < ntrk && o + 8 <= d.length; t++) {
      if (str(o, 4) !== 'MTrk') break;
      const end = Math.min(d.length, o + 8 + u32(o + 4)), p = {i: o + 8};
      let tick = 0, run = 0;
      while (p.i < end) {
        tick += vlq(d, p);
        let st = d[p.i];
        if (st & 0x80) p.i++; else st = run;
        if (st === 0xFF) {
          const type = d[p.i++], len = vlq(d, p);
          if (type === 0x51 && len === 3 && tempo == null) tempo = 60000000 / (d[p.i] << 16 | d[p.i + 1] << 8 | d[p.i + 2]);
          if (type === 0x2F) { p.i = end; break; }
          p.i += len;
        } else if (st === 0xF0 || st === 0xF7) { p.i += vlq(d, p); }
        else {
          run = st;
          const ty = st & 0xf0, n1 = d[p.i++], n2 = (ty === 0xC0 || ty === 0xD0) ? 0 : d[p.i++];
          if (ty === 0x90 && n2 > 0) notes.push({tick, ch: (st & 0x0f) + 1, note: n1, vel: n2});
        }
      }
      o = end;
    }
    notes.sort((a, b) => a.tick - b.tick);
    return {ppq: div, tempo, notes};
  }

  /** the drum channel to use: 10 if it has notes, else the channel with most mapped drum notes */
  function drumChannel(notes) {
    const c = {};
    for (const n of notes) if (NOTE_TRACK[n.note]) c[n.ch] = (c[n.ch] || 0) + 1;
    if (c[10]) return 10;
    let best = 0, bc = 0; for (const k in c) if (c[k] > bc) { bc = c[k]; best = +k; }
    return best;
  }

  /** turn a parsed file into up to 12 one-bar patterns. Returns {patterns, tempo, bars, used, skipped, ch, dropped} */
  function toPatterns(mf, opt) {
    opt = opt || {};
    const accThr = opt.accThr || 100, ch = opt.ch || drumChannel(mf.notes), maxBars = opt.maxBars || 12;
    const stepT = mf.ppq / 4, barT = mf.ppq * 4;
    const bars = [];
    let used = 0, skipped = 0, last = -1;
    for (const n of mf.notes) {
      if (n.ch !== ch) continue;
      const tr = NOTE_TRACK[n.note];
      if (!tr) { skipped++; continue; }
      const step = Math.round(n.tick / stepT), bar = Math.floor(step / 16), s = step % 16;
      last = Math.max(last, bar);
      if (bar >= maxBars) continue;
      if (!bars[bar]) { bars[bar] = {}; for (const t of TRACKS) bars[bar][t] = new Array(16).fill('.'); }
      bars[bar][tr][s] = 'x';
      if (n.vel >= accThr) bars[bar].AC[s] = 'x';
      used++;
    }
    const total = last + 1, patterns = [];
    for (let i = 0; i < Math.min(total, maxBars); i++) {
      const steps = {};
      for (const t of TRACKS) steps[t] = bars[i] ? bars[i][t].join('') : '.'.repeat(16);
      patterns.push({scale: 0, len: 16, parts: 1, steps});
    }
    return {patterns, tempo: mf.tempo, bars: total, used, skipped, ch, dropped: Math.max(0, total - maxBars)};
  }

  /** a format-0 SMF of the given patterns, one after another, at bpm. Accented steps at velocity 127, others 90 */
  function build(patterns, bpm) {
    const PPQ = 96, ev = [];
    let t0 = 0;
    for (const p of patterns) {
      const n = (p.len || 16) * (p.parts || 1), st = PPQ / SPB[p.scale || 0];
      for (let s = 0; s < n; s++) {
        const acc = p.steps.AC && p.steps.AC[s] === 'x';
        for (const tr in TRACK_NOTE) if (p.steps[tr] && p.steps[tr][s] === 'x') {
          const t = Math.round(t0 + s * st);
          ev.push([t, 0x99, TRACK_NOTE[tr], acc ? 127 : 90], [t + Math.round(st / 2), 0x89, TRACK_NOTE[tr], 0]);
        }
      }
      t0 += n * st;
    }
    ev.sort((a, b) => a[0] - b[0] || (a[1] === 0x89 ? -1 : 1));
    const body = [], put = v => { const b = [v & 0x7f]; while (v >>= 7) b.unshift((v & 0x7f) | 0x80); body.push(...b); };
    const us = Math.round(60000000 / (bpm || 120));
    body.push(0, 0xFF, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255, 0, 0xFF, 0x58, 4, 4, 2, 24, 8);
    let last = 0;
    for (const e of ev) { put(e[0] - last); last = e[0]; body.push(e[1], e[2], e[3]); }
    put(Math.max(0, Math.round(t0) - last)); body.push(0xFF, 0x2F, 0);
    const out = new Uint8Array(14 + 8 + body.length), w = (o, a) => out.set(a, o);
    w(0, [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0, PPQ]);
    w(14, [0x4D, 0x54, 0x72, 0x6B, body.length >>> 24 & 255, body.length >> 16 & 255, body.length >> 8 & 255, body.length & 255]);
    w(22, body);
    return out;
  }

  const api = {parse, toPatterns, build, drumChannel, NOTE_TRACK, TRACK_NOTE};
  g.EightMidiFile = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
