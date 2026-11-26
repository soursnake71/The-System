// Omega Five — SysEx (pure JS, no DOM). ES module. Ported to plugins/five/FiveSysex.h.
//
// 1. Generic framing: SysexAssembler (incremental F0..F7 assembly from split MIDI packets), .syx parse/write.
// 2. RX5 hardware bulk dumps (Yamaha F0 43 ...): the RX5's byte layout is NOT public (see RX5-MIDI-NOTES.md §0),
//    so this is a faithful *librarian*: dumps from a real RX5 (MIDI Job #10) are stored verbatim in
//    state.rxBulk and retransmitted byte-identical (TRANSMIT BULK), loaded/saved as .syx. Yamaha's usual checksum
//    is checked where the message follows a common Yamaha layout ("checksum error!" like the hardware).
// 3. Omega Five's own messages, F0 7D 4F 35 <cmd> ... (7D = non-commercial/educational ID):
//      (no cmd: legacy)  F0 7D 4F 35 <pack7 JSON cart> F7  = seq.sysexDump() — still accepted, same as cmd 01
//      01 full cart      F0 7D 4F 35 01 <pack7 JSON cart> cs F7
//      02 one pattern    F0 7D 4F 35 02 nn <pack7 JSON pattern> cs F7          nn = 0..99
//      03 one song       F0 7D 4F 35 03 nn <pack7 JSON song> cs F7             nn = 0..19
//      04 voice params   F0 7D 4F 35 04 <pack7 JSON {voice:[24], channel:[12]}> cs F7
//      05 fx             F0 7D 4F 35 05 <pack7 JSON fx> cs F7
//      06 param change   F0 7D 4F 35 06 tt ii <name ASCII> 00 v4 v3 v2 v1 v0 cs F7
//                        tt: 0 voice (ii 0..23), 1 channel (ii 0..11), 2 fx.rev, 3 fx.dly, 4 global (state[name]),
//                        5 key of the current set-up (ii 0..23), 6 midi (state.midi[name]);
//                        value = round(x*1000) as 35-bit two's complement, 7 bits per byte, MSB first
//      07 dump request   F0 7D 4F 35 07 ww nn cs F7    ww = 01 full, 02 pattern nn, 03 song nn, 04 voices, 05 fx, 08 identity
//    Omega messages and hardware dumps are refused over MIDI while the device number is OFF (0), like the RX5.
//      08 identity       F0 7D 4F 35 08 00 01 00 00 <ASCII "OMEGA FIVE"> cs F7   (reply to 07 08)
//    cs = Yamaha-style checksum (two's complement of the 7-bit sum) of every byte from <cmd> up to cs.
// 4. Universal identity: F0 7E <dev|7F> 06 01 F7 -> F0 7E <dev> 06 02 7D 4F 35 00 05 00 01 00 00 F7.

import {
  SYSEX_HEADER, pack7, unpack7, cartToJSON, cartFromJSON, normalizeState, b64encode, b64decode, sysexDeviceOk,
  NUM_PATTERNS, NUM_SONGS, NUM_VOICES, NUM_CHANNELS,
} from './seq.js';

export const OMEGA = { FULL: 1, PATTERN: 2, SONG: 3, VOICES: 4, FX: 5, PARAM: 6, REQUEST: 7, IDENTITY: 8 };
export const PARAM_TARGETS = ['voice', 'channel', 'rev', 'dly', 'global', 'key', 'midi'];
export const IDENTITY_REQUEST = [0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7];
// manufacturer 7D, family 4F 35, member 00 05, version 00 01 00 00
export const IDENTITY_ID = [0x7D, 0x4F, 0x35, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00];
export const IDENTITY_NAME = 'OMEGA FIVE';
const OMEGA_VERSION = [0x00, 0x01, 0x00, 0x00];

const toU8 = (b) => (b instanceof Uint8Array ? b : Uint8Array.from(b || []));
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

// ---------------------------------------------------------------- framing

/**
 * Incremental SysEx assembler. push(bytes) returns the complete F0..F7 messages (Uint8Array) finished by these
 * bytes; a message may span any number of pushes. Realtime bytes F8..FF inside (or outside) a SysEx are not part
 * of it: they are passed to opts.onRealtime(byte) and collected in `this.realtime` (bytes seen by the last push).
 * A new F0 aborts a partial message (counted in `aborted`), as does any other status byte (80..F6); bytes outside
 * a SysEx are ignored (they belong to other messages). opts.maxBytes caps one message (default 16 MB).
 */
