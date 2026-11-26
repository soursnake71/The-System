// Omega Five Node tests: node five/tools/test.mjs
// engineTests() (engine.js) and seqTests() (seq.js) live in this file; main() runs whichever exist.

async function main() {
  let failed = false;
  for (const name of ['engineTests', 'seqTests', 'rxTests', 'kitTests']) {
    let fn;
    try { fn = eval(name); } catch { continue; } // not defined (yet)
    if (typeof fn !== 'function') continue;
    console.log(`# ${name}`);
    try { await fn(); } catch (e) { failed = true; console.error(`FAIL ${name}:`, e); }
  }
  if (failed) process.exit(1);
  console.log('all tests passed');
}

// ---- engine tests ----
async function engineTests() {
  const E = await import('../engine.js');
  const assert = (await import('node:assert/strict')).default;
  let n = 0;
  const t = async (name, fn) => { await fn(); n++; console.log('  ok engine:', name); };
  const sr = 48000, dB = (x) => 20 * Math.log10(x + 1e-30);
  const run = (e, secs) => { const m = Math.round(secs * sr), L = new Float32Array(m), R = new Float32Array(m); for (let s = 0; s < m; s += 128) e.process(L.subarray(s), R.subarray(s), Math.min(128, m - s)); return { L, R }; };
  const peak = (a, i0 = 0, i1 = a.length) => { let p = 0; for (let i = i0; i < i1; i++) { const x = Math.abs(a[i]); if (x > p) p = x; } return p; };
  const rms = (a, i0, i1) => { let s = 0; for (let i = i0; i < i1; i++) s += a[i] * a[i]; return Math.sqrt(s / (i1 - i0)); };
  const finite = (a) => a.every(Number.isFinite);
  // the factory kit (five/kit, 25 kHz recordings of a real RX5), decoded once
  const W = await import('./wav.mjs'), path = await import('node:path');
  const kitWav = E.FACTORY_MAP.map((f) => W.readWav(path.join(W.KIT_DIR, f)));
  const KIT = () => { const e = new E.Engine(sr); kitWav.forEach((w, v) => e.setUserSample(v, w.data, w.sr, { rec: true })); return e; };

  await t('exports and tables', () => {
    for (const k of ['Engine', 'VOICE_NAMES', 'CHANNEL_NAMES', 'VOICE_DEFAULTS', 'REV_PROGRAMS', 'ROM_SR', 'FACTORY_MAP', 'rng', 'REV_DEFAULTS', 'DLY_DEFAULTS']) assert.ok(k in E, k);
    assert.equal(E.VOICE_NAMES.length, 24); assert.ok(E.VOICE_NAMES.every((s) => s.length <= 8));
    assert.equal(E.CHANNEL_NAMES.length, 12); assert.equal(E.VOICE_DEFAULTS.length, 24); assert.equal(E.ROM_SR, 25000);
    assert.deepEqual(E.REV_PROGRAMS.map((p) => p.name), ['HALL1', 'HALL2', 'ROOM1', 'ROOM2', 'VOCAL', 'PLATE1', 'PLATE2', 'ER1', 'ER2', 'GATE', 'REV GATE']);
  });

  await t('rng is xorshift32', () => {
    const r = E.rng(1); let s = 1n; const M = 0xFFFFFFFFn;
    for (let i = 0; i < 1000; i++) { s ^= (s << 13n) & M; s ^= s >> 17n; s ^= (s << 5n) & M; assert.equal(r(), Number(s) / 4294967296); }
  });

  await t('factory kit: 24 recordings at 25 kHz, loaded as rec; an engine without them is silent', () => {
    assert.equal(E.FACTORY_MAP.length, 24);
    const e = KIT();
    for (let v = 0; v < 24; v++) {
      const u = e.userSample(v); assert.ok(u && u.length > 1000 && finite(u), E.VOICE_NAMES[v]);
      assert.equal(e.voices[v].path, 'rec'); assert.equal(e.roms[v].length, u.length);
      assert.ok(peak(u) > .2 && peak(u) <= 1, `${E.VOICE_NAMES[v]} peak ${peak(u)}`);
    }
    const z = new E.Engine(sr); for (let v = 0; v < 24; v++) z.trigger(v); assert.equal(peak(run(z, .5).L), 0);
  });

  await t('every voice through the engine: peak -20..-1 dBFS, finite, decays to silence', () => {
    // -20: the factory recordings keep the real machine's level differences (RIDE CP sits about 11 dB under the kick)
    const e = KIT();
    for (let v = 0; v < 24; v++) {
      for (const acc of [0, 1, 2]) {
        e.trigger(v, 127, acc); const { L, R } = run(e, 3.5);
        assert.ok(finite(L) && finite(R));
        const p = dB(Math.max(peak(L), peak(R)));
        assert.ok(p >= -20 && p <= -1, `${E.VOICE_NAMES[v]} acc${acc} peak ${p.toFixed(1)} dBFS`);
        assert.ok(peak(L, L.length - 4800) < 1e-4 && peak(R, R.length - 4800) < 1e-4, `${E.VOICE_NAMES[v]} tail`);
        let m = 0; for (const x of L) m += x; assert.ok(Math.abs(m / L.length) < 1e-3, 'DC');
      }
    }
  });

  await t('accent and velocity scale the level', () => {
    const lvl = (vel, acc) => { const e = KIT(); e.trigger(1, vel, acc); return peak(run(e, .3).L); };
    const a0 = lvl(127, 0), a1 = lvl(127, 1), a2 = lvl(127, 2), v64 = lvl(64, 0);
    assert.ok(a1 > a0 * 1.2 && a2 > a1 * 1.2, `${a0} ${a1} ${a2}`); assert.ok(v64 < a0 * .65);
  });

  await t('deterministic, sample-accurate trigger offset', () => {
    const go = () => { const e = KIT(); e.setChannel(1, { sendRev: .5, sendDly: .5 }); e.trigger(1, 100, 1, 77); e.trigger(7, 90, 0, 300); return run(e, 1); };
    const a = go(), b = go(); assert.deepEqual(a.L, b.L); assert.deepEqual(a.R, b.R);
    const i = a.L.findIndex((x) => x !== 0); assert.ok(i >= 77 && i < 90, 'first sample ' + i);
    const e = KIT(); e.trigger(0, 127, 0, 200); const o = run(e, .01); assert.ok(o.L.findIndex((x) => x !== 0) >= 200, 'offset carries across blocks');
  });

  await t("rec path: no 12-bit word, no output saturation, REC_EQ undoes the playback droop; path is a saved voice param", () => {
    const tone = (f, n = 25000) => { const x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = .5 * Math.sin(2 * Math.PI * f * i / 25000); return x; };
    const amp = (rec, f) => { const e = new E.Engine(sr); e.setUserSample(2, tone(f), 25000, { rec }); e.setChannel(2, { pan: 0 }); e.trigger(2); return rms(run(e, .6).L, 4800, 24000); };
    const g1 = amp(true, 1000);
    for (const f of [3000, 6000, 8000, 10000]) { const d = dB(amp(true, f) / g1); assert.ok(Math.abs(d) < .5, `rec response ${f} Hz ${d.toFixed(2)} dB`); }
    assert.ok(dB(amp(false, 10000) / amp(false, 1000)) < -4, 'rx path (no REC_EQ) droops at 10 kHz');
    const e = new E.Engine(sr); assert.equal(e.voices[2].path, 'rx');
    e.setUserSample(2, tone(1000), 25000, { rec: true }); assert.equal(e.voices[2].path, 'rec');
    assert.ok(!e.roms[2].every((x) => Math.round(x * 2048) === x * 2048), 'no 12-bit word');
    const s = e.getState(); assert.equal(s.voices[2].path, 'rec');
    e.setVoiceParam(2, 'path', 'rx'); assert.ok(e.roms[2].every((x) => Math.round(x * 2048) === x * 2048), 'rx: 12-bit word');
    const f = new E.Engine(sr); f.setUserSample(2, tone(1000), 25000, { rec: true }); f.setVoiceParam(2, 'path', 'rx'); f.setState(s); assert.equal(f.voices[2].path, 'rec');
    f.setUserSample(2, null); assert.equal(f.voices[2].path, 'rx'); assert.equal(f.userSample(2), null); assert.equal(f.roms[2].length, 0);
  });

  await t('hi-hat choke: closed cuts open (same channel)', () => {
    const e1 = KIT(); e1.trigger(19); const open = run(e1, .6).L;
    const e2 = KIT(); e2.trigger(19); e2.trigger(7, 127, 0, 4800); const ch = run(e2, .6).L;
    assert.ok(rms(ch, 19200, 28800) < rms(open, 19200, 28800) * .05);
  });

  await t('voice params: pitch, reverse, gate, loop, bend, bits, float DAC', () => {
    const zc = (a, i0, i1) => { let c = 0; for (let i = i0 + 1; i < i1; i++) if ((a[i - 1] < 0) !== (a[i] < 0)) c++; return c; };
    const sine = new Float32Array(22050); for (let i = 0; i < sine.length; i++) sine[i] = .5 * Math.sin(2 * Math.PI * 440 * i / 44100);
    const e = new E.Engine(sr); e.setUserSample(2, sine, 44100); assert.equal(e.userSample(2).length, 12500);
    e.trigger(2); const f0 = zc(run(e, .4).L, 2400, 12000) / 2 / .2;
    e.setVoiceParam(2, 'pitch', 1200); e.trigger(2); const f1 = zc(run(e, .3).L, 2400, 7200) / 2 / .1;
    assert.ok(Math.abs(f0 - 440) < 8 && Math.abs(f1 - 880) < 15, `${f0} ${f1}`);
    e.setVoiceParam(2, 'pitch', 0); e.setVoiceParam(2, 'loop', 1); e.setVoiceParam(2, 'gate', 1000); e.trigger(2);
    const lp = run(e, 3).L; assert.ok(rms(lp, sr * .8, sr * .9) > .05 && peak(lp, sr * 2.5) < 1e-3, 'loop sustains, gate releases');
    e.setVoiceParam(2, 'loop', 0); e.setVoiceParam(2, 'gate', 0); e.setVoiceParam(2, 'bendRate', 55); e.setVoiceParam(2, 'bendRange', 1200); e.trigger(2);
    const bd = run(e, .6).L; const fb = zc(bd, 6000, 9600) / 2 / .075; assert.ok(fb > 840 && fb < 900, 'bend up an octave ' + fb);
    e.setUserSample(2, null); assert.equal(e.userSample(2), null);
    const r = KIT(); r.setVoiceParam(0, 'reverse', 1); r.trigger(0); const rv = run(r, .4).L, bl = r.roms[0].length * sr / 25000;
    let im = 0; for (let i = 0; i < rv.length; i++) if (Math.abs(rv[i]) > Math.abs(rv[im])) im = i; assert.ok(im > bl * .8, 'reverse peaks late ' + im);
    const q = KIT(); q.setVoiceParam(0, 'path', 'rx'); q.setVoiceParam(0, 'bits', 8); assert.equal(q.getState().voices[0].bits, 8);
    const rom = q.roms[0]; assert.ok(rom.every((x) => Math.round(x * 128) === x * 128));
    q.setDac('float'); assert.equal(q.getState().dac, 'float'); q.trigger(0); assert.ok(finite(run(q, .5).L));
    q.setVoiceParam(3, 'gate', 50); assert.equal(q.voices[3].gate, 100); q.setVoiceParam(3, 'pitch', 9999); assert.equal(q.voices[3].pitch, 2400);
  });

  await t('state round-trips through JSON', () => {
    const e = KIT();
    e.setVoiceParam(5, 'pitch', -300); e.setVoiceParam(9, 'd2r', 40); e.setChannel(3, { fader: .5, pan: -.3, sendRev: .2, mute: true });
    e.setFx('rev', 'program', 'PLATE2'); e.setFx('rev', 'time', 1.7); e.setFx('dly', 'sync', '1/8.'); e.setFx('dly', 'feedback', 80); e.setMaster({ volume: .7, click: .3 }); e.setDac('float');
    const s = JSON.parse(JSON.stringify(e.getState()));
    assert.equal(s.rev.program, 6); assert.equal(s.dly.sync, 3); assert.equal(s.channels[3].mute, true);
    const f = KIT(); f.setState(s); assert.deepEqual(f.getState(), e.getState());
    f.setState(JSON.parse(JSON.stringify(new E.Engine(sr).getState()))); assert.deepEqual(f.getState(), new E.Engine(sr).getState());
  });

  await t('reverb: every program rings, stays bounded and dies away', () => {
    for (let p = 0; p < E.REV_PROGRAMS.length; p++) {
      const e = KIT(); e.setFx('rev', 'program', p); e.setChannel(1, { fader: 1, sendRev: 1 }); e.trigger(1);
      const { L, R } = run(e, 12); const name = E.REV_PROGRAMS[p].name;
      assert.ok(finite(L) && finite(R) && peak(L) < 1, name);
      assert.ok(rms(L, sr * .5, sr * .6) > 1e-4, `${name} has a tail`);
      assert.ok(peak(L, sr * 11) < 1e-3, `${name} decays (${peak(L, sr * 11)})`);
    }
    const e = KIT(); e.setFx('rev', 'time', 30); e.setFx('rev', 'hiRatio', 1); e.setChannel(0, { sendRev: 1 });
    for (let k = 0; k < 20; k++) e.trigger(0, 127, 2, k * 50);
    const o = run(e, 20); assert.ok(finite(o.L) && peak(o.L) < 4, 'RT60 30 s stable ' + peak(o.L));
  });

  await t('TC delay: echo at the set time, 120 % feedback limited, sync to tempo', () => {
    const e = KIT(); e.setFx('dly', 'timeL', 300); e.setFx('dly', 'timeR', 300); e.setFx('dly', 'modDepth', 0); e.setChannel(2, { pan: 0, sendDly: 1 }); e.trigger(2);
    const { L } = run(e, 1); let im = 4800; for (let i = 4800; i < 24000; i++) if (Math.abs(L[i]) > Math.abs(L[im])) im = i;
    assert.ok(Math.abs(im - .3 * sr) < 240, 'echo at ' + im);
    const f = KIT(); f.setFx('dly', 'feedback', 120); f.setFx('dly', 'pingpong', 1); f.setChannel(0, { sendDly: 1 }); f.trigger(0, 127, 2);
    const o = run(f, 15); assert.ok(finite(o.L) && peak(o.L) < 2 && rms(o.L, sr * 14, sr * 15) > 1e-3, 'self-oscillates, bounded');
    const g = new E.Engine(sr); g.setTempo(100); g.setFx('dly', 'sync', 1); assert.ok(Math.abs(g.dlyFx.tL - .6 * sr) < 1);
    const d = KIT(); d.setFx('dly', 'on', 0); d.setChannel(0, { sendDly: 1 }); d.trigger(0); const off = run(d, 1).L; assert.ok(peak(off, sr * .8) < 1e-4, 'off = no echoes');
  });

  await t('meters, voiceActive, click', () => {
    const e = KIT(); e.trigger(0); run(e, .05);
    assert.ok(e.voiceActive(0) > .3 && e.voiceActive(5) === 0); const m = e.meters(); assert.ok(m.l > .05 && m.r > .05); assert.equal(e.meters().l, 0);
    run(e, 2); assert.ok(e.voiceActive(0) < .01);
    e.clickTick(true); const c = run(e, .1).L; assert.ok(peak(c) > .05 && peak(c, sr * .05) === 0);
  });

  await t('real-time budget (dense kit, all FX)', () => {
    const e = KIT(); for (let ch = 0; ch < 12; ch++) e.setChannel(ch, { sendRev: .3, sendDly: .2 });
    const L = new Float32Array(128), R = new Float32Array(128), blocks = Math.round(10 * sr / 128), t0 = performance.now();
    for (let b = 0; b < blocks; b++) { if (b % 47 === 0) for (let v = 0; v < 24; v += 2) e.trigger((v + b) % 24, 110, b % 3); e.process(L, R, 128); }
    const ms = performance.now() - t0; console.log(`    10 s rendered in ${ms.toFixed(0)} ms (${(10000 / ms).toFixed(1)}x real time)`);
    assert.ok(ms < 10000);
  });

  console.log(`engine: ${n} tests passed`);
}

