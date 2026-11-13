// Burns the Omega Traks sound ROM (traks/rom/traks.rom): the 13 voices of the machine, each stored the way the
// Drumtraks stores its sounds: 8-bit companded codes (mu-law segment coding, the scheme of the companding DACs of the
// period), read out by a sample clock that the TUNING control varies.
// The sounds are original: synthesized here, deterministically, and normalized. No factory samples are used.
// Your own sounds (or EPROM dumps) can be loaded onto any voice in the app and the plugin.
// usage: node traks/tools/make-rom.mjs            (writes traks/rom/traks.rom)
//        node traks/tools/make-rom.mjs --check    (fails if the file is out of date)
import fs from 'fs';
import path from 'path';
import {fileURLToPath} from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(here, '..', 'rom', 'traks.rom');
const SR = 25000;                    // the ROM's nominal sample clock (TUNING centre)
const TAU = 2 * Math.PI;

let seed = 0x1234567;
const noise = () => { let x = seed; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; seed = x; return x / 4294967296 * 2 - 1; };
const onePole = f => 1 - Math.exp(-TAU * f / SR);
class Biquad {      // RBJ band-pass (constant peak) / high-pass / low-pass
  constructor(type, f, q){ const w = TAU * f / SR, cw = Math.cos(w), al = Math.sin(w) / (2 * q); let b0, b1, b2;
    if (type === 'bp'){ b0 = al; b1 = 0; b2 = -al; } else if (type === 'hp'){ b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; } else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
    const a0 = 1 + al; this.b = [b0 / a0, b1 / a0, b2 / a0]; this.a = [-2 * cw / a0, (1 - al) / a0]; this.z1 = 0; this.z2 = 0; }
  tick(x){ const y = this.b[0] * x + this.z1; this.z1 = this.b[1] * x - this.a[0] * y + this.z2; this.z2 = this.b[2] * x - this.a[1] * y; return y; }
}
const sq = ph => (ph % 1) < 0.5 ? 1 : -1;
function render(sec, f){ const n = Math.round(sec * SR), d = new Float64Array(n); for (let i = 0; i < n; i++) d[i] = f(i / SR, i); return d; }
const att = (t, a) => t < a ? t / a : 1;

const V = {};
// BASS: a thumping sine-ish kick with a fast pitch drop and a beater click
V.bass = (() => { let ph = 0; const lp = new Biquad('lp', 3500, 0.7);
  return render(0.42, t => { const f = 50 + 120 * Math.exp(-t / 0.018) + 22 * Math.exp(-t / 0.1); ph += f / SR;
    const body = Math.sin(TAU * ph) * Math.exp(-t / 0.17) * att(t, 0.0006);
    const click = lp.tick(noise()) * Math.exp(-t / 0.0022) * 0.9;
    return Math.tanh(1.7 * (body + click)) / Math.tanh(1.7); }); })();
// SNARE: two shell modes with a little pitch drop, plus a band of snare-wire noise
V.snare = (() => { let p1 = 0, p2 = 0; const bp = new Biquad('bp', 4200, 0.6), hp = new Biquad('hp', 900, 0.7);
  return render(0.34, t => { const k = 1 + 0.12 * Math.exp(-t / 0.008); p1 += 182 * k / SR; p2 += 334 * k / SR;
    const tone = Math.sin(TAU * p1) * Math.exp(-t / 0.055) * 0.62 + Math.sin(TAU * p2) * Math.exp(-t / 0.035) * 0.38;
    const w = noise(), nz = (bp.tick(w) * 1.6 + hp.tick(w) * 0.45) * (Math.exp(-t / 0.115) + 0.7 * Math.exp(-t / 0.012));
    return Math.tanh(1.3 * (tone * 0.75 + nz * 0.8) * att(t, 0.0004)); }); })();
