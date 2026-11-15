// The plugin's circuit model (plugins/dmx/Circuit.h) against the app's (dmx/circuit.js): every kit voice at three tunings and with three sets of voice mods.
// usage: node dmx/tools/compare.mjs <build>/dmx_circuit_dump <dir for the renders> [fs]
import { createRequire } from 'module';
import { execFileSync } from 'child_process';
import { readFileSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
const here = dirname(fileURLToPath(import.meta.url)), require = createRequire(import.meta.url);
const C = require(join(here, '..', 'circuit.js')), K = require(join(here, '..', 'kits.js'));
const [bin, out, fsArg] = process.argv.slice(2), fs = +(fsArg || 48000), kits = join(here, '..', 'kits');
mkdirSync(out, {recursive:true});
console.log(execFileSync(bin, [kits, out, String(fs)]).toString().trim());
let worst = 0, n = 0, bad = 0;
for (const kit of K.DMX_KITS) kit.voices.forEach((v, vi) => {
  const s = C.parseWav(readFileSync(join(kits, kit.id, v[0] + '.wav'))), [, cn, mode] = C.cardFor(kit, vi), card = C.CARDS[cn], rom = C.makeRom(s.x, s.sr, card);
  const MODS = [undefined, {cap:2, dmod:1, early:2, filt:3, dac:1, rev:true}, {cap:.5, dmod:2, early:1, filt:1, dac:2, rev:false}, {cap:3, dmod:0, early:0, filt:2, dac:0, rev:false}];
  for (const tune of [50, 20, 85]) for (let mi = 0; mi < MODS.length; mi++){
    if (mi && tune !== 50) continue;
    const js = C.render(rom, card, mode, tune, fs, MODS[mi]), b = readFileSync(join(out, `${kit.id}_${vi}_${tune}_${mi}.f32`)), cpp = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
    let d = js.length === cpp.length ? 0 : Infinity; for (let i = 0; i < Math.min(js.length, cpp.length); i++) d = Math.max(d, Math.abs(js[i] - cpp[i]));
    n++; worst = Math.max(worst, d); if (d > 1e-5){ bad++; console.log(`DIFFERENT ${kit.id} ${v[1]} tune ${tune} mods ${mi}: ${d} (lengths ${js.length} / ${cpp.length})`); }
  }
});
console.log(`${n} renders, largest difference ${worst.toExponential(2)}${bad ? `, ${bad} DIFFERENT` : ': the same'}`);
process.exit(bad ? 1 : 0);