export class SysexAssembler {
  constructor(opts = {}) {
    this.onRealtime = opts.onRealtime || null;
    this.maxBytes = opts.maxBytes || 16 * 1024 * 1024;
    this.buf = null; this.len = 0; this.aborted = 0; this.realtime = [];
  }
  reset() { this.buf = null; this.len = 0; }
  get pending() { return this.buf ? this.len : 0; }
  _put(b) {
    if (this.len >= this.buf.length) {
      if (this.len >= this.maxBytes) { this.buf = null; this.len = 0; this.aborted++; return; }
      const n = new Uint8Array(Math.min(this.maxBytes, this.buf.length * 2)); n.set(this.buf); this.buf = n;
    }
    this.buf[this.len++] = b;
  }
  push(bytes) {
    const u8 = toU8(bytes), out = [];
    this.realtime = [];
    for (let i = 0; i < u8.length; i++) {
      const b = u8[i];
      if (b >= 0xF8) { this.realtime.push(b); if (this.onRealtime) this.onRealtime(b); continue; }
      if (b === 0xF0) {
        if (this.buf) this.aborted++;
        this.buf = new Uint8Array(256); this.len = 0; this._put(b);
        continue;
      }
      if (!this.buf) continue;
      if (b === 0xF7) { this._put(b); if (this.buf) out.push(this.buf.slice(0, this.len)); this.buf = null; this.len = 0; continue; }
      if (b & 0x80) { this.aborted++; this.buf = null; this.len = 0; continue; }
      this._put(b);
    }
    return out;
  }
}

/** .syx file (concatenated messages, junk between them ignored) -> array of Uint8Array messages. */
export function parseSyx(bytes) { return new SysexAssembler().push(bytes); }
/** Concatenate messages into .syx file bytes. */
export function writeSyx(messages) {
  const list = messages.map(toU8), out = new Uint8Array(list.reduce((n, m) => n + m.length, 0));
  let o = 0;
  for (const m of list) { out.set(m, o); o += m.length; }
  return out;
}
export const isSysex = (m) => m && m.length >= 2 && m[0] === 0xF0 && m[m.length - 1] === 0xF7;

// ---------------------------------------------------------------- Yamaha

/** Yamaha standard checksum: two's complement of the 7-bit sum of the data bytes. */
export function yamahaChecksum(data, from = 0, to = data.length) {
  let s = 0;
  for (let i = from; i < to; i++) s += data[i];
  return (-s) & 0x7F;
}
export const isYamaha = (m) => !!m && m[0] === 0xF0 && m[1] === 0x43;
/** Device-number filter for F0 43 sn (n = deviceNo-1). deviceNo 0 (OFF) refuses everything, like the RX5. */
export const yamahaDeviceOk = (m, deviceNo) => !!deviceNo && (m[2] & 0x0F) === ((deviceNo - 1) & 0x0F);
/** Bulk I/O (receive, transmit, answering requests) is enabled only with a device number set (MIDI Job #08). */
export const bulkEnabled = (state) => !!(state.midi && state.midi.deviceNo);

/**
 * Inspect a Yamaha message: {device 1..16, subStatus (0 bulk, 1 param change, 2 dump request, ...), format,
 * count, layout, checksum}. Recognized layout: F0 43 0n ff bh bl <count data bytes> cs F7, count = bh<<7 | bl
 * (DX7/TX/RX/QX-family bulk form; checksum over the data, or — some models — over bh bl + data).
 */
