/* Sky Sequencer sound data: OB-Xa patch and drum-kit parameters, drum patterns, lane rhythms, and the random
   kit / patch / preset generators. The plugin reads the same names from presets.json (written from this file by
   tools/make-sky-presets.js) and has the same tables in plugin/Source/Presets.h. */
(function (root){
  // [id, label, min, max, default, curve]  curve 'log' for frequencies and times, 'int' for switches
  const PATCH = [
    ['w1', 'Osc 1 wave', 0, 1, 0, 'int'], ['w2', 'Osc 2 wave', 0, 1, 1, 'int'], ['pw', 'Pulse width', 0.05, 0.5, 0.3], ['mix2', 'Osc 2 level', 0, 1, 0.7],
    ['det', 'Detune (cents)', 0, 30, 7], ['int2', 'Osc 2 interval', 0, 4, 0, 'int'], ['cut', 'Cutoff', 60, 12000, 1800, 'log'], ['res', 'Resonance', 0, 1, 0.3],
    ['poles', 'Filter 12/24 dB', 0, 1, 1, 'int'], ['env', 'Filter env (oct)', 0, 6, 2.5], ['fa', 'Filter attack', 0.001, 2, 0.005, 'log'], ['fd', 'Filter decay', 0.01, 3, 0.4, 'log'],
    ['fs', 'Filter sustain', 0, 1, 0.3], ['fr', 'Filter release', 0.01, 4, 0.2, 'log'], ['aa', 'Amp attack', 0.001, 3, 0.005, 'log'], ['ad', 'Amp decay', 0.01, 3, 0.4, 'log'],
    ['as', 'Amp sustain', 0, 1, 0.5], ['ar', 'Amp release', 0.01, 5, 0.15, 'log'], ['lfo', 'LFO rate', 0.05, 12, 5, 'log'], ['vib', 'Vibrato (cents)', 0, 50, 0], ['pwm', 'LFO to PW', 0, 0.45, 0],
  ];
  const INTERVALS = [0, 7, 12, -12, 5];   // semitones for 'int2'
  const KIT = [
    ['kp', 'Kick start pitch', 80, 300, 180, 'log'], ['kf', 'Kick pitch', 35, 75, 48], ['kd', 'Kick decay', 0.15, 1.2, 0.45, 'log'], ['kc', 'Kick click', 0, 1, 0.4],
    ['sf', 'Snare tone', 140, 300, 190], ['sd', 'Snare decay', 0.06, 0.4, 0.18, 'log'], ['ss', 'Snare snap', 0.3, 1, 0.7],
    ['hd', 'Closed hat decay', 0.02, 0.15, 0.05, 'log'], ['ho', 'Open hat decay', 0.15, 0.9, 0.4, 'log'], ['ht', 'Hat tone', 4000, 11000, 7000, 'log'],
    ['cd', 'Clap decay', 0.08, 0.45, 0.2, 'log'], ['tf', 'Tom pitch', 70, 240, 120, 'log'], ['td', 'Tom decay', 0.12, 0.6, 0.3, 'log'], ['rf', 'Rim pitch', 350, 1900, 900, 'log'],
    ['pf', 'Perc pitch', 300, 1300, 560, 'log'], ['pr', 'Perc ratio', 1.2, 2.6, 1.48], ['pd', 'Perc decay', 0.04, 0.45, 0.15, 'log'],
    ['cy', 'Cymbal decay', 0.4, 2.5, 1.2, 'log'], ['ct', 'Cymbal tone', 3000, 9000, 5500, 'log'], ['shd', 'Shaker decay', 0.03, 0.15, 0.06, 'log'],
    ['cgf', 'Conga pitch', 150, 420, 250, 'log'], ['cgd', 'Conga decay', 0.08, 0.4, 0.18, 'log'], ['tbd', 'Tambourine decay', 0.05, 0.3, 0.12, 'log'], ['lvl', 'Kit level', 0, 1.5, 0.9],
  ];
  // kick, snare, closed hat, open hat, clap, tom, rim, perc (cowbell), cymbal, shaker, conga, tambourine
  const DRUMS = ['k', 's', 'ch', 'oh', 'cl', 't', 'r', 'p', 'cy', 'sh', 'cg', 'tb'];
  const DRUM_NAMES = ['Kick', 'Snare', 'Closed hat', 'Open hat', 'Clap', 'Tom', 'Rim', 'Perc', 'Cymbal', 'Shaker', 'Conga', 'Tambourine'];
  const DRUM_NOTES = [36, 38, 42, 46, 39, 45, 37, 56, 49, 70, 63, 54];   // General MIDI, sent on channel 10
  // 16 steps per bar: x = accent, o = soft, . = rest
  const PATTERNS = {
    four:      {name:'Four on the floor', k:'x...x...x...x...', cl:'....x.......x...', ch:'..o...o...o...o.'},
    house:     {name:'House', k:'x...x...x...x...', cl:'....x.......x...', ch:'o...o...o...o...', oh:'..x...x...x...x.', sh:'.o.o.o.o.o.o.o.o'},
    techno:    {name:'Techno', k:'x...x...x...x...', ch:'xoxoxoxoxoxoxoxo', cl:'....x.......x...', r:'...o..o....o..o.'},
    disco:     {name:'Disco', k:'x...x...x...x...', s:'....x.......x...', oh:'..x...x...x...x.', tb:'xoxoxoxoxoxoxoxo'},
    garage:    {name:'2-step garage', k:'x......x..x.....', s:'....x.......x...', ch:'..x...x...x...x.', sh:'xoxoxoxoxoxoxoxo'},
    breakbeat: {name:'Breakbeat', k:'x.........xx....', s:'....x..o.o..x..o', ch:'x.x.x.x.x.x.x.x.'},
    funk:      {name:'Funk', k:'x.x.......x..x..', s:'....x..o.o..x..o', ch:'xoxoxoxoxoxoxoxo', oh:'.......x........'},
    electro:   {name:'Electro', k:'x.....x...x.....', s:'....x.......x...', cl:'....x.......x...', ch:'xoxoxoxoxoxoxoxo', t:'.......o......o.'},
    newjack:   {name:'New jack swing', k:'x..x..x...x..x..', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cl:'....x.......x...'},
    halftime:  {name:'Half-time', k:'x.........x.....', s:'........x.......', ch:'x.o.x.o.x.o.x.o.'},
    dnb:       {name:'Drum & bass', k:'x.........x.....', s:'....x..o....x...', ch:'xoxoxoxoxoxoxoxo'},
    jungle:    {name:'Jungle', k:'x.........x.....', s:'....x..x.x..x..o', ch:'x.x.x.x.x.x.x.x.', r:'..o.......o...o.'},
    boombap:   {name:'Boom bap', k:'x.......xx......', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', oh:'.............x..'},
    lofi:      {name:'Lo-fi', k:'x......x..x.....', s:'....x.......x...', ch:'x.xox.xox.xox.xo', sh:'..o...o...o...o.'},
    trap:      {name:'Trap', k:'x......x..x.....', s:'........x.......', ch:'xxxxxoxoxxxxxoxo', r:'.......o......o.'},
    footwork:  {name:'Footwork', k:'x..x..x...x..x..', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cl:'...x......x.....'},
    reggaeton: {name:'Reggaeton', k:'x...x...x...x...', s:'...x..x....x..x.', ch:'x.x.x.x.x.x.x.x.'},
    dub:       {name:'Dub one drop', k:'........x.......', r:'........x.......', ch:'..x...x...x...x.', oh:'..............x.'},
    afrobeat:  {name:'Afrobeat', k:'x.....x...x.....', s:'....x.......x..o', ch:'x.xxx.xxx.xxx.xx', sh:'.x.x.x.x.x.x.x.x', cg:'..x..x....x..x..'},
    latin:     {name:'Latin', k:'x...x...x...x...', cg:'x..x..x...x.x...', tb:'..x...x...x...x.', p:'x.x..x.x..x.x..x'},
    bossa:     {name:'Bossa nova', k:'x..x....x..x....', r:'x..x..x...x..x..', sh:'xoxoxoxoxoxoxoxo'},
    samba:     {name:'Samba', k:'x..xx..xx..xx..x', tb:'xoxxxoxxxoxxxoxx', cg:'..x...x...x...x.', sh:'xoxoxoxoxoxoxoxo'},
    shuffle:   {name:'Shuffle', k:'x.....x.x.......', s:'....x.......x...', ch:'x..x..x..x..x..x'},
    motorik:   {name:'Motorik', k:'x...x.x.x...x.x.', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.'},
    industrial:{name:'Industrial', k:'x...x...x...x...', s:'....x.......x...', t:'..x...x...x.x.x.', cl:'....x.......x...', cy:'x...............'},
    march:     {name:'March', k:'x.......x.......', s:'x.oox.oox.oox.oo', cy:'x...............'},
    tribal:    {name:'Tribal', k:'x.......x.......', t:'x..x..x...x.x...', p:'..x...x..x...x..', r:'....o.......o...', cg:'.x...x...x...x..'},
    toms:      {name:'Tom rolls', k:'x.......x.......', t:'x.o.x.o.....x.xo'},
    perc:      {name:'Percussion', p:'x..o..x..o..x.o.', r:'..x...x...x...x.', cl:'.......x......x.', sh:'xoxoxoxoxoxoxoxo'},
    sparse:    {name:'Sparse', k:'x...............', p:'.......o........', oh:'........x.......'},
  };
  // which 16th steps a lane plays on (climbing, descending, cruising and low & close lanes)
  const RHYTHMS = [
    ['Quarters', 'x...x...x...x...'], ['Off-beats', '..x...x...x...x.'], ['Dotted 8ths', 'x..x..x..x..x...'], ['8ths', 'x.x.x.x.x.x.x.x.'],
    ['Tresillo', 'x..x..x...x..x..'], ['Halves', 'x.......x.......'], ['Once a bar', 'x...............'], ['Gallop', 'x.x...x.x...x.x.'],
    ['Upbeats', '...x...x...x...x'], ['Syncopated', 'x..x....x..x....'], ['Bursts', 'xxx.xxx.xxx.xxx.'], ['Dotted quarters', 'x.....x.....x...'],
  ];
  const SCALES = ['minor', 'major', 'dorian', 'lydian', 'phrygian', 'pentatonic minor', 'pentatonic major', 'harmonic minor', 'whole tone'];
  const LANE_IDS = ['arp', 'rise', 'fall', 'drone', 'grid', 'hit'];
  // engine, MIDI channel, volume, reverb, delay, pan, gated, echo, gate (note length), rhythm
  const DEF_LANES = {arp:['pluck', 1, 0.8, 0.25, 0.35, 0.3, 0, 0.4, 0.6, 0], rise:['glass', 2, 0.7, 0.35, 0.25, -0.35, 0, 0.3, 0.45, 0],
    fall:['bell', 3, 0.65, 0.45, 0.3, 0.45, 0, 0.3, 0.5, 1], drone:['pad', 4, 0.75, 0.6, 0.1, -0.2, 0, 0.1, 0.5, 6],
    grid:['bass', 5, 0.75, 0.1, 0.15, 0, 0, 0.1, 0.6, 0], hit:['perc', 10, 0.7, 0.15, 0.1, 0.1, 0.4, 0.3, 0.5, 0]};
  // what each lane may sound like: the generator and auto-randomize pick from these, never the same sound twice where it can help it
  const ROLE_ENGINES = {arp:['obxa', 'pluck', 'glass', 'bell'], rise:['glass', 'lead', 'obxa', 'bell'], fall:['bell', 'obxa', 'lead', 'glass'],
    drone:['pad', 'obxa'], grid:['bass', 'obxa'], hit:['perc', 'bell', 'obxa', 'pluck']};
  const ROLE_RHYTHMS = {rise:[0, 2, 3, 4, 7, 9, 11], fall:[1, 2, 4, 8, 9, 11], drone:[5, 6, 9, 11], hit:[0, 1, 3, 4, 7, 10]};

  // small seeded generator so a seed always gives the same preset
  function rng(seed){ let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
  const R = r => ({u:(a, b) => a + r() * (b - a), log:(a, b) => a * Math.pow(b / a, r()), i:(a, b) => a + Math.floor(r() * (b - a + 1)), pick:a => a[Math.floor(r() * a.length)], p:x => r() < x});
  const round = (x, d = 3) => +x.toFixed(d);
  const defaults = spec => Object.fromEntries(spec.map(p => [p[0], p[4]]));

  // OB-Xa patch shaped for what the lane does; only pads hold on, everything else is short and plucky or medium
  function randomPatch(role, r = Math.random){
    const g = R(r), p = defaults(PATCH);
    p.w1 = g.p(0.7) ? 0 : 1; p.w2 = g.p(0.5) ? 0 : 1; p.pw = round(g.u(0.12, 0.5)); p.mix2 = round(g.u(0.4, 1)); p.det = round(g.u(2, 16), 1);
    p.int2 = g.p(0.6) ? 0 : g.pick([1, 2, 3, 4]); p.poles = g.p(0.75) ? 1 : 0; p.res = round(g.u(0.05, 0.65));
    p.lfo = round(g.log(0.2, 7)); p.vib = g.p(0.3) ? round(g.u(2, 12), 1) : 0; p.pwm = (p.w1 || p.w2) && g.p(0.5) ? round(g.u(0.05, 0.35)) : 0;
    const pluck = () => { p.cut = round(g.log(250, 3000), 0); p.env = round(g.u(2, 5), 2); p.fa = 0.002; p.fd = round(g.log(0.05, 0.35)); p.fs = round(g.u(0, 0.2)); p.fr = round(g.log(0.05, 0.25));
      p.aa = 0.002; p.ad = round(g.log(0.1, 0.5)); p.as = round(g.u(0, 0.25)); p.ar = round(g.log(0.05, 0.25)); };
    const pad = () => { p.cut = round(g.log(400, 3000), 0); p.env = round(g.u(0.5, 2.5), 2); p.fa = round(g.log(0.1, 1)); p.fd = round(g.log(0.4, 2)); p.fs = round(g.u(0.3, 0.8)); p.fr = round(g.log(0.4, 1.5));
      p.aa = round(g.log(0.05, 0.8)); p.ad = round(g.log(0.4, 1.5)); p.as = round(g.u(0.5, 0.9)); p.ar = round(g.log(0.3, 1.5)); p.det = round(g.u(6, 20), 1); p.vib = round(g.u(0, 6), 1); };
    const bass = () => { p.cut = round(g.log(90, 700), 0); p.env = round(g.u(2, 5.5), 2); p.res = round(g.u(0.2, 0.75)); p.poles = 1; p.fa = 0.002; p.fd = round(g.log(0.06, 0.3)); p.fs = round(g.u(0, 0.3)); p.fr = 0.06;
      p.aa = 0.002; p.ad = round(g.log(0.12, 0.45)); p.as = round(g.u(0.2, 0.7)); p.ar = 0.05; p.int2 = g.pick([0, 2, 3]); p.vib = 0; };
    const lead = () => { p.cut = round(g.log(600, 5000), 0); p.env = round(g.u(1, 3.5), 2); p.fa = round(g.log(0.003, 0.06)); p.fd = round(g.log(0.1, 0.6)); p.fs = round(g.u(0.2, 0.6)); p.fr = round(g.log(0.05, 0.35));
      p.aa = round(g.log(0.003, 0.04)); p.ad = round(g.log(0.15, 0.7)); p.as = round(g.u(0.3, 0.7)); p.ar = round(g.log(0.06, 0.35)); p.vib = round(g.u(3, 15), 1); };
    ({arp:pluck, rise:lead, fall:lead, drone:pad, grid:bass, hit:pluck}[role] || lead)();
    return p;
  }
  // nudge a patch part of the way toward a fresh random one (auto-randomize)
  function mutatePatch(p, role, amt = 0.35, r = Math.random){
    const t = randomPatch(role, r), out = {};
    for (const [id, , lo, hi, , curve] of PATCH){
      if (curve === 'int') out[id] = r() < amt * 0.5 ? t[id] : p[id];
      else if (curve === 'log') out[id] = round(Math.exp(Math.log(Math.max(lo, p[id])) * (1 - amt) + Math.log(t[id]) * amt), 4);
      else out[id] = round(p[id] * (1 - amt) + t[id] * amt, 4);
    }
    return out;
  }
  function randomKit(r = Math.random){
    const g = R(r), k = {};
    for (const [id, , lo, hi, , curve] of KIT) k[id] = round(curve === 'log' ? g.log(lo, hi) : g.u(lo, hi));
    k.kp = Math.max(k.kp, k.kf * 2); k.lvl = round(g.u(0.75, 1.05));
    return k;
  }
  const GROOVES = Object.keys(PATTERNS).filter(k => !['toms', 'perc', 'sparse'].includes(k));
  // a whole setup: key, tempo, a distinct sound per lane, OB-Xa patches, rhythms, note lengths, drums, mixer and effects
  function generatePreset(r = Math.random, name = 'Generated'){
    const g = R(r), pre = {name, key:g.i(0, 11), scale:g.pick(['minor', 'minor', 'dorian', 'pentatonic minor', 'major', 'lydian', 'phrygian', 'harmonic minor', 'pentatonic major']),
      bpm:g.i(78, 132), delay:{mode:g.p(0.55) ? 1 : 0, mix:round(g.u(0.3, 0.65)), fb:round(g.u(0.25, 0.55)), tone:round(g.log(1800, 6000), 0)}, kit:randomKit(r), lanes:{},
      fx:{gtime:round(g.u(0.18, 0.45)), gmix:round(g.u(0.5, 0.9)), erate:round(g.u(0.15, 0.5)), emix:round(g.u(0.4, 0.8))},
      mainBeat:g.p(0.75) ? g.pick(GROOVES) : 'none', autoBars:g.pick([4, 8, 8, 16]),
      drums:{vol:round(g.u(0.75, 1)), pan:0, rev:round(g.u(0.05, 0.2)), dly:round(g.u(0, 0.1)), gtd:round(g.u(0.25, 0.7)), echo:round(g.u(0, 0.25))}};
    const used = new Set(), pans = [-0.6, -0.3, 0, 0.3, 0.6];
    for (const id of LANE_IDS){
      const [, ch, vol, rev, dly, pan, gtd, echo, gate, rhy] = DEF_LANES[id];
      // prefer a sound no other lane has; the OB-Xa can repeat because each lane gets its own patch
      const opts = ROLE_ENGINES[id].filter(e => e === 'obxa' || !used.has(e)), engine = g.pick(opts.length ? opts : ROLE_ENGINES[id]); used.add(engine);
      pre.lanes[id] = {on:true, engine, ch, vol:round(vol * g.u(0.8, 1.15)), rev:round(Math.min(1, rev * g.u(0.6, 1.6))), dly:round(Math.min(1, dly * g.u(0.5, 1.8))),
        pan:id === 'grid' ? 0 : round(g.pick(pans) + g.u(-0.1, 0.1), 2), mute:false, solo:false, gtd:id === 'hit' ? round(g.u(0.2, 0.6)) : g.p(0.2) ? round(g.u(0.05, 0.3)) : 0,
        echo:round(Math.min(1, echo * g.u(0.5, 2))), gate:round(Math.min(1, gate * g.u(0.6, 1.4))), rhythm:ROLE_RHYTHMS[id] ? g.pick(ROLE_RHYTHMS[id]) : rhy, auto:true,
        drum:'none', dv:round(g.u(0.6, 0.95)), patch:randomPatch(id, r)};
    }
    // one lane may add a second groove or fills on top of the main beat
    if (g.p(0.6)) pre.lanes[g.pick(['grid', 'drone', 'arp', 'hit', 'rise'])].drum = g.pick(['perc', 'toms', 'sparse', 'tribal', 'latin', 'afrobeat', 'dub']);
    if (pre.mainBeat === 'none') pre.lanes[g.pick(['grid', 'drone'])].drum = g.pick(GROOVES);
    return pre;
  }
  function defaultPreset(){
    const pre = {name:'Init', key:9, scale:'minor', bpm:96, delay:{mode:0, mix:0.5, fb:0.38, tone:2800}, kit:defaults(KIT), lanes:{},
      fx:{gtime:0.3, gmix:0.7, erate:0.25, emix:0.6}, mainBeat:'none', autoBars:8, drums:{vol:0.9, pan:0, rev:0.12, dly:0.05, gtd:0.4, echo:0}};
    for (const id of LANE_IDS){ const [engine, ch, vol, rev, dly, pan, gtd, echo, gate, rhythm] = DEF_LANES[id];
      pre.lanes[id] = {on:true, engine, ch, vol, rev, dly, pan, mute:false, solo:false, gtd, echo, gate, rhythm, auto:true, drum:'none', dv:0.8, patch:randomPatch(id, rng(7 + id.length))}; }
    return pre;
  }
  const api = {PATCH, INTERVALS, KIT, DRUMS, DRUM_NAMES, DRUM_NOTES, PATTERNS, RHYTHMS, SCALES, LANE_IDS, DEF_LANES, ROLE_ENGINES, ROLE_RHYTHMS,
    rng, randomPatch, mutatePatch, randomKit, generatePreset, defaultPreset, defaults};
  if (typeof module !== 'undefined') module.exports = api; else root.SkyPresets = api;
})(typeof window !== 'undefined' ? window : globalThis);
