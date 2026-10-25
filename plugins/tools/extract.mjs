// Pulls the parameter tables (ranges, defaults, menus, presets) for each unit out of the web app,
// so the plugins and the app always agree. Usage: node extract.mjs ../../index.html > tables.json
import fs from 'fs'; import vm from 'vm';
const src = fs.readFileSync(process.argv[2], 'utf8');
const grab = (start, endRe) => { const i = src.indexOf(start); if (i < 0) throw new Error('missing ' + start); const rest = src.slice(i); const m = rest.match(endRe); return rest.slice(0, m ? m.index + m[0].length : undefined); };
const ctx = {Math, S:{bpm:120}, clamp:(v, a, b) => Math.max(a, Math.min(b, v)), console};
vm.createContext(ctx);
const run = code => vm.runInContext(code.replace(/^const /gm, 'var ').replace(/^function /gm, 'function '), ctx);
run(grab('const msF =', /\n\/\/ what each worklet receives/));
run(grab('const P5_INIT =', /\n};\n/));
run(grab('const P5_PRESETS =', /\n};\n/));
run(grab('const P5_LAYOUT =', /\n\];\n/));
const fmtKind = f => { if (f == null) return 'k10'; if (f === 'T') return 'time'; if (f === ctx.pctF) return 'pct'; if (f === ctx.msF) return 'ms'; if (f === ctx.hzF) return 'hz';
  const s = String(f); if (/\.02 \* Math\.pow\(400/.test(s)) return 'adaspeed'; if (/v < 0 \? '-'/.test(s)) return 'spct'; if (/Math\.round\(v \* 100\) \+ '%'/.test(s)) return 'pct';
  if (/' s'/.test(s) && /toFixed\(1\)/.test(s)) return 'sec'; if (/' dB'/.test(s) && /−/.test(s)) return 'negdb'; if (/' dB'/.test(s)) return /v \* 50 - 20/.test(s) ? 'u7in' : /v \* 40 - 30/.test(s) ? 'u7out' : 'db';
  if (/' Hz'/.test(s)) return /\.02 \* Math\.pow\(400/.test(s) ? 'adaspeed' : 'hzv'; if (/' ms'/.test(s)) return /Math.pow\(40/.test(s) ? 'adaman' : 'msv'; if (/:1'/.test(s)) return 'adarange'; if (/' c'/.test(s)) return 'cents';
  if (/' st'/.test(s)) return 'st'; if (/ct'/.test(s)) return 'ct'; if (/toFixed\(1\)/.test(s)) return 'n1'; return 'plain'; };
const out = {rack:{}, p5:{}};
for (const [k, U] of Object.entries(ctx.RACK)){
  const def = U.def(), knobs = [], seen = new Set();
  for (const [label, key, min, max, step, f] of U.knobs || []){ if (seen.has(key)) continue; seen.add(key);
    let lb = label; if (k === 'dp' && /^[1-4] /.test(label) && /^[xy]/.test(key)) lb = label[0] + (key[0] === 'x' ? ' Param 1' : ' Param 2');
    knobs.push({key, label:lb, min, max, step, def:def[key] ?? min, fmt:fmtKind(f)}); }
  const menus = (U.menus || []).map(([label, key, opts]) => ({key, label, opts:opts.map(([v, l]) => ({v, l})), def:def[key]}));
  out.rack[k] = {name:U.name, sub:U.sub, short:U.short, proc:U.proc, knobs, menus, presets:U.presets || {}};
}
const P5K = [], P5S = [];
for (const [title, knobs, sws] of ctx.P5_LAYOUT){ for (const [key, label, f] of knobs) P5K.push({key, label, sec:title, fmt:typeof f === 'function' ? fmtKind(f) : f === 'T' ? 'time' : 'k10'}); for (const [key, label] of sws) P5S.push({key, label, sec:title}); }
out.p5 = {init:ctx.P5_INIT, presets:ctx.P5_PRESETS, knobs:P5K, switches:P5S};
console.log(JSON.stringify(out, null, 1));
