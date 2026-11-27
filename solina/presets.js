/* Omega Solina presets: changes from the stock panel (engine.js defaults). The plugin's Presets.inc is generated from
   this file by solina/tools/gen.mjs. Names are "Category · Name". */
'use strict';
(function(G){
const off = {violin:0, viola:0, trumpet:0, horn:0, cello:0, contra:0};
const T = (...on) => { const o = Object.assign({}, off); for (const k of on) o[k] = 1; return o; };
const ph = (rate, depth, fb, mix) => ({phOn:1, phRate:rate, phDepth:depth, phFb:fb, phMix:mix == null ? .5 : mix});
const dl = (sync, fb, ret, tone, ping) => ({dlOn:1, dlSync:sync, dlFb:fb, dlRet:ret, dlTone:tone == null ? .6 : tone, dlPing:ping == null ? 1 : ping});
const rv = (size, decay, ret, damp, pre) => ({rvOn:1, rvSize:size, rvDecay:decay, rvRet:ret, rvDamp:damp == null ? .45 : damp, rvPre:pre == null ? 20 : pre});
const sends = (e, r) => { const o = {}; for (const k of ['vn', 'va', 'tp', 'hn', 'ce', 'cb']){ o[k + 'Ech'] = e; o[k + 'Rev'] = r; } return o; };
const P = (name, ...parts) => ({name, p:Object.assign({dlOn:0}, ...parts)});

const SOLINA_PRESETS = [
  // the panel as it left the factory
  P('Classic · Stock Ensemble', T('violin', 'viola', 'cello'), rv(.5, 2.2, .35), sends(0, .2)),
  P('Classic · Violin + Viola', T('violin', 'viola'), rv(.5, 2.4, .4), sends(0, .25)),
  P('Classic · Viola Alone', T('viola'), {depth:.4}, rv(.5, 2.2, .35), sends(0, .25)),
  P('Classic · Violin Alone', T('violin'), {depth:.4, susLen:.6}, rv(.5, 2.2, .35), sends(0, .25)),
  P('Classic · Full Organ Tabs', T('violin', 'viola', 'trumpet', 'horn', 'cello', 'contra'), {bassVol:.8}, rv(.55, 2.6, .35), sends(0, .2)),
  P('Classic · Dry, No Modulation', T('violin', 'viola', 'cello'), {ens:0, rvOn:0}, sends(0, 0)),
  P('Classic · Slow Crescendo', T('violin', 'viola', 'cello'), {cresc:1, susLen:.8}, rv(.6, 3, .4), sends(0, .3)),
  P('Classic · Low Output', T('violin', 'viola', 'cello'), {outSel:1, volume:.9}, rv(.5, 2, .35), sends(0, .2)),
  P('Classic · Sustain Off', T('violin', 'viola'), {sus:0}, rv(.4, 1.6, .35), sends(0, .25)),
  P('Classic · Deep Ensemble', T('violin', 'viola', 'cello'), {depth:.7, trem:.4}, rv(.55, 2.5, .35), sends(0, .25)),
  P('Classic · Wide Lines', T('violin', 'viola', 'cello'), {wide:1, depth:.45}, rv(.6, 2.8, .4), sends(0, .25)),
  P('Classic · Fast Chorus', T('violin', 'viola'), {chorusHz:1.6, tremHz:7.5, depth:.4}, rv(.5, 2, .3), sends(0, .2)),
  // strings
  P('Strings · Seventies Lush', T('violin', 'viola', 'cello'), {depth:.55, wide:1, cresc:.45, susLen:.7}, ph(.12, .7, .3, .35), rv(.7, 3.5, .45), sends(0, .3)),
  P('Strings · Disco Section', T('violin', 'viola'), {cresc:.1, susLen:.35, depth:.4, wide:1, eqHigh:3}, rv(.45, 1.6, .35), sends(0, .2)),
  P('Strings · Soft Pad', T('viola'), {cresc:.7, susLen:.9, vaTon:.6, depth:.45, eqHigh:-3}, rv(.7, 4, .45), sends(0, .4)),
  P('Strings · Octave Up', T('violin', 'viola'), {vaPit:12, depth:.4}, rv(.6, 3, .4), sends(0, .3)),
  P('Strings · Stereo Split', T('violin', 'viola', 'cello'), {vnPan:-.7, vaPan:.7, cePan:0, wide:1}, rv(.6, 3, .4), sends(0, .3)),
  P('Strings · Bowed Swell', T('violin', 'viola'), {vnAtk:.55, vaAtk:.45, cresc:.6, susLen:.8}, rv(.65, 3.2, .45), sends(0, .35)),
  P('Strings · Chamber', T('viola', 'cello'), {depth:.25, trem:.15, vaTon:.75}, rv(.35, 1.4, .35, .6, 10), sends(0, .3)),
  P('Strings · Bright Top Line', T('violin'), {eqHigh:4, eqMid:2, depth:.35, vnPan:.2}, rv(.5, 2.2, .35), sends(0, .3)),
  // brass
  P('Brass · Trumpet Tab', T('trumpet'), {depth:.25}, rv(.45, 1.8, .3), sends(0, .2)),
  P('Brass · Horn Tab', T('horn'), {depth:.25}, rv(.5, 2, .3), sends(0, .2)),
  P('Brass · Section', T('trumpet', 'horn', 'cello'), {depth:.3, tpPan:-.3, hnPan:.3, eqMid:2}, rv(.5, 2, .3), sends(0, .2)),
  P('Brass · Strings and Horns', T('violin', 'viola', 'horn', 'cello'), {hnLvl:.65, depth:.45}, rv(.6, 2.6, .4), sends(0, .25)),
  P('Brass · Muted', T('trumpet'), {tpTon:.45, outSel:1, volume:.9}, rv(.4, 1.6, .3), sends(0, .2)),
  P('Brass · Fifth Stack', T('trumpet', 'horn'), {hnPit:7, hnLvl:.6, depth:.3}, rv(.5, 2, .3), sends(0, .2)),
  // bass
  P('Bass · Cello Only', T('cello'), {bassVol:1, ens:0}, rv(.3, 1.2, .2), sends(0, .1)),
  P('Bass · Contra Bass', T('contra'), {bassVol:1, ens:0}, rv(.3, 1.2, .2), sends(0, .1)),
  P('Bass · Cello + Contra', T('cello', 'contra'), {bassVol:.9}, rv(.35, 1.4, .25), sends(0, .15)),
  P('Bass · Plucked', T('cello', 'contra'), {sus:1, susLen:.1, bassVol:1, ens:0}, rv(.3, 1, .2), sends(0, .1)),
  P('Bass · Strings over Bass', T('violin', 'viola', 'cello', 'contra'), {bassVol:.8, cePan:0, cbPan:0}, rv(.55, 2.4, .35), sends(0, .25)),
  // phaser
  P('Phased · Oxygen Strings', T('violin', 'viola', 'cello'), {depth:.5, wide:1, susLen:.85, cresc:.5}, ph(.08, 1, .55, .5), rv(.75, 4.5, .45), sends(0, .35)),
  P('Phased · Slow Sweep', T('violin', 'viola'), {depth:.4}, ph(.04, .9, .45, .5), rv(.7, 4, .45), sends(0, .35)),
  P('Phased · Jet', T('violin', 'viola', 'trumpet'), {depth:.35}, ph(.25, 1, .85, .5), rv(.55, 2.5, .35), sends(0, .25)),
  P('Phased · Fast Swirl', T('violin', 'viola'), {depth:.4}, ph(2.2, .6, .3, .5), rv(.5, 2, .3), sends(0, .2)),
  P('Phased · Horn Phase', T('horn', 'cello'), ph(.3, .8, .6, .5), rv(.5, 2.2, .35), sends(0, .25)),
  P('Phased · Equinox Pad', T('viola', 'horn'), {cresc:.8, susLen:1, depth:.55, wide:1}, ph(.06, 1, .7, .5), dl(1, .45, .45, .45), rv(.85, 6, .5), sends(.25, .45)),
  // echo and space
  P('Space · Quarter Echo', T('violin', 'viola'), dl(1, .45, .6), rv(.6, 3, .4), sends(.35, .3)),
  P('Space · Dotted Eighths', T('violin', 'viola', 'cello'), dl(3, .5, .55, .55), rv(.6, 3, .4), sends(.3, .3)),
  P('Space · Cathedral', T('violin', 'viola', 'cello', 'contra'), {cresc:.6, susLen:1}, rv(1, 9, .6, .55, 60), sends(0, .6)),
  P('Space · Starfield', T('violin', 'horn'), {cresc:.9, susLen:1, depth:.6, wide:1, vnPit:12}, ph(.05, 1, .5, .45), dl(5, .6, .5, .4), rv(.95, 8, .55, .5, 40), sends(.4, .6)),
  P('Space · Tape Echo Strings', T('viola', 'cello'), {drive:.2, eqHigh:-3}, dl(0, .6, .6, .3, 0), {dlTime:420}, rv(.5, 2.2, .3), sends(.45, .2)),
  P('Space · Frozen Lake', T('viola'), {cresc:1, susLen:1, vaAtk:.7, vaTon:.55, depth:.6}, dl(6, .7, .45, .35), rv(1, 10, .65, .6, 80), sends(.4, .7)),
  P('Space · Ping Pong Brass', T('trumpet', 'horn'), dl(2, .55, .6, .6, 1), rv(.5, 2.4, .35), sends(.4, .25)),
  // lo-fi and character
  P('Lo-Fi · Worn Unit', T('violin', 'viola', 'cello'), {drive:.35, outSel:1, volume:.9, depth:.6, tune:-18, eqHigh:-4}, rv(.4, 1.6, .3), sends(0, .2)),
  P('Lo-Fi · Overdriven Amp', T('violin', 'viola', 'trumpet'), {drive:.7, eqMid:4, eqHigh:-2}, rv(.4, 1.4, .25), sends(0, .15)),
  P('Lo-Fi · Telephone', T('violin', 'viola'), {outSel:1, eqLow:-12, eqMid:8, eqHigh:-8, volume:.95}, rv(.3, 1, .2), sends(0, .1)),
  P('Lo-Fi · Seasick', T('violin', 'viola'), {depth:1, trem:.9, chorusHz:1.2, tremHz:4}, rv(.5, 2.2, .3), sends(0, .25)),
  // mixer tricks (RX5-style strips)
  P('Mixer · Hard Panned Sections', T('violin', 'viola', 'trumpet', 'horn', 'cello'), {vnPan:-1, vaPan:1, tpPan:-.5, hnPan:.5, cePan:0, tpLvl:.55, hnLvl:.55}, rv(.6, 2.6, .35), sends(0, .25)),
  P('Mixer · Dry Bass, Wet Strings', T('violin', 'viola', 'cello', 'contra'), {ceEns:0, cbEns:0, ceRev:0, cbRev:0, vnRev:.5, vaRev:.5}, rv(.7, 4, .5)),
  P('Mixer · Echo on Violin Only', T('violin', 'viola', 'cello'), sends(0, .25), {vnEch:.6}, dl(2, .5, .6)),
  P('Mixer · Octave Choir', T('violin', 'viola', 'horn'), {vnPit:-12, hnPit:12, hnLvl:.5, hnTon:.6}, rv(.75, 4, .45), sends(0, .4)),
  P('Mixer · Slow Bass Swell', T('viola', 'cello', 'contra'), {ceAtk:.6, cbAtk:.7, ceDec:.8, cbDec:.8}, rv(.6, 3, .4), sends(0, .3)),
  P('Mixer · Pizzicato', T('violin', 'viola', 'cello'), {sus:1, susLen:0, vnDec:.15, vaDec:.15, ceDec:.15, cresc:0, depth:.25}, dl(4, .3, .4), rv(.45, 1.8, .4), sends(.15, .3)),
  // songs: settings voiced after records known for the Solina / ARP String Ensemble (interpretations, not the records' own patches)
  P('Songs · Dream Weaver', T('violin', 'viola', 'cello'), {cresc:.75, susLen:.9, depth:.55, trem:.3, wide:1, vnAtk:.35, vaAtk:.3, eqHigh:1}, ph(.07, .8, .35, .4), dl(1, .35, .35, .45), rv(.85, 5.5, .5, .5, 40), sends(.2, .45), {master:0.95}),
  P('Songs · Shine On', T('violin', 'viola', 'cello', 'contra'), {cresc:1, susLen:1, depth:.6, trem:.35, wide:1, vnAtk:.55, vaAtk:.5, ceAtk:.4, eqHigh:-2}, rv(.9, 7, .55, .55, 50), sends(.1, .5), {dlOn:1, dlSync:3, dlFb:.3, dlRet:.3, dlTone:.4}, {master:0.67}),
  P('Songs · Oxygene', T('violin', 'viola'), {cresc:.55, susLen:.95, depth:.5, wide:1, vaPit:-12, vaLvl:.6}, ph(.06, 1, .65, .5), dl(5, .55, .5, .45), rv(.85, 6, .5, .5, 30), sends(.35, .45), {master:1.00}),
  P('Songs · Equinoxe', T('viola', 'horn', 'cello'), {cresc:.6, susLen:.9, depth:.55, wide:1, hnLvl:.55}, ph(.12, .9, .55, .5), dl(2, .5, .45, .5), rv(.8, 5, .45), sends(.3, .4), {master:0.93}),
  P('Songs · Someone Saved My Life', T('violin', 'viola', 'cello'), {cresc:.35, susLen:.6, depth:.4, trem:.2, eqMid:1}, rv(.55, 2.4, .35, .5, 15), sends(0, .3), {master:0.68}),
  P('Songs · Funeral for a Friend', T('violin', 'viola', 'horn', 'cello', 'contra'), {cresc:.8, susLen:1, depth:.45, hnLvl:.5, vnAtk:.4, vaAtk:.4}, rv(.95, 8, .55, .5, 60), sends(0, .55), {master:0.55}),
  P('Songs · All Cats Are Grey', T('viola', 'cello'), {cresc:.5, susLen:.85, depth:.65, trem:.4, chorusHz:.9, outSel:1, volume:.9, vaTon:.7, eqHigh:-3}, rv(.85, 6, .55, .65, 40), sends(.15, .55), {dlOn:1, dlSync:1, dlFb:.35, dlRet:.3, dlTone:.35}, {master:0.58}),
  P('Songs · Faith', T('viola'), {cresc:.9, susLen:1, depth:.7, trem:.45, vaTon:.55, vaAtk:.6, eqLow:2, eqHigh:-5}, rv(1, 9, .6, .7, 70), sends(0, .65), {master:1.00}),
  P('Songs · Atmosphere', T('viola', 'cello', 'contra'), {cresc:.7, susLen:.9, depth:.5, trem:.25, outSel:1, volume:.95, eqHigh:-4, drive:.1}, rv(1, 8, .6, .7, 50), sends(0, .6), {master:0.47}),
  P('Songs · Decades', T('violin', 'viola'), {cresc:.6, susLen:.85, depth:.55, wide:1, vnTon:.65, vaTon:.65, eqHigh:-3}, dl(3, .4, .35, .35), rv(.9, 6, .55, .6, 40), sends(.2, .5), {master:0.89}),
  P('Songs · Warszawa', T('viola', 'horn', 'cello', 'contra'), {cresc:.85, susLen:1, depth:.35, trem:.15, hnLvl:.7, vaLvl:.6, hnTon:.6, outSel:1, volume:.95, eqHigh:-4}, rv(.9, 7, .5, .7, 60), sends(0, .5), {master:0.44}),
  P('Songs · La Femme d\'Argent', T('violin', 'viola'), {cresc:.4, susLen:.75, depth:.6, trem:.3, wide:1, drive:.15, eqHigh:-2}, ph(.15, .7, .3, .35), dl(0, .45, .4, .35, 0), {dlTime:330}, rv(.6, 3, .4), sends(.3, .35), {master:0.93}),
  P('Songs · Butterfly', T('violin', 'viola', 'cello'), {cresc:.5, susLen:.8, depth:.5, trem:.3, chorusHz:.6, eqMid:-2}, ph(.1, .7, .3, .4), rv(.7, 3.5, .45), sends(.1, .35), {master:0.92}),
  P('Songs · Fool\'s Overture', T('violin', 'viola', 'trumpet', 'cello'), {cresc:.7, susLen:.9, depth:.45, tpLvl:.5, tpTon:.7}, rv(.85, 5, .5, .5, 40), sends(.1, .45), {dlOn:1, dlSync:1, dlFb:.3, dlRet:.3}, {master:0.69}),
  P('Songs · Soft Disco Strings', T('violin', 'viola'), {cresc:.05, susLen:.3, depth:.45, wide:1, vnPit:12, vnLvl:.6, eqHigh:3}, ph(.35, .5, .2, .3), rv(.5, 1.8, .35), sends(.15, .25), {dlOn:1, dlSync:2, dlFb:.25, dlRet:.25}, {master:1.00}),
  P('Songs · Berlin School Drone', T('viola', 'horn', 'cello', 'contra'), {cresc:1, susLen:1, depth:.7, trem:.5, chorusHz:.4, tremHz:4.5, wide:1}, ph(.03, 1, .6, .5), dl(5, .65, .5, .4), rv(1, 10, .6, .6, 80), sends(.4, .6), {master:0.67}),
];

G.SOLINA_PRESETS = SOLINA_PRESETS;
if (typeof module !== 'undefined' && module.exports) module.exports = SOLINA_PRESETS;
})(typeof globalThis !== 'undefined' ? globalThis : this);