// RIM: a stick on the rim: a hard high ring and a woody low one
V.rim = (() => { const hp = new Biquad('hp', 2500, 0.7);
  return render(0.08, t => Math.tanh(2.2 * (Math.sin(TAU * 1720 * t) * Math.exp(-t / 0.011) * 0.8 + Math.sin(TAU * 470 * t) * Math.exp(-t / 0.018) * 0.55
    + hp.tick(noise()) * Math.exp(-t / 0.0016) * 0.7))); })();
// TOMS: a membrane with a pitch bend, a second mode and a stick attack
function tom(f0, len){ let p1 = 0, p2 = 0; const bp = new Biquad('bp', f0 * 6, 1.2);
  return render(len, t => { const k = 1 + 0.32 * Math.exp(-t / 0.045); p1 += f0 * k / SR; p2 += f0 * 1.52 * k / SR;
    return Math.tanh(1.4 * ((Math.sin(TAU * p1) * Math.exp(-t / 0.23) + Math.sin(TAU * p2) * Math.exp(-t / 0.06) * 0.28) * att(t, 0.0008)
      + bp.tick(noise()) * Math.exp(-t / 0.006) * 0.5)); }); }
V.tom1 = tom(168, 0.55); V.tom2 = tom(116, 0.7);
// metal: banks of square waves at inharmonic ratios (the ring of a cymbal), plus noise
function metal(freqs, len, f){
  const ph = freqs.map((_, i) => i * 0.173 % 1), hp1 = new Biquad('hp', f.hp, 0.7), hp2 = new Biquad('hp', f.hp, 0.7), bp = new Biquad('bp', f.bp, f.q);
  let lp = 0;
  return render(len, t => { let s = 0; for (let i = 0; i < freqs.length; i++){ ph[i] += freqs[i] / SR; s += sq(ph[i]); }
    s = s / freqs.length * (1 - f.noise) + noise() * f.noise;
    const h = hp2.tick(hp1.tick(s)) * 0.8 + bp.tick(s) * f.body;
    const cut = f.lp0 * Math.exp(-t / f.lpT) + f.lp1; lp += onePole(Math.min(cut, 11500)) * (h - lp);
    return lp * f.env(t); }); }
V.crash = metal([307.1, 419.6, 538.3, 651.2, 799.7, 1033.4, 1207.9, 1418.2], 2.2,
  {hp:3600, bp:5200, q:0.8, body:0.5, noise:0.45, lp0:6000, lpT:0.5, lp1:5500, env:t => att(t, 0.0015) * (Math.exp(-t / 0.62) * 0.8 + Math.exp(-t / 0.045) * 0.6)});
V.ride = (() => { const m = metal([254.3, 382.9, 497.1, 601.6, 742.4, 901.8], 1.8,
  {hp:5200, bp:7000, q:1.2, body:0.3, noise:0.25, lp0:2000, lpT:0.3, lp1:8500, env:t => att(t, 0.0008) * Math.exp(-t / 0.75) * 0.55});
  const bell = [[1290, 0.9, 0.5], [1736, 0.6, 0.4], [2342, 0.5, 0.35], [3151, 0.35, 0.3], [4405, 0.25, 0.22], [5870, 0.18, 0.18]];
  return m.map((x, i) => { const t = i / SR; let b = 0; for (const [f, a, d] of bell) b += Math.sin(TAU * f * t) * a * Math.exp(-t / d); return x + b * 0.32 * att(t, 0.0005); }); })();
const HAT = [331.7, 459.2, 567.4, 702.9, 829.3, 1087.1];
V.closed = metal(HAT, 0.13, {hp:7000, bp:9500, q:1.1, body:0.6, noise:0.5, lp0:0, lpT:1, lp1:11500, env:t => att(t, 0.0004) * Math.exp(-t / 0.032)});
V.open = metal(HAT, 0.75, {hp:6500, bp:9000, q:1.0, body:0.6, noise:0.5, lp0:2500, lpT:0.15, lp1:9000, env:t => att(t, 0.0008) * (Math.exp(-t / 0.3) * 0.85 + Math.exp(-t / 0.02) * 0.3)});
// CLAPS: a few hands slightly apart (noise bursts through a ~1.2 kHz band), then the room tail
V.claps = (() => { const bp = new Biquad('bp', 1180, 2.2), hp = new Biquad('hp', 700, 0.7); const hits = [0, 0.0095, 0.019, 0.0305];
  return render(0.36, t => { let e = 0; for (const h of hits) if (t >= h) e += Math.exp(-(t - h) / 0.0032) * (h === 0.0305 ? 0.6 : 1);
    if (t >= 0.0305) e += 0.55 * Math.exp(-(t - 0.0305) / 0.085);
    const w = noise(); return Math.tanh(1.5 * (bp.tick(w) * 2.2 + hp.tick(w) * 0.3) * e); }); })();