// ---- seq tests ----
async function seqTests() {
  const S = await import('../seq.js');
  const assert = (await import('node:assert/strict')).default;
  let n = 0;
  const t = async (name, fn) => { await fn(); n++; console.log('  ok seq:', name); };
  const evKey = (e) => `${e.t},${e.v},${e.vel},${e.acc}`;
  const sortEv = (a) => a.slice().sort((x, y) => x.t - y.t || x.v - y.v).map(evKey);

  await t('newState shape', () => {
    const s = S.newState();
    assert.equal(s.patterns.length, 100); assert.equal(s.songs.length, 20); assert.equal(s.chains.length, 3);
    assert.equal(s.noteMap[0], 45); assert.equal(s.noteMap[19], 59); assert.deepEqual(s.noteMap, S.RX5_NOTES);
    assert.equal(new Set(S.RX5_NOTES).size, 24); assert.equal(s.setups.length, 3); assert.equal(s.midi.deviceNo, 1); assert.equal(S.PPQN, 24);
    assert.equal(S.patLen(s.patterns[0]), 96);
    assert.equal(S.QUANTIZE[S.QUANTIZE_KEYPAD[5]].name, '1/16'); assert.equal(S.QUANTIZE[S.QUANTIZE_KEYPAD[9]].name, 'OFF');
  });

  await t('recordHit quantize + swing', () => {
    const s = S.newState();
    S.recordHit(s, 0, 7.4, 0, 100, 0, 5); // 1/16 grid 6 -> step 1
    assert.equal(s.patterns[0].events[0].t, 6);
    s.patterns[0].swing = 75;
    const e = S.recordHit(s, 0, 17, 1, 90, 1, 5); // step 3 (odd) -> 18 + 3
    assert.equal(e.t, 21);
    const w = S.recordHit(s, 0, 95.9, 2, 90, 0, 3); // 1/8 -> 96 wraps to 0
    assert.equal(w.t, 0);
    S.stepInsert(s, 0, 48, 7); assert.equal(S.stepDelete(s, 0, 48), 1);
    S.setBars(s, 0, 2); S.stepInsert(s, 0, 150, 7); S.setBars(s, 0, 1);
    assert.ok(s.patterns[0].events.every((x) => x.t < 96));
  });

  await t('player timing at 120 BPM / 48k across odd blocks', () => {
    const s = S.newState(); s.tempo = 120;
    const p = s.patterns[0];
    for (const tk of [0, 1, 6, 13, 24, 95]) S.stepInsert(s, 0, tk, 0, 100, 0);
    const pl = new S.Player(s); pl.setMode('pattern', 0); pl.start();
    // 1 tick = 48000*60/(120*24) = 1000 samples
    const got = []; let base = 0;
    const sizes = [127, 333, 1, 999, 4097, 511];
    let i = 0;
    while (base < 2 * 96 * 1000 + 10) {
      const nF = sizes[i++ % sizes.length];
      for (const e of pl.advance(nF, 48000)) { assert.ok(e.offset >= 0 && e.offset < nF); got.push(base + e.offset); }
      base += nF;
    }
    const exp = [];
    for (const loop of [0, 1]) for (const tk of [0, 1, 6, 13, 24, 95]) exp.push((loop * 96 + tk) * 1000);
    exp.push(192000);
    assert.deepEqual(got.filter((x) => x <= 192000), exp);
    assert.ok(p.events.length === 6);
  });

  await t('click events on beats', () => {
    const s = S.newState(); s.click.on = true;
    const pl = new S.Player(s); pl.setMode('pattern', 0); pl.start();
    const ev = pl.advance(96 * 1000, 48000);
    assert.deepEqual(ev.map((e) => [e.offset, e.click]), [[0, 2], [24000, 1], [48000, 1], [72000, 1]]);
  });

  await t('song repeats, tempo, vol, end', () => {
    const s = S.newState();
    S.stepInsert(s, 1, 0, 0, 100); S.stepInsert(s, 2, 0, 1, 100); S.stepInsert(s, 3, 0, 13, 62);
    s.songs[0] = { name: 'T', tempo: 120, steps: [
      { type: 'pat', n: 1 }, { type: 'pat', n: 2 }, { type: 'rep', from: 0, count: 3 }, { type: 'mark' },
      { type: 'vol', ch: 1, level: 0 }, { type: 'tempo', bpm: 240 }, { type: 'pat', n: 3 }] };
    const pl = new S.Player(s); pl.setMode('song', 0); pl.start();
    const out = []; let base = 0;
    while (pl.running && base < 2e6) { for (const e of pl.advance(4800, 48000)) out.push({ at: base + e.offset, v: e.v }); base += 4800; }
    // 6 bars at 120 (96000 samples each); pattern 3's SD2 muted by the vol step (channel 1)
    assert.deepEqual(out.map((e) => e.v), [0, 1, 0, 1, 0, 1]);
    assert.deepEqual(out.map((e) => e.at), [0, 1, 2, 3, 4, 5].map((k) => k * 96000));
    assert.ok(pl.ended);
    const f = S.flattenSong(s, 0);
    assert.equal(f.length, 7 * 96); assert.deepEqual(f.tempos.map((x) => x.bpm), [120, 240]);
    // locate via SPP: 16ths -> bar 3 = pattern 1, pass 2
    pl.setMode('song', 0); pl.spp(16 * 2); const ps = pl.pos(); assert.equal(ps.pattern, 1); assert.equal(ps.step, 0);
  });

  await t('external clock24 and overdub', () => {
    const s = S.newState();
    S.stepInsert(s, 0, 2, 0, 100);
    const pl = new S.Player(s); pl.setMode('pattern', 0); pl.setExternal(true); pl.start();
    assert.equal(pl.advance(4800, 48000).length, 0);
    const hits = [];
    for (let k = 0; k < 4; k++) hits.push(pl.clock24(k).length);
    assert.deepEqual(hits, [0, 0, 1, 0]);
    // overdub in internal mode: play to tick 10.x, record SD -> quantized to 12 (1/16), must not double-trigger this pass
    const p2 = new S.Player(s); p2.setMode('pattern', 0); p2.start();
    p2.advance(10500, 48000); // ticks 0..10 fired, at 10.5
    const e = p2.record(1, 100, 0);
    assert.equal(e.t, 12);
    let fired = 0;
    let all = p2.advance(86000, 48000); // up to tick 96.5 (loop wrap)
    fired = all.filter((x) => x.v === 1).length; assert.equal(fired, 0);
    all = p2.advance(96000, 48000); fired = all.filter((x) => x.v === 1).length; assert.equal(fired, 1);
  });

  await t('SMF export -> import round trip', () => {
    const s = S.newState(); s.tempo = 133;
    S.setBars(s, 5, 3);
    const p = s.patterns[5];
    S.stepInsert(s, 5, 0, 0, 110, 0); S.stepInsert(s, 5, 0, 7, 70, 1); S.stepInsert(s, 5, 24, 1, 127, 2);
    S.stepInsert(s, 5, 30, 22, 64, 0); S.stepInsert(s, 5, 96, 0, 110, 0); S.stepInsert(s, 5, 96 + 95, 19, 5, 2);
    S.stepInsert(s, 5, 200, 23, 99, 1);
    const bytes = S.exportSMF(s, { what: 'pattern', n: 5 });
    const d = S.newState();
    const r = S.importSMF(bytes.buffer, { state: d, startPattern: 10, songSlot: 2 });
    assert.equal(Math.round(r.tempo), 133);
    assert.equal(r.patterns.length, 3);
    const f = S.flattenSong(d, 2);
    assert.equal(f.length, S.patLen(p));
    assert.deepEqual(sortEv(f.events), sortEv(p.events));
    // song export: repeated bars dedupe into the same pattern with a rep step
    s.songs[1] = { name: 'RT', tempo: 100, steps: [{ type: 'pat', n: 5 }, { type: 'rep', from: 0, count: 2 }, { type: 'tempo', bpm: 150 }, { type: 'pat', n: 5 }] };
    const d2 = S.newState();
    const r2 = S.importSMF(S.exportSMF(s, { what: 'song', n: 1 }), { state: d2 });
    assert.equal(r2.patterns.length, 3);
    const a = S.flattenSong(s, 1), b = S.flattenSong(d2, 0);
    assert.deepEqual(sortEv(b.events), sortEv(a.events));
    assert.deepEqual(b.tempos.map((x) => Math.round(x.bpm)), [100, 150]);
  });

  await t('hand-built format 0 with running status', () => {
    const div = 480;
    const trk = [
      0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,            // 120 bpm
      0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08,      // 3/4
      0x00, 0x99, 36, 100,                                  // BD2 at 0
      0x00, 42, 80,                                         // running status: HH CLS at 0
      0x83, 0x60, 38, 90,                                   // +480 (VLQ 83 60): SD1 at beat 2
      0x00, 36, 0,                                          // note-on vel 0 = off
      0x81, 0x70, 0x89, 38, 64,                             // +240 note-off with status
      0x85, 0x50, 0x99, 44, 70,                             // +720: pedal hh -> HH CLS at tick 1440(div) = 72
      0x00, 0x90, 60, 100,                                  // ch 1 note: ignored (channel auto -> 10)
      0x00, 0x99, 99, 100,                                  // unmapped
      0x8B, 0x20, 0xFF, 0x2F, 0x00,                         // +1440 end -> 2 bars of 3/4
    ];
    const bytes = new Uint8Array([0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, div >> 8, div & 255,
      0x4D, 0x54, 0x72, 0x6B, 0, 0, 0, trk.length, ...trk]);
    const d = S.newState();
    const r = S.importSMF(bytes.buffer, { state: d });
    assert.equal(r.tempo, 120);
    const p = d.patterns[r.patterns[0]];
    assert.equal(p.beats, 3); assert.equal(p.unit, 4);
    assert.deepEqual(sortEv(p.events), sortEv([{ t: 0, v: 12, vel: 100, acc: 0 }, { t: 0, v: 7, vel: 80, acc: 0 }, { t: 24, v: 1, vel: 90, acc: 0 }]));
    assert.deepEqual(d.patterns[r.patterns[1]].events, [{ t: 0, v: 7, vel: 70, acc: 0 }]);
    assert.equal(S.flattenSong(d, 0).events.length, 4); // bar 1: 3 notes, bar 2: pedal hh at tick 0 of bar 2
    assert.match(r.report, /1 unmapped, 1 on other channels/);
    const smpte = bytes.slice(); smpte[12] = 0xE7; smpte[13] = 40;
    assert.throws(() => S.importSMF(smpte.buffer, { state: d }), /SMPTE/);
  });

  await t('cart round trip (plain + gzip) and sysex', async () => {
    const s = S.newState();
    S.randomize(s, 1234, 'all');
    s.voice[3].custom = { x: [1, 2] };
    const data = new Float32Array(1000); for (let i = 0; i < data.length; i++) data[i] = Math.sin(i / 7) * 0.8;
    s.samples[4] = { name: 'mine.wav', sr: 44100, data };
    const strip = (x) => JSON.parse(JSON.stringify({ ...x, samples: null }));
    for (const gzip of [false, true]) {
      const bytes = await S.saveCart(s, { gzip });
      assert.equal(bytes[0] === 0x1f && bytes[1] === 0x8b, gzip);
      const l = await S.loadCart(bytes);
      assert.deepEqual(strip(l), strip(s));
      assert.equal(l.samples[4].sr, 44100); assert.equal(l.samples[0], null);
      for (let i = 0; i < data.length; i++) assert.ok(Math.abs(l.samples[4].data[i] - data[i]) < 1 / 32000);
    }
    const sx = S.sysexDump(s);
    assert.deepEqual(Array.from(sx.slice(0, 4)), [0xF0, 0x7D, 0x4F, 0x35]); assert.equal(sx[sx.length - 1], 0xF7);
    assert.ok(sx.slice(1, -1).every((b) => b < 0x80));
    const back = S.parseSysex(sx);
    assert.deepEqual(strip(back), strip(s));
    const bin = Uint8Array.from({ length: 300 }, (_, i) => (i * 37) & 255);
    assert.deepEqual(S.unpack7(S.pack7(bin)), bin);
  });

  await t('randomize determinism', () => {
    for (const scope of ['all', 'pattern', 'voices', 'fx']) {
      const a = S.newState(), b = S.newState(), c = S.newState();
      S.randomize(a, 42, scope); S.randomize(b, 42, scope); S.randomize(c, 43, scope);
      assert.deepEqual(JSON.stringify(a), JSON.stringify(b));
      assert.notEqual(JSON.stringify(a), JSON.stringify(c));
    }
    const a = S.newState(); S.randomize(a, 7, 'all');
    assert.ok(a.patterns[0].events.length > 4); assert.ok(a.songs[0].steps.length > 3);
    for (const k of ['pitch', 'level', 'ar', 'd1r', 'd1l', 'd2r', 'rr', 'gate', 'bendRate', 'bendRange', 'reverse', 'loop']) assert.ok(k in a.voice[0]);
    assert.ok(a.fx.rev.program >= 0 && a.fx.rev.program <= 10); assert.ok('duckThresh' in a.fx.dly);
    const pl = new S.Player(a); pl.setMode('song', 0); pl.start();
    let k = 0; while (pl.running && k++ < 10000) pl.advance(4096, 48000);
    assert.ok(pl.ended);
  });

  await t('randomized values inside engine RANGES (hardware ranges)', async () => {
    const E = await import('../engine.js');
    for (let seed = 1; seed <= 40; seed++) {
      const a = S.newState(); S.randomize(a, seed, 'all');
      const inR = (grp, o) => { for (const [k, x] of Object.entries(o)) { const r = E.RANGES[grp][k]; if (r && typeof x === 'number') assert.ok(x >= r[0] && x <= r[1], `${grp}.${k}=${x} seed ${seed}`); } };
      a.voice.forEach((o) => { inR('voice', o); assert.ok(o.gate === 0 || (o.gate >= 100 && o.gate <= 6500)); });
      a.channel.forEach((o) => inR('channel', o)); inR('rev', a.fx.rev); inR('dly', a.fx.dly);
    }
  });

  console.log(`seq: ${n} tests passed`);
}

