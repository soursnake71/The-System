// Checks the C++ engine (plugins/five/Five.h via five_test) against engine.js sample by sample.
// usage: node five/tools/compare.mjs path/to/five_test scratchDir
import {execFileSync} from 'node:child_process';
import {readFileSync, mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const here = path.dirname(fileURLToPath(import.meta.url));
const [bin, dir = '/tmp'] = process.argv.slice(2);
if (!bin){ console.error('usage: node five/tools/compare.mjs <five_test> [scratchDir]'); process.exit(2); }
mkdirSync(dir, {recursive:true});
const TOL = 1e-4;
let fail = 0;
for (const sc of ['voices', 'kit']){
  const js = path.join(dir, `five-${sc}-js.f32`), cc = path.join(dir, `five-${sc}-cpp.f32`);
  execFileSync(process.execPath, [path.join(here, 'render.mjs'), js, sc], {stdio:'inherit'});
  execFileSync(bin, [cc, sc], {stdio:'inherit'});
  const rd = f => { const b = readFileSync(f); return new Float32Array(b.buffer, b.byteOffset, b.length >> 2); };
  const a = rd(js), b = rd(cc);
  let max = 0, at = -1, first = -1, pk = 0;
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++){
    const d = Math.abs(a[i] - b[i]);
    if (!(d <= max)){ max = d; at = i; }
    if (first < 0 && !(d <= TOL)) first = i;
    pk = Math.max(pk, Math.abs(a[i]));
  }
  const ok = a.length === b.length && max <= TOL;
  if (!ok) fail++;
  const fr = i => `${i} (frame ${i >> 1}, ${i & 1 ? 'R' : 'L'})`;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${sc}: ${a.length / 2}/${b.length / 2} frames, max abs diff ${max.toExponential(3)}${at >= 0 && max > 0 ? ' at ' + fr(at) : ''}, peak ${pk.toFixed(4)}` +
    (first >= 0 ? `, first mismatch > ${TOL} at ${fr(first)}: js ${a[first]} c++ ${b[first]}` : ''));
}
process.exitCode = fail ? 1 : 0;
