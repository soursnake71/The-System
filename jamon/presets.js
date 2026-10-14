/* Panels (what each machine's front panel has) and the sound presets.
   Control kinds: k = knob 0..1, s = switch (0/1), sel = selector [values, labels]. */
const K = (k, l, g) => ({t:'k', k, l, g}), SW = (k, l, g) => ({t:'s', k, l, g}), SEL = (k, l, g, opts) => ({t:'sel', k, l, g, opts});
const V808 = [['bd', 'Bass Drum'], ['sd', 'Snare'], ['lt', 'Lo Tom'], ['mt', 'Mid Tom'], ['ht', 'Hi Tom'], ['lc', 'Lo Conga'], ['mc', 'Mid Conga'], ['hc', 'Hi Conga'],
  ['rs', 'Rim Shot'], ['cl', 'Claves'], ['cp', 'Hand Clap'], ['ma', 'Maracas'], ['cb', 'Cowbell'], ['cy', 'Cymbal'], ['oh', 'Open Hat'], ['ch', 'Closed Hat']];
const VDMX = [['bd', 'Bass'], ['sd', 'Snare'], ['sd2', 'Snare 2'], ['rim', 'Rimshot'], ['clap', 'Claps'], ['t1', 'Tom 1'], ['t2', 'Tom 2'], ['t3', 'Tom 3'], ['t4', 'Tom 4'],
  ['chh', 'Closed Hat'], ['hha', 'Accent Hat'], ['ohh', 'Open Hat'], ['cym', 'Cymbal'], ['ride', 'Ride'], ['tamb', 'Tamb'], ['shk', 'Shaker']];
// General MIDI drum notes for each voice (MIDI in / out / file import)
const GM808 = {bd:36, sd:38, lt:41, mt:45, ht:48, lc:64, mc:63, hc:62, rs:37, cl:75, cp:39, ma:70, cb:56, cy:49, oh:46, ch:42};
const GMDMX = {bd:36, sd:38, sd2:40, rim:37, clap:39, t1:50, t2:47, t3:45, t4:41, chh:42, hha:44, ohh:46, cym:49, ride:51, tamb:54, shk:82};
const p808 = () => { const p = {acc:.6}; for (const [v] of V808){ p[v + 'L'] = .8; } Object.assign(p, {bdT:.4, bdD:.55, sdT:.5, sdS:.55, ltT:.5, mtT:.5, htT:.5, lcT:.5, mcT:.5, hcT:.5, cyT:.5, cyD:.5, ohD:.4}); return p; };
const pDMX = () => { const p = {acc:.5}; for (const [v] of VDMX){ p[v + 'L'] = .8; p[v + 'P'] = .5; } return p; };

