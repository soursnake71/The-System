// The plugin's circuit model (plugins/dmx/Circuit.h) against the app's (dmx/circuit.js): every kit voice at three tunings and with three sets of voice mods.
// usage: node dmx/tools/compare.mjs <build>/dmx_circuit_dump <dir for the renders> [fs] [<build>/dmx_mixer_dump]
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

// the engine (plugins/dmx/Mixer.h against dmx/engine.js): the same hits through presets, kits, circuit on and off
const mixBin = process.argv[5];
if (mixBin){
  const E = require(join(here, '..', 'engine.js')), PR = require(join(here, '..', 'presets.js'));
  console.log(execFileSync(mixBin, [kits, out]).toString().trim());
  const cases = [[1, 1, -1], [1, 1, 0], [1, 1, 37], [1, 1, 75], [1, 1, 85], [0, 0, 95], [2, 1, 33]];
  const hits = [[0, 0, 1000, 6000], [0, 7, 720, 6000], [3000, 8, 1000, 6000], [6000, 7, 720, 6000], [12000, 2, 1000, 6000], [12000, 99, 1, 0],
    [18000, 4, 720, 6000], [24000, 0, 720, -1], [30000, 98, 0, 0], [36000, 9, 1000, 6000], [42000, 14, 1000, 6000], [48000, 5, 1000, 3000], [60000, 12, 1000, 6000], [66001, 3, 720, 6000]];
  let worstM = 0;
  cases.forEach(([ki, circ, pre], ci) => {
    const kit = K.DMX_KITS[ki], M = new E.Engine(fs), P = pre < 0 ? E.defaults() : PR.PRESETS[pre].p;
    M.setAll(P); M.setTempo(120); M.setCircuit(!!circ); M.setFaders([.8, .8, .8, .8, .8, .8, .8, .5, .85]);
    kit.voices.forEach((v, vi) => {
      const s = C.parseWav(readFileSync(join(kits, kit.id, v[0] + '.wav'))), [, cn, mode] = C.cardFor(kit, vi), card = C.CARDS[cn], c = E.CARDS.indexOf(cn), m = E.mods(P, c);
      const tune = Math.min(100, Math.max(0, 50 + m.tune)), group = E.CARDS.indexOf(cn) >= 0 ? ['bass', 'snare', 'hat', 'toms', 'cym', 'perc1', 'perc2'].indexOf(v[2]) : 0;
      if (circ){ const d = C.render(C.makeRom(s.x, s.sr, card), card, mode, tune, fs, m), [pl, pr] = C.pan(card); M.setSound(vi, d, fs, c, group, K.dmxIsOpen(v), pl, pr, 1); }
      else { const d = Float32Array.from(s.x); if (m.rev) d.reverse(); M.setSound(vi, d, s.sr, c, group, K.dmxIsOpen(v), 1, 1, C.rate(card, mode, tune) / C.rate(card, mode, 50)); }
    });
    const N = fs * 3, L = new Float64Array(128), R = new Float64Array(128), b = readFileSync(join(out, `mix_${ci}.f32`)), cpp = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
    let h = 0, d = 0, pk = 0;
    for (let f0 = 0; f0 < N; f0 += 128){
      const n = Math.min(128, N - f0);
      while (h < hits.length && hits[h][0] < f0 + n){ const x = hits[h], off = Math.max(0, x[0] - f0);
        if (x[1] === 99) M.click(true, off); else if (x[1] === 98) M.release(0); else if (x[1] < kit.voices.length) M.trigger(x[1], x[2] / 1000, off, x[3]); h++; }
      M.process(L, R, n);
      for (let i = 0; i < n; i++){ const j = (f0 + i) * 2; d = Math.max(d, Math.abs(Math.fround(L[i]) - cpp[j]), Math.abs(Math.fround(R[i]) - cpp[j + 1])); pk = Math.max(pk, Math.abs(L[i])); }
    }
    worstM = Math.max(worstM, d); if (d > 1e-5){ bad++; console.log(`DIFFERENT mix ${ci} (${kit.id}, ${pre < 0 ? 'defaults' : PR.PRESETS[pre].name}): ${d}`); }
    else console.log(`mix ${ci} ${kit.id} ${circ ? 'circuit' : 'clean'} ${pre < 0 ? 'defaults' : PR.PRESETS[pre].name}: peak ${pk.toFixed(3)}, largest difference ${d.toExponential(2)}`);
  });
  // the dice: the same seed gives the same sound in the app and the plugin
  let diceBad = 0;
  PR.DICE_PARTS.forEach((part, pi) => { for (const seed of [12345, 4000000001]){
    const js = PR.dice(PR.PRESETS[12].p, part, seed), b = readFileSync(join(out, `dice_${pi}_${seed}.f64`)), cpp = new Float64Array(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
    let d = 0; for (let i = 0; i < js.length; i++) d = Math.max(d, Math.abs(js[i] - cpp[i]));
    if (d > 1e-9){ diceBad++; bad++; console.log(`DIFFERENT dice ${part} seed ${seed}: ${d}`); }
  } });
  console.log(`dice: ${PR.DICE_PARTS.length * 2} rolls${diceBad ? `, ${diceBad} DIFFERENT` : ', the same'}`);
}
process.exit(bad ? 1 : 0);