export function yamahaInfo(msg) {
  const m = toU8(msg);
  const info = { device: (m[2] & 0x0F) + 1, subStatus: m[2] >> 4, format: m[3], count: null, layout: 'unknown', checksum: 'unknown-layout' };
  if (!isYamaha(m) || !isSysex(m)) return info;
  if (info.subStatus === 0 && m.length >= 8) {
    const count = (m[4] << 7) | m[5];
    if (m.length === count + 8) {
      info.count = count;
      const cs = m[m.length - 2];
      if (yamahaChecksum(m, 6, 6 + count) === cs) { info.layout = 'F0 43 0n ff bh bl data cs F7'; info.checksum = 'ok'; }
      else if (yamahaChecksum(m, 4, 6 + count) === cs) { info.layout = 'F0 43 0n ff bh bl data cs F7 (sum incl. count)'; info.checksum = 'ok'; }
      else { info.layout = 'F0 43 0n ff bh bl data cs F7'; info.checksum = 'bad'; }
    }
  }
  return info;
}
/** 'ok' | 'bad' | 'unknown-layout' for one Yamaha message (show 'bad' as "checksum error!"). */
export const verifyYamahaChecksum = (msg) => yamahaInfo(msg).checksum;
/** Aggregate over a whole dump: any 'bad' -> 'bad'; all 'ok' -> 'ok'; else 'unknown-layout'. */
export function verifyBulk(messages) {
  const r = messages.map(verifyYamahaChecksum);
  if (r.includes('bad')) return 'bad';
  return r.length && r.every((x) => x === 'ok') ? 'ok' : 'unknown-layout';
}

/**
 * Bulk dump request in Yamaha's standard form F0 43 2n ff F7 (n = deviceNo-1). UNVERIFIED for the RX5: the
 * format byte `ff` the RX5 sends in MIDI Job #09 is not documented — capture it from hardware and pass it here.
 */
export function dumpRequest(deviceNo = 1, formatByte = 0x00) {
  return Uint8Array.from([0xF0, 0x43, 0x20 | (((deviceNo || 1) - 1) & 0x0F), formatByte & 0x7F, 0xF7]);
}

// ---------------------------------------------------------------- librarian (state.rxBulk)

/**
 * Receives a hardware bulk dump that may be many messages. Keep ONE receiver per app/plugin instance: messages
 * arriving less than gapMs apart belong to the same dump; a longer gap (or begin()) starts a new dump, which
 * replaces state.rxBulk. state.rxBulk is updated after every message:
 *   {received: ISO date, messages: [base64 of each message], status: 'ok'|'bad'|'unknown-layout', bytes, count}
 */
export class BulkReceiver {
  constructor(opts = {}) { this.gapMs = opts.gapMs ?? 2000; this.last = -Infinity; this.list = null; }
  begin() { this.list = null; this.last = -Infinity; }
  push(state, msg, now = Date.now()) {
    const m = toU8(msg);
    if (!this.list || now - this.last > this.gapMs) this.list = [];
    this.last = now;
    this.list.push(m.slice());
    state.rxBulk = makeRxBulk(this.list);
    return { status: state.rxBulk.status, count: this.list.length, checksum: verifyYamahaChecksum(m) };
  }
}
function makeRxBulk(list) {
  return { received: new Date().toISOString(), messages: list.map((m) => b64encode(m)), status: verifyBulk(list),
    bytes: list.reduce((n, m) => n + m.length, 0), count: list.length };
}
/** Store a complete set of messages as the hardware dump (replaces state.rxBulk). Non-Yamaha messages are skipped. */
export function storeBulk(state, messages) {
  const list = messages.map(toU8).filter(isYamaha);
  state.rxBulk = list.length ? makeRxBulk(list) : null;
  return state.rxBulk;
}
/** The stored hardware dump as Uint8Array messages (byte-identical to what was received). */
export function rxBulkMessages(state) {
  return state.rxBulk && Array.isArray(state.rxBulk.messages) ? state.rxBulk.messages.map((s) => b64decode(s)) : [];
}
/**
 * TRANSMIT BULK: the stored dump, byte-identical. opts.deviceNo (1..16) rewrites the device nibble of F0 43 sn
 * (outside the checksummed data) for sending to a hardware unit set to another device number.
 */
export function transmitBulk(state, opts = {}) {
  const list = rxBulkMessages(state);
  if (opts.deviceNo) for (const m of list) if (isYamaha(m)) m[2] = (m[2] & 0xF0) | ((opts.deviceNo - 1) & 0x0F);
  return list;
}
export const rxBulkToSyx = (state) => writeSyx(rxBulkMessages(state));

// ---------------------------------------------------------------- Omega messages

const OMEGA_ID = SYSEX_HEADER.slice(1); // 7D 4F 35
export const isOmega = (m) => !!m && m[0] === 0xF0 && m[1] === OMEGA_ID[0] && m[2] === OMEGA_ID[1] && m[3] === OMEGA_ID[2];

