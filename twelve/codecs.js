/* Omega Twelve file formats: WAV (16/24-bit, 32-bit float), AIFF (16/24-bit) and FLAC (16/24-bit) writers,
   and WAV/AIFF/AIFC readers for the files the browser's own decoder won't open. MP3 export uses lamejs (worker.js);
   everything else the browser can decode (MP3, AAC/M4A, OGG, Opus, FLAC, WebM, video soundtracks) goes through it. */
(function (root) {
'use strict';

// float -> integer with TPDF dither
function toInts(ch, bits, seed){
  const q = Math.pow(2, bits - 1), out = new Int32Array(ch.length);
  let s = seed || 1;
  const r = () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
  for (let i = 0; i < ch.length; i++){
    const d = bits <= 16 ? r() - r() : 0, v = Math.round(ch[i] * (q - 1) + d);
    out[i] = v < -q ? -q : v > q - 1 ? q - 1 : v;
  }
  return out;
}

function encodeWav(chs, sr, fmt){
  const float = fmt === 'f32', bits = float ? 32 : fmt === 24 ? 24 : 16, nc = chs.length, n = chs[0].length, bps = bits / 8;
  const data = n * nc * bps, buf = new ArrayBuffer(44 + data), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + data, true); str(8, 'WAVE'); str(12, 'fmt '); v.setUint32(16, 16, true);
  v.setUint16(20, float ? 3 : 1, true); v.setUint16(22, nc, true); v.setUint32(24, sr, true); v.setUint32(28, sr * nc * bps, true);
  v.setUint16(32, nc * bps, true); v.setUint16(34, bits, true); str(36, 'data'); v.setUint32(40, data, true);
  const ints = float ? null : chs.map((c, k) => toInts(c, bits, k + 1));
  let o = 44;
  for (let i = 0; i < n; i++) for (let c = 0; c < nc; c++){
    if (float){ v.setFloat32(o, chs[c][i], true); o += 4; }
    else if (bits === 16){ v.setInt16(o, ints[c][i], true); o += 2; }
    else { const x = ints[c][i]; v.setUint8(o, x & 255); v.setUint8(o + 1, (x >> 8) & 255); v.setUint8(o + 2, (x >> 16) & 255); o += 3; }
  }
  return new Uint8Array(buf);
}

function encodeAiff(chs, sr, bits){
  bits = bits === 24 ? 24 : 16;
  const nc = chs.length, n = chs[0].length, bps = bits / 8, data = n * nc * bps, buf = new ArrayBuffer(54 + data), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'FORM'); v.setUint32(4, 46 + data); str(8, 'AIFF'); str(12, 'COMM'); v.setUint32(16, 18);
  v.setUint16(20, nc); v.setUint32(22, n); v.setUint16(26, bits);
  // sample rate as an 80-bit extended float
  const e = Math.floor(Math.log2(sr)); v.setUint16(28, 16383 + e); v.setUint32(30, Math.round(sr * Math.pow(2, 31 - e)) >>> 0); v.setUint32(34, 0);
  str(38, 'SSND'); v.setUint32(42, 8 + data); v.setUint32(46, 0); v.setUint32(50, 0);
  const ints = chs.map((c, k) => toInts(c, bits, k + 1));
  let o = 54;
  for (let i = 0; i < n; i++) for (let c = 0; c < nc; c++){
    const x = ints[c][i];
    if (bits === 16){ v.setInt16(o, x); o += 2; } else { v.setUint8(o, (x >> 16) & 255); v.setUint8(o + 1, (x >> 8) & 255); v.setUint8(o + 2, x & 255); o += 3; }
  }
  return new Uint8Array(buf);
}

