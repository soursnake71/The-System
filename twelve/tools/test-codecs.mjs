// Writes every export format and reads it back with our own readers (and ffmpeg when given an output folder).
import {createRequire} from 'module';
import fs from 'fs';
const C = createRequire(import.meta.url)('../codecs.js');
const dir = process.argv[2], sr = 44100, n = 30000;
const chs = [new Float32Array(n), new Float32Array(n)];
for (let i = 0; i < n; i++){ chs[0][i] = Math.sin(i / 7) * .7; chs[1][i] = (Math.random() * 2 - 1) * .3; }
const out = {wav16:C.encodeWav(chs, sr, 16), wav24:C.encodeWav(chs, sr, 24), wavf:C.encodeWav(chs, sr, 'f32'), aif16:C.encodeAiff(chs, sr, 16), aif24:C.encodeAiff(chs, sr, 24), flac16:C.encodeFlac(chs, sr, 16), flac24:C.encodeFlac(chs, sr, 24)};
for (const [k, b] of Object.entries(out)){
  const ab = b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
  const r = k.startsWith('wav') ? C.parseWav(ab) : k.startsWith('aif') ? C.parseAiff(ab) : null;
  if (r){ let e = 0; for (let c = 0; c < 2; c++) for (let i = 0; i < n; i++) e = Math.max(e, Math.abs(r.chs[c][i] - chs[c][i])); console.log(k, b.length, 'bytes, sr', r.sr, 'max err', e.toExponential(2)); }
  else console.log(k, b.length, 'bytes');
  if (dir) fs.writeFileSync(`${dir}/t.${k}.${k.replace(/\d+|f$/g, '').replace('aif', 'aiff')}`, b);
}