/** F0 7D 4F 35 cmd <payload> cs F7. */
export function omegaMessage(cmd, payload = []) {
  const body = [cmd & 0x7F, ...payload];
  if (body.some((b) => b > 0x7F)) throw new Error('omegaMessage: payload must be 7-bit');
  return Uint8Array.from([...SYSEX_HEADER, ...body, yamahaChecksum(body), 0xF7]);
}
const jsonPayload = (obj) => pack7(utf8encode(JSON.stringify(obj)));
const fromJsonPayload = (d) => JSON.parse(utf8decode(unpack7(d)));

export const omegaFull = (state) => omegaMessage(OMEGA.FULL, jsonPayload(cartToJSON(state)));
export const omegaPattern = (state, n) => omegaMessage(OMEGA.PATTERN, [n & 0x7F, ...jsonPayload(state.patterns[n])]);
export const omegaSong = (state, n) => omegaMessage(OMEGA.SONG, [n & 0x7F, ...jsonPayload(state.songs[n])]);
export const omegaVoices = (state) => omegaMessage(OMEGA.VOICES, jsonPayload({ voice: state.voice, channel: state.channel }));
export const omegaFx = (state) => omegaMessage(OMEGA.FX, jsonPayload(state.fx));
export const omegaRequest = (what, n = 0) => omegaMessage(OMEGA.REQUEST, [what & 0x7F, n & 0x7F]);
export const omegaIdentity = () => omegaMessage(OMEGA.IDENTITY, [...OMEGA_VERSION, ...Array.from(IDENTITY_NAME, (c) => c.charCodeAt(0))]);

/**
 * Single parameter change. p = {target: 'voice'|'channel'|'rev'|'dly'|'global'|'key'|'midi', index, name};
 * value: number or boolean (sent as round(value*1000), so 3 decimals).
 */
export function omegaParam(p, value) {
  const tt = PARAM_TARGETS.indexOf(p.target);
  if (tt < 0) throw new Error('omegaParam: bad target ' + p.target);
  const name = String(p.name);
  if (!/^[\x20-\x7E]{1,32}$/.test(name)) throw new Error('omegaParam: bad name');
  const x = Math.round((typeof value === 'boolean' ? +value : +value) * 1000);
  if (!Number.isFinite(x) || Math.abs(x) >= 2 ** 34) throw new Error('omegaParam: value out of range');
  const u = x < 0 ? x + 2 ** 35 : x, v = [];
  for (let k = 4; k >= 0; k--) v.push(Math.floor(u / 2 ** (7 * k)) & 0x7F);
  return omegaMessage(OMEGA.PARAM, [tt, (p.index | 0) & 0x7F, ...Array.from(name, (c) => c.charCodeAt(0)), 0, ...v]);
}

/**
 * Decode an Omega message: {cmd, ok, error?, n?, data?, param?, request?, identity?}. Legacy (no cmd) dumps
 * come back as cmd 1. A wrong checksum gives ok:false, error:'checksum error!'.
 */
export function parseOmega(msg) {
  const m = toU8(msg), h = SYSEX_HEADER.length;
  if (!isOmega(m) || !isSysex(m)) return { cmd: 0, ok: false, error: 'not an Omega Five message' };
  try {
    if (m[h] === 0x00 && m[h + 1] === 0x7B) return { cmd: OMEGA.FULL, ok: true, legacy: true, data: fromJsonPayload(m.subarray(h, m.length - 1)) };
    const cmd = m[h];
    if (yamahaChecksum(m, h, m.length - 1) !== 0) return { cmd, ok: false, error: 'checksum error!' };
    const d = m.subarray(h + 1, m.length - 2);
    switch (cmd) {
      case OMEGA.FULL: case OMEGA.VOICES: case OMEGA.FX: return { cmd, ok: true, data: fromJsonPayload(d) };
      case OMEGA.PATTERN: case OMEGA.SONG: return { cmd, ok: true, n: d[0], data: fromJsonPayload(d.subarray(1)) };
      case OMEGA.PARAM: {
        const z = d.indexOf(0, 2);
        if (z < 0 || d.length !== z + 6) return { cmd, ok: false, error: 'bad parameter message' };
        let u = 0;
        for (let k = 0; k < 5; k++) u = u * 128 + d[z + 1 + k];
        const x = u >= 2 ** 34 ? u - 2 ** 35 : u;
        return { cmd, ok: true, param: { target: PARAM_TARGETS[d[0]], index: d[1], name: String.fromCharCode(...d.subarray(2, z)), value: x / 1000 } };
      }
      case OMEGA.REQUEST: return { cmd, ok: true, request: { what: d[0], n: d[1] | 0 } };
      case OMEGA.IDENTITY: return { cmd, ok: true, identity: { version: Array.from(d.subarray(0, 4)), name: String.fromCharCode(...d.subarray(4)) } };
    }
    return { cmd, ok: false, error: 'unknown command ' + cmd };
  } catch (e) {
    return { cmd: m[h], ok: false, error: 'bad data: ' + e.message };
  }
}

