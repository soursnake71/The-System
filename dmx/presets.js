/* Omega DMX presets: 100 factory sounds in 10 families, and the dice (as on the Omega 110 and Omega Traks). A preset sets
   every sound parameter (the eight cards' voice mods, tune offset, Omega controls and pad EQ / ADSR / drive, the reverb,
   the delay, the master EQ and the overdrive); the pattern, the kit, the faders and each voice's TUNE stay. Each family
   is a starting point plus rules that vary it, played out with a fixed seed per preset, so the 100 are the same
   everywhere; node dmx/tools/gen.mjs writes them into the plugin. Card ids: BA bass, SN snare, HH hi-hat, ST small toms,
   LT large toms, P1 perc 1 (rimshot, tambourine), P2 perc 2 (clap, shaker), CY cymbal. */
'use strict';
(function (root) {
const E = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.DMXE;
const {P, PARAMS, CARD_IDS, defaults} = E;

function rng(seed){ let x = (seed >>> 0) || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
/* a rule: [param id ('*' = every card, or a list 'BA,SN'), kind, a, b, chance]
   kinds: lin (a..b), log (a..b, even in ratio), int (a..b whole), set (always a), add (current + a..b), mul (current x a..b) */
function ids(id){ return id.includes('*') ? CARD_IDS.map(v => id.replace('*', v)) : id.split(','); }
function apply(p, rules, r){
  for (const [id, kind, a, b, chance = 1] of rules){
    for (const k of ids(id)){
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
  voices:[['*tune', 'lin', -15, 15, 0.5], ['*lv', 'lin', 0.75, 1.2, 0.5], ['*dec', 'log', 0.08, 3, 0.25], ['*pan', 'lin', -0.5, 0.5, 0.4],
    ['*cut', 'log', 1500, 20000, 0.25], ['*res', 'lin', 0, 0.6, 0.2], ['*start', 'lin', 0, 0.2, 0.08]],
  mods:[['*cap', 'log', 0.35, 3, 0.4], ['*dmod', 'int', 0, 2, 0.25], ['*early', 'int', 0, 2, 0.25], ['*filt', 'int', 0, 3, 0.3], ['*dac', 'int', 0, 2, 0.25], ['*rev', 'int', 0, 1, 0.06]],
  eq:[['*lsG', 'lin', -6, 8, 0.5], ['*lsF', 'log', 40, 300, 0.5], ['*pkG', 'lin', -9, 9, 0.6], ['*pkF', 'log', 150, 9000, 0.6], ['*pkQ', 'log', 0.5, 6, 0.6],
    ['*hsG', 'lin', -9, 7, 0.5], ['*hsF', 'log', 2500, 14000, 0.5]],
  env:[['*drv', 'lin', 0, 0.7, 0.3], ['HHatk,CYatk', 'log', 0.002, 0.3, 0.15], ['*sus', 'lin', 0.1, 0.9, 0.2], ['*dcy', 'log', 0.03, 1, 0.5], ['*rel', 'log', 0.03, 1.5, 0.25]],
  fx:[['*snd', 'lin', 0, 0.5, 0.7], ['*dls', 'lin', 0, 0.55, 0.35], ['rvProg', 'int', 0, 4, 0.7], ['rvTime', 'log', 0.6, 6, 0.7], ['rvHigh', 'lin', 0.25, 0.9, 0.6],
    ['rvDelay', 'lin', 0, 80, 0.5], ['rvDiff', 'lin', 4, 10, 0.4], ['rvEr', 'lin', 0.1, 0.8, 0.5], ['rvCut', 'log', 3000, 12000, 0.5], ['rvLevel', 'lin', 0.25, 0.8, 0.6],
    ['dlSync', 'int', 1, 13, 0.75], ['dlTime', 'log', 60, 900, 0.5], ['dlFb', 'lin', 0.15, 0.75, 0.7], ['dlHiCut', 'log', 1800, 12000, 0.6], ['dlLoCut', 'log', 40, 600, 0.5],
    ['dlModRate', 'log', 0.1, 3, 0.4], ['dlModDepth', 'lin', 0, 0.7, 0.5], ['dlDuck', 'lin', 0, 0.9, 0.6], ['dlPing', 'int', 0, 1, 0.5], ['dlLevel', 'lin', 0.2, 0.7, 0.7]],
  master:[['mLsG', 'lin', -3, 5, 0.6], ['mLsF', 'log', 40, 200, 0.5], ['mP1G', 'lin', -5, 4, 0.5], ['mP1F', 'log', 150, 1500, 0.5], ['mP1Q', 'log', 0.5, 3, 0.5],
    ['mP2G', 'lin', -4, 5, 0.5], ['mP2F', 'log', 1500, 8000, 0.5], ['mHsG', 'lin', -6, 4, 0.5], ['mHsF', 'log', 5000, 15000, 0.5],
    ['odDrive', 'lin', 0.05, 0.6, 0.35], ['odMode', 'int', 0, 2, 0.6], ['odMix', 'lin', 0.4, 1, 0.5], ['odOut', 'lin', -6, 0, 0.5]],
};
const DICE_PARTS = ['all', 'voices', 'mods', 'eq', 'env', 'fx', 'master'];
function dice(p, part = 'all', seed = (Math.random() * 4294967296) >>> 0){
  const r = rng(seed), d = defaults(), out = p.slice();
  for (const name of part === 'all' ? Object.keys(DICE) : [part]){
    const rules = DICE[name];
    for (const [id] of rules) for (const k of ids(id)) out[P[k]] = d[P[k]];
    apply(out, rules, r);
  }
  return out;
}

/* ---- the factory: 10 families x 10 */
const PAN_WIDE = [['BApan', 'set', 0], ['SNpan', 'set', -0.1], ['HHpan', 'set', 0.4], ['STpan', 'set', -0.5], ['LTpan', 'set', 0.5], ['P1pan', 'set', -0.4], ['P2pan', 'set', 0.4], ['CYpan', 'set', -0.3]];
const FAMILIES = [
  {name:'Factory', names:['Stock DMX', 'Dry Studio', 'Live Room', 'Bright', 'Warm', 'Tight', 'Big Hall', 'Plate Snare', 'Punchy', 'Wide Stereo'],
    base:[], vary:[['*tune', 'lin', -4, 4, 0.3], ['rvTime', 'log', 0.8, 2.5, 0.6]],
    each:[[], [['*snd', 'set', 0], ['rvLevel', 'set', 0]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.12, 0.3]], [['*hsG', 'lin', 2, 5], ['*hsF', 'set', 5000]], [['*hsG', 'lin', -6, -3], ['*lsG', 'lin', 2, 4]],
      [['*dec', 'lin', 0.15, 0.4], ['rvLevel', 'set', 0.25]], [['rvProg', 'set', 0], ['rvTime', 'set', 3.4], ['*snd', 'lin', 0.2, 0.4]], [['rvProg', 'set', 3], ['SNsnd', 'set', 0.6], ['P1snd', 'set', 0.4]],
      [['BAdrv', 'set', 0.25], ['SNdrv', 'set', 0.2], ['BAlsG', 'set', 3]], PAN_WIDE]},
  {name:'Tuned Down', names:['Slow Clock', 'Heavy', 'Thud', 'Dark Toms', 'Sludge', 'Low Claps', 'Gong Crash', 'Deep Rim', 'Half Speed', 'Floor'],
    base:[['*tune', 'lin', -25, -10], ['BAtune', 'lin', -20, -8], ['*hsG', 'lin', -3, 0]],
    vary:[['rvProg', 'int', 0, 3], ['BAdrv', 'lin', 0, 0.3, 0.4], ['SNsnd', 'lin', 0.2, 0.4]],
    each:[[], [['BAdrv', 'set', 0.4], ['SNdrv', 'set', 0.3]], [['BAtune', 'set', -35], ['BAlsG', 'set', 5], ['BAlsF', 'set', 70]], [['STtune', 'set', -35], ['LTtune', 'set', -40], ['STsnd', 'set', 0.4], ['LTsnd', 'set', 0.4]], [['*tune', 'lin', -40, -30], ['*dac', 'set', 1]],
      [['P2tune', 'set', -30], ['P2snd', 'set', 0.5]], [['CYtune', 'set', -40], ['CYsnd', 'set', 0.5], ['rvTime', 'set', 5]], [['P1tune', 'set', -35]], [['*tune', 'set', -40]], [['BAtune', 'set', -40], ['BAdrv', 'set', 0.5], ['mLsG', 'set', 4]]]},
  {name:'Tuned Up', names:['Fast Clock', 'Piccolo', 'Tin Kit', 'Chirp', 'Toy Box', 'Bright Toms', 'Glass Hats', 'Pop Claps', 'Timbales', 'Tiny'],
    base:[['*tune', 'lin', 8, 25], ['*snd', 'lin', 0.05, 0.25]],
    vary:[['rvProg', 'int', 1, 3], ['*pkG', 'lin', -2, 4, 0.3], ['*pkF', 'log', 1000, 6000, 0.3]],
    each:[[], [['SNtune', 'set', 35], ['SNdec', 'set', 0.25]], [['*tune', 'lin', 30, 40], ['*dac', 'set', 1]], [['BAtune', 'set', 40], ['BAdec', 'set', 0.1]], [['*tune', 'set', 40], ['*lv', 'set', 0.85]],
      [['STtune', 'set', 35], ['LTtune', 'set', 30]], [['HHtune', 'set', 38], ['HHsnd', 'set', 0.3]], [['P2tune', 'set', 25], ['P2snd', 'set', 0.35]], [['STtune', 'set', 40], ['LTtune', 'set', 38], ['P1tune', 'set', 20], ['STdec', 'set', 0.3], ['LTdec', 'set', 0.3]], [['*tune', 'set', 40], ['*dec', 'set', 0.15]]]},
  {name:'Circuit Bent', names:['Linear DAC', '6-Bit Crunch', 'No Filters', 'Long Decay', 'Choked', 'Backwards', 'Swapped Filters', 'Early Fade', 'Big Caps', 'Bent Everything'],
    base:[['*dac', 'int', 0, 2, 0.5], ['*filt', 'int', 0, 3, 0.4], ['mHsG', 'lin', -4, 0]],
    vary:[['*cap', 'log', 0.4, 3, 0.4], ['*dmod', 'int', 0, 2, 0.3], ['odDrive', 'lin', 0, 0.2, 0.3]],
    each:[[['*dac', 'set', 1]], [['*dac', 'set', 2]], [['*filt', 'set', 3], ['*dac', 'set', 0]], [['*dmod', 'set', 1], ['*cap', 'set', 4], ['*early', 'set', 2]], [['*dmod', 'set', 1], ['*cap', 'set', 0.25], ['*early', 'set', 1]],
      [['*rev', 'set', 1], ['CYrev', 'set', 1], ['*snd', 'lin', 0.2, 0.4]], [['BAfilt', 'set', 2], ['STfilt', 'set', 2], ['LTfilt', 'set', 2], ['SNfilt', 'set', 1], ['HHfilt', 'set', 1], ['CYfilt', 'set', 1]],
      [['*early', 'set', 1], ['*dmod', 'set', 1], ['*cap', 'lin', 0.5, 1]], [['*cap', 'set', 4], ['*dmod', 'set', 1], ['*early', 'set', 2], ['rvLevel', 'set', 0.3]],
      [['*dac', 'int', 1, 2], ['*filt', 'int', 1, 3], ['*cap', 'log', 0.3, 4], ['*dmod', 'int', 0, 2], ['*early', 'int', 0, 2], ['*rev', 'int', 0, 1, 0.3], ['*tune', 'lin', -30, 30]]]},
  {name:'Electro', names:['Planet Rock', 'Robot Claps', 'Breakin', 'Freestyle', 'Pop Lock', 'Laser Snare', 'Zapp Hats', 'Body Rock', 'Rim Funk', 'Tom Zaps'],
    base:[['SNtune', 'lin', 4, 15], ['P2snd', 'lin', 0.3, 0.5], ['P2dls', 'lin', 0.2, 0.4], ['dlSync', 'set', 6], ['rvProg', 'set', 3], ['HHdec', 'lin', 0.06, 0.15]],
    vary:[['*pkG', 'lin', -3, 5, 0.3], ['*pkF', 'log', 800, 5000, 0.3], ['dlFb', 'lin', 0.2, 0.5], ['BAtune', 'lin', -15, 0]],
    each:[[], [['P2tune', 'set', 12], ['P2res', 'set', 0.5], ['P2cut', 'set', 3000]], [['BAdrv', 'set', 0.3]], [['HHdec', 'set', 0.3]], [['P1dls', 'set', 0.5], ['P1tune', 'set', 20]],
      [['SNpkF', 'set', 2500], ['SNpkG', 'set', 6], ['SNpkQ', 'set', 4]], [['HHcut', 'set', 6000], ['HHres', 'set', 0.8], ['HHhsG', 'set', 5]], [['BAtune', 'set', -20], ['BAdmod', 'set', 2], ['BAlsG', 'set', 4]], [['P1tune', 'set', -8], ['P1dls', 'set', 0.4]], [['STcut', 'set', 1500], ['STres', 'set', 0.9], ['LTcut', 'set', 1200], ['LTres', 'set', 0.9]]]},
  {name:'Hip Hop', names:['Boom Bap', 'Dusty Break', 'Golden Era', 'Head Nod', 'Basement', 'Crate Dig', 'Thick Snare', 'Knock', 'Lazy Hats', 'Mixtape'],
    base:[['BAtune', 'lin', -12, 0], ['SNtune', 'lin', -12, -4], ['rvProg', 'set', 1], ['mHsG', 'lin', -4, -1], ['BAlsG', 'lin', 2, 5], ['SNpkG', 'lin', 2, 5], ['SNpkF', 'lin', 180, 260], ['*dac', 'int', 0, 1]],
    vary:[['odDrive', 'lin', 0.05, 0.25, 0.5], ['odMode', 'set', 2], ['HHlv', 'lin', 0.6, 0.9], ['P1lv', 'lin', 0.8, 1.1]],
    each:[[], [['mHsG', 'set', -6], ['*dac', 'set', 1]], [['SNsnd', 'set', 0.35]], [['BAdec', 'set', 0.3]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.2, 0.35]],
      [['odDrive', 'set', 0.3], ['mLsG', 'set', 3]], [['SNdrv', 'set', 0.4], ['SNdec', 'set', 0.6]], [['P1tune', 'set', -20], ['P1lv', 'set', 1.2], ['P1drv', 'set', 0.4]], [['HHdec', 'set', 0.1], ['HHlv', 'set', 0.7]], [['mP1G', 'set', 2], ['mP1F', 'set', 900], ['odDrive', 'set', 0.2]]]},
  {name:'80s Gated', names:['Gated Snare', 'Big Toms', 'Arena', 'Power Station', 'Synth Pop', 'Stadium Claps', 'Huge Room', 'Gated Toms', 'Ballad Hall', 'Thunder'],
    base:[['rvProg', 'set', 4], ['SNsnd', 'lin', 0.5, 0.8], ['STsnd', 'lin', 0.4, 0.6], ['LTsnd', 'lin', 0.4, 0.6], ['rvLevel', 'lin', 0.6, 0.85], ['SNdrv', 'lin', 0.1, 0.3]],
    vary:[['SNtune', 'lin', -8, 8], ['BAlsG', 'lin', 1, 4], ['P2snd', 'lin', 0.3, 0.6]],
    each:[[], [['STtune', 'set', -12], ['LTtune', 'set', -18], ['STlv', 'set', 1.2], ['LTlv', 'set', 1.2]], [['rvProg', 'set', 0], ['rvTime', 'set', 3], ['*snd', 'lin', 0.2, 0.4]], [['SNdrv', 'set', 0.5], ['SNsus', 'set', 0.6], ['SNdcy', 'set', 0.2], ['SNrel', 'set', 0.05]],
      [['P2snd', 'set', 0.6], ['*dac', 'set', 0]], [['P2snd', 'set', 0.8], ['P2tune', 'set', -8]], [['rvProg', 'set', 0], ['rvTime', 'set', 5]], [['STsus', 'set', 0.5], ['STdcy', 'set', 0.25], ['STrel', 'set', 0.05], ['LTsus', 'set', 0.5], ['LTdcy', 'set', 0.25], ['LTrel', 'set', 0.05]], [['rvProg', 'set', 0], ['rvTime', 'set', 4], ['CYsnd', 'set', 0.3]], [['BAdrv', 'set', 0.4], ['LTtune', 'set', -30], ['odDrive', 'set', 0.15]]]},
  {name:'Dub Space', names:['Dub Siren', 'Echo Chamber', 'Tubby Rim', 'Spring Yard', 'Rockers', 'Steppers', 'Skank Delay', 'Dread Hall', 'Version', 'Melodica Air'],
    base:[['dlSync', 'set', 8], ['dlFb', 'lin', 0.55, 0.8], ['dlHiCut', 'lin', 1800, 3500], ['dlLoCut', 'set', 250], ['dlPing', 'set', 1], ['dlLevel', 'lin', 0.5, 0.75], ['P1dls', 'lin', 0.4, 0.8], ['SNdls', 'lin', 0.3, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 2.5, 5]],
    vary:[['P2dls', 'lin', 0.2, 0.6, 0.6], ['HHdls', 'lin', 0, 0.4, 0.5], ['dlDuck', 'lin', 0.3, 0.8], ['BAtune', 'lin', -15, 0]],
    each:[[], [['dlSync', 'set', 6], ['dlFb', 'set', 0.85]], [['dlSync', 'set', 9], ['P1dls', 'set', 1]], [['rvProg', 'set', 3], ['rvTime', 'set', 2], ['*snd', 'lin', 0.3, 0.5]], [['dlSync', 'set', 7]],
      [['BAlsG', 'set', 5], ['BAdec', 'set', 1]], [['HHdls', 'set', 0.6], ['dlSync', 'set', 3]], [['rvTime', 'set', 7], ['rvLevel', 'set', 0.8]], [['dlModDepth', 'set', 0.6], ['dlDuck', 'set', 0.9]], [['dlSync', 'set', 11], ['dlFb', 'set', 0.7], ['P2dls', 'set', 0.6]]]},
  {name:'Industrial', names:['Factory Floor', 'Steel Plate', 'Grinder', 'Machine Shop', 'Rust', 'Pile Driver', 'Hammer', 'Foundry', 'Sheet Metal', 'Overload'],
    base:[['*drv', 'lin', 0.3, 0.8], ['odDrive', 'lin', 0.3, 0.7], ['odMode', 'set', 1], ['rvProg', 'set', 0], ['rvTime', 'lin', 3, 6], ['*snd', 'lin', 0.2, 0.45], ['*dac', 'int', 1, 2]],
    vary:[['*pkG', 'lin', -6, 9, 0.5], ['*pkF', 'log', 200, 5000, 0.5], ['*pkQ', 'log', 1, 8, 0.5], ['*tune', 'lin', -20, 15, 0.5]],
    each:[[], [['CYrev', 'set', 1], ['CYtune', 'set', -20]], [['HHres', 'set', 0.9], ['HHcut', 'set', 4000], ['HHdrv', 'set', 1]], [['P1drv', 'set', 1], ['P1tune', 'set', -30]], [['mHsG', 'set', -5], ['odMode', 'set', 2]],
      [['BAdrv', 'set', 1], ['BAtune', 'set', -20]], [['SNdrv', 'set', 1], ['SNtune', 'set', -20]], [['rvTime', 'set', 8], ['rvLevel', 'set', 0.7]], [['HHtune', 'set', 30], ['P1tune', 'set', -30]], [['odDrive', 'set', 1], ['odOut', 'set', -6]]]},
  {name:'Ambient', names:['Glass Cloud', 'Slow Swell', 'Night Air', 'Reverse Crash', 'Drift', 'Weightless', 'Bloom', 'Haze', 'Cathedral', 'Underwater'],
    base:[['HHatk', 'log', 0.05, 0.3], ['CYatk', 'log', 0.1, 0.5], ['*snd', 'lin', 0.35, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 4, 9], ['rvLevel', 'lin', 0.6, 0.85], ['dlSync', 'set', 11], ['*dls', 'lin', 0.1, 0.3], ['dlFb', 'lin', 0.5, 0.7]],
    vary:[['*tune', 'lin', -15, 15, 0.4], ['*hsG', 'lin', -6, 0], ['BAlv', 'lin', 0.6, 0.85], ['dlModDepth', 'lin', 0.3, 0.8]],
    each:[[], [['HHatk', 'set', 0.5], ['HHrel', 'set', 1.5]], [['rvHigh', 'set', 0.3], ['mHsG', 'set', -4]], [['CYrev', 'set', 1], ['HHrev', 'set', 1]], [['dlModDepth', 'set', 1], ['dlModRate', 'set', 0.15]],
      [['BAmute', 'set', 1], ['LTtune', 'set', -35]], [['CYatk', 'set', 0.8], ['CYtune', 'set', -20]], [['*lv', 'mul', 0.8, 0.9], ['mHsG', 'set', -6]], [['rvTime', 'set', 10], ['rvDiff', 'set', 10]], [['mHsF', 'set', 2000], ['mHsG', 'set', -12], ['dlHiCut', 'set', 1500], ['*cut', 'set', 3000]]]},
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

const API = {PRESETS, FAMILIES, DICE, DICE_PARTS, dice, apply, rng};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DMXPRESETS = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
