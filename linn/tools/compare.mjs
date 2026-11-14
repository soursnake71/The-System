// Runs the same front-panel scripts through linn/machine.js + engine.js and through the C++ port (plugins/linn/Machine.h
// + Linn.h, built as linn_test) and compares the audio sample by sample (the mix and OUT 1-8), the displays after every
// key and the memory at the end. usage: node linn/tools/compare.mjs path/to/linn_test scratchDir
import {createRequire} from 'module';
import fs from 'fs';
import path from 'path';
import {execFileSync} from 'child_process';
import {fileURLToPath} from 'url';
const req = createRequire(import.meta.url);
const D = req('../engine.js'), M = req('../machine.js'), KIT = req('../kit.js');
const ROM = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'rom', 'linn.rom');
const rom = D.parseRom(new Uint8Array(fs.readFileSync(ROM)));
const [bin, dir] = process.argv.slice(2);
fs.mkdirSync(dir, { recursive: true });
const K = M.K, KP = M.KP, DK = k => K.DRUM + k;
// script helpers: [seconds, key, down]
const tap = (t, k) => [[t, k, 1], [t + 0.03, k, 0]];
const digits = (t, s) => [...s].flatMap((c, i) => tap(t + i * 0.05, K.D1 + +c - 1));
const hold = (t, k, inner) => [[t, k, 1], ...inner, [t + 0.5, k, 0]];
const beat = (t0, n, sp) => { const o = []; for (let i = 0; i < n; i++){ const k = [5, 9, 3, 9, 4, 10, 2, 8, 11, 12, 13, 14, 0, 15, 6, 1][i % 16]; o.push(...tap(t0 + i * sp, DK(k))); if (i % 5 === 0) o.push([t0 + i * sp + 0.01, 100 + (i % 23), 1]); } return o; };
const cases = [
  ['song 11 (factory demo), defaults', {}, 48000, 6, [...tap(0.05, K.SONGPAT), ...tap(0.1, K.PLAY)]],
  ['pattern 22 (shuffle E) at 44.1k, then pattern 31 queued', {tempo:132}, 44100, 5, [...digits(0.02, '22'), ...tap(0.2, K.PLAY), ...digits(1.0, '31'), ...tap(4.5, K.PLAY)]],
  ['record into 61 (1/16, BEFORE C), erase the snare while recording, AFTER D', {tempo:100}, 48000, 7, [...digits(0.02, '61'),
    ...hold(0.1, KP.BEFORE, digits(0.15, '3')), [0.7, KP.RECORD, 1], ...tap(0.75, K.PLAY), [0.8, KP.RECORD, 0], ...beat(0.9, 14, 0.17),
    [3.6, KP.ERASE, 1], [3.7, DK(3), 1], [5.4, DK(3), 0], [5.5, KP.ERASE, 0], ...hold(5.6, KP.AFTER, digits(5.65, '4')), ...tap(6.5, K.PLAY)]],
  ['pattern keys: error correct, copy, double, join, erase, length', {}, 48000, 4, [...digits(0.02, '13'), ...hold(0.1, KP.ERRCOR, digits(0.15, '5')),
    ...hold(0.7, KP.COPY, digits(0.75, '62')), ...digits(1.3, '62'), ...hold(1.4, KP.COPY, digits(1.45, '62')), ...digits(2.0, '11'), ...hold(2.05, KP.COPY, digits(2.1, '62')),
    ...hold(2.6, KP.ERASE, digits(2.65, '12')), ...digits(3.1, '14'), [3.15, KP.ERASE, 1], ...tap(3.2, DK(9)), [3.3, KP.ERASE, 0],
    [3.35, KP.RECORD, 1], ...tap(3.4, KP.LENGTH), [3.45, KP.RECORD, 0], ...tap(3.9, K.PLAY)]],
  ['song keys: select, enter, end, insert, delete, repeat off, play from step', {hatMode:1, kit:1}, 48000, 6, [...tap(0.02, K.SONGPAT), ...hold(0.05, K.SONGNUM, digits(0.1, '12')),
    ...hold(0.6, K.ENTER, [...digits(0.65, '11'), ...digits(0.8, '15'), ...digits(0.95, '13')]), ...tap(1.2, K.LEFT), ...hold(1.25, K.INSERT, digits(1.3, '21')),
    ...tap(1.8, K.DELETE), ...tap(1.85, K.LEFT), [1.9, K.SONGPAT, 1], ...tap(1.95, K.END), [2.0, K.SONGPAT, 0], [2.05, K.INSERT, 1], ...tap(2.1, K.PLAY), [2.2, K.INSERT, 0]]],
  ['BPM / trigger, percussion keys, hat modes, poly, Omega controls, outputs', {poly:1, hatMode:2, hatDecay:12, SNrev:1, CRstart:0.3, HHcut:3000, HHres:0.7, LTdec:0.2, HTout:8, CPpan:20, BSdrv:0.4,
    SNatk:0.01, SNsus:0.3, SNrel:0.2, BSlsG:5, HHpkG:-6, HHpkF:7000, SNdls:0.6, CPdls:0.5, dlSync:6, mLsG:3, mHsG:-4, odDrive:0.4, odMode:2, rvProg:3, BStune:-7, CWtune:5, bassChip:1, mux:40, tSN:80, tHC:10}, 96000, 3,
    [...hold(0.02, K.BPM, digits(0.05, '7')), ...tap(0.6, DK(7)), ...beat(0.7, 24, 0.08), ...tap(2.7, DK(7))]],
  ['LINN hat run of closed hats, sound set 2, 44.1k, click, hard clip', {hatDecay:30, kit:1, odDrive:0.7, odMode:1, dlPing:0, HHdls:0.7, dlLevel:0.8, rvProg:0, rvTime:4, clickLv:100}, 44100, 3,
    [[0.02, KP.RECORD, 1], ...tap(0.05, K.PLAY), [0.1, KP.RECORD, 0], ...Array.from({length:16}, (_, i) => tap(0.2 + i * 0.11, DK(9))).flat(), ...tap(2.2, DK(10)), ...tap(2.35, DK(9))]],
];
let fail = 0;
for (const [name, over, sr, secs, script] of cases){
  const p = D.defaults(); for (const [k, v] of Object.entries(over)){ if (D.P[k] === undefined) throw new Error(k); p[D.P[k]] = v; }
  const ev = script.map(([t, k, d]) => [Math.round(t * sr), k, d]).sort((a, b) => a[0] - b[0]);
  fs.writeFileSync(dir + '/ln.p', p.join('\n')); fs.writeFileSync(dir + '/ln.s', ev.map(h => h.join(' ')).join('\n'));
  execFileSync(bin, [ROM, dir + '/ln.p', dir + '/ln.s', String(sr), String(secs), dir + '/ln.out', dir + '/ln.txt']);
  const b = fs.readFileSync(dir + '/ln.out'), f = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  const n = Math.round(sr * secs);
  const e = new D.Engine(sr); rom.forEach((L, g) => e.setGroup(g, L)); e.setAll(p);
  const m = new M.Machine(e, sr); m.bpm = p[D.P.tempo];
  const L = new Float64Array(n), R = new Float64Array(n), outs = Array.from({length:16}, () => new Float64Array(n));
  const lines = [], B = x => x ? 1 : 0;
  let pos = 0, h = 0;
  while (pos < n){
    while (h < ev.length && ev[h][0] <= pos){ if (ev[h][1] >= 100) e.trigger(ev[h][1] - 100); else m.key(ev[h][1], ev[h][2] !== 0);
      const d = m.display(); lines.push(`${d.l}|${d.r}|${B(d.song)}${d.tim}${d.ec}${B(d.rec)}${B(d.ext)}${B(d.perc)}`); h++; }
    let end = h < ev.length ? Math.min(n, ev[h][0]) : n; end = Math.min(end, pos + 128);
    m.render(L, R, pos, end - pos, outs); pos = end;
  }
  for (let i = 0; i < M.NP; i++){ const pt = m.mem.pats[i]; lines.push(`P${i}:${pt.len},${pt.ec},${pt.tim},${pt.bef}` + pt.ev.map(x => ` ${x.t}:${x.s}`).join('')); }
  for (let i = 0; i < M.NP; i++) lines.push(`S${i}:` + m.mem.songs[i].steps.map(s => ` ${s}`).join(''));
  lines.push('R' + B(m.mem.repeat));
  const cpp = fs.readFileSync(dir + '/ln.txt', 'utf8').trimEnd().split('\n');
  let textDiff = -1; for (let i = 0; i < Math.max(cpp.length, lines.length); i++) if (cpp[i] !== lines[i]){ textDiff = i; break; }
  let err = 0, pk = 0;
  for (let i = 0; i < n; i++){ err = Math.max(err, Math.abs(f[i] - L[i]), Math.abs(f[n + i] - R[i])); pk = Math.max(pk, Math.abs(L[i]));
    for (let k = 0; k < 16; k++) err = Math.max(err, Math.abs(f[(2 + k) * n + i] - outs[k][i])); }
  const ok = f.length === 18 * n && err < 1e-4 && pk > 0.05 && textDiff < 0;
  if (!ok) fail++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: max diff ${err.toExponential(2)} (peak ${pk.toFixed(3)})` + (textDiff >= 0 ? `\n   line ${textDiff}: js "${lines[textDiff]}" c++ "${cpp[textDiff]}"` : ''));
}
// the kit name rules (LOAD FOLDER) in kit.js and Kit.h
{
  const names = ['Cabasa.wav', 'Chh.wav', 'Chhl.wav', 'Chhs.wav', 'Clap.wav', 'Conga.wav', 'Congah.wav', 'Congahh.wav', 'Congal.wav', 'Congall.wav', 'Congalll.wav', 'Cowb.wav',
    'Crash.wav', 'Kick.wav', 'Kickme.wav', 'Ohh.wav', 'Reallinn.wav', 'Ride.wav', 'SnareDrum.wav', 'SnareDrumh.wav', 'Cabasa_Hard.wav', 'Cabasa_Soft.wav', 'Cl_Hat.wav',
    'Hi_Conga.wav', 'High_Tom.wav', 'Kick_Hard.wav', 'Lo_Tom.wav', 'Low_Conga.wav', 'Mid_Tom.wav', 'Open_Hat.wav', 'Ride_Hard.wav', 'Ride_Soft.wav', 'Sidestick.wav',
    'Snare_Hard.wav', 'Snare_Mid.wav', 'Snare_Soft.wav', 'Tambourine_Hard.wav', 'BD 909.wav', 'Rimshot.aif', 'LM-2 Tom 2.wav', 'tamb(soft).flac', 'Sd.wav', 'CH_808.WAV'];
  const sets = [names.slice(0, 20), names.slice(20, 37), names];
  for (const ns of sets){
    fs.writeFileSync(dir + '/kit.txt', ns.join('\n'));
    const out = execFileSync(bin, ['--kit', dir + '/kit.txt']).toString().trim();
    const c = KIT.classify(ns), js = c.groups.map((l, g) => `${g}:${l.map(i => ' ' + i).join('')}`).concat(['dyn:' + c.dyn.map(d => ' ' + d).join(''), 'left:' + c.left.map(i => ' ' + i).join('')]).join('\n');
    const ok = out === js; if (!ok) fail++;
    console.log(`${ok ? 'ok  ' : 'FAIL'} kit names (${ns.length} files)` + (ok ? '' : `\n js:\n${js}\n c++:\n${out}`));
  }
}
process.exitCode = fail ? 1 : 0;