// ---- end seq tests ----

// ---- rx tests (rxsysex.js, rxcart.js, seq.js MIDI helpers) ----
async function rxTests() {
  const S = await import('../seq.js');
  const X = await import('../rxsysex.js');
  const C = await import('../rxcart.js');
  const assert = (await import('node:assert/strict')).default;
  let n = 0;
  const t = async (name, fn) => { await fn(); n++; console.log('  ok rx:', name); };
  const arr = (u) => Array.from(u);
  // DX7-style single-voice-like message: F0 43 0n 00 bh bl data cs F7
  const dx = (dev, data) => { const cs = X.yamahaChecksum(data); return Uint8Array.from([0xF0, 0x43, dev - 1, 0x00, data.length >> 7, data.length & 127, ...data, cs, 0xF7]); };

  await t('assembler: split packets, interleaved realtime, abort on new F0', () => {
    const rt = [];
    const A = new X.SysexAssembler({ onRealtime: (b) => rt.push(b) });
    const m1 = [0xF0, 0x43, 0x00, 0x01, 0x02, 0x03, 0xF7], m2 = [0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7];
    assert.deepEqual(A.push([0x90, 36, 100, 0xF0, 0x43]), []);
    assert.equal(A.pending, 2);
    assert.deepEqual(A.push([0x00, 0xF8, 0x01]), []); // clock inside a SysEx
    assert.deepEqual(A.realtime, [0xF8]);
    const got = A.push([0x02, 0xFE, 0x03, 0xF7, ...m2.slice(0, 3)]);
    assert.deepEqual(got.map(arr), [m1]);
    assert.deepEqual(A.push(m2.slice(3)).map(arr), [m2]);
    assert.deepEqual(rt, [0xF8, 0xFE]);
    // a new F0 aborts the partial message; another status byte aborts too
    assert.deepEqual(A.push([0xF0, 1, 2, 0xF0, 5, 0xF7, 0xF0, 9, 0x80, 0xF7]).map(arr), [[0xF0, 5, 0xF7]]);
    assert.equal(A.aborted, 2);
    // .syx parse/write
    const syx = X.writeSyx([m1, m2]);
    assert.equal(syx.length, m1.length + m2.length);
    assert.deepEqual(X.parseSyx(syx).map(arr), [m1, m2]);
  });

  await t('Yamaha checksum helper on a DX7-style message', () => {
    // DX7 VCED-style: F0 43 00 00 01 1B <155 bytes> cs F7 (count 0x011B = 155)
    const data = Array.from({ length: 155 }, (_, i) => (i * 13 + 7) & 127);
    const m = dx(1, data);
    assert.equal(m[4], 0x01); assert.equal(m[5], 0x1B);
    let sum = 0; for (const d of data) sum += d;
    assert.equal((sum + m[m.length - 2]) & 127, 0);
    assert.equal(X.yamahaChecksum([0x01, 0x02, 0x7F]), (128 - ((1 + 2 + 127) & 127)) & 127);
    assert.equal(X.verifyYamahaChecksum(m), 'ok');
    const bad = m.slice(); bad[10] ^= 1;
    assert.equal(X.verifyYamahaChecksum(bad), 'bad');
    assert.equal(X.verifyYamahaChecksum(Uint8Array.from([0xF0, 0x43, 0x00, 0x7F, 1, 2, 3, 0xF7])), 'unknown-layout');
    assert.equal(X.verifyBulk([m, m]), 'ok'); assert.equal(X.verifyBulk([m, bad]), 'bad');
    assert.equal(X.yamahaInfo(m).device, 1);
    assert.deepEqual(arr(X.dumpRequest(3, 0x09)), [0xF0, 0x43, 0x22, 0x09, 0xF7]);
  });

  await t('librarian: receive, device filter, byte-identical retransmit, .syx, cart round trip', async () => {
    const s = S.newState(); s.midi.deviceNo = 2;
    const a = dx(2, Array.from({ length: 300 }, (_, i) => i & 127)), b = dx(2, [1, 2, 3]), other = dx(5, [9]);
    const rcv = new X.BulkReceiver({ gapMs: 1000 });
    assert.equal(X.handleSysex(s, other, { receiver: rcv, now: 0 }).kind, 'ignored');
    let r = X.handleSysex(s, a, { receiver: rcv, now: 0 });
    assert.equal(r.kind, 'yamaha-bulk'); assert.equal(r.checksum, 'ok');
    r = X.handleSysex(s, b, { receiver: rcv, now: 500 });
    assert.equal(r.count, 2); assert.equal(s.rxBulk.status, 'ok'); assert.match(s.rxBulk.received, /^\d{4}-/);
    assert.deepEqual(X.transmitBulk(s).map(arr), [arr(a), arr(b)]);
    assert.deepEqual(arr(X.rxBulkToSyx(s)), [...a, ...b]);
    // dump request from the other side -> stored dump, verbatim
    const req = X.handleSysex(s, X.dumpRequest(2, 0), {});
    assert.equal(req.kind, 'yamaha-request'); assert.deepEqual(req.reply.map(arr), [arr(a), arr(b)]);
    // device renumbering on transmit only touches byte 2 (checksum still ok)
    const re = X.transmitBulk(s, { deviceNo: 7 });
    assert.equal(re[0][2], 6); assert.equal(X.verifyYamahaChecksum(re[0]), 'ok');
    assert.deepEqual(X.transmitBulk(s).map(arr), [arr(a), arr(b)]); // stored copy unchanged
    // a long gap starts a new dump; a corrupted message is reported
    const bad = b.slice(); bad[6] ^= 1;
    r = X.handleSysex(s, bad, { receiver: rcv, now: 5000 });
    assert.equal(r.count, 1); assert.equal(r.message, 'checksum error!'); assert.equal(s.rxBulk.status, 'bad');
    // deviceNo 0 (OFF) accepts any device
    const s0 = S.newState(); s0.midi.deviceNo = 0;
    assert.equal(X.handleSysex(s0, other).kind, 'yamaha-bulk');
    // .syx load + survives the cartridge file and the full Omega dump
    const d = S.newState();
    const lr = X.loadSyx(d, X.writeSyx([a, b, X.omegaPattern(s, 0)]));
    assert.equal(lr.yamaha, 2); assert.equal(lr.omega, 1); assert.equal(lr.status, 'ok');
    const back = await S.loadCart(await S.saveCart(d, { gzip: false }));
    assert.deepEqual(X.transmitBulk(back).map(arr), [arr(a), arr(b)]);
    const viaSx = S.parseSysex(X.omegaFull(d));
    assert.deepEqual(X.transmitBulk(viaSx).map(arr), [arr(a), arr(b)]);
  });

  await t('identity request / reply', () => {
    const s = S.newState(); s.midi.deviceNo = 0;
    const r = X.handleSysex(s, X.IDENTITY_REQUEST);
    assert.equal(r.kind, 'identity');
    assert.deepEqual(arr(r.reply[0]), [0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x7D, 0x4F, 0x35, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0xF7]);
    s.midi.deviceNo = 3;
    assert.equal(X.handleSysex(s, [0xF0, 0x7E, 0x02, 0x06, 0x01, 0xF7]).reply[0][2], 0x02);
    assert.equal(X.handleSysex(s, [0xF0, 0x7E, 0x05, 0x06, 0x01, 0xF7]).kind, 'ignored');
    const id = X.handleSysex(s, X.omegaRequest(X.OMEGA.IDENTITY)).reply[0];
    assert.equal(X.parseOmega(id).identity.name, 'OMEGA FIVE');
  });

  await t('Omega partial dumps, param change, requests, legacy full dump', () => {
    const s = S.newState(); S.randomize(s, 99, 'all');
    s.voice[5].pitch = -1234; s.fx.rev.time = 3.3;
    const d = S.newState();
    for (const m of [X.omegaPattern(s, 1), X.omegaSong(s, 0), X.omegaVoices(s), X.omegaFx(s)]) {
      assert.ok(m.slice(1, -1).every((x) => x < 0x80));
      const r = X.handleSysex(d, m);
      assert.equal(r.kind, 'omega'); assert.ok(!r.error, r.error);
    }
    assert.deepEqual(d.patterns[1], s.patterns[1]); assert.deepEqual(d.songs[0], s.songs[0]);
    assert.deepEqual(d.voice, s.voice); assert.deepEqual(d.channel, s.channel); assert.deepEqual(d.fx, s.fx);
    // checksum error
    const bad = X.omegaPattern(s, 2); bad[8] ^= 1;
    assert.equal(X.handleSysex(d, bad).error, 'checksum error!');
    // param change
    for (const [p, v] of [[{ target: 'voice', index: 3, name: 'pitch' }, -3600], [{ target: 'channel', index: 11, name: 'pan' }, -0.571],
      [{ target: 'rev', index: 0, name: 'time' }, 12.5], [{ target: 'global', index: 0, name: 'tempo' }, 133], [{ target: 'key', index: 4, name: 'pitch' }, 250],
      [{ target: 'midi', index: 0, name: 'tunable' }, true]]) {
      const r = X.handleSysex(d, X.omegaParam(p, v));
      assert.ok(!r.error, r.error); assert.equal(r.param.target, p.target); assert.equal(r.param.value, +v);
    }
    assert.equal(d.voice[3].pitch, -3600); assert.equal(d.channel[11].pan, -0.571); assert.equal(d.fx.rev.time, 12.5);
    assert.equal(d.tempo, 133); assert.equal(d.setups[0].keys[4].pitch, 250); assert.equal(d.midi.tunable, true);
    assert.ok(X.handleSysex(d, X.omegaParam({ target: 'global', name: '__proto__' }, 1)).error);
    // requests
    assert.deepEqual(arr(X.handleSysex(s, X.omegaRequest(X.OMEGA.PATTERN, 1)).reply[0]), arr(X.omegaPattern(s, 1)));
    assert.equal(X.handleSysex(s, X.omegaRequest(X.OMEGA.FULL)).reply[0][4], 1);
    // full dump: new (cmd 01) and legacy seq.sysexDump forms both load
    const strip = (x) => JSON.parse(JSON.stringify({ ...x, samples: null }));
    const f1 = S.newState(); X.handleSysex(f1, X.omegaFull(s)); assert.deepEqual(strip(f1), strip(s));
    const f2 = S.newState(); const r2 = X.handleSysex(f2, S.sysexDump(s)); assert.equal(r2.what, 'all data'); assert.deepEqual(strip(f2), strip(s));
    assert.deepEqual(strip(S.parseSysex(X.omegaFull(s))), strip(s));
  });

  await t('cart bank build -> parse round trip (12-bit + 8-bit, checksum, 256 KB)', () => {
    const N = 3001, sine = new Float32Array(N), saw = new Float32Array(2000);
    for (let i = 0; i < N; i++) sine[i] = Math.sin(i * 0.05) * 0.9 * Math.exp(-i / 2000);
    for (let i = 0; i < saw.length; i++) saw[i] = ((i % 50) / 25 - 1) * 0.7;
    const voices = [
      { name: 'SINE12', bits: 12, pcm: sine, params: { pitch: -1230, level: 25, ar: 99, d1r: 30, d1l: 50, d2r: 40, rr: 55, gate: 0, bendRate: 10, bendRange: -1200, loop: 1 }, out: 3 },
      { name: 'SAW8', bits: 8, pcm: saw, params: { pitch: 2400, level: 31, ar: 80, d1r: 0, d1l: 60, d2r: 0, rr: 20, gate: 0, loop: 0 } },
      { name: 'HALF', bits: 12, pcm: saw, sr: 50000, params: {} },
    ];
    const bank = C.buildBank(voices);
    assert.equal(bank.length, 131072);
    assert.deepEqual(arr(bank.subarray(0, 4)), [0, 0, 0, 0]); assert.equal(bank[5], 3);
    let sum = 0; for (let i = 0; i < 0x3FE; i++) sum += bank[i];
    assert.equal((bank[0x3FE] << 8) | bank[0x3FF], sum & 0xFFFF);
    const r = C.parseBank(bank);
    assert.ok(r.checksumOk); assert.equal(r.voices.length, 3);
    const [a, b, c] = r.voices;
    assert.equal(a.name, 'SINE12'); assert.equal(a.bits, 12); assert.equal(a.sr, 25000); assert.equal(a.pcm.length, N + 1); // padded to a pair
    for (let i = 0; i < N; i++) assert.ok(Math.abs(a.pcm[i] - sine[i]) <= 1 / 2048, '12-bit sample ' + i);
    assert.equal(a.pcm[N], 0);
    assert.equal(b.bits, 8); assert.equal(b.pcm.length, saw.length);
    for (let i = 0; i < saw.length; i++) assert.ok(Math.abs(b.pcm[i] - saw[i]) <= 1 / 128);
    assert.deepEqual({ ...a.params }, { pitch: -1230, level: 25, ar: 99, d1r: 30, d1l: 50, d2r: 40, rr: 55, gate: 0, bendRate: 10, bendRange: -1200, reverse: 0, loop: 1, bits: 12 });
    assert.equal(a.out, 3); assert.equal(b.params.pitch, 2400); assert.equal(b.params.loop, 0);
    assert.equal(a.loopStart, Math.floor(N * 0.7) & ~1); assert.equal(a.loopEnd, N);
    assert.equal(c.pcm.length, 1000); // 50 kHz resampled to 25 kHz
    assert.equal(C.gateFromCode(C.codeFromGate(3000)) > 2900, true);
    // parse -> build is byte-identical (raw kept), also with a corrupted checksum detected
    assert.deepEqual(arr(C.buildBank(r.voices, { bankId: r.id, fill: r.banks[0].image })), arr(bank));
    const broken = bank.slice(); broken[0x3FF] ^= 1;
    assert.equal(C.parseBank(broken).checksumOk, false);
    // 2-bank (256 KB) image
    const two = new Uint8Array(262144); two.set(bank, 0); two.set(C.buildBank([voices[1]], { bankId: 0x11 }), 131072);
    const r2 = C.parseBank(two);
    assert.equal(r2.banks.length, 2); assert.equal(r2.voices.length, 4); assert.equal(r2.banks[1].id, 0x11); assert.ok(r2.checksumOk);
    assert.throws(() => C.parseBank(new Uint8Array(1000)), /bank/);
    assert.throws(() => C.buildBank([{ name: 'BIG', bits: 8, pcm: new Float32Array(140000) }]), /bank holds/);
  });

  await t('midiIn actions: notes, tunable, song select, SPP, transport, device filter; midiOutForEvent', () => {
    const s = S.newState(); s.midi.rxCh = 10;
    const ty = (a) => a.map((x) => x.type);
    // VOICE mode with RX5 notes; running status; vel 0 = off; other channel ignored
    let a = S.midiIn(s, null, [0x99, 45, 100, 59, 90, 45, 0, 0x90, 45, 100]);
    assert.deepEqual(a, [{ type: 'note', v: 0, vel: 100 }, { type: 'note', v: 19, vel: 90 }, { type: 'off', v: 0 }]);
    s.noteMap[12] = 45; // two voices on one note, like the hardware allows
    assert.deepEqual(S.midiIn(s, null, [0x99, 45, 64]).map((x) => x.v), [0, 12]);
    s.noteMap = S.NOTE_PRESETS.gm.slice();
    assert.deepEqual(S.midiIn(s, null, [0x99, 82, 64]), [{ type: 'note', v: 11, vel: 64 }]); // GM extra
    // program change only in tunable mode; tunable notes 36..96 pitched around 60
    assert.deepEqual(S.midiIn(s, null, [0xC9, 5]), []);
    s.midi.tunable = true; s.midi.tunableVoice = 4;
    a = S.midiIn(s, null, [0xC9, 5, 0x99, 60, 100, 72, 80, 35, 80, 96, 10, 0x89, 72, 64]);
    assert.deepEqual(a, [{ type: 'program', n: 5 }, { type: 'note', v: 4, vel: 100, pitch: 0 }, { type: 'note', v: 4, vel: 80, pitch: 1200 },
      { type: 'note', v: 4, vel: 10, pitch: 3600 }, { type: 'off', v: 4 }]);
    // song select 0..19 (always), SPP + transport + clock only when synced to MIDI
    assert.deepEqual(S.midiIn(s, null, [0xF3, 7, 0xF3, 20, 0xF2, 0x10, 0x01, 0xFA, 0xF8]), [{ type: 'song', n: 7 }]);
    s.sync = 'midi';
    assert.deepEqual(S.midiIn(s, null, [0xF2, 0x10, 0x01, 0xFA, 0xF8, 0xFC, 0xFB, 0xFE]),
      [{ type: 'spp', n: 144 }, { type: 'start' }, { type: 'clock' }, { type: 'stop' }, { type: 'cont' }, { type: 'sense' }]);
    // SysEx with device-number filter (realtime inside is still delivered)
    s.midi.deviceNo = 2;
    a = S.midiIn(s, null, [0xF0, 0x43, 0x01, 0x7F, 0xF8, 0x01, 0xF7, 0xF0, 0x43, 0x04, 0x7F, 0xF7, 0xF0, 0x7D, 0x4F, 0x35, 7, 8, 0, 0x71, 0xF7]);
    assert.deepEqual(ty(a), ['clock', 'sysex', 'sysex']);
    assert.deepEqual(arr(a[1].bytes), [0xF0, 0x43, 0x01, 0x7F, 0x01, 0xF7]);
    s.midi.deviceNo = 0;
    assert.equal(S.midiIn(s, null, [0xF0, 0x43, 0x04, 0x7F, 0xF7]).length, 1);
    // applyMidiActions drives a Player
    const p = S.newState(); S.stepInsert(p, 0, 6, 0, 100);
    const pl = new S.Player(p); pl.setMode('pattern', 0); pl.setExternal(true);
    const evs = S.applyMidiActions(pl, [{ type: 'spp', n: 1 }, { type: 'start' }]);
    assert.equal(evs.length, 0); assert.ok(pl.running);
    assert.equal(S.applyMidiActions(pl, [{ type: 'spp', n: 1 }, { type: 'clock' }]).length, 1);
    // out: note on with accent, note off 8n v=64 after the gate time
    const o = S.newState(); o.midi.notesOut = true; o.midi.txCh = 10; o.midi.voiceTxCh[1] = 2; o.midi.gateMs = 50;
    assert.deepEqual(S.midiOutForEvent(o, { offset: 0, v: 1, vel: 100, acc: 2 }), [{ bytes: [0x91, 52, 127], delayMs: 0 }, { bytes: [0x81, 52, 64], delayMs: 50 }]);
    assert.deepEqual(S.midiOutForEvent(o, { offset: 0, v: -1, vel: 0, acc: 0, click: 1 }), []);
    assert.deepEqual(S.noteOffBytes(10, 45), [0x89, 45, 64]); assert.deepEqual(S.songSelectBytes(19), [0xF3, 19]);
  });

  await t('SMF map option: auto picks RX5 vs GM; export map', () => {
    const s = S.newState();
    S.stepInsert(s, 0, 0, 0, 100); S.stepInsert(s, 0, 24, 13, 100); S.stepInsert(s, 0, 48, 19, 100);
    for (const map of ['rx5', 'gm']) {
      const bytes = S.exportSMF(s, { what: 'pattern', n: 0, map });
      const d = S.newState();
      const r = S.importSMF(bytes, { state: d });
      assert.equal(r.map, map);
      assert.deepEqual(d.patterns[r.patterns[0]].events.map((e) => e.v), [0, 13, 19]);
    }
    const gmBytes = S.exportSMF(s, { what: 'pattern', n: 0 });
    assert.ok(Array.from(gmBytes).join(',').includes([0x99, 35].join(','))); // default export = GM (BD 1 = 35)
    const d = S.newState(); const r = S.importSMF(gmBytes, { state: d, map: 'rx5' });
    assert.equal(r.map, 'rx5'); assert.match(r.report, /RX5 map/);
  });

  console.log(`rx: ${n} tests passed`);
}
// ---- end rx tests ----

