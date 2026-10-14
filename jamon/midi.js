/* Standard MIDI File read / write (formats 0 and 1). */
const SMF = (() => {
  function readVar(d, p){ let v = 0, b; do { b = d[p.i++]; v = (v << 7) | (b & 127); } while (b & 128); return v; }
  // returns {ppq, tempo (bpm), tracks:[{name, notes:[{t, d, n, v, ch}]}]}; t and d are in ticks
  function read(buf){
    const d = new Uint8Array(buf), p = {i:0}, str = n => { let s = ''; for (let k = 0; k < n; k++) s += String.fromCharCode(d[p.i++]); return s; };
    const u32 = () => (d[p.i++] << 24 | d[p.i++] << 16 | d[p.i++] << 8 | d[p.i++]) >>> 0, u16 = () => d[p.i++] << 8 | d[p.i++];
    if (str(4) !== 'MThd') throw new Error('Not a MIDI file');
    const hl = u32(), fmt = u16(), nt = u16(), div = u16(); p.i += hl - 6;
    if (div & 0x8000) throw new Error('SMPTE-timed MIDI files are not supported');
    const out = {ppq:div, tempo:120, format:fmt, tracks:[]};
    for (let t = 0; t < nt && p.i < d.length; t++){
      const id = str(4), len = u32(), end = p.i + len;
      if (id !== 'MTrk'){ p.i = end; continue; }
      const tr = {name:'', notes:[]}, open = new Map(); let tick = 0, run = 0;
      while (p.i < end){
        tick += readVar(d, p);
        let st = d[p.i];
        if (st & 128) p.i++; else st = run;
        if (st === 0xFF){ const ty = d[p.i++], l = readVar(d, p);
          if (ty === 0x03 && !tr.name) tr.name = str(l); else if (ty === 0x51 && out.tempo === 120 && l === 3){ out.tempo = 60e6 / (d[p.i] << 16 | d[p.i + 1] << 8 | d[p.i + 2]); p.i += 3; } else p.i += l; continue; }
        if (st === 0xF0 || st === 0xF7){ p.i += readVar(d, p); continue; }
        run = st; const hi = st & 0xF0, ch = st & 15;
        if (hi === 0x80 || hi === 0x90){ const n = d[p.i++], v = d[p.i++], k = ch * 128 + n;
          if (hi === 0x90 && v > 0){ if (!open.has(k)) open.set(k, []); open.get(k).push({t:tick, n, v:v / 127, ch}); }
          else { const a = open.get(k); if (a && a.length){ const o = a.shift(); o.d = Math.max(1, tick - o.t); tr.notes.push(o); } } }
        else if (hi === 0xC0 || hi === 0xD0) p.i += 1; else p.i += 2;
      }
      for (const a of open.values()) for (const o of a){ o.d = Math.max(1, tick - o.t); tr.notes.push(o); }
      tr.notes.sort((a, b) => a.t - b.t); out.tracks.push(tr); p.i = end;
    }
    return out;
  }
  const vlq = v => { const b = [v & 127]; while ((v >>= 7)) b.unshift((v & 127) | 128); return b; };
  // tracks: [{name, ch, notes:[{t, d, n, v}]}] in ticks; returns Uint8Array (format 1, track 0 = tempo)
  function write(tracks, bpm, ppq = 96, title = ''){
    const chunk = (id, bytes) => [...id].map(c => c.charCodeAt(0)).concat([bytes.length >>> 24, bytes.length >> 16 & 255, bytes.length >> 8 & 255, bytes.length & 255], bytes);
    const text = (ty, s) => [0xFF, ty, ...vlq(s.length), ...[...s].map(c => c.charCodeAt(0) & 127)];
    const us = Math.round(60e6 / bpm);
    const t0 = [0, ...text(3, title || 'Tempo'), 0, 0xFF, 0x51, 3, us >> 16, us >> 8 & 255, us & 255, 0, 0xFF, 0x58, 4, 4, 2, 24, 8, 0, 0xFF, 0x2F, 0];
    const out = [...chunk('MThd', [0, 1, 0, tracks.length + 1, ppq >> 8, ppq & 255]), ...chunk('MTrk', t0)];
    for (const tr of tracks){
      const ev = [];
      for (const n of tr.notes){ ev.push({t:n.t, k:1, b:[0x90 | tr.ch, n.n & 127, Math.max(1, Math.min(127, Math.round(n.v * 127)))]}); ev.push({t:n.t + n.d, k:0, b:[0x80 | tr.ch, n.n & 127, 0]}); }
      ev.sort((a, b) => a.t - b.t || a.k - b.k);
      const bytes = [0, ...text(3, tr.name)]; let last = 0;
      for (const e of ev){ bytes.push(...vlq(e.t - last), ...e.b); last = e.t; }
      bytes.push(0, 0xFF, 0x2F, 0); out.push(...chunk('MTrk', bytes));
    }
    return new Uint8Array(out);
  }
  return {read, write};
})();
