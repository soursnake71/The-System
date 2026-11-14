// Renders the same performances with engine.js and with the C++ port (plugins/seven/Seven.h) and compares them sample by sample.
// usage: node seven/tools/compare.mjs path/to/seven_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import {execFileSync} from 'child_process';
const require = createRequire(import.meta.url);
const DX = globalThis.DX = require('../engine.js');
const V = require('../voices.js');
const [bin, dir] = process.argv.slice(2);
const F = k => DX.FUNC_DEF.findIndex(d => d[0] === k);
const chord = (t, notes, v, len) => notes.flatMap(n => [[t, 'on', n, v], [t + len, 'off', n]]);
const cases = [
  ['init voice A4', 48000, 1.0, [[0, 'voice', DX.initVoice()], [0, 'on', 69, 100], [30000, 'off', 69]]],
  ['tine piano chord, 44.1k', 44100, 2.0, [[0, 'voice', V.FACTORY[1]], ...chord(0, [48, 55, 64, 71], 90, 40000)]],
  ['brass with vibrato + wheel', 48000, 2.0, [[0, 'voice', V.FACTORY[0]], [0, 'wheel', 100], ...chord(100, [60, 64, 67], 110, 60000)]],
  ['every algorithm, feedback 7', 48000, 3.2, [[0, 'voice', V.FACTORY[9]], [0, 'p', 135, 7], ...Array.from({length:32}, (_, i) => [[i * 4800, 'p', 134, i], [i * 4800, 'on', 50 + (i % 12), 100], [i * 4800 + 4000, 'off', 50 + (i % 12)]]).flat()]],
  ['mono + portamento + gliss + bend steps', 48000, 2.0, [[0, 'voice', V.FACTORY[20]], [0, 'f', F('mono'), 1], [0, 'f', F('portaTime'), 60], [0, 'f', F('portaMode'), 1], [0, 'f', F('gliss'), 1], [0, 'f', F('pbStep'), 2],
    [0, 'on', 48, 100], [20000, 'on', 60, 100], [40000, 'off', 60], [50000, 'bend', 5000], [70000, 'bend', -8192], [80000, 'off', 48]]],
  ['S/H LFO, amp mod, EG bias aftertouch, sustain', 96000, 1.5, [[0, 'voice', V.FACTORY[13]], [0, 'f', F('atAssign'), 6], [0, 'f', F('atRange'), 99], [0, 'at', 64], [0, 'sus', 1], ...chord(0, [40, 47, 52], 80, 30000), [100000, 'sus', 0]]],
  ['fixed-frequency snare, clean DAC', 48000, 1.0, [[0, 'voice', V.FACTORY[14]], [0, 'f', F('dac'), 1], [0, 'on', 38, 127], [12000, 'on', 38, 60], [30000, 'off', 38]]],
  ['16-voice stealing, bells', 48000, 2.0, [[0, 'voice', V.FACTORY[15]], ...Array.from({length:22}, (_, i) => [i * 2000, 'on', 40 + i * 2, 70 + i])]],
  ['live edits while sounding', 48000, 1.5, [[0, 'voice', V.FACTORY[4]], [0, 'on', 36, 100], [10000, 'p', DX.opBase(2) + 16, 60], [20000, 'p', DX.opBase(1) + 18, 2], [30000, 'p', 155, 0b101111], [40000, 'p', 137, 90], [50000, 'off', 36]]],
  ['random voice, chorus + delay + reverb', 48000, 2.5, [[0, 'rnd', 987654321], [0, 'f', F('chorus'), 60], [0, 'f', F('chorusRate'), 40], [0, 'f', F('delayMix'), 50], [0, 'f', F('delayTime'), 280], [0, 'f', F('delayFb'), 70],
    [0, 'f', F('dlModDepth'), 60], [0, 'f', F('dlDuck'), 70], [0, 'f', F('revMix'), 60], [0, 'f', F('revSize'), 40], [0, 'f', F('rvProg'), 3], [0, 'f', F('rvDelay'), 35], ...chord(0, [52, 59, 64], 100, 40000)]],
  ['REV7 hall + early ref, synced delay, 96k', 96000, 1.5, [[0, 'voice', V.FACTORY[1]], [0, 'f', F('revMix'), 70], [0, 'f', F('rvProg'), 0], [0, 'f', F('delayMix'), 40], [0, 'f', F('dlSync'), 3],
    [0, 'f', F('dlPing'), 0], [0, 'on', 60, 110], [20000, 'off', 60], [60000, 'f', F('rvProg'), 4], [60000, 'on', 67, 90], [90000, 'off', 67]]],
  ['random voices at 44.1k with reverb', 44100, 2.0, [[0, 'f', F('revMix'), 40], ...Array.from({length:8}, (_, i) => [[i * 11025, 'rnd', 1000 + i * 7919], [i * 11025, 'on', 48 + i * 3, 100], [i * 11025 + 9000, 'off', 48 + i * 3]]).flat()]],
  ['pitch EG sweep, master tune, transpose', 48000, 1.2, [[0, 'voice', V.FACTORY[16]], [0, 'f', F('tune'), 100], [0, 'p', 144, 30], [0, 'on', 60, 127], [40000, 'off', 60]]]
];
let fail = 0;
for (const [name, fs_, secs, evs] of cases){
  const frames = Math.round(fs_ * secs);
  const sorted = evs.slice().sort((a, b) => a[0] - b[0]);
  fs.writeFileSync(dir + '/s.txt', sorted.map(e => e[1] === 'voice' ? `${e[0]} voice ${Array.from(e[2]).join(' ')}` : e.join(' ')).join('\n'));
  const info = execFileSync(bin, [dir + '/s.txt', String(fs_), String(frames), dir + '/s.out']).toString().trim();
  const b = fs.readFileSync(dir + '/s.out'), cpp = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  // the same with engine.js (left then right, like the C++)
  const syn = new DX.Synth(fs_), js = new Float32Array(frames * 2), jsL = js.subarray(0, frames), jsR = js.subarray(frames);
  let pos = 0, ei = 0;
  while (pos < frames){
    while (ei < sorted.length && sorted[ei][0] <= pos){
      const [, c, a, bb] = sorted[ei];
      if (c === 'voice') syn.loadVoice(a); else if (c === 'rnd') syn.loadVoice(DX.randomVoice(a)); else if (c === 'on') syn.noteOn(a, bb); else if (c === 'off') syn.noteOff(a); else if (c === 'p') syn.setParam(a, bb);
      else if (c === 'f') syn.setFunc(DX.FUNC_DEF[a][0], bb); else if (c === 'bend') syn.setBend(a); else if (c === 'wheel') syn.setCtl('wheel', a); else if (c === 'at') syn.setCtl('at', a); else if (c === 'sus') syn.sustain(!!a);
      ei++;
    }
    const next = ei < sorted.length ? Math.min(frames, sorted[ei][0]) : frames, n = Math.max(1, next - pos);
    syn.renderStereo(jsL, jsR, pos, Math.min(n, frames - pos)); pos += n;
  }
  let err = 0, pk = 0, at = 0;
  for (let i = 0; i < frames * 2; i++){ const d = Math.abs(cpp[i] - js[i]); if (d > err){ err = d; at = i; } pk = Math.max(pk, Math.abs(js[i])); }
  const ok = cpp.length === frames * 2 && err < 1e-4 && pk > 0.005;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: max diff ${err.toExponential(2)} at ${at} (peak ${pk.toFixed(3)}) [C++ ${info}, JS voices ${syn.activeVoices()}]`);
}
process.exitCode = fail ? 1 : 0;
