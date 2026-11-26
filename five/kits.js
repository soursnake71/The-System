// Omega Five: sample kits and the RX5-style waveform library (pure JS, no DOM). ES module for the browser and Node 22.
//
// What it does:
//  * reads ZIP archives (also with data prepended, as in Bitwig .bwpreset files), WAV and AIFF/AIFC files;
//  * names each file by its RX5 waveform (RX5_NAME_MAP, then generic drum aliases, then the pad's MIDI note);
//  * builds a kit = {waveforms, assignments, report, pads, name}, shaped like the hardware's waveform memory:
//      ids 0..23  the 24 INTERNAL waveforms, one per engine voice v (engine.js VOICE_NAMES order). By default these
//                 are our synthesized models; a kit replaces some of them with recordings (assignment v -> id v).
//      ids 24..53 up to 30 CARTRIDGE/user waveforms. The 28 WRC-01 voices keep their cartridge number
//                 (id = 23 + number, so SD3 = 24 ... Ooo = 51); other files take the free slots in order.
//  * applyKit() loads the assigned waveforms into an engine.js Engine (setUserSample) and into seq.js state.
//
// Recordings of the real machine ('-RX5' files, the factory kit in five/kit/) already went through the hardware's
// 12-bit DAC, filters and a mixer: those waveforms are flagged raw:true and the engine should play them without
// re-applying its 12-bit/DAC emulation (applyKit calls engine.setVoicePath(v, 'raw'|'rx') when the engine has it).
//
// Bitwig Drum Machine presets (.bwpreset = a 'BtWg' binary object stream followed by a ZIP with samples/): the
// per-pad data is decoded from the stream: pad key (PADnn / field 0x08e5), pad name, sample path, and every float
// parameter (tag 0x07 + big-endian double): GAIN, OUTPUT, VOLUME, PANORAMA, TRANSPOSE, AMP_* etc. The units of
// GAIN/OUTPUT (linear factor in the files seen, 1.0 = unity) are not documented, so they are reported, not applied.

/* ======================================================================================================== bytes */
const u8 = (b) => b instanceof Uint8Array ? b : ArrayBuffer.isView(b) ? new Uint8Array(b.buffer, b.byteOffset, b.byteLength) : new Uint8Array(b);
const le16 = (d, o) => d[o] | d[o + 1] << 8;
const le32 = (d, o) => (d[o] | d[o + 1] << 8 | d[o + 2] << 16 | d[o + 3] << 24) >>> 0;
const be16 = (d, o) => d[o] << 8 | d[o + 1];
const be32 = (d, o) => (d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) >>> 0;
const ascii = (d, o, n) => { let s = ''; for (let i = 0; i < n && o + i < d.length; i++) s += String.fromCharCode(d[o + i]); return s; };
const utf8 = (d) => { try { return new TextDecoder('utf-8', {fatal: true}).decode(d); } catch { return ascii(d, 0, d.length); } };

