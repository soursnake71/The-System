// Checks the engine's circuit solver against ngspice: frequency response of every circuit from its impulse response.
// usage: node twelve/tools/sp-circuits.mjs <dir with ngspice .dat files: aa, fixed3..6, dynout>
import {createRequire} from 'module';
import fs from 'fs';
const req = createRequire(import.meta.url), SP = req('../sp1200.js'), TW = req('../engine.js');
const dir = process.argv[2], fo = 192000, N = 1 << 17, freqs = [100, 1000, 3000, 6000, 10000, 13000, 16000, 20000, 30000];
const spice = name => fs.readFileSync(`${dir}/${name}.dat`, 'utf8').trim().split('\n').slice(1).map(l => l.trim().split(/\s+/).map(Number));
const interp = (rows, f) => { for (let i = 1; i < rows.length; i++) if (rows[i][0] >= f){ const [f0, m0] = rows[i - 1], [f1, m1] = rows[i]; return m0 + (m1 - m0) * Math.log(f / f0) / Math.log(f1 / f0); } return NaN; };
let worst = 0;
for (const [net, dat, ref] of [['aa', 'aa', 0], ['out3', 'fixed3', 0], ['out4', 'fixed4', 0], ['out5', 'fixed5', 0], ['out6', 'fixed6', 0], ['dynout', 'dynout', 1000]]){
  const c = new SP.Circuit(SP.NETS[net], fo), re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = c.process(i ? 0 : 1);
  TW.fft(re, im);
  const mag = f => { const k = Math.round(f * N / fo); return 20 * Math.log10(Math.hypot(re[k], im[k])); };
  const rows = spice(dat), r0 = ref ? mag(ref) - interp(rows, ref) : 0;
  const line = freqs.map(f => { const d = mag(f) - r0 - interp(rows, f); worst = Math.max(worst, f <= 20000 ? Math.abs(d) : 0); return `${(mag(f) - r0).toFixed(2).padStart(7)}/${interp(rows, f).toFixed(2).padStart(7)}`; });
  console.log(net.padEnd(7), line.join(' '));
}
console.log('freqs  ', freqs.map(f => String(f).padStart(15)).join(' '));
console.log(`engine/ngspice dB; worst difference up to 20 kHz: ${worst.toFixed(3)} dB`);
