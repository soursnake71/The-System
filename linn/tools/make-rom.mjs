// Burns the Omega Linn sound ROM (linn/rom/linn.rom) from the owner's LinnDrum recordings in linn/samples/set1 and set2
// (SOUND SET 1 and 2; any names: linn/kit.js decides which sound each file is, the same rules as LOAD FOLDER in the app). Never synthesized: a sound
// with no recording is left EMPTY (silent) until one is added.
// usage: node linn/tools/make-rom.mjs            (writes linn/rom/linn.rom)
//        node linn/tools/make-rom.mjs --check    (fails if the file is out of date)
import fs from 'fs';
import path from 'path';
import {fileURLToPath} from 'url';
import {createRequire} from 'module';
const req = createRequire(import.meta.url);
const D = req('../engine.js'), KIT = req('../kit.js');
const here = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(here, '..', 'rom', 'linn.rom');
const SAMPLES = path.join(here, '..', 'samples');

export function readWav(file){
  const b = fs.readFileSync(file); let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length){ const id = b.toString('ascii', o, o + 4), n = b.readUInt32LE(o + 4);
    if (id === 'fmt ') fmt = {fmt:b.readUInt16LE(o + 8), ch:b.readUInt16LE(o + 10), sr:b.readUInt32LE(o + 12), bits:b.readUInt16LE(o + 22)};
    else if (id === 'data') data = b.subarray(o + 8, o + 8 + n);
    o += 8 + n + (n & 1); }
  if (!fmt || !data || ![16, 24, 32].includes(fmt.bits)) throw new Error(file + ': need a 16 / 24-bit PCM or 32-bit float WAV');
  const bps = fmt.bits / 8, n = Math.floor(data.length / bps / fmt.ch), d = new Float64Array(n);
  const rd = o => fmt.bits === 16 ? data.readInt16LE(o) / 32768 : fmt.bits === 24 ? data.readIntLE(o, 3) / 8388608 : fmt.fmt === 3 ? data.readFloatLE(o) : data.readInt32LE(o) / 2147483648;
  for (let i = 0; i < n; i++){ let x = 0; for (let c = 0; c < fmt.ch; c++) x += rd((i * fmt.ch + c) * bps); d[i] = x / fmt.ch; }
  return {d, sr:fmt.sr};
}
export function burn(layers){     // layers: [{g, kit, dyn, rate, clk, data}] -> the ROM image
  const head = Buffer.alloc(8 + layers.length * 16);
  head.write('OLIN', 0, 'ascii'); head.writeUInt8(2, 4); head.writeUInt16LE(layers.length, 5);
  const parts = layers.map((L, i) => {
    const o = 8 + i * 16; head.writeUInt8(L.g, o); head.writeUInt8(16, o + 1); head.writeUInt8(L.kit, o + 2); head.writeUInt8(L.dyn + 1, o + 3);
    head.writeUInt32LE(L.rate, o + 4); head.writeUInt32LE(L.clk, o + 8); head.writeUInt32LE(L.data.length, o + 12);
    const b = Buffer.alloc(L.data.length * 2); L.data.forEach((x, j) => b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(x * 32767))), j * 2)); return b;
  });
  return Buffer.concat([head, ...parts]);
}
// the two sets: linn/samples/set1 (SOUND SET 1) and set2 (SOUND SET 2)
const layers = [], left = [];
for (const [kit, dir] of [[1, 'set1'], [2, 'set2']]){
  const d = path.join(SAMPLES, dir), files = fs.readdirSync(d).filter(f => /\.wav$/i.test(f)).sort();
  const recs = files.map(f => { const w = readWav(path.join(d, f)); return {mono:w.d, rate:w.sr}; });
  const k = KIT.buildKit(files, recs);
  for (const L of k.layers) layers.push(Object.assign(L, {kit, file:dir + '/' + files[L.i]}));
  left.push(...k.left.map(i => dir + '/' + files[i]));
}
layers.sort((a, b) => a.g - b.g || a.kit - b.kit);
const out = burn(layers);
if (process.argv.includes('--check')){
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT) : Buffer.alloc(0);
  if (!old.equals(out)){ console.error('linn/rom/linn.rom is out of date: run node linn/tools/make-rom.mjs'); process.exit(1); }
  console.log('ok   ROM up to date');
} else if (process.argv[1] && process.argv[1].endsWith('make-rom.mjs')){
  fs.mkdirSync(path.dirname(OUT), {recursive:true}); fs.writeFileSync(OUT, out);
  console.log(`wrote ${OUT}: ${out.length} bytes`);
  const DN = ['', ' soft', ' mid', ' hard'];
  D.GROUPS.forEach((n, g) => { const ls = layers.filter(L => L.g === g);
    console.log(`  ${n.padEnd(10)} ${ls.length ? ls.map(L => `${L.file}${DN[L.dyn + 1]} (${(L.data.length / L.rate).toFixed(3)}s, ${(L.clk / 1000).toFixed(1)}k)`).join(', ') : 'EMPTY (no recording)'}`); });
  if (left.length) console.log('  left out:', left.join(', '));
}
