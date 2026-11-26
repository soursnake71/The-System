// Checks the plugin's C++ data model (plugins/five/FiveSeq.h, FiveSysex.h, FiveCart.h via five_seq_test) against seq.js,
// rxsysex.js and rxcart.js: cartridge JSON both ways, randomize, SMF export / import, Omega SysEx both ways, RX5 bank
// images, the Player's event stream (song, pattern with click, chain) and SysEx replies with the device filter.
// usage: node five/tools/interop.mjs <five_seq_test> [scratchDir]
import {execFileSync} from 'node:child_process';
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import path from 'node:path';
import * as SQ from '../seq.js';
import * as RX from '../rxsysex.js';
import * as RC from '../rxcart.js';

const [bin, dir = '/tmp/five-interop'] = process.argv.slice(2);
if (!bin){ console.error('usage: node five/tools/interop.mjs <five_seq_test> [scratchDir]'); process.exit(2); }
mkdirSync(dir, {recursive:true});
const P = (f) => path.join(dir, f), rd = (f) => new Uint8Array(readFileSync(P(f))), json = (f) => JSON.parse(readFileSync(P(f), 'utf8'));
let fail = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fail++; };

// every key of `a` (recursively) must be in `b` with the same value (numbers within 1e-6; JS arrays vs C++ arrays elementwise)
function sub(a, b, at = ''){
  if (typeof a === 'number' && typeof b === 'number') return Math.abs(a - b) <= 1e-6 * Math.max(1, Math.abs(a)) ? null : `${at}: ${a} vs ${b}`;
  if (typeof a === 'boolean' && typeof b === 'number') return +a === b ? null : `${at}: ${a} vs ${b}`;
  if (a === null || typeof a !== 'object') return a === b ? null : `${at}: ${JSON.stringify(a)} vs ${JSON.stringify(b)}`;
  if (b === null || typeof b !== 'object') return `${at}: object vs ${JSON.stringify(b)}`;
  if (Array.isArray(a) && (!Array.isArray(b) || a.length !== b.length)) return `${at}: length ${a.length} vs ${Array.isArray(b) ? b.length : 'not an array'}`;
  for (const k of Object.keys(a)){ const r = sub(a[k], b[k], at + '.' + k); if (r) return r; }
  return null;
}
// the parts of a state both sides model
const KEYS = ['patterns', 'songs', 'chains', 'noteMap', 'setups', 'setup', 'sync', 'tempo', 'quantize', 'swing', 'volume', 'click', 'voice', 'channel', 'fx', 'wave'];
const MIDI_KEYS = ['rxCh', 'txCh', 'voiceTxCh', 'channelMsg', 'clockIn', 'clockOut', 'notesOut', 'deviceNo', 'tunable', 'tunableVoice', 'egVel', 'gateMs', 'activeSense', 'songSelect'];
function sameState(a, b, what){
  for (const k of KEYS){ const r = sub(a[k], b[k], k); if (r){ check(false, `${what}: ${r}`); return; } }
  for (const k of MIDI_KEYS){ const r = sub(a.midi[k], b.midi[k], 'midi.' + k); if (r){ check(false, `${what}: ${r}`); return; } }
  const r = sub(a.rxBulk || null, b.rxBulk || null, 'rxBulk'); if (r){ check(false, `${what}: ${r}`); return; }
  for (let v = 0; v < 24; v++){
    const x = a.samples[v], y = b.samples[v];
    if (!!x !== !!y || (x && (x.data.length !== y.data.length || x.sr !== y.sr || x.data.some((s, i) => Math.abs(s - y.data[i]) > 1e-4)))){ check(false, `${what}: samples[${v}] differ`); return; }
  }
  const la = a.library || [], lb = b.library || [];
  if (la.length !== lb.length || la.some((w, i) => w.id !== lb[i].id || w.name !== lb[i].name || w.kind !== lb[i].kind || w.sr !== lb[i].sr || w.data.length !== lb[i].data.length ||
    w.data.some((x, j) => Math.abs(x - lb[i].data[j]) > 1e-4))){ check(false, `${what}: library differs`); return; }
  for (let v = 0; v < 24; v++) if ((a.samples[v] && a.samples[v].lib) !== (b.samples[v] && b.samples[v].lib)){ check(false, `${what}: samples[${v}].lib differs`); return; }
  check(true, what);
}
const fromFile = (f) => SQ.cartFromJSON(json(f));
const cartFile = (s) => JSON.stringify(SQ.cartToJSON(s));

