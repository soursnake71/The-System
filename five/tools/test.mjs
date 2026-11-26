// Omega Five Node tests: node five/tools/test.mjs
// engineTests() (engine.js) and seqTests() (seq.js) live in this file; main() runs whichever exist.

async function main() {
  let failed = false;
  for (const name of ['engineTests', 'seqTests', 'rxTests']) {
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

  await t('exports and tables', () => {
    for (const k of ['Engine', 'VOICE_NAMES', 'CHANNEL_NAMES', 'VOICE_DEFAULTS', 'REV_PROGRAMS', 'ROM_SR', 'makeVoiceSample', 'rng', 'REV_DEFAULTS', 'DLY_DEFAULTS']) assert.ok(k in E, k);
    assert.equal(E.VOICE_NAMES.length, 24); assert.ok(E.VOICE_NAMES.every((s) => s.length <= 8));
    assert.equal(E.CHANNEL_NAMES.length, 12); assert.equal(E.VOICE_DEFAULTS.length, 24); assert.equal(E.ROM_SR, 25000);
    assert.deepEqual(E.REV_PROGRAMS.map((p) => p.name), ['HALL1', 'HALL2', 'ROOM1', 'ROOM2', 'VOCAL', 'PLATE1', 'PLATE2', 'ER1', 'ER2', 'GATE', 'REV GATE']);
  });

  await t('rng is xorshift32', () => {
    const r = E.rng(1); let s = 1n; const M = 0xFFFFFFFFn;
    for (let i = 0; i < 1000; i++) { s ^= (s << 13n) & M; s ^= s >> 17n; s ^= (s << 5n) & M; assert.equal(r(), Number(s) / 4294967296); }
  });

  await t('voice ROMs: deterministic, finite, no DC, end in silence', () => {
    for (let v = 0; v < 24; v++) {
      const a = E.makeVoiceSample(v), b = E.makeVoiceSample(v);
      assert.deepEqual(a, b); assert.ok(finite(a), E.VOICE_NAMES[v]);
      const p = peak(a); let m = 0; for (const x of a) m += x; m /= a.length;
      assert.ok(p > .5 && p <= 1, `${E.VOICE_NAMES[v]} peak ${p}`);
      assert.ok(Math.abs(m) < .01 * p, `${E.VOICE_NAMES[v]} DC ${m}`);
      assert.ok(peak(a, a.length - Math.ceil(a.length * .01)) < 1e-3, `${E.VOICE_NAMES[v]} tail`);
    }
  });

  await t('every voice through the engine: peak -12..-1 dBFS, finite, decays to silence', () => {
    const e = new E.Engine(sr);
    for (let v = 0; v < 24; v++) {
      for (const acc of [0, 1, 2]) {
        e.trigger(v, 127, acc); const { L, R } = run(e, 3);
        assert.ok(finite(L) && finite(R));
        const p = dB(Math.max(peak(L), peak(R)));
        assert.ok(p >= -12 && p <= -1, `${E.VOICE_NAMES[v]} acc${acc} peak ${p.toFixed(1)} dBFS`);
        assert.ok(peak(L, L.length - 4800) < 1e-4 && peak(R, R.length - 4800) < 1e-4, `${E.VOICE_NAMES[v]} tail`);
        let m = 0; for (const x of L) m += x; assert.ok(Math.abs(m / L.length) < 1e-3, 'DC');
      }
    }
  });

  await t('accent and velocity scale the level', () => {
    const lvl = (vel, acc) => { const e = new E.Engine(sr); e.trigger(1, vel, acc); return peak(run(e, .3).L); };
    const a0 = lvl(127, 0), a1 = lvl(127, 1), a2 = lvl(127, 2), v64 = lvl(64, 0);
    assert.ok(a1 > a0 * 1.2 && a2 > a1 * 1.2, `${a0} ${a1} ${a2}`); assert.ok(v64 < a0 * .65);
  });

  await t('deterministic, sample-accurate trigger offset', () => {
    const go = () => { const e = new E.Engine(sr); e.setChannel(1, { sendRev: .5, sendDly: .5 }); e.trigger(1, 100, 1, 77); e.trigger(7, 90, 0, 300); return run(e, 1); };
    const a = go(), b = go(); assert.deepEqual(a.L, b.L); assert.deepEqual(a.R, b.R);
    const i = a.L.findIndex((x) => x !== 0); assert.ok(i >= 77 && i < 90, 'first sample ' + i);
    const e = new E.Engine(sr); e.trigger(0, 127, 0, 200); const o = run(e, .01); assert.ok(o.L.findIndex((x) => x !== 0) >= 200, 'offset carries across blocks');
  });

  await t('hi-hat choke: closed cuts open (same channel)', () => {
    const e1 = new E.Engine(sr); e1.trigger(19); const open = run(e1, .6).L;
    const e2 = new E.Engine(sr); e2.trigger(19); e2.trigger(7, 127, 0, 4800); const ch = run(e2, .6).L;
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
    const r = new E.Engine(sr); r.setVoiceParam(0, 'reverse', 1); r.trigger(0); const rv = run(r, .8).L;
    let im = 0; for (let i = 0; i < rv.length; i++) if (Math.abs(rv[i]) > Math.abs(rv[im])) im = i; assert.ok(im > sr * .45, 'reverse peaks late ' + im);
    const q = new E.Engine(sr); q.setVoiceParam(0, 'bits', 8); assert.equal(q.getState().voices[0].bits, 8);
    const rom = q.roms[0]; assert.ok(rom.every((x) => Math.round(x * 128) === x * 128));
    q.setDac('float'); assert.equal(q.getState().dac, 'float'); q.trigger(0); assert.ok(finite(run(q, .5).L));
    q.setVoiceParam(3, 'gate', 50); assert.equal(q.voices[3].gate, 100); q.setVoiceParam(3, 'pitch', 9999); assert.equal(q.voices[3].pitch, 2400);
  });

  await t('state round-trips through JSON', () => {
    const e = new E.Engine(sr);
    e.setVoiceParam(5, 'pitch', -300); e.setVoiceParam(9, 'd2r', 40); e.setChannel(3, { fader: .5, pan: -.3, sendRev: .2, mute: true });
    e.setFx('rev', 'program', 'PLATE2'); e.setFx('rev', 'time', 1.7); e.setFx('dly', 'sync', '1/8.'); e.setFx('dly', 'feedback', 80); e.setMaster({ volume: .7, click: .3 }); e.setDac('float');
    const s = JSON.parse(JSON.stringify(e.getState()));
    assert.equal(s.rev.program, 6); assert.equal(s.dly.sync, 3); assert.equal(s.channels[3].mute, true);
    const f = new E.Engine(sr); f.setState(s); assert.deepEqual(f.getState(), e.getState());
    f.setState(JSON.parse(JSON.stringify(new E.Engine(sr).getState()))); assert.deepEqual(f.getState(), new E.Engine(sr).getState());
  });

  await t('reverb: every program rings, stays bounded and dies away', () => {
    for (let p = 0; p < E.REV_PROGRAMS.length; p++) {
      const e = new E.Engine(sr); e.setFx('rev', 'program', p); e.setChannel(1, { fader: 1, sendRev: 1 }); e.trigger(1);
      const { L, R } = run(e, 12); const name = E.REV_PROGRAMS[p].name;
      assert.ok(finite(L) && finite(R) && peak(L) < 1, name);
      assert.ok(rms(L, sr * .5, sr * .6) > 1e-4, `${name} has a tail`);
      assert.ok(peak(L, sr * 11) < 1e-3, `${name} decays (${peak(L, sr * 11)})`);
    }
    const e = new E.Engine(sr); e.setFx('rev', 'time', 30); e.setFx('rev', 'hiRatio', 1); e.setChannel(0, { sendRev: 1 });
    for (let k = 0; k < 20; k++) e.trigger(0, 127, 2, k * 50);
    const o = run(e, 20); assert.ok(finite(o.L) && peak(o.L) < 4, 'RT60 30 s stable ' + peak(o.L));
  });

  await t('TC delay: echo at the set time, 120 % feedback limited, sync to tempo', () => {
    const e = new E.Engine(sr); e.setFx('dly', 'timeL', 300); e.setFx('dly', 'timeR', 300); e.setFx('dly', 'modDepth', 0); e.setChannel(2, { pan: 0, sendDly: 1 }); e.trigger(2);
    const { L } = run(e, 1); let im = 4800; for (let i = 4800; i < 24000; i++) if (Math.abs(L[i]) > Math.abs(L[im])) im = i;
    assert.ok(Math.abs(im - .3 * sr) < 240, 'echo at ' + im);
    const f = new E.Engine(sr); f.setFx('dly', 'feedback', 120); f.setFx('dly', 'pingpong', 1); f.setChannel(0, { sendDly: 1 }); f.trigger(0, 127, 2);
    const o = run(f, 15); assert.ok(finite(o.L) && peak(o.L) < 2 && rms(o.L, sr * 14, sr * 15) > 1e-3, 'self-oscillates, bounded');
    const g = new E.Engine(sr); g.setTempo(100); g.setFx('dly', 'sync', 1); assert.ok(Math.abs(g.dlyFx.tL - .6 * sr) < 1);
    const d = new E.Engine(sr); d.setFx('dly', 'on', 0); d.setChannel(0, { sendDly: 1 }); d.trigger(0); const off = run(d, 1).L; assert.ok(peak(off, sr * .8) < 1e-4, 'off = no echoes');
  });

  await t('meters, voiceActive, click', () => {
    const e = new E.Engine(sr); e.trigger(0); run(e, .05);
    assert.ok(e.voiceActive(0) > .3 && e.voiceActive(5) === 0); const m = e.meters(); assert.ok(m.l > .05 && m.r > .05); assert.equal(e.meters().l, 0);
    run(e, 2); assert.ok(e.voiceActive(0) < .01);
    e.clickTick(true); const c = run(e, .1).L; assert.ok(peak(c) > .05 && peak(c, sr * .05) === 0);
  });

  await t('real-time budget (dense kit, all FX)', () => {
    const e = new E.Engine(sr); for (let ch = 0; ch < 12; ch++) e.setChannel(ch, { sendRev: .3, sendDly: .2 });
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

  console.log(`seq: ${n} tests passed`);
}

// ---- end seq tests ----

main();
