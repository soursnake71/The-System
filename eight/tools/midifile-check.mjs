// MIDI file import/export check: build a file from patterns, read it back, and compare. node eight/tools/midifile-check.mjs
import {createRequire} from 'module';
const require = createRequire(import.meta.url);
const MF = require('../midifile.js');
let fail = 0;
const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fail++; };
const pat = (o) => { const steps = {}; for (const t of ['AC','BD','SD','LT','MT','HT','RS','CP','CB','CY','OH','CH','TRIG']) steps[t] = o[t] || '.'.repeat(16); return {scale: 0, len: 16, parts: 1, steps}; };
const a = pat({AC: 'x...............', BD: 'x.....x...x.....', SD: '....x.......x...', CH: 'x.x.x.x.x.x.x.xx', CP: '............x...'});
const b = pat({BD: 'x..x..x.........', OH: '..x...x...x...x.', TRIG: '....x.......x...'});
const file = MF.build([a, b], 97);
const mf = MF.parse(file.buffer);
ok(Math.abs(mf.tempo - 97) < 0.01, `tempo read back: ${mf.tempo.toFixed(2)}`);
const r = MF.toPatterns(mf);
ok(r.ch === 10 && r.bars === 2 && r.patterns.length === 2, `channel ${r.ch}, ${r.bars} bars`);
for (const [i, p] of [[0, a], [1, b]]) for (const t in p.steps) ok(r.patterns[i].steps[t] === p.steps[t], `bar ${i + 1} ${t} ${r.patterns[i].steps[t]}`);
// a hand-made format 1 file with running status, a non-drum note and an off-grid hit
const trk = (bytes) => [0x4D, 0x54, 0x72, 0x6B, 0, 0, 0, bytes.length, ...bytes];
const t1 = trk([0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 0, 0xFF, 0x2F, 0]);                     // 120 bpm
const t2 = trk([0, 0x99, 36, 100, 0, 81, 40, 0x50, 0x89, 36, 0, 0x81, 0x00, 0x99, 22, 90, 0, 0xFF, 0x2F, 0]); // BD acc, 81 unmapped (running status), note off, 22 unmapped
const f1 = new Uint8Array([0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, 2, 0, 96, ...t1, ...t2]);
const r1 = MF.toPatterns(MF.parse(f1.buffer));
ok(r1.tempo === 120 && r1.patterns[0].steps.BD[0] === 'x' && r1.patterns[0].steps.AC[0] === 'x' && r1.skipped === 2, `format 1, running status, skipped ${r1.skipped}`);
console.log(fail ? `${fail} failed` : 'all midi file checks passed');
process.exit(fail ? 1 : 0);
