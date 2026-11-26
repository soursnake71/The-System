// Omega Five Node tests: node five/tools/test.mjs
// engineTests() (engine.js) and seqTests() (seq.js) live in this file; main() runs whichever exist.

async function main() {
  let failed = false;
  for (const name of ['engineTests', 'seqTests']) {
    let fn;
    try { fn = eval(name); } catch { continue; } // not defined (yet)
    if (typeof fn !== 'function') continue;
    console.log(`# ${name}`);
    try { await fn(); } catch (e) { failed = true; console.error(`FAIL ${name}:`, e); }
  }
  if (failed) process.exit(1);
  console.log('all tests passed');
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
    assert.equal(s.patterns.length, 100); assert.equal(s.songs.length, 20); assert.equal(s.chains.length, 4);
    assert.equal(s.noteMap[0], 35); assert.equal(s.noteMap[19], 46); assert.equal(S.PPQN, 24);
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
