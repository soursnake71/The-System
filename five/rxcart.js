// Omega Five — RX5-style Waveform Data Cartridge bank I/O (pure JS, no DOM). ES module.
//
// Reads/writes the 128 KB (131072-byte) bank images of RX5 waveform cartridges, as dumped from a WRC ROM the user
// owns or as used by RX5USB-style flash cartridges. Layout per RX5-MIDI-NOTES.md §6 (from gligli's RX5USB
// rx5classes.pas, verified against the factory bank images):
//   0x000 00 00 00 00 | 0x004 bank ID | 0x005 voice count (<= 30) | 0x006 32-byte voice entries | ...zero...
//   0x3FE checksum, big-endian = 16-bit sum of bytes 0x000..0x3FD | 0x400.. PCM, every voice on a 256-byte boundary
// Voice entry (32 bytes):
//   0 octave, 1 note: pitch = b0*120 + b1 - 360 in 10-cent units (0 = 25 kHz)          [mapping UNCERTAIN]
//   2 format 0 = 8-bit, 1 = 12-bit | 3 flags: 0x40 = loop OFF, bit0 = start bit 16 | 4 start bits 15..8
//   5-7 loop start, 8-10 loop end, 11-13 sample end: 17-bit absolute addresses (b&1)<<16 | b<<8 | b
//   14 AR, 15 D1R, 16 RR, 17 D2R, 18 D1L (RX5USB's reading; 16/18 could be swapped — UNCERTAIN)
//   19 gate time code (factory always 92; coding unknown — UNCERTAIN), 20 bend rate, 21 bend range (supposed),
//   22 unknown, 23 level 0..31, 24 "channel" 0..11 (UNCERTAIN), 25 = 0, 26-31 name (6 ASCII, space padded)
// PCM: 8-bit signed; 12-bit = one leading 0x00 "latency" byte, then 2 samples per 3 bytes B1 B2 B3:
//   s1 = B2<<8 | (B1 & 0x0F)<<4, s2 = B3<<8 | (B1 >> 4)<<4 (16-bit left-justified, as RX5USB packs them).
//
// Voices map onto engine.js voice params (RANGES.voice): pitch cents, level, ar, d1r, d1l (0..60, native),
// d2r, rr, gate ms (0 = off), bendRate, bendRange cents, reverse (no cart field: 0), loop, bits.
// Every parsed voice keeps raw.entry / raw.pcm; buildBank reuses raw bytes for unchanged fields/PCM, so
// parse -> build reproduces a bank byte-for-byte.

export const BANK_SIZE = 131072;
export const HEADER_SIZE = 1024;
export const ENTRY_SIZE = 32;
export const MAX_VOICES = 30;
export const BASE_SR = 25000;
export const BASE_PITCH = 360;

const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
const s8 = (b) => (b > 127 ? b - 256 : b);
const addr17 = (e, o) => ((e[o] & 1) << 16) | (e[o + 1] << 8) | e[o + 2];

/** 16-bit byte sum of the header (bytes 0x000..0x3FD), as stored big-endian at 0x3FE. */
export function bankChecksum(bank) {
  let s = 0;
  for (let i = 0; i < HEADER_SIZE - 2; i++) s += bank[i];
  return s & 0xFFFF;
}

// gate code <-> ms. UNCERTAIN: the factory value 92 is taken as "off" (sound plays out, engine gate 0);
// other codes are spread linearly over the manual's 0.1..6.5 s.
const GATE_OFF_CODE = 92;
export const gateFromCode = (c) => (c >= GATE_OFF_CODE ? 0 : Math.round(clamp(100 + (c * 6400) / (GATE_OFF_CODE - 1), 100, 6500)));
export const codeFromGate = (ms) => (!(ms > 0) || ms >= 6500 ? GATE_OFF_CODE : clamp(Math.round(((ms - 100) * (GATE_OFF_CODE - 1)) / 6400), 0, GATE_OFF_CODE - 1));

/** Decode a 32-byte entry into engine params + extra fields. */
function decodeEntry(e) {
  const units = e[0] * 120 + e[1] - BASE_PITCH;
  return {
    name: String.fromCharCode(...e.subarray(26, 32)).replace(/[\0 ]+$/, ''),
    bits: e[2] === 1 ? 12 : 8,
    params: {
      pitch: units * 10, level: clamp(e[23], 0, 31), ar: e[14], d1r: e[15], rr: e[16], d2r: e[17], d1l: e[18],
      gate: gateFromCode(e[19]), bendRate: e[20], bendRange: s8(e[21]) * 100, reverse: 0, loop: e[3] & 0x40 ? 0 : 1,
      bits: e[2] === 1 ? 12 : 8,
    },
    out: e[24], unk: e[22],
    start: ((e[3] & 1) << 16) | (e[4] << 8), loopStartAddr: addr17(e, 5), loopEndAddr: addr17(e, 8), endAddr: addr17(e, 11),
  };
}

