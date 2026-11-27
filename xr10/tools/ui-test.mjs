// Opens the app in headless Chromium, powers it on, plays, presses buttons and checks the LCD and panel respond.
// node xr10/tools/ui-test.mjs <shot-dir>
import { createRequire } from 'module';
const { chromium } = createRequire(import.meta.url)('playwright');   // NODE_PATH=$(npm root -g) if installed globally
import http from 'http';
import fs from 'fs';
import path from 'path';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.jpg': 'image/jpeg', '.png': 'image/png', '.webmanifest': 'application/json' };
const srv = http.createServer((q, r) => {
  let f = path.join(root, decodeURIComponent(q.url.split('?')[0]));
  if (f.endsWith('/')) f += 'index.html';
  if (!f.startsWith(root) || !fs.existsSync(f)) { r.writeHead(404); return r.end(); }
  r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(r);
}).listen(0);
const port = srv.address().port, shots = process.argv[2] || '.';
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1000, height: 1400 } });
const errors = [];
page.on('pageerror', e => errors.push(e.message));
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
await page.goto(`http://localhost:${port}/`);
await page.click('#go');
await page.waitForFunction(() => window.S !== undefined || document.querySelector('#inst').children.length === 64, null, { timeout: 15000 });
await page.waitForTimeout(800);
const lcd = () => page.evaluate(() => lcdText.join(' | '));
let fail = 0;
const check = (c, m) => { if (!c) { console.log('FAIL', m); fail++; } else console.log('ok  ', m); };
let t = await lcd(); check(t.includes('ROCK 1'), 'LCD shows pattern 1: ' + t);
check(await page.evaluate(() => info.packed.every(p => p === 0)), 'sounds load from rom.bin');
// keypad 0 7 picks pattern 7
const hit = async (id) => page.evaluate(i => { send({ t: 'btn', b: i, d: 1 }); send({ t: 'btn', b: i, d: 0 }); }, id);
const clickHot = async (x, y) => { const b = await page.locator('#plate').boundingBox(); await page.mouse.click(b.x + b.width * x / 800, b.y + b.height * y / 549); };
await clickHot(427, 328); await clickHot(532, 357); await page.waitForTimeout(200);
t = await lcd(); check(t.includes('DANCE 2'), 'keypad 0 7 selects DANCE 2: ' + t);
await clickHot(707, 503); await page.waitForTimeout(900);   // START/STOP
t = await lcd(); check(t.includes('BAR'), 'playing shows bar: ' + t);
check(await page.evaluate(() => S.playing === 1), 'engine is playing');
await clickHot(619, 405); await page.waitForTimeout(150);   // FILL A
check(await page.evaluate(() => S.part === 3), 'fill A plays');
await page.screenshot({ path: shots + '/xr10-playing.png' });
await clickHot(707, 503); await page.waitForTimeout(200);
check(await page.evaluate(() => S.playing === 0), 'stopped');
// mode: tap the SOUND light, then the knob
await clickHot(497, 231); await page.waitForTimeout(150);
t = await lcd(); check(t.startsWith('SND'), 'sound mode: ' + t);
// panel: instruments and voice sliders
await page.locator('#inst button').nth(24).dispatchEvent('pointerdown');
await page.waitForTimeout(200);
check(await page.evaluate(() => S.sound === 24), 'instrument key selects HAT OP');
await page.evaluate(() => { const i = document.querySelector('#params input[data-p="2"]'); i.value = 7; i.dispatchEvent(new Event('input')); });
await page.waitForTimeout(150);
check(await page.evaluate(() => S.params[2] === 7), 'tune slider sets +7');
// grid: toggle a step on a preset -> copies to 51
await page.click('[data-tab=grid]');
await page.evaluate(() => send({ t: 'set', k: K.MODE, v: 0 }));
await page.locator('#grid .cell').nth(5).dispatchEvent('pointerdown');
await page.waitForTimeout(300);
check(await page.evaluate(() => S.pattern === 50), 'grid edit on a preset copies it to user 51');
await page.screenshot({ path: shots + '/xr10-grid.png', fullPage: true });
// song
await page.click('[data-tab=song]');
await page.click('#songadd'); await page.click('#songadd'); await page.waitForTimeout(200);
check(await page.evaluate(() => S.songLen === 2), 'song has 2 steps');
// MIDI: export the part, import it into the next user pattern
await page.evaluate(() => { window.__mid = null; const d = download; window.download = (b, n) => { window.__mid = { b, n }; }; send({ t: 'exportMidi', song: 0 }); });
await page.waitForFunction(() => window.__mid, null, { timeout: 5000 });
const mid = await page.evaluate(() => ({ n: __mid.n, len: __mid.b.length, head: String.fromCharCode(...__mid.b.slice(0, 4)) }));
check(mid.head === 'MThd' && mid.len > 40, 'MIDI export ' + mid.n + ' ' + mid.len + ' bytes');
await page.evaluate(() => { window.alert = m => { window.__alert = m; }; send({ t: 'set', k: K.PATTERN, v: 70 }); });
await page.waitForTimeout(150);
await page.evaluate(() => send({ t: 'importMidi', bytes: __mid.b.slice().buffer }));
await page.waitForFunction(() => window.__alert, null, { timeout: 5000 });
check(await page.evaluate(() => /Imported 2 bars into pattern 71/.test(__alert)), 'MIDI import: ' + await page.evaluate(() => __alert));
// sample names go to the right sounds
const matched = await page.evaluate(() => matchNames(['KICK10.wav', 'Hat OP.wav', '07 KICK7.wav', 'tom3 h.wav', 'xyz.wav']));
check(JSON.stringify(matched) === JSON.stringify([9, 24, 6, 33, -1]), 'sample names ' + JSON.stringify(matched));
// dice: a random beat lands in a free user pattern and plays; a random sound changes the sound
const before = await page.evaluate(() => S.pattern);
await page.click('#dicebeat'); await page.waitForTimeout(300);
check(await page.evaluate(() => S.pattern >= 50 && /^RAND/.test(S.patName)), 'random beat: ' + await page.evaluate(() => S.pattern + 1 + ' ' + S.patName));
const p0 = await page.evaluate(() => S.params.join(','));
await page.click('#dicesound'); await page.waitForTimeout(300);
check(await page.evaluate(p => S.params.join(',') !== p, p0), 'random sound changes the sound');
await page.click('[data-tab=fx]'); await page.waitForTimeout(100);
check(await page.evaluate(() => !$('tab-fx').hidden && S.keys[K.FXTYPE] <= 4), 'effects tab, an effect on by default');
// save state round trip through IndexedDB
await page.evaluate(() => save('auto')); await page.waitForTimeout(400);
check(await page.evaluate(async () => { const b = await dbGet(); return b && b.byteLength > 1000; }), 'state saved to IndexedDB');
// a phone on its side: the whole panel fits on the screen
await page.setViewportSize({ width: 915, height: 412 });
await page.evaluate(() => window.scrollTo(0, 0));
await page.waitForTimeout(300);
const fb = await page.locator('#face').boundingBox();
check(fb.height <= 412 && fb.y >= 0, `landscape panel fits: ${Math.round(fb.width)}x${Math.round(fb.height)} in 915x412`);
await page.screenshot({ path: shots + '/xr10-landscape.png' });
await page.setViewportSize({ width: 412, height: 900 });
await page.click('[data-tab=voice]');
await page.waitForTimeout(300);
await page.screenshot({ path: shots + '/xr10-phone.png', fullPage: true });
check(errors.length === 0, 'no page errors ' + errors.join('; '));
await browser.close(); srv.close();
console.log(fail ? fail + ' FAILED' : 'all ok');
process.exit(fail ? 1 : 0);
