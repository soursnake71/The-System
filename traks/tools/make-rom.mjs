// Burns the Omega Traks sound ROM (traks/rom/traks.rom): the 13 voices of the machine, each stored the way the
// Drumtraks stores its sounds: 8-bit companded codes (mu-law segment coding, the scheme of the companding DACs of the
// period), read out by a sample clock that the TUNING control varies.
// The sounds are the owner's Drumtraks recordings in traks/samples/<name>.wav (16-bit mono, 15625 Hz; the ride resampled from its 44.1 kHz recording to match), stored at their
// own rate. Never synthesized: a sound with no recording is left EMPTY (silent) until one is added.
// Your own sounds (or EPROM dumps) can be loaded onto any voice in the app and the plugin.
// usage: node traks/tools/make-rom.mjs            (writes traks/rom/traks.rom)
//        node traks/tools/make-rom.mjs --check    (fails if the file is out of date)
import fs from 'fs';
import path from 'path';
import {fileURLToPath} from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(here, '..', 'rom', 'traks.rom');
const EMPTY_RATE = 15625;            // the clock written for an empty sound

const ORDER = ['bass', 'snare', 'rim', 'tom1', 'tom2', 'crash', 'ride', 'closed', 'open', 'claps', 'tamb', 'cowbell', 'cabasa'];

// mu-law (the segment code: sign, 3-bit chord, 4-bit step), from 16-bit linear
function encode(x){
  let pcm = Math.max(-32767, Math.min(32767, Math.round(x * 32767)));
  const BIAS = 0x84, CLIP = 32635;
  const sign = pcm < 0 ? 0x80 : 0; if (pcm < 0) pcm = -pcm; if (pcm > CLIP) pcm = CLIP; pcm += BIAS;
  let exp = 7; for (let m = 0x4000; (pcm & m) === 0 && exp > 0; m >>= 1) exp--;
  const man = (pcm >> (exp + 3)) & 0x0F;
  return (~(sign | (exp << 4) | man)) & 0xFF;
}
// a recording: 16-bit PCM WAV, mono (channels are mixed)
function readWav(file){
  const b = fs.readFileSync(file); let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length){ const id = b.toString('ascii', o, o + 4), n = b.readUInt32LE(o + 4);
    if (id === 'fmt ') fmt = {ch:b.readUInt16LE(o + 10), sr:b.readUInt32LE(o + 12), bits:b.readUInt16LE(o + 22)};
    else if (id === 'data') data = b.subarray(o + 8, o + 8 + n);
    o += 8 + n + (n & 1); }
  if (!fmt || !data || fmt.bits !== 16) throw new Error(file + ': need a 16-bit PCM WAV');
  const n = Math.floor(data.length / 2 / fmt.ch), d = new Float64Array(n);
  for (let i = 0; i < n; i++){ let x = 0; for (let c = 0; c < fmt.ch; c++) x += data.readInt16LE((i * fmt.ch + c) * 2); d[i] = x / fmt.ch / 32768; }
  return {d, sr:fmt.sr};
}
const SAMPLES = path.join(here, '..', 'samples');
const parts = [];
const head = Buffer.alloc(8 + ORDER.length * 8);
head.write('OTRK', 0, 'ascii'); head.writeUInt8(1, 4); head.writeUInt8(ORDER.length, 5);
ORDER.forEach((k, i) => {
  const f = path.join(SAMPLES, k + '.wav'), rec = fs.existsSync(f) ? readWav(f) : null;
  const d = rec ? rec.d : new Float64Array(2), rate = rec ? rec.sr : EMPTY_RATE; let pk = 1e-9; for (const x of d) pk = Math.max(pk, Math.abs(x));
  // trim the silent tail (below the code's smallest step)
  let end = d.length; while (rec && end > 16 && Math.abs(d[end - 1]) / pk < 0.0008) end--;
  const codes = Buffer.alloc(end); for (let j = 0; j < end; j++) codes[j] = encode(d[j] / pk * 0.97);
  head.writeUInt32LE(rate, 8 + i * 8); head.writeUInt32LE(end, 12 + i * 8);
  parts.push(codes);
});
const out = Buffer.concat([head, ...parts]);
if (process.argv.includes('--check')){
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT) : Buffer.alloc(0);
  if (!old.equals(out)){ console.error('traks/rom/traks.rom is out of date: run node traks/tools/make-rom.mjs'); process.exit(1); }
  console.log('ROM up to date');
} else {
  fs.mkdirSync(path.dirname(OUT), {recursive:true}); fs.writeFileSync(OUT, out);
  console.log(`wrote ${OUT}: ${out.length} bytes`, ORDER.map((k, i) => `${k} ${(parts[i].length / head.readUInt32LE(8 + i * 8)).toFixed(2)}s${fs.existsSync(path.join(SAMPLES, k + '.wav')) ? '' : ' EMPTY (no recording)'}`).join(', '));
}
