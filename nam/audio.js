// audio.js — the test riff, WAV and ZIP writers, and the simple cab.

// A DI guitar riff made with plucked-string synthesis (Karplus-Strong with a pick and a body
// resonance), at about the level a guitar gives an interface: peaks near -8 dBFS.
// Palm-muted power chords, an open chord ring-out and a short lead line, about 9 seconds.
export function makeRiff(sr = 48000) {
  const bpm = 112, beat = 60 / bpm, len = Math.round(sr * beat * 16 + sr * 1.5);
  const out = new Float32Array(len);
  let seed = 12345; const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296) * 2 - 1;
  const hz = midi => 440 * 2 ** ((midi - 69) / 12);
  function pluck(t, midi, dur, vel, mute = false, pickPos = 0.18) {
    const f = hz(midi), N = Math.max(2, Math.round(sr / f));
    const buf = new Float32Array(N);
    for (let i = 0; i < N; i++) buf[i] = rnd() * vel;
    // pick position comb: removes harmonics at multiples of 1/pickPos
    const d = Math.max(1, Math.round(N * pickPos));
    for (let i = N - 1; i >= d; i--) buf[i] -= buf[i - d];
    const start = Math.round(t * sr), n = Math.round(dur * sr);
    const damp = mute ? 0.86 : 0.996, decayEnd = mute ? 0.12 : dur;
    let idx = 0, prev = 0;
    for (let i = 0; i < n && start + i < len; i++) {
      const cur = buf[idx];
      const nxt = buf[(idx + 1) % N];
      const v = damp * 0.5 * (cur + nxt);
      buf[idx] = v; idx = (idx + 1) % N;
      // release: fade over the last 15 ms, or quickly for muted notes
      const tt = i / sr;
      const env = mute ? Math.exp(-tt / decayEnd) : (i > n - sr * 0.015 ? (n - i) / (sr * 0.015) : 1);
      const y = cur * env;
      out[start + i] += y - 0.3 * prev; prev = y; // slight pickup tilt
    }
  }
  const chord = (t, root, dur, vel, mute) => { pluck(t, root, dur, vel, mute); pluck(t + 0.004, root + 7, dur, vel * 0.9, mute); pluck(t + 0.008, root + 12, dur, vel * 0.8, mute); };
  let t = 0.05;
  // bar 1-2: chugs on E5 with accents on G5 and A5
  const pattern = [[40, 1, 0], [40, 1, 0], [40, 1, 0], [43, 2, 1], [40, 1, 0], [40, 1, 0], [45, 2, 1], [40, 1, 0], [40, 1, 0], [38, 2, 1], [40, 1, 0], [40, 1, 0], [43, 1, 1], [45, 1, 1], [40, 2, 1]];
  for (const [root, eighths, open] of pattern) { chord(t, root, beat / 2 * eighths, open ? 0.42 : 0.36, !open); t += beat / 2 * eighths; }
  t = beat * 8 + 0.05;
  // bar 3: open A5 then E5 ring out
  chord(t, 45, beat * 2, 0.45, false); chord(t + beat * 2, 40, beat * 2, 0.45, false);
  t = beat * 12 + 0.05;
  // bar 4: lead line in E minor pentatonic
  const lead = [[64, 0.5], [67, 0.5], [69, 0.5], [71, 0.5], [74, 0.75], [71, 0.25], [69, 0.5], [67, 0.5], [64, 1.5]];
  for (const [m, b] of lead) { pluck(t, m, beat * b + 0.4, 0.5, false, 0.12); t += beat * b; }
  // body resonance (a gentle low-mid bump) and level
  let y1 = 0, y2 = 0; const w = 2 * Math.PI * 110 / sr, rr = 0.995, a1 = 2 * rr * Math.cos(w), a2 = -rr * rr;
  for (let i = 0; i < len; i++) { const y = out[i] + 0.04 * (a1 * y1 + a2 * y2); y2 = y1; y1 = y; out[i] = y; }
  let pk = 0; for (const v of out) pk = Math.max(pk, Math.abs(v));
  const g = 0.4 / (pk || 1);
  for (let i = 0; i < len; i++) out[i] *= g;
  return out;
}

export function encodeWav(data, sr) {
  const n = data.length, buf = new ArrayBuffer(44 + n * 2), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + n * 2, true); str(8, 'WAVE'); str(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true); v.setUint32(24, sr, true);
  v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); str(36, 'data'); v.setUint32(40, n * 2, true);
  for (let i = 0; i < n; i++) { const s = Math.max(-1, Math.min(1, data[i])); v.setInt16(44 + i * 2, s < 0 ? s * 0x8000 : s * 0x7fff, true); }
  return new Blob([buf], { type: 'audio/wav' });
}

// ZIP (stored, no compression). files: [{name, data: Uint8Array}]
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(d) { let c = 0xffffffff; for (let i = 0; i < d.length; i++) c = CRC[(c ^ d[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
export function makeZip(files) {
  const enc = new TextEncoder(), parts = [], central = [];
  let off = 0;
  for (const f of files) {
    const name = enc.encode(f.name), data = f.data, crc = crc32(data);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
    h.setUint32(14, crc, true); h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, name.length, true);
    parts.push(new Uint8Array(h.buffer), name, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true);
    c.setUint32(16, crc, true); c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, name.length, true); c.setUint32(42, off, true);
    central.push(new Uint8Array(c.buffer), name);
    off += 30 + name.length + data.length;
  }
  const size = central.reduce((s, p) => s + p.length, 0);
  const e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true); e.setUint32(12, size, true); e.setUint32(16, off, true);
  return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: 'application/zip' });
}

// A plain guitar-cab curve for amp-only captures: low cut, low-mid thump, upper-mid bite, steep top roll-off
export function simpleCab(ctx) {
  const mk = (type, f, q, g = 0) => { const b = ctx.createBiquadFilter(); b.type = type; b.frequency.value = f; b.Q.value = q; b.gain.value = g; return b; };
  const chain = [mk('highpass', 75, 0.7), mk('peaking', 110, 1.2, 3), mk('peaking', 400, 1, -2.5), mk('peaking', 2400, 1.4, 3), mk('lowpass', 5200, 0.8), mk('lowpass', 6500, 0.6)];
  for (let i = 0; i < chain.length - 1; i++) chain[i].connect(chain[i + 1]);
  return { input: chain[0], output: chain.at(-1) };
}
