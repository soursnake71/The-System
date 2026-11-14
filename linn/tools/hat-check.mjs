// Checks the hi-hat circuit against the owner's closed-hat recordings (linn/tools/ref): plays HIHAT 2 at the HIHAT DECAY
// setting that matches each best and compares the envelopes (5 ms RMS) with the recordings. These recordings are a LinnDrum's
// closed hat, i.e. the open-hat ROM through the VCA: if the model is right, they match.
// usage: node linn/tools/hat-check.mjs
import fs from 'fs';
import {createRequire} from 'module';
import {readWav} from './make-rom.mjs';
const req = createRequire(import.meta.url);
const D = req('../engine.js');
const rom = D.parseRom(new Uint8Array(fs.readFileSync(new URL('../rom/linn.rom', import.meta.url))));
const sr = 44100, blk = 220;
const env = x => { const e = []; for (let i = 0; i + blk <= x.length; i += blk){ let s = 0; for (let j = i; j < i + blk; j++) s += x[j] * x[j]; e.push(Math.sqrt(s / blk)); } return e; };
function play(rec, kit, decay){
  const e = new D.Engine(sr); rom.forEach((L, g) => e.setGroup(g, L));
  const p = D.defaults(); p[D.P.hatDecay] = decay; p[D.P.kit] = kit; p[D.P.rvLevel] = 0; p[D.vp(3, 5)] = 0; e.setAll(p);
  e.trigger(7);
  const n = rec.length, L = new Float64Array(n), R = new Float64Array(n); e.process(L, R, 0, n);
  const a = env(rec), b = env(L), na = Math.max(...a), nb = Math.max(...b);
  let err = 0, k = 0; for (let i = 0; i < a.length; i++){ if (a[i] / na < 0.03 && b[i] / nb < 0.03) continue; err += (a[i] / na - b[i] / nb) ** 2; k++; }
  return {err:Math.sqrt(err / k), a:a.map(x => x / na), b:b.map(x => x / nb)};
}
let worst = 0;
// the HIHAT DECAY setting each recording was made at: the one the circuit matches best
for (const [file, kit] of [['hihat-closed-short.wav', 0], ['hihat-closed.wav', 0], ['hihat-closed-long.wav', 0], ['set2-closed-hat.wav', 1]]){
  const rec = readWav(new URL('ref/' + file, import.meta.url).pathname).d;
  let best = null; for (let dcy = 0; dcy <= 100; dcy += 1){ const r = play(rec, kit, dcy); if (!best || r.err < best.err) best = Object.assign(r, {dcy}); }
  worst = Math.max(worst, best.err);
  console.log(`SET ${kit + 1} ${file.padEnd(24)} HIHAT DECAY ${String(best.dcy).padStart(3)}%  envelope error ${(best.err * 100).toFixed(1)}%`);
  console.log(`   recording ${best.a.slice(0, 14).map(x => x.toFixed(2)).join(' ')}`);
  console.log(`   circuit   ${best.b.slice(0, 14).map(x => x.toFixed(2)).join(' ')}`);
}
console.log(worst < 0.12 ? 'ok   the hat circuit matches the recordings' : 'FAIL hat circuit off');
process.exitCode = worst < 0.12 ? 0 : 1;