// TAMB: jingles: bright partials and high noise, shaken (two hits)
V.tamb = (() => { const hp = new Biquad('hp', 6000, 0.8); const parts = [[6230, 0.4], [7410, 0.35], [8890, 0.3], [10460, 0.25], [11200, 0.15]];
  return render(0.42, t => { let s = 0; for (const [f, a] of parts) s += Math.sin(TAU * f * t + f) * a;
    const e = att(t, 0.001) * (Math.exp(-t / 0.11) + (t > 0.022 ? 0.55 * Math.exp(-(t - 0.022) / 0.13) : 0));
    const am = 0.6 + 0.4 * Math.sin(TAU * 31 * t); return (s * 0.6 + hp.tick(noise()) * 0.9) * e * am; }); })();
// COWBELL: two inharmonic partials, squared up a little, through a band-pass; a fast strike and a ring
V.cowbell = (() => { const bp = new Biquad('bp', 1600, 0.9);
  return render(0.5, t => { const s = Math.tanh(2.5 * Math.sin(TAU * 562 * t)) * 0.55 + Math.tanh(2.5 * Math.sin(TAU * 837 * t)) * 0.45;
    return (bp.tick(s) * 1.8 + s * 0.25) * att(t, 0.0005) * (Math.exp(-t / 0.035) * 0.6 + Math.exp(-t / 0.2) * 0.5); }); })();
// CABASA: beads on metal: a high scrape of noise
V.cabasa = (() => { const hp = new Biquad('hp', 4800, 0.7), bp = new Biquad('bp', 8200, 1.4);
  return render(0.2, t => { const w = noise(); const e = att(t, 0.004) * Math.exp(-t / 0.045) + (t > 0.012 ? 0.3 * Math.exp(-(t - 0.012) / 0.03) : 0);
    return (hp.tick(w) * 0.6 + bp.tick(w) * 1.4) * e; }); })();

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
const parts = [];
const head = Buffer.alloc(8 + ORDER.length * 8);
head.write('OTRK', 0, 'ascii'); head.writeUInt8(1, 4); head.writeUInt8(ORDER.length, 5);
ORDER.forEach((k, i) => {
  const d = V[k]; let pk = 0; for (const x of d) pk = Math.max(pk, Math.abs(x));
  // trim the silent tail (below the code's smallest step)
  let end = d.length; while (end > 16 && Math.abs(d[end - 1]) / pk < 0.0008) end--;
  const codes = Buffer.alloc(end); for (let j = 0; j < end; j++) codes[j] = encode(d[j] / pk * 0.97);
  head.writeUInt32LE(SR, 8 + i * 8); head.writeUInt32LE(end, 12 + i * 8);
  parts.push(codes);
});
const out = Buffer.concat([head, ...parts]);
if (process.argv.includes('--check')){
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT) : Buffer.alloc(0);
  if (!old.equals(out)){ console.error('traks/rom/traks.rom is out of date: run node traks/tools/make-rom.mjs'); process.exit(1); }
  console.log('ROM up to date');
} else {
  fs.mkdirSync(path.dirname(OUT), {recursive:true}); fs.writeFileSync(OUT, out);
  console.log(`wrote ${OUT}: ${out.length} bytes`, ORDER.map((k, i) => `${k} ${(parts[i].length / SR).toFixed(2)}s`).join(', '));
}