/** Messages answering an Omega dump request {what, n}. */
export function omegaReply(state, req) {
  switch (req.what) {
    case OMEGA.FULL: return [omegaFull(state)];
    case OMEGA.PATTERN: return req.n < NUM_PATTERNS ? [omegaPattern(state, req.n)] : [];
    case OMEGA.SONG: return req.n < NUM_SONGS ? [omegaSong(state, req.n)] : [];
    case OMEGA.VOICES: return [omegaVoices(state)];
    case OMEGA.FX: return [omegaFx(state)];
    case OMEGA.IDENTITY: return [omegaIdentity()];
  }
  return [];
}

/** Apply a parameter change to state. Returns false if the target/index is invalid. */
export function applyParam(state, p) {
  const { target, index: i, name, value } = p;
  if (name === '__proto__' || name === 'constructor' || name === 'prototype') return false;
  switch (target) {
    case 'voice': if (i >= NUM_VOICES) return false; state.voice[i] = { ...state.voice[i], [name]: value }; return true;
    case 'channel': if (i >= NUM_CHANNELS) return false; state.channel[i] = { ...state.channel[i], [name]: value }; return true;
    case 'rev': case 'dly': state.fx[target] = { ...state.fx[target], [name]: value }; return true;
    case 'global': if (!['tempo', 'volume', 'swing', 'quantize', 'setup'].includes(name)) return false; state[name] = value; return true;
    case 'key': { const su = state.setups && state.setups[state.setup | 0]; if (!su || i >= su.keys.length) return false; su.keys[i] = { ...su.keys[i], [name]: value }; return true; }
    case 'midi': if (!(name in state.midi) || typeof state.midi[name] === 'object') return false; state.midi[name] = typeof state.midi[name] === 'boolean' ? !!value : value; return true;
  }
  return false;
}

/** Apply a parsed Omega message (parseOmega result) to state; returns a description, or throws on error. */
export function applyOmega(state, r) {
  if (!r.ok) throw new Error(r.error);
  switch (r.cmd) {
    case OMEGA.FULL: { const s = cartFromJSON(r.data); for (const k of Object.keys(state)) delete state[k]; Object.assign(state, s); return 'all data'; }
    case OMEGA.PATTERN: {
      if (r.n >= NUM_PATTERNS) throw new Error('pattern number out of range');
      const pats = []; pats[r.n] = r.data;
      state.patterns[r.n] = normalizeState({ patterns: pats }).patterns[r.n];
      return 'pattern ' + String(r.n).padStart(2, '0');
    }
    case OMEGA.SONG: {
      if (r.n >= NUM_SONGS) throw new Error('song number out of range');
      const songs = []; songs[r.n] = r.data;
      state.songs[r.n] = normalizeState({ songs }).songs[r.n];
      return 'song ' + String(r.n).padStart(2, '0');
    }
    case OMEGA.VOICES: {
      const s = normalizeState({ voice: r.data.voice, channel: r.data.channel });
      state.voice = s.voice; state.channel = s.channel;
      return 'voice data';
    }
    case OMEGA.FX: if (r.data && typeof r.data === 'object') state.fx = r.data; return 'fx data';
    case OMEGA.PARAM: if (!applyParam(state, r.param)) throw new Error('bad parameter ' + r.param.target + '/' + r.param.name); return 'param ' + r.param.target + ' ' + r.param.name;
  }
  return '';
}

// ---------------------------------------------------------------- identity

