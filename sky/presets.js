/* Sky Sequencer sound data: OB-Xa patch and drum-kit parameters, drum patterns, and the random kit / patch / preset generators.
   The plugin reads the same parameter names from presets.json (written from this file by tools/make-sky-presets.js). */
(function (root){
  // [id, label, min, max, default, curve]  curve 'log' for frequencies and times
  const PATCH = [
    ['w1', 'Osc 1 wave', 0, 1, 0, 'int'], ['w2', 'Osc 2 wave', 0, 1, 1, 'int'], ['pw', 'Pulse width', 0.05, 0.5, 0.3], ['mix2', 'Osc 2 level', 0, 1, 0.7],
    ['det', 'Detune (cents)', 0, 30, 7], ['int2', 'Osc 2 interval', 0, 4, 0, 'int'], ['cut', 'Cutoff', 60, 12000, 1800, 'log'], ['res', 'Resonance', 0, 1, 0.3],
    ['poles', 'Filter 12/24 dB', 0, 1, 1, 'int'], ['env', 'Filter env (oct)', 0, 6, 2.5], ['fa', 'Filter attack', 0.001, 2, 0.005, 'log'], ['fd', 'Filter decay', 0.01, 3, 0.4, 'log'],
    ['fs', 'Filter sustain', 0, 1, 0.3], ['fr', 'Filter release', 0.01, 4, 0.3, 'log'], ['aa', 'Amp attack', 0.001, 3, 0.005, 'log'], ['ad', 'Amp decay', 0.01, 3, 0.5, 'log'],
    ['as', 'Amp sustain', 0, 1, 0.7], ['ar', 'Amp release', 0.01, 5, 0.3, 'log'], ['lfo', 'LFO rate', 0.05, 12, 5, 'log'], ['vib', 'Vibrato (cents)', 0, 50, 0], ['pwm', 'LFO to PW', 0, 0.45, 0],
  ];
  const INTERVALS = [0, 7, 12, -12, 5];   // semitones for 'int2'
  const KIT = [
    ['kp', 'Kick start pitch', 80, 300, 180, 'log'], ['kf', 'Kick pitch', 35, 75, 48], ['kd', 'Kick decay', 0.15, 1.2, 0.45, 'log'], ['kc', 'Kick click', 0, 1, 0.4],
    ['sf', 'Snare tone', 140, 300, 190], ['sd', 'Snare decay', 0.06, 0.4, 0.18, 'log'], ['ss', 'Snare snap', 0.3, 1, 0.7],
    ['hd', 'Closed hat decay', 0.02, 0.15, 0.05, 'log'], ['ho', 'Open hat decay', 0.15, 0.9, 0.4, 'log'], ['ht', 'Hat tone', 4000, 11000, 7000, 'log'],
    ['cd', 'Clap decay', 0.08, 0.45, 0.2, 'log'], ['tf', 'Tom pitch', 70, 240, 120, 'log'], ['td', 'Tom decay', 0.12, 0.6, 0.3, 'log'], ['rf', 'Rim pitch', 350, 1900, 900, 'log'],
    ['pf', 'Perc pitch', 300, 1300, 560, 'log'], ['pr', 'Perc ratio', 1.2, 2.6, 1.48], ['pd', 'Perc decay', 0.04, 0.45, 0.15, 'log'], ['lvl', 'Kit level', 0, 1.5, 0.9],
  ];
  const DRUMS = ['k', 's', 'ch', 'oh', 'cl', 't', 'r', 'p'];             // kick, snare, closed hat, open hat, clap, tom, rim, perc
  const DRUM_NAMES = ['Kick', 'Snare', 'Closed hat', 'Open hat', 'Clap', 'Tom', 'Rim', 'Perc'];
  const DRUM_NOTES = [36, 38, 42, 46, 39, 45, 37, 56];                 // General MIDI, sent on channel 10
  // 16 steps per bar: x = accent, o = soft, . = rest
  const PATTERNS = {
    four:     {name:'Four on the floor', k:'x...x...x...x...', cl:'....x.......x...', ch:'..o...o...o...o.'},
    house:    {name:'House', k:'x...x...x...x...', cl:'....x.......x...', ch:'o...o...o...o...', oh:'..x...x...x...x.'},
    breakbeat:{name:'Breakbeat', k:'x.........xx....', s:'....x..o.o..x..o', ch:'x.x.x.x.x.x.x.x.'},
    electro:  {name:'Electro', k:'x.....x...x.....', s:'....x.......x...', cl:'....x.......x...', ch:'xoxoxoxoxoxoxoxo', t:'.......o......o.'},
    halftime: {name:'Half-time', k:'x.........x.....', s:'........x.......', ch:'x.o.x.o.x.o.x.o.'},
    dnb:      {name:'Drum & bass', k:'x.........x.....', s:'....x..o....x...', ch:'xoxoxoxoxoxoxoxo'},
    boombap:  {name:'Boom bap', k:'x.......xx......', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', oh:'.............x..'},
    trap:     {name:'Trap', k:'x......x..x.....', s:'........x.......', ch:'xxxxxoxoxxxxxoxo', r:'.......o......o.'},
    shuffle:  {name:'Shuffle', k:'x.....x.x.......', s:'....x.......x...', ch:'x..x..x..x..x..x'},
    motorik:  {name:'Motorik', k:'x...x.x.x...x.x.', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.'},
    tribal:   {name:'Tribal', k:'x.......x.......', t:'x..x..x...x.x...', p:'..x...x..x...x..', r:'....o.......o...'},
    toms:     {name:'Tom rolls', k:'x.......x.......', t:'x.o.x.o.....x.xo'},
    perc:     {name:'Percussion', p:'x..o..x..o..x.o.', r:'..x...x...x...x.', cl:'.......x......x.'},
    sparse:   {name:'Sparse', k:'x...............', p:'.......o........', oh:'........x.......'},
  };
  const SCALES = ['minor', 'major', 'dorian', 'lydian', 'phrygian', 'pentatonic minor', 'pentatonic major', 'harmonic minor', 'whole tone'];
  const LANE_IDS = ['arp', 'rise', 'fall', 'drone', 'grid', 'hit'];
  const DEF_LANES = {arp:['pluck', 1, 0.8, 0.25, 0.35], rise:['glass', 2, 0.7, 0.35, 0.25], fall:['bell', 3, 0.65, 0.45, 0.3], drone:['pad', 4, 0.75, 0.6, 0.1], grid:['bass', 5, 0.75, 0.1, 0.15], hit:['perc', 10, 0.7, 0.15, 0.1]};

  // small seeded generator so a seed always gives the same preset
  function rng(seed){ let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
  const R = r => ({u:(a, b) => a + r() * (b - a), log:(a, b) => a * Math.pow(b / a, r()), i:(a, b) => a + Math.floor(r() * (b - a + 1)), pick:a => a[Math.floor(r() * a.length)], p:x => r() < x});
  const round = (x, d = 3) => +x.toFixed(d);
  const defaults = spec => Object.fromEntries(spec.map(p => [p[0], p[4]]));

  // OB-Xa patch shaped for what the lane does
  function randomPatch(role, r = Math.random){
    const g = R(r), p = defaults(PATCH);
    p.w1 = g.p(0.7) ? 0 : 1; p.w2 = g.p(0.5) ? 0 : 1; p.pw = round(g.u(0.12, 0.5)); p.mix2 = round(g.u(0.4, 1)); p.det = round(g.u(2, 16), 1);
    p.int2 = g.p(0.65) ? 0 : g.pick([1, 2, 3, 4]); p.poles = g.p(0.75) ? 1 : 0; p.res = round(g.u(0.05, 0.6));
    p.lfo = round(g.log(0.2, 7)); p.vib = g.p(0.3) ? round(g.u(2, 12), 1) : 0; p.pwm = (p.w1 || p.w2) && g.p(0.5) ? round(g.u(0.05, 0.35)) : 0;
    const pluck = () => { p.cut = round(g.log(250, 2500), 0); p.env = round(g.u(2, 5), 2); p.fa = 0.002; p.fd = round(g.log(0.08, 0.5)); p.fs = round(g.u(0, 0.25)); p.fr = round(g.log(0.08, 0.4));
      p.aa = 0.002; p.ad = round(g.log(0.15, 0.8)); p.as = round(g.u(0, 0.4)); p.ar = round(g.log(0.08, 0.5)); };
    const pad = () => { p.cut = round(g.log(400, 3000), 0); p.env = round(g.u(0.5, 2.5), 2); p.fa = round(g.log(0.2, 1.5)); p.fd = round(g.log(0.5, 2.5)); p.fs = round(g.u(0.3, 0.8)); p.fr = round(g.log(0.8, 3));
      p.aa = round(g.log(0.2, 1.6)); p.ad = round(g.log(0.5, 2)); p.as = round(g.u(0.6, 1)); p.ar = round(g.log(0.8, 3.5)); p.det = round(g.u(6, 20), 1); p.vib = round(g.u(0, 6), 1); };
    const bass = () => { p.cut = round(g.log(90, 700), 0); p.env = round(g.u(2, 5.5), 2); p.res = round(g.u(0.2, 0.75)); p.poles = 1; p.fa = 0.002; p.fd = round(g.log(0.08, 0.35)); p.fs = round(g.u(0, 0.3)); p.fr = 0.08;
      p.aa = 0.002; p.ad = round(g.log(0.2, 0.6)); p.as = round(g.u(0.4, 0.9)); p.ar = 0.06; p.int2 = g.pick([0, 2, 3]); p.vib = 0; };
    const lead = () => { p.cut = round(g.log(600, 5000), 0); p.env = round(g.u(1, 3.5), 2); p.fa = round(g.log(0.005, 0.1)); p.fd = round(g.log(0.2, 1)); p.fs = round(g.u(0.3, 0.7)); p.fr = round(g.log(0.1, 0.8));
      p.aa = round(g.log(0.005, 0.08)); p.ad = round(g.log(0.2, 1)); p.as = round(g.u(0.5, 0.9)); p.ar = round(g.log(0.1, 0.8)); p.vib = round(g.u(3, 15), 1); };
    ({arp:pluck, rise:lead, fall:lead, drone:pad, grid:bass, hit:pluck}[role] || lead)();
    return p;
  }
  function randomKit(r = Math.random){
    const g = R(r), k = {};
    for (const [id, , lo, hi, , curve] of KIT) k[id] = round(curve === 'log' ? g.log(lo, hi) : g.u(lo, hi));
    k.kp = Math.max(k.kp, k.kf * 2); k.lvl = round(g.u(0.75, 1.05));
    return k;
  }
  // a whole setup: key, tempo, sounds, OB-Xa patches, drum loops, kit and delay
  function generatePreset(r = Math.random, name = 'Generated'){
    const g = R(r), pre = {name, key:g.i(0, 11), scale:g.pick(['minor', 'minor', 'dorian', 'pentatonic minor', 'major', 'lydian', 'phrygian', 'harmonic minor', 'pentatonic major']),
      bpm:g.i(78, 132), delay:{mode:g.p(0.55) ? 1 : 0, mix:round(g.u(0.3, 0.7)), fb:round(g.u(0.25, 0.6)), tone:round(g.log(1800, 6000), 0)}, kit:randomKit(r), lanes:{}};
    const engines = {arp:['obxa', 'obxa', 'pluck', 'glass', 'bell'], rise:['obxa', 'glass', 'lead', 'bell'], fall:['obxa', 'bell', 'glass', 'lead'], drone:['obxa', 'obxa', 'pad'], grid:['obxa', 'obxa', 'bass', 'pluck'], hit:['perc', 'perc', 'obxa', 'bell']};
    // one lane carries the main groove, sometimes a second adds percussion
    const groove = g.pick(['grid', 'grid', 'drone', 'arp']), grooveId = g.pick(['four', 'house', 'breakbeat', 'electro', 'halftime', 'dnb', 'boombap', 'trap', 'shuffle', 'motorik', 'tribal']);
    const extra = g.p(0.5) ? g.pick(['hit', 'rise', 'fall']) : null;
    for (const id of LANE_IDS){
      const [, ch, vol, rev, dly] = DEF_LANES[id];
      pre.lanes[id] = {on:true, engine:g.pick(engines[id]), ch, vol:round(vol * g.u(0.8, 1.15)), rev:round(Math.min(1, rev * g.u(0.6, 1.6))), dly:round(Math.min(1, dly * g.u(0.5, 1.8))),
        drum:id === groove ? grooveId : id === extra ? g.pick(['perc', 'toms', 'sparse', 'tribal']) : 'none', dv:round(g.u(0.6, 0.95)), patch:randomPatch(id, r)};
    }
    return pre;
  }
  function defaultPreset(){
    const pre = {name:'Init', key:9, scale:'minor', bpm:96, delay:{mode:0, mix:0.5, fb:0.38, tone:2800}, kit:defaults(KIT), lanes:{}};
    for (const id of LANE_IDS){ const [engine, ch, vol, rev, dly] = DEF_LANES[id]; pre.lanes[id] = {on:true, engine, ch, vol, rev, dly, drum:'none', dv:0.8, patch:randomPatch(id, rng(7 + id.length))}; }
    return pre;
  }
  const api = {PATCH, INTERVALS, KIT, DRUMS, DRUM_NAMES, DRUM_NOTES, PATTERNS, SCALES, LANE_IDS, rng, randomPatch, randomKit, generatePreset, defaultPreset, defaults};
  if (typeof module !== 'undefined') module.exports = api; else root.SkyPresets = api;
})(typeof window !== 'undefined' ? window : globalThis);
