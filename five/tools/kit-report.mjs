// Prints how a sample kit maps onto the RX5-style voices and waveform library.
// Usage: node five/tools/kit-report.mjs [file.bwpreset | file.zip | folder | factory]   (default: the factory kit)
import fs from 'node:fs';
import path from 'node:path';
import { loadKit, loadFactoryKit, formatKitReport } from '../kits.js';
import { VOICE_NAMES } from '../engine.js';

const arg = process.argv[2] || 'factory';
let kit;
if (arg === 'factory') kit = await loadFactoryKit();
else if (fs.statSync(arg).isDirectory()) kit = await loadKit(fs.readdirSync(arg).sort().map((f) => ({ name: f, bytes: fs.readFileSync(path.join(arg, f)) })));
else kit = await loadKit([{ name: path.basename(arg), bytes: fs.readFileSync(arg) }]);
console.log(`kit: ${kit.name || arg}  (${kit.waveforms.length} waveforms, ${kit.assignments.length} voices assigned, ${kit.pads.length} Bitwig pads decoded)`);
console.log(formatKitReport(kit, VOICE_NAMES));
