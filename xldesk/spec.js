/* SSL XL-Desk: every control on the desk, its range and default, every 500-series module, and the default MIDI map.
   This file is the single source of truth: the web page loads it, the web audio engine loads it, and
   plugin/tools/gen-spec.js turns it into plugin/Source/Spec.generated.h for the VST3, so all three agree.
   Every value is stored normalised 0..1 (switches 0/1); a 'law' turns it into the real value.
   Sources: XL-Desk Owner's Manual 82BMLM01B (incl. the fader scales printed in its panel artwork), the VHD Pre,
   E Series EQ, E Series Dynamics and G Series Bus Compressor 500 module guides, Sound On Sound's review.
   Values the documents don't give are marked "est." and listed in help.js. */
(() => {
const XL = {};
XL.REF = 24;                                   // internal float 1.0 = +24 dBu (0 dBFS on a +24 dBu converter)
XL.MODMAX = 16;                                // parameters per 500 location; a module uses the first n

/* ---------- laws: [type, a, b] -> value ---------- */
// fader scales measured from the manual's panel artwork (Mix A p.17, channel p.11): [position, dB]
XL.FAD = [[0, -Infinity], [.098, -30], [.208, -20], [.374, -10], [.492, -5], [.663, 0], [.794, 5], [1, 10]];
XL.FADA = [[0, -Infinity], [.091, -60], [.198, -50], [.310, -40], [.439, -30], [.572, -20], [.679, -15], [.786, -10], [.893, -5], [1, 0]];
const table = (T, p) => { if (p <= .0005) return -Infinity; for (let i = 1; i < T.length; i++) if (p <= T[i][0]){ const [p0, d0] = T[i - 1], [p1, d1] = T[i];
  if (d0 === -Infinity) return d1 + 40 * Math.log10(p / p1); return d0 + (d1 - d0) * (p - p0) / (p1 - p0); } return T[T.length - 1][1]; };
XL.tablePos = (T, d) => { for (let i = T.length - 1; i > 0; i--){ const [p0, d0] = T[i - 1], [p1, d1] = T[i]; if (d0 === -Infinity){ return d <= -200 ? 0 : p1 * Math.pow(10, (d - d1) / 40); } if (d >= d0) return p0 + (p1 - p0) * (d - d0) / (d1 - d0); } return 0; };
XL.law = (L, p) => {
  switch (L[0]){
    case 'lin': return L[1] + (L[2] - L[1]) * p;
    case 'log': return L[1] * Math.pow(L[2] / L[1], p);
    case 'bip': return (p - .5) * 2 * L[1];
    // centre-detent pots (Mix B/C/D, master trims, foldback, AFL, returns): infinity, 0 dB at 12 o'clock, +10 fully clockwise (panel artwork)
    case 'u10': return p <= .002 ? -Infinity : p >= .5 ? (p - .5) * 20 : 60 * Math.log10(p / .5);
    // channel send pots: unity at 3 o'clock, +6 dB max (est.)
    case 'send': return p <= .002 ? -Infinity : p >= .75 ? (p - .75) * 24 : 60 * Math.log10(p / .75);
    // monitor and headphone level, panel marked 0-11 (est. law): 0 dB fully clockwise
    case 'vol': return p <= .002 ? -Infinity : 40 * Math.log10(p);
    case 'fader': return table(XL.FAD, p);
    case 'faderA': return table(XL.FADA, p);
    case 'ratio': return p >= .999 ? Infinity : 1 / (1 - p);
    default: return p;
  }
};

/* ---------- 500-series modules ----------
   To add a module or a new revision: add an entry here (new type id, e.g. 'eeq2'), add its DSP class to
   dsp/console.js (web) and plugin/Source/Modules.h (VST3), register it, and list it in XL.WHERE. Saved sessions keep
   the type id, so old revisions keep loading as they were.
   params: n name, k 'pot'|'btn'|'step', d default (normalised; steps: index), law, v step values, c cap colour, lb label.
   layout: rows of param names for the faceplate ('-' = gap, '#LED' = the module's meter). */
const GSTEP = {att:[.1, .3, 1, 3, 10, 30], rel:[.1, .2, .4, .8, 1.6, 'AUTO'], ratio:[1.5, 2, 3, 4, 5, 10], hpf:['OFF', 30, 60, 105, 125, 185]};
XL.GSTEP = GSTEP;
XL.MOD = {
  // SSL VHD Pre (500): +20 to +75 dB, VHD 2nd/3rd harmonic drive, trim +/-20 dB, 18 dB/oct HPF 15-500 Hz,
  // HI-Z 1.2k -> 10k, +48 V, 20 dB pad, polarity; signal LED after the HPF (module guide block diagram)
  vhdpre:{name:'SSL VHD Pre', short:'VHD PRE', maker:'SSL', rev:1, cat:'pre', w:1, params:[
    {n:'drive', k:'pot', d:.5, law:['lin', 0, 1], c:'black', lb:'DRIVE', u:'h'}, {n:'vhd', k:'btn', d:0, lb:'VHD IN'},
    {n:'gain', k:'pot', d:.2, law:['lin', 20, 75], c:'red', lb:'GAIN', u:'dB'}, {n:'trim', k:'pot', d:.5, law:['bip', 20], c:'grey', lb:'TRIM', u:'dB'},
    {n:'hpfIn', k:'btn', d:0, lb:'HPF IN'}, {n:'hpf', k:'pot', d:.3, law:['log', 15, 500], c:'black', lb:'HPF', u:'Hz'},
    {n:'hiz', k:'btn', d:0, lb:'HI-Z'}, {n:'p48', k:'btn', d:0, lb:'+48V'}, {n:'pad', k:'btn', d:0, lb:'PAD'}, {n:'phase', k:'btn', d:0, lb:'Ø'}],
    layout:[['#LED'], ['drive', 'vhd'], ['gain'], ['trim'], ['hpfIn', 'hpf'], ['hiz'], ['p48'], ['pad'], ['phase']]},
  // SSL E Series EQ (611 E-EQ): Brown 02 EQ, BLK switches to the Black 242
  eeq:{name:'SSL E Series EQ', short:'E-EQ', maker:'SSL', rev:1, cat:'eq', w:1, params:[
    {n:'hfG', k:'pot', d:.5, law:['bip', 1], c:'red', lb:'HF', u:'eqdB'}, {n:'hfF', k:'pot', d:.5, law:['log', 1500, 16000], c:'red', lb:'kHz', u:'Hz'}, {n:'hfBell', k:'btn', d:0, lb:'BELL'},
    {n:'hmfG', k:'pot', d:.5, law:['bip', 1], c:'green', lb:'HMF', u:'eqdB'}, {n:'hmfF', k:'pot', d:.5, law:['log', 600, 7000], c:'green', lb:'kHz', u:'Hz'}, {n:'hmfQ', k:'pot', d:.5, law:['log', 2.5, .5], c:'green', lb:'Q', u:'Q'},
    {n:'blk', k:'btn', d:0, lb:'BLK'}, {n:'in', k:'btn', d:1, lb:'IN'},
    {n:'lmfG', k:'pot', d:.5, law:['bip', 1], c:'blue', lb:'LMF', u:'eqdB'}, {n:'lmfF', k:'pot', d:.5, law:['log', 200, 2500], c:'blue', lb:'kHz', u:'Hz'}, {n:'lmfQ', k:'pot', d:.5, law:['log', 2.5, .5], c:'blue', lb:'Q', u:'Q'},
    {n:'lfF', k:'pot', d:.5, law:['log', 30, 450], c:'brown', lb:'Hz', u:'Hz'}, {n:'lfG', k:'pot', d:.5, law:['bip', 1], c:'brown', lb:'LF', u:'eqdB'}, {n:'lfBell', k:'btn', d:0, lb:'BELL'}],
    layout:[['hfG', 'hfBell'], ['hfF'], ['hmfG', 'hmfF'], ['hmfQ', 'blk'], ['in'], ['lmfG', 'lmfF'], ['lmfQ'], ['lfF'], ['lfG', 'lfBell']]},
  // SSL E Series Dynamics (611 DYN)
  edyn:{name:'SSL E Series Dynamics', short:'E-DYN', maker:'SSL', rev:1, cat:'dyn', w:1, params:[
    {n:'ratio', k:'pot', d:0, law:['ratio'], c:'white', lb:'RATIO', u:':1'}, {n:'hard', k:'btn', d:0, lb:'HARD'},
    {n:'cthr', k:'pot', d:.5, law:['lin', 4, -26], c:'white', lb:'THRESH', u:'dBu'}, {n:'linrel', k:'btn', d:0, lb:'LIN REL'},
    {n:'crel', k:'pot', d:.3, law:['log', .1, 4], c:'white', lb:'RELEASE', u:'s'}, {n:'cfast', k:'btn', d:0, lb:'FAST'}, {n:'in', k:'btn', d:1, lb:'IN'},
    {n:'range', k:'pot', d:0, law:['lin', 0, 40], c:'green', lb:'RANGE', u:'dB'}, {n:'gthr', k:'pot', d:.5, law:['lin', -30, 10], c:'green', lb:'THRESH', u:'dBu'}, {n:'exp', k:'btn', d:0, lb:'EXP'},
    {n:'grel', k:'pot', d:.3, law:['log', .1, 4], c:'green', lb:'RELEASE', u:'s'}, {n:'gfast', k:'btn', d:0, lb:'FAST'}],
    layout:[['ratio', 'hard'], ['cthr', 'linrel'], ['crel', 'cfast'], ['in', '#LED'], ['range', 'exp'], ['gthr'], ['grel', 'gfast']]},
  // SSL G Series Bus Compressor (2-slot)
  gcomp:{name:'SSL G Series Bus Compressor', short:'G-COMP', maker:'SSL', rev:2, cat:'comp', w:2, st:1, params:[
    {n:'thr', k:'pot', d:.5, law:['lin', -20, 20], c:'blue', lb:'THRESHOLD', u:'dB'}, {n:'mk', k:'pot', d:.25, law:['lin', -5, 15], c:'blue', lb:'MAKE UP', u:'dB'},
    {n:'att', k:'step', d:4, v:GSTEP.att, c:'blue', lb:'ATTACK', u:'ms'}, {n:'rel', k:'step', d:5, v:GSTEP.rel, c:'blue', lb:'RELEASE', u:'s'},
    {n:'ratio', k:'step', d:3, v:GSTEP.ratio, c:'blue', lb:'RATIO', u:':1'}, {n:'hpf', k:'step', d:0, v:GSTEP.hpf, c:'blue', lb:'HPF', u:'Hz'}, {n:'in', k:'btn', d:1, lb:'IN'}],
    layout:[['#NEEDLE'], ['thr', 'mk'], ['in'], ['att', 'rel'], ['ratio', 'hpf']]}
};
for (const t in XL.MOD) XL.MOD[t].params.forEach((q, j) => { q.j = j; if (q.k === 'step') q.dn = q.d / (q.v.length - 1); else q.dn = q.d; });
// where each module can go
XL.WHERE = {};
const SLOT_TYPES = ['empty', 'vhdpre', 'eeq', 'edyn'];
for (let i = 1; i <= 16; i++) XL.WHERE['slot' + i] = SLOT_TYPES;
XL.WHERE.slot17 = ['empty', 'gcomp'];
for (let i = 1; i <= 16; i++) XL.WHERE['ins' + i] = ['none', 'loop', 'vhdpre', 'eeq', 'edyn'];
for (const b of 'ABCD') XL.WHERE['bins' + b] = ['none', 'loop', 'eeq', 'edyn', 'gcomp'];
XL.TYPE_NAME = {empty:'Empty slot', none:'Nothing patched', loop:'Patch cable (send to return)'}; for (const t in XL.MOD) XL.TYPE_NAME[t] = XL.MOD[t].name;
// fully loaded: SSL VHD Pre in slots 1-16, the G Series Bus Compressor in 17-18
XL.defTypes = () => { const t = {}; for (let i = 1; i <= 16; i++){ t['slot' + i] = 'vhdpre'; t['ins' + i] = 'edyn'; } t.slot17 = 'gcomp'; for (const b of 'ABCD') t['bins' + b] = 'loop'; return t; };
XL.modDefaults = type => { const o = {}; const M = XL.MOD[type]; for (let j = 0; j < XL.MODMAX; j++) o[j] = M && M.params[j] ? M.params[j].dn : 0; return o; };

/* ---------- every console parameter ---------- */
const P = XL.P = {};   // id -> {k:'pot'|'btn', d (normalised), law, lb, u}
const pot = (id, d, law, lb, u) => P[id] = {k:'pot', d, law, lb, u}, btn = (id, d, lb) => P[id] = {k:'btn', d:d ? 1 : 0, lb};
const SEND = ['send'], BIP20 = ['bip', 20], PAN = ['bip', 1];
for (let i = 1; i <= 16; i++){
  const m = 'm' + i + '.';
  if (i <= 8){ pot(m + 'gain', .25, ['lin', 15, 75], 'VHD gain', 'dB'); pot(m + 'drive', .5, ['lin', 0, 1], 'VHD harmonics', 'h'); btn(m + 'vhd', 0, 'VHD'); btn(m + 'hiz', 0, 'HI-Z'); btn(m + 'p48', 0, '+48V'); btn(m + 'pad', 0, 'PAD'); }
  pot(m + 'trim', .5, BIP20, 'Trim', 'dB'); btn(m + 'daw', 0, 'DAW'); btn(m + 'phase', 0, 'Ø'); btn(m + 's500', 0, '500'); btn(m + 'ins', 0, 'INS'); btn(m + 'chop', 0, 'CHOP POST');
  pot(m + 'cue', 0, SEND, 'Cue level', 'dB'); pot(m + 'cuepan', .5, PAN, 'Cue pan', 'pan'); btn(m + 'cuepost', 0, 'Cue POST'); btn(m + 'alt', 0, 'ALT');
  pot(m + 'aux1', 0, SEND, 'Aux 1', 'dB'); pot(m + 'aux2', 0, SEND, 'Aux 2', 'dB'); pot(m + 'pan', .5, PAN, 'Pan', 'pan');
  for (const b of 'ABCD') btn(m + b, b === 'A', 'Mix ' + b); btn(m + 'cut', 0, 'CUT'); btn(m + 'solo', 0, 'SOLO'); btn(m + 'safe', 0, 'Solo safe');
  pot(m + 'fader', XL.tablePos(XL.FAD, 0), ['fader'], 'Fader', 'dB');
}
for (let k = 1; k <= 4; k++){
  const s = 's' + k + '.';
  pot(s + 'trim', .5, BIP20, 'Trim', 'dB'); btn(s + 'phL', 0, 'Ø L'); btn(s + 'phR', 0, 'Ø R'); btn(s + 'mono', 0, 'MONO'); btn(s + 'chop', 0, 'CHOP POST');
  pot(s + 'cue', 0, SEND, 'Cue level', 'dB'); pot(s + 'cuepan', .5, PAN, 'Cue balance', 'pan'); btn(s + 'cuepost', 0, 'Cue POST');
  pot(s + 'aux1', 0, SEND, 'Aux 1', 'dB'); pot(s + 'aux2', 0, SEND, 'Aux 2', 'dB'); pot(s + 'pan', .5, PAN, 'Balance', 'pan');
  for (const b of 'ABCD') btn(s + b, b === 'A', 'Mix ' + b); btn(s + 'cut', 0, 'CUT'); btn(s + 'solo', 0, 'SOLO'); btn(s + 'safe', 0, 'Solo safe');
  pot(s + 'fader', XL.tablePos(XL.FAD, 0), ['fader'], 'Fader', 'dB');
}
for (let k = 1; k <= 2; k++){
  const r = 'r' + k + '.';
  pot(r + 'level', .5, ['u10'], 'Return level', 'dB'); pot(r + 'bal', .5, PAN, 'Balance', 'pan'); btn(r + 'mono', 0, 'MONO'); btn(r + 'cue', 0, 'ST CUE');
  for (const b of 'ABCD') btn(r + b, b === 'A', 'Mix ' + b); btn(r + 'afl', 0, 'AFL');
}
btn('A.s500', 0, 'Mix A 500'); btn('A.ins', 0, 'Mix A INS'); btn('A.sum', 0, 'Mix A SUM'); btn('A.comp', 0, 'COMP'); pot('A.fader', 1, ['faderA'], 'Mix A fader', 'dB');
for (const b of 'BCD'){ btn(b + '.s500', 0, 'Mix ' + b + ' 500'); btn(b + '.ins', 0, 'Mix ' + b + ' INS'); btn(b + '.sum', 0, 'Mix ' + b + ' SUM'); btn(b + '.toA', 0, 'Mix ' + b + ' TO A'); pot(b + '.level', .5, ['u10'], 'Mix ' + b + ' level', 'dB'); btn(b + '.afl', 0, 'Mix ' + b + ' AFL'); }
pot('cue.level', .5, ['u10'], 'St Cue master', 'dB'); btn('cue.afl', 0, 'Cue AFL'); btn('cue.toA', 0, 'Cue TO A');
for (const a of ['aux1', 'aux2']){ const n = a === 'aux1' ? 'Aux 1' : 'Aux 2'; pot(a + '.level', .5, ['u10'], n + ' master', 'dB'); btn(a + '.pre', 0, n + ' PRE'); btn(a + '.afl', 0, n + ' AFL'); }
btn('solo.afl', 0, 'AFL solo mode');
pot('mon.level', .55, ['vol'], 'Monitor level', 'dB'); btn('mon.dim', 0, 'DIM'); btn('mon.cut', 0, 'Monitor CUT'); pot('mon.dimamt', .5, ['lin', -40, 0], 'Dim level', 'dB'); pot('mon.afl', .5, ['u10'], 'AFL level', 'dB');
btn('mon.mono', 0, 'Monitor MONO'); btn('mon.mini1', 0, 'MINI 1'); btn('mon.mini2', 0, 'MINI 2'); btn('mon.subMain', 0, 'SUB (main)'); btn('mon.subMini1', 0, 'SUB (mini 1)'); btn('mon.subMini2', 0, 'SUB (mini 2)');
btn('mon.srcA', 1, 'Source MIX A'); btn('mon.srcExt', 0, 'Source EXT'); btn('mon.srcIj', 0, 'Source iJack'); btn('mon.sum', 0, 'Source SUM');
pot('mon.off1', .5, ['lin', -20, 20], 'Mini 1 level offset', 'dB'); pot('mon.off2', .5, ['lin', -20, 20], 'Mini 2 level offset', 'dB');
pot('hp.level', .5, ['vol'], 'Headphone level', 'dB'); btn('hp.fbA', 0, 'H/P F/B A'); btn('hp.fbB', 0, 'H/P F/B B');
for (const f of ['fbA', 'fbB']){ const n = f === 'fbA' ? 'A' : 'B'; pot(f + '.level', .5, ['u10'], 'Foldback ' + n, 'dB'); btn(f + '.mon', 0, 'F/B ' + n + ' MON'); btn(f + '.talk', 0, 'TALK ' + n); }
pot('talk.level', .5, ['lin', 0, 60], 'Talkback mic gain', 'dB'); pot('listen.level', .5, ['lin', 0, 60], 'Listen mic gain', 'dB'); btn('listen.on', 0, 'LISTEN');
for (let d = 1; d <= 12; d++) btn('dip.' + d, 0, 'DIP ' + d);
btn('sys.noise', 1, 'Analogue noise floor');
btn('rear.unlink1', 0, '500 links removed, slots 1-8'); btn('rear.unlink2', 0, '500 links removed, slots 9-16');
// 500 SLOT IN: fed from the channel's 500 send through the link cable, or from the rear connector (a mic or line plugged straight into the module)
for (let i = 1; i <= 16; i++) btn('slot' + i + '.ext', 1, 'Slot ' + i + ' input from 500 SLOT IN');
// generic module parameters: <where>.<0..15>; what each one means depends on the module loaded there
for (const w in XL.WHERE) for (let j = 0; j < XL.MODMAX; j++) P[w + '.' + j] = {k:'mod', d:0, lb:w + ' param ' + (j + 1)};
XL.defaults = () => { const o = {}; for (const id in P) o[id] = P[id].d; const T = XL.defTypes(); for (const w in T){ const d = XL.modDefaults(T[w]); for (let j = 0; j < XL.MODMAX; j++) o[w + '.' + j] = d[j]; } return o; };
XL.DIP = [['Meter scaling', '+24 dBu', '+18 dBu'], ['Talkback A', 'Sum', 'Replace'], ['Talkback A', 'Momentary', 'Latching'], ['Talkback B', 'Sum', 'Replace'], ['Talkback B', 'Momentary', 'Latching'],
  ['Unused', '-', '-'], ['MINI 2 speaker set', 'Stereo', 'Mono'], ['Unused', '-', '-'], ['Unused', '-', '-'], ['Unused', '-', '-'], ['Bargraph test mode', 'Normal', 'Enabled'], ['Programming mode', 'Normal', 'Enabled']];
// module parameter spec for a generic id, given what's loaded there
XL.modParam = (types, id) => { const m = /^(\w+)\.(\d+)$/.exec(id); if (!m) return null; const M = XL.MOD[types[m[1]]]; return M ? M.params[+m[2]] || null : null; };

/* ---------- meters ---------- */
// 20-segment bargraph, 0 dB = the selected peak level, -48 at the bottom (SOS); spacing est.
XL.SEG_DB = [0, -2, -4, -6, -8, -10, -12, -14, -16, -18, -20, -22, -24, -27, -30, -33, -36, -40, -44, -48];

/* ---------- default MIDI map (MIDI Learn overrides any of it) ----------
   MIDI channel n = mono channel n. Stereo channels, returns and the centre section use CCs 40-63, which the strips leave free. */
const MAP = XL.MIDIMAP = {};   // 'ch:cc' (ch 0-15) -> param id or @action
const STRIP_CC = {fader:7, pan:10, trim:16, gain:17, drive:18, cue:19, cuepan:20, aux1:21, aux2:22, daw:80, phase:81, s500:82, ins:83, chop:84, cuepost:85, alt:86, A:87, B:88, C:89, D:90, cut:91, solo:92, vhd:93, hiz:94, p48:95, pad:96};
XL.STRIP_CC = STRIP_CC; XL.SLOT_CC0 = 102;
for (let i = 1; i <= 16; i++){ for (const k in STRIP_CC) if (P['m' + i + '.' + k]) MAP[(i - 1) + ':' + STRIP_CC[k]] = 'm' + i + '.' + k; for (let j = 0; j < XL.MODMAX && XL.SLOT_CC0 + j < 120; j++) MAP[(i - 1) + ':' + (XL.SLOT_CC0 + j)] = 'slot' + i + '.' + j; }
const ST_CC = ['fader', 'pan', 'trim', 'cue', 'cuepan', 'aux1', 'aux2', 'mono', 'A', 'B', 'C', 'D', 'cut', 'solo', 'cuepost', 'chop', 'phL', 'phR'];
for (let k = 1; k <= 4; k++) ST_CC.forEach((n, j) => MAP[(k - 1) + ':' + (40 + j)] = 's' + k + '.' + n);
const RET_CC = ['level', 'bal', 'mono', 'cue', 'A', 'B', 'C', 'D', 'afl'];
for (let k = 1; k <= 2; k++) RET_CC.forEach((n, j) => MAP[(3 + k) + ':' + (40 + j)] = 'r' + k + '.' + n);
const C16 = ['A.fader', 'A.comp', 'A.ins', 'A.sum', 'A.s500', 'B.level', 'C.level', 'D.level', 'B.toA', 'C.toA', 'D.toA', 'B.afl', 'C.afl', 'D.afl', 'cue.level', 'aux1.level', 'aux2.level', 'cue.toA', 'aux1.pre', 'aux2.pre', 'cue.afl', 'aux1.afl', 'aux2.afl', 'solo.afl'];
const C15 = ['slot17.0', 'slot17.1', 'slot17.2', 'slot17.3', 'slot17.4', 'slot17.5', 'slot17.6', 'mon.level', 'mon.dim', 'mon.cut', 'mon.dimamt', 'mon.afl', 'mon.mono', 'mon.mini1', 'mon.mini2', '@sub', 'mon.srcA', 'mon.srcExt', 'mon.srcIj', 'mon.sum', 'hp.level', 'fbA.level', 'fbB.level', 'listen.on'];
const C14 = ['B.ins', 'B.sum', 'B.s500', 'C.ins', 'C.sum', 'C.s500', 'D.ins', 'D.sum', 'D.s500', 'fbA.mon', 'fbB.mon', 'fbA.talk', 'fbB.talk', 'talk.level', 'listen.level', 'hp.fbA', 'hp.fbB', '@soloclear', '@play', '@stop', '@rec', '@rtz', '@rew', '@ff'];
C16.forEach((id, j) => MAP['15:' + (40 + j)] = id); C15.forEach((id, j) => MAP['14:' + (40 + j)] = id); C14.forEach((id, j) => MAP['13:' + (40 + j)] = id);
globalThis.XL = XL;
})();
