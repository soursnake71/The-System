// Omega Five reference render: node five/tools/render.mjs <out> [scenario] [--wav]
//   Every engine first loads the factory kit (five/kit/FACTORY_MAP[v], 25 kHz 16-bit WAVs) with setUserSample(v, data, 25000, {rec:true}).
//   scenario 'kit' (default): Engine at 48 kHz, default state, every voice 0..23 once, 0.25 s apart, mixed velocities and
//     accents, REV program HALL1 on a few channels, TC delay on; 7 s. Output: raw float32 interleaved L/R (or WAV).
//   scenario 'voices': every voice's sample after loading (engine.userSample(v): the kit data resampled to ROM_SR, before REC_EQ),
//     concatenated with 0.1 s gaps, L = R, at ROM_SR (the WAV header says 25000 Hz).
// plugins/five/test.cpp renders the same scenarios; tools/compare.mjs checks them against each other.
import {writeFileSync} from 'node:fs';
import {Engine, FACTORY_MAP, ROM_SR} from '../engine.js';
import {loadKit} from './wav.mjs';

const args = process.argv.slice(2), wav = args.includes('--wav'), pos = args.filter(a => !a.startsWith('--'));
const out = pos[0], scenario = pos[1] || 'kit';
if (!out){ console.error('usage: node five/tools/render.mjs <out> [kit|voices] [--wav]'); process.exit(2); }

export function renderKit(sr = 48000, secs = 7, block = 128){
  const e = loadKit(new Engine(sr), FACTORY_MAP), n = Math.round(secs * sr), L = new Float32Array(n), R = new Float32Array(n);
  e.setFx('rev', 'program', 'HALL1'); e.setFx('dly', 'on', 1);
  e.setChannel(1, {sendRev:.4, sendDly:.2}); e.setChannel(2, {sendRev:.3}); e.setChannel(9, {sendRev:.25}); e.setChannel(10, {sendRev:.3, sendDly:.35});
  const hits = [];
  for (let v = 0; v < 24; v++) hits.push({at:Math.round(v * .25 * sr), v, vel:127 - (v * 17) % 60, acc:v % 3});
  const bl = new Float32Array(block), br = new Float32Array(block);
  for (let s = 0; s < n; s += block){
    const m = Math.min(block, n - s);
    for (const h of hits) if (h.at >= s && h.at < s + m) e.trigger(h.v, h.vel, h.acc, h.at - s);
    e.process(bl, br, m);
    L.set(bl.subarray(0, m), s); R.set(br.subarray(0, m), s);
  }
  return {L, R, sr};
}
export function renderVoices(){
  const e = loadKit(new Engine(ROM_SR), FACTORY_MAP), parts = [], gap = Math.round(.1 * ROM_SR);
  let n = 0;
  for (let v = 0; v < 24; v++){ const x = e.userSample(v); parts.push(x); n += x.length + gap; }
  const L = new Float32Array(n);
  let o = 0; for (const x of parts){ L.set(x, o); o += x.length + gap; }
  return {L, R:L, sr:ROM_SR};
}

const r = scenario === 'voices' ? renderVoices() : renderKit();
const n = r.L.length, inter = new Float32Array(n * 2);
for (let i = 0; i < n; i++){ inter[2 * i] = r.L[i]; inter[2 * i + 1] = r.R[i]; }
let body = Buffer.from(inter.buffer);
if (wav){
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + body.length, 4); h.write('WAVE', 8); h.write('fmt ', 12); h.writeUInt32LE(16, 16);
  h.writeUInt16LE(3, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(r.sr, 24); h.writeUInt32LE(r.sr * 8, 28); h.writeUInt16LE(8, 32); h.writeUInt16LE(32, 34);
  h.write('data', 36); h.writeUInt32LE(body.length, 40);
  body = Buffer.concat([h, body]);
}
writeFileSync(out, body);
let pk = 0, bad = 0; for (const x of inter){ if (!Number.isFinite(x)) bad++; else if (Math.abs(x) > pk) pk = Math.abs(x); }
console.log(`${scenario}: ${n} frames @ ${r.sr} Hz, peak ${(20 * Math.log10(pk)).toFixed(2)} dBFS, non-finite ${bad} -> ${out}`);