const MACH = {
  d808:{name:'TR-808', maker:'Roland · 1980', kind:'drum', voices:V808, gm:GM808, ch:9, color:'#e8742b',
    panel:[K('acc', 'Accent', 'Master'), ...V808.flatMap(([v, n]) => [K(v + 'L', 'Level', n)].concat(
      v === 'bd' ? [K('bdT', 'Tone', n), K('bdD', 'Decay', n)] : v === 'sd' ? [K('sdT', 'Tone', n), K('sdS', 'Snappy', n)] :
      /^(lt|mt|ht|lc|mc|hc)$/.test(v) ? [K(v + 'T', 'Tuning', n)] : v === 'cy' ? [K('cyT', 'Tone', n), K('cyD', 'Decay', n)] : v === 'oh' ? [K('ohD', 'Decay', n)] : []))],
    def:p808},
  dmx:{name:'DMX', maker:'Oberheim · 1981', kind:'drum', voices:VDMX, gm:GMDMX, ch:10, color:'#c9c9c9',
    panel:[K('acc', 'Accent', 'Master'), ...VDMX.flatMap(([v, n]) => [K(v + 'L', 'Level', n), K(v + 'P', 'Tuning', n)])], def:pDMX},
  tb303:{name:'TB-303', maker:'Roland · 1981', kind:'mono', proc:'tb303', ch:4, color:'#b8b8a8',
    panel:[K('tune', 'Tuning', 'Bass Line'), K('cut', 'Cutoff Freq', 'Bass Line'), K('res', 'Resonance', 'Bass Line'), K('env', 'Env Mod', 'Bass Line'), K('dec', 'Decay', 'Bass Line'), K('acc', 'Accent', 'Bass Line'), SEL('wave', 'Waveform', 'Bass Line', [[0, 'Saw'], [1, 'Square']]), K('vol', 'Volume', 'Bass Line')],
    def:() => ({tune:.5, wave:0, cut:.3, res:.75, env:.6, dec:.4, acc:.75, vol:.8})},
  proone:{name:'Pro-One', maker:'Sequential Circuits · 1981', kind:'mono', proc:'proone', ch:0, color:'#8a6a3a',
    panel:[K('oAf', 'Frequency', 'Osc A'), SW('oAsaw', 'Saw', 'Osc A'), SW('oApul', 'Pulse', 'Osc A'), K('oApw', 'Pulse Width', 'Osc A'), SW('sync', 'Sync', 'Osc A'),
      K('oBf', 'Frequency', 'Osc B'), K('oBfine', 'Fine', 'Osc B'), SW('oBsaw', 'Saw', 'Osc B'), SW('oBtri', 'Triangle', 'Osc B'), SW('oBpul', 'Pulse', 'Osc B'), K('oBpw', 'Pulse Width', 'Osc B'), SW('oBlo', 'Lo Freq', 'Osc B'), SW('oBkbd', 'Kbd', 'Osc B'),
      K('mA', 'Osc A', 'Mixer'), K('mB', 'Osc B', 'Mixer'), K('mN', 'Noise', 'Mixer'),
      K('cut', 'Cutoff', 'Filter'), K('res', 'Emphasis', 'Filter'), K('fenv', 'Env Amt', 'Filter'), SEL('kbd', 'Kbd Amt', 'Filter', [[0, 'Off'], [.5, 'Half'], [1, 'Full']]),
      K('fa', 'Attack', 'Filter Env'), K('fd', 'Decay', 'Filter Env'), K('fs', 'Sustain', 'Filter Env'), K('fr', 'Release', 'Filter Env'),
      K('aa', 'Attack', 'Amp Env'), K('ad', 'Decay', 'Amp Env'), K('as', 'Sustain', 'Amp Env'), K('ar', 'Release', 'Amp Env'),
      K('mFE', 'Filter Env', 'Modulation'), K('mOB', 'Osc B', 'Modulation'), SW('pmWheel', 'Via Wheel', 'Modulation'), K('mLFO', 'LFO', 'Modulation'), SW('lfoWheel', 'LFO Wheel', 'Modulation'),
      SW('dFA', 'Freq A', 'Modulation'), SW('dPWA', 'PW A', 'Modulation'), SW('dFB', 'Freq B', 'Modulation'), SW('dPWB', 'PW B', 'Modulation'), SW('dF', 'Filter', 'Modulation'),
      K('lfoF', 'Speed', 'LFO'), SW('lfoSaw', 'Saw', 'LFO'), SW('lfoTri', 'Triangle', 'LFO'), SW('lfoSq', 'Square', 'LFO'),
      K('glide', 'Glide', 'Control'), SEL('mode', 'Mode', 'Control', [[0, 'Normal'], [1, 'Retrig'], [2, 'Drone']]), K('vol', 'Volume', 'Control')],
    def:() => ({oAf:.5, oAsaw:1, oApul:0, oApw:.5, sync:0, oBf:.5, oBfine:.5, oBsaw:1, oBtri:0, oBpul:0, oBpw:.5, oBlo:0, oBkbd:1, mA:.8, mB:.6, mN:0, cut:.45, res:.2, fenv:.4, kbd:.5,
      fa:0, fd:.35, fs:.3, fr:.3, aa:0, ad:.4, as:.8, ar:.3, mFE:0, mOB:0, pmWheel:0, mLFO:0, lfoWheel:0, dFA:0, dPWA:0, dFB:0, dPWB:0, dF:0, lfoF:.4, lfoSaw:0, lfoTri:1, lfoSq:0, glide:0, mode:0, vol:.8})},
  sh101:{name:'SH-101', maker:'Roland · 1982', kind:'mono', proc:'ana', model:'sh101', ch:1, color:'#5d6f86',
    panel:[SEL('transpose', 'Range', 'VCO', [[-12, '16\''], [0, '8\''], [12, '4\''], [24, '2\'']]), K('lfoPitch', 'Mod', 'VCO'), K('pw', 'Pulse Width', 'VCO'), K('pwm', 'PWM (LFO)', 'VCO'),
      K('m1', 'Pulse', 'Source Mixer'), K('m2', 'Saw', 'Source Mixer'), K('sub', 'Sub Osc', 'Source Mixer'), SEL('subType', 'Sub Type', 'Source Mixer', [[0, '-1 Oct'], [2, '-2 Oct'], [1, '-2 Oct Pulse']]), K('noise', 'Noise', 'Source Mixer'),
      K('cut', 'Freq', 'VCF'), K('res', 'Res', 'VCF'), K('fenv', 'Env', 'VCF'), K('lfoCut', 'Mod', 'VCF'), K('kf', 'Kybd', 'VCF'),
      K('a', 'A', 'Env'), K('d', 'D', 'Env'), K('s', 'S', 'Env'), K('r', 'R', 'Env'), SW('ampGate', 'VCA Gate', 'Env'),
      K('lfoRate', 'Rate', 'Modulator'), SEL('lfoWave', 'Wave', 'Modulator', [[0, 'Tri'], [1, 'Square'], [2, 'Saw'], [3, 'Random']]), K('glide', 'Portamento', 'Controller'), K('vol', 'Volume', 'Controller')],
    def:() => ({transpose:0, lfoPitch:0, pw:.5, pwm:0, m1:.8, m2:.8, sub:.5, subType:0, noise:0, cut:.4, res:.3, fenv:.5, lfoCut:0, kf:.6, a:0, d:.35, s:.4, r:.15, ampGate:0, lfoRate:.3, lfoWave:0, glide:0, vol:.8})},
  juno:{name:'Juno-60', maker:'Roland · 1982', kind:'poly', proc:'ana', model:'juno60', ch:2, color:'#c23b2f',
    panel:[K('lfoRate', 'Rate', 'LFO'), K('lfoDelay', 'Delay', 'LFO'),
      K('lfoPitch', 'LFO', 'DCO'), K('pw', 'PW', 'DCO'), K('pwm', 'PWM (LFO)', 'DCO'), SW('m2', 'Pulse', 'DCO'), SW('m1', 'Saw', 'DCO'), K('sub', 'Sub', 'DCO'), K('noise', 'Noise', 'DCO'),
      SEL('hpf', 'HPF', 'HPF', [[0, '0'], [1, '1'], [2, '2'], [3, '3']]),
      K('cut', 'Freq', 'VCF'), K('res', 'Res', 'VCF'), K('fenv', 'Env', 'VCF'), K('lfoCut', 'LFO', 'VCF'), K('kf', 'Kybd', 'VCF'),
      SW('ampGate', 'Gate', 'VCA'), K('a', 'A', 'ENV'), K('d', 'D', 'ENV'), K('s', 'S', 'ENV'), K('r', 'R', 'ENV'),
      SEL('chorus', 'Chorus', 'Chorus', [[0, 'Off'], [1, 'I'], [2, 'II'], [3, 'I+II']]), K('vol', 'Volume', 'Chorus')],
    def:() => ({lfoRate:.3, lfoDelay:0, lfoPitch:0, pw:.5, pwm:0, m1:1, m2:0, sub:.3, noise:0, hpf:0, cut:.55, res:.15, fenv:.35, lfoCut:0, kf:.5, ampGate:0, a:0, d:.4, s:.6, r:.3, chorus:1, vol:.8})},
  rs09:{name:'RS-09', maker:'Roland · 1979', kind:'poly', proc:'rs09', ch:3, color:'#2f6f8a',
    panel:[SW('o16', '16\'', 'Organ'), SW('o8', '8\'', 'Organ'), SW('o4', '4\'', 'Organ'), SW('o2', '2\'', 'Organ'), SEL('orgTone', 'Voice', 'Organ', [[0, 'I'], [1, 'II']]), K('orgVol', 'Volume', 'Organ'),
      SW('s8', '8\'', 'Strings'), SW('s4', '4\'', 'Strings'), K('sAtk', 'Attack', 'Strings'), K('sRel', 'Sustain', 'Strings'), K('sTone', 'Tone', 'Strings'), K('strVol', 'Volume', 'Strings'),
      K('bal', 'Balance', 'Output'), SEL('ens', 'Ensemble', 'Output', [[0, 'Off'], [1, 'I'], [2, 'II']]), K('vol', 'Volume', 'Output')],
    def:() => ({o16:1, o8:1, o4:0, o2:0, orgTone:0, orgVol:.7, s8:1, s4:1, sAtk:.3, sRel:.5, sTone:.6, strVol:.8, bal:.85, ens:1, vol:.8})},
  pbass:{name:'Precision Bass', maker:'Fender · physical model', kind:'mono', proc:'pbass', ch:5, color:'#d9c7a0',
    panel:[SEL('style', 'Playing', 'Hands', [[0, 'Finger'], [1, 'Pick'], [2, 'Slap'], [3, 'Palm mute']]), K('pos', 'Pluck Pos', 'Hands'), K('buzz', 'Fret Buzz', 'Hands'),
      K('decay', 'Strings', 'Bass'), K('tone', 'Tone', 'Bass'), K('vol', 'Volume', 'Bass')],
    def:() => ({style:0, tone:.6, decay:.55, pos:.25, buzz:.2, vol:.8})}
};
const SEQ_MACH = ['d808', 'dmx', 'tb303', 'proone', 'sh101', 'juno', 'rs09', 'pbass'];
const VOC_PANEL = [K('mic', 'Mic Sens', 'Electro-Harmonix Vocoder'), K('music', 'Music Sens', 'Electro-Harmonix Vocoder'), K('hi', 'Hi Boost', 'Electro-Harmonix Vocoder'), K('comp', 'Compression', 'Electro-Harmonix Vocoder'),
  K('blend', 'Blend', 'Electro-Harmonix Vocoder'), K('resp', 'Response', 'Electro-Harmonix Vocoder'), K('vol', 'Output', 'Electro-Harmonix Vocoder')];
