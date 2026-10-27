// Copy the web game into ./game for packaging (everything except build tooling).
const fs = require('fs'), path = require('path');
const src = path.join(__dirname, '..'), dst = path.join(__dirname, 'game');
const SKIP = new Set(['desktop', 'tools', 'sw.js']);
fs.rmSync(dst, { recursive: true, force: true });
fs.mkdirSync(dst);
for (const f of fs.readdirSync(src)) {
  if (SKIP.has(f) || f.endsWith('.md')) continue;
  fs.cpSync(path.join(src, f), path.join(dst, f), { recursive: true });
}
console.log('staged game into', dst);
