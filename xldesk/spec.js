/* SSL XL-Desk: every control on the desk, its range, its default and its MIDI CC, shared by the page (script tag)
   and the audio engine (audioWorklet.addModule), so both read the same numbers.
   Ranges come from the XL-Desk Owner's Manual (82BMLM01B, May 2017), the G Series Bus Compressor, E Series EQ and
   E Series Dynamics 500 module user guides, and Sound On Sound's XL-Desk review. Values the documents don't give are
   marked "est." here and listed in help.js. */
(() => {
const XL = {};
XL.REF = 24;                                   // internal float 1.0 = +24 dBu (0 dBFS on a +24 dBu converter)
const LOG = (a, b) => p => a * Math.pow(b / a, p), LIN = (a, b) => p => a + (b - a) * p, BIP = r => p => (p - .5) * 2 * r;
XL.LOG = LOG; XL.LIN = LIN;
// centre-detent pots (Mix B/C/D level, master trims, foldback, AFL): unity at 12 o'clock, +10 dB fully clockwise (manual p.16)
XL.unity10 = p => p <= .002 ? -Infinity : p >= .5 ? (p - .5) * 20 : 60 * Math.log10(p / .5);
// channel send pots (cue, aux 1/2): unity at 3 o'clock, +6 dB fully clockwise (est.)
XL.send = p => p <= .002 ? -Infinity : p >= .75 ? (p - .75) * 24 : 60 * Math.log10(p / .75);
// monitor and headphone level: 0 dB fully clockwise
XL.vol = p => p <= .002 ? -Infinity : 40 * Math.log10(p);
// 100 mm fader scale (SSL-style marks, est.): +10 at the top, 0 dB at three quarters
const FAD = [[0, -Infinity], [.025, -60], [.06, -50], [.12, -40], [.2, -30], [.31, -20], [.4, -15], [.5, -10], [.625, -5], [.75, 0], [.875, 5], [1, 10]];
XL.FAD = FAD;
XL.fader = p => { if (p <= .001) return -Infinity; for (let i = 1; i < FAD.length; i++) if (p <= FAD[i][0]){ const [p0, d0] = FAD[i - 1], [p1, d1] = FAD[i]; if (d0 === -Infinity) return d1 + 40 * Math.log10(p / p1); return d0 + (d1 - d0) * (p - p0) / (p1 - p0); } return 10; };
XL.faderA = p => XL.fader(p) - 10;           // Mix A: 0 dB at the very top of the travel (manual p.17)
XL.faderPos = d => { if (d <= -100) return 0; for (let i = FAD.length - 1; i > 0; i--){ const [p0, d0] = FAD[i - 1], [p1, d1] = FAD[i]; if (d >= (d0 === -Infinity ? -100 : d0)){ if (d0 === -Infinity) return p1 * Math.pow(10, (d - d1) / 40); return p0 + (p1 - p0) * (d - d0) / (d1 - d0); } } return 0; };

/* ---------- 500-series modules ---------- */
const GSTEP = {att:[.1, .3, 1, 3, 10, 30], rel:[.1, .2, .4, .8, 1.6, 'AUTO'], ratio:[1.5, 2, 3, 4, 5, 10], hpf:['OFF', 30, 60, 105, 125, 185]};
XL.GSTEP = GSTEP;
XL.MOD = {
  // SSL E Series EQ (611 E-EQ): 'Brown Knob' 02 EQ, BLK switches to the 'Black Knob' 242 (user guide); band ranges from the faceplate
  eeq:{name:'SSL E Series EQ', short:'E-EQ', params:[
    {n:'hfG', k:'pot', d:.5, lb:'HF gain', c:'red', g:1}, {n:'hfF', k:'pot', d:.5, lb:'HF freq', c:'red', f:LOG(1500, 16000), u:'Hz'}, {n:'hfBell', k:'btn', d:0, lb:'BELL'},
    {n:'hmfG', k:'pot', d:.5, lb:'HMF gain', c:'green', g:1}, {n:'hmfF', k:'pot', d:.5, lb:'HMF freq', c:'green', f:LOG(600, 7000), u:'Hz'}, {n:'hmfQ', k:'pot', d:.5, lb:'HMF Q', c:'green', f:LOG(2.5, .5), u:'Q'},
    {n:'blk', k:'btn', d:0, lb:'BLK'}, {n:'in', k:'btn', d:1, lb:'IN'},
    {n:'lmfG', k:'pot', d:.5, lb:'LMF gain', c:'blue', g:1}, {n:'lmfF', k:'pot', d:.5, lb:'LMF freq', c:'blue', f:LOG(200, 2500), u:'Hz'}, {n:'lmfQ', k:'pot', d:.5, lb:'LMF Q', c:'blue', f:LOG(2.5, .5), u:'Q'},
    {n:'lfF', k:'pot', d:.5, lb:'LF freq', c:'brown', f:LOG(30, 450), u:'Hz'}, {n:'lfG', k:'pot', d:.5, lb:'LF gain', c:'brown', g:1}, {n:'lfBell', k:'btn', d:0, lb:'BELL'}]},
  // SSL E Series Dynamics (611 DYN): ranges, attack rates and LED points from the user guide and faceplate
  edyn:{name:'SSL E Series Dynamics', short:'E-DYN', params:[
    {n:'ratio', k:'pot', d:0, lb:'RATIO', c:'white', f:p => p >= .999 ? Infinity : 1 / (1 - p), u:':1'}, {n:'hard', k:'btn', d:0, lb:'HARD'},
    {n:'cthr', k:'pot', d:.5, lb:'THRESHOLD', c:'white', f:LIN(4, -26), u:'dBu'}, {n:'linrel', k:'btn', d:0, lb:'LIN REL'},
    {n:'crel', k:'pot', d:.3, lb:'RELEASE', c:'white', f:LOG(.1, 4), u:'s'}, {n:'cfast', k:'btn', d:0, lb:'FAST ATT'}, {n:'in', k:'btn', d:1, lb:'IN'},
    {n:'range', k:'pot', d:0, lb:'RANGE', c:'green', f:LIN(0, 40), u:'dB'}, {n:'gthr', k:'pot', d:.5, lb:'THRESHOLD', c:'green', f:LIN(-30, 10), u:'dBu'}, {n:'exp', k:'btn', d:0, lb:'EXP'},
    {n:'grel', k:'pot', d:.3, lb:'RELEASE', c:'green', f:LOG(.1, 4), u:'s'}, {n:'gfast', k:'btn', d:0, lb:'FAST ATT'}]},
  // SSL G Series Bus Compressor (G-COMP, 2-slot): every switch position from the faceplate in the 2020 user guide
  gcomp:{name:'SSL G Series Bus Compressor', short:'G-COMP', w:2, params:[
    {n:'thr', k:'pot', d:.5, lb:'THRESHOLD', c:'blue', f:LIN(-20, 20), u:'dB'}, {n:'mk', k:'pot', d:.25, lb:'MAKE UP', c:'blue', f:LIN(-5, 15), u:'dB'},
    {n:'att', k:'step', d:4, lb:'ATTACK', c:'blue', v:GSTEP.att, u:'ms'}, {n:'rel', k:'step', d:5, lb:'RELEASE', c:'blue', v:GSTEP.rel, u:'s'},
    {n:'ratio', k:'step', d:3, lb:'RATIO', c:'blue', v:GSTEP.ratio, u:':1'}, {n:'hpf', k:'step', d:0, lb:'HPF', c:'blue', v:GSTEP.hpf, u:'Hz'}, {n:'in', k:'btn', d:1, lb:'IN'}]}
};
// what can live where: 16 in-line slots, the 2-slot bus compressor space (17-18), the channel INS points and the mix INS points
XL.WHERE = {};
for (let i = 1; i <= 16; i++) XL.WHERE['slot' + i] = ['empty', 'eeq', 'edyn'];
XL.WHERE.slot17 = ['empty', 'gcomp'];
for (let i = 1; i <= 16; i++) XL.WHERE['ins' + i] = ['none', 'loop', 'eeq', 'edyn'];
for (const b of 'ABCD') XL.WHERE['bins' + b] = ['none', 'loop', 'eeq', 'edyn', 'gcomp'];
XL.TYPE_NAME = {empty:'Empty slot', none:'Nothing patched', loop:'Patch cable (send to return)', eeq:'SSL E Series EQ', edyn:'SSL E Series Dynamics', gcomp:'SSL G Series Bus Compressor'};

/* ---------- every parameter ---------- */
const P = XL.P = {};   // id -> {k:'pot'|'btn'|'step', d, f (pot law), v (step values), lb, u}
const pot = (id, d, f, lb, u) => P[id] = {k:'pot', d, f, lb, u}, btn = (id, d, lb) => P[id] = {k:'btn', d:d ? 1 : 0, lb};
for (let i = 1; i <= 16; i++){
  const m = 'm' + i + '.';
  if (i <= 8){ pot(m + 'gain', .25, LIN(15, 75), 'VHD gain', 'dB'); pot(m + 'drive', .5, LIN(0, 1), 'VHD harmonics (2nd to 3rd)'); btn(m + 'vhd', 0, 'VHD'); btn(m + 'hiz', 0, 'HI-Z'); btn(m + 'p48', 0, '+48V'); btn(m + 'pad', 0, 'PAD'); }
  pot(m + 'trim', .5, BIP(20), 'Trim', 'dB'); btn(m + 'daw', 0, 'DAW'); btn(m + 'phase', 0, 'Ø'); btn(m + 's500', 0, '500'); btn(m + 'ins', 0, 'INS'); btn(m + 'chop', 0, 'CHOP POST');
  pot(m + 'cue', 0, XL.send, 'Cue level', 'dB'); pot(m + 'cuepan', .5, BIP(1), 'Cue pan'); btn(m + 'cuepost', 0, 'POST'); btn(m + 'alt', 0, 'ALT');
  pot(m + 'aux1', 0, XL.send, 'Aux 1', 'dB'); pot(m + 'aux2', 0, XL.send, 'Aux 2', 'dB'); pot(m + 'pan', .5, BIP(1), 'Pan');
  for (const b of 'ABCD') btn(m + b, b === 'A', b); btn(m + 'cut', 0, 'CUT'); btn(m + 'solo', 0, 'SOLO'); btn(m + 'safe', 0, 'Solo safe');
  pot(m + 'fader', .75, XL.fader, 'Fader', 'dB');
}
for (let k = 1; k <= 4; k++){
  const s = 's' + k + '.';
  pot(s + 'trim', .5, BIP(20), 'Trim', 'dB'); btn(s + 'phL', 0, 'ØL'); btn(s + 'phR', 0, 'ØR'); btn(s + 'mono', 0, 'MONO'); btn(s + 'chop', 0, 'CHOP POST');
  pot(s + 'cue', 0, XL.send, 'Cue level', 'dB'); pot(s + 'cuepan', .5, BIP(1), 'Cue balance'); btn(s + 'cuepost', 0, 'POST');
  pot(s + 'aux1', 0, XL.send, 'Aux 1', 'dB'); pot(s + 'aux2', 0, XL.send, 'Aux 2', 'dB'); pot(s + 'pan', .5, BIP(1), 'Balance');
  for (const b of 'ABCD') btn(s + b, b === 'A', b); btn(s + 'cut', 0, 'CUT'); btn(s + 'solo', 0, 'SOLO'); btn(s + 'safe', 0, 'Solo safe');
  pot(s + 'fader', .75, XL.fader, 'Fader', 'dB');
}
for (let k = 1; k <= 2; k++){
  const r = 'r' + k + '.';
  pot(r + 'level', .5, XL.unity10, 'Return level', 'dB'); pot(r + 'bal', .5, BIP(1), 'Balance'); btn(r + 'mono', 0, 'MONO'); btn(r + 'cue', 0, 'ST CUE');
  for (const b of 'ABCD') btn(r + b, b === 'A', b); btn(r + 'afl', 0, 'AFL');
}
btn('A.s500', 0, '500'); btn('A.ins', 0, 'INS'); btn('A.sum', 0, 'SUM'); btn('A.comp', 0, 'COMP'); pot('A.fader', 1, XL.faderA, 'Mix A fader', 'dB');
for (const b of 'BCD'){ btn(b + '.s500', 0, '500'); btn(b + '.ins', 0, 'INS'); btn(b + '.sum', 0, 'SUM'); btn(b + '.toA', 0, 'TO A'); pot(b + '.level', .5, XL.unity10, 'Mix ' + b + ' level', 'dB'); btn(b + '.afl', 0, 'AFL'); }
pot('cue.level', .5, XL.unity10, 'St Cue master', 'dB'); btn('cue.afl', 0, 'AFL'); btn('cue.toA', 0, 'TO A');
for (const a of ['aux1', 'aux2']){ pot(a + '.level', .5, XL.unity10, a === 'aux1' ? 'Aux 1 master' : 'Aux 2 master', 'dB'); btn(a + '.pre', 0, 'PRE'); btn(a + '.afl', 0, 'AFL'); }
btn('solo.afl', 0, 'AFL solo mode');
pot('mon.level', .55, XL.vol, 'Monitor level', 'dB'); btn('mon.dim', 0, 'DIM'); btn('mon.cut', 0, 'CUT'); pot('mon.dimamt', .5, LIN(-40, 0), 'Dim level', 'dB'); pot('mon.afl', .5, XL.unity10, 'AFL level', 'dB');
btn('mon.mono', 0, 'MONO'); btn('mon.mini1', 0, 'MINI 1'); btn('mon.mini2', 0, 'MINI 2'); btn('mon.subMain', 0, 'SUB (main)'); btn('mon.subMini1', 0, 'SUB (mini 1)'); btn('mon.subMini2', 0, 'SUB (mini 2)');
btn('mon.srcA', 1, 'MIX A'); btn('mon.srcExt', 0, 'EXT'); btn('mon.srcIj', 0, 'iJack'); btn('mon.sum', 0, 'SUM');
P['mon.off1'] = {k:'num', d:0, lb:'Mini 1 level offset', u:'dB'}; P['mon.off2'] = {k:'num', d:0, lb:'Mini 2 level offset', u:'dB'};
pot('hp.level', .5, XL.vol, 'Headphone level', 'dB'); btn('hp.fbA', 0, 'F/B A'); btn('hp.fbB', 0, 'F/B B');
for (const f of ['fbA', 'fbB']){ pot(f + '.level', .5, XL.unity10, f === 'fbA' ? 'Foldback A' : 'Foldback B', 'dB'); btn(f + '.mon', 0, 'MON'); btn(f + '.talk', 0, f === 'fbA' ? 'TALK A' : 'TALK B'); }
pot('talk.level', .5, LIN(-20, 40), 'Talkback mic gain', 'dB'); pot('listen.level', .5, LIN(0, 60), 'Listen mic gain', 'dB'); btn('listen.on', 0, 'LISTEN');
for (let d = 1; d <= 12; d++) btn('dip.' + d, 0, 'DIP ' + d);
btn('sys.noise', 1, 'Analogue noise floor');
btn('rear.unlink1', 0, 'Remove 500 links, slots 1-8'); btn('rear.unlink2', 0, 'Remove 500 links, slots 9-16');
// module parameters: <where>.<type>.<name>, so each slot keeps its settings for every module you try in it
for (const w in XL.WHERE) for (const t of XL.WHERE[w]){ const M = XL.MOD[t]; if (!M) continue;
  for (const q of M.params) P[w + '.' + t + '.' + q.n] = q.k === 'step' ? {k:'step', d:q.d, v:q.v, lb:q.lb, u:q.u} : q.k === 'btn' ? {k:'btn', d:q.d, lb:q.lb} : {k:'pot', d:q.d, f:q.f, lb:q.lb, u:q.u, g:q.g}; }
XL.defaults = () => { const o = {}; for (const id in P) o[id] = P[id].d; return o; };
XL.defTypes = () => { const t = {}; for (let i = 1; i <= 16; i++){ t['slot' + i] = 'eeq'; t['ins' + i] = 'edyn'; } t.slot17 = 'gcomp'; for (const b of 'ABCD') t['bins' + b] = 'loop'; return t; };
XL.DIP = [['Meter scaling', '+24 dBu', '+18 dBu'], ['Talkback A', 'Sum', 'Replace'], ['Talkback A', 'Momentary', 'Latching'], ['Talkback B', 'Sum', 'Replace'], ['Talkback B', 'Momentary', 'Latching'],
  ['Unused', '-', '-'], ['MINI 2 speaker set', 'Stereo', 'Mono'], ['Unused', '-', '-'], ['Unused', '-', '-'], ['Unused', '-', '-'], ['Bargraph test mode', 'Normal', 'Enabled'], ['Programming mode', 'Normal', 'Enabled']];

/* ---------- default MIDI map (MIDI Learn overrides any of it) ----------
   MIDI channel n = mono channel n. Stereo channels, returns and the centre section use CCs 40-63, which the strips leave free. */
const MAP = XL.MIDIMAP = {};   // 'ch:cc' (ch 0-15) -> param id or action
const STRIP_CC = {fader:7, pan:10, trim:16, gain:17, drive:18, cue:19, cuepan:20, aux1:21, aux2:22, daw:80, phase:81, s500:82, ins:83, chop:84, cuepost:85, alt:86, A:87, B:88, C:89, D:90, cut:91, solo:92, vhd:93, hiz:94, p48:95, pad:96};
XL.STRIP_CC = STRIP_CC; XL.SLOT_CC0 = 102;
for (let i = 1; i <= 16; i++) for (const k in STRIP_CC) if (P['m' + i + '.' + k]) MAP[(i - 1) + ':' + STRIP_CC[k]] = 'm' + i + '.' + k;
const ST_CC = ['fader', 'pan', 'trim', 'cue', 'cuepan', 'aux1', 'aux2', 'mono', 'A', 'B', 'C', 'D', 'cut', 'solo', 'cuepost', 'chop', 'phL', 'phR'];
for (let k = 1; k <= 4; k++) ST_CC.forEach((n, j) => MAP[(k - 1) + ':' + (40 + j)] = 's' + k + '.' + n);
const RET_CC = ['level', 'bal', 'mono', 'cue', 'A', 'B', 'C', 'D', 'afl'];
for (let k = 1; k <= 2; k++) RET_CC.forEach((n, j) => MAP[(3 + k) + ':' + (40 + j)] = 'r' + k + '.' + n);
const C16 = ['A.fader', 'A.comp', 'A.ins', 'A.sum', 'A.s500', 'B.level', 'C.level', 'D.level', 'B.toA', 'C.toA', 'D.toA', 'B.afl', 'C.afl', 'D.afl', 'cue.level', 'aux1.level', 'aux2.level', 'cue.toA', 'aux1.pre', 'aux2.pre', 'cue.afl', 'aux1.afl', 'aux2.afl', 'solo.afl'];
const C15 = ['slot17.gcomp.thr', 'slot17.gcomp.mk', 'slot17.gcomp.att', 'slot17.gcomp.rel', 'slot17.gcomp.ratio', 'slot17.gcomp.hpf', 'slot17.gcomp.in', 'mon.level', 'mon.dim', 'mon.cut', 'mon.dimamt', 'mon.afl', 'mon.mono', 'mon.mini1', 'mon.mini2', '@sub', 'mon.srcA', 'mon.srcExt', 'mon.srcIj', 'mon.sum', 'hp.level', 'fbA.level', 'fbB.level', 'listen.on'];
const C14 = ['B.ins', 'B.sum', 'B.s500', 'C.ins', 'C.sum', 'C.s500', 'D.ins', 'D.sum', 'D.s500', 'fbA.mon', 'fbB.mon', 'fbA.talk', 'fbB.talk', 'talk.level', 'listen.level', 'hp.fbA', 'hp.fbB', '@soloclear', '@play', '@stop', '@rec', '@rtz', '@rew', '@ff'];
C16.forEach((id, j) => MAP['15:' + (40 + j)] = id); C15.forEach((id, j) => MAP['14:' + (40 + j)] = id); C14.forEach((id, j) => MAP['13:' + (40 + j)] = id);
// 500 slot n's module on MIDI channel n, CC 102 up, in faceplate order
XL.slotCC = (type, j) => XL.SLOT_CC0 + j;
globalThis.XL = XL;
})();
