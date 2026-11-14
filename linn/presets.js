/* Omega Linn presets: 100 factory sounds in 10 families, and the dice (as on the Omega 110 and Omega Traks). A preset
   sets every sound parameter (the tuning knobs, HIHAT DECAY, the hat mode, the mixer, the Omega controls, EQ / ADSR /
   drive, sends, reverb, delay, master EQ and overdrive) and leaves TEMPO, MASTER VOLUME, the CLICK and the SOUND SET alone. Each family
   is a starting point plus rules that vary it, played out with a fixed seed per preset, so the 100 are the same
   everywhere; node linn/tools/gen-presets.mjs writes them into the plugin (plugins/linn/Presets.inc). */
'use strict';
(function (root) {
const D = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.OLINN;
const {P, PARAMS, CHS, defaults} = D;
const KEEP = ['tempo', 'volume', 'clickLv', 'clickPan', 'kit'];

function rng(seed){ let x = (seed >>> 0) || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
/* a rule: [param id ('*' = every channel, or a list 'BS,SN'), kind, a, b, chance]
   kinds: lin (a..b), log (a..b, even in ratio), int (a..b whole), set (always a), add (current + a..b), mul (current x a..b) */
function apply(p, rules, r){
  for (const [id, kind, a, b, chance = 1] of rules){
    const ids = id.includes('*') ? CHS.map(v => id.replace('*', v)) : id.split(',');
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
const TUNERS = 'tSN,tHT,tMT,tLT,tHC,tLC';
const DICE = {
  voices:[[TUNERS, 'lin', 10, 90, 0.6], ['hatDecay', 'lin', 5, 95, 0.6], ['hatMode', 'int', 0, 2, 0.3], ['mux', 'lin', 15, 40, 0.2], ['bassChip', 'int', 0, 1, 0.3],
    ['*lv', 'lin', 70, 100, 0.4], ['*pan', 'lin', 25, 75, 0.4], ['*tune', 'lin', -4, 4, 0.25], ['*dec', 'log', 0.08, 3, 0.2],
    ['*cut', 'log', 1500, 20000, 0.2], ['*res', 'lin', 0, 0.6, 0.15], ['*rev', 'int', 0, 1, 0.05], ['*start', 'lin', 0, 0.2, 0.06], ['poly', 'int', 0, 1, 0.2]],
  eq:[['*lsG', 'lin', -6, 8, 0.5], ['*lsF', 'log', 40, 300, 0.5], ['*pkG', 'lin', -9, 9, 0.6], ['*pkF', 'log', 150, 9000, 0.6], ['*pkQ', 'log', 0.5, 6, 0.6],
    ['*hsG', 'lin', -9, 7, 0.5], ['*hsF', 'log', 2500, 14000, 0.5]],
  env:[['*drv', 'lin', 0, 0.7, 0.3], ['CRatk,RDatk', 'log', 0.002, 0.3, 0.15], ['*sus', 'lin', 0.1, 0.9, 0.2], ['*dcy', 'log', 0.03, 1, 0.5], ['*rel', 'log', 0.03, 1.5, 0.25]],
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
    for (const [id] of rules){ const ids = id.includes('*') ? CHS.map(v => id.replace('*', v)) : id.split(','); for (const k of ids) out[P[k]] = d[P[k]]; }
    apply(out, rules, r);
  }
  return out;
}

/* ---- the factory: 10 families x 10 */
const WIDE = [['BSpan', 'set', 50], ['SNpan', 'set', 45], ['SSpan', 'set', 65], ['HHpan', 'set', 68], ['HTpan', 'set', 30], ['MTpan', 'set', 50], ['LTpan', 'set', 72],
  ['RDpan', 'set', 75], ['CRpan', 'set', 22], ['CBpan', 'set', 80], ['TBpan', 'set', 20], ['HCpan', 'set', 35], ['LCpan', 'set', 62], ['CWpan', 'set', 15], ['CPpan', 'set', 42]];
const FAMILIES = [
  {name:'Factory', names:['Stock LinnDrum', 'Dry Studio', 'Live Room', 'Bright', 'Warm', 'Tight', 'Big Hall', 'Plate Snare', 'Wide Stereo', 'Mix Engineer'],
    base:[], vary:[[TUNERS, 'add', -4, 4, 0.3], ['rvTime', 'log', 0.8, 2.5, 0.6]],
    each:[[], [['*snd', 'set', 0], ['rvLevel', 'set', 0]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.12, 0.3]], [['*hsG', 'lin', 2, 5], ['*hsF', 'set', 5000]], [['*hsG', 'lin', -6, -3], ['*lsG', 'lin', 2, 4]],
      [['*dec', 'lin', 0.15, 0.4], ['hatDecay', 'set', 20], ['rvLevel', 'set', 0.25]], [['rvProg', 'set', 0], ['rvTime', 'set', 3.4], ['*snd', 'lin', 0.2, 0.4]], [['rvProg', 'set', 3], ['SNsnd', 'set', 0.6], ['SSsnd', 'set', 0.4]],
      WIDE, WIDE.concat([['HHlv', 'set', 88], ['RDlv', 'set', 85], ['CBlv', 'set', 80], ['TBlv', 'set', 80], ['CPlv', 'set', 92]])]},
  {name:'Tuned Down', names:['Slow Clock', 'Heavy', 'Thud', 'Dark Toms', 'Sludge', 'Low Claps', 'Gong Crash', 'Deep Cowbell', 'Half Speed', 'Floor'],
    base:[[TUNERS, 'lin', 5, 22], ['mux', 'lin', 12, 20], ['*hsG', 'lin', -3, 0]],
    vary:[['rvProg', 'int', 0, 3], ['BSdrv', 'lin', 0, 0.3, 0.4], ['SNsnd', 'lin', 0.2, 0.4]],
    each:[[], [['BSdrv', 'set', 0.4], ['SNdrv', 'set', 0.3]], [['BStune', 'set', -5], ['BSlsG', 'set', 5], ['BSlsF', 'set', 70]], [['tHT', 'set', 12], ['tMT', 'set', 7], ['tLT', 'set', 2], ['HTsnd', 'set', 0.4], ['LTsnd', 'set', 0.4]], [['mux', 'set', 5], [TUNERS, 'set', 0]],
      [['CPtune', 'set', -8], ['CPsnd', 'set', 0.5]], [['CRtune', 'set', -12], ['CRsnd', 'set', 0.5], ['rvTime', 'set', 5]], [['CWtune', 'set', -9]], [['mux', 'set', 0], [TUNERS, 'set', 3]], [['BStune', 'set', -12], ['BSdrv', 'set', 0.5], ['mLsG', 'set', 4]]]},
  {name:'Tuned Up', names:['Fast Clock', 'Piccolo', 'Tin Kit', 'Chirp', 'Toy Box', 'Bright Toms', 'Glass Hats', 'Pop Claps', 'Timbales', 'Tiny'],
    base:[[TUNERS, 'lin', 55, 90], ['mux', 'lin', 30, 50], ['*snd', 'lin', 0.05, 0.25]],
    vary:[['rvProg', 'int', 1, 3], ['*pkG', 'lin', -2, 4, 0.3], ['*pkF', 'log', 1000, 6000, 0.3]],
    each:[[], [['tSN', 'set', 80], ['SNdec', 'set', 0.25]], [[TUNERS, 'lin', 85, 100], ['mux', 'set', 70]], [['BStune', 'set', 12], ['BSdec', 'set', 0.1]], [['*tune', 'set', 12], ['*lv', 'set', 80]],
      [['tHT', 'set', 95], ['tMT', 'set', 85], ['tLT', 'set', 75]], [['HHtune', 'set', 10], ['hatDecay', 'set', 15], ['HHsnd', 'set', 0.3]], [['CPtune', 'set', 6], ['CPsnd', 'set', 0.35]], [['tHT', 'set', 100], ['tMT', 'set', 92], ['SStune', 'set', 5], ['HTdec', 'set', 0.3], ['MTdec', 'set', 0.3]], [['*tune', 'set', 12], ['*dec', 'set', 0.15]]]},
  {name:'Hat Tricks', names:['Linn Sixteenths', 'Tight Chick', 'Loose Hats', 'LM-1 Loop', 'Restart', 'Wash', 'Disco Open', 'Chopped', 'Wide Hats', 'Hat Delay'],
    base:[['HHsnd', 'lin', 0.02, 0.1]],
    vary:[['hatDecay', 'lin', 10, 70], ['HHpan', 'lin', 40, 70]],
    each:[[['hatMode', 'set', 0], ['hatDecay', 'set', 30]], [['hatMode', 'set', 0], ['hatDecay', 'set', 3]], [['hatMode', 'set', 0], ['hatDecay', 'set', 90]], [['hatMode', 'set', 2], ['hatDecay', 'set', 25]], [['hatMode', 'set', 1], ['hatDecay', 'set', 40]],
      [['hatMode', 'set', 2], ['hatDecay', 'set', 100], ['HHsnd', 'set', 0.3]], [['hatMode', 'set', 0], ['hatDecay', 'set', 60], ['HHhsG', 'set', 4]], [['hatMode', 'set', 2], ['hatDecay', 'set', 8], ['HHdrv', 'set', 0.3]], [['HHpan', 'set', 85], ['RDpan', 'set', 15], ['HHdls', 'set', 0.2], ['dlSync', 'set', 3], ['dlFb', 'set', 0.2]], [['HHdls', 'set', 0.45], ['dlSync', 'set', 5], ['dlFb', 'set', 0.45], ['dlPing', 'set', 1]]]},
  {name:'Electro', names:['Planet Rock', 'Robot Claps', 'Breakin', 'Freestyle', 'Pop Lock', 'Laser Snare', 'Zapp Hats', 'Body Rock', 'Cowbell Funk', 'Tom Zaps'],
    base:[['tSN', 'lin', 30, 45], ['CPsnd', 'lin', 0.3, 0.5], ['CPdls', 'lin', 0.2, 0.4], ['dlSync', 'set', 6], ['rvProg', 'set', 3], ['hatDecay', 'lin', 5, 25]],
    vary:[['*pkG', 'lin', -3, 5, 0.3], ['*pkF', 'log', 800, 5000, 0.3], ['dlFb', 'lin', 0.2, 0.5], ['BStune', 'lin', -4, 0]],
    each:[[], [['CPtune', 'set', 3], ['CPres', 'set', 0.5], ['CPcut', 'set', 3000]], [['BSdrv', 'set', 0.3]], [['hatMode', 'set', 0], ['hatDecay', 'set', 45]], [['SSdls', 'set', 0.5], ['SStune', 'set', 5]],
      [['SNpkF', 'set', 2500], ['SNpkG', 'set', 6], ['SNpkQ', 'set', 4]], [['HHcut', 'set', 6000], ['HHres', 'set', 0.8], ['HHhsG', 'set', 5]], [['BStune', 'set', -6], ['BSdec', 'set', 2], ['BSlsG', 'set', 4]], [['CWtune', 'set', -2], ['CWdls', 'set', 0.4]], [['HTcut', 'set', 1500], ['HTres', 'set', 0.9], ['LTcut', 'set', 1200], ['LTres', 'set', 0.9]]]},
  {name:'Hip Hop', names:['Boom Bap', 'Dusty Break', 'Golden Era', 'Head Nod', 'Basement', 'Crate Dig', 'Thick Snare', 'Knock', 'Lazy Swing', 'Mixtape'],
    base:[['BStune', 'lin', -3, 0], ['tSN', 'lin', 12, 22], ['rvProg', 'set', 1], ['mHsG', 'lin', -4, -1], ['BSlsG', 'lin', 2, 5], ['SNpkG', 'lin', 2, 5], ['SNpkF', 'lin', 180, 260]],
    vary:[['odDrive', 'lin', 0.05, 0.25, 0.5], ['odMode', 'set', 2], ['HHlv', 'lin', 75, 92], ['SSlv', 'lin', 85, 100]],
    each:[[], [['mHsG', 'set', -6], ['mux', 'set', 20]], [['SNsnd', 'set', 0.35]], [['BSdec', 'set', 0.3]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.2, 0.35]],
      [['odDrive', 'set', 0.3], ['mLsG', 'set', 3]], [['SNdrv', 'set', 0.4], ['SNdec', 'set', 0.6]], [['SStune', 'set', -5], ['SSlv', 'set', 100], ['SSdrv', 'set', 0.4]], [['hatDecay', 'set', 18], ['bassChip', 'set', 1]], [['mP1G', 'set', 2], ['mP1F', 'set', 900], ['odDrive', 'set', 0.2]]]},
  {name:'80s Gated', names:['Gated Snare', 'Big Toms', 'Arena', 'Power Station', 'Synth Pop', 'Stadium Claps', 'Huge Room', 'Gated Toms', 'Ballad Hall', 'Thunder'],
    base:[['rvProg', 'set', 4], ['SNsnd', 'lin', 0.5, 0.8], ['HTsnd,MTsnd,LTsnd', 'lin', 0.4, 0.6], ['rvLevel', 'lin', 0.6, 0.85], ['SNdrv', 'lin', 0.1, 0.3]],
    vary:[['tSN', 'lin', 15, 35], ['BSlsG', 'lin', 1, 4], ['CPsnd', 'lin', 0.3, 0.6]],
    each:[[], [['tHT', 'set', 45], ['tMT', 'set', 35], ['tLT', 'set', 25]], [['rvProg', 'set', 0], ['rvTime', 'set', 3], ['*snd', 'lin', 0.2, 0.4]], [['SNdrv', 'set', 0.5], ['SNsus', 'set', 0.6], ['SNdcy', 'set', 0.2], ['SNrel', 'set', 0.05]],
      [['CPsnd', 'set', 0.6]], [['CPsnd', 'set', 0.8], ['CPtune', 'set', -2]], [['rvProg', 'set', 0], ['rvTime', 'set', 5]], [['HTsus,MTsus,LTsus', 'set', 0.5], ['HTdcy,MTdcy,LTdcy', 'set', 0.25], ['HTrel,MTrel,LTrel', 'set', 0.05]], [['rvProg', 'set', 0], ['rvTime', 'set', 4], ['RDsnd', 'set', 0.3]], [['BSdrv', 'set', 0.4], ['tLT', 'set', 10], ['odDrive', 'set', 0.15]]]},
  {name:'Dub Space', names:['Dub Siren', 'Echo Chamber', 'Tubby Stick', 'Spring Yard', 'Rockers', 'Steppers', 'Skank Delay', 'Dread Hall', 'Version', 'Melodica Air'],
    base:[['dlSync', 'set', 8], ['dlFb', 'lin', 0.55, 0.8], ['dlHiCut', 'lin', 1800, 3500], ['dlLoCut', 'set', 250], ['dlPing', 'set', 1], ['dlLevel', 'lin', 0.5, 0.75], ['SSdls', 'lin', 0.4, 0.8], ['SNdls', 'lin', 0.3, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 2.5, 5]],
    vary:[['CPdls', 'lin', 0.2, 0.6, 0.6], ['HHdls', 'lin', 0, 0.4, 0.5], ['dlDuck', 'lin', 0.3, 0.8], ['BStune', 'lin', -4, 0]],
    each:[[], [['dlSync', 'set', 6], ['dlFb', 'set', 0.85]], [['dlSync', 'set', 9], ['SSdls', 'set', 1]], [['rvProg', 'set', 3], ['rvTime', 'set', 2], ['*snd', 'lin', 0.3, 0.5]], [['dlSync', 'set', 7]],
      [['BSlsG', 'set', 5], ['BSdec', 'set', 1]], [['HHdls', 'set', 0.6], ['dlSync', 'set', 3]], [['rvTime', 'set', 7], ['rvLevel', 'set', 0.8]], [['dlModDepth', 'set', 0.6], ['dlDuck', 'set', 0.9]], [['dlSync', 'set', 11], ['dlFb', 'set', 0.7], ['CWdls', 'set', 0.6]]]},
  {name:'Industrial', names:['Factory Floor', 'Steel Plate', 'Grinder', 'Machine Shop', 'Rust', 'Pile Driver', 'Hammer', 'Foundry', 'Sheet Metal', 'Overload'],
    base:[['*drv', 'lin', 0.3, 0.8], ['odDrive', 'lin', 0.3, 0.7], ['odMode', 'set', 1], ['rvProg', 'set', 0], ['rvTime', 'lin', 3, 6], ['*snd', 'lin', 0.2, 0.45]],
    vary:[['*pkG', 'lin', -6, 9, 0.5], ['*pkF', 'log', 200, 5000, 0.5], ['*pkQ', 'log', 1, 8, 0.5], ['*tune', 'lin', -6, 4, 0.5]],
    each:[[], [['CRrev', 'set', 1], ['CRtune', 'set', -6]], [['HHres', 'set', 0.9], ['HHcut', 'set', 4000], ['HHdrv', 'set', 1]], [['SSdrv', 'set', 1], ['SStune', 'set', -8]], [['mHsG', 'set', -5], ['odMode', 'set', 2]],
      [['BSdrv', 'set', 1], ['BStune', 'set', -6]], [['SNdrv', 'set', 1], ['tSN', 'set', 5]], [['rvTime', 'set', 8], ['rvLevel', 'set', 0.7]], [['HHtune', 'set', 7], ['TBtune', 'set', -8], ['hatMode', 'set', 2]], [['odDrive', 'set', 1], ['odOut', 'set', -6]]]},
  {name:'Ambient', names:['Glass Cloud', 'Slow Swell', 'Night Air', 'Reverse Crash', 'Drift', 'Weightless', 'Bloom', 'Haze', 'Cathedral', 'Underwater'],
    base:[['CRatk', 'log', 0.1, 0.5], ['RDatk', 'log', 0.05, 0.3], ['*snd', 'lin', 0.35, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 4, 9], ['rvLevel', 'lin', 0.6, 0.85], ['dlSync', 'set', 11], ['*dls', 'lin', 0.1, 0.3], ['dlFb', 'lin', 0.5, 0.7]],
    vary:[['*tune', 'lin', -4, 4, 0.4], ['*hsG', 'lin', -6, 0], ['BSlv', 'lin', 70, 85], ['dlModDepth', 'lin', 0.3, 0.8]],
    each:[[], [['RDatk', 'set', 0.5], ['RDrel', 'set', 1.5]], [['rvHigh', 'set', 0.3], ['mHsG', 'set', -4]], [['CRrev', 'set', 1], ['RDrev', 'set', 1]], [['dlModDepth', 'set', 1], ['dlModRate', 'set', 0.15]],
      [['BSmute', 'set', 1], ['tLT', 'set', 5]], [['CRatk', 'set', 0.8], ['CRtune', 'set', -5]], [['*lv', 'mul', 0.85, 0.95], ['mHsG', 'set', -6]], [['rvTime', 'set', 10], ['rvDiff', 'set', 10]], [['mHsF', 'set', 2000], ['mHsG', 'set', -12], ['dlHiCut', 'set', 1500], ['*cut', 'set', 3000]]]},
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
else root.OLPRESETS = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