/* ---- FLAC: fixed predictors (order 0-4), Rice-coded residuals, one partition per subframe ---- */
const CRC8 = new Uint8Array(256), CRC16 = new Uint16Array(256);
for (let i = 0; i < 256; i++){
  let c = i; for (let k = 0; k < 8; k++) c = c & 0x80 ? ((c << 1) ^ 0x07) & 255 : (c << 1) & 255; CRC8[i] = c;
  let d = i << 8; for (let k = 0; k < 8; k++) d = d & 0x8000 ? ((d << 1) ^ 0x8005) & 0xffff : (d << 1) & 0xffff; CRC16[i] = d;
}
class Bits {
  constructor(){ this.b = new Uint8Array(1 << 16); this.n = 0; this.acc = 0; this.k = 0; }
  grow(){ const nb = new Uint8Array(this.b.length * 2); nb.set(this.b); this.b = nb; }
  put(byte){ if (this.n >= this.b.length) this.grow(); this.b[this.n++] = byte; }
  w(v, n){ // up to 24 bits at a time
    while (n > 24){ this.w(Math.floor(v / Math.pow(2, n - 24)) & 0xffffff, 24); v = v % Math.pow(2, n - 24); n -= 24; }
    for (let i = n - 1; i >= 0; i--){ this.acc = (this.acc << 1) | ((v >>> i) & 1); if (++this.k === 8){ this.put(this.acc); this.acc = 0; this.k = 0; } }
  }
  zeros(n){ while (n > 0){ const m = Math.min(n, 24); this.w(0, m); n -= m; } }
  align(){ if (this.k) this.w(0, 8 - this.k); }
}
function residuals(x, order){
  const n = x.length, r = new Int32Array(n - order);
  for (let i = order; i < n; i++){
    let p = 0;
    if (order === 1) p = x[i - 1];
    else if (order === 2) p = 2 * x[i - 1] - x[i - 2];
    else if (order === 3) p = 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3];
    else if (order === 4) p = 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4];
    r[i - order] = x[i] - p;
  }
  return r;
}
function riceCost(r, k){ let s = 0; for (let i = 0; i < r.length; i++){ const u = r[i] >= 0 ? 2 * r[i] : -2 * r[i] - 1; s += Math.floor(u / (1 << k)); } return s + r.length * (k + 1); }
function bestRice(r){
  let m = 0; for (let i = 0; i < r.length; i++) m += Math.abs(r[i]); m = m / Math.max(1, r.length);
  const k0 = Math.max(0, Math.min(30, Math.floor(Math.log2(m + 1)))); let best = k0, bc = Infinity;
  for (let k = Math.max(0, k0 - 1); k <= Math.min(30, k0 + 1); k++){ const c = riceCost(r, k); if (c < bc){ bc = c; best = k; } }
  return {k:best, cost:bc};
}
function encodeFlac(chs, sr, bits){
  bits = bits === 24 ? 24 : 16;
  const nc = chs.length, n = chs[0].length, BS = 4096, ints = chs.map((c, k) => toInts(c, bits, k + 1)), out = new Bits();
  // fLaC + STREAMINFO (MD5 left zero, which the format allows)
  for (const ch of 'fLaC') out.w(ch.charCodeAt(0), 8);
  out.w(1, 1); out.w(0, 7); out.w(34, 24);
  out.w(BS, 16); out.w(BS, 16); out.w(0, 24); out.w(0, 24);
  out.w(sr, 20); out.w(nc - 1, 3); out.w(bits - 1, 5); out.w(Math.floor(n / 4294967296), 4); out.w(n >>> 0, 32);
  for (let i = 0; i < 16; i++) out.w(0, 8);
  for (let f = 0, s = 0; s < n; f++, s += BS){
    const len = Math.min(BS, n - s), start = out.n, fr = new Bits();
    fr.w(0xFFF8, 16); fr.w(7, 4); fr.w(0, 4); fr.w(nc - 1, 4); fr.w(bits === 16 ? 4 : 6, 3); fr.w(0, 1);
    // frame number, UTF-8 style
    if (f < 0x80) fr.w(f, 8);
    else { let bytes = f < 0x800 ? 2 : f < 0x10000 ? 3 : f < 0x200000 ? 4 : 5; const head = [0, 0, 0xC0, 0xE0, 0xF0, 0xF8][bytes]; fr.w(head | (f >>> (6 * (bytes - 1))), 8); for (let b = bytes - 2; b >= 0; b--) fr.w(0x80 | ((f >>> (6 * b)) & 0x3f), 8); }
    fr.w(len - 1, 16);
    let c8 = 0; for (let i = 0; i < fr.n; i++) c8 = CRC8[c8 ^ fr.b[i]];
    fr.w(c8, 8);
    for (let c = 0; c < nc; c++){
      const x = ints[c].subarray(s, s + len);
      let best = null;
      for (let o = 0; o <= Math.min(4, len - 1); o++){ const r = residuals(x, o), rc = bestRice(r); const cost = rc.cost + o * bits; if (!best || cost < best.cost) best = {o, r, k:rc.k, cost}; }
      fr.w(0, 1); fr.w(8 | best.o, 6); fr.w(0, 1);
      for (let i = 0; i < best.o; i++) fr.w(x[i] & (bits === 32 ? -1 : (1 << bits) - 1), bits);
      fr.w(1, 2); fr.w(0, 4); fr.w(best.k, 5);
      const k = best.k, r = best.r;
      for (let i = 0; i < r.length; i++){
        const u = r[i] >= 0 ? 2 * r[i] : -2 * r[i] - 1, q = Math.floor(u / (1 << k));
        fr.zeros(q); fr.w(1, 1); if (k) fr.w(u & ((1 << k) - 1), k);
      }
    }
    fr.align();
    let c16 = 0; for (let i = 0; i < fr.n; i++) c16 = ((c16 << 8) & 0xffff) ^ CRC16[(c16 >> 8) ^ fr.b[i]];
    fr.w(c16, 16);
    for (let i = 0; i < fr.n; i++) out.put(fr.b[i]);
    void start;
  }
  return out.b.slice(0, out.n);
}

