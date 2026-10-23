// Drive the original game in tools/emu.js and save screenshots.
// usage: node play.js <Dragonstomper.bin> "W60 RESET5 W30 U180 S F5 W40 S" [out-prefix]
// tokens: U/D/L/R/F combos + frames (e.g. UR30, F5), W<n> wait, S screenshot, RESET<n>/SEL<n> console switches, Z dump zero page
const { Emu } = require('./emu.js');
const e = new Emu(process.argv[2]);
const out = process.argv[4] || 'shot';
let shot = 0;
e.loadHook = n => console.log('** load', n, 'at frame', e.frame);
const dirs = { U: 0x10, D: 0x20, L: 0x40, R: 0x80 };
for (const tok of process.argv[3].split(/\s+/).filter(Boolean)) {
  let m;
  if (tok === 'S') { e.savePNG(out + (shot++) + '.png'); continue; }
  if (tok === 'Z') { console.log(Array.from(e.zp).map((v, i) => (i % 16 ? '' : '\n' + (0x80 + i).toString(16) + ':') + ' ' + v.toString(16).padStart(2, '0')).join('')); continue; }
  if ((m = tok.match(/^(RESET|SEL)(\d+)$/))) { e.swchb &= m[1] === 'RESET' ? 0xfe : 0xfd; for (let i = 0; i < +m[2]; i++) e.runFrame(); e.swchb |= 3; continue; }
  if ((m = tok.match(/^([UDLRF]*)W?(\d+)$/))) {
    let sw = 0xff; for (const c of m[1]) if (dirs[c]) sw &= ~dirs[c];
    e.swcha = sw; e.fire = m[1].includes('F');
    for (let i = 0; i < +m[2]; i++) e.runFrame();
    e.swcha = 0xff; e.fire = false; continue;
  }
  console.log('bad token', tok);
}