/** Raw PCM bytes -> Float32Array (-1..1). */
export function decodePCM(raw, bits) {
  if (bits === 8) {
    const out = new Float32Array(raw.length);
    for (let i = 0; i < raw.length; i++) out[i] = s8(raw[i]) / 128;
    return out;
  }
  const n = Math.max(0, Math.floor(((raw.length - 1) * 2) / 3)), out = new Float32Array(n);
  for (let i = 0, p = 1; i < n; i += 2, p += 3) {
    const b1 = raw[p], b2 = raw[p + 1], b3 = raw[p + 2] ?? 0;
    out[i] = (((b2 << 8) | ((b1 & 0x0F) << 4)) << 16 >> 16) / 32768;
    if (i + 1 < n) out[i + 1] = (((b3 << 8) | ((b1 >> 4) << 4)) << 16 >> 16) / 32768;
  }
  return out;
}
/** Float32Array -> raw PCM bytes (8-bit signed, or 0x00 latency byte + 12-bit triples). */
export function encodePCM(pcm, bits) {
  const q = (x) => Math.round(clamp(x, -1, 1) * 32767);
  if (bits === 8) {
    const out = new Uint8Array(pcm.length);
    for (let i = 0; i < pcm.length; i++) out[i] = clamp(Math.round(clamp(pcm[i], -1, 1) * 128), -128, 127) & 255;
    return out;
  }
  const out = new Uint8Array(1 + Math.ceil(pcm.length / 2) * 3);
  for (let i = 0, p = 1; i < pcm.length; i += 2, p += 3) {
    // round to the 12-bit grid (16-bit left-justified, low nibble 0)
    const w1 = clamp(Math.round(q(pcm[i]) / 16), -2048, 2047) * 16 & 0xFFFF;
    const w2 = i + 1 < pcm.length ? clamp(Math.round(q(pcm[i + 1]) / 16), -2048, 2047) * 16 & 0xFFFF : 0;
    out[p] = (((w2 >> 4) & 0x0F) << 4) | ((w1 >> 4) & 0x0F);
    out[p + 1] = w1 >> 8; out[p + 2] = w2 >> 8;
  }
  return out;
}
// byte offset (relative to the voice start) <-> sample index
const byteToSample = (off, bits) => (bits === 8 ? off : Math.max(0, Math.floor(((off - 1) * 2) / 3)));
const sampleToByte = (k, bits) => (bits === 8 ? k : 1 + Math.floor(k / 2) * 3 + (k & 1 ? 2 : 0));

function parseOne(bank, index) {
  const count = Math.min(bank[5], MAX_VOICES), voices = [];
  for (let i = 0; i < count; i++) {
    const at = 6 + i * ENTRY_SIZE, entry = bank.slice(at, at + ENTRY_SIZE), d = decodeEntry(entry);
    const end = Math.min(d.endAddr, BANK_SIZE), start = Math.min(d.start, end);
    const raw = bank.slice(start, end), pcm = decodePCM(raw, d.bits);
    voices.push({
      name: d.name, sr: BASE_SR, bits: d.bits, params: d.params, pcm,
      loopStart: byteToSample(Math.max(0, d.loopStartAddr - start), d.bits), loopEnd: byteToSample(Math.max(0, d.loopEndAddr - start), d.bits),
      out: d.out, bank: index, slot: i, raw: { entry, pcm: raw, start },
    });
  }
  const stored = (bank[HEADER_SIZE - 2] << 8) | bank[HEADER_SIZE - 1], computed = bankChecksum(bank);
  return { id: bank[4], count, voices, checksumOk: stored === computed, checksum: { stored, computed }, image: bank.slice() };
}

/**
 * Parse a bank image. 131072 bytes = one bank; 262144 (or up to 4 x 128 KB, e.g. an RX5USB 4 Mbit flash image)
 * = several banks. Returns {voices (all banks), checksumOk (all banks), banks: [{id, count, voices, checksumOk,
 * checksum: {stored, computed}}]}. Throws if the size is not a multiple of 128 KB or the header is not 00 00 00 00.
 */