let CRC_T = null;
export function crc32(d){
  if (!CRC_T){ CRC_T = new Uint32Array(256); for (let n = 0; n < 256; n++){ let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; CRC_T[n] = c >>> 0; } }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < d.length; i++) c = CRC_T[(c ^ d[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

async function pipeBytes(bytes, stream){
  const out = await new Response(new Blob([bytes]).stream().pipeThrough(stream)).arrayBuffer();
  return new Uint8Array(out);
}

/* ========================================================================================================== ZIP */
/** True when the bytes contain a ZIP end-of-central-directory record (also with data prepended). */
export function isZip(bytes){ return findEOCD(u8(bytes)) >= 0; }

function findEOCD(d){
  const stop = Math.max(0, d.length - 22 - 65535);
  for (let i = d.length - 22; i >= stop; i--) if (d[i] === 0x50 && d[i + 1] === 0x4B && d[i + 2] === 5 && d[i + 3] === 6) return i;
  return -1;
}

/** Lists a ZIP's entries: [{name, method, size, compSize, crc, offset (of the data), prefix (bytes before the zip)}]. */
export function zipEntries(bytes){
  const d = u8(bytes), e = findEOCD(d);
  if (e < 0) throw new Error('zip: no end-of-central-directory record');
  let n = le16(d, e + 10), cdSize = le32(d, e + 12), cdOff = le32(d, e + 16);
  if (n === 0xFFFF || cdOff === 0xFFFFFFFF) throw new Error('zip: ZIP64 archives are not supported');
  // archives with data in front (self-extractors, Bitwig presets) keep offsets relative to the zip's own start
  const shift = Math.max(0, e - cdSize - cdOff), out = [];
  let p = cdOff + shift;
  for (let k = 0; k < n && p + 46 <= d.length; k++){
    if (le32(d, p) !== 0x02014B50) throw new Error('zip: bad central directory entry');
    const flags = le16(d, p + 8), method = le16(d, p + 10), crc = le32(d, p + 16), compSize = le32(d, p + 20), size = le32(d, p + 24);
    const nl = le16(d, p + 28), xl = le16(d, p + 30), cl = le16(d, p + 32), lo = le32(d, p + 42);
    const nb = d.subarray(p + 46, p + 46 + nl), name = flags & 0x800 ? new TextDecoder().decode(nb) : utf8(nb);
    p += 46 + nl + xl + cl;
    let lh = lo + shift;
    if (le32(d, lh) !== 0x04034B50 && le32(d, lo) === 0x04034B50) lh = lo;
    if (le32(d, lh) !== 0x04034B50) throw new Error(`zip: bad local header for ${name}`);
    out.push({name, method, size, compSize, crc, offset: lh + 30 + le16(d, lh + 26) + le16(d, lh + 28), prefix: shift});
  }
  return out;
}

/** Reads every file of a ZIP: [{name, bytes}] (directories skipped). Stored and deflate entries. */
export async function readZip(bytes, opts = {}){
  const d = u8(bytes), out = [];
  for (const en of zipEntries(d)){
    if (en.name.endsWith('/')) continue;
    if (opts.filter && !opts.filter(en.name)) continue;
    const raw = d.subarray(en.offset, en.offset + en.compSize);
    let data;
    if (en.method === 0) data = raw.slice();
    else if (en.method === 8) data = await pipeBytes(raw, new DecompressionStream('deflate-raw'));
    else throw new Error(`zip: ${en.name}: compression method ${en.method} not supported`);
    if (opts.checkCrc !== false && crc32(data) !== en.crc) throw new Error(`zip: ${en.name}: CRC mismatch`);
    out.push({name: en.name, bytes: data});
  }
  return out;
}

/** Writes a ZIP ({deflate:true} compresses with CompressionStream('deflate-raw')). Used by the tests; handy for export. */
export async function makeZip(files, opts = {}){
  const enc = new TextEncoder(), parts = [], cd = [];
  let off = 0;
  for (const f of files){
    const name = enc.encode(f.name), data = u8(f.bytes), crc = crc32(data);
    let comp = data, method = 0;
    if (opts.deflate){ comp = await pipeBytes(data, new CompressionStream('deflate-raw')); method = 8; }
    const h = new Uint8Array(30 + name.length), dv = new DataView(h.buffer);
    dv.setUint32(0, 0x04034B50, true); dv.setUint16(4, 20, true); dv.setUint16(6, 0x800, true); dv.setUint16(8, method, true);
    dv.setUint32(14, crc, true); dv.setUint32(18, comp.length, true); dv.setUint32(22, data.length, true); dv.setUint16(26, name.length, true);
    h.set(name, 30);
    const c = new Uint8Array(46 + name.length), cv = new DataView(c.buffer);
    cv.setUint32(0, 0x02014B50, true); cv.setUint16(4, 20, true); cv.setUint16(6, 20, true); cv.setUint16(8, 0x800, true); cv.setUint16(10, method, true);
    cv.setUint32(16, crc, true); cv.setUint32(20, comp.length, true); cv.setUint32(24, data.length, true); cv.setUint16(28, name.length, true);
    cv.setUint32(42, off, true); c.set(name, 46);
    parts.push(h, comp); cd.push(c); off += h.length + comp.length;
  }
  const cdLen = cd.reduce((a, c) => a + c.length, 0), e = new Uint8Array(22), ev = new DataView(e.buffer);
  ev.setUint32(0, 0x06054B50, true); ev.setUint16(8, files.length, true); ev.setUint16(10, files.length, true);
  ev.setUint32(12, cdLen, true); ev.setUint32(16, off, true);
  const all = [...parts, ...cd, e], out = new Uint8Array(off + cdLen + 22);
  let p = 0; for (const a of all){ out.set(a, p); p += a.length; }
  return out;
}

/* ================================================================================================== WAV / AIFF */
// every decoder returns {sr, channels, bits, format, frames, data: Float32Array (mono = mean of the channels)}
function decodePCM(d, o, nBytes, ch, bits, kind, little){
  const bps = bits >> 3, frame = bps * ch, frames = Math.floor(Math.min(nBytes, d.length - o) / frame), out = new Float32Array(frames);
  const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
  for (let i = 0; i < frames; i++){
    let s = 0;
    for (let c = 0; c < ch; c++){
      const q = o + i * frame + c * bps;
      let x;
      if (kind === 'float') x = bits === 64 ? dv.getFloat64(q, little) : dv.getFloat32(q, little);
      else if (bits === 8) x = little ? (d[q] - 128) / 128 : ((d[q] << 24) >> 24) / 128;     // WAV 8-bit is unsigned, AIFF signed
      else if (bits === 16) x = dv.getInt16(q, little) / 32768;
      else if (bits === 24) x = (little ? (d[q] | d[q + 1] << 8 | d[q + 2] << 16) << 8 : (d[q] << 16 | d[q + 1] << 8 | d[q + 2]) << 8) / 2147483648;
      else if (bits === 32) x = dv.getInt32(q, little) / 2147483648;
      else throw new Error(`unsupported sample size ${bits}`);
      s += x;
    }
    out[i] = s / ch;
  }
  return out;
}

/** WAV (RIFF/RIFX/RF64): PCM 8/16/24/32-bit int, 32/64-bit float, WAVE_FORMAT_EXTENSIBLE, any channels/rate. */
export function parseWav(bytes){
  const d = u8(bytes), id = ascii(d, 0, 4);
  if ((id !== 'RIFF' && id !== 'RIFX' && id !== 'RF64') || ascii(d, 8, 4) !== 'WAVE') throw new Error('wav: not a RIFF/WAVE file');
  const little = id !== 'RIFX', r32 = little ? le32 : be32, r16 = little ? le16 : be16;
  let fmt = null, dataOff = -1, dataLen = 0, ds64 = 0;
  for (let p = 12; p + 8 <= d.length;){
    const cid = ascii(d, p, 4); let n = r32(d, p + 4);
    if (cid === 'ds64') ds64 = le32(d, p + 16) + le32(d, p + 20) * 4294967296;
    if (cid === 'fmt '){
      let tag = r16(d, p + 8);
      const ch = r16(d, p + 10), sr = r32(d, p + 12), bits = r16(d, p + 22);
      if (tag === 0xFFFE && n >= 40) tag = r16(d, p + 32);             // extensible: first 2 bytes of the sub-format GUID
      fmt = {tag, ch, sr, bits};
    } else if (cid === 'data'){
      if (id === 'RF64' && n === 0xFFFFFFFF) n = ds64;
      dataOff = p + 8; dataLen = Math.min(n, d.length - dataOff);
      if (fmt) break;
    }
    p += 8 + n + (n & 1);
  }
  if (!fmt) throw new Error('wav: no fmt chunk');
  if (dataOff < 0) throw new Error('wav: no data chunk');
  if (!(fmt.ch > 0) || !(fmt.sr > 0)) throw new Error('wav: bad channel count or sample rate');
  let kind;
  if (fmt.tag === 1) kind = 'int';
  else if (fmt.tag === 3) kind = 'float';
  else throw new Error(`wav: format tag ${fmt.tag} not supported (PCM and float only)`);
  if (kind === 'float' && fmt.bits !== 32 && fmt.bits !== 64) throw new Error(`wav: ${fmt.bits}-bit float not supported`);
  const data = decodePCM(d, dataOff, dataLen, fmt.ch, fmt.bits, kind, little);
  return {sr: fmt.sr, channels: fmt.ch, bits: fmt.bits, format: kind, frames: data.length, data};
}

function ext80(d, o){                                   // IEEE 754 80-bit extended (AIFF sample rate)
  const e = ((d[o] & 0x7F) << 8 | d[o + 1]) - 16383, hi = be32(d, o + 2), lo = be32(d, o + 6);
  const m = hi * 4294967296 + lo;
  return (d[o] & 0x80 ? -1 : 1) * m * Math.pow(2, e - 63);
}

/** AIFF / AIFC (NONE, twos, sowt, fl32, fl64, raw ). */
export function parseAiff(bytes){
  const d = u8(bytes), form = ascii(d, 8, 4);
  if (ascii(d, 0, 4) !== 'FORM' || (form !== 'AIFF' && form !== 'AIFC')) throw new Error('aiff: not a FORM/AIFF file');
  let comm = null, ssnd = -1, slen = 0;
  for (let p = 12; p + 8 <= d.length;){
    const cid = ascii(d, p, 4), n = be32(d, p + 4);
    if (cid === 'COMM'){
      comm = {ch: be16(d, p + 8), frames: be32(d, p + 10), bits: be16(d, p + 14), sr: ext80(d, p + 16), comp: form === 'AIFC' && n >= 22 ? ascii(d, p + 26, 4) : 'NONE'};
    } else if (cid === 'SSND'){ ssnd = p + 16 + be32(d, p + 8); slen = n - 8 - be32(d, p + 8); }
    p += 8 + n + (n & 1);
  }
  if (!comm || ssnd < 0) throw new Error('aiff: missing COMM or SSND');
  const c = comm.comp.toLowerCase();
  let kind = 'int', little = false, bits = comm.bits;
  if (c === 'sowt') little = true;
  else if (c === 'fl32'){ kind = 'float'; bits = 32; } else if (c === 'fl64'){ kind = 'float'; bits = 64; }
  else if (c === 'raw '){ bits = 8; }
  else if (c !== 'none' && c !== 'twos') throw new Error(`aiff: compression '${comm.comp}' not supported`);
  bits = Math.ceil(bits / 8) * 8;
  let data = decodePCM(d, ssnd, Math.min(slen, comm.frames * comm.ch * (bits >> 3)), comm.ch, bits, kind, little);
  if (c === 'raw ') data = data.map((x) => x >= 0 ? x - 1 : x + 1);   // unsigned 8-bit (decoded as signed above): re-centre
  return {sr: comm.sr, channels: comm.ch, bits, format: kind, frames: data.length, data};
}

/** Any supported audio file by its magic. */
export function parseAudio(bytes){
  const d = u8(bytes), id = ascii(d, 0, 4);
  if (id === 'FORM') return parseAiff(d);
  return parseWav(d);
}
const AUDIO_EXT = /\.(wav|wave|aif|aiff|aifc)$/i;

/* ============================================================================================ RX5 waveform names */
// the 24 internal waveforms in engine voice order (engine.js VOICE_NAMES); names as on the RX5 LCD (6 chars)
export const INTERNAL_WAVEFORMS = [
  'BD 1', 'SD 1', 'RIM 1', 'Tom 1', 'Tom 2', 'Tom 3', 'Tom 4', 'HHclos', 'Edge', 'Crash', 'Tambrn', 'Shaker',
  'BD 2', 'SD 2', 'RIM 2', 'ETom 1', 'ETom 2', 'ETom 3', 'ETom 4', 'HHopen', 'Cup', 'China', 'Claps', 'Cowbel'
].map((name, v) => ({id: v, v, name, kind: 'int'}));
// WRC-01 "RX5 ROM" cartridge, numbers 1..28 (RX5-MIDI-NOTES.md 4.6): id = 23 + number
export const CART_WAVEFORMS = ['SD3', 'BD3', 'CgaHMT', 'CgaHOP', 'CgaLO', 'BgoHI', 'BgoLO', 'TimblH', 'TimblL', 'AgoHI', 'AgoLO',
  'Cuica', 'Cstnt', 'Whstl', 'Timpn', 'GlsCsh', 'Gun', 'FMprc1', 'FMprc2', 'FMprc3', 'EbassH', 'EbassL', 'DXorch', 'DXmrmb',
  'DXclav', 'Hey', 'Wao', 'Ooo'].map((name, i) => ({id: 24 + i, cart: i + 1, name, kind: 'crt'}));
export const LIB_INTERNAL = 24, LIB_SLOTS = 30, LIB_SIZE = LIB_INTERNAL + LIB_SLOTS;

/** Filename stem -> its key in RX5_NAME_MAP: lower case, '-RX5' and every space/punctuation removed. */
export function normName(name){
  let s = String(name).split(/[\\/]/).pop().replace(/\.[a-z0-9]{2,5}$/i, '');
  return s.toLowerCase().replace(/[\s_\-.]*rx[\s_\-]*5(?![0-9])/g, '').replace(/[^a-z0-9]/g, '');
}

const INT_KEYS = {
  0: ['bdrum1', 'bd1', 'bassdrum1'], 12: ['bdrum2', 'bd2', 'bassdrum2'],
  1: ['sdrum1', 'sd1', 'snaredrum1', 'snare1'], 13: ['sdrum2', 'sd2', 'snaredrum2', 'snare2'],
  2: ['rim1', 'rimshot1'], 14: ['rim2', 'rimshot2'],
  3: ['tom1', 'tom1a'], 4: ['tom2', 'tom2a'], 5: ['tom3', 'tom3a'], 6: ['tom4', 'tom4a'],
  15: ['etom1', 'tom1b'], 16: ['etom2', 'tom2b'], 17: ['etom3', 'tom3b'], 18: ['etom4', 'tom4b'],
  7: ['chat', 'hhclos', 'hhclosed', 'hhcls', 'closedhat', 'closedhihat'], 19: ['ohat', 'hhopen', 'hhopn', 'openhat', 'openhihat'],
  8: ['rideedge', 'edge', 'ride'], 20: ['ridebell', 'ridecup', 'cup', 'ridecp'],
  9: ['crash'], 21: ['china', 'chinese', 'chinesecymbal'], 22: ['clap', 'claps', 'handclap'],
  10: ['tamb', 'tambrn', 'tambourine'], 11: ['shaker', 'shakr'], 23: ['cowbell', 'cowbel'],
};
const CART_KEYS = {
  SD3: ['sdrum3', 'sd3', 'snaredrum3'], BD3: ['bdrum3', 'bd3', 'bassdrum3'], Cstnt: ['cstnt', 'castanet', 'castanets'],
  Whstl: ['whstl', 'whistle'], Timpn: ['timpn', 'timpani'], DXmrmb: ['dxmrmb', 'dxmrrmb', 'dxmarimba'], GlsCsh: ['glscsh', 'glasscrash'],
};

/** normalized stem -> {kind:'int', id, v, name} | {kind:'crt', id, cart, name}. */
export const RX5_NAME_MAP = (() => {
  const m = {};
  for (const [v, keys] of Object.entries(INT_KEYS)) for (const k of keys) m[k] = INTERNAL_WAVEFORMS[+v];
  for (const w of CART_WAVEFORMS) for (const k of CART_KEYS[w.name] || [w.name.toLowerCase()]) m[k] = w;
  return Object.freeze(m);
})();

/** Exact RX5 waveform for a file name, or null. */
export function rx5Waveform(name){ return RX5_NAME_MAP[normName(name)] || null; }

const TOMS_A = [3, 4, 5, 6], TOMS_B = [15, 16, 17, 18];
/** Generic drum names ('Kick 02', 'OpenHat', 'snare_rim') -> candidate engine voices in order of preference, or null. */
export function aliasVoices(name){
  const stem = String(name).split(/[\\/]/).pop().replace(/\.[a-z0-9]{2,5}$/i, '').replace(/[\s_\-.]*rx[\s_\-]*5(?![0-9])/gi, '');
  const toks = stem.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/([a-zA-Z])(\d)/g, '$1 $2').replace(/(\d)([a-zA-Z])/g, '$1 $2')
    .toLowerCase().split(/[^a-z0-9]+/).filter(Boolean);
  const j = toks.join(''), has = (re) => toks.some((t) => re.test(t)), num = +(toks.find((t) => /^\d+$/.test(t)) || 0);
  if (has(/^(hh|hat|hats|hihat|hihats|chh|ohh|ch|oh|hhc|hho|hhat)$/) || /hihat|hat/.test(j))
    return has(/^(open|opn|oh|ohh|hho|op|o)$/) || j.includes('open') ? [19, 7] : [7, 19];
  if (j.includes('ride')) return /bell|cup/.test(j) ? [20, 8] : [8, 20];
  if (j.includes('china')) return [21, 9];
  if (/crash|splash|cymbal|cym/.test(j)) return [9, 21];
  if (j.includes('clap')) return [22];
  if (j.includes('cow')) return [23];
  if (j.includes('tamb')) return [10];
  if (/shak|maraca|cabasa/.test(j)) return [11];
  if (/rim|stick/.test(j) || has(/^rs$/)) return num === 2 ? [14, 2] : [2, 14];
  if (j.includes('tom') || has(/^floor$/)){
    const B = has(/^(e|elec|electric|electronic|syn|synth|simmons|etom)$/) || j.startsWith('etom');
    let i = num >= 1 && num <= 4 ? num - 1 : has(/^(hi|high)$/) ? 0 : has(/^(mid|med)$/) ? 1 : has(/^(lo|low)$/) ? 2 : has(/^floor$/) ? 3 : -1;
    const [P, Q] = B ? [TOMS_B, TOMS_A] : [TOMS_A, TOMS_B];
    return i >= 0 ? [P[i], Q[i]] : [...P, ...Q];
  }
  if (/kick|kik|bassdrum/.test(j) || has(/^(bd|bdrum|bassdrum)$/)) return num === 2 ? [12, 0] : [0, 12];
  if (/snare|snr/.test(j) || has(/^(sd|sdrum)$/)) return num === 2 ? [13, 1] : [1, 13];
  return null;
}

// GM drum note -> engine voices (SPEC.md GM map)
const GM_VOICES = {35: [0, 12], 36: [12, 0], 38: [1, 13], 40: [13, 1], 37: [2, 14], 39: [22], 41: [6, 18], 43: [18, 6], 45: [5, 17], 47: [17, 5],
  48: [4, 16], 50: [3, 15], 42: [7], 44: [7], 46: [19], 51: [8], 59: [8], 53: [20], 49: [9], 57: [9], 55: [9], 52: [21], 54: [10], 70: [11], 82: [11], 56: [23]};

/* ============================================================================================ Bitwig .bwpreset */
const isBitwig = (d) => d.length > 4 && ascii(d, 0, 4) === 'BtWg';

/** Decodes the drum pads of a Bitwig Drum Machine preset (the binary part before the zip).
    -> {name, creator, comment, pads:[{note, name, sample, params:{GAIN, OUTPUT, VOLUME, PANORAMA, TRANSPOSE, ...}}]} */
export function parseBitwigPreset(bytes){
  const all = u8(bytes);
  let end = all.length;
  try { const en = zipEntries(all); if (en.length) end = en[0].prefix || Math.min(...en.map((e) => e.offset)); } catch {}
  const d = all.subarray(0, end), res = {name: '', creator: '', comment: '', pads: []};
  const meta = (key) => {
    const k = new TextEncoder().encode(key);
    outer: for (let i = 0; i + k.length + 5 < d.length && i < 8192; i++){
      for (let j = 0; j < k.length; j++) if (d[i + j] !== k[j]) continue outer;
      const p = i + k.length;
      if (d[p] === 8){ const n = be32(d, p + 1); return utf8(d.subarray(p + 5, p + 5 + n)); }
    }
    return '';
  };
  res.creator = meta('creator'); res.comment = meta('comment');
  // the object stream: strings are 0x08 + u32 BE length + bytes; values are <u16 0><u16 field id><u8 type><payload>,
  // type 0x07 = double BE, 0x05 = bool/byte, 0x01 = int8. A field id precedes each string too (0x12de = display name).
  let pad = null;
  for (let i = 4; i + 5 < d.length; i++){
    if (d[i] !== 8 || d[i + 1] || d[i + 2]) continue;
    const n = be32(d, i + 1);
    if (n < 1 || n > 512 || i + 5 + n > d.length) continue;
    let ok = true;
    for (let k = 0; k < n; k++){ const c = d[i + 5 + k]; if (c < 0x20 && c !== 9){ ok = false; break; } }
    if (!ok) continue;
    const s = utf8(d.subarray(i + 5, i + 5 + n)), j = i + 5 + n, field = i >= 2 ? be16(d, i - 2) : -1;
    const m = /^PAD(\d+)$/.exec(s);
    if (m){ pad = {note: +m[1], name: '', sample: '', params: {}}; res.pads.push(pad); i = j - 1; continue; }
    if (field === 0x12DE){ if (pad && !pad.name) pad.name = s; else if (!pad && !res.name) res.name = s; }
    if (pad){
      if (!pad.sample && /^samples\//i.test(s)) pad.sample = s.slice(8);
      else if (/^[A-Z][A-Z0-9_]*$/.test(s) && j + 13 <= d.length && d[j] === 0 && d[j + 1] === 0 && d[j + 4] === 7 && !(s in pad.params))
        pad.params[s] = new DataView(d.buffer, d.byteOffset + j + 5, 8).getFloat64(0, false);
      else if (s === 'MUTE' && d[j] === 0 && d[j + 1] === 0 && d[j + 4] === 5) pad.params.MUTE = d[j + 5];
    }
    i = j - 1;
  }
  // the pad's key byte (field 0x08e5, int8) confirms/overrides the PADnn number
  for (let k = 0; k < res.pads.length; k++){
    const a = d.indexOf(0x50, 0), _ = a;                               // eslint quiet
  }
  return res;
}

/* ===================================================================================================== kits */
/** Cuts silence before the first sample above `thr` (keeps `pre` samples) and after the last one. */
export function trimSilence(x, sr, thr = 0.0005, preMs = 0.5){
  let a = 0, b = x.length;
  while (a < b && Math.abs(x[a]) < thr) a++;
  while (b > a && Math.abs(x[b - 1]) < thr) b--;
  a = Math.max(0, a - Math.round(preMs * sr / 1000));
  return {data: a === 0 && b === x.length ? x : x.slice(a, Math.max(a + 2, b)), lead: a, tail: x.length - b};
}

async function expandInputs(input, warnings){
  const files = [];
  let pads = [], presetName = '';
  const add = async (name, bytes) => {
    const d = u8(bytes);
    if (/\.(zip|bwpreset)$/i.test(name) || (!AUDIO_EXT.test(name) && isZip(d))){
      if (isBitwig(d)){ const p = parseBitwigPreset(d); pads = pads.concat(p.pads); presetName = presetName || p.name; }
      for (const f of await readZip(d)) await add(f.name, f.bytes);
      return;
    }
    const base = name.split(/[\\/]/).pop();
    if (/(^|\/)__MACOSX\//.test(name) || base.startsWith('._') || base.startsWith('.')) return;
    if (AUDIO_EXT.test(name)) files.push({name, bytes: d});
    else if (!/\.(json|txt|md|bwpreset|nfo|png|jpg)$/i.test(name)) warnings.push(`${name}: skipped (not WAV/AIFF)`);
  };
  if (Array.isArray(input)) for (const f of input) await add(f.name || '', f.bytes || f.data || f);
  else await add('kit.zip', input);
  return {files, pads, presetName};
}

/**
 * Builds a kit from audio files or a ZIP / .bwpreset.
 * input: [{name, bytes}] (WAV, AIFF, ZIP or .bwpreset; zips are expanded) | zip bytes (Uint8Array/ArrayBuffer).
 * opts: {trim:true (cut leading/trailing silence), raw:'auto'|true|false (auto = exact RX5 names are hardware recordings),
 *        name, source}
 * -> {name, waveforms:[{id, name, kind:'int'|'crt'|'user', sr, data, raw, source, file, v?, cart?, note?, pad?}],
 *     assignments:[{v, libIndex, id}], pads, report:{rows:[...], warnings:[...]}}
 */
export async function loadKit(input, opts = {}){
  const warnings = [], {files, pads, presetName} = await expandInputs(input, warnings);
  const padOf = (fname) => { const b = fname.split(/[\\/]/).pop().toLowerCase(); return pads.find((p) => p.sample.split(/[\\/]/).pop().toLowerCase() === b) || null; };
  // decode, in pad order when a preset lists them
  const items = [];
  for (const f of files){
    let a;
    try { a = parseAudio(f.bytes); } catch (e){ warnings.push(`${f.name}: ${e.message}`); continue; }
    const pad = padOf(f.name), file = f.name.split(/[\\/]/).pop();
    const t = opts.trim === false ? {data: a.data, lead: 0, tail: 0} : trimSilence(a.data, a.sr);
    items.push({file, path: f.name, audio: a, data: t.data, lead: t.lead, pad, exact: rx5Waveform(file), alias: aliasVoices(file), v: -1, how: 'none'});
  }
  if (pads.length) items.sort((x, y) => (x.pad ? pads.indexOf(x.pad) : 1e9) - (y.pad ? pads.indexOf(y.pad) : 1e9));
  // assignment to the 24 voices: exact RX5 internal names, then generic aliases, then the pad's GM note
  const taken = new Array(LIB_INTERNAL).fill(null), take = (it, v, how) => { taken[v] = it; it.v = v; it.how = how; };
  for (const it of items) if (it.exact && it.exact.kind === 'int' && !taken[it.exact.v]) take(it, it.exact.v, 'exact');
  for (const it of items) if (it.v < 0 && !it.exact && it.alias){ const v = it.alias.find((c) => !taken[c]); if (v !== undefined) take(it, v, 'alias'); }
  for (const it of items) if (it.v < 0 && !it.exact && it.pad && GM_VOICES[it.pad.note]){ const v = GM_VOICES[it.pad.note].find((c) => !taken[c]); if (v !== undefined) take(it, v, 'note'); }
  // library ids: assigned -> id v; WRC-01 names -> their cartridge slot; the rest -> free slots 24..53
  const used = new Set();
  for (const it of items) if (it.v >= 0) it.id = it.v;
  for (const it of items) if (it.v < 0 && it.exact && it.exact.kind === 'crt' && !used.has(it.exact.id)){ it.id = it.exact.id; used.add(it.id); }
  let next = LIB_INTERNAL;
  for (const it of items) if (it.v < 0 && it.id === undefined){
    while (next < LIB_SIZE && (used.has(next) || items.some((o) => o.id === next))) next++;
    if (next >= LIB_SIZE){ warnings.push(`${it.file}: library full (${LIB_SLOTS} cartridge/user slots), dropped`); it.id = -1; continue; }
    it.id = next; used.add(next);
  }
  const waveforms = [], assignments = [], rows = [];
  for (const it of items){
    if (it.id < 0) continue;
    const w = it.v >= 0 ? (it.how === 'exact' ? INTERNAL_WAVEFORMS[it.v] : null) : it.exact && it.exact.id === it.id ? it.exact : null;
    const kind = it.v >= 0 ? 'int' : w ? 'crt' : 'user';
    const raw = opts.raw === true || opts.raw === false ? opts.raw : !!it.exact;
    const name = w ? w.name : it.file.replace(/\.[a-z0-9]+$/i, '').replace(/[\s_\-.]*rx[\s_\-]*5$/i, '').slice(0, 6);
    const wf = {id: it.id, name, kind, sr: it.audio.sr, data: it.data, raw, file: it.file, source: opts.source || presetName || ''};
    if (it.v >= 0) wf.v = it.v;
    if (kind === 'crt') wf.cart = w.cart;
    if (it.pad) wf.pad = {note: it.pad.note, name: it.pad.name, gain: it.pad.params.GAIN, output: it.pad.params.OUTPUT, volume: it.pad.params.VOLUME,
      pan: it.pad.params.PANORAMA, transpose: it.pad.params.TRANSPOSE};
    const libIndex = waveforms.push(wf) - 1;
    if (it.v >= 0) assignments.push({v: it.v, libIndex, id: it.id});
    rows.push({file: it.file, id: it.id, kind, name, v: it.v, how: it.how, raw, sr: it.audio.sr, bits: it.audio.bits, channels: it.audio.channels,
      secs: +(it.data.length / it.audio.sr).toFixed(3), leadMs: +(it.lead * 1000 / it.audio.sr).toFixed(2), note: it.pad ? it.pad.note : null,
      gain: it.pad ? it.pad.params.GAIN : null});
  }
  assignments.sort((a, b) => a.v - b.v);
  const missing = [];
  for (let v = 0; v < LIB_INTERNAL; v++) if (!taken[v]) missing.push(v);
  return {name: opts.name || presetName || '', waveforms, assignments, pads, report: {rows, warnings, unassignedVoices: missing}};
}

/** Text table of a kit (for tools and logs). voiceNames: engine VOICE_NAMES (optional). */
export function formatKitReport(kit, voiceNames){
  const pad = (s, n) => String(s ?? '').padEnd(n), L = [];
  L.push(`${pad('file', 18)}${pad('note', 5)}${pad('gain', 6)}${pad('id', 4)}${pad('kind', 5)}${pad('name', 8)}${pad('voice', 12)}${pad('how', 6)}${pad('raw', 4)}secs`);
  for (const r of kit.report.rows){
    const vn = r.v >= 0 ? `${r.v} ${voiceNames ? voiceNames[r.v] : ''}` : '-';
    L.push(`${pad(r.file, 18)}${pad(r.note ?? '', 5)}${pad(r.gain != null ? r.gain.toFixed(2) : '', 6)}${pad(r.id, 4)}${pad(r.kind, 5)}${pad(r.name, 8)}${pad(vn, 12)}${pad(r.how, 6)}${pad(r.raw ? 'y' : 'n', 4)}${r.secs}`);
  }
  if (kit.report.unassignedVoices.length) L.push(`voices left on the synthesized model: ${kit.report.unassignedVoices.map((v) => voiceNames ? voiceNames[v] : v).join(', ')}`);
  for (const w of kit.report.warnings) L.push(`warning: ${w}`);
  return L.join('\n');
}

/* ============================================================================================ factory kit */
/** Loads the factory kit (five/kit/kit.json + the WAVs it lists). fetchFn(path relative to five/) may return a
    Response, ArrayBuffer or Uint8Array; by default fetch() in the browser, the file system in Node. Every waveform is
    a hardware recording: raw:true. */
export async function loadFactoryKit(fetchFn){
  const get = fetchFn || defaultFetch;
  const bytesOf = async (p) => { const r = await get(p); if (r && typeof r.arrayBuffer === 'function'){ if (r.ok === false) throw new Error(`${p}: HTTP ${r.status}`); return new Uint8Array(await r.arrayBuffer()); } return u8(r); };
  const meta = JSON.parse(new TextDecoder().decode(await bytesOf('kit/kit.json')));
  const files = await Promise.all(meta.files.map(async (f) => ({name: f, bytes: await bytesOf('kit/' + f)})));
  const kit = await loadKit(files, {raw: true, name: meta.name, source: meta.source});
  kit.factory = true;
  return kit;
}
async function defaultFetch(p){
  const url = new URL('./' + p, import.meta.url);
  if (url.protocol === 'file:'){ const fs = await import('node:fs/promises'); return fs.readFile(url); }
  return fetch(url);
}

/* ============================================================================================ applying */
function setPath(engine, v, raw){
  if (!engine) return;
  if (typeof engine.setVoicePath === 'function') engine.setVoicePath(v, raw ? 'raw' : 'rx');
}

/** Puts one waveform on voice v (null = back to the synthesized INT MODEL). */
export function assignWaveform(engine, state, v, wf){
  if (v < 0 || v >= LIB_INTERNAL) return;
  if (engine){ engine.setUserSample(v, wf ? wf.data : null, wf ? wf.sr : 0); setPath(engine, v, !!(wf && wf.raw)); }
  if (state){
    if (!state.samples) state.samples = new Array(LIB_INTERNAL).fill(null);
    state.samples[v] = wf ? {name: wf.name, sr: wf.sr, data: wf.data, raw: !!wf.raw, lib: wf.id} : null;
  }
}

/**
 * mode 'replace' (default): the kit's assigned waveforms go onto their voices (engine + state.samples) and the
 *   whole kit goes into state.library; voices the kit doesn't cover keep what they had.
 * mode 'library': only state.library is filled (choose voices later, as with KEY ASSIGN).
 * state.library = [{id, name, kind, sr, data, raw, source, file}] sorted by id (Float32Array data: the cartridge
 * file writer must serialize it like state.samples).
 */
export function applyKit(engine, state, kit, opts = {}){
  const mode = opts.mode || 'replace';
  if (state) state.library = kit.waveforms.map((w) => ({id: w.id, name: w.name, kind: w.kind, sr: w.sr, data: w.data, raw: w.raw, source: w.source, file: w.file}))
    .sort((a, b) => a.id - b.id);
  const assigned = [];
  if (mode === 'replace') for (const a of kit.assignments){ assignWaveform(engine, state, a.v, kit.waveforms[a.libIndex]); assigned.push(a.v); }
  return {assigned, library: kit.waveforms.length};
}
