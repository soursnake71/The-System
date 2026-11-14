// Omega DMX kits and factory patterns. One source for the web app and the plugin (node dmx/tools/gen.mjs writes plugins/dmx/Kits.inc).
// A voice is [file, name, fader group, role]. Fader groups follow the hardware: bass, snare, hat, toms, cym, perc1, perc2.
// Roles let one set of factory patterns play on every kit: BD SD RS CP CH HM OH T1-T4 CR RD TB SH CB.
const DMX_KITS = [
  {id:'dmx', name:'DMX', about:'Oberheim DMX recordings', voices:[
    ['kick', 'Kick', 'bass', 'BD'], ['snare', 'Snare', 'snare', 'SD'], ['rimshot', 'Rimshot', 'perc1', 'RS'], ['clap', 'Clap', 'perc2', 'CP'],
    ['hi-hat-tight', 'Hat Tight', 'hat', 'CH'], ['hi-hat-medium', 'Hat Medium', 'hat', 'HM'], ['hi-hat-open', 'Hat Open', 'hat', 'OH'],
    ['tom-1', 'Tom 1', 'toms', 'T1'], ['tom-2', 'Tom 2', 'toms', 'T2'], ['tom-3', 'Tom 3', 'toms', 'T3'], ['tom-4', 'Tom 4', 'toms', 'T4'], ['tom-5', 'Tom 5', 'toms', ''], ['tom-6', 'Tom 6', 'toms', ''],
    ['crash', 'Crash', 'cym', 'CR'], ['ride', 'Ride', 'cym', 'RD'], ['ride-accent', 'Ride Accent', 'cym', ''],
    ['tambourine', 'Tambourine', 'perc1', 'TB'], ['tambourine-accent', 'Tamb Accent', 'perc1', ''], ['shaker', 'Shaker', 'perc2', 'SH'], ['shaker-accent', 'Shaker Accent', 'perc2', '']]},
  {id:'kit-a', name:'KIT A', about:'Your kit with timbales', voices:[
    ['kick-1', 'Kick 1', 'bass', 'BD'], ['kick-2', 'Kick 2', 'bass', ''], ['snare-1', 'Snare 1', 'snare', 'SD'], ['snare-2', 'Snare 2', 'snare', ''], ['snare-3', 'Snare 3', 'snare', ''],
    ['rim', 'Rim', 'perc1', 'RS'], ['clap', 'Clap', 'perc2', 'CP'], ['hat-closed', 'Hat Closed', 'hat', 'CH'], ['hat-open', 'Hat Open', 'hat', 'OH'],
    ['tom-hi', 'Tom Hi', 'toms', 'T1'], ['tom-mid', 'Tom Mid', 'toms', 'T2'], ['tom-lo', 'Tom Lo', 'toms', 'T4'], ['timbale-hi', 'Timbale Hi', 'perc1', 'T3'], ['timbale-lo', 'Timbale Lo', 'perc1', ''],
    ['crash', 'Crash', 'cym', 'CR'], ['ride', 'Ride', 'cym', 'RD'], ['cabasa', 'Cabasa', 'perc2', 'SH'], ['tambourine', 'Tambourine', 'perc2', 'TB']]},
  {id:'kit-b', name:'KIT B', about:'Your first set (numbered sounds named by ear)', voices:[
    ['kick-1', 'Kick 1', 'bass', 'BD'], ['kick-2', 'Kick 2', 'bass', ''], ['kick-3', 'Kick 3', 'bass', ''], ['snare', 'Snare', 'snare', 'SD'], ['rim', 'Rim', 'perc1', 'RS'],
    ['clap', 'Clap', 'perc2', 'CP'], ['clap-2', 'Clap 2', 'perc2', ''],
    ['hat-closed', 'Hat Closed', 'hat', 'CH'], ['hat-closed-2', 'Hat Closed 2', 'hat', ''], ['hat-closed-3', 'Hat Closed 3', 'hat', ''], ['hat-tick', 'Hat Tick', 'hat', ''], ['hat-half', 'Hat Half', 'hat', 'HM'], ['hat-open', 'Hat Open', 'hat', 'OH'],
    ['tom-lo', 'Tom', 'toms', 'T1'], ['crash', 'Crash', 'cym', 'CR'], ['cymbal', 'Cymbal', 'cym', ''], ['ride', 'Ride', 'cym', 'RD'], ['ride-bell', 'Ride Bell', 'cym', ''],
    ['cabasa', 'Cabasa', 'perc2', 'TB'], ['shaker', 'Shaker', 'perc2', 'SH']]},
  {id:'kit-c', name:'KIT C', about:'Your second numbered set (named by ear)', voices:[
    ['kick-1', 'Kick 1', 'bass', 'BD'], ['kick-2', 'Kick 2', 'bass', ''], ['kick-3', 'Kick 3', 'bass', ''], ['snare-1', 'Snare 1', 'snare', 'SD'], ['snare-2', 'Snare 2', 'snare', ''],
    ['rim', 'Rim', 'perc1', 'RS'], ['clap', 'Clap', 'perc2', 'CP'], ['hat-closed', 'Hat Closed', 'hat', 'CH'], ['hat-closed-2', 'Hat Closed 2', 'hat', ''], ['hat-half', 'Hat Half', 'hat', 'HM'], ['hat-open', 'Hat Open', 'hat', 'OH'],
    ['tom-hi', 'Tom Hi', 'toms', 'T1'], ['tom-mid', 'Tom Mid', 'toms', 'T2'], ['tom-lo', 'Tom Lo', 'toms', 'T3'], ['tom-floor', 'Tom Floor', 'toms', 'T4'],
    ['ride-bell', 'Ride Bell', 'cym', 'RD'], ['cowbell', 'Cowbell', 'perc1', 'CB'], ['bell', 'Bell', 'perc1', ''], ['shaker', 'Shaker', 'perc2', 'SH'], ['tambourine', 'Tambourine', 'perc2', 'TB']]}];

