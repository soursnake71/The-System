// Tiny WAV reader for the Node tools: PCM 16/24-bit or float32, any channel count (channel 0 is returned).
//   readWav(path) -> {data: Float32Array (-1..1), sr}
//   loadKit(engine[, dir]) loads five/kit/FACTORY_MAP[v] into every voice with {rec:true} (the factory recordings).
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

export function readWav(file){
  const b = readFileSync(file);
  if (b.toString('latin1', 0, 4) !== 'RIFF' || b.toString('latin1', 8, 12) !== 'WAVE') throw new Error(file + ': not a WAV');
  let i = 12, fmt = null, data = null;
  while (i + 8 <= b.length){
    const id = b.toString('latin1', i, i + 4), n = b.readUInt32LE(i + 4);
    if (id === 'fmt ') fmt = {tag:b.readUInt16LE(i + 8), ch:b.readUInt16LE(i + 10), sr:b.readUInt32LE(i + 12), bits:b.readUInt16LE(i + 22)};
    else if (id === 'data'){ data = b.subarray(i + 8, Math.min(b.length, i + 8 + n)); break; }
    i += 8 + n + (n & 1);
  }
  if (!fmt || !data) throw new Error(file + ': no fmt/data chunk');
  const bps = fmt.bits / 8, fr = bps * fmt.ch, n = Math.floor(data.length / fr), out = new Float32Array(n);
  for (let k = 0; k < n; k++){
    const o = k * fr;
    out[k] = fmt.tag === 3 && fmt.bits === 32 ? data.readFloatLE(o)
      : fmt.bits === 16 ? data.readInt16LE(o) / 32768
      : fmt.bits === 24 ? data.readIntLE(o, 3) / 8388608
      : fmt.bits === 32 ? data.readInt32LE(o) / 2147483648 : (data[o] - 128) / 128;
  }
  return {data:out, sr:fmt.sr};
}

export const KIT_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'kit');
export function loadKit(engine, map, dir = KIT_DIR){
  map.forEach((f, v) => { const w = readWav(path.join(dir, f)); engine.setUserSample(v, w.data, w.sr, {rec:true}); });
  return engine;
}