// ---- kit tests ----
async function kitTests() {
  const K = await import('../kits.js');
  const fs = await import('node:fs');
  const assert = (await import('node:assert/strict')).default;
  let n = 0;
  const t = async (name, fn) => { await fn(); n++; console.log('  ok kit:', name); };
  // WAV writer for synthetic inputs (test signals only)
  const wav = (x, sr, bits, opts = {}) => {
    const ch = opts.ch || 1, fl = opts.float, ext = opts.ext, bps = bits / 8, fmtLen = ext ? 40 : 16, dl = x.length * ch * bps;
    const b = Buffer.alloc(12 + 8 + fmtLen + 8 + dl + (dl & 1));
    b.write('RIFF', 0); b.writeUInt32LE(b.length - 8, 4); b.write('WAVE', 8); b.write('fmt ', 12); b.writeUInt32LE(fmtLen, 16);
    b.writeUInt16LE(ext ? 0xFFFE : fl ? 3 : 1, 20); b.writeUInt16LE(ch, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * ch * bps, 28);
    b.writeUInt16LE(ch * bps, 32); b.writeUInt16LE(bits, 34);
    if (ext) { b.writeUInt16LE(22, 36); b.writeUInt16LE(bits, 38); b.writeUInt16LE(fl ? 3 : 1, 44); }
    let p = 20 + fmtLen; b.write('data', p); b.writeUInt32LE(dl, p + 4); p += 8;
    for (const v of x) for (let c = 0; c < ch; c++, p += bps) {
      if (fl) b.writeFloatLE(v, p);
      else if (bits === 8) b[p] = Math.round(v * 127) + 128;
      else if (bits === 16) b.writeInt16LE(Math.round(v * 32767), p);
      else if (bits === 24) b.writeIntLE(Math.round(v * 8388607), p, 3);
      else b.writeInt32LE(Math.round(v * 2147483647), p);
    }
    return new Uint8Array(b);
  };
  const tone = (len, f, sr) => Float32Array.from({ length: len }, (_, i) => 0.5 * Math.sin(2 * Math.PI * f * i / sr) * (1 - i / len));

  await t('wav formats', () => {
    const x = tone(300, 440, 48000);
    for (const [bits, o, tol] of [[8, {}, 1e-2], [16, {}, 1e-4], [24, {}, 1e-6], [32, {}, 1e-8], [32, { float: true }, 1e-7], [24, { ext: true }, 1e-6], [16, { ch: 2 }, 1e-4]]) {
      const a = K.parseWav(wav(x, 48000, bits, o));
      assert.equal(a.sr, 48000); assert.equal(a.frames, 300);
      for (let i = 0; i < 300; i++) assert.ok(Math.abs(a.data[i] - x[i]) < tol, `${bits} ${JSON.stringify(o)} @${i}`);
    }
  });

  await t('names', () => {
    assert.equal(K.rx5Waveform('BDrum1-RX5.wav').v, 0); assert.equal(K.rx5Waveform('OHat-RX5.wav').v, 19);
    assert.equal(K.rx5Waveform('RideBell-RX5.wav').v, 20); assert.equal(K.rx5Waveform('Clap-RX5.wav').v, 22);
    assert.equal(K.rx5Waveform('BDrum3-RX5.wav').id, 25); assert.equal(K.rx5Waveform('DXmrrmb-RX5.wav').name, 'DXmrmb');
    assert.equal(K.rx5Waveform('FMPrc2-RX5.wav').name, 'FMprc2'); assert.equal(K.rx5Waveform('kick'), null);
    assert.deepEqual(K.aliasVoices('Kick 01.wav'), [0, 12]); assert.deepEqual(K.aliasVoices('Open Hat.wav'), [19, 7]);
    assert.deepEqual(K.aliasVoices('hihat_closed.wav'), [7, 19]); assert.deepEqual(K.aliasVoices('Tom Floor.wav'), [6, 18]);
    assert.deepEqual(K.aliasVoices('snare2.wav'), [13, 1]); assert.equal(K.aliasVoices('pad.wav'), null);
  });

  await t('zip (stored + deflate, prepended data) -> kit at 25 kHz', async () => {
    const files = [{ name: 'k/Kick.wav', bytes: wav(tone(4800, 60, 48000), 48000, 24) }, { name: 'k/Snare.wav', bytes: wav(tone(2000, 200, 44100), 44100, 16) },
      { name: 'k/Cuica-RX5.wav', bytes: wav(tone(1000, 500, 25000), 25000, 16) }, { name: 'k/weird.wav', bytes: wav(tone(500, 900, 32000), 32000, 32, { float: true }) }];
    for (const deflate of [false, true]) {
      const z = await K.makeZip(files, { deflate });
      const pre = new Uint8Array(z.length + 777); pre.set(z, 777); pre.fill(0x42, 0, 777);
      const back = await K.readZip(pre);
      assert.deepEqual(back.map((f) => f.name), files.map((f) => f.name));
      back.forEach((f, i) => assert.deepEqual(f.bytes, files[i].bytes));
      const kit = await K.loadKit(pre);
      assert.deepEqual(kit.assignments.map((a) => a.v), [0, 1]);
      const cu = kit.waveforms.find((w) => w.name === 'Cuica');
      assert.equal(cu.id, 35); assert.equal(cu.kind, 'crt');
      assert.equal(kit.waveforms.find((w) => w.file === 'weird.wav').id, 24);
      for (const w of kit.waveforms) { assert.equal(w.sr, 25000); assert.ok(w.native); }
      const kick = kit.waveforms[kit.assignments[0].libIndex];
      assert.ok(Math.abs(kick.data.length - 2500) < 30, `${kick.data.length}`);
      const hifi = await K.loadKit(pre, { keepRate: true });
      assert.equal(hifi.waveforms[0].sr, 48000);
    }
  });

  await t('resample keeps a tone and removes aliases', () => {
    const sr = 48000, x = Float32Array.from({ length: 9600 }, (_, i) => 0.5 * Math.sin(2 * Math.PI * 1000 * i / sr) + 0.5 * Math.sin(2 * Math.PI * 20000 * i / sr));
    const y = K.resample(x, sr, 25000);
    const amp = (f) => { let c = 0, s = 0; for (let i = 1000; i < 4000; i++) { c += y[i] * Math.cos(2 * Math.PI * f * i / 25000); s += y[i] * Math.sin(2 * Math.PI * f * i / 25000); } return 2 * Math.hypot(c, s) / 3000; };
    assert.ok(Math.abs(amp(1000) - 0.5) < 0.01, `1k ${amp(1000)}`);
    assert.ok(amp(5000) < 0.001, `alias ${amp(5000)}`);              // 20 kHz would fold to 5 kHz
  });

  await t('applyKit', async () => {
    const kit = await K.loadKit([{ name: 'BD.wav', bytes: wav(tone(2000, 60, 25000), 25000, 16) }, { name: 'Gun-RX5.wav', bytes: wav(tone(200, 60, 25000), 25000, 16) }]);
    const calls = [], eng = { setUserSample: (v, d, sr) => calls.push([v, d && d.length, sr]) }, st = { samples: new Array(24).fill(null) };
    K.applyKit(eng, st, kit, { mode: 'library' });
    assert.equal(calls.length, 0); assert.equal(st.library.length, 2);
    K.applyKit(eng, st, kit);
    assert.deepEqual(calls.map((c) => c[0]), [0]); assert.equal(st.samples[0].sr, 25000); assert.equal(st.samples[0].lib, 0);
    K.assignWaveform(eng, st, 0, null); assert.equal(st.samples[0], null);
  });

  const kitDir = new URL('../kit/', import.meta.url);
  if (fs.existsSync(new URL('kit.json', kitDir))) await t('factory kit', async () => {
    const kit = await K.loadFactoryKit();
    assert.equal(kit.assignments.length, 24); assert.equal(kit.waveforms.length, 51);
    assert.equal(kit.waveforms.filter((w) => w.kind === 'crt').length, 27);
    for (const w of kit.waveforms) { assert.equal(w.sr, 25000); assert.ok(w.native); }
    const E = await import('../engine.js');
    if (E.FACTORY_MAP) for (const a of kit.assignments) assert.equal(kit.waveforms[a.libIndex].file.toLowerCase(), String(E.FACTORY_MAP[a.v]).split('/').pop().toLowerCase());
  });
  else console.log('  skip kit: factory kit (five/kit/ absent)');

  const bw = '/tmp/claude-0/-home-user-The-System/e745d8bf-4798-5de7-902d-58dd7b7809d9/scratchpad/drive/full.bwpreset';
  if (fs.existsSync(bw)) await t('Bitwig full.bwpreset', async () => {
    const d = fs.readFileSync(bw), p = K.parseBitwigPreset(d);
    assert.equal(p.pads.length, 51); assert.equal(p.pads[0].note, 36); assert.equal(p.pads[0].sample, 'BDrum1-RX5.wav');
    assert.equal(p.pads[0].params.GAIN, 1.11);
    const kit = await K.loadKit([{ name: 'full.bwpreset', bytes: d }]);
    assert.equal(kit.assignments.length, 24); assert.equal(kit.waveforms.length, 51);
  });
  else console.log('  skip kit: Bitwig presets (scratchpad files absent)');
  console.log(`kit: ${n} tests passed`);
}
// ---- end kit tests ----

main();
