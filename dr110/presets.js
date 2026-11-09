/* Omega 110 presets: 100 factory sounds in 10 families, and the dice. A preset sets every sound parameter (voices,
   voice mods, pad EQ / ADSR / drive, sends, reverb, delay, master EQ and overdrive) and leaves TEMPO, VOLUME and
   SOURCE alone. Each family is a starting point plus rules that vary it, played out with a fixed seed per preset, so
   the 100 are the same everywhere; node dr110/tools/gen-presets.mjs writes them into the plugin (Presets.inc).
   The dice uses the same kind of rules (DICE) with a random seed, over all of it or one part (voices, EQ, envelopes and
   drive, effects, master). */
'use strict';
(function (root) {
const D = typeof module !== 'undefined' && module.exports ? require('./engine.js') : root.DR110;
const {P, PARAMS, NV, VOICES, defaults} = D;
const KEEP = ['tempo', 'volume', 'src'];         // a preset never changes these

function rng(seed){ let x = (seed >>> 0) || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }

/* a rule: [param id (a voice id may be '*' for every voice, or a list like 'BD,SD'), kind, a, b, chance]
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

// the dice: one rule list per part
const DICE = {
  voices:[['*tune', 'lin', -5, 5, 0.6], ['*dec', 'log', 0.5, 2.2, 0.6], ['*lv', 'lin', 0.75, 1.2, 0.5], ['OHpan,CHpan,CYpan,KNpan', 'lin', -0.45, 0.45, 0.5],
    ['bdSnap,sdSnap,sdMix,hhFilter,cyFilter,hcFilter', 'lin', 0.15, 0.85, 0.6], ['hhMix', 'lin', 0, 0.6, 0.2], ['bdBoost,sdBoost,hcBoost,bdBass', 'int', 0, 1, 0.3],
    ['bdExtend', 'lin', 0, 1, 0.3], ['bdEnv', 'int', 0, 1, 0.3], ['swAmount', 'lin', 0, 0.8, 0.25], ['swDecay', 'lin', 0.1, 0.8, 0.5], ['swTarget', 'int', 0, 1, 0.5],
    ['accent', 'lin', 3, 9, 0.5], ['balance', 'lin', -2, 2, 0.4], ['supply', 'int', 0, 1, 0.2]],
  eq:[['*lsG', 'lin', -6, 8, 0.5], ['*lsF', 'log', 40, 300, 0.5], ['*pkG', 'lin', -9, 9, 0.6], ['*pkF', 'log', 150, 9000, 0.6], ['*pkQ', 'log', 0.5, 6, 0.6],
    ['*hsG', 'lin', -9, 7, 0.5], ['*hsF', 'log', 2500, 14000, 0.5]],
  env:[['*drv', 'lin', 0, 0.7, 0.3], ['OHatk,CYatk', 'log', 0.002, 0.3, 0.15], ['*sus', 'lin', 0.1, 0.9, 0.2], ['*dcy', 'log', 0.03, 1, 0.5], ['*rel', 'log', 0.03, 1.5, 0.25]],
  fx:[['*snd', 'lin', 0, 0.5, 0.7], ['*dls', 'lin', 0, 0.55, 0.35], ['rvProg', 'int', 0, 4, 0.7], ['rvTime', 'log', 0.6, 6, 0.7], ['rvHigh', 'lin', 0.25, 0.9, 0.6],
    ['rvDelay', 'lin', 0, 80, 0.5], ['rvDiff', 'lin', 4, 10, 0.4], ['rvEr', 'lin', 0.1, 0.8, 0.5], ['rvCut', 'log', 3000, 12000, 0.5], ['rvLevel', 'lin', 0.25, 0.8, 0.6],
    ['dlSync', 'int', 1, 13, 0.75], ['dlTime', 'log', 60, 900, 0.5], ['dlFb', 'lin', 0.15, 0.75, 0.7], ['dlHiCut', 'log', 1800, 12000, 0.6], ['dlLoCut', 'log', 40, 600, 0.5],
    ['dlModRate', 'log', 0.1, 3, 0.4], ['dlModDepth', 'lin', 0, 0.7, 0.5], ['dlDuck', 'lin', 0, 0.9, 0.6], ['dlPing', 'int', 0, 1, 0.5], ['dlLevel', 'lin', 0.2, 0.7, 0.7]],
  master:[['mLsG', 'lin', -3, 5, 0.6], ['mLsF', 'log', 40, 200, 0.5], ['mP1G', 'lin', -5, 4, 0.5], ['mP1F', 'log', 150, 1500, 0.5], ['mP1Q', 'log', 0.5, 3, 0.5],
    ['mP2G', 'lin', -4, 5, 0.5], ['mP2F', 'log', 1500, 8000, 0.5], ['mHsG', 'lin', -6, 4, 0.5], ['mHsF', 'log', 5000, 15000, 0.5],
    ['odDrive', 'lin', 0.05, 0.6, 0.35], ['odMode', 'int', 0, 2, 0.6], ['odMix', 'lin', 0.4, 1, 0.5], ['odOut', 'lin', -6, 0, 0.5]],
};
const DICE_PARTS = ['all', 'voices', 'eq', 'env', 'fx', 'master'];
// the dice: a random version of p. part: one of DICE_PARTS; that part is reset to the factory sound first
function dice(p, part = 'all', seed = (Math.random() * 4294967296) >>> 0){
  const r = rng(seed), d = defaults(), out = p.slice();
  for (const name of part === 'all' ? Object.keys(DICE) : [part]){
    const rules = DICE[name];
    // back to the defaults for what this part touches, then roll
    for (const [id] of rules){ const ids = id.includes('*') ? VOICES.map(v => id.replace('*', v)) : id.split(','); for (const k of ids) out[P[k]] = d[P[k]]; }
    apply(out, rules, r);
  }
  return out;
}

/* ---- the factory: 10 families x 10 */
const FAMILIES = [
  {name:'Factory', names:['Stock 110', 'Stock Room', 'Battery Box', 'Bright Graphic', 'Warm Graphic', 'Tight Kit', 'Big Room', 'Plate Snare', 'Clap Hall', 'Knuckle Kit'],
    base:[], vary:[['*tune', 'lin', -1, 1, 0.3], ['rvTime', 'log', 0.8, 3, 0.6], ['*snd', 'lin', 0.05, 0.3, 0.4]],
    each:[[], [['rvProg', 'set', 1], ['*snd', 'lin', 0.15, 0.3]], [['supply', 'set', 1]], [['*hsG', 'lin', 2, 5], ['*hsF', 'set', 5000]], [['*hsG', 'lin', -6, -3], ['*lsG', 'lin', 2, 4]],
      [['*dec', 'lin', 0.5, 0.7], ['rvLevel', 'set', 0.25]], [['rvProg', 'set', 0], ['rvTime', 'set', 3.4], ['*snd', 'lin', 0.25, 0.45]], [['rvProg', 'set', 3], ['SDsnd', 'set', 0.6]],
      [['HCsnd', 'set', 0.7], ['rvTime', 'set', 4.5]], [['KNlv', 'set', 1.2], ['KNtune', 'set', -3], ['HCmute', 'set', 1]]]},
  {name:'808 Boom', names:['Boom 1', 'Long Kick', 'Sub Hum', 'Bass Extender', 'Deep Low', 'Boom Bap 808', 'Trap Boom', 'Slow Boom', 'Low Rumble', 'Singing Kick'],
    base:[['bdExtend', 'lin', 0.6, 0.95], ['BDlv', 'lin', 0.45, 0.6], ['BDtune', 'lin', -7, -2], ['BDlsG', 'lin', 1, 4], ['BDlsF', 'set', 70], ['bdEnv', 'int', 0, 1], ['BDsnd', 'set', 0], ['mLsG', 'lin', 1, 3]],
    vary:[['SDtune', 'lin', -3, 2], ['rvProg', 'int', 0, 3], ['BDdrv', 'lin', 0, 0.3, 0.4], ['HCsnd', 'lin', 0.2, 0.5]],
    each:[[], [['bdExtend', 'set', 1], ['BDdec', 'set', 1.6]], [['BDtune', 'set', -9], ['bdExtend', 'set', 1], ['BDhsG', 'set', -8]], [['bdExtend', 'set', 0.8]], [['BDtune', 'set', -12], ['BDlsG', 'set', 8]],
      [['sdBoost', 'set', 1], ['SDtune', 'set', -2]], [['CHdec', 'set', 0.4], ['CHdls', 'set', 0.3], ['dlSync', 'set', 3]], [['BDdec', 'set', 2.5], ['bdEnv', 'set', 1]], [['BDdrv', 'set', 0.4], ['bdBass', 'set', 1]], [['BDdec', 'set', 4], ['bdExtend', 'set', 1], ['BDlv', 'set', 0.35]]]},
  {name:'Electro', names:['Planet Graphic', 'Robot Clap', 'Breakin', 'Freestyle', 'Pop Lock', 'Laser Snare', 'Zapp Hats', 'Body Rock', 'Clone Snare', 'Electric Knuckle'],
    base:[['sdSnap', 'lin', 0.7, 0.95], ['SDtune', 'lin', 1, 4], ['CHdec', 'lin', 0.5, 0.8], ['HCdls', 'lin', 0.3, 0.5], ['dlSync', 'set', 6], ['rvProg', 'set', 3], ['HCsnd', 'lin', 0.3, 0.5], ['SDsnd', 'lin', 0.2, 0.4]],
    vary:[['*pkG', 'lin', -3, 5, 0.3], ['*pkF', 'log', 800, 5000, 0.3], ['bdExtend', 'lin', 0.2, 0.5, 0.5], ['dlFb', 'lin', 0.2, 0.5]],
    each:[[], [['hcFilter', 'set', 0.8], ['HCtune', 'set', 3]], [['bdSnap', 'set', 0.9], ['BDdrv', 'set', 0.2]], [['OHdec', 'set', 0.6]], [['KNdls', 'set', 0.5], ['KNtune', 'set', 5]],
      [['SDpkF', 'set', 2500], ['SDpkG', 'set', 6], ['SDpkQ', 'set', 4]], [['hhFilter', 'set', 0.85], ['CHhsG', 'set', 5]], [['bdBass', 'set', 1]], [['sdMix', 'set', 0.8]], [['KNdrv', 'set', 0.5], ['KNlv', 'set', 1.3]]]},
  {name:'Lo-Fi Tape', names:['Cassette', 'Worn Tape', 'Dusty', 'Low Batteries', 'AM Radio', 'Basement', 'Bedroom 4-Track', 'Warble', 'Old Sampler', 'Faded'],
    base:[['mHsG', 'lin', -9, -5], ['mHsF', 'lin', 4000, 7000], ['mLsG', 'lin', 1, 4], ['odDrive', 'lin', 0.1, 0.3], ['odMode', 'set', 2], ['odMix', 'lin', 0.5, 0.8], ['dlModDepth', 'lin', 0.5, 0.9], ['dlHiCut', 'set', 2500]],
    vary:[['supply', 'int', 0, 1], ['rvProg', 'int', 1, 2], ['*dec', 'mul', 0.8, 1.2], ['*dls', 'lin', 0, 0.2, 0.3]],
    each:[[], [['dlModDepth', 'set', 1], ['dlModRate', 'set', 0.3], ['SDdls', 'set', 0.3]], [['mP1G', 'set', 3], ['mP1F', 'set', 400]], [['supply', 'set', 1], ['odDrive', 'set', 0.35]], [['mLsF', 'set', 300], ['mLsG', 'set', -12], ['mHsF', 'set', 3500], ['mHsG', 'set', -14], ['mP1F', 'set', 1200], ['mP1G', 'set', 6]],
      [['rvProg', 'set', 1], ['*snd', 'lin', 0.3, 0.5]], [['odDrive', 'set', 0.4], ['odMode', 'set', 0]], [['dlModRate', 'set', 2.2], ['dlModDepth', 'set', 0.8], ['*dls', 'lin', 0.1, 0.3]], [['supply', 'set', 1], ['mHsG', 'set', -10]], [['*lv', 'mul', 0.8, 1], ['mHsG', 'set', -12]]]},
  {name:'Industrial', names:['Factory Floor', 'Steel Plate', 'Grinder', 'Machine Shop', 'Rust', 'Pile Driver', 'Hammer', 'Foundry', 'Sheet Metal', 'Overload'],
    base:[['*drv', 'lin', 0.3, 0.8], ['odDrive', 'lin', 0.3, 0.7], ['odMode', 'set', 1], ['rvProg', 'set', 0], ['rvTime', 'lin', 3, 6], ['*snd', 'lin', 0.2, 0.45], ['hhMix', 'lin', 0.2, 0.6]],
    vary:[['*pkG', 'lin', -6, 9, 0.5], ['*pkF', 'log', 200, 5000, 0.5], ['*pkQ', 'log', 1, 8, 0.5], ['*tune', 'lin', -6, 4, 0.5]],
    each:[[], [['CYdec', 'set', 3], ['cyFilter', 'set', 0.2]], [['hhMix', 'set', 1], ['OHdrv', 'set', 1]], [['KNdrv', 'set', 1], ['KNtune', 'set', -8]], [['mHsG', 'set', -5], ['odMode', 'set', 2]],
      [['BDdrv', 'set', 1], ['bdExtend', 'set', 0.4]], [['SDdrv', 'set', 1], ['SDtune', 'set', -6]], [['rvTime', 'set', 8], ['rvLevel', 'set', 0.7]], [['CHtune', 'set', 7], ['OHtune', 'set', 7]], [['odDrive', 'set', 1], ['odOut', 'set', -6]]]},
  {name:'Dub Space', names:['Dub Siren', 'Echo Chamber', 'King Tubby', 'Spring Yard', 'Rockers', 'Steppers', 'Skank Delay', 'Dread Hall', 'Version', 'Melodica Air'],
    base:[['dlSync', 'set', 8], ['dlFb', 'lin', 0.55, 0.8], ['dlHiCut', 'lin', 1800, 3500], ['dlLoCut', 'set', 250], ['dlPing', 'set', 1], ['dlLevel', 'lin', 0.5, 0.75], ['SDdls', 'lin', 0.4, 0.8], ['HCdls', 'lin', 0.3, 0.7], ['rvProg', 'set', 0], ['rvTime', 'lin', 2.5, 5]],
    vary:[['KNdls', 'lin', 0.2, 0.6, 0.6], ['OHdls', 'lin', 0, 0.4, 0.5], ['dlDuck', 'lin', 0.3, 0.8], ['BDtune', 'lin', -4, 0]],
    each:[[], [['dlSync', 'set', 6], ['dlFb', 'set', 0.85]], [['dlSync', 'set', 9], ['mHsG', 'set', -4]], [['rvProg', 'set', 3], ['rvTime', 'set', 2], ['*snd', 'lin', 0.3, 0.5]], [['dlSync', 'set', 7]],
      [['BDlsG', 'set', 5], ['bdExtend', 'set', 0.4]], [['CHdls', 'set', 0.6], ['dlSync', 'set', 3]], [['rvTime', 'set', 7], ['rvLevel', 'set', 0.8]], [['dlModDepth', 'set', 0.6], ['dlDuck', 'set', 0.9]], [['dlSync', 'set', 11], ['dlFb', 'set', 0.7], ['hcFilter', 'set', 0.2]]]},
  {name:'Hip Hop', names:['Boom Bap', 'Dusty Break', 'Golden Era', 'Head Nod', 'Basement Beat', 'Crate Dig', 'Thick Snare', 'Knock', 'Lazy Swing', 'Mixtape'],
    base:[['bdBass', 'set', 1], ['BDtune', 'lin', -3, 0], ['SDtune', 'lin', -3, -1], ['sdBoost', 'int', 0, 1], ['rvProg', 'set', 1], ['mHsG', 'lin', -4, -1], ['BDlsG', 'lin', 2, 5], ['SDpkG', 'lin', 2, 5], ['SDpkF', 'lin', 180, 260]],
    vary:[['odDrive', 'lin', 0.05, 0.25, 0.5], ['odMode', 'set', 2], ['CHlv', 'lin', 0.6, 0.9], ['KNlv', 'lin', 0.8, 1.2]],
    each:[[], [['mHsG', 'set', -6], ['supply', 'set', 1]], [['SDsnd', 'set', 0.35]], [['bdExtend', 'set', 0.3]], [['rvProg', 'set', 1], ['*snd', 'lin', 0.2, 0.35]],
      [['odDrive', 'set', 0.3], ['mLsG', 'set', 3]], [['SDdec', 'set', 1.6], ['sdMix', 'set', 0.65]], [['KNtune', 'set', -5], ['KNlv', 'set', 1.4], ['KNdrv', 'set', 0.4]], [['CHdec', 'set', 1.5], ['OHdec', 'set', 0.7]], [['mP1G', 'set', 2], ['mP1F', 'set', 900], ['odDrive', 'set', 0.2]]]},
  {name:'Techno', names:['Warehouse', 'Rumble', 'Berlin', 'Detroit Graphic', 'Minimal', 'Acid Room', 'Peak Time', 'Tunnel', 'Strobe', 'Dawn'],
    base:[['bdExtend', 'lin', 0.25, 0.5], ['BDdrv', 'lin', 0.1, 0.4], ['BDsnd', 'lin', 0.15, 0.35], ['rvProg', 'set', 0], ['rvTime', 'lin', 2, 5], ['CHdec', 'lin', 0.4, 0.7], ['dlSync', 'set', 5], ['CHdls', 'lin', 0.1, 0.3]],
    vary:[['OHdec', 'lin', 0.6, 1.4], ['HCdls', 'lin', 0.1, 0.4], ['mLsG', 'lin', 0, 3], ['dlFb', 'lin', 0.3, 0.6]],
    each:[[], [['BDsnd', 'set', 0.5], ['rvHigh', 'set', 0.3], ['rvCut', 'set', 2500]], [['odDrive', 'set', 0.2], ['odMode', 'set', 0]], [['CHhsG', 'set', 4], ['SDsnd', 'set', 0.4]], [['*lv', 'mul', 0.85, 1], ['KNlv', 'set', 1.2], ['KNdls', 'set', 0.4]],
      [['hhFilter', 'set', 0.8], ['OHpkG', 'set', 6], ['OHpkF', 'set', 6000], ['OHpkQ', 'set', 5]], [['BDdrv', 'set', 0.5], ['mLsG', 'set', 3]], [['rvTime', 'set', 7], ['BDsnd', 'set', 0.4]], [['CHatk', 'set', 0], ['CHsus', 'set', 0.2], ['CHdcy', 'set', 0.03], ['CHrel', 'set', 0.03]], [['CYatk', 'set', 0.15], ['CYsnd', 'set', 0.4]]]},
  {name:'Ambient', names:['Glass Cloud', 'Slow Swell', 'Night Air', 'Ice Field', 'Drift', 'Weightless', 'Bloom', 'Haze', 'Cathedral', 'Underwater'],
    base:[['OHatk', 'log', 0.05, 0.3], ['CYatk', 'log', 0.1, 0.5], ['*snd', 'lin', 0.35, 0.6], ['rvProg', 'set', 0], ['rvTime', 'lin', 4, 9], ['rvLevel', 'lin', 0.6, 0.85], ['dlSync', 'set', 11], ['*dls', 'lin', 0.1, 0.3], ['dlFb', 'lin', 0.5, 0.7]],
    vary:[['*tune', 'lin', -4, 4, 0.4], ['*hsG', 'lin', -6, 0], ['BDlv', 'lin', 0.6, 0.9], ['dlModDepth', 'lin', 0.3, 0.8]],
    each:[[], [['OHatk', 'set', 0.5], ['OHrel', 'set', 1.5]], [['rvHigh', 'set', 0.3], ['mHsG', 'set', -4]], [['hhFilter', 'set', 0.9], ['cyFilter', 'set', 0.9]], [['dlModDepth', 'set', 1], ['dlModRate', 'set', 0.15]],
      [['BDmute', 'set', 1], ['bdExtend', 'set', 0.7]], [['CYdec', 'set', 3], ['CYatk', 'set', 0.8]], [['*lv', 'mul', 0.8, 0.9], ['mHsG', 'set', -6]], [['rvTime', 'set', 10], ['rvDiff', 'set', 10]], [['mHsF', 'set', 2000], ['mHsG', 'set', -12], ['dlHiCut', 'set', 1500]]]},
  {name:'Circuit Bent', names:['Bent 1', 'Glitch Box', 'Pitch Freak', 'Noise Hats', 'Sweep Bender', 'Drone Kick', 'Chirp', 'Broken Toy', 'Short Circuit', 'Mutant'],
    base:[['*tune', 'lin', -10, 10, 0.7], ['*dec', 'log', 0.3, 4, 0.7], ['hhMix', 'lin', 0, 1, 0.5], ['swAmount', 'lin', 0.4, 1], ['swDecay', 'lin', 0.1, 0.6], ['swTarget', 'int', 0, 1]],
    vary:[['*pkG', 'lin', -10, 12, 0.4], ['*pkQ', 'log', 2, 10, 0.4], ['*pkF', 'log', 300, 8000, 0.4], ['*drv', 'lin', 0, 0.8, 0.3]],
    each:[[], [['dlSync', 'set', 1], ['dlFb', 'set', 0.8], ['*dls', 'lin', 0.1, 0.4]], [['BDtune', 'set', 12], ['SDtune', 'set', -12], ['CHtune', 'set', 12]], [['hhMix', 'set', 1], ['hhFilter', 'set', 0.2]], [['swAmount', 'set', 1], ['swDecay', 'set', 0.8]],
      [['BDdec', 'set', 4], ['bdExtend', 'set', 1], ['BDtune', 'set', 0]], [['KNtune', 'set', 12], ['KNdec', 'set', 0.3], ['HCtune', 'set', 10]], [['supply', 'set', 1], ['odDrive', 'set', 0.5], ['odMode', 'set', 1]], [['SDatk', 'set', 0], ['SDsus', 'set', 0], ['SDdcy', 'set', 0.02], ['SDrel', 'set', 0.01]], [['cyFilter', 'set', 0], ['hcFilter', 'set', 1], ['sdSnap', 'set', 1]]]},
];
function factory(){
  const out = [];
  FAMILIES.forEach((f, fi) => f.names.forEach((name, i) => {
    const r = rng(0x9e3779b9 ^ (fi * 1009 + i * 7919 + 17)), p = defaults();
    apply(p, f.base, r); if (i) apply(p, f.vary, r); apply(p, f.each[i], r);
    out.push({name:`${f.name} · ${name}`, family:f.name, p});
  }));
  return out;
}
const PRESETS = factory();
// put a preset's sound onto params, keeping tempo, volume and source
function load(params, preset){ const out = preset.slice(); for (const k of KEEP) out[P[k]] = params[P[k]]; return out; }

const API = {PRESETS, FAMILIES, DICE, DICE_PARTS, dice, load, apply, rng, KEEP};
if (typeof module !== 'undefined' && module.exports) module.exports = API;
else root.DRPRESETS = API;
})(typeof globalThis !== 'undefined' ? globalThis : this);