export function parseBank(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (!u8.length || u8.length % BANK_SIZE || u8.length > 4 * BANK_SIZE) throw new Error('not an RX5 bank image (size must be 128 KB, 256 KB or 512 KB)');
  const banks = [];
  for (let k = 0; k < u8.length / BANK_SIZE; k++) {
    const b = u8.subarray(k * BANK_SIZE, (k + 1) * BANK_SIZE);
    if (b[0] | b[1] | b[2] | b[3]) throw new Error('not an RX5 bank image (bank ' + k + ' header)');
    banks.push(parseOne(b, k));
  }
  return { voices: banks.flatMap((b) => b.voices), checksumOk: banks.every((b) => b.checksumOk), banks, id: banks[0].id, count: banks[0].count, checksum: banks[0].checksum };
}

function resample(pcm, from, to) {
  if (!from || Math.abs(from - to) < 1e-6) return pcm;
  const n = Math.max(1, Math.round((pcm.length * to) / from)), out = new Float32Array(n), r = from / to;
  for (let i = 0; i < n; i++) {
    const x = i * r, j = Math.floor(x), f = x - j;
    out[i] = (pcm[j] ?? 0) * (1 - f) + (pcm[j + 1] ?? pcm[j] ?? 0) * f;
  }
  return out;
}

/** Raw PCM bytes a voice will occupy (before 256-byte alignment). */
export function voiceBytes(v) {
  const bits = v.bits === 8 ? 8 : 12;
  if (v.raw && v.raw.pcm && !v.pcmDirty && (v.raw.entry[2] === 1 ? 12 : 8) === bits) return v.raw.pcm.length;
  const n = v.sr && v.sr !== BASE_SR ? Math.round((v.pcm.length * BASE_SR) / v.sr) : v.pcm.length;
  return bits === 8 ? n : 1 + Math.ceil(n / 2) * 3;
}
/** Total bank bytes needed (header + 256-aligned PCM); must be <= BANK_SIZE. */
export function bankSize(voices) {
  return voices.reduce((n, v) => n + ((voiceBytes(v) + 255) & ~255), HEADER_SIZE);
}

/**
 * Build one 131072-byte bank. voices: [{name (6 chars), bits 8|12 (default 12), pcm: Float32Array at sr
 * (default 25000 = the cart's pitch-0 rate; other rates are resampled linearly), params: engine voice params,
 * loopStart/loopEnd (samples; default loop the last 30% like engine.js), out, raw?}] (max 30).
 * opts.bankId: byte at 0x004; default = RX5USB's generated ID (low ^ high byte of the header checksum).
 * opts.fill: a 131072-byte image used as background for padding (e.g. parsed.banks[k].image); default zeros.
 * Unchanged parsed voices keep their original addresses when they don't overlap (else voices are packed in order).
 * Set voice.pcmDirty = true after editing a parsed voice's pcm so its raw bytes are not reused.
 * parseBank -> buildBank(voices, {bankId: bank.id, fill: bank.image}) reproduces the image byte-for-byte.
 */
