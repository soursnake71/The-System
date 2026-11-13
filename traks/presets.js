/* Omega Traks presets: 100 factory sounds in 10 families, and the dice (as on the Omega 110). A preset sets every sound
   parameter (the 13 sounds' volume, tuning and the Omega controls, pad EQ / ADSR / drive, sends, reverb, delay, master
   EQ and overdrive, the DAC options) and leaves TEMPO, MASTER VOLUME and METRONOME VOLUME alone. Each family is a
   starting point plus rules that vary it, played out with a fixed seed per preset, so the 100 are the same everywhere;
   node traks/tools/gen-presets.mjs writes them into the plugin (plugins/traks/Presets.inc). */
'use strict';
(function (root) {
const D = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.OTRAKS;
const {P, PARAMS, VOICES, defaults} = D;
const KEEP = ['tempo', 'volume', 'metro'];

function rng(seed){ let x = (seed >>> 0) || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
/* a rule: [param id ('*' = every sound, or a list 'BS,SN'), kind, a, b, chance]
   kinds: lin (a..b), log (a..b, even in ratio), int (a..b whole), set (always a), add (current + a..b), mul (current x a..b) */
function apply(p, rules, r){
  for (const [id, kind, a, b, chance = 1] of rules){
    const ids = id.includes('*') ? VOICES.map(v => id.replace('*', v)) : id.split(',');
    for (const k of ids){
      const i = P[k]; if (i === undefined) throw new Error('no parameter ' + k);
      if (r() >= chance) continue;
      const q = PARAMS[i], u = r();
      let v = p[i];
      if (kind === 'lin') v = a + (b - a) * u;
      else if (kind === 'log') v = a * Math.pow(b / a, u);
      else if (kind === 'int') v = Math.floor(a + (b - a + 1) * u);
      else if (kind === 'set') v = a;
      else if (kind === 'add') v = v + a + (b - a) * u;
      else if (kind === 'mul') v = v * (a + (b - a) * u);
      p[i] = Math.max(q[2], Math.min(q[3], Math.round(v * 1000) / 1000));
    }
  }
  return p;
}
const DICE = {
  voices:[['*tune', 'lin', -5, 5, 0.6], ['*lv', 'lin', 0.6, 0.95, 0.5], ['*dec', 'log', 0.08, 3, 0.25], ['*pan', 'lin', -0.5, 0.5, 0.4],
    ['*cut', 'log', 1500, 20000, 0.25], ['*res', 'lin', 0, 0.6, 0.2], ['*rev', 'int', 0, 1, 0.06], ['*start', 'lin', 0, 0.25, 0.08],
    ['accent', 'lin', 3, 10, 0.5], ['dac', 'int', 0, 2, 0.25], ['interp', 'int', 0, 1, 0.2], ['poly', 'int', 0, 1, 0.25]],
  eq:[['*lsG', 'lin', -6, 8, 0.5], ['*lsF', 'log', 40, 300, 0.5], ['*pkG', 'lin', -9, 9, 0.6], ['*pkF', 'log', 150, 9000, 0.6], ['*pkQ', 'log', 0.5, 6, 0.6],
    ['*hsG', 'lin', -9, 7, 0.5], ['*hsF', 'log', 2500, 14000, 0.5]],
  env:[['*drv', 'lin', 0, 0.7, 0.3], ['OHatk,CRatk,RDatk', 'log', 0.002, 0.3, 0.15], ['*sus', 'lin', 0.1, 0.9, 0.2], ['*dcy', 'log', 0.03, 1, 0.5], ['*rel', 'log', 0.03, 1.5, 0.25]],
  fx:[['*snd', 'lin', 0, 0.5, 0.7], ['*dls', 'lin', 0, 0.55, 0.35], ['rvProg', 'int', 0, 4, 0.7], ['rvTime', 'log', 0.6, 6, 0.7], ['rvHigh', 'lin', 0.25, 0.9, 0.6],
    ['rvDelay', 'lin', 0, 80, 0.5], ['rvDiff', 'lin', 4, 10, 0.4], ['rvEr', 'lin', 0.1, 0.8, 0.5], ['rvCut', 'log', 3000, 12000, 0.5], ['rvLevel', 'lin', 0.25, 0.8, 0.6],
    ['dlSync', 'int', 1, 13, 0.75], ['dlTime', 'log', 60, 900, 0.5], ['dlFb', 'lin', 0.15, 0.75, 0.7], ['dlHiCut', 'log', 1800, 12000, 0.6], ['dlLoCut', 'log', 40, 600, 0.5],
    ['dlModRate', 'log', 0.1, 3, 0.4], ['dlModDepth', 'lin', 0, 0.7, 0.5], ['dlDuck', 'lin', 0, 0.9, 0.6], ['dlPing', 'int', 0, 1, 0.5], ['dlLevel', 'lin', 0.2, 0.7, 0.7]],
  master:[['mLsG', 'lin', -3, 5, 0.6], ['mLsF', 'log', 40, 200, 0.5], ['mP1G', 'lin', -5, 4, 0.5], ['mP1F', 'log', 150, 1500, 0.5], ['mP1Q', 'log', 0.5, 3, 0.5],
    ['mP2G', 'lin', -4, 5, 0.5], ['mP2F', 'log', 1500, 8000, 0.5], ['mHsG', 'lin', -6, 4, 0.5], ['mHsF', 'log', 5000, 15000, 0.5],
    ['odDrive', 'lin', 0.05, 0.6, 0.35], ['odMode', 'int', 0, 2, 0.6], ['odMix', 'lin', 0.4, 1, 0.5], ['odOut', 'lin', -6, 0, 0.5]],
};
const DICE_PARTS = ['all', 'voices', 'eq', 'env', 'fx', 'master'];
function dice(p, part = 'all', seed = (Math.random() * 4294967296) >>> 0){
  const r = rng(seed), d = defaults(), out = p.slice();
  for (const name of part === 'all' ? Object.keys(DICE) : [part]){
    const rules = DICE[name];
    for (const [id] of rules){ const ids = id.includes('*') ? VOICES.map(v => id.replace('*', v)) : id.split(','); for (const k of ids) out[P[k]] = d[P[k]]; }
    apply(out, rules, r);
  }
  return out;
}

/* ---- the factory: 10 families x 10 */
const FAMILIES = [
  {name:'Factory', names:['Stock Traks', 'Dry Studio', 'Live Room', 'Bright', 'Warm', 'Tight', 'Big Hall', 'Plate Snare', 'Accent Heavy', 'Wide Stereo'],
    base:[], vary:[['*tune', 'lin', -1, 1, 0.3], ['rvTime', 'log', 0.8, 2.5, 0.6]],
    each:[[], [['*snd', 'set', 0], ['rvLevel', 'set', 0]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.12, 0.3]], [['*hsG', 'lin', 2, 5], ['*hsF', 'set', 5000]], [['*hsG', 'lin', -6, -3], ['*lsG', 'lin', 2, 4]],
      [['*dec', 'lin', 0.15, 0.4], ['rvLevel', 'set', 0.25]], [['rvProg', 'set', 0], ['rvTime', 'set', 3.4], ['*snd', 'lin', 0.2, 0.4]], [['rvProg', 'set', 3], ['SNsnd', 'set', 0.6], ['RMsnd', 'set', 0.4]],
      [['accent', 'set', 12]], [['BSpan', 'set', 0], ['SNpan', 'set', -0.1], ['RMpan', 'set', 0.3], ['T1pan', 'set', -0.5], ['T2pan', 'set', 0.5], ['CRpan', 'set', -0.6], ['RDpan', 'set', 0.6], ['CHpan', 'set', 0.35], ['OHpan', 'set', 0.35], ['CPpan', 'set', -0.2], ['TBpan', 'set', 0.7], ['CBpan', 'set', -0.7], ['CApan', 'set', 0.5]]]},
  {name:'Tuned Down', names:['Slow Clock', 'Heavy', 'Thud', 'Dark Toms', 'Sludge', 'Low Claps', 'Gong Crash', 'Deep Cowbell', 'Half Speed', 'Floor'],
    base:[['*tune', 'lin', -7, -3], ['BStune', 'lin', -5, -2], ['*hsG', 'lin', -3, 0]],
    vary:[['rvProg', 'int', 0, 3], ['BSdrv', 'lin', 0, 0.3, 0.4], ['SNsnd', 'lin', 0.2, 0.4]],
    each:[[], [['BSdrv', 'set', 0.4], ['SNdrv', 'set', 0.3]], [['BStune', 'set', -9], ['BSlsG', 'set', 5], ['BSlsF', 'set', 70]], [['T1tune', 'set', -10], ['T2tune', 'set', -12], ['T1snd', 'set', 0.4], ['T2snd', 'set', 0.4]], [['*tune', 'lin', -12, -8], ['dac', 'set', 1]],
      [['CPtune', 'set', -8], ['CPsnd', 'set', 0.5]], [['CRtune', 'set', -12], ['CRsnd', 'set', 0.5], ['rvTime', 'set', 5]], [['CBtune', 'set', -9]], [['*tune', 'set', -12]], [['BStune', 'set', -12], ['BSdrv', 'set', 0.5], ['mLsG', 'set', 4]]]},
  {name:'Tuned Up', names:['Fast Clock', 'Piccolo', 'Tin Kit', 'Chirp', 'Toy Box', 'Bright Toms', 'Glass Hats', 'Pop Claps', 'Timbales', 'Tiny'],
    base:[['*tune', 'lin', 2, 7], ['*snd', 'lin', 0.05, 0.25]],
    vary:[['rvProg', 'int', 1, 3], ['*pkG', 'lin', -2, 4, 0.3], ['*pkF', 'log', 1000, 6000, 0.3]],
    each:[[], [['SNtune', 'set', 9], ['SNdec', 'set', 0.25]], [['*tune', 'lin', 8, 12], ['dac', 'set', 1]], [['BStune', 'set', 12], ['BSdec', 'set', 0.1]], [['*tune', 'set', 12], ['*lv', 'set', 0.7]],
      [['T1tune', 'set', 9], ['T2tune', 'set', 7]], [['CHtune', 'set', 10], ['OHtune', 'set', 10], ['CHsnd', 'set', 0.3]], [['CPtune', 'set', 6], ['CPsnd', 'set', 0.35]], [['T1tune', 'set', 12], ['T2tune', 'set', 10], ['RMtune', 'set', 5], ['T1dec', 'set', 0.3], ['T2dec', 'set', 0.3]], [['*tune', 'set', 12], ['*dec', 'set', 0.15]]]},
  {name:'Lo-Fi DAC', names:['Linear 8-Bit', '6-Bit Crunch', 'Smooth Clock', 'Old Cassette', 'Dusty', 'Low Batteries', 'AM Radio', 'Telephone', 'Warble', 'Faded'],
    base:[['dac', 'int', 1, 2], ['mHsG', 'lin', -8, -3], ['mHsF', 'lin', 4000, 8000], ['odDrive', 'lin', 0.05, 0.25], ['odMode', 'set', 2]],
    vary:[['*tune', 'lin', -2, 0, 0.5], ['dlModDepth', 'lin', 0.4, 0.9], ['dlHiCut', 'set', 2500], ['*dls', 'lin', 0, 0.15, 0.3]],
    each:[[['dac', 'set', 1]], [['dac', 'set', 2]], [['dac', 'set', 0], ['interp', 'set', 1], ['mHsG', 'set', -2]], [['mHsG', 'set', -10], ['mLsG', 'set', 3], ['odDrive', 'set', 0.3]], [['*cut', 'log', 3000, 7000], ['mP1G', 'set', 3], ['mP1F', 'set', 400]],
      [['*tune', 'lin', -3, -1], ['odDrive', 'set', 0.4]], [['mLsF', 'set', 300], ['mLsG', 'set', -12], ['mHsF', 'set', 3500], ['mHsG', 'set', -14], ['mP1F', 'set', 1200], ['mP1G', 'set', 6]],
      [['mLsF', 'set', 450], ['mLsG', 'set', -15], ['mHsF', 'set', 2800], ['mHsG', 'set', -15], ['odDrive', 'set', 0.5], ['odMode', 'set', 1]], [['dlModRate', 'set', 2.2], ['dlModDepth', 'set', 0.8], ['*dls', 'lin', 0.1, 0.3]], [['*lv', 'mul', 0.8, 1], ['mHsG', 'set', -12]]]},
  {name:'Electro', names:['Planet Rock', 'Robot Claps', 'Breakin', 'Freestyle', 'Pop Lock', 'Laser Snare', 'Zapp Hats', 'Body Rock', 'Cowbell Funk', 'Tom Zaps'],
    base:[['SNtune', 'lin', 1, 4], ['CPsnd', 'lin', 0.3, 0.5], ['CPdls', 'lin', 0.2, 0.4], ['dlSync', 'set', 6], ['rvProg', 'set', 3], ['CHdec', 'lin', 0.06, 0.15]],
    vary:[['*pkG', 'lin', -3, 5, 0.3], ['*pkF', 'log', 800, 5000, 0.3], ['dlFb', 'lin', 0.2, 0.5], ['BStune', 'lin', -4, 0]],
    each:[[], [['CPtune', 'set', 3], ['CPres', 'set', 0.5], ['CPcut', 'set', 3000]], [['BSdrv', 'set', 0.3]], [['OHdec', 'set', 0.3]], [['RMdls', 'set', 0.5], ['RMtune', 'set', 5]],
      [['SNpkF', 'set', 2500], ['SNpkG', 'set', 6], ['SNpkQ', 'set', 4]], [['CHcut', 'set', 6000], ['CHres', 'set', 0.8], ['CHhsG', 'set', 5]], [['BStune', 'set', -6], ['BSdec', 'set', 2], ['BSlsG', 'set', 4]], [['CBtune', 'set', -2], ['CBdls', 'set', 0.4]], [['T1cut', 'set', 1500], ['T1res', 'set', 0.9], ['T2cut', 'set', 1200], ['T2res', 'set', 0.9]]]},
  {name:'Hip Hop', names:['Boom Bap', 'Dusty Break', 'Golden Era', 'Head Nod', 'Basement', 'Crate Dig', 'Thick Snare', 'Knock', 'Lazy Swing', 'Mixtape'],
    base:[['BStune', 'lin', -3, 0], ['SNtune', 'lin', -3, -1], ['rvProg', 'set', 1], ['mHsG', 'lin', -4, -1], ['BSlsG', 'lin', 2, 5], ['SNpkG', 'lin', 2, 5], ['SNpkF', 'lin', 180, 260], ['dac', 'int', 0, 1]],
    vary:[['odDrive', 'lin', 0.05, 0.25, 0.5], ['odMode', 'set', 2], ['CHlv', 'lin', 0.5, 0.75], ['RMlv', 'lin', 0.7, 0.95]],
    each:[[], [['mHsG', 'set', -6], ['dac', 'set', 1]], [['SNsnd', 'set', 0.35]], [['BSdec', 'set', 0.3]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.2, 0.35]],
      [['odDrive', 'set', 0.3], ['mLsG', 'set', 3]], [['SNdrv', 'set', 0.4], ['SNdec', 'set', 0.6]], [['RMtune', 'set', -5], ['RMlv', 'set', 1], ['RMdrv', 'set', 0.4]], [['CHdec', 'set', 0.1], ['OHdec', 'set', 0.4]], [['mP1G', 'set', 2], ['mP1F', 'set', 900], ['odDrive', 'set', 0.2]]]},
  {name:'80s Gated', names:['Gated Snare', 'Big Toms', 'Arena', 'Power Station', 'Synth Pop', 'Stadium Claps', 'Huge Room', 'Gated Toms', 'Ballad Hall', 'Thunder'],
    base:[['rvProg', 'set', 4], ['SNsnd', 'lin', 0.5, 0.8], ['T1snd', 'lin', 0.4, 0.6], ['T2snd', 'lin', 0.4, 0.6], ['rvLevel', 'lin', 0.6, 0.85], ['SNdrv', 'lin', 0.1, 0.3]],
    vary:[['SNtune', 'lin', -2, 2], ['BSlsG', 'lin', 1, 4], ['CPsnd', 'lin', 0.3, 0.6]],
    each:[[], [['T1tune', 'set', -3], ['T2tune', 'set', -5], ['T1lv', 'set', 1], ['T2lv', 'set', 1]], [['rvProg', 'set', 0], ['rvTime', 'set', 3], ['*snd', 'lin', 0.2, 0.4]], [['SNdrv', 'set', 0.5], ['SNsus', 'set', 0.6], ['SNdcy', 'set', 0.2], ['SNrel', 'set', 0.05]],
      [['CPsnd', 'set', 0.6], ['dac', 'set', 0]], [['CPsnd', 'set', 0.8], ['CPtune', 'set', -2]], [['rvProg', 'set', 0], ['rvTime', 'set', 5]], [['T1sus', 'set', 0.5], ['T1dcy', 'set', 0.25], ['T1rel', 'set', 0.05], ['T2sus', 'set', 0.5], ['T2dcy', 'set', 0.25], ['T2rel', 'set', 0.05]], [['rvProg', 'set', 0], ['rvTime', 'set', 4], ['RDsnd', 'set', 0.3]], [['BSdrv', 'set', 0.4], ['T2tune', 'set', -9], ['odDrive', 'set', 0.15]]]},
  {name:'Dub Space', names:['Dub Siren', 'Echo Chamber', 'Tubby Rim', 'Spring Yard', 'Rockers', 'Steppers', 'Skank Delay', 'Dread Hall', 'Version', 'Melodica Air'],
    base:[['dlSync', 'set', 8], ['dlFb', 'lin', 0.55, 0.8], ['dlHiCut', 'lin', 1800, 3500], ['dlLoCut', 'set', 250], ['dlPing', 'set', 1], ['dlLevel', 'lin', 0.5, 0.75], ['RMdls', 'lin', 0.4, 0.8], ['SNdls', 'lin', 0.3, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 2.5, 5]],
    vary:[['CPdls', 'lin', 0.2, 0.6, 0.6], ['OHdls', 'lin', 0, 0.4, 0.5], ['dlDuck', 'lin', 0.3, 0.8], ['BStune', 'lin', -4, 0]],
    each:[[], [['dlSync', 'set', 6], ['dlFb', 'set', 0.85]], [['dlSync', 'set', 9], ['RMdls', 'set', 1]], [['rvProg', 'set', 3], ['rvTime', 'set', 2], ['*snd', 'lin', 0.3, 0.5]], [['dlSync', 'set', 7]],
      [['BSlsG', 'set', 5], ['BSdec', 'set', 1]], [['CHdls', 'set', 0.6], ['dlSync', 'set', 3]], [['rvTime', 'set', 7], ['rvLevel', 'set', 0.8]], [['dlModDepth', 'set', 0.6], ['dlDuck', 'set', 0.9]], [['dlSync', 'set', 11], ['dlFb', 'set', 0.7], ['CBdls', 'set', 0.6]]]},
  {name:'Industrial', names:['Factory Floor', 'Steel Plate', 'Grinder', 'Machine Shop', 'Rust', 'Pile Driver', 'Hammer', 'Foundry', 'Sheet Metal', 'Overload'],
    base:[['*drv', 'lin', 0.3, 0.8], ['odDrive', 'lin', 0.3, 0.7], ['odMode', 'set', 1], ['rvProg', 'set', 0], ['rvTime', 'lin', 3, 6], ['*snd', 'lin', 0.2, 0.45], ['dac', 'int', 1, 2]],
    vary:[['*pkG', 'lin', -6, 9, 0.5], ['*pkF', 'log', 200, 5000, 0.5], ['*pkQ', 'log', 1, 8, 0.5], ['*tune', 'lin', -6, 4, 0.5]],
    each:[[], [['CRrev', 'set', 1], ['CRtune', 'set', -6]], [['CHres', 'set', 0.9], ['CHcut', 'set', 4000], ['OHdrv', 'set', 1]], [['RMdrv', 'set', 1], ['RMtune', 'set', -8]], [['mHsG', 'set', -5], ['odMode', 'set', 2]],
      [['BSdrv', 'set', 1], ['BStune', 'set', -6]], [['SNdrv', 'set', 1], ['SNtune', 'set', -6]], [['rvTime', 'set', 8], ['rvLevel', 'set', 0.7]], [['CHtune', 'set', 7], ['OHtune', 'set', 7], ['TBtune', 'set', -8]], [['odDrive', 'set', 1], ['odOut', 'set', -6]]]},
  {name:'Ambient', names:['Glass Cloud', 'Slow Swell', 'Night Air', 'Reverse Crash', 'Drift', 'Weightless', 'Bloom', 'Haze', 'Cathedral', 'Underwater'],
    base:[['OHatk', 'log', 0.05, 0.3], ['CRatk', 'log', 0.1, 0.5], ['*snd', 'lin', 0.35, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 4, 9], ['rvLevel', 'lin', 0.6, 0.85], ['dlSync', 'set', 11], ['*dls', 'lin', 0.1, 0.3], ['dlFb', 'lin', 0.5, 0.7], ['interp', 'set', 1]],
    vary:[['*tune', 'lin', -4, 4, 0.4], ['*hsG', 'lin', -6, 0], ['BSlv', 'lin', 0.5, 0.7], ['dlModDepth', 'lin', 0.3, 0.8]],
    each:[[], [['OHatk', 'set', 0.5], ['OHrel', 'set', 1.5]], [['rvHigh', 'set', 0.3], ['mHsG', 'set', -4]], [['CRrev', 'set', 1], ['RDrev', 'set', 1], ['OHrev', 'set', 1]], [['dlModDepth', 'set', 1], ['dlModRate', 'set', 0.15]],
      [['BSmute', 'set', 1], ['T2tune', 'set', -10]], [['CRatk', 'set', 0.8], ['CRtune', 'set', -5]], [['*lv', 'mul', 0.8, 0.9], ['mHsG', 'set', -6]], [['rvTime', 'set', 10], ['rvDiff', 'set', 10]], [['mHsF', 'set', 2000], ['mHsG', 'set', -12], ['dlHiCut', 'set', 1500], ['*cut', 'set', 3000]]]},
];
function factory(){
  const out = [];
  FAMILIES.forEach((f, fi) => f.names.forEach((name, i) => {
    const r = rng(0x9e3779b9 ^ (fi * 1009 + i * 7919 + 23)), p = defaults();
    apply(p, f.base, r); if (i) apply(p, f.vary, r); apply(p, f.each[i], r);
    out.push({name:`${f.name} · ${name}`, family:f.name, p});
  }));
  return out;
}
const PRESETS = factory();
function load(params, preset){ const out = preset.slice(); for (const k of KEEP) out[P[k]] = params[P[k]]; return out; }

const API = {PRESETS, FAMILIES, DICE, DICE_PARTS, dice, load, apply, rng, KEEP};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.OTPRESETS = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
