// Renders the same hits with engine.js and with the C++ port (plugins/dr110/DR110.h, built as dr110_test) and
// compares them sample by sample. usage: node dr110/tools/compare.mjs path/to/dr110_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import {execFileSync} from 'child_process';
const D = createRequire(import.meta.url)('../engine.js');
const [bin, dir] = process.argv.slice(2);
const cases = [
  ['defaults, every voice', {}, 48000],
  ['44.1 kHz, accent 10, balance -3, plate', {accent:10, balance:-3, rvProg:3, rvTime:1.2}, 44100],
  ['96 kHz, tuned and decayed, room, pans', {BDtune:-5, SDtune:4, OHdec:2, CHdec:0.5, CYtune:3, HCdec:1.6, KNtune:-7, BDpan:-1, OHpan:0.6, rvProg:1, rvDelay:120}, 96000],
  ['mutes, sends, early ref, balance +5', {SDmute:1, KNsnd:1, HCsnd:1, rvProg:4, balance:5, volume:10}, 48000],
  ['vocal, high 0.1, diffusion 2, cut 2k', {rvProg:2, rvHigh:0.1, rvDiff:2, rvCut:2000, rvTime:8, rvLevel:1}, 48000],
  ['voice mod: snaps, boosts, filters, noise hats, sweep on kick, batteries', {bdSnap:1, bdBoost:1, bdBass:1, sdSnap:0.1, sdBoost:1, sdMix:0.8, hhFilter:0.2, hhMix:0.5, cyFilter:0.9, hcFilter:0.1, hcBoost:1, swAmount:1, swDecay:0.7, supply:1}, 48000],
  ['voice mod: sweep on closed hat decay, snare all tone', {swTarget:1, swAmount:0.8, sdMix:0, bdSnap:0, accent:8}, 44100],
  ['Omega 110: pad EQ, ADSR, drive, delay sends, master EQ, overdrive, bass extender', {BDlsG:6, BDlsF:60, SDpkG:-5, SDpkF:900, SDpkQ:3, OHhsG:4, CHhsF:9000, CHhsG:-6,
    SDatk:0.01, SDsus:0.3, SDrel:0.2, HCdcy:0.1, HCsus:0.5, HCrel:0.5, OHatk:0.002, OHrel:1, KNdrv:0.6, BDdrv:0.3, SDdls:0.5, HCdls:0.8, KNdls:0.4,
    mLsG:3, mP1G:-4, mP1Q:2, mP2G:5, mP2F:4000, mHsG:-3, odDrive:0.4, odMix:0.7, odOut:-3, bdExtend:0.8, bdEnv:1, dlFb:0.7, dlModDepth:0.8, dlDuck:0.9}, 48000],
  ['Omega 110: tempo-synced delay without ping-pong, hard clip, tube at 44.1k', {dlSync:8, tempo:96, dlPing:0, dlLevel:0.9, OHdls:1, CHdls:0.5, odDrive:0.8, odMode:1, BDatk:0.05, BDdcy:0.5, BDsus:0, BDrel:0.1}, 44100],
  ['Omega 110: tube overdrive, extender at low pitch', {odDrive:0.5, odMode:2, bdExtend:1, BDtune:-9, BDdec:2, dlSync:3, SDdls:1}, 48000],
];
let fail = 0;
for (const [name, over, sr] of cases){
  const p = D.defaults(); for (const [k, v] of Object.entries(over)) p[D.P[k]] = v;
  const secs = 3, n = Math.round(sr * secs), hits = [];
  // a beat: every voice, with accents, a closed hat choking an open hat
  for (let s = 0; s < 24; s++){ const at = Math.round(s * sr * 0.11); const pat = [[0, 1], [3, 0], [2, 0], [1, 1], [5, 0], [4, 0], [6, 1], [3, 1]][s % 8]; hits.push([at, pat[0], pat[1]]); if (s % 3 === 0) hits.push([at, 0, 0]); if (s % 5 === 2) hits.push([at, 7, 0]); hits.push([at + Math.round(sr * 0.05), 8 + pat[0], 0]); }
  hits.sort((a, b) => a[0] - b[0]);
  fs.writeFileSync(dir + '/dr.p', p.join('\n')); fs.writeFileSync(dir + '/dr.h', hits.map(h => h.join(' ')).join('\n'));
  execFileSync(bin, [dir + '/dr.p', dir + '/dr.h', String(sr), String(secs), dir + '/dr.out']);
  const b = fs.readFileSync(dir + '/dr.out'), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  const e = new D.Engine(sr); e.setAll(p);
  const L = new Float64Array(n), R = new Float64Array(n), outs = Array.from({length:8}, () => new Float64Array(n));
  let pos = 0, h = 0;
  while (pos < n){
    while (h < hits.length && hits[h][0] <= pos){ if (hits[h][1] === 7) e.sweep(); else if (hits[h][1] >= 8) e.release(hits[h][1] - 8); else e.trigger(hits[h][1], !!hits[h][2]); h++; }
    let end = h < hits.length ? Math.min(n, hits[h][0]) : n; end = Math.min(end, pos + 128);
    e.process(L, R, pos, end - pos, outs); pos = end;
  }
  let err = 0, pk = 0;
  for (let i = 0; i < n; i++){ err = Math.max(err, Math.abs(f[i] - L[i]), Math.abs(f[n + i] - R[i])); pk = Math.max(pk, Math.abs(L[i]));
    for (let k = 0; k < 8; k++) err = Math.max(err, Math.abs(f[(2 + k) * n + i] - outs[k][i])); }
  const ok = f.length === 10 * n && err < 1e-4 && pk > 0.05;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: max diff ${err.toExponential(2)} (peak ${pk.toFixed(3)})`);
}
process.exitCode = fail ? 1 : 0;