// ---------------- inputs ----------------
const S = SQ.newState();
SQ.randomize(S, 99, 'all');
SQ.randomize(S, 5, 'pattern', {pattern: 7});
SQ.setBeats(S, 10, 6, 8); SQ.setBars(S, 10, 2); SQ.setSwing(S, 10, 63);
for (let t = 0; t < SQ.patLen(S.patterns[10]); t += 12) SQ.stepInsert(S, 10, t, (t / 12) % 24, 60 + (t % 67), (t / 12) % 3);
S.patterns[10].volume[3] = 12;
SQ.setBeats(S, 11, 5, 6); SQ.stepInsert(S, 11, 16, 3, 99, 1);
SQ.songInsertStep(S, 0, S.songs[0].steps.length, {type: 'tempo', bpm: 132.5});
SQ.songInsertStep(S, 0, S.songs[0].steps.length, {type: 'vol', ch: 1, level: 9});
SQ.songInsertStep(S, 0, S.songs[0].steps.length, {type: 'pat', n: 10});
S.songs[2] = {name: 'TWO', tempo: 90, steps: [{type: 'pat', n: 11}, {type: 'pat', n: 7}, {type: 'rep', from: 0, count: 3}]};
SQ.chainSet(S, 0, [2, 0, 2]); S.chains[0].name = 'MY CHAIN';
S.noteMap[5] = 77; Object.assign(S.midi, {rxCh: 3, txCh: 4, notesOut: true, gateMs: 120, egVel: 2, deviceNo: 1});
S.midi.voiceTxCh[2] = 9; S.setups[1].name = 'SET B'; S.setups[1].keys[4] = {voice: 9, pitch: -700, level: -5, attack: 3, decay: -8, acc1: 1, acc2: 6, out: 2}; S.setup = 1;
S.quantize = 3; S.swing = 58; S.volume = .8; S.click = {on: false, level: .3}; S.tempo = 101.5;
S.voice[3] = Object.assign({}, S.voice[3], {path: 'rec'}); S.channel[6] = Object.assign({}, S.channel[6], {mute: true});
S.samples[4] = {name: 'TEST', sr: 25000, data: Float32Array.from({length: 1000}, (_, i) => Math.sin(i / 7) * .5), lib: 30};
S.wave[2] = 33; S.wave[9] = 30;
S.library = [{id: 30, name: 'KITWAV', kind: 'user', sr: 25000, data: Float32Array.from({length: 500}, (_, i) => Math.cos(i / 5) * .25), file: 'k.wav', source: 'test'}];
writeFileSync(P('js-cart.json'), cartFile(S));
writeFileSync(P('js-song.mid'), SQ.exportSMF(S, {what: 'song', n: 0, map: 'gm'}));
writeFileSync(P('js-omega.syx'), RX.writeSyx([RX.omegaPattern(S, 3), RX.omegaSong(S, 2), RX.omegaVoices(S), RX.omegaFx(S),
  RX.omegaParam({target: 'channel', index: 4, name: 'pan'}, -.5), RX.omegaParam({target: 'global', index: 0, name: 'tempo'}, 140)]));
const bankVoices = [0, 1, 2].map((k) => ({name: ['KICK', 'SNARE', 'HAT'][k], bits: k === 2 ? 8 : 12, pcm: Float32Array.from({length: 3000 + 777 * k}, (_, i) => Math.sin(i * (k + 1) / 9) * Math.exp(-i / 900)),
  params: {pitch: [0, -1200, 350][k], level: 27 - k, ar: 99, d1r: 10 * k, d1l: 60 - k, d2r: 5, rr: 40, gate: k === 1 ? 800 : 0, bendRate: k, bendRange: k * 200, loop: k === 2 ? 1 : 0}, out: k}));
writeFileSync(P('js-bank.bin'), RC.buildBank(bankVoices));
writeFileSync(P('js-rx.syx'), RX.writeSyx([RX.IDENTITY_REQUEST, [0xF0, 0x7E, 0x02, 0x06, 0x01, 0xF7], RX.omegaRequest(2, 3), RX.omegaRequest(1),
  RX.dumpRequest(1, 0), RX.dumpRequest(3, 0), RX.omegaIdentity()]));

execFileSync(bin, [dir], {stdio: 'inherit'});

