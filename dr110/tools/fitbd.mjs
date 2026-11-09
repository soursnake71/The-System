// grid search for the bass drum constants (pulse, sweep, pitch tau, ring tau) against samples/bd.wav
import {createRequire} from 'module';
import fs from 'fs';
const D = createRequire(import.meta.url)('../engine.js');
const b = fs.readFileSync(new URL('../samples/bd.wav', import.meta.url).pathname);
let o = 12, rec; while (o < b.length){ const id = b.toString('ascii', o, o + 4), n = b.readUInt32LE(o + 4); if (id === 'data'){ rec = Float64Array.from(new Int16Array(b.buffer.slice(b.byteOffset + o + 8, b.byteOffset + o + 8 + n)), v => v / 32768); break; } o += 8 + n + (n & 1); }
const start = x => { let pk = 0; for (const q of x) pk = Math.max(pk, Math.abs(q)); let i = 0; while (Math.abs(x[i]) < pk * .02) i++; return i; };
const N = 4410, r0 = start(rec);
const norm = x => { let pk = 0; for (const q of x) pk = Math.max(pk, Math.abs(q)); return x.map(q => q / pk); };
let best = [1e9];
for (const pu of [.003, .004, .005, .006, .008]) for (const sw of [2, 2.5, 3, 3.5, 4, 5]) for (const pt of [.004, .0055, .0065, .008]) for (const ta of [.026, .03, .034, .04]){
  D.setBd(pu, sw, pt, ta);
  const e = new D.Engine(44100), p = D.defaults(); p[D.P.rvLevel] = 0; e.setAll(p);
  const L = new Float64Array(N + 2000), R = new Float64Array(N + 2000); e.trigger(0, false); e.process(L, R, 0, L.length);
  const m0 = start(L), a = norm(rec.slice(r0, r0 + N)), m = norm(L.slice(m0, m0 + N));
  // best of small alignment shifts, waveform error
  let err = 1e9; for (const sg of [1, -1]) for (let sh = -60; sh <= 60; sh += 2){ let s = 0; for (let i = 60; i < N - 60; i++){ const d = a[i] - sg * (m[i + sh] || 0); s += d * d; } err = Math.min(err, s); }
  if (err < best[0]) best = [err, pu, sw, pt, ta];
}
console.log('best', best);