// the faders, left to right, as on the hardware: seven voice groups, the metronome (down = no click) and the master volume
const DMX_FADERS = [['bass', 'BASS'], ['snare', 'SNARE'], ['hat', 'HI-HAT'], ['toms', 'TOMS'], ['cym', 'CYMBAL'], ['perc1', 'PERC 1'], ['perc2', 'PERC 2'], ['met', 'METRONOME'], ['vol', 'VOLUME']];

// ten factory patterns, 16 steps: x = hit, X = accent. A role a kit lacks falls back (HM to CH, CR to RD, CB to RS, T3 to T2 ...)
const DMX_PATTERNS = [
  ['Boom Bap', {BD:'X......x..X.....', SD:'....X.......X...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Run It', {BD:'X.....X.X.....X.', SD:'....X.......X...', CH:'xxxxxxxxxxxxxxxx', CP:'....X.......X...'}],
  ['Electro Funk', {BD:'X.....X...X..x..', SD:'....X.......X...', HM:'x.xxx.xxx.xxx.xx', OH:'......X.......X.'}],
  ['New Wave', {BD:'X...X...X...X...', SD:'....X.......X...', CH:'xxxxxxxxxxxxxxxx', TB:'....X.......X...'}],
  ['Freestyle', {BD:'X..X..X...X..X..', SD:'....X.......X...', CP:'....X.......X...', CH:'x.x.x.x.x.x.x.x.', OH:'..x...x...x...x.'}],
  ['Slow Jam', {BD:'X.......X.x.....', SD:'....X.......X...', RS:'..x.....x.....x.', CH:'x.x.x.x.x.x.x.x.'}],
  ['Go-Go', {BD:'X..x..X...X..x..', SD:'...x..X....x..X.', HM:'x.xxx.xxx.xxx.xx', T1:'..x.......x.....', CB:'X.x.X.x.X.x.X.x.'}],
  ['Fills', {BD:'X.......X.......', SD:'....X.......X...', T1:'............xx..', T2:'..............xx', T4:'..............x.', CR:'X...............'}],
  ['Ride Groove', {BD:'X.....x...X.....', SD:'....X.......X...', RD:'x.x.x.x.x.x.x.x.', SH:'xxxxxxxxxxxxxxxx'}],
  ['Rimshot Dub', {BD:'X.........X.....', RS:'....X.......X...', CH:'..x...x...x...x.'}]];
const DMX_FALLBACK = {HM:'CH', OH:'CH', CR:'RD', RD:'CR', CB:'RS', TB:'SH', SH:'TB', CP:'SD', RS:'SD', T2:'T1', T3:'T2', T4:'T3'};

// a pattern for a kit: rows[voice] = 16 steps of 0 (off), 1 (hit) or 2 (accent)
function dmxFactory(kit, pi){
  const [name, P] = DMX_PATTERNS[pi], rows = kit.voices.map(() => new Array(16).fill(0));
  const voiceFor = r => { for (let k = r, n = 0; k && n < 6; k = DMX_FALLBACK[k], n++){ const i = kit.voices.findIndex(v => v[3] === k); if (i >= 0) return i; } return -1; };
  for (const r in P){ const i = voiceFor(r); if (i < 0) continue; [...P[r]].forEach((c, s) => { if (c !== '.') rows[i][s] = Math.max(rows[i][s], c === 'X' ? 2 : 1); }); }
  return {name, rows};
}
// MIDI notes: General MIDI drum notes by role (a role the kit lacks falls back as above), and every voice on its own note from C3 (60) up
const DMX_GM = {BD:[36, 35], SD:[38, 40], RS:[37], CP:[39], CH:[42], HM:[44], OH:[46], T1:[50, 48], T2:[47], T3:[45], T4:[43, 41], CR:[49, 57], RD:[51, 59], TB:[54], SH:[70, 82], CB:[56]};
function dmxNoteVoice(kit, n){
  if (n >= 60 && n - 60 < kit.voices.length) return n - 60;
  for (const r in DMX_GM) if (DMX_GM[r].includes(n)) for (let q = r, c = 0; q && c < 6; q = DMX_FALLBACK[q], c++){ const i = kit.voices.findIndex(v => v[3] === q); if (i >= 0) return i; }
  return -1;
}
// an open hat rings until a closed or medium hat cuts it
const dmxIsOpen = v => v[3] === 'OH' || /open/.test(v[0]);
if (typeof module !== 'undefined') module.exports = {DMX_KITS, DMX_FADERS, DMX_PATTERNS, DMX_FALLBACK, DMX_GM, dmxFactory, dmxNoteVoice, dmxIsOpen};
