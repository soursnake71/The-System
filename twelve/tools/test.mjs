// Node checks for the engine: renders, levels, tempo/key detection, auto master loudness, presets.
import {createRequire} from 'module';
const TW = createRequire(import.meta.url)('../engine.js');
const sr = 44100, secs = 8, n = sr * secs, bpm = 93;
const L = new Float32Array(n), R = new Float32Array(n);
// a beat: kick on every beat, plus an A minor chord pad
const beat = 60 / bpm;
for (let b = 0; b * beat < secs; b++){ const s = Math.round(b * beat * sr); for (let i = 0; i < sr * .25 && s + i < n; i++){ const t = i / sr; const v = Math.sin(2 * Math.PI * (50 + 120 * Math.exp(-t * 30)) * t) * Math.exp(-t * 12) * .8; L[s + i] += v; R[s + i] += v; } }
for (let i = 0; i < n; i++){ const t = i / sr; const c = [220, 261.63, 329.63, 110].reduce((s, f) => s + Math.sin(2 * Math.PI * f * t), 0) * .08; L[i] += c; R[i] += c; }
const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) process.exitCode = 1; };
const fin = a => a.every(Number.isFinite);
let t0 = Date.now();
const p = TW.defaults();
const r = TW.renderPre([L, R], sr, p, 48000);
ok(fin(r.pre[0]) && fin(r.pre[1]), `SP-1200 render finite (${Date.now() - t0} ms, ${r.pre[0].length} samples, peak ${TW.peakDb(r.pre).toFixed(1)} dB)`);
ok(Math.abs(r.pre[0].length - 48000 * secs) < 48000 * .03, 'length kept at 0 semis (' + r.pre[0].length + ')');
const up = TW.renderPre([L, R], sr, {...p, semis:12}, 48000);
ok(Math.abs(up.pre[0].length - 24000 * secs) < 48000 * .03, `+12 semis halves length (${up.pre[0].length})`);
ok(TW.spInc(0) === 128 && TW.spInc(-1200) === 64 && TW.spInc(1500) === 256, 'SP pitch table');
for (let m = 0; m < 7; m++) for (let f = 0; f < 8; f++){ const q = {...p, ...{machine:m, rate:TW.MACHINES[m].rate, bits:TW.MACHINES[m].bits, ftype:f, res:.9, crackle:.5, hiss:.5, wow:.5, flutter:.5, tape:.5, age:.5, hum:.5, mix:.5}}; const o = TW.renderPre([L.subarray(0, sr), R.subarray(0, sr)], sr, q, 48000); if (!fin(o.pre[0]) || TW.peakDb(o.pre) > 12) ok(false, `machine ${m} filter ${f}: peak ${TW.peakDb(o.pre)}`); }
ok(true, 'every machine x filter renders');
t0 = Date.now(); const st = TW.stretch([L, R], 1.5, sr, 60); ok(Math.abs(st[0].length - n * 1.5) < 2 && fin(st[0]), `stretch x1.5 (${Date.now() - t0} ms)`);
const tempo = TW.detectTempo([L, R], sr); ok(Math.abs(tempo - bpm) < 1.5, `tempo ${tempo} (want ${bpm})`);
const key = TW.detectKey([L, R], sr); ok(key.name === 'A minor' || key.name === 'C major', `key ${key.name} (${key.confidence})`);
t0 = Date.now(); const am = TW.autoMaster(r.pre, 48000, null, -14); const mo = TW.renderMaster(r.pre, {...p, ...am.params}, am.match, 48000);
const lu = TW.loudness(mo, 48000), pk = TW.peakDb(mo);
ok(Math.abs(lu + 14) < 1.5 && pk <= -0.99, `auto master: ${lu.toFixed(1)} LUFS, peak ${pk.toFixed(2)} dB, gain ${am.params.mGain} (${Date.now() - t0} ms)`);
const pr = TW.makePresets(); ok(pr.length === 540 && new Set(pr.map(x => x.name)).size === 540, `540 unique presets (${pr[0].name}, ${pr[499].name})`);
const rp = TW.randomPatch(42); ok(Object.keys(rp.values).length === TW.SOUND_IDS.length, 'random patch: ' + rp.name);
const c = new TW.Converter(48000); c.setup(p); let s = 0; for (let i = 0; i < 48000; i++) s += Math.abs(c.process(Math.sin(i / 10))); ok(s > 1000, 'live converter');
