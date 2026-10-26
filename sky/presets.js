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
    electrofunk:{name:'Electro funk', k:'x.....x...x.x...', s:'....x.......x...', cl:'....x.......x...', ch:'x.x.x.x.x.x.x.xx', p:'..o.......o.....'},
    miami:     {name:'Miami bass', k:'x......x..x.....', s:'....x.......x...', ch:'xoxoxoxoxoxoxoxo', cl:'....x..x....x...'},
    hiphop90s: {name:'90s hip hop', k:'x.....x...x..x..', s:'....x.......x...', ch:'x.x.x.x.x.xox.x.', oh:'...............o'},
    drill:     {name:'Drill', k:'x.........x..x..', s:'........x.......', ch:'x..x..x.x..x..x.', r:'...o......o....o'},
    dancehall: {name:'Dancehall', k:'x..x....x..x....', s:'...x..x....x..x.', ch:'x.x.x.x.x.x.x.x.', p:'..x...x...x...x.'},
    soca:      {name:'Soca', k:'x...x...x...x...', s:'..x...x...x...x.', ch:'xoxoxoxoxoxoxoxo', tb:'x.x.x.x.x.x.x.x.'},
    cumbia:    {name:'Cumbia', k:'x.......x.......', cg:'..x..x..x..x..x.', sh:'xoxoxoxoxoxoxoxo', r:'....x.......x...'},
    baile:     {name:'Baile funk', k:'x..x..x...x..x..', s:'...x..x....x..x.', cl:'...x......x.....', ch:'x.x.x.x.x.x.x.x.'},
    amapiano:  {name:'Amapiano', k:'x...x...x...x...', r:'..x...x.x..x..x.', sh:'xoxoxoxoxoxoxoxo', p:'.......x.......x', cg:'....x.......x...'},
    gqom:      {name:'Gqom', k:'x..x..x...x..x..', t:'..x....x..x....x', cl:'....x.......x...', sh:'xoxoxoxoxoxoxoxo'},
    dubstep:   {name:'Dubstep', k:'x.........x.....', s:'........x.......', ch:'x.x.x.x.x.x.x.x.', oh:'......x.......x.'},
    liquid:    {name:'Liquid', k:'x.........x.....', s:'....x.......x...', ch:'xoxoxoxoxoxoxoxo', sh:'..o...o...o...o.', r:'.......o.......o'},
    rollers:   {name:'Rollers', k:'x.........x.x...', s:'....x..o....x...', ch:'xoxxxoxxxoxxxoxx'},
    trance:    {name:'Trance', k:'x...x...x...x...', oh:'..x...x...x...x.', cl:'....x.......x...', ch:'xoxoxoxoxoxoxoxo'},
    hardstyle: {name:'Hard dance', k:'x...x...x...x...', cl:'....x.......x...', oh:'..x...x...x...x.', cy:'x...............'},
    techhouse: {name:'Tech house', k:'x...x...x...x...', cl:'....x.......x...', oh:'..x...x...x...x.', sh:'xoxoxoxoxoxoxoxo', r:'.......o......o.'},
    minimal:   {name:'Minimal', k:'x...x...x...x...', r:'...o.....o....o.', ch:'..o...o...o...o.', p:'.o..........o...'},
    afrohouse: {name:'Afro house', k:'x...x...x...x...', cg:'..x..x...x..x...', sh:'xoxoxoxoxoxoxoxo', r:'...x......x.....', cl:'....x.......x...'},
    ebm:       {name:'EBM', k:'x.x.x.x.x.x.x.x.', s:'....x.......x...', ch:'..x...x...x...x.'},
    synthpop:  {name:'Synth pop', k:'x.......x.......', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cl:'....x.......x...', t:'..............xx'},
    newwave:   {name:'New wave', k:'x...x...x...x...', s:'....x.......x...', ch:'xoxoxoxoxoxoxoxo'},
    postpunk:  {name:'Post-punk', k:'x.x...x.x.x.....', t:'x..x..x...x..x..', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.'},
    motown:    {name:'Motown', k:'x.......x.......', s:'x...x...x...x...', tb:'....x.......x...', ch:'x.x.x.x.x.x.x.x.'},
    rocknroll: {name:'Rock and roll', k:'x.....x.x.......', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cy:'x...............'},
    garagerock:{name:'Garage rock', k:'x.x.....x.x.....', s:'....x.......x...', oh:'x.x.x.x.x.x.x.x.'},
    swing:     {name:'Swing', k:'o.......o.......', cy:'x...x..xx...x..x', ch:'....x.......x...', s:'...........o....'},
    steppers:  {name:'Steppers', k:'x...x...x...x...', r:'....x.......x...', ch:'..x...x...x...x.', sh:'xoxoxoxoxoxoxoxo'},
    breakstep: {name:'Breakstep', k:'x.........x.....', s:'....x.......x...', ch:'..x...x.x.x...x.', t:'.............x.x'},
    idm:       {name:'Glitch', k:'x..x...x.x....x.', s:'...x.x....x..x..', ch:'x.xxx..x.xx.x.xx', r:'.x.....x...x....'},
    ballad:    {name:'Ballad', k:'x.......x.x.....', s:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cy:'x...............'},
  };
  // the OB-Xa patch library: [name, category, settings that differ from the defaults]
  const P_ = (name, cat, o) => [name, cat, o];
  const LIBRARY = [
    P_('Sub Bass', 'Bass', {w1:0, w2:1, mix2:0.4, int2:3, cut:220, res:0.2, env:1.5, fd:0.2, fs:0.2, ad:0.4, as:0.8, ar:0.08}),
    P_('Acid Saw', 'Bass', {w1:0, w2:0, mix2:0.3, cut:180, res:0.75, env:4.5, fd:0.18, fs:0, ad:0.25, as:0.5, ar:0.05}),
    P_('Fat Unison Bass', 'Bass', {w1:0, w2:0, det:14, cut:350, res:0.3, env:3, fd:0.25, fs:0.15, ad:0.5, as:0.7, ar:0.08}),
    P_('Pulse Bass', 'Bass', {w1:1, w2:1, pw:0.25, int2:3, cut:400, res:0.25, env:2.5, fd:0.15, as:0.6, ar:0.06}),
    P_('Rubber Bass', 'Bass', {w1:1, w2:0, pw:0.4, cut:150, res:0.6, env:5, fd:0.12, fs:0.05, ad:0.3, as:0.4, ar:0.05}),
    P_('Octave Bass', 'Bass', {w1:0, w2:0, int2:2, mix2:0.6, cut:500, res:0.2, env:2, fd:0.2, as:0.7, ar:0.07}),
    P_('Funk Bass', 'Bass', {w1:1, w2:0, pw:0.3, cut:260, res:0.5, env:4, fd:0.09, fs:0.1, ad:0.2, as:0.3, ar:0.04}),
    P_('Deep Pluck Bass', 'Bass', {w1:0, w2:1, cut:120, res:0.4, env:5.5, fd:0.1, fs:0, ad:0.3, as:0, ar:0.1}),
    P_('Fifth Lead', 'Lead', {w1:0, w2:0, int2:1, det:4, cut:3000, res:0.3, env:2, fd:0.4, fs:0.5, aa:0.005, ad:0.5, as:0.7, ar:0.2, vib:8, lfo:5.5}),
    P_('Square Lead', 'Lead', {w1:1, w2:1, pw:0.5, det:6, cut:2500, res:0.2, env:1.5, fd:0.3, fs:0.6, ad:0.4, as:0.8, ar:0.15, vib:10}),
    P_('Screaming Lead', 'Lead', {w1:0, w2:0, det:10, cut:1800, res:0.65, env:3, fd:0.5, fs:0.5, ad:0.6, as:0.8, ar:0.2, vib:12}),
    P_('Flute Lead', 'Lead', {w1:1, w2:1, pw:0.15, mix2:0.4, cut:1200, res:0.1, env:1, aa:0.05, ad:0.4, as:0.7, ar:0.2, vib:14, lfo:5}),
    P_('Whistle', 'Lead', {w1:1, w2:1, pw:0.08, mix2:0.2, cut:4000, res:0.5, env:0.5, as:0.9, ar:0.15, vib:18, lfo:6}),
    P_('Mono Lead', 'Lead', {w1:0, w2:1, pw:0.35, det:8, cut:1500, res:0.45, env:3.5, fd:0.25, fs:0.3, as:0.7, ar:0.12}),
    P_('Bright Saw Lead', 'Lead', {w1:0, w2:0, det:12, int2:2, cut:5000, res:0.15, env:1, as:0.8, ar:0.2, vib:6}),
    P_('Big Brass', 'Brass', {w1:0, w2:0, det:9, cut:900, res:0.15, env:2.8, fa:0.06, fd:0.5, fs:0.55, aa:0.02, ad:0.6, as:0.85, ar:0.25}),
    P_('Stab Brass', 'Brass', {w1:0, w2:0, det:12, cut:1000, res:0.2, env:3.2, fa:0.01, fd:0.25, fs:0.2, aa:0.005, ad:0.3, as:0.3, ar:0.12}),
    P_('Soft Horns', 'Brass', {w1:0, w2:1, pw:0.4, det:7, cut:700, res:0.1, env:2, fa:0.15, fd:0.7, fs:0.5, aa:0.08, ad:0.8, as:0.8, ar:0.35}),
    P_('Sweep Brass', 'Brass', {w1:0, w2:0, det:10, cut:400, res:0.3, env:4, fa:0.4, fd:1, fs:0.6, aa:0.1, ad:1, as:0.8, ar:0.4}),
    P_('Fanfare', 'Brass', {w1:0, w2:0, int2:1, det:8, cut:1100, res:0.2, env:2.6, fa:0.03, fd:0.4, fs:0.5, aa:0.02, ad:0.5, as:0.8, ar:0.25}),
    P_('Funk Stab', 'Brass', {w1:1, w2:0, pw:0.3, det:10, cut:1300, res:0.35, env:3, fa:0.005, fd:0.15, fs:0.1, ad:0.2, as:0.1, ar:0.08}),
    P_('Warm Pad', 'Pad', {w1:0, w2:0, det:14, cut:900, res:0.1, env:1, fa:0.8, fd:1.5, fs:0.6, aa:0.8, ad:1, as:0.9, ar:1.2, vib:3}),
    P_('Glassy Pad', 'Pad', {w1:1, w2:1, pw:0.3, det:9, cut:2200, res:0.3, env:0.8, fa:0.5, fd:1.2, fs:0.7, aa:0.6, ad:1, as:0.85, ar:1.2, pwm:0.25, lfo:0.4}),
    P_('Dark Pad', 'Pad', {w1:0, w2:1, pw:0.45, det:16, cut:450, res:0.25, env:1.5, fa:1.2, fd:2, fs:0.5, aa:1.2, ad:1.5, as:0.9, ar:1.5}),
    P_('PWM Pad', 'Pad', {w1:1, w2:1, pw:0.35, det:6, cut:1500, res:0.15, env:0.6, aa:0.5, as:0.9, ar:1, pwm:0.35, lfo:0.6}),
    P_('Choir Pad', 'Pad', {w1:1, w2:0, pw:0.2, det:12, int2:2, cut:1200, res:0.45, env:0.5, aa:0.7, ad:1, as:0.85, ar:1.3, vib:4, lfo:4.5}),
    P_('Sweep Pad', 'Pad', {w1:0, w2:0, det:18, cut:300, res:0.55, env:3, fa:1.5, fd:2.5, fs:0.4, aa:0.6, ad:1.5, as:0.9, ar:1.5}),
    P_('Fifths Pad', 'Pad', {w1:0, w2:0, int2:1, det:10, cut:1000, res:0.2, env:1, aa:0.9, as:0.9, ar:1.4}),
    P_('Space Pad', 'Pad', {w1:1, w2:0, pw:0.25, det:22, int2:3, cut:1800, res:0.35, env:1.2, fa:1, fd:2, fs:0.6, aa:1, as:0.85, ar:1.5, vib:6, lfo:0.3}),
    P_('OB Strings', 'Strings', {w1:0, w2:0, det:16, cut:2500, res:0.05, env:0.4, aa:0.3, ad:0.8, as:0.9, ar:0.8, vib:5, lfo:5}),
    P_('Slow Strings', 'Strings', {w1:0, w2:0, det:20, cut:1600, res:0.1, env:0.5, aa:1.2, ad:1, as:0.9, ar:1.2, vib:4}),
    P_('Pizzicato', 'Strings', {w1:0, w2:0, det:8, cut:1800, res:0.1, env:2, fd:0.12, fs:0, ad:0.2, as:0, ar:0.15}),
    P_('String Machine', 'Strings', {w1:1, w2:0, pw:0.45, det:18, int2:2, cut:3000, res:0.05, env:0.3, aa:0.25, as:0.95, ar:0.9, pwm:0.2, lfo:0.7}),
    P_('Poly Keys', 'Keys', {w1:0, w2:1, pw:0.4, det:6, cut:1400, res:0.2, env:2, fd:0.5, fs:0.3, ad:0.7, as:0.4, ar:0.3}),
    P_('Clav', 'Keys', {w1:1, w2:1, pw:0.15, cut:2200, res:0.4, env:3, fd:0.1, fs:0.1, ad:0.2, as:0.1, ar:0.06}),
    P_('Harp', 'Keys', {w1:1, w2:0, pw:0.45, cut:1800, res:0.2, env:2.5, fd:0.2, fs:0, ad:0.5, as:0, ar:0.4}),
    P_('Wooden Keys', 'Keys', {w1:1, w2:1, pw:0.5, int2:2, mix2:0.3, cut:900, res:0.5, env:3, fd:0.08, fs:0, ad:0.2, as:0, ar:0.15}),
    P_('Electric Piano', 'Keys', {w1:1, w2:1, pw:0.5, int2:2, mix2:0.25, cut:1600, res:0.1, env:1.5, fd:0.6, fs:0.2, ad:1, as:0.2, ar:0.4, vib:3, lfo:4}),
    P_('Organ', 'Keys', {w1:1, w2:1, pw:0.5, int2:2, mix2:0.6, cut:4000, res:0, env:0, aa:0.003, as:1, ar:0.05}),
    P_('Muted Keys', 'Keys', {w1:0, w2:1, cut:600, res:0.2, env:3, fd:0.07, fs:0, ad:0.15, as:0, ar:0.08}),
    P_('Hollow Keys', 'Keys', {w1:1, w2:1, pw:0.5, int2:4, cut:1800, res:0.2, env:1.2, fd:0.4, fs:0.3, ad:0.8, as:0.3, ar:0.4}),
    P_('Arp Blip', 'Arp', {w1:1, w2:1, pw:0.3, int2:2, cut:2500, res:0.3, env:2, fd:0.06, fs:0, ad:0.1, as:0, ar:0.05}),
    P_('Arp Saw', 'Arp', {w1:0, w2:0, det:7, cut:1200, res:0.5, env:3.5, fd:0.09, fs:0.05, ad:0.15, as:0.1, ar:0.06}),
    P_('Arp Fifth', 'Arp', {w1:1, w2:0, pw:0.2, int2:1, cut:3000, res:0.4, env:2, fd:0.15, fs:0.1, ad:0.3, as:0.1, ar:0.2}),
    P_('Arp Resonant', 'Arp', {w1:0, w2:1, cut:500, res:0.8, env:4.5, fd:0.1, fs:0.05, ad:0.18, as:0.1, ar:0.08}),
    P_('Arp Bright', 'Arp', {w1:0, w2:0, det:5, cut:1500, res:0.35, env:4, fd:0.12, fs:0, ad:0.25, as:0.05, ar:0.15}),
    P_('Arp Sparkle', 'Arp', {w1:1, w2:1, pw:0.5, int2:2, cut:5000, res:0.1, env:1, fd:0.3, fs:0, ad:0.6, as:0, ar:0.5}),
    P_('Laser', 'FX', {w1:1, w2:1, pw:0.1, cut:3000, res:0.6, env:5, fd:0.05, fs:0, ad:0.3, as:0, ar:0.1, vib:40, lfo:11}),
    P_('Siren', 'FX', {w1:0, w2:1, cut:2000, res:0.3, env:1, as:0.9, ar:0.3, vib:50, lfo:0.8}),
    P_('Wind Sweep', 'FX', {w1:1, w2:1, pw:0.05, det:30, cut:200, res:0.9, env:6, fa:0.5, fd:1, fs:0.3, aa:0.2, as:0.7, ar:0.6}),
    P_('Detuned Chaos', 'FX', {w1:0, w2:1, det:30, int2:3, cut:800, res:0.5, env:3, fd:0.3, fs:0.4, as:0.6, ar:0.3, pwm:0.4, lfo:3}),
    P_('Vowel', 'FX', {w1:0, w2:1, pw:0.3, cut:900, res:0.85, env:1, fa:0.1, fd:0.6, fs:0.4, as:0.8, ar:0.3, vib:6}),
    P_('Rave Stab', 'FX', {w1:0, w2:0, int2:1, det:14, cut:1400, res:0.4, env:3, fd:0.15, fs:0.1, ad:0.25, as:0.15, ar:0.1}),
    P_('Big Hoover', 'FX', {w1:1, w2:0, pw:0.35, det:25, int2:3, cut:1500, res:0.3, env:2, fd:0.4, fs:0.5, as:0.8, ar:0.3, pwm:0.4, lfo:5, vib:10}),
    P_('Sub Drop', 'FX', {w1:0, w2:1, int2:3, cut:300, res:0.3, env:4, fd:0.5, fs:0, ad:0.6, as:0, ar:0.2}),
    P_('Zap', 'FX', {w1:0, w2:0, int2:2, cut:6000, res:0.7, env:6, fd:0.04, fs:0, ad:0.08, as:0, ar:0.05}),
  ];
  // which lanes each category suits
  const CAT_ROLES = {Bass:['grid'], Lead:['rise', 'fall'], Brass:['rise', 'fall', 'drone'], Pad:['drone'], Strings:['drone', 'fall'], Keys:['arp', 'hit', 'rise'], Arp:['arp', 'hit'], FX:['hit', 'rise', 'fall']};
  const libraryPatch = i => Object.assign({}, Object.fromEntries(PATCH.map(p => [p[0], p[4]])), LIBRARY[i][2]);
  const libraryFor = role => LIBRARY.map((p, i) => i).filter(i => (CAT_ROLES[LIBRARY[i][1]] || []).includes(role));
  // which 16th steps a lane plays on (climbing, descending, cruising and low & close lanes)
  const RHYTHMS = [
    ['Quarters', 'x...x...x...x...'], ['Off-beats', '..x...x...x...x.'], ['Dotted 8ths', 'x..x..x..x..x...'], ['8ths', 'x.x.x.x.x.x.x.x.'],
    ['Tresillo', 'x..x..x...x..x..'], ['Halves', 'x.......x.......'], ['Once a bar', 'x...............'], ['Gallop', 'x.x...x.x...x.x.'],
    ['Upbeats', '...x...x...x...x'], ['Syncopated', 'x..x....x..x....'], ['Bursts', 'xxx.xxx.xxx.xxx.'], ['Dotted quarters', 'x.....x.....x...'],
  ];
  const SCALES = ['minor', 'major', 'dorian', 'lydian', 'phrygian', 'pentatonic minor', 'pentatonic major', 'harmonic minor', 'whole tone'];
  const LANE_IDS = ['arp', 'rise', 'fall', 'drone', 'grid', 'hit'];
  // engine, MIDI channel, volume, reverb, delay, pan, gated, echo, gate (note length), rhythm
  const DEF_LANES = {arp:['obxa', 1, 0.8, 0.25, 0.35, 0.3, 0, 0.4, 0.6, 0], rise:['obxa', 2, 0.7, 0.35, 0.25, -0.35, 0, 0.3, 0.45, 0],
    fall:['obxa', 3, 0.65, 0.45, 0.3, 0.45, 0, 0.3, 0.5, 1], drone:['obxa', 4, 0.75, 0.6, 0.1, -0.2, 0, 0.1, 0.5, 6],
    grid:['obxa', 5, 0.75, 0.1, 0.15, 0, 0, 0.1, 0.6, 0], hit:['obxa', 10, 0.7, 0.15, 0.1, 0.1, 0.4, 0.3, 0.5, 0]};
  // what each lane may sound like: the generator and auto-randomize pick from these, never the same sound twice where it can help it
  // every lane plays an OB-Xa patch shaped for its role (or MIDI only, to drive your own synth)
  const ROLE_ENGINES = {arp:['obxa'], rise:['obxa'], fall:['obxa'], drone:['obxa'], grid:['obxa'], hit:['obxa']};
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
      mainBeat:g.pick(GROOVES), autoBars:g.pick([4, 8, 8, 16]),
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
      const lib = libraryFor(id);
      if (lib.length && g.p(0.7)){ const i = g.pick(lib); pre.lanes[id].patch = mutatePatch(libraryPatch(i), id, 0.12, r); pre.lanes[id].patchName = LIBRARY[i][0]; }
    }
    // one lane may add a second groove or fills on top of the main beat
    if (g.p(0.6)) pre.lanes[g.pick(['grid', 'drone', 'arp', 'hit', 'rise'])].drum = g.pick(['perc', 'toms', 'sparse', 'tribal', 'latin', 'afrobeat', 'dub']);
    if (pre.mainBeat === 'none') pre.lanes[g.pick(['grid', 'drone'])].drum = g.pick(GROOVES);
    return pre;
  }
  function defaultPreset(){
    const pre = {name:'Init', key:9, scale:'minor', bpm:96, delay:{mode:0, mix:0.5, fb:0.38, tone:2800}, kit:defaults(KIT), lanes:{},
      fx:{gtime:0.3, gmix:0.7, erate:0.25, emix:0.6}, mainBeat:'house', autoBars:8, drums:{vol:0.9, pan:0, rev:0.12, dly:0.05, gtd:0.4, echo:0}};
    for (const id of LANE_IDS){ const [engine, ch, vol, rev, dly, pan, gtd, echo, gate, rhythm] = DEF_LANES[id];
      pre.lanes[id] = {on:true, engine, ch, vol, rev, dly, pan, mute:false, solo:false, gtd, echo, gate, rhythm, auto:true, drum:'none', dv:0.8, patch:randomPatch(id, rng(7 + id.length))}; }
    return pre;
  }
  const api = {LIBRARY, CAT_ROLES, libraryPatch, libraryFor, PATCH, INTERVALS, KIT, DRUMS, DRUM_NAMES, DRUM_NOTES, PATTERNS, RHYTHMS, SCALES, LANE_IDS, DEF_LANES, ROLE_ENGINES, ROLE_RHYTHMS,
    rng, randomPatch, mutatePatch, randomKit, generatePreset, defaultPreset, defaults};
  if (typeof module !== 'undefined') module.exports = api; else root.SkyPresets = api;
})(typeof window !== 'undefined' ? window : globalThis);
