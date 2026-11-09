// Renders each voice of engine.js alone and compares it with the recordings in dr110/samples:
// envelope (2 ms average of |x|, normalised to the peak) at fixed times, band energies, kick zero crossings.
// usage: node dr110/tools/fit.mjs
import {createRequire} from 'module';
import fs from 'fs';
const D = createRequire(import.meta.url)('../engine.js');
const dir = new URL('../samples/', import.meta.url).pathname;
function wav(f){ const b = fs.readFileSync(f); let o = 12, data, sr = 44100;
  while (o < b.length){ const id = b.toString('ascii', o, o + 4), n = b.readUInt32LE(o + 4);
    if (id === 'fmt ') sr = b.readUInt32LE(o + 12); if (id === 'data'){ data = new Int16Array(b.buffer.slice(b.byteOffset + o + 8, b.byteOffset + o + 8 + n)); break; } o += 8 + n + (n & 1); }
  return {x:Float64Array.from(data, v => v / 32768), sr}; }
function render(v, sec, acc){ const sr = 44100, e = new D.Engine(sr), p = D.defaults(); p[D.P.rvLevel] = 0; p[D.P.volume] = 10 / Math.sqrt(2); e.setAll(p);
  const n = Math.round(sr * sec), L = new Float64Array(n), R = new Float64Array(n); e.trigger(v, acc); e.process(L, R, 0, n); return {x:L, sr}; }
function env(x, sr, ms){ const k = Math.round(sr * .002), out = []; let pk = 0; const a = new Float64Array(x.length);
  let s = 0; for (let i = 0; i < x.length; i++){ s += Math.abs(x[i]); if (i >= k) s -= Math.abs(x[i - k]); a[i] = s / k; pk = Math.max(pk, a[i]); }
  for (const t of ms) out.push((a[Math.min(x.length - 1, Math.round(t * sr / 1000))] / pk).toFixed(2)); return out.join(' '); }
function bands(x, sr){ // crude band energy with biquad band filters
  const edges = [[20, 600], [600, 2000], [2000, 5000], [5000, 9000], [9000, 20000]], tot = []; let sum = 0;
  for (const [lo, hi] of edges){ const f = Math.sqrt(lo * hi), q = f / (hi - lo); const w = 2 * Math.PI * f / sr, al = Math.sin(w) / (2 * q), a0 = 1 + al;
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0; for (const v of x){ const y = (al * v - al * x2 + 2 * Math.cos(w) * y1 - (1 - al) * y2) / a0; x2 = x1; x1 = v; y2 = y1; y1 = y; e += y * y; } tot.push(e); sum += e; }
  return tot.map(e => (e / sum).toFixed(2)).join(' '); }
const T = {BD:[2, 10, 20, 30, 40, 60, 80, 100], SD:[2, 8, 16, 24, 32, 48, 64, 96], CH:[5, 10, 20, 30, 40, 50, 70, 90], OH:[40, 120, 200, 320, 440, 560, 680, 800], CY:[20, 120, 240, 360, 480, 600, 800, 1000], HC:[4, 10, 16, 26, 38, 60, 110, 200]};
const files = {BD:'bd', SD:'sd', OH:'oh', CH:'ch', CY:'cy', HC:'hc'};
for (const [k, f] of Object.entries(files)){
  const v = D.VOICES.indexOf(k), s = wav(dir + f + '.wav'), m = render(v, s.x.length / s.sr, false);
  // align: start at first sample over 2% of peak
  const al = x => { let pk = 0; for (const q of x) pk = Math.max(pk, Math.abs(q)); let i = 0; while (Math.abs(x[i]) < pk * .02) i++; return x.subarray(Math.max(0, i - 20)); };
  const a = al(s.x), b = al(m.x);
  console.log(k.padEnd(3), 'rec  env', env(a, s.sr, T[k]), '| bands', bands(a, s.sr));
  console.log('   ', 'model env', env(b, m.sr, T[k]), '| bands', bands(b, m.sr));
  if (k === 'BD'){ const zc = x => { const o = []; for (let i = 1; i < x.length && o.length < 7; i++) if (x[i - 1] < 0 && x[i] >= 0) o.push((i / 44100 * 1000).toFixed(1)); return o.join(' '); };
    console.log('    zero-up rec', zc(a), '| model', zc(b)); }
}
