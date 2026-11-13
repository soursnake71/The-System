// Runs the same front-panel scripts through traks/machine.js + engine.js and through the C++ port
// (plugins/traks/Machine.h + Traks.h, built as traks_test) and compares the audio sample by sample, the displays after
// every key and the memory at the end. usage: node traks/tools/compare.mjs path/to/traks_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import path from 'path';
import {execFileSync} from 'child_process';
import {fileURLToPath} from 'url';
const req = createRequire(import.meta.url);
const D = req('../engine.js'), M = req('../machine.js');
const ROM = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'rom', 'traks.rom');
const rom = D.parseRom(new Uint8Array(fs.readFileSync(ROM)));
const [bin, dir] = process.argv.slice(2);
const K = M.K, I = v => K.INST + v;
// script helpers: [seconds, key, down]
const tap = (t, k) => [[t, k, 1], [t + 0.03, k, 0]];
const digits = (t, s) => [...s].flatMap((c, i) => tap(t + i * 0.05, K.D0 + +c));
const beat = (t0, n, sp) => { const o = []; for (let i = 0; i < n; i++){ const v = [0, 7, 1, 7, 0, 8, 1, 9, 3, 4, 5, 6, 2, 10, 11, 12][i % 16]; o.push(...tap(t0 + i * sp, I(v))); if (i % 5 === 0) o.push([t0 + i * sp + 0.01, 100 + (i % 13), 1]); } return o; };
const cases = [
  ['song 00 (factory demo), defaults', {}, 48000, 6, [...tap(0.05, K.SONGFN), ...tap(0.1, K.RUN)]],
  ['pattern 01 with swing, 44.1k, accents from the keys, metronome always off', {tempo:132}, 44100, 4, [...digits(0.02, '01'), ...tap(0.2, K.RUN), [1.0, K.ACCENT, 1], ...tap(1.1, I(1)), [1.3, K.ACCENT, 0], ...tap(3.5, K.RUN)]],
  ['record into pattern 20 with error correct 1/16, then erase the snare', {tempo:100}, 48000, 7, [...digits(0.02, '20'), ...tap(0.1, K.PATFN), ...tap(0.15, K.RUN), ...beat(0.3, 14, 0.17),
    ...tap(3.2, K.PATFN), [3.4, I(1), 1], [5.6, I(1), 0], ...tap(5.7, K.PATFN), ...digits(5.8, "24"), ...tap(6.2, K.RUN)]],
  ['functions: swing, time sig, measures, copy / append, erase', {}, 48000, 2, [...digits(0.02, '03'), ...tap(0.1, K.PATFN), ...tap(0.12, K.PATFN), ...tap(0.14, K.PATFN), ...tap(0.16, K.PATFN),
    ...digits(0.2, '67'), ...tap(0.4, K.PATFN), ...digits(0.45, '03'), ...tap(0.6, K.RIGHT), ...tap(0.65, K.PATFN), ...digits(0.7, '02'), ...tap(0.8, K.PATFN), ...tap(0.82, K.PATFN),
    ...digits(0.9, '30'), ...tap(1.0, K.ENTER), ...digits(1.05, '00'), ...tap(1.1, K.ENTER), ...tap(1.2, K.PATFN), ...digits(1.25, '05'), ...tap(1.3, K.ENTER), ...tap(1.4, K.PATFN), ...tap(1.5, K.RUN)]],
  ['song build: patterns, vol and tempo changes, insert, delete, then play', {dac:1, interp:1}, 48000, 6, [...tap(0.02, K.SONGFN), ...digits(0.05, '03'), ...tap(0.12, K.SONGFN), ...tap(0.14, K.SONGFN), ...tap(0.16, K.SONGFN),
    ...digits(0.2, '02'), ...tap(0.3, K.ENTER), ...digits(0.35, '04'), ...tap(0.45, K.ENTER), ...tap(0.5, K.ENTER), ...tap(0.55, K.LEFT), ...tap(0.6, K.LEFT), ...tap(0.62, K.LEFT),
    ...tap(0.65, K.SONGFN), ...tap(0.67, K.SONGFN), ...digits(0.7, '50'), ...tap(0.8, K.ENTER), ...tap(0.85, K.SONGFN), ...digits(0.9, '160'), ...tap(1.0, K.ENTER), ...tap(1.05, K.SONGFN),
    ...tap(1.1, K.ENTER), ...tap(1.15, K.SONGFN), ...tap(1.2, K.RIGHT), ...tap(1.25, K.ENTER), ...tap(1.3, K.SONGFN), ...tap(1.4, K.RUN)]],
  ['Omega: poly, 6-bit DAC, reverse, start, filters, decay, outputs, pad EQ / ADSR / drive, delay, master, overdrive', {poly:1, dac:2, filt:1, SNrev:1, CRstart:0.3, CHcut:3000, CHres:0.7, OHdec:0.2, T1out:6, CPpan:-0.7, BSdrv:0.4,
    SNatk:0.01, SNsus:0.3, SNrel:0.2, BSlsG:5, CHpkG:-6, CHpkF:7000, SNdls:0.6, CPdls:0.5, dlSync:6, mLsG:3, mHsG:-4, odDrive:0.4, odMode:2, rvProg:3, BStune:-7, CBtune:5}, 96000, 3,
    [...beat(0.05, 30, 0.09)]],
  ['tuned ROM on 44.1k, hard clip, delay without ping-pong, room', {CRtune:-12, RDtune:9, TBtune:-4, odDrive:0.7, odMode:1, dlPing:0, CHdls:0.7, dlLevel:0.8, rvProg:0, rvTime:4, metro:10}, 44100, 3,
    [...tap(0.02, K.PATFN), ...tap(0.05, K.RUN), ...beat(0.2, 20, 0.12)]],
];
let fail = 0;
for (const [name, over, sr, secs, script] of cases){
  const p = D.defaults(); for (const [k, v] of Object.entries(over)){ if (D.P[k] === undefined) throw new Error(k); p[D.P[k]] = v; }
  const ev = script.map(([t, k, d]) => [Math.round(t * sr), k, d]).sort((a, b) => a[0] - b[0]);
  fs.writeFileSync(dir + '/tr.p', p.join('\n')); fs.writeFileSync(dir + '/tr.s', ev.map(h => h.join(' ')).join('\n'));
  execFileSync(bin, [ROM, dir + '/tr.p', dir + '/tr.s', String(sr), String(secs), dir + '/tr.out', dir + '/tr.txt']);
  const b = fs.readFileSync(dir + '/tr.out'), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  const n = Math.round(sr * secs);
  const e = new D.Engine(sr); rom.forEach((r, v) => e.setRom(v, r.codes, r.rate)); e.setAll(p);
  const m = new M.Machine(e, sr); m.bpm = p[D.P.tempo];
  const L = new Float64Array(n), R = new Float64Array(n), outs = Array.from({length:12}, () => new Float64Array(n));
  const lines = [];
  let pos = 0, h = 0;
  while (pos < n){
    while (h < ev.length && ev[h][0] <= pos){ if (ev[h][1] >= 100) e.trigger(ev[h][1] - 100, true); else m.key(ev[h][1], ev[h][2] !== 0);
      const d = m.display(); lines.push(`${d.l}|${d.r}|${d.lp}|${d.ls}`); h++; }
    let end = h < ev.length ? Math.min(n, ev[h][0]) : n; end = Math.min(end, pos + 128);
    m.render(L, R, pos, end - pos, outs); pos = end;
  }
  for (let i = 0; i < 100; i++){ const pt = m.mem.pats[i]; lines.push(`P${i}:${pt.meas},${pt.num}/${pt.den},${pt.swing}` + pt.ev.map(x => ` ${x.t}:${x.v}:${x.a}`).join('')); }
  for (let i = 0; i < 100; i++) lines.push(`S${i}:` + m.mem.songs[i].steps.map(s => ` ${s.p}/${s.vol}/${s.tempo}`).join(''));
  if (process.env.DUMP) console.log(lines.filter(x => /^P(0|3|5|20|30):|^S3:/.test(x)).map(x => x.slice(0, 160)).join("\n"));
  const cpp = fs.readFileSync(dir + '/tr.txt', 'utf8').trimEnd().split('\n');
  let textDiff = -1; for (let i = 0; i < Math.max(cpp.length, lines.length); i++) if (cpp[i] !== lines[i]){ textDiff = i; break; }
  let err = 0, pk = 0;
  for (let i = 0; i < n; i++){ err = Math.max(err, Math.abs(f[i] - L[i]), Math.abs(f[n + i] - R[i])); pk = Math.max(pk, Math.abs(L[i]));
    for (let k = 0; k < 12; k++) err = Math.max(err, Math.abs(f[(2 + k) * n + i] - outs[k][i])); }
  const ok = f.length === 14 * n && err < 1e-4 && pk > 0.05 && textDiff < 0;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: max diff ${err.toExponential(2)} (peak ${pk.toFixed(3)})` + (textDiff >= 0 ? `\n   line ${textDiff}: js "${lines[textDiff]}" c++ "${cpp[textDiff]}"` : ''));
}
process.exitCode = fail ? 1 : 0;