export function buildBank(voices, opts = {}) {
  if (voices.length > MAX_VOICES) throw new Error('too many voices for one bank (max ' + MAX_VOICES + ')');
  const need = bankSize(voices);
  if (need > BANK_SIZE) throw new Error(`voices need ${need} bytes, a bank holds ${BANK_SIZE}`);
  const bank = opts.fill && opts.fill.length >= BANK_SIZE ? Uint8Array.from(opts.fill.subarray(0, BANK_SIZE)) : new Uint8Array(BANK_SIZE);
  bank.fill(0, 0, HEADER_SIZE);
  bank[5] = voices.length;
  const isRaw = (v) => v.raw && v.raw.pcm && !v.pcmDirty && (v.raw.entry[2] === 1 ? 12 : 8) === (v.bits === 8 ? 8 : 12);
  // keep the original layout when every voice is unchanged raw data at a valid, non-overlapping address
  const iv = voices.map((v) => (isRaw(v) && v.raw.start >= HEADER_SIZE && !(v.raw.start & 255) ? [v.raw.start, v.raw.start + v.raw.pcm.length] : null));
  const keepLayout = iv.every((x) => x && x[1] <= BANK_SIZE) &&
    iv.slice().sort((a, b) => a[0] - b[0]).every((x, i, a) => i === 0 || a[i - 1][1] <= x[0]);
  let at = HEADER_SIZE;
  voices.forEach((v, i) => {
    const bits = v.bits === 8 ? 8 : 12;
    const rawOk = isRaw(v);
    if (keepLayout) at = v.raw.start;
    const src = rawOk ? null : resample(v.pcm, v.sr || BASE_SR, BASE_SR);
    const data = rawOk ? v.raw.pcm : encodePCM(src, bits);
    const nSamp = rawOk ? byteToSample(data.length, bits) : src.length;
    bank.set(data, at);
    const old = v.raw && v.raw.entry ? decodeEntry(v.raw.entry) : null, e = v.raw && v.raw.entry ? v.raw.entry.slice() : new Uint8Array(ENTRY_SIZE);
    const p = { ...(old ? old.params : {}), ...(v.params || {}) };
    const same = (k) => old && old.params[k] === p[k];
    if (!same('pitch')) { const u = clamp(Math.round((+p.pitch || 0) / 10), -BASE_PITCH, 240) + BASE_PITCH; e[0] = Math.floor(u / 120); e[1] = u % 120; }
    e[2] = bits === 12 ? 1 : 0;
    const set = (k, idx, enc) => { if (!same(k)) e[idx] = enc(p[k]); };
    const r99 = (lo, d) => (x) => clamp(Math.round(Number.isFinite(+x) ? +x : d), lo, 99);
    set('ar', 14, r99(1, 99)); set('d1r', 15, r99(0, 0)); set('rr', 16, r99(1, 60)); set('d2r', 17, r99(0, 0));
    set('d1l', 18, (x) => clamp(Math.round(Number.isFinite(+x) ? +x : 60), 0, 60));
    set('gate', 19, codeFromGate);
    set('bendRate', 20, (x) => clamp(Math.round(+x || 0), 0, 60));
    set('bendRange', 21, (x) => clamp(Math.round((+x || 0) / 100), -60, 60) & 255);
    set('level', 23, (x) => clamp(Math.round(Number.isFinite(+x) ? +x : 27), 0, 31));
    if (!old) e[22] = 99; // unknown byte: RX5USB writes 99
    if (v.out !== undefined || !old) e[24] = clamp(v.out | 0, 0, 11);
    e[25] = 0;
    if (!old || v.name !== old.name) {
      const nm = String(v.name || 'VOICE').slice(0, 6).padEnd(6, ' ');
      for (let k = 0; k < 6; k++) e[26 + k] = nm.charCodeAt(k) & 0x7F;
    }
    // addresses
    const loopOn = p.loop ? 1 : 0;
    let ls = v.loopStart, le = v.loopEnd;
    if (ls === undefined || le === undefined || !rawOk && v.raw) { ls = loopOn ? Math.floor(nSamp * 0.7) : 0; le = nSamp; }
    const end = at + data.length;
    const lsA = clamp(at + sampleToByte(clamp(ls, 0, nSamp), bits), at, end), leA = clamp(at + sampleToByte(clamp(le, 0, nSamp), bits), at, end);
    const keepAddr = rawOk && old && old.loopStartAddr - old.start === byteToSampleInv(ls, bits, old, 'loopStartAddr') && old.loopEndAddr - old.start === byteToSampleInv(le, bits, old, 'loopEndAddr');
    // the high address byte carries other bits in factory banks (e.g. 0x10 = bank ID?) — kept from the raw entry
    const put17 = (o, a) => { e[o] = (old ? e[o] & 0xFE : 0) | ((a >> 16) & 1); e[o + 1] = (a >> 8) & 255; e[o + 2] = a & 255; };
    e[3] = (loopOn ? 0 : 0x40) | ((at >> 16) & 1) | (old ? e[3] & 0xBE : 0); e[4] = (at >> 8) & 255;
    put17(5, keepAddr ? at + old.loopStartAddr - old.start : lsA);
    put17(8, keepAddr ? at + old.loopEndAddr - old.start : leA);
    put17(11, end);
    bank.set(e, 6 + i * ENTRY_SIZE);
    if (!keepLayout) bank.fill(0, end, Math.min(BANK_SIZE, (end + 255) & ~255));
    at = (end + 255) & ~255;
  });
  let cs = bankChecksum(bank);
  if (opts.bankId === undefined) { bank[4] = (cs & 255) ^ (cs >> 8); cs = bankChecksum(bank); } else bank[4] = opts.bankId & 255;
  cs = bankChecksum(bank);
  bank[HEADER_SIZE - 2] = cs >> 8; bank[HEADER_SIZE - 1] = cs & 255;
  return bank;
}
// relative byte offset stored in the old entry, if the sample index still decodes to it (keeps odd addresses exact)
function byteToSampleInv(k, bits, old, key) {
  const rel = old[key] - old.start;
  return byteToSample(rel, bits) === k ? rel : NaN;
}
