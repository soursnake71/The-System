// node nam/tools/check.mjs <file.nam> ...  — prints what the finder reads from each file
import { readFileSync } from 'node:fs';
import { analyzeNam, variantUnits, REFERENCE, DEVICES, cpuShare } from '../namfile.js';
const pct = x => (100 * x).toFixed(1) + '%';
console.log('Reference shapes (MACs, layers, units/sample, % of 600 MHz M7, % of Pi 4):');
const m7 = DEVICES.find(d => d.id === 'm7'), pi4 = DEVICES.find(d => d.id === 'pi4');
for (const r of REFERENCE) console.log(' ', r.label.padEnd(12), Math.round(r.shape.macs), r.shape.layers, Math.round(variantUnits(r)), pct(cpuShare(r, m7.capacity, { buffer: 128 })), pct(cpuShare(r, pi4.capacity, { buffer: 128 })));
for (const f of process.argv.slice(2)) {
  try {
    const a = analyzeNam(readFileSync(f, 'utf8'));
    console.log('\n' + f.split('/').pop(), '->', a.label, 'v' + a.version, JSON.stringify(a.meta));
    for (const v of a.variants) console.log('   ', v.label, 'params', v.params, 'macs', Math.round(v.shape.macs), 'rf', v.shape.rf, 'units', Math.round(variantUnits(v)), 'M7', pct(cpuShare(v, m7.capacity, { buffer: 128 })));
  } catch (e) { console.log('\n' + f.split('/').pop(), 'ERROR', e.message); }
}