/** Universal identity reply (dev = our device number - 1, or 7F when OFF). */
export function identityReply(deviceNo = 0) {
  return Uint8Array.from([0xF0, 0x7E, deviceNo ? (deviceNo - 1) & 0x7F : 0x7F, 0x06, 0x02, ...IDENTITY_ID, 0xF7]);
}
export const isIdentityRequest = (m) => !!m && m.length === 6 && m[0] === 0xF0 && m[1] === 0x7E && m[3] === 0x06 && m[4] === 0x01 && m[5] === 0xF7;

// ---------------------------------------------------------------- dispatcher

/**
 * Handle one complete incoming SysEx message (from SysexAssembler / midiIn {type:'sysex'} / a .syx file).
 * ctx: {receiver: BulkReceiver (keep one per instance; a fresh one stores this message as a whole dump),
 *       now: ms timestamp, deviceNo (default state.midi.deviceNo)}.
 * Returns {kind, reply: [Uint8Array] to send back, message: LCD text, ...}:
 *   kind 'identity'      — universal identity request answered
 *   kind 'omega'         — Omega dump/param applied ({what, param?}); request answered; error -> {error}
 *   kind 'yamaha-bulk'   — hardware dump message stored ({status, count, checksum})
 *   kind 'yamaha-request'— Yamaha dump request (F0 43 2n ..) answered with the stored dump (if any)
 *   kind 'ignored'       — device number mismatch;  kind 'other' — not ours; kind 'invalid' — not F0..F7
 */
export function handleSysex(state, msg, ctx = {}) {
  const m = toU8(msg);
  const deviceNo = ctx.deviceNo ?? (state.midi ? state.midi.deviceNo : 0);
  if (!isSysex(m)) return { kind: 'invalid', reply: [], message: 'bad SysEx' };
  if (isIdentityRequest(m)) {
    if (!sysexDeviceOk(m, deviceNo)) return { kind: 'ignored', reply: [], message: '' };
    return { kind: 'identity', reply: [identityReply(deviceNo)], message: '' };
  }
  if (isOmega(m)) {
    if (!deviceNo) return { kind: 'ignored', reply: [], message: 'device no. OFF' };
    const r = parseOmega(m);
    if (!r.ok) return { kind: 'omega', cmd: r.cmd, reply: [], error: r.error, message: r.error };
    if (r.cmd === OMEGA.REQUEST) return { kind: 'omega', cmd: r.cmd, reply: omegaReply(state, r.request), message: 'dump request' };
    if (r.cmd === OMEGA.IDENTITY) return { kind: 'omega', cmd: r.cmd, identity: r.identity, reply: [], message: r.identity.name };
    try {
      const what = applyOmega(state, r);
      return { kind: 'omega', cmd: r.cmd, what, param: r.param, reply: [], message: 'received ' + what };
    } catch (e) { return { kind: 'omega', cmd: r.cmd, reply: [], error: e.message, message: e.message }; }
  }
  if (isYamaha(m)) {
    if (!yamahaDeviceOk(m, deviceNo)) return { kind: 'ignored', reply: [], message: deviceNo ? '' : 'device no. OFF' };
    if ((m[2] >> 4) === 2) {
      const reply = transmitBulk(state);
      return { kind: 'yamaha-request', reply, message: reply.length ? 'transmit bulk' : 'no bulk data' };
    }
    const r = (ctx.receiver || new BulkReceiver()).push(state, m, ctx.now ?? Date.now());
    return { kind: 'yamaha-bulk', reply: [], ...r, message: r.checksum === 'bad' ? 'checksum error!' : 'bulk ' + r.count };
  }
  return { kind: 'other', reply: [], message: '' };
}

/**
 * Load a .syx file into state (a file, not MIDI I/O: works with device number OFF): Yamaha messages become the stored hardware dump (replacing state.rxBulk), Omega
 * messages are applied in order. Returns {yamaha, omega, other, status, errors: []}.
 */
export function loadSyx(state, bytes) {
  const msgs = parseSyx(bytes), yam = [], errors = [];
  let omega = 0, other = 0;
  for (const m of msgs) {
    if (isYamaha(m)) yam.push(m);
    else if (isOmega(m)) {
      const r = parseOmega(m);
      try { if (r.cmd !== OMEGA.REQUEST && r.cmd !== OMEGA.IDENTITY) applyOmega(state, r); omega++; } catch (e) { errors.push(e.message); }
    } else other++;
  }
  if (yam.length) storeBulk(state, yam);
  return { yamaha: yam.length, omega, other, status: yam.length ? state.rxBulk.status : null, errors };
}