// ---------------- checks ----------------
sameState(S, fromFile('cpp-cart.json'), 'cartridge: JS -> C++ -> JS');
{ const R = SQ.newState(); SQ.randomize(R, 4242, 'all'); SQ.randomize(R, 77, 'pattern', {pattern: 20}); sameState(R, fromFile('cpp-rand.json'), 'randomize (all, then pattern) gives the same memory'); }
{ const a = SQ.exportSMF(S, {what: 'pattern', n: 0, map: 'gm'}), b = rd('cpp-ptn.mid'); check(Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0, `SMF export pattern: ${a.length} / ${b.length} bytes`); }
{ const a = SQ.exportSMF(S, {what: 'song', n: 0, map: 'rx5'}), b = rd('cpp-song.mid'); check(Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0, `SMF export song: ${a.length} / ${b.length} bytes`); }
{ const I = SQ.newState(); SQ.importSMF(rd('js-song.mid'), {state: I, startPattern: 50, songSlot: 1}); sameState(I, fromFile('cpp-import.json'), 'SMF import'); }
{ const O = SQ.newState(); RX.loadSyx(O, rd('js-omega.syx')); sameState(O, fromFile('cpp-syx.json'), 'Omega SysEx JS -> C++'); }
{
  const C = SQ.cartFromJSON(json('js-cart.json')), mine = SQ.newState(), theirs = SQ.newState();
  RX.loadSyx(mine, RX.writeSyx([RX.omegaPattern(C, 0), RX.omegaSong(C, 0), RX.omegaVoices(C), RX.omegaFx(C), RX.omegaParam({target: 'rev', index: 0, name: 'time'}, 3.5),
    RX.omegaParam({target: 'voice', index: 5, name: 'pitch'}, -1200)]));
  const r = RX.loadSyx(theirs, rd('cpp-omega.syx'));
  check(!r.errors.length && r.omega === 6, `Omega SysEx C++ -> JS: ${r.omega} messages, errors ${JSON.stringify(r.errors)}`);
  sameState(mine, theirs, 'Omega SysEx C++ -> JS gives the same memory');
}
{ const a = rd('js-bank.bin'), b = rd('cpp-bank.bin'); let d = -1; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]){ d = i; break; }
  check(a.length === b.length && d < 0, `RX5 bank parse -> build${d >= 0 ? `: first difference at 0x${d.toString(16)} (${a[d]} vs ${b[d]})` : ''}`); }
{
  const C = SQ.cartFromJSON(json('js-cart.json')), L = [], pl = new SQ.Player(C), sr = 48000, block = 256;
  const run = (mode, n, blocks, tag) => {
    pl.setMode(mode, n); pl.start(); let at = 0;
    for (let b = 0; b < blocks && pl.running; b++, at += block) for (const e of pl.advance(block, sr)) L.push(`${tag} ${at + e.offset} ${e.v} ${e.vel} ${e.acc} ${e.click || 0}`);
    L.push(`${tag} end ${pl.running ? 1 : 0} ${pl.ended ? 1 : 0}`);
  };
  run('song', 0, 3000, 'song'); const ck = C.click.on; C.click.on = true; run('pattern', 10, 400, 'ptn'); C.click.on = ck; run('chain', 0, 4000, 'chain');
  const a = L, b = readFileSync(P('cpp-play.txt'), 'utf8').trim().split('\n'); let d = -1;
  for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]){ d = i; break; }
  check(d < 0, `Player: ${a.length} / ${b.length} events${d >= 0 ? `, first difference at ${d}: js "${a[d]}" c++ "${b[d]}"` : ''}`);
}
{
  const C = SQ.cartFromJSON(json('js-cart.json')), msgs = RX.parseSyx(rd('js-rx.syx')), L = [];
  const hex = (m) => Array.from(m, (x) => x.toString(16).toUpperCase().padStart(2, '0')).join('').slice(0, 40);
  for (const dev of [1, 0, 3]){
    const s = SQ.cartFromJSON(json('js-cart.json')); s.midi.deviceNo = dev;
    for (const m of msgs){ const r = RX.handleSysex(s, m, {receiver: new RX.BulkReceiver(), now: 0}); L.push(`${dev} ${r.kind} ${r.reply.length}` + r.reply.map((x) => ' ' + (RX.isOmega(x) ? 'omega' : hex(x))).join('')); }
  }
  void C;
  const b = readFileSync(P('cpp-rx.txt'), 'utf8').trim().split('\n'); let d = -1;
  for (let i = 0; i < Math.max(L.length, b.length); i++) if (L[i] !== b[i]){ d = i; break; }
  check(d < 0, `SysEx replies and device filter: ${L.length} / ${b.length}${d >= 0 ? `, first difference at ${d}: js "${L[d]}" c++ "${b[d]}"` : ''}`);
}
console.log(fail ? `interop: ${fail} check(s) failed` : 'interop: all checks passed');
process.exitCode = fail ? 1 : 0;
