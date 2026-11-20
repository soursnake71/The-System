// Omega 808: CPU cost of 1 s of audio with a busy pattern, REV7 and 2290 running (ms per second of audio, lower is better).
// Usage: node eight/tools/bench.mjs
import {createRequire} from 'module';
const require = createRequire(import.meta.url);
const dir = new URL('..', import.meta.url).pathname;
Object.assign(globalThis, require(dir + 'fx.js'));
const {Eight808} = require(dir + 'engine.js');
const SR = 48000, e = new Eight808(SR);
e.set('rev.send.SD', 0.3); e.set('rev.send.CP', 0.3); e.set('dly.send.CP', 0.3); e.set('dly.send.RS', 0.3); e.set('mod.TRIG.level', 0.5);
const L = new Float32Array(128), R = new Float32Array(128);
const pat = ['BD,CH', 'CH', 'CH,RS', 'CH', 'SD,CH,CP', 'CH', 'BD,CH', 'OH', 'CH,LT', 'BD,CH', 'CH,CB', 'CH', 'SD,CH,CP', 'CH,MT', 'BD,CH,HT', 'OH,CY'];
function run(seconds) {
  let n = 0, st = 0; const per = SR * 60 / (120 * 4);
  for (let b = 0; b < seconds * SR / 128; b++) {
    if (n >= per) { n -= per; for (const v of pat[st++ & 15].split(',')) e.trigger(v, st % 4 === 1 ? 1 : 0, 1); }
    e.process(L, R, 128, 0); n += 128;
  }
}
run(2);   // warm up the JIT
const t = performance.now(); run(10);
console.log(`busy pattern + REV7 + 2290: ${((performance.now() - t) / 10).toFixed(1)} ms per second of audio`);