const VOC_DEF = () => ({mic:.6, music:.6, hi:.5, comp:.6, blend:0, resp:.35, vol:.9});

/* Presets: sounds built for the roles on the record (my programming of each machine, not factory patches). */
const PRESETS = {
  tb303:{
    'Acid Bass': {cut:.25, res:.82, env:.65, dec:.35, acc:.8, wave:0},
    'Rubber Electro Bass': {cut:.18, res:.55, env:.45, dec:.5, acc:.6, wave:0},
    'Square Sub Bass': {cut:.15, res:.3, env:.25, dec:.7, acc:.4, wave:1},
    'Squelch Line': {cut:.35, res:.92, env:.8, dec:.25, acc:.9, wave:0},
    'Zap Bass': {cut:.4, res:.7, env:.9, dec:.12, acc:.7, wave:1}},
  proone:{
    'Electro Bass (sync)': {oAf:.5, oAsaw:1, oApul:0, sync:1, oBf:.25, oBsaw:1, mA:.9, mB:.3, cut:.3, res:.35, fenv:.55, kbd:.5, fd:.3, fs:.1, ad:.45, as:.6, mFE:.45, dFA:1, glide:0},
    'Fat Unison Bass': {oAf:.5, oAsaw:1, oApul:1, oApw:.4, oBf:.25, oBfine:.53, oBsaw:1, mA:.8, mB:.8, cut:.22, res:.25, fenv:.5, fd:.35, fs:.2, ad:.5, as:.7},
    'Wikki Lead': {oAf:.75, oAsaw:0, oApul:1, oApw:.3, oBf:.75, oBfine:.55, oBsaw:1, mA:.8, mB:.6, cut:.55, res:.4, fenv:.4, kbd:1, fd:.4, fs:.5, as:.9, mLFO:.08, lfoF:.55, lfoTri:1, dFA:1, dFB:1, glide:.18, lfoWheel:0},
    'Sync Scream Solo': {oAf:.75, oAsaw:1, sync:1, oBf:.5, oBsaw:1, mA:1, mB:0, cut:.65, res:.45, fenv:.3, kbd:1, fd:.5, fs:.6, as:.9, mFE:.55, dFA:1, glide:.15},
    'Laser Zap': {oAf:.75, oAsaw:0, oApul:1, oApw:.5, mA:1, mB:0, cut:.5, res:.6, fenv:.9, fd:.18, fs:0, ad:.2, as:0, mFE:1, dFA:1},
    'Space Arp': {oAf:.75, oAsaw:1, oBf:.75, oBfine:.52, oBpul:1, oBsaw:0, oBpw:.35, mA:.7, mB:.6, cut:.35, res:.55, fenv:.6, kbd:1, fd:.22, fs:0, ad:.3, as:0, ar:.25},
    'Siren FX': {oAf:.75, oAsaw:0, oApul:1, mA:1, mB:0, oBlo:1, oBf:.35, oBtri:1, oBsaw:0, mOB:.4, dFA:1, cut:.6, res:.5, as:1}},
  sh101:{
    'Squelch Arp': {m1:0, m2:1, sub:.2, cut:.2, res:.7, fenv:.7, d:.18, s:0, r:.1, kf:.8},
    'Bubble Bass': {m1:.6, m2:0, pw:.35, sub:.8, subType:0, cut:.22, res:.45, fenv:.55, d:.25, s:.1, r:.1},
    'Electro Pluck': {m1:.8, m2:.4, pw:.25, cut:.25, res:.5, fenv:.8, d:.15, s:0, r:.12, transpose:12},
    'Solo Lead': {m1:.9, m2:.7, pw:.4, cut:.55, res:.35, fenv:.25, s:.85, glide:.25, lfoPitch:.06, lfoRate:.45, transpose:12},
    'PWM Lead': {m1:.9, m2:0, pwm:.65, lfoRate:.4, cut:.6, res:.25, s:.9, glide:.15, transpose:12},
    'Noise Snare FX': {m1:0, m2:0, sub:0, noise:1, cut:.7, res:.2, d:.15, s:0, r:.1, kf:0}},
  juno:{
    'Electro Brass Stab': {m1:1, m2:0, sub:.2, cut:.35, res:.15, fenv:.6, a:.05, d:.35, s:.4, r:.25, chorus:1},
    'Vocoder Carrier': {m1:1, m2:1, pw:.5, sub:.5, noise:.04, cut:.8, res:0, fenv:0, a:0, d:.5, s:1, r:.15, chorus:0, hpf:0},
    'Space Pad': {m1:0, m2:1, pwm:.7, lfoRate:.25, sub:.3, cut:.45, res:.1, a:.55, d:.6, s:.9, r:.65, chorus:3},
    'Juno Arp Pluck': {m1:0, m2:1, pw:.4, cut:.3, res:.35, fenv:.6, d:.25, s:0, r:.2, chorus:2},
    'Hoover Chord': {m1:1, m2:1, pwm:.6, lfoRate:.3, sub:.4, cut:.6, res:.1, a:.15, s:.9, r:.45, chorus:3},
    'Sub Bass': {m1:1, m2:0, sub:.8, cut:.25, res:.25, fenv:.45, d:.35, s:.2, r:.1, chorus:0}},
  rs09:{
    'Strings': {o16:0, o8:0, o4:0, o2:0, s8:1, s4:1, sAtk:.35, sRel:.5, sTone:.6, bal:1, ens:1},
    'Slow Strings': {s8:1, s4:0, sAtk:.7, sRel:.75, sTone:.45, bal:1, ens:2},
    'Organ': {o16:1, o8:1, o4:1, o2:0, orgTone:0, bal:0, ens:0},
    'Mellow Organ': {o16:1, o8:1, o4:0, o2:0, orgTone:1, bal:0, ens:1},
    'Organ + Strings': {o16:0, o8:1, o4:1, o2:0, orgTone:1, s8:1, s4:1, sAtk:.4, sRel:.5, bal:.6, ens:1}},
  pbass:{
    'Finger Funk': {style:0, tone:.55, decay:.5, pos:.22, buzz:.1},
    'Slap': {style:2, tone:.9, decay:.55, pos:.4, buzz:.4},
    'Pick': {style:1, tone:.75, decay:.6, pos:.18, buzz:.15},
    'Palm Mute': {style:3, tone:.4, decay:.3, pos:.2, buzz:0},
    'Deep Round': {style:0, tone:.3, decay:.75, pos:.3, buzz:0}},
  d808:{
    'Electro Boom': {bdT:.35, bdD:.8, sdT:.55, sdS:.6, cyD:.6, ohD:.45, acc:.7},
    'Tight Electro': {bdT:.5, bdD:.4, sdT:.6, sdS:.8, ohD:.25, acc:.6},
    'Long Boom': {bdT:.25, bdD:.95, sdT:.4, sdS:.5, ohD:.5, acc:.8}},
  dmx:{
    'Punchy': {bdP:.5, sdP:.55, acc:.5},
    'Low and Heavy': {bdP:.4, sdP:.45, t1P:.4, t2P:.4, t3P:.4, t4P:.4, acc:.6},
    'Bright': {bdP:.55, sdP:.6, chhP:.6, ohhP:.6, acc:.4}},
  voc:{
    'Electro Robot': {voc:{mic:.65, music:.6, hi:.45, comp:.75, blend:0, resp:.3, vol:.9}, tts:{voice:'robot', semi:0, speed:1}},
    'Clear Speech': {voc:{mic:.6, music:.55, hi:.8, comp:.6, blend:.1, resp:.25, vol:.9}, tts:{voice:'normal', semi:0, speed:.9}},
    'Deep Machine': {voc:{mic:.65, music:.65, hi:.3, comp:.8, blend:0, resp:.5, vol:.9}, tts:{voice:'deep', semi:-3, speed:.8}},
    'Chipmunk': {voc:{mic:.6, music:.6, hi:.7, comp:.6, blend:.35, resp:.2, vol:.9}, tts:{voice:'high', semi:7, speed:1.35}}}
};
