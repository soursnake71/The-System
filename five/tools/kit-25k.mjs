// Builds five/kit/*.wav at the RX5's native 25 kHz from the 48 kHz recordings (24-bit mono WAVs in <srcDir>).
// The hardware's samples are 25 kHz PCM (see RX5-MIDI-NOTES.md §6.1), and its output filter already removed
// everything above ~12 kHz, so a steep low-pass at 11.8 kHz (passband) / 12.5 kHz (Nyquist) loses nothing real.
// Kaiser-windowed sinc, beta 9 (about -90 dB stopband), 64 zero crossings each side.
// Usage: node five/tools/kit-25k.mjs <srcDir> [outDir=five/kit]
import fs from 'node:fs';
import path from 'node:path';

const SR_OUT = 25000, ZC = 64, BETA = 9, FC = 12150; // cutoff between 11.8 kHz passband edge and 12.5 kHz

function readWav(buf) {
  let p = 12, fmt = null, data = null;
  while (p + 8 <= buf.length) {
    const id = buf.toString('ascii', p, p + 4), n = buf.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = { ch: buf.readUInt16LE(p + 10), sr: buf.readUInt32LE(p + 12), bits: buf.readUInt16LE(p + 22), tag: buf.readUInt16LE(p + 8) };
    if (id === 'data') data = buf.subarray(p + 8, p + 8 + n);
    p += 8 + n + (n & 1);
  }
  const bps = fmt.bits / 8, frames = Math.floor(data.length / (bps * fmt.ch)), x = new Float64Array(frames);
  for (let i = 0; i < frames; i++) {
    let s = 0;
    for (let c = 0; c < fmt.ch; c++) {
      const o = (i * fmt.ch + c) * bps;
      s += fmt.tag === 3 ? data.readFloatLE(o) : bps === 3 ? data.readIntLE(o, 3) / 8388608 : bps === 2 ? data.readInt16LE(o) / 32768 : bps === 4 ? data.readInt32LE(o) / 2147483648 : (data[o] - 128) / 128;
    }
    x[i] = s / fmt.ch;
  }
  return { sr: fmt.sr, x };
}

function bessel0(x) { let s = 1, t = 1; for (let k = 1; k < 50; k++) { t *= (x / (2 * k)) ** 2; s += t; } return s; }

function resample(x, srIn) {
  const ratio = SR_OUT / srIn, n = Math.floor(x.length * ratio), y = new Float64Array(n);
  const fc = FC / srIn, half = ZC / (2 * fc) / 2;                // kernel half-width in input samples
  const i0b = bessel0(BETA);
  for (let j = 0; j < n; j++) {
    const t = j / ratio, lo = Math.ceil(t - half), hi = Math.floor(t + half);
    let s = 0;
    for (let i = Math.max(0, lo); i <= Math.min(x.length - 1, hi); i++) {
      const d = i - t, u = d / half;
      const w = bessel0(BETA * Math.sqrt(Math.max(0, 1 - u * u))) / i0b;
      const sinc = d === 0 ? 1 : Math.sin(2 * Math.PI * fc * d) / (2 * Math.PI * fc * d);
      s += x[i] * 2 * fc * sinc * w;
    }
    y[j] = s;
  }
  return y;
}

function writeWav16(file, y, sr) {
  const b = Buffer.alloc(44 + y.length * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + y.length * 2, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(sr, 24);
  b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(y.length * 2, 40);
  // TPDF dither to 16 bits (the source is 24-bit)
  let s = 0x1234567;
  const r = () => { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return (s >>> 0) / 4294967296; };
  for (let i = 0; i < y.length; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(y[i] * 32767 + r() - r()))), 44 + i * 2);
  fs.writeFileSync(file, b);
}

const [src, out = 'five/kit'] = process.argv.slice(2);
if (!src) { console.error('usage: node five/tools/kit-25k.mjs <srcDir> [outDir]'); process.exit(1); }
fs.mkdirSync(out, { recursive: true });
const files = fs.readdirSync(src).filter(f => /\.wav$/i.test(f)).sort();
for (const f of files) {
  const { sr, x } = readWav(fs.readFileSync(path.join(src, f)));
  const y = resample(x, sr);
  let pk = 0; for (const v of y) pk = Math.max(pk, Math.abs(v));
  writeWav16(path.join(out, f), y, SR_OUT);
  console.log(f, sr, '->', SR_OUT, (y.length / SR_OUT).toFixed(3) + ' s', 'peak', (20 * Math.log10(pk)).toFixed(1) + ' dBFS');
}
const kj = path.join(out, 'kit.json');
const kit = fs.existsSync(kj) ? JSON.parse(fs.readFileSync(kj, 'utf8')) : {};
Object.assign(kit, { sampleRate: SR_OUT, bits: 16, note: 'Resampled from 48 kHz recordings to the RX5 native 25 kHz (five/tools/kit-25k.mjs).', files });
fs.writeFileSync(kj, JSON.stringify(kit, null, 1));