/* ---- readers ---- */
function parseWav(ab){
  const v = new DataView(ab), tag = o => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
  if (ab.byteLength < 12 || tag(0) !== 'RIFF' && tag(0) !== 'RF64' || tag(8) !== 'WAVE') return null;
  let o = 12, fmt = null, nc = 0, sr = 0, bits = 0, data = null, dlen = 0;
  while (o + 8 <= ab.byteLength){
    const id = tag(o), len = v.getUint32(o + 4, true);
    if (id === 'fmt '){ fmt = v.getUint16(o + 8, true); nc = v.getUint16(o + 10, true); sr = v.getUint32(o + 12, true); bits = v.getUint16(o + 22, true); if (fmt === 0xFFFE) fmt = v.getUint16(o + 32, true); }
    else if (id === 'data'){ data = o + 8; dlen = Math.min(len === 0xffffffff ? ab.byteLength - data : len, ab.byteLength - data); }
    o += 8 + len + (len & 1);
    if (data !== null && fmt !== null) break;
  }
  if (!fmt || data === null) return null;
  return readPcm(v, data, dlen, nc, sr, bits, fmt === 3, true);
}
function parseAiff(ab){
  const v = new DataView(ab), tag = o => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
  if (ab.byteLength < 12 || tag(0) !== 'FORM' || (tag(8) !== 'AIFF' && tag(8) !== 'AIFC')) return null;
  let o = 12, nc = 0, n = 0, bits = 0, sr = 0, data = null, comp = 'NONE';
  while (o + 8 <= ab.byteLength){
    const id = tag(o), len = v.getUint32(o + 4);
    if (id === 'COMM'){
      nc = v.getUint16(o + 8); n = v.getUint32(o + 10); bits = v.getUint16(o + 14);
      const e = (v.getUint16(o + 16) & 0x7fff) - 16383; sr = v.getUint32(o + 18) * Math.pow(2, e - 31) + v.getUint32(o + 22) * Math.pow(2, e - 63);
      if (tag(8) === 'AIFC' && len >= 22) comp = tag(o + 26);
    } else if (id === 'SSND') data = o + 16 + v.getUint32(o + 8);
    o += 8 + len + (len & 1);
  }
  if (data === null || !nc) return null;
  const little = comp === 'sowt', float = comp === 'fl32' || comp === 'FL32';
  if (!['NONE', 'sowt', 'fl32', 'FL32', 'twos'].includes(comp)) return null;
  return readPcm(v, data, Math.min(n * nc * Math.ceil(bits / 8), ab.byteLength - data), nc, Math.round(sr), float ? 32 : bits, float, little, true);
}
function readPcm(v, off, len, nc, sr, bits, float, little, signed8){
  const bps = Math.ceil(bits / 8), n = Math.floor(len / (bps * nc)), chs = [];
  for (let c = 0; c < nc; c++) chs.push(new Float32Array(n));
  for (let i = 0; i < n; i++) for (let c = 0; c < nc; c++){
    const o = off + (i * nc + c) * bps; let x;
    if (float) x = bps === 8 ? v.getFloat64(o, little) : v.getFloat32(o, little);
    else if (bps === 1) x = signed8 ? v.getInt8(o) / 128 : (v.getUint8(o) - 128) / 128;
    else if (bps === 2) x = v.getInt16(o, little) / 32768;
    else if (bps === 3){ const b0 = v.getUint8(o), b1 = v.getUint8(o + 1), b2 = v.getUint8(o + 2); let y = little ? b0 | (b1 << 8) | (b2 << 16) : (b0 << 16) | (b1 << 8) | b2; if (y & 0x800000) y -= 0x1000000; x = y / 8388608; }
    else x = v.getInt32(o, little) / 2147483648;
    chs[c][i] = x;
  }
  return {chs, sr};
}

const api = {encodeWav, encodeAiff, encodeFlac, parseWav, parseAiff, toInts};
if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.TWC = api;
})(typeof self !== 'undefined' ? self : this);
